/* 
 * System specific mutex implementation
 */
#ifndef EVENTS_MUTEX_H
#define EVENTS_MUTEX_H

#ifdef __cplusplus
extern "C" {
#endif


// Mutex type
//
// If this type is safe in interrupt contexts, then
// the associated event queue will also be safe in
// interrupt contexts.
#if defined(__unix__)
#include <pthread.h>
typedef pthread_mutex_t events_mutex_t;
#elif defined(__MBED__)
typedef unsigned events_mutex_t;
#endif


// Mutex operations
int events_mutex_create(events_mutex_t *m);
void events_mutex_destroy(events_mutex_t *m);
void events_mutex_lock(events_mutex_t *m);
void events_mutex_unlock(events_mutex_t *m);


#ifdef __cplusplus
}
#endif

#endif
