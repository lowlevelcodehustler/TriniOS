#!/bin/bash
# ──────────────────────────────────────────────────────────────────────────
# run_qemu.sh — Launch TriniOS in QEMU for testing
# ──────────────────────────────────────────────────────────────────────────
#
# Usage:
#   ./run_qemu.sh [options]
#
# Options:
#   -d    Enable debug mode (serial console + gdb stub)
#   -n    No initrd (kernel only, no ISO rebuild)
#   -s    Stop at startup, wait for GDB connection
#   -b    Build ISO before launching
#   -h    Show help
#
# ──────────────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# ── Defaults ────────────────────────────────────────────────────────────
DEBUG=0
GDB_STOP=0
BUILD_ISO=0
QEMU_ARCH="x86_64"
QEMU_MACHINE="q35"
QEMU_CPU="max"
QEMU_MEM="512M"
QEMU_SMP="1"

KERNEL_BIN="$ROOT_DIR/build/kernel/trini-kernel.bin"
INITRD_IMG="$ROOT_DIR/boot/initrd/output/initrd.img"
ISO_FILE="$ROOT_DIR/build/kernel/trini-os.iso"
GRUB_CFG="$SCRIPT_DIR/grub.cfg"

# ── QEMU binary detection ──────────────────────────────────────────────
QEMU_BIN="qemu-system-x86_64"
if ! command -v "$QEMU_BIN" &>/dev/null; then
    echo "[ERROR] $QEMU_BIN not found. Install qemu-system-x86."
    exit 1
fi

# ── Parse arguments ────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 [-d] [-n] [-s] [-b] [-f <logfile>] [-h]"
    echo "  -d    Enable debug (serial + gdb)"
    echo "  -n    No initrd (kernel only, no ISO rebuild)"
    echo "  -s    Stop at startup (wait for GDB)"
    echo "  -b    Build ISO before launching"
    echo "  -f    Capture serial output to file instead of curses display"
    echo "  -h    Show this help"
    exit 0
}

SERIAL_LOG=""

while getopts "dnshbf:" opt; do
    case "$opt" in
        d) DEBUG=1 ;;
        n) BUILD_ISO=0 ;;   # Don't build ISO (use existing artifacts with -kernel fallback)
        s) GDB_STOP=1 ;;
        b) BUILD_ISO=1 ;;
        f) SERIAL_LOG="$OPTARG" ;;
        h) usage ;;
        *) usage ;;
    esac
done

# ── Verify build artifacts ─────────────────────────────────────────────
if [ ! -f "$KERNEL_BIN" ]; then
    echo "[ERROR] Kernel not found: $KERNEL_BIN"
    echo "       Run 'make -C boot/grub all' or 'make kernel' first."
    exit 1
fi

