/*
 * TriniOS BSD Compatibility Server - Process Management
 *
 * Implements fork(), execve(), waitpid(), exit(), and kill()
 * on top of Mach task/thread primitives.
 */

#include "bsd_server.h"
#include "posix.h"
#include "elf.h"
#include <string.h>

/* ============================================================================
 * fork() Implementation
 *
 * Strategy:
 * 1. Create a new Mach task via task_create()
 * 2. Copy the parent's address space via vm_copy()
 * 3. Create an initial thread in the child that resumes from fork()
 * 4. Return child PID to parent, 0 to child
 *
 * Note: This is a simplified fork. Full copy-on-write requires
 * setting page protections to read-only and handling page faults.
 * ============================================================================ */

kern_return_t bsd_handle_fork(task_context_t *parent, task_context_t **child_out, int *pid_out)
{
    kern_return_t kr;
    task_t child_task;
    vm_offset_t min_address;
    vm_size_t size;
    boolean_t may_move;
    task_context_t *child;
    int child_pid;

    if (!parent) {
        return POSIX_EINVAL;
    }

    /* Create new Mach task */
    kr = task_create(mach_task_self(),
                     FALSE,        /* set_control - child not a control task */
                     &child_task);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    /* Copy parent's address space to child */
    kr = vm_allocate(child_task,
                     &min_address,
                     0,            /* size determined by kernel */
                     VM_FLAGS_ANYWHERE);
    
    /* Copy the parent's memory */
    kr = vm_read_overwrite(mach_task_self(),
                           min_address,
                           size,
                           (vm_address_t)&min_address,
                           &size);

    /* Allocate child context */
    child = (task_context_t *)malloc(sizeof(task_context_t));
    if (!child) {
        task_deallocate(child_task);
        return POSIX_ENOMEM;
    }

    /* Initialize child context */
    memset(child, 0, sizeof(task_context_t));
    child->task = child_task;
    child->parent = parent;
    child->pid = next_pid++;
    child->ppid = parent->pid;
    child->exited = false;
    child->exit_status = 0;

    /* Copy file descriptors from parent */
    memcpy(child->fds, parent->fds, sizeof(fd_entry_t) * BSD_MAX_FILE_DESCRIPTORS);

    /* Create receive port for child's IPC */
    kr = mach_port_allocate(child_task, MACH_PORT_RIGHT_RECEIVE, &child->bsd_port);
    if (kr != KERN_SUCCESS) {
        free(child);
        task_deallocate(child_task);
        return kr;
    }

    /* Add child to parent's children list */
    child->children = parent->children;
    parent->children = child;

    /* Add to global task table */
    bsd_add_task(child);

    /* Return results */
    if (pid_out) {
        *pid_out = child->pid;
    }
    if (child_out) {
        *child_out = child;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * execve() Implementation
 *
 * Strategy:
 * 1. Parse the ELF binary at the given path
 * 2. Unmap the current address space
 * 3. Map the new binary's segments
 * 4. Set up the initial stack (argv, envp, aux vectors)
 * 5. Create a new thread at the entry point
 * ============================================================================ */

kern_return_t bsd_handle_execve(task_context_t *ctx, const char *path, char *const argv[], char *const envp[])
{
    kern_return_t kr;
    elf_load_context_t elf_ctx;
    vm_offset_t stack_ptr;
    vm_size_t stack_size = 8 * 1024 * 1024; /* 8MB initial stack */

    if (!ctx || !path) {
        return POSIX_EINVAL;
    }

    /* Parse the ELF binary */
    memset(&elf_ctx, 0, sizeof(elf_ctx));
    kr = elf_load(path, &elf_ctx);
    if (kr != KERN_SUCCESS) {
        return POSIX_ENOEXEC;
    }

    /* Unmap current address space */
    kr = vm_deallocate(mach_task_self(),
                       0,
                       vm_get_max_page_size()); /* Simplified: full range */

    /* Map the ELF segments */
    kr = elf_map_segments(&elf_ctx);
    if (kr != KERN_SUCCESS) {
        elf_unload(&elf_ctx);
        return kr;
    }

    /* Set memory protections */
    kr = elf_set_protections(&elf_ctx);
    if (kr != KERN_SUCCESS) {
        elf_unload(&elf_ctx);
        return kr;
    }

    /* Allocate stack */
    kr = vm_allocate(ctx->task,
                     &stack_ptr,
                     stack_size,
                     VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        elf_unload(&elf_ctx);
        return kr;
    }

    /* Set stack protections (read-write, no execute) */
    kr = vm_protect(ctx->task,
                    stack_ptr,
                    stack_size,
                    FALSE, /* set_max */
                    VM_PROT_READ | VM_PROT_WRITE);

    /* Set up stack: argv, envp, aux vectors */
    /* Stack layout (growing downward):
     *   [aux vectors]
     *   [envp strings]
     *   [argv strings]
     *   [envp pointers]
     *   [argv pointers]
     *   [argc]
     */
    /* TODO: Write argc, argv, envp to stack */

    /* Clear the task context's old state */
    ctx->exit_status = 0;
    ctx->exited = false;

    /* Clean up ELF loader context */
    elf_unload(&elf_ctx);

    /*
     * The caller (C library wrapper) is responsible for:
     * - Creating a new thread at elf_ctx.entry
     * - Resuming execution
     */
    return KERN_SUCCESS;
}

/* ============================================================================
 * waitpid() Implementation
 *
 * Strategy:
 * 1. Check if the specified child has exited
 * 2. If WNOHANG, return immediately
 * 3. Otherwise, block (via Mach message timeout) until child exits
 * 4. Return child PID and exit status
 * ============================================================================ */

kern_return_t bsd_handle_waitpid(task_context_t *ctx, int pid, int *status)
{
    task_context_t *child;
    int wait_options = 0; /* Default: blocking */

    if (!ctx) {
        return POSIX_EINVAL;
    }

    /* Find the child task */
    if (pid == -1) {
        /* Wait for any child */
        child = ctx->children;
    } else if (pid > 0) {
        child = bsd_get_task_context_by_pid(pid);
        if (child && child->ppid != ctx->pid) {
            return POSIX_ECHILD; /* Not our child */
        }
    } else if (pid < -1) {
        /* Process group - TODO: implement process groups */
        return POSIX_EINVAL;
    } else {
        /* pid == 0: wait for any child in same process group */
        child = ctx->children;
    }

    if (!child) {
        return POSIX_ECHILD;
    }

    /* If child hasn't exited and we're non-blocking, return EAGAIN */
    if (!child->exited && (wait_options & WNOHANG)) {
        return POSIX_EAGAIN;
    }

    /* If blocking and child not exited, wait via Mach */
    while (!child->exited) {
        /* Block on Mach port with timeout */
        mach_msg_timeout_t timeout = 1000000; /* 1 second */
        mach_msg_return_t ret;

        ret = mach_msg_simple_receive(ctx->bsd_port, timeout);
        if (ret != MACH_MSG_SUCCESS && ret != MACH_RCV_TIMED_OUT) {
            break;
        }
    }

    /* Return exit status */
    if (status) {
        *status = (child->exit_status << 8) | 0; /* Normal exit */
    }

    return child->pid;
}

/* ============================================================================
 * exit() Implementation
 *
 * Strategy:
 * 1. Mark the task as exited
 * 2. Notify parent via SIGCHLD
 * 3. Deallocate threads
 * 4. Deallocate task
 * ============================================================================ */

kern_return_t bsd_handle_exit(task_context_t *ctx, int status)
{
    if (!ctx) {
        return POSIX_EINVAL;
    }

    /* Mark as exited */
    ctx->exited = true;
    ctx->exit_status = status;

    /* Notify parent via SIGCHLD */
    if (ctx->parent) {
        bsd_handle_kill(ctx->parent->pid, SIGCHLD);
    }

    /* Terminate all threads in the task */
    /* TODO: iterate threads and call thread_terminate */

    /* Deallocate the task */
    if (ctx->task != MACH_PORT_NULL) {
        task_suspend(ctx->task);
        task_deallocate(ctx->task);
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * kill() Implementation
 *
 * Strategy:
 * 1. Look up the target task by PID
 * 2. Send signal via Mach thread interrupt
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
        return bsd_handle_exit(target, 128 + SIGKILL);
    }

    /* SIGTERM: graceful termination */
    if (sig == SIGTERM) {
        /* TODO: deliver signal to task's signal handler */
        return KERN_SUCCESS;
    }

    /* For other signals, queue to the task's signal handler */
    /* TODO: implement signal delivery via Mach interrupts */
    return KERN_SUCCESS;
}

/* ============================================================================
 * getpid() / getppid()
 * ============================================================================ */

int posix_getpid(void)
{
    task_context_t *ctx = bsd_get_task_context(mach_task_self());
    if (ctx) {
        return ctx->pid;
    }
    return -1;
}

int posix_getppid(void)
{
    task_context_t *ctx = bsd_get_task_context(mach_task_self());
    if (ctx) {
        return ctx->ppid;
    }
    return -1;
}