# Direct opaque bulk connections

Host distributes fixed-capacity, opaque byte-buffer endpoints using the
[direct POD connection lifecycle](direct_pod_connections.md). The same coordinator,
startup registration, ordinary Host controls, eligible establishment deadline,
notification pins and retirement barriers serve both transport kinds.

## Configuration and ownership

`connections::SBulkConfiguration` declares the usable bytes per buffer and the
producer's module role. The connection has one producer and one consumer, each
confined to its designated service thread. Either participant may initiate it.
Both participants' `SContractPolicy` entries must explicitly accept the capacity,
producer and contract version. Host interprets no payload schema or domain data.

There is one bulk connection and one independent POD connection per unordered
exact-instance pair. Reciprocal bulk requests share a token only when capacity,
producer and version match. Conflicting capacity or producer returns
`configuration_mismatch`; version conflicts return `version_mismatch`. Repeating
an active matching request returns `already_connected` and its existing token.
Capacity or direction changes require closing the old connection and establishing
a new incarnation. Replacement module instances inherit no old authority.

Host owns three buffers through its memory context. Each buffer begins at a
128-byte boundary. Its allocation stride rounds the requested capacity up to a
multiple of 128; endpoint views expose exactly the requested capacity. Zero sizes,
overflow and sizes whose combined padded backing exceeds Foundation's memory
ceiling are invalid. Allocation failure exposes no endpoint. There is no growth
or allocation during publication or acquisition.

Bulk and POD records share the existing limits: 32 connections overall and eight
per participant, including pending and closing records. Their control protocol
registration version is now 2; rebuild Host and participating modules together.
Existing source calls that omit bulk configuration continue to request POD pipes.

## Establishment and use

Register `connections::CClient` as the first startup POD, as for POD connections.
Add a bulk policy such as:

```cpp
const connections::SContractPolicy policy{
    connections::contract(module_ids::executive, module_ids::rendering, 1u),
    nullptr, 0u, { 4096u, module_ids::executive }
};
```

The producer calls `open_bulk(peer, version, async_slot, capacity, true)`; the
consumer can initiate with the same arguments and `false`. Each uses its peer's
module role. Read the correlated `Result` through `take_result()`. To find the
active bulk token, use `active(peer, true)`; `active(peer)` retains its POD meaning.

Host offers the exact configuration, prepares backing and complementary
descriptors, waits for both `Ready` controls and publishes `Activate`. The client
helper exposes no data before local activation. No buffer is initially published.

The producer obtains a writable `CMemoryView` from `producer_data(token)`, writes
a complete state and calls `publish_bulk(token)`. That writable view expires upon
publication. Obtain a fresh view for subsequent preparation. The consumer calls
`discard_and_acquire(token)` and obtains a read-only `CMemoryConstView` from
`consumer_data(token)`. Acquisition always relinquishes the previous consumer
view, even if no new publication exists. Finish every dependent use before calling
it again. Copied views neither retain a slot nor grant another producer/consumer.

This is triple-buffered latest-state exchange: a publication can replace an
unacquired staged publication. Producer writes never overwrite the consumer-held
slot. There is no FIFO, per-publication acknowledgement, automatic ordering with
POD traffic, variable used-length metadata or historical retention. Participants
may place their own length and interpretation metadata within the opaque capacity.

`publish_bulk()` reports `published`, `inactive` or `published_wake_failed`.
Notification failure occurs after publication and must never trigger retransmission
as if nothing escaped. The helper begins local closure in that case. Foundation's
underlying `TBulk::publish()` boolean describes acquisition of the previous staged
publication; it is not a publication-success result.

Publication precedes the pinned peer notification. `has_data()` includes staged
bulk data and POD messages. Follow the existing sample-epoch, clear-notifications,
check-durable-data, wait protocol; notification latches alone do not prove data
availability. Bulk publication never waits for consumer capacity.

## Closure

Local close immediately disables new local endpoint operations; the peer stops
when it consumes Host's closing control. All installation publications precede that
closing boundary. Finish dependent buffer accesses and notification use before
acknowledging `quiesce(token)`. Host retains the backing, descriptors, module
bindings and notification targets until both sides are quiescent.

Existing held views may be used only to finish accounted local work before that
acknowledgement. They provide no independent lifetime beyond endpoint quiescence.
Abandoned execution requires the existing successful native join and final-drain
proofs. Timeout and failed join never authorise freeing exposed backing.

The broader schema-aware bulk exchange and independently protected acquisition
records proposed in the shader reflection design remain separate future work.

## Validation

Validated on 15 November 2026:

- Windows Debug x64 ordinary suites (`-t1`) passed, including real Rendering and
  Executive DLL integration: `build/bulk-debug-x64-tests.log`.
- Windows Debug Win32 ordinary suites (`-t1`) passed with matching rebuilt DLLs:
  `build/bulk-debug-win32-tests-matched.log`.
- Policy checks passed with zero errors and warnings. Existing compiler warnings
  in unchanged restriction tests and vendor sources remain.
- Line-ending and diff whitespace checks passed; Visual Studio item/filter
  metadata retains its existing CRLF and encoding.

Bulk cases cover producer- and consumer-initiated establishment, reciprocal
requests, configuration mismatch, POD coexistence, exact usable capacity, all-slot
alignment, unpublished initial storage, latest-state replacement, consumer-held
buffer protection, concurrent exchange, Host backing attribution, no allocation
during exchange, invalid sizes/roles, allocation failure, activation barriers,
partial installation, timeout, notification failure and failed-join retention.
The held-buffer tests also cover the corrected `TBulk` rotation: publication after
acquisition returns the previously released slot to the producer rather than the
consumer's newly held slot.

### TBulk acknowledgement and validation regressions

Empty consumer polling preserves the initial never-published word, so the first
publication reports no previous acquisition. This also preserves a concurrent
first publication for a subsequent acquire. The fix adds no transport fields or
layout changes.

Quiescent validation uses the publication word to distinguish a newly acquired
staged slot from a held slot already acknowledged by the producer. It also checks
that a pending publication names the producer's staged slot.

`MorphicTests --bulk-transport` runs the focused regression suite, which also runs
with the ordinary suites. Its 83 checks cover repeated empty polling, first and
subsequent acknowledgements, validation before and after producer acknowledgement,
replacement, release, the opaque byte-buffer endpoints and all 1,024 publish/acquire
sequences of length ten against an independent latest-state model. The tests
reproduced nine failures before the fixes and pass on Debug x64 and Win32 afterward.

The ordinary Debug suites also pass with the fixes on both architectures, including
the concurrent bulk exchange and Host lifecycle cases. Final logs are
`build/tbulk-final-x64-tests.log` and `build/tbulk-final-Win32-tests.log`.
