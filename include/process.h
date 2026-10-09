#ifndef PROCESS_H
#define PROCESS_H
#include "shell.h"
int execute_pipeline(Shell *shell, const Pipeline *pipeline, const char *line);
void reap_jobs(Shell *shell);
unsigned running_jobs(const Shell *shell);
void list_jobs(Shell *shell);
int wait_jobs(Shell *shell);
void terminate_jobs(Shell *shell);
#endif
