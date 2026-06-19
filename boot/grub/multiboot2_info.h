/*
 * multiboot2_info.h
 * TriniOS — Multiboot2 Info Structure Definitions
 *
 * Defines the C structures for parsing the Multiboot2 boot information
 * that GRUB2 passes to the kernel at entry.
 */

#ifndef MULTIBOOT2_INFO_H
#define MULTIBOOT2_INFO_H

#include <stdint.h>

/* ── Multiboot2 Magic Number ────────────────────────────────────────────── */
#define MULTIBOOT2_BOOTLOADER_MAGIC   0x36D76289

/* ── Tag Types ─────────────────────────────────────────────────────────── */
#define MULTIBOOT2_TAG_TYPE_REQUIRED_HEADER    1
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER        2
#define MULTIBOOT2_TAG_TYPE_MODULE             4
#define MULTIBOOT2_TAG_TYPE_ENTRY_ADDRESS      5
#define MULTIBOOT2_TAG_TYPE_LOAD_ADDRESS       6
#define MULTIBOOT2_TAG_TYPE_EFI_MMAP           7
#define MULTIBOOT2_TAG_TYPE_BASIC_MEM_INFO     8
#define MULTIBOOT2_TAG_TYPE_BOOTLOADER_NAME    9
#define MULTIBOOT2_TAG_TYPE_CMDLINE           10
#define MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME   11
#define MULTIBOOT2_TAG_TYPE_APMBIOS          12
#define MULTIBOOT2_TAG_TYPE_VBE              13
#define MULTIBOOT2_TAG_TYPE_ELF_SECTIONS     15
#define MULTIBOOT2_TAG_TYPE_APM              16
#define MULTIBOOT2_TAG_TYPE_EFI32            17
#define MULTIBOOT2_TAG_TYPE_EFI64            18
#define MULTIBOOT2_TAG_TYPE_SERIALIZATION    19
#define MULTIBOOT2_TAG_TYPE_MEMORY_MAP       20
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER_INFO 21
#define MULTIBOOT2_TAG_TYPE_LOAD_BASE_ADDR   25

/* ── Generic Tag Header ───────────────────────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_tag {
    uint16_t type;     /* Tag type */
    uint16_t flags;    /* Tag flags */
    uint32_t size;     /* Total tag size including header and alignment */
} multiboot2_tag_t;

/* ── Terminator Tag ───────────────────────────────────────────────────── */
#define MULTIBOOT2_TAG_TYPE_END  0

/* ── Total Size Tag (first tag, always present) ────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_total_size_tag {
    uint16_t type;     /* 0 (total size) */
    uint16_t flags;
    uint32_t size;     /* 8 */
    uint32_t total_size; /* Total size of multiboot info */
} multiboot2_total_size_tag_t;

/* ── Memory Map Tag (type=20) ─────────────────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_mmap_entry {
    uint64_t addr;        /* Base address */
    uint64_t len;         /* Length */
    uint32_t type;        /* Memory type */
    uint32_t zero;        /* Reserved */
} multiboot2_mmap_entry_t;

/* Memory map types */
#define MULTIBOOT2_MMAP_USABLE            1
#define MULTIBOOT2_MMAP_RESERVED          2
#define MULTIBOOT2_MMAP_ACPI_RECLAIMABLE  3
#define MULTIBOOT2_MMAP_ACPI_NVS          4
#define MULTIBOOT2_MMAP_BAD_MEMORY        5

typedef struct __attribute__((packed)) multiboot2_mmap_tag {
    uint16_t type;           /* 20 */
    uint16_t flags;
    uint32_t size;
    uint32_t entry_size;     /* Size of each mmap entry */
    uint32_t entry_version;  /* Always 1 */
    multiboot2_mmap_entry_t entries[]; /* Variable length array */
} multiboot2_mmap_tag_t;

/* ── Basic Memory Info Tag (type=8) ───────────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_basic_mem_tag {
    uint16_t type;   /* 8 */
    uint16_t flags;
    uint32_t size;   /* 16 */
    uint32_t mem_lower; /* Below 1MB */
    uint32_t mem_upper; /* Above 1MB */
} multiboot2_basic_mem_tag_t;

/* ── Boot Loader Name Tag (type=11) ───────────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_bootloader_name_tag {
    uint16_t type;  /* 11 */
    uint16_t flags;
    uint32_t size;
    char name[];   /* Null-terminated string */
} multiboot2_bootloader_name_tag_t;

/* ── Command Line Tag (type=10) ───────────────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_cmdline_tag {
    uint16_t type;  /* 10 */
    uint16_t flags;
    uint32_t size;
    char cmdline[]; /* Null-terminated string */
} multiboot2_cmdline_tag_t;

/* ── Module Tag (type=4) — initrd loaded as module ────────────────────── */
typedef struct __attribute__((packed)) multiboot2_module_tag {
    uint16_t type;     /* 4 */
    uint16_t flags;
    uint32_t size;
    uint64_t mod_start; /* Module start address */
    uint64_t mod_end;   /* Module end address */
    uint64_t cmdline;   /* Pointer to command line string */
    uint64_t reserved;
} multiboot2_module_tag_t;

/* ── Framebuffer Info Tag (type=21) ───────────────────────────────────── */
typedef struct __attribute__((packed)) multiboot2_framebuffer_tag {
    uint16_t type;      /* 21 */
    uint16_t flags;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_type;   /* 0=text, 1=EGAVGA, 2=indexed, 3=RGB, 4=gray */
    uint8_t  framebuffer_palette_size;
    uint16_t framebuffer_palette_flags;
    uint8_t  framebuffer_red_field_position;
    uint8_t  framebuffer_red_mask_size;
    uint8_t  framebuffer_green_field_position;
    uint8_t  framebuffer_green_mask_size;
    uint8_t  framebuffer_blue_field_position;
    uint8_t  framebuffer_blue_mask_size;
} multiboot2_framebuffer_tag_t;

/* ── Parsed Boot Info (our convenience wrapper) ────────────────────────── */
typedef struct {
    /* Raw multiboot2 info pointer */
    uint64_t mb_info_ptr;

    /* Memory info */
    uint32_t mem_lower;
    uint32_t mem_upper;

    /* Memory map */
    multiboot2_mmap_entry_t *mmap_entries;
    uint32_t mmap_entry_count;
    uint32_t mmap_entry_size;

    /* Initrd (loaded as module) */
    uint64_t initrd_addr;
    uint64_t initrd_end;
    uint64_t initrd_size;

    /* Framebuffer */
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t  fb_type;
    uint8_t  fb_depth;

    /* Kernel command line */
    char     cmdline[1024];

    /* Bootloader name */
    char     bootloader_name[256];
} trini_boot_info_t;

/* ── API ──────────────────────────────────────────────────────────────── */
void parse_multiboot2_info(uint64_t mb_info_ptr, trini_boot_info_t *info);
const char *mmap_type_name(uint32_t type);

#endif /* MULTIBOOT2_INFO_H */