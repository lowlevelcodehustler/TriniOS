# Initial RAM Disk (initrd)

## Overview

The initrd provides a minimal root filesystem that is loaded into RAM by the bootloader (GRUB2) and mounted as the initial rootfs by the Mach kernel. This enables the first user-space processes to start before any persistent storage is available.

## Architecture

```
GRUB2
  ├─→ Loads Mach kernel
  └─→ Loads initrd.img (cpio archive)
       ↓
Mach Kernel
  ├─→ Unpacks initrd to tmpfs/ramfs
  └─→ Executes /sbin/init
       ↓
/init (PID 1)
  ├─→ Mounts /dev, /proc, /sys
  ├─→ Starts BSD compatibility server
  └─→ Launches shell
```

## Directory Structure

```
initrd/
├── bin/          # Essential user binaries
├── sbin/         # System binaries (init, etc.)
├── lib/          # Shared libraries
├── etc/          # Configuration files
├── dev/          # Device nodes (created by init)
├── tmp/          # Temporary filesystem
├── proc/         # Process info (pseudo-filesystem)
├── sys/          # System info (pseudo-filesystem)
└── var/          # Variable data (logs, etc.)
```

## Build

```bash
cd boot/initrd
make initrd.img
```

This produces `initrd.img` - a gzip-compressed cpio archive ready for GRUB2.

## Format

Standard cpio_newc format, gzip compressed:
- Compatible with GRUB2's `initrd12` module
- Kernel unpacks directly to RAM-backed tmpfs