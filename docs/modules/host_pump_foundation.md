# Host module execution foundation

Current architecture after the 23 November common-preamble consolidation.
Implementation and validation status are recorded in the
[qualification ledger](module_preamble_validation.md); the detailed
[implementation checkpoints](module_preamble_implementation.md) and historical
results below describe their original binaries and contracts.

## Execution boundary

Every module exports `morphic_module_execution_v1` and bootstrap v1. Host checks
exact descriptor size/version, supported flags and a complete create/handle/destroy
callback set. An entirely absent callback set selects the Host standard coordinator
in its supported coordinator role. A partial set is invalid. There is no old pump
export or raw module thread-entry fallback.

A module-owned class holds local configuration, operation state and a bound
`modules::CCommunication` facade. Thin ABI adapters create, invoke and destroy it
on its execution thread. The same model serves pumped and serviced endpoints;
Host owns scheduling, transport consumption, asynchronous service cadence and the
serviced wait loop. Invocation views expire on return. Handler success reports
valid progress for that invocation; a previously latched fatal state stays fatal,
while later successful cleanup callbacks can still advance retirement.

Execution adapters use `modules::TExecutionInstance<T>` from
`framework/module/TExecutionInstance.hpp`. `create(...)` selects the ambient
memory context; `create_in(context, ...)` selects an explicit context. Both
return a null handle on allocation failure and otherwise construct one object
with its allocation token in a single aligned block. Construction and destruction
must be non-throwing. Execution classes do not carry their own allocation token.

The handle identifies the private block, not a `T` pointer. Module callbacks use
`get(handle)` to borrow the object and `destroy(handle)` to consume it exactly
once. Null access returns null and null destruction is harmless. Other handles
must be live and belong to the same template specialisation and originating
module; there is no stale-handle registry or runtime type validation. Construction
arguments are forwarded only after successful allocation. Destruction retains
the original allocation context and backing until the object's destructor and
member cleanup finish, even if another context is currently ambient.

The execution adapter still decides when destruction is safe. It checks domain
completion before calling the helper; refusal retains the handle and DLL under
the existing lifecycle contract. Failed post-construction initialization destroys
the unpublished handle before returning failure. `TInstance` remains the ordinary
scoped/member owner and does not gain release or adoption operations.

The facade contains two pointers: one immutable Host-lifetime function table and
an opaque endpoint cookie. This is the DLL-to-Host boundary, not registration or
discovery. Entries authenticate the cookie against the active invocation before
accessing endpoint state. Host-internal routing uses direct calls. A facade cannot
be used from a foreign thread or batch worker. Cross-image preparation installs
thread identity, provisioning and memory defaults without swapping global module
identity. Host infrastructure and module allocations retain separate attribution.

## Channels, dispatch and pressure

Each endpoint has two private 256-record lifecycle rings and two ordinary
1,024-message POD rings. Direct POD connections also use one fixed 1,024-message
ring per direction. Owning messages towards Host, Host worker queues, batch returns
and bulk buffers retain their specialized transports. Self and Host fallback reply
FIFOs grow only to 1,024 entries. Lifecycle controls are not ordinary payload types.

The facade submits PODs to Host, self or an authenticated connection/peer and owning
messages to Host. Origin is supplied by the bound context. `accepted` and
`published_wake_failed` both mean accepted publication; neither may be resubmitted.
`busy` is definite refusal with caller custody. Invalid/no-pipe/closed and transport
failure remain distinct. Self delivery is bounded, deferred and nonrecursive.

Private lifecycle runs first. Ordinary selection uses pressure with persistent
starvation protection and per-lane FIFO. Direct input may be paused by domain state
without pausing lifecycle, Host completions or asynchronous servicing. Host retains
one ingress head per POD/owning lane, classifies without dispatch, and reserves
fallback reply storage only for the current dispatch. Runnable heads remain
eligible with empty rings; blocked heads are excluded even with data behind them.

