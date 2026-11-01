#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define CHUNK_NUM_ITEMS (1024 * 1024)

typedef struct {
    int32_t *data;
    size_t count;
    char out_filename[256];
} SortTask;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int cmp_int32(const void *a, const void *b) {
    int32_t arg1 = *(const int32_t *)a;
    int32_t arg2 = *(const int32_t *)b;
    return (arg1 > arg2) - (arg1 < arg2);
}

void* sort_and_save_worker(void *arg) {
    SortTask *task = (SortTask *)arg;
    qsort(task->data, task->count, sizeof(int32_t), cmp_int32);

    FILE *out_file = fopen(task->out_filename, "wb");
    if (!out_file) {
        perror("Failed to open output file");
        exit(1);
    }

    fwrite(task->data, sizeof(int32_t), task->count, out_file);
    fclose(out_file);
    return NULL;
}

typedef struct {
    int32_t value;
    int file_idx;
} HeapNode;

static void heapify(HeapNode *heap, int n, int i) {
    int smallest = i;
    int left = 2 * i + 1;
    int right = 2 * i + 2;

    if (left < n && heap[left].value < heap[smallest].value) smallest = left;
    if (right < n && heap[right].value < heap[smallest].value) smallest = right;

    if (smallest != i) {
        HeapNode tmp = heap[i];
        heap[i] = heap[smallest];
        heap[smallest] = tmp;
        heapify(heap, n, smallest);
    }
}

void k_way_merge(char filenames[][256], int k, const char *output_file) {
    FILE **files = malloc(k * sizeof(FILE*));
    HeapNode *heap = malloc(k * sizeof(HeapNode));
    int heap_size = 0;

    for (int i = 0; i < k; i++) {
        files[i] = fopen(filenames[i], "rb");
        int32_t val;
        if (files[i] && fread(&val, sizeof(int32_t), 1, files[i]) == 1) {
            heap[heap_size].value = val;
            heap[heap_size].file_idx = i;
            heap_size++;
        }
    }

    for (int i = (heap_size / 2) - 1; i >= 0; i--) {
        heapify(heap, heap_size, i);
    }

    FILE *out = fopen(output_file, "wb");
    setvbuf(out, NULL, _IOFBF, 1024 * 1024);

    while (heap_size > 0) {
        HeapNode root = heap[0];
        fwrite(&root.value, sizeof(int32_t), 1, out);

        int32_t next_val;
        if (fread(&next_val, sizeof(int32_t), 1, files[root.file_idx]) == 1) {
            heap[0].value = next_val;
        } else {
            heap[0] = heap[heap_size - 1];
            heap_size--;
        }
        heapify(heap, heap_size, 0);
    }

    fclose(out);
    for (int i = 0; i < k; i++) {
        if (files[i]) {
            fclose(files[i]);
            unlink(filenames[i]);
        }
    }
    free(files);
    free(heap);
}

bool verify_sorted_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return false;

    int32_t prev, curr;
    if (fread(&prev, sizeof(int32_t), 1, f) == 1) {
        while (fread(&curr, sizeof(int32_t), 1, f) == 1) {
            if (curr < prev) {
                fclose(f);
                return false;
            }
            prev = curr;
        }
    }
    fclose(f);
    return true;
}

void generate_random_file(const char *filename, size_t num_ints) {
    FILE *f = fopen(filename, "wb");
    int32_t buf[4096];
    size_t written = 0;
    while (written < num_ints) {
        size_t batch = (num_ints - written > 4096) ? 4096 : (num_ints - written);
        for (size_t i = 0; i < batch; i++) buf[i] = rand();
        fwrite(buf, sizeof(int32_t), batch, f);
        written += batch;
    }
    fclose(f);
}

void run_sort_test(const char *input_file, const char *output_file, int threads,
                   double *t_sort, double *t_merge, double *t_total) {
    double t0 = get_time_sec();

    FILE *in = fopen(input_file, "rb");
    int run_count = 0;
    char run_files[256][256];

    pthread_t *pt = malloc(threads * sizeof(pthread_t));
    SortTask *tasks = malloc(threads * sizeof(SortTask));

    while (!feof(in)) {
        int launched = 0;
        for (int i = 0; i < threads; i++) {
            int32_t *chunk = malloc(CHUNK_NUM_ITEMS * sizeof(int32_t));
            size_t n = fread(chunk, sizeof(int32_t), CHUNK_NUM_ITEMS, in);
            if (n == 0) {
                free(chunk);
                break;
            }
            tasks[i].data = chunk;
            tasks[i].count = n;
            snprintf(tasks[i].out_filename, 256, "run_%d.tmp", run_count);
            strcpy(run_files[run_count], tasks[i].out_filename);
            run_count++;

            pthread_create(&pt[i], NULL, sort_and_save_worker, &tasks[i]);
            launched++;
        }
        for (int i = 0; i < launched; i++) {
            pthread_join(pt[i], NULL);
        }
    }
    fclose(in);
    free(pt);
    free(tasks);

    double t1 = get_time_sec();
    *t_sort = t1 - t0;

    // Слияние
    k_way_merge(run_files, run_count, output_file);
    double t2 = get_time_sec();
    *t_merge = t2 - t1;
    *t_total = t2 - t0;
}

int main(void) {
    const size_t num_elements[] = {2000000, 8000000, 16000000};
    const int thread_counts[] = {1, 2, 4, 8};
    int num_sizes = sizeof(num_elements) / sizeof(num_elements[0]);
    int num_threads = sizeof(thread_counts) / sizeof(thread_counts[0]);

    FILE *fout = fopen("report_task2.md", "w");
    if (!fout) { perror("fopen report_task2.md"); return 1; }

    fprintf(fout, "| Elements (МБ) | Threads ($P$) | Phase 1: Sorting (s) | Phase 2: Merging (s) | Total Time (s) | Speedup $S$ | Correctness |\n");
    fprintf(fout, "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |\n");

    const char *in_name = "test_raw.bin";
    const char *out_name = "test_sorted.bin";

    for (int s = 0; s < num_sizes; s++) {
        size_t total = num_elements[s];
        double mb = (double)(total * sizeof(int32_t)) / (1024.0 * 1024.0);
        generate_random_file(in_name, total);
        double t_base_total = 0.0;

        for (int t = 0; t < num_threads; t++) {
            int P = thread_counts[t];
            double t_sort, t_merge, t_total;

            run_sort_test(in_name, out_name, P, &t_sort, &t_merge, &t_total);
            if (P == 1) t_base_total = t_total;

            bool ok = verify_sorted_file(out_name);

            printf("[%.0f MB | P=%d] Sort: %.3fs | Merge: %.3fs | Total: %.3fs (%.2fx) | %s\n",
                   mb, P, t_sort, t_merge, t_total, t_base_total / t_total, ok ? "OK" : "ERR");

            fprintf(fout, "| %.1f MB (%zu) | %d | %.4f | %.4f | %.4f | %.2fx | %s |\n",
                    mb, total, P, t_sort, t_merge, t_total, t_base_total / t_total, ok ? "OK" : "FAIL");
            fflush(fout);
            unlink(out_name);
        }

        unlink(in_name);
    }

    fclose(fout);
    printf("Successfully saved results in 'report_task2.md'\n\n");
    return 0;
}
