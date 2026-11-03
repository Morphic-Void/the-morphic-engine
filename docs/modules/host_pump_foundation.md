# Host pump foundation

The subsequent [service borrow increment](service_borrow_lifetimes.md) adds
prepared views, cancellation and provider-owned resource retirement to this
foundation. Its interface and lifetime rules supersede the exclusions concerning
those capabilities below; the remaining native/modal work is still deferred.

This implements the generic Host foundation for the approved
[Platform design](platform_host_pumped_design.md) and
[contracts](platform_contracts_v1.md). It does not implement a native Platform
module or a Platform service contract.

## Execution and identity

Every installed module gets a stable Host client record, including passive
modules. Dedicated threads attach their real `CThreadPackage` to that record;
pumped modules have no thread package or fabricated thread resources. Host
workers remain separate from clients. Each successful installation gets new,
nonzero process-local instance and client tokens. A mount point does not identify
the requester for outstanding work.

Module, asset, acquisition, filesystem refresh and test dispatch use those client
records. A reply reservation, outstanding operation or accepted provider borrow
keeps its exact requester alive. A terminal requester cancels delivery; a
replacement never inherits its replies or acquisitions. Bundle closure keeps its
reservation for the existing uncorrelated final notification, or cancels it when
the owner becomes terminal.

Loading does not establish persistent loader ownership. An already loaded module
stays independent of the requesting Executive, including across Executive
replacement. Ordinary unload or replacement of a provider with open routes is
busy. Ordinary non-Executive self-unload/replacement and attempts by another
module to perform Executive's special self-transition are rejected.

## Service protocol and execution capabilities

`core/services/service_protocol.hpp` owns the execution-independent service
messages, tokens, contract identity and dispatch context in `services`, together
with their SYSTEM registrations. Exact module instance identity lives in
`core/module/module_identity.hpp` in `modules`. SYSTEM IDs and the 48-byte
request/reply layouts are unchanged by this separation.

`core/module/service_provider.hpp` defines `modules::SServiceProviderV1`, queried
through the optional `morphic_module_service_provider_v1` export. It takes the
caller's descriptor capacity and returns an advertisement with size/version,
zero flags and at most sixteen distinct nonzero contract numbers with nonzero
versions. Contract storage is immutable and remains valid until unbinding.
An absent export means no advertised contracts; an empty valid advertisement
is also permitted. An existing export returning failure or malformed data fails
installation independently of the execution capability.

Host publishes an execution-independent provider record after its supported
execution adapter becomes ready. The router uses that exact client and contract
list, never a pump descriptor or callback. The record is unavailable during stop
and is cleared before its client or binding is released. Currently only
`CPumpedClient` supplies a service execution adapter; advertisement alone does
not enable service dispatch for passive or threaded modules.

`morphic_module_host_pump_v1` is independent of bootstrap v3 and the existing
service-thread export. Discovery happens on the binding worker. The export takes
the caller's descriptor capacity and returns `modules::SHostPumpV1`. Version 1
requires the complete current descriptor, zero flags and all four callbacks.
Service advertisement is separate from this descriptor. Missing
exports mean unsupported; malformed advertisements fail installation. A module
cannot advertise both execution capabilities, and Executive remains threaded.

The Host-thread callback table is `modules::SHostPumpServices`. Pump input
contains bounded service/message batches and a time budget; native event and
observation limits belong to the provider. These same-process capability
descriptors require matching headers and coordinated Host/module rebuilds.

The former `core/system/service_messages.hpp` umbrella is split into module
messages in `modules`, logical filesystem messages in `filesystem`, transport
fallback in `threading`, and common storage type bindings under `system`.
Each message header owns its registrations and owning-payload eligibility.
`system/transported_types.hpp` remains the internal owner-operation aggregate.

Before each lifecycle call, the Host prepares the module-local ambient Host
thread identity, module memory context and pump-context provisioning. The module
borrows that stable context through successful deactivation. Its services table
requires the exact current size/version and exposes queued SYSTEM POD and owning
requests, terminal service completion, and borrow preparation/retirement.
Callbacks authenticate the currently executing pump
on the Host thread; calls from another thread or outside a lifecycle call fail.
Owning request storage transfers only on success and uses existing SYSTEM
ownership and memory-attribution checks.
After stopped, request callbacks return closing, including during deactivation;
deactivation cannot create new work behind the completed drain.

Activation may remain starting across pumps. Only ready makes installation
available. Failed activation with an escaped runtime still receives stop and
cleanup pumps. Failed activation without an escaped runtime can release the
binding. The five-second activation budget requests failure cleanup; it does not
release live state.

## Messages and admission

Service requests and replies are fixed, aligned 48-byte SYSTEM POD envelopes,
including 24 inline payload bytes. They contain no owner, borrowed resource,
function pointer or backend object. Opening a route chooses an exact provider
instance and advertised contract version. Each dispatched command carries that
contract number and version alongside its exact requester identity, outside the
48-byte envelope. Command request IDs are nonzero and strictly
increase within a route. Receipt acknowledgement precedes one terminal result;
it is not provider acceptance. Bad routes, reused IDs, invalid payload padding
and capacity exhaustion produce correlated rejections.

