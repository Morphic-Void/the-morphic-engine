Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   TMpmcTransport.md
Primary draft: OpenAI tools
Reviewed and accepted by: Ritchie Brannan
Date:   26 Jul 2026

# TMpmcTransport

## Overview

`TMpmcTransport.hpp` defines a bounded, non-blocking MPMC transport family with
compile-time capacity and persistent inline storage.

The family performs no allocation during construction or live operation and
does not carry internal memory attribution. If allocator-owned placement or
reattribution is required, ownership of the complete transport object may be
provided externally through `TInstance<...>`. A live shared transport remains
practically immovable once references or endpoints exist.

## Family

The header contains:

- `TMpmcIndexRing<t_capacity_hint>`, the status-free raw index ring;
- `TMpmcArenaTransport<T, t_capacity_hint>`, the lifecycle-aware arena
  transport;
- `TMpmcJobTransport<...>`, a simple work/feedback composition;
- `TReservedArenaSlot` and `TAcquiredArenaSlot`, scoped protocol helpers.

The raw ring is a composition primitive. The arena transport is the first
complete transport surface.

## Capacity

The capacity hint is rounded upward to a power of two.

- Minimum effective capacity: `16`.
- Maximum accepted hint and effective capacity: `1,048,576`.
- A hint above the maximum is a compile-time error.

The arena element count and both internal index-ring capacities are identical.

## Raw Index Ring

Each raw-ring slot stores an atomic sequence and a `std::uint32_t` payload.
The ring supplies non-blocking:

- `push(payload, out_sequence)`;
- `pop(out_payload, out_sequence)`.

Failure means that the immediate operation cannot be admitted. The ring has no
lifecycle status.

Construction selects one of two initial states:

- logically empty;
- logically full with payloads `0..capacity-1`.

For a successfully pushed item, the sequence returned by `push()` matches the
sequence returned when that same item is later removed by `pop()`.

## Arena Protocol

The arena transport composes:

- an initially full supplier/recycler index ring;
- an initially empty populated-work index ring;
- a persistent array of `T`.

The producer protocol is:

```text
reserve -> populate -> publish
```

The consumer protocol is:

```text
acquire -> process -> recycle
```

`reserve()` and `acquire()` return pointers into the arena. Arena elements
remain live for the lifetime of the transport, so `T` must be nothrow default
constructible and nothrow destructible.

## Lifecycle

The public states are:

- `open`;
- `closing`;
- `closed`;
- `shutdown`.

Valid transitions are:

```text
open -> closing -> closed
open -> shutdown
closing -> shutdown
```

`closed` and `shutdown` are terminal. `begin_closing()` moves an open transport
to `closing`; if no indices are outstanding it completes the transition to
`closed` immediately. `shutdown()` is the forced-stop operation.

Operation admission is:

| State | Reserve | Publish | Acquire | Recycle |
|---|---:|---:|---:|---:|
| `open` | yes | yes | yes | yes |
| `closing` | no | yes | yes | yes |
| `closed` | no | no | no | no |
| `shutdown` | no | no | no | no |

Normal full or empty rejection is not a lifecycle failure. The family has no
separate `failed` state.

## Outstanding Count

The outstanding count includes every arena index that has left the supplier
ring and has not yet returned to it:

- reserved but not published;
- published but not acquired;
- acquired but not recycled.

A successful `reserve()` increments the count. A successful `recycle()`
decrements it. During orderly closing, only recycling the final outstanding
index may complete `closing -> closed`.

This prevents a temporarily empty populated ring from being mistaken for a
fully drained transport while a producer or consumer still owns an index.

## Irrevocable Pairing

Successful admission is an irrevocable protocol commitment:

- successful `reserve()` must be paired with `publish()`;
- successful `acquire()` must be paired with `recycle()`.

There is no speculative acquisition, cancellation, or rollback path.
Completion timing is unconstrained while the transport remains open or closes
orderly.

Forced `shutdown` is the deliberate exception. It rejects all operations and
may abandon outstanding protocol commitments. It is an immediate-stop state,
not an orderly drain.

## Scoped Helpers

`TReservedArenaSlot` attempts to publish a live reservation when destroyed.
`TAcquiredArenaSlot` attempts to recycle a live acquisition when destroyed.
Both helpers may also complete their operation explicitly.

The helpers are completion guards, not rollback objects. Their obligations
remain subject to the forced-shutdown exception.

## Layout And Cache Isolation

Critical regions use a 128-byte isolation boundary. The following begin in
separate aligned regions:

- raw-ring slot-array storage;
- producer/enqueue position;
- consumer/dequeue position;
- arena lifecycle status;
- arena outstanding count;
- arena element storage.

The slot-array requirement applies to the start of the array rather than every
individual slot.

## Higher-Level Composition

`TMpmcJobTransport` contains two independently configured arena transports:

- `work`, for supplied work;
- `feedback`, for completion or status return.

It adds no queue mechanics or coordinated lifecycle beyond composition.
The [module batch runner client](../../modules/batch_runners.md) uses MPMC arena
transports for its work and return channels. Add further consumer-specific
facades only when concrete access patterns justify them.

## Validation

The focused suite covers:

- capacity conditioning and boundaries;
- initially empty and initially full raw rings;
- sequence identity and repeated wrap;
- reserve/publish/acquire/recycle flow;
- orderly drain and immediate idle close;
- forced shutdown rejection;
- scoped completion helpers;
- paired transport composition.

The completed family passes Debug x64 and x86 builds and full test runs.
Additional cross-platform stress or sanitizer coverage may accompany a future
Linux build path without reopening this completed transport milestone.
