/*
 * TriniOS - Host Local Kernel Implementation
 *
 * Machine-specific operations, host information, and system initialization.
 */

#include "host_local.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/* Forward declarations for subsystems */
#include "../ipc/ipc_port.h"
#include "../task/task_manager.h"
#include "../mm/vm_manager.h"
#include "../mm/gpu_mem.h"

/* ============================================================================
 * Internal State
 * ============================================================================ */

static host_info_t g_host_info;
static bool g_initialized = false;
static int g_log_level = KLOG_LEVEL_INFO;
static uint64_t g_boot_time_ns = 0;

/* CPU feature detection cache */
static char g_cpu_features[512];
static bool g_cpu_features_cached = false;

/* Memory map cache */
#define MAX_MEM_RANGES 64
static mem_range_t g_mem_ranges[MAX_MEM_RANGES];
static uint32_t g_mem_range_count = 0;

/* ============================================================================
 * Kernel Logging
 * ============================================================================ */

void klog(int level, const char *fmt, ...)
{
    if (level < g_log_level) {
        return;
    }

    const char *prefix;
    switch (level) {
        case KLOG_LEVEL_DEBUG: prefix = "[DEBUG]"; break;
        case KLOG_LEVEL_INFO:  prefix = "[INFO]";  break;
        case KLOG_LEVEL_WARN:  prefix = "[WARN]";  break;
        case KLOG_LEVEL_ERROR: prefix = "[ERROR]"; break;
        case KLOG_LEVEL_PANIC: prefix = "[PANIC]"; break;
        default:              prefix = "[LOG]";   break;
    }

    va_list args;
    va_start(args, fmt);
    printf("%s ", prefix);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);

    /* If panic, halt */
    if (level == KLOG_LEVEL_PANIC) {
        host_halt();
    }
}

void klog_set_level(int level)
{
    g_log_level = level;
}

int klog_get_level(void)
{
    return g_log_level;
}

/* ============================================================================
 * Bootstrap
 * ============================================================================ */

void host_bootstrap(void)
{
    klog(KLOG_LEVEL_INFO, "TriniOS kernel bootstrap starting...");

    /* Record boot time */
    g_boot_time_ns = host_get_monotonic_time();
    g_host_info.boot_time = g_boot_time_ns;

    /* Detect hardware */
    host_detect_hardware();

    /* Initialize all subsystems */
    kern_return_t kr = host_init_subsystems();
    if (kr != KERN_SUCCESS) {
        klog(KLOG_LEVEL_PANIC, "Failed to initialize subsystems: %d", kr);
    }

    klog(KLOG_LEVEL_INFO, "TriniOS kernel bootstrap complete");
    host_print_status();
}

kern_return_t host_parse_boot_params(boot_params_t *params)
{
    if (!params) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Parse command line for kernel options */
    /* TODO: Implement command line parsing
     * - console=ttyS0 for serial console
     * - loglevel=N for debug verbosity
     * - initrd=addr,size for initrd location
     * - mem=N to limit memory
     */

    return KERN_SUCCESS;
}

kern_return_t host_init_subsystems(void)
{
    kern_return_t kr;

    klog(KLOG_LEVEL_INFO, "Initializing kernel subsystems...");

    /* 1. IPC subsystem */
    klog(KLOG_LEVEL_INFO, "  Initializing IPC subsystem...");
    kr = ipc_port_init();
    if (kr != KERN_SUCCESS) {
        klog(KLOG_LEVEL_ERROR, "IPC init failed: %d", kr);
        return kr;
    }

    /* 2. Task manager */
    klog(KLOG_LEVEL_INFO, "  Initializing task manager...");
    kr = task_manager_init();
    if (kr != KERN_SUCCESS) {
        klog(KLOG_LEVEL_ERROR, "Task manager init failed: %d", kr);
        return kr;
    }

    /* 3. GPU memory manager (if GPU present) */
    klog(KLOG_LEVEL_INFO, "  Initializing GPU memory manager...");
    kr = gpu_mem_init(GPU_MEM_DEFAULT_POOL_SIZE);
    if (kr != KERN_SUCCESS) {
        klog(KLOG_LEVEL_WARN, "GPU memory init failed (continuing without GPU): %d", kr);
        /* Non-fatal - continue without GPU */
    }

    g_initialized = true;
    klog(KLOG_LEVEL_INFO, "All subsystems initialized successfully");

    return KERN_SUCCESS;
}

/* ============================================================================
 * Hardware Detection
 * ============================================================================ */

