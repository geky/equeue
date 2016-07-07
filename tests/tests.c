#include "events.h"
#include <unistd.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>


// Testing setup
static jmp_buf test_buf;
static int test_line;
static int test_failure;

#define test_assert(test) ({                                                \
    if (!(test)) {                                                          \
        test_line = __LINE__;                                               \
        longjmp(test_buf, 1);                                               \
    }                                                                       \
})

#define test_run(func, ...) ({                                              \
    printf("%s: ...", #func);                                               \
    fflush(stdout);                                                         \
                                                                            \
    if (!setjmp(test_buf)) {                                                \
        func(__VA_ARGS__);                                                  \
        printf("\r%s: \e[32mpassed\e[0m\n", #func);                         \
    } else {                                                                \
        printf("\r%s: \e[31mfailed\e[0m at line %d\n", #func, test_line);   \
        test_failure = true;                                                \
    }                                                                       \
})


// Test functions
void pass_func(void *eh) {
}

void simple_func(void *p) {
    *(bool *)p = true;
}

struct indirect {
    bool *touched;
    uint8_t buffer[7];
};

void indirect_func(void *p) {
    struct indirect *i = (struct indirect*)p;
    *i->touched = true;
}

struct timing {
    unsigned tick;
    unsigned delay;
};

void timing_func(void *p) {
    struct timing *timing = (struct timing*)p;
    unsigned tick = events_tick();

    unsigned t1 = timing->delay;
    unsigned t2 = tick - timing->tick;
    test_assert(t1 > t2 - 10 && t1 < t2 + 10);

    timing->tick = tick;
}

struct fragment {
    equeue_t *q;
    unsigned size;
    struct timing timing;
};

void fragment_func(void *p) {
    struct fragment *fragment = (struct fragment*)p;
    timing_func(&fragment->timing);

    struct fragment *nfragment = event_alloc(fragment->q, fragment->size);
    test_assert(nfragment);

    *nfragment = *fragment;
    event_delay(nfragment, fragment->timing.delay);

    int id = event_post(nfragment->q, fragment_func, nfragment);
    test_assert(id);
}


// Simple call tests
void simple_call_test(void) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    bool touched = false;
    event_call(&q, simple_func, &touched);
    equeue_dispatch(&q, 0);
    test_assert(touched);

    equeue_destroy(&q);
}

void simple_call_in_test(void) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    bool touched = false;
    int id = event_call_in(&q, 5, simple_func, &touched);
    test_assert(id);

    equeue_dispatch(&q, 10);
    test_assert(touched);

    equeue_destroy(&q);
}

void simple_call_every_test(void) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    bool touched = false;
    int id = event_call_every(&q, 5, simple_func, &touched);
    test_assert(id);

    equeue_dispatch(&q, 10);
    test_assert(touched);

    equeue_destroy(&q);
}

void simple_post_test(void) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    bool touched = false;
    struct indirect *i = event_alloc(&q, sizeof(struct indirect));
    test_assert(i);

    i->touched = &touched;
    int id = event_post(&q, indirect_func, i);
    test_assert(id);

    equeue_dispatch(&q, 0);
    test_assert(*i->touched);

    equeue_destroy(&q);
}

// Misc tests
void destructor_test(void) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    bool touched = false;
    struct indirect *i = event_alloc(&q, sizeof(struct indirect));
    test_assert(i);

    i->touched = &touched;
    event_dtor(i, indirect_func);
    int id = event_post(&q, pass_func, i);
    test_assert(id);

    equeue_dispatch(&q, 0);
    test_assert(touched);

    touched = false;
    i = event_alloc(&q, sizeof(struct indirect));
    test_assert(i);

    i->touched = &touched;
    event_dtor(i, indirect_func);
    id = event_post(&q, pass_func, i);
    test_assert(id);

    equeue_destroy(&q);
    test_assert(touched);
}

void allocation_failure_test(void) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    void *p = event_alloc(&q, 4096);
    test_assert(!p);

    for (int i = 0; i < 100; i++) {
        p = event_alloc(&q, 0);
    }
    test_assert(!p);

    equeue_destroy(&q);
}

