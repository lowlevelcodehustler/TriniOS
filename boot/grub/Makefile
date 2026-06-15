# ──────────────────────────────────────────────────────────────────────────
# Makefile — TriniOS Kernel Builder (GRUB2 Multiboot2)
# ──────────────────────────────────────────────────────────────────────────
#
# Assembles the entry point, compiles the C kernel code, and links everything
# into a flat binary suitable for GRUB2's multiboot2 module.
#
# ──────────────────────────────────────────────────────────────────────────

SHELL       := /bin/bash

# ── Paths ──────────────────────────────────────────────────────────────
ROOT_DIR    := ../..
KERNEL_DIR  := $(ROOT_DIR)/kernel
BOOT_DIR    := $(ROOT_DIR)/boot

SRC_DIR     := .
KERNEL_SRC  := $(KERNEL_DIR)/gnumach/x86_64
KERNEL_INC  := $(KERNEL_DIR)/gnumach/include
MM_SRC      := $(KERNEL_DIR)/mm
IPC_SRC     := $(KERNEL_DIR)/ipc
TASK_SRC    := $(KERNEL_DIR)/task
HOST_SRC    := $(KERNEL_DIR)/host
ASI_SRC     := $(KERNEL_DIR)/asi
TEST_SRC    := $(KERNEL_DIR)/test

BUILD_DIR   := $(ROOT_DIR)/build/kernel
OUTPUT_BIN  := $(BUILD_DIR)/trini-kernel.bin

# ── Toolchain (cross-compiler or native) ───────────────────────────────
CC          ?= gcc
AS          ?= nasm
LD          ?= ld
OBJCOPY     ?= objcopy
OBJDUMP     ?= objdump

# ── Compiler flags ─────────────────────────────────────────────────────
# Freestanding kernel code: no libc, no pie, position-independent where needed
CFLAGS      := -std=c11 -Wall -Wextra -Werror=return-type \
               -ffreestanding -fno-stack-protector -fno-pie \
               -nostdlib -fno-common \
               -I$(SRC_DIR) -I$(KERNEL_INC) -I$(KERNEL_INC)/mach \
               -I$(MM_SRC) -I$(IPC_SRC) -I$(TASK_SRC) -I$(HOST_SRC) -I$(ASI_SRC) \
               -I$(TEST_SRC) \
               -O2 -m64 -mcmodel=kernel \
               -fno-exceptions -fno-strict-aliasing

# Assembler flags (NASM for x86_64)
ASFLAGS     := -f elf64

# Linker flags
LDFLAGS     := -T $(SRC_DIR)/kernel.ld \
               -N -e _start \
               --oformat=binary

# ── Source files ───────────────────────────────────────────────────────
ASM_SRCS    := $(wildcard $(SRC_DIR)/*.asm)
C_SRCS      := $(wildcard $(SRC_DIR)/*.c) \
               $(wildcard $(KERNEL_SRC)/*.c) \
               $(wildcard $(MM_SRC)/*.c) \
               $(wildcard $(IPC_SRC)/*.c) \
               $(wildcard $(TASK_SRC)/*.c) \
               $(wildcard $(HOST_SRC)/*.c) \
               $(wildcard $(ASI_SRC)/*.c) \
               $(wildcard $(TEST_SRC)/*.c)

# ── Object files ───────────────────────────────────────────────────────
ASM_OBJS    := $(patsubst $(SRC_DIR)/%.asm,$(BUILD_DIR)/%.o,$(ASM_SRCS))
C_OBJS      := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))

ALL_OBJS    := $(ASM_OBJS) $(C_OBJS)

# ── Phony targets ──────────────────────────────────────────────────────
.PHONY: all clean help info

# ── Default target ─────────────────────────────────────────────────────
all: $(OUTPUT_BIN)
	@echo ""
	@echo "[KERNEL] Build complete: $(OUTPUT_BIN)"
	@echo "[KERNEL] Size: $$(du -h $(OUTPUT_BIN) | cut -f1)"

# ── Link: produce flat binary for GRUB2 ────────────────────────────────
$(OUTPUT_BIN): $(ALL_OBJS) $(SRC_DIR)/kernel.ld
	@echo "[LINK] Linking kernel..."
	$(LD) $(LDFLAGS) -o $(BUILD_DIR)/kernel.elf $(ALL_OBJS)
	@echo "[OBJCOPY] Converting to flat binary..."
	$(OBJCOPY) -O binary $(BUILD_DIR)/kernel.elf $(OUTPUT_BIN)
	@rm -f $(BUILD_DIR)/kernel.elf

# ── Assemble: NASM .asm -> .o ─────────────────────────────────────────
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) -o $@ $<

# ── Compile: GCC .c -> .o ─────────────────────────────────────────────
$(BUILD_DIR)/%.o: %.c
	@echo "[CC] $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Disassembly listing (debug) ────────────────────────────────────────
.PHONY: disasm
disasm: $(ALL_OBJS)
	@echo "[DISASM] Generating kernel disassembly..."
	$(LD) $(LDFLAGS:--oformat=binary=--oformat=elf64-x86-64) -o $(BUILD_DIR)/kernel.elf $(ALL_OBJS)
	$(OBJDUMP) -d $(BUILD_DIR)/kernel.elf > $(BUILD_DIR)/kernel.disasm
	@echo "[DISASM] Written to $(BUILD_DIR)/kernel.disasm"
	@rm -f $(BUILD_DIR)/kernel.elf

# ── Clean build artifacts ──────────────────────────────────────────────
clean:
	@echo "[KERNEL] Cleaning..."
	rm -rf $(BUILD_DIR)

# ── Show build configuration ──────────────────────────────────────────
info:
	@echo "TriniOS Kernel Build Configuration"
	@echo "  CC:      $(CC)"
	@echo "  AS:      $(AS)"
	@echo "  LD:      $(LD)"
	@echo "  CFLAGS:  $(CFLAGS)"
	@echo "  Output:  $(OUTPUT_BIN)"

# ── Help ───────────────────────────────────────────────────────────────
help:
	@echo "TriniOS Kernel Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all     — Build kernel binary (default)"
	@echo "  disasm  — Generate kernel disassembly"
	@echo "  clean   — Remove build artifacts"
	@echo "  info    — Show build configuration"
	@echo "  help    — Show this help"