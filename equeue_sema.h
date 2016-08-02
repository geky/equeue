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


// Semaphore type
//
// Optimal implementation is a binary semaphore,
// however a regular semaphore is sufficient.
#if defined(__unix__)
#include <semaphore.h>
typedef sem_t equeue_sema_t;
#elif defined(__MBED__)
#ifdef MBED_CONF_RTOS_PRESENT
typedef void *equeue_sema_t;
#else
typedef struct {} equeue_sema_t;
#endif
#endif


// Semaphore operations
int equeue_sema_create(equeue_sema_t *sema);
void equeue_sema_destroy(equeue_sema_t *sema);
void equeue_sema_signal(equeue_sema_t *sema);
bool equeue_sema_wait(equeue_sema_t *sema, int ms);


#ifdef __cplusplus
}
#endif

#endif
