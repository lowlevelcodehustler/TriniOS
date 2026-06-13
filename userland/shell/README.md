# TriniOS Basic Shell

Command-line interface for TriniOS Phase 1 (AI-naive). Provides an interactive REPL with 32 built-in commands, command parsing with quote handling and escape processing, redirection, pipes, background job tracking, command history, and environment variable management.

Phase 2 will extend this with AI-powered natural language command interpretation.

## Architecture

```
main.c              Entry point, argument parsing (-i, -c, -n, -v, -h)
shell_core.c        REPL loop, builtin registry, command dispatch, history
shell_parse.c       Line parsing, tokenization, quote handling, redirection, pipes
shell_builtin.c     32 built-in command implementations
shell.h             Types, config, API declarations
```

### Data Flow

```
User Input → shell_run() → shell_parse_line() → shell_execute_command()
                                         ↓
                              find_builtin() → builtin_fn()
                                         ↓
                              (not found) → "command not found" error
```

## Built-in Commands (32)

### Fully Implemented (20)

| Command | Description |
|---|---|
| `echo [-n] [args...]` | Display text |
| `cd [dir]` | Change directory (~ expansion) |
| `pwd` | Print working directory |
| `env` | Display environment variables |
| `export NAME=VALUE` | Set environment variable |
| `exit` / `quit` | Leave the shell |
| `help` | Show help |
| `history` | Show command history |
| `jobs` | List background jobs |
| `printf FORMAT ...` | Formatted output (%s, %d, %n) |
| `read [-p prompt] VAR` | Read line into variable |
| `sleep N` | Sleep for N seconds |
| `date` | Show current date/time |
| `whoami` | Show current user |
| `clear` | Clear the screen (ANSI) |
| `type name` | Show command type |
| `eval args` | Evaluate arguments |
| `basename path` | Extract filename from path |
| `dirname path` | Extract directory from path |
| `test expr` | Evaluate expression (string/numeric) |
| `true` / `false` | Return 0 / Return 1 |

### Stubs (12 - require filesystem server in Phase 2)

| Command | Description |
|---|---|
| `ls [dir]` | List directory |
| `cat [file...]` | Display file contents |
| `mkdir [-p] dir` | Create directory |
| `rm [-rf] file...` | Remove files |
| `kill [pid]` | Kill a process |
| `wc [file]` | Word/line/byte count |
| `head [-n N] file` | Show first lines |
| `tail [-n N] file` | Show last lines |
| `grep pattern file` | Search text |
| `sort file` | Sort lines |
| `cut -d, -f file` | Cut fields |
| `tr set1 set2` | Translate characters |

## Features

- **Command Parsing**: Quote handling (single/double), escape processing, variable expansion ($VAR/${VAR})
- **Redirection**: `>`, `>>`, `<`, `2>`, `&>`
- **Pipes**: `|` (Phase 1: left side only)
- **Background Jobs**: `&` tracking with job IDs
- **Command History**: Up to 256 commands
- **Environment**: 128 variable slots, KEY=VALUE format
- **Default Environment**: HOME, USER, SHELL, PATH, TERM, LANG, HOSTNAME, SHELL_VERSION

## Build

```bash
cd userland/shell
make
```

## Usage

```bash
# Interactive mode (default)
./trini_sh

# Non-login shell
./trini_sh -n

# Execute command string
./trini_sh -c "echo hello"

# Version
./trini_sh -v

# Help
./trini_sh -h
```

## Configuration

| Constant | Value | Description |
|---|---|---|
| `SHELL_MAX_LINE` | 4096 | Maximum input line length |
| `SHELL_MAX_ARGS` | 64 | Maximum arguments per command |
| `SHELL_MAX_HISTORY` | 256 | Command history entries |
| `SHELL_MAX_PATH_LEN` | 1024 | Maximum path length |
| `SHELL_MAX_JOBS` | 32 | Maximum background jobs |

## TODO (Phase 2)

- [ ] Connect filesystem stubs to ext4 bdev server
- [ ] External command execution via BSD server
- [ ] Full pipe support (both sides)
- [ ] Signal handling (SIGINT, SIGTERM)
- [ ] Job control (fg, bg, suspend)
- [ ] AI-powered command interpretation