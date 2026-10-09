#include "redirect.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int add_redirect_actions(const Command *command, posix_spawn_file_actions_t *actions)
{
    for (size_t i = 0; i < command->redirect_count; i++) {
        const Redirect *r = &command->redirects[i];
        int result = posix_spawn_file_actions_addopen(actions, r->target, r->path, r->flags, 0644);
        if (result) { errno = result; perror("redirection"); return -1; }
    }
    return 0;
}
void restore_descriptors(int saved[3])
{
    fflush(NULL);
    for (int i = 0; i < 3; i++) if (saved[i] >= 0) {
        if (dup2(saved[i], i) < 0) perror("restore descriptor");
        close(saved[i]);
    }
}
int redirect_builtin(const Command *command, int saved[3])
{
    for (int i = 0; i < 3; i++) saved[i] = -1;
    fflush(NULL);
    for (size_t i = 0; i < command->redirect_count; i++) {
        const Redirect *r = &command->redirects[i];
        if (saved[r->target] < 0) {
            saved[r->target] = fcntl(r->target, F_DUPFD_CLOEXEC, 3);
            if (saved[r->target] < 0) { perror("dup"); goto error; }
        }
        int fd = open(r->path, r->flags, 0644);
        if (fd < 0) { perror(r->path); goto error; }
        if (dup2(fd, r->target) < 0) { perror("dup2"); close(fd); goto error; }
        if (fd != r->target) close(fd);
    }
    return 0;
error:
    restore_descriptors(saved);
    return -1;
}
