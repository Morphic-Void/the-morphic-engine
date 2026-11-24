# Direct opaque bulk connections

Current interface, 23 November 2026. Bulk uses the Host-owned
[common preamble](host_pump_foundation.md) and the private
[direct connection lifecycle](direct_pod_connections.md). The former module-side
client and ordinary handshake messages have been removed.

## Configuration and establishment

The module-owned execution object uses its bound `CCommunication` facade. Submit
participation, then a `connect` intent naming both system addresses, the contract
version and `SBulkConfiguration` (usable bytes and producer module role). Either
participant may initiate. Each receives `connection_offered` and explicitly
accepts or rejects the exact token/configuration through a consent intent. Host
interprets no byte schema or domain policy.

POD and bulk are independent connections for an exact endpoint pair. Reciprocal
bulk requests share a token only when capacity, producer and version match.
Configuration/version conflicts are refused; a matching active request reports
`already_connected`. Capacity/direction changes require a new connection. Existing
limits are 32 records overall and eight per participant, including pending and
closing records. Replacement endpoints inherit no old authority.

Host owns three buffers, each beginning at a 128-byte boundary. The padded stride
does not change the usable capacity. Invalid/overflowing sizes or allocation
failure expose no endpoint. There is no growth or allocation during exchange.
Private installation and activation run in the preambles. Availability precedes
local data access; requester success waits for both activation acknowledgements.
The establishment deadline continues through that proof. No buffer is initially
published.

## Exchange and closure

The producer obtains `producer_data(token)`, writes a complete state, then calls
`publish_bulk(token)`. Its writable view expires on publication. The consumer
calls `discard_and_acquire(token)` before `consumer_data(token)`; acquisition
relinquishes the previous view even when no newer publication exists. Finish all
dependent uses first. Copied views neither retain a slot nor create another user.

This remains triple-buffered latest-state exchange: an unacquired publication may
be replaced, but producer writes cannot overwrite the consumer-held slot. There
is no FIFO, historical retention, implicit ordering with POD traffic or automatic
used-length metadata. Publication returns `published`, `inactive` or
`published_wake_failed`; the last outcome is accepted and must not be replayed.
The underlying `TBulk::publish()` Boolean describes acquisition of the previous
staged publication, not publication success.

Host owns wake/dispatch mechanics. Modules service eligible bulk work through
their handler and report runnable work or a future wake; they do not read private
transport controls or implement a wait loop. Publication never waits for consumer
capacity. All facade operations require the bound execution context.

Close disables new local operations. On `connection_closing`, finish/cancel all
domain accesses and submit `connection_quiescent` for the exact token. Held views
have no lifetime beyond that acknowledgement. Host retains descriptors, backing
and both notification pins until both sides are quiescent and admitted operations
are drained. Native fallback additionally needs the existing return/join/final
ingress proof. A timeout or failed join never authorises reclamation.

The [shader-system extension](shader_reflection_design.md#establishment-usages-and-replacement)
remains separate: capacity/role consent is not schema acknowledgement, and held
views are not independent leases.

## Historical validation

The records below describe their original binaries and tests. Current migration
and rebuilt-consumer evidence belongs to the
[implementation checkpoints](module_preamble_implementation.md).

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
