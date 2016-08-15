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


// Platform mutex type
//
// The equeue library requires at minimum a non-recursive mutex that is
// safe in interrupt contexts. The mutex section is help for a bounded
// amount of time, so simply disabling interrupts is acceptable
//
// If irq safety is not required, a regular blocking mutex can be used.
#if defined(__unix__)
#include <pthread.h>
typedef pthread_mutex_t equeue_mutex_t;
#elif defined(_WIN32)
#include <windows.h>
typedef CRITICAL_SECTION equeue_mutex_t;
#elif defined(__MBED__)
typedef unsigned equeue_mutex_t;
#elif defined(EQUEUE_PLATFORM_FREERTOS)
#include "FreeRTOS.h"
typedef UBaseType_t equeue_mutex_t;
#endif


// Platform mutex operations
//
// The equeue_mutex_create and equeue_mutex_destroy manage the lifetime
// of the mutex. On error, equeue_mutex_create should return a negative
// error code.
//
// The equeue_mutex_lock and equeue_mutex_unlock lock and unlock the
// underlying mutex.
int equeue_mutex_create(equeue_mutex_t *mutex);
void equeue_mutex_destroy(equeue_mutex_t *mutex);
void equeue_mutex_lock(equeue_mutex_t *mutex);
void equeue_mutex_unlock(equeue_mutex_t *mutex);


#ifdef __cplusplus
}
#endif

#endif
