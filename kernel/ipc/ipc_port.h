/*
 * TriniOS - IPC Port Management
 *
 * Manages Mach ports for server registration, discovery, and communication.
 * Built on top of GNU Mach's IPC primitives.
 */

#ifndef IPC_PORT_H
#define IPC_PORT_H

#include <mach/mach.h>
#include <mach/port.h>
#include <mach/message.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define IPC_MAX_SERVERS           64
#define IPC_PORT_NAME_MAX_LEN     64
#define IPC_REPLY_BUFFER_SIZE     (8 * 1024)  /* 8KB reply buffer */

/* ============================================================================
 * Server Types
 * ============================================================================ */

typedef enum {
    IPC_SERVER_BSD,           /* BSD compatibility layer */
    IPC_SERVER_AI,            /* AI Inference Server */
    IPC_SERVER_FS,            /* Filesystem Server */
    IPC_SERVER_NETWORK,       /* Network Stack Server */
    IPC_SERVER_WINDOW,        /* Window Server */
    IPC_SERVER_AUDIO,         /* Audio Server */
    IPC_SERVER_DRIVER,        /* Driver Manager */
    IPC_SERVER_CUSTOM,        /* Custom/user-defined server */
    IPC_SERVER_COUNT          /* Total number of built-in server types */
} ipc_server_type_t;

/* ============================================================================
 * Server Registration Entry
 * ============================================================================ */

typedef struct {
    ipc_server_type_t     type;
    char                  name[IPC_PORT_NAME_MAX_LEN];
    mach_port_t           send_port;     /* Port to send requests to server */
    mach_port_t           notify_port;   /* Notification port (for crashes) */
    task_t                task;          /* Server's task */
    bool                  active;        /* Server is running */
    bool                  crashed;       /* Server has crashed */
    uint32_t              ref_count;     /* Number of clients using this server */
} ipc_server_entry_t;

/* ============================================================================
 * IPC Port Set (for multiplexing server replies)
 * ============================================================================ */

typedef struct {
    mach_port_t           port_set;      /* Mach port set for receiving */
    mach_port_t           default_port;  /* Default reply port */
    uint32_t              active_requests;
} ipc_port_set_t;

/* ============================================================================
 * Pending Request Tracking
 * ============================================================================ */

#define IPC_MAX_PENDING_REQUESTS 256

typedef struct {
    uint32_t              request_id;
    mach_port_t           reply_port;
    task_t                owner_task;
    uint64_t              timeout;       /* Nanosecond timeout */
    bool                  completed;
} ipc_pending_request_t;

/* ============================================================================
 * IPC Message Headers (TriniOS-specific extensions)
 * ============================================================================ */

/* Standard request header with task identification */
typedef struct {
    mach_msg_header_t     header;
    mach_msg_size_t       msgh_descriptor_count;
    task_t                sender_task;   /* Identifying the calling task */
    uint32_t              request_id;    /* Unique request identifier */
} trini_ipc_request_header_t;

/* Standard reply header */
typedef struct {
    mach_msg_header_t     header;
    mach_msg_size_t       msgh_descriptor_count;
    kern_return_t         result;        /* Operation result */
    uint32_t              request_id;    /* Matches original request */
} trini_ipc_reply_header_t;

/* ============================================================================
 * Port Management Functions
 * ============================================================================ */

/* Initialize the IPC port subsystem */
kern_return_t ipc_port_init(void);

/* Shutdown the IPC port subsystem */
void ipc_port_shutdown(void);

/* ============================================================================
 * Server Registration
 * ============================================================================ */

/* Register a new server */
kern_return_t ipc_register_server(ipc_server_type_t type,
                                  const char *name,
                                  mach_port_t send_port,
                                  mach_port_t notify_port,
                                  task_t task);

/* Unregister a server */
kern_return_t ipc_unregister_server(ipc_server_type_t type);

/* Lookup server by type */
ipc_server_entry_t *ipc_lookup_server(ipc_server_type_t type);

/* Lookup server by name */
ipc_server_entry_t *ipc_lookup_server_by_name(const char *name);

/* Get all registered servers */
uint32_t ipc_list_servers(ipc_server_entry_t *list, uint32_t max_count);

/* Mark a server as crashed and notify dependents */
void ipc_server_crashed(ipc_server_type_t type);

/* ============================================================================
 * Port Set Management
 * ============================================================================ */

/* Create a port set for receiving messages */
kern_return_t ipc_port_set_create(ipc_port_set_t *port_set);

/* Destroy a port set */
void ipc_port_set_destroy(ipc_port_set_t *port_set);

/* Add a port to the set */
kern_return_t ipc_port_set_add(mach_port_t port);

/* Remove a port from the set */
kern_return_t ipc_port_set_remove(mach_port_t port);

/* ============================================================================
 * Message Sending/Receiving
 * ============================================================================ */

/* Send a message to a server (with timeout) */
kern_return_t ipc_send_to_server(ipc_server_type_t server_type,
                                  mach_msg_header_t *message,
                                  mach_msg_size_t message_size,
                                  uint64_t timeout_ms);

/* Receive a message on a port set */
kern_return_t ipc_receive_from_set(ipc_port_set_t *port_set,
                                    mach_msg_header_t *message,
                                    mach_msg_size_t max_size);

/* Send a reply */
kern_return_t ipc_send_reply(mach_port_t reply_port,
                              mach_msg_header_t *reply,
                              mach_msg_size_t reply_size);

/* ============================================================================
 * Request Tracking
 * ============================================================================ */

/* Track a pending request */
kern_return_t ipc_track_request(uint32_t request_id,
                                mach_port_t reply_port,
                                task_t owner,
                                uint64_t timeout_ms);

/* Mark a request as completed */
void ipc_complete_request(uint32_t request_id);

/* Cancel a pending request */
kern_return_t ipc_cancel_request(uint32_t request_id);

/* Cleanup timed-out requests */
void ipc_cleanup_timeouts(void);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/* Generate a unique request ID */
uint32_t ipc_generate_request_id(void);

/* Get server type name */
const char *ipc_server_type_name(ipc_server_type_t type);

/* Check if a server is active */
bool ipc_server_active(ipc_server_type_t type);

/* Increment/decrement server reference count */
void ipc_server_ref_inc(ipc_server_type_t type);
void ipc_server_ref_dec(ipc_server_type_t type);

#endif /* IPC_PORT_H */