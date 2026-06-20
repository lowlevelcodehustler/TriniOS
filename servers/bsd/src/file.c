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
 * External: pipe_table is defined in main.c
 * ============================================================================ */

extern pipe_buffer_t pipe_table[BSD_MAX_PIPES];

/* ============================================================================
 * open() Implementation
 *
 * Strategy:
 * 1. Allocate a file descriptor in the task's fd table
 * 2. For stdin/stdout/stderr, initialize with console ports
 * 3. For regular files, send open request to filesystem server (TODO)
 * ============================================================================ */

kern_return_t bsd_handle_open(task_context_t *ctx,
                              bsd_open_request_t *req,
                              bsd_open_reply_t *reply)
{
    int fd;

    if (!ctx || !req) {
        reply->result = -1;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    /* Check if path is empty */
    if (req->path[0] == '\0') {
        reply->result = -1;
        reply->error = POSIX_ENOENT;
        return POSIX_ENOENT;
    }

    /* Allocate a file descriptor */
    fd = bsd_alloc_fd(ctx);
    if (fd < 0) {
        reply->result = -1;
        reply->error = POSIX_EMFILE;
        return POSIX_EMFILE;
    }

    /* Initialize fd entry */
    ctx->fds[fd].fd = fd;
    ctx->fds[fd].flags = req->flags;
    ctx->fds[fd].offset = 0;
    ctx->fds[fd].is_pipe = false;
    ctx->fds[fd].port = MACH_PORT_NULL;

    /* TODO: Send open request to filesystem server via Mach IPC.
     * The FS server would return a port representing the open file handle.
     * For now, we just allocate the FD without backing it with real data. */

    reply->result = fd;
    reply->error = 0;

    return KERN_SUCCESS;
}

/* ============================================================================
 * close() Implementation
 *
 * Strategy:
 * 1. Look up fd in task's fd table
 * 2. Deallocate the port (if any)
 * 3. Clear the fd entry
 * ============================================================================ */

kern_return_t bsd_handle_close(task_context_t *ctx,
                               bsd_close_request_t *req,
                               bsd_close_reply_t *reply)
{
    if (!ctx || !req) {
        reply->result = -1;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    int fd = req->fd;

    if (fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        reply->result = -1;
        reply->error = POSIX_EBADF;
        return POSIX_EBADF;
    }

    fd_entry_t *entry = &ctx->fds[fd];

    /* Check if this FD is actually open */
    if (entry->port == MACH_PORT_NULL && entry->fd == 0) {
        reply->result = -1;
        reply->error = POSIX_EBADF;
        return POSIX_EBADF;
    }

    /* Free the fd */
    bsd_free_fd(ctx, fd);

    reply->result = 0;
    reply->error = 0;

    return KERN_SUCCESS;
}

/* ============================================================================
 * read() Implementation
 *
 * Strategy:
 * 1. Look up fd in task's fd table
 * 2. If pipe, read from pipe buffer
 * 3. If file, delegate to filesystem server (TODO)
 * 4. Copy data into reply buffer for inline return
 * ============================================================================ */

kern_return_t bsd_handle_read(task_context_t *ctx,
                              bsd_read_request_t *req,
                              mach_msg_header_t *reply_buf,
                              mach_msg_size_t reply_max,
                              mach_msg_size_t *reply_actual)
{
    fd_entry_t *entry;
    pipe_buffer_t *pipe;

    if (!ctx || !req || !reply_buf || !reply_actual) {
        return POSIX_EINVAL;
    }

    int fd = req->fd;
    mach_msg_size_t count = req->count;

    if (fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        /* Set up error reply */
        bsd_read_reply_t *rrep = (bsd_read_reply_t *)reply_buf;
        rrep->result = -1;
        rrep->error = POSIX_EBADF;
        rrep->header.msgh_size = sizeof(bsd_read_reply_t);
        *reply_actual = sizeof(bsd_read_reply_t);
        return POSIX_EBADF;
    }

    entry = &ctx->fds[fd];

    /* Check if this FD is actually open */
    if (entry->port == MACH_PORT_NULL && entry->fd == 0) {
        bsd_read_reply_t *rrep = (bsd_read_reply_t *)reply_buf;
        rrep->result = -1;
        rrep->error = POSIX_EBADF;
        rrep->header.msgh_size = sizeof(bsd_read_reply_t);
        *reply_actual = sizeof(bsd_read_reply_t);
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

        /* Set up reply pointer early so it's available for all code paths */
        bsd_read_reply_t *rrep = (bsd_read_reply_t *)reply_buf;

        if (!pipe) {
            rrep->result = -1;
            rrep->error = POSIX_EBADF;
            rrep->header.msgh_size = sizeof(bsd_read_reply_t);
            *reply_actual = sizeof(bsd_read_reply_t);
            return POSIX_EBADF;
        }

        /* Read from pipe buffer */
        mach_msg_size_t bytes_read = 0;
        mach_msg_size_t max_read = count < BSD_INLINE_REPLY_MAX ? count : BSD_INLINE_REPLY_MAX;

        while (bytes_read < max_read && pipe->head != pipe->tail) {
            rrep->data[bytes_read] = pipe->buffer[pipe->tail];
            pipe->tail = (pipe->tail + 1) % BSD_PIPE_BUFFER_SIZE;
            bytes_read++;
        }

        rrep->result = (int)bytes_read;
        rrep->error = 0;
        rrep->header.msgh_size = (mach_msg_size_t)(sizeof(bsd_read_reply_t) -
                              (BSD_INLINE_REPLY_MAX - bytes_read));
        *reply_actual = rrep->header.msgh_size;

        /* Update file offset */
        entry->offset += bytes_read;

        return KERN_SUCCESS;
    }

    /* For regular files, delegate to filesystem server.
     * TODO: Send read request to FS server via Mach IPC with OOL data. */
    bsd_read_reply_t *rrep = (bsd_read_reply_t *)reply_buf;
    rrep->result = 0;  /* EOF - no data available yet */
    rrep->error = 0;
    rrep->header.msgh_size = sizeof(bsd_read_reply_t);
    *reply_actual = sizeof(bsd_read_reply_t);

    /* Update file offset */
    entry->offset += 0;

    return KERN_SUCCESS;
}

/* ============================================================================
 * write() Implementation
 *
 * Strategy:
 * 1. Look up fd in task's fd table
 * 2. If pipe, write to pipe buffer
 * 3. If file, delegate to filesystem server (TODO)
 * 4. For stdout/stderr, track bytes written
 * ============================================================================ */

kern_return_t bsd_handle_write(task_context_t *ctx,
                               bsd_write_request_t *req,
                               bsd_write_reply_t *reply)
{
    fd_entry_t *entry;
    pipe_buffer_t *pipe;

    if (!ctx || !req || !reply) {
        return POSIX_EINVAL;
    }

    int fd = req->fd;
    mach_msg_size_t count = req->count;

    if (fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        reply->result = -1;
        reply->error = POSIX_EBADF;
        return POSIX_EBADF;
    }

    entry = &ctx->fds[fd];

    /* Check if this FD is actually open */
    if (entry->port == MACH_PORT_NULL && entry->fd == 0) {
        reply->result = -1;
        reply->error = POSIX_EBADF;
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
            reply->result = -1;
            reply->error = POSIX_EBADF;
            return POSIX_EBADF;
        }

        /* Write to pipe buffer */
        mach_msg_size_t bytes_written = 0;
        while (bytes_written < count) {
            /* Check if pipe is full */
            if (((pipe->head + 1) % BSD_PIPE_BUFFER_SIZE) == pipe->tail) {
                break; /* Pipe full */
            }

            pipe->buffer[pipe->head] = ((const char *)&req[1])[bytes_written];
            pipe->head = (pipe->head + 1) % BSD_PIPE_BUFFER_SIZE;
            bytes_written++;
        }

        reply->result = (int)bytes_written;
        reply->error = 0;

        /* Notify any waiting reader */
        /* TODO: send message to read_port */

        return KERN_SUCCESS;
    }

    /* For regular files, delegate to filesystem server.
     * TODO: Send write request to FS server via Mach IPC with OOL data. */

    /* For stdout/stderr, assume all written (console output) */
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        reply->result = (int)count;
        reply->error = 0;
    } else {
        /* Regular file - not yet implemented */
        reply->result = 0;
        reply->error = POSIX_EIO;
    }

    /* Update file offset */
    entry->offset += reply->result;

    return KERN_SUCCESS;
}

