# Host restriction and bounded maintenance

This is the implemented portable contract committed in `078b595`, following the
[notification increment](service_notifications.md). It refines the agreed
[Platform design](platform_host_pumped_design.md) and
[first-stage contracts](platform_contracts_v1.md). On 03 Nov 2026 the user approved
all five choices below and authorised portable implementation and simulated modal
tests. Its validation evidence is recorded below. The later
[first Windows window](platform_first_window.md) (`f55d514`) integrates the native
move/size, menu and cancellation paths, with separately recorded native evidence.

The `078b595` increment covers portable Host support and a simulated modal
provider on Windows and Linux. Its results do not by themselves establish native
behaviour. Host continues its existing outer polling loop.

The subsequent native integration permits authenticated restriction entry during
stopping so required cleanup can be accounted after sticky shutdown. It retains
thread/invocation checks, bounded accounting and clock-failure handling; deactivation
cannot enter a scope, and ordinary work admission stays closed.

## Ownership and header structure

Host owns availability restriction state, maintenance scheduling and client
subscriptions. A provider reports that an authenticated Host invocation is
restricted; it does not own the shared clock or the consumers' registrations.
Application pause is a separate policy. Pausing a simulation neither stops this
clock nor authorises entry into restricted Host maintenance.

Public boundaries:

| Header / namespace | Responsibility |
| --- | --- |
| `foundation/platform/threading/awake_clock.hpp`, `platform::threading` | Checked observation of the existing awake clock, without a Host dependency. |
| `framework/module/host_restriction.hpp`, `modules` | Restriction reason/scope identities, atomic publication layout, non-owning snapshot reader and local snapshot value. |
| `framework/module/host_eligible_budget.hpp`, `modules` | A budget measured in Host-eligible awake time; depends on the clock and restriction reader. |
| `framework/module/host_execution_context.hpp`, `modules` | Immutable, non-owning exact-client restriction subscription and permitted Host stop signalling endpoint. |
| `framework/module/host_pump.hpp`, `modules` | Authenticated restriction enter/leave and bounded maintenance callbacks, with their results. |
| `host/runtime/host_restrictions.hpp/.cpp`, `host` | Sole writer, bounded scope ledger and publication/fanout policy. |
| `host/runtime/host_maintenance.hpp/.cpp`, `host` | Invocation guard, deferred ingress, fairness cursors and safe-boundary replay. |

These are execution facilities. They introduce no service route, synthetic
Platform borrow or provider advertisement. Existing service protocols stay in
`framework/services/`; notification ownership remains in `threading::CNotification`.
Use the current complete pump interface with coordinated Host/module rebuilds.
Do not retain an older layout, add a compatibility adapter or silently substitute
ordinary awake timing when the restriction interface is absent.

The ownership graph is:

```text
Host process lifetime
  restriction publication block (one writer, many readers)
  bounded active-scope ledger
  exact SClientRecord
    immutable Host execution view
    restriction subscription token + CNotification
      thread predicate pin OR pumped pending-work flag
    sticky permitted Host stop request
  active outer invocation frame
    offered command/message prefixes and protected records
    bounded deferred ingress and reply storage
```

Use stable inline members and the existing curated containers. Preconstruct
deferred owning-message slots before execution; no individual `new`, placement
construction, growable container operation or owner destruction occurs inside
restricted maintenance. The graph remains retained if execution cannot prove
quiescence.

## Reporting authority and scope lifetime

Implemented callback shapes (the ABI-annotated function pointers are in
`modules::SHostPumpServices`):

```cpp
SRestrictionEnterResult enter_restriction(void* const context, const SRestrictionReason reason) noexcept;
ERestrictionStatus leave_restriction(void* const context, const SRestrictionScope scope) noexcept;
SHostMaintenanceResult maintain_host(void* const context) noexcept;
```

`SRestrictionScope` is a nonzero, monotonic Host-issued `u64`. A reason is a
provider-local slot in `[0, 31]`; Host understands counting, not native meanings
such as moving a window or opening a menu. The ledger holds at most 64
simultaneously live scopes. These capacities are review choices, not pre-existing
Platform limits.

Every entry/leave must match the stable supplied context, exact module instance,
client, Host thread and currently active outer invocation serial. Scope tokens
also bind to that invocation. No other thread, worker, stale instance, arbitrary
callback pointer or retained context may publish restriction state.

Entry is permitted from `activate`, `pump` and `begin_stop`, including native
callbacks reached synchronously from them. It is refused during `deactivate`,
another provider's invocation or maintenance itself. Sticky shutdown alone does
not reject an authenticated cleanup scope; Platform separately closes admission
to new native work. The provider must successfully enter before starting
the operation that may restrict Host. Failed entry is not permission to continue.
Existing scopes can always attempt leave during cancellation/shutdown.

