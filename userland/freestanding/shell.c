// TriniOS - Freestanding shell (no libc)
// Minimal POSIX-compatible shell using raw syscalls

#include "syscall.h"
#include "stdio.h"
#include "shell.h"
#include <string.h>
#include <stdint.h>

/* ── String helpers (freestanding) ─────────────────────────────────── */

static inline int str_len(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (int)(p - s);
}

static inline int str_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static inline void str_cpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (*src && i < max - 1) { dst[i++] = *src++; }
    dst[i] = '\0';
}

static inline void str_copy_n(char *dst, const char *src, int n)
{
    if (n <= 0) return;
    int i;
    for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static inline void str_memset(void *s, char c, int n)
{
    unsigned char *p = (unsigned char *)s;
    for (int i = 0; i < n; i++) p[i] = (unsigned char)c;
}

static inline void str_memcpy(void *dst, const void *src, int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

/* ── Environment helpers ───────────────────────────────────────────── */

static void env_init(shell_env_t *env)
{
    const char *defaults[] = {
        "HOME=/",
        "USER=root",
        "SHELL=/bin/trini_sh",
        "PATH=/bin:/sbin",
        "TERM=xterm",
        "HOSTNAME=trini",
    };
    int n = sizeof(defaults) / sizeof(defaults[0]);
    if (n > ENV_SLOTS) n = ENV_SLOTS;

    for (int i = 0; i < n; i++) {
        const char *eq = __builtin_memchr(defaults[i], '=', str_len(defaults[i]));
        if (eq) {
            int klen = (int)(eq - defaults[i]);
            if (klen >= ENV_KEY_MAX) klen = ENV_KEY_MAX - 1;
            str_copy_n(env->key[i], defaults[i], klen + 1);
            str_cpy(env->val[i], eq + 1, ENV_VAL_MAX);
        }
    }
    env->count = n;
}

static char *env_get_val(shell_env_t *env, const char *name)
{
    int nlen = str_len(name);
    for (int i = 0; i < env->count; i++) {
        if (str_cmp(env->key[i], name) == 0) return env->val[i];
    }
    return NULL;
}

static void env_set(shell_env_t *env, const char *name, const char *value)
{
    int nlen = str_len(name);
    /* Update existing */
    for (int i = 0; i < env->count; i++) {
        if (str_cmp(env->key[i], name) == 0) {
            str_cpy(env->val[i], value, ENV_VAL_MAX);
            return;
        }
    }
    /* Add new */
    if (env->count < ENV_SLOTS) {
        int klen = nlen;
        if (klen >= ENV_KEY_MAX) klen = ENV_KEY_MAX - 1;
        str_copy_n(env->key[env->count], name, klen + 1);
        str_cpy(env->val[env->count], value, ENV_VAL_MAX);
        env->count++;
    }
}

/* ── History helpers ───────────────────────────────────────────────── */

static void history_add(shell_state_t *state, const char *line)
{
    int len = str_len(line);
    while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t' || line[len-1] == '\n')) {
        len--;
    }
    if (len == 0) return;

    /* Skip duplicates */
    if (state->history_count > 0) {
        int last = state->history_count - 1;
        if (str_cmp(state->history[last], line) == 0) return;
    }

    str_copy_n(state->history[state->history_count % SHELL_MAX_HISTORY], line, SHELL_MAX_LINE);
    state->history_count++;
    state->history_pos = state->history_count;
}

/* ── Tokenizer (splits line into args, handles quotes) ─────────────── */

static int tokenize(const char *line, char **args, int max_args)
{
    int argc = 0;
    const char *p = line;

    while (*p && argc < max_args) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;

        args[argc] = (char *)p;

        if (*p == '"') {
            /* Double-quoted string */
            p++; /* skip opening quote */
            char *start = p;
            while (*p && *p != '"') p++;
            if (*p == '"') p++; /* skip closing quote */
            int len = (int)(p - start);
            args[argc][len] = '\0';
        } else if (*p == '\'') {
            /* Single-quoted string */
            p++; /* skip opening quote */
            char *start = p;
            while (*p && *p != '\'') p++;
            if (*p == '\'') p++; /* skip closing quote */
            int len = (int)(p - start);
            args[argc][len] = '\0';
        } else {
            /* Unquoted: read until whitespace or special char */
            char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
                   *p != '\r' && *p != '|' && *p != '<' && *p != '>' &&
                   *p != '&' && *p != ';') p++;
            int len = (int)(p - start);
            args[argc][len] = '\0';
        }

        argc++;
    }

    return argc;
}

/* ── Parse line into shell_command_t ───────────────────────────────── */

