/*
 * TriniOS ext4 Filesystem Driver - Inode Operations
 *
 * Reads inodes from disk, maps logical blocks to physical blocks.
 * Supports both traditional indirect block mapping and extent trees.
 */

#include "ext4.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Helper: Get block group index for an inode
 * ============================================================================ */

static uint32_t ext4_inode_to_group(ext4_fs_t *fs, uint32_t ino) {
    if (ino == 0) return 0;
    return (ino - 1) / fs->sb.s_inodes_per_group;
}

/* ============================================================================
 * Helper: Get inode offset within the inode table of its group
 * ============================================================================ */

static uint32_t ext4_inode_offset(ext4_fs_t *fs, uint32_t ino) {
    if (ino == 0) return 0;
    return (ino - 1) % fs->sb.s_inodes_per_group;
}

/* ============================================================================
 * Helper: Compute the block address of an inode
 * ============================================================================ */

static uint64_t ext4_inode_table_block(ext4_fs_t *fs, uint32_t ino) {
    uint32_t group = ext4_inode_to_group(fs, ino);
    if (group >= fs->groups_count) {
        return 0;
    }

    ext4_group_desc_t *gd = &fs->group_desc[group];
    uint64_t itable = gd->bg_inode_table_lo;

    /* For 64-bit filesystems, combine hi/lo */
    if (fs->is_64bit) {
        itable |= (uint64_t)gd->bg_inode_table_hi << 32;
    }

    uint32_t offset = ext4_inode_offset(fs, ino);
    /* Compute which block contains this inode */
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = offset / inodes_per_block;

    return itable + block_offset;
}

/* ============================================================================
 * iget: Read an inode from disk
 * ============================================================================ */

int ext4_iget(ext4_fs_t *fs, uint32_t ino, ext4_inode_t *inode) {
    if (!fs || !inode || ino == 0) {
        return EXT4_ERR_INVAL;
    }

    /* Validate inode number range */
    uint32_t group = ext4_inode_to_group(fs, ino);
    if (group >= fs->groups_count) {
        return EXT4_ERR_INVAL;
    }

    /* Read the block containing this inode */
    uint64_t block = ext4_inode_table_block(fs, ino);
    uint8_t *buf;
    int ret = ext4_cache_get_block(fs, block, &buf);
    if (ret != EXT4_OK) {
        return EXT4_ERR_IO;
    }

    /* Find the inode within the block */
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t offset_in_block = (ext4_inode_offset(fs, ino) % inodes_per_block) * fs->inode_size;

    /* Copy inode data - be careful with variable inode sizes */
    memset(inode, 0, sizeof(ext4_inode_t));
    uint32_t copy_size = fs->inode_size;
    if (copy_size > sizeof(ext4_inode_t)) {
        copy_size = sizeof(ext4_inode_t);
    }
    memcpy(inode, buf + offset_in_block, copy_size);

    return EXT4_OK;
}

/* ============================================================================
 * Helper: Get file size (combining hi/lo for large files)
 * ============================================================================ */

static uint64_t ext4_inode_size(ext4_inode_t *inode) {
    uint64_t size = inode->i_size_lo;
    size |= (uint64_t)inode->i_size_hi << 32;
    return size;
}

/* ============================================================================
 * Traditional Block Mapping (non-extent)
 * ============================================================================ */

