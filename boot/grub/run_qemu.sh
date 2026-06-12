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
#   -n    No initrd (kernel only)
#   -s    Stop at startup, wait for GDB connection
#   -h    Show help
#
# ──────────────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# ── Defaults ────────────────────────────────────────────────────────────
DEBUG=0
NO_INITRD=0
GDB_STOP=0
QEMU_ARCH="x86_64"
QEMU_MACHINE="q35"
QEMU_CPU="max"
QEMU_MEM="512M"
QEMU_SMP="1"

KERNEL_BIN="$ROOT_DIR/build/kernel/trini-kernel.bin"
INITRD_IMG="$ROOT_DIR/boot/initrd/output/initrd.img"
GRUB_CFG="$SCRIPT_DIR/grub.cfg"

# ── QEMU binary detection ──────────────────────────────────────────────
QEMU_BIN="qemu-system-x86_64"
if ! command -v "$QEMU_BIN" &>/dev/null; then
    echo "[ERROR] $QEMU_BIN not found. Install qemu-system-x86."
    exit 1
fi

# ── Parse arguments ────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 [-d] [-n] [-s] [-h]"
    echo "  -d    Enable debug (serial + gdb)"
    echo "  -n    No initrd (kernel only)"
    echo "  -s    Stop at startup (wait for GDB)"
    echo "  -h    Show this help"
    exit 0
}

while getopts "dnsh" opt; do
    case "$opt" in
        d) DEBUG=1 ;;
        n) NO_INITRD=1 ;;
        s) GDB_STOP=1 ;;
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

if [ "$NO_INITRD" -eq 0 ] && [ ! -f "$INITRD_IMG" ]; then
    echo "[ERROR] Initrd not found: $INITRD_IMG"
    echo "       Run 'make -C boot/initrd all' first."
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

# Use GRUB2 kernel directly via -kernel (simpler than building ISO)
QEMU_ARGS+=(
    -kernel "$KERNEL_BIN"
)

# Add initrd if requested and available
if [ "$NO_INITRD" -eq 0 ] && [ -f "$INITRD_IMG" ]; then
    QEMU_ARGS+=(
        -initrd "$INITRD_IMG"
    )
    echo "[*] Loading initrd: $INITRD_IMG"
fi

# Serial console
QEMU_ARGS+=(
    -serial stdio
    -display curses
)

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
echo "║  Kernel:  $KERNEL_BIN"
[ "$NO_INITRD" -eq 0 ] && echo "║  Initrd:  $INITRD_IMG" || echo "║  Initrd:  (disabled)"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# ── Launch QEMU ────────────────────────────────────────────────────────
exec "${QEMU_ARGS[@]}"