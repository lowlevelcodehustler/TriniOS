/*
 * TriniOS BSD Compatibility Server - IPC Message Definitions
 *
 * Defines request/reply message structures for Mach IPC between
 * user-space POSIX clients and the BSD server kernel extension.
 */

#ifndef BSD_MESSAGES_H
#define BSD_MESSAGES_H

#include <mach/mach.h>
#include <mach/message.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Message Types (BSD IPC over Mach)
 * ============================================================================ */

typedef enum {
    /* Process management */
    BSD_MSG_FORK        = 1000,
    BSD_MSG_EXECVE      = 1001,
    BSD_MSG_WAITPID     = 1002,
    BSD_MSG_EXIT        = 1003,
    BSD_MSG_KILL        = 1004,
    BSD_MSG_GETPID      = 1005,
    BSD_MSG_GETPPID     = 1006,

    /* File I/O */
    BSD_MSG_OPEN        = 2000,
    BSD_MSG_CLOSE       = 2001,
    BSD_MSG_READ        = 2002,
    BSD_MSG_WRITE       = 2003,
    BSD_MSG_PIPE        = 2004,
    BSD_MSG_FCNTL       = 2005,

    /* Signal handling */
    BSD_MSG_SIGNAL      = 3000,

    /* Bootstrap / initialization */
    BSD_MSG_INIT_TASK   = 4000,
} bsd_msg_type_t;

/* ============================================================================
 * Process Management Messages
 * ============================================================================ */

/* fork request - no body needed, reply contains child PID */
typedef struct {
    mach_msg_header_t header;
    /* No body for simple fork */
} bsd_fork_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* 0 = parent, >0 = child PID */
    int               error;        /* errno on failure */
} bsd_fork_reply_t;

/* execve request - path sent inline (max 256 chars), argv/envp via OOL */
#define BSD_MAX_PATH_LEN 256

typedef struct {
    mach_msg_header_t header;
    char              path[BSD_MAX_PATH_LEN];
    int               argc;
    int               envc;
} bsd_execve_request_t;

/* execve reply - entry point for new thread startup */
typedef struct {
    mach_msg_header_t header;
    int               result;       /* 0 on success, -1 on error */
    int               error;        /* errno on failure */
    uint64_t          entry_point;  /* New thread should start here */
} bsd_execve_reply_t;

/* waitpid request */
typedef struct {
    mach_msg_header_t header;
    int               pid;          /* PID to wait for (-1 = any) */
    int               options;      /* WNOHANG, etc. */
} bsd_waitpid_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* Child PID on success, -1 on error */
    int               status;       /* Exit status (if status ptr was given) */
    int               error;
} bsd_waitpid_reply_t;

/* exit request */
typedef struct {
    mach_msg_header_t header;
    int               status;
} bsd_exit_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;
} bsd_exit_reply_t;

/* kill request */
typedef struct {
    mach_msg_header_t header;
    int               pid;
    int               sig;
} bsd_kill_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* 0 on success, -1 on error */
    int               error;
} bsd_kill_reply_t;

/* getpid request */
typedef struct {
    mach_msg_header_t header;
} bsd_getpid_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* PID */
} bsd_getpid_reply_t;

/* ============================================================================
 * File I/O Messages
 * ============================================================================ */

/* open request - path sent inline */
typedef struct {
    mach_msg_header_t header;
    char              path[BSD_MAX_PATH_LEN];
    int               flags;
    int               mode;         /* Permission bits (ignored for now) */
} bsd_open_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* FD on success, -1 on error */
    int               error;
} bsd_open_reply_t;

/* close request */
typedef struct {
    mach_msg_header_t header;
    int               fd;
} bsd_close_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* 0 on success, -1 on error */
    int               error;
} bsd_close_reply_t;

/* read request - data buffer sent via OOL descriptor in send direction */
#define BSD_READ_OOL_MAX (64 * 1024)

typedef struct {
    mach_msg_header_t header;
    int               fd;
    mach_msg_size_t   count;        /* Requested byte count */
} bsd_read_request_t;

/* Read reply - data returned inline for small reads, OOL for large */
#define BSD_INLINE_REPLY_MAX 4096

typedef struct {
    mach_msg_header_t header;
    int               result;       /* Bytes read on success, -1 on error */
    int               error;
    uint8_t           data[BSD_INLINE_REPLY_MAX];
} bsd_read_reply_t;

/* write request - data sent via OOL descriptor */
typedef struct {
    mach_msg_header_t header;
    int               fd;
    mach_msg_size_t   count;        /* Byte count to write */
    /* Data follows as OOL or inline for small writes (< 64 bytes) */
} bsd_write_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* Bytes written on success, -1 on error */
    int               error;
} bsd_write_reply_t;

/* pipe request */
typedef struct {
    mach_msg_header_t header;
} bsd_pipe_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* 0 on success, -1 on error */
    int               error;
    int               read_fd;      /* Read end FD */
    int               write_fd;     /* Write end FD */
} bsd_pipe_reply_t;

/* fcntl request */
typedef struct {
    mach_msg_header_t header;
    int               fd;
    int               cmd;
    long              arg;
} bsd_fcntl_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* Value on success, -1 on error */
    int               error;
} bsd_fcntl_reply_t;

