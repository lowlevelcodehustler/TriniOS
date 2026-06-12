# BSD Compatibility Server

## Overview

The BSD server provides POSIX-compatible system calls on top of the Mach microkernel. It runs as a user-space server that translates traditional UNIX semantics (fork, exec, pipe, signals) into Mach IPC messages.

## Architecture

```
+------------------+     IPC      +------------------+
|  User-space apps | <----------> |   BSD Server     |
+------------------+             +------------------+
                                      |
                                      | Mach IPC
                                      v
                               +-------------+
                               |  Mach Kernel |
                               +-------------+
```

## Implemented System Calls

### Process Management
- `fork()` - Task duplication via `task_create` + `vm_copy`
- `execve()` - Load ELF binary into current task
- `waitpid()` - Wait for task termination
- `exit()` - Terminate current task
- `kill()` - Signal delivery via Mach interrupts

### File I/O
- `open()` / `close()` - File descriptor management
- `read()` / `write()` - Data transfer via port sets
- `pipe()` - Pipe creation via Mach message channels

### Signals
- POSIX signal handling via Mach thread interrupts

## Directory Structure

```
servers/bsd/
├── include/          # BSD server headers
│   ├── bsd_server.h  # Main server interface
│   ├── posix.h       # POSIX syscall definitions
│   └── elf.h         # ELF binary loading
├── src/              # BSD server implementation
│   ├── main.c        # Server entry point
│   ├── process.c     # fork/exec/wait/exit
│   ├── file.c        # open/close/read/write
│   ├── pipe.c        # pipe implementation
│   └── signal.c      # Signal handling
├── CMakeLists.txt    # Build configuration
└── README.md
```

## Build Configuration

```bash
cmake -S servers/bsd -B build/bsd -DCMAKE_BUILD_TYPE=Release
cmake --build build/bsd
```

## Dependencies

- Mach headers (from GNU Mach build)
- Mach IPC runtime library