void cancel_test(int N) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    bool touched = false;
    int *ids = malloc(N*sizeof(int));

    for (int i = 0; i < N; i++) {
        ids[i] = event_call(&q, simple_func, &touched);
    }

    for (int i = N-1; i >= 0; i--) {
        event_cancel(&q, ids[i]);
    }

    free(ids);

    equeue_dispatch(&q, 0);
    test_assert(!touched);

    equeue_destroy(&q);
}

void loop_protect_test(void) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    bool touched = false;
    event_call_every(&q, 0, simple_func, &touched);

    equeue_dispatch(&q, 0);
    test_assert(touched);

    touched = false;
    event_call_every(&q, 1, simple_func, &touched);

    equeue_dispatch(&q, 0);
    test_assert(touched);

    equeue_destroy(&q);
}

void break_test(void) {
    equeue_t q;
    int err = equeue_create(&q, 2048);
    test_assert(!err);

    bool touched = false;
    event_call_every(&q, 0, simple_func, &touched);

    equeue_break(&q);
    equeue_dispatch(&q, -1);
    test_assert(touched);

    equeue_destroy(&q);
}

// Barrage tests
void simple_barrage_test(int N) {
    equeue_t q;
    int err = equeue_create(&q, N * 56);
    test_assert(!err);

    for (int i = 0; i < N; i++) {
        struct timing *timing = event_alloc(&q, sizeof(struct timing));
        test_assert(timing);

        timing->tick = events_tick();
        timing->delay = (i+1)*100;
        event_delay(timing, timing->delay);
        event_period(timing, timing->delay);

        int id = event_post(&q, timing_func, timing);
        test_assert(id);
    }

    equeue_dispatch(&q, N*100);

    equeue_destroy(&q);
}

void fragmenting_barrage_test(int N) {
    equeue_t q;
    int err = equeue_create(&q, N * 1000);
    test_assert(!err);

    for (int i = 0; i < N; i++) {
        unsigned size = sizeof(struct fragment) + i*sizeof(int);
        struct fragment *fragment = event_alloc(&q, size);
        test_assert(fragment);

        fragment->q = &q;
        fragment->size = size;
        fragment->timing.tick = events_tick();
        fragment->timing.delay = (i+1)*100;
        event_delay(fragment, fragment->timing.delay);

        int id = event_post(&q, fragment_func, fragment);
        test_assert(id);
    }

    equeue_dispatch(&q, N*100);

    equeue_destroy(&q);
}

struct ethread {
    pthread_t thread;
    equeue_t *q;
    int ms;
};

static void *ethread_dispatch(void *p) {
    struct ethread *t = (struct ethread*)p;
    equeue_dispatch(t->q, t->ms);
    return 0;
}

void multithreaded_barrage_test(int N) {
    equeue_t q;
    int err = equeue_create(&q, N * 56);
    test_assert(!err);

    struct ethread t;
    t.q = &q;
    t.ms = N*100;
    err = pthread_create(&t.thread, 0, ethread_dispatch, &t);
    test_assert(!err);

    for (int i = 0; i < N; i++) {
        struct timing *timing = event_alloc(&q, sizeof(struct timing));
        test_assert(timing);

        timing->tick = events_tick();
        timing->delay = (i+1)*100;
        event_delay(timing, timing->delay);
        event_period(timing, timing->delay);

        int id = event_post(&q, timing_func, timing);
        test_assert(id);
    }

    err = pthread_join(t.thread, 0);
    test_assert(!err);

    equeue_destroy(&q);
}


int main() {
    printf("beginning tests...\n");

    test_run(simple_call_test);
    test_run(simple_call_in_test);
    test_run(simple_call_every_test);
    test_run(simple_post_test);
    test_run(destructor_test);
    test_run(allocation_failure_test);
    test_run(cancel_test, 20);
    test_run(loop_protect_test);
    test_run(break_test);
    test_run(simple_barrage_test, 20);
    test_run(fragmenting_barrage_test, 20);
    test_run(multithreaded_barrage_test, 20);

    printf("done!\n");
    return test_failure;
}
