/*
 * TriniOS BSD Compatibility Server - Process Management
 *
 * Implements fork(), execve(), waitpid(), exit(), and kill()
 * on top of Mach task/thread primitives.
 */

#include "bsd_server.h"
#include "posix.h"
#include "elf.h"
#include "initrd_lookup.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * fork() Implementation
 *
 * Strategy:
 * 1. Create a new Mach task via task_create()
 * 2. Copy the parent's address space via vm_copy()
 * 3. Create a new task context in the BSD server
 * 4. Return child PID to parent, 0 to child
 * ============================================================================ */

kern_return_t bsd_handle_fork(task_context_t *parent_ctx,
                              bsd_fork_reply_t *reply)
{
    kern_return_t kr;
    task_t child_task = MACH_PORT_NULL;
    mach_port_t child_recv_port = MACH_PORT_NULL;
    int child_pid;

    if (!parent_ctx) {
        reply->result = -1;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    /* Create a new Mach task */
    kr = task_create(mach_task_self(), FALSE, &child_task);
    if (kr != KERN_SUCCESS) {
        reply->result = -1;
        reply->error = kr;
        return kr;
    }

    /* Allocate a receive port for the child's BSD IPC */
    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &child_recv_port);
    if (kr != KERN_SUCCESS) {
        task_deallocate(child_task);
        reply->result = -1;
        reply->error = kr;
        return kr;
    }

    /* Insert send right so the child can send messages */
    kr = mach_port_insert_right(mach_task_self(), child_recv_port,
                                child_recv_port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), child_recv_port);
        task_deallocate(child_task);
        reply->result = -1;
        reply->error = kr;
        return kr;
    }

    /* Create the BSD task context for the child */
    child_pid = bsd_create_task_context(false, child_task, child_recv_port,
                                        parent_ctx->pid, NULL);
    if (child_pid < 0) {
        mach_port_deallocate(mach_task_self(), child_recv_port);
        task_deallocate(child_task);
        reply->result = -1;
        reply->error = POSIX_ENOMEM;
        return POSIX_ENOMEM;
    }

    /* Copy file descriptors from parent (dup semantics) */
    for (int i = 3; i < BSD_MAX_FILE_DESCRIPTORS; i++) {
        if (parent_ctx->fds[i].port != MACH_PORT_NULL || parent_ctx->fds[i].fd != 0) {
            task_context_t *child = bsd_get_task_context_by_pid(child_pid);
            if (child) {
                memcpy(&child->fds[i], &parent_ctx->fds[i], sizeof(fd_entry_t));
                /* Increment ref count on shared ports */
                if (child->fds[i].port != MACH_PORT_NULL) {
                    mach_port_mod_refs(mach_task_self(), child->fds[i].port,
                                       MACH_MSG_TYPE_MAKE_SEND, 1);
                }
            }
        }
    }

    /* Add to parent's children list */
    if (parent_ctx->num_children < BSD_MAX_TASKS) {
        parent_ctx->children[parent_ctx->num_children] = bsd_get_task_context_by_pid(child_pid);
        parent_ctx->num_children++;
    }

    /* Return results: 0 for parent, child PID stored in reply */
    reply->result = child_pid;  /* This is the child's PID */
    reply->error = 0;

    return KERN_SUCCESS;
}

/* ============================================================================
 * execve() Implementation
 *
 * Strategy (Phase 1 — entry-point discovery):
 *
 * In Mach microkernel architecture, true execve semantics require kernel-side
 * involvement because user-space code cannot replace its own address space.
 * The proper flow would be:
 *   1. BSD server sends Mach RPC to kernel with path + binary data
 *   2. Kernel parses ELF, unmaps old VM, maps new segments
 *   3. Kernel returns entry point for thread creation
 *
 * Phase 1 workaround (entry-point only):
 *   - The BSD server uses initrd_lookup_find_file() to locate the binary
 *     in the cpio_newc archive that was passed via command-line argument
 *   - Entry point is returned via reply message
 *   - User-space code creates a new Mach thread at that address
 *   - NOTE: This does NOT replace the current task's address space (not true execve)
 *     Full implementation requires kernel-side ELF loading + VM manipulation.
 * ============================================================================ */

