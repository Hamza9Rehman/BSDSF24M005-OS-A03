#include "shell.h"

static char* history[HISTORY_SIZE];
static int history_count = 0;

void add_to_history(const char* cmd) {
    if (cmd == NULL) return;

    const char* p = cmd;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (*p == '\0') return;

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

// Inserts spaces around '<', '>', and '|' so attached syntax like "ls>out.txt" parses cleanly
static char* pad_operators(const char* line) {
    size_t len = strlen(line);
    char* padded = (char*)malloc(len * 3 + 1);
    if (!padded) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (line[i] == '<' || line[i] == '>' || line[i] == '|') {
            padded[j++] = ' ';
            padded[j++] = line[i];
            padded[j++] = ' ';
        } else {
            padded[j++] = line[i];
        }
    }
    padded[j] = '\0';
    return padded;
}

static void init_command(Command* cmd) {
    for (int i = 0; i <= MAXARGS; i++) {
        cmd->args[i] = NULL;
    }
    cmd->infile = NULL;
    cmd->outfile = NULL;
}

static void free_command(Command* cmd) {
    for (int i = 0; cmd->args[i] != NULL; i++) {
        free(cmd->args[i]);
        cmd->args[i] = NULL;
    }
    free(cmd->infile);
    cmd->infile = NULL;
    free(cmd->outfile);
    cmd->outfile = NULL;
}

// Parses a single command segment (no pipes) into a Command struct
static int parse_command_segment(char* segment, Command* cmd) {
    init_command(cmd);
    int argc = 0;
    char* saveptr = NULL;
    char* tok = strtok_r(segment, " \t\r\n", &saveptr);

    while (tok != NULL) {
        if (strcmp(tok, "<") == 0) {
            tok = strtok_r(NULL, " \t\r\n", &saveptr);
            if (tok == NULL) {
                fprintf(stderr, "syntax error: expected filename after '<'\n");
                free_command(cmd);
                return -1;
            }
            free(cmd->infile);
            cmd->infile = strdup(tok);
        } else if (strcmp(tok, ">") == 0) {
            tok = strtok_r(NULL, " \t\r\n", &saveptr);
            if (tok == NULL) {
                fprintf(stderr, "syntax error: expected filename after '>'\n");
                free_command(cmd);
                return -1;
            }
            free(cmd->outfile);
            cmd->outfile = strdup(tok);
        } else {
            if (argc < MAXARGS) {
                cmd->args[argc++] = strdup(tok);
            }
        }
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    cmd->args[argc] = NULL;
    return argc;
}

int parse_and_execute_line(char* cmdline) {
    if (cmdline == NULL) return 0;

    char* padded = pad_operators(cmdline);
    if (!padded) return -1;

    // Check if the command contains a pipe '|'
    char* pipe_pos = strchr(padded, '|');
    if (pipe_pos != NULL) {
        *pipe_pos = '\0';
        char* left_str = padded;
        char* right_str = pipe_pos + 1;

        Command left_cmd, right_cmd;
        int left_argc = parse_command_segment(left_str, &left_cmd);
        int right_argc = parse_command_segment(right_str, &right_cmd);

        if (left_argc <= 0 || right_argc <= 0) {
            if (left_argc == 0 || right_argc == 0) {
                fprintf(stderr, "syntax error: invalid command around '|'\n");
            }
            if (left_argc > 0) free_command(&left_cmd);
            if (right_argc > 0) free_command(&right_cmd);
            free(padded);
            return -1;
        }

        int status = execute_pipe(&left_cmd, &right_cmd);
        free_command(&left_cmd);
        free_command(&right_cmd);
        free(padded);
        return status;
    }

    // No pipe: parse single command
    Command cmd;
    int argc = parse_command_segment(padded, &cmd);
    free(padded);

    if (argc <= 0) {
        return 0;
    }

    int status = 0;
    if (!handle_builtin(cmd.args)) {
        status = execute_single(&cmd);
    }

    free_command(&cmd);
    return status;
}

int handle_builtin(char** arglist) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    // 1. exit
    if (strcmp(arglist[0], "exit") == 0) {
        free_history();
        clear_history();
        printf("Exiting shell...\n");
        exit(0);
    }

    // 2. cd <directory>
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

    // 3. help
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

    // 4. history
    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    // 5. jobs (Placeholder for Feature-6)
    if (strcmp(arglist[0], "jobs") == 0) {
        printf("Job control not yet implemented.\n");
        return 1;
    }

    return 0;
}