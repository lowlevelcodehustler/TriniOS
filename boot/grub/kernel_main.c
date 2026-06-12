/*
 * kernel_main.c
 * TriniOS — C Kernel Entry Point
 *
 * Called by kernel_entry.asm after 64-bit long mode is established.
 * This is the first C function executed by the kernel.
 *
 * Responsibilities:
 *   1. Parse multiboot2 boot info
 *   2. Initialize kernel subsystems (VM, initrd, early console)
 *   3. Load initrd into VM
 *   4. Create first task (init process)
 */

#include "multiboot2_info.h"
#include "initrd_loader.h"
#include <stdint.h>

/* ── Forward declarations (kernel subsystems) ──────────────────────────── */
static void kernel_early_console(const char *msg);
static void kernel_early_hex(uint64_t val);
static void init_vm_subsystem(void);
static void init_host_subsystem(void);
static void create_first_task(void);
static void kernel_panic(const char *msg);

/* ── Global boot info (populated at entry) ────────────────────────────── */
static trini_boot_info_t g_boot_info;

/* ── kernel_main: first C function called by kernel_entry.asm ────────────
 *
 * GRUB2 passes the multiboot2 info pointer in RDI.
 */
void kernel_main(uint64_t mb_info_ptr)
{
    kernel_early_console("TriniOS kernel entry — C subsystem init\n");

    /* ── Step 1: Parse multiboot2 boot info ──────────────────────────── */
    parse_multiboot2_info(mb_info_ptr, &g_boot_info);

    kernel_early_console("Bootloader: ");
    kernel_early_console(g_boot_info.bootloader_name);
    kernel_early_console("\n");

    kernel_early_console("Memory: ");
    kernel_early_console(g_boot_info.cmdline);
    kernel_early_console("\n");

    /* Log memory map summary */
    kernel_early_console("Memory map entries: ");

    /* Log initrd info */
    kernel_early_console("Initrd at 0x");
    kernel_early_hex(g_boot_info.initrd_addr);
    kernel_early_console(" size=");
    kernel_early_hex(g_boot_info.initrd_size);
    kernel_early_console("\n");

    /* Log framebuffer info */
    if (g_boot_info.fb_addr) {
        kernel_early_console("Framebuffer: 0x");
        kernel_early_hex(g_boot_info.fb_addr);
        kernel_early_console(" (");
        kernel_early_hex(g_boot_info.fb_width);
        kernel_early_console("x");
        kernel_early_hex(g_boot_info.fb_height);
        kernel_early_console(")\n");
    }

    /* ── Step 2: Initialize VM subsystem ─────────────────────────────── */
    kernel_early_console("Initializing VM subsystem...\n");
    init_vm_subsystem();

    /* ── Step 3: Load initrd ─────────────────────────────────────────── */
    if (g_boot_info.initrd_addr && g_boot_info.initrd_size) {
        kernel_early_console("Loading initrd...\n");

        initrd_init(g_boot_info.initrd_addr, g_boot_info.initrd_size);

        kernel_early_console("Unpacking initrd...\n");
        initrd_unpack();

        kernel_early_console("Mounting initrd as rootfs...\n");
        initrd_mount();

        kernel_early_console("Initrd loaded successfully\n");

        /* Debug: dump file tree */
        // initrd_dump_tree();

        /* Free raw initrd buffer after unpacking */
        kernel_early_console("Freeing raw initrd buffer...\n");
        initrd_free_image();
    } else {
        kernel_early_console("WARNING: No initrd loaded\n");
    }

    /* ── Step 4: Initialize host subsystem ───────────────────────────── */
    kernel_early_console("Initializing host subsystem...\n");
    init_host_subsystem();

    /* ── Step 5: Create first task (init process) ────────────────────── */
    kernel_early_console("Creating first task (init)...\n");
    create_first_task();

    /* ── Normal path: kernel_main should NOT return ──────────────────── */
    kernel_panic("kernel_main returned — this should not happen");
}

/* ── kernel_early_console: write to VGA text mode buffer ──────────────── */
/* VGA text mode: 80x25, base 0xB8000, 16 bytes per char (byte=data, byte=color) */
static uint16_t *vga_buffer = (uint16_t *)0xB8000;
static uint8_t   vga_row = 0;
static uint8_t   vga_col = 0;
#define VGA_COLS 80
#define VGA_ROWS 25

static void kernel_early_scroll(void)
{
    /* Shift every row up by one */
    for (int r = 1; r < VGA_ROWS; r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            vga_buffer[r * VGA_COLS + c] = vga_buffer[(r - 1) * VGA_COLS + c];
        }
    }
    /* Clear last row */
    for (int c = 0; c < VGA_COLS; c++) {
        vga_buffer[(VGA_ROWS - 1) * VGA_COLS + c] = 0x0720; /* ' ' white-on-black */
    }
    vga_row = VGA_ROWS - 1;
}

static void kernel_early_putchar(char c)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_ROWS)
            kernel_early_scroll();
        return;
    }
    if (c == '\r') {
        vga_col = 0;
        return;
    }

    /* Write character with light-gray-on-black attribute (0x07) */
    vga_buffer[vga_row * VGA_COLS + vga_col] = (uint16_t)((0x07 << 8) | (uint8_t)c);

    vga_col++;
    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_ROWS)
            kernel_early_scroll();
    }
}

static void kernel_early_console(const char *msg)
{
    while (*msg) {
        kernel_early_putchar(*msg);
        msg++;
    }
}

/* ── kernel_early_hex: print a 64-bit value as hex ───────────────────── */
static void kernel_early_hex(uint64_t val)
{
    const char hex[] = "0123456789ABCDEF";
    for (int i = 19; i >= 0; i--) {
        kernel_early_putchar(hex[(val >> (i * 4)) & 0xF]);
    }
}

/* ── kernel_panic: halt on fatal error ────────────────────────────────── */
static void kernel_panic(const char *msg)
{
    kernel_early_console("\n*** KERNEL PANIC: ");
    kernel_early_console(msg);
    kernel_early_console(" ***\n");

    /* Disable interrupts and halt */
    __asm__ volatile("cli; hlt");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ── Stub implementations (to be replaced by real kernel subsystems) ──── */

static void init_vm_subsystem(void)
{
    /* Placeholder: real VM init will call gnumach vm/ initialization */
    /* For now, the page tables set up by kernel_entry.asm are sufficient */
}

static void init_host_subsystem(void)
{
    /* Placeholder: real host init will set up CPU info, SMP, etc. */
}

static void create_first_task(void)
{
    /* Placeholder: will create the first Mach task and exec /sbin/init
     * This requires the full Mach task/thread/VM subsystem to be operational.
     * In Phase 1, this is wired through the BSD server.
     */
    kernel_early_console("First task creation stub — full Mach task subsystem needed\n");
}