static void host_detect_hardware(void)
{
    /* Set defaults */
    strncpy(g_host_info.hostname, "trinos", HOST_NAME_MAX_LEN - 1);
    strncpy(g_host_info.version, "TriniOS 0.1.0-dev", HOST_VERSION_MAX_LEN - 1);
    strncpy(g_host_info.model, "x86_64-generic", HOST_MODEL_MAX_LEN - 1);
    g_host_info.page_size = vm_page_size;
    g_host_info.secure_boot = false;

    /* Detect CPU count from Mach host */
    host_processor_info_data_t *proc_info;
    mach_msg_type_number_t proc_count;
    kern_return_t kr = host_processor_info(mach_host_self(),
                                           HOST_PROCESSOR_INFO,
                                           (host_info_t *)&proc_info,
                                           &proc_count);
    if (kr == KERN_SUCCESS) {
        g_host_info.cpu_count = proc_count;
        vm_deallocate(mach_task_self(),
                      (vm_offset_t)proc_info,
                      proc_count * sizeof(host_processor_info_t));
    } else {
        g_host_info.cpu_count = 1;
    }

    /* Detect physical memory */
    vm_size_t mem_size = 0;
    kr = host_page_size(mach_host_self(), &mem_size);
    if (kr != KERN_SUCCESS) {
        /* Fallback: use host info */
        host_basic_info_data_t basic_info;
        mach_msg_type_number_t info_count = HOST_BASIC_INFO_COUNT;
        kr = host_info(mach_host_self(), HOST_BASIC_INFO,
                       (host_info_t *)&basic_info, &info_count);
        if (kr == KERN_SUCCESS) {
            g_host_info.phys_memory = basic_info.max_mem;
            g_host_info.cpu_type = basic_info.cpu_type;
            g_host_info.cpu_subtype = basic_info.cpu_subtype;
        }
    }

    /* Detect CPU features */
    host_detect_cpu_features();

    klog(KLOG_LEVEL_INFO, "Hardware: %s, %u CPU(s), %lu MB RAM",
         g_host_info.model, g_host_info.cpu_count,
         (unsigned long)(g_host_info.phys_memory / (1024 * 1024)));
}

/* ============================================================================
 * Host Information
 * ============================================================================ */

kern_return_t host_get_info(host_info_t *info)
{
    if (!info || !g_initialized) {
        return KERN_INVALID_ARGUMENT;
    }

    memcpy(info, &g_host_info, sizeof(host_info_t));
    info->uptime = host_get_uptime_s();
    return KERN_SUCCESS;
}

kern_return_t host_get_processor_info(uint32_t cpu_id, processor_info_t *info)
{
    if (!info || cpu_id >= g_host_info.cpu_count) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Populate basic info */
    info->cpu_id = cpu_id;
    info->active = true;
    info->freq = g_host_info.cpu_freq;
    info->load = 0;
    info->temp = 0;
    info->user_time = 0;
    info->system_time = 0;
    info->idle_time = 0;
    info->interrupt_time = 0;

    /* Try to get processor set info for load */
    processor_set_basic_info_data_t psinfo;
    mach_msg_type_number_t pscount = PROCESSOR_SET_BASIC_INFO_COUNT;
    kern_return_t kr = processor_set_info(mach_processor_self(),
                                          PROCESSOR_SET_BASIC_INFO,
                                          (processor_set_info_t)&psinfo,
                                          &pscount);
    if (kr == KERN_SUCCESS) {
        info->load = (psinfo.load_avg[0] > 0) ? 100 : 0;
    }

    return KERN_SUCCESS;
}

uint32_t host_get_cpu_count(void)
{
    return g_host_info.cpu_count;
}

kern_return_t host_get_memory_info(memory_info_t *info)
{
    if (!info) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Get VM statistics from Mach */
    vm_statistics_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    kern_return_t kr = host_page_info(mach_host_self(),
                                      HOST_VM_INFO,
                                      (host_info_t)&vm_stat,
                                      &count);

    if (kr == KERN_SUCCESS) {
        info->phys_installed = g_host_info.phys_memory;
        info->page_size = g_host_info.page_size;
        info->totalPages = info->phys_installed / info->page_size;
        info->free_count = vm_stat.free_count;
        info->active_count = vm_stat.active_count;
        info->inactive_count = vm_stat.inactive_count;
        info->wired_count = vm_stat.wired_count;
        info->speculative_count = vm_stat.speculative_count;
        info->phys_available = vm_stat.free_count * info->page_size;
    } else {
        /* Fallback */
        info->phys_installed = g_host_info.phys_memory;
        info->page_size = g_host_info.page_size;
        info->totalPages = info->phys_installed / info->page_size;
        info->free_count = info->totalPages;
    }

    return KERN_SUCCESS;
}

