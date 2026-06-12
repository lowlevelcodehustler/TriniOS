/*
 * TriniOS ext4 Filesystem Driver - Mount / Superblock
 *
 * Reads and validates the superblock and group descriptor table.
 * Initializes the in-memory filesystem state.
 */

#include "ext4.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Helper: Compute block size from superblock
 * ============================================================================ */

static uint32_t ext4_compute_block_size(ext4_superblock_t *sb) {
    uint32_t log_block = sb->s_log_block_size;

    /* For rev 0 filesystems, log is actually block size directly */
    if (sb->s_rev_level == 0) {
        return 1024 << log_block;
    }

    if (log_block < EXT4_MIN_BLOCK_LOG_SIZE || log_block > EXT4_MAX_BLOCK_LOG_SIZE) {
        return EXT4_MIN_BLOCK_SIZE; /* fallback */
    }

    return 1024 << log_block;
}

/* ============================================================================
 * Helper: Compute number of block groups
 * ============================================================================ */

static uint32_t ext4_compute_groups_count(uint32_t blocks_count, uint32_t blocks_per_group) {
    if (blocks_per_group == 0) return 0;
    return (blocks_count + blocks_per_group - 1) / blocks_per_group;
}

/* ============================================================================
 * Helper: Get block group descriptor table location
 * ============================================================================ */

static uint64_t ext4_gdt_block(ext4_superblock_t *sb, uint32_t block_size) {
    /* GDT starts right after the superblock.
     * Superblock is at block 1 (offset 1024).
     * With block_size >= 1024, GDT is at block (1 + 1024/block_size). */
    uint64_t sb_block = sb->s_first_data_block ? 0 : 1;

    /* The superblock occupies block 1. GDT follows immediately. */
    uint64_t gdt_start = sb_block + 1; /* block after superblock */

    /* But if block_size > 1024, superblock + GDT fit in block 1 */
    /* Actually: GDT starts at offset 1024 + 1024 = 2048 bytes = block 2 for 1K,
     * or still block 1 for larger block sizes. */
    /* Standard: GDT starts right after superblock at 1024-byte offset + 1024 */
    return 1 + (1024 / block_size);
}

/* ============================================================================
 * Mount: Read superblock + group descriptors
 * ============================================================================ */

