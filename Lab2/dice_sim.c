/* dice_sim.c
   Компиляция: gcc -O2 -pthread -std=c11 -o dice_sim.out dice_sim.c
   Запуск: ./dice_sim.out K cur_round A_total B_total N max_threads
   Пример: ./dice_sim.out 10 3 7 5 1000000 4

   Изменения по сравнению с оригиналом:
   - Добавлен pthread_barrier_t для гарантированного ожидания старта потоков.
   - Каждый поток печатает свой TID (syscall(SYS_gettid)).
   - main ждёт появления всех LWP в /proc/<PID>/task и печатает список потоков,
     затем отпускает барьер — потоки продолжают основную работу.
   - Оставлены атомарные счётчики для итогов.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <dirent.h>

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
    int idx; // индекс потока для отладки
} thread_arg_t;

// global atomic counters
atomic_uint_fast64_t g_winsA;
atomic_uint_fast64_t g_winsB;
atomic_uint_fast64_t g_ties;

pthread_barrier_t start_barrier;
int P_global = 0; // число рабочих потоков (для печати ошибок барьера)

static inline int roll_die(unsigned int *seed) {
    return (rand_r(seed) % 6) + 1;
}

// подсчитать каталоги в /proc/<pid>/task
static int proc_task_count(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    DIR *d = opendir(path);
    if (!d) return -1;
    int cnt = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0) continue;
        if (strcmp(ent->d_name, "..") == 0) continue;
        cnt++;
    }
    closedir(d);
    return cnt;
}

// распечатать содержимое /proc/<pid>/task
static void print_proc_task(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    DIR *d = opendir(path);
    if (!d) {
        perror("opendir(/proc/.../task)");
        return;
    }
    printf("/proc/%d/task contents:\n", pid);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0) continue;
        if (strcmp(ent->d_name, "..") == 0) continue;
        printf("  %s\n", ent->d_name);
    }
    closedir(d);
}

void *worker(void *argp) {
    thread_arg_t *arg = (thread_arg_t *)argp;
    int K = arg->K;
    int cur_round = arg->cur_round;
    int A_start = arg->A_total;
    int B_start = arg->B_total;
    uint64_t trials = arg->trials;
    unsigned int seed = arg->seed;

    // показать, что поток стартовал
    pid_t tid = (pid_t) syscall(SYS_gettid);
    printf("[worker %d] started: pthread_self=%lu, TID=%d, trials=%" PRIu64 "\n",
           arg->idx, (unsigned long)pthread_self(), (int)tid, trials);
    fflush(stdout);

    // Ждём, пока main не отпустит барьер
    int rc = pthread_barrier_wait(&start_barrier);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        fprintf(stderr, "[worker %d] barrier_wait failed: %d\n", arg->idx, rc);
        // продолжаем работу, но это необычно
    }

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

    arg->local.winsA = winsA;
    arg->local.winsB = winsB;
    arg->local.ties = ties;

    // atomically add to globals
    atomic_fetch_add(&g_winsA, winsA);
    atomic_fetch_add(&g_winsB, winsB);
    atomic_fetch_add(&g_ties, ties);

    return NULL;
}

static double timespec_to_sec(const struct timespec *t) {
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

    // initialize atomic globals
    atomic_init(&g_winsA, 0);
    atomic_init(&g_winsB, 0);
    atomic_init(&g_ties, 0);

    int P = max_threads;
    P_global = P;

    pthread_t *threads = calloc(P, sizeof(pthread_t));
    thread_arg_t *targs = calloc(P, sizeof(thread_arg_t));
    if (!threads || !targs) {
        perror("calloc");
        return 1;
    }

    // distribute trials as evenly as possible
    uint64_t base = N / P;
    uint64_t rem = N % P;

    // init barrier for P workers + main
    if (pthread_barrier_init(&start_barrier, NULL, P + 1) != 0) {
        perror("pthread_barrier_init");
        free(threads);
        free(targs);
        return 1;
    }

    // create threads
    for (int i = 0; i < P; ++i) {
        targs[i].K = K;
        targs[i].cur_round = cur_round;
        targs[i].A_total = A_total;
        targs[i].B_total = B_total;
        targs[i].trials = base + (i < (int)rem ? 1 : 0);
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid() ^ (unsigned int)(i*1103515245);
        targs[i].seed = seed;
        targs[i].local.winsA = targs[i].local.winsB = targs[i].local.ties = 0;
        targs[i].idx = i;

        int rc = pthread_create(&threads[i], NULL, worker, &targs[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create[%d] failed: %s\n", i, strerror(rc));
            // join previously created threads and exit
            for (int j = 0; j < i; ++j) pthread_join(threads[j], NULL);
            pthread_barrier_destroy(&start_barrier);
            free(threads);
            free(targs);
            return 1;
        }
    }

    // ждём, пока ОС зарегистрирует все LWP (таймаут ~5s)
    pid_t mypid = getpid();
    int expected = P + 1; // main + workers
    int seen = 0;
    int attempts = 0;
    const int max_attempts = 50; // 50 * 100ms = 5s
    while (attempts++ < max_attempts) {
        seen = proc_task_count(mypid);
        if (seen == expected) break;
        usleep(100000); // 100 ms
    }

    printf("PID процесса: %d\n", mypid);
    printf("Ожидалось потоков: %d; Система видит: %d\n", expected, seen);
    print_proc_task(mypid);
    fflush(stdout);

    // отпускаем барьер, чтобы потоки начали основную работу
    int rc = pthread_barrier_wait(&start_barrier);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        fprintf(stderr, "[main] barrier_wait failed: %d\n", rc);
    }

    struct timespec tstart, tend;
    clock_gettime(CLOCK_MONOTONIC, &tstart);

    // join
    for (int i = 0; i < P; ++i) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &tend);

    // gather results
    uint64_t winsA = atomic_load(&g_winsA);
    uint64_t winsB = atomic_load(&g_winsB);
    uint64_t ties  = atomic_load(&g_ties);

    double elapsed = timespec_to_sec(&tend) - timespec_to_sec(&tstart);

    printf("Simulations: %" PRIu64 "\n", N);
    printf("Threads used: %d\n", P);
    printf("K=%d, cur_round=%d, remaining_rounds=%d\n", K, cur_round, (K - cur_round + 1) < 0 ? 0 : (K - cur_round + 1));
    printf("Wins A: %" PRIu64 " (%.6f%%)\n", winsA, (double)winsA * 100.0 / N);
    printf("Wins B: %" PRIu64 " (%.6f%%)\n", winsB, (double)winsB * 100.0 / N);
    printf("Ties  : %" PRIu64 " (%.6f%%)\n", ties, (double)ties * 100.0 / N);
    printf("Elapsed time: %.6f s\n", elapsed);

    pthread_barrier_destroy(&start_barrier);
    free(threads);
    free(targs);
    return 0;
}
