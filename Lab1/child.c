#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

int main() {
    size_t len;
    char* line;
    while (getline(&line, &len, stdin) != -1) {
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