Asynchronous results remain in existing operation records. Finite output retry
turns rotate across producers and their operations, then give pending ingress an
admission opportunity. Logical reply credits are separate from fallback storage.
Terminal recipients receive no new fallback publication; cancellation waits for
quiescence. Saturation alone is nonfatal. Accepted-obligation loss is fatal.
Rate-limited pressure/starvation warnings and recovery summaries report stalled
work without changing admission or inventing a new timeout policy.

## Children, services and retirement

`create_child` supplies an execution descriptor, module-owned context, logical
thread, priority and optional designated-service role. Host copies the descriptor
while its creation obligation pins it. Creation, readiness, execution-ended and
retirement are distinct private outcomes delivered as typed handler notifications.
The standard coordinator waits for creation and exact-child readiness, stops its
child on shutdown and waits for retirement. Custom coordinators can order multiple
children. Ordinary stop preserves that ordering; emergency shutdown may stop all.

Optional service-provider v1 advertisements remain independent of execution.
Routes retain exact client/provider identities, 64 route slots and 256 operations.
`ServiceCommand` exposes an immutable retained operation descriptor;
`CProviderClient` uses the common facade for bounded correlated provider exchanges.
Borrow and notification lifetimes remain governed by their existing proofs.

Admission closure and seal messages carry independent POD/owning publication
frontiers. Host accounts consumed or retained prefixes before acknowledging them.
The destruction grant additionally requires direct retirement, provider-borrow
retirement and no active Host invocation. Destroy runs affinely; a serviced context
then returns and must join before endpoint retirement. Acquisitions, staged replies,
batch publishers and notification users remain independent retention obligations.
Unread direct messages are explicitly cancelled by orderly closure, which also
requires dependent conversations to relinquish access.

Fatal failures, including active/pending Executive failure, initiate framework-wide
shutdown. Grace expiry retains live resources while progress continues; the existing
fatal deadline can terminate without unwinding unsafe live state. Only the narrowly
proved pumped sealed destroy-refusal path can return failure while retaining the
whole Host graph after workers/batch runners stop. It never fabricates successful
destruction, endpoint quiescence or DLL unload. Serviced refusal remains conservative.

Startup module requests retain their owning ingress head until readiness allows
admission. This creates no reply reservation and does not block private readiness
or stop processing. It does not resolve an initialization cycle waiting on a nested
load before publishing readiness.

See the [execution design](module_preamble_design.md),
[direct POD](direct_pod_connections.md), [bulk](direct_bulk_connections.md),
[borrow](service_borrow_lifetimes.md), [waiting](host_timed_wait.md) and
[restriction/maintenance](host_restriction_maintenance.md) references.

## Historical validation

The following evidence predates the common-preamble contract. It is preserved as
history, not validation of current sources. Current build provenance and remaining
qualification are tracked in the qualification ledger.

## Validation on 20 November 2026

The common endpoint implementation and all affected consumers were rebuilt with
the final version-1 contracts. Export inspection passed for all 16 module DLLs in
each configuration: the bootstrap is version 1 and no replaced advertised-thread
or higher-version pump/bootstrap export remains. The obsolete MissingThread
fixture outputs were removed and replaced at that checkpoint by MissingPump
fixtures. The later preamble consolidation replaced those with MissingExecution;
this paragraph records only the 20 November state.

| Windows configuration | Solution and consumers | Core tests | DLL lifecycle | Selected pump cases |
| --- | --- | --- | --- | --- |
| x64 Debug | Passed | Passed | 44 passed | All 52 portable cases passed |
| x64 Release | Passed | Passed | 44 passed | 13 passed, one Host worker |
| Win32 Debug | Passed | Passed | 44 passed | 13 passed, one Host worker; native-report limitation below |
| Win32 Release | Passed | Passed | 44 passed | 13 passed, one Host worker |

