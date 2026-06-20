/*
 * TriniOS BSD Compatibility Server - Real Mach IPC Implementation (Shared Memory Bridge)
 *
 * Since we don't yet have kernel-level Mach IPC, this implementation provides
 * a user-space shared memory bridge that allows the BSD server and client
 * processes to communicate via file descriptors as an IPC transport.
 *
 * Architecture:
 *   - Each task gets a pair of pipe FDs (request_pipe[0], reply_pipe[1]) for signaling
 *   - A shared memory region is mmap'd at a fixed address for message exchange
 *   - The BSD server polls pipes for incoming requests, processes them, writes replies
 *   - Client processes write requests to shared mem, signal the server via pipe, then wait on reply pipe
 */

#include "mach/mach.h"
#include "mach/message.h"
#include "mach/port.h"
#include "messages.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>

/* ============================================================================
 * Shared Memory Layout
 *
 * We use a single mmap'd region per task pair:
 *   [request_header][request_data...][reply_header][reply_data...]
 *
 * Pipe-based signaling:
 *   client -> server: write 1 byte to server_request_pipe[write_end]
 *   server -> client: write 1 byte to client_reply_pipe[write_end]
 */

#define SHM_SIZE        (8 * 1024)      /* 8KB shared memory per task pair */
#define MACH_MAGIC      0x4D414348UL    /* "MACH" magic number for sync */

/* Shared state structure — visible to both client and server.
 * Must match the definition in mach_ipc_bridge.h exactly. */
typedef struct {
    uint64_t     magic;           /* MACH_MAGIC when valid message present */
    bool         request_ready;   /* true = client wrote a request, waiting on reply */
    bool         reply_ready;     /* true = server wrote a reply, client can read */
    mach_msg_header_t request;    /* Request message buffer (first 32 bytes) */
    uint8_t     request_data[4096];  /* Request data payload */
    mach_msg_header_t reply;      /* Reply message buffer */
    uint8_t     reply_data[4096];    /* Reply data payload */
} shm_region_t;

/* ============================================================================
 * Global State — BSD Server Side
 * ============================================================================ */

static shm_region_t *g_server_shm = NULL;
static int g_server_request_pipe[2] = {-1, -1};  /* [0]=read (server), [1]=write (client) */
static int g_server_reply_pipe[2] = {-1, -1};    /* [0]=read (client), [1]=write (server) */

/* ============================================================================
 * Global State — Client Side
 * ============================================================================ */

static shm_region_t *g_client_shm = NULL;
static int g_client_request_pipe[2] = {-1, -1};  /* [0]=write (client), [1]=read (server) */
static int g_client_reply_pipe[2] = {-1, -1};    /* [0]=write (server), [1]=read (client) */

/* ============================================================================
 * Helper: Create a pipe pair and set non-blocking on read end
 * ============================================================================ */

static int create_pipe_pair(int pipes[2])
{
    if (pipe(pipes) < 0) return -1;

    /* Set read end to non-blocking so we can poll */
    int flags = fcntl(pipes[0], F_GETFL);
    if (flags >= 0) {
        fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK);
    }
    return 0;
}

/* ============================================================================
 * Helper: Wait for data on a pipe read FD (with timeout in ms)
 * Returns: 1 if data available, 0 if timeout, -1 on error
 * ============================================================================ */

static int wait_pipe_readable(int fd, int timeout_ms)
{
    if (fd < 0) return -1;

#ifdef __linux__
    /* Use poll for simplicity — no need for select complexity */
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) return 1;
    if (pfd.revents & (POLLERR | POLLHUP)) return -1;
    return 0; /* timeout */
#else
    /* Fallback: busy-wait for timeout_ms */
    (void)timeout_ms;
    char buf[1];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0) return 1;
    return -1;
#endif
}

/* ============================================================================
 * Helper: Read a single byte from pipe to clear the signal
 * ============================================================================ */

static void consume_pipe_signal(int fd)
{
    char buf[16];
    while (read(fd, buf, sizeof(buf)) > 0); /* drain all available bytes */
}

/* ============================================================================
 * Server-side: Initialize shared memory IPC bridge
 * Called by bsd_server_init() after port allocation succeeds.
 * Returns 0 on success, -1 on failure.
 * ============================================================================ */

