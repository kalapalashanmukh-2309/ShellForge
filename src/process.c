#include "process.h"
#include "builtin.h"
#include "redirect.h"
#include "signals.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

static int exit_status(int status)
{
    return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}
void reap_jobs(Shell *shell)
{
    for (size_t j = 0; j < MAX_JOBS; j++) {
        Job *job = &shell->jobs[j];
        if (!job->remaining) continue;
        for (size_t i = 0; i < job->count; i++) {
            if (!job->pids[i]) continue;
            int status;
            pid_t result = waitpid(job->pids[i], &status, WNOHANG);
            if (result > 0 || (result < 0 && errno == ECHILD)) {
                if (i == job->count - 1 && result > 0) job->status = exit_status(status);
                job->pids[i] = 0;
                job->remaining--;
            }
        }
    }
}
unsigned running_jobs(const Shell *shell)
{
    unsigned result = 0;
    for (size_t i = 0; i < MAX_JOBS; i++) if (shell->jobs[i].remaining) result++;
    return result;
}
void list_jobs(Shell *shell)
{
    reap_jobs(shell);
    bool any = false;
    for (size_t i = 0; i < MAX_JOBS; i++) {
        Job *job = &shell->jobs[i];
        if (!job->id) continue;
        printf("[%u] %-7s %ld  %s\n", job->id, job->remaining ? "Running" : "Done", (long)job->pgid, job->label);
        any = true;
    }
    if (!any) puts("No background jobs.");
}
int wait_jobs(Shell *shell)
{
    int result = 0;
    for (size_t j = 0; j < MAX_JOBS; j++) {
        Job *job = &shell->jobs[j];
        for (size_t i = 0; i < job->count; i++) {
            if (!job->pids[i]) continue;
            int status;
            pid_t waited;
            do {
                waited = waitpid(job->pids[i], &status, 0);
                if (shell_interrupted) { shell_interrupted = 0; return 130; }
            } while (waited < 0 && errno == EINTR);
            if (waited > 0 && i == job->count - 1) job->status = exit_status(status);
            job->pids[i] = 0;
        }
        if (job->id) result = job->status;
        memset(job, 0, sizeof(*job));
    }
    return result;
}
void terminate_jobs(Shell *shell)
{
    /* Kill tracked process groups, then reap direct children; never leave jobs behind. */
    for (size_t i = 0; i < MAX_JOBS; i++) if (shell->jobs[i].remaining) {
        kill(-shell->jobs[i].pgid, SIGHUP);
        kill(-shell->jobs[i].pgid, SIGKILL);
    }
    shell_interrupted = 0;
    (void)wait_jobs(shell);
}

