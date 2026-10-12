/*
 * elf_loader.c — Kernel-side ELF64 x86_64 binary loader (freestanding)
 *
 * Loads ET_EXEC and ET_DYN (PIE) binaries from memory. Used by
 * create_first_task() to load user-space programs from the initrd tree.
 *
 * This is a freestanding implementation — no libc, no malloc.
 * Uses memcpy/memset from kernel_freestanding_stubs.h or inline assembly.
 */

#include "elf_loader.h"
#include "initrd_loader.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Inline string operations (freestanding — no libc strchr/strlen)
 * ============================================================================ */

static const char *elf_strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return s;
        s++;
    }
    return NULL;
}

static size_t elf_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int elf_strncmp(const char *a, const char *b, size_t n)
{
    if (!n) return 0;
    while (*a && *b && n > 1 && *a == *b) { a++; b++; n--; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static void *elf_memcpy(void *dst, const void *src, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint64_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

static void *elf_memset(void *dst, int val, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (uint64_t i = 0; i < n; i++) {
        d[i] = (uint8_t)val;
    }
    return dst;
}

/* ============================================================================
 * elf_is_valid — Check ELF magic bytes and basic format
 * ============================================================================ */

bool elf_is_valid(const void *data, uint64_t size)
{
    if (size < sizeof(elf64_header_t)) {
        return false;
    }

    const unsigned char *e_ident = (const unsigned char *)data;

    /* e_ident[0..3] must be 0x7F 'E' 'L' 'F' */
    if (e_ident[0] != 0x7F || e_ident[1] != 'E' ||
        e_ident[2] != 'L' || e_ident[3] != 'F') {
        return false;
    }

    /* e_ident[4] must be ELFCLASS64 */
    if (e_ident[4] != ELFCLASS64) {
        return false;
    }

    /* e_ident[5] must be ELFDATA2LSB */
    if (e_ident[5] != ELFDATA2LSB) {
        return false;
    }

    return true;
}

/* ============================================================================
 * elf_parse — Parse ELF header and compute entry point
 *
 * Strategy:
 *   1. Validate ELF magic + format
 *   2. Check it's x86_64 ET_EXEC or ET_DYN
 *   3. Walk program headers to find PT_LOAD segments
 *   4. Compute the actual load address and entry point
 *
 * NOTE: This version does NOT map segments into VM — that requires kernel
 * VM primitives (vm_allocate, vm_protect). For now, we just parse and return
 * the entry point for use by create_first_task().
 * ============================================================================ */

bool elf_parse(const void *data, uint64_t size, elf_load_result_t *result)
{
    if (!result || !data || size < sizeof(elf64_header_t)) {
        return false;
    }

    /* Validate ELF magic */
    if (!elf_is_valid(data, size)) {
        return false;
    }

    const elf64_header_t *hdr = (const elf64_header_t *)data;

    /* Check x86_64 architecture */
    if (hdr->e_machine != EM_X86_64) {
        return false;
    }

    /* Check it's an executable or shared object */
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
        return false;
    }

    /* Check we have program headers */
    if (hdr->e_phnum == 0 || hdr->e_phentsize < sizeof(elf64_phdr_t)) {
        return false;
    }

    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)((const char *)data + hdr->e_phoff);

    /* Validate phdr offset doesn't exceed file size */
    if (hdr->e_phoff + ((uint64_t)hdr->e_phnum * hdr->e_phentsize) > size) {
        return false;
    }

    /* Walk program headers to find PT_LOAD segments */
    uint64_t load_base = 0;
    bool first_load = true;

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = &phdrs[i];

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        /* First PT_LOAD segment determines the load base */
        if (first_load) {
            if (hdr->e_type == ET_EXEC) {
                /* Fixed address: use p_vaddr directly */
                load_base = phdr->p_vaddr;
            } else {
                /* PIE: loaded at the fixed user-space base used by
                 * elf_load_segments() — keep both in sync. */
                load_base = TRINI_USER_LOAD_BASE;
            }
            first_load = false;
        }
    }

    /* Compute entry point */
    uint64_t entry = hdr->e_entry;
    if (load_base != 0 && hdr->e_type == ET_EXEC) {
        /* For ET_EXEC, e_entry is already an absolute address */
        entry = hdr->e_entry;
    } else if (hdr->e_type == ET_DYN) {
        /* For PIE, e_entry is relative to load base */
        entry = load_base + hdr->e_entry;
    }

    result->entry_point = entry;
    result->is_pie = (hdr->e_type == ET_DYN);

    return true;
}

/* ============================================================================
 * elf_find_in_initrd — Find a file in the initrd tree by path
 *
 * Walks the initrd filesystem tree to find a regular file at the given path.
 * Returns a pointer to the file data and its size.
 * ============================================================================ */

