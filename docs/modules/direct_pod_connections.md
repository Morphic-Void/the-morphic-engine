# Direct POD connections

Implementation work authorised 5 November 2026, starting from `5ff920e`.
This contract supersedes the fixed-ring proposal for general direct messages.
It does not change Platform-owned input streams.

## Contract

Host owns each connection and its two growable `CErasedPodMsgTransport` queues.
Their storage uses the Host memory context even when a service thread grows a
queue. Discard is disabled. The public endpoint API does not expose queue capacity
mechanics, so a future fixed transport can preserve the lifecycle contract.
Exactly one designated service thread produces and consumes on each side.

The logical key is the unordered exact-instance pair and canonical pair of
existing module roles (`module_ids`). Version is checked, not part of the key.
Every establishment receives a process-unique, nonreused token. Reciprocal
requests share the connection, retaining one initiating correlation per side;
further requests while pending receive busy. Active requests return the existing
token, and closing requests return closing. There is no implicit module load,
replacement substitution, or successor queue.

A service installs its Framework control handler and posts registration as its
first startup POD before publishing Running. Host consumes that first record
before reporting successful startup; legacy services need not register.
Registration establishes only control support. Each new offer requires explicit
acceptance of its exact peer, role contract and version. Host then publishes
endpoint descriptors, waits for both installation/readiness replies, and sends
activation. An installed side holds data until it observes activation.

Routine controls and busy replies use the existing regular Host pipes. Normal
control latency is allowed. Connection capacity must never stop unrelated Host
queue drains or cleanup. Restricted maintenance keeps its existing capture,
deferred replay and no-growth publication rules.

Direct messages retain `CErasedPodMsg`'s 64-byte record and 48-byte payload.
Framework admission requires SYSTEM identity and the agreed contract, version
and direction. POD pointers convey no ownership or independent resource access;
owning transfers continue through Host. Successful posting means queued, not
processed. Notification failure is separate from enqueue/read success. Queue
allocation poisoning is terminal and may follow publication; it must never cause
automatic retransmission as though the record had not escaped.

One pinned notification targets each participant. Publication precedes signalling;
consumer progress also wakes a sender waiting for capacity. Waiters sample the
thread epoch, clear the latch, drain/check durable data and retry retained output,
then wait using the original epoch. Every potentially coalesced hint is followed
by those durable checks. Lifecycle controls are checked between bounded batches.

Closing is durable in Host and local state. A local close request stops new local
work immediately; the peer stops when it observes Host's closing control. Every
installation publication, including retries, precedes that closing boundary.
Nothing can install or reactivate afterward. Participants cancel known dependent
operations and validate tokens before deferred continuations. Background work
retains tokens, never unprotected endpoint pointers. Graceful conversations finish
while active; retirement does not require processing unread data.

Beginning participant retirement also closes its Host-local admission, even when
it has no connection records yet. Late registration cannot reopen that instance.
A replacement starts with fresh unregistered state.

Quiescence acknowledges completed relinquishment of both pipe roles, notification
readers and outgoing signals after consuming the closing boundary. Host retains
all exposed descriptors, transport storage, participant bindings and notification
targets until both sides are quiescent. Never-exposed sides need no endpoint
acknowledgement. Abandonment instead requires successful native join plus final
queue/deferred-record/batch checks. Native join must remain reachable while direct
notification pins exist. Failed joins and timeouts never authorise reclamation.

Initial connection limits are 32 overall and eight per participant, including
pending and closing records. Establishment has a five-second eligible deadline;
declared Host restriction intervals are excluded. Data queues initially use the
existing growable Host policy, not a 64-record fixed limit. Ordinary capacity
pressure is not an emergency. Existing failure and exit mechanisms are reused.

New public controls, descriptors and helpers live in Framework; the coordinator
and owned storage live in Host. Core provides generic primitives and opaque
identity plumbing without depending on Framework.

## Implementation and validation

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
