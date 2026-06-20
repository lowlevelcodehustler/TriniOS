/*
 * TriniOS BSD Compatibility Server - Mach IPC Bridge Header
 *
 * Declares the shared memory IPC bridge functions that replace stub mach_* calls.
 */

#ifndef MACH_IPC_BRIDGE_H
#define MACH_IPC_BRIDGE_H

#include <mach/mach.h>
#include <mach/message.h>
#include <stdbool.h>
#include <stdint.h>

/* Shared memory region layout — exposed for server dispatch access */
#define BSD_INLINE_REPLY_MAX 4096

typedef struct {
    uint64_t     magic;           /* MACH_MAGIC when valid message present */
    bool         request_ready;   /* true = client wrote a request, waiting on reply */
    bool         reply_ready;     /* true = server wrote a reply, client can read */
    mach_msg_header_t request;    /* Request message buffer (first 32 bytes) */
    uint8_t     request_data[BSD_INLINE_REPLY_MAX];  /* Request data payload */
    mach_msg_header_t reply;      /* Reply message buffer */
    uint8_t     reply_data[BSD_INLINE_REPLY_MAX];    /* Reply data payload */
} shm_region_t;

/* Initialize the server-side shared memory IPC bridge */
int mach_ipc_bridge_init(void);

/* Wait for and process a single request from client.
 * Returns: 0 on success, -1 if timeout (retry), -2 on fatal error */
int mach_ipc_bridge_process_one(void);

/* Send a reply back to the client via shared memory + pipe signaling */
int mach_ipc_bridge_send_reply(mach_msg_header_t *reply, mach_msg_size_t size);

/* Initialize the client-side shared memory IPC bridge with pipe FDs */
int mach_ipc_bridge_client_init(int req_pipe_read_fd, int req_pipe_write_fd,
                                 int reply_pipe_read_fd, int reply_pipe_write_fd);

/* Core client send+receive via shared memory */
mach_msg_return_t mach_ipc_bridge_client_send_receive(mach_msg_header_t *request,
                                                       mach_msg_size_t req_size,
                                                       mach_msg_header_t *reply,
                                                       mach_msg_size_t rep_size);

/* Accessor: get the server-side shared memory region pointer for dispatch */
void *mach_ipc_bridge_get_server_shm(void);

#endif /* MACH_IPC_BRIDGE_H */
