#include "contract.h"
#include <stdio.h>
#include <stdlib.h>

float Pi(int K) {
    float sum = 0.0;
    for (int k = 0; k <= K; ++k)
        sum += 1.0f / (2 * k + 1) * (k % 2 ? -1 : 1);
    return sum * 4;
}

char* translation(long x) {
    if (x == 0) {
        char* result = malloc(2 * sizeof(char));
        if (!result) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        result[0] = '0';
        result[1] = '\0';
        return result;
    }

    int is_negative = x < 0;
    unsigned long ux = (unsigned long)(is_negative ? -x : x);

    unsigned long tmp = ux;
    int digits = 0;
    while (tmp > 0) {
        digits++;
        tmp /= 2;
    }

    int total_len = digits + (is_negative ? 1 : 0);
    char* result = malloc((total_len + 1) * sizeof(char));
    if (!result) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    result[total_len] = '\0';

    for (int i = total_len - 1; i >= (is_negative ? 1 : 0); i--) {
        result[i] = '0' + (ux % 2);
        ux /= 2;
    }

    if (is_negative) result[0] = '-';

    return result;
}
