/*
 * boot_entry32.asm
 * TriniOS x86_64 Kernel Entry Point - 32-bit portion
 *
 * Assembled with: as --32 (matching GRUB2's 32-bit protected mode handoff)
 *
 * This file provides two things:
 *   1. The Multiboot2 header that GRUB2 scans for at load time
 *   2. A stub entry point (_start) that jumps to the real kernel entry
 */

    .text
    .code32
    .globl _start

/* ── Multiboot2 Header (must be first for GRUB2 to find) ─────────────── */
    .section .multiboot_header,"a"
    .align 8

    /* total header size: magic(4) + arch(4) + hdr_len(4) = 12 bytes before tags
     * Entry tag (type=5): type(2) + flags(2) + size(4) + data(16) = 24 bytes
     * Terminator (type=0): type(2) + flags(2) + size(4) = 8 bytes
     * Total header_length = 12 + 24 + 8 = 44, rounded to 48 for alignment */
    .long   48                  /* total_header_size */

    /* magic = 0xE8525006 for Multiboot2 */
    .long   0xE8525006          /* magic */

    /* architecture = 0 (i386 protected mode) */
    .long   0                   /* architecture */

    /* checksum = -(magic + header_length + reserved) mod 2^32, where reserved=0 */
    /* -(0xE8525006 + 48 + 0) = -0xE8525038 = 0x17ADAFE1 (mod 2^32) */
    .long   0x17ADAFE1          /* checksum */

/* ── Tag 1: Entry Address Tag (type=5) ──────────────────────────────── */
    .short  5                   /* tag_type = ENTRY_ADDRESS_TAG */
    .short  0                   /* tag_flags */
    .long   24                  /* tag_size (8 header + 16 data) */
    .long   0                   /* header_addr_hi */
    .long   0                   /* load_addr_hi */
    /* entry_addr: use two .long for low/high 32-bit in --32 mode.
     * _start_64 will be at ~0x100000+ after linking (BASE_ADDRESS = 0x100000).
     * Low 32 bits of 0x100000+ = 0x000xxxxx */
    .long   _start_64           /* entry_addr low 32 bits */

/* ── Tag 0: Terminator ─────────────────────────────────────────────── */
    .short  0                   /* tag_type = TERMINAL_TAG */
    .short  0                   /* tag_flags */
    .long   8                   /* tag_size */

    .previous

/* External symbols defined in enter64.asm */
    .extern _start_64           /* renamed to avoid conflict with boot_entry32's _start */
    .extern mb_info_ptr
    .extern page_table_base
    .extern gdt64_ptr
    .extern parse_multiboot2_info

/*
 * _start: GRUB2 entry point (32-bit protected mode)
 */
_start:
    /* GRUB2 hands control here in 32-bit protected mode.
     * EBX = pointer to Multiboot info structure.
     * This stub simply jumps to _start_64 which is defined in enter64.asm.
     * The actual entry point code (stack setup, PAE, long mode) lives there. */
    jmp     _start_64

/* End of boot_entry32.asm */