Each successful entry increments total depth and that reason's count. Leaving
requires its exact live token; duplicate, foreign and previous-invocation tokens
are errors without decrementing anything. Overlapping scopes may leave in any
order. The first entry opens one restricted interval; the last leave closes it.
Nested reasons never double-count time. Counts, IDs and arithmetic are checked
before mutation; capacity or token exhaustion rejects admission without partial
publication. Once token space is exhausted it is not reset for a new provider.

Every scope must end before its outer invocation returns. At return Host first
establishes that the synchronous callback stack has unwound. An outstanding scope
then constitutes a provider protocol failure. Host closes the residual interval
at this safe boundary if clock accounting remains valid, records the failure and
begins normal failure handling. It does not leave a successor permanently paused,
nor interpret the forced ledger close as resource or execution quiescence.

A clock or publication failure sets a sticky failed state, closes admission and
requests cooperative cancellation. Private scope bookkeeping still permits
unwind; eligible budgets return failure instead of using incomplete accounting.
No timeout repairs a counter, invents an elapsed duration or proves native unwind.

## Coherent restriction publication

The public snapshot contains:

- Publication revision and immutable Host clock generation.
- Exact reporting instance and outer invocation serial while active.
- Total depth, active reason mask and 32 reason nesting counts.
- Cumulative completed restricted nanoseconds and current interval start.
- A clock-sampling transition flag and sticky accounting-failure status; the
  independent stop signal is described below and does not depend on publication
  capacity.

The generation names this Host clock lifetime, not an individual provider.
Provider replacement never resets cumulative restricted time. A reader stores
the generation when starting a budget and rejects a different one.

Follow the agreed atomic-word protocol: the revision and **every payload word**
are lock-free atomic `u32`; split `u64` values into two atomic words. Initially all
publication and observation accesses are sequentially consistent. A writer marks
the revision odd, stores the payload and publishes an even revision. Readers copy
only atomic words and accept a value only when both revision reads are equal and
even. Never use ordinary payload reads under a seqlock, atomic casts of plain
storage or an assumed lock-free `atomic<u64>` on Win32.

Validation happens before publication. No native call, allocation, logging,
signalling or module callback is allowed while the revision is odd. A transition
that samples a boundary clock uses two complete publications:

1. Publish the clock-sampling flag with an even revision, keeping the previous
   accounting fields intact. Eligible observations of this state return `retry`.
2. Query the awake clock, outside an incomplete atomic write.
3. Publish the new accounting fields with that flag cleared, or publish failure.

This closes a subtle entry race: sampling the entry time before any publication
could let a reader accept the old unrestricted snapshot at a later time, then see
eligible time move backwards when that earlier entry timestamp is published.
The transitional publication prevents that reader from accepting a budget
observation. It is readable diagnostic state, never a usable clock baseline.
Reserve enough revision space for both publications and a failure result before
starting this sequence. Fanout follows the completed accounting publication.

Reader bound: four attempts, then `retry`. This is neither an unrestricted
snapshot nor clock failure. Callers retry from their surrounding work/wait loop;
they may not park indefinitely on an unvalidated observation. Notification state
is a hint and cannot substitute for a coherent snapshot.

Reserve the final even revision (`0xfffffffe`) for terminal failure publication.
Before a successful update would exhaust that reserve, publish failure and refuse
further successful clock updates. Never wrap the revision or reuse a generation.
After terminal failure, private unwind state and the independent stop signal can
still progress; no consumer may construct a successful eligible-time observation
from that frozen block. Generation exhaustion prevents a new clock lifetime.

## Checked awake clock and eligible budgets

Expose the following observation using the same platform clock source as the
existing `CWaitDeadline` implementation in
[wait_word.cpp](../../foundation/platform/threading/wait_word.cpp):

```cpp
[[nodiscard]] WaitError query_awake_nanoseconds(std::uint64_t& value) noexcept;
```

Factor the raw query and checked rational conversion so both paths share clock
selection. Do not use the performance counter as a fallback. Preserve existing
`CWaitDeadline` rounding, zero-duration, invalidation, deadline-range and error
semantics. Absolute awake observation rounds down to nanoseconds, with checked
intermediates; avoid overflowing an intermediate product whose final quotient
would fit. Leave the output unchanged on failure.

The approved error choice appends `EWaitError::overflow` for the new observation's
unrepresentable nanosecond result. Existing deadline errors retain their current
values and classifications. Unsupported clocks and native failures preserve the
existing checked error information. Budget arithmetic reports the same checked
`overflow` error through its failed observation.

