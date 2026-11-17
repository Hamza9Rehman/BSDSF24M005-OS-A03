#include "shell.h"

int main() {
    char* cmdline;

    // Bind Tab key to Readline's default completion behavior
    rl_bind_key('\t', rl_complete);

    while ((cmdline = readline(PROMPT)) != NULL) {
        char* trimmed = cmdline;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        // Handle !n re-execution before adding to history or parsing
        if (trimmed[0] == '!') {
            char* endptr;
            long n = strtol(trimmed + 1, &endptr, 10);

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

            printf("%s\n", recalled);
            free(cmdline);
            cmdline = recalled;
            trimmed = cmdline;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        }

        // Record non-empty command in both Readline history and internal history
        if (*trimmed != '\0') {
            add_history(cmdline);
            add_to_history(cmdline);
            parse_and_execute_line(cmdline);
        }

        free(cmdline);
    }

    free_history();
    clear_history();
    printf("\nShell exited.\n");
    return 0;
}