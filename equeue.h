/* 
 * Flexible event queue for dispatching events
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#ifndef EQUEUE_H
#define EQUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

// System specific files
#include "equeue_tick.h"
#include "equeue_mutex.h"
#include "equeue_sema.h"


// Definition of the minimum size of an event
// This size fits the events created in the event_call set of functions.
#define EQUEUE_EVENT_SIZE (sizeof(struct equeue_event) + 3*sizeof(void*))

// Event/queue structures
struct equeue_event {
    struct equeue_event *next;
    int id;
    unsigned target;
    int period;
    void (*dtor)(void *);

    void (*cb)(void *);
    // data follows
};

typedef struct equeue {
    struct equeue_event *queue;
    int next_id;

    void *buffer;
    struct equeue_chunk {
        unsigned size;
        struct equeue_chunk *next;
        struct equeue_chunk *nchunk;
    } *chunks;
    struct equeue_slab {
        unsigned size;
        unsigned char *data;
    } slab;

    struct equeue_event break_;

    equeue_sema_t eventsema;
    equeue_mutex_t queuelock;
    equeue_mutex_t freelock;
} equeue_t;


// Queue operations
//
// Creation results in negative value on failure.
int equeue_create(equeue_t *queue, unsigned size);
int equeue_create_inplace(equeue_t *queue, unsigned size, void *buffer);
void equeue_destroy(equeue_t *queue);

// Dispatch events
//
// Executes any callbacks enqueued for the specified time in milliseconds,
// or forever if ms is negative
void equeue_dispatch(equeue_t *queue, int ms);

// Break a running event loop
//
// Shuts down an unbounded event loop. Already pending events may finish
// executing, but the queue will not continue looping indefinitely.
void equeue_break(equeue_t *queue);

// Simple event calls
//
// Passed callback will be executed in the associated equeue's
// dispatch call with the data pointer passed unmodified
//
// equeue_call       - Immediately post an event to the queue
// equeue_call_in    - Post an event after a specified time in milliseconds
// equeue_call_every - Post an event periodically in milliseconds
//
// These calls will result in 0 if no memory is available, otherwise they
// will result in a unique identifier that can be passed to equeue_cancel.
int equeue_call(equeue_t *queue, void (*cb)(void *), void *data);
int equeue_call_in(equeue_t *queue, int ms, void (*cb)(void *), void *data);
int equeue_call_every(equeue_t *queue, int ms, void (*cb)(void *), void *data);

// Events with queue handled blocks of memory
//
// Argument to equeue_post must point to a result of a equeue_alloc call
// and the associated memory is automatically freed after the event
// is dispatched.
//
// equeue_alloc will result in null if no memory is available
// or the requested size is less than the size passed to equeue_create.
void *equeue_alloc(equeue_t *queue, unsigned size);
void equeue_dealloc(equeue_t *queue, void *event);

// Configure an allocated event
// 
// equeue_event_delay  - Millisecond delay before posting an event
// equeue_event_period - Millisecond period to repeatedly post an event
// equeue_event_dtor   - Destructor to run when the event is deallocated
void equeue_event_delay(void *event, int ms);
void equeue_event_period(void *event, int ms);
void equeue_event_dtor(void *event, void (*dtor)(void *));

// Post an allocted event to the event queue
//
// Argument to equeue_post must point to a result of a equeue_alloc call
// and the associated memory is automatically freed after the event
// is dispatched.
//
// This call results in an unique identifier that can be passed to
// equeue_cancel.
int equeue_post(equeue_t *queue, void (*cb)(void *), void *event);

// Cancel events that are in flight
//
// Every equeue_call function returns a non-negative identifier on success
// that can be used to cancel an in-flight event. If the event has already
// been dispatched or does not exist, no error occurs. Note, this can not
// stop a currently executing event
void equeue_cancel(equeue_t *queue, int event);


#ifdef __cplusplus
}
#endif

#endif
