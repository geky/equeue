/*
 * Implementation for Posix compliant platforms
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#if defined(__unix__)

#include "events_tick.h"
#include "events_sema.h"
#include "events_mutex.h"

#include <time.h>
#include <sys/time.h>


// Tick operations
unsigned events_tick(void) {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned)(tv.tv_sec*1000 + tv.tv_usec/1000);
}


// Mutex operations
int events_mutex_create(events_mutex_t *m) {
    return pthread_mutex_init(m, 0);
}

void events_mutex_destroy(events_mutex_t *m) {
    pthread_mutex_destroy(m);
}

void events_mutex_lock(events_mutex_t *m) {
    pthread_mutex_lock(m);
}

void events_mutex_unlock(events_mutex_t *m) {
    pthread_mutex_unlock(m);
}


// Semaphore operations
int events_sema_create(events_sema_t *s) {
    return sem_init(s, 0, 0);
}

void events_sema_destroy(events_sema_t *s) {
    sem_destroy(s);
}

void events_sema_release(events_sema_t *s) {
    sem_post(s);
}

bool events_sema_wait(events_sema_t *s, int ms) {
    if (ms < 0) {
        return !sem_wait(s);
    } else {
        ms += events_tick();
        struct timespec ts = {
            .tv_sec = ms/1000,
            .tv_nsec = ms*1000000,
        };
        return !sem_timedwait(s, &ts);
    }
}

#endif
