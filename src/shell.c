#include "shell.h"

static char* history[HISTORY_SIZE];
static int history_count = 0;

static Job jobs_list[MAX_JOBS];
static int next_job_id = 1;

static VarNode* var_head = NULL;

/* ==================== SHELL VARIABLES FUNCTIONS ==================== */

void set_shell_var(const char* name, const char* value) {
    if (name == NULL || value == NULL) return;

    // Update existing variable if already defined
    VarNode* curr = var_head;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            free(curr->value);
            curr->value = strdup(value);
            return;
        }
        curr = curr->next;
    }

    // Otherwise prepend a new variable node
    VarNode* node = (VarNode*)malloc(sizeof(VarNode));
    if (!node) return;
    node->name = strdup(name);
    node->value = strdup(value);
    node->next = var_head;
    var_head = node;
}

const char* get_shell_var(const char* name) {
    if (name == NULL) return NULL;
    VarNode* curr = var_head;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }
    return NULL;
}

void print_shell_vars(void) {
    VarNode* curr = var_head;
    if (curr == NULL) {
        printf("No shell variables defined.\n");
        return;
    }
    while (curr != NULL) {
        printf("%s=%s\n", curr->name, curr->value);
        curr = curr->next;
    }
}

void free_shell_vars(void) {
    VarNode* curr = var_head;
    while (curr != NULL) {
        VarNode* next = curr->next;
        free(curr->name);
        free(curr->value);
        free(curr);
        curr = next;
    }
    var_head = NULL;
}

// Checks if token is of form VARNAME=value (valid identifier before '=', no spaces around '=')
static int try_handle_assignment(const char* token) {
    if (token == NULL) return 0;

    const char* eq = strchr(token, '=');
    if (eq == NULL || eq == token) return 0;

    // Validate variable name: must start with letter or '_', followed by alphanumeric or '_'
    if (!isalpha((unsigned char)token[0]) && token[0] != '_') return 0;
    for (const char* p = token + 1; p < eq; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') return 0;
    }

    size_t name_len = (size_t)(eq - token);
    char* name = (char*)malloc(name_len + 1);
    if (!name) return 1;
    strncpy(name, token, name_len);
    name[name_len] = '\0';

    const char* raw_val = eq + 1;
    char* val = NULL;

    // If the right-hand side starts with '$', expand it during assignment too (e.g., A=$B)
    if (raw_val[0] == '$' && raw_val[1] != '\0') {
        const char* looked_up = get_shell_var(raw_val + 1);
        val = strdup(looked_up ? looked_up : "");
    } else {
        val = strdup(raw_val);
    }

    set_shell_var(name, val);
    free(name);
    free(val);
    return 1;
}

// Expands any argument starting with '$' into its stored shell variable value
static void expand_command_variables(Command* cmd) {
    for (int i = 0; cmd->args[i] != NULL; i++) {
        if (cmd->args[i][0] == '$' && cmd->args[i][1] != '\0') {
            const char* var_name = cmd->args[i] + 1;
            const char* val = get_shell_var(var_name);
            free(cmd->args[i]);
            cmd->args[i] = strdup(val != NULL ? val : "");
        }
    }

    if (cmd->infile != NULL && cmd->infile[0] == '$' && cmd->infile[1] != '\0') {
        const char* val = get_shell_var(cmd->infile + 1);
        free(cmd->infile);
        cmd->infile = strdup(val != NULL ? val : "");
    }

    if (cmd->outfile != NULL && cmd->outfile[0] == '$' && cmd->outfile[1] != '\0') {
        const char* val = get_shell_var(cmd->outfile + 1);
        free(cmd->outfile);
        cmd->outfile = strdup(val != NULL ? val : "");
    }
}

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

/* ==================== IF-THEN-ELSE-FI CONTROL STRUCTURE ==================== */

static char* trim_whitespace(char* str) {
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                       str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
    return str;
}

int is_if_statement(const char* line) {
    if (line == NULL) return 0;
    while (*line == ' ' || *line == '\t') line++;
    return (strncmp(line, "if", 2) == 0 &&
            (line[2] == ' ' || line[2] == '\t' || line[2] == '\0'));
}