A successful observation takes snapshot A, queries awake time, then takes snapshot
B. Both snapshots must have the same successful even revision and generation,
with the clock-sampling flag clear.
Otherwise retry the complete observation, with the same four-attempt bound.
Backward clock movement, a time before the active start, subtraction underflow or
duration overflow is explicit failure. Within a budget, reject an eligible time
earlier than its last accepted observation.

For a coherent snapshot and awake time `t`:

```text
R(t) = completed_restricted_ns
       + (depth != 0 ? t - active_start_ns : 0)
E(t) = t - R(t)
elapsed = E(now) - E(start)
remaining = max(0, budget - elapsed)
```

`E` is constant throughout a restriction, including overlapping reasons. Store
`E(start)` once. Starting part-way through a restriction excludes only time after
that start from the operation's elapsed budget. Missing both enter and leave
notifications is harmless because the completed total is durable. An operation
already at or beyond its budget when restriction starts is expired; observing the
restriction cannot revive it. Once observed, expiration is sticky.

Proposed local interface:

```cpp
class CHostEligibleBudget
{
public:
    EBudgetStatus start(const SHostRestrictionReader reader, const std::uint64_t duration_ns) noexcept;
    SEligibleBudgetObservation observe() noexcept;
};
```

An observation distinguishes `running`, `restricted`, `expired`, `retry` and
`failed`, and carries checked remaining nanoseconds and error information.
Zero duration expires immediately. A failed/retried start leaves no live budget;
the caller may not submit the operation until start succeeds. Expired state wins
over `restricted`. A successful start establishes one baseline; an ordinary wake,
ACK, progress hint or restriction notification does not restart it.

Threaded waiting follows the existing epoch/notification discipline:

1. Sample the existing thread epoch, inspect messages, exit and durable state.
2. Clear the restriction notification, then recheck durable state and budget.
3. While unrestricted, derive a native wait deadline from the current remaining
   eligible duration. Waking or reaching this native deadline triggers another
   eligible observation, not automatic operation timeout or budget renewal.
4. While restricted with positive budget, wait for state/message/exit change,
   optionally bounded by a separately required awake watchdog. Do not repeatedly
   wait on an already expired native deadline.
5. Never resample the epoch between the final recheck and wait. If observation
   returns `retry`, retain the baseline and make another bounded observation;
   do not enter a wait based on unknown restriction state.

The native wait is an observation opportunity, not a second source of elapsed
budget. Pumped clients clear/recheck and return to Host without waiting. Existing
wait-layer predicate/exit precedence remains unchanged. Completion-versus-expiry
policy belongs to each operation; a completed operation does not become pending
again when a timeout is reported.

## Exact-client restriction subscriptions

**Install one subscription with every executing Host
client, before its thread starts or pumped `activate` is called.** This removes a
bootstrap gap in watchdog timing and avoids a new asynchronous registration
protocol solely to reach Host-owned state. Passive modules have no reader or wake
endpoint until execution is installed. Workers are not implicit Host clients.

An immutable `SHostExecutionContext` contains the exact instance/client tokens,
monotonic `SHostRestrictionSubscription` token, snapshot reader, notification
reader and permitted stop endpoint. It is stored inline in the stable client
record. Thread resources borrow its address; `CThreadContext` exposes it read-only.
`SHostPumpContext` borrows the same view for pumped execution. No caller may retain
it beyond the exact client's execution lifetime or hand it to an untracked thread.

Host owns the subscription's `CNotification` inline in that client. The snapshot
block has Host process lifetime. For threaded clients, install and pin the
notification after acquiring predicate control and before native thread creation.
This needs an explicit startup preparation step: current `startup()` acquires the
predicate immediately before creating the thread, so merely calling today's
`pin_notification()` before `startup()` is insufficient. Startup failure removes
fanout and rolls back the pin before releasing predicate control; if native
execution did start, first establish its quiescence and drain obligations. For pumped
clients, target the existing stable atomic pending-work flag.

Host is the sole signal producer for these subscriptions. It never exports a
producer endpoint to a module. Publish first, mark subscriptions needing fanout,
then signal under normal Host progress or the bounded maintenance allowance. Each
signal attempt is one maintenance operation. Keep the last-signalled revision
per subscription so continuation/coalescing cannot lose the final publication.
A newly installed subscription starts pending. Snapshot reads remain valid even
if multiple revisions were coalesced or fanout has not reached that client yet.

The subscription is acquired by successful execution installation and correlated
by the exact instance/client/subscription tuple. It remains installed until that
execution ends; the contract deliberately has no separate early unsubscribe.
There is **no asynchronous acquisition reply carrying this reader**, so a late
reply cannot install a reclaimed restriction handle. Existing borrowed-service
registration replies still use their existing close/retire tracking and must be
drained or discarded without dereferencing expired handles. They cannot create,
replace or retire a Host restriction subscription.

