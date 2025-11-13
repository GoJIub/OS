#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>

#define FN_SIZE 256

struct shmseg {
    char filename[FN_SIZE];  // имя файла
    float result;            // результат вычислений
    int done;                // флаг конца передачи: 0 = продолжаем, 1 = конец
    sem_t sem_file_empty;
    sem_t sem_file_full;
    sem_t sem_res_empty;
    sem_t sem_res_full;
};

int main() {
    const char *shm_name = "/shm_float";

    // создаём shared memory
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd == -1) { perror("shm_open"); exit(EXIT_FAILURE); }
    ftruncate(fd, sizeof(struct shmseg));

    struct shmseg *shm = mmap(NULL, sizeof(struct shmseg),
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); exit(EXIT_FAILURE); }

    // инициализация семафоров
    sem_init(&shm->sem_file_empty, 1, 1);
    sem_init(&shm->sem_file_full, 1, 0);
    sem_init(&shm->sem_res_empty, 1, 1);
    sem_init(&shm->sem_res_full, 1, 0);
    shm->done = 0;

    // читаем имя файла
    printf("Enter filename: ");
    char file_name[FN_SIZE];
    if (scanf("%s", file_name) != 1) {
        fprintf(stderr, "Error reading filename\n");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == -1) { perror("fork"); exit(EXIT_FAILURE); }

    if (pid == 0) {
        execl("./child.out", "child.out", NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    }

    // Parent: передаём имя файла child
    sem_wait(&shm->sem_file_empty);
    strncpy(shm->filename, file_name, FN_SIZE);
    sem_post(&shm->sem_file_full);

    // Parent: читаем результаты
    while (1) {
        sem_wait(&shm->sem_res_full);
        float res = shm->result;
        int done = shm->done;
        sem_post(&shm->sem_res_empty);

        if (done) break;

        printf("Result of calculations: %f\n", res);
    }

    wait(NULL); // ждём child

    // очистка
    munmap(shm, sizeof(struct shmseg));
    shm_unlink(shm_name);

    return 0;
}