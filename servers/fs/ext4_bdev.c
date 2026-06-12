/*
 * TriniOS ext4 Filesystem Driver - Block Device Layer
 *
 * Abstracts block-level I/O over various storage backends.
 * Currently supports: raw disk image, memory-backed device.
 */

#include "ext4.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Block Device Initialization
 * ============================================================================ */

int ext4_bdev_init(ext4_block_device_t *bdev, void *dev,
                   uint64_t total_blocks, uint32_t block_size) {
    if (!bdev || !dev || !total_blocks || !block_size) {
        return EXT4_ERR_INVAL;
    }

    /* Validate block size is power of 2 within ext4 range */
    if (block_size < EXT4_MIN_BLOCK_SIZE || block_size > EXT4_MAX_BLOCK_SIZE) {
        return EXT4_ERR_INVAL;
    }

    uint32_t tmp = block_size;
    while ((tmp & 1) == 0) {
        tmp >>= 1;
    }
    if (tmp != 1) {
        return EXT4_ERR_INVAL;
    }

    bdev->dev = dev;
    bdev->total_blocks = total_blocks;
    bdev->block_size = block_size;
    bdev->read_blocks = NULL;
    bdev->write_blocks = NULL;

    return EXT4_OK;
}

/* ============================================================================
 * Block Read
 * ============================================================================ */

int ext4_bdev_read(ext4_block_device_t *bdev, uint64_t block,
                   uint32_t count, void *buf) {
    if (!bdev || !bdev->read_blocks || !buf) {
        return EXT4_ERR_INVAL;
    }

    if (block + count > bdev->total_blocks) {
        return EXT4_ERR_IO;
    }

    return bdev->read_blocks(bdev->dev, block, count, buf);
}

/* ============================================================================
 * Block Write
 * ============================================================================ */

int ext4_bdev_write(ext4_block_device_t *bdev, uint64_t block,
                    uint32_t count, const void *buf) {
    if (!bdev || !bdev->write_blocks || !buf) {
        return EXT4_ERR_INVAL;
    }

    if (block + count > bdev->total_blocks) {
        return EXT4_ERR_IO;
    }

    return bdev->write_blocks(bdev->dev, block, count, buf);
}

/* ============================================================================
 * Memory-Backed Block Device (for testing / initrd integration)
 * ============================================================================ */

typedef struct {
    uint8_t  *data;
    uint64_t  total_blocks;
    uint32_t  block_size;
} mem_bdev_t;

static int mem_read_blocks(void *dev, uint64_t block, uint32_t count, void *buf) {
    mem_bdev_t *mb = (mem_bdev_t *)dev;
    if (!mb || !mb->data) return EXT4_ERR_IO;

    uint64_t src = block * mb->block_size;
    memcpy(buf, mb->data + src, (size_t)(count * mb->block_size));
    return EXT4_OK;
}

static int mem_write_blocks(void *dev, uint64_t block, uint32_t count,
                            const void *buf) {
    mem_bdev_t *mb = (mem_bdev_t *)dev;
    if (!mb || !mb->data) return EXT4_ERR_IO;

    uint64_t dst = block * mb->block_size;
    memcpy(mb->data + dst, buf, (size_t)(count * mb->block_size));
    return EXT4_OK;
}

ext4_block_device_t *ext4_create_mem_bdev(uint8_t *data,
                                          uint64_t total_blocks,
                                          uint32_t block_size) {
    mem_bdev_t *mb = (mem_bdev_t *)malloc(sizeof(mem_bdev_t));
    if (!mb) return NULL;

    mb->data = data;
    mb->total_blocks = total_blocks;
    mb->block_size = block_size;

    ext4_block_device_t *bdev = (ext4_block_device_t *)malloc(sizeof(ext4_block_device_t));
    if (!bdev) {
        free(mb);
        return NULL;
    }

    bdev->dev = mb;
    bdev->total_blocks = total_blocks;
    bdev->block_size = block_size;
    bdev->read_blocks = mem_read_blocks;
    bdev->write_blocks = mem_write_blocks;

    return bdev;
}

void ext4_destroy_mem_bdev(ext4_block_device_t *bdev) {
    if (bdev) {
        free(bdev->dev);
        free(bdev);
    }
}

/* ============================================================================
 * Helper: Read a single block into a buffer
 * ============================================================================ */

int ext4_read_block(ext4_fs_t *fs, uint64_t block, void *buf) {
    return ext4_bdev_read(fs->bdev, block, 1, buf);
}

/* ============================================================================
 * Helper: Write a single block from a buffer
 * ============================================================================ */

int ext4_write_block(ext4_fs_t *fs, uint64_t block, const void *buf) {
    return ext4_bdev_write(fs->bdev, block, 1, buf);
}

/* ============================================================================
 * Block Cache (simple single-block cache for metadata reads)
 * ============================================================================ */

static int cache_valid = 0;
static uint64_t cache_block_addr = 0;
static uint8_t cache_buf[EXT4_MAX_BLOCK_SIZE];

static void cache_invalidate(void) {
    cache_valid = 0;
}

static int cache_get_block(ext4_fs_t *fs, uint64_t block, const uint8_t **out) {
    if (cache_valid && cache_block_addr == block) {
        *out = cache_buf;
        return EXT4_OK;
    }

    int ret = ext4_bdev_read(fs->bdev, block, 1, cache_buf);
    if (ret == EXT4_OK) {
        cache_valid = 1;
        cache_block_addr = block;
        *out = cache_buf;
    }
    return ret;
}

/* Public API to access cache for internal use */
int ext4_cache_get_block(ext4_fs_t *fs, uint64_t block, const uint8_t **out) {
    return cache_get_block(fs, block, out);
}

void ext4_cache_invalidate(void) {
    cache_invalidate();
}