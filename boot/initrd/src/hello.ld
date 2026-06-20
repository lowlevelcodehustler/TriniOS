/*
 * TriniOS - /bin/hello Linker Script
 *
 * Links the hello binary as a position-independent ET_EXEC ELF64 x86_64
 * executable at address 0x1000000 (16 MB).
 *
 * This is a safe location: above kernel memory, below typical user-space
 * regions, and within the first 2 GB of virtual address space.
 */

ENTRY(_start)

SECTIONS
{
    /* Base load address */
    . = 0x1000000;

    /* Text segment (code + read-only data) */
    .text : ALIGN(4K) {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    /* Data segment (initialized data) */
    .data : ALIGN(4K) {
        *(.data)
        *(.data.*)
    }

    /* BSS segment (uninitialized data) */
    .bss : ALIGN(4K) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /* Discard everything else — we don't need dynamic linking, etc. */
    /DISCARD/ : {
        *(.eh_frame)
        *(.note*)
        *(.comment)
    }
}