int ext4_mount(ext4_fs_t *fs, ext4_block_device_t *bdev) {
    if (!fs || !bdev) {
        return EXT4_ERR_INVAL;
    }

    /* --- Step 1: Read superblock (always at offset 1024 = block 1 for 512B sectors) --- */
    uint8_t sb_buf[EXT4_PRIV_DATA_BLOCK_SIZE];
    int ret = ext4_bdev_read(bdev, 1, 1, sb_buf); /* block 1 in 512B sectors */
    if (ret != EXT4_OK) {
        return EXT4_ERR_IO;
    }

    memcpy(&fs->sb, sb_buf, sizeof(ext4_superblock_t));

    /* Validate magic */
    if (fs->sb.s_magic != EXT4_SUPERBLOCK_MAGIC) {
        return EXT4_ERR_INVAL; /* Not an ext4 filesystem */
    }

    /* --- Step 2: Compute block size --- */
    fs->block_size = ext4_compute_block_size(&fs->sb);
    fs->block_size_log2 = 0;
    {
        uint32_t bs = EXT4_MIN_BLOCK_SIZE;
        while (bs < fs->block_size) {
            fs->block_size_log2++;
            bs <<= 1;
        }
    }

    /* --- Step 3: Compute filesystem geometry --- */
    fs->blocks_count = fs->sb.s_blocks_count_lo;
    /* For 64-bit feature, combine hi/lo */
    if (fs->sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) {
        fs->is_64bit = true;
        fs->blocks_count |= (uint64_t)fs->sb.s_blocks_count_hi << 32;
    }

    fs->blocks_count = fs->sb.s_blocks_count_lo;
    fs->groups_count = ext4_compute_groups_count(
        fs->sb.s_blocks_count_lo, fs->sb.s_blocks_per_group);

    fs->inode_size = fs->sb.s_inode_size;
    if (fs->inode_size == 0) {
        fs->inode_size = EXT4_DEFAULT_INODE_SIZE;
    }

    /* --- Step 4: Detect features --- */
    fs->has_journal = !!(fs->sb.s_feature_compat & EXT4_FEATURE_COMPAT_HAS_JOURNAL);
    fs->has_extents = !!(fs->sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_EXTENT);

    fs->journal_ino = fs->sb.s_journal_inum;
    fs->journal_dev = fs->sb.s_journal_dev;

    /* --- Step 5: Read group descriptor table --- */
    uint32_t desc_size = fs->sb.s_desc_size;
    if (desc_size == 0) desc_size = 32; /* Default for old filesystems */

    uint32_t gdt_blocks = (fs->groups_count * desc_size + fs->block_size - 1) / fs->block_size;

    /* GDT starts right after the superblock */
    uint64_t gdt_start_block;
    if (fs->block_size >= 1024) {
        gdt_start_block = 1 + (1024 / fs->block_size);
    } else {
        gdt_start_block = 2;
    }

    /* Read all GDT blocks */
    uint32_t gdt_total_bytes = gdt_blocks * fs->block_size;
    uint8_t *gdt_buf = (uint8_t *)malloc(gdt_total_bytes);
    if (!gdt_buf) {
        return EXT4_ERR_IO;
    }

    ret = ext4_bdev_read(bdev, gdt_start_block, gdt_blocks, gdt_buf);
    if (ret != EXT4_OK) {
        free(gdt_buf);
        return EXT4_ERR_IO;
    }

    /* Allocate and copy group descriptors */
    fs->group_desc = (ext4_group_desc_t *)malloc(fs->groups_count * sizeof(ext4_group_desc_t));
    if (!fs->group_desc) {
        free(gdt_buf);
        return EXT4_ERR_IO;
    }

    for (uint32_t i = 0; i < fs->groups_count; i++) {
        uint32_t offset = i * desc_size;
        memcpy(&fs->group_desc[i], gdt_buf + offset, sizeof(ext4_group_desc_t));
    }

    free(gdt_buf);

    /* Store block device */
    fs->bdev = bdev;

    /* Initialize cache */
    fs->block_cache = NULL;
    fs->block_cache_addr = 0;
    fs->block_cache_count = 0;

    return EXT4_OK;
}

/* ============================================================================
 * Unmount: Clean up resources
 * ============================================================================ */

void ext4_unmount(ext4_fs_t *fs) {
    if (!fs) return;

    if (fs->group_desc) {
        free(fs->group_desc);
        fs->group_desc = NULL;
    }

    if (fs->block_cache) {
        free(fs->block_cache);
        fs->block_cache = NULL;
    }

    fs->bdev = NULL;
    memset(&fs->sb, 0, sizeof(ext4_superblock_t));
}

/* ============================================================================
 * Debug: Dump superblock information
 * ============================================================================ */

void ext4_dump_superblock(ext4_fs_t *fs) {
    if (!fs) return;

    printf("[ext4] Superblock Dump\n");
    printf("  Magic:           0x%04X\n", fs->sb.s_magic);
    printf("  Block size:      %u\n", fs->block_size);
    printf("  Blocks count:    %u\n", fs->blocks_count);
    printf("  Blocks per grp:  %u\n", fs->sb.s_blocks_per_group);
    printf("  Inodes per grp:  %u\n", fs->sb.s_inodes_per_group);
    printf("  Inode size:      %u\n", fs->inode_size);
    printf("  Groups count:    %u\n", fs->groups_count);
    printf("  Feature compat:  0x%08X\n", fs->sb.s_feature_compat);
    printf("  Feature incompat: 0x%08X\n", fs->sb.s_feature_incompat);
    printf("  Feature ro:      0x%08X\n", fs->sb.s_feature_ro_compat);
    printf("  Has journal:     %s\n", fs->has_journal ? "yes" : "no");
    printf("  Has extents:     %s\n", fs->has_extents ? "yes" : "no");
    printf("  64-bit:          %s\n", fs->is_64bit ? "yes" : "no");
    printf("  Volume name:     %s\n", fs->sb.s_volume_name);
    printf("  State:           0x%04X %s\n",
           fs->sb.s_state,
           fs->sb.s_state & EXT4_VALID_FS ? "(clean)" : "(dirty)");
}