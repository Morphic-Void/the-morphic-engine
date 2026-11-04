<!--
  File:    platform_first_window.md
  Date:    04 Nov 26

  Records the first-window contract, ownership and execution boundaries,
  implementation evidence and deferred Platform scope.
-->

# Platform shell and first Windows window

Implementation contract following `078b595`, 4 November 2026. The user approved
this increment in the coordinating chat. This document fixes the first-window
interfaces and records the reviewed implementation and validation below.
It refines [Platform contracts](platform_contracts_v1.md), using the implemented
[borrow lifetimes](service_borrow_lifetimes.md),
[notifications](service_notifications.md) and
[restriction/maintenance boundary](host_restriction_maintenance.md).

## Scope and initial policy

`MorphicPlatform` is an on-demand service provider, contract 1/version 1, with an
advertised Host-pumped capability and no service thread. Binding and activation
create no native window. The portable shell builds on Windows and Linux; Linux
returns `unsupported` for native creation. No Linux backend is selected here.
Core/Host retain only generic module, routing, borrow and execution contracts.

Generic `RunTestsRequest` receives a correlated `unavailable` result. Platform
retains that reply through output pressure and stopping, and does not consume
another test request until it can retain its reply. Native acceptance remains an
explicit external harness; loading the module or requesting generic tests never
opens a window or silently reports an empty successful test run.

Support one live application window at a time, including a closing window. A
second create returns `occupied`; acquiring another use of the existing window
is supported. Native output selection and multiple windows remain deferred.

Creation has an empty payload. It selects the already agreed fixed defaults:
decorated, resizable, windowed, title `Morphic Engine`, default native placement,
1280 by 720 logical client units at 96 units/inch, reduced as necessary to fit
the monitor work area including decoration. Preserve the requested aspect ratio
when fitting. This is a named implementation default, not a configuration format;
schema-selected policies will supply the later configuration interface.

Host and the native acceptance harnesses declare Per-Monitor V2 awareness in
their executable manifests, preserving the agreed process default. The adapter
checks the effective context before creating a window and reports unsupported
awareness rather than silently falling back. No temporary thread-context override
is required by this increment. It creates without
visibility, resolves actual monitor DPI/work area, adjusts the client/non-client
extent, then shows the window. `WM_DPICHANGED` applies the suggested placement.
Snapshot pixel size comes from the actual client rectangle. Logical size is the
nearest integer to pixel size * 96 / DPI; all values share one snapshot revision.
Minimised pixel extent is zero. Paint is acknowledged without graphics ownership.

## Header and ownership structure

| Location | Responsibility |
| --- | --- |
| `platform/services/window.hpp` | `platform::windows` service opcodes, tokens, requests and tagged immutable descriptor. No Windows SDK types. |
| `platform/services/window_state.hpp` | Coherent atomic snapshot publication and bounded reader, callable inline by clients without a Platform DLL import. |
| `platform/runtime/platform_runtime.hpp/.cpp` | `platform::runtime` pumped lifecycle, authenticated commands, bounded uses and retained completions. |
| `platform/runtime/window_record.hpp/.cpp` | Window/use state, close authority, durable close intent, publication and retirement. |
| `platform/native/window.hpp` | Private native adapter interface, included only by Platform. |
| `platform/native/windows/window_windows.cpp` | Windows creation, dispatch, callbacks, DPI, modal maintenance and destruction. |
| `platform/native/unsupported/window_unsupported.cpp` | Explicit unsupported native adapter for Linux. |
| `platform/module/binding/platform_binding.cpp` | Bootstrap, service advertisement and pumped capability export. |
| `platform/module/types/` | Module-local type catalogue and memory attribution, following existing modules. |

The module owns its runtime through `TInstance<CPlatformRuntime>`. The runtime
owns the single window record directly; stable bounded use storage uses
`TUnorderedCollection<SWindowUse>`, with at most 32 live uses. It does not reset a
window record or remove a use while a descriptor, notification, native callback
or retained completion can refer to it. No application placement new or owning
raw allocation is required. Out-of-line member bodies keep interfaces readable.

