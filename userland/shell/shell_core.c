/*
 * TriniOS Basic Shell - Core
 *
 * Main shell loop, initialization, built-in registry, command dispatch.
 */

#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================================================
 * Built-in registry
 * ============================================================================ */

#define SHELL_BUILTIN_COUNT 32

static builtin_cmd_t builtin_registry[SHELL_BUILTIN_COUNT];
static int builtin_count = 0;

static void register_all_builtins(void) {
    struct {
        const char   *name;
        builtin_fn_t  fn;
    } cmds[] = {
        {"echo",     builtin_echo},
        {"cd",       builtin_cd},
        {"pwd",      builtin_pwd},
        {"ls",       builtin_ls},
        {"cat",      builtin_cat},
        {"mkdir",    builtin_mkdir},
        {"rmdir",    (builtin_fn_t)builtin_rm},
        {"rm",       builtin_rm},
        {"env",      builtin_env},
        {"export",   builtin_export},
        {"exit",     builtin_exit},
        {"quit",     builtin_exit},
        {"help",     builtin_help},
        {"history",  builtin_history},
        {"jobs",     builtin_jobs},
        {"kill",     builtin_kill},
        {"true",     builtin_true},
        {"false",    builtin_false},
        {"test",     builtin_test},
        {"[",        builtin_test},
        {"printf",   builtin_printf},
        {"read",     builtin_read},
        {"sleep",    builtin_sleep},
        {"date",     builtin_date},
        {"whoami",   builtin_whoami},
        {"clear",    builtin_clear},
        {"type",     builtin_type},
        {"eval",     builtin_eval},
        {"wc",       builtin_wc},
        {"head",     builtin_head},
        {"tail",     builtin_tail},
        {"grep",     builtin_grep},
    };

    for (int i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); i++) {
        if (builtin_count < SHELL_BUILTIN_COUNT) {
            builtin_registry[builtin_count].name = cmds[i].name;
            builtin_registry[builtin_count].fn   = cmds[i].fn;
            builtin_registry[builtin_count].help_short = "";
            builtin_registry[builtin_count].help_long  = "";
            builtin_count++;
        }
    }
}

static builtin_fn_t find_builtin(const char *name) {
    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(builtin_registry[i].name, name) == 0) {
            return builtin_registry[i].fn;
        }
    }
    return NULL;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

void shell_init(shell_state_t *state, bool interactive, bool login) {
    memset(state, 0, sizeof(shell_state_t));

    state->interactive  = interactive;
    state->login_shell  = login;
    state->running      = true;
    state->last_exit_status = 0;
    state->next_job_id  = 1;
    state->job_count    = 0;
    state->history_count = 0;
    state->history_pos  = 0;
    state->env_count    = 0;

    /* Default working directory */
    strncpy(state->cwd, "/", SHELL_MAX_PATH_LEN - 1);

    /* Default prompt */
    state->prompt = (char *)SHELL_PROMPT_DEFAULT;

    /* Default environment */
    const char *defaults[] = {
        "HOME=/root",
        "USER=root",
        "SHELL=/bin/trini_sh",
        "PATH=/bin:/usr/bin:/sbin:/usr/sbin",
        "TERM=xterm",
        "LANG=C.UTF-8",
        "HOSTNAME=trini",
        "SHELL_VERSION=0.1.0",
    };

    for (int i = 0; i < (int)(sizeof(defaults) / sizeof(defaults[0])); i++) {
        if (state->env_count < 128) {
            state->env[state->env_count] = (char *)defaults[i];
            state->env_count++;
        }
    }

    /* Register built-ins */
    builtin_count = 0;
    register_all_builtins();

    /* Login message */
    if (interactive && login) {
        printf("TriniOS Shell v0.1.0\n");
        printf("Type 'help' for a list of built-in commands.\n\n");
    }
}

void shell_destroy(shell_state_t *state) {
    if (!state) return;
    state->running = false;
}

/* ============================================================================
 * Prompt
 * ============================================================================ */

