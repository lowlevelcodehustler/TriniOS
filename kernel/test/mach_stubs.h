/*
 * mach_stubs.h
 * TriniOS — Mach Type Stubs for Kernel-Space Self-Tests
 *
 * Provides minimal type definitions so Phase 2 headers compile
 * in freestanding kernel context without a real Mach runtime.
 *
 * IMPORTANT: This header is ONLY included by kernel_selftest.c.
 * It must be included BEFORE any Phase 2 headers to prevent
 * them from pulling in real Mach headers.
 */

#ifndef MACH_STUBS_H
#define MACH_STUBS_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Mach core types
 * ============================================================================ */

typedef int              kern_return_t;
typedef uint64_t         mach_port_t;
typedef uint64_t         task_t;
typedef uint64_t         thread_t;
typedef uint64_t         vm_offset_t;
typedef uint64_t         vm_size_t;
typedef uint32_t         vm_prot_t;
typedef uint32_t         vm_inherit_t;
typedef int              boolean_t;
typedef uint32_t         mach_msg_size_t;
typedef uint32_t         mach_msg_type_name_t;
typedef uint32_t         mach_msg_id_t;

/* Mach message header (simplified for self-tests) */
typedef struct {
    mach_port_t         msgh_remote_port;
    mach_port_t         msgh_local_port;
    mach_port_t         msgh_voucher_port;
    mach_msg_size_t     msgh_size;
    mach_port_t         msgh_id;
    uint32_t            msgh_bits;
} mach_msg_header_t;

typedef mach_msg_header_t mach_msg_t;

/* ============================================================================
 * Mach constants
 * ============================================================================ */

#define KERN_SUCCESS              0
#define KERN_INVALID_ADDRESS      1
#define KERN_INVALID_RIGHT        2
#define KERN_INVALID_VALUE        3
#define KERN_ACCESS_DENIED        4
#define KERN_RESOURCE_SHORTAGE    5
#define KERN_NOT_IMPLEMENTED      6

#define MACH_MSG_TYPE_MAKE_SEND   1
#define MACH_MSG_TYPE_COPY_SEND   2

#define VM_PROT_NONE              0x00
#define VM_PROT_READ              0x01
#define VM_PROT_WRITE             0x02
#define VM_PROT_EXECUTE           0x04

#define VM_INHERIT_COPY           0
#define VM_INHERIT_SHARE          1
#define VM_INHERIT_NONE           2

#define VM_FLAGS_ANYWHERE         0x1
#define VM_FLAGS_FIXED            0x0

#define TRUE    1
#define FALSE   0

/* ============================================================================
 * Mach function stubs (not callable, just prevent linker errors)
 * ============================================================================ */

static inline task_t mach_task_self(void) { return 1; }
static inline kern_return_t vm_allocate(task_t, vm_offset_t *, vm_size_t, boolean_t) { return KERN_SUCCESS; }
static inline kern_return_t vm_deallocate(task_t, vm_offset_t, vm_size_t) { return KERN_SUCCESS; }
static inline kern_return_t vm_protect(task_t, vm_offset_t, vm_size_t, boolean_t, vm_prot_t) { return KERN_SUCCESS; }

#define vm_page_size  4096

#endif /* MACH_STUBS_H */