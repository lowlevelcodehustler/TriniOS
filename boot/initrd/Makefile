# ──────────────────────────────────────────────────────────────────────────
# Makefile — TriniOS Initial RAM Disk Builder
# ──────────────────────────────────────────────────────────────────────────
#
# Builds the initrd image from:
#   1. User-space programs (compiled from src/*.c)
#   2. rootfs directory tree
#
# Produces a cpio_newc archive (not gzip tar — required for initrd_lookup).
# The cpio format is expected by both the kernel initrd loader and the
# BSD server's user-space initrd_lookup module.
#
# ──────────────────────────────────────────────────────────────────────────

SHELL       := /bin/bash
ROOTFS      := rootfs
SRC_DIR     := src
OUTPUT_DIR  := output
# cpio_newc content, .img name: run_qemu.sh and grub.cfg both expect
# initrd.img (GRUB loads it as an opaque module — the extension is irrelevant).
INITRD_IMG  := $(OUTPUT_DIR)/initrd.img

# ── Toolchain for user-space programs ───────────────────────────────────
CC          := x86_64-linux-gnu-gcc
OBJCOPY     := x86_64-linux-gnu-objcopy

# ── Phony targets ───────────────────────────────────────────────────────
.PHONY: all clean help user-space rootfs

# ── Default target ──────────────────────────────────────────────────────
all: user-space $(INITRD_IMG)
	@echo "[INITRD] Build complete: $(INITRD_IMG)"

# ── Build user-space programs first ─────────────────────────────────────
user-space:
	$(MAKE) -C $(SRC_DIR) all

# ── Copy compiled binaries into rootfs ──────────────────────────────────
rootfs: user-space
	# Ship the ELF itself (not the flat objcopy output): the kernel's
	# elf_loader needs a real ELF64 header + PT_LOAD segments to load it.
	@if [ -f $(SRC_DIR)/../output/binaries/hello.elf ]; then \
		cp $(SRC_DIR)/../output/binaries/hello.elf $(ROOTFS)/bin/hello; \
		chmod +x $(ROOTFS)/bin/hello; \
		echo "[INITRD] Copied hello.elf to rootfs/bin/hello"; \
	else \
		echo "[INITRD] WARNING: hello.elf not found, skipping"; \
	fi
	@if [ -f $(SRC_DIR)/../output/binaries/cat.bin ]; then \
		cp $(SRC_DIR)/../output/binaries/cat.bin $(ROOTFS)/bin/cat; \
		chmod +x $(ROOTFS)/bin/cat; \
		echo "[INITRD] Copied cat.bin to rootfs/bin/cat"; \
	else \
		echo "[INITRD] WARNING: cat.bin not found, skipping"; \
	fi
	@if [ -f $(SRC_DIR)/../output/binaries/ls.bin ]; then \
		cp $(SRC_DIR)/../output/binaries/ls.bin $(ROOTFS)/bin/ls; \
		chmod +x $(ROOTFS)/bin/ls; \
		echo "[INITRD] Copied ls.bin to rootfs/bin/ls"; \
	else \
		echo "[INITRD] WARNING: ls.bin not found, skipping"; \
	fi

# ── Build initrd image (cpio_newc format) ───────────────────────────────
$(INITRD_IMG): rootfs
	@echo "[INITRD] Building cpio_newc initrd image..."
	@mkdir -p $(OUTPUT_DIR)
	@cd $(ROOTFS) && find . -not -name '.placeholder' | sort | cpio -o -H newc > ../$(INITRD_IMG) 2>/dev/null
	@echo "[INITRD] Image size: $$(du -h $(INITRD_IMG) | cut -f1)"

# ── Clean build artifacts ──────────────────────────────────────────────
clean:
	@echo "[INITRD] Cleaning..."
	rm -rf $(OUTPUT_DIR)
	$(MAKE) -C $(SRC_DIR) clean
	# Remove compiled binaries from rootfs (keep directory structure)
	rm -f $(ROOTFS)/bin/hello $(ROOTFS)/bin/cat $(ROOTFS)/bin/ls

# ── Help ───────────────────────────────────────────────────────────────
help:
	@echo "TriniOS Initrd Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all     — Build user-space programs + initrd image (default)"
	@echo "  clean   — Remove build artifacts"
	@echo "  help    — Show this help"