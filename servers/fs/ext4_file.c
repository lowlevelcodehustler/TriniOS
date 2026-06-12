/*
 * TriniOS ext4 Filesystem Driver - File Operations
 *
 * Open, close, read, write, lseek, stat, unlink, rename.
 */

#include "ext4.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Open File
 * ============================================================================ */

int ext4_open(ext4_fs_t *fs, const char *path, uint16_t flags, ext4_file_t *file) {
    if (!fs || !path || !file) {
        return EXT4_ERR_INVAL;
    }

    /* Lookup path to get inode number */
    uint32_t ino;
    int ret = ext4_path_lookup(fs, path, &ino, NULL);
    if (ret != EXT4_OK) {
        /* If O_CREAT equivalent is set, we'd create the file here.
         * For Phase 1, we only support opening existing files. */
        if (flags & 0x0001) { /* O_CREAT placeholder */
            return EXT4_ERR_NOENT;
        }
        return ret;
    }

    /* Read inode to determine file type */
    ext4_inode_t inode;
    ret = ext4_iget(fs, ino, &inode);
    if (ret != EXT4_OK) return ret;

    /* Validate access mode */
    if ((flags & 0x0002) && !(inode.i_mode & 0200)) { /* O_WRONLY without write perm */
        return EXT4_ERR_ACCES;
    }

    /* Initialize file handle */
    file->ino = ino;
    file->pos = 0;
    file->flags = flags;
    file->mode = (uint8_t)(inode.i_mode & 0777);
    file->is_dir = EXT4_S_ISDIR(inode.i_mode);

    /* Cannot open a directory for writing */
    if (file->is_dir && (flags & 0x0002)) {
        return EXT4_ERR_ISDIR;
    }

    /* Cannot write to a directory */
    if (file->is_dir && (flags & 0x0002)) {
        return EXT4_ERR_ISDIR;
    }

    return EXT4_OK;
}

/* ============================================================================
 * Close File
 * ============================================================================ */

int ext4_close(ext4_fs_t *fs, ext4_file_t *file) {
    if (!fs || !file) {
        return EXT4_ERR_INVAL;
    }

    /* In a full implementation, we'd flush dirty data here.
     * For Phase 1 read-only, just clear the handle. */
    file->ino = 0;
    file->pos = 0;
    file->flags = 0;
    file->is_dir = false;

    return EXT4_OK;
}

/* ============================================================================
 * Read File
 * ============================================================================ */

int ext4_read(ext4_fs_t *fs, ext4_file_t *file, void *buf,
              uint64_t count, uint64_t *done) {
    if (!fs || !file || !buf || !done) {
        return EXT4_ERR_INVAL;
    }

    if (file->ino == 0) {
        return EXT4_ERR_BADF;
    }

    /* Read the inode to get file size */
    ext4_inode_t inode;
    int ret = ext4_iget(fs, file->ino, &inode);
    if (ret != EXT4_OK) return ret;

    uint64_t file_size = ((uint64_t)inode.i_size_hi << 32) | inode.i_size_lo;

    /* Check if we're past EOF */
    if (file->pos >= file_size) {
        if (done) *done = 0;
        return EXT4_OK;
    }

    /* Limit read to file size */
    uint64_t remaining = file_size - file->pos;
    if (count > remaining) {
        count = remaining;
    }

    uint64_t total_read = 0;
    uint8_t *pbuf = (uint8_t *)buf;

    while (total_read < count) {
        /* Compute current logical block */
        uint64_t lblock = file->pos / fs->block_size;
        uint32_t offset_in_block = (uint32_t)(file->pos % fs->block_size);

        /* Map logical block to physical block */
        uint64_t pblock;
        ret = ext4_inode_data_block(fs, &inode, lblock, &pblock);
        if (ret != EXT4_OK) {
            /* Hole in file - zero-fill */
            uint32_t chunk = fs->block_size - offset_in_block;
            if (chunk > count - total_read) {
                chunk = (uint32_t)(count - total_read);
            }
            memset(pbuf + total_read, 0, chunk);
            total_read += chunk;
            file->pos += chunk;
            continue;
        }

        /* Read the physical block */
        uint8_t block_buf[EXT4_MAX_BLOCK_SIZE];
        ret = ext4_read_block(fs, pblock, block_buf);
        if (ret != EXT4_OK) return ret;

        /* Copy data from block to user buffer */
        uint32_t chunk = fs->block_size - offset_in_block;
        if (chunk > count - total_read) {
            chunk = (uint32_t)(count - total_read);
        }

        memcpy(pbuf + total_read, block_buf + offset_in_block, chunk);
        total_read += chunk;
        file->pos += chunk;
    }

    if (done) *done = total_read;
    return EXT4_OK;
}

