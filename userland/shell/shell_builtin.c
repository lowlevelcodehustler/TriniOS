/*
 * TriniOS Basic Shell - Built-in Commands
 *
 * Implements all 32 built-in commands declared in shell.h.
 * These are self-contained implementations that do not require
 * external process execution.
 */

#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * Helper: print usage for a built-in
 * ============================================================================ */

static void builtin_usage(const char *name, const char *usage) {
    fprintf(stderr, "Usage: %s %s\n", name, usage);
}

/* ============================================================================
 * Helper: lookup environment variable
 * ============================================================================ */

static const char *env_lookup(shell_state_t *state, const char *name) {
    if (!state || !name) return NULL;
    for (int i = 0; i < state->env_count; i++) {
        char *eq = strchr(state->env[i], '=');
        if (eq) {
            uint32_t nlen = (uint32_t)(eq - state->env[i]);
            if (nlen == strlen((char *)name) &&
                strncmp(state->env[i], name, nlen) == 0) {
                return eq + 1;
            }
        }
    }
    return NULL;
}

/* ============================================================================
 * Helper: get or set environment variable
 * ============================================================================ */

static int env_set(shell_state_t *state, const char *name, const char *value) {
    if (!state || !name) return -1;
    char *eq = strchr((char *)name, '=');
    if (eq) return -1; /* name must not contain '=' */

    /* Check if already exists */
    for (int i = 0; i < state->env_count; i++) {
        char *eq = strchr(state->env[i], '=');
        if (eq) {
            uint32_t nlen = (uint32_t)(eq - state->env[i]);
            if (nlen == strlen((char *)name) &&
                strncmp(state->env[i], name, nlen) == 0) {
                /* Rebuild the entry */
                size_t new_len = strlen((char *)name) + 1 + strlen(value) + 1;
                char *new_env = malloc(new_len);
                if (new_env) {
                    snprintf(new_env, new_len, "%s=%s", name, value);
                    state->env[i] = new_env;
                }
                return 0;
            }
        }
    }

    /* Add new */
    if (state->env_count >= 128) return -1;
    size_t new_len = strlen((char *)name) + 1 + strlen(value) + 1;
    char *new_env = malloc(new_len);
    if (!new_env) return -1;
    snprintf(new_env, new_len, "%s=%s", name, value);
    state->env[state->env_count] = new_env;
    state->env_count++;
    return 0;
}

/* ============================================================================
 * echo - display a line of text
 * ============================================================================ */

int builtin_echo(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    bool newline = true;
    int start = 1;

    /* Check for -n flag */
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = false;
        start = 2;
    }

    for (int i = start; i < argc; i++) {
        if (i > start) printf(" ");
        printf("%s", argv[i]);
    }
    if (newline) printf("\n");
    return 0;
}

/* ============================================================================
 * cd - change working directory
 * ============================================================================ */

int builtin_cd(shell_state_t *state, int argc, char *argv[]) {
    (void)argc;
    if (!state) return -1;

    const char *target = "/";
    if (argc > 1) {
        target = argv[1];
    } else {
        /* cd with no args goes to home */
        const char *home = env_lookup(state, "HOME");
        if (home) target = home;
    }

    /* Handle ~ expansion */
    if (target[0] == '~') {
        const char *home = env_lookup(state, "HOME");
        if (home) {
            static char expanded[SHELL_MAX_PATH_LEN];
            snprintf(expanded, sizeof(expanded), "%s%s", home, target + 1);
            target = expanded;
        }
    }

    strncpy(state->cwd, target, SHELL_MAX_PATH_LEN - 1);
    state->cwd[SHELL_MAX_PATH_LEN - 1] = '\0';
    return 0;
}

/* ============================================================================
 * pwd - print working directory
 * ============================================================================ */

int builtin_pwd(shell_state_t *state, int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!state) return -1;
    printf("%s\n", state->cwd);
    return 0;
}

/* ============================================================================
 * ls - list directory contents (simplified stub)
 * ============================================================================ */

