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
#include "messages.h"
#include <mach/mach.h>
#include <mach/message.h>
#include <mach/port.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * BSD Server Port Management
 * ============================================================================ */

// No static here — bsd_server.h already declares extern mach_port_t g_bsd_server_port
// We use that global instead of a local static copy.

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

static kern_return_t bsd_client_send_receive(bsd_msg_type_t type,
                                             mach_msg_header_t *request,
                                             mach_msg_size_t req_size,
                                             mach_msg_header_t *reply,
                                             mach_msg_size_t rep_size)
{
    if (!request || !reply) {
        return KERN_INVALID_ARGUMENT;
    }

    if (g_bsd_server_port == MACH_PORT_NULL) {
        return KERN_INVALID_RIGHT;
    }

    /* Set up the request message header */
    request->msgh_remote_port = g_bsd_server_port;
    request->msgh_local_port = MACH_PORT_NULL;
    request->msgh_size = (mach_msg_size_t)req_size;
    request->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    request->msgh_id = (mach_msg_id_t)type;

    /* Set up reply buffer header */
    reply->msgh_remote_port = g_bsd_server_port;
    reply->msgh_local_port = MACH_PORT_NULL;
    reply->msgh_size = (mach_msg_size_t)rep_size;
    reply->msgh_bits = MACH_MSGH_BITS(0, MACH_MSG_TYPE_MAKE_SEND);
    reply->msgh_id = 0; /* Will be filled by server */

    /* Send request and receive reply in one call */
    kern_return_t kr = mach_msg(request,
                                MACH_SEND_MSG | MACH_RCV_MSG,
                                req_size,
                                rep_size,
                                g_bsd_server_port,
                                MACH_MSG_TIMEOUT_NONE,
                                MACH_PORT_NULL);

    if (kr != KERN_SUCCESS) {
        return kr;
    }

    /* Extract result from reply */
    bsd_reply_t *breply = (bsd_reply_t *)reply;
    return breply->result;
}

/* ============================================================================
 * Helper: Send request with OOL (out-of-line) data
 * ============================================================================ */

static kern_return_t bsd_client_send_ool(bsd_msg_type_t type,
                                         mach_msg_header_t *request,
                                         mach_msg_size_t req_size,
                                         void *ool_data,
                                         mach_msg_size_t ool_size,
                                         mach_msg_header_t *reply,
                                         mach_msg_size_t rep_size)
{
    if (!request || !reply) {
        return KERN_INVALID_ARGUMENT;
    }

    if (g_bsd_server_port == MACH_PORT_NULL) {
        return KERN_INVALID_RIGHT;
    }

    /* Set up OOL descriptor in the request */
    mach_msg_ool_descriptor_t *ool_desc = NULL;

    /* Find space for OOL descriptor after the request header */
    /* The request must have room for at least one OOL descriptor */
    mach_msg_size_t total_size = req_size + sizeof(mach_msg_ool_descriptor_t);

    /* Set up the request message header */
    request->msgh_remote_port = g_bsd_server_port;
    request->msgh_local_port = MACH_PORT_NULL;
    request->msgh_size = (mach_msg_size_t)req_size;
    request->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND | MACH_MSG_OOL_DESCRIPTOR, 0);
    request->msgh_id = (mach_msg_id_t)type;
    /* msgh_descriptor_count is a no-op in freestanding builds */

    /* Set up reply buffer header */
    reply->msgh_remote_port = g_bsd_server_port;
    reply->msgh_local_port = MACH_PORT_NULL;
    reply->msgh_size = (mach_msg_size_t)rep_size;
    reply->msgh_bits = MACH_MSGH_BITS(0, MACH_MSG_TYPE_MAKE_SEND);
    reply->msgh_id = 0;

    /* Send request and receive reply */
    kern_return_t kr = mach_msg(request,
                                MACH_SEND_MSG | MACH_RCV_MSG,
                                total_size,
                                rep_size,
                                g_bsd_server_port,
                                MACH_MSG_TIMEOUT_NONE,
                                MACH_PORT_NULL);

    if (kr != KERN_SUCCESS) {
        return kr;
    }

    bsd_reply_t *breply = (bsd_reply_t *)reply;
    return breply->result;
}

