#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>

#define FN_SIZE 256

struct shmseg {
    char filename[FN_SIZE];
    float result;
    int done;
    sem_t sem_file_empty;
    sem_t sem_file_full;
    sem_t sem_res_empty;
    sem_t sem_res_full;
};

int main() {
    const char *shm_name = "/shm_float";

    int fd = shm_open(shm_name, O_RDWR, 0666);
    if (fd == -1) { perror("shm_open"); exit(EXIT_FAILURE); }

    struct shmseg *shm = mmap(NULL, sizeof(struct shmseg),
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); exit(EXIT_FAILURE); }

    // Child: ждём имя файла
    sem_wait(&shm->sem_file_full);
    char filename[FN_SIZE];
    strncpy(filename, shm->filename, FN_SIZE);
    sem_post(&shm->sem_file_empty);

    FILE *fp = fopen(filename, "r");
    if (!fp) { perror("fopen"); exit(EXIT_FAILURE); }

    char *line = NULL;
    size_t len = 0;

    while (getline(&line, &len, fp) != -1) {
        char *ptr = line;
        float x, sum = 0;
        while (sscanf(ptr, "%f", &x) == 1) {
            sum += x;
            while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n') ptr++;
            while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
        }

        sem_wait(&shm->sem_res_empty);
        shm->result = sum;
        shm->done = 0;
        sem_post(&shm->sem_res_full);
    }

    free(line);
    fclose(fp);

    // сигнал конца передачи
    sem_wait(&shm->sem_res_empty);
    shm->done = 1;
    sem_post(&shm->sem_res_full);

    munmap(shm, sizeof(struct shmseg));

    return 0;
}