Retirement order:

1. Close client work admission and request cooperative execution exit. Continue
   restriction publication and wakeups while that execution can use its reader.
2. Establish consumer quiescence. For a thread, use terminal-before-final-drain,
   drained transports, no outstanding batch work and no return publishers. For a
   pumped client, require returned outer calls, stopped execution, completed
   deactivation and no escaped callbacks. A stopped phase alone is insufficient.
3. At a safe Host boundary, join the native thread before removing its subscription.
   A terminal publication precedes native return; failed join retains the package,
   predicate, pin, execution view and binding. As Host is the only synchronous signal
   producer, being outside fanout proves no signal is in flight. After successful
   join, remove future fanout, release the predicate pin and invalidate the view.
4. Close the thread package and retire the client only after its other route,
   borrow, notification and owning-message obligations also permit destruction.

Terminal state is not permission to erase a client inside restricted maintenance.
Emptying a source transport into deferred capture is not final drain. Retirement
proof must include every captured record, staged delivery, outstanding reply
credit and deferred owning/completion obligation, including failure and invalid
prefix paths. Those obligations retain their exact route, client, package,
subscription and module dependencies until safe replay or proven cancellation
discharges them. Neither a source queue becoming empty nor an outer callback
returning alone permits retirement.
Replacement gets a new record, subscription and pending latch; old snapshots or
messages never retarget it. Shutdown preserves subscriptions required by live
clients. An unproven terminal/drain state, refused package shutdown or unresolved
borrow retains the necessary graph, including the publication block.

Use the same clear/recheck protocol and sticky signal-failure handling as
[borrowed-service notifications](service_notifications.md). A rejected predicate
epoch update is a failure; the existing void native wake API still does not report
arbitrary OS wake errors. The pumped aggregate flag is only a scheduling hint.

## Maintenance entry, allowance and fairness

`maintain_host(context)` authenticates the same exact invocation as restriction
publication. It requires a live restriction scope in `activate`, `pump` or
`begin_stop`. Calling it outside that scope, on a worker, after return or from a
foreign context returns an error without executing work. It does not invoke
another module or re-enter `CHost::run()`.

A separate maintenance guard refuses recursive entry. Read-only sticky stop/error
indications may still be returned on refusal, but no queue, credit, owner or scope
state is changed. Provider callbacks during maintenance are not an extension
point. Ordinary callback authentication must continue to identify the outer
invocation, without preparing/replacing its binding context recursively.

The result contains status (`success`, `invalid_context`, `reentrant`, `failed`),
operations performed and flags for continuation, deferred-capacity pressure,
shutdown requested and cooperative native cancellation requested. A count of zero
is legitimate. It never reports that native execution has been interrupted or
that the provider may destroy resources.

Each pass allows **at most 32 safe operations and 1 ms of awake elapsed time**.
Check the clock before starting and between operations. Stop when either limit
is reached; clock failure ends ordinary maintenance and records cancellation.
The duration is a cooperative scheduling bound, not hard real-time preemption:
one admitted primitive must itself be bounded and nonblocking. Do not budget this
pass in eligible time, which is frozen during the call.

Rotate persistent cursors across clients and across ingress, independent controls,
safe replies and notification fanout. Count each admitted record, recorded control,
reply attempt or signal attempt as an operation. Bound unsuccessful probes too:
one selection probe consumes an allowance, so empty/full queues cannot cause an
unbounded scan. A full class yields to the others and retains its cursor for the
next opportunity. Poll the small sticky stop state at entry/exit regardless of
ordinary queue capacity; this poll cannot initiate teardown.

The agreed nominal 10 ms modal opportunity is a later native integration target,
not a latency guarantee. The portable provider explicitly invokes maintenance at
controlled points. Continuation state survives a short pass; no callback spins
until all pending work completes.

## Whitelist and actual handler boundaries

The existing normal Host loop and its handlers are **not** a maintenance API.
The following table is the complete whitelist; everything else waits
for a safe outer boundary.

