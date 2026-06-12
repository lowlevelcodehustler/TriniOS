# TriniOS Boot System

## Overview

The boot directory contains all components needed to boot TriniOS:
- Bootloader configuration (GRUB2)
- Initial RAM disk (initrd) with minimal rootfs
- Kernel boot parameters

## Components

### GRUB2 Bootloader (`grub/`)

GRUB2 is the recommended bootloader for Phase 1 (x86_64 development). It provides:
- Multiboot2 support for Mach kernel loading
- initrd loading via `initrd12` module
- Menu-based boot selection
- Filesystem support (ext4, FAT, ISO9660)

### Initial RAM Disk (`initrd/`)

The initrd provides a minimal root filesystem loaded into RAM before any persistent storage is available.

**Contents:**
- `/sbin/init` - Initial process (PID 1) that bootstraps the system
- `/sbin/bsd_server` - BSD compatibility server binary (linked from servers/bsd/)
- `/bin/sh` - Shell binary (linked from userland/shell/)
- `/etc/profile` - Shell environment configuration
- `/etc/fstab` - Filesystem table
- `/etc/inittab` - Init configuration

**Build:**
```bash
cd boot/initrd
make all
```

This produces `output/initrd.img` - a gzip-compressed cpio_newc archive.

## Boot Sequence

```
Power On / Reset
    ↓
GRUB2 (from MBR/EFI)
    ├─→ Load Mach kernel
    ├─→ Load initrd.img
    └─→ Transfer control to kernel entry point
    ↓
Mach Kernel (entry point)
    ├─→ Parse multiboot2 boot info
    ├─→ Initialize VM subsystem
    ├─→ Call initrd_init() with initrd address/size
    ├─→ Call initrd_unpack() to parse cpio archive
    ├─→ Call initrd_mount() to register as rootfs
    └─→ Call initrd_free_image() to release raw buffer
    ↓
Create first task (/sbin/init)
    ↓
init (PID 1)
    ├─→ Set up signal handlers (SIGHUP, SIGCHLD)
    ├─→ Create mount points (/dev, /proc, /sys, /tmp, /var)
    ├─→ Create device nodes (/dev/null, /dev/zero, /dev/console, etc.)
    ├─→ Fork + exec bsd_server
    ├─→ Fork + exec /bin/sh
    └─→ Wait for shell, restart on exit
    ↓
BSD Server starts accepting IPC requests
    ↓
Shell prompt appears
```

## Kernel Integration

The initrd loader (`kernel/mm/initrd_loader.c`) provides these API functions:

| Function | Description |
|---|---|
| `initrd_init(addr, size)` | Record initrd location from multiboot2 boot info |
| `initrd_unpack()` | Parse cpio archive into in-memory file tree |
| `initrd_mount()` | Register as root filesystem |
| `initrd_get_root()` | Access the unpacked filesystem tree |
| `initrd_is_loaded()` | Check if initrd was successfully loaded |
| `initrd_free_image()` | Release raw initrd buffer after unpacking |
| `initrd_dump_tree()` | Debug: print filesystem tree |
| `initrd_cleanup()` | Free all initrd resources |

## File Format

The initrd uses **cpio_newc** (CRC format) compressed with **gzip**:
- Header: 110 bytes per entry (hex-encoded fields)
- Magic: `070701`
- Trailer: `TRAILER!!!`
- Alignment: 4-byte boundary after each entry's data

## Memory Layout

```
0x00000000 - 0x0009FFFF  (640KB)  BIOS Memory Area
0x000A0000 - 0x000BFFFF  (128KB)  Video/EGA Buffer
0x000C0000 - 0x000FFFFF  (384KB)  ROM Area
0x00100000 - 0x002FFFFF  (2MB)    Kernel Load Address (Mach)
0x00300000 - 0x003FFFFF  (1MB)    Boot Info / Multiboot Headers
0x00400000 - 0x????????  (Dynamic) Initrd Buffer (GRUB2-loaded)
0x???????? - 0xFFFFFFFF  (Rest)   Available for Kernel + RAM Disk