/*
 * TriniOS BSD Compatibility Server - Main Entry Point
 *
 * The BSD server runs as a user-space Mach server. It receives IPC messages
 * from user-space applications requesting POSIX system calls, translates
 * them into Mach kernel operations, and returns results.
 */

#include "bsd_server.h"
#include "posix.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

static mach_port_t bsd_server_port;      /* Main server receive port */
static mach_port_t bsd_bootstrap_port;   /* Bootstrap port from launchd */
static int         next_pid = 1;         /* Monotonically increasing PID counter */

/* Array of all known task contexts */
static task_context_t *task_table[BSD_MAX_TASKS];
static int task_count = 0;

/* Array of all pipe buffers */
static pipe_buffer_t pipe_table[BSD_MAX_PIPES];
static int pipe_count = 0;

/* ============================================================================
 * Helper: Find task context by task
 * ============================================================================ */

task_context_t *bsd_get_task_context(task_t task)
{
    for (int i = 0; i < task_count; i++) {
        if (task_table[i] && task_table[i]->task == task) {
            return task_table[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Helper: Find task context by PID
 * ============================================================================ */

static task_context_t *bsd_get_task_context_by_pid(int pid)
{
    for (int i = 0; i < task_count; i++) {
        if (task_table[i] && task_table[i]->pid == pid && !task_table[i]->exited) {
            return task_table[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Helper: Add task to table
 * ============================================================================ */

static void bsd_add_task(task_context_t *ctx)
{
    if (task_count >= BSD_MAX_TASKS) {
        return; /* Task table full */
    }
    for (int i = 0; i < BSD_MAX_TASKS; i++) {
        if (task_table[i] == NULL) {
            task_table[i] = ctx;
            task_count++;
            return;
        }
    }
}

/* ============================================================================
 * Helper: Remove task from table
 * ============================================================================ */

void bsd_free_task_context(task_context_t *ctx)
{
    for (int i = 0; i < BSD_MAX_TASKS; i++) {
        if (task_table[i] == ctx) {
            task_table[i] = NULL;
            task_count--;
            /* Deallocate the task's VM */
            if (ctx->task != MACH_PORT_NULL) {
                task_deallocate(ctx->task);
            }
            free(ctx);
            return;
        }
    }
}

/* ============================================================================
 * Helper: Allocate file descriptor
 * ============================================================================ */

int bsd_alloc_fd(task_context_t *ctx)
{
    for (int i = 0; i < BSD_MAX_FILE_DESCRIPTORS; i++) {
        if (ctx->fds[i].fd == 0 && ctx->fds[i].port == MACH_PORT_NULL) {
            ctx->fds[i].fd = i;
            return i;
        }
    }
    return -POSIX_EMFILE;
}

/* ============================================================================
 * Helper: Free file descriptor
 * ============================================================================ */

void bsd_free_fd(task_context_t *ctx, int fd)
{
    if (fd >= 0 && fd < BSD_MAX_FILE_DESCRIPTORS) {
        if (ctx->fds[fd].port != MACH_PORT_NULL) {
            mach_port_deallocate(mach_task_self(), ctx->fds[fd].port);
        }
        memset(&ctx->fds[fd], 0, sizeof(fd_entry_t));
    }
}

/* ============================================================================
 * Helper: Allocate new pipe buffer
 * ============================================================================ */

static int bsd_alloc_pipe(void)
{
    for (int i = 0; i < BSD_MAX_PIPES; i++) {
        if (pipe_table[i].read_port == MACH_PORT_NULL) {
            return i;
        }
    }
    return -1; /* Pipe table full */
}

/* ============================================================================
 * Message Dispatch
 * ============================================================================ */

kern_return_t bsd_dispatch_request(bsd_request_t *req, bsd_reply_t *reply)
{
    task_context_t *ctx = NULL;

    /* Look up the calling task context from the message sender */
    ctx = bsd_get_task_context_by_pid(0); /* TODO: derive from message port */

    switch (req->type) {
        /* Process management */
        case BSD_MSG_FORK:
            return bsd_handle_fork(ctx, NULL, NULL);
        case BSD_MSG_EXECVE:
            /* Path, argv, envp come in message body */
            return KERN_SUCCESS; /* Placeholder */
        case BSD_MSG_WAITPID:
            return bsd_handle_waitpid(ctx, -1, NULL);
        case BSD_MSG_EXIT:
            return bsd_handle_exit(ctx, 0);
        case BSD_MSG_KILL:
            return bsd_handle_kill(-1, 0);

        /* File I/O */
        case BSD_MSG_OPEN:
            return bsd_handle_open(ctx, "", 0, NULL);
        case BSD_MSG_CLOSE:
            return bsd_handle_close(ctx, -1);
        case BSD_MSG_READ:
            return bsd_handle_read(ctx, -1, NULL, 0, NULL);
        case BSD_MSG_WRITE:
            return bsd_handle_write(ctx, -1, NULL, 0, NULL);
        case BSD_MSG_PIPE:
            return bsd_handle_pipe(ctx, NULL);

        /* Signal handling */
        case BSD_MSG_SIGNAL:
            return bsd_handle_signal(ctx, 0, NULL);

        default:
            return KERN_INVALID_RIGHT;
    }
}

/* ============================================================================
 * Server Main Loop
 * ============================================================================ */

void bsd_server_main(void)
{
    kern_return_t kr;
    mach_msg_size_t max_msg_size = 4096;

    /* Create the server's main receive port */
    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &bsd_server_port);
    if (kr != KERN_SUCCESS) {
        /* Cannot continue without a receive port */
        return;
    }

    /* Insert the port into the task's port set */
    kr = mach_port_insert_right(mach_task_self(), bsd_server_port, bsd_server_port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        return;
    }

    /* Initialize stdin/stdout/stderr for init task */
    /* TODO: Create initial task context for init process */

    /* Main server loop - receive and dispatch messages */
    while (1) {
        bsd_request_t request;
        bsd_reply_t reply;
        mach_msg_return_t ret;

        /* Wait for incoming message */
        ret = mach_msg(&request.header,
                       MACH_RCV_MSG | MACH_RCV_INTERRUPTIBLE,
                       0,
                       max_msg_size,
                       bsd_server_port,
                       MACH_MSG_TIMEOUT_NONE,
                       MACH_PORT_NULL);

        if (ret != MACH_MSG_SUCCESS) {
            if (ret == MACH_RCV_TIMED_OUT) {
                continue;
            }
            /* Handle other errors */
            continue;
        }

        /* Dispatch the request */
        reply.result = bsd_dispatch_request(&request, &reply);

        /* Send reply back to caller */
        if (request.header.msgh_remote_port != MACH_PORT_NULL) {
            mach_msg(&reply.header,
                     MACH_SEND_MSG,
                     sizeof(bsd_reply_t),
                     0,
                     MACH_PORT_NULL,
                     MACH_MSG_TIMEOUT_NONE,
                     MACH_PORT_NULL);
        }
    }
}

/* ============================================================================
 * Entry Point
 * ============================================================================ */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Initialize the BSD server */
    bsd_server_main();

    return 0;
}