Window identity combines the exact provider instance and a monotonically issued
nonzero local window token. No reuse or wrap is permitted. Every use has its own
Host `SServiceBorrow` token; there is no duplicate Platform use-token protocol.
The creator's exact client/instance and route control ordinary close decisions.
Loading the module gives no ownership privilege. Control transfer is deferred.

## Command and descriptor interface

All requests use the existing 24-byte inline service payload and checked POD
copies. Unused bytes/flags/reserved fields must be zero. Unknown opcodes return
`unsupported`; malformed payloads return `invalid_request`. All non-mutating
parameters have matching const decoration in declarations and definitions.

| Opcode | Request | Result |
| --- | --- | --- |
| 16 `create_window` | Empty; fixed initial policy. | Prepared borrow containing a window descriptor, then terminal success after readiness. |
| 17 `acquire_window` | Exact provider instance is implied by route; `SWindowToken` (`u64`). | Separate prepared borrow and readiness, with no controller authority. |
| 19 `request_close` | `SWindowToken`. Any authenticated holder of a use may request ordinary close. | Terminal status; creates/coalesces durable close intent, not accepted closure. |
| 20 `answer_close` | Window token, nonzero close-request sequence (`u64` each), decision (`u32`, reject=0/accept=1), reserved zero (`u32`). | Only the exact controller may answer the current pending sequence; stale replies cannot affect later requests. |
| 22 `window_ready` | Window token, Host borrow token, notification registration token (three `u64`). | Binds notification for that exact use and completes acquisition; idempotent with the same valid tokens. |

Opcode 18 is reserved: release uses generic `ReleaseServiceBorrow` and
`ServiceBorrowQuiescent`, not a parallel domain release command. Configuration,
mode and input opcodes remain unsupported in this increment.

`SWindowViewV1` has immutable size/version, native kind (`windows=1`), provider
instance, window token, controlling-client flag, native window and instance
values (`uintptr_t`), and a pointer to the read-only state publication. All fields
are fixed before `prepare_borrow` and remain unchanged through retirement. Native
handle values are capabilities borrowed for native integration, never authority
to destroy, subclass, reparent or dispatch the window. Clients create and own all
graphics resources independently. Validate kind/version/size before reading.

`SWindowSnapshot` supplies revision, logical/client pixel extents, DPI, minimised
flag, phase (`open`, `close_pending`, `closing`), close-request sequence and
failure code. The pending sequence stays present until answered or superseded by
unconditional closing. Rejection clears pending phase but retains the last issued
sequence; a new intent advances it. Multiple OS close requests while pending
coalesce to that sequence. Closing is sticky, and state remains readable until
the consumer stops access for quiescence. No `closed` snapshot permits reads after
retirement. Exhausted revisions/sequences fail closed without wrapping identities.

The Host thread is the only writer. Every shared payload word is atomic `u32`,
including split 64-bit sequence fields. Use an odd/even sequentially consistent
revision protocol and four bounded read attempts, returning success/retry/failed.
No ordinary mutable field participates in a concurrent snapshot read, including
on Win32. No native operation, allocation, logging or notification runs within an
incomplete publication. Reentrant native callbacks serialise complete writes on
the same Host thread. Publication exhaustion reserves a terminal failed snapshot.

## Registration, readiness and queue pressure

1. Create/acquire prepares a stable descriptor through Host, retaining the accepted
   acquisition until its terminal completion can be delivered. Native callbacks
   may already publish resize/close state; no registered reader is assumed.
2. The consumer observes `prepared`, validates the descriptor and reads its initial
   snapshot while acquisition is pending. It requests the existing Host notification
   registration and sends `window_ready` with the returned token.
3. Platform checks window/use/route/exact-client identity, binds through Host, then
   unconditionally signals the current durable state before completing readiness
   and acquisition. A close intent from before registration is therefore observable.
   Readiness after pending close remains valid; after accepted closing it returns
   `closing` and follows borrow retirement instead of resurrecting the use.
