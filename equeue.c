/*
 * Flexible event queue for dispatching events
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#include "equeue.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// special id for pending static events
#define EQUEUE_PENDING 1

// calculate the relative-difference between absolute times while
// correctly handling overflow conditions
static inline equeue_stick_t equeue_tickdiff(
        equeue_tick_t a, equeue_tick_t b) {
    return equeue_scmp(a, b);
}

// calculate the relative-difference between absolute times, but
// also clamp to zero, resulting in only non-zero values.
static inline equeue_stick_t equeue_clampdiff(
        equeue_tick_t a, equeue_tick_t b) {
    equeue_stick_t diff = equeue_tickdiff(a, b);
    return ~(diff >> (8*sizeof(int)-1)) & diff;
}

// Increment the unique id in an event, hiding the event from cancel
static inline void equeue_incid(equeue_t *q, equeue_event_t *e) {
    e->id += 1;
    if (((equeue_id_t)e->id << q->npw2) <= 0) {
        // TODO natural overflow?
        e->id = 1;
    }
}


// equeue lifetime management
int equeue_create(equeue_t *q, size_t size) {
    // dynamically allocate the specified buffer
    void *buffer = equeue_malloc(size);
    if (!buffer) {
        return EQUEUE_ERR_NOMEM;
    }

    int err = equeue_create_inplace(q, size, buffer);
    if (err) {
        equeue_free(buffer);
        return err;
    }

    q->allocated = buffer;
    return 0;
}

int equeue_create_inplace(equeue_t *q, size_t size, void *buffer) {
    // setup queue around provided buffer
    // check that buffer and size are aligned
    EQUEUE_ASSERT((uintptr_t)buffer % sizeof(uintptr_t) == 0 &&
            size % sizeof(uintptr_t) == 0);
    q->buffer = buffer;
    q->allocated = NULL;
    q->npw2 = equeue_npw2(size);

    q->chunks = NULL;
    q->slab.size = size;
    q->slab.data = q->buffer;

    q->queue = NULL;
    q->tick = equeue_tick();
    q->generation = 0;
    q->break_requested = false;

    q->background.active = false;
    q->background.update = NULL;
    q->background.timer = NULL;

    // initialize platform resources
    int err = equeue_sema_create(&q->eventsema);
    if (err < 0) {
        return err;
    }

    err = equeue_mutex_create(&q->queuelock);
    if (err < 0) {
        return err;
    }

    err = equeue_mutex_create(&q->memlock);
    if (err < 0) {
        return err;
    }

    return 0;
}

void equeue_destroy(equeue_t *q) {
    // call destructors on pending events
    for (equeue_event_t *es = q->queue; es; es = es->next) {
        for (equeue_event_t *e = es->sibling; e; e = e->sibling) {
            if (e->dtor) {
                e->dtor(e + 1);
            }
        }
        if (es->dtor) {
            es->dtor(es + 1);
        }
    }
    // notify background timer
    if (q->background.update) {
        q->background.update(q->background.timer, -1);
    }

    // clean up platform resources + memory
    equeue_mutex_destroy(&q->memlock);
    equeue_mutex_destroy(&q->queuelock);
    equeue_sema_destroy(&q->eventsema);
    equeue_free(q->allocated);
}


// equeue chunk allocation functions
static equeue_event_t *equeue_mem_alloc(equeue_t *q, size_t size) {
    // add event overhead
    size = equeue_alignup(size + sizeof(equeue_event_t), sizeof(void*));

    equeue_mutex_lock(&q->memlock);

    // check if a good chunk is available
    for (equeue_event_t **p = &q->chunks; *p; p = &(*p)->next) {
        if ((*p)->size >= size) {
            equeue_event_t *e = *p;
            if (e->sibling) {
                *p = e->sibling;
                (*p)->next = e->next;
            } else {
                *p = e->next;
            }

            equeue_mutex_unlock(&q->memlock);
            return e;
        }
    }

    // otherwise allocate a new chunk out of the slab
    if (q->slab.size >= size) {
        equeue_event_t *e = (equeue_event_t *)q->slab.data;
        q->slab.data += size;
        q->slab.size -= size;
        e->size = size;
        e->id = 1;

        equeue_mutex_unlock(&q->memlock);
        return e;
    }

    equeue_mutex_unlock(&q->memlock);
    return NULL;
}

static void equeue_mem_dealloc(equeue_t *q, equeue_event_t *e) {
    equeue_mutex_lock(&q->memlock);

    // stick chunk into list of chunks
    equeue_event_t **p = &q->chunks;
    while (*p && (*p)->size < e->size) {
        p = &(*p)->next;
    }

    if (*p && (*p)->size == e->size) {
        e->sibling = *p;
        e->next = (*p)->next;
    } else {
        e->sibling = NULL;
        e->next = *p;
    }
    *p = e;

    equeue_mutex_unlock(&q->memlock);
}

void *equeue_alloc(equeue_t *q, size_t size) {
    equeue_event_t *e = equeue_mem_alloc(q, size);
    if (!e) {
        return NULL;
    }

    e->target = 0;
    e->period = -1;
    e->dtor = NULL;

    return e + 1;
}

void equeue_dealloc(equeue_t *q, void *p) {
    equeue_event_t *e = (equeue_event_t*)p - 1;

    if (e->dtor) {
        e->dtor(e+1);
    }

    equeue_mem_dealloc(q, e);
}


// equeue scheduling functions
static int equeue_enqueue(equeue_t *q, equeue_event_t *e,
        equeue_id_t id, equeue_tick_t tick, bool coalesce) {
    e->target = tick + equeue_clampdiff(e->target, tick);
    e->generation = q->generation;

    equeue_mutex_lock(&q->queuelock);

    // update our id unless we are coalescing and id already matches
    if (coalesce && e->id == id) {
        equeue_mutex_unlock(&q->queuelock);
        return EQUEUE_ERR_INVAL; // TODO correct err?
    }
    e->id = id;

    // find the event slot
    equeue_event_t **p = &q->queue;
    while (*p && equeue_tickdiff((*p)->target, e->target) < 0) {
        p = &(*p)->next;
    }

    // insert at head in slot
    if (*p && (*p)->target == e->target) {
        e->next = (*p)->next;
        if (e->next) {
            e->next->ref = &e->next;
        }
        e->sibling = *p;
        e->sibling->next = NULL;
        e->sibling->ref = &e->sibling;
    } else {
        e->next = *p;
        if (e->next) {
            e->next->ref = &e->next;
        }

        e->sibling = NULL;
    }

    *p = e;
    e->ref = p;

    // notify background timer
    if ((q->background.update && q->background.active) &&
        (q->queue == e && !e->sibling)) {
        q->background.update(q->background.timer,
                equeue_clampdiff(e->target, tick));
    }

    equeue_mutex_unlock(&q->queuelock);

    return 0;
}

static int equeue_unqueue(equeue_t *q, equeue_event_t *e, equeue_id_t id) {
    equeue_mutex_lock(&q->queuelock);
    // check that the local id matches
    if (e->id != id) {
        equeue_mutex_unlock(&q->queuelock);
        return EQUEUE_ERR_NOENT;
    }

    // clear the event and check if already in-flight
    e->cb = NULL;
    e->period = -1;

    equeue_stick_t diff = equeue_tickdiff(e->target, q->tick);
    if (diff < 0 || (diff == 0 && e->generation != q->generation)) {
        equeue_mutex_unlock(&q->queuelock);
        return EQUEUE_ERR_NOENT;
    }

    // disentangle from queue
    if (e->sibling) {
        e->sibling->next = e->next;
        if (e->sibling->next) {
            e->sibling->next->ref = &e->sibling->next;
        }

        *e->ref = e->sibling;
        e->sibling->ref = e->ref;
    } else {
        *e->ref = e->next;
        if (e->next) {
            e->next->ref = e->ref;
        }
    }

    equeue_incid(q, e);
    equeue_mutex_unlock(&q->queuelock);

    return 0;
}

static equeue_event_t *equeue_dequeue(equeue_t *q, equeue_tick_t target) {
    equeue_mutex_lock(&q->queuelock);

    // find all expired events and mark a new generation
    q->generation += 1;
    if (equeue_tickdiff(q->tick, target) <= 0) {
        q->tick = target;
    }

    equeue_event_t *head = q->queue;
    equeue_event_t **p = &head;
    while (*p && equeue_tickdiff((*p)->target, target) <= 0) {
        p = &(*p)->next;
    }

    q->queue = *p;
    if (q->queue) {
        q->queue->ref = &q->queue;
    }

    *p = NULL;

    equeue_mutex_unlock(&q->queuelock);

    // reverse and flatten each slot to match insertion order
    equeue_event_t **tail = &head;
    equeue_event_t *ess = head;
    while (ess) {
        equeue_event_t *es = ess;
        ess = es->next;

        equeue_event_t *prev = NULL;
        for (equeue_event_t *e = es; e; e = e->sibling) {
            e->next = prev;
            prev = e;
        }

        *tail = prev;
        tail = &es->next;
    }

    return head;
}

equeue_id_t equeue_post(equeue_t *q, void (*cb)(void*), void *p) {
    equeue_event_t *e = (equeue_event_t*)p - 1;
    equeue_tick_t tick = equeue_tick();
    e->cb = cb;
    e->target = tick + e->target;

    // hash local id with buffer offset for unique id
    equeue_id_t id = (e->id << q->npw2) | ((unsigned char *)e - q->buffer);

    equeue_enqueue(q, e, e->id, tick, false);
    equeue_sema_signal(&q->eventsema);
    return id;
}

int equeue_cancel(equeue_t *q, equeue_id_t id) {
    if (id < 0) {
        return EQUEUE_ERR_INVAL;
    }

    // decode event from unique id
    equeue_event_t *e = (equeue_event_t *)&q->buffer[id & ((1 << q->npw2)-1)];
    int err = equeue_unqueue(q, e, id >> q->npw2);
    if (err) {
        return err;
    }

    equeue_dealloc(q, e + 1);
    return 0;
}

int equeue_gettimeleft(equeue_t *q, equeue_id_t id) {
    if (id < 0) {
        return EQUEUE_ERR_INVAL;
    }

    // decode event from unique id and check that the local id matches
    equeue_event_t *e = (equeue_event_t *)&q->buffer[id & ((1 << q->npw2)-1)];

    int ret = EQUEUE_ERR_NOENT;
    equeue_mutex_lock(&q->queuelock);
    if (e->id == id >> q->npw2) {
        ret = equeue_clampdiff(e->target, equeue_tick());
    }
    equeue_mutex_unlock(&q->queuelock);
    return ret;
}

void equeue_break(equeue_t *q) {
    equeue_mutex_lock(&q->queuelock);
    q->break_requested = true;
    equeue_mutex_unlock(&q->queuelock);
    equeue_sema_signal(&q->eventsema);
}

int equeue_dispatch(equeue_t *q, equeue_stick_t ms) {
    equeue_tick_t tick = equeue_tick();
    equeue_tick_t timeout = tick + ms;
    q->background.active = false;

    while (1) {
        // collect all the available events and next deadline
        equeue_event_t *es = equeue_dequeue(q, tick);

        // dispatch events
        while (es) {
            equeue_event_t *e = es;
            es = e->next;

            // actually dispatch the callbacks
            void (*cb)(void *) = e->cb;
            if (cb) {
                cb(e + 1);
            }

            if (e->period >= 0) {
                // reenqueue periodic events
                e->target += e->period;
                equeue_enqueue(q, e, e->id, equeue_tick(), false);
            } else if (e->size == 0) {
                // static event means we just mark as not pending
                equeue_mutex_lock(&q->queuelock);
                e->id = 0;
                equeue_mutex_unlock(&q->queuelock);
            } else {
                // deallocate
                equeue_incid(q, e);
                equeue_dealloc(q, e+1);
            }
        }

        equeue_stick_t deadline = -1;
        tick = equeue_tick();

        // check if we should stop dispatching soon
        if (ms >= 0) {
            deadline = equeue_tickdiff(timeout, tick);
            if (deadline <= 0) {
                // update background timer if necessary
                if (q->background.update) {
                    equeue_mutex_lock(&q->queuelock);
                    if (q->background.update && q->queue) {
                        q->background.update(q->background.timer,
                                equeue_clampdiff(q->queue->target, tick));
                    }
                    q->background.active = true;
                    equeue_mutex_unlock(&q->queuelock);
                }
                q->break_requested = false;
                return EQUEUE_ERR_TIMEDOUT;
            }
        }

        // find closest deadline
        equeue_mutex_lock(&q->queuelock);
        if (q->queue) {
            equeue_stick_t diff = equeue_clampdiff(q->queue->target, tick);
            if ((equeue_tick_t)diff < (equeue_tick_t)deadline) {
                deadline = diff;
            }
        }
        equeue_mutex_unlock(&q->queuelock);

        // wait for events
        equeue_sema_wait(&q->eventsema, deadline);

        // check if we were notified to break out of dispatch
        if (q->break_requested) {
            equeue_mutex_lock(&q->queuelock);
            if (q->break_requested) {
                q->break_requested = false;
                equeue_mutex_unlock(&q->queuelock);
                return EQUEUE_ERR_BREAK;
            }
            equeue_mutex_unlock(&q->queuelock);
        }

        // update tick for next iteration
        tick = equeue_tick();
    }
}


// event functions
void equeue_setdelay(equeue_t *q, void *p, equeue_stick_t ms) {
    (void)q;
    equeue_event_t *e = (equeue_event_t*)p - 1;
    e->target = ms;
}

void equeue_setperiod(equeue_t *q, void *p, equeue_stick_t ms) {
    (void)q;
    equeue_event_t *e = (equeue_event_t*)p - 1;
    e->period = ms;
}

void equeue_setdtor(equeue_t *q, void *p, void (*dtor)(void *)) {
    (void)q;
    equeue_event_t *e = (equeue_event_t*)p - 1;
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
        return EQUEUE_ERR_NOMEM;
    }

    e->cb = cb;
    e->data = data;
    return equeue_post(q, ecallback_dispatch, e);
}

int equeue_call_in(equeue_t *q, int ms, void (*cb)(void*), void *data) {
    struct ecallback *e = equeue_alloc(q, sizeof(struct ecallback));
    if (!e) {
        return EQUEUE_ERR_NOMEM;
    }

    equeue_setdelay(q, e, ms);
    e->cb = cb;
    e->data = data;
    return equeue_post(q, ecallback_dispatch, e);
}

int equeue_call_every(equeue_t *q, int ms, void (*cb)(void*), void *data) {
    struct ecallback *e = equeue_alloc(q, sizeof(struct ecallback));
    if (!e) {
        return EQUEUE_ERR_NOMEM;
    }

    equeue_setdelay(q, e, ms);
    equeue_setperiod(q, e, ms);
    e->cb = cb;
    e->data = data;
    return equeue_post(q, ecallback_dispatch, e);
}

// statically allocated event functions
int equeue_event_create(equeue_t *q, equeue_event_t *e) {
    // size = 0 indicate we are static
    e->size = 0;
    e->id = 0;

    // defaults
    e->target = 0;
    e->period = -1;
    e->dtor = NULL;

    return 0;
}

void equeue_event_destroy(equeue_t *q, equeue_event_t *e) {
    // cancel in case we're pending
    equeue_event_cancel(q, e);

    // call destructor
    if (e->dtor) {
        e->dtor(e+1);
    }
}

void equeue_event_setdelay(equeue_t *q,
        equeue_event_t *e, equeue_stick_t ms) {
    EQUEUE_ASSERT(e->id != EQUEUE_PENDING);
    e->target = ms;
}

void equeue_event_setperiod(equeue_t *q,
        equeue_event_t *e, equeue_stick_t ms) {
    EQUEUE_ASSERT(e->id != EQUEUE_PENDING);
    e->period = ms;
}

void equeue_event_setdtor(equeue_t *q,
        equeue_event_t *e, void (*dtor)(void *)) {
    EQUEUE_ASSERT(e->id != EQUEUE_PENDING);
    e->dtor = dtor;
}

int equeue_event_post(equeue_t *q, void (*cb)(void*), equeue_event_t *e) {
    equeue_tick_t tick = equeue_tick();
    e->cb = cb;
    e->target = tick + e->target;

    int err = equeue_enqueue(q, e, EQUEUE_PENDING, tick, true);
    if (err) {
        return err;
    }

    equeue_sema_signal(&q->eventsema);
    return 0;
}

int equeue_event_cancel(equeue_t *q, equeue_event_t *e) {
    // unqueue event
    return equeue_unqueue(q, e, EQUEUE_PENDING);
}

equeue_stick_t equeue_event_gettimeleft(equeue_t *q, equeue_event_t *e) {
    int ret = EQUEUE_ERR_NOENT;
    equeue_mutex_lock(&q->queuelock);
    // not pending => NOENT
    if (e->id == EQUEUE_PENDING) {
        ret = equeue_clampdiff(e->target, equeue_tick());
    }
    equeue_mutex_unlock(&q->queuelock);
    return ret;
}

// backgrounding
void equeue_background(equeue_t *q,
        void (*update)(void *timer, equeue_stick_t ms), void *timer) {
    equeue_mutex_lock(&q->queuelock);
    if (q->background.update) {
        q->background.update(q->background.timer, -1);
    }

    q->background.update = update;
    q->background.timer = timer;

    if (q->background.update && q->queue) {
        q->background.update(q->background.timer,
                equeue_clampdiff(q->queue->target, equeue_tick()));
    }
    q->background.active = true;
    equeue_mutex_unlock(&q->queuelock);
}

struct equeue_chain_context {
    equeue_t *q;
    equeue_t *target;
    equeue_id_t id;
};

static void equeue_chain_dispatch(void *p) {
    equeue_dispatch((equeue_t *)p, 0);
}

static void equeue_chain_update(void *p, equeue_stick_t ms) {
    struct equeue_chain_context *c = (struct equeue_chain_context *)p;
    equeue_cancel(c->target, c->id);

    if (ms >= 0) {
        c->id = equeue_call_in(c->target, ms, equeue_chain_dispatch, c->q);
    } else {
        equeue_dealloc(c->q, c);
    }
}

int equeue_chain(equeue_t *q, equeue_t *target) {
    if (!target) {
        equeue_background(q, NULL, NULL);
        return 0;
    }

    struct equeue_chain_context *c = equeue_alloc(q,
            sizeof(struct equeue_chain_context));
    if (!c) {
        return EQUEUE_ERR_NOMEM;
    }

    c->q = q;
    c->target = target;
    c->id = 0;

    equeue_background(q, equeue_chain_update, c);
    return 0;
}