static int ext4_map_block_traditional(ext4_fs_t *fs, ext4_inode_t *inode,
                                      uint64_t lblock, uint64_t *pblock) {
    uint32_t block_size = fs->block_size;
    uint32_t indirects_per_block = block_size / sizeof(uint32_t);

    /* Direct blocks: i_block[0..11] */
    if (lblock < 12) {
        *pblock = inode->i_block[lblock];
        return *pblock ? EXT4_OK : EXT4_ERR_INVAL;
    }

    lblock -= 12;

    /* Single indirect: i_block[12] */
    if (lblock < indirects_per_block) {
        uint32_t indirect_block = inode->i_block[12];
        if (!indirect_block) return EXT4_ERR_INVAL;

        uint8_t *buf;
        int ret = ext4_cache_get_block(fs, indirect_block, &buf);
        if (ret != EXT4_OK) return EXT4_ERR_IO;

        *pblock = ((uint32_t *)buf)[lblock];
        return *pblock ? EXT4_OK : EXT4_ERR_INVAL;
    }

    lblock -= indirects_per_block;

    /* Double indirect: i_block[13] */
    uint64_t double_indirect_limit = (uint64_t)indirects_per_block * indirects_per_block;
    if (lblock < double_indirect_limit) {
        uint32_t double_indirect_block = inode->i_block[13];
        if (!double_indirect_block) return EXT4_ERR_INVAL;

        uint32_t first_level = lblock / indirects_per_block;
        uint32_t second_level = lblock % indirects_per_block;

        uint8_t *buf;
        int ret = ext4_cache_get_block(fs, double_indirect_block, &buf);
        if (ret != EXT4_OK) return EXT4_ERR_IO;

        uint32_t indirect_block = ((uint32_t *)buf)[first_level];
        if (!indirect_block) return EXT4_ERR_INVAL;

        ret = ext4_cache_get_block(fs, indirect_block, &buf);
        if (ret != EXT4_OK) return EXT4_ERR_IO;

        *pblock = ((uint32_t *)buf)[second_level];
        return *pblock ? EXT4_OK : EXT4_ERR_INVAL;
    }

    lblock -= double_indirect_limit;

    /* Triple indirect: i_block[14] */
    uint64_t triple_limit = (uint64_t)indirects_per_block * indirects_per_block * indirects_per_block;
    if (lblock < triple_limit) {
        uint32_t triple_block = inode->i_block[14];
        if (!triple_block) return EXT4_ERR_INVAL;

        uint32_t l1 = lblock / ((uint64_t)indirects_per_block * indirects_per_block);
        uint64_t remainder = lblock % ((uint64_t)indirects_per_block * indirects_per_block);
        uint32_t l2 = remainder / indirects_per_block;
        uint32_t l3 = remainder % indirects_per_block;

        uint8_t *buf;
        int ret = ext4_cache_get_block(fs, triple_block, &buf);
        if (ret != EXT4_OK) return EXT4_ERR_IO;

        uint32_t b2 = ((uint32_t *)buf)[l1];
        if (!b2) return EXT4_ERR_INVAL;

        ret = ext4_cache_get_block(fs, b2, &buf);
        if (ret != EXT4_OK) return EXT4_ERR_IO;

        uint32_t b3 = ((uint32_t *)buf)[l2];
        if (!b3) return EXT4_ERR_INVAL;

        ret = ext4_cache_get_block(fs, b3, &buf);
        if (ret != EXT4_OK) return EXT4_ERR_IO;

        *pblock = ((uint32_t *)buf)[l3];
        return *pblock ? EXT4_OK : EXT4_ERR_INVAL;
    }

    return EXT4_ERR_FBIG; /* Beyond addressable range */
}

/* ============================================================================
 * Extent Tree Mapping
 * ============================================================================ */

static int ext4_extent_map_block(ext4_fs_t *fs, uint32_t ino,
                                 uint64_t lblock, uint64_t *pblock,
                                 uint32_t depth, void *path) {
    /* Read the block pointed to by the extent path */
    uint8_t *buf;
    int ret = ext4_cache_get_block(fs, *(uint32_t *)path, &buf);
    if (ret != EXT4_OK) return EXT4_ERR_IO;

    /* Verify extent header magic */
    ext4_extent_header_t *eh = (ext4_extent_header_t *)buf;
    if (eh->eh_magic != EXT4_EXTENT_MAGIC) {
        return EXT4_ERR_IO;
    }

    if (depth == 0) {
        /* Leaf level: search extent entries */
        ext4_extent_t *ext = (ext4_extent_t *)(buf + sizeof(ext4_extent_header_t));
        for (uint16_t i = 0; i < eh->eh_max; i++) {
            if (lblock >= ext->ee_block &&
                lblock < ext->ee_block + ext->ee_len) {
                uint64_t pblock_offset = lblock - ext->ee_block;
                *pblock = ((uint64_t)ext->ee_start_hi << 32) |
                          ext->ee_start_lo + pblock_offset;
                return EXT4_OK;
            }
            ext++;
        }
        return EXT4_ERR_INVAL; /* No extent covers this block */
    }

    /* Index level: find the right child */
    ext4_extent_index_t *idx = (ext4_extent_index_t *)(buf + sizeof(ext4_extent_header_t));
    for (uint16_t i = 0; i < eh->eh_max; i++) {
        if (lblock <= idx->ei_block) {
            *(uint32_t *)path = idx->ei_start;
            return ext4_extent_map_block(fs, ino, lblock, pblock,
                                        depth - 1, path);
        }
        idx++;
    }

    /* Use the last index entry if lblock is beyond all entries */
    *(uint32_t *)path = idx->ei_start;
    return ext4_extent_map_block(fs, ino, lblock, pblock, depth - 1, path);
}

