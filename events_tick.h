/* 
 * System specific tick implementation
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#ifndef EVENTS_TICK_H
#define EVENTS_TICK_H

#ifdef __cplusplus
extern "C" {
#endif


// Monotonic tick
//
// Returns a tick that is incremented every millisecond,
// must intentionally overflow to 0 after 2^32-1
unsigned events_tick(void);


#ifdef __cplusplus
}
#endif

#endif
