/*
 * Flexible event queue for dispatching events
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#include "equeue.h"

#include <stdlib.h>
#include <string.h>


int equeue_create(equeue_t *q, size_t size) {
    void *buffer = malloc(size);
    if (!buffer) {
        return -1;
    }

    int err = equeue_create_inplace(q, size, buffer);
    q->buffer = buffer;
    return err;
}

int equeue_create_inplace(equeue_t *q, size_t size, void *buffer) {
    q->slab.size = size;
    q->slab.data = buffer;
    q->chunks = 0;
    q->buffer = 0;

    q->queue = 0;
    q->next_id = 42;
    q->break_ = (struct equeue_event){
        .id = 0,
        .period = -1,
    };

    int err;
    err = equeue_sema_create(&q->eventsema);
    if (err < 0) {
        return err;
    }

    err = equeue_mutex_create(&q->queuelock);
    if (err < 0) {
        return err;
    }

    err = equeue_mutex_create(&q->freelock);
    if (err < 0) {
        return err;
    }

    return 0;
}

void equeue_destroy(equeue_t *q) {
    while (q->queue) {
        struct equeue_event *e = q->queue;
        q->queue = e->next;
        equeue_dealloc(q, e+1);
    }

    equeue_mutex_destroy(&q->freelock);
    equeue_mutex_destroy(&q->queuelock);
    equeue_sema_destroy(&q->eventsema);
    free(q->buffer);
}

// equeue allocation functions
static void *equeue_mem_alloc(equeue_t *q, size_t size) {
    size = size + sizeof(unsigned);
    size = (size + sizeof(unsigned)-1) & ~(sizeof(unsigned)-1);
    if (size < sizeof(struct equeue_chunk)) {
        size = sizeof(struct equeue_chunk);
    }

    equeue_mutex_lock(&q->freelock);

    for (struct equeue_chunk **p = &q->chunks; *p; p = &(*p)->nchunk) {
        if ((*p)->size >= size) {
            struct equeue_chunk *c = *p;
            if (c->next) {
                *p = c->next;
                (*p)->nchunk = c->nchunk;
            } else {
                *p = c->nchunk;
            }
            equeue_mutex_unlock(&q->freelock);
            return (unsigned *)c + 1;
        }
    }

    if (q->slab.size >= size) {
        struct equeue_chunk *c = (struct equeue_chunk *)q->slab.data;
        q->slab.data += size;
        q->slab.size -= size;
        c->size = size;
        equeue_mutex_unlock(&q->freelock);
        return (unsigned *)c + 1;
    }

    equeue_mutex_unlock(&q->freelock);
    return 0;
}

static void equeue_mem_dealloc(equeue_t *q, void *e) {
    struct equeue_chunk *c = (struct equeue_chunk *)((unsigned *)e - 1);

    equeue_mutex_lock(&q->freelock);

    struct equeue_chunk **p = &q->chunks;
    while (*p && (*p)->size < c->size) {
        p = &(*p)->nchunk;
    }

    if (*p && (*p)->size == c->size) {
        c->next = *p;
        c->nchunk = (*p)->nchunk;
    } else {
        c->next = 0;
        c->nchunk = *p;
    }
    *p = c;
    
    equeue_mutex_unlock(&q->freelock);
}

// event allocation functions
static inline int equeue_next_id(equeue_t *q) {
    int id = q->next_id++;
    if (q->next_id < 0) {
        q->next_id = 42;
    }
    return id;
}

void *equeue_alloc(equeue_t *q, size_t size) {
    struct equeue_event *e = equeue_mem_alloc(q,
            sizeof(struct equeue_event) + size);
    if (!e) {
        return 0;
    }

    e->id = equeue_next_id(q);
    e->target = 0;
    e->period = -1;
    e->dtor = 0;

    return e + 1;
}

void equeue_dealloc(equeue_t *q, void *p) {
    struct equeue_event *e = (struct equeue_event*)p - 1;

    if (e->dtor) {
        e->dtor(e+1);
    }

    equeue_mem_dealloc(q, e);
}

// equeue scheduling functions
static inline int equeue_tickdiff(unsigned a, unsigned b) {
    return (int)(a - b);
}

static void equeue_enqueue(equeue_t *q, struct equeue_event *e, unsigned ms) {
    e->target = equeue_tick() + ms;

    struct equeue_event **p = &q->queue;
    while (*p && equeue_tickdiff((*p)->target, e->target) <= 0) {
        p = &(*p)->next;
    }
    
    e->next = *p;
    *p = e;
}

static struct equeue_event *equeue_dequeue(equeue_t *q, int id) {
    for (struct equeue_event **p = &q->queue; *p; p = &(*p)->next) {
        if ((*p)->id == id) {
            struct equeue_event *e = *p;
            *p = (*p)->next;
            return e;
        }
    }

    return 0;
}

static int equeue_post_in(equeue_t *q, struct equeue_event *e, int ms) {
    int id = e->id;
    if (ms < 0) {
        equeue_dealloc(q, e+1);
        return id;
    }

    equeue_mutex_lock(&q->queuelock);
    equeue_enqueue(q, e, ms);
    equeue_mutex_unlock(&q->queuelock);

    equeue_sema_release(&q->eventsema);
    return id;
}

int equeue_post(equeue_t *q, void (*cb)(void*), void *p) {
    struct equeue_event *e = (struct equeue_event*)p - 1;
    e->cb = cb;
    int id = equeue_post_in(q, e, e->target);
    return id;
}

void equeue_cancel(equeue_t *q, int id) {
    equeue_mutex_lock(&q->queuelock);
    struct equeue_event *e = equeue_dequeue(q, id);
    equeue_mutex_unlock(&q->queuelock);

    if (e) {
        equeue_dealloc(q, e+1);
    }
}

void equeue_break(equeue_t *q) {
    equeue_post_in(q, &q->break_, 0);
}

void equeue_dispatch(equeue_t *q, int ms) {
    if (ms >= 0) {
        equeue_post_in(q, &q->break_, ms);
    }

    while (1) {
        int deadline = -1;

        while (q->queue) {
            deadline = -1;

            equeue_mutex_lock(&q->queuelock);
            if (!q->queue) {
                equeue_mutex_unlock(&q->queuelock);
                break;
            }

            deadline = equeue_tickdiff(q->queue->target, equeue_tick());
            if (deadline > 0) {
                equeue_mutex_unlock(&q->queuelock);
                break;
            }

            struct equeue_event *e = q->queue;
            q->queue = e->next;

            if (e->period >= 0) {
                // requeue periodic tasks to avoid race conditions
                // in equeue_cancel
                equeue_enqueue(q, e, e->period);
            }
            equeue_mutex_unlock(&q->queuelock);

            if (e == &q->break_) {
                return;
            }

            // actually dispatch the callback
            e->cb(e + 1);

            if (e->period < 0) {
                equeue_dealloc(q, e+1);
            }
        }

        equeue_sema_wait(&q->eventsema, deadline);
    }
}

// event functions
void equeue_event_delay(void *p, int ms) {
    struct equeue_event *e = (struct equeue_event*)p - 1;
    e->target = ms;
}

void equeue_event_period(void *p, int ms) {
    struct equeue_event *e = (struct equeue_event*)p - 1;
    e->period = ms;
}

void equeue_event_dtor(void *p, void (*dtor)(void *)) {
    struct equeue_event *e = (struct equeue_event*)p - 1;
    e->dtor = dtor;
}

// simple callbacks 
struct ecallback {
    void (*cb)(void*);
    void *data;
};

static void ecallback_dispatch(void *p) {
    struct ecallback *e = (struct ecallback*)p;
    e->cb(e->data);
}

int equeue_call(equeue_t *q, void (*cb)(void*), void *data) {
    struct ecallback *e = equeue_alloc(q, sizeof(struct ecallback));
    if (!e) {
        return 0;
    }

    e->cb = cb;
    e->data = data;
    return equeue_post(q, ecallback_dispatch, e);
}

int equeue_call_in(equeue_t *q, int ms, void (*cb)(void*), void *data) {
    struct ecallback *e = equeue_alloc(q, sizeof(struct ecallback));
    if (!e) {
        return 0;
    }

    equeue_event_delay(e, ms);
    e->cb = cb;
    e->data = data;
    return equeue_post(q, ecallback_dispatch, e);
}

int equeue_call_every(equeue_t *q, int ms, void (*cb)(void*), void *data) {
    struct ecallback *e = equeue_alloc(q, sizeof(struct ecallback));
    if (!e) {
        return 0;
    }

    equeue_event_delay(e, ms);
    equeue_event_period(e, ms);
    e->cb = cb;
    e->data = data;
    return equeue_post(q, ecallback_dispatch, e);
}
