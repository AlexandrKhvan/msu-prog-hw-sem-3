#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

void execute_pipeline(char *cmds[][10], int n) {
    int in_fd = STDIN_FILENO;
    int pipefd[2];
    pid_t pids[n];

    for (int i = 0; i < n; i++) {
        if (i < n - 1) {
            if (pipe(pipefd) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        pids[i] = fork();
        if (pids[i] == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pids[i] == 0) {
            if (in_fd != STDIN_FILENO) {
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }

            if (i < n - 1) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }

            execvp(cmds[i][0], cmds[i]);
            perror("execvp");
            exit(EXIT_FAILURE);
        }

        if (in_fd != STDIN_FILENO) {
            close(in_fd);
        }

        if (i < n - 1) {
            close(pipefd[1]);
            in_fd = pipefd[0];
        }
    }       

    for (int i = 0; i < n; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

static void capture_pipeline_output(char *cmds[][10], int n, char *out_buf, size_t buf_size) {
    int capture_pipe[2];
    if (pipe(capture_pipe) == -1) {
        perror("pipe capture");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork capture");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        close(capture_pipe[0]);
        dup2(capture_pipe[1], STDOUT_FILENO);
        close(capture_pipe[1]);

        execute_pipeline(cmds, n);
        exit(EXIT_SUCCESS);
    }

    close(capture_pipe[1]);
    memset(out_buf, 0, buf_size);

    size_t total_read = 0;
    ssize_t bytes;
    while (total_read < buf_size - 1 &&
           (bytes = read(capture_pipe[0], out_buf + total_read, buf_size - 1 - total_read)) > 0) {
        total_read += bytes;
    }
    out_buf[total_read] = '\0';

    close(capture_pipe[0]);
    waitpid(pid, NULL, 0);
}

static void test_single_command(void) {
    char buf[256];

    char *cmd1[][10] = {
        {"echo", "test_message", NULL}
    };
    capture_pipeline_output(cmd1, 1, buf, sizeof(buf));
    assert(strcmp(buf, "test_message\n") == 0);

    char *cmd2[][10] = {
        {"printf", "line1\nline2\n", NULL}
    };
    capture_pipeline_output(cmd2, 1, buf, sizeof(buf));
    assert(strcmp(buf, "line1\nline2\n") == 0);

    char *cmd3[][10] = {
        {"expr", "20", "+", "22", NULL}
    };
    capture_pipeline_output(cmd3, 1, buf, sizeof(buf));
    assert(strcmp(buf, "42\n") == 0);

    char *cmd4[][10] = {
        {"echo", "-n", "no_newline", NULL}
    };
    capture_pipeline_output(cmd4, 1, buf, sizeof(buf));
    assert(strcmp(buf, "no_newline") == 0);
}

static void test_two_commands(void) {
    char buf[256];

    char *pipe1[][10] = {
        {"echo", "hello world", NULL},
        {"tr", "a-z", "A-Z", NULL}
    };
    capture_pipeline_output(pipe1, 2, buf, sizeof(buf));
    assert(strcmp(buf, "HELLO WORLD\n") == 0);

    char *pipe2[][10] = {
        {"printf", "delta\nbeta\nalpha\n", NULL},
        {"sort", NULL}
    };
    capture_pipeline_output(pipe2, 2, buf, sizeof(buf));
    assert(strcmp(buf, "alpha\nbeta\ndelta\n") == 0);

    char *pipe3[][10] = {
        {"printf", "apple\nbanana\napricot\n", NULL},
        {"grep", "^ap", NULL}
    };
    capture_pipeline_output(pipe3, 2, buf, sizeof(buf));
    assert(strcmp(buf, "apple\napricot\n") == 0);

    char *pipe4[][10] = {
        {"echo", "user:x:1000:1000", NULL},
        {"cut", "-d:", "-f1", NULL}
    };
    capture_pipeline_output(pipe4, 2, buf, sizeof(buf));
    assert(strcmp(buf, "user\n") == 0);

    char *pipe5[][10] = {
        {"printf", "one\ntwo\nthree\n", NULL},
        {"head", "-n", "1", NULL}
    };
    capture_pipeline_output(pipe5, 2, buf, sizeof(buf));
    assert(strcmp(buf, "one\n") == 0);
}

static void test_multi_stage_pipeline(void) {
    char buf[512];

    char *pipe1[][10] = {
        {"printf", "cherry\napple\nbanana\n", NULL},
        {"grep", "-v", "banana", NULL},
        {"sort", NULL}
    };
    capture_pipeline_output(pipe1, 3, buf, sizeof(buf));
    assert(strcmp(buf, "apple\ncherry\n") == 0);

    char *pipe2[][10] = {
        {"printf", "b 2\na 3\nc 1\n", NULL},
        {"sort", "-k2,2n", NULL},
        {"cut", "-d", " ", "-f1", NULL},
        {"tr", "a-z", "A-Z", NULL}
    };
    capture_pipeline_output(pipe2, 4, buf, sizeof(buf));
    assert(strcmp(buf, "C\nB\nA\n") == 0);

    char *pipe3[][10] = {
        {"printf", "foo\nbar\nfoo\nbaz\nfoo\n", NULL},
        {"sort", NULL},
        {"uniq", "-c", NULL},
        {"grep", "foo", NULL},
        {"tr", "-s", " ", NULL}
    };
    capture_pipeline_output(pipe3, 5, buf, sizeof(buf));
    assert(strstr(buf, "3 foo") != NULL);

    char *big_pipeline[][10] = {
        {"cat", "/etc/passwd", NULL},
        {"cut", "-d:", "-f1", NULL},
        {"sort", NULL},
        {"tr", "a-z", "A-Z", NULL},
        {"head", "-n", "5", NULL}
    };
    capture_pipeline_output(big_pipeline, 5, buf, sizeof(buf));
    assert(strlen(buf) > 0);

    int newline_count = 0;
    for (size_t i = 0; i < strlen(buf); i++) {
        if (buf[i] == '\n') {
            newline_count++;
        } else {
            assert(isupper((unsigned char)buf[i]) || isdigit((unsigned char)buf[i]) || buf[i] == '_' || buf[i] == '-');
        }
    }
    assert(newline_count == 5);
}

int main(void) {
    test_single_command();
    test_two_commands();
    test_multi_stage_pipeline();
    printf("All tests passed successfully.\n");
    return 0;
}
