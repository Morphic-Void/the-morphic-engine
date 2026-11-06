# Portable Platform input connections

Implementation authorised 6 November 2026 after review of the readiness,
ordering and recovery contract. This reference supersedes prospective input
mechanics in `platform_contracts_v1.md`; native acquisition and production
configuration installation remain later increments.

## Ownership and admission

Platform owns one exclusive desktop connection and one independent controller
connection. Desktop groups text and action/control streams on one designated
reader and protects its window through retirement. Controller ownership is
independent of windows. Host authenticates exact client/module instances and
protects generic borrows; it does not relay input records.

The reader is the consumer's registered execution context (service thread or
Host-pumped execution). The current notification API does not register arbitrary
secondary threads. Both connections held by the same client use that execution.
There is no implicit queue, eviction, replacement inheritance or ownership transfer.

Stable, versioned descriptors expose consumer-only Core endpoints, capacities,
identities, a coherent control publication and a non-owning Host capacity wake.
Platform retains them through generic borrow retirement. Data uses typed bounded
rings: 256 action/control records per connection and 64 text records for desktop.
Action records are 48 bytes and text records 40 bytes, aligned to eight bytes.
Consumers cannot reset, resize or destroy the rings.

Configuration, profile and context references require an installed matching
record. Synthetic tests install private fixture records; production has no
zero-token or unconfigured-input bypass. Mapping and schema loading are deferred.

## Readiness and authority

Reserve -> prepare borrow -> install readers -> register notification ->
InputReady -> initial markers -> BoundaryReady -> Active/acquisition completion.

InputReady correlates connection, notification registration and acquisition.
Platform binds the registration to the exact borrow before publishing markers.
Desktop requires both initial markers even when text is disabled. Consumers
must drain and acknowledge boundaries while acquisition is pending.

Application authority is coherent Active state, cleared affected barriers and
current generations observed and acknowledged by this reader. Local cancellation,
closing and eligibility gates override it. Terminal acquisition success reports
completion; delivery of that reply is not a fence for the independent data paths.
Readiness has one five-second eligible awake-time budget, including the initial
boundary exchange. Timeout initiates cleanup but never proves quiescence.

## Publication, ordering and recovery

Desktop shares one sequence and committed watermark across both streams;
controller has its own. Timestamps use the endpoint's declared clock domain.
The watermark certifies no earlier allocated sequence remains unresolved.
Allocate marker sequences only when they can post, and never advance past a
blocked reservation. Published records are immutable; gaps are not estimates of
lost physical inputs.

Readers sample coherent state, refill both missing heads, and merge eligible
records by sequence up to that watermark. A previously empty observation cannot
justify ignoring the other stream. Reads and application are revalidated against
the stream generation and authority. An already copied marker survives snapshot
contention until it can be classified; discarding it blindly could lose the only
current boundary. Ordinary records carry no generation: after invalidation they
are discarded until the matching marker is consumed.

Every barrier uses the same handshake: stop affected ordinary publication,
advance affected generations, publish durable reasons, notify, drain obsolete
records, publish matching markers, acknowledge the current generation vector,
then reopen eligible publication. The provider derives the pending mask; stale
acknowledgements cannot reopen it. Repeated retries do not advance generations.
Independent recoveries combine masks while retaining still-current markers.

Context changes affect both desktop streams and disable text until explicitly
enabled for the intended transition/target. Text-only recovery leaves actions
unaffected. Recovery cancels affected logical state without normal-release
semantics; held actions require a neutral observation before reactivation.
Lost pulses are not reconstructed. Loss counters and reasons remain outside
full queues. No overwrite, producer-side reader-index reset or live ring clear
implements recovery. Closure supersedes every barrier and requires no marker.

Context changes are serialised: a second context request, or text-target change
during the joint context exchange, returns `busy`. Target changes also serialize
their own boundary exchange. Context selection may supersede overflow recovery;
independent stream recoveries may overlap. Selecting the already active context
still creates a fresh boundary, allowing recovery after a rejected request without
silently restoring cancelled state. The initial transition is the acquisition
request ID; each context selection uses its request ID as the new transition.

The producer's `observe_subject` baseline must remain current during recovery.
Neutral observations can arm the next generation, but a subsequent held observation
removes that permission during a barrier. Publication does not infer physical
neutrality from record payloads. Native mapping must keep observing physical state
while publication is gated. This layer never reconstructs a pulse
or generates an ordinary release to represent cancellation.

Control publications use bounded sequence-validated reads of atomic 32-bit
words, including split 64-bit values, with sequentially consistent operations.
No non-atomic seqlock payload is read. Counter exhaustion closes the connection
instead of wrapping identities or allowing ABA.

