/*
 * TriniOS - Task & Thread Management Implementation
 *
 * Wraps Mach task/thread primitives for process lifecycle management.
 * Stub implementations for freestanding builds (no real Mach kernel yet).
 */

#include "task_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Internal State
 * ============================================================================ */

#define MAX_TASKS             1024
#define MIN_VALID_PID         3         /* 0=swapper, 1=init, 2=reserved */
#define MAX_PID               32768

static task_desc_t *g_task_table[MAX_TASKS];
static uint32_t g_task_count = 0;
static bool g_initialized = false;

static task_desc_t *g_init_task = NULL;
static int g_next_pid = MIN_VALID_PID;

/* PID bitmap for efficient allocation (tracks used PIDs) */
static bool g_pid_used[MAX_PID];

/* ============================================================================
 * task_manager_init - Initialize the task manager
 * ============================================================================ */

kern_return_t task_manager_init(void)
{
    if (g_initialized) {
        return KERN_SUCCESS;
    }

    memset(g_task_table, 0, sizeof(g_task_table));
    memset(g_pid_used, 0, sizeof(g_pid_used));
    g_pid_used[0] = true;   /* swapper */
    g_pid_used[1] = true;   /* init */
    g_pid_used[2] = true;   /* reserved */
    g_task_count = 0;
    g_next_pid = MIN_VALID_PID;

    g_initialized = true;
    return KERN_SUCCESS;
}

/* ============================================================================
 * task_manager_shutdown - Shutdown the task manager
 * ============================================================================ */

void task_manager_shutdown(void)
{
    if (!g_initialized) {
        return;
    }

    /* Terminate all tasks except init */
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_task_table[i] && g_task_table[i] != g_init_task) {
            task_terminate(g_task_table[i], 137); /* SIGKILL */
            task_destroy(g_task_table[i]);
        }
    }

    g_initialized = false;
}

/* ============================================================================
 * PID Management
 * ============================================================================ */

int task_alloc_pid(void)
{
    if (!g_initialized) {
        return -1;
    }

    /* Simple linear search for next free PID */
    for (int attempts = 0; attempts < MAX_PID; attempts++) {
        if (!g_pid_used[g_next_pid]) {
            g_pid_used[g_next_pid] = true;
            int pid = g_next_pid;
            g_next_pid++;
            if (g_next_pid >= MAX_PID) {
                g_next_pid = MIN_VALID_PID;
            }
            return pid;
        }
        g_next_pid++;
        if (g_next_pid >= MAX_PID) {
            g_next_pid = MIN_VALID_PID;
        }
    }

    return -1;  /* No PIDs available */
}

void task_free_pid(int pid)
{
    if (pid >= 0 && pid < MAX_PID) {
        g_pid_used[pid] = false;
    }
}

/* ============================================================================
 * Task Creation
 * ============================================================================ */

kern_return_t task_create(const task_attr_t *attr, task_desc_t **task_out)
{
    task_desc_t *desc = NULL;
    task_t new_task;

    if (!g_initialized || !attr || !task_out) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Check task table capacity */
    if (g_task_count >= MAX_TASKS) {
        return KERN_RESOURCE_SHORTAGE;
    }

    /* Allocate task descriptor */
    desc = (task_desc_t *)calloc(1, sizeof(task_desc_t));
    if (!desc) {
        return KERN_RESOURCE_SHORTAGE;
    }

    /* Create Mach task (stub: use mach_task_self for now)
     * In a real implementation, this would call task_create() RPC to the kernel. */
    new_task = mach_task_self();

    /* Fill in descriptor */
    desc->task = new_task;
    desc->state = TASK_STATE_RUNNING;
    desc->pid = task_alloc_pid();
    desc->ppid = 0;
    desc->pgid = desc->pid;  /* Initially own process group */
    desc->sid = desc->pid;
    desc->exit_status = 0;
    desc->signal = 0;
    desc->entry_point = attr->entry_point;
    desc->stack_size = (attr->stack_size > 0) ? attr->stack_size : TASK_STACK_DEFAULT;
    desc->stack_base = 0;
    desc->thread_count = 0;
    desc->threads = NULL;
    desc->parent_task = attr->parent;
    desc->children = NULL;
    desc->next = NULL;
    desc->argc = 0;
    desc->argv = NULL;
    desc->envp = NULL;

    if (attr->name) {
        strncpy(desc->name, attr->name, TASK_NAME_MAX_LEN - 1);
        desc->name[TASK_NAME_MAX_LEN - 1] = '\0';
    } else {
        snprintf(desc->name, TASK_NAME_MAX_LEN, "task_%d", desc->pid);
    }

    if (attr->path) {
        strncpy(desc->path, attr->path, TASK_PATH_MAX - 1);
        desc->path[TASK_PATH_MAX - 1] = '\0';
    }

    /* Add to task table */
    uint32_t slot = 0;
    while (slot < MAX_TASKS && g_task_table[slot] != NULL) {
        slot++;
    }

    if (slot >= MAX_TASKS) {
        free(desc);
        return KERN_RESOURCE_SHORTAGE;
    }

    g_task_table[slot] = desc;
    g_task_count++;

    /* If this is PID 1, mark as init */
    if (desc->pid == 1) {
        g_init_task = desc;
    }

    /* Link to parent's children list */
    if (attr->parent != TASK_NULL) {
        task_desc_t *parent = task_get_by_mach_task(attr->parent);
        if (parent) {
            desc->ppid = parent->pid;
            desc->next = parent->children;
            parent->children = desc;
        }
    }

    *task_out = desc;
    return KERN_SUCCESS;
}