static int parse_line(const char *line, shell_command_t *cmd)
{
    str_memset(cmd, 0, sizeof(*cmd));

    /* Skip whitespace and comments */
    while (*line == ' ' || *line == '\t' || *line == '\n') line++;
    if (*line == '#' || *line == '\0') return -1;

    char *args[SHELL_MAX_ARGS];
    int argc = tokenize(line, args, SHELL_MAX_ARGS);
    if (argc == 0) return -1;

    cmd->program = args[0];
    for (int i = 1; i < argc; i++) {
        cmd->args[i-1] = args[i];
    }
    cmd->arg_count = argc - 1;

    /* Check for redirections */
    for (int i = 0; i < cmd->arg_count; i++) {
        if (str_cmp(cmd->args[i], ">") == 0 && i + 1 < cmd->arg_count) {
            cmd->redirect_out = 1;
            for (int j = i; j < cmd->arg_count - 2; j++) {
                cmd->args[j] = cmd->args[j + 2];
            }
            cmd->arg_count -= 2;
            i--;
        } else if (str_cmp(cmd->args[i], ">>") == 0 && i + 1 < cmd->arg_count) {
            cmd->redirect_out = 2;
            for (int j = i; j < cmd->arg_count - 2; j++) {
                cmd->args[j] = cmd->args[j + 2];
            }
            cmd->arg_count -= 2;
            i--;
        } else if (str_cmp(cmd->args[i], "<") == 0 && i + 1 < cmd->arg_count) {
            cmd->redirect_in = 1;
            for (int j = i; j < cmd->arg_count - 2; j++) {
                cmd->args[j] = cmd->args[j + 2];
            }
            cmd->arg_count -= 2;
            i--;
        }
    }

    return 0;
}

/* ── Built-in commands ─────────────────────────────────────────────── */

