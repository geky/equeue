/* 
 * Flexible event queue for dispatching events
 */
#ifndef EVENTS_H
#define EVENTS_H

// System specific files
#include "events_tick.h"
#include "events_sync.h"


// Event/queue structures
struct event {
    struct event *next;
    unsigned target;
    int period;
    int id;
    events_sema_t *sema;

    void (*cb)(void *);
    void *data;
};

struct equeue {
    unsigned size;
    struct event *queue;
    struct event *free;
    void *buffer;
    int next_id;

    events_sema_t eventsema;
    events_mutex_t queuelock;
    events_mutex_t freelock;
};

// Queue operations
//
// Creation results in negative value on failure. equeue_dispatch
// will execute any callbacks enqeueud in the specified queue.
int equeue_create(struct equeue*, unsigned count, unsigned size);
int equeue_create_inplace(struct equeue*,
        unsigned count, unsigned size, void *buffer);
void equeue_destroy(struct equeue*);
void equeue_dispatch(struct equeue*, int ms);

// Simple event calls
//
// Passed callback will be executed in the associated equeue's
// dispatch call.
//
// event_call will result in a negative value if no memory is available.
int event_call(struct equeue*, void (*cb)(void*), void*);
int event_call_in(struct equeue*, void (*cb)(void*), void*, int ms);
int event_call_every(struct equeue*, void (*cb)(void*), void*, int ms);
int event_call_and_wait(struct equeue*, void (*cb)(void*), void*);

// Events with queue handled blocks of memory
//
// Argument to event_call_alloced must point to a result of a 
// event_alloc call and the associated memory is automatically
// freed after the event is dispatched.
//
// event_alloc will result in null and event_call_alloced will result
// in a negative value if no memory is available or requested size is
// less than the size passed to equeue_create.
void *event_alloc(struct equeue*, unsigned size);
void event_dealloc(struct equeue*, void*);
int event_call_alloced(struct equeue*, void (*cb)(void*), void*);
int event_call_alloced_in(struct equeue*, void (*cb)(void*), void*, int ms);
int event_call_alloced_every(struct equeue*, void (*cb)(void*), void*, int ms);
int event_call_alloced_and_wait(struct equeue*, void (*cb)(void*), void*);

// Cancel events that are in flight
//
// Every event_call function returns a non-negative identifier on success
// that can be used to cancel an in-flight event. If the event has already
// been dispatched or does not exist, no error occurs. Note, this does not
// stop a currently executing function
void event_cancel(struct equeue *, int event);


#endif
