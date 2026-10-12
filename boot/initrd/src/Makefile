# ──────────────────────────────────────────────────────────────────────────
# Makefile — TriniOS User-Space Programs (freestanding, no libc)
# ──────────────────────────────────────────────────────────────────────────

SHELL       := /bin/bash
CC          := x86_64-linux-gnu-gcc
OBJCOPY     := objcopy
STRIP       := strip

# ── Common toolchain flags for freestanding (no libc) compilation ───────
CFLAGS      := -m64 -mno-red-zone -nostdlib -fno-builtin -Wall -O2 -fno-stack-protector

# ── Output directory ────────────────────────────────────────────────────
OUTPUT_DIR  := ../output/binaries

# ── Programs to build (list of .bin outputs) ────────────────────────────
# hello is shipped as an ELF (see below), so it is not in this list.
BINARIES    := $(OUTPUT_DIR)/cat.bin \
               $(OUTPUT_DIR)/ls.bin

# ── Phony targets ───────────────────────────────────────────────────────
.PHONY: all clean help

# ── Default target ──────────────────────────────────────────────────────
all: $(OUTPUT_DIR) $(BINARIES) $(OUTPUT_DIR)/hello.elf $(OUTPUT_DIR)/hello.bin
	@echo "[USER-SPACE] Build complete"

# ── Output directory ────────────────────────────────────────────────────
$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)

# ── hello binary (uses custom linker script, entry=_start) ──────────────
# The kernel's elf_loader parses ELF64 headers and copies PT_LOAD segments,
# so the initrd must ship the ELF itself — a flat objcopy -O binary has no
# header/entry point and would fail elf_parse(). hello.elf is therefore the
# primary artifact; hello.bin (flat) is kept for reference/legacy tooling.
$(OUTPUT_DIR)/hello.elf: hello.c hello.ld | $(OUTPUT_DIR)
	@echo "[USER-SPACE] Compiling hello..."
	$(CC) $(CFLAGS) -c hello.c -o hello.o
	$(CC) $(CFLAGS) -static -nostartfiles -nodefaultlibs -Wl,-N -Wl,--oformat=elf64-x86-64 -T hello.ld hello.o -o $@
	@rm -f hello.o
	@echo "[USER-SPACE]   -> $@"

$(OUTPUT_DIR)/hello.bin: $(OUTPUT_DIR)/hello.elf
	$(OBJCOPY) -O binary $< $@
	@echo "[USER-SPACE]   -> $@"

# ── cat binary (freestanding, uses open/read/write syscalls, entry=main) ──
$(OUTPUT_DIR)/cat.bin: cat.c | $(OUTPUT_DIR)
	@echo "[USER-SPACE] Compiling cat..."
	$(CC) $(CFLAGS) -c cat.c -o cat.o
	$(CC) $(CFLAGS) -static -nostartfiles -nodefaultlibs -Wl,-N -Wl,--oformat=elf64-x86-64 -e main cat.o -o cat.elf
	$(OBJCOPY) -O binary cat.elf $@
	@rm -f cat.o cat.elf
	@echo "[USER-SPACE]   -> $@"

# ── ls binary (freestanding, uses getdents64 syscall, entry=main) ───────
$(OUTPUT_DIR)/ls.bin: ls.c | $(OUTPUT_DIR)
	@echo "[USER-SPACE] Compiling ls..."
	$(CC) $(CFLAGS) -c ls.c -o ls.o
	$(CC) $(CFLAGS) -static -nostartfiles -nodefaultlibs -Wl,-N -Wl,--oformat=elf64-x86-64 -e main ls.o -o ls.elf
	$(OBJCOPY) -O binary ls.elf $@
	@rm -f ls.o ls.elf
	@echo "[USER-SPACE]   -> $@"

# ── Clean build artifacts ───────────────────────────────────────────────
clean:
	@echo "[USER-SPACE] Cleaning..."
	rm -rf $(OUTPUT_DIR)
	@rm -f hello.o

# ── Help ────────────────────────────────────────────────────────────────
help:
	@echo "TriniOS User-Space Programs Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all     — Build all programs (default)"
	@echo "  clean   — Remove build artifacts"
	@echo "  help    — Show this help"