4. Consumers follow the existing epoch/drain/clear/recheck/wait discipline. Snapshot
   retry means recheck, not absence of work. Every wake and pump checks durable
   state; resize revisions may coalesce. Pending close and closing never depend on
   a count of notifications or successful ordinary message delivery.

Notification endpoints are stored per use and signal only after completed state
publication. A failed signal records failure and initiates closing; it never
discards the borrow or notification pin. Late registration/reply handling uses
`CServiceBorrowClient` so expired reader pointers are discarded without access.
Retained terminal replies have bounded capacity reserved before consuming each
command. Busy transport/Host callbacks retain the exact operation for retry; no
native operation is repeated to obtain another reply. Full ordinary/control
queues retain readiness/retirement obligations, with no unbounded backlog.

Acquisitions reserve one of 32 acquisition-completion slots separately from 64
ordinary terminal-completion slots. Each live use may retain one acquisition,
but it never consumes the capacity needed to answer `window_ready`, close requests
or decisions. If use/acquisition capacity is exhausted, consume the excess
create/acquire using an ordinary terminal slot and answer `busy` (or `occupied`
for a second create). The same ordinary-reservation rejection path handles
malformed/stale requests and other failures known before admitting a use. Do not
leave a rejected command at the head of Host's FIFO prefix waiting for a live
acquisition to become ready. A command is not consumed until its applicable slot
is reserved; a failure after admitting an acquisition uses that acquisition's
reserved slot for its terminal error. If the ordinary store fills, retry its
already final replies first: no entry waits for a later readiness command to drain.
Preserve FIFO consumption and the original request correlations. All terminal
slots are retried each pump independently of new command admission. Host's existing
256-operation limit still applies; consumers drain replies and retry submission
when that limit is reached. They must service prepared acquisitions before waiting
for terminal success. Generic borrow controls use Host's independent control lane
and are processed even if both completion stores are full. Publication, cancellation,
retirement and shutdown progress never wait for a free ordinary-completion slot.

## Operation and cleanup deadlines

Start a five-second eligible budget once, when Platform reserves storage and
consumes a create/acquire command, before any native creation. Native restriction
time is excluded through the installed Host restriction reader. Descriptor
preparation, registration and `window_ready` all share that acquisition budget;
readiness does not renew it. Check expiry before accepting readiness, including
late readiness already queued by Host. Expiry/cancellation withdraws the borrow,
records the acquisition's terminal `timed_out`/`cancelled` result and retains
storage until cleanup and terminal delivery finish. A late readiness request
returns `closing` while the use remains, or `stale` after retirement, and never
republishes a descriptor. A cancellation suppressed by Host still receives the
provider's one terminal completion so the Host tombstone can drain.

Other window commands start a five-second eligible budget when their reserved
completion is accepted. Their synchronous policy transition normally completes
immediately; completed decisions are never undone because reply delivery stalls.
Provider completion submission retries retain the recorded terminal result and
its original correlation, without restarting the operation/budget or repeating
the native effect. Native work is cooperative: check budgets before/after it, and
if an unexposed creation finishes after cancellation/expiry, close that candidate
after callbacks unwind. Clock/restriction failure initiates safe failure cleanup;
it never reports success or fabricates a quiescence proof.

An OS close intent has no automatic decision deadline. It remains pending until
the controller answers or unconditional shutdown/controller departure supersedes
it. A timed-out close-request command never constitutes acceptance of closure.
Cooperative release/cancellation emits a one-time diagnostic after five seconds
of awake time; the existing Host ten-second awake shutdown grace remains unchanged.
Both are diagnostic boundaries and retain unresolved native uses/callbacks.

## Close authority and retirement

OS `WM_CLOSE` only records close intent; it never calls `DestroyWindow`. The
controller asynchronously accepts or rejects the current sequence. A foreign
decision is `invalid_request`; a mismatched/finished sequence is `stale`. Accepted
closure publishes sticky `closing`, rejects new acquisitions, withdraws every
borrow through Host, and waits for independent uses and callbacks to retire.
Releasing a non-controller use affects only that use. Releasing/losing the
controller use initiates unconditional closure; shutdown does the same and bypasses
veto. A window close does not post quit or request engine shutdown.

