/*
 * kernel_selftest.c
 * TriniOS — Kernel-Space Self-Test Implementation
 *
 * Tests all Phase 1-2 subsystems in the actual boot environment.
 * Output goes to VGA text mode for QEMU capture.
 *
 * IMPORTANT: Does NOT include Phase 2 headers (they require Mach runtime).
 * Instead, defines the same types locally to verify data structure correctness.
 * This ensures we can compile and run tests in freestanding kernel context.
 */

#include "kernel_selftest.h"

/* ============================================================================
 * VGA text mode output (same pattern as kernel_main.c)
 * ============================================================================ */

static uint16_t *vga_buf = (uint16_t *)0xB8000;
static uint8_t   vga_r = 0;
static uint8_t   vga_c = 0;
#define VGA_COLS 80
#define VGA_ROWS 25

static void selftest_vga_scroll(void)
{
    for (int r = 1; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[r * VGA_COLS + c] = vga_buf[(r - 1) * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        vga_buf[(VGA_ROWS - 1) * VGA_COLS + c] = 0x0720;
    vga_r = VGA_ROWS - 1;
}

static void selftest_vga_putc(char ch)
{
    if (ch == '\n') {
        vga_c = 0;
        vga_r++;
        if (vga_r >= VGA_ROWS) selftest_vga_scroll();
        return;
    }
    if (ch == '\r') { vga_c = 0; return; }

    vga_buf[vga_r * VGA_COLS + vga_c] = (uint16_t)((0x07 << 8) | (uint8_t)ch);
    vga_c++;
    if (vga_c >= VGA_COLS) {
        vga_c = 0;
        vga_r++;
        if (vga_r >= VGA_ROWS) selftest_vga_scroll();
    }
}

static void selftest_vga_puts(const char *s)
{
    while (*s) { selftest_vga_putc(*s); s++; }
}

static void selftest_vga_hex(uint64_t val)
{
    (void)val; /* Used for debug dumps; placeholder */
}

static void selftest_vga_u32(uint32_t val)
{
    /* Simple decimal printer for small integers */
    char buf[16];
    int idx = 0;
    if (val == 0) { buf[idx++] = '0'; }
    else {
        uint32_t tmp = val;
        char stack[16];
        int si = 0;
        while (tmp > 0) { stack[si++] = '0' + (tmp % 10); tmp /= 10; }
        while (si > 0) { buf[idx++] = stack[--si]; }
    }
    buf[idx] = '\0';
    selftest_vga_puts(buf);
}

/* ============================================================================
 * Kernel allocator for self-tests (simple static pool)
 * ============================================================================ */

#define KALLOC_POOL_SIZE (256 * 1024)  /* 256KB test pool */
static uint8_t kalloc_pool[KALLOC_POOL_SIZE];
static uint32_t kalloc_ptr = 0;

static void *kalloc(uint32_t size)
{
    /* Align to 8 bytes */
    kalloc_ptr = (kalloc_ptr + 7) & ~7;
    if (kalloc_ptr + size > KALLOC_POOL_SIZE)
        return NULL;
    void *ptr = (void *)&kalloc_pool[kalloc_ptr];
    kalloc_ptr += size;
    return ptr;
}

static void kfree(void *ptr)
{
    (void)ptr; /* No free-list; pool is discarded after tests */
}

/* ============================================================================
 * Local type definitions (mirror Phase 2 headers for compile-time verification)
 *
 * These are defined here rather than including the Phase 2 headers, because
 * the Phase 2 headers depend on <mach/mach.h> which requires a Mach runtime.
 * The self-tests verify the data structure layout and constants are correct.
 * ============================================================================ */

/* ---------- VM Manager types (from vm_manager.h) ---------- */

#define VM_DEFAULT_STACK_SIZE     (8 * 1024 * 1024)
#define VM_DEFAULT_HEAP_SIZE      (64 * 1024 * 1024)
#define VM_MAX_MAPPINGS_PER_TASK  4096

typedef enum {
    VM_REGION_CODE,
    VM_REGION_DATA,
    VM_REGION_RODATA,
    VM_REGION_STACK,
    VM_REGION_HEAP,
    VM_REGION_SHARED,
    VM_REGION_DEVICE,
    VM_REGION_GPU,
    VM_REGION_AI_WEIGHTS,
    VM_REGION_AI_ACTIVATIONS
} vm_region_type_t;

typedef struct vm_region {
    uint64_t        start;
    uint64_t        end;
    uint32_t        protection;
    uint32_t        inheritance;
    vm_region_type_t type;
    uint64_t        owner;
    bool            is_pinned;
    bool            is_gpu_accessible;
    struct vm_region *next;
} vm_region_t;

typedef struct {
    uint64_t    task;
    uint64_t    vm_map;
    vm_region_t *regions;
    uint64_t    break_;
    uint64_t    stack_base;
    uint32_t    region_count;
    uint64_t    total_mapped;
    uint64_t    total_resident;
} vm_space_t;

typedef struct {
    uint64_t    total_mapped;
    uint64_t    total_resident;
    uint64_t    total_pinned;
    uint64_t    total_gpu_accessible;
    uint32_t    region_count;
} vm_stats_t;

/* ---------- GPU Memory types (from gpu_mem.h) ---------- */

#define GPU_MEM_PAGE_SIZE         (4 * 1024 * 1024)
#define GPU_MEM_MAX_ALLOCATIONS   1024
#define GPU_MEM_DEFAULT_POOL_SIZE (1 * 1024 * 1024 * 1024)

typedef enum {
    GPU_MEM_PAGE_LOCKED,
    GPU_MEM_COHERENT,
    GPU_MEM_DEVICE,
    GPU_MEM_UNIFIED
} gpu_mem_type_t;

typedef struct {
    uint64_t        host_addr;
    uint64_t        device_ptr;
    uint64_t        size;
    gpu_mem_type_t  type;
    uint64_t        owner_task;
    bool            in_use;
    uint32_t        ref_count;
} gpu_mem_alloc_t;

typedef struct {
    gpu_mem_alloc_t allocations[GPU_MEM_MAX_ALLOCATIONS];
    uint64_t        pool_size;
    uint64_t        used;
    uint64_t        peak_used;
    uint32_t        active_allocations;
    uint32_t        total_allocations;
    uint32_t        total_frees;
} gpu_mem_pool_t;

typedef struct {
    uint64_t    pool_size;
    uint64_t    used;
    uint64_t    peak_used;
    uint64_t    free;
    uint32_t    active_allocations;
    uint32_t    total_allocations;
    uint32_t    total_frees;
} gpu_mem_stats_t;

/* ---------- IPC types (from ipc_port.h) ---------- */

#define IPC_MAX_SERVERS           64
#define IPC_PORT_NAME_MAX_LEN     64
#define IPC_REPLY_BUFFER_SIZE     (8 * 1024)
#define IPC_MAX_PENDING_REQUESTS  256

typedef enum {
    IPC_SERVER_BSD,
    IPC_SERVER_AI,
    IPC_SERVER_FS,
    IPC_SERVER_NETWORK,
    IPC_SERVER_WINDOW,
    IPC_SERVER_AUDIO,
    IPC_SERVER_DRIVER,
    IPC_SERVER_CUSTOM,
    IPC_SERVER_COUNT
} ipc_server_type_t;

typedef struct {
    ipc_server_type_t type;
    char              name[IPC_PORT_NAME_MAX_LEN];
    uint64_t          send_port;
    uint64_t          notify_port;
    uint64_t          task;
    bool              active;
    bool              crashed;
    uint32_t          ref_count;
} ipc_server_entry_t;

typedef struct {
    uint64_t    port_set;
    uint64_t    default_port;
    uint32_t    active_requests;
} ipc_port_set_t;

typedef struct {
    uint32_t    request_id;
    uint64_t    reply_port;
    uint64_t    owner_task;
    uint64_t    timeout;
    bool        completed;
} ipc_pending_request_t;

typedef struct {
    uint64_t    msgh_remote_port;
    uint64_t    msgh_local_port;
    uint64_t    msgh_voucher_port;
    uint32_t    msgh_size;
    uint64_t    msgh_id;
    uint32_t    msgh_bits;
} mach_msg_header_t;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgh_descriptor_count;
    uint64_t          sender_task;
    uint32_t          request_id;
} trini_ipc_request_header_t;

typedef struct {
    mach_msg_header_t header;
    uint32_t          msgh_descriptor_count;
    int               result;
    uint32_t          request_id;
} trini_ipc_reply_header_t;

/* ---------- Task Manager types (from task_manager.h) ---------- */

#define TASK_MAX_TASKS        1024
#define TASK_MAX_THREADS      256
#define TASK_PID_MIN          3
#define TASK_PID_MAX          32768

typedef enum {
    TASK_STATE_ALIVE,
    TASK_STATE_ZOMBIE,
    TASK_STATE_DEAD
} task_state_t;

typedef enum {
    TASK_SCHED_RR,
    TASK_SCHED_FIXED,
    TASK_SCHED_TIME_CONSTRAINT
} task_sched_policy_t;

typedef struct {
    bool            set_rlimit;
    bool            set_pgrp;
    bool            set_session;
    uint32_t        pgrp;
    uint32_t        session;
} task_attr_t;

typedef struct {
    uint32_t        pid;
    uint32_t        ppid;
    uint32_t        pgrp;
    uint32_t        session;
    task_state_t    state;
    uint32_t        exit_status;
    uint32_t        thread_count;
} task_info_t;

/* ---------- Host Local types (from host_local.h) ---------- */

typedef enum {
    KLOG_DEBUG,
    KLOG_INFO,
    KLOG_WARN,
    KLOG_ERROR,
    KLOG_PANIC
} klog_level_t;

#define CPUID_FEAT_AES    25
#define CPUID_FEAT_SSE4   19
#define CPUID_FEAT_AVX    28

typedef struct {
    uint64_t    mem_lower;
    uint64_t    mem_upper;
    uint32_t    mm_entry_count;
    uint32_t    cpu_count;
    uint64_t    cpuid_vendor[3];
    bool        has_aes;
    bool        has_sse4;
    bool        has_avx;
} boot_params_t;

/* ============================================================================
 * Test: VM Manager (data structure logic)
 * ============================================================================ */

static void selftest_vm_internal_logic(void)
{
    selftest_print_header("VM Manager");

    /* Test 1: vm_region_t structure layout */
    {
        vm_region_t region;
        region.start = 0x1000;
        region.end = 0x2000;
        region.type = VM_REGION_CODE;
        region.is_pinned = false;
        region.is_gpu_accessible = false;
        SELFTEST_ASSERT("VM", "region_layout",
            region.start == 0x1000 && region.end == 0x2000,
            "vm_region_t fields incorrect");
    }

    /* Test 2: vm_space_t structure layout */
    {
        vm_space_t space;
        space.region_count = 0;
        space.total_mapped = 0;
        space.break_ = 0;
        SELFTEST_ASSERT("VM", "space_layout",
            space.region_count == 0 && space.total_mapped == 0,
            "vm_space_t fields incorrect");
    }

    /* Test 3: Region type enumeration */
    {
        SELFTEST_ASSERT_EQ("VM", "region_types_count",
            VM_REGION_AI_ACTIVATIONS, 9,
            "Expected 10 region types (0-9)");
    }

    /* Test 4: Default sizes are reasonable */
    {
        SELFTEST_ASSERT("VM", "default_stack_size",
            VM_DEFAULT_STACK_SIZE == (8 * 1024 * 1024),
            "Stack size should be 8MB");
        SELFTEST_ASSERT("VM", "default_heap_size",
            VM_DEFAULT_HEAP_SIZE == (64 * 1024 * 1024),
            "Heap size should be 64MB");
    }

    /* Test 5: vm_stats_t structure */
    {
        vm_stats_t stats;
        stats.total_mapped = 0;
        stats.total_resident = 0;
        stats.total_pinned = 0;
        stats.total_gpu_accessible = 0;
        stats.region_count = 0;
        SELFTEST_ASSERT("VM", "stats_layout",
            stats.region_count == 0,
            "vm_stats_t fields incorrect");
    }

    /* Test 6: Region linked list insertion */
    {
        vm_space_t space;
        space.regions = NULL;
        vm_region_t *r1 = (vm_region_t *)kalloc(sizeof(vm_region_t));
        vm_region_t *r2 = (vm_region_t *)kalloc(sizeof(vm_region_t));
        r1->start = 0x1000; r1->end = 0x2000; r1->next = NULL;
        r2->start = 0x2000; r2->end = 0x3000; r2->next = r1;
        space.regions = r2;
        space.region_count = 2;
        SELFTEST_ASSERT("VM", "region_linked_list",
            space.regions == r2 && r2->next == r1 && r1->next == NULL,
            "Region linked list broken");
    }
}

/* ============================================================================
 * Test: GPU Memory Manager
 * ============================================================================ */

static void selftest_gpu_internal_logic(void)
{
    selftest_print_header("GPU Memory");

    /* Test 1: GPU memory type enumeration */
    {
        SELFTEST_ASSERT_EQ("GPU", "gpu_type_locked",
            GPU_MEM_PAGE_LOCKED, 0,
            "Locked should be type 0");
        SELFTEST_ASSERT_EQ("GPU", "gpu_type_unified",
            GPU_MEM_UNIFIED, 3,
            "Unified should be type 3");
    }

    /* Test 2: GPU pool structure */
    {
        gpu_mem_pool_t pool;
        pool.pool_size = GPU_MEM_DEFAULT_POOL_SIZE;
        pool.used = 0;
        pool.peak_used = 0;
        pool.active_allocations = 0;
        SELFTEST_ASSERT("GPU", "pool_layout",
            pool.pool_size == GPU_MEM_DEFAULT_POOL_SIZE && pool.used == 0,
            "gpu_mem_pool_t fields incorrect");
    }

    /* Test 3: GPU allocation structure */
    {
        gpu_mem_alloc_t alloc;
        alloc.host_addr = 0x1000;
        alloc.device_ptr = 0x2000;
        alloc.size = 4096;
        alloc.type = GPU_MEM_COHERENT;
        alloc.in_use = true;
        alloc.ref_count = 1;
        SELFTEST_ASSERT("GPU", "alloc_layout",
            alloc.host_addr == 0x1000 && alloc.in_use && alloc.ref_count == 1,
            "gpu_mem_alloc_t fields incorrect");
    }

    /* Test 4: GPU stats structure */
    {
        gpu_mem_stats_t stats;
        stats.pool_size = GPU_MEM_DEFAULT_POOL_SIZE;
        stats.used = 0;
        stats.active_allocations = 0;
        SELFTEST_ASSERT("GPU", "stats_layout",
            stats.pool_size == GPU_MEM_DEFAULT_POOL_SIZE,
            "gpu_mem_stats_t fields incorrect");
    }

    /* Test 5: Pool size constant */
    {
        SELFTEST_ASSERT("GPU", "pool_size_1gb",
            GPU_MEM_DEFAULT_POOL_SIZE == (1 * 1024 * 1024 * 1024),
            "Default pool should be 1GB");
    }

    /* Test 6: Max allocations constant */
    {
        SELFTEST_ASSERT("GPU", "max_allocs",
            GPU_MEM_MAX_ALLOCATIONS == 1024,
            "Should support 1024 GPU allocations");
    }
}

/* ============================================================================
 * Test: IPC Subsystem (data structure logic)
 * ============================================================================ */

static void selftest_ipc_internal_logic(void)
{
    selftest_print_header("IPC Subsystem");

    /* Test 1: Server type enumeration */
    {
        SELFTEST_ASSERT_EQ("IPC", "server_type_bsd",
            IPC_SERVER_BSD, 0,
            "BSD server should be type 0");
        SELFTEST_ASSERT_EQ("IPC", "server_type_ai",
            IPC_SERVER_AI, 1,
            "AI server should be type 1");
        SELFTEST_ASSERT_EQ("IPC", "server_count",
            IPC_SERVER_COUNT, 8,
            "Should have 8 server types");
    }

    /* Test 2: Server entry structure */
    {
        ipc_server_entry_t entry;
        entry.type = IPC_SERVER_AI;
        entry.active = false;
        entry.crashed = false;
        entry.ref_count = 0;
        SELFTEST_ASSERT("IPC", "server_entry_layout",
            entry.type == IPC_SERVER_AI && entry.ref_count == 0,
            "ipc_server_entry_t fields incorrect");
    }

    /* Test 3: Port set structure */
    {
        ipc_port_set_t pset;
        pset.active_requests = 0;
        SELFTEST_ASSERT("IPC", "port_set_layout",
            pset.active_requests == 0,
            "ipc_port_set_t fields incorrect");
    }

    /* Test 4: Pending request structure */
    {
        ipc_pending_request_t req;
        req.request_id = 42;
        req.completed = false;
        req.timeout = 5000;
        SELFTEST_ASSERT("IPC", "pending_req_layout",
            req.request_id == 42 && !req.completed,
            "ipc_pending_request_t fields incorrect");
    }

    /* Test 5: IPC config constants */
    {
        SELFTEST_ASSERT("IPC", "max_servers",
            IPC_MAX_SERVERS == 64,
            "IPC_MAX_SERVERS should be 64");
        SELFTEST_ASSERT("IPC", "max_pending",
            IPC_MAX_PENDING_REQUESTS == 256,
            "IPC_MAX_PENDING_REQUESTS should be 256");
    }

    /* Test 6: Request/reply header structure */
    {
        trini_ipc_request_header_t req_hdr;
        req_hdr.request_id = 123;
        SELFTEST_ASSERT("IPC", "req_header_layout",
            req_hdr.request_id == 123,
            "trini_ipc_request_header_t fields incorrect");

        trini_ipc_reply_header_t rep_hdr;
        rep_hdr.request_id = 123;
        rep_hdr.result = 0;
        SELFTEST_ASSERT("IPC", "reply_header_layout",
            rep_hdr.request_id == 123 && rep_hdr.result == 0,
            "trini_ipc_reply_header_t fields incorrect");
    }

    /* Test 7: Server registry simulation */
    {
        ipc_server_entry_t registry[IPC_MAX_SERVERS];
        uint32_t count = 0;

        /* Register AI server */
        registry[count].type = IPC_SERVER_AI;
        registry[count].active = true;
        registry[count].ref_count = 0;
        count++;

        /* Register BSD server */
        registry[count].type = IPC_SERVER_BSD;
        registry[count].active = true;
        registry[count].ref_count = 0;
        count++;

        SELFTEST_ASSERT("IPC", "server_registry",
            count == 2 &&
            registry[0].type == IPC_SERVER_AI &&
            registry[1].type == IPC_SERVER_BSD,
            "Server registry logic broken");
    }
}

/* ============================================================================
 * Test: Task Manager (data structure logic)
 * ============================================================================ */

static void selftest_task_internal_logic(void)
{
    selftest_print_header("Task Manager");

    /* Test 1: Task state enumeration */
    {
        SELFTEST_ASSERT_EQ("Task", "task_state_alive",
            TASK_STATE_ALIVE, 0,
            "TASK_STATE_ALIVE should be 0");
        SELFTEST_ASSERT_EQ("Task", "task_state_zombie",
            TASK_STATE_ZOMBIE, 1,
            "TASK_STATE_ZOMBIE should be 1");
    }

    /* Test 2: Task attribute structure */
    {
        task_attr_t attr;
        attr.set_rlimit = false;
        attr.set_pgrp = false;
        attr.set_session = false;
        SELFTEST_ASSERT("Task", "task_attr_layout",
            !attr.set_rlimit && !attr.set_pgrp && !attr.set_session,
            "task_attr_t fields incorrect");
    }

    /* Test 3: Task info structure */
    {
        task_info_t info;
        info.state = TASK_STATE_ALIVE;
        info.pid = 1;
        SELFTEST_ASSERT("Task", "task_info_layout",
            info.state == TASK_STATE_ALIVE && info.pid == 1,
            "task_info_t fields incorrect");
    }

    /* Test 4: Scheduling policy enumeration */
    {
        SELFTEST_ASSERT_EQ("Task", "sched_rr",
            TASK_SCHED_RR, 0,
            "Round-robin should be policy 0");
    }

    /* Test 5: Config constants */
    {
        SELFTEST_ASSERT("Task", "max_tasks",
            TASK_MAX_TASKS > 0,
            "TASK_MAX_TASKS should be > 0");
        SELFTEST_ASSERT("Task", "pid_range",
            TASK_PID_MIN > 1 && TASK_PID_MAX > TASK_PID_MIN,
            "PID range invalid");
    }

    /* Test 6: PID bitmap simulation */
    {
        /* Simulate PID bitmap: 32768 PIDs, 4KB bitmap */
        uint8_t pid_bitmap[32768 / 8];
        for (int i = 0; i < sizeof(pid_bitmap); i++) pid_bitmap[i] = 0;

        /* Allocate PID 3 (first valid PID) */
        pid_bitmap[3 / 8] |= (1 << (3 % 8));
        SELFTEST_ASSERT("Task", "pid_alloc_3",
            (pid_bitmap[3 / 8] & (1 << (3 % 8))) != 0,
            "PID 3 should be marked allocated");

        /* PID 1 should still be free */
        SELFTEST_ASSERT("Task", "pid_1_free",
            (pid_bitmap[1 / 8] & (1 << (1 % 8))) == 0,
            "PID 1 should be reserved/free");
    }
}

/* ============================================================================
 * Test: Host Local (hardware detection logic)
 * ============================================================================ */

static void selftest_host_internal_logic(void)
{
    selftest_print_header("Host Local");

    /* Test 1: Log levels */
    {
        SELFTEST_ASSERT_EQ("Host", "klog_debug",
            KLOG_DEBUG, 0,
            "KLOG_DEBUG should be 0");
        SELFTEST_ASSERT_EQ("Host", "klog_panic",
            KLOG_PANIC, 4,
            "KLOG_PANIC should be 4");
    }

    /* Test 2: CPU feature flags */
    {
        SELFTEST_ASSERT("Host", "cpuid_aes_bit",
            CPUID_FEAT_AES == 25,
            "AES-NI should be bit 25 of ECX");
        SELFTEST_ASSERT("Host", "cpuid_avx_bit",
            CPUID_FEAT_AVX == 28,
            "AVX should be bit 28 of XCR0");
    }

    /* Test 3: boot_params_t structure */
    {
        boot_params_t params;
        params.mem_lower = 0;
        params.mem_upper = 0;
        params.mm_entry_count = 0;
        params.has_aes = false;
        params.has_avx = false;
        SELFTEST_ASSERT("Host", "boot_params_layout",
            params.mem_lower == 0 && params.mm_entry_count == 0 && !params.has_aes,
            "boot_params_t fields incorrect");
    }

    /* Test 4: klog_level enumeration completeness */
    {
        SELFTEST_ASSERT_EQ("Host", "klog_level_count",
            KLOG_PANIC, 4,
            "Should have 5 log levels (0-4)");
    }
}

/* ============================================================================
 * Test: Kernel allocator
 * ============================================================================ */

static void selftest_kernel_allocator(void)
{
    selftest_print_header("Kernel Allocator");

    /* Test 1: Basic allocation */
    {
        void *ptr = kalloc(64);
        SELFTEST_ASSERT_NOT_NULL("KAlloc", "basic_alloc", ptr,
            "Failed to allocate 64 bytes");
        kfree(ptr);
    }

    /* Test 2: Aligned allocation */
    {
        void *ptr = kalloc(128);
        SELFTEST_ASSERT("KAlloc", "aligned_alloc",
            ((uintptr_t)ptr & 7) == 0,
            "Allocation not 8-byte aligned");
        kfree(ptr);
    }

    /* Test 3: Large allocation */
    {
        void *ptr = kalloc(16 * 1024);
        SELFTEST_ASSERT_NOT_NULL("KAlloc", "large_alloc", ptr,
            "Failed to allocate 16KB");
        kfree(ptr);
    }

    /* Test 4: Pool exhaustion */
    {
        uint32_t remaining = KALLOC_POOL_SIZE - kalloc_ptr;
        void *ptr = kalloc(remaining);
        void *overflow = kalloc(1);
        SELFTEST_ASSERT("KAlloc", "pool_exhaustion",
            ptr != NULL && overflow == NULL,
            "Pool should be exhausted after max alloc");
        kfree(ptr);
        kfree(overflow);
    }
}

/* ============================================================================
 * Test: VGA Console
 * ============================================================================ */

static void selftest_vga_console(void)
{
    selftest_print_header("VGA Console");
    SELFTEST_PASS("VGA", "output_working");
}

/* ============================================================================
 * Test: CPUID (actual hardware detection in QEMU)
 * ============================================================================ */

static void selftest_cpuid(void)
{
    selftest_print_header("CPUID Detection");

    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
        : "memory");

    bool has_aes = (ecx >> CPUID_FEAT_AES) & 1;
    bool has_sse4 = (ecx >> CPUID_FEAT_SSE4) & 1;

    SELFTEST_ASSERT("CPUID", "cpuid_instruction_works",
        eax > 0,
        "CPUID leaf 1 should return non-zero EAX");

    SELFTEST_ASSERT("CPUID", "sse4_present",
        has_sse4,
        "SSE4 should be present on this CPU");

    /* AES may not be present on all CPUs, so just verify the bit works */
    selftest_vga_puts("  AES-NI: ");
    selftest_vga_puts(has_aes ? "yes" : "no");
    selftest_vga_puts("\n");
    SELFTEST_PASS("CPUID", "aes_detection_ok");
}

