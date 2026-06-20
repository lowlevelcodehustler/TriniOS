/*
 * TriniOS BSD Compatibility Server - Pipe Implementation
 *
 * Implements pipe() using ring buffers with Mach ports for synchronization.
 */

#include "bsd_server.h"
#include "posix.h"
#include <string.h>

/* ============================================================================
 * External: pipe_table is defined in main.c
 * ============================================================================ */

extern pipe_buffer_t pipe_table[BSD_MAX_PIPES];
extern int pipe_count;

/* ============================================================================
 * pipe() Implementation
 *
 * Strategy:
 * 1. Allocate a pipe buffer from the pipe table
 * 2. Create two Mach ports (read and write ends)
 * 3. Allocate two file descriptors in the task's fd table
 * 4. Return fds[0] (read) and fds[1] (write) via reply struct
 * ============================================================================ */

kern_return_t bsd_handle_pipe(task_context_t *ctx,
                              bsd_pipe_request_t *req,
                              bsd_pipe_reply_t *reply)
{
    kern_return_t kr;
    int pipe_idx;
    pipe_buffer_t *pipe;
    int read_fd, write_fd;

    (void)req;

    if (!ctx || !reply) {
        reply->result = -1;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    /* Allocate a pipe buffer */
    pipe_idx = -1;
    for (int i = 0; i < BSD_MAX_PIPES; i++) {
        if (pipe_table[i].read_port == MACH_PORT_NULL &&
            pipe_table[i].write_port == MACH_PORT_NULL) {
            pipe_idx = i;
            break;
        }
    }

    if (pipe_idx < 0) {
        reply->result = -1;
        reply->error = POSIX_ENFILE;
        return POSIX_ENFILE;
    }

    pipe = &pipe_table[pipe_idx];

    /* Initialize pipe buffer */
    memset(pipe, 0, sizeof(pipe_buffer_t));
    pipe->head = 0;
    pipe->tail = 0;
    pipe->closed_read = false;
    pipe->closed_write = false;

    /* Create Mach ports for synchronization */
    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &pipe->read_port);
    if (kr != KERN_SUCCESS) {
        reply->result = -1;
        reply->error = kr;
        return kr;
    }

    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &pipe->write_port);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), pipe->read_port);
        pipe->read_port = MACH_PORT_NULL;
        reply->result = -1;
        reply->error = kr;
        return kr;
    }

    /* Insert send rights */
    kr = mach_port_insert_right(mach_task_self(), pipe->read_port,
                                pipe->read_port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), pipe->write_port);
        pipe->write_port = MACH_PORT_NULL;
        mach_port_deallocate(mach_task_self(), pipe->read_port);
        pipe->read_port = MACH_PORT_NULL;
        reply->result = -1;
        reply->error = kr;
        return kr;
    }

    kr = mach_port_insert_right(mach_task_self(), pipe->write_port,
                                pipe->write_port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), pipe->write_port);
        pipe->write_port = MACH_PORT_NULL;
        mach_port_deallocate(mach_task_self(), pipe->read_port);
        pipe->read_port = MACH_PORT_NULL;
        reply->result = -1;
        reply->error = kr;
        return kr;
    }

    /* Allocate file descriptors */
    read_fd = bsd_alloc_fd(ctx);
    if (read_fd < 0) {
        mach_port_deallocate(mach_task_self(), pipe->write_port);
        pipe->write_port = MACH_PORT_NULL;
        mach_port_deallocate(mach_task_self(), pipe->read_port);
        pipe->read_port = MACH_PORT_NULL;
        memset(pipe, 0, sizeof(pipe_buffer_t));
        reply->result = -1;
        reply->error = POSIX_EMFILE;
        return POSIX_EMFILE;
    }

    write_fd = bsd_alloc_fd(ctx);
    if (write_fd < 0) {
        bsd_free_fd(ctx, read_fd);
        mach_port_deallocate(mach_task_self(), pipe->write_port);
        pipe->write_port = MACH_PORT_NULL;
        mach_port_deallocate(mach_task_self(), pipe->read_port);
        pipe->read_port = MACH_PORT_NULL;
        memset(pipe, 0, sizeof(pipe_buffer_t));
        reply->result = -1;
        reply->error = POSIX_EMFILE;
        return POSIX_EMFILE;
    }

    /* Set up read end */
    ctx->fds[read_fd].fd = read_fd;
    ctx->fds[read_fd].port = pipe->read_port;
    ctx->fds[read_fd].flags = O_RDONLY;
    ctx->fds[read_fd].offset = 0;
    ctx->fds[read_fd].is_pipe = true;

    /* Set up write end */
    ctx->fds[write_fd].fd = write_fd;
    ctx->fds[write_fd].port = pipe->write_port;
    ctx->fds[write_fd].flags = O_WRONLY;
    ctx->fds[write_fd].offset = 0;
    ctx->fds[write_fd].is_pipe = true;

    /* Return file descriptors */
    reply->result = 0;
    reply->error = 0;
    reply->read_fd = read_fd;
    reply->write_fd = write_fd;

    /* Increment pipe count */
    pipe_count++;

    return KERN_SUCCESS;
}

/* ============================================================================
 * Internal: Close a pipe buffer and its Mach ports
 * ============================================================================ */

static void bsd_close_pipe(int pipe_idx)
{
    if (pipe_idx < 0 || pipe_idx >= BSD_MAX_PIPES) {
        return;
    }

    pipe_buffer_t *pipe = &pipe_table[pipe_idx];

    if (pipe->read_port != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), pipe->read_port);
        pipe->read_port = MACH_PORT_NULL;
    }

    if (pipe->write_port != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), pipe->write_port);
        pipe->write_port = MACH_PORT_NULL;
    }

    memset(pipe, 0, sizeof(pipe_buffer_t));

    if (pipe_count > 0) {
        pipe_count--;
    }
}

/* ============================================================================
 * Internal: Close write end of a pipe (for close() handling)
 * ============================================================================ */

void bsd_pipe_close_write(task_context_t *ctx, int fd)
{
    if (!ctx || fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        return;
    }

    fd_entry_t *entry = &ctx->fds[fd];
    if (!entry->is_pipe || entry->port == MACH_PORT_NULL) {
        return;
    }

    /* Find the pipe buffer by matching write_port */
    for (int i = 0; i < BSD_MAX_PIPES; i++) {
        if (pipe_table[i].write_port == entry->port) {
            pipe_table[i].closed_write = true;

            /* If both ends closed, clean up */
            if (pipe_table[i].closed_read) {
                bsd_close_pipe(i);
            }
            break;
        }
    }
}

/* ============================================================================
 * Internal: Close read end of a pipe (for close() handling)
 * ============================================================================ */

void bsd_pipe_close_read(task_context_t *ctx, int fd)
{
    if (!ctx || fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) {
        return;
    }

    fd_entry_t *entry = &ctx->fds[fd];
    if (!entry->is_pipe || entry->port == MACH_PORT_NULL) {
        return;
    }

    /* Find the pipe buffer by matching read_port */
    for (int i = 0; i < BSD_MAX_PIPES; i++) {
        if (pipe_table[i].read_port == entry->port) {
            pipe_table[i].closed_read = true;

            /* If both ends closed, clean up */
            if (pipe_table[i].closed_write) {
                bsd_close_pipe(i);
            }
            break;
        }
    }
}