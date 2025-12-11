#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libs/contract.h"

int main() {
    char *line = NULL;
    size_t len = 0;

    printf("prog1 (linked at compile-time) ready. Commands:\n");
    printf("1 K            -> compute Pi(K)\n");
    printf("2 x            -> translation(x)\n");
    printf("q              -> quit\n");

    while (1) {
        printf("> ");

        ssize_t nread = getline(&line, &len, stdin);
        if (nread == -1) break;
        if (nread > 0 && line[nread - 1] == '\n') line[nread - 1] = '\0';
        if (line[0] == '\0') continue;
        if (line[0] == 'q') break;

        char *tok = strtok(line, " \t");
        if (!tok) continue;
        char cmd = tok[0];

        if (cmd == '1') {
            tok = strtok(NULL, " \t");
            if (!tok) { 
                printf("Missing K\n"); 
                continue; 
            }
            int K = atoi(tok);
            float p = Pi(K);
            printf("Pi approximation (K=%d): %.10f\n", K, (double)p);
        } 
        else if (cmd == '2') {
            tok = strtok(NULL, " \t");
            if (!tok) { 
                printf("Missing x\n"); 
                continue; 
            }
            long x = atol(tok);
            char *s = translation(x);
            printf("translation(%ld) = %s\n", x, s);
            free(s);
        } else printf("Unknown command\n");
    }

    free(line);
    return 0;
}
