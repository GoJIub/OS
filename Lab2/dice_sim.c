#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint64_t winsA;
    uint64_t winsB;
    uint64_t ties;
} result_t;

typedef struct {
    int K;
    int cur_round;
    int A_total;
    int B_total;
    uint64_t trials;
    unsigned int seed;
    result_t local;
} thread_arg_t;

uint64_t g_winsA = 0;
uint64_t g_winsB = 0;
uint64_t g_ties  = 0;

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static inline int roll_die(unsigned int *seed) {
    return (rand_r(seed) % 6) + 1;
}

void *worker(void *argp) {
    thread_arg_t *arg = (thread_arg_t *)argp;
    int K = arg->K;
    int cur_round = arg->cur_round;
    int A_start = arg->A_total;
    int B_start = arg->B_total;
    uint64_t trials = arg->trials;
    unsigned int seed = arg->seed;

    int remaining_rounds = K - cur_round + 1;
    if (remaining_rounds < 0) remaining_rounds = 0;

    uint64_t winsA = 0, winsB = 0, ties = 0;

    for (uint64_t t = 0; t < trials; ++t) {
        int A = A_start;
        int B = B_start;
        for (int r = 0; r < remaining_rounds; ++r) {
            int a1 = roll_die(&seed);
            int a2 = roll_die(&seed);
            int b1 = roll_die(&seed);
            int b2 = roll_die(&seed);
            A += a1 + a2;
            B += b1 + b2;
        }
        if (A > B) ++winsA;
        else if (B > A) ++winsB;
        else ++ties;
    }

    pthread_mutex_lock(&g_lock);

    g_winsA += winsA;
    g_winsB += winsB;
    g_ties  += ties;

    pthread_mutex_unlock(&g_lock);

    return NULL;
}

double timespec_to_sec(const struct timespec *t) {
    return (double)t->tv_sec + (double)t->tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc != 7) {
        fprintf(stderr, "Usage: %s K cur_round A_total B_total N max_threads\n", argv[0]);
        return 1;
    }

    int K = atoi(argv[1]);
    int cur_round = atoi(argv[2]);
    int A_total = atoi(argv[3]);
    int B_total = atoi(argv[4]);
    uint64_t N = strtoull(argv[5], NULL, 10);
    int max_threads = atoi(argv[6]);
    if (K < 0 || cur_round < 1 || max_threads < 1 || N == 0) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    int P = max_threads;
    pthread_t *threads = calloc(P, sizeof(pthread_t));
    thread_arg_t *targs = calloc(P, sizeof(thread_arg_t));
    if (!threads || !targs) {
        perror("calloc");
        return 1;
    }

    uint64_t base = N / P;
    uint64_t rem = N % P;

    struct timespec tstart, tend;
    clock_gettime(CLOCK_MONOTONIC, &tstart);

    for (int i = 0; i < P; ++i) {
        targs[i].K = K;
        targs[i].cur_round = cur_round;
        targs[i].A_total = A_total;
        targs[i].B_total = B_total;
        targs[i].trials = base + (i < (int)rem ? 1 : 0);
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid() ^ (unsigned int)(i*1103515245);
        targs[i].seed = seed;

        int rc = pthread_create(&threads[i], NULL, worker, &targs[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create[%d] failed: %s\n", i, strerror(rc));
            for (int j = 0; j < i; ++j) pthread_join(threads[j], NULL);
            free(threads);
            free(targs);
            return 1;
        }
    }

    for (int i = 0; i < P; ++i) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &tend);

    double elapsed = timespec_to_sec(&tend) - timespec_to_sec(&tstart);

    printf("Simulations: %lu\n", N);
    printf("Threads used: %d\n", P);
    printf("K=%d, cur_round=%d, remaining_rounds=%d\n", K, cur_round, (K - cur_round + 1) < 0 ? 0 : (K - cur_round + 1));
    printf("Wins A: %lu (%.6f%%)\n", g_winsA, (double)g_winsA * 100.0 / N);
    printf("Wins B: %lu (%.6f%%)\n", g_winsB, (double)g_winsB * 100.0 / N);
    printf("Ties  : %lu (%.6f%%)\n", g_ties, (double)g_ties * 100.0 / N);
    printf("Elapsed time: %.6f s\n", elapsed);

    free(threads);
    free(targs);

    pthread_mutex_destroy(&g_lock);

    return 0;
}