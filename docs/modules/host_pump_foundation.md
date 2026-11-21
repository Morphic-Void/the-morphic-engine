# Host pump foundation

Host gives every module one lifecycle pump and zero or more explicitly requested
service threads. All of those execution contexts use the same asynchronous
endpoint protocol. The selected scope is recorded in
[the first-pass specification](common_endpoint_first_pass.md); later topology,
generic provider registration and bounded-capacity work remain separate.

## Execution and identity

A module instance owns its coordinator, requested threads, binding and shared
module state. Each endpoint has its own nonzero process-local token, distinct
from the module instance, system address and native thread ID. Tokens are never
reused. A replacement cannot inherit an earlier endpoint's replies, acquisitions
or service routes. Host workers remain separate from module endpoints.

`CEndpointResources` owns growable POD transports in both directions, an owning
transport towards Host, notification pins, stop/failure state and a batch client.
Provisioning uses `initialise_growable(0u)` for POD and `initialise(0u)` for owning
messages. `CEndpointContext` supplies identity, read, typed post and seal helpers.
`CThreadContext` adds native waits and progress; `CThreadPackage` owns launch and
join. Pump endpoints have no fabricated native package or wait predicate.

Every module exports `morphic_module_host_pump_v1` and the current bootstrap-v1
contract. Host checks descriptor size, version, flags and all four callbacks.
The advertised service-thread launch interface has been removed. The pump's
activation context contains its endpoint; invocations supply scheduling budgets
and progress metadata. There are no input arrays, consumed-prefix reports,
service callbacks or separate cleanup/control queues.

Host-side publication uses `post`, `reply`, `retry_reply` and `publish_reply`
according to whether an operation needs obligation accounting, retry or an explicit
publication outcome. They share one outgoing POD FIFO. `reserve_replies` and
`cancel_reservation` count logical obligations, not reserved transport slots;
control messages use the same methods. Endpoint execution mode needs no duplicate
classification field, and modules no longer advertise an optional pump flag.

Before module code runs, Host installs the correct ambient module/thread identity,
memory context and provisioning pointer. Native restriction/maintenance callbacks
remain Host-thread authenticated in `SHostExecutionContext`; ordinary endpoint
posts do not execute services inline or reenter another module.

## Requested threads and readiness

`CreateThreadRequest` supplies an entry function, module-owned context, thread
address, priority and optional `designated_service` nomination. That selector
serves existing module-addressed bulk and the Executive stop lookup; POD participation
does not depend on it, and stopping does not depend on bulk readiness. Host authenticates
the submitting endpoint's module instance, checks stopping and duplicate roles,
and retains an attempt before provisioning. Creation success means native launch
and preparation succeeded. Host does not spin waiting for module initialization.

The requester receives a correlated result where deliverable. The owning pump
receives creation, execution-ended and retirement notices, in that order for each
successful child. A request remains Host's obligation after the requester exits;
its entry context must be owned by the module, not by that requester's stack.
Partial preparation failure is joined and cleaned before its failure settles.

The minimal `TServiceThreadPump` waits for both successful creation and an
`EndpointReady` notice naming the exact child. The child publishes readiness after
local initialization. Host authenticates the child and forwards readiness to the
owning coordinator independently of direct participation. Direct enablement uses
its own correlated `Register`/`Registered` exchange, before any resulting offers;
see [direct POD connections](direct_pod_connections.md).
Readiness cannot reverse stopping, failure or retirement. No-child pumps can
report ready immediately. This narrow module-coordination message does not add
public discovery or a general readiness subscription.

Loading does not give the loader persistent ownership. Independent providers
survive Executive replacement. Existing module-request admission and dependent
asset cleanup rules remain in force across all relevant endpoints.

## Services and publication

Optional `morphic_module_service_provider_v1` advertises immutable contracts,
independently of execution. Existing advertised providers bind to the module's
pump. Routes authenticate exact requesting and providing endpoints. The router
retains its bounds of 64 routes and 256 admitted operations.

Host publishes `ServiceCommand` in the provider's incoming FIFO. It names an
immutable descriptor in the operation record, which survives until authenticated
completion or proven final execution drain. Providers use asynchronous complete,
resource-commit, borrow-prepare/retirement and notification-bind messages. Each
has a correlated `ProviderOperationResult`. `CProviderClient` retains the bounded
pending provider exchanges; domain state retains accepted work while waiting.

A queued post means publication, not admission or completion. Post results
distinguish queued, busy, closed, invalid and transport-failed. Definitely failed
owning submissions preserve the caller's envelope. Existing reply counters account
for obligations; they do not reserve physical queue slots. Temporary pressure
uses existing operation storage for retries. Uncertain publication is never
retried as if nothing escaped: descriptors and exposed acquisitions remain
protected through completion or quiescence.