static void shell_print_prompt(shell_state_t *state) {
    if (!state->interactive) return;

    /* Build prompt: user@host:cwd$ */
    const char *user = "root";
    const char *host = "trini";
    const char *cwd  = state->cwd;

    /* Shorten home directory */
    const char *display_cwd = cwd;
    if (strcmp(cwd, "/root") == 0) {
        display_cwd = "~";
    }

    printf("%s@%s:%s%s", user, host, display_cwd,
           state->login_shell ? "#" : "$");
    printf(" ");
    fflush(stdout);
}

/* ============================================================================
 * History Management
 * ============================================================================ */

static void shell_add_history(shell_state_t *state, const char *line) {
    if (!line || line[0] == '\0') return;

    /* Trim trailing whitespace */
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t' ||
                       line[len-1] == '\n')) {
        len--;
    }
    if (len == 0) return;

    /* Don't add duplicates */
    if (state->history_count > 0) {
        int last = state->history_count - 1;
        if (strcmp(state->history[last], line) == 0) return;
    }

    strncpy(state->history[state->history_count % SHELL_MAX_HISTORY],
            line, SHELL_MAX_LINE - 1);
    state->history_count++;
    state->history_pos = state->history_count;
}

/* ============================================================================
 * Command Execution
 * ============================================================================ */

static int shell_execute_command(shell_state_t *state, shell_command_t *cmd) {
    if (!cmd || !cmd->program) {
        return 0;
    }

    /* Check if built-in */
    builtin_fn_t fn = find_builtin(cmd->program);
    if (fn) {
        cmd->type = SHELL_CMD_BUILTIN;
        char *argv[SHELL_MAX_ARGS];
        for (int i = 0; i < cmd->arg_count; i++) {
            argv[i] = cmd->args[i];
        }
        if (cmd->arg_count < SHELL_MAX_ARGS) {
            argv[cmd->arg_count] = NULL;
        }
        return fn(state, cmd->arg_count, argv);
    }

    /* External command - for Phase 1, we report it as not found
     * since we don't have execve integration yet */
    cmd->type = SHELL_CMD_EXTERNAL;
    fprintf(stderr, "trini_sh: %s: command not found (use 'help' for builtins)\n",
            cmd->program);
    return 127;
}

/* ============================================================================
 * Main Shell Loop
 * ============================================================================ */

int shell_run(shell_state_t *state) {
    if (!state) return -1;

    char line[SHELL_MAX_LINE];

    while (state->running) {
        /* Print prompt */
        shell_print_prompt(state);

        /* Read line */
        if (!fgets(line, sizeof(line), stdin)) {
            /* EOF or error */
            if (state->interactive) {
                printf("\n");
            }
            break;
        }

        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* Skip empty lines */
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        /* Add to history */
        shell_add_history(state, line);

        /* Parse and execute */
        shell_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));

        if (shell_parse_line(line, &cmd) == 0) {
            int status = shell_execute_command(state, &cmd);
            state->last_exit_status = status;

            /* Free parsed arguments */
            if (cmd.program) {
                free(cmd.program);
                cmd.program = NULL;
            }
            for (int i = 0; i < cmd.arg_count; i++) {
                if (cmd.args[i]) {
                    free(cmd.args[i]);
                    cmd.args[i] = NULL;
                }
            }
        }
    }

    return state->last_exit_status;
}

/* ============================================================================
 * Error String Helper
 * ============================================================================ */

const char *shell_strerror(int err) {
    switch (err) {
    case 0:      return "Success";
    case 1:      return "General error";
    case 126:    return "Command invoked cannot execute";
    case 127:    return "Command not found";
    case 128:    return "Invalid argument to exit";
    case 130:    return "Script terminated by Ctrl-C";
    case 255:    return "Exit status out of range";
    default:     return "Unknown error";
    }
}

void shell_set_prompt(shell_state_t *state, const char *prompt) {
    if (state && prompt) {
        state->prompt = (char *)prompt;
    }
}