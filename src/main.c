#include "shell.h"
#include "input.h"
#include "parser.h"
#include "process.h"
#include "builtin.h"
#include "signals.h"
#include "thread.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static int execute_line(Shell *shell, const char *line)
{
    Pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) { perror("calloc"); return 1; }
    int result = parse_line(line, pipeline) < 0 ? 2 : execute_pipeline(shell, pipeline, line);
    free_pipeline(pipeline);
    free(pipeline);
    return result;
}
static void executable_path(char *buffer, size_t length, const char *argv0)
{
#ifdef __APPLE__
    uint32_t size = (uint32_t)length;
    if (_NSGetExecutablePath(buffer, &size) == 0) return;
#else
    ssize_t size = readlink("/proc/self/exe", buffer, length - 1);
    if (size > 0) { buffer[size] = '\0'; return; }
#endif
    snprintf(buffer, length, "%s", argv0);
}
int main(int argc, char **argv)
{
    Shell shell = {0};
    executable_path(shell.executable, sizeof(shell.executable), argv[0]);
    if (argc >= 3 && !strcmp(argv[1], "--exec")) {
        execvp(argv[2], &argv[2]);
        int error = errno;
        fprintf(stderr, "shellforge: %s: %s\n", argv[2], strerror(error));
        return error == ENOENT ? 127 : 126;
    }
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts(SHELL_NAME " " VERSION); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        Command help = {.argv = {"help", NULL}, .argc = 1};
        return execute_builtin(&shell, &help);
    }
    /* Pipeline built-ins execute in a fresh process, without inheriting mutexes. */
    if (argc >= 3 && !strcmp(argv[1], "--builtin")) {
        if (argc - 2 > MAX_ARGS) return 2;
        Command command = {0};
        for (int i = 2; i < argc; i++) command.argv[command.argc++] = argv[i];
        return execute_builtin(&shell, &command);
    }
    if (argc != 1 && !(argc == 3 && !strcmp(argv[1], "-c"))) {
        fprintf(stderr, "usage: shellforge [-c command | --help | --version]\n"); return 2;
    }
    shell.interactive = argc == 1 && isatty(STDIN_FILENO);
    /* Prevent stdio from consuming script lines that belong to a child command. */
    setvbuf(stdin, NULL, _IONBF, 0);
    initialize_signals(&shell);
    if (start_monitor_thread(false) < 0) return 1;
    if (shell.interactive) puts("ShellForge " VERSION " | Type help to begin.");
    unsigned long commands = 0;
    if (argc == 3) shell.status = execute_line(&shell, argv[2]);
    else while (!shell.exiting) {
        reap_jobs(&shell);
        update_monitor(commands, running_jobs(&shell));
        shell_interrupted = 0;
        if (shell.interactive) { printf("shellforge> "); fflush(stdout); }
        errno = 0;
        char *line = read_line();
        if (!line) {
            if (errno == EINTR || shell_interrupted) { shell.status = 130; continue; }
            if (errno == E2BIG) { shell.status = 2; continue; }
            if (errno) shell.status = 1;
            break;
        }
        if (*line) commands++;
        shell.status = execute_line(&shell, line);
        free(line);
    }
    stop_monitor_thread();
    terminate_jobs(&shell);
    if (shell.interactive) puts("Goodbye!");
    return shell.status;
}
