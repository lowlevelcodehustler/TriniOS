// TriniOS - Freestanding syscall interface (x86_64)
// Raw syscalls for user-space programs running without libc

#ifndef FREESTANDING_SYSCALL_H
#define FREESTANDING_SYSCALL_H

#include <stdint.h>

/* Freestanding type definitions (no libc headers) */
typedef long ssize_t;
typedef unsigned long size_t;

/* x86_64 Linux syscall numbers */
#define __NR_read             0
#define __NR_write            1
#define __NR_open             2
#define __NR_close            3
#define __NR_stat             4
#define __NR_fstat            5
#define __NR_lseek            8
#define __NR_sigaction       13
#define __NR_rt_sigprocmask  14
#define __NR_rt_sigreturn    15
#define __NR_access        21
#define __NR_getpid         39
#define __NR_dup            32
#define __NR_dup2           33
#define __NR_lseek          8
#define __NR_unlink        87
#define __NR_mkdir         83
#define __NR_rename        82
#define __NR_readlink      58
#define __NR_getcwd        79
#define __NR_link          86
#define __NR_fork          57
#define __NR_execve        59
#define __NR_wait4         61
#define __NR_exit          60
#define __NR_pipe          59
#define __NR_getdents64   217
#define __NR_pipe2         293

/* File descriptors */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* Open flags */
#define O_RDONLY        00

/* Syscall return: -errno on error */
typedef long syscall_ret_t;

static inline syscall_ret_t syscall0(long n)
{
    syscall_ret_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static inline syscall_ret_t syscall1(long n, long a0)
{
    syscall_ret_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a0) : "rcx", "r11", "memory");
    return ret;
}

static inline syscall_ret_t syscall2(long n, long a0, long a1)
{
    syscall_ret_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline syscall_ret_t syscall3(long n, long a0, long a1, long a2)
{
    syscall_ret_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1), "d"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline syscall_ret_t syscall4(long n, long a0, long a1, long a2, long a3)
{
    syscall_ret_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1), "d"(a2), "r"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline syscall_ret_t syscall5(long n, long a0, long a1, long a2, long a3, long a4)
{
    syscall_ret_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1), "d"(a2), "r"(a3), "c"(a4) : "rcx", "r11", "memory");
    return ret;
}

static inline syscall_ret_t syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5)
{
    syscall_ret_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1), "d"(a2), "r"(a3), "c"(a4), "b"(a5) : "rcx", "r11", "memory");
    return ret;
}

/* Convenience wrappers matching POSIX function signatures */
static inline ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)syscall3(__NR_read, fd, (long)buf, count);
}

static inline ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)syscall3(__NR_write, fd, (long)buf, count);
}

static inline int open(const char *pathname, int flags)
{
    return (int)syscall2(__NR_open, (long)pathname, flags);
}

static inline int close(int fd)
{
    return (int)syscall1(__NR_close, fd);
}

#endif /* FREESTANDING_SYSCALL_H */