/* ============================================================================
 * Process Management
 * ============================================================================ */

int posix_fork(void)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_FORK,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    /* Fork returns 0 to child, PID of child to parent */
    /* The BSD server handles this by returning the child's PID in reply.result */
    /* For a proper implementation, we'd need the kernel to differentiate.
     * For now, return 0 (child) on success since fork succeeded. */
    (void)kr;
    return 0; /* Simplified: always returns as if we're the child */
}

int posix_execve(const char *path, char *const argv[], char *const envp[])
{
    if (!path) {
        return -POSIX_EINVAL;
    }

    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    /* Copy path into request body */
    mach_msg_size_t path_len = strlen(path);
    if (path_len >= BSD_MAX_PATH_LEN) {
        return -POSIX_ENAMETOOLONG;
    }
    memcpy(((bsd_execve_request_t *)&request)->path, path, path_len + 1);

    /* Count argv/envp */
    int argc = 0, envc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }
    if (envp) {
        while (envp[envc]) envc++;
    }

    ((bsd_execve_request_t *)&request)->argc = argc;
    ((bsd_execve_request_t *)&request)->envc = envc;

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_EXECVE,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -POSIX_ENOEXEC;
    }

    /* execve doesn't return on success */
    return -1; /* Failed */
}

int posix_waitpid(int pid, int *status, int options)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    ((bsd_waitpid_request_t *)&request)->pid = pid;
    ((bsd_waitpid_request_t *)&request)->options = options;

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_WAITPID,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    if (status) {
        *status = ((bsd_waitpid_reply_t *)&reply)->status;
    }

    /* Return child PID on success */
    return ((bsd_waitpid_reply_t *)&reply)->result;
}

void posix_exit(int status)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    ((bsd_exit_request_t *)&request)->status = status;

    /* Send exit message */
    kern_return_t kr = mach_msg((mach_msg_header_t *)&request,
                                MACH_SEND_MSG,
                                sizeof(request),
                                0,
                                MACH_PORT_NULL,
                                MACH_MSG_TIMEOUT_NONE,
                                MACH_PORT_NULL);

    (void)kr;
    (void)reply;

    /* Don't return - process should terminate */
    if (g_bsd_server_port != MACH_PORT_NULL) {
        task_suspend(mach_task_self());
        task_terminate(mach_task_self());
    }
    exit(status);
}

int posix_kill(int pid, int sig)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    ((bsd_kill_request_t *)&request)->pid = pid;
    ((bsd_kill_request_t *)&request)->sig = sig;

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_KILL,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    return 0;
}

int posix_getpid(void)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_GETPID,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    return ((bsd_getpid_reply_t *)&reply)->result;
}

int posix_getppid(void)
{
    /* getppid requires querying the BSD server for our parent's PID */
    /* For now, return -1 as this needs a different message type */
    return -1;
}

/* ============================================================================
 * File I/O
 * ============================================================================ */

int posix_open(const char *path, int flags, ...)
{
    if (!path) {
        return -POSIX_EINVAL;
    }

    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    /* Copy path */
    mach_msg_size_t path_len = strlen(path);
    if (path_len >= BSD_MAX_PATH_LEN) {
        return -POSIX_ENAMETOOLONG;
    }
    memcpy(((bsd_open_request_t *)&request)->path, path, path_len + 1);

    /* Set flags and mode */
    ((bsd_open_request_t *)&request)->flags = flags;

    va_list args;
    va_start(args, flags);
    int mode = va_arg(args, int);
    va_end(args);
    ((bsd_open_request_t *)&request)->mode = mode;

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_OPEN,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    return ((bsd_open_reply_t *)&reply)->result;
}

