// TriniOS - cat (display file contents)
// Freestanding: uses raw syscalls only

#include "syscall.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>

#define BUF_SIZE 4096

static inline int str_len(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (int)(p - s);
}

static void usage(void)
{
    write(STDERR_FILENO, "Usage: cat [file...]\n", 20);
}

int main(int argc, char *argv[])
{
    (void)argc;

    if (argc < 2) {
        /* No args: read from stdin */
        char buf[BUF_SIZE];
        while (1) {
            int n = read(STDIN_FILENO, buf, BUF_SIZE);
            if (n <= 0) break;
            write(STDOUT_FILENO, buf, (size_t)n);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], 0); /* O_RDONLY = 0 */
        if (fd < 0) {
            write(STDERR_FILENO, "cat: ", 5);
            write(STDERR_FILENO, argv[i], str_len(argv[i]));
            write(STDERR_FILENO, ": cannot open\n", 13);
            continue;
        }

        char buf[BUF_SIZE];
        int n;
        while ((n = read(fd, buf, BUF_SIZE)) > 0) {
            write(STDOUT_FILENO, buf, (size_t)n);
        }
        close(fd);
    }

    return 0;
}