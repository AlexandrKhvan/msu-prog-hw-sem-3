CC = gcc
CFLAGS = -Wall -Wextra -Werror -O2 -std=c11 -pthread

all: task1 task2 task3

task1: matrix_mult.c
	$(CC) $(CFLAGS) -o task1 matrix_mult.c -lm

task2: external_sort.c
	$(CC) $(CFLAGS) -o task2 external_sort.c

task3: process_search.c
	$(CC) $(CFLAGS) -o task3 process_search.c

reports: all
	./task1
	./task2
	./task3

clean:
	rm -f task1 task2 task3 *.o *.tmp *.bin search_test_*.txt report_task*.md