int builtin_ls(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    /* Without filesystem integration, show a stub message */
    puts("ls: filesystem server not available yet (Phase 2)");
    puts("  (built-in stub - will list directory when fs server is connected)");
    return 0;
}

/* ============================================================================
 * cat - concatenate and display files (simplified stub)
 * ============================================================================ */

int builtin_cat(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("cat", "[file ...]");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        printf("cat: %s - filesystem not available yet (Phase 2)\n", argv[i]);
    }
    return 0;
}

/* ============================================================================
 * mkdir - create directory (simplified stub)
 * ============================================================================ */

int builtin_mkdir(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("mkdir", "[-p] directory");
        return 1;
    }
    bool recursive = false;
    int start = 1;
    if (strcmp(argv[1], "-p") == 0) {
        recursive = true;
        start = 2;
    }
    for (int i = start; i < argc; i++) {
        (void)recursive;
        printf("mkdir: %s - filesystem not available yet (Phase 2)\n", argv[i]);
    }
    return 0;
}

/* ============================================================================
 * rm - remove files (simplified stub)
 * ============================================================================ */

int builtin_rm(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("rm", "[-rf] file ...");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        printf("rm: %s - filesystem not available yet (Phase 2)\n", argv[i]);
    }
    return 0;
}

/* ============================================================================
 * env - display environment
 * ============================================================================ */

int builtin_env(shell_state_t *state, int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!state) return -1;
    for (int i = 0; i < state->env_count; i++) {
        printf("%s\n", state->env[i]);
    }
    return 0;
}

/* ============================================================================
 * export - set environment variable
 * ============================================================================ */

int builtin_export(shell_state_t *state, int argc, char *argv[]) {
    if (!state) return -1;
    if (argc < 2) {
        /* With no args, show all env vars */
        return builtin_env(state, 0, NULL);
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            int ret = env_set(state, argv[i], eq + 1);
            if (ret != 0) {
                fprintf(stderr, "export: failed to set %s\n", argv[i]);
                status = 1;
            }
        } else {
            /* Export without value: just print current value */
            const char *val = env_lookup(state, argv[i]);
            if (val) {
                printf("%s=%s\n", argv[i], val);
            } else {
                fprintf(stderr, "export: %s: not found\n", argv[i]);
                status = 1;
            }
        }
    }
    return status;
}

/* ============================================================================
 * exit - leave the shell
 * ============================================================================ */

int builtin_exit(shell_state_t *state, int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!state) return -1;
    state->running = false;
    return 0;
}

/* ============================================================================
 * help - show help
 * ============================================================================ */

int builtin_help(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    puts("TriniOS Shell Built-in Commands:");
    puts("================================");
    puts("  echo [-n] [args...]   Display text");
    puts("  cd [dir]             Change directory");
    puts("  pwd                  Print working directory");
    puts("  ls [dir]             List directory (stub)");
    puts("  cat [file...]        Display file (stub)");
    puts("  mkdir [-p] dir       Create directory (stub)");
    puts("  rm [-rf] file...     Remove file (stub)");
    puts("  env                  Display environment");
    puts("  export NAME=VALUE    Set environment variable");
    puts("  exit                 Leave the shell");
    puts("  help                 Show this help");
    puts("  history              Show command history");
    puts("  jobs                 List background jobs");
    puts("  kill [pid]           Kill a process (stub)");
    puts("  printf FORMAT ...    Formatted output");
    puts("  read [-p prompt] VAR Read line into variable");
    puts("  sleep N              Sleep for N seconds");
    puts("  date                 Show current date/time");
    puts("  whoami               Show current user");
    puts("  clear                Clear the screen");
    puts("  type name            Show command type");
    puts("  eval args            Evaluate arguments");
    puts("  wc [file]            Word/line count (stub)");
    puts("  head [-n N] file     Show first lines (stub)");
    puts("  tail [-n N] file     Show last lines (stub)");
    puts("  grep pattern file    Search text (stub)");
    puts("  sort file            Sort lines (stub)");
    puts("  cut -d, -f file      Cut fields (stub)");
    puts("  tr set1 set2         Translate chars (stub)");
    puts("  basename path        Extract filename");
    puts("  dirname path         Extract directory");
    puts("  test expr            Evaluate expression");
    puts("  true                 Return success (exit 0)");
    puts("  false                Return failure (exit 1)");
    puts("");
    puts("  Commands marked (stub) require filesystem server integration (Phase 2).");
    puts("  Use 'help <command>' for detailed help on a specific command.");
    return 0;
}

