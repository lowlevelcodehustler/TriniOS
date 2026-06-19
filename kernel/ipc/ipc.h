/*
 * TriniOS - Mach IPC Core Implementation
 *
 * Provides a minimal but functional Mach-style IPC subsystem using
 * in-kernel message queues and shared memory.
 *
 * Design:
 *   - Each task has a port table (array of port descriptors)
 *   - Ports have send, receive, and issue rights
 *   - Messages are passed via per-port message queues (ring buffers)
 *   - mach_msg() is the single entry point for send/receive/try
 */

#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Basic type aliases (Mach naming conventions)
 * ============================================================================ */

typedef uint32_t kern_return_t;
typedef uint32_t mach_port_name_t;
typedef uint32_t mach_msg_size_t;
typedef uint32_t mach_msg_timeout_t;
typedef int      boolean_t;
typedef uint64_t vm_offset_t;
typedef uint64_t vm_size_t;
typedef uint32_t vm_prot_t;

/* ============================================================================
 * Mach-style return codes
 * ============================================================================ */

#define KERN_SUCCESS              0
#define KERN_INVALID_ADDRESS      1
#define KERN_PROTECTION_FAILURE   2
#define KERN_NO_SPACE             3
#define KERN_INVALID_VALUE        4
#define KERN_INVALID_RIGHT        5
#define KERN_INVALID_WAIT         6
#define KERN_NOT_RECEIVER         7
#define KERN_ACCESS_DENIED        8
#define KERN_DONT_CHECK           9
#define KERN_INVALID_NAME         10
#define KERN_TOO_MANY_SYMBOLS     11
#define KERN_NO_MEMORY            12
#define KERN_NO_MORE_PORTS        13
#define KERN_NO_DEFAULT_BEHAVIOR  14
#define KERN_INVALID_CAPABILITY   15
#define KERN_RIGHT_EXISTS         16
#define KERN_RESOURCE_SHORTAGE    17
#define KERN_PORT_IS_DEAD         18
#define KERN_INTERRUPT            19
#define KERN_UNDEFINED            20
#define KERN_NOT_IMPLEMENTED      21

/* ============================================================================
 * Mach message header (full definition)
 * ============================================================================ */

#define MACH_MSGH_BITS_CONSTANT(union_or_var)   ((union_or_var).msgh_bits & 0xff)
#define MACH_MSGH_BITS_REQUEST(union_or_var)    (MACH_MSGH_BITS_CONSTANT(union_or_var) & 0x03)
#define MACH_MSGH_BITS_SERVER(union_or_var)     ((MACH_MSGH_BITS_CONSTANT(union_or_var) >> 2) & 0x03)

#define MACH_MSGH_BITS_REMOTE(a)       (((a) >> 8) & 0xff)
#define MACH_MSGH_BITS_LOCAL(a)        ((a) & 0xff)
#define MACH_MSGH_BITS_SET(remote, local)  (((remote) << 8) | (local))

#define MACH_MSG_TYPE_MAKE_SEND        0x0100
#define MACH_MSG_TYPE_MAKE_SEND_ONCE   0x0200

#define MACH_MSG_SUCCESS               0
#define MACH_RCV_PORT_DIED             1101
#define MACH_SEND_INVALID_DEST         1087

/* Message descriptor types */
#define MACH_MSG_OOL_DESCRIPTOR        4
#define MACH_MSG_PORT_DESCRIPTOR       5
#define MACH_MSG_INTEGER_DESCRIPTOR    6

typedef struct mach_msg_port_descriptor {
    uint32_t     name;      /* Mach port name */
    uint32_t     pad1;
    bool         disposition; /* true = send right, false = receive right */
    uint8_t      type;      /* MACH_MSG_PORT_DESCRIPTOR */
} mach_msg_port_descriptor_t;

