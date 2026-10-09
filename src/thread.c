#include "thread.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_t monitor_thread;
static pthread_mutex_t monitor_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t monitor_wake = PTHREAD_COND_INITIALIZER;
static bool started, stopping, enabled;
static unsigned long command_count;
static unsigned job_count;

static void *monitor(void *unused)
{
    (void)unused;
    /* Terminal interrupts belong to the shell's main thread. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
    pthread_mutex_lock(&monitor_lock);
    while (!stopping) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 10;
        int result = 0;
        while (!stopping && result != ETIMEDOUT)
            result = pthread_cond_timedwait(&monitor_wake, &monitor_lock, &until);
        if (!stopping && enabled) {
            fprintf(stderr, "\n[Monitor] ShellForge running | commands: %lu | background jobs: %u\n", command_count, job_count);
        }
    }
    pthread_mutex_unlock(&monitor_lock);
    return NULL;
}
int start_monitor_thread(bool enable)
{
    enabled = enable;
    stopping = false;
    sigset_t mask, old;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, &old);
    int result = pthread_create(&monitor_thread, NULL, monitor, NULL);
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (result) { fprintf(stderr, "pthread_create: %s\n", strerror(result)); return -1; }
    started = true;
    return 0;
}
void stop_monitor_thread(void)
{
    if (!started) return;
    pthread_mutex_lock(&monitor_lock);
    stopping = true;
    pthread_cond_signal(&monitor_wake);
    pthread_mutex_unlock(&monitor_lock);
    pthread_join(monitor_thread, NULL);
    started = false;
}
void set_monitor(bool value)
{
    pthread_mutex_lock(&monitor_lock);
    enabled = value;
    pthread_mutex_unlock(&monitor_lock);
}
void update_monitor(unsigned long commands, unsigned running)
{
    pthread_mutex_lock(&monitor_lock);
    command_count = commands;
    job_count = running;
    pthread_mutex_unlock(&monitor_lock);
}
void print_monitor_status(void)
{
    pthread_mutex_lock(&monitor_lock);
    printf("Monitor: %s | commands: %lu | background jobs: %u\n", enabled ? "on" : "off", command_count, job_count);
    pthread_mutex_unlock(&monitor_lock);
}

static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned counter;
static void *counter_worker(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 100000; i++) {
        pthread_mutex_lock(&counter_lock);
        counter++;
        pthread_mutex_unlock(&counter_lock);
    }
    return NULL;
}
int demonstrate_threads(void)
{
    pthread_t workers[4];
    unsigned created = 0;
    counter = 0;
    for (; created < 4; created++) {
        int result = pthread_create(&workers[created], NULL, counter_worker, NULL);
        if (result) { fprintf(stderr, "pthread_create: %s\n", strerror(result)); break; }
    }
    for (unsigned i = 0; i < created; i++) pthread_join(workers[i], NULL);
    printf("POSIX threads: %u workers joined\nMutex-protected counter: %u (expected %u)\n", created, counter, created * 100000);
    return created == 4 && counter == 400000 ? 0 : 1;
}

/* A deterministic lost-update example, using atomics to avoid C data-race UB.
 * Both threads load before either stores; the compound increment is not atomic. */
static pthread_mutex_t race_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_wake = PTHREAD_COND_INITIALIZER;
static unsigned arrived, generation;
static bool race_start, race_abort;
static atomic_uint race_counter;
static void rendezvous(void)
{
    pthread_mutex_lock(&race_lock);
    unsigned previous = generation;
    if (++arrived == 2) {
        arrived = 0;
        generation++;
        pthread_cond_broadcast(&race_wake);
    } else while (generation == previous) pthread_cond_wait(&race_wake, &race_lock);
    pthread_mutex_unlock(&race_lock);
}
static void *race_worker(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&race_lock);
    while (!race_start && !race_abort) pthread_cond_wait(&race_wake, &race_lock);
    bool abort = race_abort;
    pthread_mutex_unlock(&race_lock);
    if (abort) return NULL;
    for (unsigned i = 0; i < 1000; i++) {
        unsigned value = atomic_load(&race_counter);
        rendezvous();
        atomic_store(&race_counter, value + 1);
        rendezvous();
    }
    return NULL;
}
int demonstrate_race(void)
{
    pthread_t workers[2];
    unsigned created = 0;
    arrived = generation = 0;
    race_start = race_abort = false;
    atomic_store(&race_counter, 0);
    for (; created < 2; created++) {
        int result = pthread_create(&workers[created], NULL, race_worker, NULL);
        if (result) { fprintf(stderr, "pthread_create: %s\n", strerror(result)); break; }
    }
    pthread_mutex_lock(&race_lock);
    race_abort = created != 2;
    race_start = created == 2;
    pthread_cond_broadcast(&race_wake);
    pthread_mutex_unlock(&race_lock);
    for (unsigned i = 0; i < created; i++) pthread_join(workers[i], NULL);
    if (created != 2) return 1;
    printf("Lost-update demonstration: 2 workers x 1000 increments\nExpected: 2000 | Actual: %u\nSeparate atomic reads/writes do not make an increment atomic.\nRun threads to see a mutex protect the whole increment.\n", atomic_load(&race_counter));
    return 0;
}