/* ============================================================================
 * Task Destruction
 * ============================================================================ */

kern_return_t task_destroy(task_desc_t *desc)
{
    if (!desc) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Destroy all threads */
    thread_desc_t *thread = desc->threads;
    while (thread) {
        thread_desc_t *next = thread->next;
        thread_destroy(thread);
        free(thread);
        thread = next;
    }
    desc->threads = NULL;
    desc->thread_count = 0;

    /* Free argv/envp */
    if (desc->argv) {
        free(desc->argv);
        desc->argv = NULL;
    }
    if (desc->envp) {
        free(desc->envp);
        desc->envp = NULL;
    }

    /* Remove from task table */
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_task_table[i] == desc) {
            g_task_table[i] = NULL;
            g_task_count--;
            break;
        }
    }

    /* Remove from parent's children list */
    if (desc->parent_task != TASK_NULL) {
        task_desc_t *parent = task_get_by_mach_task(desc->parent_task);
        if (parent) {
            task_desc_t **cpp = &parent->children;
            while (*cpp) {
                if (*cpp == desc) {
                    *cpp = desc->next;
                    break;
                }
                cpp = &(*cpp)->next;
            }
        }
    }

    /* Free PID */
    task_free_pid(desc->pid);

    /* Clear init pointer */
    if (g_init_task == desc) {
        g_init_task = NULL;
    }

    free(desc);
    return KERN_SUCCESS;
}

/* ============================================================================
 * Task Termination
 * ============================================================================ */

kern_return_t task_terminate(task_desc_t *desc, int exit_status)
{
    if (!desc) {
        return KERN_INVALID_ARGUMENT;
    }

    desc->exit_status = exit_status;
    desc->state = TASK_STATE_ZOMBIE;

    /* Stub: in a real implementation, this would call task_suspend() RPC */
    return KERN_SUCCESS;
}

kern_return_t task_wait(task_desc_t *desc, int *exit_status)
{
    if (!desc) {
        return KERN_INVALID_ARGUMENT;
    }

    /* In a full implementation, this would block until the task exits.
     * For now, check if already exited. */
    if (desc->state == TASK_STATE_ZOMBIE || desc->state == TASK_STATE_DEAD) {
        if (exit_status) {
            *exit_status = desc->exit_status;
        }
        return KERN_SUCCESS;
    }

    return KERN_FAILURE;
}

/* ============================================================================
 * Task Lookup
 * ============================================================================ */

task_desc_t *task_get_by_pid(int pid)
{
    if (!g_initialized) {
        return NULL;
    }

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_task_table[i] && g_task_table[i]->pid == pid) {
            return g_task_table[i];
        }
    }
    return NULL;
}

task_desc_t *task_get_by_mach_task(task_t task)
{
    if (!g_initialized) {
        return NULL;
    }

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_task_table[i] && g_task_table[i]->task == task) {
            return g_task_table[i];
        }
    }
    return NULL;
}