| Operation | Restricted action | Deferred action |
| --- | --- | --- |
| Restriction entry/leave | Checked scope mutation and atomic publication outside the maintenance guard. | Diagnostics and failure/lifecycle processing. |
| Notification fanout | Signal an installed, pinned exact-client subscription. | Erasure, unpinning or package teardown. |
| Incoming POD request/result | Reserve fixed capture storage and correlation, then copy one message. | Ordinary dispatch, terminal routing and any handler with side effects. |
| Incoming owning request/result | Move into a reserved empty stable slot; retain original allocation attribution. | Validation requiring module code, reattribution, processing, rejection/destruction and finalisation. |
| Cancellation/release/quiescence | Record a monotonic pending intent against an existing exact record, or capture the original message. | Router progress, lease release, retirement proof delivery and erasure. |
| Already committed POD reply | Publish only with an existing credit, prevalidated identity, no transport growth and no protected inbox/prefix dependency. | Any reply whose publication would grow storage, change an offered inbox or require a completion handler. |
| Watchdog/exit observation | Checked accounting and sticky diagnostic/stop intent. | Logging, transitions, stop/deactivate/join/unload and owner cleanup. |

Relevant current code makes the exclusions concrete:

- [CPumpedClient::pump](../../host/runtime/pumped_client.cpp) offers copied
  commands/messages before invoking the provider, then validates consumed
  prefixes only after return. General router progress must not run in between.
- [CServiceRouter](../../host/runtime/service_router.cpp) progress can publish
  terminal replies and erase operations/routes; borrow progress can erase
  registrations and unpin packages. Capture intent instead of calling them.
- [CHost::receive_request / receive_message](../../host/runtime/host.cpp) include
  module requests, asset requests/releases, direct connection controls and shutdown handling.
  They are not safe wholesale. Even the POD asset-release path erases an
  acquisition. Existing emergency/debug shutdown helpers also mutate lifecycle
  state or destroy the retained Executive request.
- [CModuleService::complete / finish](../../host/runtime/module_service.cpp) can
  destroy its owning request and arm thread startup. A POD worker result is not
  thereby safe to complete during maintenance.
- [CAssetService](../../host/runtime/asset_service.cpp) completion, release,
  disposal and bundle finalisation can process owners, release backing storage,
  publish dependent work and erase records. Capture the result intact.
- Filesystem scan completion/refresh dispatch can adopt documents, post work and
  clear owning requests. Test completion includes correlation changes and reply
  delivery. Both remain deferred in this increment; extracting an additional
  safe fast path requires a separate explicit whitelist review.

Normal transport operations also need narrow entry points. The current POD
`TQueue` read only copies and advances its reader state, but growable posting can
allocate. Use non-growing preflight and a single Host producer before permitting
reply posting. Owner transport read moves into its destination; overwriting an
occupied destination destroys its old owner. Bypass the package read wrapper's
optional reattribution for restricted capture, and require an empty slot. Do not
introduce a local owning temporary whose destructor runs on an error branch.

## Offered prefixes, bounded storage and backpressure

The outer invocation frame protects every offered operation, request, route,
borrow, relevant client and offered inbox lane until its result is validated.
Maintenance cannot mark offered work consumed, release its reply credit, publish
a terminal result based on assumed consumption, erase its records or reuse its
storage. Existing provider `complete` and borrow callbacks remain authenticated;
they may record only their existing prepared/completed/retirement state. Actual
publication/reclamation waits for normal post-return progress.

In particular, a pumped client's inbox copies controls before ordinary replies.
Appending a new control during the callback can change which physical entries
the eventual consumed prefix denotes. Therefore **stage all new deliveries to
that offered inbox**, including controls, until Host has applied its original
consumed prefix. A control's priority is not permission to reorder that prefix.
Service reply data, descriptor storage and notification pins remain protected
even when a consumer records release during the same callback.

Proposed additional fixed Host storage, allocated before execution:

| Capture class | Capacity | When full |
| --- | ---: | --- |
| Client POD ingress | 128 | Leave the next message in its source transport. |
| Worker POD completion ingress | 128 | Preserve worker backpressure; do not steal request capacity. |
| Client owning ingress | 32 | Leave ownership with the source transport. |
| Worker owning completion ingress | 32 | Leave ownership with the source transport. |
| Independent cleanup controls from existing control lanes | 32 | Leave the next control queued; still observe sticky stop. |
| POD delivery staging for protected inboxes | 64 | Retain the reply/control in its existing record, or refuse new admission before transfer. |

These are approved global Host limits, distinct from the existing 256
ordinary pending commands per pumped module. Each captured entry retains exact
source identity, original correlation, source-stream order and the references
that keep its package, client, operation and module code alive. A staged delivery
also reserves its client's ordinary/control capacity and retains the original
reply credit. Capacity and credit are separate: threaded credit arithmetic alone
does not reserve non-growing transport space. If a reply can remain in its
existing operation record, leave it there instead of occupying a staging slot.
Do not allocate an unbounded parallel queue or consume a credit merely to stage
a message.

