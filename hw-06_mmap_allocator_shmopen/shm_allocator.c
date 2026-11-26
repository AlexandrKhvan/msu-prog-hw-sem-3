#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>

#define ALIGN16(x) (((x) + 15) & ~(size_t)15)

typedef struct {
    size_t size;
    size_t next_free;
    bool is_free;
} block_header_t;

typedef struct {
    pthread_mutex_t mtx;
    size_t total_size;
    size_t free_list_head;
    char arena[];
} mem_t;

#define INVALID_OFFSET ((size_t)-1)

static inline block_header_t *get_block(mem_t *mem, size_t offset) {
    return (offset == INVALID_OFFSET) ? NULL : (block_header_t *)(mem->arena + offset);
}

void shm_alloc_init(mem_t *mem, size_t total_arena_size) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&mem->mtx, &attr);
    pthread_mutexattr_destroy(&attr);

    mem->total_size = total_arena_size;
    mem->free_list_head = 0;

    block_header_t *first = get_block(mem, 0);
    first->size = total_arena_size - sizeof(block_header_t);
    first->next_free = INVALID_OFFSET;
    first->is_free = true;
}

void *shm_malloc(mem_t *mem, size_t size) {
    if (size == 0) return NULL;
    size = ALIGN16(size);

    pthread_mutex_lock(&mem->mtx);

    size_t prev_off = INVALID_OFFSET;
    size_t curr_off = mem->free_list_head;

    while (curr_off != INVALID_OFFSET) {
        block_header_t *curr = get_block(mem, curr_off);

        if (curr->is_free && curr->size >= size) {
            size_t min_split = sizeof(block_header_t) + 16;

            if (curr->size >= size + min_split) {
                size_t next_off = curr_off + sizeof(block_header_t) + size;
                block_header_t *rem = get_block(mem, next_off);

                rem->size = curr->size - size - sizeof(block_header_t);
                rem->is_free = true;
                rem->next_free = curr->next_free;

                curr->size = size;

                if (prev_off == INVALID_OFFSET) {
                    mem->free_list_head = next_off;
                } else {
                    get_block(mem, prev_off)->next_free = next_off;
                }
            } else {
                if (prev_off == INVALID_OFFSET) {
                    mem->free_list_head = curr->next_free;
                } else {
                    get_block(mem, prev_off)->next_free = curr->next_free;
                }
            }

            curr->is_free = false;
            curr->next_free = INVALID_OFFSET;
            pthread_mutex_unlock(&mem->mtx);
            return (char *)curr + sizeof(block_header_t);
        }

        prev_off = curr_off;
        curr_off = curr->next_free;
    }

    pthread_mutex_unlock(&mem->mtx);
    return NULL;
}

void shm_free(mem_t *mem, void *ptr) {
    if (!ptr) return;

    pthread_mutex_lock(&mem->mtx);

    block_header_t *blk = (block_header_t *)((char *)ptr - sizeof(block_header_t));
    size_t blk_off = (char *)blk - mem->arena;
    blk->is_free = true;

    size_t prev_off = INVALID_OFFSET;
    size_t curr_off = mem->free_list_head;

    while (curr_off != INVALID_OFFSET && curr_off < blk_off) {
        prev_off = curr_off;
        curr_off = get_block(mem, curr_off)->next_free;
    }

    blk->next_free = curr_off;
    if (prev_off == INVALID_OFFSET) {
        mem->free_list_head = blk_off;
    } else {
        get_block(mem, prev_off)->next_free = blk_off;
    }

    if (curr_off != INVALID_OFFSET && blk_off + sizeof(block_header_t) + blk->size == curr_off) {
        block_header_t *right = get_block(mem, curr_off);
        blk->size += sizeof(block_header_t) + right->size;
        blk->next_free = right->next_free;
    }

    if (prev_off != INVALID_OFFSET) {
        block_header_t *left = get_block(mem, prev_off);
        if (prev_off + sizeof(block_header_t) + left->size == blk_off) {
            left->size += sizeof(block_header_t) + blk->size;
            left->next_free = blk->next_free;
        }
    }

    pthread_mutex_unlock(&mem->mtx);
}

int main(void) {
    size_t arena_cap = 4096;
    size_t total_mmap = sizeof(mem_t) + arena_cap;

    mem_t *mem = mmap(NULL, total_mmap, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) { perror("mmap"); return 1; }

    shm_alloc_init(mem, arena_cap);

    char *str = shm_malloc(mem, 64);
    strcpy(str, "Allocated in parent!");
    size_t str_offset = (char *)str - mem->arena;

    if (fork() == 0) {
        char *child_str = mem->arena + str_offset;
        printf("[Child] Read from shared alloc: \"%s\"\n", child_str);

        shm_free(mem, child_str);

        int *arr = shm_malloc(mem, 10 * sizeof(int));
        for (int i = 0; i < 10; ++i) arr[i] = i * i;
        printf("[Child] Allocated new array, arr[5] = %d\n", arr[5]);
        shm_free(mem, arr);
        return 0;
    }

    wait(NULL);

    block_header_t *root = get_block(mem, mem->free_list_head);
    printf("[Parent] Free block after coalescing: size = %zu (expected %zu)\n",
           root->size, arena_cap - sizeof(block_header_t));

    pthread_mutex_destroy(&mem->mtx);
    munmap(mem, total_mmap);
    return 0;
}