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
static void kernel_early_uint32(uint32_t val);
static void init_vm_subsystem(void);
static void init_host_subsystem(void);
static void create_first_task(void);
static void setup_user_page_tables(uint64_t prog_start, uint64_t prog_end);
static void install_trampoline(uint64_t entry_point);
static void enter_user_mode(uint64_t entry_point);
static void kernel_panic(const char *msg);

/* ── Page-table / image symbols from enter64.asm (identity-mapped) ───── */
extern uint64_t page_table_pd[];  /* PD for virtual 0–1 GiB: PDE[0]→pt_low, PDE[8]→pt_hi */
extern uint64_t pt_low[];         /* phase-B PTEs for virtual 0–2 MiB (512 entries) */
extern uint64_t pt_hi[];          /* phase-B PTEs for virtual 0x1000000–0x1200000 */
extern uint8_t _end;              /* end of kernel image (assigned by kernel.ld) */

/* ── User-space layout (must stay in sync with the page-table plan above) ─ */
#define USER_HEAP_BASE    0x1000000ULL   /* hello link base (hello.ld) */
#define USER_STACK_PAGE   0x100F0000ULL  /* dedicated user stack page */
#define USER_STACK_TOP    (USER_STACK_PAGE + 4096)
#define TRAMPOLINE_ADDR   0x100F8000ULL  /* kernel-written ring-3 trampoline */

