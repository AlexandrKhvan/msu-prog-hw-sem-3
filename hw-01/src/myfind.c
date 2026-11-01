#define _GNU_SOURCE
#include "myfind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int myfind(const char *filepath, const char *word) {
    if (!filepath || !word) {
        return -1;
    }

    FILE *file = fopen(filepath, "r");
    if (!file) {
        perror("myfind: open file error");
        return -1;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read_bytes;
    int line_number = 0;
    int match_count = 0;

    while ((read_bytes = getline(&line, &len, file)) != -1) {
        line_number++;
        if (strstr(line, word) != NULL) {
            printf("%d: %s", line_number, line);
            if (read_bytes > 0 && line[read_bytes - 1] != '\n') {
                printf("\n");
            }
            match_count++;
        }
    }

    free(line);
    fclose(file);
    return match_count;

}
