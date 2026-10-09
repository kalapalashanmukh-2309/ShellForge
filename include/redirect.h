#ifndef REDIRECT_H
#define REDIRECT_H
#include "shell.h"
#include <spawn.h>
int add_redirect_actions(const Command *command, posix_spawn_file_actions_t *actions);
int redirect_builtin(const Command *command, int saved[3]);
void restore_descriptors(int saved[3]);
#endif
