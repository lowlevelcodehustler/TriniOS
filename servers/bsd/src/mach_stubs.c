/*
 * TriniOS BSD Compatibility Server - Mach IPC Stubs (Freestanding)
 *
 * These are placeholder implementations of Mach IPC primitives needed
 * by the BSD server during Phase 1-3 development. They return error codes
 * and do nothing real until a proper Mach user-space library is linked in.
 */

#include <mach/mach.h>
#include <mach/message.h>
#include <mach/port.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * mach_task_self — returns a fake task port for freestanding builds
 * ============================================================================ */

task_t mach_task_self(void)
{
    /* Return a sentinel value indicating "self" in freestanding mode.
     * In real Mach, this is a special register-based lookup. */
    return (task_t)(uintptr_t)-1;
}

/* ============================================================================
 * mach_msg — stub that returns MACH_SEND_INVALID_DEST for all calls
 * ============================================================================ */

kern_return_t mach_msg(mach_msg_header_t *msg, int options, mach_msg_size_t send_size,
                       mach_msg_size_t recv_limit, mach_port_name_t rcv_name,
                       mach_msg_timeout_t timeout, mach_port_name_t notify)
{
    (void)msg; (void)options; (void)send_size; (void)recv_limit;
    (void)rcv_name; (void)timeout; (void)notify;
    /* Stub: no real Mach IPC available in freestanding build */
    return MACH_SEND_INVALID_DEST;
}

kern_return_t mach_msg_send(mach_msg_header_t *msg)
{
    (void)msg;
    return MACH_SEND_INVALID_DEST;
}

kern_return_t mach_msg_receive(mach_msg_header_t *msg)
{
    (void)msg;
    return MACH_RCV_INVALID_NAME;
}

/* ============================================================================
 * Port management stubs — all return KERN_FAILURE
 * ============================================================================ */

kern_return_t mach_port_allocate(task_t task, int right, mach_port_name_t *name)
{
    (void)task; (void)right; (void)name;
    return KERN_FAILURE;
}

kern_return_t mach_port_deallocate(task_t task, mach_port_name_t name)
{
    (void)task; (void)name;
    return KERN_SUCCESS; /* Best-effort: deallocate always succeeds */
}

kern_return_t mach_port_destroy(task_t task, mach_port_name_t name)
{
    (void)task; (void)name;
    return KERN_FAILURE;
}

kern_return_t mach_port_insert_right(task_t task, mach_port_name_t name,
                                     mach_port_name_t port, int rights)
{
    (void)task; (void)name; (void)port; (void)rights;
    return KERN_FAILURE;
}

kern_return_t mach_port_insert_member(mach_port_name_t member, mach_port_name_t port_set)
{
    (void)member; (void)port_set;
    return KERN_FAILURE;
}

kern_return_t mach_port_extract_member(mach_port_name_t member, mach_port_name_t port_set)
{
    (void)member; (void)port_set;
    return KERN_FAILURE;
}

/* ============================================================================
 * Task management stubs — all return KERN_FAILURE
 * ============================================================================ */

kern_return_t task_create(task_t parent_task, boolean_t inherited_set, task_t *new_task)
{
    (void)parent_task; (void)inherited_set; (void)new_task;
    return KERN_FAILURE;
}

kern_return_t task_deallocate(task_t task)
{
    (void)task;
    return KERN_SUCCESS; /* Best-effort */
}

kern_return_t task_suspend(task_t task)
{
    (void)task;
    return KERN_FAILURE;
}

kern_return_t task_terminate(task_t task)
{
    (void)task;
    return KERN_FAILURE;
}

/* ============================================================================
 * Port reference counting stubs
 * ============================================================================ */

kern_return_t mach_port_mod_refs(mach_port_name_t name, mach_port_t port, int right, boolean_t add_ref)
{
    (void)name; (void)port; (void)right; (void)add_ref;
    return KERN_FAILURE;
}

/* ============================================================================
 * VM stubs — all return KERN_FAILURE
 * ============================================================================ */

kern_return_t vm_allocate(task_t task, vm_offset_t *address, vm_size_t size, int flags)
{
    (void)task; (void)address; (void)size; (void)flags;
    return KERN_FAILURE;
}

kern_return_t vm_deallocate(task_t task, vm_offset_t address, vm_size_t size)
{
    (void)task; (void)address; (void)size;
    return KERN_FAILURE;
}

kern_return_t vm_memory_object_control(vm_offset_t object, vm_offset_t address, vm_size_t size, boolean_t control)
{
    (void)object; (void)address; (void)size; (void)control;
    return KERN_FAILURE;
}

kern_return_t vm_protect(task_t task, vm_offset_t address, vm_size_t size, boolean_t max_protection, vm_prot_t protection)
{
    (void)task; (void)address; (void)size; (void)max_protection; (void)protection;
    return KERN_FAILURE;
}

/* ============================================================================
 * Mach error string — returns a static message
 * ============================================================================ */

const char *mach_error_string(kern_return_t kr)
{
    (void)kr;
    return "Mach IPC stub: no real error info available";
}

/* ============================================================================
 * Freestanding helpers
 * ============================================================================ */

/* Simple strdup for freestanding — returns NULL since no malloc available */
char *strdup(const char *s)
{
    if (!s) return (char *)0;
    /* No malloc in freestanding — return NULL */
    return (char *)0;
}

char *strtok_r(char *s, const char *sep, char **last)
{
    (void)s; (void)sep; (void)last;
    return (char *)0; /* Stub: no tokenizing in freestanding */
}
