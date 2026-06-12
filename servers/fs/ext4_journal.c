/*
 * TriniOS ext4 Filesystem Driver - Journal Awareness
 *
 * Detects journal presence, checks journal state, and identifies
 * whether filesystem recovery is needed.
 *
 * Note: Phase 1 implements read-only journal awareness. We detect
 * and report journal state but do not perform recovery (Phase 2+).
 */

#include "ext4.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ============================================================================
 * Journal Check: Verify journal superblock
 * ============================================================================ */

int ext4_journal_check(ext4_fs_t *fs) {
    if (!fs) {
        return EXT4_ERR_INVAL;
    }

    /* If no journal feature flag, nothing to check */
    if (!fs->has_journal) {
        return EXT4_OK;
    }

    /* If external journal device, we can't check it without the device */
    if (fs->journal_dev != 0) {
        printf("[ext4] External journal detected (dev %u) - skipping check\n",
               fs->journal_dev);
        return EXT4_OK;
    }

    /* Read the journal inode */
    ext4_inode_t jinode;
    int ret = ext4_iget(fs, fs->journal_ino, &jinode);
    if (ret != EXT4_OK) {
        printf("[ext4] Warning: Cannot read journal inode %u\n", fs->journal_ino);
        return ret;
    }

    /* Verify it's a regular file */
    if (!EXT4_S_ISREG(jinode.i_mode)) {
        printf("[ext4] Warning: Journal inode is not a regular file (mode 0%o)\n",
               jinode.i_mode);
        return EXT4_ERR_INVAL;
    }

    /* Get journal size in blocks */
    uint64_t jsize = ((uint64_t)jinode.i_size_hi << 32) | jinode.i_size_lo;
    uint32_t jblocks = (uint32_t)(jsize / fs->block_size);

    if (jblocks == 0) {
        printf("[ext4] Warning: Journal size is zero\n");
        return EXT4_ERR_INVAL;
    }

    /* Read the journal superblock (first block of journal) */
    uint64_t pblock;
    ret = ext4_inode_data_block(fs, &jinode, 0, &pblock);
    if (ret != EXT4_OK) {
        printf("[ext4] Warning: Cannot map journal block 0\n");
        return ret;
    }

    ext4_journal_superblock_t jsb;
    ret = ext4_read_block(fs, pblock, &jsb);
    if (ret != EXT4_OK) {
        printf("[ext4] Warning: Cannot read journal superblock\n");
        return ret;
    }

    /* Verify journal magic */
    if (jsb.s_magic != 0xC03B4985) {
        printf("[ext4] Warning: Invalid journal magic 0x%08X\n", jsb.s_magic);
        return EXT4_ERR_INVAL;
    }

    printf("[ext4] Journal OK: %u blocks, sequence %u, magic 0x%08X\n",
           jsb.s_maxlen, jsb.s_sequence, jsb.s_magic);

    return EXT4_OK;
}

/* ============================================================================
 * Needs Recovery: Check if RECOVER flag is set
 * ============================================================================ */

int ext4_journal_needs_recovery(ext4_fs_t *fs, bool *needs_recovery) {
    if (!fs || !needs_recovery) {
        return EXT4_ERR_INVAL;
    }

    *needs_recovery = false;

    /* Check filesystem state for RECOVER flag */
    if (fs->sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_RECOVER) {
        *needs_recovery = true;
        printf("[ext4] Filesystem needs journal recovery\n");
        return EXT4_OK;
    }

    /* Check if filesystem is clean */
    if (fs->sb.s_state & EXT4_VALID_FS) {
        printf("[ext4] Filesystem is clean, no recovery needed\n");
        *needs_recovery = false;
        return EXT4_OK;
    }

    /* Filesystem was not cleanly unmounted */
    if (!(fs->sb.s_state & EXT4_VALID_FS)) {
        printf("[ext4] Warning: Filesystem was not cleanly unmounted\n");
        if (fs->has_journal) {
            *needs_recovery = true;
            printf("[ext4] Journal recovery recommended\n");
        } else {
            printf("[ext4] No journal present, run fsck before mounting writable\n");
        }
    }

    return EXT4_OK;
}

/* ============================================================================
 * Journal Block Type Name (debug helper)
 * ============================================================================ */

