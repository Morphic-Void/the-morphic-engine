# Module preamble implementation decisions

Status: completed implementation decision record; originally proposed on
22 November 2026. Module consent, 256-message lifecycle rings,
1,024-message ordinary rings, local FIFO growth capped at 1,024 messages,
pressure-based draining with starvation protection, existing-state deferral and
module-fatal message loss are approved. Fatal events in an active or pending
Executive require framework-wide shutdown. Implementation was authorised on
22 November 2026 as a separate Astra 6 Extra High chat under coordinator review.
The [agreed design](module_preamble_design.md) defines the scope and authority.
The recorded review decisions below describe the delivered scope. Numeric
defaults are implemented policy values; later tuning requires separate evidence.
The [final qualification ledger](module_preamble_validation.md)
records implementation and automated qualification complete within their stated
scope, with final coordinator review accepted on 23 November. Its failed/unrun
qualifications and retained limitations remain explicit. The separate 24 November
physical follow-up confirmed both operator checklists and ordinary cleanup;
instrumented native cleanup failed. That ledger separately records the corrected,
review-complete fixture follow-up, consolidated coordinator acceptance on
24 November and affected-consumer validation, without claiming a corrected native
process rerun.
Historical audit and migration language below describes the agreed plan.

Use one Host-owned preamble per execution endpoint, a private pair of lifecycle
rings, fixed ordinary POD rings, and a small versioned module execution interface.
Reuse existing Host connection, resource and retirement accounting. Replace module
pump loops and connection mechanics; preserve domain state machines, workers and
native affinity.

## Findings from the pre-implementation audit (22 November)

This table records the implementation before migration; removed API names are
historical findings, not current interfaces. See the implementation checkpoints
for subsequent changes and validation.

| Area | Behaviour before migration | Migration decision |
| --- | --- | --- |
| Common endpoints | [CEndpointContext](../../framework/threading/CEndpointContext.hpp) supplies reads, posts and sealing; CThreadContext adds waits and progress. | Reuse the endpoint resources and counters internally; remove transport consumption and sealing from the module-facing view. |
| Thread launch | [CThreadPackage](../../host/threading/CThreadPackage.cpp) prepares DLL context, then calls the DLL entry for its entire execution loop. | The native entry should run the Host preamble and invoke individual DLL callbacks. |
| Module coordination | `TServiceThreadPump` (removed) is compiled into DLLs and manages child creation, readiness, stop and retirement. | Move the standard single-child coordinator into Host while preserving a way for custom module coordinators to order multiple children. |
| Pump execution | [CPumpedClient](../../host/runtime/execution/pumped_client.cpp) invokes activate, pump, begin_stop and deactivate under restriction guards. | Retain the guards, native-owner checks and retention rules; replace DLL transport loops with the common Host dispatcher. |
| Direct selection | `CClient::receive` (removed) round-robins Host and direct inputs; it does not currently implement Host-first selection. | The proposed priority order is an intentional selection change, with explicit fairness tests. |
| Connection mechanics | CClient validates descriptors and processes controls but also returns those controls to module code. POD offers require module consent. | Move mechanics into Host, replace raw controls with deliberate module-facing notifications, and retain consent. |
| Identity | [system_context.cpp](../../foundation/system/system_context.cpp) has an image-local module ID and image-local TLS thread ID. | DLL transitions already select the appropriate image's module identity; do not change the module-global ID around callbacks. |
| Allocation and provisioning | [CBoundModule::prepare_thread](../../host/module/bound_module.cpp) installs DLL thread identity, provisioning and the default module memory context. | Reuse installation and keep Host infrastructure allocations separate from module state. |
| Sealing | [endpoint_runtime.cpp](../../host/runtime/host_endpoints.cpp) requires the seal's POD count to equal the already-accounted count when its ordinary FIFO record is read. | That equality and the later extra count for the seal record are invalid after moving sealing to a separate channel. |
| Full ordinary output | [SClientRecord::reply](../../host/runtime/execution/client_record.cpp) consumes a logical reply credit before posting and marks refusal as accounting failure; reserve_replies only counts obligations. | Full rings must become temporary pressure. Already-owed immediate results need retained storage; credits alone do not reserve a ring slot or memory. |
| Existing retries | Direct Audio/Rendering replies retain their POD on full; CProviderClient retries definite busy submissions. Host publish_reply/retry_reply users often retain operation records. | Reuse those retry paths and audit remaining one-shot boolean posts rather than hiding all pressure in a new outgoing queue. |

Module differences must survive consolidation:

- [Rendering](../../rendering/runtime/rendering_execution.cpp) receives up to 32 batch
  returns and 32 messages per loop. It can pause direct reads, drains outstanding
  jobs and keeps Exiting visible while waiting during shutdown.
- [Audio](../../audio/runtime/audio_execution.cpp) checks/cancels device work, retains
  a refused reply, pauses direct input when needed and uses a timed wait while a
  run is active. Cancellation performs device cleanup before completion.
- [Executive](../../executive/runtime/executive_execution.cpp) checks eligible-time
  budgets before input, including under continuous traffic, and uses restriction
  notifications to resume timing. Its self-stop and replacement semantics remain.
  The later user clarification treats these acceptance watchdogs as stall
  detectors: a transient incoherent observation permits domain dispatch, so a
  completion can settle without strict proof of its prior deadline. Keep candidate
  budget starts separate and withhold corresponding publication until anchored.
  Track continuous timing unavailability across phase changes, log at 1s/5s,
  summarize recovery and fail through the existing timing path at 30s of awake
  time. Use a 1ms timed wake when retry is the only work. No additional ordinary
  admission flag or retained-message mechanism is introduced.
- [Platform](../../platform/runtime/platform_runtime.cpp) uses a two-millisecond
  maximum pump budget, progresses accepted service commands and native/input/report
  work, and waits for domain cleanup before sealing. Native restrictions and modal
  maintenance remain necessary; a callback is not preemptible.

The existing service threads can end without a cooperative seal; Host still joins
and drains their final submissions. The new normal path can share cooperative
sealing, but native-return/failure retirement remains an essential supported path,
not a compatibility shim to remove.

