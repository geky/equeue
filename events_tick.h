/* 
 * System specific tick implementation
 */
#ifndef EVENTS_TICK_H
#define EVENTS_TICK_H


// Monotonic tick
//
// Returns a tick that is incremented every millisecond,
// must intentionally overflow to 0 after 2^32-1
unsigned events_tick(void);


#endif
