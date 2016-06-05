#include "events.h"

#include <stdlib.h>


static inline struct event *equeue_event(struct equeue *q, unsigned i) {
    return (struct event*)((char*)q->buffer
            + i*(sizeof(struct event)+q->size));
}

int equeue_create(struct equeue *q, unsigned count, unsigned size) {
    void *buffer = malloc(count * (sizeof(struct event)+q->size));
    if (!buffer) {
        return -1;
    }

    return equeue_create_inplace(q, count, size, buffer);
}

int equeue_create_inplace(struct equeue *q,
        unsigned count, unsigned size, void *buffer) {
    q->size = size;
    q->buffer = buffer;
    q->free = (struct event*)buffer;
    q->queue = 0;

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

static int equeue_enqueue(struct equeue *q, struct event *e, int ms) {
    e->target = events_tick() + (unsigned)ms;

    events_mutex_lock(&q->queuelock);
    struct event **p = &q->queue;
    while (*p && (*p)->target < e->target) {
        p = &(*p)->next;
    }
    
    e->next = *p;
    *p = e;
    events_mutex_unlock(&q->queuelock);

    return 0;
}

void equeue_dispatch(struct equeue *q, int ms) {
    int deadline = ms;

    while (1) {
        if (q->queue) {
            events_mutex_lock(&q->queuelock);
            while (q->queue) {
                deadline = (int)(q->queue->target - events_tick());
                if (deadline > 0) {
                    break;
                }

                struct event *e = q->queue;
                q->queue = e->next;
                events_mutex_unlock(&q->queuelock);

                e->cb(e->data);

                if (e->sema) {
                    events_sema_release(e->sema);
                }

                if (e->period >= 0) {
                    equeue_enqueue(q, e, e->period);
                } else {
                    equeue_dealloc(q, e);
                }

                deadline = ms;
                events_mutex_lock(&q->queuelock);
            }
            events_mutex_unlock(&q->queuelock);
        }

        if (ms >= 0 && ms < deadline) {
            deadline = ms;
        }

        if (!events_sema_wait(&q->eventsema, deadline) && ms == deadline) {
            return;
        }
    }
}


int event_call(struct equeue *q, void (*cb)(void*), void *data) {
    struct event *e = equeue_alloc(q);
    if (!e) {
        return -1;
    }

    e->cb = cb;
    e->data = data;
    e->period = -1;
    e->sema = 0;
    return equeue_enqueue(q, e, 0);
}

int event_call_in(struct equeue *q, void (*cb)(void*), void *data, int ms) {
    struct event *e = equeue_alloc(q);
    if (!e) {
        return -1;
    }

    e->cb = cb;
    e->data = data;
    e->period = -1;
    e->sema = 0;
    return equeue_enqueue(q, e, ms);
}

int event_call_every(struct equeue *q, void (*cb)(void*), void *data, int ms) {
    struct event *e = equeue_alloc(q);
    if (!e) {
        return -1;
    }

    e->cb = cb;
    e->data = data;
    e->period = ms;
    e->sema = 0;
    return equeue_enqueue(q, e, 0);
}

int event_call_and_wait(struct equeue *q, void (*cb)(void*), void *data) {
    events_sema_t sema;
    int err = events_sema_create(&sema);
    if (err < 0) {
        return err;
    }

    struct event *e = equeue_alloc(q);
    e->cb = cb;
    e->data = data;
    e->period = -1;
    e->sema = 0;
    err = equeue_enqueue(q, e, 0);

    events_sema_wait(&sema, -1);
    events_sema_destroy(&sema);
    return err;
}


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

int event_call_alloced(struct equeue *q,
        void (*cb)(void*), void *p) {
    struct event *e = (struct event*)p - 1;
    e->cb = cb;
    e->data = p;
    e->period = -1;
    e->sema = 0;
    return equeue_enqueue(q, e, 0);
}

int event_call_alloced_in(struct equeue *q,
        void (*cb)(void*), void *p, int ms) {
    struct event *e = (struct event*)p - 1;
    e->cb = cb;
    e->data = p;
    e->period = -1;
    e->sema = 0;
    return equeue_enqueue(q, e, ms);
}

int event_call_alloced_every(struct equeue *q,
        void (*cb)(void*), void *p, int ms) {
    struct event *e = (struct event*)p - 1;
    e->cb = cb;
    e->data = p;
    e->period = ms;
    e->sema = 0;
    return equeue_enqueue(q, e, 0);
}

int event_call_alloced_and_wait(struct equeue *q,
        void (*cb)(void*), void *p) {
    events_sema_t sema;
    int err = events_sema_create(&sema); 
    if (err < 0) {
        return err;
    }

    struct event *e = (struct event*)p - 1;
    e->cb = cb;
    e->data = p;
    e->period = -1;
    e->sema = &sema;
    err = equeue_enqueue(q, e, 0);

    events_sema_wait(&sema, -1);
    events_sema_destroy(&sema);
    return err;
}