## Ownership and public boundary

Introduce a Host-private CModulePreamble owned by the stable endpoint record. Its
state includes the bound module callbacks, module state pointer, logical system
ID, exact endpoint token, private lifecycle state, direct connection table, source
selection counters and local self-message FIFO. Host main owns global admission,
connection coordination and retirement; the endpoint thread owns its preamble's
local mutable state. Host main communicates with that state through the rings and
existing durable atomics, not concurrent table mutation.

Move connection descriptors, admission gates, installation controls, endpoint
lifecycle payloads and their mechanical helpers out of shared module headers.
Keep ordinary service, asset, borrow and resource protocols public: they describe
domain work and retained resources, not this private execution handshake. This
change does not redesign those protocols or migrate the input/report services.

The public execution descriptor supplies create, handle and destroy functions.
There is one message handler, not a callback registry. Create establishes
module-owned state on the eventual execution thread; destroy releases it under
module identity after execution obligations are safe. Host stores and pins all
callbacks through the existing module binding lifetime.

The module-defined state is an execution-context class owning its configuration,
domain data and a reference or bound value of the installed communication facade.
The ABI callbacks are thin adapters to that object. Both serviced and pumped
contexts use this form. Inline facade methods encapsulate the fixed DLL-to-Host
function table and cookie; module code calls those methods, never raw table
entries. Host-internal routing uses direct calls. This adds no base-class hierarchy,
handler registration or capability-discovery mechanism.

The handler accepts an immutable invocation view containing:

- The ordinary POD message or a typed framework-generated event: start, stop,
  asynchronous service, connection offer/availability/closure, or request result.
- Authenticated source metadata: system ID, exact endpoint token and connection
  token when applicable. System ID is the visible address; exact tokens preserve
  replacement safety and existing reply routing.
- Remaining invocation budget and relevant immutable execution facilities.

Generated events are public module notifications, not the private lifecycle
records that caused them. They use the same handler. The view expires on return;
module work retains copied PODs/tokens and its existing explicitly retained
resources, never Host connection descriptors.

Source kind and identity come from the bound endpoint and selected input, never
from claims in the payload. A peer cannot send a Host-shaped payload to enter
Host-control handling. Self delivery identifies the bound module context, not
Host, even though Host owns its FIFO.

The handler reports ready/not-ready, immediate asynchronous work, next wake
deadline if any, direct-input eligibility, cleanup completion or failure. Keep
native-event-active and application-stop flags with their existing capability
checks. Readiness is monotonic and cannot reverse stop or failure. A successful
handler return means this invocation was handled; it does not complete an
asynchronous domain operation. Domain state remains in the module.

Expose one bound communication facade with POD and owning-message submission
forms. Both take destination information; neither takes an originator argument.
The facade calls Host function pointers with an opaque endpoint cookie. It has no
transport read, wait, seal or handshake methods. It is confined to its bound
execution context; using it from a batch worker or foreign thread is rejected.

Use explicit destination kinds: Host, self, peer system ID with optional expected
connection token, or exact connection token. This avoids ambiguities between
logical addresses and stale connections. Sending to an absent pipe returns
no-pipe; it does not implicitly establish a connection or buffer until one exists.
Calls into Host only submit or inspect local state; they never dispatch another
module handler or recursively run the Host loop.

Retain existing information in submission results: accepted, busy/full, closed or
no-pipe, invalid, transport failure and published-but-wake-failed. Do not collapse
uncertain publication into a retryable refusal. Owning messages remain supported
towards Host only; direct and initial self messages use the existing POD envelope.
No arbitrary owning-message network is added. For the initial self path, accept
registered SYSTEM payloads; support for module-local opaque payload IDs would need
a separately specified validation boundary because the routing code runs in Host.

Bulk views remain token-based specialized operations on this facade, using existing
latest-state and held-view rules. Bulk bytes are not converted to FIFO messages.
Batch submission/return remains the specialized CBatchClient facility; module
asynchronous servicing drains it. Neither worker scheduling nor batch ownership
is redesigned to force it into the ordinary message envelope.

## Identity and memory

Foundation and Framework sources are compiled into Host and each DLL, including
their ambient state. Crossing a DLL function pointer selects that DLL's module ID
and memory defaults; returning selects Host's again. Only thread ID, provisioning
and any per-thread memory override need installation in the selected image.

Before each DLL callback, use the binding's preparation mechanism with the stable
module execution view. Host preamble execution has module_ids::host and the
service thread ID for serviced execution, or the caller's thread ID for pumping.
Pumped entry preserves any Host thread-local state it temporarily changes. Nested
execution of the same context is rejected. No mutable global module-ID swapping
and no new thread-wide cross-DLL identity singleton are required.

Transport, connection-table and self-FIFO backing use an explicit Host memory
context. Module-created state and domain allocations retain module attribution.
Owning submissions preserve existing transfer/accounting machinery. Add real-DLL
tests, because same-image test callbacks alone cannot prove image-local identity.

## Fixed ordinary POD rings and provisional capacity

Replace TQueue backing only for ordinary module-endpoint POD lanes: both directions
between each preamble and Host, and both directions of each direct POD connection.
Use TRing<CErasedPodMsg> behind a Host-owned adapter retaining message admission,
endpoint authentication and notification behaviour. Keep TQueue itself and its
unrelated consumers, owning-message transports, Host worker channels, batch queues
and bulk buffers unchanged. Shared CThreadPackage provisioning must distinguish
module endpoints from Host workers rather than replace every POD transport globally.

The user selected 1,024 messages per one-way ordinary ring as the provisional
capacity. With sizeof(CErasedPodMsg) equal to 64, this is 65,536 bytes (64 KiB) per
direction and 128 KiB for a bidirectional pair, excluding metadata. Thirty-two
direct pairs would use 4 MiB of message backing. Lifecycle rings have a separate
256-record capacity. Local self and fallback Host reply FIFOs grow on demand up
to 1,024 messages each; they do not inherit the old transport growth ceiling.
Allocate transport backing before exposing an endpoint;
allocation failure rejects creation/installation and unwinds unexposed resources.

