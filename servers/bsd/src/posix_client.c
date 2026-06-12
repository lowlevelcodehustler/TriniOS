/*
 * TriniOS BSD Compatibility Server - POSIX Client Library
 *
 * Client-side wrappers for POSIX syscalls.
 * Each function sends a Mach IPC message to the BSD server
 * and waits for the reply.
 *
 * Linked as libbsd_client.a for user-space applications.
 */

#include "posix.h"
#include "bsd_server.h"
#include <mach/mach.h>
#include <mach/message.h>
#include <mach/port.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * BSD Server Port Management
 * ============================================================================ */

/* Global BSD server port - set during init or obtained from bootstrap */
static mach_port_t g_bsd_server_port = MACH_PORT_NULL;

/* Initialize connection to BSD server */
void bsd_client_init(mach_port_t server_port)
{
    g_bsd_server_port = server_port;
}

/* Get the current BSD server port */
mach_port_t bsd_client_get_port(void)
{
    return g_bsd_server_port;
}

/* ============================================================================
 * Helper: Send a BSD request and wait for reply
 * ============================================================================ */

static kern_return_t bsd_client_send_request(bsd_msg_type_t type,
                                             mach_msg_size_t req_size,
                                             void *req_buf,
                                             mach_msg_size_t reply_size,
                                             void *reply_buf)
{
    kern_return_t kr;
    mach_msg_option_t options;

    if (g_bsd_server_port == MACH_PORT_NULL) {
        return KERN_INVALID_RIGHT;
    }

    /* Set up the request message header */
    bsd_request_t *request = (bsd_request_t *)req_buf;
    request->header.msgh_remote_port = g_bsd_server_port;
    request->header.msgh_local_port = MACH_PORT_NULL;
    request->header.msgh_size = (mach_msg_size_t)req_size;
    request->header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    request->header.msgh_id = (mach_msg_id_t)type;
    request->type = type;

    /* Set up reply buffer */
    bsd_reply_t *reply = (bsd_reply_t *)reply_buf;
    reply->header.msgh_remote_port = g_bsd_server_port;
    reply->header.msgh_local_port = MACH_PORT_NULL;
    reply->header.msgh_size = (mach_msg_size_t)reply_size;
    reply->header.msgh_bits = MACH_MSGH_BITS(0, MACH_MSG_TYPE_MAKE_SEND);
    reply->header.msgh_id = (mach_msg_id_t)(type + 100); /* Reply ID = request + 100 */

    /* Send request */
    options = MACH_SEND_MSG | MACH_RCV_MSG;

    kr = MachMsgSend(&request->header, req_size, 0,
                     reply_buf, reply_size,
                     MACH_PORT_NULL,
                     MACH_MSG_TIMEOUT_NONE,
                     MACH_PORT_NULL);

    if (kr != KERN_SUCCESS) {
        return kr;
    }

    return reply->result;
}

/* ============================================================================
 * Process Management
 * ============================================================================ */

int posix_fork(void)
{
    /* Fork requires special handling - the BSD server creates a new task
     * and returns the child PID. This is a simplified implementation. */
    kern_return_t kr;
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kr = bsd_client_send_request(BSD_MSG_FORK,
                                 sizeof(request),
                                 &request,
                                 sizeof(reply),
                                 &reply);

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    /* TODO: Parse PID from reply */
    return 0;
}

int posix_execve(const char *path, char *const argv[], char *const envp[])
{
    /* Execve requires sending the path and argument arrays to the BSD server.
     * For now, this is a placeholder. */
    (void)path;
    (void)argv;
    (void)envp;

    /* TODO: Implement execve message with out-of-line memory descriptors */
    return -POSIX_ENOSYS;
}

int posix_waitpid(int pid, int *status, int options)
{
    kern_return_t kr;
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kr = bsd_client_send_request(BSD_MSG_WAITPID,
                                 sizeof(request),
                                 &request,
                                 sizeof(reply),
                                 &reply);

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    if (status) {
        *status = (int)reply.result;
    }

    /* TODO: Parse actual PID from reply */
    return 0;
}

void posix_exit(int status)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    (void)bsd_client_send_request(BSD_MSG_EXIT,
                                  sizeof(request),
                                  &request,
                                  sizeof(reply),
                                  &reply);

    /* Don't return - process should terminate */
    task_suspend(mach_task_self());
    /* If suspend returns, try to terminate */
    task_terminate(mach_task_self());
    /* Final fallback */
    exit(status);
}

int posix_kill(int pid, int sig)
{
    kern_return_t kr;
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kr = bsd_client_send_request(BSD_MSG_KILL,
                                 sizeof(request),
                                 &request,
                                 sizeof(reply),
                                 &reply);

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    return 0;
}

int posix_getpid(void)
{
    /* Get PID from task context - requires querying BSD server */
    /* For now, return a placeholder */
    /* TODO: Implement PID query message */
    return 0;
}

int posix_getppid(void)
{
    /* Get parent PID from task context */
    /* TODO: Implement PPID query message */
    return 0;
}

/* ============================================================================
 * File I/O
 * ============================================================================ */

int posix_open(const char *path, int flags, ...)
{
    (void)path;
    (void)flags;

    /* TODO: Implement open with path descriptor in message */
    return -POSIX_ENOSYS;
}

int posix_close(int fd)
{
    kern_return_t kr;
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kr = bsd_client_send_request(BSD_MSG_CLOSE,
                                 sizeof(request),
                                 &request,
                                 sizeof(reply),
                                 &reply);

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    return 0;
}

int posix_read(int fd, void *buf, unsigned int count)
{
    kern_return_t kr;
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kr = bsd_client_send_request(BSD_MSG_READ,
                                 sizeof(request),
                                 &request,
                                 sizeof(reply),
                                 &reply);

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    /* TODO: Copy data from reply to buf */
    (void)buf;
    (void)count;
    return 0;
}

int posix_write(int fd, const void *buf, unsigned int count)
{
    kern_return_t kr;
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kr = bsd_client_send_request(BSD_MSG_WRITE,
                                 sizeof(request),
                                 &request,
                                 sizeof(reply),
                                 &reply);

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    /* TODO: Send data via out-of-line memory descriptor */
    (void)buf;
    (void)count;
    return 0;
}

int posix_pipe(int pipefd[2])
{
    kern_return_t kr;
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kr = bsd_client_send_request(BSD_MSG_PIPE,
                                 sizeof(request),
                                 &request,
                                 sizeof(reply),
                                 &reply);

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    /* TODO: Parse pipe file descriptors from reply */
    pipefd[0] = 3; /* Placeholder read end */
    pipefd[1] = 4; /* Placeholder write end */
    return 0;
}

int posix_fcntl(int fd, int cmd, ...)
{
    (void)fd;
    (void)cmd;

    /* TODO: Implement fcntl */
    return -POSIX_ENOSYS;
}

/* ============================================================================
 * Signal Handling
 * ============================================================================ */

void (*posix_signal(int sig, void (*handler)(int)))(int)
{
    kern_return_t kr;
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kr = bsd_client_send_request(BSD_MSG_SIGNAL,
                                 sizeof(request),
                                 &request,
                                 sizeof(reply),
                                 &reply);

    if (kr != KERN_SUCCESS) {
        return SIG_ERR;
    }

    (void)sig;
    (void)handler;
    return NULL;
}