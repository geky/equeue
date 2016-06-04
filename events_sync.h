/* 
 * System specific mutex implementation
 */
#ifndef EVENTS_SYNC_H
#define EVENTS_SYNC_H

#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <stdbool.h>


// Mutex type
typedef pthread_mutex_t events_mutex_t;

// Mutex operations
static inline void events_mutex_create(events_mutex_t *m) {
    pthread_mutex_init(m, 0);
}

static inline void events_mutex_destroy(events_mutex_t *m) {
    pthread_mutex_destroy(m);
}

static inline void events_mutex_lock(events_mutex_t *m) {
    pthread_mutex_lock(m);
}

static inline void events_mutex_unlock(events_mutex_t *m) {
    pthread_mutex_unlock(m);
}


// Semaphore type
typedef sem_t events_sema_t;

// Semaphore operations
static inline void events_sema_create(events_sema_t *s, unsigned v) {
    sem_init(s, 0, v);
}

static inline void events_sema_destroy(events_sema_t *s) {
    sem_destroy(s);
}

static inline void events_sema_release(events_sema_t *s) {
    sem_post(s);
}

static inline bool events_sema_wait(events_sema_t *s, int ms) {
    if (ms < 0) {
        return !sem_wait(s);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += ms/1000;
        ts.tv_nsec = (ts.tv_nsec + ms*1000) % 1000000;
        return !sem_timedwait(s, &ts);
    }
}


#endif
