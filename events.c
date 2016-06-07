#include "events.h"

#include <stdlib.h>


struct ecallback {
    void (*cb)(void*);
    void *data;
};

// equeue functions
static inline struct event *equeue_event(struct equeue *q, unsigned i) {
    return (struct event*)((char*)q->buffer
            + i*(sizeof(struct event)+q->size));
}

int equeue_create(struct equeue *q, unsigned count, unsigned size) {
    if (size < sizeof(struct ecallback)) {
        size = sizeof(struct ecallback);
    }

    void *buffer = malloc(count * (sizeof(struct event)+size));
    if (!buffer) {
        return -1;
    }

    return equeue_create_inplace(q, count, size, buffer);
}

int equeue_create_inplace(struct equeue *q,
        unsigned count, unsigned size, void *buffer) {
    if (size < sizeof(struct ecallback)) {
        size = sizeof(struct ecallback);
    }

    q->size = size;
    q->buffer = buffer;
    q->free = (struct event*)buffer;
    q->queue = 0;
    q->next_id = 42;

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

static int equeue_requeue(struct equeue *q, struct event *e, int ms) {
    e->target = events_tick() + (unsigned)ms;

    struct event **p = &q->queue;
    while (*p && tickdiff((*p)->target, e->target) <= 0) {
        p = &(*p)->next;
    }
    
    e->next = *p;
    *p = e;

    return e->id;
}

static int equeue_enqueue(struct equeue *q, struct event *e, int ms) {
    events_mutex_lock(&q->queuelock);
    int id = equeue_requeue(q, e, ms);
    events_mutex_unlock(&q->queuelock);
    events_sema_release(&q->eventsema);
    return id;
}

static void equeue_cancel(struct equeue *q, int id) {
    struct event *e = 0;

    events_mutex_lock(&q->queuelock);
    for (struct event **p = &q->queue; *p; p = &(*p)->next) {
        if ((*p)->id == id) {
            e = *p;
            *p = (*p)->next;
            break;
        }
    }
    events_mutex_unlock(&q->queuelock);

    if (e) {
        equeue_dealloc(q, e);
    }
}

void equeue_dispatch(struct equeue *q, int ms) {
    unsigned timeout = events_tick() + (unsigned)ms;
    int deadline = -1;

    while (1) {
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
                equeue_requeue(q, e, e->period);
            }
            events_mutex_unlock(&q->queuelock);

            // actually dispatch the callback
            e->cb(e + 1);

            if (e->period < 0) {
                equeue_dealloc(q, e);
            }
        }

        if (ms >= 0) {
            int nms = tickdiff(timeout, events_tick());
            if ((unsigned)nms < (unsigned)deadline) {
                deadline = nms;
            }
        }

        events_sema_wait(&q->eventsema, deadline);

        if (ms >= 0 && tickdiff(timeout, events_tick()) <= 0) {
            return;
        }
    }
}

// event functions
void *event_alloc(struct equeue *q, unsigned size) {
    if (size > q->size) {
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
    return equeue_enqueue(q, e, e->target);
}

void event_cancel(struct equeue *q, int id) {
    return equeue_cancel(q, id);
}

// event helper functions
static void ecallback_dispatch(void *p) {
    struct ecallback *e = (struct ecallback*)p;
    e->cb(e->data);
}

int event_call(struct equeue *q, void (*cb)(void*), void *data) {
    struct ecallback *e = event_alloc(q, sizeof(struct ecallback));
    e->cb = cb;
    e->data = data;
    return event_post(q, ecallback_dispatch, e);
}

int event_call_in(struct equeue *q, void (*cb)(void*), void *data, int ms) {
    struct ecallback *e = event_alloc(q, sizeof(struct ecallback));
    event_delay(e, ms);
    e->cb = cb;
    e->data = data;
    return event_post(q, ecallback_dispatch, e);
}

int event_call_every(struct equeue *q, void (*cb)(void*), void *data, int ms) {
    struct ecallback *e = event_alloc(q, sizeof(struct ecallback));
    event_delay(e, ms);
    event_period(e, ms);
    e->cb = cb;
    e->data = data;
    return event_post(q, ecallback_dispatch, e);
}