# ── Build ISO if needed ────────────────────────────────────────────────
if [ "$BUILD_ISO" -eq 1 ] || [ ! -f "$ISO_FILE" ]; then
    echo "[*] Building GRUB2 ISO..."

    # Ensure kernel and initrd are in place for ISO build
    if [ ! -f "$INITRD_IMG" ]; then
        echo "[ERROR] Initrd not found: $INITRD_IMG"
        echo "       Run 'make -C boot/initrd all' first."
        exit 1
    fi

    # Create a clean build directory for the ISO with correct file layout:
    #   /boot/grub/grub.cfg       — GRUB config
    #   /boot/trini-kernel.bin    — kernel binary (multiboot2)
    #   /boot/initrd.img          — initrd CPIO archive
    BUILD_DIR="$SCRIPT_DIR/build_iso"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR/boot/grub"

    # Copy grub.cfg into build dir at correct path
    cp "$GRUB_CFG" "$BUILD_DIR/boot/grub/grub.cfg"

    # Copy kernel and initrd to /boot/ in the ISO
    cp "$KERNEL_BIN" "$BUILD_DIR/boot/trini-kernel.bin"
    cp "$INITRD_IMG" "$BUILD_DIR/boot/initrd.img"

    # Copy GRUB modules from iso/boot/grub (i386-pc, fonts, etc.)
    if [ -d "$SCRIPT_DIR/iso/boot/grub" ]; then
        cp -r "$SCRIPT_DIR/iso/boot/grub"/* "$BUILD_DIR/boot/grub/"
    fi

    # Build ISO using grub-mkrescue (hybrid EFI+BIOS) from clean build dir
    if command -v grub-mkrescue &>/dev/null; then
        cd "$BUILD_DIR"
        grub-mkrescue -o "$ISO_FILE" . 2>&1 | tail -5
        echo "[*] ISO built: $ISO_FILE ($(du -h "$ISO_FILE" | cut -f1))"
    else
        # Fallback: use mkisofs with eltorito boot image from build dir
        cd "$BUILD_DIR"
        mkisofs -o "$ISO_FILE" \
            -V "TRINIOS" \
            -no-emul-boot \
            -boot-load-size 4 \
            -boot-info-table \
            -eltorito-boot boot/grub/i386-pc/core.img \
            -eltorito-platform bios \
            . 2>&1
        echo "[*] ISO built (mkisofs): $ISO_FILE ($(du -h "$ISO_FILE" | cut -f1))"
    fi

    # Cleanup build dir
    rm -rf "$BUILD_DIR"
fi

if [ ! -f "$ISO_FILE" ]; then
    echo "[ERROR] ISO not found: $ISO_FILE"
    exit 1
fi

# ── Build QEMU command ─────────────────────────────────────────────────
QEMU_ARGS=(
    "$QEMU_BIN"
    -machine "$QEMU_MACHINE"
    -cpu "$QEMU_CPU"
    -m "$QEMU_MEM"
    -smp "$QEMU_SMP"
)

# Boot from CD-ROM (ISO image with GRUB2)
QEMU_ARGS+=(
    -boot d
    -cdrom "$ISO_FILE"
)

echo "[*] Booting from ISO: $ISO_FILE"

# Display configuration
if [ -n "$SERIAL_LOG" ]; then
    # Headless mode: no display; capture the serial console (COM1) to a file.
    # The kernel's boot messages, initrd unpacking and self-tests all go out
    # on the serial port, so this is what you inspect after QEMU exits.
    # (VGA-only output such as hello's banner needs interactive curses mode.)
    QEMU_ARGS+=(
        -display none
        -serial "file:$SERIAL_LOG"
    )
    echo "[*] Headless mode — serial output captured to: $SERIAL_LOG"
    echo "    (VGA-only output such as hello's banner needs interactive mode:"
    echo "     ./run_qemu.sh)"
else
    # Interactive curses display with VGA text mode
    QEMU_ARGS+=(
        -display curses
        -serial null
    )
fi

# Debug options
if [ "$DEBUG" -eq 1 ]; then
    QEMU_ARGS+=(
        -d guest_errors,int,cpu_reset
    )
    echo "[*] Debug mode enabled"
fi

if [ "$GDB_STOP" -eq 1 ]; then
    QEMU_ARGS+=(
        -S
        -s  # equivalent to -gdb tcp::1234
    )
    echo "[*] Waiting for GDB connection on port 1234..."
fi

# ── Header ─────────────────────────────────────────────────────────────
echo "╔══════════════════════════════════════════════════════════╗"
echo "║              TriniOS QEMU Launch                        ║"
echo "╠══════════════════════════════════════════════════════════╣"
echo "║  Arch:    $QEMU_ARCH"
echo "║  Machine: $QEMU_MACHINE"
echo "║  CPU:     $QEMU_CPU"
echo "║  Memory:  $QEMU_MEM"
echo "║  SMP:     $QEMU_SMP"
echo "║  ISO:     $ISO_FILE"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# ── Launch QEMU ────────────────────────────────────────────────────────
exec "${QEMU_ARGS[@]}"