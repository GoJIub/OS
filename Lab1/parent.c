#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/wait.h"

const int FN_SIZE = 256;

int create_process() {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    return pid;
}

int main() {
    int pipe_id[2];
    if (pipe(pipe_id) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    printf("Enter filename: ");
    char file_name[FN_SIZE];

    if (scanf("%s", file_name) != 1) {
        fprintf(stderr, "Error reading filename\n");
        return EXIT_FAILURE;
    }

    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    pid_t pid = create_process();
    if (pid == 0) {
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2 file->stdin");
            _exit(EXIT_FAILURE);
        }
        close(fd);

        close(pipe_id[0]);
        if (dup2(pipe_id[1], STDOUT_FILENO) == -1) {
            perror("dup2 pipe->stdout");
            _exit(EXIT_FAILURE);
        }
        close(pipe_id[1]);

        execl("./child.out", "child.out", NULL);
        perror("execl");
        _exit(EXIT_FAILURE);

    } else {
        close(fd);
        close(pipe_id[1]);

        if (dup2(pipe_id[0], STDIN_FILENO) == -1) {
            perror("dup2 pipe->stdin");
            return EXIT_FAILURE;
        }
        close(pipe_id[0]);

        float res;
        while (1) {
            int ret = scanf("%f", &res);
            if (ret == EOF) break;
            if (ret != 1) {
                fprintf(stderr, "Error reading result from child\n");
                break;
            }
            printf("Result of calculations: %f\n", res);
        }

        wait(NULL);
    }

    return EXIT_SUCCESS;
}