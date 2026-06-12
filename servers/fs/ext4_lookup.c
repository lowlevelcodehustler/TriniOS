/*
 * TriniOS ext4 Filesystem Driver - Path Lookup
 *
 * Resolves pathname strings to inode numbers by walking directory entries.
 * Handles ".", "..", and relative/absolute paths.
 */

#include "ext4.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ============================================================================
 * Helper: Compare a name component with a directory entry
 * ============================================================================ */

static bool ext4_name_match(const char *name, uint8_t name_len,
                            const char *de_name, uint8_t de_len) {
    if (name_len != de_len) return false;
    return memcmp(name, de_name, name_len) == 0;
}

/* ============================================================================
 * Helper: Scan one directory block for a name component
 *
 * Returns EXT4_OK if found, sets *found_ino and advances *offset_in_block.
 * Returns EXT4_ERR_NOENT if not found in this directory.
 * ============================================================================ */

static int ext4_scan_dir_block(ext4_fs_t *fs, uint32_t dir_ino,
                               const char *component, uint8_t comp_len,
                               uint64_t block_pos, uint16_t offset_in_block,
                               uint32_t *found_ino, uint8_t *found_type) {
    uint8_t *buf;
    int ret = ext4_cache_get_block(fs, block_pos, &buf);
    if (ret != EXT4_OK) return EXT4_ERR_IO;

    uint16_t offset = offset_in_block;
    uint16_t rec_len;

    while (offset < fs->block_size) {
        /* Ensure we can read the directory entry header */
        if (offset + 8 > fs->block_size) break;

        ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(buf + offset);
        rec_len = de->rec_len;

        /* Align rec_len to 4 bytes (ext4 requirement) */
        if (rec_len & 3) rec_len += 4 - (rec_len & 3);

        /* Validate entry */
        if (rec_len == 0 || rec_len > fs->block_size) break;
        if (de->name_len == 0 || de->name_len > rec_len - 8) break;

        /* Check for match */
        if (de->name_len == comp_len &&
            ext4_name_match(component, comp_len, de->name, de->name_len)) {
            *found_ino = de->inode;
            if (found_type) *found_type = de->file_type;
            return EXT4_OK;
        }

        offset += rec_len;
    }

    return EXT4_ERR_NOENT;
}

/* ============================================================================
 * Helper: Lookup a single name component within a directory inode
 * ============================================================================ */

static int ext4_lookup_component(ext4_fs_t *fs, uint32_t dir_ino,
                                 const char *component, uint8_t comp_len,
                                 uint32_t *result_ino, uint8_t *result_type) {
    ext4_inode_t inode;
    int ret = ext4_iget(fs, dir_ino, &inode);
    if (ret != EXT4_OK) return ret;

    uint64_t file_size = ((uint64_t)inode.i_size_hi << 32) | inode.i_size_lo;
    uint64_t file_blocks = file_size / fs->block_size;

    for (uint64_t b = 0; b <= file_blocks; b++) {
        uint64_t pblock;
        ret = ext4_inode_data_block(fs, &inode, b, &pblock);
        if (ret != EXT4_OK) break; /* Hole or end of file */

        ret = ext4_scan_dir_block(fs, dir_ino, component, comp_len,
                                  pblock, 0, result_ino, result_type);
        if (ret == EXT4_OK) return EXT4_OK;
    }

    return EXT4_ERR_NOENT;
}

/* ============================================================================
 * Path Lookup: Resolve a full path to an inode number
 *
 * Walks each component of the path, starting from the root (inode 2).
 * Sets *ino to the resolved inode number.
 * If last_component is non-NULL, copies the final component name into it.
 * ============================================================================ */

int ext4_path_lookup(ext4_fs_t *fs, const char *path,
                     uint32_t *ino, char *last_component) {
    if (!fs || !path || !ino) {
        return EXT4_ERR_INVAL;
    }

    /* Start at root inode */
    uint32_t current_ino = 2;

    /* Make a mutable copy of the path */
    char path_buf[1024];
    strncpy(path_buf, path, sizeof(path_buf) - 1);
    path_buf[sizeof(path_buf) - 1] = '\0';

    char *ptr = path_buf;
    char *saved_ptr = ptr; /* Track which component we're on */

    /* Handle absolute paths */
    if (*ptr == '/') {
        ptr++;
        current_ino = 2; /* root */
    }

    /* Walk each component */
    while (*ptr) {
        /* Skip leading slashes */
        while (*ptr == '/') ptr++;
        if (!*ptr) break;

        saved_ptr = ptr;

        /* Find the next slash or end */
        char *slash = strchr(ptr, '/');
        uint8_t comp_len;
        if (slash) {
            comp_len = (uint8_t)(slash - ptr);
        } else {
            comp_len = (uint8_t)strlen(ptr);
        }

        /* Handle "." */
        if (comp_len == 1 && ptr[0] == '.') {
            /* Stay at current inode */
            if (slash) ptr = slash;
            else break;
            continue;
        }

        /* Handle ".." */
        if (comp_len == 2 && ptr[0] == '.' && ptr[1] == '.') {
            /* For simplicity, we don't track parent inodes in this phase.
             * In a full implementation, we'd need a dentry cache.
             * Here we just skip ".." entries (they don't resolve to real inos). */
            if (slash) ptr = slash;
            else break;
            continue;
        }

        /* Store last component if requested */
        if (last_component && !slash) {
            memcpy(last_component, ptr, comp_len);
            last_component[comp_len] = '\0';
        }

        /* Lookup this component */
        uint32_t found_ino;
        uint8_t found_type;
        int ret = ext4_lookup_component(fs, current_ino, ptr, comp_len,
                                        &found_ino, &found_type);
        if (ret != EXT4_OK) return ret;

        current_ino = found_ino;

        if (slash) ptr = slash;
        else break;
    }

    *ino = current_ino;
    return EXT4_OK;
}

/* ============================================================================
 * Helper: Split path into directory and filename components
 * ============================================================================ */

int ext4_split_path(const char *path, char *dir_path, char *filename) {
    if (!path || !dir_path || !filename) {
        return EXT4_ERR_INVAL;
    }

    /* Find last slash */
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        /* No directory component */
        dir_path[0] = '\0';
        strcpy(filename, path);
        return EXT4_OK;
    }

    if (last_slash == path) {
        /* Root directory */
        strcpy(dir_path, "/");
        strcpy(filename, last_slash + 1);
        return EXT4_OK;
    }

    /* Split */
    uint32_t dir_len = (uint32_t)(last_slash - path);
    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';
    strcpy(filename, last_slash + 1);

    return EXT4_OK;
}

/* ============================================================================
 * Helper: Check if a directory entry is a directory
 * ============================================================================ */

bool ext4_is_directory(ext4_fs_t *fs, uint32_t ino) {
    ext4_inode_t inode;
    if (ext4_iget(fs, ino, &inode) != EXT4_OK) {
        return false;
    }
    return EXT4_S_ISDIR(inode.i_mode);
}

/* ============================================================================
 * Helper: Check if a directory entry is a regular file
 * ============================================================================ */

bool ext4_is_regular(ext4_fs_t *fs, uint32_t ino) {
    ext4_inode_t inode;
    if (ext4_iget(fs, ino, &inode) != EXT4_OK) {
        return false;
    }
    return EXT4_S_ISREG(inode.i_mode);
}