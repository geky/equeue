/* 
 * Flexible event queue for dispatching events
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#ifndef EQUEUE_H
#define EQUEUE_H

#include "equeue_util.h"
#include "equeue_platform.h"

#ifdef __cplusplus
extern "C" {
#endif


// Version info
// Major (top-nibble), incremented on backwards incompatible changes
// Minor (bottom-nibble), incremented on feature additions
#define EQUEUE_VERSION 0x00010001
#define EQUEUE_VERSION_MAJOR (0xffff & (EQUEUE_VERSION >> 16))
#define EQUEUE_VERSION_MINOR (0xffff & (EQUEUE_VERSION >>  0))


// The minimum size of an event
// This size is guaranteed to fit events created by event_call
#define EQUEUE_EVENT_SIZE (sizeof(struct equeue_event) + 2*sizeof(void*))

// Type of event ids, used to represent queued events
typedef int32_t equeue_id_t;

// Internal event structure
struct equeue_event {
    size_t size;
    uint8_t id;
    uint8_t generation;

    struct equeue_event *next;
    struct equeue_event *sibling;
    struct equeue_event **ref;

    equeue_tick_t target;
    equeue_stick_t period;
    void (*dtor)(void *);

    void (*cb)(void *);
    // data follows
};

// Event queue structure
typedef struct equeue {
    struct equeue_event *queue;
    equeue_tick_t tick;
    bool break_requested;
    uint8_t generation;

    uint8_t *buffer;
    uint8_t npw2;
    void *allocated;

    struct equeue_event *chunks;
    struct equeue_slab {
        size_t size;
        uint8_t *data;
    } slab;

    struct equeue_background {
        bool active;
        void (*update)(void *timer, equeue_stick_t ms);
        void *timer;
    } background;

    equeue_sema_t eventsema;
    equeue_mutex_t queuelock;
    equeue_mutex_t memlock;
} equeue_t;


// Queue lifetime operations
//
// Creates and destroys an event queue. The event queue either allocates a
// buffer of the specified size with malloc or uses a user provided buffer
// if constructed with equeue_create_inplace. If a buffer is provided, it
// it must be word aligned.
//
// If the event queue creation fails, equeue_create returns a negative
// error code.
int equeue_create(equeue_t *queue, size_t size);
int equeue_create_inplace(equeue_t *queue, size_t size, void *buffer);
void equeue_destroy(equeue_t *queue);

// Dispatch events
//
// Executes events until the specified milliseconds have passed. If ms is
// negative, equeue_dispatch will dispatch events indefinitely or until
// equeue_break is called on this queue.
//
// When called with a finite timeout, the equeue_dispatch function is
// guaranteed to terminate. When called with a timeout of 0, the
// equeue_dispatch does not wait and is irq safe.
//
// Returns EQUEUE_ERR_TIMEDOUT if the timeout was reached, or returns
// EQUEUE_ERR_BREAK if equeue_break was called.
int equeue_dispatch(equeue_t *queue, equeue_stick_t ms);

// Break out of a running event loop
//
// Forces the specified event queue's dispatch loop to terminate. Pending
// events may finish executing, but no new events will be executed.
void equeue_break(equeue_t *queue);

// Simple event calls
//
// The specified callback will be executed in the context of the event queue's
// dispatch loop. When the callback is executed depends on the call function.
//
// equeue_call       - Immediately post an event to the queue
// equeue_call_in    - Post an event after a specified time in milliseconds
// equeue_call_every - Post an event periodically every milliseconds
//
// All equeue_call functions are irq safe and can act as a mechanism for
// moving events out of irq contexts.
//
// The return value is a unique id that represents the posted event and can
// be passed to equeue_cancel. If there is not enough memory to allocate the
// event, equeue_call returns LFS_ERR_NOMEM.
equeue_id_t equeue_call(equeue_t *queue,
        void (*cb)(void *), void *data);
equeue_id_t equeue_call_in(equeue_t *queue, equeue_stick_t ms,
        void (*cb)(void *), void *data);
equeue_id_t equeue_call_every(equeue_t *queue, equeue_stick_t ms,
        void (*cb)(void *), void *data);

// Allocate memory for events
//
// The equeue_alloc function allocates an event that can be manually dispatched
// with equeue_post. The equeue_dealloc function may be used to free an event
// that has not been posted. Once posted, an event's memory is managed by the
// event queue and should not be deallocated.
//
// Both equeue_alloc and equeue_dealloc are irq safe.
//
// The equeue allocator is designed to minimize jitter in interrupt contexts as
// well as avoid memory fragmentation on small devices. The allocator achieves
// both constant-runtime and zero-fragmentation for fixed-size events, however
// grows linearly as the quantity of different sized allocations increases.
//
// The equeue_alloc function returns a pointer to the event's allocated memory
// and acts as a handle to the underlying event. If there is not enough memory
// to allocate the event, equeue_alloc returns NULL.
void *equeue_alloc(equeue_t *queue, size_t size);
void equeue_dealloc(equeue_t *queue, void *event);

// Configure an allocated event
//
// equeue_event_delay  - Millisecond delay before dispatching an event
// equeue_event_period - Millisecond period for repeating dispatching an event
// equeue_event_dtor   - Destructor to run when the event is deallocated
void equeue_event_delay(void *event, equeue_stick_t ms);
void equeue_event_period(void *event, equeue_stick_t ms);
void equeue_event_dtor(void *event, void (*dtor)(void *));

// Post an event onto the event queue
//
// The equeue_post function takes a callback and a pointer to an event
// allocated by equeue_alloc. The specified callback will be executed in the
// context of the event queue's dispatch loop with the allocated event
// as its argument.
//
// The equeue_post function is irq safe and can act as a mechanism for
// moving events out of irq contexts.
//
// The return value is a unique id that represents the posted event and can
// be passed to equeue_cancel.
equeue_id_t equeue_post(equeue_t *queue, void (*cb)(void *), void *event);

// Cancel an in-flight event
//
// Attempts to cancel an event referenced by the unique id returned from
// equeue_call or equeue_post. It is safe to call equeue_cancel after an event
// has already been dispatched.
//
// The equeue_cancel function is irq safe.
//
// If called while the event queue's dispatch loop is active in another thread,
// equeue_cancel does not guarantee that the event will not execute after it
// returns as the event may have already begun executing.
//
// equeue_cancel returns 0 if it successfully cancelled an event that was
// pending exectution, otherwise it returns LFS_ERR_NOENT if the event is
// not in the queue. Note it is not always necessary to check the return value
// as the event is guaranteed to not execute after equeue_cancel returns.
int equeue_cancel(equeue_t *queue, equeue_id_t id);

// Query how much time is left for delayed event
//
// If event is delayed, this function can be used to query how much time
// is left until the event is due to be dispatched.
//
// This function is irq safe.
//
// If the event is not in the queue, LFS_ERR_NOENT is returned.
equeue_stick_t equeue_timeleft(equeue_t *q, equeue_id_t id);

// Background an event queue onto a single-shot timer
//
// The provided update function will be called to indicate when the queue
// should be dispatched. A negative timeout will be passed to the update
// function when the timer is no longer needed.
//
// Passing a null update function disables the existing timer.
//
// The equeue_background function allows an event queue to take advantage
// of hardware timers or even other event loops, allowing an event queue to
// be effectively backgrounded.
void equeue_background(equeue_t *queue,
        void (*update)(void *timer, equeue_stick_t ms), void *timer);

// Chain an event queue onto another event queue
//
// After chaining a queue to a target, calling equeue_dispatch on the
// target queue will also dispatch events from this queue. The queues
// use their own buffers and events must be managed independently.
//
// Passing a null queue as the target will unchain the existing queue.
//
// The equeue_chain function allows multiple equeues to be composed, sharing
// the context of a dispatch loop while still being managed independently.
//
// If the event queue chaining fails, equeue_chain returns a negative
// error code.
int equeue_chain(equeue_t *queue, equeue_t *target);


#ifdef __cplusplus
}
#endif

#endif
