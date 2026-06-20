/*
 * TriniOS - /bin/hello (Minimal Test User-Space Program)
 *
 * A bare-metal C program that writes "Hello from TriniOS!" to the VGA
 * text mode buffer. No libc, no syscalls — just raw memory writes.
 *
 * Compiled as: x86_64 ELF64 ET_EXEC, entry point at _start
 * Linked at address 0x1000000 (16 MB) — a safe location above kernel VM.
 *
 * This is the "Hello World" of TriniOS user-space. When we successfully
 * boot into QEMU and execute this binary, it proves:
 *   1. Kernel can load ELF binaries from initrd
 *   2. User-space code executes correctly
 *   3. VGA buffer memory writes work from user-space address space
 */

/* ── _start: entry point (called by kernel after ELF loading) ─────────── */

/* VGA text mode buffer: 80 columns x 25 rows, 16 bytes per cell
 * Base address: 0xB8000 (physical = virtual in user-space for now) */
static volatile unsigned short *vga_buffer = (unsigned short *)0xB8000;

#define VGA_COLS 80
#define VGA_ROWS 25

/* ── VGA helper: write a single character at (row, col) ──────────────── */
static void vga_putc(int row, int col, char c, unsigned char attr)
{
    vga_buffer[row * VGA_COLS + col] = ((unsigned short)attr << 8) | (unsigned char)c;
}

/* ── VGA helper: clear the entire screen ─────────────────────────────── */
static void vga_clear(void)
{
    for (int row = 0; row < VGA_ROWS; row++) {
        for (int col = 0; col < VGA_COLS; col++) {
            vga_putc(row, col, ' ', 0x07); /* Light gray on black */
        }
    }
}

/* ── VGA helper: print a string at the current cursor position ───────── */
static int vga_row = 0;
static int vga_col = 0;

static void vga_print(const char *msg)
{
    while (*msg) {
        if (*msg == '\n') {
            vga_col = 0;
            vga_row++;
            if (vga_row >= VGA_ROWS) vga_row = VGA_ROWS - 1;
        } else if (*msg == '\r') {
            vga_col = 0;
        } else {
            vga_putc(vga_row, vga_col, *msg, 0x07); /* Light gray on black */
            vga_col++;
            if (vga_col >= VGA_COLS) {
                vga_col = 0;
                vga_row++;
                if (vga_row >= VGA_ROWS) vga_row = VGA_ROWS - 1;
            }
        }
        msg++;
    }
}

/* ── _start: user-space entry point ──────────────────────────────────── */
void _start(void)
{
    /* Clear screen */
    vga_clear();

    /* Print welcome banner */
    vga_row = 2;
    vga_col = 0;
    vga_print("========================================\n");
    vga_print("   Hello from TriniOS User Space!      \n");
    vga_print("========================================\n");
    vga_print("\n");

    /* Print system info */
    vga_print("Kernel: Mach Microkernel 0.1\n");
    vga_print("Architecture: x86_64\n");
    vga_print("Binary Format: ELF64 ET_EXEC\n");
    vga_print("Entry Point: 0x1000000\n");
    vga_print("\n");

    /* Print success indicator */
    vga_print("STATUS: User-space execution SUCCESSFUL!\n");
    vga_print("\n");
    vga_print("Press Ctrl+C in QEMU to halt.\n");

    /* Infinite loop — user-space should never return to kernel */
    for (;;) {
        __asm__ volatile("hlt");
    }
}