/* ============================================================================
 * history - show command history
 * ============================================================================ */

int builtin_history(shell_state_t *state, int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!state) return -1;
    int total = state->history_count;
    int start = 0;
    if (total > SHELL_MAX_HISTORY) {
        start = total - SHELL_MAX_HISTORY;
    }
    for (int i = start; i < total; i++) {
        printf("  %-4d %s\n", (i - start + 1), state->history[i % SHELL_MAX_HISTORY]);
    }
    return 0;
}

/* ============================================================================
 * jobs - list background jobs
 * ============================================================================ */

int builtin_jobs(shell_state_t *state, int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!state) return -1;
    if (state->job_count == 0) {
        puts("No jobs running.");
        return 0;
    }
    for (int i = 0; i < state->job_count; i++) {
        shell_job_t *job = &state->jobs[i];
        const char *status = job->running ? "running" : "done";
        printf("  [%d] %s %s\n", job->job_id, status, job->command ? job->command : "");
    }
    return 0;
}

/* ============================================================================
 * kill - kill a process (stub)
 * ============================================================================ */

int builtin_kill(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("kill", "[signal] pid");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        printf("kill: pid %s - process server not available yet (Phase 2)\n", argv[i]);
    }
    return 0;
}

/* ============================================================================
 * printf - formatted output
 * ============================================================================ */

int builtin_printf(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("printf", "FORMAT [args...]");
        return 1;
    }
    /* Simple printf: the first arg is the format string,
     * remaining args are format arguments (basic support). */
    const char *fmt = argv[1];
    /* For safety, we just print the format with basic %s substitution */
    int arg_idx = 2;
    const char *f = fmt;
    while (*f) {
        if (*f == '%' && *(f + 1) && arg_idx < argc) {
            if (*(f + 1) == 's') {
                printf("%s", argv[arg_idx++]);
                f += 2;
            } else if (*(f + 1) == 'd' || *(f + 1) == 'i') {
                printf("%d", atoi(argv[arg_idx++]));
                f += 2;
            } else if (*(f + 1) == 'n') {
                printf("%d", atoi(argv[arg_idx++]));
                f += 2;
            } else {
                putchar(*f++);
            }
        } else {
            putchar(*f++);
        }
    }
    return 0;
}

/* ============================================================================
 * read - read a line from stdin
 * ============================================================================ */

int builtin_read(shell_state_t *state, int argc, char *argv[]) {
    if (!state) return -1;
    bool prompt = false;
    const char *prompt_text = "";
    const char *var_name = NULL;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            prompt = true;
            prompt_text = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-r") == 0) {
            /* -r flag: don't interpret backslash escapes (ignored for now) */
            i++;
        } else {
            var_name = argv[i];
            i++;
        }
    }

    if (!var_name) {
        builtin_usage("read", "[-p prompt] variable");
        return 1;
    }

    if (prompt) {
        printf("%s", prompt_text);
        fflush(stdout);
    }

    char buf[SHELL_MAX_LINE];
    if (fgets(buf, sizeof(buf), stdin)) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        env_set(state, (char *)var_name, buf);
    }
    return 0;
}

/* ============================================================================
 * sleep - delay for a specified time
 * ============================================================================ */

int builtin_sleep(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("sleep", "N");
        return 1;
    }
    unsigned seconds = (unsigned)atoi(argv[1]);
    sleep(seconds);
    return 0;
}

/* ============================================================================
 * date - display current date/time
 * ============================================================================ */

int builtin_date(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    /* Simple ISO-ish format */
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", tm_info);
    printf("%s\n", buf);
    return 0;
}

