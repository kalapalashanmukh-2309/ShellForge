#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../include/process.h"

void demonstrate_process_creation(void)
{
    pid_t pid;

    printf("\n=== Process Creation Demo ===\n");

    fflush(NULL);
    pid = fork();

    if (pid < 0)
    {
        perror("fork failed");
        return;
    }
if (pid == 0)
{
    printf("Child Process\n");
    printf("Child PID  : %d\n", getpid());
    printf("Parent PID : %d\n", getppid());

    sleep(2);

    printf("Child process completed.\n");
    printf("=== Process Demo Finished ===\n\n");

    fflush(stdout);
    _exit(0);
}
else
{
    printf("Parent Process\n");
    printf("Parent PID : %d\n", getpid());
    printf("Child PID  : %d\n", pid);

    wait(NULL);

    printf("Parent: Child process has completed.\n");
    printf("=== Process Demo Finished ===\n\n");
}
}
