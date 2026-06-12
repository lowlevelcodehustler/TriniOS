/*
 * TriniOS BSD Compatibility Server - File I/O
 *
 * Implements open(), close(), read(), write() on top of Mach IPC.
 * File operations are delegated to the filesystem server via Mach ports.
 */

#include "bsd_server.h"
#include "posix.h"
#include <string.h>

/* ============================================================================
 * File Server Port
 * ============================================================================ */

static mach_port_t fs_server_port = MACH_PORT_NULL;

/*
 * Initialize connection to the filesystem server.
 * Called during BSD server startup.
 */
void bsd_file_init(mach_port_t fs_port)
{
    fs_server_port = fs_port;
}

/* ============================================================================
 * open() Implementation
 *
 * Strategy:
 * 1. Send BSD_MSG_OPEN request to filesystem server
 * 2. Allocate a file descriptor in the task's fd table
 * 3. Return fd to caller
 * ============================================================================ */

kern_return_t bsd_handle_open(task_context_t *ctx, const char *path, int flags, int *fd_out)
{
    kern_return_t kr;
    int fd;
    mach_msg_type_number_t data_count;

    if (!ctx || !path) {
        return POSIX_EINVAL;
    }

    /* Check if path is empty */
    if (path[0] == '\0') {
        return POSIX_ENOENT;
    }

    /* Allocate a file descriptor */
    fd = bsd_alloc_fd(ctx);
    if (fd < 0) {
        return POSIX_EMFILE;
    }

    /*
     * Send open request to filesystem server.
     * The FS server returns a port representing the open file.
     */
    /* TODO: Implement actual IPC to filesystem server
     * For now, simulate with a local port
     */

    /* Initialize fd entry */
    ctx->fds[fd].fd = fd;
    ctx->fds[fd].flags = flags;
    ctx->fds[fd].offset = 0;
    ctx->fds[fd].is_pipe = false;

    /* For stdin/stdout/stderr, use the console port */
    if (fd <= 2) {
        /* TODO: connect to console/terminal server */
        ctx->fds[fd].port = MACH_PORT_NULL; /* Placeholder */
    }

    if (fd_out) {
        *fd_out = fd;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * close() Implementation
 *
 * Strategy:
 * 1. Look up fd in task's fd table
 * 2. Notify filesystem server (if applicable)
 * 3. Deallocate the port
 * 4. Clear the fd entry
 * ============================================================================ */

kern_return_t bsd_handle_close(task_context_t *ctx, int fd)
{
    if (!ctx) {
        return POSIX_EINVAL;
    }

    if (fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        return POSIX_EBADF;
    }

    if (ctx->fds[fd].port == MACH_PORT_NULL && ctx->fds[fd].fd == 0) {
        return POSIX_EBADF;
    }

    /* Notify filesystem server if this is a real file */
    if (!ctx->fds[fd].is_pipe && ctx->fds[fd].port != MACH_PORT_NULL) {
        /* TODO: send close message to FS server */
    }

    /* Free the fd */
    bsd_free_fd(ctx, fd);

    return KERN_SUCCESS;
}

/* ============================================================================
 * read() Implementation
 *
 * Strategy:
 * 1. Look up fd in task's fd table
 * 2. If pipe, read from pipe buffer
 * 3. If file, send read request to filesystem server
 * 4. Copy data into caller's buffer
 * ============================================================================ */

kern_return_t bsd_handle_read(task_context_t *ctx, int fd, void *buf, mach_msg_size_t count, mach_msg_size_t *done)
{
    fd_entry_t *entry;
    pipe_buffer_t *pipe;

    if (!ctx) {
        return POSIX_EINVAL;
    }

    if (fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        return POSIX_EBADF;
    }

    entry = &ctx->fds[fd];
    if (entry->port == MACH_PORT_NULL && entry->fd == 0) {
        return POSIX_EBADF;
    }

    /* Check if reading from a pipe */
    if (entry->is_pipe) {
        /* Find the pipe buffer */
        pipe = NULL;
        for (int i = 0; i < BSD_MAX_PIPES; i++) {
            if (pipe_table[i].read_port == entry->port) {
                pipe = &pipe_table[i];
                break;
            }
        }

        if (!pipe) {
            return POSIX_EBADF;
        }

        /* Read from pipe buffer */
        mach_msg_size_t bytes_read = 0;
        while (bytes_read < count && pipe->head != pipe->tail) {
            ((char *)buf)[bytes_read] = pipe->buffer[pipe->tail];
            pipe->tail = (pipe->tail + 1) % BSD_PIPE_BUFFER_SIZE;
            bytes_read++;
        }

        if (done) {
            *done = bytes_read;
        }

        return (bytes_read == 0) ? KERN_SUCCESS : KERN_SUCCESS; /* 0 bytes = EOF or non-blocking */
    }

    /* For regular files, delegate to filesystem server */
    /* TODO: send read request to FS server via Mach IPC */

    /* Update file offset */
    if (done) {
        entry->offset += *done;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * write() Implementation
 *
 * Strategy:
 * 1. Look up fd in task's fd table
 * 2. If pipe, write to pipe buffer
 * 3. If file, send write request to filesystem server
 * ============================================================================ */

kern_return_t bsd_handle_write(task_context_t *ctx, int fd, const void *buf, mach_msg_size_t count, mach_msg_size_t *done)
{
    fd_entry_t *entry;
    pipe_buffer_t *pipe;

    if (!ctx) {
        return POSIX_EINVAL;
    }

    if (fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        return POSIX_EBADF;
    }

    entry = &ctx->fds[fd];
    if (entry->port == MACH_PORT_NULL && entry->fd == 0) {
        return POSIX_EBADF;
    }

    /* Check if writing to a pipe */
    if (entry->is_pipe) {
        /* Find the pipe buffer */
        pipe = NULL;
        for (int i = 0; i < BSD_MAX_PIPES; i++) {
            if (pipe_table[i].write_port == entry->port) {
                pipe = &pipe_table[i];
                break;
            }
        }

        if (!pipe) {
            return POSIX_EBADF;
        }

        /* Write to pipe buffer */
        mach_msg_size_t bytes_written = 0;
        while (bytes_written < count) {
            /* Check if pipe is full */
            if (((pipe->head + 1) % BSD_PIPE_BUFFER_SIZE) == pipe->tail) {
                break; /* Pipe full */
            }

            pipe->buffer[pipe->head] = ((const char *)buf)[bytes_written];
            pipe->head = (pipe->head + 1) % BSD_PIPE_BUFFER_SIZE;
            bytes_written++;
        }

        if (done) {
            *done = bytes_written;
        }

        /* Notify any waiting reader */
        /* TODO: send message to read_port */

        return KERN_SUCCESS;
    }

    /* For regular files, delegate to filesystem server */
    /* TODO: send write request to FS server via Mach IPC */

    /* For stdout/stderr, write to console */
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        /* TODO: send to console server */
        if (done) {
            *done = count; /* Assume all written */
        }
    }

    /* Update file offset */
    if (done) {
        entry->offset += *done;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * fcntl() Implementation (partial)
 * ============================================================================ */

kern_return_t bsd_handle_fcntl(task_context_t *ctx, int fd, int cmd, long arg, long *result)
{
    fd_entry_t *entry;

    if (!ctx || fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        return POSIX_EBADF;
    }

    entry = &ctx->fds[fd];
    if (entry->port == MACH_PORT_NULL && entry->fd == 0) {
        return POSIX_EBADF;
    }

    switch (cmd) {
        case F_DUPFD: {
            int new_fd = bsd_alloc_fd(ctx);
            if (new_fd < 0) {
                return POSIX_EMFILE;
            }
            memcpy(&ctx->fds[new_fd], entry, sizeof(fd_entry_t));
            ctx->fds[new_fd].fd = new_fd;
            if (result) {
                *result = new_fd;
            }
            return KERN_SUCCESS;
        }

        case F_GETFD:
            if (result) {
                *result = 0; /* No FD_CLOEXEC for now */
            }
            return KERN_SUCCESS;

        case F_SETFD:
            /* TODO: support FD_CLOEXEC */
            return KERN_SUCCESS;

        case F_GETFL:
            if (result) {
                *result = entry->flags;
            }
            return KERN_SUCCESS;

        case F_SETFL:
            entry->flags = (int)arg;
            return KERN_SUCCESS;

        default:
            return POSIX_EINVAL;
    }
}