For every use, the consumer first stops descriptor/latch reads and destroys all
dependent native/graphics objects, then acknowledges generic quiescence. Platform
stops signalling the endpoint before finishing retirement and erasing the use.
`never_published` proves no external dependent resource exists. `relinquished`
asserts completed consumer cleanup. `execution_ended` proves CPU inactivity only:
a published native view with that provenance is retained as unresolved rather
than inferring graphics cleanup. This can hold shutdown and unload indefinitely;
timeout does not authorise destruction. Controller departure still initiates
closure, but does not silently resolve its own abandoned native use.

Destroy the native window only after all use retirement, at zero native dispatch/
callback/modal depth, on Host outside native callbacks. Class registration and
native reverse references remain alive through destruction callbacks. Partial
creation failure destroys its unexposed candidate only after creation callbacks
unwind. A failed destruction retains the window/runtime/binding and reports failure.
Only then can the runtime report stopped and permit deactivation/unbind.

## Native restriction and maintenance boundary

Every native creation, dispatch and destruction call has an authenticated Host
restriction scope and a separate native invocation-depth guard. Reentrant window procedures
have callback-depth guards; move/size and menu entry/exit have separate balanced
restriction scopes. Native reason IDs are opaque provider-local numbers. An
unbalanced callback sequence, exhausted scope capacity or invalid context fails
closed, cancels native interaction and retains all lifetime obligations.

Restriction scopes measure Host unavailability; they do not authorise ordinary
service/native work. The minimal generic Host correction is to stop rejecting
authenticated `enter_restriction` solely because sticky shutdown is set. Existing
invocation/thread authentication, bounded accounting and clock failure checks
remain mandatory, and deactivation still cannot enter a scope. Both `begin_stop`
and subsequent stopping pump invocations may therefore account required native
cleanup. Existing Host admission continues rejecting new ordinary service work.
Platform separately closes native creation and new interactive-mode admission as
soon as stop/cancellation is observed; the permitted native calls then service
cancellation/unwind and destruction after the lifetime proofs.

Invocation/callback-depth guards remain active independently of accounting,
including destruction callbacks. A failed accounting entry never permits a new
native operation; existing frames may execute the minimal cancellation/unwind
path under their live guards. Failed clock/publication/accounting retains the
graph and reports failure if safe guarded destruction cannot proceed. A successful
shutdown with explicit use cleanup must enter its cleanup scope, destroy the
window, balance all scopes and finish. Add an actual Host test with sticky stop
already set to prove that route, plus a failure test proving retained state.

During native move/size and system menus, invoke bounded `maintain_host` on entry,
exit, ordinary callback opportunities and a window timer. Continuous motion and
stationary interaction both need opportunities. The timer is an opportunity, not
a latency guarantee: Windows `WM_TIMER` is low priority. No callback recursively
invokes the full Host loop, pumps modules, consumes new Platform service commands,
destroys owners or unbinds. Maintenance merely captures/stages generic traffic and
observes sticky shutdown; normal service dispatch resumes after the outer pump.

On cancellation/shutdown/maintenance failure, request native cancellation with a
reentrancy guard (`WM_CANCELMODE`/menu termination as appropriate), and let native
frames unwind. Do not make unwind success or elapsed grace a lifetime proof.
Destroying the window is deferred to a later outer pump even if it has no uses.
Audit existing emergency Host shutdown and busy/in-use module paths for retention
of this graph; make only necessary generic corrections, with targeted tests.

Windows reference contracts:
[DPI awareness](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows),
[move/size loop](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-entersizemove),
[timer priority](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-timer),
[cancellation](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-cancelmode).

## Validation and exclusions

Deterministic portable tests use a private native adapter seam, with no production
configuration or fake-window mode. Cover partial creation/destruction failure,
coherent snapshots, token/authority rejection, two uses, close accept/reject,
controller departure, cancellation/late completion, registration before/after
publication, sticky close across queue pressure, notification lifetime, callback
unwind and explicit versus execution-ended cleanup provenance. Actual Host tests
exercise real route/borrow/control/registration admission and stopped/unbind checks.
An actual-prefix test fills all 32 pending acquisitions, places an excess
acquisition ahead of readiness commands, and proves immediate bounded rejection
and readiness progress without waiting for acquisition expiry.