For reference, current endpoint and direct queues use initialise_growable(0u).
[TQueueTransport.hpp](../../foundation/threading/transports/TQueueTransport.hpp)
caps growth at min(0x00100000, memory::t_max_elements<T>()). The
[memory policy](../../foundation/memory/memory_policies.hpp) permits 2 GiB for one
allocation, so the effective cap for a 64-byte POD is the transport's 1,048,576
elements, not the larger 33,554,432-element memory limit. The selected ring has
1/1,024 of that old effective element ceiling. This comparison deliberately ignores
triple buffering. The old ceiling was a lazy growth allowance, not a sensible
fixed allocation target: using it directly would require 64 MiB per direction.

The 1,024 default is a rough starting point, not measured adequacy or an admission
guarantee. Later sizing work should measure peak occupancy, full refusals, burst
duration, waiting time and retained replies under representative loads. Keep small
test capacities available internally to force saturation; production tuning and
per-pipe configuration are not prerequisites for this refactor.

## Handling full pipes

### Module submissions

Attempt ordinary POD publication directly to the resolved output ring. On full,
return busy/full with no accepted message, no increment to publication counters
and no loss of the caller's POD or associated ownership. The module or its existing
operation helper retains the original correlation and resources for a later retry.
There is no generic preamble outbox for module-to-Host or direct sends in this
increment. Thus successful ordinary submission still means actual publication,
and ring occupancy remains a useful indication of external pressure.

The preamble records a blocked-output interest for that exact route/token, not a
copy of the message. Capacity-return wakes trigger an asynchronous service
opportunity when the route becomes writable; closure triggers the existing closed
notification. Clear the interest when serviced, retaining it again if another
attempt is full. Recheck writable state before parking, using the existing sampled
epoch protocol so capacity returned before interest recording is not lost. Never
retarget a retained message to a replacement connection implicitly.

Returning busy does not block the handler. Continue eligible input, asynchronous
work and lifecycle processing while output is full. A failed output alone does
not justify repeatedly reporting work_pending. Existing operation state separates
waiting-for-capacity from work that can run now. The synthetic service event can
be coalesced for several writable outputs; no per-message retry callback registry
is required. The module still decides whether its operation should retry or cancel.

Do not turn full into endpoint failure. Invalid routing, expired connection and
notification failure remain distinct. TRing publication itself is all-or-nothing
and allocates no memory; a wake failure after publication is still accepted-but-
failed-to-notify and must never trigger retransmission. Owning-message refusal
keeps its existing move/ownership contract and separate transport.

Audit boolean one-shot submissions during migration, especially Executive scenario
requests, Rendering asset operations and startup readiness. Safe full must retain
the existing operation and resume without replaying side effects, reallocating a
correlation or starting its completion timer twice. Existing Audio/Rendering direct
reply and CProviderClient busy paths provide models. Retained module buffers remain
module policy; the preamble does not promise a general solution to cyclic protocols
whose participants deliberately stop reading all data while waiting to send.

### Host results and notifications

Prefer retaining a deferred response in its existing asynchronous operation or
state-transition record. Do not erase or advance that record past publication,
release referenced resources or create a second queued copy merely because the
pipe is full. Retry from that same state with the same correlation when capacity
returns. Explicit cancellation/recipient retirement uses the existing protocol.
This applies to ordinary responses as well as private handshake transitions.

Ordinary Host output needs two explicit contracts. Current reply() callers often
have only a temporary result and immediately finish their operation, whereas
publish_reply() callers use physical publication to govern borrowed-resource
retirement. Treating both as fire-and-forget local acceptance would be incorrect.

For already-owed immediate replies without suitable operation-owned storage,
provide a clearly named Host-only retained-reply operation and a per-recipient
fallback FIFO. It publishes immediately when possible
and no earlier retained output exists; otherwise it copies into reserved Host
storage. Success means the Host owns the pending reply, not that the receiver has
seen it. The endpoint's SPSC producer remains Host main. This local queue may grow
up to 1,024 messages and is not another transport or an input queue visible to the
module. Existing operation storage remains the first choice.

Before accepting reply-producing work, establish where each owed result will be
retained: an existing operation record, or reserved fallback FIFO storage. Keep
logical reply obligations separate from FIFO storage reservations; a result held
in an operation does not also reserve a duplicate FIFO slot. For the fallback
path, preflight capacity for retained entries plus outstanding FIFO reservations.
Unreserved notifications also need storage preflight or a retained operation.
Transferring a reply to the fallback FIFO transfers its responsibility exactly
once; the retained entry remains an obligation until published or safely cancelled.
Endpoint retirement includes operation-held and FIFO-held pending output;
reserved_replies == 0 alone is insufficient. Storage allocation failure before
acceptance declines/defers work. Failure to preserve an accepted obligation is
module-fatal message loss. This reserves local storage, not physical ring slots or
a new end-to-end credit protocol. Existing domain limits continue to apply.

Fallback storage reservations last only for the current dispatch: publish or
materialise the immediate reply in the FIFO, or release unused storage before
returning. Never hold fallback capacity while waiting for later input or an
asynchronous event. Such work retains its result obligation and required resources
in its operation record instead. This restriction concerns storage reservations,
not logical reply credits, which may remain outstanding until settlement.

Bound fallback FIFO entries plus its outstanding storage reservations to 1,024
per recipient, as well as available allocated capacity. Reaching that limit
prevents accepting new work dependent on that FIFO, but does not prevent completion
of already-reserved work or retry from existing operation storage. Do not count
operation-held results against a FIFO they do not occupy; their existing operation
limits still apply. Growth must not allocate beyond the configured FIFO ceiling.
The old one-million-element transport limit is historical sizing context only.