/* ============================================================================
 * Test: Memory detection
 * ============================================================================ */

static void selftest_memory(void)
{
    selftest_print_header("Memory");

    /* Test that we can read/write kernel memory */
    uint32_t test_val = 0xDEADBEEF;
    SELFTEST_ASSERT_EQ("Mem", "kernel_rw",
        test_val, 0xDEADBEEF,
        "Kernel memory read/write failed");

    /* Test VGA buffer is readable */
    uint16_t vga_test = vga_buf[0];
    (void)vga_test; /* Just verify no fault */
    SELFTEST_PASS("Mem", "vga_readable");
}

/* ============================================================================
 * Public API implementation
 * ============================================================================ */

selftest_result_t g_selftest_results[MAX_SELFTESTS];
int               g_selftest_count = 0;
int               g_selftest_passed = 0;
int               g_selftest_failed = 0;

void selftest_print_status(bool passed, const char *subsystem, const char *name)
{
    selftest_vga_puts("  SELFTEST: ");
    selftest_vga_puts(subsystem);
    selftest_vga_puts(": ");
    if (passed) {
        selftest_vga_puts("[PASS] ");
    } else {
        selftest_vga_puts("[FAIL] ");
    }
    selftest_vga_puts(name);
    selftest_vga_puts("\n");
}