/* PTE flags */
#define PTE_PRESENT_RW      0x3   /* P|R/W — supervisor only (U/S=0) */
#define PTE_PRESENT_RW_US   0xB   /* P|R/W|U/S — user+kernel */

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

    /* ── Step 1: Parse multiboot2 boot info (spec v2.0 tag walk) ─────── */
    parse_multiboot2_info(mb_info_ptr, &g_boot_info);

    kernel_early_console("Boot info at 0x");
    kernel_early_hex(g_boot_info.mb_info_ptr);
    if (g_boot_info.bootloader_name[0]) {
        kernel_early_console("\nBootloader: ");
        kernel_early_console(g_boot_info.bootloader_name);
        kernel_early_console("\n");
    }
    if (g_boot_info.cmdline[0]) {
        kernel_early_console("Cmdline: ");
        kernel_early_console(g_boot_info.cmdline);
        kernel_early_console("\n");
    }

    /* Log basic memory info (KiB) */
    kernel_early_console("Memory: lower=");
    kernel_early_uint32(g_boot_info.mem_lower);
    kernel_early_console(" KiB, upper=");
    kernel_early_uint32(g_boot_info.mem_upper);
    kernel_early_console(" KiB\n");

    /* Log memory map summary (first few regions) */
    kernel_early_console("Memory map entries: ");
    kernel_early_uint32(g_boot_info.mmap_entry_count);
    kernel_early_console("\n");
    for (uint32_t i = 0; i < g_boot_info.mmap_entry_count && i < 4; i++) {
        const multiboot2_mmap_entry_t *e = &g_boot_info.mmap_entries[i];
        kernel_early_console("  [");
        kernel_early_uint32(i);
        kernel_early_console("] 0x");
        kernel_early_hex(e->addr);
        kernel_early_console("-0x");
        kernel_early_hex(e->addr + e->len);
        kernel_early_console(" (");
        kernel_early_console(mmap_type_name(e->type));
        kernel_early_console(")\n");
    }

    /* Log initrd info (arrives as the first multiboot2 module tag) */
    if (g_boot_info.initrd_addr) {
        kernel_early_console("Initrd at 0x");
        kernel_early_hex(g_boot_info.initrd_addr);
        kernel_early_console("-0x");
        kernel_early_hex(g_boot_info.initrd_end);
        kernel_early_console(" size=");
        kernel_early_hex(g_boot_info.initrd_size);
        kernel_early_console("\n");
    }

    /* Log framebuffer info */
    if (g_boot_info.fb_addr) {
        kernel_early_console("Framebuffer: 0x");
        kernel_early_hex(g_boot_info.fb_addr);
        kernel_early_console(" (");
        kernel_early_uint32(g_boot_info.fb_width);
        kernel_early_console("x");
        kernel_early_uint32(g_boot_info.fb_height);
        kernel_early_console(") ");
        kernel_early_uint32(g_boot_info.fb_bpp);
        kernel_early_console(" bpp\n");
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

/* ── kernel_early_uint32: print a 32-bit value in decimal ────────────── */
static void kernel_early_uint32(uint32_t val)
{
    char buf[10];
    int pos = (int)sizeof(buf);

    if (val == 0) {
        kernel_early_putchar('0');
        return;
    }
    while (val > 0 && pos > 0) {
        buf[--pos] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = pos; i < (int)sizeof(buf); i++) {
        kernel_early_putchar(buf[i]);
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

/* ── setup_user_page_tables — phase B of the page-table plan ────────────
 *
 * enter64.asm boots long mode on a single 2 MiB huge page (phase A) so this
 * code can run. Before entering ring 3 we replace it with per-page tables:
 *   - pt_low covers virtual 0–2 MiB: kernel image pages become supervisor-
 *     only (U/S=0), while the VGA text buffer page stays user+kernel
 *     writable so ring-3 programs can write it directly.
 *   - pt_hi covers virtual 0x1000000–0x1200000: program pages, the user
 *     stack page and the trampoline page become user RWX (U/S=1).
 * The tables themselves live inside the kernel image, so they remain mapped.
 */
static void setup_user_page_tables(uint64_t prog_start, uint64_t prog_end)
{
    /* The kernel image must fit inside the first 2 MiB (pt_low coverage);
     * otherwise the per-page loop below would write past pt_low. */
    if ((uint64_t)&_end >= 0x200000ULL) {
        kernel_panic("Kernel image exceeds 2 MiB — page-table plan violated");
    }

    /* VGA text buffer: ring-3 hello writes here directly */
    pt_low[0xB8000 >> 12] = 0xB8000 | PTE_PRESENT_RW_US;

    /* Kernel image pages [0x100000, _end): supervisor-only (U/S=0) */
    for (uint64_t p = 0x100000ULL; p < (uint64_t)&_end; p += 4096) {
        pt_low[p >> 12] = p | PTE_PRESENT_RW;
    }

    /* Program pages: user RWX */
    for (uint64_t v = prog_start & ~4095ULL; v < prog_end; v += 4096) {
        pt_hi[(v - USER_HEAP_BASE) >> 12] = v | PTE_PRESENT_RW_US;
    }

    /* User stack page + trampoline page: user RWX */
    pt_hi[(USER_STACK_PAGE - USER_HEAP_BASE) >> 12] = USER_STACK_PAGE | PTE_PRESENT_RW_US;
    pt_hi[(TRAMPOLINE_ADDR - USER_HEAP_BASE) >> 12] = TRAMPOLINE_ADDR | PTE_PRESENT_RW_US;

    /* Activate: replace the phase-A huge page with per-page tables.
     * In-place modification is safe — all table pages stay mapped, and the
     * CPU invalidates TLB entries derived from a modified PDE automatically. */
    page_table_pd[0] = (uint64_t)pt_low | 0x3;   /* P|R/W, no PS bit */
    page_table_pd[8] = (uint64_t)pt_hi  | 0x3;
}

/* ── install_trampoline — write the ring-3 trampoline into user space ───
 *
 * After iretq the CPU keeps DS/ES at their old (kernel) selectors, and a
 * ring-3 process using kernel data selectors would #GP on its first memory
 * access. The trampoline reloads the user data segments before jumping to
 * the program entry point:
 *
 *   offset  bytes                        instruction
 *   0x00    B8 20 00                    mov $0x20, %ax      ; user data selector
 *   0x03    8E D0                       mov %ax, %ds
 *   0x05    8E C0                       mov %ax, %es
 *   0x07    8E C5                       mov %ax, %fs
 *   0x09    8E C6                       mov %ax, %gs
 *   0x0B    48 8B 05 06 00 00 00       mov [rip+6], %rax   ; load target slot
 *   0x12    FF E0                       jmp *%rax           ; → entry point
 *   0x14    (pad to offset 24)
 *   0x18    <uint64 target>             program entry point
 */
static void install_trampoline(uint64_t entry_point)
{
    static const uint8_t code[20] = {
        0xB8, 0x20, 0x00,                       /* mov $0x20, %ax */
        0x8E, 0xD0,                             /* mov %ax, %ds */
        0x8E, 0xC0,                             /* mov %ax, %es */
        0x8E, 0xC5,                             /* mov %ax, %fs */
        0x8E, 0xC6,                             /* mov %ax, %gs */
        0x48, 0x8B, 0x05, 0x06, 0x00, 0x00, 0x00, /* mov [rip+6], %rax */
        0xFF, 0xE0                              /* jmp *%rax */
    };

    uint8_t *t = (uint8_t *)TRAMPOLINE_ADDR;
    for (int i = 0; i < 20; i++) {
        t[i] = code[i];
    }
    *(volatile uint64_t *)(t + 24) = entry_point;
}

/* ── enter_user_mode — the actual ring-3 transition ─────────────────────
 *
 * Builds an iretq frame on the user stack and returns to ring 3:
 *   [RIP]    = trampoline (reloads DS/ES, then jumps to entry_point)
 *   [CS]     = 0x18 (user code, DPL 3)
 *   [RFLAGS] = 0x200 (IF set; IOPL=0 — the TSS I/O bitmap allows out/in)
 *   [SS]     = 0x23 (user data, DPL 3)
 *   [new RSP]= just below the frame
 */
static void enter_user_mode(uint64_t entry_point)
{
    install_trampoline(entry_point);

    uint64_t *frame = (uint64_t *)(USER_STACK_TOP - 5 * 8);
    frame[0] = TRAMPOLINE_ADDR;          /* RIP → trampoline */
    frame[1] = 0x18;                    /* CS: user code */
    frame[2] = 0x200;                   /* RFLAGS: IF set */
    frame[3] = 0x23;                    /* SS: user data */
    frame[4] = USER_STACK_TOP - 5 * 8;  /* new RSP: just below the frame */

    kernel_early_console("Entering ring 3 at 0x");
    kernel_early_hex(entry_point);
    kernel_early_console("\n");

    __asm__ volatile (
        "mov %0, %%rsp\n\t"
        "iretq\n\t"
        :
        : "r"(frame)
        : "memory"
    );

    /* Never reached */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ── create_first_task — Load and execute the first user-space program ───
 *
 * Strategy:
 *   1. Get the initrd root filesystem tree from initrd_get_root()
 *   2. Find /bin/hello in the tree using elf_find_in_initrd() (the file
 *      must be an ELF64 binary — see boot/initrd/src/Makefile)
 *   3. Parse the ELF header to get the entry point via elf_parse()
 *   4. Map user pages: program segments, a dedicated stack page and the
 *      ring-3 trampoline page (setup_user_page_tables)
 *   5. Copy PT_LOAD segments into the mapped region (elf_load_segments)
 *   6. Enter ring 3 at the entry point via an iretq frame
 *
 * NOTE: still a simplified implementation — no TCB creation, no per-task VM
 * bookkeeping and no syscall interface yet; those belong to later phases.
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

    /* Compute the virtual range the program occupies (all PT_LOAD segments) */
    uint64_t seg_start = 0, seg_end = 0;
    if (!elf_segment_range(binary_data, binary_size, &seg_start, &seg_end)) {
        kernel_early_console("ERROR: No loadable ELF segments\n");
        kernel_panic("No loadable segments");
    }

    /* Map the user pages (program + stack + trampoline) before touching them */
    setup_user_page_tables(seg_start, seg_end);

    /* Copy PT_LOAD segments into the mapped user region (zero-fills BSS tail) */
    if (!elf_load_segments(binary_data, binary_size)) {
        kernel_early_console("ERROR: ELF segment load failed\n");
        kernel_panic("ELF segment load failed");
    }

    /* Jump to user-space! */
    enter_user_mode(result.entry_point);
}
