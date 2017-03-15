## The design of the equeue library ##

The equeue library is designed to be a "Swiss Army knife" for scheduling
on embedded systems.

Targeting embedded systems comes with several interesting constraints:
 - Low RAM footprint
 - Low ROM footprint 
 - Power consumption
 - Interrupt contexts with jitter constraints

However, the primary design goal of the equeue library is to be a reliable
"Swiss Army knife", that is, provide a set of useful robust tools that come
with the fewest surprises to the user, to encourage fast application
development.

To reach this goal, the equeue library prioritizes simplicity and puts in
the extra effort to match user expectations when the behaviour may otherwise
be undefined.

## Scheduler design ##

The primary component of the equeue library is the scheduler itself. The
scheduler went through several iterations before arriving at the current
implementation. To start, here are several existing schedulers that were
considered.

#### Existing design - Sorted linked-list ####

```
  +-----------+  +-----------+  +-----------+  +-----------+  +-----------+
->| event t=1 |->| event t=1 |->| event t=4 |->| event t=5 |->| event t=5 |
  |           |  |           |  |           |  |           |  |           |
  +-----------+  +-----------+  +-----------+  +-----------+  +-----------+
```

Perhaps one of the simpliest schedulers, a sorted linked list is difficult
to beat in terms of simplicity. In fact, a sorted linked list started as the
initial design of the equeue library.

However, a sorted linked list has the largest cost for insertion. To maintain
insertion order (what the user expects), insertion must iterate over all
events in the same timeslice. For delayed events, this isn't that bad, but
if the queue is used to defer events from interrupt context, non-constant
jitter may be unacceptable. (Forshadowing, if only we had some way to skip
over events in the same timeslice).

#### Existing design - Unsorted linked-list ####

```
  +-----------+  +-----------+  +-----------+  +-----------+  +-----------+
->| event t=4 |->| event t=1 |->| event t=5 |->| event t=1 |->| event t=5 |
--|           |--|           |--|           |--|           |->|           |
  +-----------+  +-----------+  +-----------+  +-----------+  +-----------+
```

Very common in embedded systems when only used to defer from interrupt
contexts. Timing can be accomplished with a separate scheduler or hardware
registers. A pointer to the end of the list allows insertions (while
maintaining insertion order) in constant time.

For the equeue library, an unsorted linked list could be extended to support
timing by simply storing the time in each event, and iterating the entire
list to find the event that will expire the soonest. This does solve the
jitter problem of the sorted linked-list, but results in a large cost
for dispatch, since the entire list has to be iterated over.

#### Existing design - Heap ####

```
                 +-----------+
                >| event t=5 |
  +-----------+/ |           |
->| event t=1 |  +-----------+  +-----------+
  |           |                >| event t=5 |
  +-----------+\ +-----------+/ |           |
                >| event t=1 |  +-----------+
                 |           |
                 +-----------+\ +-----------+
                               >| event t=4 |
                                |           |
                                +-----------+
```

A very useful data structure for ordering elements, a heap is a tree
where each parent is set to expire sooner than any of its children.
Consuming the next event requires iterating through the height of the
tree to maintain this property. A heap provides O(log n) insertion and
dispatch, which beats most other data structures in terms of algorithmic
complexity.

For scheduling on embedded systems, a heap has a few shortcomings. The
O(log n) insertion cost is difficult to short-circuit for events without
delays, putting the heap after the unsorted linked-list in terms of
jitter. Additionally, a heap is inherently unstable, that is, dispatch
does not mainain insertion order. These shortcomings led to the equeue
library pursuing a simpler data structure.

#### Existing design - Timing wheel ####

```
+-+  +-+  +-+                                +-+  +-----------+
|_|  |_|->|_|                                |_|->| event t=4 |
|_|  |_|  |_|  +-----------+  +-----------+  |_|  |           |
|_|  |_|  |_|->| event t=5 |->| event t=5 |  |_|  +-----------+
|_|  |_|  |_|  |           |  |           |  |_|
|_|->|_|  |_|  +-----------+  +-----------+  |_|  +-----------+  +-----------+
|_|  |_|  |_|                                |_|->| event t=1 |->| event t=1 |
|_|  |_|  |_|------------------------------->|_|  |           |  |           |
| |  | |  | |                                | |  +-----------+  +-----------+
+-+  +-+  +-+                                +-+
```