Build coordinated Host/Core/modules/tests for Windows Debug x64 and Win32 and
Linux GCC Debug; preserve ordinary suites and existing pump/Host acceptance.
Catalogue/build-source/policy/CI integration includes the portable module. Ordinary
and headless CI never requires a visible window. Explicitly invoked Windows native
acceptance creates the real window and checks default geometry/DPI, repeated
resize/close, stationary and continuous move/size, system menus, cancellation and
shutdown, with evidence separated into automated native, manual and untested.

Input/GameInput, schema preparation, shortcuts/modes, Vulkan presentation,
extensions, multiple windows/output selection, native Linux/SteamOS and Host
sleep/wake remain deferred. No commit or push is authorised for this increment.

## Implementation evidence, 4 November 2026

The portable shell, Windows adapter, manifests, build catalogue and private test
seam are implemented. Source review resolved unexpected native-loss retention,
one shared pump time budget across commands/native dispatch, and the generic
test-reply obligation. The service-borrow client/Host headers also now match the
const-qualified value parameters in their definitions; the notification helper's
read-only record reference is const in both places. Manual formatting was retained.

| Validation | Result |
| --- | --- |
| Windows Debug x64 and Win32 coordinated solution builds | Passed. |
| Windows Debug x64 and Win32 ordinary suites, after the final reply fix | Passed; HostPump reports 4,410 checks on each architecture. |
| Focused portable window/Host tests after the final fix | 643 passed on Windows x64, Win32 and Linux GCC Debug. Includes reply pressure across stopping and preserved unconsumed prefixes. |
| Windows x64 actual-module pump acceptance | All 43 cases passed with one and two workers, including repeated Platform test requests and ordinary unload. |
| Windows x64 Host acceptance | Passed with one and two workers. |
| Linux GCC Debug coordinated build and CTest, before the final small reply fix | All seven entries passed: ordinary suite, policy includes, reflection-fatal checks, 43 pump cases per worker count, and Host acceptance per worker count. |
| Linux focused rerun after the reply fix | Rebuilt, all 643 checks passed, and actual Platform shell/test-slot/unload acceptance passed with one and two workers. The already passing broad matrix was not repeated. |
| In-process real Windows adapter acceptance | Five scenarios, 213 checks passed on x64 and Win32: geometry/PMv2, async close accept/reject, stationary move, continuous sizing, system menu cancellation, and unexpected native loss/retained use. |
| Actual Host plus loaded Platform DLL native acceptance | Move, size and menu cases passed on x64 with one worker and Win32 with two. Verified exact modal authority, deferred ordinary work, sticky stop, native window live before consumer quiescence, then borrow retirement/destruction and balanced restriction scopes. |

The native runner is `tests/platform/run_native.py`. Real Host/DLL cases use
`tests/host_pump/run_tests.py --native`; they are excluded from the default case
list and require Windows. The native harnesses own their windows and may move the
pointer. A timeout terminates the isolated test process and is never evidence of
safe runtime unwind. The initial in-process stationary-move test used an incorrect
mouse-dependent system command; switching the harness to the standard system-menu
Move command resolved it without a production workaround. The first reply-pressure
assertion inspected the ordinary queue count instead of the control queue; the
corrected test passed. One Win32 link attempt collided with a still-running test
executable; the sequenced rebuild and tests passed after that process ended.

Build/test logs are beneath `build/platform-first-window-2026-11-04/`. Repository
line-ending/whitespace checks and source-manifest tests passed; policy reports zero
errors/warnings, retaining the existing intentional negative-test suppression.
Native evidence above is scripted automation. No manual dragging review,
cross-monitor/hardware DPI transition, graphics-dependent cleanup, Linux native
backend, Release or Clang validation is claimed for this increment.
