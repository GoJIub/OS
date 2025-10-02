#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include <fcntl.h>

const int BUF_SIZE = 256;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Not enough arguments\n");
        exit(EXIT_FAILURE);
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    if (dup2(fd, STDIN_FILENO) == -1) {
        perror("dup2");
        close(fd);
        return EXIT_FAILURE;
    }
    close(fd);

    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), stdin)) {
        char *ptr = line;
        float x, res = 0;
        while (sscanf(ptr, "%f", &x) == 1) {
            res += x;
            // пропускаем число в строке
            while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n') ptr++;
            while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
        }
        printf("%f\n", res);
    }
}