Before removing a client request from transport, reserve both a capture slot and
its possible reply credits. When its type is not yet known, use the existing
conservative two-credit reservation. Release surplus exactly once after normal
admission identifies the type. Completion capture uses the operation's existing
credit, not a new request reservation. Unknown/invalid messages still occupy
their capture slot until safe-boundary rejection. Never consume and then silently
drop a message because no storage or credit remains.

This restricted admission transfers responsibility to retain and later inspect
the message; it is not a successful service ACK or promise of semantic acceptance.
The ordinary protocol sends the appropriate ACK/result after safe dispatch. Busy
on a provider post retains the provider's owner. An owner already accepted into
Host storage remains Host's responsibility, including on shutdown or rejection.

Preserve FIFO within each original transport. POD and owning transports do not
gain a new cross-transport ordering guarantee. Replay earlier captured messages
before reading later messages from that same source. Existing independent control
priority is allowed only where its protocol already permits it; maintain request
admission/cancellation correlation so cleanup cannot overtake unadmitted work.
Do not inspect past a blocked head to find a convenient completion.

The bounded deferred queue does not make existing growable source transports
globally bounded. It prevents maintenance itself from growing them or consuming
more owners than it can retain. Completion reserves avoid starvation by newly
captured client work; fixed owning source queues preserve their existing producer
backpressure. Converting all transports is outside this increment.

## Independent stop signalling and safe unwind

Shutdown cannot depend on a spare ordinary request, reply, capture or owning slot.
Propose one sticky atomic stop-request word per exact client execution context,
polled by Host maintenance and normal progress. Publication is a non-owning,
monotonic operation that cannot allocate or fail because a queue is full. Its
storage has the same lifetime as the client's restriction view.

Preserve the existing authority boundary: Executive may request whole-Host stop;
other clients have no such endpoint. Other client failures/self-exit use existing
Host policy. Host-owned fatal conditions and external shutdown also set the Host
sticky stop state. The endpoint does not carry an unload/replace owner or bypass
the protected Executive replacement protocol. The Executive's ordinary shutdown
path should use this endpoint, while replacement remains an ordinary operation.

When stop is observed inside restriction, maintenance records closed admission,
starts/observes the independent awake shutdown grace and reports cancellation to
the current provider. It does not call `begin_stop`, destroy windows, unload code,
join a thread, recurse into a pump or execute an ordinary shutdown handler. Later
native Platform code maps this request to an explicitly supported cooperative
modal cancellation path at a valid native boundary. The portable fixture models
request and unwind without claiming an OS cancellation mechanism.

After the outer call returns:

1. Close the invocation guard and verify scope balance; retain the frame's
   protected references while validating the original result.
2. Validate and apply consumed command/message prefixes. An invalid prefix is
   protocol failure, not permission to guess consumption or release prepared
   resources. Retain ambiguous obligations through existing failure/quiescence
   handling.
3. Revalidate exact requester/provider identities, cancellation, terminal state
   and shutdown before replaying deferred records. A pending stop closes new work
   admission before later ordinary work can start.
4. Replay in source order, accounting each original reply reservation exactly
   once. Cancelled requests receive their protocol-defined outcomes; accepted
   owners are processed or destroyed only now, under valid module context.
5. Run normal completion, borrow retirement and lifecycle progression when their
   respective proofs permit it. Begin another native operation only after this
   safe-boundary work has resolved conflicting intent.

The agreed shutdown grace is **10 seconds of awake time**, including restriction.
Start it on the first accepted shutdown intent and never renew it on another
notification or maintenance pass. Expiry/clock failure records diagnostic failure
and retains reachable resources. It cannot authorise interrupting native code,
pretending a callback returned, unpinning a predicate, freeing an owner or returning
normal successful shutdown with live execution. If no safe maintenance opportunity
occurs, the Host cannot promise timely observation inside the native call.

## Watchdog migration scope

The implementation applies eligible budgets to these explicit logical operations:

| Path | Implemented timing policy |
| --- | --- |
| Host pumped activation in `pumped_client.cpp` | Existing 5-second budget becomes eligible awake time, started once before activation. Progress does not renew it. |
| Executive `TModuleTestWatchdog` | Existing 30-second outer module-test budget becomes eligible awake time because results traverse Host. Check before draining unrelated messages; retain its deterministic clock seam. |
| Executive module load ACK/completion phases | One 30-second eligible budget from successful submission through terminal completion. An ACK does not start a fresh completion allowance. |
| Executive sequential asset scenarios | One 30-second eligible budget per submitted scenario; a new scenario receives a new budget. |
| Executive concurrent saves | One 30-second eligible budget for the finite batch, started with its first successful submission. Partial completions do not renew it; retain correlation for all submitted members. |
| Executive filesystem and bundle exercises | One 30-second eligible budget per named exercise transaction, started with its first submission. Intermediate progress does not renew it. |
| Executive acquisition/release/disposal exercise | Track the 30-second eligible budget of each outstanding correlated operation, including disposal that waits for later releases. A reply for another slot cannot renew it. |
| Executive awaiting system exit | Keep a separate 30-second awake diagnostic budget for the existing outer test harness policy; Host's 10-second shutdown grace remains authoritative for Host diagnostics. Neither timer proves quiescence. |
| Host shutdown grace | Existing 10 seconds, awake time; start when stop intent is first accepted, including inside maintenance. |
| Audio internal test watchdogs | Existing 15-second overall / 2-second progress budgets remain ordinary awake `CWaitDeadline` budgets. Audio continues independently during Host restriction/application pause. |

