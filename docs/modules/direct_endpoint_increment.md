# Direct POD endpoint increment

Agreed scope, 21 November 2026. Extend explicit direct POD connections to any two
distinct participating module endpoints, using the common Host endpoint foundation
implemented in `ca8770d`. This is a self-contained Host refactor increment.
Implementation authorised on 21 November 2026 in a separate implementation chat.
The common receiving contract, establishment handshake and helper/module closing
responsibilities are agreed. The selected defaults and implementation review
requirements are listed below.

The [broader endpoint design](common_endpoint_lifecycle_design.md#direct-pod-addressing-and-use)
records the agreed behavioural contract. The current
[direct POD](direct_pod_connections.md) and [bulk](direct_bulk_connections.md)
references describe the implemented endpoint path. Only bulk and the existing
Executive stop lookup retain the designated-service-thread selector.

## Outcome

Two known, participating endpoints can explicitly establish a connection, exchange
POD messages in both directions and safely close and retire the connection. Support
pump-to-pump, pump-to-thread and thread-to-thread pairs, both within one module and
between modules. Host workers and arbitrary unregistered native threads are not
module endpoints in this increment.

Host continues to own connection records, transport storage and notification
lifetimes. Each participant owns its outbound producer and inbound consumer roles.
Owning-message transfers remain exclusively towards Host.

## Addressing and establishment

- Any endpoint can request a connection between two named participant system IDs,
  combining module and thread identity. The requester need not be a participant.
  A module pump uses its module ID with `thread_ids::host`.
- Host authenticates the requester independently and resolves both participants
  once to exact endpoint instances. Missing or unready participants return `unavailable`.
  Establishment does not load modules, wait for availability or follow replacements.
- There is at most one POD connection per unordered pair of exact endpoints,
  including pending, active and closing records. Self-connection is invalid.
- Establishment is explicit. Sending through an absent or unavailable pipe returns
  `no_pipe`; it does not initiate establishment or retain the message for later.
- Preserve offer/acceptance, descriptor installation, both-side readiness and
  activation. Request/result traffic belongs to the requester; offer, installation
  and lifecycle traffic belongs to the two participants. Concurrent requests for
  the same pair share one establishment, including reciprocal participant requests.
  Both participants explicitly accept offers, including when they requested the
  connection. Helpers install descriptors and acknowledge readiness; Host activates
  only after both readiness responses. The agreed flow is recorded in the
  [establishment design](common_endpoint_lifecycle_design.md#explicit-direct-connection-establishment).
- Request results remain independent of participant lifecycle controls. A
  third-party requester's departure settles its undeliverable result without
  cancelling the participants' attempt or connection. Participant departure closes
  the connection. Requesting does not grant ownership or closure authority.
- Pending requests for a pair share one offer/installation sequence and retain
  bounded, separate request correlations. Excess or redundant same-requester
  pending requests return `busy`; allow eight requester correlations per pending
  establishment.
  Active pairs return `already connected`; closing pairs return `closing`.
- Each establishment has a fresh, nonreused token. Normal sends use the recipient
  address's existing local mapping. Deferred work for a particular connection
  lifetime must additionally validate its saved token.

The POD transport no longer requires a module-role conversation contract or
application producer/consumer roles. Preserve SYSTEM-message admission; application
message meaning and acceptance belong to the receiving module. Transport/layout
compatibility checks remain explicit.

## Readiness and receiving

Each endpoint explicitly enables direct participation after installing its receiving
and connection-control machinery. Host returns a correlated acknowledgement on the
ordinary Host FIFO before any resulting offers. Readiness is independent of native launch and
overall module startup completion. A responsive endpoint may wait asynchronously
for other modules or connections while handling incoming work. Host accepts this
declaration without interpreting module dependencies.

Incoming Host messages and messages from every direct inbound pipe use a common
receiving mechanism. Physical queues remain separate SPSC transports. Receiving
must be bounded, preserve FIFO per queue and expose the authenticated immediate
source. Direct messages identify the sender's system ID, exact endpoint instance
and connection token. There is no total ordering across queues.

Thread notifications wake the thread; pump notifications make the pump runnable
and wake Host as needed. Publication never invokes a receiving module inline.
Consumption continues to support sender progress under temporary capacity pressure.

### Common receiving contract

Pumps and service threads use the same nonblocking receive operation. Each call
returns at most one POD record and its authenticated immediate-source metadata,
preserving the existing payload and asynchronous request slot. The source is Host
or a direct peer; direct metadata includes the sender's system ID, exact endpoint
identity and connection token. Metadata comes from the selected queue/descriptor,
not the message payload. A Host-routed command's original requester is distinct
from its immediate Host source. This does not introduce incoming owning messages.

The helper applies bounded mechanical endpoint bookkeeping for Host controls
before returning the original record to the caller. It does not silently consume
controls, loop until an application message appears, or invoke module code inline.
Every returned record counts against the caller's receive budget. Installation,
activation and closing update generic helper state; offers remain visible for
module consent, and results and lifecycle notices remain visible for module work
and cleanup. Modules do not repeat the helper's mechanical handling.

Dispatch checks the authenticated source before interpreting message type as
authority. Host controls receive generic handling and are then exposed; Host
service messages reach the existing service/request handlers; direct messages
reach the module's direct handler with peer identity. A peer-supplied message with
a Host control or service-result type cannot invoke the Host handling path.

Selection is round-robin across the Host inbox and active direct inboxes, with a
cursor retained across calls and pump invocations. A receive call examines at
most one traversal of the eligible queues and returns at most one record. Preserve
FIFO within each queue; there is no global ordering across different senders.
The caller may exclude all direct data for one receive call while retaining Host
eligibility. This preserves bounded application buffering under outbound pressure;
it does not change connection readiness or admission. The default includes direct
data. Recompute eligibility each call, preserve the cursor and unread FIFO data,
and use the same eligibility for work-availability/wait decisions. There are no
per-peer/type filters or persistent pause controls. The flag also excludes direct
replies; choosing it remains the module's responsibility.

The caller supplies its overall count/time budget. A service thread uses its usual
wait loop; a pump yields. Notifications are hints to recheck durable state and
queues, not evidence that a message or state transition has been consumed.

Concrete C++ names and record layout remain implementation details to settle.

## Closure and lifetime

Include Host-controlled atomic admission for direct data operations. Closing
admission prevents new operations while previously admitted operations and their
notification signals finish. A status load alone does not establish that boundary.

Endpoint sealing stops local direct publication and new establishment. Host closes
admission and revokes affected pending or active connections. Sealing establishes
that no admitted direct publication can still finish; storage retirement additionally
requires both participants to relinquish access. Closing and quiescence messages
remain legal Host settlement traffic.

Preserve explicit two-sided quiescence and the existing execution-quiescence proof
for abandoned participants. Never-exposed sides require no descriptor acknowledgement.
Timeouts do not permit reclamation. Normal close allows a later explicit request
after retirement; sealing and endpoint retirement permanently prohibit that exact
endpoint from reconnecting. General authority/API for arbitrary pair prohibitions
and their removal is deferred.

Use the existing logged failure/shutdown policy for unrecoverable failures,
including uncertain publication. Do not add recovery queues or silently retry a
possibly published message.

### Helper and module responsibilities

A local close immediately disables local use and requests closure through Host.
Host closes shared admission and posts `Closing` to both participants. Previously
admitted queue operations and their notification signals may finish.

Before returning `Closing` through the common receiver, the helper marks the
connection unavailable, removes it from ordinary receive selection and cancels
obsolete installation/activation work. It retains the state needed for closure;
delayed controls cannot reactivate the connection.

The module cancels pending direct conversations and finishes connection-dependent
users before calling `quiesce(token)`. Cleanup cannot depend on another direct
reply after closing. Deferred work uses exact tokens and helper validation, never
unprotected transport pointers or an automatic replacement found by system ID.
Unrelated Host operations and independent copies of message data need not finish
or be destroyed before quiescence. Unread POD messages need not be processed.
Graceful application conversations finish before closing.

The helper detaches queue and notification access, ensures its admitted operations
have finished and sends the quiescence acknowledgement through the Host endpoint.
Safe temporary publication pressure leaves the control pending for normal helper
progress; no blocking wait is introduced. Quiescence neither waits for the peer's
acknowledgement nor for retirement. The endpoint continues Host progress while
retirement remains outstanding.

After both participants have discharged access obligations, Host releases retained
transport resources and posts `Retired` where deliverable. The helper clears its
remaining connection bookkeeping before returning the notice. Request-result
correlations have independent lifetimes and must still settle. Abandonment uses
the established execution-quiescence proof; timeout alone never allows reclamation.

## Bulk boundary and retained policies

POD and opaque bulk connections currently share implementation. Preserve existing
bulk behaviour and adapt shared internals only where required by the POD changes.
One POD connection per endpoint pair does not remove the existing independent bulk
connection. Bulk addressing generalisation, producer/consumer role changes and
held-view semantics are outside this increment.

Retain growable POD transports, the current 32 overall/eight-per-participant
connection limits shared with bulk, and the five-second eligible establishment
deadline. This increment does not introduce fixed-ring policy, new global capacity
guarantees or reserved response slots.

## Migration and validation

Migrate existing POD users to the common endpoint interface and remove the replaced
POD execution-selection and role-contract paths. Preserve version/layout checks,
apply the repository's pre-release version-1 policy to changed contracts, and rebuild
affected consumers. Do not retain a parallel legacy POD implementation.

Validation must cover:

- All execution-context pairings and distinct endpoints within one module.
- Explicit enablement, unavailable participants, third-party and reciprocal
  requests, separate requester/participant departure, and pair uniqueness through
  retirement.
- Authenticated source metadata, FIFO per queue and bounded progress across Host
  and direct inputs, including sleeping-thread and pump scheduling races.
- Partial installation, departure/replacement at the same address, delayed controls
  and stale-token rejection.
- Revocation and sealing during queue access and notification, followed by safe
  quiescence, retention and retirement under pressure or abandonment.
- Delayed module cleanup, pending quiescence publication and independent request
  results that outlive connection retirement; no premature storage reclamation.
- Existing POD consumers and bulk behaviour after shared implementation changes.

## Implementation and validation record

The implementation uses `connections::CClient` over `CEndpointContext`, exact
system-ID resolution in Host, separate requester records, and a closed-bit plus
operation-count atomic gate. The current API and memory-ordering proof are in
[direct POD connections](direct_pod_connections.md). Executive, Audio, Rendering,
ordinary test consumers and lifecycle fixtures use authenticated common receiving.
The replaced POD role contracts, designated-thread addressing and separate result
drain are removed. Bulk keeps its designated-thread/capacity policies.

The shared control/descriptor contract is version 1 with explicit size/version
checks. These messages and descriptors are runtime-only; no maintained stored
payload format needs conversion. Engine, modules and fixture binaries are rebuilt
against the current headers. Existing authored assets retain their formats.

Focused regressions cover all seven valid pump/thread and same/across-module
pairings (one module cannot have two distinct pumps), reciprocal and third-party
requests, the eight-requester and shared connection limits, both-side consent,
activation-before-result ordering, source authentication, persistent fair receiving,
paused direct input, real pump scheduling, native thread capacity waking, partial
installation, closing during an admitted signal, deferred cleanup and control
publication, stale tokens/address replacement, result publication after retirement,
requester departure and execution/join-based abandonment. Bulk backing, publication,
held views, capacity, partial installation and notification regressions remain.

Coordinator review corrected result consumption, existing-active result ordering
and fatal direct failure escalation. The focused Debug x64 run passed 64,974
assertions with no failures (`build/direct-endpoint-review2-tests.log`). A real
Host/pump poisoning test observes logged emergency shutdown and retained cleanup
obligations; wake-failure tests check the endpoint failure state. The common Host
lane remains usable when only a direct queue is poisoned.

Windows validation evidence (21 November 2026):

- All four Debug/Release x64/Win32 solutions and their three input, three pump and
  six lifecycle fixture roles built successfully: 52 target/configuration builds,
  with zero warnings and errors in the final target logs. The build transcripts
  are `build/direct-endpoint-final-build-matrix.log` and
  `build/direct-endpoint-final-build-resume.log`; final source refreshes are in
  `build/direct-endpoint-final-refresh.log` and
  `build/direct-endpoint-final-unread-rebuild.log`. The initial lifecycle missing
  include and conversation-retirement fixture failures were corrected before
  the final builds and affected-case reruns.

- Debug and Release, x64 and Win32 ordinary `-t1` suites passed, including Audio,
  TextureBatch, HostPump and bulk tests. Logs follow
  `build/direct-endpoint-final-<Configuration>-<Platform>-tests.log`.
- Release x64 DLL lifecycle: all 44 case names have passing evidence. The initial
  41 passes are in `build/direct-endpoint-final-lifecycle-Release-x64.log`; the ten
  affected/remaining direct and service cases passed after fixture migration in
  `build/direct-endpoint-final-lifecycle-Release-x64-direct.log`.
- The strengthened replacement case also passed with fifteen copied POD records
  left unread, token-specific discard before quiescence, an empty copied-record
  buffer after retirement and a fresh replacement token
  (`build/direct-endpoint-final-lifecycle-unread.log`).
- Release x64 Host pump acceptance passed all 52 cases with two requested workers
  (`build/direct-endpoint-final-pump-Release-x64.log`). Native interactive window,
  physical input and audible-output qualification are outside this increment.

Linux GCC Debug also rebuilt the engine, all module/fixture consumers and tests
(`build/direct-endpoint-final-linux-build.log`, followed by the final source refresh
in `build/direct-endpoint-final-linux-refresh.log`). All 29 CTest entries passed
with the repository's private headless PulseAudio harness, including ordinary
suites, policy checks, reflection failure containment, input acceptance and Host
pump/asset acceptance with one and two requested workers
(`build/direct-endpoint-final-linux-ctest.log`, 137.22 seconds). No Linux Clang or
Release matrix was repeated for this increment. The build policy validator reported
zero errors and warnings; its one existing negative-test suppression remained.

`tools/check_line_endings.ps1` and `git diff --check` passed. No Visual Studio item
or filter files changed. No commit or push is included in this increment.

## Implementation decisions and review

The behavioural contract is settled. Allow eight requester correlations attached
to one pending establishment, with at most one pending request per requester for that
pair and `busy` for excess requests. This is independent of participant connection
limits and does not add ownership or change offer/installation behaviour.

The implementation specifies and reviews these concrete details against
the agreed contract, rather than introduce another design layer:

- Public types and helper signatures, exact-token validation, bounded local
  records and distinct outcomes for unavailable pipes, temporary pressure and
  uncertain publication. Reuse existing result/failure conventions where practical.
- The atomic admission/operation-guard representation, memory ordering and wake
  lifetime proof. Revocation must race safely with both queue access and signalling;
  zero active operations does not replace module quiescence.
- Fair receiving and wait integration, including pending control progress and
  pump scheduling. Module dispatch must use authenticated immediate-source metadata.
- Migration of existing POD consumers and shared internals while preserving bulk
  semantics, and the final version-1 contract/build/validation checks above.

Current Host sealing waits for direct connections to retire before acknowledging
the seal. Retain that stronger ordering in this increment. The
broader design permits an earlier acknowledgement after publication stops, but
implementing that relaxation is unnecessary for endpoint generalisation.

The separate implementing chat works against this scope and the broader design,
with review coordinated by chat `01a1f426-8548-7c40-912c-4fe77908a67a`. Design changes affecting
behaviour or scope return to the coordinator. Commit requires separate explicit
user instruction and coordinator review; the user performs pushes.

## Separate work

Availability queries, topology revisions/notifications, public registries, automatic
dependency management, generic HID/report registration, asynchronous asset-ID
abandonment policy, schema-work placement and high-rate Debug performance remain
separate increments. None is a prerequisite for explicitly connecting two known
ready endpoints.
