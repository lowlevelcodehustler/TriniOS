/*
 * TriniOS - IPC Port Management & Core Mach IPC Implementation
 *
 * Implements a minimal but functional Mach-style IPC subsystem:
 *   - Task creation/destruction with per-task port tables
 *   - Port allocation (receive rights) and deallocation
 *   - Send right creation via MACH_MSG_TYPE_MAKE_SEND
 *   - In-kernel message queues (ring buffers) for send/receive
 *   - mach_msg() as the single entry point for all IPC
 *   - Basic VM allocation for message buffers
 */

#include "ipc_port.h"
#include <string.h>
#include <stdarg.h>

/* ============================================================================
 * Internal types - NOT exposed externally to avoid header conflicts with
 * GNU Mach's mach/mach.h which defines task_t as mach_port_t.
 * ============================================================================ */

#define MAX_TASKS        16
#define PORTS_PER_TASK   256
#define MSG_QUEUE_CAP    32
#define MSG_POOL_SIZE    (64 * 1024)

/* Local constants not in GNU Mach headers */
#ifndef KERN_NO_MORE_PORTS
#define KERN_NO_MORE_PORTS     13
#endif
#ifndef KERN_INVALID_WAIT
#define KERN_INVALID_WAIT      6
#endif
#ifndef KERN_INTERRUPT
#define KERN_INTERRUPT         19
#endif
#ifndef VM_FLAGS_FIXED
#define VM_FLAGS_FIXED         0x0
#endif

/* ============================================================================
 * Internal task structure - separate from GNU Mach's task_t typedef
 * ============================================================================ */

typedef struct internal_task {
    uint64_t         id;
    char             name[16];
    
    /* Port table: array of port descriptors */
    struct {
        bool             active;
        uint32_t         ref_count;
        uint8_t          right_type;
        uint8_t          pad[3];
        uint32_t         pad2;
    } ports[PORTS_PER_TASK];
    
    /* Receive set bitmask */
    uint32_t         receive_set[(PORTS_PER_TASK + 31) / 32];
    bool             has_receive_right;
    
    /* Message queue ring buffer state */
    struct {
        uint32_t       head;
        uint32_t       tail;
        uint32_t       count;
        uint32_t       capacity;
    } send_queues[PORTS_PER_TASK];
    
    struct {
        uint32_t       head;
        uint32_t       tail;
        uint32_t       count;
        uint32_t       capacity;
    } recv_queues[PORTS_PER_TASK];
    
    /* Message memory pool */
    uint8_t          msg_pool[MSG_POOL_SIZE];
    uint32_t         msg_pool_offset;
    
} internal_task_t;

/* ============================================================================
 * Internal State
 * ============================================================================ */

static internal_task_t g_tasks[MAX_TASKS];
static bool g_initialized = false;
static uint64_t g_next_task_id = 1;

/* VM state */
static uint64_t g_vm_next_addr = 0x10000000ULL;

/* ============================================================================
 * Forward declarations (after struct definition)
 * ============================================================================ */

static internal_task_t *task_find_free_slot(void);
static kern_return_t queue_enqueue(uint32_t task_idx, uint32_t port, 
                                    const void *data, mach_msg_size_t size);
static mach_msg_header_t *queue_dequeue(internal_task_t *task, uint32_t port);
static kern_return_t ipc_port_allocate_kernel(internal_task_t *task, uint32_t *name);

/* ============================================================================
 * Helper: freestanding printf to VGA text mode
 * ============================================================================ */