/* ============================================================================
 * whoami - display current user
 * ============================================================================ */

int builtin_whoami(shell_state_t *state, int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!state) return -1;
    const char *user = env_lookup(state, "USER");
    if (!user) user = "root";
    printf("%s\n", user);
    return 0;
}

/* ============================================================================
 * clear - clear the screen
 * ============================================================================ */

int builtin_clear(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    /* ANSI escape to clear screen */
    printf("\033[2J\033[H");
    fflush(stdout);
    return 0;
}

/* ============================================================================
 * type - report command type
 * ============================================================================ */

int builtin_type(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("type", "name ...");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        /* For Phase 1 all known commands are built-ins */
        printf("%s is a shell built-in\n", argv[i]);
    }
    return 0;
}

/* ============================================================================
 * eval - evaluate arguments as shell command (stub)
 * ============================================================================ */

int builtin_eval(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("eval", "args...");
        return 1;
    }
    /* Simple eval: concatenate args and print (full eval would re-parse) */
    for (int i = 1; i < argc; i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i]);
    }
    printf("\n");
    return 0;
}

/* ============================================================================
 * wc - word/line/byte count (stub for file ops, inline for echo-like)
 * ============================================================================ */

int builtin_wc(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        /* Read from stdin */
        char buf[SHELL_MAX_LINE];
        int lines = 0, words = 0, chars = 0;
        while (fgets(buf, sizeof(buf), stdin)) {
            lines++;
            chars += (int)strlen(buf);
            const char *p = buf;
            bool in_word = false;
            while (*p) {
                if (isspace((unsigned char)*p)) {
                    in_word = false;
                } else if (!in_word) {
                    words++;
                    in_word = true;
                }
                p++;
            }
        }
        printf("%8d %8d %8d\n", lines, words, chars);
        return 0;
    }
    /* File arguments - stub */
    for (int i = 1; i < argc; i++) {
        printf("wc: %s - filesystem not available yet (Phase 2)\n", argv[i]);
    }
    return 0;
}

/* ============================================================================
 * head - show first lines (stub)
 * ============================================================================ */

int builtin_head(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    puts("head: filesystem not available yet (Phase 2)");
    return 0;
}

/* ============================================================================
 * tail - show last lines (stub)
 * ============================================================================ */

int builtin_tail(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    puts("tail: filesystem not available yet (Phase 2)");
    return 0;
}

/* ============================================================================
 * grep - search for patterns (stub)
 * ============================================================================ */

int builtin_grep(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    puts("grep: filesystem not available yet (Phase 2)");
    return 0;
}

/* ============================================================================
 * sort - sort lines (stub)
 * ============================================================================ */

int builtin_sort(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    puts("sort: filesystem not available yet (Phase 2)");
    return 0;
}

/* ============================================================================
 * cut - cut fields (stub)
 * ============================================================================ */

int builtin_cut(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    puts("cut: filesystem not available yet (Phase 2)");
    return 0;
}

/* ============================================================================
 * tr - translate characters (stub)
 * ============================================================================ */

int builtin_tr(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    puts("tr: filesystem not available yet (Phase 2)");
    return 0;
}

/* ============================================================================
 * basename - extract filename from path
 * ============================================================================ */

int builtin_basename(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("basename", "path [suffix]");
        return 1;
    }
    char *path = argv[1];
    char *slash = strrchr(path, '/');
    if (slash) {
        path = slash + 1;
    }
    /* Optional suffix removal */
    if (argc >= 3) {
        const char *suffix = argv[2];
        size_t path_len = strlen(path);
        size_t suffix_len = strlen(suffix);
        if (path_len > suffix_len &&
            strcmp(path + path_len - suffix_len, suffix) == 0) {
            path[path_len - suffix_len] = '\0';
        }
    }
    printf("%s\n", path);
    return 0;
}

/* ============================================================================
 * dirname - extract directory from path
 * ============================================================================ */

