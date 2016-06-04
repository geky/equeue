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
    events_sema_t *sema;

    void (*cb)(void *);
    void *data;
};

struct equeue {
    unsigned size;
    struct event *queue;
    struct event *free;
    void *buffer;

    events_sema_t eventsema;
    events_mutex_t queuelock;
    events_mutex_t freelock;
};

// Queue operations
void equeue_create(struct equeue*, unsigned count, unsigned size);
void equeue_create_inplace(struct equeue*,
        unsigned count, unsigned size, void *buffer);
void equeue_destroy(struct equeue*);
void equeue_dispatch(struct equeue*, int ms);

// Simple event calls
void event_call(struct equeue*, void (*cb)(void*), void*);
void event_call_in(struct equeue*, void (*cb)(void*), void*, int ms);
void event_call_every(struct equeue*, void (*cb)(void*), void*, int ms);
void event_call_and_wait(struct equeue*, void (*cb)(void*), void*);

// Events with queue handled blocks of memory
void *event_alloc(struct equeue*, unsigned size);
void event_dealloc(struct equeue*, void*);
void event_call_alloced(struct equeue*, void (*cb)(void*), void*);
void event_call_alloced_in(struct equeue*, void (*cb)(void*), void*, int ms);
void event_call_alloced_every(struct equeue*, void (*cb)(void*), void*, int ms);
void event_call_alloced_and_wait(struct equeue*, void (*cb)(void*), void*);


#endif