The performance-counter fallback in
[executive_thread.cpp](../../executive/runtime/executive_thread.cpp) has been
removed from these Host-dependent waits. Budgets are checked at every
operation-loop iteration before unrelated traffic, using validated correlations
for completion. Intermediate replies do not renew them, and a busy inbox cannot
suppress timeout checks. The approved phase/batch policies above define the new
behaviour; no compatibility timing path remains.

Clock/subscription installation failure fails startup/admission explicitly; no
unrestricted timing fallback is installed. Failure while waiting requests the
existing failure/cancellation path while preserving resources and correlations.
After a timeout, continue required late-result/owner cleanup.

The later first-window increment implements a five-second eligible acquisition
budget from command reservation through readiness, without renewing it at native
creation or notification registration. Completed replies retained under output
pressure do not expire. Input/mode/configuration budgets and cooperative-release
diagnostics remain future domain work.
Do not add timers to all worker jobs or convert independent rendering/audio
progress policy merely because Host restriction state is available.

## Acceptance criteria

Documentation review is not evidence that code compiles or that a
native modal loop is safe. The implementation handoff should provide:

- Deterministic clock tests for shared raw-source conversion, rounding and error
  propagation without changing `CWaitDeadline`; unsupported/native failures,
  representability boundaries and arithmetic overflow on 32- and 64-bit builds.
- Snapshot interleavings proving no mixed accepted words, bounded retry, generation
  mismatch, terminal revision exhaustion and no plain-data races. Include a reader
  sampling time between the writer's boundary query and final publication to
  prove the clock-sampling transition prevents eligible-time regression. Nested same and
  different reasons, out-of-order leaves, stale/foreign tokens, capacity limits,
  balanced and failed outer unwind must be exercised.
- Budget tests for start before/during/after restriction, overlapping intervals
  counted once, missed enter/leave notifications, zero/exact boundary/already
  expired budgets, backward clocks, failed start and repeated ordinary wakeups.
  Verify restricted waits do not spin on an expired native deadline.
- Exact-client subscription tests for startup preparation/rollback, pending latch
  and epoch clear/recheck races, coalescing and delayed fanout, threaded/pumped
  consumers, failure, replacement and package-pin refusal. Exercise late ordinary
  service-notification replies without installing them as restriction readers.
- Portable DLL/SO simulated-modal cases entering from activation, pump and stop;
  invalid/deactivate/foreign-thread entry; nested maintenance refusal; 32-operation
  and 1-ms stopping; bounded empty probes; class/client fairness and continuation.
- Hold an outer pump inside the fixture while it offers commands, ordinary replies
  and controls. Capture completion, cancellation and borrow release concurrently.
  Confirm no prefix is consumed/reordered early, prepared views stay alive, no
  registration is erased/unpinned, and valid/invalid return prefixes follow the
  required safe-boundary paths.
- Saturate each capture class, reply credits and source owning queues. Instrument
  owner destruction, allocation/reattribution and module invocation to prove they
  do not occur in maintenance. Verify preserved source order, no dropped owners,
  no duplicate ACKs/credits and independent stop observation under saturation.
- Shutdown with a live provider, pending borrows, deferred owners and worker
  completions; cooperative and refused unwind; awake grace expiry; exact-client
  replacement and actual Host graph retention. Grace expiry must never be the
  cause of reclamation. Verify the migrated Executive paths under a busy inbox
  and synthetic restrictions, while Audio's own timers continue to advance.

Run appropriate Windows and Linux build/policy/regression checks and the DLL/SO
fixtures. Include Win32 layout/atomic checks; report runtime coverage separately
from compile-only evidence. Reuse passing matrices unless changes or review
findings justify repeating them. Real Windows move/resize and window/system-menu
tests, native cancellation, repaint/input behaviour and native callback unwind
remain a later acceptance gate, not a claim from the simulated provider.

## Approved implementation choices