int builtin_dirname(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        builtin_usage("dirname", "path");
        return 1;
    }
    char path[SHELL_MAX_PATH_LEN];
    strncpy(path, argv[1], sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    char *slash = strrchr(path, '/');
    if (!slash) {
        printf(".\n");
    } else if (slash == path) {
        printf("/\n");
    } else {
        *slash = '\0';
        printf("%s\n", path);
    }
    return 0;
}

/* ============================================================================
 * true - always succeed
 * ============================================================================ */

int builtin_true(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    return 0;
}

/* ============================================================================
 * false - always fail
 * ============================================================================ */

int builtin_false(shell_state_t *state, int argc, char *argv[]) {
    (void)state; (void)argc; (void)argv;
    return 1;
}

/* ============================================================================
 * test - evaluate expression
 * ============================================================================ */

int builtin_test(shell_state_t *state, int argc, char *argv[]) {
    (void)state;
    if (argc < 2) {
        return 1;
    }

    /* String tests */
    if (argc == 2) {
        /* test STRING - true if string is non-empty */
        return strlen(argv[1]) > 0 ? 0 : 1;
    }

    /* File/string operators with 3 args: test ARG1 OP ARG2 */
    if (argc == 3) {
        const char *op = argv[2];

        /* String comparison */
        if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
            return (strcmp(argv[1], argv[3]) == 0) ? 0 : 1;
        }
        if (strcmp(op, "!=") == 0) {
            return (strcmp(argv[1], argv[3]) != 0) ? 0 : 1;
        }

        /* Numeric comparison */
        if (strcmp(op, "-eq") == 0) return (atoi(argv[1]) == atoi(argv[3])) ? 0 : 1;
        if (strcmp(op, "-ne") == 0) return (atoi(argv[1]) != atoi(argv[3])) ? 0 : 1;
        if (strcmp(op, "-lt") == 0) return (atoi(argv[1]) <  atoi(argv[3])) ? 0 : 1;
        if (strcmp(op, "-le") == 0) return (atoi(argv[1]) <= atoi(argv[3])) ? 0 : 1;
        if (strcmp(op, "-gt") == 0) return (atoi(argv[1]) >  atoi(argv[3])) ? 0 : 1;
        if (strcmp(op, "-ge") == 0) return (atoi(argv[1]) >= atoi(argv[3])) ? 0 : 1;

        /* String operators */
        if (strcmp(op, "-z") == 0) return (strlen(argv[3]) == 0) ? 0 : 1;
        if (strcmp(op, "-n") == 0) return (strlen(argv[3]) > 0) ? 0 : 1;

        /* Unknown operator with 3 args */
        fprintf(stderr, "test: %s: unexpected argument\n", op);
        return 1;
    }

    /* Unary flags: test -FLAG ARG */
    if (argc == 3 && argv[1][0] == '-') {
        const char *flag = argv[1];
        /* These would check filesystem state - stub for now */
        if (strcmp(flag, "-z") == 0) return (strlen(argv[2]) == 0) ? 0 : 1;
        if (strcmp(flag, "-n") == 0) return (strlen(argv[2]) > 0) ? 0 : 1;
        /* File tests are stubs */
        (void)flag;
        return 1;
    }

    /* Logical operators */
    if (argc >= 5) {
        if (strcmp(argv[2], "-a") == 0 || strcmp(argv[2], "-o") == 0) {
            /* Simplified: just check both sides are non-empty */
            bool left = strlen(argv[1]) > 0;
            bool right = strlen(argv[3]) > 0;
            if (strcmp(argv[2], "-a") == 0) {
                return (left && right) ? 0 : 1;
            } else {
                return (left || right) ? 0 : 1;
            }
        }
    }

    /* Fallback: if we got here with unexpected args */
    return 1;
}

/* ============================================================================
 * shell_execute_builtin - dispatch to the correct built-in
 * ============================================================================ */

int shell_execute_builtin(shell_state_t *state, const char *cmd,
                          int argc, char *argv[]) {
    if (!cmd) return -1;

    /* This is called by shell_core.c's find_builtin, but provided here
     * as an alternative dispatch mechanism if needed */
    (void)state; (void)cmd; (void)argc; (void)argv;
    return 0;
}