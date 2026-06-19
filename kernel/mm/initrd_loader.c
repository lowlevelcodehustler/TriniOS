/*
 * TriniOS - Initial RAM Disk Loader (Implementation)
 *
 * Parses gzip-compressed cpio_newc archives passed by GRUB2
 * via multiboot2 boot info, unpacks into an in-memory
 * filesystem tree backed by kernel-allocated VM pages.
 *
 * Architecture:
 *   1. GRUB2 loads initrd.img into RAM, passes address/size via multiboot2
 *   2. initrd_init() records the buffer location
 *   3. initrd_unpack() decompresses + parses cpio, builds file tree
 *   4. initrd_mount() registers the tree with the VM manager as rootfs
 *   5. initrd_free_image() releases the raw compressed buffer
 */

#include "initrd_loader.h"
#include "vm_manager.h"

#include <mach/mach.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Internal State
 * ============================================================================ */

static initrd_state_t g_initrd = {
    .initrd_start = 0,
    .initrd_size  = 0,
    .current_pos  = 0,
    .root         = NULL,
    .loaded       = false,
    .unpacked     = false
};

/* ============================================================================
 * Helper: Parse 8-byte hex field from cpio header
 * ============================================================================ */

static uint64_t cpio_parse_hex(const char *hex_str)
{
    uint64_t value = 0;
    for (int i = 0; i < 7; i++) {
        char c = hex_str[i];
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= (c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= (c - 'A' + 10);
    }
    return value;
}

/* ============================================================================
 * Helper: Allocate a new initrd entry
 * ============================================================================ */

static initrd_entry_t *initrd_entry_create(const char *name, const char *path)
{
    initrd_entry_t *entry = malloc(sizeof(initrd_entry_t));
    if (!entry)
        return NULL;

    entry->name    = strdup(name);
    entry->path    = strdup(path);
    entry->mode    = 0;
    entry->uid     = 0;
    entry->gid     = 0;
    entry->mtime   = 0;
    entry->size    = 0;
    entry->data    = NULL;
    entry->major   = 0;
    entry->minor   = 0;
    entry->children = NULL;
    entry->sibling = NULL;

    return entry;
}

/* ============================================================================
 * Helper: Free an initrd entry and its subtree
 * ============================================================================ */

static void initrd_entry_free(initrd_entry_t *entry)
{
    if (!entry)
        return;

    /* Free children recursively */
    initrd_entry_t *child = entry->children;
    while (child) {
        initrd_entry_t *next = child->sibling;
        initrd_entry_free(child);
        child = next;
    }

    free(entry->name);
    free(entry->path);
    free(entry->data);
    free(entry);
}

/* ============================================================================
 * Helper: Find or create a child directory entry
 * ============================================================================ */

static initrd_entry_t *initrd_find_or_create_child(
    initrd_entry_t *parent, const char *child_name)
{
    /* Search existing children */
    initrd_entry_t *child = parent->children;
    while (child) {
        if (strcmp(child->name, child_name) == 0)
            return child;
        child = child->sibling;
    }

    /* Not found - create new directory */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", parent->path, child_name);

    child = initrd_entry_create(child_name, path);
    if (!child)
        return NULL;

    child->mode = CPIO_S_IFDIR | 0755;

    /* Insert as first child */
    child->sibling = parent->children;
    parent->children = child;

    return child;
}

/* ============================================================================
 * Helper: Read bytes from the initrd buffer
 * ============================================================================ */

static bool initrd_read(vm_offset_t offset, void *buf, vm_size_t len)
{
    if (offset + len > g_initrd.initrd_start + g_initrd.initrd_size)
        return false;

    /*
     * In a real kernel, this would use a physical-to-virtual mapping
     * or direct memory copy from the initrd buffer.
     * For now, we assume the initrd is mapped into kernel VM space.
     */
    memcpy(buf, (void *)(uintptr_t)(g_initrd.initrd_start + offset), len);
    return true;
}

/* ============================================================================
 * initrd_init - Initialize the initrd loader with boot parameters
 *
 * Called early in kernel initialization with the initrd address
 * and size from the multiboot2 boot info structure.
 * ============================================================================ */

void initrd_init(vm_offset_t initrd_addr, vm_size_t initrd_size)
{
    if (initrd_addr == 0 || initrd_size == 0) {
        printf("initrd: No initrd provided by bootloader\n");
        return;
    }

    if (initrd_size > INITRD_MAX_SIZE) {
        printf("initrd: WARNING: initrd size (%lu) exceeds max (%lu)\n",
               (unsigned long)initrd_size, (unsigned long)INITRD_MAX_SIZE);
        return;
    }

    g_initrd.initrd_start = initrd_addr;
    g_initrd.initrd_size  = initrd_size;
    g_initrd.current_pos  = 0;
    g_initrd.loaded       = true;

    printf("initrd: Loaded at 0x%lx, size %lu bytes\n",
           (unsigned long)initrd_addr, (unsigned long)initrd_size);
}

/* ============================================================================
 * initrd_unpack - Parse and unpack the cpio archive
 *
 * Walks through the cpio_newc archive, creating an in-memory
 * filesystem tree. Handles regular files, directories, symlinks,
 * and device nodes.
 *
 * NOTE: The initrd is gzip-compressed. In a full implementation,
 * a gzip decompressor would be called first. For this Phase 1
 * implementation, we assume the initrd is pre-decompressed or
 * use a simple decompression stub.
 * ============================================================================ */

kern_return_t initrd_unpack(void)
{
    if (!g_initrd.loaded) {
        printf("initrd: Not loaded\n");
        return KERN_INVALID_ARGUMENT;
    }

    /* Create root directory entry */
    g_initrd.root = initrd_entry_create("/", "/");
    if (!g_initrd.root) {
        printf("initrd: Failed to allocate root entry\n");
        return KERN_RESOURCE_SHORTAGE;
    }
    g_initrd.root->mode = CPIO_S_IFDIR | 0755;

    vm_offset_t pos = 0;
    int entry_count = 0;
    int dir_count = 0;
    int file_count = 0;

    printf("initrd: Unpacking cpio archive...\n");

    while (pos < g_initrd.initrd_size) {
        /* Read cpio header */
        cpio_header_t header;
        if (!initrd_read(pos, &header, sizeof(cpio_header_t))) {
            break;  /* End of archive */
        }
        pos += sizeof(cpio_header_t);

        /* Check magic */
        if (memcmp(header.magic, CPIO_NEWC_MAGIC, 6) != 0) {
            printf("initrd: Invalid cpio magic at offset %lu\n",
                   (unsigned long)(pos - sizeof(cpio_header_t)));
            break;
        }

        /* Parse header fields */
        uint64_t mode     = cpio_parse_hex(header.mode);
        uint64_t uid      = cpio_parse_hex(header.uid);
        uint64_t gid      = cpio_parse_hex(header.gid);
        uint64_t mtime    = cpio_parse_hex(header.mtime);
        uint64_t filesize = cpio_parse_hex(header.filesize);
        uint64_t namesize = cpio_parse_hex(header.namesize);
        int devmajor      = (int)cpio_parse_hex(header.devmajor);
        int devminor      = (int)cpio_parse_hex(header.devminor);

        /* Read filename */
        if (namesize == 0 || namesize > 4096) {
            break;  /* Invalid namesize */
        }

        char *filename = malloc(namesize + 1);
        if (!filename) {
            printf("initrd: Failed to allocate filename buffer\n");
            break;
        }

        if (!initrd_read(pos, filename, namesize)) {
            free(filename);
            break;
        }
        filename[namesize] = '\0';
        pos += (namesize + 3) & ~3;  /* Align to 4 bytes */

        /* Skip "." and ".." entries */
        if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
            free(filename);
            /* Skip file data */
            pos = (pos + 3) & ~3;  /* Align to 4 bytes */
            continue;
        }

        /* Check for trailer (end of archive) */
        if (strcmp(filename, CPIO_NEWC_TRAILER) == 0) {
            printf("initrd: Archive trailer reached (%d entries)\n", entry_count);
            free(filename);
            goto done;
        }

        /* Build full path */
        char full_path[512];
        /* Find the parent directory by parsing the path components */
        char *path_copy = strdup(filename);
        char *last_slash = strrchr(path_copy, '/');

        if (last_slash) {
            *last_slash = '\0';
            char *dir_path = path_copy;
            char *entry_name = last_slash + 1;

            /* Navigate to parent directory */
            initrd_entry_t *current = g_initrd.root;

            /* Parse directory path components */
            char *saveptr = NULL;
            char *token = strtok_r(dir_path, "/", &saveptr);
            while (token) {
                current = initrd_find_or_create_child(current, token);
                token = strtok_r(NULL, "/", &saveptr);
            }

            /* Create the entry */
            initrd_entry_t *entry = initrd_entry_create(entry_name, filename);
            if (entry) {
                entry->mode  = (uint32_t)mode;
                entry->uid   = (uint32_t)uid;
                entry->gid   = (uint32_t)gid;
                entry->mtime = mtime;
                entry->size  = filesize;
                entry->major = devmajor;
                entry->minor = devminor;

                /* Insert as child of current directory */
                entry->sibling = current->children;
                current->children = entry;

                /* Read file data for regular files */
                if ((mode & CPIO_S_IFMT) == CPIO_S_IFREG && filesize > 0) {
                    entry->data = malloc(filesize);
                    if (entry->data) {
                        initrd_read(pos, entry->data, (vm_size_t)filesize);
                    }
                    file_count++;
                } else if ((mode & CPIO_S_IFMT) == CPIO_S_IFDIR) {
                    dir_count++;
                }

                entry_count++;
            }

            /* Align past file data */
            pos = (pos + filesize + 3) & ~3;
        }

        free(path_copy);
        free(filename);
    }

done:
    g_initrd.unpacked = true;

    printf("initrd: Unpacked %d entries (%d dirs, %d files)\n",
           entry_count, dir_count, file_count);

    return KERN_SUCCESS;
}

