## The equeue library ##

The equeue library provides a composable event queue implementation
that acts as a drop in scheduler and event framework.

``` c
#include "equeue.h"
#include <stdio.h>

void print(void *s) {
    puts((const char *)s);
}

int main() {
    // creates a queue with space for 32 basic events
    equeue_t queue;
    equeue_create(&queue, 32*EQUEUE_EVENT_SIZE);

    // events are simple callbacks
    equeue_call(&queue, print, "called immediately");
    equeue_call_in(&queue, 2000, print, "called in 2 seconds");
    equeue_call_every(&queue, 1000, print, "called every 1 seconds");

    // events are executed when dispatch is called
    equeue_dispatch(&queue, 3000);

    print("called after 3 seconds");

    // dispatch can be called in an infinite loop
    equeue_dispatch(&queue, -1);
}
```

The equeue library can be used for a normal event loops, however it also
supports composition and multithreaded environments. More information on
the idea behind composable event loops 
[here](https://gist.github.com/geky/4969d940f1bd5596bdc10e79093e2553).

## Tests ##

The equeue library uses a set of local tests based on the posix implementation.

Runtime tests are located in [tests.c](tests/tests.c):

``` bash
make test
```

Profiling tests based on rdtsc are located in [prof.c](tests/prof.c):

``` bash
make prof
```

To make profiling results more tangible, the profiler also supports percentage
comparison with previous runs:
``` bash
make prof | tee results.txt
cat results.txt | make prof
```

## Porting ##

The events library requires a small porting layer:
- [equeue_tick](equeue_tick.h) - monotonic counter
- [equeue_mutex](equeue_mutex.h) - non-recursive mutex
- [equeue_sema](equeue_sema.h) - binary semaphore
