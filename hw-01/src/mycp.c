#include "mycp.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

int mycp(const char *src_path, const char *dst_path) {
    if (!src_path || !dst_path) {
        return -1;
    }

    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        perror("mycp: open source");
        return -1;
    }

    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        perror("mycp: open destination");
        close(src_fd);
        return -1;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    int status = 0;

    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written = 0;
        while (bytes_written < bytes_read) {
            ssize_t res = write(dst_fd, buffer + bytes_written, bytes_read - bytes_written);
            if (res < 0) {
                perror("mycp: write error");
                status = -1;
                break;
            }
            bytes_written += res;
        }
        if (status < 0) {
            break;
        }
    }

    if (bytes_read < 0) {
        perror("mycp: read error");
        status = -1;
    }

    close(src_fd);
    close(dst_fd);
    return status;
}