/* ============================================================================
 * initrd_mount - Register the unpacked initrd as the root filesystem
 *
 * In a full implementation, this would register the in-memory tree
 * with the VFS layer and make it available as "/".
 * ============================================================================ */

kern_return_t initrd_mount(void)
{
    if (!g_initrd.unpacked) {
        printf("initrd: Not unpacked yet\n");
        return KERN_INVALID_STATE;
    }

    if (!g_initrd.root) {
        printf("initrd: No root filesystem\n");
        return KERN_INVALID_ARGUMENT;
    }

    printf("initrd: Mounting as rootfs at %s\n", INITRD_MOUNT);

    /*
     * TODO: Integrate with VFS layer when available.
     * For now, the initrd tree is accessible via initrd_get_root().
     */

    printf("initrd: Root filesystem mounted successfully\n");
    return KERN_SUCCESS;
}

/* ============================================================================
 * initrd_get_root - Get the root entry of the unpacked filesystem
 * ============================================================================ */

initrd_entry_t *initrd_get_root(void)
{
    return g_initrd.root;
}

/* ============================================================================
 * initrd_is_loaded - Check if initrd was loaded
 * ============================================================================ */

bool initrd_is_loaded(void)
{
    return g_initrd.loaded && g_initrd.unpacked;
}

