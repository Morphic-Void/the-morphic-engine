# Common endpoint first pass refactor

**Completed and committed in `ca8770d`, including consolidation.** This is the
scope and decision record for the first pass, originally selected 20 November
2026; it is not an outstanding implementation plan. The retained specification
below describes that checkpoint. The [Host foundation](host_pump_foundation.md)
defines current behaviour, and the subsequent [direct POD increment](direct_endpoint_increment.md)
was completed in `84506a7`.

See [completed milestones](../project/completed_milestones.md#common-host-endpoints-and-direct-pod-connections)
for delivery status and the [Host and endpoint backlog](../backlog/host_endpoint_follow_on.md)
for remaining work. The later [preamble consolidation](module_preamble_design.md)
is also complete; the old transport/control descriptions below are historical,
not an alternative supported interface.

## Purpose and boundary

Give every module a pump and zero or more explicitly requested service threads,
with a common Host endpoint interface for both execution contexts. Separate module
ownership, execution lifetime and endpoint lifetime so Host can provide services
without treating pumped modules and service-thread modules as different kinds of
client.

The first pass includes a complete ordinary startup, shutdown and retirement path.
It may use logging and application shutdown for unrecoverable edge cases. It does
not promise recovery from arbitrary resource exhaustion or guaranteed settlement
through failed transports.

## Execution and communication decisions

- Every module has one pump. The pump coordinates module lifecycle and cleanup.
- A module requests zero or more service threads through a single asynchronous
  create-and-start operation. Host provisions the endpoint and execution resources
  before entering module code.
- Each pump and service thread owns a separate endpoint: POD SPSC in each
  direction and an owning-message SPSC towards Host only. Common interface does
  not mean shared queues between execution contexts.
- Ordinary Host services have the same asynchronous submission and reception
  semantics for pumps and service threads. A pump submission does not invoke a
  service inline or reenter another module.
- One module-side execution context owns each endpoint's queue roles. Host is the
  producer of replies; worker results return through Host.
- Execution mode determines scheduling and waking, not service authority. An
  endpoint may request a thread or module stop for its own module instance.
- Threads belong to the module instance, not to the requesting endpoint. A
  submitted creation request remains an obligation if its requester exits; the
  module-owned entry context must survive that requester.
- Exact endpoint instance identity is distinct from a reusable system address and
  a native thread ID. Stale results cannot be delivered to a replacement instance.

Creation outcomes go to the requester where deliverable and to the module
lifecycle coordinator. The coordinator observes creation outcome, execution ended
and endpoint retired in that order for each successfully created thread. There
is no ordering guarantee between the two destinations. Bookkeeping must preserve
these obligations or invoke the failure policy below; it must not silently lose
them.

Existing Host facilities must be attributed and routed to the correct endpoint
where the refactor touches them. This includes adapting consumers that currently
assume one client per module. New generic provider registration and platform
service redesign are separate work.

## Shutdown and lifetime requirements

Module stopping is durable state. Once it begins, Host rejects thread creation
that has not been admitted and accounts for previously admitted attempts. A
thread created by such an attempt remains part of the stopping module.

The pump coordinates service-thread stopping and remains available for module
cleanup. Host continues dispatching services and settlement work; it does not
block its loop joining a thread that still needs Host progress. Recording a stop
condition does not depend on finding inbox space.

The first pass must establish submission and incoming-work closure boundaries
across both POD and owning-message queues. The full design's draining, sealing
and final-settlement rules supply the behavioural requirements: finish accepted
provider work that needs fresh requests before sealing; after sealing allow only
settlement of existing obligations. A generic cleanup flag must not admit
arbitrary new work. The selected interface and closure mechanisms are specified
below; no additional control transport is introduced.

A returned service thread is observed and joined. Host then accounts for its
final submissions and outstanding obligations; execution ending is not endpoint
retirement. It marks the endpoint unable to receive results or produce further
responses and never waits for impossible cooperation. Cancellation and cleanup
respect workers and other users still holding resources.

A pump reports when no further invocations are needed. Final deactivation occurs
only after module execution and required cleanup have finished; it performs local
destruction without starting asynchronous work. Host releases endpoints, runtime
state and module code only when their respective execution, resource and
notification users are quiescent. Existing direct and borrowed-resource users
remain part of that proof even though their protocols are not being generalised
in this pass.

No timeout, failure notification or request to stop proves quiescence. If safe
retirement cannot be established, retain the affected storage while shutdown is
attempted and terminate the process if necessary. Do not force unload live code
or reclaim storage that might still be accessed.

## Transport and failure policy

Use current service-thread transport choices and capacity behaviour for every
endpoint in this refactor, replacing the separate pumped communication path.
Replacing growable POD transports with fixed rings, adding a general outbox,
reserving response slots and introducing a new control-pipe topology are not
first-pass requirements.

| Condition | First-pass response |
| --- | --- |
| Temporary pressure with a safe place to retain pending work | Defer publication and retry while continuing receive, stop and settlement progress. |
| New work cannot be accepted safely | Refuse it where the submission or service contract permits; preserve ownership on a definitely unsuccessful owning submission. |
| An accepted non-discardable obligation cannot be retained or delivered safely | Log the failure and initiate application shutdown. Do not silently drop it. |
| Transport poisoning, uncertain publication or a violated lifecycle invariant | Treat as a runtime failure. Do not retry as though publication definitely did not occur. |
| Orderly shutdown is unsafe or cannot progress | Escalate to process termination without unsafe reclamation or forced module unloading. |

Ordinary queue fullness or a scheduling delay alone is not a fatal condition.
Existing operation records and staged results may retain pending delivery where
they already support it. No additional recovery queue or handshake is required
solely to survive an otherwise unrecoverable edge case.

Failure reporting and escalation must not depend on successful publication into
the failed or saturated path. For each failure site, specify its detector,
diagnostic information, whether orderly shutdown remains safe, and the condition
for termination. The selected escalation rules below do not require recovery to
succeed or diagnostics to be perfectly deliverable after a runtime failure.

## Deliberately separate work

The [direct POD endpoint increment](direct_endpoint_increment.md) is complete in
`84506a7`. Its API supersedes the first-pass designated-thread POD selection and
registration-dependent child-readiness details retained below as checkpoint history.

Availability/topology, bounded-capacity alternatives, undelivered new asset IDs,
generic provider registration and richer readiness/recovery are tracked only in
the [Host and endpoint backlog](../backlog/host_endpoint_follow_on.md). They are
independent scopes, not remaining first-pass completion requirements.

## Selected first pass specification

### Common resources and endpoint interface

Extract the communication resources and read/post behaviour of
`CThreadResources` and `CThreadContext` into a common endpoint resource object and
context. `CThreadPackage` retains native launch, join and thread scheduling; its
module-facing context adds waits and thread progress to the common endpoint
interface. Pump activation receives that same endpoint interface and retains it
through its execution lifetime. Pump invocations supply budget/progress and native
execution information, not incoming message arrays.

The common API provides identity access, `read(CErasedPodMsg&)`, POD and owning
`post` operations, stop observation and sealing. Typed helpers use the existing
message catalogue, erased envelopes and async-slot correlation. There is no
second service dispatcher behind the pump interface. Successful post means
queued, not admitted, executed or received. Failed owning post preserves the
caller envelope, matching the existing owning transport.

Each endpoint uses `CErasedPodMsgTransport::initialise_growable(0u)` in both POD
directions and `CErasedOwnerMsgTransport::initialise(0u)` towards Host, matching
current service-thread provisioning. Keep the existing 64-byte erased POD record
and its 48-byte payload. Do not widen every message to accommodate one service.
Public post helpers distinguish queued, busy, closed, invalid and transport-failed
outcomes; a poisoned POD transport is transport-failed, never ordinary busy.
Reads retain their current consume-one-message semantics.

Use one incoming message stream and one outgoing POD stream per endpoint. Remove
pump input arrays, consumed-prefix reporting, separate ordinary/cleanup POD
queues and separate ordinary/control inbox arrays. Messages retain FIFO within
their lane; there is no priority overtaking or cross-lane FIFO guarantee. Existing
service reply-obligation counters remain useful accounting, as on current service
threads; they do not reserve physical slots in the growable transport. Existing
bounded router and operation tables keep their service-specific admission limits.

Host uses the same endpoint drain and dispatch loop for both execution modes.
Retain bounded per-turn work using the current ordinary 64-message drain budget
per outbound kind as the initial default. Pump readers also yield within their
invocation budget. Every active or stopping endpoint gets progress opportunities;
one busy endpoint must not prevent another from being serviced.

### Identity and module coordination

Keep `SModuleInstance` as the exact module lifetime and use `SClientToken` for the
exact endpoint lifetime. Their values are independent; remove assumptions that
one token denotes both. Allocate nonzero 64-bit instance tokens without reuse
within the process. Exhaustion is a runtime fault, not wraparound.

Keep the existing module/thread system ID vocabulary. The pump uses its module ID
and `thread_ids::host`; requested service threads supply distinct registered thread
IDs. An address remains occupied through retirement. Host stores address-stable
endpoint records under the owning module record, rather than one client indexed
only by module ID. Correlations, acquisitions and routing use exact endpoint
tokens; allocation attribution and code lifetime retain module ownership.

Bind the module's lifecycle destination to its pump endpoint when Host creates the
module record. This is explicit record ownership, not a permission check based on
execution mode. There is no coordinator replacement or delegation protocol in the
first pass. The module pump remains the coordinator until shutdown; its unexpected
loss invokes application failure handling. Same-module thread management is
permitted from any endpoint, while cross-module thread management is rejected.
Existing module load/unload permissions remain otherwise unchanged.

Retain the existing pump progress phases for module readiness: the coordinator
reports ready when its required initialization has finished. Existing advertised
services become available with that ready phase. Thread launch alone does not
mark the module ready. No separate readiness subscription or service-by-service
publication protocol is added. During draining, use established dependencies;
opening a new service route or loading a new dependency for cleanup is refused
in this pass. Ordinary work through existing dependencies may continue until
sealing under the documented draining rules.

### Create and start

Submit a typed POD create-and-start request on the ordinary endpoint-to-Host lane.
It carries an async correlation, requested thread ID and priority, module entry
function, module-owned context pointer and an optional designated-service
nomination. Only one live or retiring thread may occupy that role per module;
duplicate nomination is rejected. The role originally selected direct traffic;
since `84506a7` it serves bulk and the existing Executive stop lookup only.
Host derives the module instance from
the authenticated sending endpoint. The entry receives its endpoint/thread context
and the supplied module context. The function and context are protected by the
module lifetime, not transferred as owning messages or trusted as another module's
authority.

Host checks identity, stopping state and duplicate addresses, then provisions an
endpoint, native package, ambient binding context, batch client and notifications.
Provisioning failure produces a correlated failure and coordinator outcome after
partial setup is safe. Publication of the request transfers responsibility for
settling the attempt; requester exit does not cancel it.

Reuse the native package's preparation and entry wrapper, but remove its Host-side
spin waiting for module startup. The wrapper publishes a durable preparation
outcome before entering module code. Host observes it during ordinary progress.
Successful preparation and native launch mean creation success; application
readiness remains a module decision. A thread that immediately exits still has a
recorded creation outcome before execution-ended and retirement notifications.
Preparation failure before module entry is creation failure, with native execution
joined before reclaiming partial setup.

The minimal service-thread pump additionally waits for `EndpointReady` from the
matching child before reporting module readiness. The child publishes after local
initialization. Host authenticates the exact child and forwards readiness after
validation. The first-pass registration prerequisite was removed by `84506a7`:
direct enablement now has its own correlated acknowledgement and is independent
of native launch, child readiness and overall module readiness.
Creation success and readiness remain separate facts; either arrival order is
handled without reversing stop, failure or retirement. No-child pumps may be ready
immediately. `EndpointReady` is a module-coordination message, not public discovery.
The wrapper records return/failure even
if module code omits a final progress-state update.

Requester results use the existing async-slot convention. Coordinator messages
also identify the originating endpoint and correlation plus the new exact endpoint
on success. Keep per-attempt/per-thread lifecycle records until required outcomes
are published or explicitly undeliverable. No new generic outbox is introduced.

Every module exposes the pump lifecycle, including modules that previously only
advertised a service thread or had passive execution. A minimal pump can request
its existing service thread during activation and coordinate it thereafter. Remove
the advertised-thread launch path and the pumped-versus-threaded capability
exclusion from the replaced contract; do not keep both launch mechanisms.

### Existing services and provider dispatch

| Facility | First-pass mapping |
| --- | --- |
| Assets, filesystem and module requests | Keep current typed messages, async slots and workers. Route results to the exact requesting endpoint and retain existing service admission and disposal policy. |
| Bundle ownership and acquisitions | Bind requester-specific rights and releases to the endpoint token. Module dependency cleanup waits for all relevant endpoints and operations. |
| Batch work | Give each endpoint its own batch client using the existing shared worker pool and module binding protection. Pump waiting is incremental progress, never a blocking wait. |
| Service routes and provider commands | Keep contract negotiation and router operation records. Authenticate the exact source endpoint. Existing module-advertised providers bind to the pump; a new public provider-registration system is deferred. |
| Borrow preparation, commit and retirement | Replace synchronous pump callbacks with typed requests/results through the same endpoint. Preserve existing borrow and quiescence rules. |
| Notification registration and binding | Keep existing registration/pin objects. Return preparation/binding results asynchronously and retain publisher/reader protection through retirement. |
| HID and platform reports | Adapt existing traffic to the common endpoint without inventing a generic registry or changing the domain protocol. Preserve current staged report ownership/accounting where needed. |
| Direct POD and bulk connections | The first pass preserved designated-thread uses. `84506a7` subsequently generalised POD addressing to pump/thread endpoint pairs; bulk retains explicit designated-thread selection. Public availability queries remain deferred. |

The first pass bound the nominated direct-service role to an exact thread
instance, with unavailable results for missing/retiring targets. The completed
direct endpoint increment replaced that POD lookup with explicit system-ID pairs.
Only bulk and the existing Executive stop lookup retain the designated-service
selector; another thread never silently inherits that role.

`SDispatchedCommand` includes more metadata than the erased POD payload holds.
Publish a small typed POD notice containing an exact operation token and a pointer
to a stable, immutable Host-owned command descriptor in the existing router
operation record. This is a borrowed descriptor, not an owning transfer or another
inbox. It includes the authenticated requester identity and negotiated contract.

Successful notice publication establishes possible provider access. Keep the
descriptor until the provider has relinquished it by submitting its terminal
completion, or execution quiescence and final drain prove it unreachable. The
provider must cease all descriptor access before posting that completion.
Cancellation after publication does not reclaim the descriptor: the provider must
still settle the delivered operation, matching existing cancellation semantics.
Never-published commands can be cancelled locally. Transport failure with uncertain
publication requires conservative retention and failure handling. No per-command
consumption acknowledgement or pump prefix-confirmation mechanism remains.

Provider `complete` and `commit_resource` become ordinary correlated requests;
success from post means queued, and Host returns their operation status. Borrow
preparation returns the borrow token; notification binding returns the existing
protected notification endpoint. Begin/finish retirement return their existing
status asynchronously. Callers retain offered descriptors and other borrowed
inputs until the corresponding outcome and lifetime protocol permit release.
Existing route/request fields and the async slot identify each exchange; no
module-specific opcode knowledge is added to Host.

Native restriction entry/exit and bounded Host maintenance remain synchronous
execution facilities through `SHostExecutionContext`. They are the explicit
exception for native loops, not an alternate service path. Preserve their current
no-growth/reentrancy restrictions and staging where necessary for either execution
mode; remove staging whose only purpose was pump input-array prefix confirmation.

### Admission closure and sealing

Use ordinary POD lifecycle messages plus durable stop/failure state. Do not add a
secondary control pipe, lifecycle mailbox or per-message receipt protocol.

A provider first requests closure of new command admission through its endpoint.
Host closes that endpoint's provider gate, publishes or cancels all previously
admitted but unpublished commands, then posts an admission-closed result on the
same incoming FIFO. Published commands precede that result. The provider continues
draining and finishes accepted commands that need fresh Host requests before
sealing. The Host gate is generic and does not classify provider opcodes as cleanup.

Maintain 64-bit successful-publication counts locally for the two outgoing lanes
and corresponding Host accounting counts. These are endpoint wrapper bookkeeping,
not added fields on every message. No wraparound is allowed. A poisoned posting
does not advance a trusted boundary or permit a retry; it enters failure handling.

Beginning seal disables independent local submissions and captures the POD and
owning counts. The seal request carries both boundaries in the ordinary outgoing
POD lane, outside the captured prefixes. If it is definitely not posted, retry
that same seal with the same boundaries. Host can consume the seal before the
owning prefix has arrived; it continues draining both lanes and classifies owning
messages by their captured boundary. A dequeued but still unaccounted request does
not count as settled merely because its queue slot was released.

Host acknowledges sealing after both prefixes are admitted/rejected with their
obligations recorded and independent incoming work is closed. Sealing need not
wait for completion of every accepted operation. Common dispatch permits only
typed settlement messages after the boundaries: existing results/completions,
cancellations, disposals/releases, borrow and notification retirement, and existing
thread/module cleanup. Preparation of a new borrow, new service work or a new thread
must precede sealing. Host validates the relevant existing identity/obligation;
there is no caller-supplied flag that makes arbitrary traffic cleanup.

Direct connections stop publication and retire as part of closure. The first pass
integrated the then-supported pairings; `84506a7` generalised the endpoint pairs
and guarded publication boundary. Continue Host settlement traffic until
execution ends. Observe terminal execution before final queue drain, as in the
current service-thread handling; a previously observed empty queue is insufficient.
Host takes over module-side queue consumption only after execution quiescence.

### Stop state, retries and wake lifetime

Extend the existing sticky exit/failure state and Host owner wake to all endpoints.
Self/module stop can be recorded durably without posting into a full pipe. Requests
to stop another same-module thread use its exact endpoint identity; once Host
accepts them it sets the target's durable stop state and signals its wake target.
Host can also set those states directly during shutdown.

Retain the existing thread epoch/wait protocol: successful publication signals the
consumer; consumption signals the producer's progress mechanism. A service thread
captures the epoch before inspecting/draining work, then rechecks its pending work
and stop state before waiting against that epoch. Pending work must not sleep past
a capacity change. A pump uses Host scheduling/work-pending state and retries on
later invocations instead of acquiring a native wait control. Host includes
retained operations awaiting publication in its progress scheduling.

Notification pins belong to endpoint lifetime rather than requiring a native
thread package. Keep the existing `CNotification` targets: a thread wait predicate
or Host work-pending state and owner wake. Retain queue storage, signal targets and
registrations through all readers, signal calls, pending operations and execution
quiescence. Joining a native thread alone does not discharge other publishers.

### Failure detection and escalation

The endpoint helper detects poisoned transports and invalid publication state;
Host admission detects allocation/accounting failures; the execution wrapper or
pump invocation detects failed/ended execution. Record the first fatal reason in
durable state and signal the Host owner wake. Host routes that state into its
existing emergency-shutdown path rather than submitting an error message through
the affected endpoint. Log exact module/endpoint identity, operation/correlation
when known, the failing stage and native/transport status. Logging is best effort.

Known rejection before accepting an obligation uses the normal correlated failure
or unsuccessful-post result. Unexpected loss of a coordinator, a failed service
thread, loss of an accepted non-discardable outcome, poisoning or a lifecycle
invariant violation initiates application failure shutdown. A clean service-thread
return merely reports execution ended; the pump decides whether that is expected
for its module and requests stopping as needed. No automatic restart is added.

Preserve the existing ten-second ordinary shutdown grace diagnostic: it is not
permission to unload live state. For a fatal communication/lifecycle failure,
allow at most ten seconds from the first fatal observation for safe emergency
shutdown. This fatal deadline is not extended by new activity. If safe termination
has not completed, terminate the process without unwinding the retained live Host
graph. If the clock cannot be established/read during fatal shutdown, or continuing
Host progress is itself unsafe, escalate immediately. This hard fatal deadline is
an intentional addition to the current diagnostic-only grace behaviour.

Use the existing process-lifetime retention mechanism when returning failure with
quiescent execution but an unsafe destructor/unbind path. If execution remains
live, use process termination rather than C++ teardown. Neither path forces
individual thread termination or pretends that timed-out resources are quiescent.
The Host loop enforces the fatal deadline while it can execute; this pass adds no
watchdog capable of interrupting a pump stuck indefinitely in native/module code.
That case still requires external process termination.

## Completion record

The first pass and consolidation delivered the selected API, service mapping,
creation outcomes, closure and failure handling. The replaced pump-array/callback
paths and advertised-thread launch path were removed. Final common endpoint,
bootstrap and pump layouts use version 1 with explicit checks, and affected
consumers and fixtures were rebuilt against those layouts.

Validation covered pump-only modules, multiple requested threads, common service
access, requester departure, partially failed creation, both submission lanes,
execution ending before retirement, stale instances and safe failure escalation.
Checkpoint-specific configurations and limits are retained in the
[Host foundation validation record](host_pump_foundation.md). Subsequent direct
endpoint coverage belongs to its own [increment record](direct_endpoint_increment.md).
No first-pass implementation or validation item remains open.
