#include "../include/utils.h"

#define FN_SIZE 256

int main() {
    const char *shm_name = "/shm_float";

    int fd = check_shm_open(shm_name, O_RDWR, 0666);

    struct shmseg *shm = check_mmap(NULL, sizeof(struct shmseg), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    check_sem_wait(&shm->sem_file_full);
    char filename[FN_SIZE];
    check_strncpy(filename, shm->filename, FN_SIZE);
    check_sem_post(&shm->sem_file_empty);

    FILE *fp = check_fopen(filename, "r", shm);

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

        check_sem_wait(&shm->sem_res_empty);
        shm->result = sum;
        check_sem_post(&shm->sem_res_full);
    }

    free(line);
    fclose(fp);

    check_sem_wait(&shm->sem_res_empty);
    shm->done = 1;
    check_sem_post(&shm->sem_res_full);

    check_munmap(shm, sizeof(struct shmseg));

    return 0;
}