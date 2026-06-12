/*
 * TriniOS BSD Compatibility Server - Signal Handling
 *
 * Implements POSIX signal delivery and handling on top of
 * Mach thread interrupts and task notifications.
 */

#include "bsd_server.h"
#include "posix.h"
#include <string.h>

/* ============================================================================
 * Signal Handler Table
 * ============================================================================ */

typedef enum {
    SIG_DFL = 0,    /* Default action */
    SIG_IGN = 1,    /* Ignore signal */
    SIG_ERR = -1    /* Error */
} sig_action_t;

/* Per-task signal handler table */
static void (*signal_handlers[BSD_MAX_TASKS][32])(int);

/* ============================================================================
 * Default Signal Actions
 * ============================================================================ */

typedef enum {
    SIG_ACTION_TERMINATE = 0,   /* Terminate process */
    SIG_ACTION_CORE,            /* Terminate + dump core */
    SIG_ACTION_STOP,            /* Stop process */
    SIG_ACTION_CONTINUE,        /* Continue if stopped */
    SIG_ACTION_IGNORE           /* Ignore */
} sig_default_action_t;

/* Default actions for each signal */
static const sig_default_action_t sig_defaults[32] = {
    [SIGHUP]    = SIG_ACTION_TERMINATE,
    [SIGINT]    = SIG_ACTION_TERMINATE,
    [SIGQUIT]   = SIG_ACTION_CORE,
    [SIGILL]    = SIG_ACTION_CORE,
    [SIGTRAP]   = SIG_ACTION_CORE,
    [SIGABRT]   = SIG_ACTION_CORE,
    [SIGBUS]    = SIG_ACTION_CORE,
    [SIGFPE]    = SIG_ACTION_CORE,
    [SIGKILL]   = SIG_ACTION_TERMINATE,  /* Cannot be caught or ignored */
    [SIGUSR1]   = SIG_ACTION_TERMINATE,
    [SIGSEGV]   = SIG_ACTION_CORE,
    [SIGUSR2]   = SIG_ACTION_TERMINATE,
    [SIGPIPE]   = SIG_ACTION_IGNORE,
    [SIGALRM]   = SIG_ACTION_IGNORE,
    [SIGTERM]   = SIG_ACTION_TERMINATE,
    [SIGSTKFLT] = SIG_ACTION_TERMINATE,
    [SIGCHLD]   = SIG_ACTION_IGNORE,
    [SIGCONT]   = SIG_ACTION_CONTINUE,
    [SIGSTOP]   = SIG_ACTION_STOP,
    [SIGTSTP]   = SIG_ACTION_STOP,
    [SIGTTIN]   = SIG_ACTION_STOP,
    [SIGTTOU]   = SIG_ACTION_STOP,
    [SIGURG]    = SIG_ACTION_IGNORE,
    [SIGXCPU]   = SIG_ACTION_TERMINATE,
    [SIGXFSZ]   = SIG_ACTION_CORE,
    [SIGVTALRM] = SIG_ACTION_IGNORE,
    [SIGPROF]   = SIG_ACTION_IGNORE,
    [SIGWINCH]  = SIG_ACTION_IGNORE,
    [SIGIO]     = SIG_ACTION_IGNORE,
    [SIGPWR]    = SIG_ACTION_TERMINATE,
    [SIGSYS]    = SIG_ACTION_TERMINATE,
};

/* ============================================================================
 * signal() / sigaction() Implementation
 *
 * Set the signal handler for a given signal in the task's context.
 * ============================================================================ */

