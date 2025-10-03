#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

const int BUF_SIZE = 256;

int main() {
    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), stdin)) {
        char *ptr = line;
        float x, res = 0;
        while (sscanf(ptr, "%f", &x) == 1) {
            res += x;
            while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n') ptr++;
            while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
        }
        printf("%f\n", res);
    }
}