/*
 * TriniOS - Virtual Memory Manager
 *
 * Manages virtual memory spaces for all tasks.
 * Wraps Mach VM primitives with higher-level abstractions
 * for the BSD server and user-space applications.
 */

#ifndef VM_MANAGER_H
#define VM_MANAGER_H

#include <mach/mach.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define VM_DEFAULT_STACK_SIZE     (8 * 1024 * 1024)    /* 8MB */
#define VM_DEFAULT_HEAP_SIZE      (64 * 1024 * 1024)   /* 64MB */
#define VM_MAX_MAPPINGS_PER_TASK  4096

/* ============================================================================
 * Memory Region Types
 * ============================================================================ */

typedef enum {
    VM_REGION_CODE,         /* Executable code */
    VM_REGION_DATA,         /* Read-write data */
    VM_REGION_RODATA,       /* Read-only data */
    VM_REGION_STACK,        /* Thread stack */
    VM_REGION_HEAP,         /* Heap allocations */
    VM_REGION_SHARED,       /* Shared memory */
    VM_REGION_DEVICE,       /* Device memory mapping */
    VM_REGION_GPU,          /* GPU accessible memory */
    VM_REGION_AI_WEIGHTS,   /* AI model weights (pinned) */
    VM_REGION_AI_ACTIVATIONS /* AI activations (GPU accessible) */
} vm_region_type_t;

/* ============================================================================
 * Memory Region Descriptor
 * ============================================================================ */

typedef struct vm_region {
    vm_offset_t       start;
    vm_offset_t       end;
    vm_prot_t         protection;
    vm_inherit_t      inheritance;
    vm_region_type_t  type;
    task_t            owner;
    bool              is_pinned;            /* Cannot be paged out */
    bool              is_gpu_accessible;
    struct vm_region *next;
} vm_region_t;

/* ============================================================================
 * VM Space (per-task virtual memory layout)
 * ============================================================================ */

typedef struct {
    task_t            task;
    task_t            vm_map;               /* Mach task port for VM operations (freestanding: vm_map_t is kernel-internal) */
    vm_region_t      *regions;              /* Linked list of mapped regions */
    vm_offset_t       break_;               /* Current program break (heap top) */
    vm_offset_t       stack_base;           /* Initial stack location */
    uint32_t          region_count;
    vm_size_t         total_mapped;
    vm_size_t         total_resident;
} vm_space_t;

/* ============================================================================
 * VM Manager Functions
 * ============================================================================ */

/* Create a new VM space for a task (called during task creation / fork) */
vm_space_t *vm_space_create(task_t task);

/* Destroy a VM space and deallocate all regions (called during task exit) */
void vm_space_destroy(vm_space_t *space);

/* Allocate a region in the task's address space */
kern_return_t vm_allocate_region(vm_space_t *space,
                                 vm_offset_t *address,
                                 vm_size_t size,
                                 boolean_t flags,
                                 vm_prot_t protection,
                                 vm_region_type_t type);

/* Deallocate a region */
kern_return_t vm_deallocate_region(vm_space_t *space,
                                   vm_offset_t address,
                                   vm_size_t size);

/* Change protection on a region */
kern_return_t vm_protect_region(vm_space_t *space,
                                vm_offset_t address,
                                vm_size_t size,
                                boolean_t max,
                                vm_prot_t protection);

/* Find a region containing the given address */
vm_region_t *vm_find_region(vm_space_t *space, vm_offset_t address);

/* Get or create the heap region and extend the break (sbrk semantics) */
kern_return_t vm_set_brk(vm_space_t *space, vm_offset_t addr);
vm_offset_t   vm_get_brk(vm_space_t *space);

/* Get or create the stack region */
kern_return_t vm_create_stack(vm_space_t *space,
                              vm_offset_t *stack_top,
                              vm_size_t size);

/* Pin pages in physical memory (for AI model weights / GPU access) */
kern_return_t vm_pin_pages(vm_space_t *space,
                           vm_offset_t address,
                           vm_size_t size);

/* Unpin pages */
kern_return_t vm_unpin_pages(vm_space_t *space,
                             vm_offset_t address,
                             vm_size_t size);

/* Mark pages as GPU-accessible (coherent with GPU) */
kern_return_t vm_mark_gpu_accessible(vm_space_t *space,
                                     vm_offset_t address,
                                     vm_size_t size);

/* Get VM space statistics */
typedef struct {
    vm_size_t  total_mapped;
    vm_size_t  total_resident;
    vm_size_t  total_pinned;
    vm_size_t  total_gpu_accessible;
    uint32_t   region_count;
} vm_stats_t;

void vm_get_stats(vm_space_t *space, vm_stats_t *stats);

/* Copy VM space (for fork) */
kern_return_t vm_space_copy(vm_space_t *src, task_t dest_task, vm_space_t **dest_out);

#endif /* VM_MANAGER_H */