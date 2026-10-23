/*
 * enter64.asm
 * TriniOS x86_64 Kernel Entry - 64-bit portion + all shared data
 *
 * Assembled with: as --64
 *
 * Layout in the binary (GRUB scans first page for magic):
 *   [0]     Multiboot2 header (magic at offset 0) + address/entry tags
 *   [+header_end] _start — entry point code
 *   ...kernel code...
 */

    .text

/* Official GRUB Multiboot2 header magic per spec v2.0 (0xE85250D6).
 * NOTE: this is NOT 0xE8525006 — that value was a typo in an earlier
 * revision and makes GRUB reject the image entirely. */
MULTIBOOT2_HEADER_MAGIC = 0xE85250D6

/* Multiboot2 header tag types (spec v2.0, section 3.1):
 *   type 2 = address tag: header_addr/load_addr/load_end_addr/bss_end_addr
 *   type 3 = entry address tag: entry_addr */
MB2_TAG_ADDRESS         = 2
MB2_TAG_ENTRY_ADDR      = 3

/* Kernel base load address — must match BASE_ADDRESS in kernel.ld.
 * The image is a flat binary with absolute addresses, so GRUB MUST load it
 * here; the address header tag below makes that requirement explicit and
 * spec-mandated ("when the address tag is present it must be used"). */
KERNEL_BASE             = 0x100000

/* Total header length: base(16) + address_tag(24) + entry_tag(16) + terminator(8) */
MB2_HEADER_LEN          = 64

    .globl _start
_start:
    /* ── Multiboot2 Header (MUST be at offset 0 of the binary) ───────
     * Per GRUB Multiboot2 spec v2.0:
     *   [0..3]    magic       = 0xE85250D6
     *   [4..7]    architecture = 0 (i386 protected mode)
     *   [8..11]   header_len  = total size of header including all tags
     *   [12..15]  checksum    = -(magic + arch + hdr_len) mod 2^32
     * After the base header come the address tag (type 2), the entry
     * address tag (type 3) and a terminator tag. */
    .long   MULTIBOOT2_HEADER_MAGIC       /* magic — spec v2.0 value */
    .long   0                             /* architecture = 0 (i386 protected mode) */
    .long   MB2_HEADER_LEN                /* total header length incl. tags */
    .long   -(MULTIBOOT2_HEADER_MAGIC + 0 + MB2_HEADER_LEN)

/* ── Address tag (type 2): pin the image at KERNEL_BASE ────────────────
 * load_end_addr = 0 → the whole file is loaded; bss_end_addr = _end →
 * GRUB zeros/reserves our BSS region. */
    .short  MB2_TAG_ADDRESS               /* type = 2 */
    .short  0                             /* flags */
    .long   24                            /* size (8 header + 16 data) */
    .long   KERNEL_BASE                   /* header_addr — where the magic lands */
    .long   KERNEL_BASE                   /* load_addr — start of text segment */
    .long   0                             /* load_end_addr: 0 = whole file loaded */
    .long   _end                          /* bss_end_addr (forward reference) */

/* ── Entry address tag (type 3): tell GRUB exactly where to jump ─────── */
    .short  MB2_TAG_ENTRY_ADDR            /* type = 3 */
    .short  0                             /* flags */
    .long   16                            /* size (8 header + 8 data) */
    .long   _start                        /* entry_addr — forward self-reference */

/* ── Terminator tag: type=0, flags=0, size=8 ─────────────────────────── */
    .short  0                             /* type: end of header */
    .short  0                             /* flags */
    .long   8                             /* size */

/* ── Entry Point (GRUB jumps here after finding the multiboot2 header) ─ */
    .code32

    /* Multiboot2 machine state at entry:
     *   EAX = 0x36D76289 — magic ("bootloader passed multiboot2 info")
     *   EBX = physical address of the boot information structure */

    /* Verify the magic; if we were not booted via multiboot2 there is
     * nothing sensible to do (no console yet). */
    cmp     $0x36D76289, %eax
    jne     .bad_boot

    /* Set up a temporary stack */
    mov     $stack32_top, %esp

    /* Save the multiboot info pointer for later use in 64-bit mode.
     * The spec passes it in EBX (NOT EDI). The structure always lives
     * below 4 GiB and mb_info_ptr is zero-initialized, so a 32-bit store
     * leaves the upper half at zero. */
    mov     %ebx, mb_info_ptr

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

    /* ── Far jump to 64-bit code segment (still on GRUB's GDT) ──── */
    jmp     $0x08, $1f

