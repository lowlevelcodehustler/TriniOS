// TriniOS - ls (list directory contents)
// Freestanding: uses raw syscalls only, including getdents64 for directory reading

#include "syscall.h"
#include <stdint.h>
#include <stddef.h>

#define BUF_SIZE 8192

/* d_type values */
#define DT_UNKNOWN  0
#define DT_REG      1
#define DT_DIR      2
#define DT_LNK      3

/* getdents64 dirent structure - must match kernel layout */
struct linux_dirent64 {
    unsigned long  d_ino;    /* Inode number */
    unsigned long  d_off;    /* Offset to next entry */
    unsigned short d_reclen; /* Length of this record */
    unsigned char  d_type;   /* File type */
    char           d_name[]; /* Filename (null-terminated) */
};

static inline int str_len(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (int)(p - s);
}

static void print_entry(const char *name, unsigned char type)
{
    /* Print file type indicator */
    switch (type) {
    case DT_DIR:
        write(STDOUT_FILENO, "/", 1);
        break;
    default:
        break;
    }

    /* Print filename */
    write(STDOUT_FILENO, name, str_len(name));
    write(STDOUT_FILENO, "\n", 1);
}

static void list_dir(const char *path)
{
    int fd = open(path, 0); /* O_RDONLY */
    if (fd < 0) {
        write(STDERR_FILENO, "ls: cannot access '", 18);
        write(STDERR_FILENO, path, str_len(path));
        write(STDERR_FILENO, "': no such file or directory\n", 28);
        return;
    }

    char buf[BUF_SIZE];
    int n;
    while ((n = syscall3(__NR_getdents64, fd, (uintptr_t)buf, BUF_SIZE)) > 0) {
        unsigned long offset = 0;
        while (offset < (unsigned long)n) {
            struct linux_dirent64 *dent = (struct linux_dirent64 *)(buf + offset);
            if (dent->d_name[0] != '\0') { /* Skip . and .. */
                print_entry(dent->d_name, dent->d_type);
            }
            offset += dent->d_reclen;
        }
    }

    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        /* List current directory */
        list_dir(".");
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        list_dir(argv[i]);
    }

    return 0;
}