typedef struct mach_msg_ool_descriptor {
    void         *address;  /* Pointer to data */
    vm_size_t     size;     /* Size of data in bytes */
    bool          deallocate;
    uint32_t      pad1;
    uint8_t       type;     /* MACH_MSG_OOL_DESCRIPTOR */
} mach_msg_ool_descriptor_t;

typedef struct mach_msg_header {
    union {
        struct {
            mach_port_name_t msgh_local_port;
            mach_port_name_t msgh_remote_port;
            mach_msg_size_t  msgh_size;
            uint8_t          msgh_bits;
            uint8_t          msgh_id;
        };
        uint32_t _msgh_padding[4];
    };
} mach_msg_header_t;

typedef struct mach_msg_trailer {
    uint32_t       type;     /* MACH_MSG_TRAILER_FORMAT */
    uint32_t       size;
    uint64_t       local_port;
    uint64_t       remote_port;
    uint8_t        task_id[16];
} mach_msg_trailer_t;

#define MACH_MSG_TRAILER_FORMAT             0
#define MACH_MSG_TRAILER_MINIMUM_SIZE       (sizeof(mach_msg_trailer_t))

/* ============================================================================
 * Port rights and types
 * ============================================================================ */

#define MACH_PORT_RIGHT_RECEIVE      0
#define MACH_PORT_RIGHT_SEND         1
#define MACH_PORT_RIGHT_SEND_ONCE    2
#define MACH_PORT_RIGHT_DESTROY      3
#define MACH_PORT_RIGHT_SET_MEMBER   4
#define MACH_PORT_MAX_RIGHTS         5

/* Port type constants */
#define MACH_PORT_TYPE_NONE          0x00
#define MACH_PORT_TYPE_RECEIVE       0x01
#define MACH_PORT_TYPE_SEND          0x02
#define MACH_PORT_TYPE_DENSE         0x80

/* Mach message options for mach_msg() */
#define MACH_MSG_SUCCESS_CODE        0
#define MACH_RCV_MSG                 0x0001
#define MACH_RCV_LARGE               0x0008
#define MACH_SEND_MSG                0x0010
#define MACH_RCV_INTERRUPT           0x0020
#define MACH_RCV_TIMEOUT             0x0040
#define MACH_RCV_VOUCHER             0x0080
#define MACH_RCV_NOTIFY              0x0100
#define MACH_RCV_LARGE_IDENTITY      0x0200

/* Timeout constants */
#define MACH_MSG_TIMEOUT_NONE        0xFFFFFFFF
#define MACH_MSG_EMPTY               KERN_INTERRUPT

/* ============================================================================
 * Configuration (pool-based to save memory)
 * ============================================================================ */

#define MAX_TASKS                16    /* Max concurrent tasks */
#define PORTS_PER_TASK           256   /* Ports per task */
#define MSG_QUEUE_CAPACITY       32    /* Messages per queue entry */
#define MSG_POOL_SIZE            (64 * 1024)  /* 64KB message pool per task */

/* ============================================================================
 * Port descriptor - represents a single Mach port in a task's port table
 * ============================================================================ */

typedef struct {
    bool             active;       /* Is this slot occupied? */
    uint32_t         ref_count;    /* Reference count for this right */
    uint8_t          right_type;   /* MACH_PORT_RIGHT_* */
    uint8_t          pad[3];
    uint32_t         pad2;
} port_descriptor_t;

/* ============================================================================
 * Message queue (ring buffer) - shared by send and recv queues
 * ============================================================================ */

typedef struct {
    mach_msg_header_t *buffer;     /* Pointer to pool storage */
    uint32_t           capacity;   /* Entries in buffer */
    uint32_t           head;       /* Read pointer */
    uint32_t           tail;       /* Write pointer */
    uint32_t           count;      /* Pending messages */
} msg_queue_t;

/* ============================================================================
 * Task - represents a Mach task with its own address space and port table
 * ============================================================================ */

