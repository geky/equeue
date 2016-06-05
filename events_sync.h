/* 
 * System specific mutex implementation
 */
#ifndef EVENTS_SYNC_H
#define EVENTS_SYNC_H

#include <stdbool.h>


// Mutex type
typedef struct {} events_mutex_t;

// Mutex operations
int events_mutex_create(events_mutex_t *m);
void events_mutex_destroy(events_mutex_t *m);
void events_mutex_lock(events_mutex_t *m);
void events_mutex_unlock(events_mutex_t *m);


// Semaphore type
typedef struct {} events_sema_t;

// Semaphore operations
int events_sema_create(events_sema_t *s);
void events_sema_destroy(events_sema_t *s);
void events_sema_release(events_sema_t *s);
bool events_sema_wait(events_sema_t *s, int ms);


#endif