int handle_if_block(const char* first_line) {
    char* copy = strdup(first_line);
    if (!copy) return -1;

    char* p = trim_whitespace(copy);
    p += 2;
    p = trim_whitespace(p);

    if (*p == '\0') {
        fprintf(stderr, "syntax error: expected condition command after 'if'\n");
        free(copy);
        return -1;
    }

    char* cond_cmd = strdup(p);
    free(copy);

    char* then_cmds[MAX_BLOCK_LINES];
    char* else_cmds[MAX_BLOCK_LINES];
    int then_count = 0;
    int else_count = 0;

    int state = 0;
    int syntax_ok = 0;

    while (1) {
        char* next_line = readline(SUB_PROMPT);
        if (next_line == NULL) {
            fprintf(stderr, "syntax error: unexpected EOF while looking for matching 'fi'\n");
            break;
        }

        char* trimmed = trim_whitespace(next_line);

        if (strcmp(trimmed, "then") == 0) {
            if (state != 0) {
                fprintf(stderr, "syntax error: unexpected 'then'\n");
                free(next_line);
                break;
            }
            state = 1;
            free(next_line);
            continue;
        }

        if (strcmp(trimmed, "else") == 0) {
            if (state != 1) {
                fprintf(stderr, "syntax error: unexpected 'else'\n");
                free(next_line);
                break;
            }
            state = 2;
            free(next_line);
            continue;
        }

        if (strcmp(trimmed, "fi") == 0) {
            if (state == 0) {
                fprintf(stderr, "syntax error: 'fi' encountered before 'then'\n");
                free(next_line);
                break;
            }
            syntax_ok = 1;
            free(next_line);
            break;
        }

        if (*trimmed == '\0') {
            free(next_line);
            continue;
        }

        if (state == 0) {
            fprintf(stderr, "syntax error: expected 'then', got '%s'\n", trimmed);
            free(next_line);
            break;
        } else if (state == 1) {
            if (then_count < MAX_BLOCK_LINES) {
                then_cmds[then_count++] = strdup(trimmed);
            } else {
                fprintf(stderr, "error: maximum lines exceeded in 'then' block\n");
            }
        } else if (state == 2) {
            if (else_count < MAX_BLOCK_LINES) {
                else_cmds[else_count++] = strdup(trimmed);
            } else {
                fprintf(stderr, "error: maximum lines exceeded in 'else' block\n");
            }
        }

        free(next_line);
    }

    int final_status = 0;
    if (syntax_ok) {
        int cond_status = parse_and_execute_line(cond_cmd);

        if (cond_status == 0) {
            for (int i = 0; i < then_count; i++) {
                final_status = parse_and_execute_line(then_cmds[i]);
            }
        } else {
            for (int i = 0; i < else_count; i++) {
                final_status = parse_and_execute_line(else_cmds[i]);
            }
        }
    } else {
        final_status = -1;
    }

    free(cond_cmd);
    for (int i = 0; i < then_count; i++) free(then_cmds[i]);
    for (int i = 0; i < else_count; i++) free(else_cmds[i]);

    return final_status;
}

/* ==================== PARSING & EXECUTION ==================== */

// Pads '<', '>', and '|' outside of quotes so quoted strings remain untouched
static char* pad_operators(const char* line) {
    size_t len = strlen(line);
    char* padded = (char*)malloc(len * 3 + 1);
    if (!padded) return NULL;

    size_t j = 0;
    char quote = '\0';
    for (size_t i = 0; i < len; i++) {
        char c = line[i];
        if (quote == '\0' && (c == '"' || c == '\'')) {
            quote = c;
            padded[j++] = c;
        } else if (quote != '\0' && c == quote) {
            quote = '\0';
            padded[j++] = c;
        } else if (quote == '\0' && (c == '<' || c == '>' || c == '|')) {
            padded[j++] = ' ';
            padded[j++] = c;
            padded[j++] = ' ';
        } else {
            padded[j++] = c;
        }
    }
    padded[j] = '\0';
    return padded;
}

