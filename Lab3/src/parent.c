#include "../include/utils.h"

#define FN_SIZE 256

int main() {
    const char *shm_name = "/shm_float";

    int fd = check_shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    check_ftruncate(fd, sizeof(struct shmseg));

    struct shmseg *shm = check_mmap(NULL, sizeof(struct shmseg), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    check_sem_init(&shm->sem_file_empty, 1, 1);
    check_sem_init(&shm->sem_file_full, 1, 0);
    check_sem_init(&shm->sem_res_empty, 1, 1);
    check_sem_init(&shm->sem_res_full, 1, 0);
    shm->done = 0;

    printf("Enter filename: ");
    char file_name[FN_SIZE];
    if (scanf("%s", file_name) != 1) {
        fprintf(stderr, "Error reading filename\n");
        exit(EXIT_FAILURE);
    }

    pid_t pid = check_fork();

    if (pid == 0) {
        execl("./child.out", "child.out", NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    }

    check_sem_wait(&shm->sem_file_empty);
    check_strncpy(shm->filename, file_name, FN_SIZE);
    check_sem_post(&shm->sem_file_full);

    while (1) {
        check_sem_wait(&shm->sem_res_full);

        if (shm->done == -1) {
            fprintf(stderr, "Child failed to open file.\n");
            check_sem_post(&shm->sem_res_empty);
            break;
        }
        
        float res = shm->result;
        int done = shm->done;
        check_sem_post(&shm->sem_res_empty);

        if (done) break;

        printf("Result of calculations: %f\n", res);
    }

    wait(NULL);

    check_munmap(shm, sizeof(struct shmseg));
    check_shm_unlink(shm_name);

    return 0;
}