kern_return_t host_set_hostname(const char *name)
{
    if (!name) {
        return KERN_INVALID_ARGUMENT;
    }

    strncpy(g_host_info.hostname, name, HOST_NAME_MAX_LEN - 1);
    g_host_info.hostname[HOST_NAME_MAX_LEN - 1] = '\0';
    return KERN_SUCCESS;
}

kern_return_t host_get_hostname(char *name, uint32_t *name_len)
{
    if (!name || !name_len) {
        return KERN_INVALID_ARGUMENT;
    }

    uint32_t len = strlen(g_host_info.hostname);
    if (*name_len < len + 1) {
        return KERN_INVALID_ARGUMENT;
    }

    strcpy(name, g_host_info.hostname);
    *name_len = len + 1;
    return KERN_SUCCESS;
}

/* ============================================================================
 * System Time
 * ============================================================================ */

uint64_t host_get_uptime_ns(void)
{
    if (g_boot_time_ns == 0) {
        return 0;
    }
    return host_get_monotonic_time() - g_boot_time_ns;
}

uint64_t host_get_uptime_s(void)
{
    return host_get_uptime_ns() / 1000000000ULL;
}

uint64_t host_get_monotonic_time(void)
{
    /* Use Mach host info for uptime */
    host_clock_info_data_t clock_info;
    mach_msg_type_number_t count = HOST_CLOCK_INFO_COUNT;

    kern_return_t kr = host_info(mach_host_self(), HOST_CLOCK_INFO,
                                 (host_info_t)&clock_info, &count);
    if (kr == KERN_SUCCESS) {
        return clock_info.system_clock;
    }

    /* Fallback: use mach_absolute_time */
    return mach_absolute_time();
}

kern_return_t host_get_wall_clock(host_timeval_t *tv)
{
    if (!tv) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Use host_info to get calendar time */
    host_clock_info_data_t clock_info;
    mach_msg_type_number_t count = HOST_CLOCK_INFO_COUNT;

    kern_return_t kr = host_info(mach_host_self(), HOST_CLOCK_INFO,
                                 (host_info_t)&clock_info, &count);
    if (kr == KERN_SUCCESS) {
        tv->tv_sec = clock_info.calendar_time / 1000000000ULL;
        tv->tv_nsec = clock_info.calendar_time % 1000000000ULL;
        return KERN_SUCCESS;
    }

    /* Fallback */
    tv->tv_sec = host_get_uptime_s();
    tv->tv_nsec = (host_get_uptime_ns() % 1000000000ULL);
    return KERN_SUCCESS;
}

/* ============================================================================
 * System Control
 * ============================================================================ */

void host_reboot(void)
{
    klog(KLOG_LEVEL_INFO, "System reboot initiated");

    /* Flush all I/O */
    /* Shutdown subsystems */
    gpu_mem_shutdown();
    task_manager_shutdown();
    ipc_port_shutdown();

    /* Request reboot via Mach */
    host_priv_info_data_t host_info;
    mach_msg_type_number_t count = HOST_PRIV_INFO_COUNT;
    host_info(mach_host_self(), HOST_PRIV_INFO,
              (host_info_t)&host_info, &count);

    /* Fallback: trigger triple fault */
    klog(KLOG_LEVEL_INFO, "Rebooting via machine check...");
    __asm__ volatile("int $3");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void host_halt(void)
{
    klog(KLOG_LEVEL_INFO, "System halted");

    /* Shutdown subsystems */
    gpu_mem_shutdown();
    task_manager_shutdown();
    ipc_port_shutdown();

    /* Halt */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void host_panic(const char *fmt, ...)
{
    printf("\n\n*** KERNEL PANIC ***\n");

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");

    /* Dump host info */
    printf("Uptime: %lu seconds\n", (unsigned long)host_get_uptime_s());
    printf("Hostname: %s\n", g_host_info.hostname);

    /* Halt */
    host_halt();
}

/* ============================================================================
 * Memory Management
 * ============================================================================ */

uint32_t host_get_mem_map(mem_range_t *ranges, uint32_t max_ranges)
{
    if (!ranges || max_ranges == 0) {
        return 0;
    }

    uint32_t count = (g_mem_range_count < max_ranges) ? g_mem_range_count : max_ranges;
    memcpy(ranges, g_mem_ranges, count * sizeof(mem_range_t));
    return count;
}

uint64_t host_get_phys_memory(void)
{
    return g_host_info.phys_memory;
}

/* ============================================================================
 * CPU Features
 * ============================================================================ */

static void host_detect_cpu_features(void)
{
    /* Use cpuid to detect features */
    uint32_t eax, ebx, ecx, edx;

    /* CPUID leaf 1: feature flags */
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1)
                     : "memory");

    g_host_info.aes_ni = (ecx & (1 << 25)) != 0;
    g_host_info.sse4 = (ecx & (1 << 19)) != 0;
    g_host_info.avx = (ecx & (1 << 28)) != 0;

    /* Build feature string */
    char *buf = g_cpu_features;
    int remaining = sizeof(g_cpu_features);

    if (g_host_info.sse4) {
        remaining -= snprintf(buf, remaining, "SSE4 ");
        buf += 4;
    }
    if (g_host_info.avx) {
        remaining -= snprintf(buf, remaining, "AVX ");
        buf += 3;
    }
    if (g_host_info.aes_ni) {
        remaining -= snprintf(buf, remaining, "AES-NI ");
        buf += 6;
    }

    g_cpu_features_cached = true;
}

