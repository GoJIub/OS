#include "contract.h"
#include <stdio.h>
#include <stdlib.h>

float Pi(int K) {
    float product = 1.0;
    for (int n = 1; n <= K; ++n)
        product *= 4.0f * n * n / (4.0f * n * n - 1.0f);
    return product * 2;
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
        tmp /= 3;
    }

    int total_len = digits + (is_negative ? 1 : 0);
    char* result = malloc((total_len + 1) * sizeof(char));
    if (!result) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    result[total_len] = '\0';

    for (int i = total_len - 1; i >= (is_negative ? 1 : 0); i--) {
        result[i] = '0' + (ux % 3);
        ux /= 3;
    }

    if (is_negative) result[0] = '-';

    return result;
}
