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
    // creates a queue with 32 events with default size
    struct equeue queue;
    equeue_create(&queue, 32, 0);

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


