/*
 * multiboot2_info.c
 * TriniOS — Multiboot2 Info Parser Implementation
 *
 * Walks the tag list provided by GRUB2 and populates trini_boot_info_t
 * with all parsed boot information.
 */

#include "multiboot2_info.h"
#include <string.h>

/* ── Internal helper: safe string copy from physical address ────────────── */
static void copy_string(char *dest, size_t dest_size, uint64_t src_addr)
{
    if (!src_addr || !dest || !dest_size)
        return;

    const char *src = (const char *)src_addr;
    size_t i = 0;
    while (src[i] && i + 1 < dest_size) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* ── mmap_type_name: human-readable memory map type ────────────────────── */
const char *mmap_type_name(uint32_t type)
{
    switch (type) {
    case MULTIBOOT2_MMAP_USABLE:           return "Usable";
    case MULTIBOOT2_MMAP_RESERVED:         return "Reserved";
    case MULTIBOOT2_MMAP_ACPI_RECLAIMABLE: return "ACPI Reclaimable";
    case MULTIBOOT2_MMAP_ACPI_NVS:         return "ACPI NVS";
    case MULTIBOOT2_MMAP_BAD_MEMORY:       return "Bad Memory";
    default:                               return "Unknown";
    }
}

/* Stub: parse_multiboot2_info is defined in enter64.asm for freestanding builds.
 * This C version is kept as a no-op placeholder to avoid duplicate symbol errors. */
void parse_multiboot2_info(uint64_t mb_info_ptr, trini_boot_info_t *info)
{
    /* Stub: real implementation is in enter64.asm */
    (void)mb_info_ptr;
    if (info) {
        info->mb_info_ptr = 0;
        info->cmdline[0] = '\0';
        info->bootloader_name[0] = '\0';
    }
}
