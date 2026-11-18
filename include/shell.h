#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LEN 512
#define MAXARGS 32
#define HISTORY_SIZE 20
#define MAX_JOBS 64
#define MAX_BLOCK_LINES 32
#define PROMPT "FCIT> "
#define SUB_PROMPT "> "

// Represents a single command with optional I/O redirections and background flag
typedef struct {
    char* args[MAXARGS + 1];
    char* infile;
    char* outfile;
    int background;
} Command;

// Represents an active background job
typedef struct {
    int job_id;
    pid_t pid;
    char* cmd;
    int active;
} Job;

// Linked-list node for shell variables
typedef struct VarNode {
    char* name;
    char* value;
    struct VarNode* next;
} VarNode;

// Core shell prototypes
int parse_and_execute_line(char* cmdline);
int execute_single(Command* cmd, const char* raw_cmd);
int execute_pipe(Command* left, Command* right, int background, const char* raw_cmd);
int handle_builtin(char** arglist, int* exit_status);
int is_if_statement(const char* line);
int handle_if_block(const char* first_line);

// Shell variable prototypes
void set_shell_var(const char* name, const char* value);
const char* get_shell_var(const char* name);
void print_shell_vars(void);
void free_shell_vars(void);

// Job control prototypes
void add_job(pid_t pid, const char* cmd);
void reap_zombies(void);
void print_jobs(void);
void free_jobs(void);

// History prototypes
void add_to_history(const char* cmd);
void print_history(void);
char* get_history_command(int index);
void free_history(void);

#endif // SHELL_H