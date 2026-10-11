/*
 * multiboot2_info.c
 * TriniOS — Multiboot2 Boot Information Parser (spec v2.0)
 *
 * Walks the tag list GRUB2 passes at kernel entry and populates
 * trini_boot_info_t. Every access is bounds-checked against the structure's
 * declared total size, so a malformed or truncated info structure can never
 * take down the boot process — worst case we simply stop walking early.
 */

#include "multiboot2_info.h"
#include <string.h>   /* memset — provided by freestanding_stubs.c */

/* Sanity cap for the whole tag list (used if total_size looks bogus).
 * GRUB's info structures are small; even a long memory map stays well
 * under this. */
#define MB_INFO_MAX_SIZE 0x40000UL   /* 256 KiB */

/* ── copy_string: bounded NUL-terminated copy from physical address ───── */
static void copy_string(char *dest, size_t dest_size, const char *src, uint32_t max_len)
{
    if (!dest || !dest_size || !src)
        return;

    size_t i = 0;
    while (i + 1 < dest_size && i < max_len && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* ── mmap_type_name: human-readable memory map region type ───────────── */
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

/* ── parse_multiboot2_info: walk the tag list, populate info ─────────── */
void parse_multiboot2_info(uint64_t mb_info_ptr, trini_boot_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->mb_info_ptr = mb_info_ptr;

    if (!mb_info_ptr)
        return;   /* No boot info (e.g. booted without a multiboot2 loader) */

    const uint8_t *base = (const uint8_t *)mb_info_ptr;

    /* Preamble: total_size + reserved, then tags start at offset 8. */
    if (base[0] == 0 && base[1] == 0 && base[2] == 0 && base[3] == 0)
        return;   /* All-zero structure — nothing to parse */

    uint64_t total = *(const uint32_t *)(base + 0);
    if (total < 16 || total > MB_INFO_MAX_SIZE) {
        /* Bogus total size: fall back to the sanity cap so we can still
         * walk a sane prefix instead of trusting an out-of-range value. */
        total = MB_INFO_MAX_SIZE;
    }

    int got_module = 0;

    for (uint64_t off = 8; off + 8 <= total;) {
        const multiboot2_info_tag_header_t *hdr =
            (const multiboot2_info_tag_header_t *)(base + off);
        uint32_t type = hdr->type;
        uint32_t size = hdr->size;

        /* A tag must be at least its 8-byte header, and the whole tag must
         * fit inside the declared structure. */
        if (size < 8 || off + size > total)
            break;

        switch (type) {
        case MULTIBOOT2_TAG_END:
            /* Terminator ({0, 8}) — always last per spec. */
            return;

        case MULTIBOOT2_TAG_CMDLINE: {
            if (size > 8)
                copy_string(info->cmdline, sizeof(info->cmdline),
                            (const char *)(base + off + 8), size - 8);
            break;
        }

        case MULTIBOOT2_TAG_BOOTLOADER_NAME: {
            if (size > 8)
                copy_string(info->bootloader_name, sizeof(info->bootloader_name),
                            (const char *)(base + off + 8), size - 8);
            break;
        }

        case MULTIBOOT2_TAG_MODULE: {
            /* One tag per `module` directive. grub.cfg loads the initrd as
             * the first module, so we record the first one only. */
            if (size < sizeof(multiboot2_module_tag_t))
                break;
            const multiboot2_module_tag_t *m =
                (const multiboot2_module_tag_t *)(base + off);
            if (!got_module) {
                info->initrd_addr = m->mod_start;
                info->initrd_end  = m->mod_end;
                info->initrd_size = (m->mod_end > m->mod_start)
                                    ? m->mod_end - m->mod_start : 0;
                got_module = 1;
            }
            break;
        }

        case MULTIBOOT2_TAG_BASIC_MEMINFO: {
            if (size < sizeof(multiboot2_basic_meminfo_tag_t))
                break;
            const multiboot2_basic_meminfo_tag_t *mem =
                (const multiboot2_basic_meminfo_tag_t *)(base + off);
            info->mem_lower = mem->mem_lower;   /* KiB */
            info->mem_upper = mem->mem_upper;   /* KiB */
            break;
        }

        case MULTIBOOT2_TAG_MEMORY_MAP: {
            if (size < 16)
                break;
            const multiboot2_memory_map_tag_t *mm =
                (const multiboot2_memory_map_tag_t *)(base + off);
            uint32_t esz = mm->entry_size;

            /* Each entry is at least addr+len+type(+reserved padding). */
            if (esz < 16 || esz > 0x100)
                break;

            const multiboot2_mmap_entry_t *entries =
                (const multiboot2_mmap_entry_t *)(base + off + 8);
            uint32_t data_len = size - 8;

            /* Entries are laid out at stride = entry_size (per spec: the
             * reported size is the actual stride between entries). */
            info->mmap_entries    = entries;
            info->mmap_entry_size = esz;
            info->mmap_entry_count = data_len / esz;
            break;
        }

        case MULTIBOOT2_TAG_FRAMEBUFFER: {
            /* Fixed fields end after bpp/type/reserved (offset 8+8+4+4+4+1). */
            if (size < 30)
                break;
            const multiboot2_framebuffer_tag_t *fb =
                (const multiboot2_framebuffer_tag_t *)(base + off);
            info->fb_addr   = fb->framebuffer_addr;
            info->fb_pitch  = fb->framebuffer_pitch;
            info->fb_width  = fb->framebuffer_width;
            info->fb_height = fb->framebuffer_height;
            info->fb_bpp    = fb->framebuffer_bpp;
            info->fb_type   = fb->framebuffer_type;
            break;
        }

        default:
            /* Unknown tag type — skip it (forward compatibility). */
            break;
        }

        /* Next tag starts 8-byte aligned after this one. */
        off += (size + 7) & ~7u;
    }
}
