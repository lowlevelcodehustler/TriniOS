/*
 * boot_entry32.asm
 * TriniOS x86_64 Kernel Entry Point - 32-bit portion
 *
 * Assembled with: as --32
 *
 * GRUB2 hands control in 32-bit protected mode. This code:
 *   1. Sets up a temporary stack
 *   2. Enables PAE + long mode via MSR writes
 *   3. Loads page tables identity-mapping low memory
 *   4. Loads 64-bit GDT
 *   5. Far jumps to enter_long_mode (in enter64.asm)
 *
 * All data (GDT, page tables, mb_info, stacks) lives in enter64.asm
 * so 64-bit relocations are supported.
 */

    .text
    .code32
    .globl _start

/* External symbols defined in enter64.asm */
    .extern enter_long_mode
    .extern mb_info_ptr
    .extern page_table_base
    .extern gdt64_ptr
    .extern parse_multiboot2_info

/*
 * _start: GRUB2 entry point (32-bit protected mode)
 */
_start:
    cli                     /* ensure interrupts off */
    cld                     /* forward string direction */

    /* Preserve multiboot info pointer (EBX) */
    mov     %ebx, %eax
    mov     %eax, mb_info_ptr

    /* Set up temporary 32-bit stack (use stack32_top from enter64.asm) */
    .extern stack32_top
    mov     $stack32_top, %esp

    /* Parse multiboot2 info */
    call    parse_multiboot2_info

    /* Enable Physical Address Extension (PAE) */
    mov     %cr0, %eax
    btsl    $5, %eax        /* set CR0.PAE = 1 */
    mov     %eax, %cr0

    /* Set up 4-level page tables for identity mapping */
    mov     $page_table_base, %eax
    mov     %eax, %cr3

    /* Enable Long Mode via EFER MSR */
    mov     $0xC0000080, %ecx
    rdmsr
    btsl    $8, %eax        /* set EFER.LME = 1 */
    wrmsr

    /* Enable Paging (enters long mode) */
    mov     %cr0, %eax
    btsl    $31, %eax       /* set CR0.PG = 1 */
    mov     %eax, %cr0

    /* Load 64-bit GDT */
    lgdt    gdt64_ptr

    /* Far jump to 64-bit code segment (enter_long_mode in enter64.asm) */
    .byte   0x66, 0xea
    .word   $enter_long_mode
    .word   $0x08

/* End of boot_entry32.asm */