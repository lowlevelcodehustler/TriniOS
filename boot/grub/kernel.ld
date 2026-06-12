/*
 * kernel.ld — Linker Script for TriniOS Kernel
 *
 * Ensures:
 *   1. Multiboot2 header is within first 32 KiB (GRUB2 requirement)
 *   2. Kernel loads at 1 MB (0x100000, standard multiboot address)
 *   3. Text, data, bss sections are properly ordered
 *   4. _end symbol marks end of kernel image
 */

ENTRY(_start)

/* ── Base load address ───────────────────────────────────────────────── */
BASE_ADDRESS = 0x100000;  /* 1 MB — standard multiboot load point */

SECTIONS
{
    /* ── Set start of kernel image ───────────────────────────────────── */
    . = BASE_ADDRESS;

    /* ── .text: code + multiboot2 header ────────────────────────────────
     * The multiboot2_header MUST be within the first 32 KiB of the image.
     * We place it first, followed by the entry point code.
     */
    .text :
    {
        /* Multiboot2 header — MUST be first for GRUB2 to find it */
        *(.multiboot_header)
        *(multiboot2_header)

        /* Kernel entry point (32-bit to 64-bit transition) */
        *(.text.start)
        *(.text.entry)
        *(.text)

        /* Read-only data embedded in text */
        *(.rodata*)
    }

    /* ── .rodata: additional read-only data ──────────────────────────── */
    .rodata :
    {
        *(.rodata*)
    }

    /* ── .data: initialized data ─────────────────────────────────────── */
    .data :
    {
        *(.data*)
        *(.sdata*)
    }

    /* ── .bss: zero-initialized data ─────────────────────────────────── */
    .bss :
    {
        bss_start = .;
        *(.bss*)
        *(COMMON)
        bss_end = .;
    }

    /* ── Mark end of kernel image ────────────────────────────────────── */
    _end = .;
    end = .;

    /* ── Discard unnecessary sections ────────────────────────────────── */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.note*)
        *(.comment*)
        *(.GCC.command.line)
    }
}