/*
 * TriniOS ext4 Filesystem Server
 *
 * User-space Mach server that provides ext4 filesystem operations
 * to the BSD compatibility layer via IPC.
 *
 * This server handles mount requests and dispatches file/directory
 * operations from the VFS layer.
 */

#include "ext4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Message Types for VFS IPC
 * ============================================================================ */

typedef enum {
    FS_MSG_MOUNT      = 0x1001,
    FS_MSG_UNMOUNT    = 0x1002,
    FS_MSG_OPEN       = 0x1010,
    FS_MSG_CLOSE      = 0x1011,
    FS_MSG_READ       = 0x1012,
    FS_MSG_WRITE      = 0x1013,
    FS_MSG_LSEEK      = 0x1014,
    FS_MSG_STAT       = 0x1015,
    FS_MSG_UNLINK     = 0x1016,
    FS_MSG_RENAME     = 0x1017,
    FS_MSG_OPENDIR    = 0x1020,
    FS_MSG_READDIR    = 0x1021,
    FS_MSG_CLOSEDIR   = 0x1022,
    FS_MSG_MKDIR      = 0x1023,
    FS_MSG_RMDIR      = 0x1024,
    FS_MSG_FSCK       = 0x1030,
    FS_MSG_DUMP_SB    = 0x1031,
} fs_msg_type_t;

/* ============================================================================
 * Mount Context - tracks mounted filesystems
 * ============================================================================ */

#define MAX_MOUNTS 16

typedef struct {
    char            mount_point[256];
    char            device[256];
    ext4_fs_t       fs;
    bool            mounted;
} mount_entry_t;

static mount_entry_t mounts[MAX_MOUNTS];
static int mount_count = 0;

/* ============================================================================
 * File Handle Table
 * ============================================================================ */

#define MAX_FILES 256

typedef struct {
    uint32_t    handle;
    int         mount_idx;
    ext4_file_t file;
    bool        in_use;
} file_table_entry_t;

static file_table_entry_t file_table[MAX_FILES];

/* ============================================================================
 * Directory Handle Table
 * ============================================================================ */

#define MAX_DIRS 64

typedef struct {
    uint32_t        handle;
    int             mount_idx;
    ext4_dirhandle_t dir;
    bool            in_use;
} dir_table_entry_t;

static dir_table_entry_t dir_table[MAX_DIRS];

static uint32_t next_file_handle = 1;
static uint32_t next_dir_handle = 1;

/* ============================================================================
 * Helper: Find or allocate resources
 * ============================================================================ */

static int find_mount_by_path(const char *mount_point) {
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].mounted &&
            strcmp(mounts[i].mount_point, mount_point) == 0) {
            return i;
        }
    }
    return -1;
}

static int alloc_file_slot(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_table[i].in_use) {
            file_table[i].in_use = true;
            file_table[i].handle = next_file_handle++;
            return i;
        }
    }
    return -1;
}

static int alloc_dir_slot(void) {
    for (int i = 0; i < MAX_DIRS; i++) {
        if (!dir_table[i].in_use) {
            dir_table[i].in_use = true;
            dir_table[i].handle = next_dir_handle++;
            return i;
        }
    }
    return -1;
}

static void free_file_slot(uint32_t handle) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].in_use && file_table[i].handle == handle) {
            file_table[i].in_use = false;
            return;
        }
    }
}

static void free_dir_slot(uint32_t handle) {
    for (int i = 0; i < MAX_DIRS; i++) {
        if (dir_table[i].in_use && dir_table[i].handle == handle) {
            dir_table[i].in_use = false;
            ext4_closedir(&dir_table[i].dir);
            return;
        }
    }
}

/* ============================================================================
 * Command Handlers
 * ============================================================================ */

