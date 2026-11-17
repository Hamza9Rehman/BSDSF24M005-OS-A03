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
#define PROMPT "FCIT> "

// Represents a single command with optional I/O redirections
typedef struct {
    char* args[MAXARGS + 1];
    char* infile;
    char* outfile;
} Command;

// Core shell prototypes
int parse_and_execute_line(char* cmdline);
int execute_single(Command* cmd);
int execute_pipe(Command* left, Command* right);
int handle_builtin(char** arglist);

// History prototypes
void add_to_history(const char* cmd);
void print_history(void);
char* get_history_command(int index);
void free_history(void);

#endif // SHELL_H