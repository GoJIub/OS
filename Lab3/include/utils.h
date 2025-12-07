#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <semaphore.h>

struct shmseg {
    char filename[256];
    float result;
    int done;
    sem_t sem_file_empty;
    sem_t sem_file_full;
    sem_t sem_res_empty;
    sem_t sem_res_full;
};

int check_shm_open(const char *name, int flags, mode_t mode);
void check_ftruncate(int fd, off_t length);
struct shmseg* check_mmap(void *addr, size_t size, int prot, int flags, int fd, off_t offset);
void check_munmap(void *addr, size_t size);
void check_shm_unlink(const char *name);

void check_sem_init(sem_t *sem, int pshared, unsigned int value);
void check_sem_wait(sem_t *sem);
void check_sem_post(sem_t *sem);

void check_strncpy(char *dest, const char *src, size_t n);

pid_t check_fork(void);
FILE* check_fopen(const char *filename, const char *mode, struct shmseg *shm);