static int handle_mount(const char *device, const char *mount_point) {
    printf("[ext4_server] MOUNT %s -> %s\n", device, mount_point);

    /* Check if already mounted */
    if (find_mount_by_path(mount_point) >= 0) {
        printf("[ext4_server] Already mounted at %s\n", mount_point);
        return 0;
    }

    /* Find free mount slot */
    int slot = -1;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts[i].mounted) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        printf("[ext4_server] No free mount slots\n");
        return -1;
    }

    /* For now, we accept a "memory" device or a disk image path.
     * In Phase 2+, this will integrate with the block device driver. */
    if (strcmp(device, "memory") == 0 || device[0] == '/') {
        /* TODO: Open the block device and mount ext4 */
        /* For Phase 1, we just record the mount point */
        strncpy(mounts[slot].device, device, sizeof(mounts[slot].device) - 1);
        strncpy(mounts[slot].mount_point, mount_point,
                sizeof(mounts[slot].mount_point) - 1);
        mounts[slot].mounted = true;
        mount_count++;

        printf("[ext4_server] Mounted %s at %s\n", device, mount_point);
        return 0;
    }

    printf("[ext4_server] Unknown device type: %s\n", device);
    return -1;
}

static int handle_open(const char *mount_point, const char *path,
                       uint16_t flags, uint32_t *out_handle) {
    int midx = find_mount_by_path(mount_point);
    if (midx < 0) {
        printf("[ext4_server] Mount point %s not found\n", mount_point);
        return -1;
    }

    int fslot = alloc_file_slot();
    if (fslot < 0) {
        printf("[ext4_server] No free file slots\n");
        return -1;
    }

    int ret = ext4_open(&mounts[midx].fs, path, flags, &file_table[fslot].file);
    if (ret != EXT4_OK) {
        file_table[fslot].in_use = false;
        return ret;
    }

    file_table[fslot].mount_idx = midx;
    if (out_handle) *out_handle = file_table[fslot].handle;
    return 0;
}

static int handle_close(uint32_t handle) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].in_use && file_table[i].handle == handle) {
            ext4_close(&mounts[file_table[i].mount_idx].fs,
                       &file_table[i].file);
            file_table[i].in_use = false;
            return 0;
        }
    }
    return -1;
}

static int handle_read(uint32_t handle, void *buf, uint64_t count,
                       uint64_t *done) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].in_use && file_table[i].handle == handle) {
            int midx = file_table[i].mount_idx;
            return ext4_read(&mounts[midx].fs, &file_table[i].file,
                            buf, count, done);
        }
    }
    return -1;
}

static int handle_stat(const char *mount_point, const char *path,
                       ext4_stat_t *st) {
    int midx = find_mount_by_path(mount_point);
    if (midx < 0) return -1;
    return ext4_stat(&mounts[midx].fs, path, st);
}

static int handle_opendir(const char *mount_point, const char *path,
                          uint32_t *out_handle) {
    int midx = find_mount_by_path(mount_point);
    if (midx < 0) return -1;

    int dslot = alloc_dir_slot();
    if (dslot < 0) return -1;

    int ret = ext4_opendir(&mounts[midx].fs, path, &dir_table[dslot].dir);
    if (ret != EXT4_OK) {
        dir_table[dslot].in_use = false;
        return ret;
    }

    dir_table[dslot].mount_idx = midx;
    if (out_handle) *out_handle = dir_table[dslot].handle;
    return 0;
}

static int handle_readdir(uint32_t handle, ext4_dirent_t *entry) {
    for (int i = 0; i < MAX_DIRS; i++) {
        if (dir_table[i].in_use && dir_table[i].handle == handle) {
            int midx = dir_table[i].mount_idx;
            return ext4_readdir(&mounts[midx].fs, &dir_table[i].dir, entry);
        }
    }
    return -1;
}

static int handle_closedir(uint32_t handle) {
    free_dir_slot(handle);
    return 0;
}

/* ============================================================================
 * Main Server Loop
 *
 * In a full Mach implementation, this would use mach_msg() to receive
 * IPC messages. For Phase 1, we provide a command-line interface for
 * testing and a simple dispatch loop.
 * ============================================================================ */

static void print_usage(void) {
    printf("ext4_server commands:\n");
    print_usage();
    printf("  mount <device> <mount_point>    Mount an ext4 filesystem\n");
    printf("  open <mount> <path> [flags]      Open a file\n");
    printf("  read <handle> <bytes>            Read from a file\n");
    printf("  close <handle>                   Close a file\n");
    printf("  stat <mount> <path>              Stat a file\n");
    printf("  ls <mount> <path>                List a directory\n");
    printf("  fsck <mount>                     Run light filesystem check\n");
    printf("  dump_sb <mount>                  Dump superblock info\n");
    printf("  quit                             Exit\n");
}

