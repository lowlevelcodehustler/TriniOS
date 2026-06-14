/*
 * TriniOS VM Manager Tests
 * ========================
 * Tests for vm_manager data structures and logic.
 * Mach types are mocked so these run on the build host.
 */

#include "../test_framework.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Mock Mach types (so we don't need mach/mach.h)
 * ============================================================================ */

typedef int    task_t;
typedef int    vm_map_t;
typedef uint64_t vm_offset_t;
typedef uint64_t vm_size_t;
typedef int    vm_prot_t;
typedef int    vm_inherit_t;

#define VM_PROT_READ    0x01
#define VM_PROT_WRITE   0x02
#define VM_PROT_EXECUTE 0x04
#define VM_INHERIT_COPY 0

/* ============================================================================
 * Inline the vm_manager data structures (extracted from vm_manager.h)
 * ============================================================================ */

typedef enum {
    VM_REGION_CODE,
    VM_REGION_DATA,
    VM_REGION_RODATA,
    VM_REGION_STACK,
    VM_REGION_HEAP,
    VM_REGION_SHARED,
    VM_REGION_DEVICE,
    VM_REGION_GPU,
    VM_REGION_AI_WEIGHTS,
    VM_REGION_AI_ACTIVATIONS
} vm_region_type_t;

typedef struct vm_region {
    vm_offset_t       start;
    vm_offset_t       end;
    vm_prot_t         protection;
    vm_inherit_t      inheritance;
    vm_region_type_t  type;
    task_t            owner;
    bool              is_pinned;
    bool              is_gpu_accessible;
    struct vm_region *next;
} vm_region_t;

typedef struct {
    task_t            task;
    vm_map_t          vm_map;
    vm_region_t      *regions;
    vm_offset_t       break_;
    vm_offset_t       stack_base;
    uint32_t          region_count;
    vm_size_t         total_mapped;
    vm_size_t         total_resident;
} vm_space_t;

typedef struct {
    vm_size_t  total_mapped;
    vm_size_t  total_resident;
    vm_size_t  total_pinned;
    vm_size_t  total_gpu_accessible;
    uint32_t   region_count;
} vm_stats_t;

/* ============================================================================
 * Helper functions (standalone, no Mach dependency)
 * ============================================================================ */

static vm_space_t *vm_space_create_mock(task_t task)
{
    vm_space_t *space = calloc(1, sizeof(vm_space_t));
    if (!space) return NULL;
    space->task = task;
    space->vm_map = (vm_map_t)task;
    space->break_ = 0;
    space->stack_base = 0;
    space->region_count = 0;
    space->total_mapped = 0;
    space->total_resident = 0;
    return space;
}

static vm_region_t *vm_region_create(vm_offset_t start, vm_offset_t end,
                                      vm_prot_t prot, vm_region_type_t type)
{
    vm_region_t *r = calloc(1, sizeof(vm_region_t));
    if (!r) return NULL;
    r->start = start;
    r->end = end;
    r->protection = prot;
    r->type = type;
    r->inheritance = VM_INHERIT_COPY;
    r->is_pinned = false;
    r->is_gpu_accessible = false;
    r->next = NULL;
    return r;
}

static void vm_region_add(vm_space_t *space, vm_region_t *r)
{
    r->next = space->regions;
    space->regions = r;
    space->region_count++;
    space->total_mapped += (r->end - r->start);
}

static void vm_space_destroy_mock(vm_space_t *space)
{
    if (!space) return;
    vm_region_t *r = space->regions;
    while (r) {
        vm_region_t *next = r->next;
        free(r);
        r = next;
    }
    free(space);
}

static vm_region_t *vm_find_region_mock(vm_space_t *space, vm_offset_t address)
{
    vm_region_t *r = space->regions;
    while (r) {
        if (address >= r->start && address < r->end)
            return r;
        r = r->next;
    }
    return NULL;
}

