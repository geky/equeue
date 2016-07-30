/* 
 * System specific mutex implementation
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#ifndef EQUEUE_MUTEX_H
#define EQUEUE_MUTEX_H

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
typedef pthread_mutex_t equeue_mutex_t;
#elif defined(__MBED__)
typedef unsigned equeue_mutex_t;
#endif


// Mutex operations
int equeue_mutex_create(equeue_mutex_t *mutex);
void equeue_mutex_destroy(equeue_mutex_t *mutex);
void equeue_mutex_lock(equeue_mutex_t *mutex);
void equeue_mutex_unlock(equeue_mutex_t *mutex);


#ifdef __cplusplus
}
#endif

#endif
