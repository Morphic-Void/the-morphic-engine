Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   platform_contracts_v1.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   02 Nov 2026

# Platform first-stage contracts

## Scope and status

The original contract pass for the [Platform design](platform_host_pumped_design.md)
was reviewed against `07064fc`. As of `f55d514` (4 November 2026), the
[Host foundation](host_pump_foundation.md), [borrow protocol](service_borrow_lifetimes.md),
[notifications](service_notifications.md), [restriction/maintenance](host_restriction_maintenance.md)
and [first Windows window](platform_first_window.md) are implemented and committed.
Those references and current headers define the implemented APIs and validation
limits. This document retains the broader design starting point, with current
window commands separated from prospective configuration/mode contracts.
The 6 November [portable input contract](input_connections.md) supersedes this
document's prospective input mechanics, including concrete commands, readiness,
ordering, recovery and the designated-reader restriction. The input transport is
implemented. Portable configuration installation is described in
[input configuration installation](input_configuration_installation.md).
The [portable mapper](input_mapping.md) and connection integration were implemented
8 November 2026, using input service number 2/version 2 and unchanged record
framing. [Desktop acquisition](desktop_input.md) subsequently adds Windows input,
portable ingestion and capture using input service/endpoint version 3.

Generic borrow release/quiescence replaces the original domain release messages;
Platform retains readiness, stream-barrier and native cleanup responsibilities.
There is no backward-compatibility promise for these unfinished interfaces: Host,
modules and fixtures require matching headers and coordinated rebuilds.

