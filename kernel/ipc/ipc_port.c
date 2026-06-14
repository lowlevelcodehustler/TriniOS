/*
 * TriniOS - IPC Port Management Implementation
 *
 * Server registration, discovery, and port lifecycle management.
 */

#include "ipc_port.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Internal State
 * ============================================================================ */

static ipc_server_entry_t g_servers[IPC_MAX_SERVERS];
static bool g_initialized = false;

static ipc_pending_request_t g_pending[IPC_MAX_PENDING_REQUESTS];
static uint32_t g_next_request_id = 1;

/* ============================================================================
 * ipc_port_init - Initialize the IPC port subsystem
 * ============================================================================ */

kern_return_t ipc_port_init(void)
{
    if (g_initialized) {
        return KERN_SUCCESS;
    }

    /* Clear server registry */
    memset(g_servers, 0, sizeof(g_servers));
    for (uint32_t i = 0; i < IPC_MAX_SERVERS; i++) {
        g_servers[i].active = false;
        g_servers[i].send_port = MACH_PORT_NULL;
        g_servers[i].notify_port = MACH_PORT_NULL;
        g_servers[i].task = TASK_NULL;
    }

    /* Clear pending request tracker */
    memset(g_pending, 0, sizeof(g_pending));
    for (uint32_t i = 0; i < IPC_MAX_PENDING_REQUESTS; i++) {
        g_pending[i].completed = true;
    }

    g_next_request_id = 1;
    g_initialized = true;

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

    /* Unregister all servers */
    for (uint32_t i = 0; i < IPC_MAX_SERVERS; i++) {
        if (g_servers[i].active) {
            /* Deallocate ports */
            if (g_servers[i].send_port != MACH_PORT_NULL) {
                mach_port_deallocate(mach_task_self(), g_servers[i].send_port);
            }
            if (g_servers[i].notify_port != MACH_PORT_NULL) {
                mach_port_deallocate(mach_task_self(), g_servers[i].notify_port);
            }
            g_servers[i].active = false;
        }
    }

    /* Deallocate all pending reply ports */
    for (uint32_t i = 0; i < IPC_MAX_PENDING_REQUESTS; i++) {
        if (!g_pending[i].completed && g_pending[i].reply_port != MACH_PORT_NULL) {
            mach_port_deallocate(mach_task_self(), g_pending[i].reply_port);
        }
    }

    g_initialized = false;
}

/* ============================================================================
 * Server Registration
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

    /* Check if already registered */
    ipc_server_entry_t *existing = ipc_lookup_server(type);
    if (existing && existing->active) {
        return KERN_ALREADY_INTELLIGENT;
    }

    /* Find or reuse a slot */
    ipc_server_entry_t *entry = NULL;
    if (existing) {
        entry = existing;
    } else {
        for (uint32_t i = 0; i < IPC_MAX_SERVERS; i++) {
            if (!g_servers[i].active) {
                entry = &g_servers[i];
                break;
            }
        }
    }

    if (!entry) {
        return KERN_RESOURCE_SHORTAGE;
    }

    /* Fill in the entry */
    entry->type = type;
    strncpy(entry->name, name, IPC_PORT_NAME_MAX_LEN - 1);
    entry->name[IPC_PORT_NAME_MAX_LEN - 1] = '\0';
    entry->send_port = send_port;
    entry->notify_port = notify_port;
    entry->task = task;
    entry->active = true;
    entry->crashed = false;
    entry->ref_count = 0;

    return KERN_SUCCESS;
}

kern_return_t ipc_unregister_server(ipc_server_type_t type)
{
    if (!g_initialized) {
        return KERN_INVALID_HOST;
    }

    ipc_server_entry_t *entry = ipc_lookup_server(type);
    if (!entry || !entry->active) {
        return KERN_INVALID_RIGHT;
    }

    /* Cannot unregister if clients still reference it */
    if (entry->ref_count > 0) {
        return KERN_RESOURCE_DEAD;
    }

    entry->active = false;
    entry->send_port = MACH_PORT_NULL;
    entry->notify_port = MACH_PORT_NULL;
    entry->task = TASK_NULL;

    return KERN_SUCCESS;
}

