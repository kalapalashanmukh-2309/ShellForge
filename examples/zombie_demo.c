#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
    pid_t pid;
    int status;

    printf("Parent Process: PID = %d\n", getpid());

    fflush(NULL);
    pid = fork();

    if (pid < 0)
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
        /* Child Process */
        printf("Child Process: PID = %d\n", getpid());
        printf("Child Process: Exiting...\n");
        exit(10);
    }
    else
    {
        /* Parent Process */
        printf("Parent Process: Child PID = %d\n", pid);
        printf("\nChild has terminated.\n");
        printf("Parent has NOT called wait() yet.\n");
        printf("Therefore, the child becomes a ZOMBIE.\n");
        printf("\n>>> Check the process table NOW.\n");
        printf(">>> You have 20 seconds to run: ps -el\n\n");

        /*
         * Parent does not call wait() immediately.
         * The terminated child remains as a zombie.
         */
        sleep(20);

        /*
         * Now the parent calls wait().
         * This collects the child's exit status
         * and removes the zombie from the process table.
         */
        printf("\nParent: Calling wait() now...\n");

        if (wait(&status) == pid)
        {
            if (WIFEXITED(status))
            {
                printf("Parent: Child exited normally.\n");
                printf("Parent: Child exit status = %d\n",
                       WEXITSTATUS(status));
            }

            printf("Parent: Child has been reaped.\n");
            printf("Parent: Zombie process eliminated.\n");
        }
    }

    return 0;
}