Perhaps the most efficient scheduler in terms of runtime, a timing
wheel is a sort of hash-table that uses the relative expiration time
as an offset into an array of linked lists. A hierarchical timing
wheel expands on this by maintaining layers of events at different
granularities that "cascade" into the lower layers when a wheel
has been exhausted. A hierarchical timing wheel is a very graceful
data structure that naturally groups delays on their relative order
of magnitude.

Unfortunately, a timing wheel is a poor fit for embedded systems.
The arrays that back the timing wheel can take a large amount of RAM,
and the cascade operation can unexpected spikes in runtime for dispatch
operations.

#### The equeue scheduler ####

The current scheduler prioritizes a small RAM footprint and a constant
jitter for events without delays. The core data structure is the sorted
linked-list, but with a nested linked list for all events in a single
timeslice. An event can be inserted in a timeslice in constant-time
in a similar manner to the unsorted linked-list.

```
  +-----------+  +-----------+  +-----------+
->| event t=1 |->| event t=4 |->| event t=5 |
  |           |  |           |  |           |
  +-----------+  +-----------+  +-----------+
    v                             v
  +-----------+                 +-----------+
  | event t=1 |                 | event t=5 |
  |           |                 |           |
  +-----------+                 +-----------+
```

This may seem like a small improvement, but this garuntees a constant-time
insertion if no delays are used. This means that a system using the equeue
library to simply defer events from interrupt context could find the number
of instructions needed to insert an event, and put a hard upper-bound on the
jitter the event queue introduces into the system.

A few other small improvements were added to the base design:

- Each event contains a back-reference to any pointers in the data structure.
  This allows any event to be cancelled and removed from the data structure
  in constant-time, as opposed to iterating over the data structure to find
  which nested list the event lives in. If you notice in the above diagram,
  there is only ever a single pointer referencing an event, so only one
  back-reference is needed.

- Rather than store a pointer to the end of the list for each event, events
  are just pushed onto the head of the list. This means each timeslice is
  actually stored in reverse order of insertion, but the original order can
  be constructed by reversing the list during dispatch. Additionally, this
  reversal can be performed outside of any critical section, and only a
  constant-time operation is needed to get the current timeslice out of the
  event queue.

#### Other considerations ####

There were a few other considerations for the scheduler. Many features
were omitted to best match the goal of stability, though it would be very
interesting to see event queue designs that build on the core library with
different features.

- Lock-less data structures - Being primarily pointer based, it may be
  possible to implement the event queue using only atomic operations. While
  the potential improvement in contention is very appealing, lock-less
  algorithms are notoriously difficult to get right. The equeue library
  avoided lock-less data structures, prioritizing stability.

- Tolerance-aware scheduling - In the context of embedded systems there has
  been some interesting work in schedulers that rearrange events to try to
  best meet the deadlines of events with different tolerances. However, this
  feature did not mesh well with the prioritization of stability and user
  experience.  The equeue library does coalesce events in the same timeslice
  (1ms by default), but otherwise maintains insertion order.

## Allocator design ##

The secondary component of the equeue library is the memory allocator. The
initial uses of the event queue quickly identified the tricky problem of
handling memory in interrupt contexts. Most embedded systems do not provide
irq-safe synchronization primitives short of the jitter-inducing mechanism
of disabling interrupts. For this reason, most system-wide heaps are
off-limits in interrupt context. Other embedded systems may simply not
provide a system-wide heap at all.

With the goal of providing a "Swiss Army knife" for scheduling, the equeue
library includes a built-in memory allocator to prevent the user from needing
to roll their own. Fitting in with the rest of the event queue, the allocator
is designed to be irq-safe, with ability to provide constant jitter. The
allocator can also manage variable sized events, giving the user more
flexibility with the context they associate with an event.

