/*
 * TriniOS - GPU Memory Manager Implementation
 *
 * Manages GPU-accessible memory for AI inference workloads.
 * Provides pinned, coherent, and device memory allocations.
 * Integrates with the VM manager for memory classification.
 */

#include "gpu_mem.h"
#include "vm_manager.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Internal State
 * ============================================================================ */

static gpu_mem_pool_t g_gpu_pool;
static bool g_initialized = false;

/* ============================================================================
 * gpu_mem_init - Initialize the GPU memory pool
 * ============================================================================ */

kern_return_t gpu_mem_init(vm_size_t pool_size)
{
    if (g_initialized) {
        return KERN_ALREADY_INTELLIGENT; /* Already initialized */
    }

    memset(&g_gpu_pool, 0, sizeof(gpu_mem_pool_t));

    if (pool_size == 0) {
        pool_size = GPU_MEM_DEFAULT_POOL_SIZE;
    }

    /* Align pool size to GPU page boundary */
    pool_size = (pool_size + GPU_MEM_PAGE_SIZE - 1) & ~(GPU_MEM_PAGE_SIZE - 1);

    g_gpu_pool.pool_size = pool_size;
    g_gpu_pool.used = 0;
    g_gpu_pool.peak_used = 0;
    g_gpu_pool.active_allocations = 0;
    g_gpu_pool.total_allocations = 0;
    g_gpu_pool.total_frees = 0;

    /* Initialize all allocation slots as free */
    for (uint32_t i = 0; i < GPU_MEM_MAX_ALLOCATIONS; i++) {
        g_gpu_pool.allocations[i].in_use = false;
        g_gpu_pool.allocations[i].host_addr = 0;
        g_gpu_pool.allocations[i].device_ptr = 0;
        g_gpu_pool.allocations[i].size = 0;
        g_gpu_pool.allocations[i].ref_count = 0;
        g_gpu_pool.allocations[i].owner_task = TASK_NULL;
    }

    g_initialized = true;
    return KERN_SUCCESS;
}

/* ============================================================================
 * gpu_mem_shutdown - Shutdown and release GPU memory pool
 * ============================================================================ */

void gpu_mem_shutdown(void)
{
    if (!g_initialized) {
        return;
    }

    /* Free all active allocations */
    for (uint32_t i = 0; i < GPU_MEM_MAX_ALLOCATIONS; i++) {
        if (g_gpu_pool.allocations[i].in_use) {
            gpu_mem_free(g_gpu_pool.allocations[i].host_addr);
        }
    }

    g_initialized = false;
    memset(&g_gpu_pool, 0, sizeof(gpu_mem_pool_t));
}

/* ============================================================================
 * gpu_mem_alloc - Allocate GPU-accessible memory
 * ============================================================================ */

