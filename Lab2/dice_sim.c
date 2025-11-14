#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>

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
} thread_arg_t;

uint64_t g_winsA = 0;
uint64_t g_winsB = 0;
uint64_t g_ties  = 0;

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

pid_t getpid_checked(void) {
    pid_t pid = getpid();
    if (pid == -1) {
        perror("getpid");
        exit(EXIT_FAILURE);
    }
    printf("[syscall] getpid() = %d\n", pid);
    return pid;
}

time_t time_checked(time_t *tloc) {
    time_t t = time(tloc);
    if (t == (time_t)-1) {
        perror("time");
        exit(EXIT_FAILURE);
    }
    printf("[syscall] time() = %ld\n", t);
    return t;
}

int clock_gettime_checked(clockid_t clk_id, struct timespec *tp) {
    int rc = clock_gettime(clk_id, tp);
    if (rc == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    printf("[syscall] clock_gettime(%d) = 0, tv_sec=%ld, tv_nsec=%ld\n",
           clk_id, tp->tv_sec, tp->tv_nsec);
    return rc;
}

void *calloc_checked(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    printf("[syscall] calloc(%zu, %zu) = %p\n", nmemb, size, p);
    if (!p) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    return p;
}

int pthread_create_checked(pthread_t *thread, const pthread_attr_t *attr,
                           void *(*start_routine)(void *), void *arg) {
    int rc = pthread_create(thread, attr, start_routine, arg);
    printf("[syscall] pthread_create() = %d\n", rc);
    if (rc != 0) {
        errno = rc;
        perror("pthread_create");
        return rc;
    }
    return 0;
}

int pthread_join_checked(pthread_t thread, void **retval) {
    int rc = pthread_join(thread, retval);
    printf("[syscall] pthread_join() = %d\n", rc);
    if (rc != 0) {
        errno = rc;
        perror("pthread_join");
        return rc;
    }
    return 0;
}

int pthread_mutex_lock_checked(pthread_mutex_t *m) {
    int rc = pthread_mutex_lock(m);
    printf("[syscall] pthread_mutex_lock() = %d\n", rc);
    if (rc != 0) {
        errno = rc;
        perror("pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int pthread_mutex_unlock_checked(pthread_mutex_t *m) {
    int rc = pthread_mutex_unlock(m);
    printf("[syscall] pthread_mutex_unlock() = %d\n", rc);
    if (rc != 0) {
        errno = rc;
        perror("pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int pthread_mutex_destroy_checked(pthread_mutex_t *m) {
    int rc = pthread_mutex_destroy(m);
    printf("[syscall] pthread_mutex_destroy() = %d\n", rc);
    if (rc != 0) {
        errno = rc;
        perror("pthread_mutex_destroy");
        exit(EXIT_FAILURE);
    }
    return 0;
}

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

    pthread_mutex_lock_checked(&g_lock);
    g_winsA += winsA;
    g_winsB += winsB;
    g_ties  += ties;
    pthread_mutex_unlock_checked(&g_lock);

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

    int P = max_threads;

    pthread_t *threads = calloc_checked(P, sizeof(pthread_t));
    thread_arg_t *targs = calloc_checked(P, sizeof(thread_arg_t));

    if (!threads || !targs) return 1;

    uint64_t base = N / P;
    uint64_t rem = N % P;

    struct timespec tstart, tend;
    clock_gettime_checked(CLOCK_MONOTONIC, &tstart);

    for (int i = 0; i < P; ++i) {
        targs[i].K = K;
        targs[i].cur_round = cur_round;
        targs[i].A_total = A_total;
        targs[i].B_total = B_total;
        targs[i].trials = base + (i < (int)rem ? 1 : 0);
        targs[i].seed = (unsigned int)time_checked(NULL) ^ (unsigned int)getpid_checked() ^ (unsigned int)(i*1103515245);

        int rc = pthread_create_checked(&threads[i], NULL, worker, &targs[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create[%d] failed: %s\n", i, strerror(rc));
            for (int j = 0; j < i; ++j) pthread_join_checked(threads[j], NULL);
            free(threads);
            free(targs);
            return 1;
        }
    }

    for (int i = 0; i < P; ++i) pthread_join_checked(threads[i], NULL);

    clock_gettime_checked(CLOCK_MONOTONIC, &tend);
    double elapsed = timespec_to_sec(&tend) - timespec_to_sec(&tstart);

    printf("Simulations: %lu\n", N);
    printf("Threads used: %d\n", P);
    printf("K=%d, cur_round=%d, remaining_rounds=%d\n", K, cur_round, (K - cur_round + 1) < 0 ? 0 : (K - cur_round + 1));
    printf("Wins A: %lu (%.6f%%)\n", g_winsA, (double)g_winsA * 100.0 / N);
    printf("Wins B: %lu (%.6f%%)\n", g_winsB, (double)g_winsB * 100.0 / N);
    printf("Ties  : %lu (%.6f%%)\n", g_ties, (double)g_ties * 100.0 / N);
    printf("Elapsed time: %.6f s\n", elapsed);

    pthread_mutex_destroy_checked(&g_lock);

    free(threads);
    free(targs);

    return 0;
}