/* 
 * System specific semaphore implementation
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#ifndef EVENTS_SEMA_H
#define EVENTS_SEMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>


// Semaphore type
//
// Optimal implementation is a binary semaphore,
// however a regular semaphore is sufficient.
#if defined(__unix__)
#include <semaphore.h>
typedef sem_t events_sema_t;
#elif defined(__MBED__)
#ifdef MBED_CONF_RTOS_PRESENT
typedef void *events_sema_t;
#else
typedef struct {} events_sema_t;
#endif
#endif


// Semaphore operations
int events_sema_create(events_sema_t *sema);
void events_sema_destroy(events_sema_t *sema);
void events_sema_release(events_sema_t *sema);
bool events_sema_wait(events_sema_t *sema, int ms);


#ifdef __cplusplus
}
#endif

#endif
