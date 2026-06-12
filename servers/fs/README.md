# servers/fs — Filesystem Servers

TriniOS filesystem servers run as user-space Mach servers, following the microkernel philosophy. Each filesystem is an isolated process that communicates with the BSD compatibility layer via IPC.

## Architecture

```
[Process: open/read/write]
    ↓ IPC
[BSD Layer: VFS Interface]
    ↓ IPC
[Filesystem Server: ext4 / NTFS / exFAT]
    ↓ IPC
[Block Device Driver]
    ↓ IPC
[Storage Controller Driver: AHCI/NVMe]
    ↓
[Hardware: Disk/SSD/NVMe]
```

This design provides:
- **Isolation**: A filesystem driver crash doesn't crash the kernel
- **Flexibility**: New filesystems added without kernel modification
- **Security**: Each server runs with minimal privileges

## ext4 Driver (Phase 1)

### Files

| File | Description |
|------|-------------|
| `ext4.h` | On-disk structures, constants, runtime types, API declarations |
| `ext4_bdev.c` | Block device abstraction layer with memory-backed device support |
| `ext4_mount.c` | Superblock + group descriptor reading, mount/unmount |
| `ext4_inode.c` | Inode reading, logical-to-physical block mapping (extents + traditional) |
| `ext4_lookup.c` | Path resolution, directory entry scanning |
| `ext4_file.c` | Open, close, read, write, lseek, stat, unlink, rename |
| `ext4_dir.c` | opendir, readdir, closedir, mkdir, rmdir |
| `ext4_journal.c` | Journal detection, state checking, light fsck |
| `ext4_server.c` | Main server entry, VFS IPC interface, handle management |

### Features (Phase 1)

- ✅ Read superblock and group descriptor table
- ✅ Read inodes (variable size, 128-256 bytes)
- ✅ Extent tree block mapping (depth 0-4)
- ✅ Traditional indirect block mapping (direct/single/double/triple)
- ✅ Path lookup with component walking
- ✅ File read with hole zero-fill
- ✅ Directory iteration
- ✅ Journal presence detection and state checking
- ✅ Light filesystem consistency check
- ✅ 64-bit filesystem support
- ✅ Block cache for metadata reads
- ⏳ Write support (Phase 2)
- ⏳ Journal recovery (Phase 2)
- ⏳ Block allocation (Phase 2)
- ⏳ File creation/deletion (Phase 2)

### Build

```bash
cd servers/fs
make
```

### Interactive Testing

```bash
make run
# Then:
mount /path/to/disk.img /mnt
ls /mnt /
stat /mnt /etc/passwd
fsck /mnt
dump_sb /mnt
quit
```

## Future Filesystems

- NTFS driver (Phase 5)
- exFAT driver (Phase 5)
- FAT32 driver (Phase 5)
- APFS read support (Phase 5)
- ISO9660 read support (Phase 5)

## VFS Integration

The BSD compatibility layer routes file operations through a VFS abstraction. Each operation is dispatched to the appropriate filesystem server via Mach IPC. The message types are defined in `ext4_server.c` and will be standardized across all filesystem servers.