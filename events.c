#include "events.h"

#include <stdlib.h>
#include <stddef.h>


// internal callback callback
struct ecallback {
    void (*cb)(void*);
    void *data;
};

static void ecallback_dispatch(void *p) {
    struct ecallback *e = (struct ecallback*)p;
    e->cb(e->data);
}

// equeue functions
static inline struct event *equeue_event(struct equeue *q, unsigned i) {
    return (struct event*)((char*)q->buffer + i*q->size);
}

static inline unsigned equeue_size(unsigned size) {
    if (size < sizeof(struct ecallback)) {
        size = sizeof(struct ecallback);
    }

    unsigned alignment = offsetof(struct { char c; struct event e; }, e);
    size += sizeof(struct event);
    return (size + alignment-1) & ~(alignment-1);
}

int equeue_create(struct equeue *q, unsigned count, unsigned size) {
    void *buffer = malloc(count * equeue_size(size));
    if (!buffer) {
        return -1;
    }

    return equeue_create_inplace(q, count, size, buffer);
}

int equeue_create_inplace(struct equeue *q,
        unsigned count, unsigned size, void *buffer) {
    q->size = equeue_size(size);
    q->buffer = buffer;
    q->free = (struct event*)buffer;
    q->queue = 0;
    q->next_id = 42;
    q->break_ = (struct event){
        .id = 0,
        .period = -1,
    };

    if (q->free) {
        for (unsigned i = 0; i < count-1; i++) {
            equeue_event(q, i)->next = equeue_event(q, i+1);
        }
        equeue_event(q, count-1)->next = 0;
    }

    int err;
    err = events_sema_create(&q->eventsema);
    if (err < 0) {
        return err;
    }

    err = events_mutex_create(&q->queuelock);
    if (err < 0) {
        return err;
    }

    err = events_mutex_create(&q->freelock);
    if (err < 0) {
        return err;
    }

    return 0;
}

void equeue_destroy(struct equeue *q) {
    events_mutex_destroy(&q->freelock);
    events_mutex_destroy(&q->queuelock);
    events_sema_destroy(&q->eventsema);
    free(q->buffer);
}

// equeue mem functions
static int equeue_next_id(struct equeue *q) {
    int id = q->next_id++;
    if (q->next_id < 0) {
        q->next_id = 42;
    }
    return id;
}

static struct event *equeue_alloc(struct equeue *q) {
    struct event *e = 0;

    events_mutex_lock(&q->freelock);
    if (!q->free) {
        events_mutex_unlock(&q->freelock);
        return 0;
    }

    e = q->free;
    q->free = e->next;
    events_mutex_unlock(&q->freelock);

    e->id = equeue_next_id(q);
    e->target = 0;
    e->period = -1;
    e->dtor = 0;
    return e;
}

static void equeue_dealloc(struct equeue *q, struct event *e) {
    if (e->dtor) {
        e->dtor(e+1);
    }

    events_mutex_lock(&q->freelock);
    e->next = q->free;
    q->free = e;
    events_mutex_unlock(&q->freelock);
}

// equeue scheduling functions
static inline int tickdiff(unsigned a, unsigned b) {
    return (int)(a - b);
}

static int equeue_enqueue(struct equeue *q, struct event *e, int ms) {
    e->target = events_tick() + (unsigned)ms;

    struct event **p = &q->queue;
    while (*p && tickdiff((*p)->target, e->target) <= 0) {
        p = &(*p)->next;
    }
    
    e->next = *p;
    *p = e;

    return e->id;
}

static struct event *equeue_dequeue(struct equeue *q, int id) {
    for (struct event **p = &q->queue; *p; p = &(*p)->next) {
        if ((*p)->id == id) {
            struct event *e = *p;
            *p = (*p)->next;
            return e;
        }
    }

    return 0;
}

static int equeue_post(struct equeue *q, struct event *e, int ms) {
    events_mutex_lock(&q->queuelock);
    int id = equeue_enqueue(q, e, ms);
    events_mutex_unlock(&q->queuelock);
    events_sema_release(&q->eventsema);
    return id;
}

static void equeue_cancel(struct equeue *q, int id) {
    events_mutex_lock(&q->queuelock);
    struct event *e = equeue_dequeue(q, id);
    events_mutex_unlock(&q->queuelock);

    if (e) {
        equeue_dealloc(q, e);
    }
}

void equeue_break(struct equeue *q) {
    equeue_post(q, &q->break_, 0);
}

void equeue_dispatch(struct equeue *q, int ms) {
    if (ms >= 0) {
        equeue_post(q, &q->break_, ms);
    }

    while (1) {
        int deadline = -1;

        while (q->queue) {
            deadline = -1;

            events_mutex_lock(&q->queuelock);
            if (!q->queue) {
                events_mutex_unlock(&q->queuelock);
                break;
            }

            deadline = tickdiff(q->queue->target, events_tick());
            if (deadline > 0) {
                events_mutex_unlock(&q->queuelock);
                break;
            }

            struct event *e = q->queue;
            q->queue = e->next;

            if (e->period >= 0) {
                // requeue periodic tasks to avoid race conditions
                // in event_cancel
                equeue_enqueue(q, e, e->period);
            }
            events_mutex_unlock(&q->queuelock);

            if (e == &q->break_) {
                return;
            }

            // actually dispatch the callback
            e->cb(e + 1);

            if (e->period < 0) {
                equeue_dealloc(q, e);
            }
        }

        events_sema_wait(&q->eventsema, deadline);
    }
}

// event functions
void *event_alloc(struct equeue *q, unsigned size) {
    if (size > q->size - sizeof(struct event)) {
        return 0;
    }

    struct event *e = equeue_alloc(q);
    if (!e) {
        return 0;
    }

    return e + 1;
}

void event_dealloc(struct equeue *q, void *p) {
    struct event *e = (struct event*)p - 1;
    equeue_dealloc(q, e);
}

// configuring events
void event_delay(void *p, int ms) {
    struct event *e = (struct event*)p - 1;
    e->target = ms;
}

void event_period(void *p, int ms) {
    struct event *e = (struct event*)p - 1;
    e->period = ms;
}

void event_dtor(void *p, void (*dtor)(void *)) {
    struct event *e = (struct event*)p - 1;
    e->dtor = dtor;
}

// event operations
int event_post(struct equeue *q, void (*cb)(void*), void *p) {
    struct event *e = (struct event*)p - 1;
    e->cb = cb;
    int id = equeue_post(q, e, e->target);
    return id;
}

void event_cancel(struct equeue *q, int id) {
    return equeue_cancel(q, id);
}

// event helper functions
int event_call(struct equeue *q, void (*cb)(void*), void *data) {
    struct ecallback *e = event_alloc(q, sizeof(struct ecallback));
    if (!e) {
        return 0;
    }

    e->cb = cb;
    e->data = data;
    return event_post(q, ecallback_dispatch, e);
}

int event_call_in(struct equeue *q, void (*cb)(void*), void *data, int ms) {
    struct ecallback *e = event_alloc(q, sizeof(struct ecallback));
    if (!e) {
        return 0;
    }

    event_delay(e, ms);
    e->cb = cb;
    e->data = data;
    return event_post(q, ecallback_dispatch, e);
}

int event_call_every(struct equeue *q, void (*cb)(void*), void *data, int ms) {
    struct ecallback *e = event_alloc(q, sizeof(struct ecallback));
    if (!e) {
        return 0;
    }

    event_delay(e, ms);
    event_period(e, ms);
    e->cb = cb;
    e->data = data;
    return event_post(q, ecallback_dispatch, e);
}
