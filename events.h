/* 
 * Flexible event queue for dispatching events
 */
#ifndef EVENTS_H
#define EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

// System specific files
#include "events_tick.h"
#include "events_mutex.h"
#include "events_sema.h"


// Event/queue structures
struct event {
    struct event *next;
    unsigned target;
    int period;
    int id;
    events_sema_t *sema;

    void (*cb)(void *);
    // data follows
};

struct equeue {
    unsigned size;
    struct event *head;
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
// Creation results in negative value on failure.
//
// event_dispatch will execute any callbacks enqueued for the
// specified time in milliseconds, or forever if ms is negative.
int equeue_create(struct equeue*, unsigned count, unsigned size);
int equeue_create_inplace(struct equeue*,
        unsigned count, unsigned size, void *buffer);
void equeue_destroy(struct equeue*);
void equeue_dispatch(struct equeue*, int ms);

// Simple event calls
//
// Passed callback will be executed in the associated equeue's
// dispatch call with the data pointer passed unmodified
//
// event_call       - Immediately post an event to the queue
// event_call_in    - Post an event after a specified time in milliseconds
// event_call_every - Post an event periodically in milliseconds
//
// These calls will result in a negative value if no memory is available,
// otherwise it will result in a unique identifier that can be passed to
// event_wait and event_cancel.
int event_call(struct equeue*, void (*cb)(void*), void *data);
int event_call_in(struct equeue*, void (*cb)(void*), void *data, int ms);
int event_call_every(struct equeue*, void (*cb)(void*), void *data, int ms);

// Events with queue handled blocks of memory
//
// Argument to event_post must point to a result of a event_alloc call
// and the associated memory is automatically freed after the event
// is dispatched.
//
// event_alloc will result in null if no memory is available
// or the requested size is less than the size passed to equeue_create.
void *event_alloc(struct equeue*, unsigned size);
void event_dealloc(struct equeue*, void*);

// Configure an allocated event
// 
// event_delay     - Specify a millisecond delay before posting an event
// event_period    - Specify a millisecond period to repeatedly post an event
// event_tolerance - Specify a +/- millisecond hint to the event loop
void event_delay(void *event, int ms);
void event_period(void *event, int ms);
void event_tolerance(void *event, int ms);

// Post an allocted event to the event queue
//
// Argument to event_post must point to a result of a event_alloc call
// and the associated memory is automatically freed after the event
// is dispatched.
//
// This call results in an unique identifier that can be passed to
// event_wait and event_cancel.
int event_post(struct equeue*, void (*cb)(void*), void *event);

// Waits for an event to complete
//
// Returns a negative value if the event took longer to complete
// than the specified time in milliseconds. A negative time will
// wait forever.
int event_wait(struct equeue*, int event, int ms);

// Cancel events that are in flight
//
// Every event_call function returns a non-negative identifier on success
// that can be used to cancel an in-flight event. If the event has already
// been dispatched or does not exist, no error occurs. Note, this can not
// stop a currently executing event
void event_cancel(struct equeue*, int event);


#ifdef __cplusplus
}
#endif

#endif
