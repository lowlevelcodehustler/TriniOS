#!/bin/bash
# ──────────────────────────────────────────────────────────────────────────
# smoke_test.sh — Headless boot smoke test for TriniOS in QEMU
# ──────────────────────────────────────────────────────────────────────────
# Boots the ISO headlessly, captures serial output (COM1) to a log file,
# and checks for expected kernel markers. Exits non-zero on any failure so
# it can be wired into CI later.
#
# Usage: ./smoke_test.sh [-t <timeout_seconds>]
#   -t  QEMU run time before the log is evaluated (default: 60). The kernel
#       idles in ring 3 after boot, so QEMU never exits on its own; it is
#       killed at the timeout and whatever serial output was captured is
#       then checked.
#
# Serial log: smoke-serial.log next to this script (matches *.log in
# .gitignore). Kept after every run — inspect it when the test fails.
#
# Build order (NOTE: 'make -C boot/grub all' builds ONLY the kernel binary;
# the ISO is produced by run_qemu.sh, so a successful make does NOT refresh
# it — an unrefreshed ISO silently tests a previous build):
#   make -C boot/initrd all      # hello.elf + initrd.img (cpio_newc content)
#   make -C boot/grub all        # kernel binary only
#   ./run_qemu.sh -b             # rebuilds the ISO from current artifacts
#
# Assumptions: standard ISO built by ./run_qemu.sh -b (kernel + initrd,
# default GRUB entry boots WITH initrd).
#
# Exit codes:
#   0  all markers found
#   1  environment error (missing qemu / missing ISO)
#   2  GRUB-level failure detected in serial output
#   3  kernel never started, or a required marker is missing (hang/crash)
# ──────────────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

TIMEOUT=60
ISO_FILE="$ROOT_DIR/build/kernel/trini-os.iso"
LOG_FILE="$SCRIPT_DIR/smoke-serial.log"

usage() {
    echo "Usage: $0 [-t <timeout_seconds>]"
    echo "  -t  QEMU run time before evaluating the serial log (default 60)"
    exit 1
}

while getopts "t:h" opt; do
    case "$opt" in
        t) TIMEOUT="$OPTARG" ;;
        h) usage ;;
        *) usage ;;
    esac
done

# ── Prerequisites ────────────────────────────────────────────────────────
if ! command -v qemu-system-x86_64 &>/dev/null; then
    echo "[ERROR] qemu-system-x86_64 not found. Install qemu-system-x86."
    exit 1
fi

if [ ! -f "$ISO_FILE" ]; then
    echo "[ERROR] ISO not found: $ISO_FILE"
    echo "       Build it first: ./run_qemu.sh -b   (make -C boot/grub all only builds the kernel)"
    exit 1
fi

# ── Staleness guard: an ISO older than the kernel binary is a previous
# ── build — booting it tests nothing about the current tree.
KERNEL_BIN="$ROOT_DIR/build/kernel/trini-kernel.bin"
if [ -f "$KERNEL_BIN" ] && [ "$ISO_FILE" -ot "$KERNEL_BIN" ]; then
    echo "[ERROR] ISO is older than $KERNEL_BIN — it is a stale image."
    echo "        Rebuild the ISO first: ./run_qemu.sh -b   (then re-run this test)"
    exit 1
fi

# ── Boot headlessly, capture serial ──────────────────────────────────────
echo "[*] Booting $ISO_FILE (evaluating after ${TIMEOUT}s)..."
qemu-system-x86_64 \
    -machine q35 -cpu max -m 512M -smp 1 \
    -boot d -cdrom "$ISO_FILE" \
    -display none -serial "file:$LOG_FILE" &
QEMU_PID=$!

# Run until QEMU exits on its own or the timeout elapses.
DEADLINE=$((SECONDS + TIMEOUT))
while kill -0 "$QEMU_PID" 2>/dev/null; do
    if [ "$SECONDS" -ge "$DEADLINE" ]; then
        echo "[*] Timeout reached — stopping QEMU and evaluating serial log."
        kill "$QEMU_PID" 2>/dev/null || true
        break
    fi
    sleep 1
done
wait "$QEMU_PID" 2>/dev/null || true

if [ ! -s "$LOG_FILE" ]; then
    echo "[FAIL] No serial output captured at all ($LOG_FILE is empty)."
    echo "       QEMU/GRUB produced nothing on COM1 — check the ISO build."
    exit 3
fi

echo "[*] Serial log: $LOG_FILE ($(wc -l < "$LOG_FILE") lines)"

# ── Failure mode: GRUB-level error before kernel start ───────────────────
if grep -q "^error:" "$LOG_FILE"; then
    echo "[FAIL] GRUB-level error detected in serial output:"
    grep "^error:" "$LOG_FILE" | head -5
    exit 2
fi

# ── Required markers, in order of boot progress ──────────────────────────
MARKERS=(
    "TriniOS Kernel"          # banner — kernel_main reached, serial works
    "Boot info at 0x"         # multiboot2 info structure parsed (G1)
    "Memory: lower="          # BasicMeminfo tag present in boot info
    "Initrd at 0x"            # initrd module tag found (G2 input)
    "Entering ring 3 at 0x"   # final transition reached (G2)
)

MISSING=()
for m in "${MARKERS[@]}"; do
    if grep -qF "$m" "$LOG_FILE"; then
        echo "[ OK ] $m"
    else
        echo "[MISS] $m"
        MISSING+=("$m")
    fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
    echo ""
    echo "[FAIL] ${#MISSING[@]} marker(s) missing. Last serial lines:"
    tail -15 "$LOG_FILE"
    if ! grep -qF "TriniOS Kernel" "$LOG_FILE"; then
        echo ""
        echo "       (kernel never started — no banner in log, and no GRUB error visible)"
    fi
    exit 3
fi

echo ""
echo "[PASS] All boot markers present — G1/G2 boot chain verified."
exit 0
