/*
 * kernel_selftest.h
 * TriniOS — Kernel-Space Self-Test Framework
 *
 * Runs inside the kernel immediately after bootstrap.
 * Tests each subsystem initialization using VGA console output.
 * No stdlib dependency — all output goes through VGA text mode.
 *
 * Output format:
 *   SELFTEST: <subsystem>: <PASS|FAIL> <detail>
 *
 * Final summary:
 *   SELFTEST: SUMMARY: <passed>/<total> tests passed
 */

#ifndef KERNEL_SELFTEST_H
#define KERNEL_SELFTEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Freestanding memcpy replacement — avoid system string.h with -nostdlib */
#define TRINI_MEMCPY(dst, src, n) do {                              \
    uint8_t *_dst = (uint8_t *)(dst);                               \
    const uint8_t *_src = (const uint8_t *)(src);                   \
    size_t _n = (n);                                                \
    for (size_t _i = 0; _i < _n; _i++) {                            \
        _dst[_i] = _src[_i];                                        \
    }                                                               \
} while (0)

/* Use TRINI_MEMCPY directly in macros */

/* ---------- test result tracking ---------- */

#define MAX_SELFTESTS 64

typedef struct {
    char         subsystem[32];
    char         name[64];
    bool         passed;
} selftest_result_t;

extern selftest_result_t g_selftest_results[MAX_SELFTESTS];
extern int               g_selftest_count;
extern int               g_selftest_passed;
extern int               g_selftest_failed;

/* ---------- macros for test registration ---------- */

/* Record a passing test */
#define SELFTEST_PASS(subsystem, name) do {        \
    if (g_selftest_count < MAX_SELFTESTS) {         \
        selftest_result_t *r = &g_selftest_results[g_selftest_count]; \
        TRINI_MEMCPY(r->subsystem, subsystem, 31);  \
        r->subsystem[31] = '\0';                    \
        TRINI_MEMCPY(r->name, name, 63);            \
        r->name[63] = '\0';                         \
        r->passed = true;                           \
        g_selftest_count++;                         \
        g_selftest_passed++;                        \
    }                                              \
    selftest_print_status(true, subsystem, name);   \
} while (0)

/* Record a failing test */
#define SELFTEST_FAIL(subsystem, name, detail) do { \
    if (g_selftest_count < MAX_SELFTESTS) {         \
        selftest_result_t *r = &g_selftest_results[g_selftest_count]; \
        TRINI_MEMCPY(r->subsystem, subsystem, 31);  \
        r->subsystem[31] = '\0';                    \
        TRINI_MEMCPY(r->name, name, 63);            \
        r->name[63] = '\0';                         \
        r->passed = false;                          \
        g_selftest_count++;                         \
        g_selftest_failed++;                        \
    }                                              \
    selftest_print_status(false, subsystem, name);  \
    if (detail)                                    \
        selftest_print_detail(detail);              \
} while (0)

/* ---------- assertion macros ---------- */

#define SELFTEST_ASSERT(subsystem, name, expr, detail) do { \
    if (!(expr))                                           \
        SELFTEST_FAIL(subsystem, name, detail);            \
    else                                                   \
        SELFTEST_PASS(subsystem, name);                    \
} while (0)

#define SELFTEST_ASSERT_EQ(subsystem, name, a, b, detail) do { \
    if ((a) != (b))                                          \
        SELFTEST_FAIL(subsystem, name, detail);              \
    else                                                     \
        SELFTEST_PASS(subsystem, name);                      \
} while (0)

#define SELFTEST_ASSERT_NULL(subsystem, name, ptr, detail) do { \
    if ((ptr) != NULL)                                        \
        SELFTEST_FAIL(subsystem, name, detail);               \
    else                                                      \
        SELFTEST_PASS(subsystem, name);                       \
} while (0)

#define SELFTEST_ASSERT_NOT_NULL(subsystem, name, ptr, detail) do { \
    if ((ptr) == NULL)                                          \
        SELFTEST_FAIL(subsystem, name, detail);                 \
    else                                                        \
        SELFTEST_PASS(subsystem, name);                         \
} while (0)

/* ---------- public API ---------- */

/* Print a PASS/FAIL line to VGA */
void selftest_print_status(bool passed, const char *subsystem, const char *name);

/* Print a detail/failure reason to VGA */
void selftest_print_detail(const char *detail);

/* Print a section header to VGA */
void selftest_print_header(const char *subsystem);

/* Print the final summary to VGA */
void selftest_print_summary(void);

/* Run all subsystem self-tests */
void selftest_run_all(void);

/* Individual subsystem test suites */
void selftest_vm_manager(void);
void selftest_ipc_subsystem(void);
void selftest_task_manager(void);
void selftest_host_local(void);
void selftest_gpu_memory(void);

#endif /* KERNEL_SELFTEST_H */