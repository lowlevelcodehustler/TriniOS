/*
 * TriniOS - Initial RAM Disk Loader
 *
 * Kernel-side component for loading and unpacking the initrd
 * (initial RAM disk) image passed by the bootloader (GRUB2).
 *
 * The initrd is received as a memory buffer via multiboot2
 * boot info headers, unpacked into a RAM-backed tmpfs,
 * and used as the initial root filesystem.
 */

#ifndef INITRD_LOADER_H
#define INITRD_LOADER_H

#include <mach/types.h>
#include <mach/vm.h>
#include <stdbool.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

/* Maximum initrd size (128 MB) */
#define INITRD_MAX_SIZE       (128 * 1024 * 1024)

/* tmpfs mount point for initrd */
#define INITRD_MOUNT          "/"

/* tmpfs backing memory allocation granularity (pages) */
#define INITRD_PAGE_SIZE      4096

/* ============================================================================
 * CPIO Archive Format (newc / CRC format)
 * ============================================================================
 *
 * Each entry in a cpio_newc archive has a 110-byte header:
 * - 6-byte magic: "070701"
 * - 30 x 6-byte hex fields (inode, mode, uid, gid, nlink, mtime,
 *   filesize, devmajor, devminor, rdevmajor, rdevminor, namesize,
 *   check (unused), mode (repeated), major, minor)
 * - 2 x 6-byte hex: inode (repeated), namesize (repeated)
 * - null-padded filename
 *
 * Total header: 0x6C = 108 bytes for data fields + variable filename
 * Standard total: 110 bytes (0x6E)
 */

#define CPIO_NEWC_MAGIC       "070701"
#define CPIO_NEWC_TRAILER     "TRAILER!!!"
#define CPIO_NEWC_HEADER_SIZE 110

/* CPIO mode flags */
#define CPIO_S_IFMT           0170000   /* type mask */
#define CPIO_S_IFNR           0160000   /* network */
#define CPIO_S_IFLNK          0120000   /* symbolic link */
#define CPIO_S_IFREG          0100000   /* regular file */
#define CPIO_S_IFBLK          0060000   /* block device */
#define CPIO_S_IFDIR          0040000   /* directory */
#define CPIO_S_IFCHR          0020000   /* character device */
#define CPIO_S_IFIFO          0010000   /* FIFO */

/* ============================================================================
 * CPIO Entry Header (parsed structure)
 * ============================================================================ */

typedef struct {
    char    magic[6];           /* "070701" */
    char    inode[8];           /* inode number (hex) */
    char    mode[8];            /* protection mode (hex) */
    char    uid[8];             /* owner uid (hex) */
    char    gid[8];             /* owner gid (hex) */
    char    nlink[8];           /* number of links (hex) */
    char    mtime[8];           /* modification time (hex) */
    char    filesize[8];        /* file size in bytes (hex) */
    char    devmajor[8];        /* major device number (hex) */
    char    devminor[8];        /* minor device number (hex) */
    char    rdevmajor[8];       /* remote major device number (hex) */
    char    rdevminor[8];       /* remote minor device number (hex) */
    char    namesize[8];        /* name size without null (hex) */
    char    check[8];           /* check sum or zero (hex) */
    char    major[8];           /* major number for owner */
    char    minor[8];           /* minor number for owner */
} __attribute__((packed)) cpio_header_t;

/* ============================================================================
 * In-Memory File Entry (for tmpfs)
 * ============================================================================ */

typedef struct initrd_entry {
    char                *name;         /* filename */
    char                *path;         /* full path */
    uint32_t            mode;          /* file mode (type + permissions) */
    uint32_t            uid;           /* owner uid */
    uint32_t            gid;           /* owner gid */
    uint64_t            mtime;         /* modification time */
    uint64_t            size;          /* file size */
    uint8_t             *data;         /* file data (NULL for dirs) */
    int                 major;         /* device major (for char/block devs) */
    int                 minor;         /* device minor */
    struct initrd_entry *children;     /* linked list of children */
    struct initrd_entry *sibling;      /* next sibling */
} initrd_entry_t;

/* ============================================================================
 * Initrd State
 * ============================================================================ */

typedef struct {
    vm_offset_t         initrd_start;  /* Physical address of initrd */
    vm_size_t           initrd_size;   /* Size of initrd in bytes */
    vm_offset_t         current_pos;   /* Current parse position */
    initrd_entry_t     *root;         /* Root of unpacked filesystem tree */
    bool                loaded;        /* Whether initrd was successfully loaded */
    bool                unpacked;      /* Whether initrd was successfully unpacked */
} initrd_state_t;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/* Initialize the initrd loader with boot params */
void initrd_init(vm_offset_t initrd_addr, vm_size_t initrd_size);

/* Parse and unpack the cpio archive into an in-memory tree */
kern_return_t initrd_unpack(void);

/* Mount the unpacked initrd as the root filesystem */
kern_return_t initrd_mount(void);

/* Get the root entry of the unpacked filesystem */
initrd_entry_t *initrd_get_root(void);

/* Check if initrd was loaded */
bool initrd_is_loaded(void);

/* Free initrd resources (after mount, the raw image can be freed) */
void initrd_free_image(void);

/* Dump the filesystem tree (for debugging) */
void initrd_dump_tree(void);

#endif /* INITRD_LOADER_H */