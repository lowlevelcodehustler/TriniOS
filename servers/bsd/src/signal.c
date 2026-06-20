/*
 * TriniOS BSD Compatibility Server - Signal Handling
 *
 * Implements signal() and signal delivery on top of Mach IPC.
 */

#include "bsd_server.h"
#include "posix.h"
#include <string.h>

/* ============================================================================
 * Signal Handler Table (per-task)
 * ============================================================================ */

/* Maximum number of signals we can track per task */
#define BSD_MAX_SIGNALS 32

/* Signal handler entry */
typedef struct {
    int sig;
    void (*handler)(int);
    bool registered;
} signal_handler_t;

/* ============================================================================
 * Helper: Get signal handler for a task and signal
 * ============================================================================ */

static void *bsd_get_signal_handler(task_context_t *ctx, int sig)
{
    if (!ctx || sig <= 0 || sig > BSD_MAX_SIGNALS) {
        return NULL;
    }

    /* For now, we don't have a per-task signal handler table.
     * This is a placeholder for future implementation. */
    (void)sig;
    return NULL;
}

/* ============================================================================
 * signal() Implementation
 *
 * Strategy:
 * 1. Register/unregister a signal handler for the given signal number
 * 2. Return the previous handler (or SIG_ERR on error)
 * 3. For SIG_DFL/SIG_IGN, set special handling
 * ============================================================================ */

kern_return_t bsd_handle_signal(task_context_t *ctx,
                                bsd_signal_request_t *req,
                                bsd_signal_reply_t *reply)
{
    if (!ctx || !req || !reply) {
        reply->result = -1;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    int sig = req->sig;
    uint64_t handler_addr = req->handler_addr;

    /* Validate signal number */
    if (sig <= 0 || sig > 31) {
        reply->result = -1;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    /* For now, we don't have a full signal handler table.
     * Return SIG_ERR to indicate signals are not fully supported yet. */
    (void)handler_addr;

    /* SIGKILL and SIGSTOP cannot be caught or ignored */
    if (sig == SIGKILL || sig == SIGSTOP) {
        reply->result = -1;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    /* TODO: Implement full signal handler registration.
     * In a complete implementation, we would:
     * 1. Store the handler in a per-task signal table
     * 2. When the signal is raised (via bsd_handle_kill), deliver it
     *    by sending an interrupt message to the task's Mach port
     * 3. The task would receive the signal and invoke its handler */

    reply->result = -1; /* SIG_ERR */
    reply->error = POSIX_ENOSYS;
    return POSIX_ENOSYS;
}

/* ============================================================================
 * Signal Delivery (internal)
 *
 * Deliver a signal to a task by sending an interrupt message.
 * This is called from bsd_handle_kill() for non-fatal signals.
 * ============================================================================ */

void bsd_deliver_signal(task_context_t *target, int sig)
{
    if (!target || target->bsd_port == MACH_PORT_NULL) {
        return;
    }

    /* Send an interrupt message to the task's BSD port.
     * The task should be listening on this port and check for signals. */
    /* TODO: Implement signal delivery via Mach RPC interrupt or similar mechanism */
    (void)sig;
}