kern_return_t bsd_handle_signal(task_context_t *ctx, int sig, void (*handler)(int))
{
    if (!ctx) {
        return POSIX_EINVAL;
    }

    if (sig <= 0 || sig > 31) {
        return POSIX_EINVAL;
    }

    /* SIGKILL and SIGSTOP cannot be caught or ignored */
    if (sig == SIGKILL || sig == SIGSTOP) {
        return POSIX_EINVAL;
    }

    /* Store the handler */
    if (handler == SIG_IGN) {
        signal_handlers[ctx->pid][sig] = NULL;
    } else {
        signal_handlers[ctx->pid][sig] = handler;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * Signal Delivery
 *
 * Deliver a signal to a task. This is called when:
 * - kill() sends a signal
 * - The kernel generates a signal (e.g., SIGSEGV)
 * - An alarm expires (SIGALRM)
 * ============================================================================ */

kern_return_t bsd_deliver_signal(task_context_t *ctx, int sig)
{
    kern_return_t kr;
    void (*handler)(int);
    sig_default_action_t default_action;

    if (!ctx || ctx->exited) {
        return POSIX_ESRCH;
    }

    if (sig <= 0 || sig > 31) {
        return POSIX_EINVAL;
    }

    /* Get the handler */
    handler = signal_handlers[ctx->pid][sig];
    default_action = sig_defaults[sig];

    /* Check if signal should be ignored */
    if (handler == NULL && default_action == SIG_ACTION_IGNORE) {
        return KERN_SUCCESS;
    }

    /* If no handler and default is terminate, exit the task */
    if (handler == NULL && (default_action == SIG_ACTION_TERMINATE || default_action == SIG_ACTION_CORE)) {
        return bsd_handle_exit(ctx, 128 + sig);
    }

    /* If default is stop, suspend the task */
    if (handler == NULL && default_action == SIG_ACTION_STOP) {
        kr = task_suspend(ctx->task);
        return kr;
    }

    /* If default is continue, resume the task */
    if (handler == NULL && default_action == SIG_ACTION_CONTINUE) {
        kr = task_resume(ctx->task);
        return kr;
    }

    /* Call the user's signal handler */
    if (handler) {
        /*
         * Signal handler invocation via Mach:
         * 1. Create a thread in the target task
         * 2. Set up the handler's stack frame
         * 3. Start the thread at the handler address
         * 4. When handler returns, terminate the thread
         */
        /* TODO: implement proper signal handler invocation */
        /* For now, just note that the signal was delivered */

        /*
         * Simplified approach: send a Mach message to the BSD server's
         * port in the target task, which triggers the handler.
         */
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * Pending Signals
 * ============================================================================ */

typedef struct {
    uint32_t pending;      /* Bitmap of pending signals */
    uint32_t blocked;      /* Bitmap of blocked signals */
} sig_state_t;

static sig_state_t sig_state[BSD_MAX_TASKS];

/*
 * Block signals.
 */
kern_return_t bsd_sigblock(task_context_t *ctx, int sig)
{
    if (!ctx || sig <= 0 || sig > 31) {
        return POSIX_EINVAL;
    }
    sig_state[ctx->pid].blocked |= (1 << (sig - 1));
    return KERN_SUCCESS;
}

/*
 * Unblock signals.
 */
kern_return_t bsd_sigunblock(task_context_t *ctx, int sig)
{
    if (!ctx || sig <= 0 || sig > 31) {
        return POSIX_EINVAL;
    }
    sig_state[ctx->pid].blocked &= ~(1 << (sig - 1));

    /* Check if this signal is pending and should be delivered */
    if (sig_state[ctx->pid].pending & (1 << (sig - 1))) {
        return bsd_deliver_signal(ctx, sig);
    }

    return KERN_SUCCESS;
}

/*
 * Queue a signal (if not blocked).
 */
kern_return_t bsd_queue_signal(task_context_t *ctx, int sig)
{
    if (!ctx || sig <= 0 || sig > 31) {
        return POSIX_EINVAL;
    }

    /* Mark as pending */
    sig_state[ctx->pid].pending |= (1 << (sig - 1));

    /* If not blocked, deliver immediately */
    if (!(sig_state[ctx->pid].blocked & (1 << (sig - 1)))) {
        return bsd_deliver_signal(ctx, sig);
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * pause() - Wait for any signal
 * ============================================================================ */

kern_return_t bsd_pause(task_context_t *ctx)
{
    if (!ctx) {
        return POSIX_EINVAL;
    }

    /*
     * Block until a signal is received.
     * Implemented by waiting on the task's BSD port.
     */
    mach_msg_return_t ret;

    ret = mach_msg_simple_receive(ctx->bsd_port, MACH_MSG_TIMEOUT_NONE);

    if (ret == MACH_MSG_SUCCESS) {
        /* A signal was delivered */
        return KERN_SUCCESS;
    }

    return POSIX_EINTR;
}