ipc_server_entry_t *ipc_lookup_server(ipc_server_type_t type)
{
    if (!g_initialized) {
        return NULL;
    }

    for (uint32_t i = 0; i < IPC_MAX_SERVERS; i++) {
        if (g_servers[i].active && g_servers[i].type == type) {
            return &g_servers[i];
        }
    }
    return NULL;
}

ipc_server_entry_t *ipc_lookup_server_by_name(const char *name)
{
    if (!g_initialized || !name) {
        return NULL;
    }

    for (uint32_t i = 0; i < IPC_MAX_SERVERS; i++) {
        if (g_servers[i].active &&
            strncmp(g_servers[i].name, name, IPC_PORT_NAME_MAX_LEN) == 0) {
            return &g_servers[i];
        }
    }
    return NULL;
}

uint32_t ipc_list_servers(ipc_server_entry_t *list, uint32_t max_count)
{
    if (!g_initialized || !list || max_count == 0) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < IPC_MAX_SERVERS && count < max_count; i++) {
        if (g_servers[i].active) {
            memcpy(&list[count], &g_servers[i], sizeof(ipc_server_entry_t));
            count++;
        }
    }
    return count;
}

void ipc_server_crashed(ipc_server_type_t type)
{
    ipc_server_entry_t *entry = ipc_lookup_server(type);
    if (!entry) {
        return;
    }

    entry->crashed = true;
    entry->active = false;

    /* In a full implementation, we would:
     * 1. Notify all tasks waiting on this server
     * 2. Fail all pending requests with KERN_SERVER_CRASHED
     * 3. Log the crash for diagnostics
     * 4. Optionally trigger server restart
     */
}

/* ============================================================================
 * Port Set Management
 * ============================================================================ */

kern_return_t ipc_port_set_create(ipc_port_set_t *port_set)
{
    kern_return_t kr;

    /* Create a port set */
    kr = mach_port_construct(mach_task_self(),
                             &port_set->port_set,
                             0,
                             MACH_PORT_TYPE_PORT_SET);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    /* Create a default reply port */
    kr = mach_port_allocate(mach_task_self(),
                            port_set->default_port,
                            MACH_PORT_RIGHT_RECEIVE);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), port_set->port_set);
        return kr;
    }

    port_set->active_requests = 0;
    return KERN_SUCCESS;
}

void ipc_port_set_destroy(ipc_port_set_t *port_set)
{
    if (port_set->port_set != MACH_PORT_NULL) {
        mach_port_destroy(mach_task_self(), port_set->port_set);
        port_set->port_set = MACH_PORT_NULL;
    }
    if (port_set->default_port != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), port_set->default_port);
        port_set->default_port = MACH_PORT_NULL;
    }
    port_set->active_requests = 0;
}

kern_return_t ipc_port_set_add(mach_port_t port)
{
    return mach_port_insert_member(port, mach_task_self());
}

kern_return_t ipc_port_set_remove(mach_port_t port)
{
    return mach_port_extract_member(port, mach_task_self());
}

/* ============================================================================
 * Message Sending/Receiving
 * ============================================================================ */

kern_return_t ipc_send_to_server(ipc_server_type_t server_type,
                                 mach_msg_t message,
                                 mach_msg_size_t message_size,
                                 uint64_t timeout_ms)
{
    ipc_server_entry_t *server = ipc_lookup_server(server_type);
    if (!server || !server->active) {
        return KERN_INVALID_RIGHT;
    }

    /* Set the destination port */
    message->msgh_remote_port = server->send_port;
    message->msgh_size = message_size;

    /* Send with timeout */
    kern_return_t kr = mach_msg_send(message);
    if (kr != KERN_SUCCESS) {
        /* If the server port is dead, mark server as crashed */
        if (kr == MACH_RCV_PORT_DIED || kr == MACH_SEND_INVALID_DEST) {
            ipc_server_crashed(server_type);
        }
        return kr;
    }

    return KERN_SUCCESS;
}