int posix_close(int fd)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    ((bsd_close_request_t *)&request)->fd = fd;

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_CLOSE,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    return 0;
}

int posix_read(int fd, void *buf, unsigned int count)
{
    bsd_request_t request;
    uint8_t reply_buf[BSD_INLINE_REPLY_MAX];

    memset(&request, 0, sizeof(request));
    memset(reply_buf, 0, sizeof(reply_buf));

    ((bsd_read_request_t *)&request)->fd = fd;
    ((bsd_read_request_t *)&request)->count = (mach_msg_size_t)count;

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_READ,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)reply_buf,
                                               BSD_INLINE_REPLY_MAX);

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    bsd_read_reply_t *rrep = (bsd_read_reply_t *)reply_buf;
    int bytes_read = rrep->result;

    if (bytes_read > 0 && buf) {
        memcpy(buf, rrep->data, (mach_msg_size_t)bytes_read);
    }

    return bytes_read;
}

int posix_write(int fd, const void *buf, unsigned int count)
{
    /* For small writes (< 64 bytes), send inline in the request body */
    if (count <= 64) {
        bsd_request_t request;
        bsd_reply_t reply;

        mach_msg_size_t total_size = sizeof(bsd_write_request_t) + count;

        memset(&request, 0, sizeof(request));
        memset(&reply, 0, sizeof(reply));

        ((bsd_write_request_t *)&request)->fd = fd;
        ((bsd_write_request_t *)&request)->count = (mach_msg_size_t)count;

        /* Copy data after the request header */
        if (buf && count > 0) {
            memcpy(((uint8_t *)&request) + sizeof(bsd_write_request_t), buf, count);
        }

        kern_return_t kr = bsd_client_send_receive(BSD_MSG_WRITE,
                                                   (mach_msg_header_t *)&request,
                                                   total_size,
                                                   (mach_msg_header_t *)&reply,
                                                   sizeof(reply));

        if (kr != KERN_SUCCESS) {
            return -1;
        }

        return ((bsd_write_reply_t *)&reply)->result;
    }

    /* For larger writes, we'd use OOL descriptors.
     * This is a simplified implementation that returns ENOSYS for now. */
    (void)buf;
    return -POSIX_ENOSYS;
}

int posix_pipe(int pipefd[2])
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_PIPE,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    pipefd[0] = ((bsd_pipe_reply_t *)&reply)->read_fd;
    pipefd[1] = ((bsd_pipe_reply_t *)&reply)->write_fd;

    return 0;
}

int posix_fcntl(int fd, int cmd, ...)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    ((bsd_fcntl_request_t *)&request)->fd = fd;
    ((bsd_fcntl_request_t *)&request)->cmd = cmd;

    va_list args;
    va_start(args, cmd);
    long arg = va_arg(args, long);
    va_end(args);
    ((bsd_fcntl_request_t *)&request)->arg = arg;

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_FCNTL,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return -1;
    }

    return ((bsd_fcntl_reply_t *)&reply)->result;
}

/* ============================================================================
 * Signal Handling
 * ============================================================================ */

void (*posix_signal(int sig, void (*handler)(int)))(int)
{
    bsd_request_t request;
    bsd_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    ((bsd_signal_request_t *)&request)->sig = sig;
    ((bsd_signal_request_t *)&request)->handler_addr = (uint64_t)(uintptr_t)(size_t)handler;

    kern_return_t kr = bsd_client_send_receive(BSD_MSG_SIGNAL,
                                               (mach_msg_header_t *)&request,
                                               sizeof(request),
                                               (mach_msg_header_t *)&reply,
                                               sizeof(reply));

    if (kr != KERN_SUCCESS) {
        return SIG_ERR;
    }

    return (void (*)(int))(uintptr_t)(size_t)((bsd_signal_reply_t *)&reply)->result;
}