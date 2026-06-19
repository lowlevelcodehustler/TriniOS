/*
 * freestanding_stubs.c - Minimal freestanding function implementations
 *
 * Provides memset, strlen, strncpy, snprintf, vsnprintf, calloc, strncmp,
 * strrchr, printf, and Mach IPC stubs that are normally part of libc but
 * unavailable with -ffreestanding -nostdlib.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

/* Mach types needed for freestanding kernel builds */
typedef uint32_t mach_port_name_t;
typedef uint32_t mach_msg_size_t;
typedef uint32_t mach_msg_timeout_t;
typedef int kern_return_t;
typedef mach_port_name_t task_t;
typedef mach_port_name_t thread_act_t;
typedef void *vm_offset_t;
typedef uint64_t vm_size_t;
typedef uint32_t vm_prot_t;
typedef bool boolean_t;

/* Mach message header (minimal definition) */
typedef struct {
    mach_port_name_t msgh_remote_port;
    mach_port_name_t msgh_local_port;
    mach_port_name_t msgh_voucher_port;
    mach_msg_size_t  msgh_size;
    uint32_t         msgh_id;
    uint32_t         msgh_bits;
} mach_msg_header_t;

/* Mach port type constants */
#define MACH_MSG_TYPE_MAKE_SEND 14
#define MACH_MSG_SUCCESS        0
#define MACH_RCV_MSG            0x00000001
#define MACH_RCV_LARGE          0x00000008
#define MACH_SEND_INVALID_DEST  1087
#define MACH_RCV_PORT_DIED      1101

/* Page size constant */
#ifndef vm_page_size
#define vm_page_size 4096
#endif

/* KERN_* constants */
#ifndef KERN_SUCCESS
#define KERN_SUCCESS          0
#endif
#ifndef KERN_INVALID_ADDRESS
#define KERN_INVALID_ADDRESS  1
#endif
#ifndef KERN_INVALID_TASK
#define KERN_INVALID_TASK     4
#endif
#ifndef KERN_INVALID_VALUE
#define KERN_INVALID_VALUE    5
#endif
#ifndef KERN_RESOURCE_SHORTAGE
#define KERN_RESOURCE_SHORTAGE 6
#endif
#ifndef KERN_INVALID_RIGHT
#define KERN_INVALID_RIGHT    8
#endif
#ifndef KERN_NOT_IMPLEMENTED
#define KERN_NOT_IMPLEMENTED  12
#endif

/* Mach VM protection types */
#define VM_PROT_READ   0x01
#define VM_PROT_WRITE  0x02
#define VM_PROT_EXECUTE 0x04

/* ── Memory operations ─────────────────────────────────────────────── */

void *memset(void *dst, int val, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)val;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return x[i] - y[i];
    }
    return 0;
}

/* ── String operations ─────────────────────────────────────────────── */

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    if (!n) return dst;
    size_t _i;
    for (_i = 0; _i < n && src[_i]; _i++) {
        dst[_i] = src[_i];
    }
    for (; _i < n; _i++) {
        dst[_i] = '\0';
    }
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    if (!n) return 0;
    while (*a && *b && n > 1 && *a == *b) { a++; b++; n--; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return (char *)last;
}

/* ── Memory allocation stubs ─────────────────────────────────────── */

void *calloc(size_t nmemb, size_t size)
{
    if (!nmemb || !size) return NULL;
    /* Simple static pool for calloc */
    #define CALLOcK_POOL_SIZE (64 * 1024)
    static uint8_t _pool[CALLOcK_POOL_SIZE];
    static size_t _offset = 0;

    size_t alloc_size = nmemb * size;
    /* Align to 16 bytes */
    alloc_size = (alloc_size + 15) & ~15;

    if (_offset + alloc_size > CALLOcK_POOL_SIZE) return NULL;
    void *ptr = &_pool[_offset];
    _offset += alloc_size;
    /* Zero-initialize as calloc requires */
    for (size_t i = 0; i < alloc_size; i++) ((uint8_t *)ptr)[i] = 0;
    return ptr;
}

/* ── Formatted output ──────────────────────────────────────────────── */