task_desc_t *task_get_by_name(const char *name)
{
    if (!g_initialized || !name) {
        return NULL;
    }

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_task_table[i] &&
            strncmp(g_task_table[i]->name, name, TASK_NAME_MAX_LEN) == 0) {
            return g_task_table[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Task Information
 * ============================================================================ */

kern_return_t task_get_info(task_desc_t *desc, trini_task_info_t *info)
{
    if (!desc || !info) {
        return KERN_INVALID_ARGUMENT;
    }

    info->pid = desc->pid;
    info->ppid = desc->ppid;
    strncpy(info->name, desc->name, TASK_NAME_MAX_LEN);
    info->name[TASK_NAME_MAX_LEN - 1] = '\0';
    strncpy(info->path, desc->path, TASK_PATH_MAX);
    info->path[TASK_PATH_MAX - 1] = '\0';
    info->state = desc->state;
    info->thread_count = desc->thread_count;
    info->vm_size = 0;
    info->user_time = 0;
    info->system_time = 0;

    return KERN_SUCCESS;
}

uint32_t task_list_all(task_desc_t **list, uint32_t max_count)
{
    if (!g_initialized || !list || max_count == 0) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < MAX_TASKS && count < max_count; i++) {
        if (g_task_table[i]) {
            list[count] = g_task_table[i];
            count++;
        }
    }
    return count;
}

/* ============================================================================
 * Thread Management
 * ============================================================================ */

kern_return_t thread_create(task_desc_t *task_desc,
                            const char *name,
                            vm_offset_t entry_point,
                            vm_offset_t arg,
                            vm_size_t stack_size,
                            thread_desc_t **thread_out)
{
    thread_desc_t *desc = NULL;

    if (!task_desc || !thread_out) {
        return KERN_INVALID_ARGUMENT;
    }

    if (stack_size == 0) {
        stack_size = TASK_STACK_DEFAULT;
    }

    /* Allocate thread descriptor */
    desc = (thread_desc_t *)calloc(1, sizeof(thread_desc_t));
    if (!desc) {
        return KERN_RESOURCE_SHORTAGE;
    }

    /* Create Mach thread (stub: use mach_task_self for now)
     * In a real implementation, this would call thread_create() RPC. */
    desc->thread = mach_task_self();

    /* Fill in descriptor */
    if (name) {
        strncpy(desc->name, name, TASK_NAME_MAX_LEN - 1);
        desc->name[TASK_NAME_MAX_LEN - 1] = '\0';
    } else {
        snprintf(desc->name, TASK_NAME_MAX_LEN, "thread_%d", task_desc->thread_count);
    }

    desc->state = THREAD_STATE_READY;
    desc->priority = 0;  /* Default priority */
    desc->entry_point = entry_point;
    desc->arg = arg;
    desc->stack_size = stack_size;
    desc->stack_base = 0;
    desc->next = task_desc->threads;

    /* Link into task's thread list */
    task_desc->threads = desc;
    task_desc->thread_count++;

    *thread_out = desc;
    return KERN_SUCCESS;
}

kern_return_t thread_destroy(thread_desc_t *desc)
{
    if (!desc) {
        return KERN_INVALID_ARGUMENT;
    }

    desc->state = THREAD_STATE_DEAD;
    desc->thread = THREAD_NULL;

    return KERN_SUCCESS;
}

thread_desc_t *thread_get_by_mach_thread(thread_t thread)
{
    if (!g_initialized) {
        return NULL;
    }

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (!g_task_table[i]) continue;

        thread_desc_t *t = g_task_table[i]->threads;
        while (t) {
            if (t->thread == thread) {
                return t;
            }
            t = t->next;
        }
    }
    return NULL;
}

kern_return_t thread_set_priority(thread_desc_t *desc, int priority)
{
    if (!desc) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Stub: in a real implementation, this would call thread_policy_set() RPC */
    desc->priority = priority;
    return KERN_SUCCESS;
}

trini_thread_state_t thread_get_state(thread_desc_t *desc)
{
    if (!desc) {
        return THREAD_STATE_DEAD;
    }
    return desc->state;
}

/* ============================================================================
 * Scheduling
 * ============================================================================ */

kern_return_t task_set_policy(task_desc_t *desc, int policy, int priority)
{
    if (!desc) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Apply policy to all threads in the task */
    thread_desc_t *thread = desc->threads;
    while (thread) {
        thread_set_policy(thread, policy, priority);
        thread = thread->next;
    }
    return KERN_SUCCESS;
}

kern_return_t thread_set_policy(thread_desc_t *desc, int policy, int priority)
{
    if (!desc) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Stub: in a real implementation, this would call thread_policy_set() RPC */
    (void)policy;
    desc->priority = priority;
    return KERN_SUCCESS;
}

/* ============================================================================
 * Exception Handling
 * ============================================================================ */

kern_return_t task_install_exception_handler(task_desc_t *desc,
                                             uint32_t mask,
                                             mach_port_t handler,
                                             uint32_t behavior,
                                             uint32_t flavor)
{
    if (!desc) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Stub: in a real implementation, this would call
     * task_set_exception_ports() RPC to the kernel. */
    (void)mask;
    (void)handler;
    (void)behavior;
    (void)flavor;
    return KERN_SUCCESS;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

task_desc_t *task_get_init(void)
{
    return g_init_task;
}

task_desc_t *task_get_current(void)
{
    return task_get_by_mach_task(mach_task_self());
}

uint32_t task_get_total_count(void)
{
    return g_task_count;
}