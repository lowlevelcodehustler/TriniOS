# TriniOS Kernel

## Overview

The TriniOS kernel is built on GNU Mach microkernel with extensions for AI-first computing. The kernel provides:
- Mach IPC (Inter-Process Communication)
- Virtual memory management
- Thread scheduling
- Device driver framework (kext)

## Components

### GNU Mach (kernel/gnumach/)
Base microkernel providing task/thread management, VM, and IPC.

### Memory Management (kernel/mm/)
Extended memory management for AI workload optimization:
- Page allocation and deallocation
- Virtual memory mapping
- GPU memory management (for AI inference)
- Page caching and buffering

## Directory Structure

```
kernel/
├── gnumach/          # GNU Mach source (git submodule)
├── mm/               # Memory management extensions
│   ├── vm_manager.c  # VM space management
│   ├── page.c        # Page allocation
│   └── gpu_mem.c     # GPU memory management
├── CMakeLists.txt    # Kernel build configuration
└── README.md
```

## Build

```bash
cd kernel/gnumach
./bootstrap && autoreconf -fi
./configure --target=x86_64 --enable-kdb
make