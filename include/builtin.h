#ifndef BUILTIN_H
#define BUILTIN_H
#include "shell.h"
bool is_builtin(const char *name);
int execute_builtin(Shell *shell, const Command *command);
#endif
