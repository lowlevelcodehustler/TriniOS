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

/* ============================================================================
 * Server Configuration
 * ============================================================================ */

#define BSD_MAX_FILE_DESCRIPTORS 256
#define BSD_MAX_TASKS 1024
#define BSD_MAX_PIPES 256
#define BSD_PIPE_BUFFER_SIZE 65536

/* ============================================================================
 * Message Types (BSD IPC over Mach)
 * ============================================================================ */

typedef enum {
    BSD_MSG_FORK        = 0x0001,
    BSD_MSG_EXECVE      = 0x0002,
    BSD_MSG_WAITPID     = 0x0003,
    BSD_MSG_EXIT        = 0x0004,
    BSD_MSG_KILL        = 0x0005,
    BSD_MSG_OPEN        = 0x0101,
    BSD_MSG_CLOSE       = 0x0102,
    BSD_MSG_READ        = 0x0103,
    BSD_MSG_WRITE       = 0x0104,
    BSD_MSG_PIPE        = 0x0105,
    BSD_MSG_SIGNAL      = 0x0201,
} bsd_msg_type_t;

/* ============================================================================
 * BSD Request/Reply Messages
 * ============================================================================ */

typedef struct {
    mach_msg_header_t       header;
    mach_msg_size_t         msgh_descriptor_count;
    bsd_msg_type_t          type;
    mach_msg_id_t           reply_id;
} bsd_request_t;

typedef struct {
    mach_msg_header_t       header;
    mach_msg_size_t         msgh_descriptor_count;
    kern_return_t           result;
} bsd_reply_t;

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

typedef struct {
    task_t         task;
    mach_port_t    bsd_port;       /* Port for IPC with this task */
    struct task_context *parent;   /* Parent task (for fork) */
    struct task_context *children; /* Linked list of children */
    int            pid;
    int            ppid;
    int            exit_status;
    bool           exited;
    fd_entry_t     fds[BSD_MAX_FILE_DESCRIPTORS];
} task_context_t;

/* ============================================================================
 * Pipe Buffer
 * ============================================================================ */

typedef struct {
    char            buffer[BSD_PIPE_BUFFER_SIZE];
    int             head;
    int             tail;
    mach_port_t     read_port;
    mach_port_t     write_port;
    bool            closed_read;
    bool            closed_write;
} pipe_buffer_t;

/* ============================================================================
 * Server Functions
 * ============================================================================ */

/* Main server entry point */
void bsd_server_main(void);

/* Message dispatching */
kern_return_t bsd_dispatch_request(bsd_request_t *req, bsd_reply_t *reply);

/* Process management */
kern_return_t bsd_handle_fork(task_context_t *caller, task_context_t **child, int *pid);
kern_return_t bsd_handle_execve(task_context_t *ctx, const char *path, char *const argv[], char *const envp[]);
kern_return_t bsd_handle_waitpid(task_context_t *ctx, int pid, int *status);
kern_return_t bsd_handle_exit(task_context_t *ctx, int status);
kern_return_t bsd_handle_kill(int pid, int sig);

/* File I/O */
kern_return_t bsd_handle_open(task_context_t *ctx, const char *path, int flags, int *fd);
kern_return_t bsd_handle_close(task_context_t *ctx, int fd);
kern_return_t bsd_handle_read(task_context_t *ctx, int fd, void *buf, mach_msg_size_t count, mach_msg_size_t *done);
kern_return_t bsd_handle_write(task_context_t *ctx, int fd, const void *buf, mach_msg_size_t count, mach_msg_size_t *done);
kern_return_t bsd_handle_pipe(task_context_t *ctx, int *fds);

/* Signal handling */
kern_return_t bsd_handle_signal(task_context_t *ctx, int sig, void (*handler)(int));

/* Task context management */
task_context_t *bsd_get_task_context(task_t task);
void bsd_free_task_context(task_context_t *ctx);

/* File descriptor management */
int bsd_alloc_fd(task_context_t *ctx);
void bsd_free_fd(task_context_t *ctx, int fd);

#endif /* BSD_SERVER_H */