.bad_boot:
    cli
    hlt
    jmp     .bad_boot

1:
    /* Still 32-bit here! The CPU only switches to 64-bit mode when CS is
     * reloaded from a long-mode descriptor — which happens on the far jump
     * below. This instruction must therefore be assembled in .code32: GAS
     * rejects immediate-segment far jumps under .code64, and encoding it as
     * 64-bit would be wrong anyway (the executing CPU is still in
     * compatibility mode until CS changes). */

    /* ── Load our own GDT so we stop depending on GRUB's segment table.
     * Until now CS/SS came from whatever GDT GRUB left loaded; after the
     * far jump below all selectors refer to gdt64_start. */
    lgdt    gdt64_ptr
    jmp     $0x08, $2f            /* Reload CS from our GDT (long-mode descriptor) */

.code64
2:
    /* ── Set up 64-bit segment registers from our own GDT ─────────── */
    movw    $0x10, %ax            /* kernel data selector */
    mov     %ax, %ds
    mov     %ax, %es
    movw    $0x10, %ss            /* load SS directly (DPL 0 == CPL); explicit w: immediate size is ambiguous for segment-register destinations */
    mov     %ax, %fs
    mov     %ax, %gs

    /* ── Load TSS (all-allow I/O bitmap) so ring-3 code may use
     * out/in instructions without #GP. LTR takes a register or memory —
     * not an immediate. ─────────────────────────────────────────────── */
    movw    $0x28, %cx
    ltr     %cx

    /* ── Set up 64-bit stack ───────────────────────────────────── */
    mov     $stack64_top, %rsp

    /* ── Clear BSS. bss_start/bss_end are defined by kernel.ld around
     * the ENTIRE .bss section (stacks + C globals), so this zeroes all
     * of it — not just a fixed 256-byte block as an earlier revision did. */
    xor     %rax, %rax
    mov     $bss_start, %rdi
    mov     $bss_end, %rcx
    sub     %rdi, %rcx
    rep     stosb

    /* ── Call C kernel entry, passing mb_info_ptr (System V AMD64:
     * first argument in RDI) ─────────────────────────────────────── */
    mov     mb_info_ptr, %rdi
    call    kernel_main

    /* If kernel_main returns, halt */
.halt:
    cli
    hlt
    jmp     .halt

/* ── GDT — our own 64-bit segment table (loaded with LGDT above) ───────
 * Selector layout:
 *   0x00  null descriptor
 *   0x08  kernel code (DPL 0, long mode)
 *   0x10  kernel data (DPL 0)
 *   0x18  user code (DPL 3)
 *   0x20  user data (DPL 3)
 *   0x28  TSS descriptor (all-allow I/O bitmap for ring-3 out/in)
 */
    .data
    .align 16
.globl gdt64_start
gdt64_start:
    /* [0] null descriptor */
    .quad   0
    /* [1] kernel code: base 0, limit 4 GiB, DPL 0, long mode (L=1) */
    .long   0                   /* base[31:0] */
    .word   0xFFFF              /* limit[15:0] */
    .byte   0x9A                /* access: P=1 DPL=0 S=1 code+readable */
    .byte   0xF8                /* limit[23:16]=0xF, G=1 (page granularity) */
    .byte   0                   /* base[39:32] */
    /* [2] kernel data: base 0, limit 4 GiB, DPL 0, writable */
    .long   0
    .word   0xFFFF
    .byte   0x92                /* access: P=1 DPL=0 S=1 W=1 */
    .byte   0xF8
    .byte   0
    /* [3] user code: base 0, limit 4 GiB, DPL 3, long mode (L=1) */
    .long   0
    .word   0xFFFF
    .byte   0xFA                /* access: P=1 DPL=3 S=1 code+readable */
    .byte   0xF8
    .byte   0
    /* [4] user data: base 0, limit 4 GiB, DPL 3, writable */
    .long   0
    .word   0xFFFF
    .byte   0xF2                /* access: P=1 DPL=3 S=1 W=1 */
    .byte   0xF8
    .byte   0
    /* [5] TSS descriptor: base = tss_struct, limit covers fields + bitmap */
    .long   tss_struct          /* base[31:0] (forward reference) */
    .word   0x7B                /* limit = 123 (TSS fields end at 108, +16-byte bitmap) */
    .byte   0x89                /* access: P=1 DPL=0 S=0 type=9 (available TSS) */
    .byte   0x00                /* limit[23:16]=0, G=0 (byte granularity) */
    .byte   0                   /* base[39:32] */
