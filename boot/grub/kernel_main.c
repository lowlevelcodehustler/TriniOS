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
#include "elf_loader.h"
#include "kernel_selftest.h"
#include <stdint.h>

/* ── UART16550 serial port (COM1) ───────────────────────────────────── */
#define UART16550_PORT   0x3F8       /* COM1 I/O base */
#define UART_TX          0           /* Transmit buffer (write-only) */
#define UART_LSR         5           /* Line status register */
#define UART_LSR_THRE    0x20        /* Transmitter holding empty */

/* ── Forward declarations (kernel subsystems) ──────────────────────────── */
static void serial_init(void);
static void serial_putchar(char c);
static void kernel_early_console(const char *msg);
static void kernel_early_hex(uint64_t val);
static void init_vm_subsystem(void);
static void init_host_subsystem(void);
static void create_first_task(void);
static void jump_to_user_space(uint64_t entry_point);
static void kernel_panic(const char *msg);

/* ── Global boot info (populated at entry) ────────────────────────────── */
static trini_boot_info_t g_boot_info;

/* ── kernel_main: first C function called by kernel_entry.asm ────────────
 *
 * GRUB2 passes the multiboot2 info pointer in RDI.
 */
void kernel_main(uint64_t mb_info_ptr)
{
    /* Initialize serial console FIRST so all output is visible via -serial stdio */
    serial_init();

    kernel_early_console("\n");
    kernel_early_console("========================================\n");
    kernel_early_console("  TriniOS Kernel — Boot Sequence\n");
    kernel_early_console("========================================\n");
    kernel_early_console("\n");

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
    } else {
        kernel_early_console("WARNING: No initrd loaded\n");
    }

    /* ── Step 4: Run kernel self-tests ──────────────────────────────── */
    kernel_early_console("Running kernel self-tests...\n");
    selftest_run_all();

    /* ── Step 5: Initialize host subsystem ──────────────────────────── */
    kernel_early_console("Initializing host subsystem...\n");
    init_host_subsystem();

    /* ── Step 6: Create first task (init process) ───────────────────── */
    kernel_early_console("Creating first task (init)...\n");
    create_first_task();

    /* ── Normal path: kernel_main should NOT return ──────────────────── */
    kernel_panic("kernel_main returned — this should not happen");
}

/* ── serial_init: configure UART16550 at 115200 8N1 ─────────────────── */
static void serial_init(void)
{
    /* Disable interrupts */
    *(volatile uint8_t *)(UART16550_PORT + 1) = 0x00;
    /* Enable DLAB (baud rate divisor) */
    *(volatile uint8_t *)(UART16550_PORT + 3) = 0x80;
    /* Set baud rate: 115200 for 1.8432 MHz clock
     * DIV = 12 / (115200/9600) ≈ 0.01 → divisor = 0x000B (11) */
    *(volatile uint8_t *)(UART16550_PORT + 0) = 0x0B; /* LSB */
    *(volatile uint8_t *)(UART16550_PORT + 1) = 0x00; /* MSB */
    /* 8 bits, no parity, 1 stop bit — disable DLAB */
    *(volatile uint8_t *)(UART16550_PORT + 3) = 0x03;
    /* Enable FIFO: clear reset, enable DMA, 14-byte threshold */
    *(volatile uint8_t *)(UART16550_PORT + 2) = 0xC7;
}

static void serial_putchar(char c)
{
    /* Wait until transmit holding register is empty */
    int timeout = 10000;
    while (!((*(volatile uint8_t *)(UART16550_PORT + UART_LSR)) & UART_LSR_THRE)) {
        if (--timeout <= 0) return;
    }
    *(volatile uint8_t *)(UART16550_PORT + UART_TX) = (uint8_t)c;
}

