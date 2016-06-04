/* 
 * System specific tick implementation
 */
#ifndef EVENTS_TICK_H
#define EVENTS_TICK_H

#include <time.h>


static inline unsigned events_gettick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)(ts.tv_sec*1000 + ts.tv_nsec/1000000);
}


#endif