## Framework reader integration

`CInputReader` is non-owning and is used alongside `CServiceBorrowClient`:

1. Begin generic acquisition tracking before posting the command. Observe replies
   in transport order and lifecycle controls on every iteration, including while
   waiting for preparation, registration or terminal completion.
2. Obtain the still-live generic view, check its size/version, then install the
   input descriptor for the exact consumer instance. Request and observe the
   generic notification registration before submitting `SInputReady`.
3. Clear/recheck the registered latch using the execution's normal wait protocol.
   Call `next` while acquisition is pending. It processes at most 64 merge steps;
   `progress` means more bounded draining is needed. `retry` requests a coherent
   snapshot retry. `empty` can mean waiting for publication or an acknowledgement.
4. Consume `take_cancellation()` before applying further affected logical state.
   Matching markers return `boundary`. Once `boundary_ready()` succeeds, submit
   that vector and call `submitted()` only after transport admission succeeds.
   The same vector is not offered repeatedly while outstanding. A correlated
   `busy` reply uses `retry_boundary()`; a stale vector is superseded by the next
   coherent generation. Other failures close the reader and initiate cleanup.
5. Records include connection/stream/generation stamps. `validate()` rechecks
   authority immediately before deferred application. Keep application ordering
   while validation retries; discard stale records. Before requesting a context
   or target change, `cancel_application()` suppresses affected application.
   Retry a contended cancellation snapshot before submitting the command. After
   confirming the intended request succeeded (including its transition/target),
   acknowledge its newer boundary and explicitly call `resume_application()` with
   that vector. Rejected requests and unrelated overflow recovery never reopen
   local cancellation. Reselecting the same context or text target creates a fresh
   boundary for this purpose.
6. Close the reader and end all notification/latch use before generic quiescence.
   Late prepared or registration replies stay under `CServiceBorrowClient`'s
   existing closing rules. No ring drain is required for retirement.

The Host-pump context is version 2 and includes the non-owning `owner_wake` used
for capacity returns. The pump capability remains `SHostPumpV2`; its callbacks
are unchanged. The complete context size/version is checked and Host/modules
require coordinated rebuilds. No platform-native type enters the input contract.

## Wakes and retirement

One consumer notification covers both desktop streams and durable controls.
Consumers retain the ordinary epoch/latch/drain/recheck/wait discipline and keep
servicing lifecycle messages. Reads and obsolete-record discards signal the
coalesced Host capacity wake, allowing prompt marker retries. Waiting for capacity
or acknowledgement is blocked work, not an immediately runnable pump indication.
Signal failure is separate from publication success and never permits replay.

Release uses ReleaseServiceBorrow and ServiceBorrowQuiescent. Local cancellation
immediately prevents application. The consumer ends every endpoint/latch access
and outgoing signal before quiescence. Platform stops publication and final
signals, then retires storage at an outer-pump safe point and finishes the borrow.
Retain pending acquisition/completion tracking until its existing obligations end.

Pure CPU input borrows may retire after proven execution_ended quiescence;
window/graphics borrows retain their separate cleanup requirements. Desktop's
window dependency lasts until input retirement, including escaped pending views.
Native/callback scopes never reclaim input storage. Failed joins, deadline expiry
and a missing acknowledgement do not authorise unsafe reclamation.

## Validation

Synthetic coverage exercises readiness before terminal success, disabled-text
markers, shared ordering, bounded reader retries, generation changes around reads,
full rings, combined/superseded recovery, neutral reactivation, capacity wakes,
stale/foreign controls, cancellation, replacement, timeout and retirement. The
fixture uses real routed services, borrows and pumped notifications; a separate
concurrent stream stress case exercises 20,000 publication attempts with bounded
merge/recovery. No native input or hardware acceptance is claimed.

Validation on 6 November 2026:

| Target | Evidence |
| --- | --- |
| Windows Debug x64 | Full solution build and ordinary `-t1` suite passed, including the new input cases. Host-pump acceptance passed all 45 cases with two workers. |
| Windows Debug Win32 | Full solution build and focused `--platform-input` suite passed: 1,780 checks. Host-pump acceptance passed all 45 cases with two workers. Corrected stale V1 pump linker aliases to V2 in Platform and pump fixtures. |
| Linux, existing `build/linux-identity` | Reconfigured/rebuilt successfully; all seven CTest targets passed, including ordinary tests, one/two-worker Host pump and Host acceptance. |
| Repository checks | Policy validator: zero errors/warnings (one existing negative-test suppression). Tracked/new-file line endings and diff whitespace passed. |

Win32 build warnings were confined to unchanged vendor and HostRestriction test
code. Android/macOS native integration and hardware input remain unvalidated.
