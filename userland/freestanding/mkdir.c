// TriniOS - mkdir (create directory)
// Freestanding: uses raw syscalls only

#include "syscall.h"
#include <stdint.h>
#include <stddef.h>

static inline int str_len(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (int)(p - s);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        write(STDERR_FILENO, "Usage: mkdir dir\n", 17);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int ret = syscall2(__NR_mkdir, (uintptr_t)argv[i], 0755);
        if (ret < 0) {
            write(STDERR_FILENO, "mkdir: ", 7);
            write(STDERR_FILENO, argv[i], str_len(argv[i]));
            write(STDERR_FILENO, ": cannot create\n", 15);
        }
    }

    return 0;
}