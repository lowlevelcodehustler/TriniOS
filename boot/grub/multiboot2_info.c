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

/* ── parse_multiboot2_info ───────────────────────────────────────────────
 *
 * Walk the multiboot2 tag list starting at mb_info_ptr.
 * Populate the trini_boot_info_t structure with parsed data.
 *
 * The multiboot2 info structure layout:
 *   Offset 0:  uint64_t total_size (8 bytes)
 *   Offset 8:  tags begin (4-byte aligned)
 *
 * Each tag:
 *   uint16_t type   (2 bytes)
 *   uint16_t flags  (2 bytes)
 *   uint32_t size   (4 bytes) — total tag size including alignment padding
 */
void parse_multiboot2_info(uint64_t mb_info_ptr, trini_boot_info_t *info)
{
    /* ── Initialize output ────────────────────────────────────────────── */
    memset(info, 0, sizeof(*info));
    info->mb_info_ptr = mb_info_ptr;
    info->cmdline[0]     = '\0';
    info->bootloader_name[0] = '\0';

    if (!mb_info_ptr)
        return;

    /* ── Read total size ──────────────────────────────────────────────── */
    uint32_t total_size = *(uint32_t *)mb_info_ptr;
    uint64_t tag_addr = mb_info_ptr + 8;  /* skip total_size field */
    uint64_t end_addr = mb_info_ptr + total_size;

    /* Safety: don't walk past total_size */
    if (tag_addr >= end_addr)
        return;

    /* ── Walk tags ────────────────────────────────────────────────────── */
    while (tag_addr < end_addr) {
        const multiboot2_tag_t *tag = (const multiboot2_tag_t *)tag_addr;
        uint16_t type  = tag->type;
        uint32_t size  = tag->size;

        /* Terminator tag — end of list */
        if (type == MULTIBOOT2_TAG_TYPE_END)
            break;

        /* Safety: tag must have non-zero size and stay within bounds */
        if (size == 0 || tag_addr + size > end_addr)
            break;

        /* ── Tag 8: Basic Memory Info ─────────────────────────────────── */
        if (type == MULTIBOOT2_TAG_TYPE_BASIC_MEM_INFO) {
            const multiboot2_basic_mem_tag *mmt =
                (const multiboot2_basic_mem_tag *)tag_addr;
            info->mem_lower = mmt->mem_lower;
            info->mem_upper = mmt->mem_upper;
        }

        /* ── Tag 20: Memory Map ───────────────────────────────────────── */
        else if (type == MULTIBOOT2_TAG_TYPE_MEMORY_MAP) {
            const multiboot2_mmap_tag *mmap =
                (const multiboot2_mmap_tag *)tag_addr;
            uint32_t data_size = size - 16;  /* subtract header */
            uint32_t entry_sz  = mmap->entry_size;

            if (entry_sz >= sizeof(multiboot2_mmap_entry_t)) {
                info->mmap_entries = mmap->entries;
                info->mmap_entry_count = data_size / entry_sz;
                info->mmap_entry_size = entry_sz;
            }
        }

        /* ── Tag 4: Module (initrd) ───────────────────────────────────── */
        else if (type == MULTIBOOT2_TAG_TYPE_MODULE) {
            const multiboot2_module_tag *mod =
                (const multiboot2_module_tag *)tag_addr;
            info->initrd_addr = mod->mod_start;
            info->initrd_end  = mod->mod_end;
            info->initrd_size = mod->mod_end - mod->mod_start;
        }

        /* ── Tag 21: Framebuffer Info ─────────────────────────────────── */
        else if (type == MULTIBOOT2_TAG_TYPE_FRAMEBUFFER_INFO) {
            const multiboot2_framebuffer_tag *fb =
                (const multiboot2_framebuffer_tag *)tag_addr;
            info->fb_addr   = fb->framebuffer_addr;
            info->fb_width  = fb->framebuffer_width;
            info->fb_height = fb->framebuffer_height;
            info->fb_pitch  = fb->framebuffer_pitch;
            info->fb_type   = fb->framebuffer_type;
            info->fb_depth  = fb->framebuffer_red_mask_size +
                              fb->framebuffer_green_mask_size +
                              fb->framebuffer_blue_mask_size;
        }

        /* ── Tag 10: Command Line ─────────────────────────────────────── */
        else if (type == MULTIBOOT2_TAG_TYPE_CMDLINE) {
            const multiboot2_cmdline_tag *cl =
                (const multiboot2_cmdline_tag *)tag_addr;
            copy_string(info->cmdline, sizeof(info->cmdline),
                        (uint64_t)cl->cmdline);
        }

        /* ── Tag 11: Bootloader Name ──────────────────────────────────── */
        else if (type == MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME) {
            const multiboot2_bootloader_name_tag *bl =
                (const multiboot2_bootloader_name_tag *)tag_addr;
            copy_string(info->bootloader_name, sizeof(info->bootloader_name),
                        (uint64_t)bl->name);
        }

        /* ── Advance to next tag ──────────────────────────────────────── */
        tag_addr += size;
    }
}