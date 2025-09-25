#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "sys/wait.h"

const int FN_SIZE = 256;

int create_process() {
    pid_t pid = fork();
    if (-1 == pid) {
        perror("fork");
        exit(-1);
    }
    return pid;
}

int main() {
    int pipe_id[2];
    int err = pipe(pipe_id);
    if (err == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    printf("Enter filename: ");
    char file_name[FN_SIZE];

    if (scanf("%s", file_name) != 1) {
        fprintf(stderr, "Error reading filename\n");
        return EXIT_FAILURE;
    }

    pid_t pid = create_process();
    if (pid == 0) {
        close(pipe_id[0]);
        if (dup2(pipe_id[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipe_id[1]);

        execl("./child.out", "child.out", file_name, NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    } else {
        close(pipe_id[1]);
        if (dup2(pipe_id[0], STDIN_FILENO) == -1) {
            perror("dup2");
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
}