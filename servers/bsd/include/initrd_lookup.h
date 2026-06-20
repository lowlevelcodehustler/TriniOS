/*
 * TriniOS BSD Compatibility Server - Initrd File Lookup (User-Space)
 *
 * Standalone initrd parser that works in user-space without kernel symbols.
 * Provides functions to find files by path within a cpio_newc archive.
 */

#ifndef INITRD_LOOKUP_H
#define INITRD_LOOKUP_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum number of files the lookup table can hold */
#define INITRD_MAX_FILES 256

/* ============================================================================
 * API Functions
 * ============================================================================ */

/**
 * Initialize the initrd lookup table from a decompressed cpio_newc archive.
 *
 * @param raw_data   Pointer to the decompressed cpio archive in memory
 * @param raw_size   Size of the archive in bytes
 * @return           Number of files registered on success, -1 on failure
 */
int initrd_lookup_init(const void *raw_data, uint64_t raw_size);

/**
 * Find a file by its full path and return a pointer to its data.
 *
 * @param path       Full path (e.g., "/sbin/init")
 * @param out_size   Output: size of the file data in bytes
 * @return           Pointer to file data (into original buffer), or NULL if not found
 */
const void *initrd_lookup_find_file(const char *path, uint64_t *out_size);

/**
 * Get file data with explicit offset and size output.
 *
 * @param path       Full path
 * @param out_data   Output: pointer to file data
 * @param out_size   Output: size of the file data in bytes
 * @return           0 on success, -1 if not found
 */
int initrd_lookup_get_file(const char *path, const void **out_data, uint64_t *out_size);

/**
 * Return the number of files registered in the lookup table.
 */
int initrd_lookup_count(void);

#endif /* INITRD_LOOKUP_H */