#include "shell.h"

int main() {
    char* cmdline;
    char** arglist;

    while ((cmdline = read_cmd(PROMPT, stdin)) != NULL) {
        // Skip leading whitespace when checking for !n
        char* trimmed = cmdline;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        // Handle !n re-execution before adding to history or tokenizing
        if (trimmed[0] == '!') {
            char* endptr;
            long n = strtol(trimmed + 1, &endptr, 10);

            // Ensure there are digits after '!' and no trailing garbage
            while (*endptr == ' ' || *endptr == '\t') endptr++;
            if (trimmed[1] == '\0' || !isdigit((unsigned char)trimmed[1]) || *endptr != '\0') {
                fprintf(stderr, "history: invalid event specification '%s'\n", trimmed);
                free(cmdline);
                continue;
            }

            char* recalled = get_history_command((int)n);
            if (recalled == NULL) {
                fprintf(stderr, "history: event %ld not found\n", n);
                free(cmdline);
                continue;
            }

            printf("%s\n", recalled); // Echo the resolved command
            free(cmdline);
            cmdline = recalled;
        }

        // Record non-empty command in history
        add_to_history(cmdline);

        if ((arglist = tokenize(cmdline)) != NULL) {
            // Check built-ins first before forking an external process
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }

            // Free the memory allocated by tokenize()
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        free(cmdline);
    }

    free_history();
    printf("\nShell exited.\n");
    return 0;
}