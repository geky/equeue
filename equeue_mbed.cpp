/*
 * Implementation for the mbed library
 * https://github.com/mbedmicro/mbed
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#if defined(__MBED__)

#include "equeue_tick.h"
#include "equeue_sema.h"
#include "equeue_mutex.h"

#include <stdbool.h>
#include "mbed.h"


// Ticker operations
class EqueueTicker {
public:
    EqueueTicker() {
        _tick = 0;
        _timer.start();
        _ticker.attach_us(this, &EqueueTicker::update, (1 << 16) * 1000);
    };

    void update() {
        _timer.reset();
        _tick += 1 << 16;
    }

    unsigned tick() {
        return _tick + (unsigned)_timer.read_ms();
    }

private:
    unsigned _tick;
#ifdef DEVICE_LOWPOWERTIMER
    LowPowerTimer _timer;
    LowPowerTicker _ticker;
#else
    Timer _timer;
    Ticker _ticker;
#endif
};

static EqueueTicker equeue_ticker;

unsigned equeue_tick() {
    return equeue_ticker.tick();
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
    MBED_ASSERT(sizeof(equeue_sema_t) >= sizeof(Semaphore));
    new (s) Semaphore(0);
    return 0;
}

void equeue_sema_destroy(equeue_sema_t *s) {
    reinterpret_cast<Semaphore*>(s)->~Semaphore();
}

void equeue_sema_signal(equeue_sema_t *s) {
    reinterpret_cast<Semaphore*>(s)->release();
}

bool equeue_sema_wait(equeue_sema_t *s, int ms) {
    if (ms < 0) {
        ms = osWaitForever;
    }

    return (reinterpret_cast<Semaphore*>(s)->wait(ms) > 0);
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
    *s = true;
}

bool equeue_sema_wait(equeue_sema_t *s, int ms) {
    Timeout timeout;
    timeout.attach_us(s, equeue_sema_signal, ms*1000);

    core_util_critical_section_enter();
    while (!*(volatile equeue_sema_t *)s) {
        __WFI();
        core_util_critical_section_exit();
        core_util_critical_section_enter();
    }
    *s = false;
    core_util_critical_section_exit();

    return true;
}

#endif

#endif
