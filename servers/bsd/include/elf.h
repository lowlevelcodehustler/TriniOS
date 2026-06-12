/*
 * TriniOS BSD Compatibility Server - ELF Binary Loading
 *
 * ELF parsing and loading for execve() implementation.
 * Supports ELF64 x86_64 binaries.
 */

#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <mach/mach.h>
#include <mach/vm_map.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * ELF Constants
 * ============================================================================ */

#define ELF_MAGIC     0x7F454C46
#define ELFCLASS32    1
#define ELFCLASS64    2
#define ELFDATA2LSB   1
#define ELFDATA2MSB   2
#define EV_CURRENT    1
#define ET_EXEC       2
#define ET_DYN        3
#define EM_386        3
#define EM_X86_64     62
#define PT_LOAD       1
#define PT_DYNAMIC    2
#define PT_INTERP     3
#define PT_PHDR       6
#define PF_X          0x1
#define PF_W          0x2
#define PF_R          0x4
#define DT_NULL       0
#define DT_NEEDED     1
#define DT_STRTAB     5
#define DT_STRSZ      10
#define DT_SYMTAB     6
#define DT_SYMENT     4
#define DT_RPATH      15
#define DT_RUNPATH    29
#define DT_FLAGS      30
#define DT_SONAME     14
#define DT_DEBUG      21
#define DT_TEXTREL    26
#define DF_BIND_NOW   0x8
#define SHT_NULL      0
#define SHT_PROGBITS  1
#define SHT_SYMTAB    2
#define SHT_STRTAB    3
#define SHT_RELA      4
#define SHT_HASH      5
#define SHT_DYNAMIC   6
#define SHT_NOTE      7
#define SHT_NOBITS    8
#define SHT_REL       9
#define SHT_DYNSYM    11
#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4

/* ============================================================================
 * ELF Header Structures (64-bit)
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
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
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

typedef struct {
    uint32_t      sh_name;
    uint32_t      sh_type;
    uint64_t      sh_flags;
    uint64_t      sh_addr;
    uint64_t      sh_offset;
    uint64_t      sh_size;
    uint32_t      sh_link;
    uint32_t      sh_info;
    uint64_t      sh_addralign;
    uint64_t      sh_entsize;
} elf64_shdr_t;

typedef struct {
    uint32_t      st_name;
    uint8_t       st_info;
    uint8_t       st_other;
    uint16_t      st_shndx;
    uint64_t      st_value;
    uint64_t      st_size;
} elf64_sym_t;

typedef struct {
    uint64_t      d_tag;
    union {
        uint64_t  d_val;
        uint64_t  d_ptr;
    } d_un;
} elf64_dyn_t;

/* ============================================================================
 * ELF Load Context
 * ============================================================================ */

typedef struct {
    vm_offset_t       file_data;
    vm_size_t         file_size;
    elf64_header_t   *header;
    elf64_phdr_t     *phdrs;
    char             *strtab;
    vm_offset_t       load_base;
    vm_offset_t       entry;
    char             *interp_path;
    bool              is_dynamic;
    char            **needed_libs;
    int               needed_count;
} elf_load_context_t;

/* ============================================================================
 * ELF Loader Functions
 * ============================================================================ */

kern_return_t elf_load(const char *path, elf_load_context_t *ctx);
kern_return_t elf_load_from_memory(vm_offset_t data, vm_size_t size, elf_load_context_t *ctx);
kern_return_t elf_parse_header(vm_offset_t data, vm_size_t size, elf_load_context_t *ctx);
kern_return_t elf_map_segments(elf_load_context_t *ctx);
kern_return_t elf_set_protections(elf_load_context_t *ctx);
kern_return_t elf_parse_dynamic(elf_load_context_t *ctx);
kern_return_t elf_get_interpreter(elf_load_context_t *ctx, char *buf, uint32_t bufsz);
void elf_unload(elf_load_context_t *ctx);
bool elf_is_valid(vm_offset_t data, vm_size_t size);
uint8_t elf_get_class(vm_offset_t data);
uint16_t elf_get_machine(vm_offset_t data);

#endif /* ELF_LOADER_H */