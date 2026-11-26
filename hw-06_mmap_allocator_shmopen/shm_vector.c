#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>

#define SHM_NAME "/shm_vec_demo"
#define INITIAL_CAP 4

typedef struct {
    pthread_mutex_t mtx;
    size_t size;
    size_t capacity;
    int data[];
} shm_vector_t;

shm_vector_t *vec_create(int *shm_fd) {
    shm_unlink(SHM_NAME);
    *shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0644);
    if (*shm_fd == -1) { perror("shm_open"); exit(1); }

    size_t bytes = sizeof(shm_vector_t) + INITIAL_CAP * sizeof(int);
    if (ftruncate(*shm_fd, bytes) == -1) { perror("ftruncate"); exit(1); }

    shm_vector_t *v = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, *shm_fd, 0);
    if (v == MAP_FAILED) { perror("mmap"); exit(1); }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&v->mtx, &attr);
    pthread_mutexattr_destroy(&attr);

    v->size = 0;
    v->capacity = INITIAL_CAP;
    return v;
}

shm_vector_t *vec_sync_mapping(shm_vector_t *v, size_t *mapped_bytes) {
    size_t actual_bytes = sizeof(shm_vector_t) + v->capacity * sizeof(int);
    if (actual_bytes > *mapped_bytes) {
        v = mremap(v, *mapped_bytes, actual_bytes, MREMAP_MAYMOVE);
        if (v == MAP_FAILED) { perror("mremap"); exit(1); }
        *mapped_bytes = actual_bytes;
    }
    return v;
}

shm_vector_t *vec_push_back(shm_vector_t *v, int shm_fd, size_t *mapped_bytes, int val) {
    pthread_mutex_lock(&v->mtx);
    v = vec_sync_mapping(v, mapped_bytes);

    if (v->size == v->capacity) {
        size_t new_cap = v->capacity * 2;
        size_t new_bytes = sizeof(shm_vector_t) + new_cap * sizeof(int);

        if (ftruncate(shm_fd, new_bytes) == -1) { perror("ftruncate grow"); exit(1); }

        shm_vector_t *new_v = mremap(v, *mapped_bytes, new_bytes, MREMAP_MAYMOVE);
        if (new_v == MAP_FAILED) { perror("mremap grow"); exit(1); }

        v = new_v;
        *mapped_bytes = new_bytes;
        v->capacity = new_cap;
    }

    v->data[v->size++] = val;
    pthread_mutex_unlock(&v->mtx);
    return v;
}

int main(void) {
    int shm_fd;
    shm_vector_t *v = vec_create(&shm_fd);

    pid_t pid = fork();
    if (pid == 0) {
        size_t local_mapped = sizeof(shm_vector_t) + INITIAL_CAP * sizeof(int);
        shm_vector_t *child_v = mmap(NULL, local_mapped, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

        for (int i = 1; i <= 10; ++i) {
            child_v = vec_push_back(child_v, shm_fd, &local_mapped, i * 10);
        }
        munmap(child_v, local_mapped);
        close(shm_fd);
        return 0;
    }

    wait(NULL);

    size_t parent_mapped = sizeof(shm_vector_t) + INITIAL_CAP * sizeof(int);
    v = vec_sync_mapping(v, &parent_mapped);

    printf("Vector size: %zu, capacity: %zu\nElements: ", v->size, v->capacity);
    for (size_t i = 0; i < v->size; ++i) {
        printf("%d ", v->data[i]);
    }
    printf("\n");

    pthread_mutex_destroy(&v->mtx);
    munmap(v, parent_mapped);
    close(shm_fd);
    shm_unlink(SHM_NAME);
    return 0;
}