The established defaults remain 32 operations / 1 ms per maintenance pass, nominal
10 ms native opportunities, 5-second activation eligibility, 10-second awake
shutdown grace and independent Audio watchdogs. The user approved all five choices
on 03 Nov 2026:

1. **Client-installed subscriptions.** Use one lifetime-bound subscription per
   executing client, with no asynchronous registration or early unsubscribe. This
   requires predicate-ready startup preparation and an immutable thread execution
   view, but removes acquisition races and the watchdog bootstrap gap.
2. **Opaque reasons and finite storage.** Use 32 provider-local reason slots,
   64 live scopes, four observation attempts and the deferred capacities above.
   These are the approved framework limits for this increment.
3. **Overflow reporting.** Use a new awake-observation overflow error while
   preserving all existing deadline error semantics.
4. **Independent Executive stop endpoint.** Use a permissioned sticky signal
   to whole-Host shutdown; preserve ordinary replacement ownership/protocol.
5. **Executive budget boundaries.** Use the explicit per-operation/batch
   policies above, including removal of intermediate-reply renewal and the
   separate awake awaiting-exit diagnostic.

No additional fast completion handler or native cancellation mechanism is assumed
approved. Any expansion of the whitelist or native scope returns for review.

## Portable implementation and evidence — 03 Nov 2026

The implementation follows the header boundaries above. Host owns the restriction
ledger, fixed deferred stores and subscription lifetimes. Pump callbacks authenticate
the current invocation; ordinary handlers and owner destruction run after return and
prefix validation. Executive uses eligible operation budgets and epoch waits, with
the separate awake exit diagnostic and permissioned stop endpoint.

The new deterministic cases are in
[HostRestriction_test_cases.cpp](../../tests/test_suites/HostRestriction_test_cases.cpp)
and [HostMaintenance_test_cases.cpp](../../tests/test_suites/HostMaintenance_test_cases.cpp).
They cover overlapping scopes, sampling races, concurrent atomic publication,
exhaustion, conversion boundaries, missed notifications, frozen budgets, bounded
maintenance, staging, installed subscriptions and native-join failure retention.
The invalid-prefix test invokes the actual Host pump wrapper, captures an owning
request and a real worker completion inside the held frame, checks retirement
refusal, and checks replay/staging followed by cancellation only after stopped proof.

The DLL/SO fixture adds `modal-normal`, `modal-full`, `modal-shutdown` and
`modal-activation`. The last holds activation restricted for more than five awake
seconds; the eligible activation budget remains valid. The other cases exercise
nested reasons, saturated capture, protected offered prefixes, independent shutdown
signalling and borrowed-resource retirement after consumer quiescence.

Validation artifacts are under `build/host-maintenance-2026-11-03/`:

| Coverage | Result / evidence |
| --- | --- |
| Windows MSVC Debug x64 solution and ordinary suite | Passed; `windows-build.log`, `windows-tests-final-build.log`, `windows-ordinary-isolated.log`. HostPump: 3767 passed, 0 failed. |
| Windows DLL pump fixtures, one and two workers | All 42 cases passed for each worker count; `windows-pump-1.log`, `windows-pump-2.log`. |
| Windows real Executive Host acceptance, one and two workers | Passed; `windows-acceptance-1.log`, `windows-acceptance-2.log`. |
| Linux GCC Debug x64 build, policy/include checks, SO pump fixtures and Host acceptance | Passed; `linux-build.log`, `linux-build-final.log`, `linux-ctest.log`. Pump and acceptance ran with both worker counts. |
| Linux ordinary suite | Initial run failed two later TextureBatch assertions because the new failed-start test reused a Rendering SO whose one-time bootstrap stayed resident after `dlclose`. The test now uses process-qualified private module copies. The focused ordinary rerun passed: `linux-ordinary-final.log`; HostPump: 3767 passed, 0 failed. |
| Windows MSVC Debug Win32 | Build and full ordinary runtime suite passed: `win32-build.log`, `win32-ordinary.log`. HostPump: 3767 passed, 0 failed; this includes runtime atomic/publication and lifetime checks, not just compilation. |
| Repository checks | Source-manifest tests: 7 passed. Policy: 0 errors, 0 warnings, one existing suppressed negative-test diagnostic. Line endings, source opening/final newlines, Visual Studio XML/CRLF/BOM preservation and `git diff --check` passed. |

The Win32 build emitted an existing vendor source-encoding warning in Basis
Universal. Release and Clang matrices were not repeated for this increment.
Native Windows modal loops, cancellation and Platform integration were outside
this portable acceptance; their later evidence is in the first-window reference.
Coordinator source review accepted the portable increment on 03 Nov 2026 with no
outstanding findings, and it was committed as `078b595` after user review.
