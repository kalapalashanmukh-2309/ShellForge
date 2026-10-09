#ifndef SHELL_H
#define SHELL_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#define SHELL_NAME "ShellForge"
#define VERSION "3.0.0"
#define MAX_COMMANDS 32
#define MAX_ARGS 256
#define MAX_REDIRECTS 32
#define MAX_JOBS 64
#define MAX_LINE 65536
typedef struct { int target; int flags; char *path; } Redirect;
typedef struct {
    char *argv[MAX_ARGS + 1];
    size_t argc;
    Redirect redirects[MAX_REDIRECTS];
    size_t redirect_count;
} Command;
typedef struct {
    Command commands[MAX_COMMANDS];
    size_t count;
    bool background;
} Pipeline;
typedef struct {
    unsigned id;
    pid_t pgid, pids[MAX_COMMANDS];
    size_t count, remaining;
    int status;
    char label[160];
} Job;
typedef struct {
    bool interactive, exiting;
    int status;
    pid_t pgid;
    unsigned next_job;
    Job jobs[MAX_JOBS];
    char executable[4096];
} Shell;
#endif