Classify ingress before reserving fallback storage. Replace the current blanket
two-reply preflight with the actual message's obligations and retention choice.
Use at most one retained head message per ingress lane (POD or owning), reusing
existing capture storage where suitable. Reading into that slot transfers custody,
not admission to execute the request; account it only after dispatch or explicit
protocol settlement. Preserve owning payloads and include the slot in retirement
proofs. Zero-reply releases/cancellations and work with existing result storage do
not require fallback FIFO capacity. If the head needs unavailable storage, retain
it and stop that lane; continue lifecycle, outputs and other eligible lanes.
Do not scan past it, reorder the FIFO or add an unbounded deferred-ingress queue.
Settlement behind a blocked head can still wait: report that head-of-line pressure
and use existing operation deadlines, rather than promising recovery from every
application dependency cycle.

A retained head remains the lane's next message. When runnable, it participates
in service and starvation protection even if the underlying ring is empty. When
blocked on storage or admission, exclude that lane from runnable input selection
and work_pending regardless of ring occupancy. Re-evaluate the head when storage
is released, the admission pause ends, or relevant stop/cancellation state changes;
schedule a new Host turn if necessary. Preserve its retained custody throughout.

Keep post(), retry_reply() and publish_reply() physical-publication paths for
callers that retain their own state. Full returns busy and leaves obligations and
resources untouched. These calls cannot bypass older retained replies: flush that
FIFO first, or return busy. A declined physical publication has not acquired an
output-order position. Preserve protocol dependencies such as prepared-before-
terminal and cancellation receipts through the existing operation state machines.

Prevent continuous new fallback work from starving operation-held output with a
small admission rule. While a ready operation-held response waits behind fallback
output, pause new fallback storage reservations and unreserved fallback acceptance
for that recipient. Honour existing reservations, so that finite backlog can drain.
Give ready operation-held outputs a finite retry turn, then allow at least one
pending ingress admission opportunity when storage permits before another output
turn can renew the pause. Reopening must not wait for all ready output to disappear;
newly ready responses cannot extend the current turn. Persist rotation both across
producers and among their pending operations. A turn interrupted by capacity or
execution-budget exhaustion resumes from that cursor, without resetting its finite
allowance. Use existing operation records, not a second payload queue or a global
output scheduler. An operation awaiting a domain event is not ready and must not
hold this admission pause. Already-issued FIFO order and actual-publication receipts
remain unchanged. Persistent lack of progress is diagnosed; it does not justify
dropping replies or enlarging storage automatically.

Audit each immediate reply/notification, including module acknowledgements, asset
results/acquisitions, provider-operation results and report request/cancel messages.
A copied POD does not own its referenced memory. Stage a pointer-bearing reply
only when existing backing/borrow records keep every referent valid through staging,
publication and retirement; otherwise retain the producing operation and use the
physical-publication path. In particular, keep report retirement receipts and
borrow/schema publication outcomes on publish_reply(): queued continues to mean
actual ring publication, cancelled means proven non-publication under its existing
rules, and busy retains the operation. Do not reinterpret an outbox copy as exposure.

Assign a per-recipient ordinary-output ordinal when retained acceptance or immediate
physical publication succeeds. Drain retained entries FIFO, without duplicate
copies on retry. Lifecycle frontiers that cover Host output use the issued ordinal,
including staged replies, and cannot complete until that prefix has been published
and dispatched as required. Endpoint-to-Host seal counts still refer only to actual
publication because module submissions are not staged by the preamble.

On recipient termination, stop new publication and settle pending output using the
existing operation-specific undeliverable/retirement rules, after the required
quiescence proof. Release pins and reply obligations exactly once. Do not extend
this task into the separately deferred orphan-asset cleanup policy.

### Progress and shutdown

Every ordinary read signals capacity return to its producer using the existing
notification/epoch mechanism, including Host reads and preamble reads. Lifecycle
progress never depends on ordinary reply credits, ordinary output space or the
retained-reply FIFO. Retry private controls from their existing transition records.

Retry ready outputs in bounded passes, then consume input and run module work;
one full destination must not block other destinations or the whole Host turn.
Pending output to a full ring is not immediate work. Output with available capacity
is. Preserve this distinction in both serviced waits and pumped work_pending.
Cooperative sealing/retirement continues until already-accepted obligations settle;
full rings alone neither authorise dropping work nor prove execution quiescence.

Message loss is fatal for the module whose communication obligation is lost. This
includes losing an accepted self message, overwriting unread data, discarding an
owed response without protocol settlement, or forgetting a pending lifecycle
transition. Record module failure through the durable Host failure path and use
the existing shutdown/retention rules. Host-owned loss affecting a module also
fails that module. Preserve current framework-wide shutdown escalation for fatal
endpoint/module failures, including non-Executive modules. Isolated failure
containment and restart are outside this refactor; recording the affected module
does not imply the rest of the framework continues. Safe refusal
before acceptance, preserved deferred work and explicit protocol cancellation are
not message loss. Published-but-wake-failed remains its existing failure case and
never permits resending as though publication had failed.

Executive failure has framework-wide scope. Any fatal event in an active or
pending Executive module, including message loss in any of its execution contexts,
must initiate shutdown of the entire framework through Host's durable failure
path. This covers initial startup, normal operation and a pending replacement;
another active/pending Executive does not mask the fatal event. Framework operation
requires an active or pending Executive. An orderly replacement with a pending
Executive remains a valid transition, distinct from fatal failure.

Record the fatal cause and begin shutdown without depending on spare message-pipe
capacity or an acknowledgement from the failed Executive. Continue safe settlement,
draining and retirement under the existing fatal deadline/retention policy; a
shutdown decision is not permission to destroy live state. No automatic Executive
restart or fallback is introduced.

### Pressure diagnostics and complexity limit

Use the existing logging facility and small owner-local counters/timestamps to
make sustained pressure actionable. Record full/refused submissions, high-water
occupancy, fallback reservations, pending output age and time since progress.
Report prolonged saturation, ingress blocked on reply storage, overdue eligible
input/output service, and lifecycle/activation stalls. Include endpoint/system
identity, direction or connection token, queue occupancy/capacity, retained counts,
blocked operation kind and correlation where available. Distinguish deliberate
direct-input pausing and native restrictions from an eligible source being starved;
do not log message contents or infer a cause that the counters cannot establish.