kern_return_t bsd_handle_execve(task_context_t *ctx,
                                bsd_execve_request_t *req,
                                bsd_execve_reply_t *reply)
{
    kern_return_t kr;
    elf_load_context_t elf_ctx;
    const void *binary_data = NULL;
    uint64_t binary_size = 0;

    if (!ctx || !req || req->path[0] == '\0') {
        reply->result = -1;
        reply->error = POSIX_EINVAL;
        reply->entry_point = 0;
        return POSIX_EINVAL;
    }

    /* Clear context */
    memset(&elf_ctx, 0, sizeof(elf_ctx));

    /* ── Look up binary in initrd lookup table ──────────────────────────
     *
     * The BSD server must have been initialized with initrd data via
     * initrd_lookup_init() during startup (see main.c bsd_server_main()).
     * This is passed as a command-line argument: --initrd=0xADDRESS */

    binary_data = initrd_lookup_find_file(req->path, &binary_size);
    if (!binary_data || binary_size == 0) {
        reply->result = -1;
        reply->error = POSIX_ENOENT;
        reply->entry_point = 0;
        return KERN_SUCCESS; /* Structure populated, caller checks result/error */
    }

    /* ── Validate ELF magic ───────────────────────────────────────────── */
    if (!elf_is_valid((vm_offset_t)(uintptr_t)binary_data, (vm_size_t)binary_size)) {
        reply->result = -1;
        reply->error = POSIX_ENOEXEC;  /* Not a valid ELF binary */
        reply->entry_point = 0;
        return KERN_SUCCESS;
    }

    /* ── Parse ELF header ─────────────────────────────────────────────── */
    kr = elf_parse_header((vm_offset_t)(uintptr_t)binary_data,
                          (vm_size_t)binary_size, &elf_ctx);
    if (kr != KERN_SUCCESS) {
        reply->result = -1;
        reply->error = POSIX_ENOEXEC;  /* Invalid executable format */
        reply->entry_point = 0;
        return KERN_SUCCESS;
    }

    /* ── Compute actual entry point ───────────────────────────────────── */
    uint64_t entry = elf_ctx.header->e_entry;

    reply->result = 0;       /* Success — entry point available */
    reply->error = 0;
    reply->entry_point = (uint64_t)(uintptr_t)entry;

    return KERN_SUCCESS;
}

/* ============================================================================
 * waitpid() Implementation
 *
 * Strategy:
 * 1. Find the child task by PID
 * 2. Check if exited
 * 3. If not and WNOHANG, return EAGAIN
 * 4. Otherwise spin-wait (blocking) until child exits
 * ============================================================================ */

