/*
 * TriniOS BSD Compatibility Server - Initrd File Lookup (User-Space)
 *
 * Standalone initrd parser that works in user-space without kernel symbols.
 * Provides functions to find files by path within a cpio_newc archive.
 *
 * Usage:
 *   1. Call initrd_lookup_init(raw_data, raw_size) with the compressed or
 *      decompressed cpio archive (GRUB2 passes this via multiboot2).
 *   2. Call initrd_lookup_find_file("path/to/file") to get data+size.
 *   3. The returned pointer is into the original buffer — do NOT free it.
 *
 * NOTE: This module handles DECOMPRESSED cpio_newc archives only.
 * If the bootloader passes a gzip-compressed initrd, decompression must
 * happen before calling initrd_lookup_init().
 */

#include "initrd_lookup.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * CPIO newc header (110 bytes) — matches kernel/mm/initrd_loader.c format
 * ============================================================================ */

#define CPIO_NEWC_MAGIC       "070701"
#define CPIO_NEWC_TRAILER     "TRAILER!!!"

typedef struct __attribute__((packed)) cpio_header {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
    char major[8];
    char minor[8];
} cpio_header_t;

/* ============================================================================
 * In-memory file entry (simplified — no tree, flat lookup table)
 * ============================================================================ */

typedef struct {
    const char *path;     /* Full path (e.g., "/sbin/init") */
    uint64_t    offset;   /* Byte offset in cpio archive */
    uint64_t    size;     /* File data size */
} initrd_file_entry_t;

/* ============================================================================
 * Global state — flat lookup table of all files in the initrd
 * ============================================================================ */

static const void *g_initrd_raw = NULL;   /* Pointer to decompressed cpio archive */
static uint64_t     g_initrd_size = 0;    /* Total size of archive */
static initrd_file_entry_t g_files[INITRD_MAX_FILES];
static int         g_file_count = 0;

/* ============================================================================
 * Helper: Parse 8-byte hex field from cpio header
 * ============================================================================ */

static uint64_t parse_hex(const char *hex_str)
{
    uint64_t value = 0;
    for (int i = 0; i < 7 && hex_str[i]; i++) {
        char c = hex_str[i];
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= (c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= (c - 'A' + 10);
    }
    return value;
}

/* ============================================================================
 * Helper: Compare two strings for equality
 * ============================================================================ */

static int str_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* ============================================================================
 * initrd_lookup_init — Parse the cpio archive and build lookup table
 *
 * Walks through the cpio_newc archive, recording each regular file's path,
 * offset, and size into a flat lookup table. This is much simpler than
 * building a full tree but sufficient for execve() lookups.
 * ============================================================================ */

int initrd_lookup_init(const void *raw_data, uint64_t raw_size)
{
    if (!raw_data || raw_size == 0) {
        fprintf(stderr, "initrd_lookup: No data provided\n");
        return -1;
    }

    g_initrd_raw = raw_data;
    g_initrd_size = raw_size;
    g_file_count = 0;

    const uint8_t *pos = (const uint8_t *)raw_data;
    const uint8_t *end = pos + raw_size;

    fprintf(stderr, "initrd_lookup: Parsing cpio archive (%lu bytes)\n",
            (unsigned long)raw_size);

    while (pos + sizeof(cpio_header_t) <= end && g_file_count < INITRD_MAX_FILES) {
        /* Check magic */
        if (memcmp(pos, CPIO_NEWC_MAGIC, 6) != 0) {
            fprintf(stderr, "initrd_lookup: Invalid cpio magic at offset %lu\n",
                    (unsigned long)(pos - (const uint8_t *)raw_data));
            break;
        }

        /* Parse header */
        const cpio_header_t *hdr = (const cpio_header_t *)pos;
        pos += sizeof(cpio_header_t);

        uint64_t namesize = parse_hex(hdr->namesize);
        uint64_t filesize = parse_hex(hdr->filesize);

        if (namesize == 0 || namesize > 4096) {
            break; /* Invalid namesize */
        }

        /* Read filename */
        const char *filename = (const char *)pos;
        pos += (namesize + 3) & ~3U; /* Align to 4 bytes */

        /* Skip "." and ".." entries */
        if (str_equal(filename, ".") || str_equal(filename, "..")) {
            pos += (filesize + 3) & ~3U;
            continue;
        }

        /* Check for trailer (end of archive) */
        if (str_equal(filename, CPIO_NEWC_TRAILER)) {
            fprintf(stderr, "initrd_lookup: Archive trailer reached (%d files)\n",
                    g_file_count);
            break;
        }

        /* Parse mode to check file type (S_IFREG = 0100000) */
        uint64_t mode = parse_hex(hdr->mode);
        if ((mode & 0170000) == 0100000 && filesize > 0) {
            /* Regular file — record it */
            g_files[g_file_count].path = filename;
            g_files[g_file_count].offset = (uint64_t)(pos - (const uint8_t *)raw_data);
            g_files[g_file_count].size = filesize;
            g_file_count++;

            fprintf(stderr, "  Found: %s (%lu bytes)\n", filename,
                    (unsigned long)filesize);
        }

        /* Advance past file data */
        pos += (filesize + 3) & ~3U;
    }

    fprintf(stderr, "initrd_lookup: Registered %d files\n", g_file_count);
    return g_file_count;
}

/* ============================================================================
 * initrd_lookup_find_file — Find a file by path and return its data+size
 *
 * Returns NULL if not found. The returned pointer is into the original
 * cpio archive buffer — do NOT free it.
 * ============================================================================ */

const void *initrd_lookup_find_file(const char *path, uint64_t *out_size)
{
    if (!path || !out_size || g_file_count == 0) {
        return NULL;
    }

    for (int i = 0; i < g_file_count; i++) {
        if (str_equal(g_files[i].path, path)) {
            *out_size = g_files[i].size;
            fprintf(stderr, "initrd_lookup: Found '%s' at offset %lu, size %lu\n",
                    path, (unsigned long)g_files[i].offset, (unsigned long)g_files[i].size);
            return ((const uint8_t *)g_initrd_raw) + g_files[i].offset;
        }
    }

    fprintf(stderr, "initrd_lookup: '%s' not found\n", path);
    return NULL;
}

/* ============================================================================
 * initrd_lookup_get_file — Get file data with explicit offset and size
 * ============================================================================ */

int initrd_lookup_get_file(const char *path, const void **out_data, uint64_t *out_size)
{
    uint64_t size = 0;
    const void *data = initrd_lookup_find_file(path, &size);
    if (!data || size == 0) {
        return -1; /* ENOENT */
    }

    *out_data = data;
    *out_size = size;
    return 0;
}

/* ============================================================================
 * initrd_lookup_count — Return number of files registered
 * ============================================================================ */

int initrd_lookup_count(void)
{
    return g_file_count;
}