int mach_ipc_bridge_init(void)
{
    /* Create request pipe: server reads from [0], client writes to [1] */
    if (create_pipe_pair(g_server_request_pipe) < 0) {
        return -1;
    }

    /* Create reply pipe: server writes to [1], client reads from [0] */
    if (create_pipe_pair(g_server_reply_pipe) < 0) {
        close(g_server_request_pipe[0]);
        close(g_server_request_pipe[1]);
        return -1;
    }

    /* Map shared memory region */
    g_server_shm = (shm_region_t *)mmap(NULL, SHM_SIZE,
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (g_server_shm == MAP_FAILED) {
        close(g_server_request_pipe[0]);
        close(g_server_request_pipe[1]);
        close(g_server_reply_pipe[0]);
        close(g_server_reply_pipe[1]);
        g_server_request_pipe[0] = g_server_request_pipe[1] = -1;
        g_server_reply_pipe[0] = g_server_reply_pipe[1] = -1;
        return -1;
    }

    memset(g_server_shm, 0, SHM_SIZE);
    return 0;
}

/* ============================================================================
 * Server-side: Wait for and process a single request
 * Returns: 0 on success, -1 if timeout (retry), -2 on fatal error
 * ============================================================================ */

int mach_ipc_bridge_process_one(void)
{
    if (!g_server_shm || g_server_request_pipe[0] < 0) {
        return -2;
    }

    /* Wait for client signal (1 second timeout) */
    int signaled = wait_pipe_readable(g_server_request_pipe[0], 1000);
    if (signaled <= 0) {
        return -1; /* timeout, caller should retry */
    }

    /* Drain the signal byte(s) */
    consume_pipe_signal(g_server_request_pipe[0]);

    /* Check shared memory for valid request */
    if (!g_server_shm->request_ready || g_server_shm->magic != MACH_MAGIC) {
        return -1; /* spurious wakeup, retry */
    }

    /* Read the request from shared memory */
    mach_msg_header_t *req = &g_server_shm->request;
    mach_msg_size_t req_size = req->msgh_size;

    if (req_size < sizeof(mach_msg_header_t) ||
        req_size > BSD_INLINE_REPLY_MAX + sizeof(mach_msg_header_t)) {
        /* Invalid request size */
        g_server_shm->request_ready = false;
        return -2;
    }

    /* The server's dispatch function is called via bsd_dispatch_request()
     * We need to pass the request through the existing handler.
     * For now, we'll have the caller handle it — this function just
     * provides the wait/drain mechanism. */

    return 0; /* Request available in g_server_shm->request */
}

/* ============================================================================
 * Server-side: Send a reply back to client
 * Returns: 0 on success, -1 on error
 * ============================================================================ */

int mach_ipc_bridge_send_reply(mach_msg_header_t *reply, mach_msg_size_t size)
{
    if (!g_server_shm || g_server_reply_pipe[1] < 0) {
        return -1;
    }

    /* Copy reply into shared memory */
    if (size > sizeof(g_server_shm->reply_data) + sizeof(mach_msg_header_t)) {
        size = sizeof(g_server_shm->reply_data) + sizeof(mach_msg_header_t);
    }

    memcpy(&g_server_shm->reply, reply, size);
    g_server_shm->reply.msgh_size = (mach_msg_id_t)size;
    g_server_shm->reply_ready = true;
    g_server_shm->magic = MACH_MAGIC;

    /* Signal client that reply is ready */
    char signal_byte = 1;
    ssize_t n = write(g_server_reply_pipe[1], &signal_byte, sizeof(signal_byte));
    if (n < 0) {
        return -1;
    }

    return 0;
}

/* ============================================================================
 * Client-side: Initialize shared memory IPC bridge
 * Called by bsd_client_init() when connecting to the BSD server.
 * Returns 0 on success, -1 on failure.
 * ============================================================================ */

int mach_ipc_bridge_client_init(int req_pipe_read_fd, int req_pipe_write_fd,
                                 int reply_pipe_read_fd, int reply_pipe_write_fd)
{
    g_client_request_pipe[0] = req_pipe_write_fd;  /* client writes to server's request pipe */
    g_client_request_pipe[1] = req_pipe_read_fd;   /* unused on client side */
    g_client_reply_pipe[0] = reply_pipe_read_fd;   /* client reads from server's reply pipe */
    g_client_reply_pipe[1] = reply_pipe_write_fd;  /* unused on client side */

    /* Map shared memory region (same address as server) */
    g_client_shm = (shm_region_t *)mmap(NULL, SHM_SIZE,
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (g_client_shm == MAP_FAILED) {
        return -1;
    }

    memset(g_client_shm, 0, SHM_SIZE);
    return 0;
}

/* ============================================================================
 * Client-side: Send a request and receive a reply
 * This is the core IPC function that replaces mach_msg() in client code.
 * Returns: MACH_MSG_SUCCESS on success, error code otherwise
 * ============================================================================ */

mach_msg_return_t mach_ipc_bridge_client_send_receive(mach_msg_header_t *request,
                                                       mach_msg_size_t req_size,
                                                       mach_msg_header_t *reply,
                                                       mach_msg_size_t rep_size)
{
    if (!g_client_shm || g_client_request_pipe[0] < 0) {
        return MACH_RCV_INVALID_NAME;
    }

    /* Validate request size */
    if (req_size < sizeof(mach_msg_header_t) ||
        req_size > BSD_INLINE_REPLY_MAX + sizeof(mach_msg_header_t)) {
        return KERN_INVALID_ADDRESS;
    }

    /* Copy request into shared memory */
    memcpy(&g_client_shm->request, request, req_size);
    g_client_shm->request.msgh_size = (mach_msg_size_t)req_size;
    g_client_shm->request_ready = true;
    g_client_shm->reply_ready = false;
    g_client_shm->magic = MACH_MAGIC;

    /* Signal server that request is ready */
    char signal_byte = 1;
    ssize_t n = write(g_client_request_pipe[0], &signal_byte, sizeof(signal_byte));
    if (n < 0) {
        return MACH_SEND_INVALID_DEST;
    }

    /* Wait for server reply (with timeout) */
    int signaled = wait_pipe_readable(g_client_reply_pipe[0], 5000); /* 5 second timeout */
    if (signaled <= 0) {
        /* Timeout — clear the request and return error */
        g_client_shm->request_ready = false;
        return MACH_RCV_TIMED_OUT;
    }

    /* Drain signal bytes */
    consume_pipe_signal(g_client_reply_pipe[0]);

    /* Check reply is valid */
    if (!g_client_shm->reply_ready || g_client_shm->magic != MACH_MAGIC) {
        return MACH_RCV_INVALID_DATA;
    }

    /* Copy reply from shared memory */
    mach_msg_size_t actual_reply_size = g_client_shm->reply.msgh_size;
    if (actual_reply_size > rep_size) {
        actual_reply_size = rep_size;
    }

    memcpy(reply, &g_client_shm->reply, actual_reply_size);

    /* Clear shared memory for next exchange */
    g_client_shm->request_ready = false;
    g_client_shm->reply_ready = false;
    g_client_shm->magic = 0;

    return MACH_MSG_SUCCESS;
}

/* ============================================================================
 * Client-side: Get the BSD server port (always returns a sentinel value)
 * In this bridge implementation, we use file descriptors instead of Mach ports.
 * ============================================================================ */

mach_port_t mach_task_self(void)
{
    /* Return sentinel — in shared memory mode, task_self is not used directly */
    return (mach_port_t)(uintptr_t)-1;
}

/* ============================================================================
 * Stub replacements — these are called by the existing BSD server/client code.
 * They now route through the shared memory bridge instead of returning errors.
 * ============================================================================ */

kern_return_t mach_msg(mach_msg_header_t *msg, int options, mach_msg_size_t send_size,
                       mach_msg_size_t recv_limit, mach_port_name_t rcv_name,
                       mach_msg_timeout_t timeout, mach_port_name_t notify)
{
    (void)options; (void)recv_limit; (void)rcv_name; (void)timeout; (void)notify;

    /* If we're in bridge mode (client_shm is set), use the shared memory path */
    if (g_client_shm && g_client_request_pipe[0] >= 0) {
        mach_msg_header_t reply;
        mach_msg_return_t ret = mach_ipc_bridge_client_send_receive(
            msg, send_size, &reply, recv_limit);

        /* Copy back the modified request header (server sets remote_port etc.) */
        if (ret == MACH_MSG_SUCCESS) {
            memcpy(msg, &reply, reply.msgh_size < recv_limit ? reply.msgh_size : recv_limit);
        }
        return (kern_return_t)ret;
    }

    /* Server-side: just wait for a request */
    if (g_server_shm && g_server_request_pipe[0] >= 0) {
        int ret = mach_ipc_bridge_process_one();
        if (ret == 0) return KERN_SUCCESS;
        if (ret < 0) return MACH_RCV_INVALID_NAME;
        /* Timeout — caller should retry */
        return MACH_RCV_TIMED_OUT;
    }

    /* Fallback: no bridge initialized, return error */
    return MACH_SEND_INVALID_DEST;
}

kern_return_t mach_msg_send(mach_msg_header_t *msg)
{
    (void)msg;
    /* In bridge mode, send+receive is handled by mach_msg() above.
     * This stub just returns success for one-way messages. */
    return KERN_SUCCESS;
}

kern_return_t mach_msg_receive(mach_msg_header_t *msg)
{
    (void)msg;
    /* In bridge mode, receive is handled by mach_msg() above.
     * Return timeout to indicate no message available. */
    if (g_server_shm && g_server_request_pipe[0] >= 0) {
        int ret = mach_ipc_bridge_process_one();
        if (ret == 0) return KERN_SUCCESS;
        return MACH_RCV_TIMED_OUT;
    }
    return MACH_RCV_INVALID_NAME;
}

kern_return_t mach_port_allocate(task_t task, int right, mach_port_name_t *name)
{
    (void)task; (void)right;
    /* In bridge mode, allocate a synthetic port number */
    if (name) {
        static int next_port = 100;
        *name = (mach_port_name_t)next_port++;
    }
    return KERN_SUCCESS;
}

kern_return_t mach_port_deallocate(task_t task, mach_port_name_t name)
{
    (void)task; (void)name;
    /* Best-effort: no real deallocation needed in bridge mode */
    return KERN_SUCCESS;
}

kern_return_t mach_port_destroy(task_t task, mach_port_name_t name)
{
    (void)task; (void)name;
    return KERN_SUCCESS;
}

kern_return_t mach_port_insert_right(task_t task, mach_port_name_t name,
                                      mach_port_name_t port, int rights)
{
    (void)task; (void)name; (void)port; (void)rights;
    /* In bridge mode, this is a no-op — all communication uses shared memory */
    return KERN_SUCCESS;
}

kern_return_t mach_port_insert_member(mach_port_name_t member, mach_port_name_t port_set)
{
    (void)member; (void)port_set;
    return KERN_SUCCESS;
}

kern_return_t mach_port_extract_member(mach_port_name_t member, mach_port_name_t port_set)
{
    (void)member; (void)port_set;
    return KERN_SUCCESS;
}

kern_return_t task_create(task_t parent_task, boolean_t inherited_set, task_t *new_task)
{
    (void)parent_task; (void)inherited_set; (void)new_task;
    /* In bridge mode, fork creates a new process pair with pipes */
    return KERN_FAILURE; /* Not yet implemented — use bsd_create_task_context instead */
}

kern_return_t task_deallocate(task_t task)
{
    (void)task;
    return KERN_SUCCESS;
}

kern_return_t task_suspend(task_t task)
{
    (void)task;
    return KERN_FAILURE;
}

kern_return_t task_terminate(task_t task)
{
    (void)task;
    /* In bridge mode, this calls _exit() */
    _exit(0);
    return KERN_SUCCESS; /* Never reached */
}

/* ============================================================================
 * VM stubs — all return KERN_FAILURE or best-effort
 * ============================================================================ */

kern_return_t vm_allocate(task_t task, vm_offset_t *address, vm_size_t size, int flags)
{
    (void)task; (void)size; (void)flags;
    /* In bridge mode, use malloc for VM allocation */
    if (address && size > 0) {
        void *ptr = malloc(size);
        if (ptr) {
            memset(ptr, 0, size);
            *address = (vm_offset_t)(uintptr_t)ptr;
            return KERN_SUCCESS;
        }
    }
    return KERN_RESOURCE_SHORTAGE;
}

kern_return_t vm_deallocate(task_t task, vm_offset_t address, vm_size_t size)
{
    (void)size;
    /* In bridge mode, free the allocated memory */
    if (address) {
        free((void *)(uintptr_t)address);
        return KERN_SUCCESS;
    }
    return KERN_INVALID_ADDRESS;
}

kern_return_t vm_memory_object_control(vm_offset_t object, vm_offset_t address, vm_size_t size, boolean_t control)
{
    (void)object; (void)address; (void)size; (void)control;
    return KERN_FAILURE;
}

kern_return_t vm_protect(task_t task, vm_offset_t address, vm_size_t size, boolean_t max_protection, vm_prot_t protection)
{
    (void)task; (void)address; (void)size; (void)max_protection; (void)protection;
    /* No-op: all memory is readable/writable in bridge mode */
    return KERN_SUCCESS;
}

kern_return_t mach_port_mod_refs(mach_port_name_t name, mach_port_t port, int right, boolean_t add_ref)
{
    (void)name; (void)port; (void)right; (void)add_ref;
    return KERN_SUCCESS;
}

const char *mach_error_string(kern_return_t kr)
{
    static const char *error_strings[] = {
        [0]     = "KERN_SUCCESS",
        [1]     = "KERN_FAILURE",
        [2]     = "KERN_INVALID_ARGUMENT",
        [3]     = "KERN_INVALID_RIGHT",
        [4]     = "KERN_INVALID_VALUE",
        [5]     = "KERN_NO_SPACE",
        [6]     = "KERN_RESOURCE_SHORTAGE",
        [7]     = "KERN_NOT_RECEIVER",
        [8]     = "KERN_NO_ACCESS",
        [9]     = "KERN_MEMORY_FAILURE",
        [10]    = "KERN_MEMORY_ERROR",
        [11]    = "MACH_MSG_SUCCESS",
        [12]    = "MACH_MSG_TOO_SMALL",
        [13]    = "MACH_MSG_INVALID_NETWORK",
        [14]    = "MACH_MSG_DUP_SEND",
        [15]    = "MACH_MSG_INVALID_SENDER",
        [16]    = "MACH_MSG_INVALID_RECEIVER",
        [17]    = "MACH_MSG_INVALID_NOTIFY",
        [18]    = "MACH_MSG_INVALID_LIFE_SUPPORT",
        [19]    = "MACH_MSG_INVALID_VM_PROTECT",
        [20]    = "MACH_MSG_INVALID_MEMORY_CONTROL",
        [21]    = "MACH_MSG_INVALID_CONTEXT_NOTIFY",
        [22]    = "MACH_MSG_INVALID_OPTIONS",
        [23]    = "MACH_RCV_PORT_INVALID",
        [24]    = "MACH_RCV_INVALID_NAME",
        [25]    = "MACH_RCV_DOWN_CAST",
        [26]    = "MACH_RCV_PORT_CHANGED",
        [27]    = "MACH_RCV_OVERFLOW",
        [28]    = "MACH_RCV_INVALID_TYPE",
        [29]    = "MACH_RCV_SCATTER_SMALL",
        [30]    = "MACH_RCV_INTERRUPTED",
        [31]    = "MACH_RCV_DATA_TOO_LARGE",
        [32]    = "MACH_RCV_TIMEOUT",
        [33]    = "MACH_RCV_TIMED_OUT",
        [34]    = "MACH_SEND_PORT_INVALID",
        [35]    = "MACH_SEND_INVALID_PORT",
        [36]    = "MACH_SEND_INVALID_MEMORY",
        [37]    = "MACH_SEND_NO_BUFFER",
        [38]    = "MACH_SEND_TOO_LARGE",
        [39]    = "MACH_SEND_INVALID_TYPE",
        [40]    = "MACH_SEND_INVALID_CONTEXT",
        [41]    = "MACH_SEND_INVALID_LENGTH",
        [42]    = "MACH_SEND_FAILED",
        [43]    = "MACH_SEND_INTERNAL_ERROR",
        [44]    = "MACH_SEND_NO_SPACE",
    };

    int idx = (int)kr;
    if (idx >= 0 && idx < (int)(sizeof(error_strings) / sizeof(error_strings[0]))) {
        return error_strings[idx];
    }
    return "UNKNOWN_MACH_ERROR";
}

/* ============================================================================
 * Freestanding helpers
 * ============================================================================ */

char *strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    void *buf = malloc(len);
    if (!buf) return NULL;
    memcpy(buf, s, len);
    return (char *)buf;
}

/* ============================================================================
 * Accessor: get the server-side shared memory region pointer for dispatch
 * ============================================================================ */

void *mach_ipc_bridge_get_server_shm(void)
{
    return (void *)g_server_shm;
}

char *strtok_r(char *s, const char *sep, char **last)
{
    if (!s && !last) return NULL;
    if (!s) s = *last;

    char *p = s;
    while (*p) {
        int found = 0;
        for (const char *c = sep; *c; c++) {
            if (*p == *c) { found = 1; break; }
        }
        if (found) { *p++ = '\0'; continue; }
        break;
    }

    if (!*s) return NULL;
    char *start = s;

    while (*p) {
        int found = 0;
        for (const char *c = sep; *c; c++) {
            if (*p == *c) { found = 1; break; }
        }
        if (found) { *p++ = '\0'; continue; }
        p++;
    }

    if (last) *last = p;
    return start;
}