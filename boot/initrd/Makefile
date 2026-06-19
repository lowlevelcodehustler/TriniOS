# ──────────────────────────────────────────────────────────────────────────
# Makefile — TriniOS Initial RAM Disk Builder
# ──────────────────────────────────────────────────────────────────────────
#
# Builds the initrd image from the rootfs directory tree.
# Produces a gzip-compressed tar archive (simpler, more portable).
#
# ──────────────────────────────────────────────────────────────────────────

SHELL       := /bin/bash
ROOTFS      := rootfs
OUTPUT_DIR  := output
INITRD_IMG  := $(OUTPUT_DIR)/initrd.img

# ── Toolchain ──────────────────────────────────────────────────────────
MKDIR       := mkdir -p
RM          := rm -f
TAR         := tar

# ── Phony targets ──────────────────────────────────────────────────────
.PHONY: all clean help

# ── Default target ─────────────────────────────────────────────────────
all: $(INITRD_IMG)
	@echo "[INITRD] Build complete: $(INITRD_IMG)"

# ── Build initrd image ─────────────────────────────────────────────────
$(INITRD_IMG): $(ROOTFS)
	@echo "[INITRD] Building initrd image..."
	$(MKDIR) $(OUTPUT_DIR)
	cd $(ROOTFS) && $(TAR) czf ../$(INITRD_IMG) .
	@echo "[INITRD] Image size: $$(du -h $(INITRD_IMG) | cut -f1)"

# ── Clean build artifacts ──────────────────────────────────────────────
clean:
	@echo "[INITRD] Cleaning..."
	$(RM) $(INITRD_IMG)
	$(RM) -r $(OUTPUT_DIR)

# ── Help ───────────────────────────────────────────────────────────────
help:
	@echo "TriniOS Initrd Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all     — Build initrd image (default)"
	@echo "  clean   — Remove build artifacts"
	@echo "  help    — Show this help"