// Quote-aware tokenizer that preserves spaces inside "..." or '...' and strips outer quotes
static char* next_token(char** cursor) {
    char* p = *cursor;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }

    char* buf = (char*)malloc(strlen(p) + 1);
    if (!buf) return NULL;

    size_t j = 0;
    char quote = '\0';
    while (*p != '\0') {
        if (quote == '\0') {
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                break;
            } else if (*p == '"' || *p == '\'') {
                quote = *p++;
            } else {
                buf[j++] = *p++;
            }
        } else {
            if (*p == quote) {
                quote = '\0';
                p++;
            } else {
                buf[j++] = *p++;
            }
        }
    }
    buf[j] = '\0';
    *cursor = p;
    return buf;
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
    char* cursor = segment;
    char* tok = next_token(&cursor);

    while (tok != NULL) {
        if (strcmp(tok, "<") == 0) {
            free(tok);
            tok = next_token(&cursor);
            if (tok == NULL) {
                fprintf(stderr, "syntax error: expected filename after '<'\n");
                free_command(cmd);
                return -1;
            }
            free(cmd->infile);
            cmd->infile = tok;
        } else if (strcmp(tok, ">") == 0) {
            free(tok);
            tok = next_token(&cursor);
            if (tok == NULL) {
                fprintf(stderr, "syntax error: expected filename after '>'\n");
                free_command(cmd);
                return -1;
            }
            free(cmd->outfile);
            cmd->outfile = tok;
        } else {
            if (argc < MAXARGS) {
                cmd->args[argc++] = tok;
            } else {
                free(tok);
            }
        }
        tok = next_token(&cursor);
    }

    cmd->args[argc] = NULL;
    return argc;
}

static int execute_single_segment(char* segment) {
    while (*segment == ' ' || *segment == '\t') segment++;
    size_t len = strlen(segment);
    while (len > 0 && (segment[len - 1] == ' ' || segment[len - 1] == '\t' || segment[len - 1] == '\n')) {
        segment[--len] = '\0';
    }
    if (len == 0) return 0;

    char* raw_display = strdup(segment);

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

    // Check if segment contains a pipe '|' outside quotes
    char* pipe_pos = NULL;
    char quote = '\0';
    for (char* p = padded; *p != '\0'; p++) {
        if (quote == '\0' && (*p == '"' || *p == '\'')) quote = *p;
        else if (quote != '\0' && *p == quote) quote = '\0';
        else if (quote == '\0' && *p == '|') {
            pipe_pos = p;
            break;
        }
    }

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

        expand_command_variables(&left_cmd);
        expand_command_variables(&right_cmd);

        int status = execute_pipe(&left_cmd, &right_cmd, is_background, raw_display);
        free_command(&left_cmd);
        free_command(&right_cmd);
        free(padded);
        free(raw_display);
        return status;
    }

    Command cmd;
    int argc = parse_command_segment(padded, &cmd);
    free(padded);

    if (argc <= 0) {
        free(raw_display);
        return 0;
    }

    // Check for standalone variable assignment (e.g., MESSAGE="Hello, World!")
    if (argc == 1 && try_handle_assignment(cmd.args[0])) {
        free_command(&cmd);
        free(raw_display);
        return 0;
    }

    // Expand $VARNAME arguments before built-in or external execution
    expand_command_variables(&cmd);

    cmd.background = is_background;

    int status = 0;
    if (!handle_builtin(cmd.args, &status)) {
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

int handle_builtin(char** arglist, int* exit_status) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    // 1. exit
    if (strcmp(arglist[0], "exit") == 0) {
        free_history();
        free_jobs();
        free_shell_vars();
        clear_history();
        printf("Exiting shell...\n");
        exit(0);
    }

    // 2. cd <directory>
    if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL) {
            fprintf(stderr, "cd: expected argument to \"cd\"\n");
            if (exit_status) *exit_status = 1;
        } else {
            if (chdir(arglist[1]) != 0) {
                perror("cd failed");
                if (exit_status) *exit_status = 1;
            } else {
                if (exit_status) *exit_status = 0;
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
        printf("  set             : Display all defined shell variables\n");
        if (exit_status) *exit_status = 0;
        return 1;
    }

    // 4. history
    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        if (exit_status) *exit_status = 0;
        return 1;
    }

    // 5. jobs
    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        if (exit_status) *exit_status = 0;
        return 1;
    }

    // 6. set: Displays all shell variables
    if (strcmp(arglist[0], "set") == 0) {
        print_shell_vars();
        if (exit_status) *exit_status = 0;
        return 1;
    }

    return 0;
}