Count transient full conditions without warning on every attempt. Emit a warning
when a pressure episode persists beyond a named diagnostic threshold, rate-limit
continuing reports and emit one recovery summary with duration, peak occupancy and
refusal count. Use existing eligible-time accounting for restricted operation
deadlines; diagnostic elapsed time does not change timeout semantics. Thresholds
and report cadence are tuning constants to select during implementation, with
shorter test settings. Check on existing maintenance/service turns; logging must
not require space in the affected transport, per-message allocation or a new
monitoring thread. Logging failure must not delay communication or shutdown.

Warnings are evidence for correcting traffic, handler behaviour or sizing, not
automatic module failure. Existing protocol deadlines retain their defined outcomes;
actual message loss or invariant failure retains the fatal policy. Keep bounded
passes, simple admission pauses and existing retained state. No automatic capacity
growth beyond the agreed limits, general dependency solver, arbitrary queue bypass
or comprehensive recovery from overload is part of this change.

## Private lifecycle transport and ordering

Use two TRing<SLifecycleMessage> instances from
[TRingTransport.hpp](../../foundation/threading/transports/TRingTransport.hpp),
initially 256 records each. The record is a Host-private tagged, trivially copyable
structure with a local Host operation ID and small tokens/counters or references
to retained Host records. No shared system type IDs are assigned to it.

Host main is the sole producer towards a preamble and sole consumer from it.
The preamble is the opposite role, including when both execute serially on the
same native thread. Other contexts submit their requests through their own
preamble; they never become additional producers of another context's ring.

Families include context creation/outcome, readiness, stop, provider-admission
closure, seal/acknowledgement, connection registration/offer/decision/install/
ready/activation/closing/quiescence/retirement and correlated control outcomes.
Public module intents are translated into these records by Host code. Domain
service results remain on ordinary Host queues.

256 is the agreed initial provisioning choice for the expected fixed transitions,
handshakes and relatively small variable connection-installation population; it
is not a proof that overflow is impossible.
On full, retain the exact pending transition in its existing operation/connection
record and retry without blocking. Preserve per-operation order; a later closing
or retirement record cannot overtake an earlier exposed installation. Read and
process inbound control even when outbound control is full. New module requests
without retained capacity are declined before acceptance. Stop/failure stays
durable outside the rings and always signals the existing wake target.

Lifecycle consumption returns capacity and signals the producer. Private controls
do not require ordinary service reply credits or ordinary queue capacity. Extend
the existing epoch/latch protocol to the rings and pending transitions. A pending
write to a full queue alone is not runnable work; capacity return supplies a wake.

The separate channel requires explicit ordering in three places:

1. **Endpoint-to-Host boundaries.** Seal carries the accepted ordinary POD and
   owning publication counts. Host records the boundary even if it arrives first,
   preserves classification of each prefix and acknowledges only after both
   prefixes are accounted and direct retirement is complete. The seal no longer
   counts as an ordinary POD. Later typed settlement does not move its boundary.
   Ordered controls such as provider-admission closure use the needed ordinary
   publication frontier; control reception need not wait for its effect to become
   eligible. Stop remains immediate and durable, as today.
2. **Host-to-endpoint boundaries.** Provider-admission-closed records include the
   ordinary Host issued-output frontier covering earlier accepted output, including
   retained replies and already-dispatched commands. The preamble cannot report
   the input side drained until that prefix has reached
   the module and the module reports accepted work complete. Reading the control
   record alone is insufficient.
3. **Connection activation.** Installation and activation remain ordered within
   the lifecycle FIFO. The preamble delivers its generated availability event
   before enabling local ordinary dispatch from that connection. It retains the
   event as connection state until delivered; an unrelated lower-priority Host
   queue cannot delay it behind direct data from the same pipe.

These are publication counters and per-operation dependencies, not global sequence
sorting. Keep bounded native-maintenance capture/replay: a nested callback may
record allowed Host transitions, but must not reenter a module handler. Deferred
ordinary messages still count only when actually accounted by Host.

## Processing policy

Lifecycle precedence, pressure-based ordinary selection and starvation protection
are agreed. This replaces the fixed 16/4/1 cycle. The following concrete defaults
implement that policy without changing the module message interface:

- Lifecycle has absolute precedence at each dispatch boundary. Process up to 64
  private records per turn, rechecking before every module invocation. If more
  private input remains, yield/restart the turn before ordinary dispatch. Record
  durable stop/failure before considering any ordinary work.
  The 64-record turn limit is a processing budget, independent of the 256 slots
  in each lifecycle ring.
- Compare each eligible ordinary input ring by occupied slots / fixed capacity.
  Sample on the consumer side under the existing connection lifetime protection.
  Store actual conditioned capacity at creation; do not call producer-only APIs
  to inspect input. Compare fractions with widened integer cross-products. With
  equal capacities this reduces to comparing readable counts. Snapshots guide
  selection; they are not reservations or proof that later reads will succeed.
- Select the highest-pressure ring for at most eight messages, then reassess all
  eligible inputs. Break equal-pressure ties by the original direct-before-Host
  preference and persistent rotation among equivalent pipes. Recheck lifecycle
  and direct-input eligibility before each dispatch; end a slice early on empty,
  ineligible, stop or budget exhaustion.
- After at most eight nonempty pressure slices, dispatch one message from the
  longest-waiting eligible ordinary source, including self, before resuming
  pressure selection. Maintain a persistent service-order ticket per source;
  serving it moves it to the back of that waiting order, and empty/ineligible
  sources leave the order. New eligible sources join at the back. Persist the
  slice count, order and rotation across pump calls; a shortened nonempty slice
  still counts. Thus every continuously eligible source receives an opportunity
  within a finite number of successful ordinary dispatches even under saturation.
  This is not a wall-clock bound for blocking handlers or endless lifecycle input.
- A nonempty self FIFO is eligible for that starvation-protection opportunity
  and is selected normally when no external ring is eligible. Its growable
  capacity percentage is not comparable to fixed-pipe pressure. Do not reserve
  a fixed share for direct versus Host traffic; a pressured Host ring can lead.