.globl gdt64_end
gdt64_end:

.globl gdt64_ptr
gdt64_ptr:
    .word   gdt64_end - gdt64_start - 1
    .quad   gdt64_start

/* ── TSS — x86_64 task state segment (only the I/O bitmap is used) ─────
 * The all-allow bitmap lets ring-3 programs use out/in instructions
 * (COM1/VGA) without #GP. Ports covered by the 16-byte bitmap have their
 * bits cleared (= allow); ports beyond it are allowed unconditionally. */
tss_struct:
    .quad   0                   /* offset 0:  prev_ss (unused in long mode) */
    .quad   0                   /* offset 8:  RSP0 (no interrupts yet) */
    .quad   0                   /* offset 16: RSP1 */
    .quad   0                   /* offset 24: RSP2 */
    .fill   56, 1, 0           /* offset 32-87: IST[7] (unused) */
    .fill   16, 1, 0           /* offset 88-103: reserved */
    .word   108                 /* offset 104: I/O map base offset → bitmap below */
    .word   0                   /* offset 106: reserved */
tss_io_bitmap:                  /* offset 108 within the TSS */
    .fill   16, 1, 0           /* all-allow (zero bits = allow) */

/* ── Page Tables (identity map) ────────────────────────────────────────
 * Phase A (static — used to enter long mode and run the setup code):
 *   PML4[0] → PDPT[0] → PD[0] = a single 2 MiB huge page covering 0–2 MiB.
 * Phase B (built by kernel_main.c before entering ring 3):
 *   PD[0] is replaced with pt_low — per-page PTEs for the first 2 MiB:
 *     - kernel image pages [0x100000, _end) → supervisor-only (U/S=0)
 *     - VGA text buffer page 0xB8000        → user+kernel RW (hello writes it)
 *   PD[8] is set to pt_hi — per-page PTEs for [0x1000000, 0x1200000):
 *     - program pages (hello links at 0x1000000), user stack page and the
 *       ring-3 trampoline page → user RWX.
 * All tables live inside the kernel image (identity-mapped), so they stay
 * validly mapped after phase B. Pointers use real symbol addresses — never
 * hardcode physical addresses here (an earlier revision pointed at 0x2000/
 * 0x3000, where no tables exist). */

    /* Each table is kept 16-byte aligned so its low bits are zero — which
     * makes '+ 0x3' exactly equal to '| 0x3' for the P|R/W flags. GAS cannot
     * emit a relocation for 'section_symbol | constant' (relocations are
     * linear: sym + addend), but 'sym + 0x3' is a plain R_X86_64_64 and
     * links fine. */
    .globl page_table_base
.align 16
page_table_base:
    /* PML4E[0] → PDPT (real address; virtual == physical in the identity map) */
    .quad   page_table_pdpt + 0x3   /* P|R/W — supervisor only */
    .fill   511, 8, 0

.align 16
page_table_pdpt:                /* covers virtual 0–512 GiB; entry 0 → PD */
    .quad   page_table_pd + 0x3     /* P|R/W */
    .fill   511, 8, 0

.align 16
page_table_pd:                  /* covers virtual 0–1 GiB */
    .quad   0xA7                    /* phase A: 2 MiB huge page at physical 0 (P|RW|US|PS) */
    .fill   511, 8, 0              /* entry [8] is set by kernel_main.c → pt_hi */

/* Phase B tables — zero-initialized in .bss; filled by
 * setup_user_page_tables() in kernel_main.c before the ring-3 jump. */
    .bss
    .align 4096
.globl pt_low
pt_low:                         /* PTEs for virtual 0–2 MiB (512 entries) */
    .space  4096
.globl pt_hi
pt_hi:                          /* PTEs for virtual 0x1000000–0x1200000 (512 entries) */
    .space  4096

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

/* NOTE: the _end symbol is intentionally NOT defined here. kernel.ld assigns
 * `_end = .` at the true end of ALL sections (including C objects' BSS), and
 * that value is what setup_user_page_tables() in kernel_main.c uses to map
 * every kernel page. An object-local definition would shadow the linker-script
 * one and point past only this file's own BSS. */

/* End of enter64.asm */
