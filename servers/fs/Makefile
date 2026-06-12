CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter -O2
LDFLAGS =

SRCDIR = .
OBJDIR = build
TARGET = $(OBJDIR)/ext4_server

SOURCES = \
	$(SRCDIR)/ext4_bdev.c \
	$(SRCDIR)/ext4_mount.c \
	$(SRCDIR)/ext4_inode.c \
	$(SRCDIR)/ext4_lookup.c \
	$(SRCDIR)/ext4_file.c \
	$(SRCDIR)/ext4_dir.c \
	$(SRCDIR)/ext4_journal.c \
	$(SRCDIR)/ext4_server.c

OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(OBJDIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR)

run: $(TARGET)
	@echo "Starting ext4_server in interactive mode..."
	@echo "Usage: mount <device> <mount_point>, ls <mount> <path>, stat <mount> <path>"
	@echo "Type 'help' for full command list, 'quit' to exit."
	./$(TARGET)