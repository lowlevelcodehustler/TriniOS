/*
 * TriniOS - Virtual Memory Manager Implementation
 *
 * Wraps Mach VM primitives for task memory management.
 */

#include "vm_manager.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * vm_space_create - Create VM space for new task
 * ============================================================================ */

vm_space_t *vm_space_create(task_t task)
{
    kern_return_t kr;
    vm_space_t *space;
    vm_offset_t stack_addr;
    vm_size_t stack_size = VM_DEFAULT_STACK_SIZE;

    space = (vm_space_t *)calloc(1, sizeof(vm_space_t));
    if (!space) {
        return NULL;
    }

    space->task = task;
    space->vm_map = mach_task_self();
    space->break_ = 0;
    space->regions = NULL;
    space->region_count = 0;
    space->total_mapped = 0;
    space->total_resident = 0;

    kr = vm_create_stack(space, &stack_addr, stack_size);
    if (kr != KERN_SUCCESS) {
        free(space);
        return NULL;
    }
    space->stack_base = stack_addr;

    return space;
}

/* ============================================================================
 * vm_space_destroy - Destroy VM space
 * ============================================================================ */

void vm_space_destroy(vm_space_t *space)
{
    vm_region_t *region, *next;

    if (!space) {
        return;
    }

    region = space->regions;
    while (region) {
        next = region->next;
        vm_deallocate(space->task, region->start, region->end - region->start);
        free(region);
        region = next;
    }

    space->regions = NULL;
    space->region_count = 0;
    free(space);
}

/* ============================================================================
 * vm_allocate_region - Allocate memory region
 * ============================================================================ */

kern_return_t vm_allocate_region(vm_space_t *space,
                                 vm_offset_t *address,
                                 vm_size_t size,
                                 boolean_t flags,
                                 vm_prot_t protection,
                                 vm_region_type_t type)
{
    kern_return_t kr;
    vm_region_t *region;

    size = (size + vm_page_size - 1) & ~(vm_page_size - 1);

    kr = vm_allocate(space->task, address, size, flags);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    kr = vm_protect(space->task, *address, size, FALSE, protection);
    if (kr != KERN_SUCCESS) {
        vm_deallocate(space->task, *address, size);
        return kr;
    }

    region = (vm_region_t *)calloc(1, sizeof(vm_region_t));
    if (!region) {
        vm_deallocate(space->task, *address, size);
        return KERN_RESOURCE_SHORTAGE;
    }

    region->start = *address;
    region->end = *address + size;
    region->protection = protection;
    region->inheritance = VM_INHERIT_COPY;
    region->type = type;
    region->owner = space->task;
    region->is_pinned = false;
    region->is_gpu_accessible = false;
    region->next = space->regions;

    space->regions = region;
    space->region_count++;
    space->total_mapped += size;

    return KERN_SUCCESS;
}

/* ============================================================================
 * vm_deallocate_region - Deallocate memory region
 * ============================================================================ */

kern_return_t vm_deallocate_region(vm_space_t *space,
                                   vm_offset_t address,
                                   vm_size_t size)
{
    vm_region_t *region, *prev = NULL;

    region = space->regions;
    while (region) {
        if (region->start <= address && region->end >= address + size) {
            break;
        }
        prev = region;
        region = region->next;
    }

    if (!region) {
        return KERN_INVALID_ADDRESS;
    }

    if (prev) {
        prev->next = region->next;
    } else {
        space->regions = region->next;
    }

    space->total_mapped -= (region->end - region->start);
    space->region_count--;

    vm_deallocate(space->task, region->start, region->end - region->start);
    free(region);

    return KERN_SUCCESS;
}

/* ============================================================================
 * vm_protect_region - Change protection
 * ============================================================================ */

kern_return_t vm_protect_region(vm_space_t *space,
                                vm_offset_t address,
                                vm_size_t size,
                                boolean_t max,
                                vm_prot_t protection)
{
    vm_region_t *region = vm_find_region(space, address);
    if (!region) {
        return KERN_INVALID_ADDRESS;
    }

    kern_return_t kr = vm_protect(space->task, address, size, max, protection);
    if (kr == KERN_SUCCESS) {
        region->protection = protection;
    }
    return kr;
}

/* ============================================================================
 * vm_find_region - Find region containing address
 * ============================================================================ */

vm_region_t *vm_find_region(vm_space_t *space, vm_offset_t address)
{
    vm_region_t *region = space->regions;
    while (region) {
        if (address >= region->start && address < region->end) {
            return region;
        }
        region = region->next;
    }
    return NULL;
}

/* ============================================================================
 * vm_set_brk / vm_get_brk - Program break (sbrk semantics)
 * ============================================================================ */