static int builtin_echo(shell_state_t *state, int argc, char **argv)
{
    (void)state;
    int first = 1;
    for (int i = 0; i < argc; i++) {
        if (!first) write(STDOUT_FILENO, " ", 1);
        write(STDOUT_FILENO, argv[i], str_len(argv[i]));
        first = 0;
    }
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int builtin_pwd(shell_state_t *state, int argc, char **argv)
{
    (void)argc; (void)argv;
    write(STDOUT_FILENO, state->cwd, str_len(state->cwd));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

static int builtin_cd(shell_state_t *state, int argc, char **argv)
{
    (void)state; (void)argc; (void)argv;
    /* For Phase 1: just print a message */
    const char *msg = "cd: not implemented yet (needs filesystem server)\n";
    write(STDOUT_FILENO, msg, str_len(msg));
    return 0;
}

static int builtin_env(shell_state_t *state, int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < state->env.count; i++) {
        write(STDOUT_FILENO, state->env.key[i], str_len(state->env.key[i]));
        write(STDOUT_FILENO, "=", 1);
        write(STDOUT_FILENO, state->env.val[i], str_len(state->env.val[i]));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

static int builtin_export(shell_state_t *state, int argc, char **argv)
{
    if (argc == 0) {
        for (int i = 0; i < state->env.count; i++) {
            write(STDOUT_FILENO, state->env.key[i], str_len(state->env.key[i]));
            write(STDOUT_FILENO, "=", 1);
            write(STDOUT_FILENO, state->env.val[i], str_len(state->env.val[i]));
            write(STDOUT_FILENO, "\n", 1);
        }
        return 0;
    }

    for (int i = 0; i < argc; i++) {
        char *eq = __builtin_memchr(argv[i], '=', str_len(argv[i]));
        if (!eq) {
            const char *msg = "export: invalid variable name\n";
            write(STDERR_FILENO, msg, str_len(msg));
            return 1;
        }
        int nlen = (int)(eq - argv[i]);
        char name[ENV_KEY_MAX];
        if (nlen >= ENV_KEY_MAX) nlen = ENV_KEY_MAX - 1;
        str_copy_n(name, argv[i], nlen + 1);

        env_set(&state->env, name, eq + 1);
    }
    return 0;
}

static int builtin_exit(shell_state_t *state, int argc, char **argv)
{
    (void)argc; (void)argv;
    state->running = 0;
    return 0;
}

static int builtin_help(shell_state_t *state, int argc, char **argv)
{
    (void)argc; (void)argv; (void)state;
    const char *msg =
        "TriniOS Shell v0.1.0 - Built-in commands:\n"
        "  echo [text]            Display text\n"
        "  cd [dir]               Change directory (not implemented)\n"
        "  pwd                    Print working directory\n"
        "  env                    Show environment variables\n"
        "  export NAME=VALUE      Set environment variable\n"
        "  exit                   Exit the shell\n"
        "  help                   Show this help\n"
        "  clear                  Clear screen\n"
        "  true                   Return success (0)\n"
        "  false                  Return failure (1)\n";
    write(STDOUT_FILENO, msg, str_len(msg));
    return 0;
}

static int builtin_clear(shell_state_t *state, int argc, char **argv)
{
    (void)argc; (void)argv; (void)state;
    const char *esc = "\x1b[H\x1b[2J";
    write(STDOUT_FILENO, esc, str_len(esc));
    return 0;
}

static int builtin_true(shell_state_t *state, int argc, char **argv)
{
    (void)argc; (void)argv; (void)state;
    return 0;
}

static int builtin_false(shell_state_t *state, int argc, char **argv)
{
    (void)argc; (void)argv; (void)state;
    return 1;
}

/* ── Builtin registry ──────────────────────────────────────────────── */

typedef struct {
    const char *name;
    builtin_fn_t fn;
} builtin_entry_t;

static builtin_entry_t builtin_table[] = {
    {"echo",     builtin_echo},
    {"pwd",      builtin_pwd},
    {"cd",       builtin_cd},
    {"env",      builtin_env},
    {"export",   builtin_export},
    {"exit",     builtin_exit},
    {"quit",     builtin_exit},
    {"help",     builtin_help},
    {"clear",    builtin_clear},
    {"true",     builtin_true},
    {"false",    builtin_false},
};

static int builtin_table_size = sizeof(builtin_table) / sizeof(builtin_table[0]);

static builtin_fn_t find_builtin(const char *name)
{
    for (int i = 0; i < builtin_table_size; i++) {
        if (str_cmp(name, builtin_table[i].name) == 0) {
            return builtin_table[i].fn;
        }
    }
    return NULL;
}

/* ── Execute a parsed command ──────────────────────────────────────── */

static int execute_command(shell_state_t *state, shell_command_t *cmd)
{
    if (!cmd->program) return 0;

    /* Check builtins */
    builtin_fn_t fn = find_builtin(cmd->program);
    if (fn) {
        return fn(state, cmd->arg_count, cmd->args);
    }

    const char *fmt_msg = "trini_sh: %s: command not found. Type 'help' for builtins.\n";
    /* Manual printf-like output without recursion */
    write(STDERR_FILENO, "trini_sh: ", 10);
    write(STDERR_FILENO, cmd->program, str_len(cmd->program));
    write(STDERR_FILENO, ": command not found. Type 'help' for builtins.\n", 47);
    return 127;
}

/* ── Print prompt ──────────────────────────────────────────────────── */

static void print_prompt(shell_state_t *state)
{
    if (!state->interactive) return;

    write(STDOUT_FILENO, "root@trini:", 11);
    write(STDOUT_FILENO, state->cwd, str_len(state->cwd));

    if (str_cmp(state->cwd, "/") == 0) {
        write(STDOUT_FILENO, "# ", 2);
    } else {
        write(STDOUT_FILENO, "$ ", 2);
    }
}

/* ── Main shell loop ──────────────────────────────────────────────── */

int shell_run(shell_state_t *state)
{
    if (!state) return -1;

    char line[SHELL_MAX_LINE];

    while (state->running) {
        print_prompt(state);

        /* Read a line from stdin */
        int n = 0;
        while (n < SHELL_MAX_LINE - 1) {
            int r = read(STDIN_FILENO, &line[n], 1);
            if (r <= 0) {
                /* EOF or error */
                if (n == 0) return 0;
                line[n] = '\n';
                n++;
                break;
            }
            line[n] = (char)r;
            n++;
            if ((unsigned char)line[n-1] == '\n') break;
        }
        line[n] = '\0';

        /* Skip empty lines and comments */
        int p = 0;
        while (p < n && (line[p] == ' ' || line[p] == '\t' || line[p] == '\n')) p++;
        if (p == n || line[p] == '#') continue;

        /* Add to history */
        history_add(state, line);

        /* Parse and execute */
        shell_command_t cmd;
        str_memset(&cmd, 0, sizeof(cmd));

        if (parse_line(line, &cmd) == 0) {
            state->last_exit_status = execute_command(state, &cmd);
        }
    }

    return state->last_exit_status;
}

/* ── Shell initialization ─────────────────────────────────────────── */

void shell_init(shell_state_t *state, int interactive, int login)
{
    str_memset(state, 0, sizeof(*state));

    state->interactive = interactive;
    state->running = 1;
    state->last_exit_status = 0;
    state->history_count = 0;
    state->history_pos = 0;

    /* Default working directory */
    str_cpy(state->cwd, "/", SHELL_MAX_PATH_LEN);

    /* Initialize environment */
    env_init(&state->env);

    if (interactive && login) {
        const char *banner = "TriniOS Shell v0.1.0\nType 'help' for a list of built-in commands.\n\n";
        write(STDOUT_FILENO, banner, str_len(banner));
    }
}

/* ── Entry point ──────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    shell_state_t state;
    shell_init(&state, 1, 1);

    int ret = shell_run(&state);
    return ret;
}