/*
 * enter64.asm
 * TriniOS x86_64 Kernel Entry - 64-bit portion + all shared data
 *
 * Assembled with: as --64
 *
 * Layout in the binary (GRUB scans first page for magic):
 *   [0]     Multiboot2 header (magic at offset 0)
 *   [+header_end] _start — entry point code
 *   ...kernel code...
 */

    .text

MULTIBOOT2_HEADER_MAGIC = 0x36722891
MULTIBOOT2_ARCH         = 0
MULTIBOOT2_CHECKSUM     = -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCH)

/* ── Multiboot2 Header (MUST be at offset 0 of the binary) ─────────── */
    .globl _start
_start:
    /* magic */
    .long   MULTIBOOT2_HEADER_MAGIC
    /* architecture */
    .long   MULTIBOOT2_ARCH
    /* header length (28 bytes = 16 base + 4+4+8 terminator) */
    .long   28
    /* checksum (-magic -arch) */
    .long   MULTIBOOT2_CHECKSUM

/* Terminator tag: type=0, flags=0, size=8 */
    .short  0              /* type: end */
    .short  0              /* flags */
    .long   8              /* size */

/* ── Entry Point (GRUB jumps here after finding multiboot header) ──── */
    .code32

    /* The multiboot2 spec says GRUB passes mb_info in EDI and flags in EAX.
     * We need to set up a stack first, then switch to 64-bit mode. */

    /* Set up a temporary stack */
    mov     $stack32_top, %esp

    /* Save multiboot info pointer for later use in 64-bit mode */
    mov     %edi, mb_info_ptr

    /* ── Enable PAE ─────────────────────────────────────────────── */
    mov     %cr4, %eax
    or      $0x20, %eax          /* Set PAE bit */
    mov     %eax, %cr4

    /* ── Load CR3 with page table base (physical address) ───────── */
    lea     page_table_base, %eax
    mov     %eax, %cr3

    /* ── Enable Long Mode via MSR_EFER ──────────────────────────── */
    mov     $0xC0000080, %ecx
    rdmsr
    or      $(1 << 8), %eax      /* Set LME (Long Mode Enable) bit */
    wrmsr

    /* ── Enable Paging and Protected Mode ───────────────────────── */
    mov     %cr0, %eax
    or      $0x80000001, %eax    /* Set PG and MP bits */
    mov     %eax, %cr0

    /* ── Far jump to 64-bit code segment ────────────────────────── */
    jmp     $0x08, $1f

1:
    .code64

    /* ── Set up 64-bit segment registers ────────────────────────── */
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     %ax, %fs
    mov     %ax, %gs

    /* ── Set up 64-bit stack ───────────────────────────────────── */
    mov     $stack64_top, %rsp

    /* ── Clear BSS ─────────────────────────────────────────────── */
    xor     %rax, %rax
    mov     $bss_start, %rdi
    mov     $bss_end, %rcx
    sub     %rdi, %rcx
    rep     stosb

    /* ── Call C kernel entry (mb_info_ptr is already saved) ─────── */
    call    kernel_main

    /* If kernel_main returns, halt */
.halt:
    cli
    hlt
    jmp     .halt

/* ── GDT - 64-bit (used in long mode) ───────────────────────────── */
    .globl gdt64_start
gdt64_start:
    .quad   0                    /* NULL descriptor */
    .word   0xFFFF               /* limit low */
    .word   0                    /* base low */
    .byte   0                    /* base middle */
    .byte   0xA8                   /* access (executable, code, conforming, DPL=0) */
    .byte   0x20                   /* page granular, 32-bit */
    .byte   0                    /* base high */
    .quad   0

.globl gdt64_end
gdt64_end:

    .globl gdt64_ptr
gdt64_ptr:
    .word   gdt64_end - gdt64_start - 1
    .quad   gdt64_start

/* ── Page Tables (identity map) ─────────────────────────────────── */
    .globl page_table_base
page_table_base:
    /* PML4E — entry 0 maps to PDPT at physical 0x2000 */
    .quad   0x2000 + 0x27        /* PDPT | P | R/W | U/S */
    .fill   511, 8, 0

page_table_pdpt:                 /* at physical 0x2000 */
    /* PDPTE — entry 0 maps to PD at physical 0x3000 */
    .quad   0x3000 + 0x27        /* PD | P | R/W | U/S */
    .fill   511, 8, 0

page_table_pd:                   /* at physical 0x3000 */
    /* PDE — entry 0 = 2MiB huge page at physical 0x000000 */
    .quad   0xA7                 /* P | R/W | U/S | Huge | PS=1 */
    .fill   511, 8, 0

/* ── Data: multiboot2 info parsed results ──────────────────────── */
    .data
    .align 8
    .globl mb_info_ptr
mb_info_ptr:
    .quad   0

    .globl initrd_load_addr
initrd_load_addr:
    .long   0
    .globl initrd_load_end
initrd_load_end:
    .long   0
    .globl initrd_load_size
initrd_load_size:
    .long   0

    .globl mmap_entry_count
mmap_entry_count:
    .long   0
    .globl mmap_entry_size
mmap_entry_size:
    .word   0
    .globl mmap_addr
mmap_addr:
    .long   0

    .globl video_mode
video_mode:
    .long   0
    .globl video_width
video_width:
    .long   0
    .globl video_height
video_height:
    .long   0
    .globl video_depth
video_depth:
    .long   0
    .globl video_addr
video_addr:
    .long   0

/* ── Stacks ───────────────────────────────────────────────────── */
    .bss
    .align 16
    .globl stack32_start
stack32_start:
    .space  4096
    .globl stack32_top
stack32_top:

    .globl stack64_start
stack64_start:
    .space  16384
    .globl stack64_top
stack64_top:

/* ── BSS markers ─────────────────────────────────────────────── */
    .globl bss_start
bss_start:
    .space 256
    .globl bss_end
bss_end:

/* ── Mark end of kernel image ──────────────────────────────── */
    .globl _end
_end:
    .quad  0

/* End of enter64.asm */