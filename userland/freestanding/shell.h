// TriniOS - Freestanding shell types and config
// No libc dependency

#ifndef FREESTANDING_SHELL_H
#define FREESTANDING_SHELL_H

#include <stdint.h>
#include <stddef.h>

/* ── Config ─────────────────────────────────────────────────────────── */

#define SHELL_MAX_LINE     4096
#define SHELL_MAX_ARGS      64
#define SHELL_MAX_HISTORY   256
#define SHELL_MAX_PATH_LEN 1024
#define SHELL_PROMPT_DEFAULT "trini$ "

/* ── Environment ────────────────────────────────────────────────────── */

#define ENV_SLOTS       64
#define ENV_KEY_MAX      64
#define ENV_VAL_MAX     256

typedef struct {
    char key[ENV_SLOTS][ENV_KEY_MAX];
    char val[ENV_SLOTS][ENV_VAL_MAX];
    int count;
} shell_env_t;

/* ── Parsed command ─────────────────────────────────────────────────── */

typedef enum {
    SHELL_CMD_BUILTIN,
    SHELL_CMD_EXTERNAL,
    SHELL_CMD_REDIRECTION,
    SHELL_CMD_PIPE
} shell_cmd_type_t;

typedef struct {
    char *program;
    char *args[SHELL_MAX_ARGS];
    int arg_count;

    /* Redirections */
    int redirect_in;   /* -1 = none, >= 0 = fd number of file to open for reading */
    int redirect_out;  /* -1 = none, >= 0 = fd number of file to open for writing */
    int append_out;    /* 1 = >>, 0 = > */

    /* Pipe */
    int pipe_read_fd;   /* -1 = no pipe read end */
    int pipe_write_fd;  /* -1 = no pipe write end */
} shell_command_t;

/* ── Shell state ────────────────────────────────────────────────────── */

typedef struct {
    char cwd[SHELL_MAX_PATH_LEN];
    char history[SHELL_MAX_HISTORY][SHELL_MAX_LINE];
    int history_count;
    int history_pos;

    shell_env_t env;

    int last_exit_status;
    int running;
    int interactive;
} shell_state_t;

/* ── Built-in command function type ─────────────────────────────────── */

typedef int (*builtin_fn_t)(shell_state_t *state, int argc, char **argv);

#endif /* FREESTANDING_SHELL_H */