The full core runs include endpoint creation/departure, both submission lanes,
readiness ordering, exact-instance authentication, pump-ordered child shutdown,
uncertain backing exposure and terminal batch retention. The final test-helper
decode fix was rebuilt in all four configurations; x64 Debug then repeated the
focused endpoint suite. Win32 builds retained existing vendor and unrelated
HostRestriction warnings. Whitespace, source-header and line-ending checks passed.

On x64 Debug, all ten real-input functional cases and the documented 120 Hz smoke
passed with two Host workers. Win32 Release passed input lifecycle, second
consumer, blocked shutdown and source cleanup with one worker. The x64 Debug
native-window suite passed 449 checks and all four real-module move/size/menu/report
cases passed. All three isolated fatal-clock/deadline probes passed with live
execution retained; the deadline probe terminated after 12.138 seconds including
process setup. Physical controller acceptance and Linux qualification were not
performed in this refactor.

The uncontended x64 Release input baseline used one Host worker, four controllers
at 1,000 reports/s each, and tracing. It passed the documented two-second warm-up
and three ten-second measured windows (32.49 seconds total). Every window admitted
40,625 inputs and matched all 80,625 expected outputs, with no source loss,
missing/invalid/stale samples or trace/clock errors. Offered-to-applied p99 was
7.59–7.74 ms across the windows, with a 9.58 ms maximum. The runner's `--seconds`
argument does not change these synthetic measurement windows.

Two qualification gaps remain explicit:

- The previously documented [Win32 Debug native-report deadline failure](platform_report_validation.md)
  reproduced without an active build: 6.363 seconds of synchronous schema work
  exceeded the unchanged five-second capture deadline. Host rejected publication
  and delivered the retirement receipt. The same startup report work blocked this
  run's Win32 Debug real-input lifecycle; the other selected real-input cases were
  not rerun through that failed prerequisite. Native report schema placement is
  separate work, and these cases are not claimed as passing.
- Earlier x64 Debug 1,000 Hz smoke attempts failed history/output checks. This
  refactor does not establish whether that high-rate Debug failure predates the
  change. The documented 120 Hz Debug smoke and 1,000 Hz Release baseline above
  passed with their original acceptance checks unchanged.

Local evidence is retained under `build/common-final-pump-*`,
`build/common-final-input-*` and `build/common-final-*-core.log`. The original x64
Debug full-core log tag is `common-endpoint-final-debug`; its final focused log is
`build/common-final-Debug-x64-endpoints.log`. DLL lifecycle runtime suffixes are
`f88d5fac` (x64 Debug), `5a09ac65` (x64 Release), `cee85acc` (Win32 Debug) and
`ed97f2d7` (Win32 Release). The Release baseline is
`build/common-final-input-Release-x64/load-1-seokgc93/report.json`. The idle native
failure is `build/common-final-pump-Debug-Win32-idle/platform-report-1-gvi9hsok`;
the dependent input failure is
`build/common-final-input-Debug-Win32/lifecycle-1-eluz7qyj`. Earlier high-rate Debug
smoke evidence remains under `build/common-endpoint-input`.

## Consolidation on 21 November 2026

Removed the obsolete control/reply aliases and write-only execution/pump-presence
fields. The window notification-pressure test now fills a single fixed FIFO at
the transport's minimum capacity and verifies refusal of another message before
checking notification and retirement progress. No endpoint protocol changed.

x64 Debug and Release Host/test builds passed with zero warnings or errors. The
focused endpoint suite, including Host, borrow, direct-connection, Platform report,
input and window regressions, passed 63,679 checks in Debug and 63,680 in Release.
Evidence is in `build/common-consolidation-*-build.log` and
`build/common-consolidation-*-tests-final.log`. Line-ending, whitespace and local
documentation-link checks passed. The qualification limits above are unchanged;
native, performance and four-configuration matrices were not repeated for this
cleanup.
