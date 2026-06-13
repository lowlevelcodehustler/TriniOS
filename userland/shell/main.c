/*
 * TriniOS Basic Shell - Entry Point
 *
 * Main entry point for the shell executable.
 * Initializes the shell state and starts the REPL loop.
 */

#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Usage
 * ============================================================================ */

static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -i         Force interactive mode\n");
    printf("  -n         Do not read .profile (non-login shell)\n");
    printf("  -c CMD     Execute command string\n");
    printf("  -s         Read commands from stdin\n");
    printf("  -v         Print shell version\n");
    printf("  -h         Show this help\n");
}

/* ============================================================================
 * Version
 * ============================================================================ */

static void print_version(void) {
    printf("TriniOS Shell version 0.1.0\n");
    printf("Phase 1 - Basic command interpreter (AI-naive)\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    bool interactive = true;
    bool login = true;
    const char *cmd_string = NULL;

    /* Parse shell options */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "-i") == 0) {
            interactive = true;
        }
        if (strcmp(argv[i], "-n") == 0) {
            login = false;
        }
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cmd_string = argv[++i];
            interactive = false;
        }
        if (strcmp(argv[i], "-s") == 0) {
            /* Read from stdin (default behavior) */
        }
    }

    /* Initialize shell state */
    shell_state_t state;
    shell_init(&state, interactive, login);

    /* If a command string was provided, execute it and exit */
    if (cmd_string) {
        shell_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        if (shell_parse_line(cmd_string, &cmd) == 0) {
            /* Simple execution for -c mode */
            printf("Executing: %s\n", cmd_string);
            /* In a full implementation we'd call the builtin directly here */
            if (cmd.program) free(cmd.program);
            for (int i = 0; i < cmd.arg_count; i++) {
                if (cmd.args[i]) free(cmd.args[i]);
            }
        }
        shell_destroy(&state);
        return state.last_exit_status;
    }

    /* Run the interactive REPL loop */
    int exit_code = shell_run(&state);

    /* Cleanup */
    shell_destroy(&state);

    return exit_code;
}