/*
 * TriniOS BSD Compatibility Server
 *
 * Main server interface - manages IPC message dispatching
 * between user-space POSIX applications and the Mach kernel.
 */

#ifndef BSD_SERVER_H
#define BSD_SERVER_H

#include <mach/mach.h>
#include <mach/message.h>
#include <mach/port.h>
#include <mach/vm_map.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "messages.h"

/* ============================================================================
 * Server Configuration
 * ============================================================================ */

#define BSD_MAX_FILE_DESCRIPTORS 256
#define BSD_MAX_TASKS 1024
#define BSD_MAX_PIPES 256
#define BSD_PIPE_BUFFER_SIZE 65536

/* ============================================================================
 * File Descriptor Table Entry
 * ============================================================================ */

typedef struct {
    int         fd;
    mach_port_t port;
    int         flags;    /* O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc. */
    vm_offset_t offset;   /* Current file offset */
    bool        is_pipe;
} fd_entry_t;

/* ============================================================================
 * Task Context (per-process state managed by BSD server)
 * ============================================================================ */

typedef struct task_context {
    mach_port_t    task_port;       /* Send right to Mach task port */
    mach_port_t    bsd_port;        /* Receive port for this task's requests */
    struct task_context *parent;    /* Parent task (for fork) */
    struct task_context **children; /* Array of child tasks */
    int            num_children;
    int            pid;
    int            ppid;
    int            exit_status;
    bool           exited;
    bool           is_init;         /* Is this the init process? */
    fd_entry_t     fds[BSD_MAX_FILE_DESCRIPTORS];
} task_context_t;

/* ============================================================================
 * Pipe Buffer
 * ============================================================================ */

typedef struct {
    char            buffer[BSD_PIPE_BUFFER_SIZE];
    int             head;
    int             tail;
    mach_port_t     read_port;      /* Send right for reader */
    mach_port_t     write_port;     /* Send right for writer */
    bool            closed_read;
    bool            closed_write;
} pipe_buffer_t;

/* ============================================================================
 * Global State (defined in main.c)
 * ============================================================================ */

extern mach_port_t g_bsd_server_port;
extern mach_port_t g_bootstrap_port;
extern int         next_pid;
extern task_context_t *task_table[BSD_MAX_TASKS];
extern int task_count;
extern pipe_buffer_t pipe_table[BSD_MAX_PIPES];
extern int pipe_count;

/* ============================================================================
 * Server Functions
 * ============================================================================ */

/* Main server entry point */
void bsd_server_main(void);

/* Initialize the BSD server (called once at startup) */
int bsd_server_init(mach_port_t bootstrap_port);

/* Message dispatching - parses Mach messages and calls handlers */
kern_return_t bsd_dispatch_request(mach_msg_header_t *msg, mach_msg_size_t msg_size);

/* ============================================================================
 * Process Management Handlers
 * ============================================================================ */

kern_return_t bsd_handle_fork(task_context_t *parent_ctx,
                              bsd_fork_reply_t *reply);

kern_return_t bsd_handle_execve(task_context_t *ctx,
                                bsd_execve_request_t *req,
                                bsd_execve_reply_t *reply);

kern_return_t bsd_handle_waitpid(task_context_t *ctx,
                                 bsd_waitpid_request_t *req,
                                 bsd_waitpid_reply_t *reply);

kern_return_t bsd_handle_exit(task_context_t *ctx,
                              bsd_exit_request_t *req,
                              bsd_exit_reply_t *reply);

kern_return_t bsd_handle_kill(int pid, int sig);

/* ============================================================================
 * File I/O Handlers
 * ============================================================================ */

kern_return_t bsd_handle_open(task_context_t *ctx,
                              bsd_open_request_t *req,
                              bsd_open_reply_t *reply);

kern_return_t bsd_handle_close(task_context_t *ctx,
                               bsd_close_request_t *req,
                               bsd_close_reply_t *reply);

kern_return_t bsd_handle_read(task_context_t *ctx,
                              bsd_read_request_t *req,
                              mach_msg_header_t *reply_buf,
                              mach_msg_size_t reply_max,
                              mach_msg_size_t *reply_actual);

kern_return_t bsd_handle_write(task_context_t *ctx,
                               bsd_write_request_t *req,
                               bsd_write_reply_t *reply);

kern_return_t bsd_handle_pipe(task_context_t *ctx,
                              bsd_pipe_request_t *req,
                              bsd_pipe_reply_t *reply);

kern_return_t bsd_handle_fcntl(task_context_t *ctx,
                               bsd_fcntl_request_t *req,
                               bsd_fcntl_reply_t *reply);

/* ============================================================================
 * Signal Handlers
 * ============================================================================ */

kern_return_t bsd_handle_signal(task_context_t *ctx,
                                bsd_signal_request_t *req,
                                bsd_signal_reply_t *reply);

/* ============================================================================
 * Task Context Management
 * ============================================================================ */

task_context_t *bsd_get_task_context(mach_port_t port);
task_context_t *bsd_get_task_context_by_pid(int pid);
void bsd_free_task_context(task_context_t *ctx);
int bsd_create_task_context(bool is_init, mach_port_t task_port,
                            mach_port_t bsd_recv_port, int ppid,
                            const char *name);

/* ============================================================================
 * File Descriptor Management
 * ============================================================================ */

int bsd_alloc_fd(task_context_t *ctx);
void bsd_free_fd(task_context_t *ctx, int fd);

/* Initialize stdin/stdout/stderr for a task context */
void bsd_init_std_fds(task_context_t *ctx);

/* ============================================================================
 * Pipe Management (from pipe.c)
 * ============================================================================ */

/* Close read/write ends of a pipe when FD is closed */
void bsd_pipe_close_write(task_context_t *ctx, int fd);
void bsd_pipe_close_read(task_context_t *ctx, int fd);

/* ============================================================================
 * Signal Delivery (from signal.c)
 * ============================================================================ */

/* Deliver a signal to a task */
void bsd_deliver_signal(task_context_t *target, int sig);

#endif /* BSD_SERVER_H */