kern_return_t vm_set_brk(vm_space_t *space, vm_offset_t addr)
{
    vm_offset_t old_brk = space->break_;

    if (addr > old_brk) {
        vm_size_t grow = addr - old_brk;
        kern_return_t kr = vm_allocate_region(space, &old_brk, grow,
                                              VM_FLAGS_ANYWHERE,
                                              VM_PROT_READ | VM_PROT_WRITE,
                                              VM_REGION_HEAP);
        if (kr != KERN_SUCCESS) {
            return kr;
        }
    } else if (addr < old_brk) {
        vm_size_t shrink = old_brk - addr;
        vm_deallocate_region(space, old_brk, shrink);
    }

    space->break_ = addr;
    return KERN_SUCCESS;
}

vm_offset_t vm_get_brk(vm_space_t *space)
{
    return space->break_;
}

/* ============================================================================
 * vm_create_stack - Create stack region
 * ============================================================================ */

kern_return_t vm_create_stack(vm_space_t *space,
                              vm_offset_t *stack_top,
                              vm_size_t size)
{
    vm_offset_t addr;
    kern_return_t kr;

    kr = vm_allocate_region(space, &addr, size,
                            VM_FLAGS_ANYWHERE,
                            VM_PROT_READ | VM_PROT_WRITE,
                            VM_REGION_STACK);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    *stack_top = addr + size;
    return KERN_SUCCESS;
}

/* ============================================================================
 * vm_pin_pages - Pin pages (prevent paging)
 * ============================================================================ */

kern_return_t vm_pin_pages(vm_space_t *space,
                           vm_offset_t address,
                           vm_size_t size)
{
    vm_region_t *region = vm_find_region(space, address);
    if (!region) {
        return KERN_INVALID_ADDRESS;
    }

    region->is_pinned = true;
    return KERN_SUCCESS;
}

/* ============================================================================
 * vm_unpin_pages - Unpin pages
 * ============================================================================ */

kern_return_t vm_unpin_pages(vm_space_t *space,
                             vm_offset_t address,
                             vm_size_t size)
{
    vm_region_t *region = vm_find_region(space, address);
    if (!region) {
        return KERN_INVALID_ADDRESS;
    }

    region->is_pinned = false;
    return KERN_SUCCESS;
}

/* ============================================================================
 * vm_mark_gpu_accessible - Mark pages as GPU-coherent
 * ============================================================================ */

kern_return_t vm_mark_gpu_accessible(vm_space_t *space,
                                     vm_offset_t address,
                                     vm_size_t size)
{
    vm_region_t *region = vm_find_region(space, address);
    if (!region) {
        return KERN_INVALID_ADDRESS;
    }

    region->is_gpu_accessible = true;
    region->is_pinned = true;
    return KERN_SUCCESS;
}

/* ============================================================================
 * vm_get_stats - Get VM statistics
 * ============================================================================ */

void vm_get_stats(vm_space_t *space, vm_stats_t *stats)
{
    vm_region_t *region;
    stats->total_mapped = space->total_mapped;
    stats->total_resident = space->total_resident;
    stats->total_pinned = 0;
    stats->total_gpu_accessible = 0;
    stats->region_count = space->region_count;

    region = space->regions;
    while (region) {
        vm_size_t region_size = region->end - region->start;
        if (region->is_pinned) {
            stats->total_pinned += region_size;
        }
        if (region->is_gpu_accessible) {
            stats->total_gpu_accessible += region_size;
        }
        region = region->next;
    }
}

/* ============================================================================
 * vm_space_copy - Copy VM space for fork
 * ============================================================================ */

kern_return_t vm_space_copy(vm_space_t *src, task_t dest_task, vm_space_t **dest_out)
{
    vm_space_t *dest;
    vm_region_t *region;
    vm_offset_t new_addr;

    dest = (vm_space_t *)calloc(1, sizeof(vm_space_t));
    if (!dest) {
        return KERN_RESOURCE_SHORTAGE;
    }

    dest->task = dest_task;
    dest->vm_map = mach_task_self();
    dest->break_ = src->break_;
    dest->stack_base = 0;
    dest->regions = NULL;
    dest->region_count = 0;
    dest->total_mapped = 0;
    dest->total_resident = 0;

    region = src->regions;
    while (region) {
        vm_size_t size = region->end - region->start;
        kern_return_t kr;

        kr = vm_allocate_region(dest, &new_addr, size,
                                VM_FLAGS_ANYWHERE,
                                region->protection,
                                region->type);
        if (kr != KERN_SUCCESS) {
            vm_space_destroy(dest);
            return kr;
        }

        region = region->next;
    }

    *dest_out = dest;
    return KERN_SUCCESS;
}
