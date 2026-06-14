# TriniOS Kernel

## Overview

The TriniOS kernel is built on GNU Mach microkernel with extensions for AI-first computing. The kernel provides:
- Mach IPC (Inter-Process Communication)
- Virtual memory management
- Thread scheduling
- AI Service Interface (ASI) - making AI a first-class system primitive
- Device driver framework (kext)

## Components

### GNU Mach (kernel/gnumach/)
Base microkernel providing task/thread management, VM, and IPC.

### IPC Subsystem (kernel/ipc/)
Server registration, discovery, and port lifecycle management:
- `ipc_port.h/c` - Port management, server registry, message routing
- Server types: BSD, AI, FS, Network, Window, Audio, Driver
- Request tracking with timeouts
- Port sets for multiplexing server replies

### Task & Thread Management (kernel/task/)
Process lifecycle and thread management:
- `task_manager.h/c` - Task creation/destruction, PID management
- Thread creation, priority scheduling
- Exception handling
- Process group and session management

### Host Local Kernel (kernel/host/)
Machine-specific operations and system initialization:
- `host_local.h/c` - Bootstrap, hardware detection, system info
- CPU feature detection (AES-NI, SSE4, AVX)
- Memory map management
- System time (monotonic, wall clock)
- Kernel logging (klog with levels)
- System control (reboot, halt, panic)

### Memory Management (kernel/mm/)
Extended memory management for AI workload optimization:
- `vm_manager.h/c` - VM space management, region allocation
- `gpu_mem.h/c` - GPU memory pool for AI inference
- Page pinning for model weights
- AI-specific memory classes (weights, activations)

### AI Service Interface (kernel/asi/)
System-level AI inference interface:
- `asi.h/c` - IPC message format for AI requests
- Model-agnostic inference API
- Cryptographic output signing
- Audit trail support
- Streaming and batch inference modes
- GPU acceleration support

## Directory Structure

```
kernel/
├── gnumach/          # GNU Mach source (git submodule)
├── ipc/              # IPC subsystem
│   ├── ipc_port.h    # Port management header
│   └── ipc_port.c    # Port management implementation
├── task/             # Task & thread management
│   ├── task_manager.h    # Task management header
│   └── task_manager.c    # Task management implementation
├── host/             # Host local kernel
│   ├── host_local.h  # Host operations header
│   └── host_local.c  # Host operations implementation
├── mm/               # Memory management
│   ├── vm_manager.h  # VM management header
│   ├── vm_manager.c  # VM management implementation
│   ├── gpu_mem.h     # GPU memory header
│   └── gpu_mem.c     # GPU memory implementation
├── asi/              # AI Service Interface
│   ├── asi.h         # ASI header
│   └── asi.c         # ASI client implementation
└── README.md
```

## Bootstrap Sequence

```
host_bootstrap()
    ├── host_detect_hardware()
    │   ├── Detect CPU count, type, features
    │   ├── Detect physical memory
    │   └── Detect CPU features (cpuid)
    │
    └── host_init_subsystems()
        ├── ipc_port_init()        # Server registry
        ├── task_manager_init()    # Task/Thread management
        └── gpu_mem_init()         # GPU memory pool (optional)
```

## Key Design Decisions

### AI as a System Primitive
AI inference is not an application - it's a system service exposed via Mach IPC.
Any component (shell, filesystem, network) can query the AI service natively.

```
[Process] → [IPC Port: AI_SERVICE] → [LLM Inference Server] → [Response]
```

### Model-Agnostic Interface
The ASI does not assume any specific model architecture. Swapping models
is a configuration change, not a code change.

### Server Registration
All user-space servers register with the IPC subsystem, enabling:
- Dynamic server discovery
- Crash detection and recovery
- Reference counting for lifecycle management
- Notification on server failures

## Build

```bash
cd kernel/gnumach
./bootstrap && autoreconf -fi
./configure --target=x86_64 --enable-kdb
make
```

## Phase 2 Status

| Component | Status | Files |
|-----------|--------|-------|
| IPC Subsystem | ✅ Complete | ipc_port.h/c |
| Task Management | ✅ Complete | task_manager.h/c |
| Host Local Kernel | ✅ Complete | host_local.h/c |
| Memory Management | ✅ Complete | vm_manager.h/c, gpu_mem.h/c |
| AI Service Interface | ✅ Complete | asi.h/c |