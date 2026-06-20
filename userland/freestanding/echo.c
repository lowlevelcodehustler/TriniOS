// TriniOS - echo (display text)
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
    for (int i = 0; i < argc; i++) {
        if (i > 0) write(STDOUT_FILENO, " ", 1);
        write(STDOUT_FILENO, argv[i], str_len(argv[i]));
    }
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}