- Limit a turn to 64 ordinary messages and two milliseconds for pumped execution,
  or the caller's smaller budget. A serviced driver can immediately take another
  turn when useful work remains. All handlers are non-preemptive.
- Service asynchronous work at the beginning of a turn, after every 32 ordinary
  messages, and before yielding/waiting/returning. Coalesce coincident service
  points if no intervening work occurred. This guarantees an idle invocation has
  an opportunity to progress, and lets existing Audio checks run before input.
  The ordinary-message counter persists across short turns.
- Lifecycle checks precede asynchronous callbacks too. Exhausting a lifecycle
  budget can defer module callbacks to the next turn; control floods never give
  lower-priority module work precedence. Finite lifecycle bursts are assumed.

Apply the same pressure-selection rule to ordinary POD ingress at Host main across
module endpoints, including runnable retained heads in eligibility and starvation
protection even at zero ring occupancy. A blocked head makes its lane ineligible;
pressure behind it cannot make that lane runnable. Retain separate bounded
owning-message and worker-service passes
so POD pressure cannot monopolise the Host loop. Local retained-reply flushing is
an output task with its own bounded pass, not an input source with fabricated
occupancy. Full ordinary rings remain visible as full regardless of producer-side
retained work; no additional producer-pressure telemetry is required initially.

An availability/closing/stop event is a bounded consequence of lifecycle
processing, delivered before dependent ordinary work, not a general priority that
modules can assign to their messages. A pending stop can supersede a not-yet-
delivered availability event; it cannot reopen a connection or invoke a handler
after destruction. Control handling itself never recursively enters the handler.

Keep direct-input eligibility because Audio and Rendering already use it to
bound domain state. Re-evaluate after each handler return. Paused direct input
does not suppress Host completions, lifecycle processing or asynchronous work,
and does not alone force busy polling. Reading a message transfers it to the
handler exactly once; the framework does not introduce replay-on-handler-busy.

Deadline-sensitive module logic still checks its budgets before handling each
ordinary message where required, as Executive does now. The progress result
reports the next wait deadline; the serviced driver uses existing epoch/deadline
waits and the pumped driver limits the aggregate Host wait. Preserve eligible-time
restriction behaviour. Keep Platform's native sub-budgets inside its asynchronous
service handler and supply the remaining outer budget; do not turn each ordinary
message into a fresh two-millisecond native work allowance.

The final asynchronous invocation receives any remaining budget, possibly zero;
it may perform mandatory bounded bookkeeping and report future work, but must not
start another unbounded drain. Lifecycle and native callbacks can overrun a budget
as today. This change supplies cooperative yield points, not preemption.

## Self messages

Use Host-owned TPodFifo<CErasedPodMsg> local storage, from
[TPodFifo.hpp](../../foundation/containers/TPodFifo.hpp), with ensure_free before
acceptance and an initial hard limit of 1,024 messages. Grow only as needed within
that limit. At the limit, or on allocation failure, decline without changing the
queue or taking responsibility from the caller. Never discard an accepted message
to admit a newer one. No SPSC transport or cross-thread wake is needed for
same-context submission.

Delivery is deferred, FIFO and non-recursive. A self message may be dispatched
later in the same turn, after the submitting handler returns; it need not wait for
another external pump call. Appending during dispatch goes to the tail. Both the
ordinary budget and asynchronous cadence count self messages, bounding a handler
that repeatedly sends to itself. New self work is refused after local stopping;
already accepted messages are presented while stopping so the module can cancel
or settle them. Completion requires their consumption or an explicit module-owned
cancellation decision; no silent loss of accepted work is introduced.

## Connections and consent

Keep exact endpoint identities, nonreused connection tokens, reciprocal-request
merging, existing limits, eligible establishment deadlines, shared admission
gates and notification pins in CDirectConnections. Requester departure settles
its result obligation without closing an otherwise valid pair. Participant
departure closes the pair. A third-party requester gains no close authority.

Move endpoint-local CClient mechanics into Host. Do not give modules descriptors,
ring pointers or notifications whose lifetime they could extend accidentally.
The bound facade retains token-based send, close, query and bulk operations.

Agreed consent interface: the private Offer produces a public connection-
offered notification through the single handler. The module submits accept/reject
through the same outbound interface, correlated by the offered token. Host
performs every subsequent installation step. This preserves current per-offer
decisions and the existing static bulk policy without making Host understand
Executive/Rendering/Audio domain policy. Default refusal is safe. No handler
registration system or new asynchronous consent scheduler is needed.

Once both installations are acknowledged, Host opens admission and sends local
activation records. Each preamble delivers availability before local direct
dispatch and then acknowledges that activation. Release request success after
both activation acknowledgements. This is a small strengthening of today's
publication-based success point and uses the same retained request record.

Keep the existing eligible establishment deadline running through both activation
acknowledgements, even after data admission opens. Distinguish admission-open from
establishment-complete in the retained connection state and deadline/wake checks.
Timeout or closure before completion fixes one failed/cancelled result for each
requester and enters normal closing; actual result delivery may remain pending
under pressure. Later acknowledgements cannot change that outcome or reopen the
connection. Once both acknowledgements complete establishment, retire that deadline;
it is not a deadline for the requester to consume the result.

Success records completed establishment, not a lifetime guarantee. Closure can
race with result consumption. No cross-context observation order is promised;
peer data may arrive before the requesting context consumes its result. Modules
must use the availability notification and exact token, not assume result-first
delivery. Failed or cancelled establishment produces a request result; any exposed
side still completes safe closing/retirement.

Closing first disables new local operations, then notifies the module. The module
cancels conversations and relinquishes held bulk users through a token-based
completion intent. The preamble sends private quiescence only after that module
work and its own local operations finish. Zero active queue operations is not
proof that the module released a held view. Retain the existing two-sided proof,
closed admission, final-drain/join fallback and no-free-on-timeout rules.

