#include "../include/utils.h"

int check_shm_open(const char *name, int flags, mode_t mode) {
    int fd = shm_open(name, flags, mode);
    if (fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    return fd;
}

void check_ftruncate(int fd, off_t length) {
    if (ftruncate(fd, length) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
}

struct shmseg* check_mmap(void *addr, size_t size, int prot, int flags, int fd, off_t offset) {
    struct shmseg *shm = mmap(addr, size, prot, flags, fd, offset);
    if (shm == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    return shm;
}

void check_munmap(void *addr, size_t size) {
    if (munmap(addr, size) == -1) {
        perror("munmap");
        exit(EXIT_FAILURE);
    }
}

void check_shm_unlink(const char *name) {
    if (shm_unlink(name) == -1) {
        perror("shm_unlink");
        exit(EXIT_FAILURE);
    }
}

void check_sem_init(sem_t *sem, int pshared, unsigned int value) {
    if (sem_init(sem, pshared, value) == -1) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }
}

void check_sem_wait(sem_t *sem) {
    if (sem_wait(sem) == -1) {
        perror("sem_wait");
        exit(EXIT_FAILURE);
    }
}

void check_sem_post(sem_t *sem) {
    if (sem_post(sem) == -1) {
        perror("sem_post");
        exit(EXIT_FAILURE);
    }
}

void check_strncpy(char *dest, const char *src, size_t n) {
    if (strncpy(dest, src, n) == NULL) {
        fprintf(stderr, "strncpy failed\n");
        exit(EXIT_FAILURE);
    }
}

pid_t check_fork(void) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    return pid;
}

FILE* check_fopen(const char *filename, const char *mode, struct shmseg *shm) {
    FILE *fp = fopen(filename, mode);
    if (!fp) {
        perror("fopen");
        shm->done = -1;
        check_sem_post(&shm->sem_res_full);
        check_munmap(shm, sizeof(struct shmseg));
        exit(EXIT_FAILURE);
    }
    return fp;
}