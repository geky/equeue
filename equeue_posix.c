/*
 * Implementation for Posix compliant platforms
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#if defined(__unix__)

#include "equeue_tick.h"
#include "equeue_sema.h"
#include "equeue_mutex.h"

#include <time.h>
#include <sys/time.h>


// Tick operations
unsigned equeue_tick(void) {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned)(tv.tv_sec*1000 + tv.tv_usec/1000);
}


// Mutex operations
int equeue_mutex_create(equeue_mutex_t *m) {
    return pthread_mutex_init(m, 0);
}

void equeue_mutex_destroy(equeue_mutex_t *m) {
    pthread_mutex_destroy(m);
}

void equeue_mutex_lock(equeue_mutex_t *m) {
    pthread_mutex_lock(m);
}

void equeue_mutex_unlock(equeue_mutex_t *m) {
    pthread_mutex_unlock(m);
}


// Semaphore operations
int equeue_sema_create(equeue_sema_t *s) {
    return sem_init(s, 0, 0);
}

void equeue_sema_destroy(equeue_sema_t *s) {
    sem_destroy(s);
}

void equeue_sema_release(equeue_sema_t *s) {
    sem_post(s);
}

bool equeue_sema_wait(equeue_sema_t *s, int ms) {
    if (ms < 0) {
        return !sem_wait(s);
    } else {
        ms += equeue_tick();
        struct timespec ts = {
            .tv_sec = ms/1000,
            .tv_nsec = ms*1000000,
        };
        return !sem_timedwait(s, &ts);
    }
}

#endif