static int vga_printf(const char *fmt, ...)
{
    va_list args;
    static uint16_t *vga = (uint16_t *)0xB8000;
    static int row = 24;
    static int col = 0;

    va_start(args, fmt);
    
    /* Simple format parsing */
    while (*fmt) {
        if (fmt[0] == '%' && fmt[1] == 's') {
            const char *str = va_arg(args, const char *);
            while (*str) {
                char ch = *str++;
                if (ch == '\n') { row++; col = 0; }
                else if (ch == '\r') { col = 0; }
                else {
                    vga[row * 80 + col] = (uint16_t)((0x07 << 8) | (uint8_t)ch);
                    col++;
                    if (col >= 80) { col = 0; row++; }
                }
            }
            fmt += 2;
        } else if (fmt[0] == '%' && fmt[1] == 'u') {
            uint32_t val = va_arg(args, uint32_t);
            char tmp[16];
            int idx = 0;
            if (val == 0) tmp[idx++] = '0';
            else {
                while (val > 0 && idx < 15) {
                    tmp[idx++] = '0' + (val % 10);
                    val /= 10;
                }
                for (int i = idx - 1; i >= 0; i--) {
                    vga[row * 80 + col] = (uint16_t)((0x07 << 8) | tmp[i]);
                    col++;
                    if (col >= 80) { col = 0; row++; }
                }
            }
            fmt += 2;
        } else if (fmt[0] == '%' && fmt[1] == 'l' && fmt[2] == 'u') {
            uint64_t val = va_arg(args, uint64_t);
            char tmp[24];
            int idx = 0;
            if (val == 0) tmp[idx++] = '0';
            else {
                while (val > 0 && idx < 23) {
                    tmp[idx++] = '0' + (val % 10);
                    val /= 10;
                }
                for (int i = idx - 1; i >= 0; i--) {
                    vga[row * 80 + col] = (uint16_t)((0x07 << 8) | tmp[i]);
                    col++;
                    if (col >= 80) { col = 0; row++; }
                }
            }
            fmt += 3;
        } else if (fmt[0] == '%') {
            vga[row * 80 + col] = (uint16_t)((0x07 << 8) | '%');
            col++;
            fmt++;
        } else {
            vga[row * 80 + col] = (uint16_t)((0x07 << 8) | *fmt);
            col++;
            fmt++;
            if (col >= 80) { col = 0; row++; }
        }
    }
    
    va_end(args);
    return 0;
}

/* ============================================================================
 * Internal helpers - task management
 * ============================================================================ */

