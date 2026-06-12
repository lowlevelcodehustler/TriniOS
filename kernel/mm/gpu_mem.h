/*
 * TriniOS - GPU Memory Manager
 *
 * Manages GPU-accessible memory for AI inference workloads.
 * Integrates with NVIDIA CUDA driver for pinned/coherent allocations.
 */

#ifndef GPU_MEM_H
#define GPU_MEM_H

#include <mach/mach.h>
#include <mach/vm_map.h>
#include <stdint.h>
#include <stdbool.h>

#define GPU_MEM_PAGE_SIZE         (4 * 1024 * 1024)
#define GPU_MEM_MAX_ALLOCATIONS   1024
#define GPU_MEM_DEFAULT_POOL_SIZE (1 * 1024 * 1024 * 1024)

typedef enum {
    GPU_MEM_PAGE_LOCKED,
    GPU_MEM_COHERENT,
    GPU_MEM_DEVICE,
    GPU_MEM_UNIFIED
} gpu_mem_type_t;

typedef struct {
    vm_offset_t       host_addr;
    uint64_t          device_ptr;
    vm_size_t         size;
    gpu_mem_type_t    type;
    task_t            owner_task;
    bool              in_use;
    uint32_t          ref_count;
} gpu_mem_alloc_t;

typedef struct {
    gpu_mem_alloc_t   allocations[GPU_MEM_MAX_ALLOCATIONS];
    vm_size_t         pool_size;
    vm_size_t         used;
    vm_size_t         peak_used;
    uint32_t          active_allocations;
    uint32_t          total_allocations;
    uint32_t          total_frees;
} gpu_mem_pool_t;

kern_return_t gpu_mem_init(vm_size_t pool_size);
void gpu_mem_shutdown(void);
kern_return_t gpu_mem_alloc(vm_offset_t *host_addr, uint64_t *device_ptr, vm_size_t size, gpu_mem_type_t type);
kern_return_t gpu_mem_free(vm_offset_t host_addr);
gpu_mem_alloc_t *gpu_mem_get_info(vm_offset_t host_addr);

typedef struct {
    vm_size_t   pool_size;
    vm_size_t   used;
    vm_size_t   peak_used;
    vm_size_t   free;
    uint32_t    active_allocations;
    uint32_t    total_allocations;
    uint32_t    total_frees;
} gpu_mem_stats_t;

void gpu_mem_get_stats(gpu_mem_stats_t *stats);

typedef enum {
    GPU_MEM_DIR_HOST_TO_DEVICE,
    GPU_MEM_DIR_DEVICE_TO_HOST,
    GPU_MEM_DIR_BIDIRECTIONAL
} gpu_mem_direction_t;

kern_return_t gpu_mem_sync(vm_offset_t host_addr, uint64_t device_ptr, vm_size_t size, gpu_mem_direction_t dir);
kern_return_t gpu_mem_map_existing(task_t task, vm_offset_t addr, vm_size_t size, uint64_t *device_ptr);
kern_return_t gpu_mem_unmap(vm_offset_t host_addr);

#endif /* GPU_MEM_H */
