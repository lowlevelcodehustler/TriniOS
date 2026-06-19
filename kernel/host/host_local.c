/*
 * TriniOS - Host Local Kernel Implementation (Stub)
 *
 * Stub implementations for freestanding builds.
 * Real hardware detection and host management will be wired up later.
 */

#include "host_local.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ============================================================================
 * Internal State
 * ============================================================================ */

static trini_host_info_t g_host_info;
static bool g_initialized = false;
/* Boot time tracked via multiboot info, not used yet */
// static uint64_t g_boot_time_ns = 0;

/* Simple monotonic counter (not real hardware clock yet) */
static uint64_t g_monotonic_counter = 0;

/* ============================================================================
 * klog - Kernel log
 * ============================================================================ */

#define KLOG_MAX_DEPTH 1024
static int g_log_level = KLOG_LEVEL_INFO;
static char g_klog_buf[KLOG_MAX_DEPTH];

void klog(int level, const char *fmt, ...)
{
    if (level > g_log_level) return;

    va_list args;
    va_start(args, fmt);
    vsnprintf(g_klog_buf, sizeof(g_klog_buf), fmt, args);
    va_end(args);

    /* Write to VGA text mode buffer for freestanding build */
    uint16_t *vga = (uint16_t *)0xB8000;
    static int row = 24;
    static int col = 0;

    /* Simple write: just append characters */
    for (int i = 0; g_klog_buf[i] && row < 25; i++) {
        char c = g_klog_buf[i];
        if (c == '\n') {
            row++;
            col = 0;
        } else if (c == '\r') {
            col = 0;
        } else {
            vga[row * 80 + col] = (uint16_t)((0x07 << 8) | (uint8_t)c);
            col++;
            if (col >= 80) { col = 0; row++; }
        }
    }

    /* Scroll if needed */
    if (row >= 25) {
        for (int r = 1; r < 25; r++) {
            for (int c = 0; c < 80; c++) {
                vga[(r - 1) * 80 + c] = vga[r * 80 + c];
            }
        }
        row = 24;
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
 * host_bootstrap - Main kernel entry point (stub)
 * ============================================================================ */

void host_bootstrap(void)
{
    /* Initialize host info with defaults */
    memset(&g_host_info, 0, sizeof(g_host_info));
    strncpy(g_host_info.hostname, "trinos", HOST_NAME_MAX_LEN - 1);
    strncpy(g_host_info.version, "TriniOS 0.1.0-dev", HOST_VERSION_MAX_LEN - 1);
    strncpy(g_host_info.model, "x86_64-generic", HOST_MODEL_MAX_LEN - 1);
    g_host_info.cpu_count = 1;
    g_host_info.page_size = 4096;
    g_host_info.phys_memory = 0; /* Will be set from multiboot memory map */
    g_host_info.boot_time = 0;
    g_host_info.uptime = 0;
    g_host_info.secure_boot = false;
    g_host_info.aes_ni = true;
    g_host_info.sse4 = true;
    g_host_info.avx = true;

    g_monotonic_counter = 1;
    g_initialized = true;

    klog(KLOG_LEVEL_INFO, "TriniOS host_bootstrap complete\n");
}

/* ============================================================================
 * Boot parameter parsing (stub)
 * ============================================================================ */

kern_return_t host_parse_boot_params(boot_params_t *params)
{
    if (!params) return KERN_INVALID_ARGUMENT;
    /* Stub: in real implementation, parse multiboot info into boot params */
    return KERN_SUCCESS;
}

kern_return_t host_init_subsystems(void)
{
    if (g_initialized) return KERN_SUCCESS;
    host_bootstrap();
    return KERN_SUCCESS;
}

/* ============================================================================
 * Host Information (stub implementations)
 * ============================================================================ */

kern_return_t host_get_info(trini_host_info_t *info)
{
    if (!info || !g_initialized) {
        return KERN_INVALID_ARGUMENT;
    }
    memcpy(info, &g_host_info, sizeof(trini_host_info_t));
    info->uptime = host_get_uptime_s();
    return KERN_SUCCESS;
}

kern_return_t host_get_processor_info(uint32_t cpu_id, trini_processor_info_t *info)
{
    if (!info || !g_initialized || cpu_id >= g_host_info.cpu_count) {
        return KERN_INVALID_ARGUMENT;
    }
    memset(info, 0, sizeof(trini_processor_info_t));
    info->cpu_id = cpu_id;
    info->active = true;
    info->freq = 2400000000ULL; /* Stub: 2.4GHz */
    return KERN_SUCCESS;
}

uint32_t host_get_cpu_count(void)
{
    if (!g_initialized) return 0;
    return g_host_info.cpu_count;
}

kern_return_t host_get_memory_info(trini_memory_info_t *info)
{
    if (!info || !g_initialized) {
        return KERN_INVALID_ARGUMENT;
    }
    memset(info, 0, sizeof(trini_memory_info_t));
    info->phys_installed = g_host_info.phys_memory;
    info->page_size = g_host_info.page_size;
    /* Stub: no real VM stats yet */
    return KERN_SUCCESS;
}

kern_return_t host_set_hostname(const char *name)
{
    if (!g_initialized || !name) return KERN_INVALID_ARGUMENT;
    strncpy(g_host_info.hostname, name, HOST_NAME_MAX_LEN - 1);
    g_host_info.hostname[HOST_NAME_MAX_LEN - 1] = '\0';
    return KERN_SUCCESS;
}

kern_return_t host_get_hostname(char *name, uint32_t *name_len)
{
    if (!g_initialized || !name || !name_len) return KERN_INVALID_ARGUMENT;
    strncpy(name, g_host_info.hostname, HOST_NAME_MAX_LEN);
    name[HOST_NAME_MAX_LEN - 1] = '\0';
    *name_len = (uint32_t)strlen(g_host_info.hostname);
    return KERN_SUCCESS;
}

/* ============================================================================
 * System Time (stub implementations using monotonic counter)
 * ============================================================================ */

uint64_t host_get_uptime_ns(void)
{
    if (!g_initialized) return 0;
    g_monotonic_counter += 1; /* Stub: increment by 1ns per call */
    return g_monotonic_counter;
}

uint64_t host_get_uptime_s(void)
{
    return host_get_uptime_ns() / 1000000000ULL;
}

uint64_t host_get_monotonic_time(void)
{
    return host_get_uptime_ns();
}

kern_return_t host_get_wall_clock(host_timeval_t *tv)
{
    if (!tv || !g_initialized) return KERN_INVALID_ARGUMENT;
    tv->tv_sec = 0; /* Stub: no real wall clock */
    tv->tv_nsec = 0;
    return KERN_SUCCESS;
}

/* ============================================================================
 * System Control (stub implementations - halt on reboot/panic)
 * ============================================================================ */

void host_reboot(void)
{
    klog(KLOG_LEVEL_ERROR, "TriniOS: Reboot requested — halting\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void host_halt(void)
{
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void host_panic(const char *fmt, ...)
{
    klog(KLOG_LEVEL_ERROR, "*** KERNEL PANIC: ");
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(g_klog_buf, sizeof(g_klog_buf), fmt, args);
        va_end(args);
        klog(KLOG_LEVEL_ERROR, g_klog_buf);
    }
    klog(KLOG_LEVEL_ERROR, " ***\n");

    /* Dump host info */
    if (g_initialized) {
        klog(KLOG_LEVEL_ERROR, "  Hostname: %s\n", g_host_info.hostname);
        klog(KLOG_LEVEL_ERROR, "  CPUs:     %u\n", g_host_info.cpu_count);
    }

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

/* ============================================================================
 * Memory Management (stub implementations)
 * ============================================================================ */

uint32_t host_get_mem_map(mem_range_t *ranges, uint32_t max_ranges)
{
    (void)max_ranges; /* Stub: always returns 1 region */
    if (!ranges || !g_initialized) return 0;
    /* Stub: single usable memory region from 1MB upwards */
    ranges[0].base = 1024 * 1024;
    ranges[0].size = 0x7E000000; /* ~2GB */
    ranges[0].type = 0; /* usable */
    return 1;
}

uint64_t host_get_phys_memory(void)
{
    if (!g_initialized) return 0;
    return g_host_info.phys_memory;
}

/* ============================================================================
 * CPU Features (stub implementations)
 * ============================================================================ */

bool host_cpu_has_feature(const char *feature)
{
    if (!g_initialized || !feature) return false;
    /* Stub: report all known features as supported */
    if (strcmp(feature, "AES-NI") == 0) return g_host_info.aes_ni;
    if (strcmp(feature, "SSE4") == 0) return g_host_info.sse4;
    if (strcmp(feature, "AVX") == 0) return g_host_info.avx;
    return false;
}

void host_get_cpu_features(char *buffer, uint32_t buffer_size)
{
    if (!g_initialized || !buffer || buffer_size == 0) return;
    buffer[0] = '\0';
    uint32_t offset = 0;
    if (g_host_info.aes_ni && offset < buffer_size - 7) {
        offset += (uint32_t)snprintf(buffer + offset, buffer_size - offset, "AES-NI ");
    }
    if (g_host_info.sse4 && offset < buffer_size - 5) {
        offset += (uint32_t)snprintf(buffer + offset, buffer_size - offset, "SSE4.1 SSE4.2 ");
    }
    if (g_host_info.avx && offset < buffer_size - 4) {
        offset += (uint32_t)snprintf(buffer + offset, buffer_size - offset, "AVX ");
    }
}

/* ============================================================================
 * Debug / Diagnostics (stub implementations)
 * ============================================================================ */

void host_print_status(void)
{
    if (!g_initialized) {
        klog(KLOG_LEVEL_ERROR, "  Host not initialized\n");
        return;
    }
    klog(KLOG_LEVEL_INFO, "=== TriniOS System Status ===\n");
    klog(KLOG_LEVEL_INFO, "  Hostname: %s\n", g_host_info.hostname);
    klog(KLOG_LEVEL_INFO, "  Version:  %s\n", g_host_info.version);
    klog(KLOG_LEVEL_INFO, "  Model:    %s\n", g_host_info.model);
    klog(KLOG_LEVEL_INFO, "  CPUs:     %u\n", g_host_info.cpu_count);
    if (g_host_info.phys_memory) {
        klog(KLOG_LEVEL_INFO, "  Memory:   %lu MB\n",
             (unsigned long)(g_host_info.phys_memory / (1024 * 1024)));
    }
    klog(KLOG_LEVEL_INFO, "  Page Size: %u bytes\n", g_host_info.page_size);
}

void host_dump_mem_map(void)
{
    mem_range_t ranges[64];
    uint32_t count = host_get_mem_map(ranges, 64);
    klog(KLOG_LEVEL_INFO, "Memory map: %u regions\n", count);
    for (uint32_t i = 0; i < count && i < 64; i++) {
        klog(KLOG_LEVEL_INFO, "  [%u] base=0x%lx size=0x%lx type=%u\n",
             i, (unsigned long)ranges[i].base,
             (unsigned long)ranges[i].size, ranges[i].type);
    }
}

void host_dump_processor_info(void)
{
    if (!g_initialized) return;
    klog(KLOG_LEVEL_INFO, "Processor info: %u CPUs\n", g_host_info.cpu_count);
    for (uint32_t i = 0; i < g_host_info.cpu_count && i < 64; i++) {
        trini_processor_info_t info;
        host_get_processor_info(i, &info);
        klog(KLOG_LEVEL_INFO, "  CPU[%u]: active=%s freq=%luMHz load=%u\n",
             i, info.active ? "yes" : "no",
             (unsigned long)(info.freq / 1000000), info.load);
    }
}