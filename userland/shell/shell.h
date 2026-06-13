/*
 * TriniOS Basic Shell
 *
 * Command-line interface for Phase 1 (AI-naive).
 * Provides prompt, command parsing, execution, built-ins,
 * pipe/redirection, and signal handling basics.
 *
 * Phase 2 will extend this with AI-powered natural language
 * command interpretation.
 */

#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define SHELL_MAX_LINE         4096
#define SHELL_MAX_ARGS         64
#define SHELL_MAX_HISTORY      256
#define SHELL_MAX_PATH_LEN     1024
#define SHELL_PROMPT_DEFAULT   "trini:~$ "
#define SHELL_PROMPT_ROOT      "trini:~# "

/* ============================================================================
 * Command Types
 * ============================================================================ */

typedef enum {
    SHELL_CMD_EXTERNAL,
    SHELL_CMD_BUILTIN,
    SHELL_CMD_PIPE,
    SHELL_CMD_REDIRECT,
    SHELL_CMD_BACKGROUND,
    SHELL_CMD_EXIT,
} shell_cmd_type_t;

/* ============================================================================
 * Redirection Types
 * ============================================================================ */

typedef enum {
    SHELL_REDIR_NONE,
    SHELL_REDIR_OUTPUT,
    SHELL_REDIR_APPEND,
    SHELL_REDIR_INPUT,
    SHELL_REDIR_ERR_OUT,
    SHELL_REDIR_ERR_APPEND,
    SHELL_REDIR_BOTH,
} shell_redir_type_t;

/* ============================================================================
 * Parsed Command
 * ============================================================================ */

typedef struct {
    shell_cmd_type_t   type;
    char              *program;
    char              *args[SHELL_MAX_ARGS];
    int                arg_count;
    shell_redir_type_t redir_type;
    char               redir_path[SHELL_MAX_PATH_LEN];
    bool               background;
    int                job_id;
} shell_command_t;

/* ============================================================================
 * Job (for background process tracking)
 * ============================================================================ */

#define SHELL_MAX_JOBS 32

typedef struct {
    int     job_id;
    int     pid;
    char   *command;
    bool    running;
    bool    stopped;
    int     exit_status;
} shell_job_t;

/* ============================================================================
 * Shell State
 * ============================================================================ */

typedef struct {
    char               cwd[SHELL_MAX_PATH_LEN];
    char              *env[128];
    int                env_count;
    char               history[SHELL_MAX_HISTORY][SHELL_MAX_LINE];
    int                history_pos;
    int                history_count;
    shell_job_t        jobs[SHELL_MAX_JOBS];
    int                job_count;
    int                next_job_id;
    bool               interactive;
    bool               login_shell;
    bool               running;
    int                last_exit_status;
    char              *prompt;
} shell_state_t;

/* ============================================================================
 * Built-in Command Handler
 * ============================================================================ */

typedef int (*builtin_fn_t)(shell_state_t *state, int argc, char *argv[]);

typedef struct {
    const char   *name;
    builtin_fn_t  fn;
    const char   *help_short;
    const char   *help_long;
} builtin_cmd_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

void  shell_init(shell_state_t *state, bool interactive, bool login);
void  shell_destroy(shell_state_t *state);
int   shell_run(shell_state_t *state);
int   shell_parse_line(const char *line, shell_command_t *cmd);
int   shell_parse_args(const char *line, char *args[], int max_args);
int   shell_execute_builtin(shell_state_t *state, const char *cmd,
                            int argc, char *argv[]);
const char *shell_strerror(int err);
void        shell_set_prompt(shell_state_t *state, const char *prompt);

/* Built-in implementations */
int   builtin_echo(shell_state_t *state, int argc, char *argv[]);
int   builtin_cd(shell_state_t *state, int argc, char *argv[]);
int   builtin_pwd(shell_state_t *state, int argc, char *argv[]);
int   builtin_ls(shell_state_t *state, int argc, char *argv[]);
int   builtin_cat(shell_state_t *state, int argc, char *argv[]);
int   builtin_mkdir(shell_state_t *state, int argc, char *argv[]);
int   builtin_rm(shell_state_t *state, int argc, char *argv[]);
int   builtin_env(shell_state_t *state, int argc, char *argv[]);
int   builtin_export(shell_state_t *state, int argc, char *argv[]);
int   builtin_exit(shell_state_t *state, int argc, char *argv[]);
int   builtin_help(shell_state_t *state, int argc, char *argv[]);
int   builtin_history(shell_state_t *state, int argc, char *argv[]);
int   builtin_jobs(shell_state_t *state, int argc, char *argv[]);
int   builtin_kill(shell_state_t *state, int argc, char *argv[]);
int   builtin_true(shell_state_t *state, int argc, char *argv[]);
int   builtin_false(shell_state_t *state, int argc, char *argv[]);
int   builtin_test(shell_state_t *state, int argc, char *argv[]);
int   builtin_printf(shell_state_t *state, int argc, char *argv[]);
int   builtin_read(shell_state_t *state, int argc, char *argv[]);
int   builtin_sleep(shell_state_t *state, int argc, char *argv[]);
int   builtin_date(shell_state_t *state, int argc, char *argv[]);
int   builtin_whoami(shell_state_t *state, int argc, char *argv[]);
int   builtin_clear(shell_state_t *state, int argc, char *argv[]);
int   builtin_type(shell_state_t *state, int argc, char *argv[]);
int   builtin_eval(shell_state_t *state, int argc, char *argv[]);
int   builtin_wc(shell_state_t *state, int argc, char *argv[]);
int   builtin_head(shell_state_t *state, int argc, char *argv[]);
int   builtin_tail(shell_state_t *state, int argc, char *argv[]);
int   builtin_grep(shell_state_t *state, int argc, char *argv[]);
int   builtin_sort(shell_state_t *state, int argc, char *argv[]);
int   builtin_cut(shell_state_t *state, int argc, char *argv[]);
int   builtin_tr(shell_state_t *state, int argc, char *argv[]);
int   builtin_basename(shell_state_t *state, int argc, char *argv[]);
int   builtin_dirname(shell_state_t *state, int argc, char *argv[]);

#endif /* SHELL_H */