/* Forward declaration for vsnprintf (used by snprintf) */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int written = 0;

    if (!buf || !size) return -1;

    va_start(args, fmt);
    written = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return written;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    int written = 0;

    if (!buf || !size) return -1;

    while (*fmt && (written < (int)size - 1)) {
        if (fmt[0] == '%' && fmt[1] == 's') {
            const char *str = va_arg(args, const char *);
            while (*str && written < (int)size - 1) {
                buf[written++] = *str++;
            }
            fmt += 2;
        } else if (fmt[0] == '%' && fmt[1] == 'u') {
            unsigned long val = va_arg(args, unsigned long);
            char tmp[32];
            int idx = 0;
            if (val == 0) tmp[idx++] = '0';
            else {
                uint32_t tmp_val = (uint32_t)val;
                while (tmp_val > 0 && idx < 31) {
                    tmp[idx++] = '0' + (tmp_val % 10);
                    tmp_val /= 10;
                }
                for (int i = idx - 1; i >= 0; i--) buf[written++] = tmp[i];
            }
            fmt += 2;
        } else if (fmt[0] == '%' && fmt[1] == 'l' && fmt[2] == 'u') {
            unsigned long val = va_arg(args, unsigned long);
            char tmp[32];
            int idx = 0;
            if (val == 0) tmp[idx++] = '0';
            else {
                while (val > 0 && idx < 31) {
                    tmp[idx++] = '0' + (val % 10);
                    val /= 10;
                }
                for (int i = idx - 1; i >= 0; i--) buf[written++] = tmp[i];
            }
            fmt += 3;
        } else if (fmt[0] == '%' && fmt[1] == 'x') {
            unsigned long val = va_arg(args, unsigned long);
            char hex[] = "0123456789abcdef";
            buf[written++] = '0';
            buf[written++] = 'x';
            for (int i = 12; i >= 0; i -= 4) {
                if (written < (int)size - 1)
                    buf[written++] = hex[(val >> i) & 0xf];
            }
            fmt += 2;
        } else if (fmt[0] == '%' && fmt[1] == 'c') {
            char c = va_arg(args, int);
            buf[written++] = c;
            fmt += 2;
        } else if (fmt[0] == '%') {
            buf[written++] = '%';
            fmt++;
        } else {
            buf[written++] = *fmt++;
        }
    }

    if ((size_t)written >= size) written = (int)size - 1;
    buf[(size_t)written] = '\0';
    return written;
}

/* ── Printf stubs ─────────────────────────────────────────────────── */

int printf(const char *fmt, ...)
{
    va_list args;
    int written = 0;

    va_start(args, fmt);
    /* Write to VGA text mode buffer for freestanding builds */
    uint16_t *vga = (uint16_t *)0xB8000;
    static int row = 24;
    static int col = 0;

    char buf[512];
    written = vsnprintf(buf, sizeof(buf), fmt, args);

    for (int i = 0; buf[i] && row < 25; i++) {
        char ch = buf[i];
        if (ch == '\n') { row++; col = 0; }
        else if (ch == '\r') { col = 0; }
        else {
            vga[row * 80 + col] = (uint16_t)((0x07 << 8) | (uint8_t)ch);
            col++;
            if (col >= 80) { col = 0; row++; }
        }
    }
    if (row >= 25) row = 24;

    va_end(args);
    return written;
}

/* ── Mach IPC stubs (freestanding) ─────────────────────────────────── */
/*
 * NOTE: The real implementations of mach_msg, mach_port_*, vm_allocate, etc.
 * are now in kernel/ipc/ipc_port.c.  This file intentionally omits them
 * to avoid duplicate-symbol linker errors.
 *
 * We keep the old stub declarations here as __attribute__((weak)) so that
 * code compiled without ipc_port.o will still link (returning
 * KERN_NOT_IMPLEMENTED).
 */

__attribute__((weak))
task_t mach_task_self(void)
{
    return (task_t)(uintptr_t)0x1000;
}

__attribute__((weak))
kern_return_t mach_msg_send(mach_msg_header_t *msg)
{
    (void)msg;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t mach_msg_receive(mach_msg_header_t *msg)
{
    (void)msg;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t mach_port_allocate(task_t task, mach_port_name_t *name, int msgt_np)
{
    (void)task; (void)name; (void)msgt_np;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t mach_port_deallocate(task_t task, mach_port_name_t name)
{
    (void)task; (void)name;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t mach_port_construct(task_t task, mach_port_name_t *name, int polycount, int msgt_poly)
{
    (void)task; (void)name; (void)polycount; (void)msgt_poly;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t mach_port_destroy(task_t task, mach_port_name_t name)
{
    (void)task; (void)name;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t mach_port_insert_member(mach_port_name_t member, mach_port_name_t port_set)
{
    (void)member; (void)port_set;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t mach_port_extract_member(mach_port_name_t member, mach_port_name_t port_set)
{
    (void)member; (void)port_set;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t mach_msg(mach_msg_header_t *msg, int options, mach_msg_size_t send_size,
                       mach_msg_size_t recv_limit, mach_port_name_t rcv_name,
                       mach_msg_timeout_t timeout, mach_port_name_t notify)
{
    (void)msg; (void)options; (void)send_size; (void)recv_limit;
    (void)rcv_name; (void)timeout; (void)notify;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t vm_allocate(task_t task, vm_offset_t *address, vm_size_t size, int flags)
{
    (void)task; (void)address; (void)size; (void)flags;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t vm_deallocate(task_t task, vm_offset_t address, vm_size_t size)
{
    (void)task; (void)address; (void)size;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t vm_protect(task_t task, vm_offset_t address, vm_size_t size, boolean_t max_protection, vm_prot_t protection)
{
    (void)task; (void)address; (void)size; (void)max_protection; (void)protection;
    return KERN_NOT_IMPLEMENTED;
}

__attribute__((weak))
kern_return_t vm_memory_object_control(vm_offset_t object, vm_offset_t address, vm_size_t size, boolean_t control)
{
    (void)object; (void)address; (void)size; (void)control;
    return KERN_NOT_IMPLEMENTED;
}
