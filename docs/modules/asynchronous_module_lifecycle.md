Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   asynchronous_module_lifecycle.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   21 Sep 2026

# Asynchronous module lifecycle

This reference describes the implemented Host module service, Executive startup
policy and module lifecycles with Host-owned preambles, a coordinator context
and optional requested service contexts. The
[Host foundation](host_pump_foundation.md), [borrow protocol](service_borrow_lifetimes.md)
and [restriction/maintenance contract](host_restriction_maintenance.md) specify
the generic execution and lifetime boundaries in detail. Completion history and
validation evidence are recorded in [completed milestones](../project/completed_milestones.md).

Module loading, binding, context installation, compatibility
checks and unloading execute on the Host I/O worker. All Host background workers
use the same handlers. With one configured worker, it shares I/O and conditioning;
with more than one, the first handles I/O and the rest handle conditioning.

## Current identity catalogue

The module catalogue contains `executable`, `executive`, `conditioning_general`,
`rendering`, `audio` and `platform`, at the `executable`, `executive`, `conditioning`,
`render`, `audio` and `platform` mounts respectively. The conditioning module and
`simulation` thread identities support independent-service lifecycle fixtures. The production renderer's identity
is backend-neutral. The batch-runner thread range remains contiguous and covers
all 32 supported runners. Audio requests its service thread through its minimal
Host standard coordinator; Platform runs its service provider in a pumped
module-owned execution class.

Pre-release catalogue ordinals are not a compatibility promise. Removing entries
changes encoded identities and dependency bits, so clean-build all components and
fixture DLLs together and use fresh validation runtimes. The binding ABI version
alone does not detect a mismatched catalogue. No reserved type slots are retained
solely to preserve earlier pre-release numbering.

## Startup and Executive transitions

The Host starts its configured workers and builds the filesystem image on its file I/O
worker before requesting the Executive DLL. Its normal
message loop receives the binding completion and starts the Executive
coordinator, which asynchronously requests its serviced child. Per-thread context
installation runs on the newly created thread because it installs thread-local
state. Creation success is separate from module readiness; the Host standard
coordinator waits for its exact child readiness notice after initialization and
validated direct participation.

