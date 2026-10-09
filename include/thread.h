#ifndef THREAD_H
#define THREAD_H
#include <stdbool.h>
int start_monitor_thread(bool enabled);
void stop_monitor_thread(void);
void set_monitor(bool enabled);
void update_monitor(unsigned long commands, unsigned running);
void print_monitor_status(void);
int demonstrate_threads(void);
int demonstrate_race(void);
#endif
