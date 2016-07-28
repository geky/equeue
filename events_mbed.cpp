/*
 * Implementation for the mbed library
 * https://github.com/mbedmicro/mbed
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#if defined(__MBED__)

#include "events_tick.h"
#include "events_sema.h"
#include "events_mutex.h"

#include <stdbool.h>
#include "mbed.h"
#ifdef MBED_CONF_RTOS_PRESENT
#include "rtos.h"
#endif


// Ticker operations
static class GlobalTicker {
public:
    GlobalTicker() {
        _tick = 0;
        _timer.start();
        _ticker.attach_us(this, &GlobalTicker::step, (1 << 16) * 1000);
    };

    void step() {
        _timer.reset();
        _tick += 1 << 16;
    }

    unsigned tick() {
        return _tick + (unsigned)_timer.read_ms();
    }

private:
    unsigned _tick;
    Timer _timer;
    Ticker _ticker;
} gticker;

unsigned events_tick() {
    return gticker.tick();
}


// Mutex operations
int events_mutex_create(events_mutex_t *m) { return 0; }
void events_mutex_destroy(events_mutex_t *m) { }

void events_mutex_lock(events_mutex_t *m) {
    *m = __get_PRIMASK();
    __disable_irq();
}

void events_mutex_unlock(events_mutex_t *m) {
    __set_PRIMASK(*m);
}


// Semaphore operations
#ifdef MBED_CONF_RTOS_PRESENT

static inline Semaphore *sema(events_sema_t *s) {
    return static_cast<Semaphore*>(*s);
}

int events_sema_create(events_sema_t *s) {
    *s = new Semaphore(0);
    return sema(s) ? 0 : -1;
}

void events_sema_destroy(events_sema_t *s) {
    delete sema(s);
}

void events_sema_release(events_sema_t *s) {
    sema(s)->release();
}

bool events_sema_wait(events_sema_t *s, int ms) {
    int t = sema(s)->wait(ms < 0 ? osWaitForever : ms);
    return t > 0;
}

#else

// Semaphore operations
int events_sema_create(events_sema_t *s) { return 0; }
void events_sema_destroy(events_sema_t *s) {}
void events_sema_release(events_sema_t *s) {}

static void events_sema_wakeup() {}

bool events_sema_wait(events_sema_t *s, int ms) {
    Timeout timeout;
    timeout.attach_us(events_sema_wakeup, ms*1000);

    __WFI();

    return true;
}

#endif

#endif
