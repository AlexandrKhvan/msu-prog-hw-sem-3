#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define ROUNDS 6

const char *item_names[3] = {"TOBACCO", "PAPER", "MATCHES"};

pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t agent_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t smoker_cond[3] = {
    PTHREAD_COND_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    PTHREAD_COND_INITIALIZER
};

int on_table[3] = {0, 0, 0};
int table_busy = 0;
int simulation_done = 0;

void *agent(void *arg) {
    (void)arg;

    for (int r = 1; r <= ROUNDS; ++r) {
        pthread_mutex_lock(&mtx);

        while (table_busy) {
            pthread_cond_wait(&agent_cond, &mtx);
        }

        // Выбираем, какого ингредиента НЕ будет на столе (он есть у целевого курильщика)
        int missing = rand() % 3;
        int item1 = (missing + 1) % 3;
        int item2 = (missing + 2) % 3;

        on_table[item1] = 1;
        on_table[item2] = 1;
        table_busy = 1;

        printf("\n[AGENT | Round %d] Put %s and %s on the table\n",
               r, item_names[item1], item_names[item2]);

        // Будим ровно того курильщика, у которого есть missing
        pthread_cond_signal(&smoker_cond[missing]);

        pthread_mutex_unlock(&mtx);
        sleep(1);
    }

    pthread_mutex_lock(&mtx);
    while (table_busy) {
        pthread_cond_wait(&agent_cond, &mtx);
    }
    simulation_done = 1;
    for (int i = 0; i < 3; ++i) {
        pthread_cond_signal(&smoker_cond[i]);
    }
    pthread_mutex_unlock(&mtx);

    printf("\n[AGENT] Out of supplies. Simulation finished.\n");
    pthread_exit(NULL);
}

void *smoker(void *arg) {
    int id = *(int *)arg;
    int need1 = (id + 1) % 3;
    int need2 = (id + 2) % 3;

    while (1) {
        pthread_mutex_lock(&mtx);

        while (!(on_table[need1] && on_table[need2]) && !simulation_done) {
            pthread_cond_wait(&smoker_cond[id], &mtx);
        }

        if (simulation_done && !(on_table[need1] && on_table[need2])) {
            pthread_mutex_unlock(&mtx);
            break;
        }

        // Забираем ингредиенты со стола
        on_table[need1] = 0;
        on_table[need2] = 0;
        table_busy = 0;

        printf("  -> [SMOKER with %s] Took %s and %s, rolling cigarette...\n",
               item_names[id], item_names[need1], item_names[need2]);

        // Разрешаем агенту выложить следующую пару ингредиентов
        pthread_cond_signal(&agent_cond);
        pthread_mutex_unlock(&mtx);

        // Процесс курения происходит вне критической секции
        sleep(rand() % 2 + 1);
        printf("  <- [SMOKER with %s] Finished smoking.\n", item_names[id]);
    }

    pthread_exit(NULL);
}

int main(void) {
    srand(time(NULL));

    pthread_t agent_tid;
    pthread_t smoker_tids[3];
    int smoker_ids[3] = {0, 1, 2};

    for (int i = 0; i < 3; ++i) {
        pthread_create(&smoker_tids[i], NULL, smoker, &smoker_ids[i]);
    }
    pthread_create(&agent_tid, NULL, agent, NULL);

    pthread_join(agent_tid, NULL);
    for (int i = 0; i < 3; ++i) {
        pthread_join(smoker_tids[i], NULL);
    }

    return 0;
}