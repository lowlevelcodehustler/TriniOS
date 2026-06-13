# TriniOS Basic Shell - Makefile
#
# Builds the shell as a standalone userland binary.
# This is a native build (not cross-compiled) since the shell
# runs in userland with standard C library support.

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wno-unused-function
LDFLAGS ?=

# Source files
SRCS = main.c shell_core.c shell_parse.c shell_builtin.c

# Object files
OBJS = $(SRCS:.c=.o)

# Target
TARGET = trini_sh

# Header
HDRS = shell.h

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

# Quick test: run shell with -v to verify build
test: $(TARGET)
	@echo "=== Shell version test ==="
	./$(TARGET) -v
	@echo "=== Shell help test ==="
	./$(TARGET) -h
	@echo "=== Built-in test (echo) ==="
	echo "echo hello from TriniOS shell" | ./$(TARGET) -i
	@echo "=== Build OK ==="