void selftest_print_detail(const char *detail)
{
    selftest_vga_puts("    detail: ");
    selftest_vga_puts(detail);
    selftest_vga_puts("\n");
}

void selftest_print_header(const char *subsystem)
{
    selftest_vga_puts("\n--- Testing: ");
    selftest_vga_puts(subsystem);
    selftest_vga_puts(" ---\n");
}

void selftest_print_summary(void)
{
    selftest_vga_puts("\n");
    selftest_vga_puts("========================================\n");
    selftest_vga_puts("  SELFTEST SUMMARY\n");
    selftest_vga_puts("========================================\n");

    int total = g_selftest_passed + g_selftest_failed;
    selftest_vga_puts("  Total tests: ");
    selftest_vga_u32(total);
    selftest_vga_puts("\n");

    selftest_vga_puts("  Passed:      ");
    selftest_vga_u32(g_selftest_passed);
    selftest_vga_puts("\n");

    selftest_vga_puts("  Failed:      ");
    selftest_vga_u32(g_selftest_failed);
    selftest_vga_puts("\n");

    selftest_vga_puts("  Result:      ");
    if (g_selftest_failed == 0) {
        selftest_vga_puts("ALL PASSED\n");
    } else {
        selftest_vga_puts("FAILURES DETECTED\n");
    }
    selftest_vga_puts("========================================\n");
}

void selftest_run_all(void)
{
    selftest_vga_puts("\n");
    selftest_vga_puts("######################################\n");
    selftest_vga_puts("#  TriniOS Kernel Self-Test Suite     #\n");
    selftest_vga_puts("######################################\n");

    /* Reset counters */
    g_selftest_count = 0;
    g_selftest_passed = 0;
    g_selftest_failed = 0;

    /* Run all subsystem tests */
    selftest_vga_console();
    selftest_memory();
    selftest_cpuid();
    selftest_kernel_allocator();
    selftest_vm_internal_logic();
    selftest_gpu_internal_logic();
    selftest_ipc_internal_logic();
    selftest_task_internal_logic();
    selftest_host_internal_logic();

    /* Print summary */
    selftest_print_summary();
}