# Direct POD endpoint connections

Host owns connection storage, fixed SPSC rings, descriptors, admission and pinned
notification targets. Module code uses the bound `CCommunication` facade and
single execution handler described by the [common preamble](host_pump_foundation.md).
Pumped and serviced endpoints share this contract. A pumped address combines its
module ID with `thread_ids::host`; Host workers are not participants.

## Participation and consent

Submit `SExecutionIntent::participate` with a correlation. Its typed request result
precedes offers. Participation, child readiness and module readiness are distinct.
Submit `connect` with both system addresses and a correlation; Host resolves each
once to exact endpoint identities. There is no implicit loading or wait for a
missing peer. Third-party requesters receive results but gain no close authority.

The unordered exact endpoint pair has at most one POD connection. Reciprocal
requests share a handshake, with eight requester slots and at most one pending
request per requester/pair. Repetition returns `busy`, `already_connected` or
`closing` according to the existing state. Tokens are never reused; replacement
cannot inherit an earlier endpoint's connection.

Both participants receive `connection_offered`, including the initiator. Each
submits `consent` for that token. Host then installs its private descriptors and
waits for both installation acknowledgements, opens admission and sends activation.
Each preamble delivers `connection_available` before direct data and acknowledges
activation. Request success waits for both activation acknowledgements. The
five-second eligible establishment deadline remains active through that point.
Late acknowledgements cannot reverse timeout or closure.

The requester result has its own retained state and can outlive participant
retirement under lifecycle pressure. Requester departure cancels that result
without closing an otherwise valid pair. Success records establishment, not a
future lifetime guarantee or a global result-before-data observation order.

## Data and pressure

`communication.submit(destination, pod)` supports an exact token or peer address
with an optional expected token. Each direction has 1,024 POD slots. Busy/full
refuses without taking custody, allocating or advancing publication. Domain
operation state retains the original message and correlation for retry. Capacity
return schedules asynchronous servicing; a full output alone is not runnable work.
A publication whose wake fails remains accepted exactly once and latches failure.

The handler receives immutable message/source data: Host/direct/self kind,
system address, exact endpoint and connection token. Source is authenticated by
Host, not inferred from payload fields. Private controls never reach ordinary
payload dispatch. Within each lane FIFO is preserved; pressure selection and
persistent starvation protection choose among eligible lanes. A domain can pause
direct input while retaining Host, lifecycle and asynchronous progress.

## Closure and lifetime

`close_connection` immediately stops local operations; Host closes shared admission
and sends private closing records. `connection_closing` lets the module cancel
conversations and cease local accesses before `connection_quiescent`. The preamble
cancels unread direct PODs and waits for its admitted operations, including final
signals. Transport acceptance does not guarantee handler delivery after cancellation.

Host retains backing and notification pins until both sides prove quiescence and
the admission gate is drained. A native fallback additionally requires actual
return and successful join. Neither timeout nor zero active operations alone is
sufficient. Final `connection_retired` clears preamble bookkeeping. A later explicit
connection receives a fresh token; stopping endpoints cannot reopen participation.

POD and [bulk](direct_bulk_connections.md) share 32 global/eight per-endpoint slots.
Their private descriptor layout is version 1 with exact size/version checks.
Lifecycle rings hold 256 records per direction and are independent of ordinary
reply credits and saturation. Host and consumers must be rebuilt together.

Current validation and its controlled/native/DLL distinctions are recorded in the
[implementation checkpoints](module_preamble_implementation.md). The history below
retains its original API and binary scope.

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