static void cmd_ls(const char *mount_point, const char *path) {
    uint32_t dhandle;
    int ret = handle_opendir(mount_point, path, &dhandle);
    if (ret != 0) {
        printf("[ext4_server] opendir failed: %d\n", ret);
        return;
    }

    printf("\n");
    printf("  Inode    Type   Name\n");
    printf("  ------   ----   ----\n");

    ext4_dirent_t entry;
    while (handle_readdir(dhandle, &entry) == EXT4_OK) {
        const char *type_str = "???";
        switch (entry.file_type) {
        case EXT4_DE_REG_FILE: type_str = "file"; break;
        case EXT4_DE_DIR:      type_str = "dir";  break;
        case EXT4_DE_CHRDEV:   type_str = "chr";  break;
        case EXT4_DE_BLKDEV:   type_str = "blk";  break;
        case EXT4_DE_FIFO:     type_str = "fifo"; break;
        case EXT4_DE_SOCK:     type_str = "sock"; break;
        case EXT4_DE_LNK:      type_str = "lnk";  break;
        }
        printf("  %-8u %-7s %s\n", entry.ino, type_str, entry.name);
    }
    printf("\n");

    handle_closedir(dhandle);
}

/* ============================================================================
 * ext4_server_main - Entry point for the filesystem server
 * ============================================================================ */

void ext4_server_main(void) {
    printf("[ext4_server] TriniOS ext4 Filesystem Server\n");
    printf("[ext4_server] Phase 1 - Read-only with journal awareness\n\n");

    char line[512];

    while (fgets(line, sizeof(line), stdin)) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* Skip empty lines */
        if (line[0] == '\0') continue;

        if (strncmp(line, "mount ", 6) == 0) {
            char device[256], mpoint[256];
            if (sscanf(line + 6, "%255s %255s", device, mpoint) == 2) {
                handle_mount(device, mpoint);
            }
        } else if (strncmp(line, "ls ", 3) == 0) {
            char mpoint[256], fpath[256];
            if (sscanf(line + 3, "%255s %255s", mpoint, fpath) == 2) {
                cmd_ls(mpoint, fpath);
            }
        } else if (strncmp(line, "stat ", 5) == 0) {
            char mpoint[256], fpath[256];
            if (sscanf(line + 5, "%255s %255s", mpoint, fpath) == 2) {
                ext4_stat_t st;
                int midx = find_mount_by_path(mpoint);
                if (midx >= 0) {
                    int ret = ext4_stat(&mounts[midx].fs, fpath, &st);
                    if (ret == EXT4_OK) {
                        printf("  Inode:  %u\n", st.ino);
                        printf("  Mode:   0%07o\n", st.mode);
                        printf("  Size:   %llu\n", (unsigned long long)st.size);
                        printf("  Links:  %u\n", st.nlinks);
                        printf("  UID/GID: %u/%u\n", st.uid, st.gid);
                    } else {
                        printf("  stat failed: %d\n", ret);
                    }
                }
            }
        } else if (strncmp(line, "fsck ", 5) == 0) {
            char mpoint[256];
            if (sscanf(line + 5, "%255s", mpoint) == 1) {
                int midx = find_mount_by_path(mpoint);
                if (midx >= 0) {
                    ext4_fsck_light(&mounts[midx].fs);
                }
            }
        } else if (strncmp(line, "dump_sb ", 8) == 0) {
            char mpoint[256];
            if (sscanf(line + 8, "%255s", mpoint) == 1) {
                int midx = find_mount_by_path(mpoint);
                if (midx >= 0) {
                    ext4_dump_superblock(&mounts[midx].fs);
                }
            }
        } else if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            break;
        } else if (strcmp(line, "help") == 0) {
            print_usage();
        } else {
            printf("Unknown command: %s (type 'help' for usage)\n", line);
        }
    }

    /* Cleanup: unmount all filesystems */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts[i].mounted) {
            ext4_unmount(&mounts[i].fs);
            mounts[i].mounted = false;
        }
    }

    printf("[ext4_server] Shutting down\n");
}

/* ============================================================================
 * Standalone entry point
 * ============================================================================ */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    ext4_server_main();
    return 0;
}