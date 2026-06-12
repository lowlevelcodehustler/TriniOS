# ──────────────────────────────────────────────────────────────────────────
# Makefile — TriniOS Initial RAM Disk Builder
# ──────────────────────────────────────────────────────────────────────────
#
# Builds the initrd image from the rootfs directory tree.
# Produces a gzip-compressed cpio_newc archive.
#
# ──────────────────────────────────────────────────────────────────────────

SHELL       := /bin/bash
ROOTFS      := rootfs
OUTPUT_DIR  := output
INITRD_IMG  := $(OUTPUT_DIR)/initrd.img

# ── Toolchain ──────────────────────────────────────────────────────────
CP          := cp
GZIP        := gzip
MKDIR       := mkdir -p
RM          := rm -f
FIND        := find
CPIO        := cpio

# ── Phony targets ──────────────────────────────────────────────────────
.PHONY: all clean help

# ── Default target ─────────────────────────────────────────────────────
all: $(INITRD_IMG)
	@echo "[INITRD] Build complete: $(INITRD_IMG)"

# ── Build initrd image ─────────────────────────────────────────────────
$(INITRD_IMG): $(ROOTFS) $(wildcard $(ROOTFS)/etc/*) $(wildcard $(ROOTFS)/sbin/*)
	@echo "[INITRD] Building initrd image..."
	$(MKDIR) $(OUTPUT_DIR)
	@cd $(ROOTFS) && $(FIND) . -print0 \
		| $(CPIO) --null --create --newc --verbose \
		| $(GZIP) -9 > $(INITRD_IMG)
	@echo "[INITRD] Image size: $$(du -h $(INITRD_IMG) | cut -f1)"

# ── List contents of initrd ───────────────────────────────────────────
.PHONY: list
list:
	@echo "[INITRD] Contents:"
	$(GZIP) -dc $(INITRD_IMG) 2>/dev/null | $(CPIO) --list --quiet
	@echo ""

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
	@echo "  list    — List initrd contents"
	@echo "  clean   — Remove build artifacts"
	@echo "  help    — Show this help"
	@echo ""
	@echo "Usage:"
	@echo "  make all"
	@echo "  make clean"