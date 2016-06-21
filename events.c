#include "events.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>


int equeue_create(struct equeue *q, unsigned size) {
    void *buffer = malloc(size);
    if (!buffer) {
        return -1;
    }

    int err = equeue_create_inplace(q, size, buffer);
    q->buffer = buffer;
    return err;
}

int equeue_create_inplace(struct equeue *q, unsigned size, void *buffer) {
    q->slab.size = size;
    q->slab.data = buffer;
    memset(q->chunks, 0, EVENT_CHUNK_LISTS*sizeof(struct event*));
    q->buffer = 0;

    q->queue = 0;
    q->next_id = 42;
    q->break_ = (struct event){
        .id = 0,
        .period = -1,
    };

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

// equeue allocation functions
static inline unsigned equeue_size(unsigned size) {
    size += sizeof(struct event);
    unsigned alignment = offsetof(struct { char c; struct event e; }, e);
    return (size + alignment-1) & ~(alignment-1);
}

static struct event *equeue_alloc(struct equeue *q, unsigned size) {
    size = equeue_size(size);

    events_mutex_lock(&q->freelock);

    for (int i = 0; i < EVENT_CHUNK_LISTS; i++) {
        if (q->chunks[i] && q->chunks[i]->size >= size) {
            struct event *e = q->chunks[i];
            q->chunks[i] = e->next;
            events_mutex_unlock(&q->freelock);
            return e;
        }
    }

    if (q->slab.size >= size) {
        struct event *e = (struct event *)q->slab.data;
        q->slab.data += size;
        q->slab.size -= size;
        e->size = size;
        events_mutex_unlock(&q->freelock);
        return e;
    }

    events_mutex_unlock(&q->freelock);
    return 0;
}

static void equeue_dealloc(struct equeue *q, struct event *e) {
    int i = 0;

    events_mutex_lock(&q->freelock);

    for (; i < EVENT_CHUNK_LISTS-1; i++) {
        if (q->chunks[i+1] && q->chunks[i+1]->size >= e->size) {
            break;
        }
    }

    e->next = q->chunks[i];
    q->chunks[i] = e;

    events_mutex_unlock(&q->freelock);
}

// event allocation functions
static inline int event_next_id(struct equeue *q) {
    int id = q->next_id++;
    if (q->next_id < 0) {
        q->next_id = 42;
    }
    return id;
}

void *event_alloc(struct equeue *q, unsigned size) {
    struct event *e = equeue_alloc(q, size);
    if (!e) {
        return 0;
    }

    e->id = event_next_id(q);
    e->target = 0;
    e->period = -1;
    e->dtor = 0;

    return e + 1;
}

void event_dealloc(struct equeue *q, void *p) {
    struct event *e = (struct event*)p - 1;

    if (e->dtor) {
        e->dtor(e+1);
    }

    equeue_dealloc(q, e);
}

// equeue scheduling functions
static inline int equeue_tickdiff(unsigned a, unsigned b) {
    return (int)(a - b);
}

static int equeue_enqueue(struct equeue *q, struct event *e, int ms) {
    e->target = events_tick() + (unsigned)ms;

    struct event **p = &q->queue;
    while (*p && equeue_tickdiff((*p)->target, e->target) <= 0) {
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
        event_dealloc(q, e+1);
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

            deadline = equeue_tickdiff(q->queue->target, events_tick());
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
                event_dealloc(q, e+1);
            }
        }

        events_sema_wait(&q->eventsema, deadline);
    }
}

// event functions
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

// simple callbacks 
struct ecallback {
    void (*cb)(void*);
    void *data;
};

static void ecallback_dispatch(void *p) {
    struct ecallback *e = (struct ecallback*)p;
    e->cb(e->data);
}

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
