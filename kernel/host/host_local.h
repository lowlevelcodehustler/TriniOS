/*
 * TriniOS - Host Local Kernel
 *
 * Machine-specific operations, host information, and system initialization.
 * Provides the bootstrap entry point for the TriniOS kernel.
 */

#ifndef HOST_LOCAL_H
#define HOST_LOCAL_H

#include <mach/mach.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * System Information
 * ============================================================================ */

#define HOST_NAME_MAX_LEN         64
#define HOST_VERSION_MAX_LEN      128
#define HOST_MODEL_MAX_LEN        128
#define HOST_SERIAL_MAX_LEN       64

/* ============================================================================
 * Host Information Structure
 * ============================================================================ */

typedef struct {
    char                hostname[HOST_NAME_MAX_LEN];
    char                version[HOST_VERSION_MAX_LEN];
    char                model[HOST_MODEL_MAX_LEN];
    char                serial[HOST_SERIAL_MAX_LEN];
    uint64_t            phys_memory;        /* Total physical memory (bytes) */
    uint32_t            cpu_count;          /* Number of CPUs */
    uint32_t            cpu_type;           /* CPU architecture type */
    uint32_t            cpu_subtype;        /* CPU subtype */
    uint64_t            cpu_freq;           /* CPU frequency (Hz) */
    uint32_t            page_size;          /* System page size */
    uint64_t            boot_time;          /* Boot timestamp */
    uint64_t            uptime;             /* System uptime (seconds) */
    bool                secure_boot;        /* Secure boot enabled */
    bool                aes_ni;             /* AES-NI support */
    bool                sse4;               /* SSE4 support */
    bool                avx;                /* AVX support */
} trini_host_info_t;

/* ============================================================================
 * Processor Information
 * ============================================================================ */

typedef struct {
    uint32_t            cpu_id;
    uint64_t            user_time;          /* User mode time (ticks) */
    uint64_t            system_time;        /* System mode time (ticks) */
    uint64_t            idle_time;          /* Idle time (ticks) */
    uint64_t            interrupt_time;     /* Interrupt handling time (ticks) */
    uint64_t            freq;               /* Current frequency (Hz) */
    uint32_t            load;               /* Current load (0-100%) */
    uint32_t            temp;               /* Temperature (Celsius) */
    bool                active;             /* Processor is active */
} trini_processor_info_t;

/* ============================================================================
 * Memory Information
 * ============================================================================ */

typedef struct {
    uint64_t            phys_installed;     /* Installed physical memory */
    uint64_t            phys_available;     /* Available physical memory */
    uint64_t            wired_count;        /* Wired (pinned) pages */
    uint64_t            active_count;       /* Active pages */
    uint64_t            inactive_count;     /* Inactive pages */
    uint64_t            speculative_count;  /* Speculative pages */
    uint64_t            free_count;         /* Free pages */
    uint64_t            page_size;          /* Page size */
    uint64_t            totalPages;         /* Total pages */
} trini_memory_info_t;

/* ============================================================================
 * Boot Parameters
 * ============================================================================ */

typedef struct {
    uint64_t            kernel_base;        /* Kernel load address */
    uint64_t            kernel_size;        /* Kernel size */
    uint64_t            initrd_base;        /* Initrd load address */
    uint64_t            initrd_size;        /* Initrd size */
    uint64_t            mem_map_base;       /* Memory map base */
    uint64_t            mem_map_size;       /* Memory map size */
    uint64_t            cmdline_base;       /* Command line base */
    uint64_t            cmdline_size;       /* Command line size */
    char                cmdline[1024];      /* Boot command line */
} boot_params_t;

/* ============================================================================
 * Host Local Functions
 * ============================================================================ */

/* ============================================================================
 * Bootstrap
 * ============================================================================ */

/* Main kernel entry point (called by bootloader) */
void host_bootstrap(void);

/* Parse boot parameters */
kern_return_t host_parse_boot_params(boot_params_t *params);

/* Initialize all kernel subsystems */
kern_return_t host_init_subsystems(void);

/* ============================================================================
 * Host Information
 * ============================================================================ */

/* Get host information */
kern_return_t host_get_info(trini_host_info_t *info);

/* Get processor information */
kern_return_t host_get_processor_info(uint32_t cpu_id, trini_processor_info_t *info);

/* Get number of processors */
uint32_t host_get_cpu_count(void);

/* Get memory information */
kern_return_t host_get_memory_info(trini_memory_info_t *info);

/* Set hostname */
kern_return_t host_set_hostname(const char *name);

/* Get hostname */
kern_return_t host_get_hostname(char *name, uint32_t *name_len);

/* ============================================================================
 * System Time
 * ============================================================================ */

/* Get current time (nanoseconds since boot) */
uint64_t host_get_uptime_ns(void);

/* Get current time (seconds since boot) */
uint64_t host_get_uptime_s(void);

/* Get monotonic clock (nanoseconds) */
uint64_t host_get_monotonic_time(void);

/* Get wall clock time */
typedef struct {
    uint64_t    tv_sec;     /* Seconds */
    uint64_t    tv_nsec;    /* Nanoseconds */
} host_timeval_t;

kern_return_t host_get_wall_clock(host_timeval_t *tv);

/* ============================================================================
 * System Control
 * ============================================================================ */

/* Reboot the system */
void host_reboot(void) __attribute__((noreturn));

/* Halt the system */
void host_halt(void) __attribute__((noreturn));

/* Panic the system */
void host_panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* ============================================================================
 * Memory Management
 * ============================================================================ */

/* Get physical memory map */
typedef struct {
    uint64_t    base;
    uint64_t    size;
    uint32_t    type;       /* 0=usable, 1=reserved, 2=ACPI, 3=NVS */
} mem_range_t;

uint32_t host_get_mem_map(mem_range_t *ranges, uint32_t max_ranges);

/* Get total physical memory */
uint64_t host_get_phys_memory(void);

/* ============================================================================
 * CPU Features
 * ============================================================================ */

/* Check if CPU supports a feature */
bool host_cpu_has_feature(const char *feature);

/* Get CPU feature string */
void host_get_cpu_features(char *buffer, uint32_t buffer_size);

/* ============================================================================
 * Debug / Diagnostics
 * ============================================================================ */

/* Print system status */
void host_print_status(void);

/* Dump memory map */
void host_dump_mem_map(void);

/* Dump processor info */
void host_dump_processor_info(void);

/* Kernel log */
#define KLOG_LEVEL_DEBUG    0
#define KLOG_LEVEL_INFO     1
#define KLOG_LEVEL_WARN     2
#define KLOG_LEVEL_ERROR    3
#define KLOG_LEVEL_PANIC    4

void klog(int level, const char *fmt, ...);

/* Set log level */
void klog_set_level(int level);

/* Get log level */
int klog_get_level(void);

#endif /* HOST_LOCAL_H */