/* ============================================================================
 * Write File (stub - read-only for Phase 1)
 * ============================================================================ */

int ext4_write(ext4_fs_t *fs, ext4_file_t *file, const void *buf,
               uint64_t count, uint64_t *done) {
    /* Phase 1: read-only filesystem. Write is not supported yet. */
    (void)fs;
    (void)file;
    (void)buf;
    (void)count;
    if (done) *done = 0;
    return EXT4_ERR_ROFS;
}

/* ============================================================================
 * Lseek
 * ============================================================================ */

int ext4_lseek(ext4_fs_t *fs, ext4_file_t *file, int64_t offset,
               int whence, uint64_t *new_pos) {
    if (!fs || !file || !new_pos) {
        return EXT4_ERR_INVAL;
    }

    if (file->ino == 0) {
        return EXT4_ERR_BADF;
    }

    /* Get file size for SEEK_END and bounds checking */
    ext4_inode_t inode;
    int ret = ext4_iget(fs, file->ino, &inode);
    if (ret != EXT4_OK) return ret;

    uint64_t file_size = ((uint64_t)inode.i_size_hi << 32) | inode.i_size_lo;

    uint64_t result;
    switch (whence) {
    case 0: /* SEEK_SET */
        result = (uint64_t)(offset < 0 ? 0 : offset);
        break;
    case 1: /* SEEK_CUR */
        if (offset < 0 && (uint64_t)(-offset) > file->pos) {
            result = 0;
        } else {
            result = file->pos + (uint64_t)offset;
        }
        break;
    case 2: /* SEEK_END */
        result = file_size + (uint64_t)(offset < 0 ? 0 : offset);
        break;
    case 3: /* SEEK_DATA */
        /* Find next data block at or beyond offset */
        result = (uint64_t)(offset < 0 ? 0 : offset);
        break;
    case 4: /* SEEK_HOLE */
        /* Find next hole at or beyond offset */
        result = (uint64_t)(offset < 0 ? 0 : offset);
        break;
    default:
        return EXT4_ERR_INVAL;
    }

    file->pos = result;
    *new_pos = result;
    return EXT4_OK;
}

/* ============================================================================
 * Stat
 * ============================================================================ */

int ext4_stat(ext4_fs_t *fs, const char *path, ext4_stat_t *st) {
    if (!fs || !path || !st) {
        return EXT4_ERR_INVAL;
    }

    /* Lookup path */
    uint32_t ino;
    int ret = ext4_path_lookup(fs, path, &ino, NULL);
    if (ret != EXT4_OK) return ret;

    /* Read inode */
    ext4_inode_t inode;
    ret = ext4_iget(fs, ino, &inode);
    if (ret != EXT4_OK) return ret;

    /* Populate stat structure */
    st->ino = ino;
    st->mode = inode.i_mode;
    st->nlinks = inode.i_links_count;
    st->uid = ((uint32_t)inode.i_uid_hi << 16) | inode.i_uid_lo;
    st->gid = ((uint32_t)inode.i_gid_hi << 16) | inode.i_gid_lo;
    st->size = ((uint64_t)inode.i_size_hi << 32) | inode.i_size_lo;
    st->blocks = ((uint64_t)inode.i_blocks_hi << 32) | inode.i_blocks_lo;
    st->atime = inode.i_atime;
    st->mtime = inode.i_mtime;
    st->ctime = inode.i_ctime;

    return EXT4_OK;
}

/* ============================================================================
 * Unlink (stub - read-only for Phase 1)
 * ============================================================================ */

int ext4_unlink(ext4_fs_t *fs, const char *path) {
    (void)fs;
    (void)path;
    return EXT4_ERR_ROFS;
}

/* ============================================================================
 * Rename (stub - read-only for Phase 1)
 * ============================================================================ */

int ext4_rename(ext4_fs_t *fs, const char *old_path, const char *new_path) {
    (void)fs;
    (void)old_path;
    (void)new_path;
    return EXT4_ERR_ROFS;
}