typedef struct {
    uint64_t         id;           /* Task ID */
    char             name[16];     /* Human-readable name */
    
    /* Port table: array of port descriptors */
    port_descriptor_t ports[PORTS_PER_TASK];
    
    /* Receive set (for multiplexed receive) - bitmask */
    uint32_t         receive_set[(PORTS_PER_TASK + 31) / 32];
    bool             has_receive_right;                /* Own receive right for own task port */
    
    /* Message queues: one per send-right target */
    msg_queue_t      send_queues[PORTS_PER_TASK];
    
    /* Per-port receive queue: messages waiting for this port */
    msg_queue_t      recv_queues[PORTS_PER_TASK];
    
    /* Message memory pool (shared by all queues) */
    uint8_t          msg_pool[MSG_POOL_SIZE];
    uint32_t         msg_pool_offset;
    
} task_t;

/* ============================================================================
 * Port allocation / deallocation
 * ============================================================================ */

/* Allocate a new receive right for the given task.
 * Returns KERN_SUCCESS and sets *name to the allocated port name on success. */
kern_return_t ipc_port_allocate(task_t *task, uint32_t *name);

/* Deallocate a port right (send, receive, or destroy).
 * If it's a send right and no other send rights exist, deallocate the receive side too. */
kern_return_t ipc_port_deallocate(task_t *task, uint32_t name, uint8_t right_type);

/* Look up a port descriptor by name and right type.
 * Returns NULL if not found or inactive. */
port_descriptor_t *ipc_port_lookup(task_t *task, uint32_t name, uint8_t right_type);

/* ============================================================================
 * Task management
 * ============================================================================ */

/* Create a new Mach task. Returns the task pointer on success, NULL on failure. */
task_t *ipc_task_create(const char *name);

/* Destroy a task and all its ports. */
void ipc_task_destroy(task_t *task);

/* Look up a task by ID. Returns NULL if not found. */
task_t *ipc_task_lookup(uint64_t id);

/* ============================================================================
 * mach_msg() - the core IPC entry point
 *
 * This is the only function that user code calls for Mach message passing.
 * It handles both send and receive based on the options parameter.
 *
 * Parameters:
 *   msg        - Pointer to the Mach message buffer
 *   option     - Bitmask of MACH_* flags (MACH_SEND_MSG, MACH_RCV_MSG, etc.)
 *   send_size  - Size of message in bytes for sends (0 if not sending)
 *   recv_limit - Maximum receive size in bytes (0 if not receiving)
 *   rcv_name   - Receive port or port set (used when option & MACH_RCV_MSG)
 *   timeout    - Timeout in microseconds (MACH_MSG_TIMEOUT_NONE = wait forever)
 *   notify     - Notification port to signal when mach_msg completes (optional)
 *
 * Returns:
 *   MACH_MSG_SUCCESS on success
 *   KERN_* error codes on failure
 */
kern_return_t ipc_mach_msg(mach_msg_header_t *msg,
                           uint32_t option,
                           mach_msg_size_t send_size,
                           mach_msg_size_t recv_limit,
                           uint32_t rcv_name,
                           uint32_t timeout,
                           uint32_t notify);

/* ============================================================================
 * VM allocation (for message buffers and port data)
 * ============================================================================ */

#define VM_MIN_ADDRESS   0x10000000ULL
#define VM_MAX_ADDRESS   0x80000000ULL
#define VM_PAGE_SIZE     4096

kern_return_t ipc_vm_allocate(task_t *task, uint64_t *address, vm_size_t size, int flags);
kern_return_t ipc_vm_deallocate(task_t *task, uint64_t address, vm_size_t size);
kern_return_t ipc_vm_protect(task_t *task, uint64_t address, vm_size_t size,
                              boolean_t max_protection, uint32_t protection);

/* ============================================================================
 * Initialization / shutdown
 * ============================================================================ */

/* Initialize the IPC subsystem. Must be called before any other IPC functions. */
kern_return_t ipc_init(void);

/* Shutdown the IPC subsystem and free all resources. */
void ipc_shutdown(void);

#endif /* IPC_H */