Orderly connection closure explicitly cancels unread PODs on that connection and
requires the module to settle/cancel dependent conversations before quiescence.
It does not require every accepted POD to reach a handler. Successful posting
means transport acceptance, not completed processing; the connection's closing
notification supplies cancellation context without new per-message receipts.
Storage is reclaimed only after the existing two-sided retirement proof. This
defined cancellation is not fatal message loss. Discarding messages outside that
protocol, or abandoning owed results without settlement, remains fatal.

## Module coordination and shutdown

The implementation retains one pumped coordinator per module instance and the
requested service threads. A Host standard coordinator replaces the removed
`TServiceThreadPump`, configured by an optional initial child execution descriptor.
No-child modules and custom coordinators remain supported. A custom coordinator uses the same
handler and public context-creation/stop intents; it does not implement a pump.
Descriptors and creation context are module-owned or Host-copied with explicit
lifetime through settlement, never borrowed from a departing requester's stack.

Native launch/preparation, module-handler readiness, direct participation and
overall module readiness remain distinct. Explicit readiness/participation intents
use the bound facade; the preamble generates the private protocol. Preserve the
ability to receive connection offers before overall startup is complete. Child
creation, execution-ended and retirement outcomes remain independently tracked.

Ordinary module stop reaches its coordinator first. The standard coordinator
stops its child and waits for retirement; custom coordinators retain ordered
multi-child stop. Emergency failure may stop all children as today. A durable
module-stopping bit must not accidentally force every child to stop before the
coordinator's chosen order.

For an endpoint, the preamble owns the common sequence: record stop, notify the
handler once, stop admitting new independent local work, close provider admission
when requested/appropriate, continue domain cleanup and eligible completions,
close/quiesce connections, seal ordinary publication prefixes, and complete
execution. The module reports completion of its own work; it never publishes a
seal or claims Host retirement. Provider-admission-closed notification respects
the frontier described above. Retain typed post-seal settlement checks rather
than accepting an arbitrary cleanup flag.

Keep Rendering's batch draining, Audio's cancellation, Executive's shutdown and
Platform's resource/native teardown. The preamble cannot declare module work
complete merely because queues are empty. Batch responses remain consumed by
their owner; outstanding responses at terminal execution are still a failure.

Keep create, handle and normal destroy on the context's execution thread. State
that owns a native device/window or other affine resource releases it through
the handler before destruction. A module-work-complete report is not execution
termination: the preamble can keep progressing Host-private settlement while
waiting for permission to destroy state that other callbacks or borrows still
depend on. That permission concerns module-state users and must not require the
thread's own final join, which would create a cycle. Destroy then runs before
native return, or before the pumped context stops receiving invocations.

Host endpoint resources and the binding remain pinned beyond module-state
destruction wherever final Host obligations require them. Thread retirement still
requires actual return, successful join and final submission drain. If execution
fails before safe affine destruction, retain unsafe state under the existing
failure policy; do not run its destructor on a substitute thread. Pump completion
alone is not proof of resource retirement. Retain the existing grace/fatal
deadlines and process-lifetime retention policy.

The pumped destroy-refusal preservation case uses a separate, narrowly named
process-retention proof: the destroy callback returned failure after the Host seal
grant, the outer pumped invocation unwound successfully, final ingress is drained
and accounted, no invocation remains active, and batch returns/publishers are
settled. Destroy runs once; failure remains latched and no domain callback resumes.
Settle a pending unload as `unload_failed` before ordinary endpoint retirement;
preserve the object, binding, notification pins, dependencies and entire Host
graph. Final shutdown still stops/joins Host workers and batch runners before
returning failure. Do not make retained state generally quiescent or reuse this
proof for acquisition reclamation, endpoint retirement or DLL unload. Failed
create with state, failed callback entry/unwind and other unsafe retention remain
ineligible. This correction is pumped-only: unresolved serviced refusal retains
the existing fatal deadline until actual native return/join and parent lifecycle
proof exist, without synthetic child-retired notices or new lifecycle machinery.

## Completed migration sequence and validation requirements

The following sequence records the approved plan that was carried out in the
shared checkout. It is not an outstanding task list. Actual results, exceptions
and final source/binary provenance are in the
[qualification ledger](module_preamble_validation.md).

The original sequence was:

1. Add the versioned execution descriptor and bound facade; add the Host preamble,
   lifecycle rings and fixed ordinary POD rings. Use focused fixtures to establish
   source identity, pressure selection, ownership, saturation, wake behaviour and
   cross-channel publication boundaries.
2. Move connection mechanics and standard module coordination into Host. Adapt
   Host maintenance, retained replies, actual-publication outcomes, reply accounting,
   sealing and resource-retirement proofs. Migrate all affected one-shot posts so
   ordinary full conditions do not become failure or lose accepted work.
3. Migrate Audio first, then Rendering and Executive. Extract their state machines
   from outer loops without changing domain cancellation or deadlines. Migrate
   Platform's dispatch and asynchronous progress while preserving native guards.
4. Migrate custom lifecycle/pump, input and schema fixtures and consumers. Remove
   old module pump loops, raw shared lifecycle controls and the module-side direct
   client implementation. Keep Foundation transport algorithms and Host worker
   loops intact; the scoped Host POD adapter selects the existing TRing backend.
5. Update architecture references, build manifests, generators and maintained
   fixtures. Finish with only the final current contracts and rebuilt consumers.

The delivered `morphic_module_execution_v1` export rejects historical host-pump
layouts unambiguously. Affected contract markers are version 1. Migration adapters
were removed, consumers rebuilt and affected fixture/runtime artifacts regenerated.
Unrelated service/storage versions and the existing bootstrap remain unchanged.

The original focused validation requirements were:

- Real DLL Host/module identity and allocation attribution; preserved caller state;
  cross-thread facade misuse and source impersonation rejection.
- Serviced and pumped equivalence; readiness, failed creation, requester departure,
  multiple-child stop order, failure, replacement and final destruction.
- Fatal events in active and pending Executive instances, including startup,
  replacement and message loss with saturated pipes, must begin framework-wide
  shutdown. Verify an orderly nonfatal replacement still proceeds normally and
  that shutdown preserves live-state retention and retirement proofs.
