#include "shell.h"

static char* history[HISTORY_SIZE];
static int history_count = 0;

void add_to_history(const char* cmd) {
    if (cmd == NULL) return;

    // Skip whitespace-only commands
    const char* p = cmd;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (*p == '\0') return;

    // If history is full, free the oldest entry and shift left
    if (history_count == HISTORY_SIZE) {
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history_count--;
    }

    history[history_count] = strdup(cmd);
    if (history[history_count] != NULL) {
        history_count++;
    }
}

void print_history(void) {
    for (int i = 0; i < history_count; i++) {
        printf("%d  %s\n", i + 1, history[i]);
    }
}

char* get_history_command(int index) {
    if (index < 1 || index > history_count) {
        return NULL;
    }
    return strdup(history[index - 1]);
}

void free_history(void) {
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
        history[i] = NULL;
    }
    history_count = 0;
}

char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    char* cmdline = (char*) malloc(sizeof(char) * MAX_LEN);
    int c, pos = 0;

    while ((c = getc(fp)) != EOF) {
        if (c == '\n') break;
        cmdline[pos++] = c;
    }

    if (c == EOF && pos == 0) {
        free(cmdline);
        return NULL; // Handle Ctrl+D
    }
    
    cmdline[pos] = '\0';
    return cmdline;
}

char** tokenize(char* cmdline) {
    // Edge case: empty command line
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    for (int i = 0; i < MAXARGS + 1; i++) {
        arglist[i] = (char*)malloc(sizeof(char) * ARGLEN);
        bzero(arglist[i], ARGLEN);
    }

    char* cp = cmdline;
    char* start;
    int len;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++; // Skip leading whitespace
        
        if (*cp == '\0') break; // Line was only whitespace

        start = cp;
        len = 1;
        while (*++cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            len++;
        }
        strncpy(arglist[argnum], start, len);
        arglist[argnum][len] = '\0';
        argnum++;
    }

    if (argnum == 0) { // No arguments were parsed
        for(int i = 0; i < MAXARGS + 1; i++) free(arglist[i]);
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
}

int handle_builtin(char** arglist) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    // 1. exit: Terminates the shell gracefully
    if (strcmp(arglist[0], "exit") == 0) {
        free_history();
        printf("Exiting shell...\n");
        exit(0);
    }

    // 2. cd <directory>: Changes the current working directory
    if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL) {
            fprintf(stderr, "cd: expected argument to \"cd\"\n");
        } else {
            if (chdir(arglist[1]) != 0) {
                perror("cd failed");
            }
        }
        return 1;
    }

    // 3. help: Displays brief help for built-in commands
    if (strcmp(arglist[0], "help") == 0) {
        printf("FCIT Custom Shell - Built-in Commands:\n");
        printf("  cd <directory>  : Change the current working directory\n");
        printf("  exit            : Exit the shell\n");
        printf("  help            : Display this help message\n");
        printf("  history         : Display command history\n");
        printf("  !n              : Re-execute the n-th command from history\n");
        printf("  jobs            : Display background jobs\n");
        return 1;
    }

    // 4. history: Displays stored command history
    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    // 5. jobs: Placeholder for Feature-6
    if (strcmp(arglist[0], "jobs") == 0) {
        printf("Job control not yet implemented.\n");
        return 1;
    }

    return 0; // Not a built-in command
}