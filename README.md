# TriniOS

> **AI-First Operating System** — Mach Microkernel-Based

TriniOS is a new operating system built on a Mach microkernel architecture, designed from the ground up with AI as a first-class citizen.

## Core Design Principles

1. **AI-First**: LLM operations are native to the kernel/userland boundary
2. **Local-First**: On-device inference, privacy, and data sovereignty
3. **Microkernel Architecture**: Minimal TCB, maximum modularity
4. **Open Source**: Fully transparent development
5. **Capital Efficient**: Leverages existing OSS ecosystems

## Project Structure

```
TriniOS/
├── kernel/           # Mach 3.0 microkernel source
├── kext/             # Kernel extensions
├── servers/          # User-space Mach servers
│   ├── bsd/          # BSD compatibility layer (POSIX)
│   ├── ai/           # LLM Inference Server
│   ├── fs/           # Filesystem servers (ext4, NTFS, exFAT)
│   ├── window/       # Window server (compositing, input)
│   ├── network/      # Network stack server (TCP/IP)
│   └── audio/        # Audio server
├── drivers/          # User-space device drivers
├── userland/         # Applications and utilities
│   ├── shell/        # AI-powered shell
│   ├── utils/        # Core utilities (ls, cp, mv, etc.)
│   ├── apps/         # End-user applications
│   └── dev/          # Development tools
├── boot/             # Bootloader (GRUB2)
├── toolchain/        # Cross-compilation toolchain
├── build/            # Build system configuration
├── docs/             # Documentation & ADRs
└── tests/            # Test suite
```

## Status

**Phase 0: Foundation Research & Environment Setup**

Build environment initialization in progress.

## License

Core: BSD 3-Clause | See individual components for details

---

*TriniOS — Building the AI-First Operating System.*