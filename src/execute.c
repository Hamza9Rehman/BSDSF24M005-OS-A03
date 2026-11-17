#include "shell.h"

// Helper called ONLY inside a child process before execvp()
static void apply_redirections(Command* cmd) {
    if (cmd->infile != NULL) {
        int fd_in = open(cmd->infile, O_RDONLY);
        if (fd_in < 0) {
            perror(cmd->infile);
            exit(1);
        }
        if (dup2(fd_in, STDIN_FILENO) < 0) {
            perror("dup2 infile failed");
            close(fd_in);
            exit(1);
        }
        close(fd_in);
    }

    if (cmd->outfile != NULL) {
        int fd_out = open(cmd->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            perror(cmd->outfile);
            exit(1);
        }
        if (dup2(fd_out, STDOUT_FILENO) < 0) {
            perror("dup2 outfile failed");
            close(fd_out);
            exit(1);
        }
        close(fd_out);
    }
}

int execute_single(Command* cmd, const char* raw_cmd) {
    if (cmd == NULL || cmd->args[0] == NULL) {
        return 0;
    }

    int status;
    pid_t cpid = fork();

    switch (cpid) {
        case -1:
            perror("fork failed");
            return -1;
        case 0: // Child process
            apply_redirections(cmd);
            execvp(cmd->args[0], cmd->args);
            perror(cmd->args[0]);
            exit(1);
        default: // Parent process
            if (cmd->background) {
                add_job(cpid, raw_cmd);
                return 0;
            } else {
                waitpid(cpid, &status, 0);
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
    }
}

int execute_pipe(Command* left, Command* right, int background, const char* raw_cmd) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe failed");
        return -1;
    }

    // Fork left child (writer)
    pid_t pid_left = fork();
    if (pid_left < 0) {
        perror("fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid_left == 0) {
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("dup2 pipe write failed");
            exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);

        apply_redirections(left);
        execvp(left->args[0], left->args);
        perror(left->args[0]);
        exit(1);
    }

    // Fork right child (reader)
    pid_t pid_right = fork();
    if (pid_right < 0) {
        perror("fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(pid_left, NULL, 0);
        return -1;
    }

    if (pid_right == 0) {
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            perror("dup2 pipe read failed");
            exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);

        apply_redirections(right);
        execvp(right->args[0], right->args);
        perror(right->args[0]);
        exit(1);
    }

    // Parent closes both pipe ends immediately
    close(pipefd[0]);
    close(pipefd[1]);

    if (background) {
        // Track the pipeline leader/reader PID in background jobs
        add_job(pid_right, raw_cmd);
        return 0;
    }

    int status_left, status_right;
    waitpid(pid_left, &status_left, 0);
    waitpid(pid_right, &status_right, 0);

    return WIFEXITED(status_right) ? WEXITSTATUS(status_right) : -1;
}