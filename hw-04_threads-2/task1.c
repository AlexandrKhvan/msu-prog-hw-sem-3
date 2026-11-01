#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_SIZE 16
#define N 10

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_full = PTHREAD_COND_INITIALIZER;

typedef struct {
    int data[MAX_SIZE];
    int head;
    int tail;
    int n;
} queue_t;

int empty(queue_t *q) {
    return q->n == 0;
}

int full(queue_t *q) {
    return q->n == MAX_SIZE;
}

void push(queue_t *q, int value) {
    pthread_mutex_lock(&mutex);

    while (full(q)) {
        pthread_cond_wait(&queue_full, &mutex);
    }

    q->data[q->tail] = value;
    q->tail = (q->tail + 1) % MAX_SIZE;
    ++q->n;
    pthread_cond_signal(&queue_empty);
    pthread_mutex_unlock(&mutex);
}

int pop(queue_t *q) {
    pthread_mutex_lock(&mutex);

    while (empty(q)) {
        pthread_cond_wait(&queue_empty, &mutex);
    }
    
    int value = q->data[q->head];
    q->head = (q->head + 1) % MAX_SIZE;
    --q->n;
    pthread_cond_signal(&queue_full);
    pthread_mutex_unlock(&mutex);
    return value;
}

void *producer(void *arg) {
    queue_t *q = (queue_t*) arg;
    int i = 0;
    while (i < N) {
        int x = (i + 1) * 10;
        push(q, x);
        printf("producer: %d\n", x);
        ++i;
        usleep(50000);
    }
    return NULL;
}

void *consumer(void *arg) {
    queue_t *q = (queue_t*) arg;
    int i = 0;
    while (i < N) {
        int x = pop(q);
        printf("  consumer: %d\n", x);
        ++i;
        usleep(100000);
    }
    return NULL;
}

int main(void) {
    queue_t q = {.head = 0, .tail = 0, .n = 0};
    pthread_t tid_p, tid_c;
    pthread_create(&tid_p, NULL, producer, &q);
    pthread_create(&tid_c, NULL, consumer, &q);
    pthread_join(tid_p, NULL);
    pthread_join(tid_c, NULL);

    return 0;
}