- Equal and unequal ring capacities, pressure reversals, all sources continuously
  busy, late-arriving sources, and persisted starvation protection across tiny
  budgets; direct receive pausing, asynchronous servicing and self-message chains.
- Full ordinary rings in both directions at small test capacities; exact retry
  correlations, no duplicate side effects, capacity returned before/after parking,
  closure while waiting, and unrelated destinations continuing to make progress.
- Host reply storage preflight/allocation failure, credit-to-pending transfer,
  pending-output FIFO, termination with staged replies, retained pointer lifetimes,
  and actual-publication receipts refusing to report local staging as publication.
- Full fallback storage with zero-reply ingress, a retained blocked head and
  settlement behind it; verify custody/accounting, continued unrelated progress
  and pressure reporting. Continuous new fallback work must not starve ready
  operation-held output when the recipient continues returning capacity.
- Continuous ready operation-held output must also permit ingress admission after
  finite retry turns, with persistent producer/operation rotation across tiny
  budgets and capacity wakes. Fallback reservations must not survive dispatch to
  wait for later input. Test a runnable captured head with an empty ring and a
  blocked head with a full ring: no starvation, lost rescheduling or busy polling.
- Sustained saturation, eligible-source starvation and recovery diagnostics:
  useful identities/counters, bounded log rate, no warning flood for transient
  refusal, and no failure caused solely by exceeding a diagnostic threshold.
- Self and fallback reply FIFO limits at 1,024, including growth and outstanding
  fallback reservations; reduced test limits; no double reservation or duplicate
  publication when a response stays in existing operation storage. Force loss of
  an accepted obligation and verify module failure, while safe refusal and valid
  deferral remain nonfatal. Preserve unaffected live resources during cleanup.
- Both lifecycle rings full at once, pending acknowledgements, capacity wakeups,
  no ordinary reply-credit dependency and stop while ordinary queues are blocked.
- Seal and admission closure overtaking ordinary prefixes, captured maintenance
  work, staged Host output, late settlement and no premature acknowledgement or
  reclamation.
- Delayed consent/installation/activation, availability-before-data, independent
  requester result, closure races, held bulk views and failed-join retention.
- Activation timeout after admission opens, late acknowledgements and exactly-once
  result settlement; orderly closure with unread PODs cancels dependent work without
  false message-loss failure. Fatal non-Executive failures still shut down the
  framework under the existing escalation policy.
- Audio deadlines/cancellation, Rendering batch drain, Executive restricted timing,
  Platform native/modal work and provider borrows.

Run the affected core/endpoint/direct/bulk suites and real DLL lifecycle/pump
acceptance, plus Platform native/input regressions touched by the migration.
Rebuild engine and consumers in supported Windows configurations before completion;
qualify Linux where an environment is available and report any gap explicitly.
Keep the existing native-report/high-rate Debug qualification limits visible.
Do not repeat a passing matrix without a new change or unresolved concern.

## Recorded review decisions

Current decisions approved by the user on 22 November 2026:

1. **Fixed ordinary POD rings.** Use TRingTransport for the scoped ordinary POD
   pipes, with a provisional 1,024-message capacity per direction. Appropriate
   sizing remains future investigation.
2. **Pressure-based draining.** Lifecycle remains first. Adapt ordinary source
   selection to pipe pressure with starvation protection. This supersedes the
   earlier approved 16 direct / 4 Host / 1 self allocation.
3. **Module consent.** Module-facing offer notifications and accept/reject
   submissions retain per-offer consent while Host keeps the lifecycle handshake
   and installation mechanics private.
4. **Lifecycle and local storage limits.** Lifecycle rings hold 256 messages per
   direction. Local message FIFOs may grow but initially stop at 1,024 messages
   each; this includes self and fallback Host reply FIFOs. Smaller limits may be
   used for backpressure tests.
5. **Deferral and loss.** Prefer retaining deferred responses/transitions in their
   existing operation state when a pipe is full. Loss of a message or accepted
   response obligation is fatal for the affected module. Safe refusal, preserved
   deferral and explicit protocol cancellation are distinct from loss.
6. **Executive failure.** Any fatal event in an active or pending Executive causes
   shutdown of the entire framework, which requires an active or pending Executive
   to operate. This includes startup/replacement failures classified as fatal and
   preserves existing safe shutdown and retention rules.
7. **Failure and closure policy.** Preserve framework-wide escalation for fatal
   non-Executive endpoint/module failures too; isolated containment is deferred.
   Orderly direct closure cancels unread PODs and dependent conversations through
   the existing quiescence protocol. Transport acceptance does not promise handler
   delivery after cancellation.
8. **Bounded pressure handling and diagnostics.** Classify ingress before fallback
   reservation, retain at most one blocked head per lane, and pause new fallback
   admission to give ready operation-held output a progress opportunity. Report
   sustained pressure/starvation and recovery with bounded logging. Prefer these
   small rules and investigation of causes over comprehensive overload recovery.
9. **Activation completion.** Establishment remains subject to its existing eligible
   deadline until both activation acknowledgements arrive. Timeout/closure fixes
   the result once; late acknowledgements cannot revive the connection.
10. **Final progress clarifications.** Output retry turns are finite and alternate
    with ingress admission opportunities; rotation covers producers and their
    operations. Fallback storage reservations end within the current dispatch.
    Retained ingress heads are scheduled by their eligibility, independently of
    whether the ring behind them is empty or full.

The full-pipe design uses these bounded mechanisms: module submissions refuse
full without hidden buffering, private controls and asynchronous replies use
existing state, and only immediate replies lacking such state use reserved fallback
storage. Publication-sensitive callers retain their operation until publication.
The eight-message pressure slice and eight-slice starvation threshold remain
provisional internal defaults. Diagnostic thresholds/report cadence and final
data-structure/API details remain implementation choices within these rules;
appropriate production ring sizes remain later measurement work.

Initial POD-only self delivery and retained specialized batch/bulk APIs bound this
increment. The user separately authorised implementation on 22 November 2026;
implementation and build validation must be established by that task, not this
design record. Commits still require explicit user instruction and coordinator review.