bool host_cpu_has_feature(const char *feature)
{
    if (!feature) {
        return false;
    }

    if (g_cpu_features_cached) {
        return strstr(g_cpu_features, feature) != NULL;
    }

    /* Check known features directly */
    if (strcmp(feature, "AES-NI") == 0) return g_host_info.aes_ni;
    if (strcmp(feature, "SSE4") == 0) return g_host_info.sse4;
    if (strcmp(feature, "AVX") == 0) return g_host_info.avx;

    return false;
}

void host_get_cpu_features(char *buffer, uint32_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return;
    }

    if (!g_cpu_features_cached) {
        host_detect_cpu_features();
    }

    strncpy(buffer, g_cpu_features, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
}

/* ============================================================================
 * Debug / Diagnostics
 * ============================================================================ */

void host_print_status(void)
{
    klog(KLOG_LEVEL_INFO, "=== TriniOS System Status ===");
    klog(KLOG_LEVEL_INFO, "  Hostname: %s", g_host_info.hostname);
    klog(KLOG_LEVEL_INFO, "  Version:  %s", g_host_info.version);
    klog(KLOG_LEVEL_INFO, "  Model:    %s", g_host_info.model);
    klog(KLOG_LEVEL_INFO, "  CPUs:     %u", g_host_info.cpu_count);
    klog(KLOG_LEVEL_INFO, "  RAM:      %lu MB",
         (unsigned long)(g_host_info.phys_memory / (1024 * 1024)));
    klog(KLOG_LEVEL_INFO, "  Page Size: %u bytes", g_host_info.page_size);
    klog(KLOG_LEVEL_INFO, "  Uptime:   %lu seconds", (unsigned long)host_get_uptime_s());

    char features[512];
    host_get_cpu_features(features, sizeof(features));
    klog(KLOG_LEVEL_INFO, "  CPU Features: %s", features);

    /* Task count */
    klog(KLOG_LEVEL_INFO, "  Active Tasks: %u", task_get_total_count());

    /* GPU status */
    gpu_mem_stats_t gpu_stats;
    gpu_mem_get_stats(&gpu_stats);
    klog(KLOG_LEVEL_INFO, "  GPU Pool: %lu MB (used: %lu MB)",
         (unsigned long)(gpu_stats.pool_size / (1024 * 1024)),
         (unsigned long)(gpu_stats.used / (1024 * 1024)));

    klog(KLOG_LEVEL_INFO, "===========================");
}

void host_dump_mem_map(void)
{
    klog(KLOG_LEVEL_INFO, "Memory Map (%u ranges):", g_mem_range_count);
    for (uint32_t i = 0; i < g_mem_range_count; i++) {
        klog(KLOG_LEVEL_INFO, "  [%u] 0x%016lx - 0x%016lx (%lu MB) type=%u",
             i,
             (unsigned long)g_mem_ranges[i].base,
             (unsigned long)(g_mem_ranges[i].base + g_mem_ranges[i].size),
             (unsigned long)(g_mem_ranges[i].size / (1024 * 1024)),
             g_mem_ranges[i].type);
    }
}

void host_dump_processor_info(void)
{
    for (uint32_t i = 0; i < g_host_info.cpu_count; i++) {
        processor_info_t info;
        if (host_get_processor_info(i, &info) == KERN_SUCCESS) {
            klog(KLOG_LEVEL_INFO, "CPU %u: active=%d freq=%lu Hz load=%u%%",
                 i, info.active, (unsigned long)info.freq, info.load);
        }
    }
}