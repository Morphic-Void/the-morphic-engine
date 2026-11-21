# Direct POD endpoint connections

The [21 November endpoint increment](direct_endpoint_increment.md) replaces the
original designated-thread POD path. Host owns each connection, both growable
SPSC queues, descriptors, atomic admission and pinned notification targets.
Registered module pumps and service threads can participate within or across
modules. A pump's address combines its module ID and `thread_ids::host`.
Host workers and unregistered native threads are not participants. Owning messages
continue to travel only towards Host.

## Enablement and establishment

Construct one `connections::CClient` with the endpoint's `CEndpointContext`;
`CThreadContext` supplies the same interface for a service thread. After installing
receiving and consent handling, call `enable(correlation)`. The ordinary Host FIFO
returns `Registered` at that correlation before any resulting offers. Its status
is explicit. Enablement is independent of native launch, `EndpointReady`, and
module readiness; sealing or retirement cannot be undone by another declaration.

`open(first_system_id, second_system_id, correlation)` requests two participants.
`open(peer_system_id, correlation)` uses the caller as the first participant.
Host authenticates the requester separately, resolves both addresses once to exact
endpoint identities, and does not load or wait for missing participants. A
third-party requester receives results and has no close authority over the pair.

An unordered exact endpoint pair has at most one pending, active or closing POD
connection. Each establishment has a fresh, nonreused token. Reciprocal and other
concurrent requests merge into one handshake, with at most eight requester
correlations and one pending request per requester for that pair. Excess or
redundant requests return `busy`; an active pair returns `already_connected` and
its token, and a closing pair returns `closing`.

Both participants receive `Offer`, even if they requested the connection. Module
code calls `decide(token, status)` to accept or reject the exact offer. The helper
then validates `Install`, integrates the descriptor and submits `Ready`. After
both readiness acknowledgements and an admission recheck, Host activates the gate
and publishes `Activate`. A participant's successful result follows its own
activation in the Host FIFO. A result records establishment, not continuing
availability; it may arrive after the connection has retired.

Request correlations have independent Host and helper records. A third-party
requester's departure settles its undeliverable result without closing the pair.
Participant departure closes the connection. Transport retirement does not erase
an outstanding requester result or retain participant pointers on its behalf.

## Receiving and sending

`receive(SReceived&, direct_data = true)` returns at most one original POD record,
including its asynchronous slot, plus authenticated immediate-source metadata.
`SSource` distinguishes Host from a direct sender; direct sources include the
sender's system ID, exact endpoint identity and connection token. Dispatch checks
this source before interpreting the payload. A peer's Host-control or service-result
payload cannot enter generic Host handling.

The helper applies bounded mechanical handling only to records from the Host
queue, then exposes the original record. Offers, results, installation and lifecycle
controls remain visible to the module and count towards its receive budget. A Host
`Result` releases its pending local correlation before being returned; module code
handles that original record directly. There is no separate result-drain interface. There
is no hidden drain loop or inline module callback. `is_control(message)` is useful
only after checking that the authenticated source is Host.

Selection is persistent round-robin over Host and active direct inboxes, bounded
by one traversal per call. Each queue remains FIFO; there is no cross-queue order.
A caller may set `direct_data = false` while it cannot accept any direct data,
including replies. Host remains eligible, direct queues remain untouched, and the
cursor is retained for resumption. Recompute this flag each call, including within
a batch. `has_data(direct_data)` uses the same eligibility, so unread paused data
alone does not keep a thread or pump runnable. This is local receive selection,
not connection admission or a new pause protocol.

`post(peer_system_id, message)` uses the current active local mapping. Deferred
work uses `post(peer_system_id, message, saved_token)` or `post(saved_token, message)`.
An absent, establishing, closed or stale pipe returns `no_pipe`; sending never
creates a connection or retains a message for later. Admission requires a SYSTEM
message, with no module-role or direction policy for POD payload semantics.

`queued` means published, not processed. `full` is a safe refusal that permits
retaining and retrying the original message. `queued_wake_failed` means publication
succeeded but notification failed. `transport_failed` may follow publication and
must never cause automatic retransmission. Direct poisoning and unrecoverable wake
failure mark the endpoint failed and trigger existing Host shutdown handling. The
unaffected Host control queue remains usable for safe cleanup. Logged shutdown/termination
policy handles uncertainty; it never permits unsafe reclamation.

Publication and consumption signal the peer to support wake and capacity progress.
A thread samples its epoch, clears notification latches, checks eligible durable
work and pending controls, then waits against the sampled epoch. A pump clears
latches, consumes its bounded budget and returns work availability consistently
with direct-data eligibility. Notification callbacks never execute module code.

## Closing, admission and lifetime