static void vm_get_stats_mock(vm_space_t *space, vm_stats_t *stats)
{
    memset(stats, 0, sizeof(vm_stats_t));
    stats->total_mapped = space->total_mapped;
    stats->total_resident = space->total_resident;
    stats->region_count = space->region_count;

    vm_region_t *r = space->regions;
    while (r) {
        if (r->is_pinned) stats->total_pinned += (r->end - r->start);
        if (r->is_gpu_accessible) stats->total_gpu_accessible += (r->end - r->start);
        r = r->next;
    }
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(space_create_destroy) {
    vm_space_t *space = vm_space_create_mock(42);
    TEST_ASSERT_NOT_NULL(space);
    TEST_ASSERT_EQ(space->task, 42);
    TEST_ASSERT_EQ(space->region_count, 0);
    TEST_ASSERT_EQ(space->total_mapped, 0ULL);
    vm_space_destroy_mock(space);
}

TEST(region_create_basic) {
    vm_region_t *r = vm_region_create(0x1000, 0x2000, VM_PROT_READ | VM_PROT_WRITE, VM_REGION_DATA);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQ(r->start, 0x1000ULL);
    TEST_ASSERT_EQ(r->end, 0x2000ULL);
    TEST_ASSERT_EQ(r->protection, VM_PROT_READ | VM_PROT_WRITE);
    TEST_ASSERT_EQ(r->type, VM_REGION_DATA);
    TEST_ASSERT_FALSE(r->is_pinned);
    TEST_ASSERT_FALSE(r->is_gpu_accessible);
    free(r);
}

TEST(add_region_to_space) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_t *r = vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE);
    vm_region_add(space, r);
    TEST_ASSERT_EQ(space->region_count, 1);
    TEST_ASSERT_EQ(space->total_mapped, 0x1000ULL);
    vm_space_destroy_mock(space);
}

TEST(add_multiple_regions) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_add(space, vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE));
    vm_region_add(space, vm_region_create(0x2000, 0x3000, VM_PROT_READ | VM_PROT_WRITE, VM_REGION_DATA));
    vm_region_add(space, vm_region_create(0x3000, 0x4000, VM_PROT_READ | VM_PROT_WRITE, VM_REGION_HEAP));
    TEST_ASSERT_EQ(space->region_count, 3);
    TEST_ASSERT_EQ(space->total_mapped, 0x3000ULL);
    vm_space_destroy_mock(space);
}

TEST(find_region_exact_start) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_add(space, vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE));
    vm_region_t *found = vm_find_region_mock(space, 0x1000);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQ(found->type, VM_REGION_CODE);
    vm_space_destroy_mock(space);
}

TEST(find_region_middle) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_add(space, vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE));
    vm_region_t *found = vm_find_region_mock(space, 0x1500);
    TEST_ASSERT_NOT_NULL(found);
    vm_space_destroy_mock(space);
}

TEST(find_region_just_before_end) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_add(space, vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE));
    vm_region_t *found = vm_find_region_mock(space, 0x1FFF);
    TEST_ASSERT_NOT_NULL(found);
    vm_space_destroy_mock(space);
}

TEST(find_region_at_end_returns_null) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_add(space, vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE));
    vm_region_t *found = vm_find_region_mock(space, 0x2000);
    TEST_ASSERT_NULL(found);
    vm_space_destroy_mock(space);
}

TEST(find_region_not_found) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_add(space, vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE));
    vm_region_t *found = vm_find_region_mock(space, 0x5000);
    TEST_ASSERT_NULL(found);
    vm_space_destroy_mock(space);
}

TEST(find_region_empty_space) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_t *found = vm_find_region_mock(space, 0x1000);
    TEST_ASSERT_NULL(found);
    vm_space_destroy_mock(space);
}

TEST(find_region_multiple_non_overlapping) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_add(space, vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE));
    vm_region_add(space, vm_region_create(0x3000, 0x4000, VM_PROT_READ | VM_PROT_WRITE, VM_REGION_DATA));
    vm_region_add(space, vm_region_create(0x5000, 0x6000, VM_PROT_READ, VM_REGION_STACK));
    TEST_ASSERT_EQ(vm_find_region_mock(space, 0x1500)->type, VM_REGION_CODE);
    TEST_ASSERT_EQ(vm_find_region_mock(space, 0x3500)->type, VM_REGION_DATA);
    TEST_ASSERT_EQ(vm_find_region_mock(space, 0x5500)->type, VM_REGION_STACK);
    TEST_ASSERT_NULL(vm_find_region_mock(space, 0x2500));
    vm_space_destroy_mock(space);
}

TEST(stats_empty_space) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_stats_t stats;
    vm_get_stats_mock(space, &stats);
    TEST_ASSERT_EQ(stats.region_count, 0);
    TEST_ASSERT_EQ(stats.total_mapped, 0ULL);
    TEST_ASSERT_EQ(stats.total_pinned, 0ULL);
    TEST_ASSERT_EQ(stats.total_gpu_accessible, 0ULL);
    vm_space_destroy_mock(space);
}