const char *ext4_journal_block_type_name(uint8_t blocktype) {
    switch (blocktype) {
    case EXT4_JBD2_MAGIC_BARRIER:        return "BARRIER";
    case EXT4_JBD2_MAGIC_DATA:           return "DATA";
    case EXT4_JBD2_MAGIC_DESCRIPTOR:     return "DESCRIPTOR";
    case EXT4_JBD2_MAGIC_REVOKE:         return "REVOKE";
    case EXT4_JBD2_MAGIC_REVOKE_RECORD:  return "REVOKE_RECORD";
    case EXT4_JBD2_MAGIC_COMMIT:         return "COMMIT";
    case EXT4_JBD2_MAGIC_ABORT:          return "ABORT";
    case EXT4_JBD2_MAGIC_REVOKE_HEADER:  return "REVOKE_HEADER";
    case EXT4_JBD2_MAGIC_CHECKPOINT_DELETE: return "CHECKPOINT_DELETE";
    case EXT4_JBD2_MAGIC_CHECKPOINT_BLOCK:  return "CHECKPOINT_BLOCK";
    case EXT4_JBD2_MAGIC_UUID:           return "UUID";
    case EXT4_JBD2_MAGIC_FREE:           return "FREE";
    case EXT4_JBD2_MAGIC_MEMORY_BARRIER: return "MEMORY_BARRIER";
    default:                             return "UNKNOWN";
    }
}

/* ============================================================================
 * Light Filesystem Check
 *
 * Performs basic consistency checks without modifying the filesystem.
 * Returns 0 if OK, negative error code if problems found.
 * ============================================================================ */

int ext4_fsck_light(ext4_fs_t *fs) {
    if (!fs) {
        return EXT4_ERR_INVAL;
    }

    int errors = 0;

    /* Check 1: Superblock magic */
    if (fs->sb.s_magic != EXT4_SUPERBLOCK_MAGIC) {
        printf("[fsck] ERROR: Invalid superblock magic 0x%04X\n",
               fs->sb.s_magic);
        errors++;
    }

    /* Check 2: Block size validity */
    if (fs->block_size < EXT4_MIN_BLOCK_SIZE ||
        fs->block_size > EXT4_MAX_BLOCK_SIZE) {
        printf("[fsck] ERROR: Invalid block size %u\n", fs->block_size);
        errors++;
    }

    /* Check 3: Groups count consistency */
    uint32_t expected_groups = ext4_compute_groups_count_safe(
        fs->sb.s_blocks_count_lo, fs->sb.s_blocks_per_group);
    if (fs->groups_count != expected_groups) {
        printf("[fsck] ERROR: Groups count mismatch: %u vs expected %u\n",
               fs->groups_count, expected_groups);
        errors++;
    }

    /* Check 4: Root inode validity */
    ext4_inode_t root_inode;
    if (ext4_iget(fs, 2, &root_inode) != EXT4_OK) {
        printf("[fsck] ERROR: Cannot read root inode\n");
        errors++;
    } else if (!EXT4_S_ISDIR(root_inode.i_mode)) {
        printf("[fsck] ERROR: Root inode is not a directory\n");
        errors++;
    }

    /* Check 5: Journal consistency (if present) */
    if (fs->has_journal) {
        if (ext4_journal_check(fs) != EXT4_OK) {
            printf("[fsck] WARNING: Journal check failed\n");
        }
    }

    /* Check 6: Filesystem state */
    if (fs->sb.s_state & EXT4_ERROR_FS) {
        printf("[fsck] WARNING: Filesystem error flag set\n");
    }
    if (fs->sb.s_state & EXT4_ORPHAN_FS) {
        printf("[fsck] WARNING: Orphan list present\n");
    }

    if (errors == 0) {
        printf("[fsck] Light check passed\n");
        return EXT4_OK;
    } else {
        printf("[fsck] %d error(s) found\n", errors);
        return EXT4_ERR_IO;
    }
}

/* Helper used in fsck */
static uint32_t ext4_compute_groups_count_safe(uint32_t blocks, uint32_t bpg) {
    if (bpg == 0) return 0;
    return (blocks + bpg - 1) / bpg;
}