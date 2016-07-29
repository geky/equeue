## Events ##

The events library provides a flexible event queue implementation
that acts as a drop in scheduler and framework for composable event
loops.

``` c
#include "events.h"
#include <stdio.h>

void print(void *s) {
    puts((const char *)s);
}

int main() {
    // creates a queue with space for 32 basic events
    equeue_t queue;
    equeue_create(&queue, 32*EVENTS_EVENT_SIZE);

    // events are simple callbacks
    event_call(&queue, print, "called immediately");
    event_call_in(&queue, print, "called in 2 seconds", 2000);
    event_call_every(&queue, print, "called every 1 seconds", 1000);

    // events are executed when dispatch is called
    equeue_dispatch(&queue, 3000);

    print("called after 3 seconds");

    // dispatch can be called in an infinite loop
    equeue_dispatch(&queue, -1);
}
```

The events library can be used for normal event loops, however it also
supports multithreaded environments. More information on the idea
behind composable event loops 
[here](https://gist.github.com/geky/4969d940f1bd5596bdc10e79093e2553).

## Tests ##

The events library uses a set of local tests based on the posix implementation.

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
- [events_tick](events_tick.h) - monotonic counter
- [events_mutex](events_mutex.h) - non-recursive mutex
- [events_sema](events_sema.h) - binary semaphore