Initial bounds are 64 routes and 256 admitted operations across the Host, 512
ordinary reply reservations/messages and an independently accounted 16 control
reservations/messages per pumped inbox, 256 ordinary outbound POD entries, 32
cleanup POD entries and 64 outbound owning entries per pump. Each POD lane can
also hold one inspected head awaiting admission. Asset releases and zero-reply
test completions use the cleanup lane; releases reserve control reply credit,
while test completions require none. Control replies are offered before ordinary
replies, with consumed prefixes returning the corresponding lane's capacity.
Dedicated threads keep their existing growable transport semantics and do not
inherit the pumped inbox limit. A request reserves
its acknowledgement and terminal reply before the Host removes it from the
outbound transport. The POD ring returns busy without poisoning future posts.
Per turn, the Host offers up to 64 commands and 64 SYSTEM replies and drains up
to 64 messages of each ordinary outbound kind and 16 cleanup messages per client.

Pump input arrays are borrowed for that call only. The provider returns consumed
prefix lengths; the Host retains the suffix. A completion made during a call
cannot be delivered until that command's consumption is confirmed. Invalid
prefixes stop ordinary admission and preserve affected storage through
quiescence. An already produced, consumed terminal result takes precedence over
stop cancellation. Prepared notices, unsolicited service events and borrowed
resource retirement are not advertised by this foundation.

## Retirement

Stop is an out-of-band lifecycle operation, so a full inbox cannot block it.
The Host continues pumps, worker completions and cleanup replies while stopping.
Once Executive is terminal during normal shutdown, all pumped providers receive
stop before the Host waits for outstanding provider operations. Executive
replacement leaves independent providers active.

Only stopped establishes provider quiescence. Deactivation follows drained
outbound queues, retired acquisitions, completed/cancelled requests and worker
borrows. Worker failure first retires every worker and drains its final queue
before cancelling borrowed jobs. Terminal publication is observed before the
queue drain, so a later publication requires another complete drain. Rejected
initial-scan admission leaves no phantom in-flight scan. A ten-second shutdown grace expiry reports
failure and continues progress with resources retained; it never authorises
destruction or unbinding. Clock setup/read failures also report failure explicitly
and continue progress without granting cleanup permission.

If a quiescent provider fails deactivation, the process returns failure and
retains the complete Host graph, native binding, callback storage, module memory
contexts and Host allocation context until process exit. The debug writer and
other native worker threads are stopped first. Retaining only a DLL handle would
leave dangling callback and allocator state.

The process-facing owner is `TProcessLifetime<SHostProcessState>`, with static
storage duration and direct inline construction. `CHostContext` owns the allocator
and Host memory context and installs the ambient context before the embedded
`CHost` constructs its containers. The module service borrows this allocator
explicitly. Normal completion destroys the runtime first, restores the previous
ambient context, and then destroys the context and allocator. Rejected context
installation follows the same destruction order without starting the runtime.

After quiescent failed deactivation, one irreversible retention decision suppresses
destruction of the complete process state. The owner cannot be reconstructed,
moved, released or destroyed after retention. It performs no heap allocation or
placement new; ordinary nested allocations retain their existing curated owners.
The Host-specific placement policy exceptions and separate context-retention flag
are removed. Shutdown deadlines still cannot authorise destruction of live state.

## Validation and remaining scope

`HostPump_test_suite.cpp` exercises envelope layout, route authentication,
correlation, prefix retention, reserved completion capacity, saturation,
requester replacement, independent control credits, multiple contracts and
cancellation after quiescence. `HostRuntime_test_cases.cpp` compiles the actual
Host into the test executable with narrow test-only fault boundaries. It covers
initial-scan rejection, a real native worker failure racing its healthy sibling's
final publication, and shutdown clock setup/read failures with log assertions.
The portable
`tests/host_pump/run_tests.py` harness loads real DLL/SO providers and Executives,
including malformed advertisements, failed activation, pump protocol failure,
Host services from pumped clients, delayed shutdown replies, replacement,
provider reload, saturated inboxes, emergency shutdown and failed deactivation.
Separate advertisement checks cover invalid size, version, flags, counts,
null contract storage, zero versions, duplicate contracts and failed queries;
an empty advertisement still permits pumped execution and orderly retirement.
It also exercises 512 held pumped disposal reservations through cleanup, 600
threaded reservations through releases and self-exit, actual activation deadline
expiry, and live provider progress beyond shutdown grace expiry.
The Windows wrapper builds its dedicated fixture project; CMake registers the
same scenarios with one and two requested Host workers.

Native windows and events, input notifications, GameInput, HID, mapper, Vulkan,
native resource handles, prepared configuration/results, modal restriction and
clock accounting, and sleep/wake integration remain deferred. No native
Platform dependency or extension is installed by this milestone.
