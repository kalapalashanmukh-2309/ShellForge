#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "../include/pipe.h"

#define BUFFER_SIZE 1024
#define MESSAGE_COUNT 10000

void demonstrate_producer_consumer(void)
{
    int fd[2];
    pid_t pid;
    char buffer[BUFFER_SIZE];
    const char *message = "ShellForge Producer Message";

    if (pipe(fd) == -1)
    {
        perror("pipe");
        return;
    }

    fflush(NULL);
    pid = fork();

    if (pid == -1)
    {
        perror("fork");
        close(fd[0]); close(fd[1]);
        return;
    }

    if (pid == 0)
    {
        /* Child - Consumer */
        close(fd[1]);

        int total_bytes = 0;
        ssize_t bytes_read;

        while ((bytes_read = read(fd[0], buffer, BUFFER_SIZE - 1)) > 0)
        {
            buffer[bytes_read] = '\0';
            total_bytes += bytes_read;
        }

        close(fd[0]);

        printf("\n=== Consumer Process ===\n");
        printf("Consumer PID: %d\n", getpid());
        printf("Data received: %d bytes\n", total_bytes);
        printf("Consumer completed.\n");
        fflush(stdout);
        _exit(0);
    }
    else
    {
        /* Parent - Producer */
        close(fd[0]);

        struct timeval start, end;

        gettimeofday(&start, NULL);

        for (int i = 0; i < MESSAGE_COUNT; i++)
        {
            write(fd[1], message, strlen(message));
        }

        close(fd[1]);

        gettimeofday(&end, NULL);

        double elapsed =
            (end.tv_sec - start.tv_sec) +
            (end.tv_usec - start.tv_usec) / 1000000.0;

        wait(NULL);

        printf("\n=== Producer Process ===\n");
        printf("Producer PID: %d\n", getpid());
        printf("Messages sent: %d\n", MESSAGE_COUNT);
        printf("Communication time: %.6f seconds\n", elapsed);

        if (elapsed > 0)
        {
            double throughput =
                (MESSAGE_COUNT * strlen(message)) / elapsed;

            printf("Throughput: %.2f bytes/second\n", throughput);
        }

        printf("Producer completed.\n");
    }
}
