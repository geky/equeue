/*
 * equeue utilities and config
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 *
 * Can be overridden by users with their own configuration by defining
 * EQUEUE_UTIL as a header file (-DEQUEUE_UTIL=my_equeue_util.h)
 *
 * If EQUEUE_UTIL is defined, none of the default definitions will be
 * emitted and must be provided by the user's header file. To start, I would
 * suggest copying equeue_util.h and modifying as needed.
 */
#ifndef EQUEUE_UTIL_H
#define EQUEUE_UTIL_H

#ifdef EQUEUE_UTIL
#define EQUEUE_STRINGIZE(x) EQUEUE_STRINGIZE2(x)
#define EQUEUE_STRINGIZE2(x) #x
#include EQUEUE_STRINGIZE(EQUEUE_UTIL)
#else

// Standard includes, mostly needed for type definitions
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#ifndef EQUEUE_NO_MALLOC
#include <stdlib.h>
#endif
#ifndef EQUEUE_NO_ASSERT
#include <assert.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


// Possible error codes, these are negative to allow valid positive
// return values. May be redefined to system error codes as long as
// they are negative.
enum equeue_error {
    EQUEUE_ERR_OK       = 0,    // No error
    EQUEUE_ERR_NOENT    = -2,   // No such event
    EQUEUE_ERR_NOMEM    = -12,  // Out of memory
    EQUEUE_ERR_INVAL    = -22,  // Invalid parameter
    EQUEUE_ERR_TIMEDOUT = -110, // Timed out
    EQUEUE_ERR_BREAK    = -125, // Operation canceled
};


// Runtime assertions
#ifndef EQUEUE_NO_ASSERT
#define EQUEUE_ASSERT(test) assert(test)
#else
#define EQUEUE_ASSERT(test)
#endif


// Optional memory allocation
static inline void *equeue_malloc(size_t size) {
#ifndef EQUEUE_NO_MALLOC
    return malloc(size);
#else
    (void)size;
    return NULL;
#endif
}

static inline void equeue_free(void *p) {
#ifndef EQUEUE_NO_MALLOC
    free(p);
#else
    (void)p;
#endif
}


// Builtin functions, these may be replaced by more efficient
// toolchain-specific implementations. EQUEUE_NO_INTRINSICS falls back to a more
// expensive basic C implementation for debugging purposes

// Find the sequence comparison of a and b, this is the distance
// between a and b ignoring overflow
static inline int32_t equeue_scmp(uint32_t a, uint32_t b) {
    return (int32_t)(uint32_t)(a - b);
}

// Find the next smallest power of 2 less than or equal to a
static inline uint32_t equeue_npw2(uint32_t a) {
#if !defined(EQUEUE_NO_INTRINSICS) && (defined(__GNUC__) || defined(__CC_ARM))
    return 32 - __builtin_clz(a-1);
#else
    uint32_t r = 0;
    uint32_t s;
    a -= 1;
    s = (a > 0xffff) << 4; a >>= s; r |= s;
    s = (a > 0xff  ) << 3; a >>= s; r |= s;
    s = (a > 0xf   ) << 2; a >>= s; r |= s;
    s = (a > 0x3   ) << 1; a >>= s; r |= s;
    return (r | (a >> 1)) + 1;
#endif
}


#ifdef __cplusplus
}
#endif

#endif
#endif