int execute_pipeline(Shell *shell, const Pipeline *pipeline, const char *line)
{
    if (!pipeline->count) return shell->status;
    if (pipeline->count == 1 && !pipeline->background && is_builtin(pipeline->commands[0].argv[0])) {
        int saved[3];
        if (redirect_builtin(&pipeline->commands[0], saved) < 0) return 1;
        int result = execute_builtin(shell, &pipeline->commands[0]);
        restore_descriptors(saved);
        return result;
    }
    reap_jobs(shell);
    Job *job = NULL;
    if (pipeline->background) {
        for (size_t i = 0; i < MAX_JOBS; i++) if (!shell->jobs[i].remaining) { job = &shell->jobs[i]; break; }
        if (!job) { fprintf(stderr, "shellforge: background job limit reached\n"); return 1; }
    }
    int pipes[MAX_COMMANDS - 1][2];
    size_t pipe_count = 0, created = 0;
    pid_t pids[MAX_COMMANDS], pgid = 0;
    int result = 1;
    fflush(NULL);
    for (; pipe_count + 1 < pipeline->count; pipe_count++) {
        if (pipe(pipes[pipe_count]) < 0) { perror("pipe"); goto cleanup; }
        fcntl(pipes[pipe_count][0], F_SETFD, FD_CLOEXEC);
        fcntl(pipes[pipe_count][1], F_SETFD, FD_CLOEXEC);
    }
    for (size_t i = 0; i < pipeline->count; i++) {
        const Command *command = &pipeline->commands[i];
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        int error = posix_spawn_file_actions_init(&actions);
        if (error) { errno = error; perror("spawn actions"); goto cleanup; }
        error = posix_spawnattr_init(&attributes);
        if (error) { posix_spawn_file_actions_destroy(&actions); errno = error; perror("spawn attributes"); goto cleanup; }
#define ACTION(call) do { error = (call); if (error) goto spawn_error; } while (0)
        if (i) ACTION(posix_spawn_file_actions_adddup2(&actions, pipes[i-1][0], STDIN_FILENO));
        else if (pipeline->background) ACTION(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
        if (i + 1 < pipeline->count) ACTION(posix_spawn_file_actions_adddup2(&actions, pipes[i][1], STDOUT_FILENO));
        for (size_t j = 0; j < pipe_count; j++) {
            ACTION(posix_spawn_file_actions_addclose(&actions, pipes[j][0]));
            ACTION(posix_spawn_file_actions_addclose(&actions, pipes[j][1]));
        }
        if (add_redirect_actions(command, &actions) < 0) { error = errno; goto spawn_error; }
        sigset_t defaults, empty;
        sigemptyset(&empty);
        sigemptyset(&defaults);
        sigaddset(&defaults, SIGINT); sigaddset(&defaults, SIGQUIT);
        sigaddset(&defaults, SIGTSTP); sigaddset(&defaults, SIGTTIN); sigaddset(&defaults, SIGTTOU);
        ACTION(posix_spawnattr_setsigdefault(&attributes, &defaults));
        ACTION(posix_spawnattr_setsigmask(&attributes, &empty));
        ACTION(posix_spawnattr_setpgroup(&attributes, pgid));
        ACTION(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK));
        char *builtin_args[MAX_ARGS + 3];
        char *const *argv = command->argv;
        const char *file = argv[0];
        char demo_path[4096];
        const char *demo = !strcmp(file, "process") ? "process-demo" :
                           !strcmp(file, "pipe") ? "pipe-demo" :
                           !strcmp(file, "ls-grep") ? "ls-grep-demo" :
                           !strcmp(file, "zombie") ? "zombie-demo" : NULL;
        if (demo) {
            snprintf(demo_path, sizeof(demo_path), "%s", shell->executable);
            char *slash = strrchr(demo_path, '/');
            if (slash) snprintf(slash + 1, sizeof(demo_path) - (size_t)(slash + 1 - demo_path), "%s", demo);
            else snprintf(demo_path, sizeof(demo_path), "bin/%s", demo);
            file = demo_path;
        }
        if (is_builtin(file)) {
            builtin_args[0] = shell->executable;
            builtin_args[1] = "--builtin";
            for (size_t j = 0; j <= command->argc; j++) builtin_args[j + 2] = command->argv[j];
            argv = builtin_args;
            file = shell->executable;
        } else {
            /* A fresh launcher reports exec failures through redirected stderr. */
            builtin_args[0] = shell->executable;
            builtin_args[1] = "--exec";
            builtin_args[2] = (char *)file;
            for (size_t j = 1; j <= command->argc; j++) builtin_args[j + 2] = command->argv[j];
            argv = builtin_args;
            file = shell->executable;
        }
        error = posix_spawnp(&pids[created], file, &actions, &attributes, argv, environ);
spawn_error:
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        if (error) {
            fprintf(stderr, "shellforge: %s: %s\n", command->argv[0], strerror(error));
            result = error == ENOENT ? 127 : 126;
            goto cleanup;
        }
        if (!pgid) pgid = pids[created];
        created++;
    }
    for (size_t i = 0; i < pipe_count; i++) { close(pipes[i][0]); close(pipes[i][1]); }
    pipe_count = 0;
    if (job) {
        memset(job, 0, sizeof(*job));
        job->id = ++shell->next_job;
        job->pgid = pgid;
        job->count = job->remaining = created;
        memcpy(job->pids, pids, created * sizeof(pid_t));
        snprintf(job->label, sizeof(job->label), "%s", line);
        printf("[%u] %ld\n", job->id, (long)pgid);
        return 0;
    }
    if (shell->interactive) (void)tcsetpgrp(STDIN_FILENO, pgid);
    /* A reader may have received SIGTTIN before terminal handoff. */
    kill(-pgid, SIGCONT);
    result = 0;
    for (size_t i = 0; i < created; i++) {
        int status = 0;
        pid_t waited;
        for (;;) {
            waited = waitpid(pids[i], &status, WUNTRACED);
            if (waited < 0 && errno == EINTR) {
                if (shell_interrupted) { kill(-pgid, SIGINT); shell_interrupted = 0; }
                continue;
            }
            if (waited > 0 && WIFSTOPPED(status)) {
                fprintf(stderr, "shellforge: suspended jobs are unsupported; terminating stopped pipeline\n");
                kill(-pgid, SIGKILL);
                continue;
            }
            break;
        }
        if (waited < 0) { perror("waitpid"); result = 1; }
        else if (i == created - 1) result = exit_status(status);
    }
    if (shell->interactive) (void)tcsetpgrp(STDIN_FILENO, shell->pgid);
    return result;
cleanup:
    for (size_t i = 0; i < pipe_count; i++) { close(pipes[i][0]); close(pipes[i][1]); }
    if (pgid) kill(-pgid, SIGKILL);
    for (size_t i = 0; i < created; i++) while (waitpid(pids[i], NULL, 0) < 0 && errno == EINTR) {}
    return result;
}
