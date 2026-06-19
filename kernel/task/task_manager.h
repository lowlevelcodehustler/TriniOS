/*
 * TriniOS - Task & Thread Management
 *
 * Wraps Mach task/thread primitives with higher-level abstractions
 * for process lifecycle, thread creation, and scheduling.
 */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <mach/mach.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define TASK_MAX_THREADS        256
#define TASK_NAME_MAX_LEN       64
#define TASK_ARGV_MAX           128
#define TASK_ENV_MAX            128
#define TASK_PATH_MAX           512
#define TASK_STACK_DEFAULT      (8 * 1024 * 1024)   /* 8MB */

/* ============================================================================
 * Task States
 * ============================================================================ */

typedef enum {
    TASK_STATE_RUNNING,       /* Currently executing */
    TASK_STATE_STOPPED,       /* Stopped (signal or explicit) */
    TASK_STATE_WAITING,       /* Waiting for child (waitpid) */
    TASK_STATE_ZOMBIE,        /* Exited but not reaped */
    TASK_STATE_DEAD           /* Fully terminated */
} trini_task_state_t;

/* ============================================================================
 * Thread States
 * ============================================================================ */

typedef enum {
    THREAD_STATE_RUNNING,
    THREAD_STATE_READY,
    THREAD_STATE_WAITING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_HALTED,
    THREAD_STATE_DEAD
} trini_thread_state_t;

/* ============================================================================
 * Thread Descriptor
 * ============================================================================ */

typedef struct thread_desc {
    thread_t            thread;         /* Mach thread */
    char                name[TASK_NAME_MAX_LEN];
    trini_thread_state_t state;
    int                 priority;       /* Scheduling priority */
    vm_offset_t         stack_base;     /* Stack base address */
    vm_size_t           stack_size;     /* Stack size */
    vm_offset_t         entry_point;    /* Thread entry function */
    vm_offset_t         arg;            /* Argument to entry function */
    struct thread_desc *next;           /* Linked list */
} thread_desc_t;

/* ============================================================================
 * Task Descriptor
 * ============================================================================ */

typedef struct task_desc {
    task_t              task;           /* Mach task */
    char                name[TASK_NAME_MAX_LEN];
    char                path[TASK_PATH_MAX];  /* Executable path */
    trini_task_state_t  state;
    int                 pid;            /* Process ID (BSD-compatible) */
    int                 ppid;           /* Parent PID */
    int                 pgid;           /* Process group ID */
    int                 sid;            /* Session ID */
    int                 exit_status;    /* Exit status */
    int                 signal;         /* Signal that caused termination */
    vm_offset_t         entry_point;    /* Task entry point address */
    vm_offset_t         stack_base;     /* Initial stack base */
    vm_size_t           stack_size;     /* Initial stack size */
    thread_desc_t      *threads;        /* Linked list of threads */
    uint32_t            thread_count;
    task_t              parent_task;    /* Parent task */
    struct task_desc   *children;       /* Linked list of children */
    struct task_desc   *next;          /* Linked list */
    /* Environment */
    char              **argv;
    char              **envp;
    int                 argc;
} task_desc_t;

/* ============================================================================
 * Task Creation Attributes
 * ============================================================================ */

typedef struct {
    const char         *name;           /* Task name */
    const char         *path;           /* Executable path */
    char              **argv;           /* Argument vector */
    char              **envp;           /* Environment vector */
    vm_offset_t         entry_point;    /* Entry point address */
    vm_size_t           stack_size;     /* Stack size (0 = default) */
    task_t              parent;         /* Parent task */
    bool                inherit_ports;  /* Inherit IPC ports */
    int                 priority;       /* Initial scheduling priority */
} task_attr_t;

/* ============================================================================
 * Task Manager Functions
 * ============================================================================ */

/* Initialize the task manager */
kern_return_t task_manager_init(void);

/* Shutdown the task manager */
void task_manager_shutdown(void);

/* ============================================================================
 * Task Lifecycle
 * ============================================================================ */

/* Create a new task */
kern_return_t task_create(const task_attr_t *attr, task_desc_t **task_out);

/* Destroy a task (cleanup) */
kern_return_t task_destroy(task_desc_t *desc);

/* Terminate a task */
kern_return_t task_terminate(task_desc_t *desc, int exit_status);

/* Wait for a task to exit */
kern_return_t task_wait(task_desc_t *desc, int *exit_status);

/* Get task by PID */
task_desc_t *task_get_by_pid(int pid);

/* Get task by Mach task_t */
task_desc_t *task_get_by_mach_task(task_t task);

/* Get task by name */
task_desc_t *task_get_by_name(const char *name);

/* ============================================================================
 * Task Information
 * ============================================================================ */

/* Get task info */
typedef struct {
    int                 pid;
    int                 ppid;
    char                name[TASK_NAME_MAX_LEN];
    char                path[TASK_PATH_MAX];
    trini_task_state_t  state;
    uint32_t            thread_count;
    vm_size_t           vm_size;
    uint64_t            user_time;
    uint64_t            system_time;
} trini_task_info_t;

kern_return_t task_get_info(task_desc_t *desc, trini_task_info_t *info);

/* List all tasks */
uint32_t task_list_all(task_desc_t **list, uint32_t max_count);

/* ============================================================================
 * Thread Management
 * ============================================================================ */

/* Create a new thread in a task */
kern_return_t thread_create(task_desc_t *task_desc,
                            const char *name,
                            vm_offset_t entry_point,
                            vm_offset_t arg,
                            vm_size_t stack_size,
                            thread_desc_t **thread_out);

/* Destroy a thread */
kern_return_t thread_destroy(thread_desc_t *desc);

/* Get thread by Mach thread_t */
thread_desc_t *thread_get_by_mach_thread(thread_t thread);

/* Set thread priority */
kern_return_t thread_set_priority(thread_desc_t *desc, int priority);

/* Get thread state */
trini_thread_state_t thread_get_state(thread_desc_t *desc);

/* ============================================================================
 * Scheduling
 * ============================================================================ */

/* Set scheduling policy */
kern_return_t task_set_policy(task_desc_t *desc, int policy, int priority);

/* Set thread scheduling policy */
kern_return_t thread_set_policy(thread_desc_t *desc, int policy, int priority);

/* ============================================================================
 * Exception Handling
 * ============================================================================ */

/* Install exception handler for a task */
kern_return_t task_install_exception_handler(task_desc_t *desc,
                                             uint32_t mask,
                                             mach_port_t handler,
                                             uint32_t behavior,
                                             uint32_t flavor);

/* ============================================================================
 * PID Management
 * ============================================================================ */

/* Allocate a new PID */
int task_alloc_pid(void);

/* Free a PID */
void task_free_pid(int pid);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/* Get the init task (PID 1) */
task_desc_t *task_get_init(void);

/* Get the current task descriptor */
task_desc_t *task_get_current(void);

/* Get total task count */
uint32_t task_get_total_count(void);

#endif /* TASK_MANAGER_H */