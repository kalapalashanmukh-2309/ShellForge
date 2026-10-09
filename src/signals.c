#include "signals.h"
#include <stdio.h>
#include <unistd.h>

volatile sig_atomic_t shell_interrupted = 0;
static void interrupt_handler(int signal_number)
{
    (void)signal_number;
    shell_interrupted = 1;
    const char newline = '\n';
    (void)write(STDERR_FILENO, &newline, 1);
}
void initialize_signals(Shell *shell)
{
    struct sigaction action = {0};
    sigemptyset(&action.sa_mask);
    action.sa_handler = interrupt_handler;
    sigaction(SIGINT, &action, NULL);
    action.sa_handler = SIG_IGN;
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGTSTP, &action, NULL);
    sigaction(SIGTTOU, &action, NULL);
    sigaction(SIGTTIN, &action, NULL);
    shell->pgid = getpgrp();
    if (shell->interactive) {
        /* A shell started in an ordinary terminal owns a separate process group. */
        if (getpgrp() != getpid()) (void)setpgid(0, 0);
        shell->pgid = getpgrp();
        (void)tcsetpgrp(STDIN_FILENO, shell->pgid);
    }
}
