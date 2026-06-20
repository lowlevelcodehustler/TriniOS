/*
 * elf_loader.h — Kernel-side ELF64 x86_64 binary loader
 *
 * Loads ET_EXEC and ET_DYN (PIE) binaries from memory into kernel VM space.
 * Used by create_first_task() to load user-space programs from the initrd.
 *
 * This is a freestanding implementation — no libc, no malloc.
 */

#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * ELF Constants (matching servers/bsd/include/elf.h)
 * ============================================================================ */

#define ELF_MAGIC     0x7F454C46
#define ELFCLASS64    2
#define ELFDATA2LSB   1
#define ET_EXEC       2
#define ET_DYN        3
#define EM_X86_64     62
#define PT_LOAD       1
#define PF_X          0x1
#define PF_W          0x2
#define PF_R          0x4

/* ============================================================================
 * ELF Structures (same as servers/bsd/include/elf.h)
 * ============================================================================ */

typedef struct {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
} elf64_header_t;

typedef struct {
    uint32_t      p_type;
    uint32_t      p_flags;
    uint64_t      p_offset;
    uint64_t      p_vaddr;
    uint64_t      p_paddr;
    uint64_t      p_filesz;
    uint64_t      p_memsz;
    uint64_t      p_align;
} elf64_phdr_t;

/* ============================================================================
 * ELF Load Result (returned by elf_load_from_memory)
 * ============================================================================ */

typedef struct {
    uint64_t entry_point;   /* Entry point address to jump to */
    bool     is_pie;        /* True if ET_DYN (PIE) binary */
} elf_load_result_t;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * Validate ELF magic bytes and basic format.
 * @param data  Pointer to ELF file data in memory
 * @param size  Size of the ELF file in bytes
 * @return      true if valid ELF64 x86_64 ET_EXEC or ET_DYN binary
 */
bool elf_is_valid(const void *data, uint64_t size);

/**
 * Parse ELF header and program headers.
 * @param data       Pointer to ELF file data in memory
 * @param size       Size of the ELF file in bytes
 * @param result     Output: load result (entry point, PIE flag)
 * @return           true on success
 */
bool elf_parse(const void *data, uint64_t size, elf_load_result_t *result);

/**
 * Find a file by path in the initrd tree and return its data+size.
 * This is a wrapper around initrd_entry_find_file().
 * @param root       Root of the initrd filesystem tree
 * @param path       Full path (e.g., "/bin/hello")
 * @param out_data   Output: pointer to file data
 * @param out_size   Output: size in bytes
 * @return           true if found
 */
bool elf_find_in_initrd(void *root, const char *path,
                        const void **out_data, uint64_t *out_size);

#endif /* ELF_LOADER_H */