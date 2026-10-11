/*
 * multiboot2_info.h
 * TriniOS — Multiboot2 Boot Information Structures (spec v2.0)
 *
 * Defines the C structures for parsing the Multiboot2 boot information
 * that GRUB2 passes to the kernel at entry:
 *   - EAX = 0x36D76289 (magic, "bootloader passed multiboot2 info")
 *   - EBX = physical address of the boot information structure
 *
 * Layout and tag numbers follow the official Multiboot2 specification v2.0
 * (https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html) as
 * implemented by GRUB:
 *
 *   The info structure begins with two u32 fields, then a list of tags:
 *     [u32 total_size]  — size of the whole structure incl. terminator tag
 *     [u32 reserved=0]
 *     [tags ...]        — terminated by {type=0, size=8}
 *
 *   Every info tag starts with an 8-byte header:
 *     u32 type;         — tag identifier (see below)
 *     u32 size;         — tag size incl. this header, excl. padding
 *   Tags are padded so each starts at an 8-byte aligned address; the next
 *   tag is at offset + ((size + 7) & ~7).
 *
 * Tag types (spec v2.0):
 *   0  end of list
 *   1  command line (NUL-terminated string)
 *   2  boot loader name (NUL-terminated string, e.g. "GRUB 2.xx")
 *   3  module (mod_start/mod_end + optional per-module cmdline string;
 *      one tag per `module` directive in grub.cfg — the initrd arrives here)
 *   4  basic memory info (mem_lower/mem_upper, KiB)
 *   5  boot device (biosdev/slice/partition)
 *   6  memory map (entry_size/entry_version + entries[])
 *   7  VBE control/mode information
 *   8  framebuffer (addr/pitch/width/height/bpp/type [+ palette if indexed])
 *   9  ELF section headers of the kernel image
 *   10 APM table
 *   11 pointer to i386 EFI system table (u32)
 *   12 pointer to amd64 EFI system table (u64)
 */

#ifndef MULTIBOOT2_INFO_H
#define MULTIBOOT2_INFO_H

#include <stdint.h>

/* ── Magic value in EAX at entry ("bootloader passed multiboot2 info") ── */
#define MULTIBOOT2_BOOTLOADER_MAGIC   0x36D76289

/* ── Info tag types (spec v2.0 — do not renumber!) ───────────────────── */
#define MULTIBOOT2_TAG_END            0
#define MULTIBOOT2_TAG_CMDLINE        1
#define MULTIBOOT2_TAG_BOOTLOADER_NAME 2
#define MULTIBOOT2_TAG_MODULE         3
#define MULTIBOOT2_TAG_BASIC_MEMINFO  4
#define MULTIBOOT2_TAG_BOOTDEV        5
#define MULTIBOOT2_TAG_MEMORY_MAP     6
#define MULTIBOOT2_TAG_VBE            7
#define MULTIBOOT2_TAG_FRAMEBUFFER    8
#define MULTIBOOT2_TAG_ELF_SECTIONS   9
#define MULTIBOOT2_TAG_APM_BIOS       10
#define MULTIBOOT2_TAG_EFI32_SYSTAB   11
#define MULTIBOOT2_TAG_EFI64_SYSTAB   12

/* ── Generic info tag header (type FIRST, then size) ─────────────────── */
typedef struct __attribute__((packed)) multiboot2_info_tag_header {
    uint32_t type;     /* Tag type identifier */
    uint32_t size;     /* Total tag size incl. this header, excl. padding */
} multiboot2_info_tag_header_t;

/* ── Info structure preamble (before the first tag) ──────────────────── */
typedef struct __attribute__((packed)) multiboot2_info_preamble {
    uint32_t total_size;   /* Whole structure size incl. terminator tag */
    uint32_t reserved;     /* Always 0, must be ignored */
} multiboot2_info_preamble_t;

/* ── Tag type 1: command line (NUL-terminated string) ────────────────── */
typedef struct __attribute__((packed)) multiboot2_cmdline_tag {
    multiboot2_info_tag_header_t header; /* type = 1 */
    char cmdline[];                      /* NUL-terminated, tag padded to 8B */
} multiboot2_cmdline_tag_t;

/* ── Tag type 2: boot loader name (NUL-terminated string) ────────────── */
typedef struct __attribute__((packed)) multiboot2_bootloader_name_tag {
    multiboot2_info_tag_header_t header; /* type = 2 */
    char name[];                         /* e.g. "GRUB 2.04" */
} multiboot2_bootloader_name_tag_t;

