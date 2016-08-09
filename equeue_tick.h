/* 
 * System specific tick implementation
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#ifndef EQUEUE_TICK_H
#define EQUEUE_TICK_H

#ifdef __cplusplus
extern "C" {
#endif


// Platform millisecond counter
//
// Return a tick that represents the number of milliseconds that have passed
// since an arbitrary point in time. The granularity does not need to be at
// the millisecond level, however the accuracy of the equeue library is
// limited by the accuracy of this tick.
//
// Must intentionally overflow to 0 after 2^32-1
unsigned equeue_tick(void);


#ifdef __cplusplus
}
#endif

#endif
