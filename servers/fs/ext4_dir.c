/*
 * TriniOS ext4 Filesystem Driver - Directory Operations
 *
 * opendir, readdir, closedir, mkdir, rmdir.
 */

#include "ext4.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Open Directory
 * ============================================================================ */

int ext4_opendir(ext4_fs_t *fs, const char *path, ext4_dirhandle_t *dh) {
    if (!fs || !path || !dh) {
        return EXT4_ERR_INVAL;
    }

    /* Lookup path to get directory inode */
    uint32_t ino;
    int ret = ext4_path_lookup(fs, path, &ino, NULL);
    if (ret != EXT4_OK) return ret;

    /* Verify it's a directory */
    ext4_inode_t inode;
    ret = ext4_iget(fs, ino, &inode);
    if (ret != EXT4_OK) return ret;

    if (!EXT4_S_ISDIR(inode.i_mode)) {
        return EXT4_ERR_NOTDIR;
    }

    /* Initialize directory handle */
    dh->dir_ino = ino;
    dh->block_pos = 0;
    dh->offset_in_block = 0;
    dh->block_buf = (uint8_t *)malloc(fs->block_size);
    if (!dh->block_buf) {
        return EXT4_ERR_IO;
    }

    /* Pre-load the first block */
    uint64_t pblock;
    ret = ext4_inode_data_block(fs, &inode, 0, &pblock);
    if (ret != EXT4_OK) {
        free(dh->block_buf);
        dh->block_buf = NULL;
        return ret;
    }

    ret = ext4_read_block(fs, pblock, dh->block_buf);
    if (ret != EXT4_OK) {
        free(dh->block_buf);
        dh->block_buf = NULL;
        return ret;
    }

    dh->block_pos = 1; /* Next block to load */
    return EXT4_OK;
}

/* ============================================================================
 * Read Directory Entry
 * ============================================================================ */

int ext4_readdir(ext4_fs_t *fs, ext4_dirhandle_t *dh, ext4_dirent_t *entry) {
    if (!fs || !dh || !entry || !dh->block_buf) {
        return EXT4_ERR_INVAL;
    }

    if (dh->dir_ino == 0) {
        return EXT4_ERR_BADF;
    }

    ext4_inode_t inode;
    int ret = ext4_iget(fs, dh->dir_ino, &inode);
    if (ret != EXT4_OK) return ret;

    uint64_t file_size = ((uint64_t)inode.i_size_hi << 32) | inode.i_size_lo;
    uint64_t total_blocks = (file_size + fs->block_size - 1) / fs->block_size;

    while (dh->offset_in_block < fs->block_size) {
        /* Ensure we can read the directory entry header */
        if (dh->offset_in_block + 8 > fs->block_size) break;

        ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)
            (dh->block_buf + dh->offset_in_block);

        uint16_t rec_len = de->rec_len;

        /* Align rec_len to 4 bytes */
        if (rec_len & 3) rec_len += 4 - (rec_len & 3);

        /* Validate entry */
        if (rec_len == 0 || rec_len > fs->block_size) {
            /* Corrupted entry, try next block */
            dh->offset_in_block = fs->block_size;
            continue;
        }
        if (de->name_len == 0 || de->name_len > rec_len - 8) {
            dh->offset_in_block = fs->block_size;
            continue;
        }

        /* Skip "." and ".." */
        if (de->name_len == 1 && de->name[0] == '.') {
            dh->offset_in_block += rec_len;
            continue;
        }
        if (de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.') {
            dh->offset_in_block += rec_len;
            continue;
        }

        /* Copy entry to output */
        entry->ino = de->inode;
        entry->file_type = de->file_type;
        uint32_t copy_len = de->name_len;
        if (copy_len >= sizeof(entry->name)) {
            copy_len = sizeof(entry->name) - 1;
        }
        memcpy(entry->name, de->name, copy_len);
        entry->name[copy_len] = '\0';

        /* Advance offset for next call */
        dh->offset_in_block += rec_len;

        /* If we've reached the end of this block, prepare to load the next */
        if (dh->offset_in_block >= fs->block_size) {
            dh->offset_in_block = 0;
            if (dh->block_pos < total_blocks) {
                uint64_t pblock;
                ret = ext4_inode_data_block(fs, &inode, dh->block_pos, &pblock);
                if (ret == EXT4_OK) {
                    ret = ext4_read_block(fs, pblock, dh->block_buf);
                    if (ret == EXT4_OK) {
                        dh->block_pos++;
                    } else {
                        dh->dir_ino = 0; /* Mark as exhausted */
                        return EXT4_ERR_IO;
                    }
                } else {
                    dh->dir_ino = 0;
                    return EXT4_ERR_NOENT;
                }
            } else {
                /* No more blocks */
                dh->dir_ino = 0;
                return EXT4_ERR_NOENT;
            }
        }

        return EXT4_OK;
    }

    /* End of this block, try to load next */
    dh->offset_in_block = 0;
    if (dh->block_pos < total_blocks) {
        uint64_t pblock;
        ret = ext4_inode_data_block(fs, &inode, dh->block_pos, &pblock);
        if (ret == EXT4_OK) {
            ret = ext4_read_block(fs, pblock, dh->block_buf);
            if (ret == EXT4_OK) {
                dh->block_pos++;
                /* Recurse to read from the new block */
                return ext4_readdir(fs, dh, entry);
            } else {
                dh->dir_ino = 0;
                return EXT4_ERR_IO;
            }
        } else {
            dh->dir_ino = 0;
            return EXT4_ERR_NOENT;
        }
    }

    /* End of directory */
    dh->dir_ino = 0;
    return EXT4_ERR_NOENT;
}

/* ============================================================================
 * Close Directory
 * ============================================================================ */

int ext4_closedir(ext4_dirhandle_t *dh) {
    if (!dh) {
        return EXT4_ERR_INVAL;
    }

    if (dh->block_buf) {
        free(dh->block_buf);
        dh->block_buf = NULL;
    }

    dh->dir_ino = 0;
    dh->block_pos = 0;
    dh->offset_in_block = 0;

    return EXT4_OK;
}

/* ============================================================================
 * Create Directory (stub - read-only for Phase 1)
 * ============================================================================ */

int ext4_mkdir(ext4_fs_t *fs, const char *path, uint16_t mode) {
    (void)fs;
    (void)path;
    (void)mode;
    return EXT4_ERR_ROFS;
}

/* ============================================================================
 * Remove Directory (stub - read-only for Phase 1)
 * ============================================================================ */

int ext4_rmdir(ext4_fs_t *fs, const char *path) {
    (void)fs;
    (void)path;
    return EXT4_ERR_ROFS;
}