bool elf_find_in_initrd(void *root, const char *path,
                        const void **out_data, uint64_t *out_size)
{
    if (!root || !path || !out_data || !out_size) {
        return false;
    }

    /* Navigate the initrd tree to find the file at the given path.
     * The initrd_entry_t structure has:
     *   - name: filename
     *   - children: linked list of child entries
     *   - sibling: next sibling entry
     *   - data: pointer to file contents (for regular files)
     *   - size: file size in bytes
     */

    initrd_entry_t *entry = (initrd_entry_t *)root;
    const char *slash = path;
    const char *component_start = path + 1; /* Skip leading '/' */

    while (*slash) {
        /* Find next component (path segment between slashes) */
        const char *next_slash = elf_strchr(slash, '/');
        uint64_t comp_len = next_slash ? (size_t)(next_slash - slash - 1) : elf_strlen(component_start);

        if (comp_len == 0 && !next_slash) {
            break; /* End of path */
        }

        /* Search children for matching component name */
        initrd_entry_t *found = NULL;
        initrd_entry_t *child = entry->children;

        while (child) {
            if (elf_strlen(child->name) == comp_len &&
                elf_strncmp(child->name, component_start, comp_len) == 0) {
                found = child;
                break;
            }
            child = child->sibling;
        }

        if (!found) {
            return false; /* Path not found */
        }

        entry = found;

        if (next_slash) {
            slash = next_slash;
            component_start = next_slash + 1;
        } else {
            break; /* Last component */
        }
    }

    /* Check if this is a regular file (mode & S_IFREG == 0100000) */
    if ((entry->mode & 0170000) != 0100000 || !entry->data || entry->size == 0) {
        return false; /* Not a regular file or no data */
    }

    *out_data = entry->data;
    *out_size = entry->size;

    return true;
}

/* ============================================================================
 * PT_LOAD segment collection (shared by elf_segment_range / elf_load_segments)
 * ===========================================================================*/

#define MAX_LOAD_SEGMENTS 32

struct load_seg {
    uint64_t vstart;   /* virtual start address (absolute) */
    uint64_t vsize;    /* p_memsz */
    uint64_t foff;     /* file offset of segment data */
    uint64_t fsz;      /* p_filesz (bytes present in the file) */
};

/* Collect all PT_LOAD segments. Returns count, or -1 on error. */
static int elf_collect_loads(const void *data, uint64_t size,
                             struct load_seg *segs)
{
    if (!data || size < sizeof(elf64_header_t)) {
        return -1;
    }

    const elf64_header_t *hdr = (const elf64_header_t *)data;

    if (!elf_is_valid(data, size) || hdr->e_machine != EM_X86_64) {
        return -1;
    }
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
        return -1;
    }
    if (hdr->e_phnum == 0 || hdr->e_phentsize < sizeof(elf64_phdr_t)) {
        return -1;
    }

    const char *file = (const char *)data;
    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)(file + hdr->e_phoff);

    /* Validate phdr table bounds */
    if (hdr->e_phoff + ((uint64_t)hdr->e_phnum * hdr->e_phentsize) > size) {
        return -1;
    }

    bool is_pie = (hdr->e_type == ET_DYN);
    uint64_t base = is_pie ? TRINI_USER_LOAD_BASE : 0;

    int n = 0;
    for (uint16_t i = 0; i < hdr->e_phnum && n < MAX_LOAD_SEGMENTS; i++) {
        const elf64_phdr_t *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        /* File offset must be inside the image */
        if (phdr->p_offset + phdr->p_filesz > size) {
            return -1;
        }

        segs[n].vstart = base + phdr->p_vaddr;
        segs[n].vsize  = phdr->p_memsz;
        segs[n].foff   = phdr->p_offset;
        segs[n].fsz    = phdr->p_filesz;
        n++;
    }

    return (n > 0) ? n : -1;
}

/* ============================================================================
 * elf_segment_range — virtual range covered by all PT_LOAD segments
 * ===========================================================================*/

bool elf_segment_range(const void *data, uint64_t size,
                       uint64_t *out_start, uint64_t *out_end)
{
    if (!out_start || !out_end) {
        return false;
    }

    struct load_seg segs[MAX_LOAD_SEGMENTS];
    int n = elf_collect_loads(data, size, segs);
    if (n < 0) {
        return false;
    }

    uint64_t start = segs[0].vstart;
    uint64_t end   = segs[0].vstart + segs[0].vsize;

    for (int i = 1; i < n; i++) {
        if (segs[i].vstart < start) start = segs[i].vstart;
        uint64_t seg_end = segs[i].vstart + segs[i].vsize;
        if (seg_end > end) end = seg_end;
    }

    /* Everything must fit inside the mapped user region */
    if (start < TRINI_USER_LOAD_BASE || end > TRINI_USER_REGION_END) {
        return false;
    }

    *out_start = start;
    *out_end   = end;
    return true;
}

/* ============================================================================
 * elf_load_segments — copy PT_LOAD segments into their virtual addresses
 *
 * The target pages must already be mapped (setup_user_page_tables()).
 * ===========================================================================*/

bool elf_load_segments(const void *data, uint64_t size)
{
    struct load_seg segs[MAX_LOAD_SEGMENTS];
    int n = elf_collect_loads(data, size, segs);
    if (n < 0) {
        return false;
    }

    const uint8_t *file = (const uint8_t *)data;

    for (int i = 0; i < n; i++) {
        /* Target must be inside the mapped user region */
        if (segs[i].vstart < TRINI_USER_LOAD_BASE ||
            segs[i].vstart + segs[i].vsize > TRINI_USER_REGION_END) {
            return false;
        }

        uint8_t *dst = (uint8_t *)segs[i].vstart;

        /* Copy file-backed bytes */
        for (uint64_t j = 0; j < segs[i].fsz; j++) {
            dst[j] = file[segs[i].foff + j];
        }
        /* Zero-fill the BSS tail (p_filesz < p_memsz) */
        for (uint64_t j = segs[i].fsz; j < segs[i].vsize; j++) {
            dst[j] = 0;
        }
    }

    return true;
}