The [bootstrap configuration file](../project/bootstrap_configuration.md) requires
`executive=<logical module filename>` to select the implementation advertising the
Executive identity and exporting `morphic_module_execution_v1`. The checked-in
flow-test presets select `package:/bin/MorphicExecutive.${module_extension}`.
The bootstrap reader expands the token to the native extension before requesting the module.
The [development filesystem image](../../development/README.md#build-output-redirect)
resolves discovered DLLs adjacent to the running executable without copying them.

Rendering selection belongs to the Executive. The Host bootstraps only the
Executive; a selector implementation can run without a renderer or choose another
implementation later. The normal Executive first sends an owning load request
for `package:/bin/MorphicRendering` with the native extension appended by
`platform::module::make_module_filename`, and `rendering`. It validates
the correlated acknowledgement before accepting completion, then asks Rendering
to run its own tests through `RunTestsRequest` on a negotiated direct connection.
After completion and connection retirement, it loads `MorphicAudio` and
requests its finite tone acceptance test. Audio success or explicit no-output-device
`unavailable`/skipped completion permits the sequential and concurrent asset
acceptance flow. Other Audio failures fail startup.
See the [Audio validation record](audio_module_validation.md). The normal flow
does not load Platform or create a window.

After Executive replacement, `already_loaded` is accepted only for the requested
Rendering/Audio identity with `available == true`; no extra thread, unload or replacement
is requested. Missing/unavailable services, failed completion and malformed
startup replies report a fatal Executive callback result and initiate Host-wide
shutdown. Definite transport busy retains the unsent operation; accepted publication
is never duplicated. Watchdogs cover acknowledgement/completion and progress,
using the [stall-detector policy](module_preamble_design.md) rather than an
unbounded timing-read loop. No automatic failed-module restart is selected.

```mermaid
stateDiagram-v2
    [*] --> Starting: start Host workers
    Starting --> Running: bind completion, start Executive
    Running --> StoppingExecutive: accepted self request
    StoppingExecutive --> ReplacingExecutive: joined, operations drained, replacement requested
    ReplacingExecutive --> Running: replacement bound and thread started
    StoppingExecutive --> ShuttingDown: no replacement
    Running --> ShuttingDown: fatal active/pending Executive failure
    Starting --> ShuttingDown: failure
    ReplacingExecutive --> ShuttingDown: failure
    ShuttingDown --> [*]: dependent cleanup and DLL unloads, remaining assets, workers
```

On receiving an Executive unload or replacement request, the Host immediately
sets that thread's exit-request state and wakes it. This happens before validation
or waiting for other work. The outgoing Executive receives neither acknowledgement
nor completion for that request. It must still return through its normal exit
path; the Host joins it before unloading the DLL. The Host also drains accepted
operations and disposes of the old thread package, including queued messages.

Self-unload without replacement requests full system shutdown. Self-replacement
keeps independent retained assets, cleans up assets dependent on the outgoing
Executive, unloads it, binds the replacement, and starts
its thread. Failure at any replacement stage produces an error-level log and
full orderly shutdown, without rollback, retries or an outgoing notification. The
diagnostic is logged without invoking a debugger breakpoint. Bootstrap failure
uses the same failure handling.

## Ordinary DLL operations

`ModuleRequest` owns its logical filename (for example
`package:/bin/MorphicRendering.dll` on Windows or `package:/bin/MorphicRendering.so`
on Linux) and carries an action (`load`, `unload`, or
`replace`), a registered module identity and an optional required function identity.
The mounting point encoded in the module identity selects the service being
changed. A replacement may select another registered implementation at that point.
Unload must identify the currently loaded implementation.

The Host sends two correlated `ModuleResult` notifications for requests concerning
other DLLs:

1. `acknowledged`: receipt for processing, before validation or worker execution.
2. `completed`: success or failure, with the available implementation and optional
   borrowed function pointer when applicable.

Acknowledgement does not establish readiness or permit renewed use. There is one
pending module operation at a time. A further ordinary request receives
acknowledgement followed by a `busy` completion. This is intentionally bounded;
there is no additional module-operation queue or automatic retry policy.

Successful completion means loading, binding, installation and any required
function lookup have all succeeded. A threaded service must publish readiness;
a pumped service must reach ready through activation/pumping. The original binding
ABI, version and unsupported-function checks are retained. Failed installation is cleaned up by the
same worker. Replacement unloads the old implementation before loading the new
one. If the new load fails, the service remains unavailable. If unloading itself
is refused, the old binding remains present and the result reports that state.
A rendering binding whose thread was stopped is reported unavailable even if
unbinding fails; no automatic restart or rollback is attempted.

Every live module client can issue ordinary module requests; Executive is not the
exclusive requester. Each installation has an exact instance/client identity, and
a replacement cannot inherit replies, acquisitions or borrows. Loading establishes
no persistent loader ownership. Open provider routes make ordinary unload or
replacement busy; only Executive itself or Host lifecycle handling can use its
special self-transition path. Ordinary non-Executive self-transitions are rejected.

The requesting client must quiesce its use of the affected DLL until completion.
The default Executive reports a fatal failed module operation; Host initiates
framework-wide shutdown. More selective recovery is left to future Executive policy.

## Rendering DLL and thread

`MorphicRendering.vcxproj` is a separate solution project producing
`MorphicRendering.dll`; the CMake build produces `MorphicRendering.so` on Linux.
It uses the `rendering` module
identity at the `render` mount on both Windows and Linux, with the existing
`rendering` thread identity.
Vulkan is the chosen backend; the module identity does not encode that choice.
This stage contains no graphics API calls, graphics resources or actual rendering.

An ordinary owning `ModuleRequest` loads the DLL. Rendering exports the current
execution descriptor with a Host standard coordinator and initial serviced child.
Its module-owned class publishes readiness through progress and handles requests,
asynchronous work and batch completions through the common preamble. Thin ABI
adapters preserve execution-thread identity and affine creation/destruction.
Selector Executives may omit Rendering entirely. Explicit unsupported function
requests are rejected; the lifecycle fixture verifies this with a current message
type identity that is not a callable function.

Rendering load/replacement keeps the module operation pending through thread
startup. A failed startup joins and destroys its package, then dispatches
unbind/unload back to the I/O worker before delivering the failure completion.
It reports `installation_failed` when that cleanup succeeds, or the cleanup
failure otherwise. Both acknowledgement and completion retain the original
client correlation.

Rendering unload/replacement requests stop and preserves the final publication
frontiers. Domain work completion, affine destruction, native return/join and
endpoint retirement are separate facts. Final accepted disposal requests may
settle after native join; the endpoint and its operation state remain retained
until the required asset/reply obligations settle. Host then retires the endpoint,
disposes remaining dependent assets and dispatches unbinding. Full shutdown uses the
same path. Ordinary rendering transitions neither request Executive exit nor
suppress its replies. A loaded rendering thread survives Executive replacement
until a rendering request or full system shutdown stops it.

The real stub replaces the successful service fixture in lifecycle validation.
The test Executive, deliberate missing-export/failed-startup DLLs, and a rendering
fixture that posts disposals during exit remain fixtures; production rendering
contains no test switches.

## Common execution contexts and requested children

Every bound module exports `morphic_module_execution_v1`. Host validates the
version-1 `SExecutionV1` descriptor on the binding worker. Custom contexts supply
thin create/handle/destroy adapters to a module-owned class with a bound facade;
an entirely absent callback set selects the Host standard coordinator, optionally
with one initial serviced child. Partial callbacks and unsupported layouts fail.

Each pumped or serviced context has its own Host preamble, private lifecycle rings,
ordinary POD rings and owning lane towards Host. Contexts submit child creation
and stop intents through the facade. Creation outcome, readiness, native end and
endpoint retirement are distinct private lifecycle facts, reported as typed
notifications. Accepted creation remains an obligation after requester departure.
The standard coordinator waits for successful creation and matching readiness;
a stopped/failed child cannot complete startup.

Ordinary stop reaches the coordinator first so custom ordered child shutdown is
preserved. Host continues cleanup, accounts final publication prefixes and grants
affine destruction only after module-state users are safe. Native return, join,
final ingress/resource retirement and worker-side unbinding remain separate gates.
Executive replacement preserves independent modules. See the
[execution reference](host_pump_foundation.md) for the current boundary and failure
retention rules.

## Host-pumped services

Provider operations use ordinary asynchronous endpoint messages and results.
The provider advertisement remains independent of execution; the existing
module service destination is its coordinator endpoint. There is no inline
Host service callback table or mutually exclusive pumped/threaded capability.
See the [common endpoint reference](host_pump_foundation.md).

Platform is an on-demand pumped provider. Binding/activation creates no window;
explicit service commands create or acquire the first Windows window. Its native
callbacks perform only authenticated bounded maintenance while restricted, with
ordinary dispatch and owner destruction deferred until safe outer progress. See
the [first-window reference](platform_first_window.md).

## Module test conversations

Executive runs Rendering and Audio tests through explicitly established
[direct POD connections](direct_pod_connections.md). The Framework contract in
`framework/communications/module_tests.hpp` defines `RunTestsRequest` and
`RunTestsResult`; each module validates its own destination, correlation, status
and counts. Host establishes and retires the connection but does not forward or
interpret test payloads. Each service admits one local run at a time.

Normal conversations consume their final result before closing. Explicit unload,
replacement and shutdown close the connection and cancel unfinished work. No
synthetic test completion is required for retirement. Rendering drains dependent
asset releases, disposals and batch work; Audio stops its device. Endpoint storage
and notification pins remain alive until both participants relinquish them, or
Host proves an abandoned participant's final queue drain and native join.

Lifecycle fixtures exercise Rendering as caller and Executive as worker, final
replies, cooperative Executive self-stop with a pending POD conversation, and
caller unload during a conversation. Separate native lifetime tests cover
return/join retention; cooperative self-stop is not abrupt-thread-return evidence.
Platform's window/input protocols continue to use service commands. Direct POD
transport itself supports pumps and service threads; this test conversation's
participants are an application choice. Its shell fixtures check repeated unsupported
commands and orderly unload; native acceptance uses explicit external harnesses.

## Ownership, dependencies and shutdown

The Host owns address-stable module records, their memory contexts and the owning
request envelope. `ModuleWorkRequest` borrows one stable job from the Host. Only the
worker mutates its binding objects until the completion is received. The job's
filename, registry, debug service and contexts outlive that borrow. Clients send
identities and owned filenames; they do not send borrowed binding objects into
the Host.

Worker jobs begin only after pending asset operations have drained. Asset transfers
preserve their explicit mounting-point dependency flags in the retained Host
carrier. Once a module's thread has exited and been joined, and asset operations
have drained, the Host expects no retained assets to depend on that module. If
any remain, it emits an assertion-level diagnostic and disposes of them before
dispatching unbind/unload to the I/O worker. This diagnostic does not trigger a
debugger breakpoint or cancel unloading. Independent assets remain retained.
The Executive and threaded services have Host-managed threads; pumped services
retain their execution context and provider storage without a thread package.
Callers must still quiesce their own use of affected DLL functions and borrowed views before
requesting unload or replacement.
Existing module memory-attribution checks also remain enforced. Generic
[borrow records](service_borrow_lifetimes.md) protect provider and consumer instances
through explicit quiescence and final provider cleanup, including notification
publishers. Terminal execution proves CPU inactivity only, not external graphics
cleanup. These checks do not track arbitrary cached function
pointers or introduce a general module reference-counting system.

Clients release individual retained assets with `AssetDisposeRequest`, a POD
containing only `CAssetId`. Its correlated `AssetDisposeResult` contains the ID and
success/failure status, with no borrowed views. The Host waits for already accepted
operations using the asset, rejects later saves using that ID, then destroys it.
An invalid, stale or already pending disposal ID receives `invalid_asset`.
Other borrowers must quiesce before requesting disposal; their views do not extend
lifetime. Requests can come from any connected thread package.

Original bundle IDs are the exception: only the thread instance that requested
the bake/load may close them. Closure has no correlated success reply; final
`BundleDisposedNotification` delivery follows storage destruction and virtual-root
removal. Retained sub-files can outlive that thread and remain usable after closure.
The Host detaches the bundle owner/notification route before destroying either
an Executive or Rendering package. A replacement package does not inherit the
route or management authority. Reference-only waits do not prevent module cleanup.
See the [bundle lifetime contract](../assets/bundle_specification.md#references-and-disposal).

Full shutdown stops and joins the Executive, drains operations, performs the same
assertion and dependent-asset cleanup before each module unload, then releases
remaining independent assets while the I/O worker is still running. Only
after those completions does it stop the workers and debug service. If a terminal
worker/transport failure prevents safe unloading, cleanup retains the required
Host graph, bindings, contexts and callback storage through process exit rather
than invoking unsafe destruction. Pumped stop/retirement continues while required
cleanup can progress. Expired grace periods report failure without authorising
destruction or unbinding; see the Host foundation's process-ownership contract.

## Implementation map

- `framework/module/module_messages.hpp`: public module requests and notices in `modules`.
- `framework/filesystem/filesystem_messages.hpp`: logical filesystem refresh messages in `filesystem`.
- `host/runtime/host_payloads.hpp`: private worker exchange, registered in the
  Host's LOCAL catalogue (and separately in the service-test binary).
- `host/module/module_service.*`: admission, stable records, worker-job lifetime
  and completion routing, including execution-start completion and retained results.
- `host/runtime/host.cpp`: bootstrap, queue draining and thread/package lifetime.
- `host/runtime/execution/client_record.*` and `pumped_client.*`: exact clients
  and pumped execution.
- `host/runtime/services/service_router.*` and `service_borrows.*`: routes and
  borrow retirement.
- `host/runtime/execution/module_preamble.*`, `module_preamble_communication.cpp`
  and `module_preamble_lifecycle.cpp`: common dispatch, facade routing and private
  lifecycle/frontier processing.
- `host/runtime/host_endpoints.cpp` and `host_ingress.cpp`: Host endpoint lifetime,
  retained input and bounded admission/output progression.
- `host/runtime/workers/host_worker_thread.cpp`: shared binding/unbinding handlers.
- `rendering/module/binding/*` and `rendering/runtime/*`: module binding and the
  Rendering service, module-local tests and conditioning work.
- `executive/runtime/executive_execution.cpp`: Rendering/Audio selection and acceptance gates.
- `platform/runtime/*` and `platform/native/*`: portable pumped window service and native adapters.

## Validation

Run `tests/module_lifecycle/run_tests.ps1 -Configuration Debug -Platform x64`
from PowerShell. The script builds the solution and six roles of the standalone
`MorphicLifecycleFixture.vcxproj`, then launches isolated Host processes.
Use `Release` and/or `Win32` for other configurations; `-SkipBuild` requires all
matching solution and fixture binaries to have already been built.
Use `-SkipSolutionBuild` to build only the fixture DLLs against an existing
solution build, and `-MSBuild` to select a toolchain explicitly. Windows CI runs
the harness in each Debug/Release and x64/Win32 job with one and two requested
Host workers, retaining the isolated runtime logs and test output.

The fixture project is maintained test infrastructure outside the solution.
It produces Executive, MissingExecution, RenderingFailure, RenderingDisposal,
Service and Passive DLLs. Successful rendering service cases also use the real
Rendering module. MissingExecution deliberately lacks the current execution export.
The production renderer contains no test switches. Adding the fixture project
to the solution for IDE visibility is not required by the harness.

The lifecycle harness covers:

- Correlated acknowledgement/completion, normal and duplicate loads, missing
  files/functions, replacement and unavailable service after failure.
- Executive bootstrap, self-shutdown without notifications, replacement and
  startup failures; selector Executives that never load rendering.
- Rendering load/unload/replacement, failed startup cleanup and Rendering identity.
  A renderer survives Executive replacement and is reused without a second thread.
- Normal Executive startup and unavailable-renderer shutdown before asset work.
  Successful startup/replacement runs the current asset acceptance flow,
  followed by filesystem refresh/cache operations and bundle persistence.
- Explicit disposal, invalid/stale IDs, saves admitted before disposal, dependent
  cleanup and preservation of unrelated retained assets.
- Disposal requests accepted immediately before domain completion: Host accounts
  final ingress and retains operation/resource state until settlement, including
  completion after native join but before endpoint retirement/unload.
- Bundle persistence, deterministic rebaking, virtual-root refresh/cache rules,
  post-closure sub-file saves and final-notification ordering before root reuse.
  Batch borrowers run both queued and inline while retained records protect bytes.
- A complete-view reply handled across closure, diagnosing the convention
  violation and retaining storage until that reference is disposed of.
- Executive replacement with a closed bundle held by an ordinary sub-file record;
  the replacement saves and releases it without inheriting the outgoing recipient's
  notification. Full shutdown also covers undisposed bundle records.
- Rendering-owned bundle disposal, rejection of another requester's original-ID
  disposal, and sub-file use/release after the Rendering package is destroyed.
- Malformed loads, missing inputs, an empty selection and an inventoried file
  removed before baking; every failure releases the reserved destination root.

Bundle fixtures stage boundary-sized files, empty files/directories, mixed-case
and metadata-like names, a reparse entry and an opaque copy of an existing shader.
Successful bundle cases check that Executive allocation attribution returns to
its starting baseline. The replacement fixture saves an opaque asset-ID handoff
for use only within that Host process; this is test coordination, not a persisted
asset-ID contract.

Checks include process status, notification order, saved bytes, dependency
assertions, exit/join/unload ordering, I/O-worker building and conditioning-worker
preparation (or shared-worker fallback). The rendering
exit-disposal regression allows saves to finish before disposal is requested.
When a reply is deferred, it checks deferral-to-completion ordering; in either
case, both disposal completions must precede endpoint retirement and DLL unload;
completion after native join is explicitly allowed.
Missing/failing normal-renderer tests use isolated binary copies under `build/`.

Core `MorphicTests -t1` covers dependency disposal and compact message compatibility.
Its real-Executive protocol tests exercise successful and already-loaded replies,
wrong identity/correlation, unavailable service, load failure, missing/duplicate
acknowledgement and initial submission failure. The test script and suites are
the executable coverage inventory; milestone counts describe dated runs. The
[current qualification ledger](module_preamble_validation.md) records the final
45-case Windows lifecycle evidence, matching consumers and failed/unrun limits.