/* ============================================================================
 * Signal Messages
 * ============================================================================ */

/* signal request */
typedef struct {
    mach_msg_header_t header;
    int               sig;          /* Signal number (0 = unset handler) */
    uint64_t          handler_addr; /* Handler function address in user space */
} bsd_signal_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* Previous handler on success, SIG_ERR (-1) */
    int               error;
} bsd_signal_reply_t;

/* ============================================================================
 * Bootstrap / Initialization Messages
 * ============================================================================ */

/* init_task request - sent by a new task to register with BSD server */
typedef struct {
    mach_msg_header_t header;
    char              name[64];
    int               ppid;         /* Parent PID */
} bsd_init_task_request_t;

typedef struct {
    mach_msg_header_t header;
    int               result;       /* 0 on success, -1 on error */
    int               pid;          /* Assigned PID */
    int               error;
} bsd_init_task_reply_t;

/* ============================================================================
 * Generic Request/Reply Types (used by posix_client.c)
 * ============================================================================ */

/* Generic request — used when no specific data needed (e.g. fork, getpid) */
typedef struct {
    mach_msg_header_t header;
} bsd_request_t;

/* Generic reply — contains result code for simple IPC calls */
typedef struct {
    mach_msg_header_t header;
    int               result;
    int               error;
} bsd_reply_t;

/* ============================================================================
 * OOL (Out-of-Line) Descriptor Helpers
 * ============================================================================ */

/* Maximum number of OOL descriptors per message */
#define BSD_MAX_OOL_DESCRIPTORS 4

/* Mach OOL descriptor type for use in posix_client.c */
typedef struct {
    void *address;
    mach_msg_size_t size;
    boolean_t copy;
} mach_msg_ool_descriptor_t;

/* OOL flag for msgh_bits — GNU Mach doesn't define this in user headers */
#ifndef MACH_MSG_OOL_DESCRIPTOR
#define MACH_MSG_OOL_DESCRIPTOR 0x1000
#endif

/* msgh_descriptor_count is not in the GNU Mach header struct,
 * but posix_client.c uses it. Map to a no-op for freestanding builds. */
#ifndef msgh_descriptor_count
#define msgh_descriptor_count /* empty — ignored in freestanding */
#endif

/* OOL descriptor type for data transfer */
typedef enum {
    BSD_OOL_PATH       = 0,   /* execve path string */
    BSD_OOL_ARGV       = 1,   /* execve argv array */
    BSD_OOL_ENVP       = 2,   /* execve envp array */
    BSD_OOL_READ_BUF   = 3,   /* read buffer (receive direction) */
    BSD_OOL_WRITE_DATA = 4,   /* write data (send direction) */
} bsd_ool_type_t;

/* ============================================================================
 * Message Packing/Unpacking Helpers
 * ============================================================================ */

/*
 * Pack argv/envp arrays into a contiguous buffer for OOL transfer.
 * Returns total bytes written. Caller must free the buffer.
 */
static inline mach_msg_size_t bsd_pack_argv_envp(char *const argv[], char *const envp[],
                                                  void **out_buf, mach_msg_size_t *out_size)
{
    if (!argv && !envp) {
        *out_buf = NULL;
        *out_size = 0;
        return 0;
    }

    /* First pass: calculate total size */
    mach_msg_size_t total = 0;

    if (argv) {
        for (int i = 0; argv[i]; i++) {
            total += strlen(argv[i]) + 1;
        }
        total += sizeof(char *); /* NULL terminator pointer */
    }
    total += sizeof(char *);   /* NULL terminator for argv array itself */

    if (envp) {
        for (int i = 0; envp[i]; i++) {
            total += strlen(envp[i]) + 1;
        }
        total += sizeof(char *); /* NULL terminator pointer */
    }
    total += sizeof(char *);   /* NULL terminator for envp array itself */

    /* Allocate buffer */
    void *buf = malloc(total);
    if (!buf) {
        *out_buf = NULL;
        *out_size = 0;
        return 0;
    }

    char *p = (char *)buf;

    /* Write argv strings and pointers */
    if (argv) {
        for (int i = 0; argv[i]; i++) {
            mach_msg_size_t len = strlen(argv[i]) + 1;
            memcpy(p, argv[i], len);
            p += len;
        }
        *(char ***)p = NULL;
        p += sizeof(char *);
    }

    /* Write envp strings and pointers */
    if (envp) {
        for (int i = 0; envp[i]; i++) {
            mach_msg_size_t len = strlen(envp[i]) + 1;
            memcpy(p, envp[i], len);
            p += len;
        }
        *(char ***)p = NULL;
        p += sizeof(char *);
    }

    *out_buf = buf;
    *out_size = (mach_msg_size_t)(p - (char *)buf);
    return *out_size;
}

/* Unpack argv from a contiguous buffer. Returns pointer to argv array. */
static inline char **bsd_unpack_argv(const void *buf, int argc)
{
    if (!buf || argc <= 0) {
        return NULL;
    }
    /* argv is at the start of the buffer as an array of pointers */
    return (char **)buf;
}

#endif /* BSD_MESSAGES_H */