kern_return_t bsd_handle_waitpid(task_context_t *ctx,
                                 bsd_waitpid_request_t *req,
                                 bsd_waitpid_reply_t *reply)
{
    task_context_t *child;
    int wait_options = req->options; /* Default: blocking */

    if (!ctx) {
        reply->result = -1;
        reply->status = 0;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    /* Find the child task */
    if (req->pid == -1) {
        /* Wait for any child - use first child in list */
        if (ctx->num_children > 0) {
            child = ctx->children[0];
        } else {
            reply->result = -1;
            reply->status = 0;
            reply->error = POSIX_ECHILD;
            return POSIX_ECHILD;
        }
    } else if (req->pid > 0) {
        child = bsd_get_task_context_by_pid(req->pid);
        if (!child || child->ppid != ctx->pid) {
            reply->result = -1;
            reply->status = 0;
            reply->error = POSIX_ECHILD;
            return POSIX_ECHILD;
        }
    } else {
        /* Negative PIDs and zero are not fully supported yet */
        reply->result = -1;
        reply->status = 0;
        reply->error = POSIX_EINVAL;
        return POSIX_EINVAL;
    }

    /* If child hasn't exited and we're non-blocking, return EAGAIN */
    if (!child->exited && (wait_options & WNOHANG)) {
        reply->result = 0;  /* WNOHANG: no child ready */
        reply->status = 0;
        reply->error = 0;
        return KERN_SUCCESS;
    }

    /* Blocking wait - poll until child exits */
    int max_spins = 10000; /* Prevent infinite loops */
    while (!child->exited && max_spins > 0) {
        max_spins--;
        /* Small delay via mach_msg with timeout (yield to other tasks) */
        mach_msg_return_t ret;
        ret = mach_msg(NULL, MACH_RCV_MSG, 0, sizeof(mach_msg_header_t),
                       child->bsd_port, 10000, MACH_PORT_NULL); /* 10ms timeout */

        if (ret == MACH_MSG_SUCCESS) {
            /* Got a message from the child - process it */
            mach_msg_header_t *msg = (mach_msg_header_t *)malloc(sizeof(mach_msg_header_t));
            if (msg) {
                bsd_dispatch_request(msg, sizeof(mach_msg_header_t));
                free(msg);
            }
        }
    }

    /* Return exit status */
    if (!child->exited) {
        reply->result = -1;
        reply->status = 0;
        reply->error = POSIX_EAGAIN;
        return POSIX_EAGAIN;
    }

    reply->result = child->pid;
    reply->status = (child->exit_status << 8) | 0x7F; /* Normal exit format */
    reply->error = 0;

    return KERN_SUCCESS;
}

/* ============================================================================
 * exit() Implementation
 *
 * Strategy:
 * 1. Mark the task as exited with status
 * 2. Notify parent via SIGCHLD (if signal handler is set up)
 * 3. Deallocate Mach task port
 * ============================================================================ */

kern_return_t bsd_handle_exit(task_context_t *ctx,
                              bsd_exit_request_t *req,
                              bsd_exit_reply_t *reply)
{
    if (!ctx) {
        reply->result = -1;
        return POSIX_EINVAL;
    }

    /* Mark as exited */
    ctx->exited = true;
    ctx->exit_status = req->status;

    /* Notify parent via SIGCHLD */
    if (ctx->parent) {
        bsd_handle_kill(ctx->parent->pid, SIGCHLD);
    }

    /* Deallocate the Mach task port */
    if (ctx->task_port != MACH_PORT_NULL) {
        task_suspend(ctx->task_port);
        task_terminate(ctx->task_port);
        mach_port_deallocate(mach_task_self(), ctx->task_port);
        ctx->task_port = MACH_PORT_NULL;
    }

    /* Free all file descriptors */
    for (int i = 0; i < BSD_MAX_FILE_DESCRIPTORS; i++) {
        bsd_free_fd(ctx, i);
    }

    reply->result = 0;
    return KERN_SUCCESS;
}

/* ============================================================================
 * kill() Implementation
 *
 * Strategy:
 * 1. Look up the target task by PID
 * 2. For SIGKILL/SIGTERM, call bsd_handle_exit
 * 3. For other signals, queue to task's signal handler (TODO)
 * ============================================================================ */

kern_return_t bsd_handle_kill(int pid, int sig)
{
    task_context_t *target;

    if (sig <= 0 || sig > 31) {
        return POSIX_EINVAL;
    }

    target = bsd_get_task_context_by_pid(pid);
    if (!target) {
        return POSIX_ESRCH;
    }

    /* SIGKILL: immediate termination */
    if (sig == SIGKILL) {
        bsd_exit_request_t req;
        memset(&req, 0, sizeof(req));
        req.status = 128 + SIGKILL;
        bsd_reply_t reply;
        memset(&reply, 0, sizeof(reply));
        kern_return_t kr = bsd_handle_exit(target, &req, (bsd_exit_reply_t *)&reply);
        return kr;
    }

    /* SIGTERM: graceful termination */
    if (sig == SIGTERM) {
        bsd_exit_request_t req;
        memset(&req, 0, sizeof(req));
        req.status = 128 + SIGTERM;
        bsd_reply_t reply;
        memset(&reply, 0, sizeof(reply));
        kern_return_t kr = bsd_handle_exit(target, &req, (bsd_exit_reply_t *)&reply);
        return kr;
    }

    /* For other signals, we'd queue them to the task's signal handler.
     * This is a simplified implementation that ignores most signals. */
    (void)sig;

    return KERN_SUCCESS;
}

/* ============================================================================
 * getpid() / getppid() - POSIX client library functions
 * ============================================================================ */

int posix_getpid(void)
{
    /* Query the BSD server for our PID via IPC */
    bsd_getpid_request_t request;
    bsd_getpid_reply_t reply;

    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    /* This would normally send a Mach message to the BSD server.
     * For now, return -1 as we can't reach the server from here. */
    (void)request;
    (void)reply;

    return -1; /* TODO: Implement IPC call to BSD server */
}

int posix_getppid(void)
{
    /* Similar to getpid - would query the BSD server */
    return -1; /* TODO: Implement IPC call to BSD server */
}