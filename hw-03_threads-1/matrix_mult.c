#define GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

typedef struct {
    int thread_id;
    int num_threads;
    int M, K, N;
    const double *A;
    const double *B;
    double *C; 
} ThreadArgs;


static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void* matmul_standard_worker(void *arg) {
    ThreadArgs *t = (ThreadArgs *)arg;
    int from = (t->M * t->thread_id) / t->num_threads;
    int to = (t->M * (t->thread_id + 1)) / t->num_threads;

    for (int i = from; i < to; i++) {
        for (int j = 0; j < t->N; j++) {
            double sum = 0.0;
            for (int k = 0; k < t->K; k++) {
                sum += t->A[i * t->K + k] * t->B[k * t->N + j];
            }
            t->C[i * t->N + j] = sum;
        }
    }

    return NULL;
}

void transpose_matrix(const double *B, double *B_t, int K, int N) {
    for (int i = 0; i < K; i++) {
        for (int j = 0; j < N; j++) {
            B_t[j * K + i] = B[i * N + j];
        }
    }
}

void* matmul_transposed_worker(void *arg) {
    ThreadArgs *t = (ThreadArgs *)arg;
    int from = (t->M * t->thread_id) / t->num_threads;
    int to = (t->M * (t->thread_id + 1)) / t->num_threads;

    for (int i = from; i < to; i++) {
        for (int j = 0; j < t->N; j++) {
            double sum = 0.0;
            for (int k = 0; k < t->K; k++) {
                sum += t->A[i * t->K + k] * t->B[j * t->K + k]; // Accessing transposed B
            }
            t->C[i * t->N + j] = sum;
        }
    }

    return NULL;
}

double run_benchmark(void* (worker)(void*), int threads, int M, int K, int N, const double *A, const double *B_or_BT, double *C) {
    pthread_t *pt = malloc(threads*sizeof(pthread_t));
    ThreadArgs *args = malloc(threads*sizeof(ThreadArgs));

    double t0 = get_time_sec();
    for (int i = 0; i < threads; i++) {
        args[i] = (ThreadArgs){.thread_id = i, .num_threads = threads, .M = M, .K = K, .N = N, .A = A, .B = B_or_BT, .C = C};
        pthread_create(&pt[i], NULL, worker, &args[i]);
    }

    for (int i = 0; i < threads; i++) {
        pthread_join(pt[i], NULL);
    }
    double elapsed = get_time_sec() - t0;
    free(pt);
    free(args);
    return elapsed;
}

bool verify_matrices(const double *C1, const double *C2, size_t size, double eps) {
    for (size_t i = 0; i < size; i++) {
        if (fabs(C1[i] - C2[i]) > eps) {
            return false;
        }
    }
    return true;
}

int main(void) {
    const int sizes[] = {500, 1000, 1500};
    const int thread_counts[] = {1, 2, 4, 8};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    int num_threads = sizeof(thread_counts) / sizeof(thread_counts[0]);
    FILE *fout = fopen("report_task1.md", "w");
    if (!fout) {
        perror("Failed to open report_task1.md");
        return 1;
    }

    fprintf(fout, "| Size | Threads | Standard Time (s) | Transposed Time (s) | Verification |\n");
    fprintf(fout, "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |\n");

    for (int s = 0; s < num_sizes; s++) {
        int N = sizes[s];
        size_t sz = (size_t)N * N;
        double *A = malloc(sz * sizeof(double));
        double *B = malloc(sz * sizeof(double));
        double *B_T = malloc(sz * sizeof(double));
        double *C_ref = malloc(sz * sizeof(double));
        double *C_test = malloc(sz * sizeof(double));

        for (size_t i = 0; i < sz; i++) {
            A[i] = (double)(rand() % 100) / 10.0;
            B[i] = (double)(rand() % 100) / 10.0;
        }

        transpose_matrix(B, B_T, N, N);

        double t_base_std = 0.0;
        double t_base_tr = 0.0;

        for (int t = 0; t < num_threads; t++) {
            int P = thread_counts[t];

            double t_std = run_benchmark(matmul_standard_worker, P, N, N, N, A, B, C_test);
            if (P == 1) {
                t_base_std = t_std;
                for (size_t i = 0; i < sz; i++) C_ref[i] = C_test[i];
            }

            double t_tr = run_benchmark(matmul_transposed_worker, P, N, N, N, A, B_T, C_test);
            if (P == 1) {
                t_base_tr = t_tr;
            }

            bool ok = verify_matrices(C_ref, C_test, sz, 1e-6);

            printf("[N=%4d | P=%d] Std: %.4fs (%.2fx) | Transposed: %.4fs (%.2fx) | %s\n",
                   N, P, t_std, t_base_std / t_std, t_tr, t_base_tr / t_tr, ok ? "OK" : "ERR");

            fprintf(fout, "| %d | %d | %.4f | %.2fx | %.4f | %.2fx | %s |\n",
                    N, P, t_std, t_base_std / t_std, t_tr, t_base_tr / t_tr, ok ? "OK" : "FAIL");
            fflush(fout);
        }

        free(A); free(B); free(B_T); free(C_ref); free(C_test);
    }

    fclose(fout);
    printf("Successfully saved results in 'report_task1.md'\n\n");
    return 0;
}