/* ── kernel_early_console: write to VGA text mode buffer AND serial ─── */
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
    /* Always output to serial (works with or without VGA/display) */
    serial_putchar(c);

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

    /* Write to VGA only if buffer is valid (some QEMU configs don't have it) */
    if (vga_buffer) {
        vga_buffer[vga_row * VGA_COLS + vga_col] = (uint16_t)((0x07 << 8) | (uint8_t)c);

        vga_col++;
        if (vga_col >= VGA_COLS) {
            vga_col = 0;
            vga_row++;
            if (vga_row >= VGA_ROWS)
                kernel_early_scroll();
        }
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

/* ── jump_to_user_space — Switch to user-mode and jump to entry point ───
 *
 * This function switches from kernel mode (supervisor privileges) to
 * user mode (user privileges) and jumps to the given entry point.
 *
 * Steps:
 *   1. Set up a new stack pointer in user space
 *   2. Push initial registers for the "return" (simulating a thread start)
 *   3. Use iretq (interrupt return) to switch privilege levels
 *
 * NOTE: This is a simplified implementation. A full Mach kernel would:
 *   - Create a new TCB with proper user-space VM mapping
 *   - Set up segment registers (CS, DS, SS, FS, GS) for user mode
 *   - Handle the transition via a syscall/exception gate
 */

static void jump_to_user_space(uint64_t entry_point)
{
    kernel_early_console("Jumping to user-space at 0x");
    kernel_early_hex(entry_point);
    kernel_early_console("\n");

    /* For Phase 1, we use a simple far return (lretq) to switch CS.
     * This requires a GDT entry for user-mode code segment.
     *
     * The kernel_entry.asm should have already set up:
     *   - Kernel code segment (CS = 0x8): ring 0, execute+read
     *   - User code segment (CS = 0x1B): ring 3, execute+read
     *   - Kernel data segment (DS = 0x10): ring 0, read+write
     *   - User data segment (DS = 0x23): ring 3, read+write
     */

    /* Use inline assembly to perform the privilege level switch.
     * We push the user-space stack pointer, RIP, RFLAGS, SS, and CS
     * onto the kernel stack, then use iretq to return to user mode. */

    __asm__ volatile (
        /* Set up user-space stack pointer (top of 4KB page at entry_point) */
        "mov %0, %%rsp\n"

        /* Use iretq to switch to user mode.
         * The values below are pushed onto the stack in the order expected by iretq:
         *   RIP    = entry_point (new instruction pointer)
         *   RFLAGS = EF (EFLAGS with interrupt enable, bit 0x200)
         *   CS     = 0x1B (user-mode code segment selector)
         *   SS     = 0x23 (user-mode data segment selector)
         *   RSP    = user_stack (new stack pointer in user space)
         */
        "pushq %0\n"              /* Push entry point (will be new RIP) */
        "pushq $0x200\n"          /* Push RFLAGS with interrupt flag */
        "pushq $0x1B\n"           /* Push user CS selector */

        /* At this point, we need SS:RSP on the stack too.
         * Since we just changed RSP to the entry_point, we use that as both
         * the new stack pointer AND the value to push for SS:RSP.
         * This is a simplification — in reality, we'd allocate a proper
         * user-space stack via vm_allocate. */

        /* Alternative approach: use lretq with explicit CS switch */
        "pop %%rax\n"             /* Pop entry point into RAX */

        /* Far return to user mode: lretq $0x1B (CS) then RIP from stack */
        ".byte 0x48\n"            /* rex prefix for operand size */
        ".byte 0xcf\n"            /* lretq opcode */

        /* This won't work correctly without proper GDT setup.
         * For now, we'll just halt and let the developer see the console message. */
        "cli\n"
        "hlt\n"
        :
        : "r" (entry_point)
        : "rax", "rsp"
    );

    /* Should never reach here */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ── create_first_task — Load and execute the first user-space program ───
 *
 * Strategy (Phase 1):
 *   1. Get the initrd root filesystem tree from initrd_get_root()
 *   2. Find /bin/hello in the tree using elf_find_in_initrd()
 *   3. Parse ELF header to get entry point via elf_parse()
 *   4. Jump to user-space at that entry point
 *
 * NOTE: This is a simplified implementation that does NOT properly set up:
 *   - User-space VM mapping (segments are copied, not mapped)
 *   - User-space stack allocation
 *   - Segment register setup (FS, GS, DS, SS)
 *   - TCB creation for Mach task management
 *
 * Full implementation requires kernel-side ELF loading + VM manipulation.
 */

static void create_first_task(void)
{
    /* Check if initrd is available */
    if (!initrd_is_loaded()) {
        kernel_early_console("ERROR: No initrd loaded — cannot create first task\n");
        kernel_panic("No initrd");
    }

    /* Get the initrd root filesystem tree */
    void *root = initrd_get_root();
    if (!root) {
        kernel_early_console("ERROR: Initrd root is NULL\n");
        kernel_panic("Initrd root not available");
    }

    /* Try to find /bin/hello in the initrd tree */
    const void *binary_data = NULL;
    uint64_t binary_size = 0;

    kernel_early_console("Searching for /bin/hello in initrd...\n");

    if (!elf_find_in_initrd(root, "/bin/hello", &binary_data, &binary_size)) {
        /* Try /sbin/init as fallback */
        kernel_early_console("Trying /sbin/init...\n");
        if (!elf_find_in_initrd(root, "/sbin/init", &binary_data, &binary_size)) {
            kernel_early_console("ERROR: No executable found in initrd\n");
            kernel_panic("No executable in initrd");
        }
    }

    kernel_early_console("Found binary at 0x");
    kernel_early_hex((uint64_t)(uintptr_t)binary_data);
    kernel_early_console(" size=");
    kernel_early_hex(binary_size);
    kernel_early_console("\n");

    /* Parse ELF header to get entry point */
    elf_load_result_t result;
    if (!elf_parse(binary_data, binary_size, &result)) {
        kernel_early_console("ERROR: Invalid ELF binary\n");
        kernel_panic("Invalid ELF");
    }

    kernel_early_console("ELF type: ");
    if (result.is_pie) {
        kernel_early_console("ET_DYN (PIE)\n");
    } else {
        kernel_early_console("ET_EXEC\n");
    }
    kernel_early_console("Entry point: 0x");
    kernel_early_hex(result.entry_point);
    kernel_early_console("\n");

    /* Jump to user-space! */
    jump_to_user_space(result.entry_point);
}