static int ext4_map_block_extent(ext4_fs_t *fs, ext4_inode_t *inode,
                                 uint64_t lblock, uint64_t *pblock) {
    /* The extent root is stored in i_block[0] for extent inodes */
    uint32_t root = inode->i_block[0];
    if (!root) return EXT4_ERR_INVAL;

    /* Read extent header from the root block */
    uint8_t *buf;
    int ret = ext4_cache_get_block(fs, root, &buf);
    if (ret != EXT4_OK) return EXT4_ERR_IO;

    ext4_extent_header_t *eh = (ext4_extent_header_t *)buf;
    if (eh->eh_magic != EXT4_EXTENT_MAGIC) {
        return EXT4_ERR_IO;
    }

    if (eh->eh_depth == 0) {
        /* Direct leaf in inode */
        ext4_extent_t *ext = (ext4_extent_t *)(buf + sizeof(ext4_extent_header_t));
        for (uint16_t i = 0; i < eh->eh_max; i++) {
            if (lblock >= ext->ee_block &&
                lblock < ext->ee_block + ext->ee_len) {
                uint64_t pblock_offset = lblock - ext->ee_block;
                *pblock = ((uint64_t)ext->ee_start_hi << 32) |
                          ext->ee_start_lo + pblock_offset;
                return EXT4_OK;
            }
            ext++;
        }
        return EXT4_ERR_INVAL;
    }

    /* Tree traversal */
    uint32_t path = root;
    return ext4_extent_map_block(fs, 0, lblock, pblock,
                                 eh->eh_depth, &path);
}

/* ============================================================================
 * Public: Map logical block to physical block
 * ============================================================================ */

int ext4_inode_data_block(ext4_fs_t *fs, ext4_inode_t *inode,
                          uint64_t lblock, uint64_t *pblock) {
    if (!fs || !inode || !pblock) {
        return EXT4_ERR_INVAL;
    }

    /* Check if this inode uses extents */
    if (inode->i_flags & EXT4_EXTENTS_FL) {
        return ext4_map_block_extent(fs, inode, lblock, pblock);
    }

    /* Traditional block mapping */
    return ext4_map_block_traditional(fs, inode, lblock, pblock);
}

/* ============================================================================
 * Debug: Dump inode information
 * ============================================================================ */

void ext4_dump_inode(ext4_fs_t *fs, ext4_inode_t *inode) {
    if (!fs || !inode) return;

    uint64_t size = ext4_inode_size(inode);
    printf("[ext4] Inode Dump\n");
    printf("  Mode:       0%07o\n", inode->i_mode);
    printf("  UID/GID:    %u/%u\n", inode->i_uid_lo, inode->i_gid_lo);
    printf("  Size:       %llu bytes\n", (unsigned long long)size);
    printf("  Blocks:     %u (512B)\n", inode->i_blocks_lo);
    printf("  Links:      %u\n", inode->i_links_count);
    printf("  Flags:      0x%08X\n", inode->i_flags);
    printf("  Generation: %u\n", inode->i_generation);
    printf("  Atime:      %u\n", inode->i_atime);
    printf("  Mtime:      %u\n", inode->i_mtime);
    printf("  Ctime:      %u\n", inode->i_ctime);

    if (EXT4_S_ISREG(inode->i_mode)) {
        printf("  Type:       Regular file\n");
    } else if (EXT4_S_ISDIR(inode->i_mode)) {
        printf("  Type:       Directory\n");
    } else if (EXT4_S_ISLNK(inode->i_mode)) {
        printf("  Type:       Symlink\n");
    } else {
        printf("  Type:       Other\n");
    }

    if (inode->i_flags & EXT4_EXTENTS_FL) {
        printf("  Mapping:    Extent tree\n");
    } else {
        printf("  Mapping:    Traditional indirect blocks\n");
    }
}