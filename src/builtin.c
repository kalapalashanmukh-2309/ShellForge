#include "builtin.h"
#include "process.h"
#include "thread.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool is_builtin(const char *name)
{
    const char *names[] = {"cd", "pwd", "exit", "help", "jobs", "wait", "status", "monitor", "threads", "race-demo", NULL};
    for (size_t i = 0; names[i]; i++) if (!strcmp(name, names[i])) return true;
    return false;
}
int execute_builtin(Shell *shell, const Command *command)
{
    char *const *args = command->argv;
    const char *name = args[0];
    if (!strcmp(name, "exit")) {
        if (command->argc > 2) { fprintf(stderr, "usage: exit [0-255]\n"); return 2; }
        long code = shell->status;
        if (args[1]) {
            char *end;
            errno = 0;
            code = strtol(args[1], &end, 10);
            if (errno || !*args[1] || *end || code < 0 || code > 255) { fprintf(stderr, "exit: expected a number from 0 to 255\n"); return 2; }
        }
        shell->exiting = true;
        return (int)code;
    }
    if (!strcmp(name, "cd")) {
        if (command->argc > 2) { fprintf(stderr, "usage: cd [directory]\n"); return 2; }
        const char *path = args[1] ? args[1] : getenv("HOME");
        if (!path) { fprintf(stderr, "cd: HOME is unset\n"); return 1; }
        if (chdir(path) < 0) { perror("cd"); return 1; }
        return 0;
    }
    if (!strcmp(name, "pwd")) {
        char *cwd = getcwd(NULL, 0);
        if (!cwd) { perror("pwd"); return 1; }
        puts(cwd); free(cwd); return 0;
    }
    if (!strcmp(name, "help")) {
        puts("ShellForge " VERSION " — Parallel Task Execution Shell\n"
             "Run programs:       ls -l    echo \"hello world\"\n"
             "Pipelines:          ls src | grep .c | wc -l\n"
             "Redirection:        echo hello > out.txt    echo again >> out.txt\n"
             "                    cat < out.txt    ls missing 2> errors.txt\n"
             "Parallel jobs:      sleep 2 &    jobs    wait\n"
             "Built-ins:          cd pwd exit help jobs wait status\n"
             "Threads:            threads    race-demo    monitor on|off|status\n"
             "Earlier demos:      process    pipe    ls-grep    zombie\n"
             "Ctrl+C interrupts a foreground program; Ctrl+D exits at an empty prompt.\n"
             "Quotes and backslash escaping are supported. No expansion, &&, ||, or ;.");
        return 0;
    }
    if (!strcmp(name, "jobs")) { list_jobs(shell); return 0; }
    if (!strcmp(name, "wait")) return wait_jobs(shell);
    if (!strcmp(name, "status")) { printf("%d\n", shell->status); return 0; }
    if (!strcmp(name, "threads")) return demonstrate_threads();
    if (!strcmp(name, "race-demo")) return demonstrate_race();
    if (!strcmp(name, "monitor")) {
        if (command->argc > 2) { fprintf(stderr, "usage: monitor on|off|status\n"); return 2; }
        if (!args[1] || !strcmp(args[1], "status")) { print_monitor_status(); return 0; }
        if (!strcmp(args[1], "on")) set_monitor(true);
        else if (!strcmp(args[1], "off")) set_monitor(false);
        else { fprintf(stderr, "usage: monitor on|off|status\n"); return 2; }
        print_monitor_status(); return 0;
    }
    return 127;
}