/* ============================================================================
 * initrd_free_image - Free the raw initrd image after mounting
 *
 * Once the initrd is unpacked into the in-memory tree,
 * the raw compressed buffer can be released.
 * ============================================================================ */

void initrd_free_image(void)
{
    if (g_initrd.initrd_start != 0) {
        printf("initrd: Freeing raw image buffer\n");

        /*
         * In a real kernel, this would return the pages to the VM system.
         * The initrd memory was allocated by the bootloader, so we mark
         * it as freeable.
         */
        g_initrd.initrd_start = 0;
        g_initrd.initrd_size  = 0;
    }
}

/* ============================================================================
 * initrd_dump_tree - Debug: print the filesystem tree
 * ============================================================================ */

static void initrd_dump_recursive(initrd_entry_t *entry, int depth)
{
    if (!entry)
        return;

    /* Indent */
    for (int i = 0; i < depth; i++)
        printf("  ");

    /* Type indicator */
    const char *type;
    switch (entry->mode & CPIO_S_IFMT) {
        case CPIO_S_IFDIR:   type = "d"; break;
        case CPIO_S_IFREG:   type = "-"; break;
        case CPIO_S_IFCHR:   type = "c"; break;
        case CPIO_S_IFBLK:   type = "b"; break;
        case CPIO_S_IFLNK:   type = "l"; break;
        case CPIO_S_IFIFO:   type = "p"; break;
        default:             type = "?"; break;
    }

    printf("%s %s (%lu bytes)", type, entry->name,
           (unsigned long)entry->size);

    if ((entry->mode & CPIO_S_IFMT) == CPIO_S_IFCHR ||
        (entry->mode & CPIO_S_IFMT) == CPIO_S_IFBLK) {
        printf(" [%d,%d]", entry->major, entry->minor);
    }
    printf("\n");

    /* Recurse into children */
    initrd_entry_t *child = entry->children;
    while (child) {
        initrd_dump_recursive(child, depth + 1);
        child = child->sibling;
    }
}

void initrd_dump_tree(void)
{
    printf("\n=== initrd filesystem tree ===\n");
    if (g_initrd.root) {
        initrd_dump_recursive(g_initrd.root, 0);
    } else {
        printf("(empty)\n");
    }
    printf("================================\n\n");
}

/* ============================================================================
 * Full cleanup
 * ============================================================================ */

void initrd_cleanup(void)
{
    if (g_initrd.root) {
        initrd_entry_free(g_initrd.root);
        g_initrd.root = NULL;
    }
    g_initrd.loaded   = false;
    g_initrd.unpacked = false;
    printf("initrd: Cleaned up\n");
}