#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    char filename[128];
    int line_num;
} MatchRecord;

typedef struct {
    atomic_int total_found;
    int target_N;
    int num_workers;
    pid_t worker_pids[32];
} SharedState;

static volatile sig_atomic_t g_stop_search = 0;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void sigusr1_stop_handler(int sig) {
    (void)sig;
    g_stop_search = 1;
}

void worker_process(int worker_idx, int num_files, char files[][64], const char *query,
                    int write_pipe_fd, SharedState *state) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_stop_handler;
    sigaction(SIGUSR1, &sa, NULL);

    for (int i = 0; i < num_files; i++) {
        if (g_stop_search) break;

        FILE *f = fopen(files[i], "r");
        if (!f) continue;

        char *line = NULL;
        size_t len = 0;
        int line_num = 0;

        while (getline(&line, &len, f) != -1) {
            if (g_stop_search) break;
            line_num++;

            if (strstr(line, query) != NULL) {
                int prev = atomic_fetch_add(&state->total_found, 1);
                if (prev < state->target_N) {
                    MatchRecord rec;
                    strncpy(rec.filename, files[i], sizeof(rec.filename) - 1);
                    rec.line_num = line_num;
                    if (write(write_pipe_fd, &rec, sizeof(rec)) < 0) { perror("write to pipe"); }

                    if (prev + 1 >= state->target_N) {
                        for (int w = 0; w < state->num_workers; w++) {
                            if (w != worker_idx && state->worker_pids[w] > 0) {
                                kill(state->worker_pids[w], SIGUSR1);
                            }
                        }
                        g_stop_search = 1;
                        break;
                    }
                } else {
                    g_stop_search = 1;
                    break;
                }
            }
        }
        free(line);
        fclose(f);
    }

    close(write_pipe_fd);
    exit(0);
}

double run_search_experiment(int P, int N, const char *query, int total_files, char files[][64], int *out_found) {
    SharedState *shared = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    atomic_init(&shared->total_found, 0);
    shared->target_N = N;
    shared->num_workers = P;
    memset(shared->worker_pids, 0, sizeof(shared->worker_pids));

    int pipes[32][2];
    char worker_files[32][128][64];
    int worker_file_counts[32] = {0};

    for (int i = 0; i < total_files; i++) {
        int w = i % P;
        strcpy(worker_files[w][worker_file_counts[w]++], files[i]);
    }

    double t0 = get_time_sec();

    for (int i = 0; i < P; i++) {
        if (pipe(pipes[i]) == -1) { perror("pipe"); exit(1); }
        pid_t pid = fork();
        if (pid == 0) {
            close(pipes[i][0]);
            while (shared->worker_pids[P - 1] == 0) {
                usleep(500);
            }
            worker_process(i, worker_file_counts[i], worker_files[i], query, pipes[i][1], shared);
        } else {
            shared->worker_pids[i] = pid;
            close(pipes[i][1]);
        }
    }

    int collected = 0;
    for (int i = 0; i < P; i++) {
        MatchRecord rec;
        while (read(pipes[i][0], &rec, sizeof(rec)) == sizeof(rec)) {
            collected++;
        }
        close(pipes[i][0]);
    }

    for (int i = 0; i < P; i++) {
        waitpid(shared->worker_pids[i], NULL, 0);
    }

    double elapsed = get_time_sec() - t0;
    *out_found = collected;

    munmap(shared, sizeof(SharedState));
    return elapsed;
}

void create_dummy_dataset(int num_files, int lines_per_file, char files[][64]) {
    for (int i = 0; i < num_files; i++) {
        snprintf(files[i], 64, "search_test_%d.txt", i);
        FILE *f = fopen(files[i], "w");
        for (int l = 1; l <= lines_per_file; l++) {
            if (l % 150 == 0) {
                fprintf(f, "Line %d: Target query TARGET_WORD here!\n", l);
            } else {
                fprintf(f, "Line %d: regular logs and data strings\n", l);
            }
        }
        fclose(f);
    }
}

int main(void) {
    int total_files = 32;
    char files[32][64];
    create_dummy_dataset(total_files, 5000, files);

    FILE *fout = fopen("report_task3.md", "w");
    if (!fout) { perror("fopen report_task3.md"); return 1; }

    fprintf(fout, "| Required ($N$) | Processes ($P$) | Found | Search Time (s) | Speedup $S$ | Coordination Mechanism |\n");
    fprintf(fout, "| :--- | :--- | :--- | :--- | :--- | :--- |\n");

    const int target_limits[] = {10, 50, 200};
    const int proc_counts[] = {1, 2, 4, 8};

    for (size_t n_idx = 0; n_idx < sizeof(target_limits)/sizeof(int); n_idx++) {
        int N = target_limits[n_idx];
        double t_base = 0.0;

        for (size_t p_idx = 0; p_idx < sizeof(proc_counts)/sizeof(int); p_idx++) {
            int P = proc_counts[p_idx];
            int found = 0;

            double elapsed = run_search_experiment(P, N, "TARGET_WORD", total_files, files, &found);
            if (P == 1) t_base = elapsed;

            printf("[N=%3d | P=%d] Found: %d | Time: %.4fs | Speedup: %.2fx\n",
                   N, P, found, elapsed, t_base / elapsed);

            fprintf(fout, "| %d | %d | %d | %.4f | %.2fx | Shared Mem + SIGUSR1 |\n",
                    N, P, found, elapsed, t_base / elapsed);
            fflush(fout);
        }
    }

    for (int i = 0; i < total_files; i++) {
        unlink(files[i]);
    }

    fclose(fout);
    printf("Successfully saved results in 'report_task3.md'\n\n");
    return 0;
}