kern_return_t ipc_receive_from_set(ipc_port_set_t *port_set,
                                   mach_msg_t *message,
                                   mach_msg_size_t max_size)
{
    kern_return_t kr = mach_msg(
        message,
        MACH_RCV_MSG | MACH_RCV_LARGE,
        0,                       /* send size */
        max_size,                /* max receive size */
        port_set->port_set,     /* receive port */
        MACH_MSG_TIMEOUT_NONE,  /* no timeout by default */
        MACH_PORT_NULL          /* notify */
    );

    if (kr == MACH_MSG_SUCCESS) {
        port_set->active_requests--;
    }
    return kr;
}

kern_return_t ipc_send_reply(mach_port_t reply_port,
                             mach_msg_t reply,
                             mach_msg_size_t reply_size)
{
    reply->msgh_remote_port = reply_port;
    reply->msgh_size = reply_size;

    return mach_msg_send(reply);
}

/* ============================================================================
 * Request Tracking
 * ============================================================================ */

kern_return_t ipc_track_request(uint32_t request_id,
                                mach_port_t reply_port,
                                task_t owner,
                                uint64_t timeout_ms)
{
    /* Find a free slot */
    for (uint32_t i = 0; i < IPC_MAX_PENDING_REQUESTS; i++) {
        if (g_pending[i].completed) {
            g_pending[i].request_id = request_id;
            g_pending[i].reply_port = reply_port;
            g_pending[i].owner_task = owner;
            g_pending[i].timeout = timeout_ms * 1000000ULL; /* Convert to ns */
            g_pending[i].completed = false;
            return KERN_SUCCESS;
        }
    }

    return KERN_RESOURCE_SHORTAGE;
}

void ipc_complete_request(uint32_t request_id)
{
    for (uint32_t i = 0; i < IPC_MAX_PENDING_REQUESTS; i++) {
        if (!g_pending[i].completed && g_pending[i].request_id == request_id) {
            g_pending[i].completed = true;
            return;
        }
    }
}

kern_return_t ipc_cancel_request(uint32_t request_id)
{
    for (uint32_t i = 0; i < IPC_MAX_PENDING_REQUESTS; i++) {
        if (!g_pending[i].completed && g_pending[i].request_id == request_id) {
            /* Deallocate the reply port if we own it */
            if (g_pending[i].reply_port != MACH_PORT_NULL) {
                mach_port_deallocate(mach_task_self(), g_pending[i].reply_port);
            }
            g_pending[i].completed = true;
            return KERN_SUCCESS;
        }
    }
    return KERN_INVALID_RIGHT;
}

void ipc_cleanup_timeouts(void)
{
    /* In a full implementation, we would check timestamps and
     * cancel any requests that have exceeded their timeout.
     * This requires a monotonic clock source. */
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

uint32_t ipc_generate_request_id(void)
{
    uint32_t id = g_next_request_id++;
    if (g_next_request_id == 0) {
        g_next_request_id = 1;  /* Avoid zero ID */
    }
    return id;
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
    ipc_server_entry_t *entry = ipc_lookup_server(type);
    return entry && entry->active && !entry->crashed;
}

void ipc_server_ref_inc(ipc_server_type_t type)
{
    ipc_server_entry_t *entry = ipc_lookup_server(type);
    if (entry) {
        entry->ref_count++;
    }
}

void ipc_server_ref_dec(ipc_server_type_t type)
{
    ipc_server_entry_t *entry = ipc_lookup_server(type);
    if (entry && entry->ref_count > 0) {
        entry->ref_count--;
    }
}