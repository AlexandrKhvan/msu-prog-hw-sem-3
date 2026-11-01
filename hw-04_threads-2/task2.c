#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

const char* path = "file.txt";

sem_t sem_w;
sem_t mutex_r;
sem_t mutex_w;
sem_t read_try;

int readers_count = 0;
int writers_count = 0;

void *reader(void *arg) {
    int id = *(int*)arg;
    ssize_t nread;
    char c;
    int count = 0;
    sem_wait(&read_try);
    sem_wait(&mutex_r);
    readers_count++;
    if (readers_count == 1) {
        sem_wait(&sem_w);
    }
    sem_post(&mutex_r);
    sem_post(&read_try);

    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        while ((nread = read(fd, &c, 1)) > 0) {
            ++count;
        }
        close(fd);
    }
    printf("Reader %d read %d bytes\n", id, count);

    sem_wait(&mutex_r);
    readers_count--;
    if (readers_count == 0) {
        sem_post(&sem_w);
    }
    sem_post(&mutex_r);

    return NULL;
}

void *writer(void *arg) {
    int id = *(int*)arg;
    sem_wait(&mutex_w);
    writers_count++;
    if (writers_count == 1) {
        sem_wait(&read_try);
    }
    sem_post(&mutex_w);
    sem_wait(&sem_w);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char msg = 'A' + (id % 26);
        write(fd, &msg, 1);
        close(fd);
    }
    printf("Writer %d wrote a byte\n", id);
    sem_post(&sem_w);
    sem_wait(&mutex_w);
    writers_count--;
    if (writers_count == 0) {
        sem_post(&read_try);
    }
    sem_post(&mutex_w);

    return NULL;
}

int main(void) {
    sem_init(&sem_w, 0, 1);
    sem_init(&mutex_r, 0, 1);
    sem_init(&mutex_w, 0, 1);
    sem_init(&read_try, 0, 1);

    pthread_t r[5], w[2];
    int r_ids[5], w_ids[2];

    for (int i = 0; i < 5; ++i) {
        r_ids[i] = i + 1;
        pthread_create(&r[i], NULL, reader, &r_ids[i]);
    }
    for (int i = 0; i < 2; ++i) {
        w_ids[i] = i + 1;
        pthread_create(&w[i], NULL, writer, &w_ids[i]);
    }

    for (int i = 0; i < 5; ++i) pthread_join(r[i], NULL);
    for (int i = 0; i < 2; ++i) pthread_join(w[i], NULL);

    sem_destroy(&sem_w);
    sem_destroy(&mutex_r);
    sem_destroy(&mutex_w);
    sem_destroy(&read_try);

    return 0;
}
