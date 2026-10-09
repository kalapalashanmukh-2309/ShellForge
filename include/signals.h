#ifndef SIGNALS_H
#define SIGNALS_H
#include "shell.h"
#include <signal.h>
extern volatile sig_atomic_t shell_interrupted;
void initialize_signals(Shell *shell);
#endif
