/*
 * TriniOS BSD Compatibility Server - Main Entry Point
 *
 * The BSD server runs as a user-space Mach server. It receives IPC messages
 * from user-space applications requesting POSIX system calls, translates
 * them into Mach kernel operations, and returns results.
 */

#include "bsd_server.h"
#include "initrd_lookup.h"
#include "mach_ipc_bridge.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

mach_port_t g_bsd_server_port = MACH_PORT_NULL;
mach_port_t g_bootstrap_port = MACH_PORT_NULL;
int         next_pid = 1;

task_context_t *task_table[BSD_MAX_TASKS];
int task_count = 0;

pipe_buffer_t pipe_table[BSD_MAX_PIPES];
int pipe_count = 0;

/* ============================================================================
 * Helper: Find task context by receive port
 * ============================================================================ */

task_context_t *bsd_get_task_context(mach_port_t port)
{
    for (int i = 0; i < BSD_MAX_TASKS; i++) {
        if (task_table[i] && task_table[i]->bsd_port == port) {
            return task_table[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Helper: Find task context by PID
 * ============================================================================ */

task_context_t *bsd_get_task_context_by_pid(int pid)
{
    for (int i = 0; i < BSD_MAX_TASKS; i++) {
        if (task_table[i] && task_table[i]->pid == pid && !task_table[i]->exited) {
            return task_table[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Helper: Create task context
 * ============================================================================ */

int bsd_create_task_context(bool is_init, mach_port_t task_port,
                            mach_port_t bsd_recv_port, int ppid,
                            const char *name)
{
    (void)name; /* For now, don't store name */

    for (int i = 0; i < BSD_MAX_TASKS; i++) {
        if (task_table[i] == NULL) {
            task_context_t *ctx = malloc(sizeof(task_context_t));
            if (!ctx) return -1;

            memset(ctx, 0, sizeof(task_context_t));
            ctx->task_port = task_port;
            ctx->bsd_port = bsd_recv_port;
            ctx->is_init = is_init;
            ctx->pid = next_pid++;
            ctx->ppid = ppid;
            ctx->exited = false;
            ctx->exit_status = 0;
            ctx->num_children = 0;

            /* Initialize children array */
            ctx->children = malloc(sizeof(task_context_t *) * BSD_MAX_TASKS);
            if (!ctx->children) {
                free(ctx);
                return -1;
            }

            /* Allocate file descriptors for init process (stdin/stdout/stderr) */
            if (is_init) {
                bsd_init_std_fds(ctx);
            }

            task_table[i] = ctx;
            task_count++;
            return ctx->pid;
        }
    }
    return -1; /* Table full */
}

/* ============================================================================
 * Helper: Initialize stdin/stdout/stderr for a task context
 * ============================================================================ */

void bsd_init_std_fds(task_context_t *ctx)
{
    /* stdin = fd 0, stdout = fd 1, stderr = fd 2 */
    ctx->fds[0].fd = 0;
    ctx->fds[0].port = MACH_PORT_NULL;
    ctx->fds[0].flags = O_RDONLY;
    ctx->fds[0].offset = 0;
    ctx->fds[0].is_pipe = false;

    ctx->fds[1].fd = 1;
    ctx->fds[1].port = MACH_PORT_NULL;
    ctx->fds[1].flags = O_WRONLY;
    ctx->fds[1].offset = 0;
    ctx->fds[1].is_pipe = false;

    ctx->fds[2].fd = 2;
    ctx->fds[2].port = MACH_PORT_NULL;
    ctx->fds[2].flags = O_WRONLY;
    ctx->fds[2].offset = 0;
    ctx->fds[2].is_pipe = false;
}

/* ============================================================================
 * Helper: Remove task from table
 * ============================================================================ */

void bsd_free_task_context(task_context_t *ctx)
{
    if (!ctx) return;

    for (int i = 0; i < BSD_MAX_TASKS; i++) {
        if (task_table[i] == ctx) {
            /* Free children array */
            if (ctx->children) {
                free(ctx->children);
                ctx->children = NULL;
            }

            task_table[i] = NULL;
            task_count--;

            /* Deallocate Mach ports */
            if (ctx->task_port != MACH_PORT_NULL) {
                mach_port_deallocate(mach_task_self(), ctx->task_port);
            }
            if (ctx->bsd_port != MACH_PORT_NULL) {
                mach_port_deallocate(mach_task_self(), ctx->bsd_port);
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
    for (int i = 3; i < BSD_MAX_FILE_DESCRIPTORS; i++) { /* Skip stdin/stdout/stderr */
        if (ctx->fds[i].port == MACH_PORT_NULL && ctx->fds[i].fd == 0) {
            ctx->fds[i].fd = i;
            return i;
        }
    }
    return -1; /* EMFILE */
}

/* ============================================================================
 * Helper: Free file descriptor
 * ============================================================================ */

void bsd_free_fd(task_context_t *ctx, int fd)
{
    if (fd < 0 || fd >= BSD_MAX_FILE_DESCRIPTORS) return;

    if (ctx->fds[fd].port != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), ctx->fds[fd].port);
        ctx->fds[fd].port = MACH_PORT_NULL;
    }
    memset(&ctx->fds[fd], 0, sizeof(fd_entry_t));
}

/* ============================================================================
 * Helper: Allocate new pipe buffer
 * ============================================================================ */

static int bsd_alloc_pipe(void)
{
    for (int i = 0; i < BSD_MAX_PIPES; i++) {
        if (pipe_table[i].read_port == MACH_PORT_NULL &&
            pipe_table[i].write_port == MACH_PORT_NULL) {
            memset(&pipe_table[i], 0, sizeof(pipe_buffer_t));
            return i;
        }
    }
    return -1; /* Pipe table full */
}

/* ============================================================================
 * Helper: Send a Mach reply message via IPC bridge
 * In the shared memory bridge mode, replies go through mach_ipc_bridge_send_reply()
 * which writes to shared memory and signals the client via pipe.
 * ============================================================================ */

static kern_return_t bsd_send_reply(mach_port_t remote_port,
                                    mach_msg_header_t *reply,
                                    mach_msg_size_t size)
{
    if (remote_port == MACH_PORT_NULL) {
        return KERN_INVALID_RIGHT;
    }

    reply->msgh_bits |= MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    reply->msgh_remote_port = remote_port;

    /* Use the IPC bridge to send the reply via shared memory + pipe signaling */
    int ret = mach_ipc_bridge_send_reply(reply, size);
    if (ret != 0) {
        return KERN_FAILURE;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * Message Dispatch - parses Mach messages and calls appropriate handlers
 * ============================================================================ */

kern_return_t bsd_dispatch_request(mach_msg_header_t *msg, mach_msg_size_t msg_size)
{
    task_context_t *ctx = NULL;
    kern_return_t kr;

    /* Find the calling task context by looking up the sender's receive port */
    ctx = bsd_get_task_context(msg->msgh_local_port);
    if (!ctx && msg->msgh_remote_port != MACH_PORT_NULL) {
        /* Try to find by matching remote_port to a task's bsd_port */
        for (int i = 0; i < BSD_MAX_TASKS; i++) {
            if (task_table[i] && task_table[i]->bsd_port == msg->msgh_remote_port) {
                ctx = task_table[i];
                break;
            }
        }
    }

    /* If we still can't find the context, try to use init task as fallback */
    if (!ctx) {
        for (int i = 0; i < BSD_MAX_TASKS; i++) {
            if (task_table[i] && task_table[i]->is_init) {
                ctx = task_table[i];
                break;
            }
        }
    }

    bsd_msg_type_t type = (bsd_msg_type_t)msg->msgh_id;

    switch (type) {
        /* ========================================================================
         * Process Management
         * ======================================================================== */
        case BSD_MSG_FORK: {
            bsd_fork_request_t  *req = (bsd_fork_request_t *)msg;
            bsd_fork_reply_t    reply;
            (void)req;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_fork(ctx, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        case BSD_MSG_EXECVE: {
            bsd_execve_request_t  *req = (bsd_execve_request_t *)msg;
            bsd_execve_reply_t     reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_execve(ctx, req, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        case BSD_MSG_WAITPID: {
            bsd_waitpid_request_t *req = (bsd_waitpid_request_t *)msg;
            bsd_waitpid_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_waitpid(ctx, req, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        case BSD_MSG_EXIT: {
            bsd_exit_request_t *req = (bsd_exit_request_t *)msg;
            bsd_exit_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_exit(ctx, req, &reply);
            return kr; /* No reply needed for exit */
        }

        case BSD_MSG_KILL: {
            bsd_kill_request_t *req = (bsd_kill_request_t *)msg;
            bsd_kill_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_kill(req->pid, req->sig);
            reply.result = (kr == KERN_SUCCESS) ? 0 : -1;
            reply.header.msgh_remote_port = msg->msgh_local_port;
            reply.header.msgh_id = msg->msgh_id + 100;
            reply.header.msgh_size = sizeof(reply);
            kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            return kr;
        }

        case BSD_MSG_GETPID: {
            bsd_getpid_request_t *req = (bsd_getpid_request_t *)msg;
            bsd_getpid_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            (void)req;
            if (ctx) {
                reply.result = ctx->pid;
            } else {
                reply.result = -1;
            }
            reply.header.msgh_remote_port = msg->msgh_local_port;
            reply.header.msgh_id = msg->msgh_id + 100;
            reply.header.msgh_size = sizeof(reply);
            kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            return kr;
        }

        /* ========================================================================
         * File I/O
         * ======================================================================== */
        case BSD_MSG_OPEN: {
            bsd_open_request_t  *req = (bsd_open_request_t *)msg;
            bsd_open_reply_t     reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_open(ctx, req, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        case BSD_MSG_CLOSE: {
            bsd_close_request_t *req = (bsd_close_request_t *)msg;
            bsd_close_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_close(ctx, req, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        case BSD_MSG_READ: {
            bsd_read_request_t *req = (bsd_read_request_t *)msg;
            uint8_t reply_buf[BSD_INLINE_REPLY_MAX];
            mach_msg_size_t reply_actual;

            memset(reply_buf, 0, sizeof(reply_buf));
            kr = bsd_handle_read(ctx, req, (mach_msg_header_t *)reply_buf,
                                 BSD_INLINE_REPLY_MAX, &reply_actual);
            if (kr == KERN_SUCCESS) {
                /* Set up the reply header */
                mach_msg_header_t *rep = (mach_msg_header_t *)reply_buf;
                rep->msgh_remote_port = msg->msgh_local_port;
                rep->msgh_id = msg->msgh_id + 100;
                rep->msgh_size = reply_actual;
                kr = bsd_send_reply(msg->msgh_remote_port, rep, reply_actual);
            }
            return kr;
        }

        case BSD_MSG_WRITE: {
            bsd_write_request_t *req = (bsd_write_request_t *)msg;
            bsd_write_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_write(ctx, req, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        case BSD_MSG_PIPE: {
            bsd_pipe_request_t *req = (bsd_pipe_request_t *)msg;
            bsd_pipe_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_pipe(ctx, req, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        case BSD_MSG_FCNTL: {
            bsd_fcntl_request_t *req = (bsd_fcntl_request_t *)msg;
            bsd_fcntl_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_fcntl(ctx, req, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        /* ========================================================================
         * Signal Handling
         * ======================================================================== */
        case BSD_MSG_SIGNAL: {
            bsd_signal_request_t *req = (bsd_signal_request_t *)msg;
            bsd_signal_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            kr = bsd_handle_signal(ctx, req, &reply);
            if (kr == KERN_SUCCESS) {
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        /* ========================================================================
         * Bootstrap / Initialization
         * ======================================================================== */
        case BSD_MSG_INIT_TASK: {
            bsd_init_task_request_t *req = (bsd_init_task_request_t *)msg;
            bsd_init_task_reply_t    reply;

            memset(&reply, 0, sizeof(reply));
            if (!ctx) {
                /* Create a new task context */
                mach_port_t recv_port = MACH_PORT_NULL;
                kern_return_t kr2 = mach_port_allocate(mach_task_self(),
                                                       MACH_PORT_RIGHT_RECEIVE,
                                                       &recv_port);
                if (kr2 != KERN_SUCCESS) {
                    reply.result = -1;
                    reply.error = 1;
                    return kr2;
                }

                /* Insert send right */
                kr2 = mach_port_insert_right(mach_task_self(), recv_port,
                                             recv_port, MACH_MSG_TYPE_MAKE_SEND);
                if (kr2 != KERN_SUCCESS) {
                    mach_port_deallocate(mach_task_self(), recv_port);
                    reply.result = -1;
                    reply.error = 2;
                    return kr2;
                }

                int pid = bsd_create_task_context(false, MACH_PORT_NULL,
                                                 recv_port, req->ppid,
                                                 req->name);
                if (pid < 0) {
                    mach_port_deallocate(mach_task_self(), recv_port);
                    reply.result = -1;
                    reply.error = 3; /* Table full */
                } else {
                    reply.result = 0;
                    reply.pid = pid;
                    reply.error = 0;

                    /* Send the new task's receive port back so it can send messages */
                    reply.header.msgh_remote_port = msg->msgh_local_port;
                    reply.header.msgh_id = msg->msgh_id + 100;
                    reply.header.msgh_size = sizeof(reply);
                    kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
                }
            } else {
                /* Task already exists */
                reply.result = -1;
                reply.error = 4; /* Already initialized */
                reply.header.msgh_remote_port = msg->msgh_local_port;
                reply.header.msgh_id = msg->msgh_id + 100;
                reply.header.msgh_size = sizeof(reply);
                kr = bsd_send_reply(msg->msgh_remote_port, &reply.header, sizeof(reply));
            }
            return kr;
        }

        default:
            /* Unknown message type */
            fprintf(stderr, "Unknown BSD server message type: %d\n", (int)type);
            return KERN_INVALID_ARGUMENT;
    }
}

/* ============================================================================
 * Initialize the BSD server
 * ============================================================================ */

int bsd_server_init(mach_port_t bootstrap_port)
{
    kern_return_t kr;

    g_bootstrap_port = bootstrap_port;

    /* Initialize the shared memory IPC bridge (pipe-based signaling + mmap) */
    kr = mach_ipc_bridge_init();
    if (kr != 0) {
        fprintf(stderr, "bsd_server_init: IPC bridge init failed\n");
        return -1;
    }
    fprintf(stderr, "BSD server: shared memory IPC bridge initialized\n");

    /* Create the server's main receive port */
    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_bsd_server_port);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "bsd_server_init: mach_port_allocate failed: %s\n",
                mach_error_string(kr));
        return -1;
    }

    /* Insert send right so we can register with bootstrap */
    kr = mach_port_insert_right(mach_task_self(), g_bsd_server_port,
                                g_bsd_server_port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "bsd_server_init: mach_port_insert_right failed: %s\n",
                mach_error_string(kr));
        return -1;
    }

    /* Register with bootstrap port */
    if (bootstrap_port != MACH_PORT_NULL) {
        /* TODO: Actually register with bootstrap server via bootstrap_register2() */
        fprintf(stderr, "BSD server registered on port %u\n", g_bsd_server_port);
    }

    return 0;
}

/* ============================================================================
 * Server Main Loop
 * ============================================================================ */

void bsd_server_main(void)
{
    kern_return_t kr;
    mach_msg_size_t max_msg_size = 4096;
    uint8_t msg_buffer[4096];

    /* Initialize the BSD server with bootstrap port */
    kr = bsd_server_init(g_bootstrap_port);
    if (kr != 0) {
        return;
    }

    /* Create init task context (PID 1) */
    mach_port_t init_recv_port = MACH_PORT_NULL;
    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &init_recv_port);
    if (kr == KERN_SUCCESS) {
        kr = mach_port_insert_right(mach_task_self(), init_recv_port,
                                    init_recv_port, MACH_MSG_TYPE_MAKE_SEND);
        if (kr == KERN_SUCCESS) {
            int pid = bsd_create_task_context(true, MACH_PORT_NULL,
                                             init_recv_port, 0, "init");
            if (pid < 0) {
                mach_port_deallocate(mach_task_self(), init_recv_port);
            } else {
                fprintf(stderr, "Init task created with PID %d\n", pid);
            }
        }
    }

    /* Main server loop - receive and dispatch messages via IPC bridge */
    while (1) {
        /* Use the IPC bridge to wait for a request from a client.
         * The shared memory + pipe signaling handles all transport. */
        int ret = mach_ipc_bridge_process_one();
        if (ret == -1) {
            /* Timeout — retry */
            continue;
        }
        if (ret < 0) {
            /* Fatal error */
            fprintf(stderr, "IPC bridge fatal error\n");
            break;
        }

        /* Get the shared memory region via accessor and dispatch the request */
        shm_region_t *shm = (shm_region_t *)mach_ipc_bridge_get_server_shm();
        kern_return_t result = bsd_dispatch_request(&shm->request,
                                                     shm->request.msgh_size);

        /* If dispatch didn't send a reply (e.g., for exit), skip */
        if (result != KERN_SUCCESS && result != 0) {
            fprintf(stderr, "Dispatch failed: %s\n", mach_error_string(result));
        }
    }
}

/* ============================================================================
 * Helper: Parse --initrd=0xADDRESS command-line argument
 * ============================================================================ */

static const void *parse_initrd_arg(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        /* Check for --initrd= prefix */
        const char *prefix = "--initrd=";
        size_t prefix_len = strlen(prefix);

        if (strncmp(argv[i], prefix, prefix_len) == 0) {
            /* Extract hex address */
            const char *hex_str = argv[i] + prefix_len;
            unsigned long addr = strtoul(hex_str, NULL, 16);
            fprintf(stderr, "TriniOS BSD server: initrd at 0x%lx\n", addr);
            return (const void *)(uintptr_t)addr;
        }
    }
    return NULL;
}

/* ============================================================================
 * Entry Point
 * ============================================================================ */

int main(int argc, char *argv[])
{
    /* ── Parse initrd address from command line ──────────────────────── */
    const void *initrd_data = parse_initrd_arg(argc, argv);
    uint64_t initrd_size = 0;

    if (initrd_data) {
        /* Default initrd size: 128 MB max. In a full implementation, the
         * bootloader would pass this via multiboot2 or command-line arg. */
        const char *size_arg = NULL;
        for (int i = 1; i < argc; i++) {
            const char *prefix = "--initrd-size=";
            size_t prefix_len = strlen(prefix);
            if (strncmp(argv[i], prefix, prefix_len) == 0) {
                size_arg = argv[i] + prefix_len;
                break;
            }
        }

        if (size_arg) {
            initrd_size = strtoul(size_arg, NULL, 10);
        } else {
            /* Default: assume 64 MB */
            initrd_size = 64 * 1024 * 1024;
        }

        fprintf(stderr, "TriniOS BSD server: initrd size %lu bytes\n", (unsigned long)initrd_size);

        /* Initialize the lookup table from the cpio archive */
        int file_count = initrd_lookup_init(initrd_data, initrd_size);
        if (file_count < 0) {
            fprintf(stderr, "TriniOS BSD server: FAILED to parse initrd\n");
            return 1;
        }

        fprintf(stderr, "TriniOS BSD server: %d files registered in initrd lookup\n", file_count);
    } else {
        fprintf(stderr, "TriniOS BSD server: WARNING — no --initrd= argument provided\n");
        fprintf(stderr, "TriniOS BSD server: execve() will fail (no binary data source)\n");
        fprintf(stderr, "TriniOS BSD server: Usage: %s --initrd=0xADDRESS [--initrd-size=SIZE]\n",
                argv[0]);
    }

    /* ── Initialize Mach ports and start main loop ───────────────────── */
    fprintf(stderr, "TriniOS BSD Compatibility Server starting...\n");
    bsd_server_main();

    return 0;
}
