// TriniOS - Minimal freestanding stdio replacements (x86_64)
// Provides write(), read(), puts(), printf()-like output without libc

#ifndef FREESTANDING_STDIO_H
#define FREESTANDING_STDIO_H

#include <stdint.h>
#include "syscall.h"

/* File descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Write exactly n bytes to fd. Returns 0 on success, -errno on error. */
static inline int write_exact(int fd, const void *buf, size_t count)
{
    size_t total = 0;
    while (total < count) {
        syscall_ret_t r = syscall3(__NR_write, fd, (uintptr_t)(buf + total), count - total);
        if (r < 0) return (int)r;
        total += (size_t)r;
    }
    return 0;
}

/* Write up to n bytes to fd. Returns bytes written or -errno. */
static inline int write(int fd, const void *buf, size_t count)
{
    return (int)syscall3(__NR_write, fd, (uintptr_t)buf, count);
}

/* Read from fd into buf. Returns bytes read or -errno. */
static inline int read(int fd, void *buf, size_t count)
{
    return (int)syscall3(__NR_read, fd, (uintptr_t)buf, count);
}

/* Write a string to stdout (with newline). */
static void puts(const char *s)
{
    if (!s) s = "(null)";
    write(STDOUT_FILENO, s, __builtin_strlen(s));
    write(STDOUT_FILENO, "\n", 1);
}

/* Write a string to stderr. */
static void eputs(const char *s)
{
    if (!s) s = "(null)";
    write(STDERR_FILENO, s, __builtin_strlen(s));
    write(STDERR_FILENO, "\n", 1);
}

/* Minimal printf-like for integers and strings to stdout.
 * Supports: %s %d %u %x %p %% */
void printf(const char *fmt, ...);

/* Format a string into a buffer (returns bytes written, excluding NUL). */
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif /* FREESTANDING_STDIO_H */