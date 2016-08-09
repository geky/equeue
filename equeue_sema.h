/* 
 * System specific semaphore implementation
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#ifndef EQUEUE_SEMA_H
#define EQUEUE_SEMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>


// Platform semaphore type
//
// The equeue library requires a binary semaphore type that can be safely
// signaled from interrupt contexts and from inside a equeue_mutex section.
//
// The equeue_signal_wait is relied upon by the equeue library to sleep the
// processor between events. Spurious wakeups have no negative-effects.
//
// A counting semaphore will also work, however may cause the event queue
// dispatch loop to run unnecessarily. For that matter, equeue_signal_wait
// may even be implemented as a single return statement.
#if defined(__unix__)
#include <semaphore.h>
typedef sem_t equeue_sema_t;
#elif defined(__MBED__)
#ifdef MBED_CONF_RTOS_PRESENT
typedef unsigned equeue_sema_t[8];
#else
typedef bool equeue_sema_t;
#endif
#endif


// Platform semaphore operations
//
// The equeue_sema_create and equeue_sema_destroy manage the lifetime
// of the semaphore. On error, equeue_sema_create should return a negative
// error code.
//
// The equeue_sema_signal marks a semaphore as signalled such that the next
// equeue_sema_wait will return true.
//
// The equeue_sema_wait waits for a semaphore to be signalled or returns
// immediately if equeue_sema_signal had been called since the last
// equeue_sema_wait. The equeue_sema_wait returns true if it detected that
// equeue_sema_signal had been called.
int equeue_sema_create(equeue_sema_t *sema);
void equeue_sema_destroy(equeue_sema_t *sema);
void equeue_sema_signal(equeue_sema_t *sema);
bool equeue_sema_wait(equeue_sema_t *sema, int ms);


#ifdef __cplusplus
}
#endif

#endif
