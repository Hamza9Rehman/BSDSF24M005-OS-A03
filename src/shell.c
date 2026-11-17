#include "shell.h"

static char* history[HISTORY_SIZE];
static int history_count = 0;

static Job jobs_list[MAX_JOBS];
static int next_job_id = 1;

/* ==================== JOB CONTROL FUNCTIONS ==================== */

void add_job(pid_t pid, const char* cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs_list[i].active) {
            jobs_list[i].job_id = next_job_id++;
            jobs_list[i].pid = pid;
            jobs_list[i].cmd = strdup(cmd ? cmd : "");
            jobs_list[i].active = 1;
            printf("[%d] %d\n", jobs_list[i].job_id, pid);
            return;
        }
    }
    fprintf(stderr, "jobs: maximum background job limit reached\n");
}

void reap_zombies(void) {
    int status;
    pid_t pid;

    // Non-blocking loop to reap all terminated background children
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs_list[i].active && jobs_list[i].pid == pid) {
                printf("\n[%d] Done\t\t%s\n", jobs_list[i].job_id, jobs_list[i].cmd);
                free(jobs_list[i].cmd);
                jobs_list[i].cmd = NULL;
                jobs_list[i].active = 0;
                break;
            }
        }
    }

    // Reset job ID counter if no active background jobs remain
    int any_active = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs_list[i].active) {
            any_active = 1;
            break;
        }
    }
    if (!any_active) {
        next_job_id = 1;
    }
}

void print_jobs(void) {
    reap_zombies();
    int found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs_list[i].active) {
            printf("[%d] Running\tPID: %d\t%s\n",
                   jobs_list[i].job_id, jobs_list[i].pid, jobs_list[i].cmd);
            found = 1;
        }
    }
    if (!found) {
        printf("No active background jobs.\n");
    }
}

void free_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs_list[i].active) {
            free(jobs_list[i].cmd);
            jobs_list[i].cmd = NULL;
            jobs_list[i].active = 0;
        }
    }
}

/* ==================== HISTORY FUNCTIONS ==================== */

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

/* ==================== PARSING & EXECUTION ==================== */

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
    cmd->background = 0;
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

// Executes a single semicolon-delimited segment (which may contain '|' and/or trailing '&')
static int execute_single_segment(char* segment) {
    // Trim leading whitespace
    while (*segment == ' ' || *segment == '\t') segment++;
    size_t len = strlen(segment);
    while (len > 0 && (segment[len - 1] == ' ' || segment[len - 1] == '\t' || segment[len - 1] == '\n')) {
        segment[--len] = '\0';
    }
    if (len == 0) return 0;

    // Save clean display string for jobs listing
    char* raw_display = strdup(segment);

    // Check for trailing '&' for background execution
    int is_background = 0;
    if (segment[len - 1] == '&') {
        is_background = 1;
        segment[--len] = '\0';
        while (len > 0 && (segment[len - 1] == ' ' || segment[len - 1] == '\t')) {
            segment[--len] = '\0';
        }
    }

    if (len == 0) {
        fprintf(stderr, "syntax error near unexpected token '&'\n");
        free(raw_display);
        return -1;
    }

    char* padded = pad_operators(segment);
    if (!padded) {
        free(raw_display);
        return -1;
    }

    // Check if segment contains a pipe '|'
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
            free(raw_display);
            return -1;
        }

        int status = execute_pipe(&left_cmd, &right_cmd, is_background, raw_display);
        free_command(&left_cmd);
        free_command(&right_cmd);
        free(padded);
        free(raw_display);
        return status;
    }

    // Single command (no pipe)
    Command cmd;
    int argc = parse_command_segment(padded, &cmd);
    free(padded);

    if (argc <= 0) {
        free(raw_display);
        return 0;
    }

    cmd.background = is_background;

    int status = 0;
    if (!handle_builtin(cmd.args)) {
        status = execute_single(&cmd, raw_display);
    }

    free_command(&cmd);
    free(raw_display);
    return status;
}

int parse_and_execute_line(char* cmdline) {
    if (cmdline == NULL) return 0;

    char* line_copy = strdup(cmdline);
    if (!line_copy) return -1;

    int last_status = 0;
    char* saveptr = NULL;
    char* segment = strtok_r(line_copy, ";", &saveptr);

    while (segment != NULL) {
        last_status = execute_single_segment(segment);
        segment = strtok_r(NULL, ";", &saveptr);
    }

    free(line_copy);
    return last_status;
}

int handle_builtin(char** arglist) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    // 1. exit
    if (strcmp(arglist[0], "exit") == 0) {
        free_history();
        free_jobs();
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
        printf("  jobs            : Display active background jobs\n");
        return 1;
    }

    // 4. history
    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    // 5. jobs: Lists active background jobs after reaping completed ones
    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        return 1;
    }

    return 0;
}