Host drains up to 64 POD and 64 owning messages per endpoint per turn, and dispatches
up to 64 service commands per provider. Pump consumers also yield within their
budget. There is FIFO within each lane and no ordering guarantee across POD and
owning lanes. Prepared views, terminal results and borrow controls share the same
incoming stream. [Borrow lifetimes](service_borrow_lifetimes.md) and
[notifications](service_notifications.md) define their independent resource proof.

## Closure and retirement

Module stop and fatal state are durable; recording either does not require inbox
space. Host keeps progressing pumps, requested threads, workers and settlement.
A provider closes admission and drains accepted work that needs fresh requests
before sealing. `SealEndpoint` records both outbound publication boundaries;
Host acknowledges after it accounts for the complete prefixes. Later submissions
are limited to typed settlement of existing obligations, with normal Host-side
identity and resource validation. A generic cleanup flag grants no authority.

Native execution return is recorded even when module code omits its final progress
update. Host observes return, joins, and drains final submissions before endpoint
retirement. A progress-state exit alone is insufficient. Batch workers, notification
publishers, direct connections, borrows and acquisitions remain part of the lifetime
proof. A stopped pump receives no further invocations; deactivation performs only
local destruction after the required drains and cleanup are complete.

The ordinary ten-second grace expiry logs failure and retains live state while
progress continues. Fatal transport or lifecycle failure starts a separate hard
ten-second deadline at the first observation. New activity does not extend it.
Failure to establish/read that clock, unsafe progress or expiry terminates the
process without unwinding live Host state. There is no watchdog that can interrupt
a pump stuck inside module/native code.

Quiescent execution with a failed destructor/unbind path uses the existing
`TProcessLifetime<SHostProcessState>` retention mechanism. It retains the complete
Host graph, binding, callbacks and allocator context until process exit. Live
execution instead requires process termination; neither timeout nor a stop request
proves quiescence.

Validated child readiness and direct enablement each take effect while Host drains
the child's FIFO. Neither declaration substitutes for the other or for per-connection consent.
Native preparation records the creation outcome only. Individual stop, sealing,
closed admission or module stopping cannot be reversed by a delayed readiness
message; the module coordinator still determines overall module readiness.

Ordinary module stop leaves child shutdown ordering to the pump. The minimal
single-child pump explicitly stops its child after creation succeeds and waits
for retirement. Emergency shutdown or coordinator failure may request all child
stops. A terminal endpoint with unconsumed batch responses is a lifecycle failure:
its resources stay retained while the fatal deadline governs shutdown.

## Validation entry points

`MorphicTests --host-endpoints` exercises the common router, endpoint lifecycle,
maintenance, direct connections, borrowed resources, Platform windows, reports
and input. `HostMaintenance_test_cases.cpp` covers native preparation failure,
requester departure, multiple children, buffered registration/readiness and both
seal lanes. `tests/host_pump/run_tests.py` and the module-lifecycle fixtures exercise
real DLLs and subprocess failures. Validation results are reported separately;
the existence of a scenario is not a claim that every platform has passed it.

Related contracts: [module lifecycle](asynchronous_module_lifecycle.md),
[Host waiting](host_timed_wait.md), [restriction and maintenance](host_restriction_maintenance.md),
[direct connections](direct_pod_connections.md), [Platform report](platform_report.md),
[window service](platform_first_window.md), [input configuration](input_configuration_installation.md).

During Executive startup or replacement, ordinary module requests may arrive on
the owning lane before Host consumes the readiness outcome. Host retains these in
the existing bounded maintenance owner slots, pins their exact source endpoint,
and leaves their owning ingress unaccounted until replay. Later owning messages
from that source cannot overtake them; POD traffic and other endpoints continue.
Once the bootstrap outcome is consumed, normal module-service admission applies.
Stop, failure or requester exit rejects the held work and settles its credits.
An early Executive self-change uses the existing single lifecycle owner slot;
duplicates cannot overwrite it, and its stop side effect waits for running.

This sequencing supports the built-in Executive, which submits its initial load
before readiness but does not wait for that result before publishing readiness.
It does not resolve a dependency cycle in which initialization waits for a nested
module load before it can become ready. The single module-operation service and
existing startup failure/deadline policy remain in force.

## Validation on 20 November 2026

The common endpoint implementation and all affected consumers were rebuilt with
the final version-1 contracts. Export inspection passed for all 16 module DLLs in
each configuration: the bootstrap is version 1 and no replaced advertised-thread
or higher-version pump/bootstrap export remains. The obsolete MissingThread
fixture outputs were removed and replaced by current MissingPump fixtures.

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