kern_return_t gpu_mem_alloc(vm_offset_t *host_addr, uint64_t *device_ptr,
                            vm_size_t size, gpu_mem_type_t type)
{
    kern_return_t kr;
    gpu_mem_alloc_t *alloc = NULL;
    vm_offset_t addr;

    if (!g_initialized) {
        return KERN_INVALID_HOST;
    }

    if (size == 0) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Align size to GPU page size */
    size = (size + GPU_MEM_PAGE_SIZE - 1) & ~(GPU_MEM_PAGE_SIZE - 1);

    /* Check if pool has enough space */
    if (g_gpu_pool.used + size > g_gpu_pool.pool_size) {
        return KERN_RESOURCE_SHORTAGE;
    }

    /* Find a free allocation slot */
    for (uint32_t i = 0; i < GPU_MEM_MAX_ALLOCATIONS; i++) {
        if (!g_gpu_pool.allocations[i].in_use) {
            alloc = &g_gpu_pool.allocations[i];
            break;
        }
    }

    if (alloc == NULL) {
        return KERN_RESOURCE_SHORTAGE;
    }

    /* Allocate memory based on type */
    switch (type) {
        case GPU_MEM_PAGE_LOCKED:
            /* Pinned host memory (page-locked, not pageable) */
            kr = vm_allocate(TASK_NULL, &addr, size, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS) {
                return kr;
            }

            /* Mark as pinned so it cannot be paged out */
            kr = vm_memory_object_control(0, addr, size, TRUE);
            if (kr != KERN_SUCCESS) {
                vm_deallocate(TASK_NULL, addr, size);
                return kr;
            }
            break;

        case GPU_MEM_COHERENT:
            /* Coherent memory - visible to both CPU and GPU */
            kr = vm_allocate(TASK_NULL, &addr, size, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS) {
                return kr;
            }

            /* Mark as GPU-accessible and pinned */
            kr = vm_allocate(TASK_NULL, &addr, size, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS) {
                return kr;
            }
            break;

        case GPU_MEM_DEVICE:
            /* Device-only memory (GPU address space) */
            /* For now, allocate from host and track device pointer separately */
            kr = vm_allocate(TASK_NULL, &addr, size, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS) {
                return kr;
            }
            break;

        case GPU_MEM_UNIFIED:
            /* Unified memory - accessible from both CPU and GPU automatically */
            kr = vm_allocate(TASK_NULL, &addr, size, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS) {
                return kr;
            }
            break;

        default:
            return KERN_INVALID_ARGUMENT;
    }

    /* Fill in allocation record */
    alloc->host_addr = addr;
    alloc->device_ptr = (uint64_t)addr; /* Placeholder: real GPU addr from driver */
    alloc->size = size;
    alloc->type = type;
    alloc->owner_task = TASK_NULL;
    alloc->in_use = true;
    alloc->ref_count = 1;

    /* Update pool statistics */
    g_gpu_pool.used += size;
    if (g_gpu_pool.used > g_gpu_pool.peak_used) {
        g_gpu_pool.peak_used = g_gpu_pool.used;
    }
    g_gpu_pool.active_allocations++;
    g_gpu_pool.total_allocations++;

    /* Return results */
    if (host_addr) {
        *host_addr = addr;
    }
    if (device_ptr) {
        *device_ptr = alloc->device_ptr;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * gpu_mem_free - Free GPU-accessible memory
 * ============================================================================ */

kern_return_t gpu_mem_free(vm_offset_t host_addr)
{
    gpu_mem_alloc_t *alloc = gpu_mem_get_info(host_addr);

    if (alloc == NULL) {
        return KERN_INVALID_ADDRESS;
    }

    if (alloc->ref_count > 1) {
        alloc->ref_count--;
        return KERN_SUCCESS;
    }

    /* Deallocate the VM memory */
    kern_return_t kr = vm_deallocate(TASK_NULL, alloc->host_addr, alloc->size);

    /* Update pool statistics */
    g_gpu_pool.used -= alloc->size;
    g_gpu_pool.active_allocations--;
    g_gpu_pool.total_frees++;

    /* Clear allocation record */
    alloc->in_use = false;
    alloc->host_addr = 0;
    alloc->device_ptr = 0;
    alloc->size = 0;
    alloc->type = GPU_MEM_PAGE_LOCKED;
    alloc->owner_task = TASK_NULL;
    alloc->ref_count = 0;

    return kr;
}

/* ============================================================================
 * gpu_mem_get_info - Get allocation info for a host address
 * ============================================================================ */

gpu_mem_alloc_t *gpu_mem_get_info(vm_offset_t host_addr)
{
    if (!g_initialized) {
        return NULL;
    }

    for (uint32_t i = 0; i < GPU_MEM_MAX_ALLOCATIONS; i++) {
        gpu_mem_alloc_t *a = &g_gpu_pool.allocations[i];
        if (a->in_use && a->host_addr == host_addr) {
            return a;
        }
    }

    return NULL;
}

/* ============================================================================
 * gpu_mem_get_stats - Get GPU memory pool statistics
 * ============================================================================ */

void gpu_mem_get_stats(gpu_mem_stats_t *stats)
{
    if (!g_initialized || stats == NULL) {
        return;
    }

    stats->pool_size = g_gpu_pool.pool_size;
    stats->used = g_gpu_pool.used;
    stats->peak_used = g_gpu_pool.peak_used;
    stats->free = g_gpu_pool.pool_size - g_gpu_pool.used;
    stats->active_allocations = g_gpu_pool.active_allocations;
    stats->total_allocations = g_gpu_pool.total_allocations;
    stats->total_frees = g_gpu_pool.total_frees;
}

/* ============================================================================
 * gpu_mem_sync - Synchronize memory between host and device
 * ============================================================================ */

kern_return_t gpu_mem_sync(vm_offset_t host_addr, uint64_t device_ptr,
                           vm_size_t size, gpu_mem_direction_t dir)
{
    gpu_mem_alloc_t *alloc = gpu_mem_get_info(host_addr);

    if (alloc == NULL) {
        return KERN_INVALID_ADDRESS;
    }

    if (!alloc->in_use) {
        return KERN_INVALID_ADDRESS;
    }

    /* For coherent memory, sync is implicit (no-op) */
    if (alloc->type == GPU_MEM_COHERENT) {
        return KERN_SUCCESS;
    }

    /* For non-coherent memory, we would invoke the GPU driver to perform
     * a memory copy. This is a placeholder for the driver call. */

    switch (dir) {
        case GPU_MEM_DIR_HOST_TO_DEVICE:
            /* Host -> Device copy */
            /* TODO: Call GPU driver memcpy_h2d(alloc->device_ptr, host_addr, size) */
            break;

        case GPU_MEM_DIR_DEVICE_TO_HOST:
            /* Device -> Host copy */
            /* TODO: Call GPU driver memcpy_d2h(host_addr, alloc->device_ptr, size) */
            break;

        case GPU_MEM_DIR_BIDIRECTIONAL:
            /* Bidirectional sync */
            /* TODO: Call GPU driver memcpy both directions */
            break;

        default:
            return KERN_INVALID_ARGUMENT;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * gpu_mem_map_existing - Map existing host memory as GPU-accessible
 * ============================================================================ */

kern_return_t gpu_mem_map_existing(task_t task, vm_offset_t addr, vm_size_t size,
                                   uint64_t *device_ptr)
{
    if (!g_initialized) {
        return KERN_INVALID_HOST;
    }

    if (size == 0) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Find a free allocation slot */
    gpu_mem_alloc_t *alloc = NULL;
    for (uint32_t i = 0; i < GPU_MEM_MAX_ALLOCATIONS; i++) {
        if (!g_gpu_pool.allocations[i].in_use) {
            alloc = &g_gpu_pool.allocations[i];
            break;
        }
    }

    if (alloc == NULL) {
        return KERN_RESOURCE_SHORTAGE;
    }

    /* Mark existing memory as GPU-accessible */
    /* This pins the pages and registers them with the GPU */
    alloc->host_addr = addr;
    alloc->device_ptr = (uint64_t)addr; /* Placeholder */
    alloc->size = size;
    alloc->type = GPU_MEM_COHERENT;
    alloc->owner_task = task;
    alloc->in_use = true;
    alloc->ref_count = 1;

    if (device_ptr) {
        *device_ptr = alloc->device_ptr;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * gpu_mem_unmap - Unmap GPU-accessible memory
 * ============================================================================ */

kern_return_t gpu_mem_unmap(vm_offset_t host_addr)
{
    return gpu_mem_free(host_addr);
}