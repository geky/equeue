/*
 * Implementation for the mbed library
 * https://github.com/mbedmicro/mbed
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#include "equeue_platform.h"

#if defined(EQUEUE_PLATFORM_MBED)

#include <stdbool.h>
#include "mbed.h"


// Ticker operations
static bool equeue_tick_inited = false;
static volatile unsigned equeue_minutes = 0;
static unsigned equeue_timer[
        (sizeof(Timer)+sizeof(unsigned)-1)/sizeof(unsigned)];
static unsigned equeue_ticker[
        (sizeof(Ticker)+sizeof(unsigned)-1)/sizeof(unsigned)];

static void equeue_tick_update() {
    equeue_minutes += reinterpret_cast<Timer*>(equeue_timer)->read_ms();
    reinterpret_cast<Timer*>(equeue_timer)->reset();
}

static void equeue_tick_init() {
    MBED_STATIC_ASSERT(sizeof(equeue_timer) >= sizeof(Timer),
            "The equeue_timer buffer must fit the class Timer");
    MBED_STATIC_ASSERT(sizeof(equeue_ticker) >= sizeof(Ticker),
            "The equeue_ticker buffer must fit the class Ticker");
    Timer *timer = new (equeue_timer) Timer;
    Ticker *ticker = new (equeue_ticker) Ticker;

    equeue_minutes = 0;
    timer->start();
    ticker->attach_us(equeue_tick_update, 1000 << 16);

    equeue_tick_inited = true;
}

unsigned equeue_tick() {
    if (!equeue_tick_inited) {
        equeue_tick_init();
    }

    unsigned minutes;
    unsigned ms;

    do {
        minutes = equeue_minutes;
        ms = reinterpret_cast<Timer*>(equeue_timer)->read_ms();
    } while (minutes != equeue_minutes);

    return minutes + ms;
}


// Mutex operations
int equeue_mutex_create(equeue_mutex_t *m) { return 0; }
void equeue_mutex_destroy(equeue_mutex_t *m) { }

void equeue_mutex_lock(equeue_mutex_t *m) {
    core_util_critical_section_enter();
}

void equeue_mutex_unlock(equeue_mutex_t *m) {
    core_util_critical_section_exit();
}


// Semaphore operations
#ifdef MBED_CONF_RTOS_PRESENT

int equeue_sema_create(equeue_sema_t *s) {
    osEventFlagsAttr_t attr;
    memset(&attr, 0, sizeof(attr));
    attr.cb_mem = &s->mem;
    attr.cb_size = sizeof(s->mem);

    s->id = osEventFlagsNew(&attr);
    return !s->id ? -1 : 0;
}

void equeue_sema_destroy(equeue_sema_t *s) {
    osEventFlagsDelete(s->id);
}

void equeue_sema_signal(equeue_sema_t *s) {
    osEventFlagsSet(s->id, 1);
}

bool equeue_sema_wait(equeue_sema_t *s, int ms) {
    if (ms < 0) {
        ms = osWaitForever;
    }

    return (osEventFlagsWait(s->id, 1, osFlagsWaitAny, ms) == 1);
}

#else

// Semaphore operations
int equeue_sema_create(equeue_sema_t *s) {
    *s = false;
    return 0;
}

void equeue_sema_destroy(equeue_sema_t *s) {
}

void equeue_sema_signal(equeue_sema_t *s) {
    *s = 1;
}

static void equeue_sema_timeout(equeue_sema_t *s) {
    *s = -1;
}

bool equeue_sema_wait(equeue_sema_t *s, int ms) {
    int signal = 0;
    Timeout timeout;
    if (ms == 0) {
        return false;
    } else if (ms > 0) {
        timeout.attach_us(callback(equeue_sema_timeout, s), ms*1000);
    }

    core_util_critical_section_enter();
    while (!*s) {
        sleep();
        core_util_critical_section_exit();
        core_util_critical_section_enter();
    }

    signal = *s;
    *s = false;
    core_util_critical_section_exit();

    return (signal > 0);
}

#endif

#endif
