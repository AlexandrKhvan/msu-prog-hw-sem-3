#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>

#define POOL_SIZE 4096
#define NULL_OFFSET 0

typedef struct {
    int value;
    size_t prev;
    size_t next;
} node_t;

typedef struct {
    pthread_mutex_t mtx;
    size_t head;
    size_t tail;
    size_t free_head;
    size_t bump_off;
} shm_list_t;

static inline node_t *offset_to_ptr(void *base, size_t offset) {
    return (offset == NULL_OFFSET) ? NULL : (node_t *)((char *)base + offset);
}

static size_t alloc_node(void *base) {
    shm_list_t *list = (shm_list_t *)base;

    if (list->free_head != NULL_OFFSET) {
        size_t reused = list->free_head;
        node_t *n = offset_to_ptr(base, reused);
        list->free_head = n->next;
        return reused;
    }

    if (list->bump_off + sizeof(node_t) > POOL_SIZE) {
        return NULL_OFFSET;
    }
    size_t off = list->bump_off;
    list->bump_off += sizeof(node_t);
    return off;
}

void list_push_back(void *base, int val) {
    shm_list_t *list = (shm_list_t *)base;
    pthread_mutex_lock(&list->mtx);

    size_t new_off = alloc_node(base);
    if (new_off == NULL_OFFSET) {
        pthread_mutex_unlock(&list->mtx);
        return;
    }

    node_t *node = offset_to_ptr(base, new_off);
    node->value = val;
    node->next = NULL_OFFSET;
    node->prev = list->tail;

    if (list->tail != NULL_OFFSET) {
        offset_to_ptr(base, list->tail)->next = new_off;
    } else {
        list->head = new_off;
    }
    list->tail = new_off;

    pthread_mutex_unlock(&list->mtx);
}

void list_remove_front(void *base) {
    shm_list_t *list = (shm_list_t *)base;
    pthread_mutex_lock(&list->mtx);

    if (list->head != NULL_OFFSET) {
        size_t old_head = list->head;
        node_t *node = offset_to_ptr(base, old_head);

        list->head = node->next;
        if (list->head != NULL_OFFSET) {
            offset_to_ptr(base, list->head)->prev = NULL_OFFSET;
        } else {
            list->tail = NULL_OFFSET;
        }

        node->next = list->free_head;
        list->free_head = old_head;
    }

    pthread_mutex_unlock(&list->mtx);
}

int main(void) {
    void *base = mmap(NULL, POOL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }

    shm_list_t *list = (shm_list_t *)base;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&list->mtx, &attr);
    pthread_mutexattr_destroy(&attr);

    list->head = NULL_OFFSET;
    list->tail = NULL_OFFSET;
    list->free_head = NULL_OFFSET;
    list->bump_off = sizeof(shm_list_t);

    if (fork() == 0) {
        list_push_back(base, 100);
        list_push_back(base, 200);
        list_push_back(base, 300);
        list_remove_front(base);
        list_push_back(base, 400);
        return 0;
    }

    wait(NULL);

    printf("Forward traversal:  ");
    for (size_t cur = list->head; cur != NULL_OFFSET; cur = offset_to_ptr(base, cur)->next) {
        printf("%d ", offset_to_ptr(base, cur)->value);
    }
    printf("\nBackward traversal: ");
    for (size_t cur = list->tail; cur != NULL_OFFSET; cur = offset_to_ptr(base, cur)->prev) {
        printf("%d ", offset_to_ptr(base, cur)->value);
    }
    printf("\n");

    pthread_mutex_destroy(&list->mtx);
    munmap(base, POOL_SIZE);
    return 0;
}