TEST(stats_with_regions) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_region_t *r1 = vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_CODE);
    vm_region_t *r2 = vm_region_create(0x2000, 0x4000, VM_PROT_READ | VM_PROT_WRITE, VM_REGION_DATA);
    vm_region_t *r3 = vm_region_create(0x4000, 0x6000, VM_PROT_READ, VM_REGION_AI_WEIGHTS);
    r3->is_pinned = true;
    r3->is_gpu_accessible = true;
    vm_region_add(space, r1);
    vm_region_add(space, r2);
    vm_region_add(space, r3);

    vm_stats_t stats;
    vm_get_stats_mock(space, &stats);
    TEST_ASSERT_EQ(stats.region_count, 3);
    TEST_ASSERT_EQ(stats.total_mapped, 0x5000ULL);
    TEST_ASSERT_EQ(stats.total_pinned, 0x2000ULL);
    TEST_ASSERT_EQ(stats.total_gpu_accessible, 0x2000ULL);
    vm_space_destroy_mock(space);
}

TEST(region_types_enum) {
    TEST_ASSERT_EQ(VM_REGION_CODE, 0);
    TEST_ASSERT_EQ(VM_REGION_DATA, 1);
    TEST_ASSERT_EQ(VM_REGION_STACK, 3);
    TEST_ASSERT_EQ(VM_REGION_HEAP, 4);
    TEST_ASSERT_EQ(VM_REGION_AI_WEIGHTS, 8);
    TEST_ASSERT_EQ(VM_REGION_AI_ACTIVATIONS, 9);
}

TEST(destroy_releases_all_regions) {
    vm_space_t *space = vm_space_create_mock(1);
    for (int i = 0; i < 10; i++) {
        vm_region_add(space, vm_region_create(
            (vm_offset_t)(0x1000 * i), (vm_offset_t)(0x1000 * (i + 1)),
            VM_PROT_READ, VM_REGION_DATA));
    }
    TEST_ASSERT_EQ(space->region_count, 10);
    vm_space_destroy_mock(space);
}

TEST(stack_region_convention) {
    vm_space_t *space = vm_space_create_mock(1);
    vm_offset_t stack_base = 0x7FFFF000;
    vm_region_t *stack = vm_region_create(stack_base - 0x800000, stack_base,
                                          VM_PROT_READ | VM_PROT_WRITE, VM_REGION_STACK);
    vm_region_add(space, stack);
    TEST_ASSERT_EQ(vm_find_region_mock(space, stack_base - 1)->type, VM_REGION_STACK);
    TEST_ASSERT_EQ(vm_find_region_mock(space, stack_base - 0x7FFFF)->type, VM_REGION_STACK);
    TEST_ASSERT_NULL(vm_find_region_mock(space, stack_base));
    vm_space_destroy_mock(space);
}

TEST(pin_and_gpu_flags) {
    vm_region_t *r = vm_region_create(0x1000, 0x2000, VM_PROT_READ, VM_REGION_AI_WEIGHTS);
    r->is_pinned = true;
    r->is_gpu_accessible = true;
    TEST_ASSERT_TRUE(r->is_pinned);
    TEST_ASSERT_TRUE(r->is_gpu_accessible);

    vm_space_t *space = vm_space_create_mock(1);
    vm_region_add(space, r);
    vm_stats_t stats;
    vm_get_stats_mock(space, &stats);
    TEST_ASSERT_EQ(stats.total_pinned, 0x1000ULL);
    TEST_ASSERT_EQ(stats.total_gpu_accessible, 0x1000ULL);
    vm_space_destroy_mock(space);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    printf("VM Manager Tests\n");
    printf("================\n");

    TEST_REGISTER(space_create_destroy);
    TEST_REGISTER(region_create_basic);
    TEST_REGISTER(add_region_to_space);
    TEST_REGISTER(add_multiple_regions);
    TEST_REGISTER(find_region_exact_start);
    TEST_REGISTER(find_region_middle);
    TEST_REGISTER(find_region_just_before_end);
    TEST_REGISTER(find_region_at_end_returns_null);
    TEST_REGISTER(find_region_not_found);
    TEST_REGISTER(find_region_empty_space);
    TEST_REGISTER(find_region_multiple_non_overlapping);
    TEST_REGISTER(stats_empty_space);
    TEST_REGISTER(stats_with_regions);
    TEST_REGISTER(region_types_enum);
    TEST_REGISTER(destroy_releases_all_regions);
    TEST_REGISTER(stack_region_convention);
    TEST_REGISTER(pin_and_gpu_flags);

    test_run_all();
    return test_summary();
}