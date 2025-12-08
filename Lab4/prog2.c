#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef float (*pi_func_t)(int);
typedef char* (*trans_func_t)(long);

void* load_lib(const char* path, pi_func_t* pi_f, trans_func_t* tr_f) {
    dlerror();
    void* handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "dlopen(%s) failed: %s\n", path, dlerror());
        return NULL;
    }

    dlerror();
    *pi_f = (pi_func_t)dlsym(handle, "Pi");
    char* err = dlerror();
    if (err) { fprintf(stderr, "dlsym(Pi) failed: %s\n", err); dlclose(handle); return NULL; }

    dlerror();
    *tr_f = (trans_func_t)dlsym(handle, "translation");
    err = dlerror();
    if (err) { fprintf(stderr, "dlsym(translation) failed: %s\n", err); dlclose(handle); return NULL; }

    return handle;
}

int main() {
    const char* lib1 = "libs/libmath_dl1.so";
    const char* lib2 = "libs/libmath_dl2.so";

    void* handle = NULL;
    pi_func_t Pi = NULL;
    trans_func_t translation = NULL;

    handle = load_lib(lib1, &Pi, &translation);
    if (!handle) {
        fprintf(stderr, "Не удалось загрузить начальную библиотеку %s\n", lib1);
        return 1;
    }
    const char* current = lib1;

    printf("prog2 (runtime) ready. Commands:\n");
    printf("0              -> switch implementation (toggle)\n");
    printf("1 K            -> compute Pi(K)\n");
    printf("2 x            -> translation(x)\n");
    printf("q              -> quit\n");

    char *line = NULL;
    size_t len = 0;

    while (1) {
        printf("[%s] > ", current);
        ssize_t nread = getline(&line, &len, stdin);
        if (nread == -1) break;
        if (nread > 0 && line[nread - 1] == '\n') line[nread - 1] = '\0';
        if (line[0] == '\0') continue;
        if (line[0] == 'q') break;

        char *tok = strtok(line, " \t");
        if (!tok) continue;

        if (tok[0] == '0') {
            const char* next = (current == lib1) ? lib2 : lib1;
            if (handle) {
                dlclose(handle);
                handle = NULL;
            }
            Pi = NULL;
            translation = NULL;
            handle = load_lib(next, &Pi, &translation);
            if (!handle) {
                fprintf(stderr, "Switch failed, attempting to reload previous\n");
                handle = load_lib(current, &Pi, &translation);
                if (!handle) {
                    fprintf(stderr, "Fatal: cannot reload previous library\n");
                    break;
                }
            } else {
                current = next;
                printf("Switched to %s\n", current);
            }
        } else if (tok[0] == '1') {
            tok = strtok(NULL, " \t");
            if (!tok) {
                printf("Missing K\n");
                continue;
            }
            int K = atoi(tok);
            if (!Pi) {
                printf("Function Pi not loaded\n");
                continue;
            }
            float p = Pi(K);
            printf("Pi approximation (K=%d): %.10f\n", K, (double)p);
        } else if (tok[0] == '2') {
            tok = strtok(NULL, " \t");
            if (!tok) {
                printf("Missing x\n");
                continue;
            }
            long x = atol(tok);
            if (!translation) {
                printf("Function translation not loaded\n");
                continue;
            }
            char* s = translation(x);
            printf("translation(%ld) = %s\n", x, s);
            free(s);
        } else printf("Unknown command\n");
    }

    if (handle) dlclose(handle);
    free(line);
    return 0;
}