/* ============================================================================
 * fcntl() Implementation
 * ============================================================================ */

kern_return_t bsd_handle_fcntl(task_context_t *ctx,
                               bsd_fcntl_request_t *req,
                               bsd_fcntl_reply_t *reply)
{
    fd_entry_t *entry;

    if (!ctx || !req || reply) {
        return POSIX_EINVAL;
    }

    int fd = req->fd;
    int cmd = req->cmd;
    long arg = req->arg;

    if (fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        reply->result = -1;
        reply->error = POSIX_EBADF;
        return POSIX_EBADF;
    }

    entry = &ctx->fds[fd];

    /* Check if this FD is actually open */
    if (entry->port == MACH_PORT_NULL && entry->fd == 0) {
        reply->result = -1;
        reply->error = POSIX_EBADF;
        return POSIX_EBADF;
    }

    switch (cmd) {
        case F_DUPFD: {
            int new_fd = bsd_alloc_fd(ctx);
            if (new_fd < 0) {
                reply->result = -1;
                reply->error = POSIX_EMFILE;
                return POSIX_EMFILE;
            }
            memcpy(&ctx->fds[new_fd], entry, sizeof(fd_entry_t));
            ctx->fds[new_fd].fd = new_fd;
            if (entry->port != MACH_PORT_NULL) {
                mach_port_mod_refs(mach_task_self(), entry->port,
                                   MACH_MSG_TYPE_MAKE_SEND, 1);
            }
            reply->result = new_fd;
            reply->error = 0;
            return KERN_SUCCESS;
        }

        case F_GETFD:
            reply->result = 0; /* No FD_CLOEXEC for now */
            reply->error = 0;
            return KERN_SUCCESS;

        case F_SETFD:
            /* TODO: support FD_CLOEXEC */
            reply->result = 0;
            reply->error = 0;
            return KERN_SUCCESS;

        case F_GETFL:
            reply->result = (long)entry->flags;
            reply->error = 0;
            return KERN_SUCCESS;

        case F_SETFL:
            entry->flags = (int)arg;
            reply->result = 0;
            reply->error = 0;
            return KERN_SUCCESS;

        default:
            reply->result = -1;
            reply->error = POSIX_EINVAL;
            return POSIX_EINVAL;
    }
}