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

    void *buffer = malloc(count * (sizeof(struct event)+q->size));
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
    q->head = 0;
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
static struct event *equeue_alloc(struct equeue *q) {
    struct event *e = 0;

    events_mutex_lock(&q->freelock);
    if (q->free) {
        e = q->free;
        q->free = e->next;
    }
    events_mutex_unlock(&q->freelock);

    return e;
}

static void equeue_dealloc(struct equeue *q, struct event *e) {
    events_mutex_lock(&q->freelock);
    e->next = q->free;
    q->free = e;
    events_mutex_unlock(&q->freelock);
}

// equeue scheduling functions
static inline int events_until(unsigned t) {
    return (int)(t - events_tick());
}

static int equeue_next_id(struct equeue *q) {
    int id = q->next_id++;
    if (q->next_id < 0) {
        q->next_id = 42;
    }
    return id;
}

static int equeue_requeue(struct equeue *q, struct event *e, int ms) {
    e->target = events_tick() + (unsigned)ms;

    struct event **p = &q->queue;
    while (*p && (*p)->target <= e->target) {
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

static int equeue_wait(struct equeue *q, int id, int ms) {
    events_sema_t sema;
    struct event *e = 0;

    int err = events_sema_create(&sema);
    if (err < 0) {
        return err;
    }

    events_mutex_lock(&q->queuelock);
    if (q->head->id == id) {
        e = q->head;
    } else {
        for (struct event **p = &q->queue; *p; p = &(*p)->next) {
            if ((*p)->id == id) {
                e = *p;
                break;
            }
        }
    }

    // not enqueued
    if (!e) {
        events_mutex_unlock(&q->queuelock);
        return 0;
    }

    // already waited on
    if (e->sema) {
        events_mutex_unlock(&q->queuelock);
        return -1;
    }

    e->sema = &sema;
    events_mutex_unlock(&q->queuelock);

    return events_sema_wait(&sema, ms);
}

static void equeue_cancel(struct equeue *q, int id) {
    events_mutex_lock(&q->queuelock);
    for (struct event **p = &q->queue; *p; p = &(*p)->next) {
        if ((*p)->id == id) {
            *p = (*p)->next;
            break;
        }
    }
    events_mutex_unlock(&q->queuelock);

    equeue_dealloc(q, e);
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

            deadline = events_until(q->queue->target);
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

            q->head = e;
            events_mutex_unlock(&q->queuelock);

            // actually dispatch the callback
            e->cb(e + 1);

            events_mutex_lock(&q->queuelock);
            q->head = 0;

            if (e->sema) {
                events_sema_release(e->sema);
                e->sema = 0;
            }
            events_mutex_unlock(&q->queuelock);

            if (e->period < 0) {
                equeue_dealloc(q, e);
            }
        }

        if (ms >= 0) {
            int nms = events_until(timeout);
            if ((unsigned)nms < (unsigned)deadline) {
                deadline = nms;
            }
        }

        events_sema_wait(&q->eventsema, deadline);

        if (ms >= 0 && events_until(timeout) <= 0) {
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

    e->id = equeue_next_id(q);
    e->target = 0;
    e->period = -1;
    e->sema = 0;
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

// event operations
int event_post(struct equeue *q, void (*cb)(void*), void *p) {
    struct event *e = (struct event*)p - 1;
    e->cb = cb;
    return equeue_enqueue(q, e, e->target);
}

int event_wait(struct equeue *q, int id, int ms) {
    return equeue_wait(q, id, ms);
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