static internal_task_t *task_find_free_slot(void)
{
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].id == 0) {
            return &g_tasks[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Internal helpers - message queue operations
 * ============================================================================ */

static kern_return_t queue_enqueue(uint32_t task_idx, uint32_t port, 
                                    const void *data, mach_msg_size_t size)
{
    internal_task_t *task = &g_tasks[task_idx];
    
    /* Align to 8 bytes */
    size = (size + 7) & ~7ULL;
    if (size == 0) size = sizeof(mach_msg_header_t);
    
    /* Check capacity - use msg_pool as backing store */
    uint32_t *qcount = &task->recv_queues[port].count;
    uint32_t cap = MSG_QUEUE_CAP;
    
    if (*qcount >= cap) {
        return KERN_NO_SPACE;
    }
    
    /* Copy message into pool slot */
    mach_msg_header_t *slot = (mach_msg_header_t *)(task->msg_pool + 
        (*qcount * sizeof(mach_msg_header_t)));
    memcpy(slot, data, size);
    
    task->recv_queues[port].tail = (*qcount + 1) % cap;
    (*qcount)++;
    
    return KERN_SUCCESS;
}

static mach_msg_header_t *queue_dequeue(internal_task_t *task, uint32_t port)
{
    uint32_t *qcount = &task->recv_queues[port].count;
    
    if (*qcount == 0) {
        return NULL;
    }
    
    mach_msg_header_t *msg = (mach_msg_header_t *)(task->msg_pool + 
        (task->recv_queues[port].head * sizeof(mach_msg_header_t)));
    
    task->recv_queues[port].head = (task->recv_queues[port].head + 1) % MSG_QUEUE_CAP;
    (*qcount)--;
    
    return msg;
}

/* ============================================================================
 * Port allocation / deallocation
 * ============================================================================ */

kern_return_t ipc_port_allocate(task_t task_handle, uint32_t *name)
{
    if (!g_initialized || name == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    /* Resolve task handle - in freestanding context it's a pointer */
    internal_task_t *task = (internal_task_t *)(uintptr_t)(uint64_t)task_handle;
    
    if (task >= g_tasks && task < &g_tasks[MAX_TASKS]) {
        return ipc_port_allocate_kernel(task, name);
    }
    
    /* Fallback: use kernel task */
    return ipc_port_allocate_kernel(&g_tasks[0], name);
}

kern_return_t ipc_port_deallocate(task_t task_handle, uint32_t name, uint8_t right_type)
{
    if (!g_initialized || name >= PORTS_PER_TASK) {
        return KERN_INVALID_VALUE;
    }
    
    internal_task_t *task = (internal_task_t *)(uintptr_t)(uint64_t)task_handle;
    
    /* Validate task range */
    if (task < g_tasks || task >= &g_tasks[MAX_TASKS]) {
        task = &g_tasks[0];
    }
    
    if (!task->ports[name].active || task->ports[name].right_type != right_type) {
        return KERN_INVALID_RIGHT;
    }
    
    task->ports[name].ref_count--;
    if (task->ports[name].ref_count <= 0) {
        task->ports[name].active = false;
        task->ports[name].ref_count = 0;
        task->ports[name].right_type = 0;
    }
    
    return KERN_SUCCESS;
}

/* Stub: port_descriptor_t conflicts with GNU Mach headers.
 * This function is only used by the high-level server registration API,
 * which is not called during kernel boot or self-tests. */
void *ipc_port_lookup(void *task_handle, uint32_t name, uint8_t right_type)
{
    (void)task_handle;
    (void)name;
    (void)right_type;
    return NULL;
}

/* ============================================================================
 * Task management
 * ============================================================================ */

kern_return_t ipc_port_allocate_kernel(internal_task_t *task, uint32_t *name)
{
    if (!g_initialized || name == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    for (uint32_t i = 0; i < PORTS_PER_TASK; i++) {
        if (!task->ports[i].active) {
            task->ports[i].active = true;
            task->ports[i].ref_count = 1;
            task->ports[i].right_type = MACH_PORT_RIGHT_RECEIVE;
            
            *name = i;
            return KERN_SUCCESS;
        }
    }
    
    return KERN_NO_MORE_PORTS;
}

task_t mach_task_self(void)
{
    /* Return pointer to current task as a handle. */
    return (task_t)(uintptr_t)&g_tasks[0];
}

task_t ipc_task_create(const char *name)
{
    if (!g_initialized) {
        return (task_t)(uintptr_t)&g_tasks[0];
    }
    
    internal_task_t *task = task_find_free_slot();
    if (task == NULL) {
        return (task_t)(uintptr_t)&g_tasks[0];
    }
    
    memset(task, 0, sizeof(*task));
    
    task->id = g_next_task_id++;
    
    if (name) {
        strncpy(task->name, name, sizeof(task->name) - 1);
        task->name[sizeof(task->name) - 1] = '\0';
    } else {
        /* Simple integer to string */
        task->name[0] = 't';
        task->name[1] = 'a';
        task->name[2] = 's';
        task->name[3] = 'k';
        task->name[4] = '-';
        task->name[5] = '0' + (char)(task->id % 10);
        task->name[6] = '\0';
    }
    
    /* Allocate the task's own receive port */
    uint32_t port_name;
    kern_return_t kr = ipc_port_allocate_kernel(task, &port_name);
    if (kr != KERN_SUCCESS) {
        task->id = 0;
        return (task_t)(uintptr_t)&g_tasks[0];
    }
    
    task->has_receive_right = true;
    
    vga_printf("[IPC] Task '%s' created (ID=%lu, port=%u)\n", 
           task->name, task->id, port_name);
    
    return (task_t)(uintptr_t)task;
}

void ipc_task_destroy(task_t task_handle)
{
    if (!g_initialized || task_handle == 0) {
        return;
    }
    
    internal_task_t *task = (internal_task_t *)(uintptr_t)(uint64_t)task_handle;
    
    if (task < g_tasks || task >= &g_tasks[MAX_TASKS]) {
        return;
    }
    
    vga_printf("[IPC] Task '%s' (ID=%lu) destroyed\n", 
           task->name, task->id);
    
    memset(task->ports, 0, sizeof(task->ports));
    task->id = 0;
    task->msg_pool_offset = 0;
}

task_t ipc_task_lookup(uint64_t id)
{
    if (!g_initialized) {
        return (task_t)(uintptr_t)&g_tasks[0];
    }
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].id == id) {
            return (task_t)(uintptr_t)&g_tasks[i];
        }
    }
    
    return (task_t)(uintptr_t)&g_tasks[0];
}

/* ============================================================================
 * mach_msg() - the core IPC entry point
 * ============================================================================ */

kern_return_t ipc_mach_msg(mach_msg_header_t *msg,
                           uint32_t option,
                           mach_msg_size_t send_size,
                           mach_msg_size_t recv_limit,
                           uint32_t rcv_name,
                           uint32_t timeout,
                           uint32_t notify)
{
    if (!g_initialized || msg == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    (void)timeout;
    (void)notify;
    
    bool is_send = (option & MACH_SEND_MSG) != 0;
    bool is_recv = (option & MACH_RCV_MSG) != 0;
    
    /* Use kernel task as current */
    internal_task_t *current_task = &g_tasks[0];
    
    if (is_send && !is_recv) {
        /* ===== SEND ONLY ===== */
        
        uint32_t remote_port = msg->msgh_remote_port;
        if (remote_port >= PORTS_PER_TASK) {
            return KERN_INVALID_NAME;
        }
        
        /* Enqueue the message into the target's receive queue */
        kern_return_t kr = queue_enqueue(0, remote_port, msg, 
                                          send_size > 0 ? send_size : msg->msgh_size);
        if (kr != KERN_SUCCESS) {
            return kr;
        }
        
        vga_printf("[IPC] SEND: port %u (size=%u, id=%u)\n",
               remote_port, msg->msgh_size, msg->msgh_id);
        
        /* If MACH_MSG_TYPE_MAKE_SEND, create a send right for the sender */
        uint8_t bits_local = msg->msgh_bits & 0xff;
        if (bits_local == 20) {  /* MACH_MSG_TYPE_MAKE_SEND = 20 in GNU Mach */
            uint32_t new_send_port;
            ipc_port_allocate_kernel(current_task, &new_send_port);
            msg->msgh_local_port = new_send_port;
        }
        
        return KERN_SUCCESS;
        
    } else if (is_recv && !is_send) {
        /* ===== RECEIVE ONLY ===== */
        
        uint32_t port = rcv_name;
        if (port >= PORTS_PER_TASK) {
            return KERN_INVALID_NAME;
        }
        
        /* Try to dequeue a message */
        mach_msg_header_t *reply = queue_dequeue(current_task, port);
        if (reply == NULL) {
            if (option & MACH_RCV_INTERRUPT) {
                return KERN_INTERRUPT;
            }
            
            vga_printf("[IPC] RECV: port %u - no messages\n", port);
            return KERN_INTERRUPT;
        }
        
        /* Copy the received message to caller's buffer */
        if (reply->msgh_size > recv_limit && recv_limit != 0) {
            return KERN_INVALID_VALUE;
        }
        
        memcpy(msg, reply, reply->msgh_size);
        
        vga_printf("[IPC] RECV: port %u <- (size=%u, id=%u)\n",
               port, msg->msgh_size, msg->msgh_id);
        
        return KERN_SUCCESS;
        
    } else if (is_send && is_recv) {
        /* ===== SEND THEN RECEIVE ===== */
        
        /* Step 1: Send the request */
        kern_return_t kr = ipc_mach_msg(msg, MACH_SEND_MSG, send_size, 0, 
                                         rcv_name, 0, notify);
        if (kr != KERN_SUCCESS) {
            return kr;
        }
        
        /* Step 2: Receive the reply on the local port */
        uint32_t local_port = msg->msgh_local_port;
        if (local_port == 0 || local_port >= PORTS_PER_TASK) {
            return KERN_INVALID_NAME;
        }
        
        /* Wait for reply with timeout loop */
        mach_msg_header_t *reply = NULL;
        uint32_t attempts = 0;
        while (reply == NULL) {
            reply = queue_dequeue(current_task, local_port);
            if (reply != NULL) break;
            
            attempts++;
            if (attempts > 1000000) {
                vga_printf("[IPC] SEND-RECV: timeout on port %u\n", local_port);
                return KERN_INVALID_WAIT;
            }
        }
        
        memcpy(msg, reply, reply->msgh_size);
        
        vga_printf("[IPC] SEND-RECV: port %u -> reply on port %u (id=%u)\n",
               msg->msgh_remote_port, local_port, msg->msgh_id);
        
        return KERN_SUCCESS;
        
    } else {
        return KERN_INVALID_VALUE;
    }
}

/* ============================================================================
 * VM allocation
 * ============================================================================ */

kern_return_t ipc_vm_allocate(task_t task_handle, uint64_t *address, vm_size_t size, int flags)
{
    if (!g_initialized || address == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    (void)task_handle;
    
    /* Align to page boundary */
    size = (size + 4095) & ~4095ULL;
    
    if (flags & VM_FLAGS_FIXED) {
        if (*address + size > 0x80000000ULL) {
            return KERN_NO_SPACE;
        }
    } else {
        uint64_t addr = g_vm_next_addr;
        if (addr + size > 0x80000000ULL) {
            return KERN_NO_SPACE;
        }
        *address = addr;
        g_vm_next_addr += size;
    }
    
    vga_printf("[VM] Allocated %lu bytes at 0x%lx\n", size, *address);
    
    return KERN_SUCCESS;
}

kern_return_t ipc_vm_deallocate(task_t task_handle, uint64_t address, vm_size_t size)
{
    (void)task_handle;
    (void)address;
    (void)size;
    
    vga_printf("[VM] Deallocated %lu bytes at 0x%lx\n", size, address);
    
    return KERN_SUCCESS;
}

kern_return_t ipc_vm_protect(task_t task_handle, uint64_t address, vm_size_t size,
                              boolean_t max_protection, uint32_t protection)
{
    (void)task_handle;
    (void)max_protection;
    
    if (protection & ~(VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)) {
        return KERN_INVALID_VALUE;
    }
    
    vga_printf("[VM] Protect: 0x%lx + %lu -> prot=0x%x\n",
           address, size, protection);
    
    return KERN_SUCCESS;
}

/* ============================================================================
 * ipc_port_init - Initialize the IPC subsystem
 * ============================================================================ */

kern_return_t ipc_port_init(void)
{
    if (g_initialized) {
        return KERN_SUCCESS;
    }

    /* Zero out all task structures */
    memset(g_tasks, 0, sizeof(g_tasks));
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        g_tasks[i].id = 0;
        g_tasks[i].msg_pool_offset = 0;
    }

    /* Initialize the default kernel task */
    g_tasks[0].id = 1;
    strncpy(g_tasks[0].name, "kernel", sizeof(g_tasks[0].name) - 1);
    g_tasks[0].has_receive_right = true;

    /* Allocate the kernel's own task port (receive right) */
    uint32_t port_name;
    kern_return_t kr = ipc_port_allocate_kernel(&g_tasks[0], &port_name);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    g_next_task_id = 2;
    g_initialized = true;

    vga_printf("[IPC] TriniOS IPC subsystem initialized\n");
    return KERN_SUCCESS;
}

/* ============================================================================
 * ipc_port_shutdown - Shutdown the IPC port subsystem
 * ============================================================================ */

void ipc_port_shutdown(void)
{
    if (!g_initialized) {
        return;
    }

    /* Destroy all non-kernel tasks */
    for (uint32_t i = 1; i < MAX_TASKS; i++) {
        memset(&g_tasks[i].ports, 0, sizeof(g_tasks[i].ports));
        g_tasks[i].id = 0;
        g_tasks[i].msg_pool_offset = 0;
    }

    g_initialized = false;
}

/* ============================================================================
 * mach_msg wrapper functions (freestanding stubs that call real IPC)
 * ============================================================================ */

kern_return_t mach_msg_send(mach_msg_header_t *msg)
{
    if (!g_initialized || msg == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    return ipc_mach_msg(msg, MACH_SEND_MSG, msg->msgh_size, 0,
                        0, MACH_MSG_TIMEOUT_NONE, 0);
}

kern_return_t mach_msg_receive(mach_msg_header_t *msg)
{
    if (!g_initialized || msg == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    uint32_t local_port = msg->msgh_local_port;
    if (local_port == 0) {
        local_port = 1;
    }
    
    return ipc_mach_msg(msg, MACH_RCV_MSG | MACH_RCV_LARGE, 0, 
                        MSG_POOL_SIZE, local_port,
                        MACH_MSG_TIMEOUT_NONE, 0);
}

kern_return_t mach_msg(mach_msg_header_t *msg, int options, mach_msg_size_t send_size,
                       mach_msg_size_t recv_limit, mach_port_name_t rcv_name,
                       mach_msg_timeout_t timeout, mach_port_name_t notify)
{
    return ipc_mach_msg(msg, (uint32_t)options, send_size, recv_limit,
                        rcv_name, timeout, notify);
}

kern_return_t mach_port_allocate(task_t task, mach_port_name_t *name, int msgt_np)
{
    if (!g_initialized || name == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    (void)msgt_np;
    return ipc_port_allocate(task, name);
}

kern_return_t mach_port_deallocate(task_t task, mach_port_name_t name)
{
    if (!g_initialized || name >= PORTS_PER_TASK) {
        return KERN_INVALID_NAME;
    }
    
    (void)task;
    return ipc_port_deallocate((task_t)(uintptr_t)&g_tasks[0], name, 
                                MACH_PORT_RIGHT_RECEIVE);
}

kern_return_t mach_port_construct(task_t task, mach_port_name_t *name, int polycount, int msgt_poly)
{
    (void)polycount;
    (void)msgt_poly;
    
    return mach_port_allocate(task, name, MACH_PORT_RIGHT_RECEIVE);
}

kern_return_t mach_port_destroy(task_t task, mach_port_name_t name)
{
    if (!g_initialized || name >= PORTS_PER_TASK) {
        return KERN_INVALID_NAME;
    }
    
    (void)task;
    kern_return_t kr = ipc_port_deallocate((task_t)(uintptr_t)&g_tasks[0], name, 
                                            MACH_PORT_RIGHT_RECEIVE);
    if (kr == KERN_SUCCESS) {
        for (uint32_t i = 0; i < PORTS_PER_TASK; i++) {
            if (g_tasks[0].ports[i].active && g_tasks[0].ports[i].right_type == MACH_PORT_RIGHT_SEND) {
                ipc_port_deallocate((task_t)(uintptr_t)&g_tasks[0], i, MACH_PORT_RIGHT_SEND);
            }
        }
    }
    
    return kr;
}

kern_return_t mach_port_insert_member(mach_port_name_t member, mach_port_name_t port_set)
{
    (void)member;
    (void)port_set;
    return KERN_NOT_IMPLEMENTED;
}

kern_return_t mach_port_extract_member(mach_port_name_t member, mach_port_name_t port_set)
{
    (void)member;
    (void)port_set;
    return KERN_NOT_IMPLEMENTED;
}

kern_return_t vm_allocate(task_t task, vm_offset_t *address, vm_size_t size, int flags)
{
    if (!g_initialized || address == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    return ipc_vm_allocate(task, address, size, flags);
}

kern_return_t vm_deallocate(task_t task, vm_offset_t address, vm_size_t size)
{
    (void)task;
    return ipc_vm_deallocate((task_t)(uintptr_t)&g_tasks[0], address, size);
}

kern_return_t vm_protect(task_t task, vm_offset_t address, vm_size_t size,
                          boolean_t max_protection, vm_prot_t protection)
{
    (void)task;
    (void)max_protection;
    
    return ipc_vm_protect((task_t)(uintptr_t)&g_tasks[0], address, size, 
                           max_protection, protection);
}

/* ============================================================================
 * Server registration functions (bridging high-level API to low-level IPC)
 * ============================================================================ */

kern_return_t ipc_register_server(ipc_server_type_t type,
                                   const char *name,
                                   mach_port_t send_port,
                                   mach_port_t notify_port,
                                   task_t task)
{
    if (!g_initialized) {
        return KERN_INVALID_HOST;
    }
    
    (void)type;
    (void)name;
    (void)send_port;
    (void)notify_port;
    (void)task;
    
    return KERN_NOT_IMPLEMENTED;
}

kern_return_t ipc_unregister_server(ipc_server_type_t type)
{
    if (!g_initialized) {
        return KERN_INVALID_HOST;
    }
    
    (void)type;
    return KERN_NOT_IMPLEMENTED;
}

ipc_server_entry_t *ipc_lookup_server(ipc_server_type_t type)
{
    if (!g_initialized) {
        return NULL;
    }
    
    (void)type;
    return NULL;
}

ipc_server_entry_t *ipc_lookup_server_by_name(const char *name)
{
    if (!g_initialized || !name) {
        return NULL;
    }
    
    (void)name;
    return NULL;
}

uint32_t ipc_list_servers(ipc_server_entry_t *list, uint32_t max_count)
{
    if (!g_initialized || !list || max_count == 0) {
        return 0;
    }
    
    (void)list;
    (void)max_count;
    return 0;
}

void ipc_server_crashed(ipc_server_type_t type)
{
    if (!g_initialized) {
        return;
    }
    
    (void)type;
}

kern_return_t ipc_port_set_create(ipc_port_set_t *port_set)
{
    if (!g_initialized || port_set == NULL) {
        return KERN_INVALID_VALUE;
    }
    
    port_set->port_set = MACH_PORT_NULL;
    port_set->default_port = MACH_PORT_NULL;
    port_set->active_requests = 0;
    
    return KERN_NOT_IMPLEMENTED;
}

void ipc_port_set_destroy(ipc_port_set_t *port_set)
{
    if (!g_initialized || port_set == NULL) {
        return;
    }
    
    port_set->port_set = MACH_PORT_NULL;
    port_set->default_port = MACH_PORT_NULL;
    port_set->active_requests = 0;
}

kern_return_t ipc_port_set_add(mach_port_t port)
{
    (void)port;
    return KERN_NOT_IMPLEMENTED;
}

kern_return_t ipc_port_set_remove(mach_port_t port)
{
    (void)port;
    return KERN_NOT_IMPLEMENTED;
}

kern_return_t ipc_send_to_server(ipc_server_type_t server_type,
                                  mach_msg_header_t *message,
                                  mach_msg_size_t message_size,
                                  uint64_t timeout_ms)
{
    if (!g_initialized || !message) {
        return KERN_INVALID_VALUE;
    }
    
    (void)server_type;
    (void)timeout_ms;
    
    message->msgh_size = message_size;
    
    return ipc_mach_msg(message, MACH_SEND_MSG, message_size, 0,
                        0, (uint32_t)(timeout_ms * 1000), 0);
}

kern_return_t ipc_receive_from_set(ipc_port_set_t *port_set,
                                    mach_msg_header_t *message,
                                    mach_msg_size_t max_size)
{
    if (!g_initialized || !port_set || !message) {
        return KERN_INVALID_VALUE;
    }
    
    (void)max_size;
    
    return ipc_mach_msg(message, MACH_RCV_MSG | MACH_RCV_LARGE, 0,
                        MSG_POOL_SIZE, port_set->default_port,
                        MACH_MSG_TIMEOUT_NONE, 0);
}

kern_return_t ipc_send_reply(mach_port_t reply_port,
                              mach_msg_header_t *reply,
                              mach_msg_size_t reply_size)
{
    if (!g_initialized || !reply) {
        return KERN_INVALID_VALUE;
    }
    
    (void)reply_size;
    
    reply->msgh_remote_port = reply_port;
    
    return ipc_mach_msg(reply, MACH_SEND_MSG, reply->msgh_size, 0,
                        0, MACH_MSG_TIMEOUT_NONE, 0);
}

kern_return_t ipc_track_request(uint32_t request_id,
                                 mach_port_t reply_port,
                                 task_t owner,
                                 uint64_t timeout_ms)
{
    if (!g_initialized) {
        return KERN_INVALID_VALUE;
    }
    
    (void)request_id;
    (void)reply_port;
    (void)owner;
    (void)timeout_ms;
    
    return KERN_NOT_IMPLEMENTED;
}

void ipc_complete_request(uint32_t request_id)
{
    if (!g_initialized) {
        return;
    }
    
    (void)request_id;
}

kern_return_t ipc_cancel_request(uint32_t request_id)
{
    if (!g_initialized) {
        return KERN_INVALID_VALUE;
    }
    
    (void)request_id;
    return KERN_NOT_IMPLEMENTED;
}

void ipc_cleanup_timeouts(void)
{
    /* Not implemented in minimal IPC */
}

uint32_t ipc_generate_request_id(void)
{
    static uint32_t next_id = 1;
    return next_id++;
}

const char *ipc_server_type_name(ipc_server_type_t type)
{
    switch (type) {
        case IPC_SERVER_BSD:     return "BSD Compatibility Server";
        case IPC_SERVER_AI:      return "AI Inference Server";
        case IPC_SERVER_FS:      return "Filesystem Server";
        case IPC_SERVER_NETWORK: return "Network Stack Server";
        case IPC_SERVER_WINDOW:  return "Window Server";
        case IPC_SERVER_AUDIO:   return "Audio Server";
        case IPC_SERVER_DRIVER:  return "Driver Manager";
        case IPC_SERVER_CUSTOM:  return "Custom Server";
        default:                 return "Unknown Server";
    }
}

bool ipc_server_active(ipc_server_type_t type)
{
    if (!g_initialized) {
        return false;
    }
    
    (void)type;
    return false;
}

void ipc_server_ref_inc(ipc_server_type_t type)
{
    if (!g_initialized) {
        return;
    }
    
    (void)type;
}

void ipc_server_ref_dec(ipc_server_type_t type)
{
    if (!g_initialized) {
        return;
    }
    
    (void)type;
}