/* ── Tag type 3: module (one per `module` directive in grub.cfg) ─────── */
typedef struct __attribute__((packed)) multiboot2_module_tag {
    multiboot2_info_tag_header_t header; /* type = 3 */
    uint32_t mod_start;                  /* Physical start of module data */
    uint32_t mod_end;                    /* End address (exclusive) */
    char cmdline[];                      /* Optional per-module string, NUL-terminated */
} multiboot2_module_tag_t;

/* ── Tag type 4: basic memory info (KiB) ─────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_basic_meminfo_tag {
    multiboot2_info_tag_header_t header; /* type = 4 */
    uint32_t mem_lower;                  /* KiB below the first 1 MiB (max 640) */
    uint32_t mem_upper;                  /* KiB above the first 1 MiB */
} multiboot2_basic_meminfo_tag_t;

/* ── Tag type 5: boot device ─────────────────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_bootdev_tag {
    multiboot2_info_tag_header_t header; /* type = 5 */
    uint32_t biosdev;                    /* BIOS drive number (0x80 = first disk) */
    uint32_t slice;                      /* Slice/partition index */
    uint32_t part;                       /* Sub-partition index */
} multiboot2_bootdev_tag_t;

/* ── Tag type 6: memory map ──────────────────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_mmap_entry {
    uint64_t addr;        /* Base address */
    uint64_t len;         /* Length in bytes */
    uint32_t type;        /* Memory region type (see below) */
    uint32_t reserved;    /* Always 0, must be ignored */
} multiboot2_mmap_entry_t;

/* Memory map region types */
#define MULTIBOOT2_MMAP_USABLE            1
#define MULTIBOOT2_MMAP_RESERVED          2
#define MULTIBOOT2_MMAP_ACPI_RECLAIMABLE  3
#define MULTIBOOT2_MMAP_ACPI_NVS          4
#define MULTIBOOT2_MMAP_BAD_MEMORY        5

typedef struct __attribute__((packed)) multiboot2_memory_map_tag {
    multiboot2_info_tag_header_t header; /* type = 6 */
    uint32_t entry_size;                 /* Size of each entry (typically 24) */
    uint32_t entry_version;              /* Always 1 */
    multiboot2_mmap_entry_t entries[];   /* Stride between entries = entry_size */
} multiboot2_memory_map_tag_t;

/* ── Tag type 8: framebuffer ─────────────────────────────────────────── */
#define MULTIBOOT2_FB_TYPE_INDEXED        0   /* color_info palette follows */
#define MULTIBOOT2_FB_TYPE_RGB            1   /* (non-indexed) */

typedef struct __attribute__((packed)) multiboot2_framebuffer_tag {
    multiboot2_info_tag_header_t header; /* type = 8 */
    uint64_t framebuffer_addr;           /* Physical address of the buffer */
    uint32_t framebuffer_pitch;          /* Bytes per row */
    uint32_t framebuffer_width;          /* Pixels */
    uint32_t framebuffer_height;         /* Pixels/lines */
    uint8_t  framebuffer_bpp;            /* Bits per pixel (0 = text mode) */
    uint8_t  framebuffer_type;           /* MULTIBOOT2_FB_TYPE_* */
    uint8_t  reserved;                   /* Always 0, must be ignored */
    /* If type == INDEXED: u32 palette_num_colors + color descriptors follow. */
} multiboot2_framebuffer_tag_t;

/* ── Parsed boot info (our convenience wrapper) ──────────────────────── */
typedef struct {
    /* Raw pointer GRUB passed in EBX */
    uint64_t mb_info_ptr;

    /* Tag type 4: basic memory info (KiB) */
    uint32_t mem_lower;
    uint32_t mem_upper;

    /* Tag type 6: memory map */
    const multiboot2_mmap_entry_t *mmap_entries;
    uint32_t mmap_entry_count;
    uint32_t mmap_entry_size;

    /* Tag type 3: first module = initrd (grub.cfg: `module /boot/initrd.img`) */
    uint64_t initrd_addr;           /* mod_start */
    uint64_t initrd_end;            /* mod_end (exclusive) */
    uint64_t initrd_size;           /* mod_end - mod_start */

    /* Tag type 8: framebuffer */
    uint64_t fb_addr;
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint8_t  fb_bpp;
    uint8_t  fb_type;

    /* Tag type 1: kernel command line */
    char     cmdline[1024];

    /* Tag type 2: boot loader name (e.g. "GRUB 2.04") */
    char     bootloader_name[64];
} trini_boot_info_t;

/* ── API ─────────────────────────────────────────────────────────────── */
void parse_multiboot2_info(uint64_t mb_info_ptr, trini_boot_info_t *info);
const char *mmap_type_name(uint32_t type);

#endif /* MULTIBOOT2_INFO_H */
