/*
 * TriniOS BSD Compatibility Server - Pipe Implementation
 *
 * Implements pipe() using Mach message channels.
 * Each pipe is a ring buffer with associated Mach ports for
 * synchronization between reader and writer.
 */

#include "bsd_server.h"
#include "posix.h"
#include <string.h>

/* ============================================================================
 * Global pipe table (declared in main.c)
 * extern pipe_buffer_t pipe_table[BSD_MAX_PIPES];
 * extern int pipe_count;
 * ============================================================================ */

/* ============================================================================
 * pipe() Implementation
 *
 * Strategy:
 * 1. Allocate a pipe buffer from the pipe table
 * 2. Create two Mach ports (read and write ends)
 * 3. Allocate two file descriptors in the task's fd table
 * 4. Return fds[0] (read) and fds[1] (write)
 * ============================================================================ */

kern_return_t bsd_handle_pipe(task_context_t *ctx, int *fds)
{
    kern_return_t kr;
    int pipe_idx;
    pipe_buffer_t *pipe;
    int read_fd, write_fd;

    if (!ctx || !fds) {
        return POSIX_EINVAL;
    }

    /* Allocate a pipe buffer */
    pipe_idx = bsd_alloc_pipe();
    if (pipe_idx < 0) {
        return POSIX_ENFILE; /* Too many open files in system */
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
        return kr;
    }

    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &pipe->write_port);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), pipe->read_port);
        pipe->read_port = MACH_PORT_NULL;
        return kr;
    }

    /* Insert send rights */
    kr = mach_port_insert_right(mach_task_self(), pipe->read_port, pipe->read_port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        bsd_close_pipe(pipe_idx);
        return kr;
    }

    kr = mach_port_insert_right(mach_task_self(), pipe->write_port, pipe->write_port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        bsd_close_pipe(pipe_idx);
        return kr;
    }

    /* Allocate file descriptors */
    read_fd = bsd_alloc_fd(ctx);
    if (read_fd < 0) {
        bsd_close_pipe(pipe_idx);
        return POSIX_EMFILE;
    }

    write_fd = bsd_alloc_fd(ctx);
    if (write_fd < 0) {
        bsd_free_fd(ctx, read_fd);
        bsd_close_pipe(pipe_idx);
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
    fds[0] = read_fd;
    fds[1] = write_fd;

    /* Increment pipe count */
    pipe_count++;

    return KERN_SUCCESS;
}

/* ============================================================================
 * Pipe Close (internal)
 *
 * Cleans up a pipe buffer and its Mach ports.
 * Called when both ends are closed or on error.
 * ============================================================================ */

static void bsd_close_pipe(int pipe_idx)
{
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
    pipe_count--;
}

/* ============================================================================
 * Pipe Write End Closed
 *
 * Called when the write end of a pipe is closed.
 * Readers should get EOF (0 bytes returned).
 * ============================================================================ */

void bsd_pipe_close_write(int pipe_idx)
{
    pipe_buffer_t *pipe = &pipe_table[pipe_idx];
    pipe->closed_write = true;

    /* If there's data remaining, readers can still drain it */
    /* If buffer is empty, readers get EOF immediately */
    if (pipe->head == pipe->tail) {
        /* Notify waiting readers of EOF */
        /* TODO: send EOF message to read_port */
    }
}

/* ============================================================================
 * Pipe Read End Closed
 *
 * Called when the read end of a pipe is closed.
 * Writers should get SIGPIPE.
 * ============================================================================ */

void bsd_pipe_close_read(int pipe_idx)
{
    pipe_buffer_t *pipe = &pipe_table[pipe_idx];
    pipe->closed_read = true;

    /* Writers to a closed pipe should receive SIGPIPE */
    /* TODO: send SIGPIPE to any task holding the write fd */

    /* If both ends closed, clean up */
    if (pipe->closed_write && pipe->closed_read) {
        bsd_close_pipe(pipe_idx);
    }
}

/* ============================================================================
 * Pipe Data Available Check
 *
 * Returns the number of bytes available for reading.
 * ============================================================================ */

int bsd_pipe_bytes_available(int pipe_idx)
{
    pipe_buffer_t *pipe = &pipe_table[pipe_idx];

    if (pipe->head >= pipe->tail) {
        return pipe->head - pipe->tail;
    } else {
        return BSD_PIPE_BUFFER_SIZE - pipe->tail + pipe->head;
    }
}

/* ============================================================================
 * Pipe Space Available Check
 *
 * Returns the number of bytes that can be written.
 * ============================================================================ */

int bsd_pipe_space_available(int pipe_idx)
{
    return BSD_PIPE_BUFFER_SIZE - 1 - bsd_pipe_bytes_available(pipe_idx);
}