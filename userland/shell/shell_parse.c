/*
 * TriniOS Basic Shell - Parser
 *
 * Command line parsing: tokenization, quote handling,
 * escape processing, redirection, pipes, background jobs.
 */

#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Argument Parsing
 * ============================================================================ */

int shell_parse_args(const char *line, char *args[], int max_args) {
    if (!line || !args || max_args <= 0) return -1;

    char buf[SHELL_MAX_LINE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int count = 0;
    char *src = buf;
    char *dst = buf;

    while (*src && count < max_args - 1) {
        /* Skip whitespace */
        while (*src && isspace((unsigned char)*src)) src++;
        if (!*src) break;

        /* Save position for this argument */
        char *arg_start = dst;

        /* Process the argument */
        if (*src == '"') {
            /* Double-quoted string */
            src++;
            while (*src && *src != '"') {
                if (*src == '\\' && *(src + 1)) {
                    src++;
                }
                *dst++ = *src++;
            }
            if (*src == '"') src++;
        } else if (*src == '\'') {
            /* Single-quoted string (no escape processing) */
            src++;
            while (*src && *src != '\'') {
                *dst++ = *src++;
            }
            if (*src == '\'') src++;
        } else {
            /* Unquoted - stop at whitespace, pipe, redirect */
            while (*src && !isspace((unsigned char)*src) &&
                   *src != '|' && *src != '>' && *src != '<') {
                if (*src == '\\' && *(src + 1)) {
                    src++;
                }
                *dst++ = *src++;
            }
        }

        *dst++ = '\0';
        args[count++] = arg_start;
    }

    args[count] = NULL;
    return count;
}

/* ============================================================================
 * Line Parsing - Full Command with Redirection & Pipes
 * ============================================================================ */

int shell_parse_line(const char *line, shell_command_t *cmd) {
    if (!line || !cmd) return -1;

    memset(cmd, 0, sizeof(shell_command_t));

    /* Make a mutable copy */
    char buf[SHELL_MAX_LINE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Check for background job */
    char *amp = strchr(buf, '&');
    if (amp) {
        *amp = '\0';
        cmd->background = true;
    }

    /* Check for redirections (simple: last > or < wins) */
    char *redir = NULL;
    cmd->redir_type = SHELL_REDIR_NONE;

    /* Search for redirection operators */
    for (char *p = buf; *p; p++) {
        if (p[0] == '>' && p[1] == '>') {
            redir = p + 2;
            cmd->redir_type = SHELL_REDIR_APPEND;
            *p = '\0';
        } else if (p[0] == '2' && p[1] == '>') {
            redir = p + 2;
            cmd->redir_type = SHELL_REDIR_ERR_OUT;
            *p = '\0';
        } else if (*p == '&' && *(p+1) == '>') {
            redir = p + 2;
            cmd->redir_type = SHELL_REDIR_BOTH;
            *p = '\0';
        } else if (*p == '>') {
            redir = p + 1;
            cmd->redir_type = SHELL_REDIR_OUTPUT;
            *p = '\0';
        } else if (*p == '<') {
            redir = p + 1;
            cmd->redir_type = SHELL_REDIR_INPUT;
            *p = '\0';
        }
    }

    /* Trim redirection target */
    if (redir) {
        while (*redir && isspace((unsigned char)*redir)) redir++;
        strncpy(cmd->redir_path, redir, SHELL_MAX_PATH_LEN - 1);
    }

    /* Trim trailing whitespace from command */
    char *end = buf + strlen(buf);
    while (end > buf && isspace((unsigned char)*(end - 1))) {
        *(--end) = '\0';
    }

    /* Check for pipe (simple: first pipe splits) */
    char *pipe = strchr(buf, '|');
    if (pipe) {
        *pipe = '\0';
        cmd->type = SHELL_CMD_PIPE;
        /* For Phase 1, just use the left side of the pipe */
    }

    /* Parse arguments from the remaining command */
    if (buf[0] == '\0') {
        return 0; /* Empty command */
    }

    /* Allocate program name */
    /* Find first non-whitespace */
    char *start = buf;
    while (*start && isspace((unsigned char)*start)) start++;

    /* Find end of program name */
    char *pend = start;
    while (*pend && !isspace((unsigned char)*pend)) pend++;

    uint32_t prog_len = (uint32_t)(pend - start);
    cmd->program = (char *)malloc(prog_len + 1);
    if (!cmd->program) return -1;

    strncpy(cmd->program, start, prog_len);
    cmd->program[prog_len] = '\0';

    /* Parse remaining arguments */
    while (*pend && isspace((unsigned char)*pend)) pend++;

    cmd->arg_count = 0;
    while (*pend && cmd->arg_count < SHELL_MAX_ARGS - 1) {
        char *arg_start = pend;
        char *arg_end = arg_start;

        if (*arg_start == '"') {
            arg_start++;
            while (*arg_end && *arg_end != '"') {
                if (*arg_end == '\\' && *(arg_end + 1)) arg_end++;
                arg_end++;
            }
            if (*arg_end == '"') arg_end++;
        } else if (*arg_start == '\'') {
            arg_start++;
            while (*arg_end && *arg_end != '\'') arg_end++;
            if (*arg_end == '\'') arg_end++;
        } else {
            while (*arg_end && !isspace((unsigned char)*arg_end)) arg_end++;
        }

        uint32_t arg_len = (uint32_t)(arg_end - arg_start);
        cmd->args[cmd->arg_count] = (char *)malloc(arg_len + 1);
        if (cmd->args[cmd->arg_count]) {
            strncpy(cmd->args[cmd->arg_count], arg_start, arg_len);
            cmd->args[cmd->arg_count][arg_len] = '\0';
            cmd->arg_count++;
        }

        pend = arg_end;
        while (*pend && isspace((unsigned char)*pend)) pend++;
    }

    return 0;
}

/* ============================================================================
 * Variable Expansion (simple $VAR and ${VAR} support)
 * ============================================================================ */

static char *expand_variables(shell_state_t *state, const char *str) {
    if (!state || !str) return NULL;

    /* Simple implementation: allocate buffer and do expansion */
    char *result = (char *)malloc(SHELL_MAX_LINE);
    if (!result) return NULL;

    char *dst = result;
    const char *src = str;

    while (*src) {
        if (*src == '$' && *(src + 1)) {
            src++;
            if (*src == '{') {
                /* ${VAR} form */
                src++;
                const char *var_start = src;
                while (*src && *src != '}') src++;
                uint32_t var_len = (uint32_t)(src - var_start);

                /* Lookup in environment */
                char var_name[256];
                if (var_len >= sizeof(var_name)) var_len = sizeof(var_name) - 1;
                strncpy(var_name, var_start, var_len);
                var_name[var_len] = '\0';

                const char *value = NULL;
                for (int i = 0; i < state->env_count; i++) {
                    char *eq = strchr(state->env[i], '=');
                    if (eq) {
                        uint32_t nlen = (uint32_t)(eq - state->env[i]);
                        if (nlen == var_len &&
                            strncmp(state->env[i], var_name, var_len) == 0) {
                            value = eq + 1;
                            break;
                        }
                    }
                }

                if (value) {
                    uint32_t vlen = (uint32_t)strlen(value);
                    if ((dst - result) + vlen < SHELL_MAX_LINE - 1) {
                        strncpy(dst, value, vlen);
                        dst += vlen;
                    }
                }

                if (*src == '}') src++;
            } else if (isalpha((unsigned char)*src) || *src == '_') {
                /* $VAR form */
                const char *var_start = src;
                while (isalnum((unsigned char)*src) || *src == '_') src++;
                uint32_t var_len = (uint32_t)(src - var_start);

                char var_name[256];
                if (var_len >= sizeof(var_name)) var_len = sizeof(var_name) - 1;
                strncpy(var_name, var_start, var_len);
                var_name[var_len] = '\0';

                const char *value = NULL;
                for (int i = 0; i < state->env_count; i++) {
                    char *eq = strchr(state->env[i], '=');
                    if (eq) {
                        uint32_t nlen = (uint32_t)(eq - state->env[i]);
                        if (nlen == var_len &&
                            strncmp(state->env[i], var_name, var_len) == 0) {
                            value = eq + 1;
                            break;
                        }
                    }
                }

                if (value) {
                    uint32_t vlen = (uint32_t)strlen(value);
                    if ((dst - result) + vlen < SHELL_MAX_LINE - 1) {
                        strncpy(dst, value, vlen);
                        dst += vlen;
                    }
                }
            }
        } else {
            if ((dst - result) < SHELL_MAX_LINE - 1) {
                *dst++ = *src;
            }
            src++;
        }
    }

    *dst = '\0';
    return result;
}