`close(token)` immediately disables local use and queues a Host close request.
Host closes the shared admission gate and publishes `Closing` to both participants.
Before exposing that record, the helper disables receiving and obsolete decision,
installation and activation work. Delayed controls cannot reopen the token.

The admission word is a lock-free 32-bit atomic: the high bit closes admission and
the remaining bits count acquired operations. Acquisition uses an acquire-release
CAS on that same word, refusing closed or saturated state; Host closes it with an
acquire-release `fetch_or`. An RAII guard covers queue access and notification
signalling, including failure paths. Release decrements with release ordering;
Host observes the closed, zero-count word with acquire ordering. Admission opens
only once after the two-sided readiness check.

Module code cancels connection-dependent conversations and finishes users before
calling `quiesce(token)`. It need not consume unread PODs or finish unrelated Host
work. Helper calls are confined to the endpoint's serialized execution; deferred
work retains tokens and independent copies, not borrowed descriptor pointers.
Quiescence detaches transport and notification access and submits the acknowledgement.
Safe temporary Host publication pressure leaves it pending for `progress()`;
there is no blocking wait or dependence on peer acknowledgement.

Host retains exposed resources until both participants discharge access obligations
and all admitted operations, including signals, have finished. Zero operations alone
is not module quiescence. Abandonment requires the existing final-drain/execution
proof and successful native join for threads; timeout alone never reclaims storage.
Never-exposed sides need no descriptor acknowledgement. `Retired` clears remaining
helper bookkeeping before being returned. Normal close permits later explicit
establishment; `close_all()` is permanent helper shutdown, and sealing permanently
prohibits that exact endpoint. Host retains the
stronger existing ordering: seal acknowledgement follows direct retirement.

The limits remain 32 total connections and eight per participant, shared with
[opaque bulk](direct_bulk_connections.md), and a five-second eligible establishment
deadline. Bulk retains designated-service role/capacity policies and held-view
semantics. POD and bulk connections remain independent. The common protocol and
POD descriptor version are 1, with explicit descriptor-size and version checks;
Host and all consumers must be rebuilt together. These controls have no stored
wire-data artifacts to convert.

The history below describes the original implementation and its validation, not
the current API. Current validation evidence belongs to the endpoint increment.
## Historical implementation and validation, 5 November 2026

The generic coordinator and Framework participant are implemented, and Executive's
Rendering/Audio conversations now use them. Test payloads live in
`framework/communications/module_tests.hpp`. Host's special test routing, global
run gate and synthetic worker-exit result are removed together with the callers,
responders and fixtures. Platform shell fixtures use ordinary service commands.
Normal conversations consume the result before closing; explicit lifecycle
operations cancel and drain dependent work before endpoint retirement.

Deterministic coverage includes reciprocal and delayed requests, version mismatch,
busy rejection, partial installation, both-ready activation, exposed timeouts,
irreversible participant retirement admission, saturation recovery, notification
failure, and publication followed by allocation poisoning without retransmission.
Two controlled capacity-wakeup interleavings cover reads before and after clearing
the notification latch. A real Audio thread also fills its result queue and makes
progress when capacity is released without unrelated Host traffic. The native
failed-join test publishes terminal state while execution can still signal its
peer, proving that pins and transport storage survive until successful join.

Validation performed on 5 November 2026:

- Windows Debug x64 ordinary suites passed, including the final deterministic
  wakeup cases (`build/direct-final-dbg64-tests.log`).
- Windows Debug Win32 ordinary suites passed against the final implementation,
  including the same wakeup cases (`build/direct-final-dbg32-tests.log`). Both
  final Windows solution builds completed with zero warnings and zero errors.
- All 46 Windows Debug x64 DLL lifecycle cases passed. Logs are
  `build/direct-lifecycle-tests.log` (39 completed cases) and
  `build/direct-lifecycle-remaining-tests.log` (seven remaining cases after
  correcting the deliberate worker-abandonment expectation).
- All 43 Windows Debug x64 Host pump cases passed with two workers
  (`build/direct-pump-tests.log`). The saturation probe observes control-queue
  consumption while 512 ordinary disposal replies remain reserved. Its final
  strengthened assertion requires two replacement posts, excluding a queue peek
  as proof, and passed targeted Windows/two-worker and Linux/one- and two-worker
  runs (`build/direct-final-control-drain-windows.log`,
  `build/direct-final-control-drain-linux1.log`, and
  `build/direct-final-control-drain-linux2.log`).
- Linux GCC Debug passed all seven CTest entries: policy includes, ordinary suites,
  reflection arena failure, and Host pump/acceptance with one and two workers
  (`build/direct-migration-ctest.log`). After adding the deterministic wakeup cases,
  the ordinary suite passed again (`build/direct-final-linux-ordinary.log`).

Current documentation describes the direct contract. Historical design and
validation records retain their original evidence with a supersession note.