The next scope review joins input lifetimes/transports with direct POD connections
between module service threads. The agreed
[general direct-message architecture](platform_host_pumped_design.md#host-owned-duplex-connections-for-general-pod-messaging)
uses Host-owned duplex connections: two bounded SPSC pipes under one identity and
coordinated lifetime, initiated by either participant through Host. Every new offer
requires an explicit receiving service-thread accept/reject response; advertised
contract support does not replace it. Host coordinates complementary endpoint and
notification installation, then authorises activation only after both participants
report readiness. Ordinary POD traffic and
wakes flow directly. Input/HID retains Platform-owned producer streams, with grouped
desktop text/control and an independent controller connection.

The logical key is the unordered pair of exact module instances plus the shared
contract identity, formed from the canonical unordered pair of participating roles.
Reuse existing role identities rather than introduce an independently named protocol
catalogue, with one connection per pair/contract and no channel ID initially.
Version is a checked establishment parameter, not a separate key. Each lifetime
has a fresh, nonreused Host token, distinct from each initiator's request correlation.
The canonical design specifies reciprocal convergence, active/closing outcomes,
authenticated controls and retirement; acceptance and identity rules are settled.

The agreed [closing policy](platform_host_pumped_design.md#closing-cancellation-and-deferred-completion)
cancels known queued/reply-waiting connection work and requests cooperative
cancellation of running work. Work that safely finishes without the pipe returns
its expected lifetime token to the owning service thread for validation before a
pipe-dependent continuation. Token-only work may outlive the pipe; endpoint and
notification references must quiesce before reclamation. Existing job/module/context
lifetimes still govern completion delivery; the token keeps neither the destination
nor its DLL alive. Graceful completion occurs
while active; unread POD records may be reclaimed after quiescence, with separately
protected resource obligations still requiring cleanup.

The [shared Framework project](platform_host_pumped_design.md#shared-framework-contracts),
`MorphicFramework`, now contains Platform's public window contract and published
state for Host and module consumers. Framework may use Core; Core does not depend on
Framework and retains its current include boundary. Core-consumed definitions stay
there until safely separable. Registration and dependency review precedes further moves;
broader Core decomposition is separate work, and module-private messages stay local.

General direct pipes reuse the existing 64-byte `CErasedPodMsg`, with its 16-byte
header, 48-byte payload, type identity and `async_slot`; no new per-record wrapper
is selected. This does not change the routed-service command payload described
below or Platform's typed input records. The [direct admission rules](platform_host_pumped_design.md#direct-pod-records-and-admission)
require SYSTEM identities and agreed contract/version/direction checks; the carrier
alone does not supply them.

Queue capacities, concrete role/version representation and admission APIs remain
open, as do control byte layouts and APIs.
Minimal Host-mediated connection control versus a
broader threaded provider adapter also remains an implementation-design choice,
not a selected prerequisite.
Owning transfers stay via Host. No new endpoint ABI or implementation is defined here.

Keep the existing Rendering load/unload semantics. Host owns module records;
there is no persistent original-loader ownership. Window control, input connections,
asset acquisitions and request lifetimes remain distinct.

## Original framework assessment, before implementation

The following table records the `07064fc` baseline and the changes it motivated;
its absence statements are historical. Client records, generic request/test
routing, pump preparation, checked clocks and borrow notifications are now
implemented. Worker configuration preparation and direct input connections remain
future work, as detailed in the current references above.

| Existing code | Consequence |
| --- | --- |
| [Binding ABI](../../core/module/module_binding.hpp), [binding preparation](../../core/module/bound_module.cpp) | Bootstrap v3 has the required thread identity, provisioning and memory-context setters. Add an optional pumped export and Host-call preparation helper; do not extend `SCoreFunctions` or fabricate `CThreadResources`. |
| [Module service](../../host/runtime/module_service.hpp) | Requester state is per-operation; record readiness is currently tied to optional thread startup. Add pumped activation/stop states and module-instance identities. |
| [Host routing and shutdown](../../host/runtime/host.cpp) | Ordinary module requests currently enter through Executive; other service owners are sent to asset handling. Generalise requester routing and replace blocking join-before-progress cleanup for pumped dependencies. |
| [Asset client state](../../host/runtime/asset_service.hpp) | Operations/acquisitions retain `CThreadPackage*`. A genuine pumped client needs a stable Host client record and generic reply delivery. |
| [POD messages](../../core/threading/messages/CErasedPodMsg.hpp) | Inline payload capacity is 48 bytes, aligned to at most 16 bytes. New generic transport messages must fit; large descriptors are lifetime-protected borrowed views. |
| [Identity admission](../../core/system/erased_transport_admission.cpp) | Cross-component LOCAL identities are rejected. Platform commands need a registered generic SYSTEM envelope with provider-scoped opcodes, not relaxed LOCAL admission. |
| [Typed ring](../../core/threading/transports/TRingTransport.hpp) | `TRing<T>` provides fixed SPSC storage and role endpoints. Its occupied-count ledger uses every slot; requests of 256/64 already produce those usable capacities. |
| [Thread wakeup](../../core/threading/CThreadPackage.hpp) | `post()` and registered external notification endpoints wake the same package predicate. `observe_epoch()` supports the clear/recheck/wait protocol without posting a dummy message for every input event. |
| [Deadline clock](../../core/platform/threading/wait_word.cpp) | The awake clock query is private; the performance counter is not its substitute. Expose a checked awake-clock observation for pause accounting while preserving existing wait semantics. |
| [Schema aggregate](../../core/schema/schema.hpp), [layout generation](../schema/runtime-api.md) | Existing validation/materialisation and generated declarations suffice. Foreign-key and binding checks belong to Platform. Physical layouts must be checked, not inferred from C++ casts. |

## Identity and Host client records

Use distinct wrapper types around nonzero `u64` values for module instances, service
routes, requests, windows, window uses, input connections, devices, installations
and notification registrations. Host issues module-instance/client/route identities;
Platform issues its resource identities. Issue monotonically within the provider
lifetime, never recycle a live/stale token, and fail admission before wrap. A resource
reference is interpreted with its provider route/instance; a module catalogue ID
alone never validates an old resource. Local configuration IDs remain `u32`.

Introduce an address-stable Host-private `SClientRecord`: instance token, module ID,
execution kind, phase, optional thread-package pointer, reply destination, accepted
operation count, notification registrations, acquisitions and dependency edges.
Workers remain Host workers, not module service clients. A pumped client has no
thread package. Register it before activation for its own lifecycle/asset work;
admit external clients only after the provider is ready.

Migrate requester references in module, asset, test and connection services to this
record. Thread clients adapt through their existing transports; pumped clients
receive queued SYSTEM messages in the pump input. Preserve asset acquisition
ownership by exact client instance, separate from module load semantics. Join/stop
does not destroy the record until accepted replies, acquisitions and notification
publishers have retired. Failed recipient delivery enters explicit cancellation/
cleanup; it cannot silently release an escaped view.

Only Executive itself or Host internal lifecycle may invoke Executive's special
self-unload/replacement path. Other module clients receive invalid-request for that
special target. Ordinary module requests use the same generic service regardless
of which module submitted them. Keep one pending module transition, existing
acknowledged/completed notices, `busy`, `already_loaded`, and unload-before-replace.
Add `require_host_pump` alongside `require_service_thread`; reject both requirements
or both execution advertisements in v1. Passive modules remain valid when neither
is required. Availability follows execution readiness, not DLL binding alone.

## Optional pumped capability and Host services

Export `morphic_module_host_pump_v1`, queried by `CBoundModule::query_host_pump` on
the binding worker. Its query accepts the caller's descriptor size and returns
`EBindingResult`; absent export means unsupported, malformed advertisement fails
admission. Querying advertises capabilities only: no windows, device initialisation
or thread-affine native work on the binding worker.

Use a standard-layout `SHostPumpV1` containing `u32 size`, `u32 version`, flags,
and four function pointers. Independently query `morphic_module_service_provider_v1`
for `modules::SServiceProviderV1`: size/version, flags, contract count and a borrowed
immutable contract-descriptor array. The advertisement describes offered services;
the pump descriptor describes execution. The router consumes a Host provider record
and has no dependency on pump callbacks. Version is 1; reserved bits are zero.
All calls use `MV_STD_ABI_CALL`,
`noexcept`, explicit status and pointer/count validation. No STL containers or
owning C++ objects occur in the advertised descriptor.

These are same-process, same-architecture contracts built from matching headers.
Native pointer fields/descriptors are not file formats, cross-bitness messages or
promises that independently built compiler runtimes interoperate. Fixed input and
envelope sizes below still require static assertions for each supported compiler.

| Entry | Logical signature and result |
| --- | --- |
| `activate` | `(const SHostPumpContext*, void** runtime, SProgress*) -> ECallStatus`; runtime is provider-owned and may be non-null on partial failure. |
| `pump` | `(void* runtime, const SPumpInput*, SPumpResult*) -> ECallStatus`; processes accepted command/completion prefixes and bounded native/lifecycle work. |
| `begin_stop` | `(void* runtime, EStopReason, SProgress*) -> ECallStatus`; idempotent for an existing stop; closes ordinary admission. |
| `deactivate` | `(void* runtime) -> ECallStatus`; releases final provider state after stopped/quiescent. Failure retains the binding/state rather than pretending teardown succeeded. |

`SProgress` carries phase (`starting=0`, `ready`, `stopping`, `stopped`, `failed`),
an error domain/code and work-pending flags. `ECallStatus` describes invocation
validity/failure, not a particular command result. A failed phase can still require
cleanup pumping; failure is not synonymous with stopped. Partial activation cleanup
uses the same stop/pump/deactivate sequence for any returned runtime. A null runtime
on failure means no provider state escaped.

Use `ECallStatus` success=0/invalid_argument/unsupported_version/failed, and
`EStopReason` requested=0/replacement/shutdown/activation_failure/pump_failure.
If deactivation returns failure, provider runtime state must remain valid for the
declared failure-cleanup path; it must not return a dangling runtime pointer.

`SHostPumpContext` is a stable Host-owned record: size/version, module-instance and
client tokens, Host thread ID, service-table pointer/context and an execution-context
pointer providing restriction/stop access. `SPumpInput` contains borrowed arrays
of dispatched service commands and Host SYSTEM messages plus a generic time budget.
Native event and observation limits
are provider policy. `SPumpResult` reports consumed
prefix counts, lifecycle progress and remaining work. Reject counts beyond supplied
lengths. Host retains unconsumed entries; Platform copies information needed for
pending operations and never retains pointers into a batch. Stop controls and release
acknowledgements have reserved admission separate from ordinary capacity.

SYSTEM messages include asset completions and service lifecycle controls.
Module tests between Executive, Rendering and Audio use the separate Framework
[direct service-thread contract](direct_pod_connections.md); they do not pass
through the pumped SYSTEM lane. Platform shell fixtures use unsupported service
commands, retaining their completions through pressure and stopping. Native
acceptance uses explicit external harnesses. Headless fixtures do not prove
window/controller behaviour. No Platform-specific test request is added to Core.

The Host-thread pump callback table (`modules::SHostPumpServices`) supplies
queued `post_pod`, `post_owner`, terminal `complete`, `prepare_borrow`,
`begin_borrow_retirement`, `finish_borrow_retirement`, `bind_borrow_notification`,
`enter_restriction`, `leave_restriction` and `maintain_host`. It requires the exact
current size/version and coordinated Host/module rebuilds. Worker preparation via
`submit_work` remains a proposal. There is no `emit_event` callback: window changes
use durable publication and borrow-bound notifications.
Callbacks authenticate the context/client supplied by Host, not a sender field in
the payload. They enqueue or perform whitelisted bookkeeping; they never recursively
pump another module. `post_owner` is solely module-to-Host: on accepted submission
Host moves the owning SYSTEM message, otherwise the caller retains it. Apply the
existing erased-type/attribution checks. No owning Host-to-module or module-to-module
transfer is introduced.

The proposed `submit_work` would provide a narrow Host-worker job: provider binding
lease, borrowed state, provider entry and completion correlation. Pin code, state and memory context through
completion; run only thread-independent work. Reuse the existing worker dispatcher
and module batch-context preparation, with no new Platform service thread. Return
completion through the pumped client's inbox. This is needed for configuration
materialisation, which must not block the native pump for an arbitrarily large parse.

`CBoundModule::prepare_host_call(host_thread, provisioning)` uses existing
installers with a stable `SHostPumpContext` as provisioning and a null thread memory
override, giving allocations the module memory context. Native callbacks on that
same Host thread validate the retained context. Module-local TLS is separate in
each DLL; do not change Host's ambient module ID to impersonate Platform. Any worker
entry prepares its own provider thread/memory context. No native dispatch occurs
while a Host/provider record or publication is only partly updated.

## Generic command transport within 48 bytes

The existing generic service-open/request/reply/control SYSTEM message types remain
registered in Core because current Core execution and borrow interfaces consume
them. Platform defines its contract and opcode enums, with public window declarations
in Framework. Contract numbers are
scoped by provider module and major version; Platform contract 1/version 1 names
the first window/input service. Service-provider discovery advertises supported
contracts independently of execution discovery. The execution-independent envelope
and dispatch types live in `services` in `core/services/service_protocol.hpp`.

`OpenServiceRequest` carries target module, contract number/version and request ID.
Host returns a route token after validating current instance/readiness/version.
Opening does not load Platform, acquire a window or claim input. Closing the route
requires its outstanding resources/operations to retire; it never implies unload.

The ordinary request envelope is exactly 48 bytes:

| Field | Bytes |
| --- | ---: |
| Route token | 8 |
| Request ID | 8 |
| Opcode, flags, payload size, reserved (`u16` each) | 8 |
| Inline payload | 24 |

The reply substitutes opcode/notice/status/payload-size (`u16` each) in the middle
eight bytes. Acknowledged, prepared and completed notices are implemented. The
enum also contains `event`, but unsolicited-event routing is not implemented and
request ID zero is not a valid ordinary request. Host maps to the existing async slot as
well as checking route/client/request ID, preventing slot reuse from accepting an
old reply. Unused bytes are zero. Copy with `memcpy` into checked POD types rather
than treating an unaligned byte array as a live object. Assert envelope size/alignment
and `CErasedPodMsg::is_payload_compatible_with` on all build targets.

Generic service statuses are success=0, invalid_request, unavailable, unsupported,
busy, occupied, already_connected, closing, stale, timed_out, cancelled,
allocation_failed, native_failure, protocol_failure and delivery_failed, in that
order. Error payloads carry a domain/native code and bounded field/definition locator;
long explanations go to diagnostics. Existing module/asset/test result enums remain
their own contracts; do not replace them with this new service enum.

Host authenticates origin from the receiving client record, stamps the dispatched
command with that instance, checks route phase and reserves operation/completion
storage before accepting it. Provider completions are accepted only for
registered operations. Generic Host code never interprets window/HID
opcodes. No arbitrary LOCAL identity becomes cross-module admissible.

An accepted request has one terminal result; prepared/progress notices do not end
its lifetime. Cancellation/timeout resolves the client's wait, but keeps a tombstone
and all outstanding provider borrows until late work has drained. A late success
that would create an orphan resource triggers provider cleanup, not publication to
a new client. Completion backpressure retains the operation; no unbounded reply
queue or dropped completion. Rejection uses reserved control/result capacity.

## Platform commands, views and authority

### Implemented window contract

Contract 1/version 1 is defined in
[`framework/platform/window.hpp`](../../framework/platform/window.hpp), namespace
`platform::windows`. The [first-window reference](platform_first_window.md) defines
its fixed defaults and lifecycle. These are the implemented provider-scoped opcodes:

| Command | Opcode | Payload |
| --- | ---: | --- |
| `create_window` | 16 | Empty; no configuration, dimensions or policy payload. |
| `acquire_window` | 17 | `SWindowRequest`: window token. |
| `request_close` | 19 | `SWindowRequest`: window token; any holder may request close intent. |
| `answer_close` | 20 | `SAnswerClose`: window, close-request ID, accept value and zero reserved field; requires controller authority. |
| `window_ready` | 22 | `SWindowReady`: window, generic borrow and notification registration. |

Create/acquire publish a provider-owned `SWindowViewV1` through the generic prepared
borrow reply. After notification binding, readiness completes the acquisition.
Release and quiescence use `ReleaseServiceBorrow` and `ServiceBorrowQuiescent`.
There is no opcode 18 release-window-use, nor duplicate input release/quiescence
opcodes 36/37 to implement. These gaps do not preserve an older supported ABI.

Window changes, close intent, closing and failure are durable `SWindowSnapshot`
state, signalled through borrow-bound registrations. The original proposed window
event codes are superseded by this publication; Host has no unsolicited-event
dispatcher. Consumers stop all reads and destroy dependent graphics/native objects
before acknowledging generic quiescence. `execution_ended` proves CPU inactivity,
not external resource cleanup; published native uses can remain unresolved.

### Prospective configuration, input and mode contracts

The following opcodes/layouts are design starting points, not implemented API:
install=1, retire=2, set-mode=21; acquire-desktop=32, acquire-controller=33,
input-ready=34, boundary-ready=35, select-context=38, enable-text=39, disable-text=40,
set-capture=41, release-capture=42, enumerate-device=48 and assign-device=49.
Their domain controls must build on generic borrow lifetime operations. Unknown
commands are rejected. Each proposed payload must fit 24 bytes; larger results use
a lifetime-protected immutable descriptor.

| Commands | Essential payload / result |
| --- | --- |
| `InstallConfiguration`, `RetireConfiguration` | Source asset ID and format version; or installation token. Result token or structured error. Source must remain retained until Platform has acquired it or installation terminates. |
| `SetMode` | Window token and requested mode. Completion/event carries confirmed state and native error. |
| `AcquireDesktop` | Window token, installation token, profile ID, initial context ID. |
| `AcquireController` | Installation token, requested device token (zero means default), profile ID, context ID. |
| `InputReady`, `BoundaryReady` | Connection token plus the correlated acquisition/transition or observed generations. Generic borrow cleanup remains possible while ordinary admission closes. |
| `SelectContext`, `EnableText`, `DisableText` | Connection token, context/text-target identity and transition correlation, split into separately correlated controls where necessary to fit 24 bytes. No caller memory pointers. |
| `SetCapture`, `ReleaseCapture` | Desktop connection token and gesture correlation; completion and capture-lost event. |
| `EnumerateDevice`, `AssignDevice` | Opaque revision/cursor for enumeration; connection/device tokens for assignment. Return one bounded device record per query, with stale-cursor retry rather than borrowed mutable lists. |

For text enable, use connection token, current desktop transition ID and text-target
ID (three `u64`). A context selection uses connection token and local context ID;
its request ID is the transition ID. Boundary acknowledgements use connection token
and both observed stream generations; Host/provider look up the current transition.
Text-only target transitions are serialised separately from a pending desktop
transition. Capture/closure/device events include their resource token and source
sequence; consumers do not infer identity from notification order alone.

The original design proposed event codes 0x8001 through 0x8005 for window changes,
close intent, closing, capture loss and device changes. None is implemented. Window
state now uses the durable publication above; capture uses the durable input
publication described in [desktop acquisition](desktop_input.md). Device notification
details remain for input review. No future notification may silently discard a resource
withdrawal obligation under delivery pressure.

Native views are tagged, versioned borrowed descriptors containing window ID,
Windows window/instance handles, drawable/logical extent state and its revision.
Keep Windows types out of Core. Stable identity/handle fields are immutable while
the use exists; changing extent/state uses a coherent snapshot. Renderers release
all graphics objects before asserting `ServiceBorrowQuiescent`. A descriptor is not an ownership
transfer and cannot outlive its use. Future Linux fields require a new supported
descriptor variant, not reinterpretation of a Windows pointer.

Input prepared results return a connection token and borrowed `SInputEndpointV3`
pointer plus size/version; check it fits the envelope on x86/x64. The descriptor
contains only endpoint/snapshot pointers, capacities, clock-domain information and
tokens. Platform retains it until retirement. Consumer code constructs/uses Core
`TRingConsumerEndpoint` wrappers; it does not call Platform code from its worker
thread to read input and cannot initialise/deallocate/reset the ring. Desktop text
and control readers initially belong to the same designated consumer thread, which
also coordinates both markers. The controller reader may be another thread/module.

Host issues notification registrations for validated consumer execution records.
`InputReady` carries connection token, registration token and acquisition request
ID (24 bytes). The consumer must drain prepared endpoints and acknowledge initial
boundaries while acquisition is pending; waiting only for terminal acquisition
success without servicing them would deadlock. Framework client helpers should
make that prepared/ready/boundary sequence explicit.

## Input records and publication

This section describes proposed input layouts and barriers. The generic lifetime
and notification foundations are implemented; these input endpoints are not.

Use Platform-owned typed rings, independent of the 48-byte erased-message admission.
Initial records are explicitly aligned to 8 bytes and trivially copyable:

| Record | Layout |
| --- | --- |
| `SActionInputV1` (48 bytes) | `u64 sequence`, `u64 time_ns`, `u32 subject`, `u16 kind`, `u16 flags`, 24-byte payload. |
| `STextInputV1` (40 bytes) | `u64 sequence`, `u64 time_ns`, `u16 kind`, `u16 byte_count`, `u32 reserved`, 16-byte payload. |

Action kinds are digital, scalar, vector, relative delta, pulse, pointer position,
button, wheel, modifiers and boundary. The subject is an action ID or source code
according to kind. Ordinary values use `f32` pairs and explicit modifier/flag fields;
zero unused bytes. A boundary payload contains stream generation (`u64`), transition
ID (`u64`), context (`u32`) and reason bits (`u32`). Cancellation is distinct from
normal release. Pointer/button coordinates and modifier snapshots fit within the
24-byte payload; do not derive past modifiers from current state.

Text records initially contain one complete UTF-8 scalar (1-4 bytes); the larger
payload permits a boundary containing generation and transition IDs. Text has no
per-character generation field. An action vector is one record, not a mandatory
record per axis. The earlier two-record expansion factor remains a sizing allowance.
At 256/256/64 slots these layouts use 27,136 bytes of data storage across all three
streams, excluding rings, descriptors and control state. Confirm compiler layouts.

Desktop's two streams share one publication sequence; the controller connection
has its own sequence. Loss counters and generations report discarded observations.
Do not infer the
number of missing physical inputs from an action sequence gap. Time domain is
declared by the endpoint; boundary timestamps use the same domain or an explicit
no-source-time flag. Shared Host timing is never silently mixed with native time.

Platform is sole writer of each control snapshot. Use a sequence-validated snapshot
whose revision and every shared payload word are atomic `u32`; split `u64` fields
into atomic words. Initially use sequentially consistent accesses for a simple
portable C++17 proof. Writer marks odd revision, stores fields, publishes even;
reader accepts matching even revisions. No non-atomic payload seqlock reads. Limit
reader retries and retry later on contention rather than spinning unboundedly.
No native call, logging callback or notification runs inside an incomplete write.
Retire before revision/generation wrap rather than allowing an ABA snapshot.

Snapshot fields include connection phase, context/transition, text enabled/target,
per-stream generation, pending reasons, cumulative loss and terminal error. Payload
queues keep their existing acquire/release ledger; snapshot atomic policy does not
change `TRing`. Ordinary reads check control state before and after copying a record
and again before applying a locally buffered batch. A concurrent generation change
invalidates unapplied data; input already applied before a later change is cancelled
at the next observation, not retroactively undone.

Require lock-free atomic `u32` on supported native targets and verify that assumption
at compile time. Do not cast the snapshot to a plain POD view to bypass its atomic
loads. GameInput callbacks, if used for discovery notifications, only publish a
thread-safe dirty indication into retained callback state; Host pumping remains
the sole writer of mapper state, rings and control snapshots. Unregister and quiesce
vendor callbacks before releasing their context or binding.

### Desktop ordering across the two pipes

Separate storage must not reorder text and editing actions. For example, inserting
`a`, applying Backspace and inserting `b` must not become `ab` because the consumer
drained the action pipe before the text pipe. The common desktop publication sequence
orders records emitted by Platform, including markers; it makes no claim about
physical hardware simultaneity or ordering against the controller connection.

Publish a committed desktop sequence in the coherent snapshot after records have
been posted (or their loss state published). The consumer samples that watermark,
then reads/rechecks both pipes and merges their heads by sequence up to the watermark.
It must not apply a cached head while overlooking an earlier record in the other
pipe. A missing sequence is not awaited blindly: sequence allocation can also
represent suppressed/dropped input covered by durable generation/loss state.
Do not mutate already published records to coalesce them.

The Windows adapter must also preserve native text/editing order before publication.
Do not emit Backspace/Enter twice from both raw key-down and translated control
characters. Resolve character-producing editing keys through the translated path,
and preserve ordering with pending translation before publishing navigation/editing
commands. Character translation is not one-to-one with key messages, so do not infer
its modifiers/source solely from the last key-down; retain explicit translation
state and test queued typing, deletion, native repeat, dead keys and AltGr together.
This is a native acceptance requirement, not permission to delay text indefinitely
waiting for a character that a key will never produce. IME pre-edit remains deferred.

### Barrier and overflow sequence

1. Platform updates mapper/native bookkeeping, stops ordinary publication for the
   affected stream(s), advances generation(s) and publishes durable reason/state.
2. Notify the consumer. Queue fullness does not hide this state. The consumer cancels
   affected actions and drains obsolete records using ordinary ring reads.
3. Platform retries pending boundary markers when space exists. New generations may
   supersede queued old markers; only markers matching the current snapshot count.
4. For a desktop context transition, wait for both matching markers. The consumer
   sends `BoundaryReady` for both generations; Platform validates against current
   state before resuming either ordinary stream. This prevents one stream's new data
   accumulating behind the other's barrier. Text remains disabled until explicitly
   enabled for that transition/target.
5. For text-only flushing, a matching text marker resumes eligible text without
   affecting desktop actions. For action overflow, the matching recovery marker
   permits baseline recovery with neutral inhibition. Initial acquisition uses
   readiness plus boundary acknowledgement before terminal success.

During a pending joint barrier, native state keeps updating but application input
is intentionally suppressed. A rejected/stale acknowledgement changes nothing.
Closure supersedes recovery and needs no data-queue marker to become observable;
reader quiescence, not marker delivery, is the final reclamation condition. Never
mutate the consumer index, overwrite unread slots or clear the ring concurrently.

## Notification and race-free waiting

The Core non-owning external-notification endpoint is backed by the consumer package's
existing `CWaitPredicate`. Host creates and pins its registration; consumers provide
a registration token, not an arbitrary function pointer. Platform can publish input
then signal the endpoint directly on Host without routing an input message through
Host's dispatcher. The endpoint carries no Platform callback and no owning state.

`CThreadContext::observe_epoch()` supplies epoch observation. Waiting follows: sample epoch, drain
messages and input/control state, acknowledge boundaries as needed, recheck durable
conditions, then `wait_for_new_epoch(sampled_epoch)`. If publication happens between
the checks and wait, its epoch change prevents sleep on the old value. Producers
publish first and signal second; an empty queue is not sufficient to sleep while
control is pending. Never sample a new epoch after checking queues and then wait
without rechecking them.

Use a per-registration pending latch to coalesce notifications: publisher signals
only on clear-to-set, consumer clears before its final durable-state recheck. If
publication races with clearing, either the consumer observes durable work or the
publisher signals again. A registration that remains pending while its consumer
is idle is a protocol failure. Keep the existing message/exit predicate notifications
intact; do not introduce a separate indefinite wait that misses message/exit wakes.
For pumped consumers, signal a Host-owned work-pending flag; no Host sleeping API is
implied. Initial Platform connections use one reader thread per logical connection.

Retirement stops the producer and all signal calls before releasing the registration.
The consumer's explicit quiescent acknowledgement, or proven terminal reader state,
precedes destruction. Add package teardown checks so wait-control release is not
mistaken for publisher quiescence. Forced module cleanup on Host cannot leave an
input endpoint holding a pointer into an erased thread package.

The concrete [registration interfaces and retirement ordering](service_notifications.md)
bind one optional registration to each published borrow. The consumer helper tracks
ordinary registration replies so controls that overtake them cannot reopen access
to a retired reader. Pumped pending-work flags are hints; signal failure is explicit.

## Configuration schema v1

Configuration is data described by schema structures, not the live input record
schema. Use supported schema enum labels, ordered members, named instances and
bulk collections. The following fixes the v1 fields; generate declarations from
these definitions and validate their layouts on x64/Win32 and Linux compilers.
Do not use schema non-storage associations as record references.

| Structure | Ordered fields |
| --- | --- |
| `ConfigurationHeader` | `format_version:u32` (1); exactly one named `configuration` instance. |
| `ActionDefinition` | `id:u32`, `kind:ActionKind` (`u32`). |
| `ContextDefinition` | `id:u32`. |
| `ContextAction` | `context:u32`, `action:u32`. |
| `BindingProfile` | `id:u32`, `connection_kind:ConnectionKind` (`u32`). |
| `BindingDefinition` | `id`, `profile`, `context`, `action`, `source`, `parameters` (`u32`); `activation:ActivationKind` (`u32`); `allow_during_text:b8`, `native_repeat:b8`, `reserved:u16` (0). |
| `InputSourceDefinition` | `id:u32`, `kind:SourceKind`, `control:ControlCode`, `direction:Direction` (all enums `u32`). |
| `BindingModifier` | `binding:u32`, `source:u32`; all rows for a binding form its all-of set. |
| `AnalogParameters` | `id:u32`; `dead_zone`, `neutral_enter`, `neutral_leave`, `press`, `release`, `scale` (`f32`). |
| `WindowPolicy` | `id:u32`; identifies a named immutable shortcut policy. |
| `ShortcutDefinition` | `id:u32`, `policy:u32`, `primary_source:u32`, `operation:ShortcutOperation` (`u32`). |
| `ShortcutModifier` | `shortcut:u32`, `source:u32`. |

`ActionKind` preserves `digital_state=0`, `pulse=1`, `scalar_state=2`,
`vector_state=3`, `relative_delta=4`. `ConnectionKind` is desktop=0/controller=1.
`ActivationKind` is regular=0/start_press/release_press/direct/threshold/chord.
`SourceKind` is physical_key=0/mouse_button/mouse_position/mouse_wheel/gamepad_button/
gamepad_stick/gamepad_trigger. `Direction` is whole=0/positive_x/negative_x/
positive_y/negative_y. `ControlCode` is a symbolic enum owned by Platform, with
none=0, explicit key positions/navigation/modifiers and standard gamepad controls;
never store native scan codes or device IDs as configuration controls. The following
fixes the initial code ranges; names refer to physical positions on a reference
keyboard, not translated characters. Write out individual labels in the schema:

| `ControlCode` range | Labels in increasing value order |
| --- | --- |
| 1-26 | `key_a` through `key_z`. |
| 27-36 | `digit_1` through `digit_9`, `digit_0`. |
| 37-41 | `enter`, `escape`, `backspace`, `tab`, `space`. |
| 42-53 | `minus`, `equal`, `left_bracket`, `right_bracket`, `backslash`, `semicolon`, `apostrophe`, `grave`, `comma`, `period`, `slash`, `caps_lock`. |
| 54-65 | `f1` through `f12`. |
| 66-78 | `print_screen`, `scroll_lock`, `pause`, `insert`, `home`, `page_up`, `key_delete`, `end`, `page_down`, `right`, `left`, `down`, `up`. |
| 79-84 | `num_lock`, `keypad_divide`, `keypad_multiply`, `keypad_subtract`, `keypad_add`, `keypad_enter`. |
| 85-96 | `keypad_1` through `keypad_9`, `keypad_0`, `keypad_decimal`, `non_us_backslash`. |
| 97-105 | `left_ctrl`, `left_shift`, `left_alt`, `left_super`, `right_ctrl`, `right_shift`, `right_alt`, `right_super`, `application_menu`. |
| 106-110 | `either_shift`, `either_ctrl`, `either_alt`, `either_super`, `alt_graph`. |
| 128-134 | `mouse_primary`, `mouse_secondary`, `mouse_middle`, `mouse_x1`, `mouse_x2`, `mouse_position`, `mouse_wheel`. |
| 256-273 | `pad_south`, `pad_east`, `pad_west`, `pad_north`, `pad_left_shoulder`, `pad_right_shoulder`, `pad_left_click`, `pad_right_click`, `pad_up`, `pad_down`, `pad_left`, `pad_right`, `pad_menu`, `pad_view`, `pad_left_stick`, `pad_right_stick`, `pad_left_trigger`, `pad_right_trigger`. |

Other values are unsupported in v1, not speculative reserved definitions. Validate
kind/control pairings. A source direction projects the named positive/negative
component for scalar/threshold bindings; whole stick maps to a vector. Native repeat
is permitted only for eligible physical editing-key start-press bindings. A chord
activator requires modifiers and produces held digital state; start/release bindings
can also have the same all-of modifier requirements without changing their pulse kind.

`ShortcutOperation` is toggle_windowed=0/toggle_exclusive. Enum ordinals are
configuration-format values, independent of Core's identity catalogue.

IDs must be unique/nonzero within each definition category. Zero parameter reference
means the built-in default for that source; explicit profiles override it. Named
definitions carry IDs, membership/modifier rows use bulk collections, and names are
retained for diagnostics. Empty relationships are omitted rather than inventing an
empty bulk collection unsupported by the schema. Require exact expected fields/
enum meanings for v1, validate reserved zeros, references and finite numeric ranges.
Reject unknown required records, illegal directions, duplicated modifier rows,
cross-connection sources, mixed pulse/state actions, ambiguous absolute contributors
and unsupported native-repeat activators. Defaults do not excuse incomplete bulk rows.

The scene/menu catalogue and profiles remain as in the main design. A sample binding
row is `(id=1, profile=1, context=1, action=1, source=1, parameters=0,
activation=direct, allow_during_text=false, native_repeat=false, reserved=0)` where
source 1 is the whole left stick. Membership associates scene/action 1 independently.
A Shift+mouse chord has a primary mouse-button source plus a `BindingModifier` row
pointing at the either-Shift source. Non-consuming ordinary bindings still run.

Load configuration as a Host-retained raw UTF-8 asset. The approved
[installation contract](input_configuration_installation.md) supersedes the
original worker-job proposal: a nominated Host/Core conditioner prepares an
acquisition-scoped immutable schema result, and Platform incrementally validates
and copies its own installation tables. Host workers accept no arbitrary module
jobs; batch-runner inline fallback remains unchanged. The linked contract defines
source/result ownership, limits, admission, deadlines, cancellation, durable
commit and retirement.

## Window/input and shutdown sequences

The combined sequence below includes prospective configuration and input steps.
The implemented window-only readiness/close sequence is in the first-window
reference and has no configuration prerequisite.

### Successful startup

Host worker binds/discovers -> Host registers generic client/prepares TLS ->
activate/pump reaches ready -> module load completes -> client opens service route ->
configuration preparation completes -> controller creates window/acquires its use ->
Rendering obtains its separate use/native view if presentation is requested ->
input acquisition reserves endpoints -> prepared descriptor reaches consumer ->
consumer registers reader/notification and sends `InputReady` -> Platform publishes
initial boundaries -> consumer acknowledges them -> acquisition completes.

No window or input connection appears merely because Platform loads. Publication
of a descriptor begins a borrow even before final acquisition success. If prepared
delivery fails with certainty before escape, clean up directly; otherwise retain it
through cancellation/quiescence. Unconfirmed delivery is not proof of non-escape.

### Release and consumer removal

Consumer locally cancels/stops applying input -> requests release -> Platform stops
ordinary publication and signals durable closing -> consumer stops every read and
notification use, then sends `ServiceBorrowQuiescent` -> Host/Platform detach notification
and prove no publisher/callback remains -> provider frees ring/descriptor, releases
configuration/window references and completes release. Terminal consumer removal
substitutes proven reader termination for its acknowledgement, logs omitted release
and performs the same cleanup. A live but unresponsive reader retains resources.

### Window close and unload

OS close intent -> controller decision (unless shutdown/controller departure) ->
window closing, reject acquisitions -> retire desktop connection and notify window
users -> Rendering releases presentation objects/use -> other uses retire -> destroy
native window on Host outside active dispatch. Other windows/independent controller
connections are not implicitly destroyed by a single window close.

Ordinary unload first checks unrelated live routes/uses and returns busy/in-use;
do not stop the provider and only then discover a protected borrower. Global shutdown
closes admission, requests client exit and native-modal cancellation, and continues
safe routing/pumping until clients release their dependencies. Join a thread only
after it is terminal and no accepted completion needs its route. Stop/deactivate
Platform on Host only after conditioning results, endpoints, windows, notifications and native
callbacks are quiescent; then dispose module-dependent assets and worker-unbind.
Same ordering applies after activation/pump failure. The implemented Host emergency
path preserves pumped progress and retains unproven obligations; prospective
input/configuration work must preserve that ordering.

On shutdown grace expiry diagnose and retain unsafe resources/code. Never kill a
thread or free a Host client record while execution can still reach it. Continue
bounded shutdown progress while any live reader/callback needs Host: ten seconds is
a failure threshold, not a safe deadline for returning through destructors. An
unquiescent thread therefore prevents a normal completed shutdown. Once execution
is quiescent, an unload failure can retain the DLL and necessary binding records
until process exit. Audit this path together with package destructors; retaining
a DLL alone cannot protect freed Host callback or wait-predicate storage.

## Pause publication and deadline accounting

This design summary is implemented by the
[restriction and maintenance contract](host_restriction_maintenance.md), including
its precise coherent-sampling, failure, retirement and budget rules. The native
first-window integration also permits authenticated cleanup scopes after stop.

Expose checked `query_awake_nanoseconds(u64&) -> WaitError` by factoring the same
native clock selection/conversion used by `CWaitDeadline`; no performance-counter
fallback and no changes to existing timed/indefinite waits. Preserve explicit native
failure/unsupported/overflow outcomes. Add focused tests for consistency, including
suspend-domain differences, rather than rerunning old matrices for documentation.

Host is sole restriction-snapshot writer. Platform reports reason enter/leave
through its authenticated service callback, including native modal callbacks.
Publish reason mask/nesting, generation, cumulative completed restricted nanoseconds
and active-start time using the atomic-word snapshot protocol above. Snapshot update
does not pump work. Restriction notification uses pinned registered wake endpoints.

Define effective eligible time as `E(t) = awake_now(t) - restricted_total(t)`, where
the total includes the current active interval and counts overlaps once. Store
`E(start)` for a Host-dependent operation and compare `E(now)-E(start)` with its
budget. This clips exclusion to the operation lifetime, including operations starting
inside an existing restriction; expired work stays expired. Observe snapshot/time/
snapshot coherently and retry if publication changed. Independent watchdogs and
shutdown grace use awake time directly. Rebuild a native deadline only for a deliberate
accounting change, not to restart a budget on ordinary wakeups. During restriction,
wait on state/message/exit notifications rather than busy-waiting an expired native
deadline for a paused budget; unrelated deadlines may still bound that wait.

## Review checkpoints and validation

These are the original broader acceptance criteria. Completed foundation,
notification, maintenance and first-window evidence is recorded in their linked
references and the [completion record](../project/completed_milestones.md#host-pumped-services-and-first-windows-window).
Configuration, input, controller and mode criteria remain future work. No runtime
matrix was repeated for this documentation reconciliation.

1. Add generic client/routing fixtures before Platform native code: threaded/passive
   behaviour, requester replacement, per-client asset acquisition ownership, in-flight
   cleanup, already-loaded capabilities and protected Executive self-transitions.
2. Validate the optional pump ABI and every envelope/record size on Windows x64/Win32
   and Linux GCC/Clang. Probe GameInput architecture support before freezing the
   package; the current limited-x86 note is not resolved by this document.
3. Exercise pending activation, failed activation with escaped state, consumed batch
   prefixes, completion backpressure and worker preparation cancellation. No native
   calls occur on the binding worker and no fake thread resources are constructed.
4. Generate/validate configuration declarations, reject layout/version/reference
   mismatches and malformed enum/numeric inputs, run the scene/menu/text fixtures and
   verify parameter domains. Syntax-only JSON parsing is not schema validation.
5. Stress SPSC marker recovery, simultaneous text/control barriers, stale acknowledgements,
   full queues, notification clear/publish races, control-release races and 32-bit
   snapshot reads. Confirm no ordinary payload data races or double consumers.
6. Trace shutdown during modal dispatch, borrowed native views, configuration jobs and
   forced consumer removal. Repeat those paths through emergency cleanup. Prove that
   provider code, Host callbacks and wait predicates all outlive their users.
7. Run existing Windows/Linux build and regression matrices after implementation;
   native window/device tests are separate evidence. Linux runs portable mapper,
   protocol and pumped-fixture coverage; its native window adapter remains deferred.

Remaining input/configuration/mode work requires its own bounded implementation
scope and compile/layout, behavioural and native evidence. Completed stages do not
authorise the later features or establish their validation.