The most difficult constraint for the memory allocator is the constant
runtime requirement. This leaves us with only a few allocators to work with.

#### Existing design - Never free allocator ####

```
+-----------------------------------------+
| used memory |             unused memory |
|             |->                         |
|             |                           |
+-----------------------------------------+
```

Perhaps the simplest allocator, a never free heap is just a pointer into
a slab of memory that indicates what has already been allocated. Allocation
is just a pointer update and trivially constant-time. This allocator does have
one glaring flaw though, you can not free memory, making it nearly useless for
an embedded system. Attempts to reclaim memory by wrapping at the slab
boundary adds work which removes the constant runtime of the allocator.

#### Existing design - Fixed sized allocator ####

```
  +-----------+  +-----------+  +-----------+  +-----------+  +-----------+
->|           |->|           |->|           |->|           |->|           |
  |           |  |           |  |           |  |           |  |           |
  +-----------+  +-----------+  +-----------+  +-----------+  +-----------+
```

One of the most useful allocators is the fixed sized allocator. If we assume
fixed-sized allocations, the chunks can be strung together in a simple
linked-list that resides in unused memory. Allocation and freeing is a single
pointer update and easily constant-time. Fixed-size allocators are a
fundamental building-block for designing higher-level memory allocators. The
only downside is in its namesake, the user must decide the size of the
memory allocations beforehand.


#### The equeue allocator ####

To accomplish the goal of an irq-safe allocator with variable sized events,
the equeue allocator takes a hybrid approach. The primary allocator for the
event queue is a set of fixed-size chunk allocators. These chunk allocators
are fed by a slab of memory driven by a never-free allocator. The resulting
allocator can allocate in constant-time, and provides variable sized events.

```
         +-----------+  +-----------+  +-----------+
chunks ->|           |->|           |->|           |
         +-----------+  |           |  |           |
           v            +-----------+  |           |
         +-----------+    v            |           |
         |           |  +-----------+  |           |
         +-----------+  |           |  +-----------+
           v            |           |
         +-----------+  +-----------+
         |           |    v
         +-----------+  +-----------+
                        |           |
                        |           |
                        +-----------+

         +-----------------------------------------+
slab   ->| used memory |             unused memory |
         |             |->                         |
         |             |                           |
         +-----------------------------------------+
```

An attentive reader may note that the above allocator does, in fact,
_not_ allocate in constant time. This is true, but the dependent variable
is not the quantity of events, but the quantity of _sizes_ of events.
This means that if the number of differently sized events is kept finite,
the resulting runtime will also be finite. The best example is when there
is only one event size, in which case the above allocator will devolve into
a simple fixed-size allocator. This property makes this allocator unreasonable
as a general purpose memory allocator, but useful for a scheduler, where most
of the events are similar sizes, just unknown to the user.

#### Other considerations ####

There are a few other things to consider related to the memory allocator.

- Fragmentation - A benefit of the equeue allocator is that there is zero
  internal fragmentation. Once a set of events is allocated, the events
  will never coalesce. This is valuable for an embedded system, where
  devices should run for years without accruing issues. However, it is
  up to the user to avoid external fragmentation. Once chunked, memory is
  not returned to the slab allocator, so if there is an issue with external
  fragmentation, it should be quickly noticable, but this means that memory
  can not be shared between events of different sizes.

- Memory regions - The equeue library provides the rather useful operation of
  queue chaining. If a user needs more control over the memory backing events,
  the user can create multiple event queues with different memory regions, and
  chain them to a single dispatch context.

- Measuring memory - Because of the nature of the allocator, the measurements
  that can be reported on memory usage are a bit limited. The most useful
  measurement is stored in the equeue struct. The `equeue.slab.size` variable
  contains the size of the slab that has never been touched. The internals
  of the event queue is susceptible to change, but it can be useful for
  evaluating memory consumption.
