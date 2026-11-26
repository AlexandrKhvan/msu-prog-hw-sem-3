#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NUM_BARBERS 2
#define QUEUE_LIMIT 4
#define NUM_CLIENTS 12

typedef struct {
    int id;
    int served;
    int barber_id;
    pthread_cond_t cond;
} client_t;

pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t wakeup = PTHREAD_COND_INITIALIZER;

client_t *queue[QUEUE_LIMIT];
int head = 0, tail = 0, count = 0;
int clients_left = NUM_CLIENTS;

void *barber(void *arg) {
    int barber_id = *(int *)arg;

    while (1) {
        pthread_mutex_lock(&mtx);

        while (count == 0 && clients_left > 0) {
            printf("[BARBER %d] No clients, sleeping...\n", barber_id);
            pthread_cond_wait(&wakeup, &mtx);
        }

        if (count == 0 && clients_left == 0) {
            pthread_mutex_unlock(&mtx);
            break;
        }

        client_t *c = queue[head];
        head = (head + 1) % QUEUE_LIMIT;
        --count;

        c->served = 1;
        c->barber_id = barber_id;
        pthread_cond_signal(&c->cond);

        pthread_mutex_unlock(&mtx);

        int duration = rand() % 3 + 2;
        printf("[BARBER %d] Started cutting Client #%d (time: %d sec)\n", barber_id, c->id, duration);
        sleep(duration);
        printf("[BARBER %d] Finished Client #%d!\n", barber_id, c->id);

        pthread_cond_destroy(&c->cond);
        free(c);

        pthread_mutex_lock(&mtx);
        --clients_left;
        if (clients_left == 0) {
            pthread_cond_broadcast(&wakeup);
        }
        pthread_mutex_unlock(&mtx);
    }

    printf("[BARBER %d] Shift ended, going home.\n", barber_id);
    pthread_exit(NULL);
}

void *customer(void *arg) {
    int id = *(int *)arg;

    client_t *me = malloc(sizeof(client_t));
    if (!me) {
        perror("malloc");
        pthread_exit(NULL);
    }
    me->id = id;
    me->served = 0;
    me->barber_id = -1;
    pthread_cond_init(&me->cond, NULL);

    pthread_mutex_lock(&mtx);
    printf("[CLIENT #%d] Entered the barbershop\n", id);

    if (count < QUEUE_LIMIT) {
        queue[tail] = me;
        tail = (tail + 1) % QUEUE_LIMIT;
        ++count;
        printf("[CLIENT #%d] Took a seat in queue (%d/%d)\n", id, count, QUEUE_LIMIT);

        pthread_cond_signal(&wakeup);

        while (!me->served) {
            pthread_cond_wait(&me->cond, &mtx);
        }
        int assigned_barber = me->barber_id;
        pthread_mutex_unlock(&mtx);

        printf("  -> [CLIENT #%d] Getting haircut from BARBER %d!\n", id, assigned_barber);
    } else {
        --clients_left;
        if (clients_left == 0) {
            pthread_cond_broadcast(&wakeup);
        }
        pthread_mutex_unlock(&mtx);

        printf("  XX [CLIENT #%d] No free chairs, LEAVING!\n", id);
        pthread_cond_destroy(&me->cond);
        free(me);
    }

    pthread_exit(NULL);
}

int main(void) {
    srand(time(NULL));

    pthread_t barber_tids[NUM_BARBERS];
    pthread_t client_tids[NUM_CLIENTS];
    int barber_ids[NUM_BARBERS];
    int client_ids[NUM_CLIENTS];

    for (int i = 0; i < NUM_BARBERS; ++i) {
        barber_ids[i] = i + 1;
        pthread_create(&barber_tids[i], NULL, barber, &barber_ids[i]);
    }

    for (int i = 0; i < NUM_CLIENTS; ++i) {
        client_ids[i] = i + 1;
        pthread_create(&client_tids[i], NULL, customer, &client_ids[i]);
        sleep(1);
    }

    for (int i = 0; i < NUM_CLIENTS; ++i) {
        pthread_join(client_tids[i], NULL);
    }
    for (int i = 0; i < NUM_BARBERS; ++i) {
        pthread_join(barber_tids[i], NULL);
    }

    return 0;
}