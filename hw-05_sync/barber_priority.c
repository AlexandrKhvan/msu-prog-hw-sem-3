#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NUM_BARBERS 2
#define QUEUE_LIMIT 5
#define NUM_CLIENTS 12

typedef struct {
    int id;
    int priority; // 0 - Regular, 1 - VIP, 2 - SuperVIP
    int served;
    int barber_id;
    pthread_cond_t cond;
} client_t;

pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t wakeup = PTHREAD_COND_INITIALIZER;

client_t *queue[QUEUE_LIMIT];
int count = 0;
int clients_left = NUM_CLIENTS;

void enqueue_priority(client_t *c) {
    int pos = count;
    while (pos > 0 && queue[pos - 1]->priority < c->priority) {
        queue[pos] = queue[pos - 1];
        --pos;
    }
    queue[pos] = c;
    ++count;
}

client_t *dequeue_priority(void) {
    client_t *top = queue[0];
    for (int i = 0; i < count - 1; ++i) {
        queue[i] = queue[i + 1];
    }
    --count;
    return top;
}

void *barber(void *arg) {
    int barber_id = *(int *)arg;

    while (1) {
        pthread_mutex_lock(&mtx);

        while (count == 0 && clients_left > 0) {
            printf("[BARBER %d] Sleeping...\n", barber_id);
            pthread_cond_wait(&wakeup, &mtx);
        }

        if (count == 0 && clients_left == 0) {
            pthread_mutex_unlock(&mtx);
            break;
        }

        client_t *c = dequeue_priority();
        c->served = 1;
        c->barber_id = barber_id;
        pthread_cond_signal(&c->cond);

        pthread_mutex_unlock(&mtx);

        int duration = rand() % 3 + 2;
        printf("[BARBER %d] Cutting Client #%d [Priority %d] (%d sec)\n",
               barber_id, c->id, c->priority, duration);
        sleep(duration);
        printf("[BARBER %d] Done with Client #%d [Priority %d]\n",
               barber_id, c->id, c->priority);

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
    int prio = rand() % 3;

    client_t *me = malloc(sizeof(client_t));
    if (!me) {
        perror("malloc");
        pthread_exit(NULL);
    }
    me->id = id;
    me->priority = prio;
    me->served = 0;
    me->barber_id = -1;
    pthread_cond_init(&me->cond, NULL);

    pthread_mutex_lock(&mtx);
    printf("[CLIENT #%d | Prio %d] Arrived\n", id, prio);

    if (count < QUEUE_LIMIT) {
        enqueue_priority(me);
        printf("[CLIENT #%d | Prio %d] Waiting in queue (size: %d/%d)\n", id, prio, count, QUEUE_LIMIT);

        pthread_cond_signal(&wakeup);

        while (!me->served) {
            pthread_cond_wait(&me->cond, &mtx);
        }
        int assigned_barber = me->barber_id;
        pthread_mutex_unlock(&mtx);

        printf("  -> [CLIENT #%d | Prio %d] Served by BARBER %d!\n", id, prio, assigned_barber);
    } else {
        --clients_left;
        if (clients_left == 0) {
            pthread_cond_broadcast(&wakeup);
        }
        pthread_mutex_unlock(&mtx);

        printf("  XX [CLIENT #%d | Prio %d] Queue full, LEAVING!\n", id, prio);
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
        usleep(700000);
    }

    for (int i = 0; i < NUM_CLIENTS; ++i) {
        pthread_join(client_tids[i], NULL);
    }
    for (int i = 0; i < NUM_BARBERS; ++i) {
        pthread_join(barber_tids[i], NULL);
    }

    return 0;
}