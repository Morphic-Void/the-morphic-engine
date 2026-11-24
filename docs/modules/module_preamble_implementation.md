# Module preamble implementation checkpoints

Implementation checkpoints, 22–23 November 2026. The implementation and automated
qualification are complete within the recorded scope, with final coordinator
review accepted on 23 November; see the
[final qualification ledger](module_preamble_validation.md) for current results,
explicit failed/unrun qualifications and the 24 November physical follow-up:
both operator checklists confirmed, ordinary cleanup passed, and instrumented
native cleanup failed. The fixture follow-up is corrected and review-complete
after consolidated coordinator acceptance on 24 November, with separate
affected-consumer validation; no corrected native process was rerun. Entries
retain their historical scope and do not individually claim final completion.
The user completed manual review and authorised the commit on 24 November 2026.

The separate 24 November cleanup investigation has not changed the accepted
23 November implementation or automated evidence. See the ledger's
[physical follow-up record](module_preamble_validation.md#physical-input-follow-up--24-november)
for exact run provenance, outcomes and the accepted fixture-only correction.

## Checkpoint 1: execution boundary and ownership

The initial code adds `framework/module/module_execution.hpp`, public connection
value types, and Host-private lifecycle, ordinary-ring, bounded-FIFO and pressure
selection primitives. The existing execution path is still present until consumers
are migrated. No compatibility export will survive the migration.

- `SExecutionV1` uses the distinct `morphic_module_execution_v1` export, exact
  size/version checks, and create/handle/destroy callbacks. All three callbacks
  absent selects the Host standard coordinator, with an optional initial child.
  A custom coordinator supplies the same callbacks as any other execution.
- `SExecutionContext` is stable through destruction. It contains immutable
  identity/facility references, the bound facade, specialized batch access and the
  module's creation context. It contains no endpoint resource or transport pointer.
- `SInvocation` is an immutable per-call view. It distinguishes ordinary input
  from generated start/stop/service/connection/child/admission notifications.
  Source metadata is supplied by the preamble. Notification payload identities
  describe the affected parties; their source remains Host.
- `CCommunication` has POD, owning and typed intent submission through a single
  immutable Host-lifetime function table and opaque endpoint cookie. The fixed
  table crosses the DLL-to-Host boundary: modules do not import a Host API, and
  Framework sources compile separately into each image. It is not a registration
  or discovery mechanism; Host-internal routing uses normal direct calls.
  Routing is Host/self/peer/exact connection;
  peer routing may specify an expected token. There is no origin parameter. Query
  and specialized bulk access remain token-based. No call dispatches another
  handler. Host validates its currently executing preamble before dereferencing
  the cookie, rejecting use from a foreign thread or batch worker.
- `SExecutionProgress` separates monotonic readiness, immediate asynchronous work,
  direct-input eligibility, domain cleanup completion and next wake delay. Native
  queue/stop flags retain capability validation. Domain completion is not endpoint
  termination or permission to reclaim borrowed state.

The execution descriptor and child context are module-owned immutable/retained
storage through the correlated creation outcome. Host copies the descriptor into
its stable attempt record before launching a child and pins the binding. The
module-defined creation context remains valid for the child's documented usage.
The standard coordinator's initial-child descriptor lives through the binding.

The user clarified the consumer shape during checkpoint 1: each module-defined
execution context is a class owning its local configuration/state and a reference
or bound value of `CCommunication`. The ABI create/handle/destroy functions are
thin adapters to that object. Module code uses the inline facade methods and never
accesses raw table entries. Pumped and serviced contexts share this model; no
inheritance or registration layer is introduced. The Host standard coordinator
remains supported.

## Host state and liveness choices

The stable client record will own the preamble, private lifecycle pair, ordinary
module pipes and immediate-reply fallback storage. Host workers keep their current
queue transport. Host main owns global creation, direct connection coordination,
reply obligations and retirement. Only the execution thread mutates its preamble,
local connections and self FIFO. Cross-thread changes use private rings and the
existing durable stop/failure atomics. Allocation uses an explicit Host context.

Create/handle/destroy run with the DLL's prepared thread identity and provisioning;
the return to Host restores image-local Host identity naturally. Pumped entry
preserves any Host TLS it changes. A Host TLS invocation guard authenticates facade
calls and forbids recursive execution. Native restrictions continue to wrap pumped
calls and maintain/replay Host work without entering another module handler.

The two private rings hold 256 records per direction. Their local enum is not in
the system type registry. Ordinary module rings hold 1,024 messages per direction.
Both self and fallback FIFO backing are capped at 1,024 entries. Smaller internal
test capacities are supported; the Foundation ring's existing minimum is 32.

The private seal carries actual endpoint POD/owning publication prefixes and never
counts as an ordinary POD. Host may receive it before the prefix is accounted.
Provider closure also records the Host issued-output frontier, including staged
immediate replies. Effects wait for their frontier; receiving controls continues.
Activation and its acknowledgement are distinct transitions. Availability delivery
precedes local data dispatch; only both activation acknowledgements complete
establishment and retire its deadline.

The existing connection/request record retains each unpublished control. No
control depends on ordinary reply credits. Close invalidates admission, notifies
the module, explicitly cancels unread direct PODs and waits for domain quiescence,
held-view release and the existing two-sided retirement proof.

Host reply handling will have two contracts:

- Physical publication (`post`, `retry_reply`, `publish_reply`) leaves the producing
  operation intact on full and never bypasses an older fallback reply.
- Immediate retained acceptance preflights fallback storage before admission,
  assigns an issued-output ordinal once, and transfers the obligation once into
  Host storage. Dispatch-local storage reservations materialise or release before
  dispatch returns. Logical reply credits remain independent.

Each Host ingress lane retains at most one captured head. Its classification
determines fallback requirements. Runnable captured heads remain eligible at zero
ring occupancy; blocked heads suppress their lane even at full occupancy. Storage
release, output-turn completion and relevant stop/state changes re-evaluate them.

Ready operation-held output gets bounded retry turns with persistent producer and
per-producer operation cursors. Capacity/budget interruptions preserve the remaining
allowance. Each completed turn opens a pending ingress admission opportunity before
another turn pauses admission. New responses cannot extend that turn. Waiting for
a domain event is not ready output and cannot hold the pause. Full output alone is
not runnable work; every read signals capacity return and parking rechecks epochs.

Ordinary selection uses eight-message pressure slices and a longest-waiting source
after eight nonempty slices. Persistent dense waiting ranks avoid counter wrap;
eligibility is separate from occupancy. Self participates in waiting order, without
pretending its growable capacity is external pressure. Shortened nonempty slices
still count. Lifecycle is checked before every callback. Asynchronous service runs
at turn start, every 32 ordinary messages and before yielding, with coincident
points coalesced. Pumped turns retain the 64-message/two-millisecond cap.

The selector binds a fixed array extent at construction. Direct connection slots
and Host endpoint address slots are withdrawn before retirement/reuse; the array is
never compacted. Population changes do not reset surviving sources' waiting order
or the slice allowance. The pipe adapter distinguishes published/full/invalid/
not-ready explicitly; only full may register a capacity interest. The facade's
static table compares the opaque cookie to its current invocation before touching
endpoint state, including for a copied stale facade.

Existing module-fatal escalation remains framework-wide, including active and
pending Executive failures. Logging will use owner-local counters, named pressure
thresholds, rate-limited warnings and one recovery summary. Diagnostics add no
failure deadline. Safe refusal and explicitly retained obligations remain nonfatal.

## Checkpoint 2: Host runtime and lifecycle integration

The common runtime is connected to Host ingress, serviced native execution,
retained outputs and private lifecycle handling. Production module loading still
uses the old export until consumers migrate; both paths are temporary within this
authorised task. The new path is exercised by production Host classes and a
same-image binding fixture at checkpoint 2. Rebuilt DLL qualification follows in
the consumer increments below.

- `module_preamble.cpp`, `module_communication.cpp`, `preamble_lifecycle.cpp` and
  `CThreadPackage.cpp` run affine create/handle/destroy, the standard child
  coordinator, authenticated facade calls, pressure selection and bounded private
  transitions. Unsupported serviced coordinator/native roles fail before creating
  state. A failed create which leaves live state retains it.
- `module_ingress.cpp` captures at most one ordinary POD/owning head, checks private
  lifecycle before ordinary dispatch, preflights dispatch-local fallback storage,
  and rotates finite output turns across nine producers and their live operations.
  Post-maintenance state changes resample runnable work. Writable retained output
  explicitly schedules continuation after its eight-message flush allowance;
  full output alone does not spin. Terminal fallback output stops publication and
  remains retained until execution quiescence permits cancellation.
- `endpoint_runtime.cpp` records private seal/admission publication frontiers.
  Prefix classification survives an early seal; controls add no ordinary count.
  Admission closure takes effect after both endpoint publication prefixes and
  records the Host issued ordinal including staged replies. `sealed` is also the
  affine destruction grant: it requires direct retirement, provider-borrow
  retirement and no active Host invocation, without requiring the serviced
  endpoint's own join. Borrow retirement preserves the existing notification/user
  proof. Native return, join, final ingress drain, remaining Host obligations and
  binding pins still govern endpoint storage retirement afterwards.
- `direct_connections.cpp` uses private lifecycle records and fixed POD rings for
  common endpoints. Offers require both decisions; installation, availability
  and both activation acknowledgements precede establishment success. The eligible
  deadline remains active after admission opens through both acknowledgements.
  Timeout fixes failure, and a late acknowledgement cannot revive it. Closing
  cancels unread direct PODs on their sole consumer after the closed admission gate
  drains; module quiescence separately releases conversations and held views.
- Child creation copies the descriptor while the correlated request retains it.
  Common outcomes return to their requesting context through creation, readiness,
  native end and retirement. The requester endpoint stays retained through its
  child records, including departure; the standard coordinator waits for child
  retirement. Ordinary module stop still reaches the coordinator before its
  children. Emergency escalation remains framework-wide.
- Immediate private outcomes have bounded per-endpoint retained slots for the
  facade's 64 outstanding requests plus participation. Frontier and connection
  transitions retain their own state. Full outbound lifecycle space does not stop
  inbound lifecycle processing or require ordinary reply credits.

The coordinator accepted checkpoint 2 for continued consumer migration after the
setup rollback review. This is acceptance of the Host integration scope; the
outstanding migration, DLL, diagnostics and final-validation work remains below.

Checkpoint 2 review evidence:

- `build/preamble-lifecycle-build.log`: Debug x64 build, zero warnings/errors.
  `--host-endpoints --log-tag=preamble-private-lifecycle`: 79,638 passed, zero
  failures. Covers both fallback corrections, actual Host frontiers with staged
  output, private consent/install/activation, third-party success and a timeout
  after only one activation acknowledgement.
- `build/preamble-lifetime-build.log`: Debug x64 build, zero warnings/errors.
  `--host-endpoints --log-tag=preamble-affine-retirement`: 79,662 passed, zero
  failures. Adds two common preambles, availability before peer data, deliberate
  direct-input pausing, cancellation of 100 unread messages, held quiescence,
  retirement and destruction. A live provider borrow blocks the destruction grant
  after domain completion and seal; release/retirement then allows affine destroy.
- `build/preamble-child-build.log`: Debug x64 build, zero warnings/errors.
  `--host-endpoints --log-tag=preamble-standard-child`: 79,673 passed, zero
  failures. Adds the actual Host standard coordinator creating a native serviced
  child, readiness, ordered stop, destruction on the native thread, join and final
  retirement. Unsupported serviced coordinator/native roles reject safely.
- The setup review found partial common-channel initialization could reach
  retirement without reply storage. `initialise_execution` now rolls back all
  companions before returning failure, including an uninstalled preamble and
  pumped owning transport. `--host-endpoints --log-tag=preamble-setup-rollback`
  passed 79,802 checks, zero failures, with allocation faults walked through
  channel/ring/reply/preamble setup and actual failed-child outcomes/retirement.
  Every failed attempt produces one failure, no callbacks or leaked allocations.
  `build/preamble-setup-final-build.log` records the subsequent clean Debug x64
  build after changed-code style expansion and classification of Audio's new
  module-owned placement allocation; no behavior changed in the rollback fix.
- With the lifecycle build, `--schema-process --log-tag=preamble-schema-producers`
  passed 896 checks and `--platform-report --log-tag=preamble-report-producers`
  passed 1,492 checks, both with zero failures. These retain existing producer
  regression coverage; they do not qualify migrated DLL consumers.

At checkpoint 2 acceptance, diagnostics, broader fault/closure/ordered-multiple-child
cases, real DLL identity and allocation attribution, consumer migration, removal
of old paths and final multi-configuration/artifact validation remained outstanding.
Later evidence is recorded below; checkpoint 2 is not final completion.

## Consumer migration increments

The user confirmed retaining the two-pointer `CCommunication`: each bound facade
holds the immutable Host-lifetime table pointer and its opaque preamble cookie.
There is no module-load global-table mechanism. Module-owned classes use the
facade's inline methods; Host implementation code calls its own methods directly.

Audio's class owns its run state and bound facade. Thin callbacks allocate it from
the module context and destroy it on the same execution thread. The domain adapter
retains full-pipe replies, pauses direct admission for a retained rejection, keeps
the original run deadlines, and closes the device before connection quiescence.
`--audio --log-tag=preamble-audio-context` passed 440 assertions with no failures.

`build/preamble-audio-dll-build.log` records zero warnings/errors. The focused
`--module-preamble --log-tag=preamble-audio-dll-fixed` run passed 14,863 assertions
with no failures. It loads the rebuilt Audio DLL through the new execution export
in both pumped and standard-coordinator/native-child modes, checks Audio allocation
attribution and affine release, and verifies unchanged caller identity/memory.
The DLL log records `audio:host` and `audio:audio` as appropriate. The initial DLL
fixture omitted its required logger; the corrected fixture supplies a scoped
logger. The coordinator reviewed and accepted this Audio increment for continued
migration, without claiming final production selection.

Rendering now has the same module-owned class/facade shape. Its acquisition and
release publication stays in each existing resource-operation record when full;
a bounded retry pass rotates through those records. A result remains unavailable
until the acquisition release is acknowledged. The texture-flow operation retains
its owning transfer or disposal post and cancels unpublished transfer work on
closure. Batch responses and local completions continue draining during stop.
`--texture-service --log-tag=preamble-rendering-pressure` passed 151 assertions
with no failures, including repeated full acquire/release refusal and subsequent
completion/disposal without loss or duplicate publication.

The Rendering DLL and tests built without warnings/errors in
`build/preamble-rendering-build.log` and `build/preamble-rendering-dll-build.log`.
The latter focused run (`--module-preamble --log-tag=preamble-rendering-dll`)
passed 15,169 assertions with no failures and added actual Rendering DLL idle
activation/destruction in both execution modes. The full texture acceptance flow
now runs through the new context, actual Host asset service and two batch runners.
All 23 scenarios complete in both pumped and native-child modes, followed by asset
disposal, connection retirement, affine destruction and empty module attribution.
The fixture uses distinct Host/module memory contexts backed by the same allocator,
as required by the existing owning-transfer contract.

That end-to-end run exposed a missing notification re-arm in the common preamble:
an earlier capacity notification could suppress the final direct-result wake.
Each turn now clears coalesced direct notifications before queue/capacity inspection,
after the serviced driver's epoch observation. It never accesses a descriptor after
publishing quiescence. Host restriction notifications are re-armed at the same point.
`build/preamble-rendering-final-build.log` records the focused Debug x64 build with
zero warnings/errors. `--module-preamble --log-tag=preamble-rendering-wake-regression`
passed 15,222 assertions with no failures, including repeated data/capacity wakes,
the full rebuilt Rendering DLL flow in both modes and existing closure checks.
The assertion count includes asynchronous pump iterations and is timing-dependent.

Both modules remain transitional until Executive and the other consumers migrate.
Their old entries, temporary texture-flow adapters and old Host loading selection
must be removed before final validation. Executive/Platform migration, diagnostics,
remaining lifetime/fault cases, final version-1 contracts, artifact rebuilding and
the final Windows configuration checks remain outstanding.

Platform now has a module-owned allocation and thin execution adapters, preserving
native capability checks, restriction scopes and domain borrow retirement. Its
common handler receives authenticated Host input and services native work through
the existing bounded domain pass. Waiting provider exchanges and full report posts
do not by themselves advertise runnable continuation; publication state remains
retained. The provider client has a bound-facade constructor and explicitly treats
published-with-wake-failure as accepted, retaining the correlated exchange exactly
once. Legacy constructors/loops and the compiled input-acceptance adapter remain
transitional and must be removed/migrated before final validation.

`build/preamble-platform-fixture-build.log` records a clean Debug x64 build of the
Platform DLL and tests. The portable window fixture now drives the common handler
and facade: `--platform-window --log-tag=preamble-platform-window` passed 2,901
assertions, including refusal/recovery without busy looping. Unchanged domain
regressions `--platform-report --log-tag=preamble-platform-report` and
`--platform-input --log-tag=preamble-platform-input` passed 1,492 and 34,104
assertions respectively. The latter input/report fixtures still use their
transitional adapters; these results do not claim complete native migration.

The real DLL fixture now also covers pumped Platform activation, readiness,
shutdown, affine destruction and empty allocation attribution. The first run
exposed the execution object's size exceeding the memory token's 16-bit element
stride. The factory now uses aligned byte storage, matching `TInstance`; Audio
and Rendering factories use the same form. After rebuilding all three DLLs and
tests, `build/preamble-platform-allocation-build.log` is clean and
`--module-preamble --log-tag=preamble-platform-allocation` passed 20,303 assertions
with zero failures. This also exercises the provider facade tests and diagnostic
counter/recovery refinements added after the earlier diagnostics run. It does not
claim native window/input acceptance or final production selection.

The user approved the simpler Executive timing policy during implementation:
acceptance watchdogs detect stalls, and no new ordinary-input admission flag is
added. Domain completion can settle an operation during snapshot retry without
strict proof of its previous deadline. Candidate budget starts preserve the live
anchor and withhold new publication until anchored. A separate timing-availability
episode survives phase changes, warns at 1s/5s, summarizes coherent recovery and
fails after 30s of continuous retry using the independent awake clock. Retry-only
work uses a 1ms timed wake. This policy is recorded in the design and proposal;
`executive/runtime/execution_timing.hpp` now implements candidate anchors and the
independent availability episode. `CExecutiveExecution` owns its context/facade
and bounded request records (one sequential/module/bundle, 32 saves, six filesystem
and eight acquisitions). It publishes at most eight per async call in preparation
order, preserves refusals, and never republishes an accepted uncertain wake.
The old Executive blocking loop/export has been removed. Host production loading
now queries `morphic_module_execution_v1` and constructs the common pumped driver.

`build/preamble-executive-timing-build.log` is clean; the deterministic policy run
passed 20,411 assertions. `build/preamble-executive-dll-final-build.log` is clean;
`--module-preamble --log-tag=preamble-executive-dll-retry` passed 20,499 assertions,
zero failures. The actual rebuilt Executive DLL accepts completion/phase changes
during retry, withholds connection and test publication until anchored, retains
full output without runnable spin, settles uncertain publication once, and stops
while an initial or next-phase anchor is pending. Allocation attribution is empty
after each destruction and caller image state is preserved. Deterministic awake
clock tests cover the continuous 30-second failure episode across phase starts,
one/five-second warnings, recovery and failed clock reads. Full fatal-shutdown
fixture migration remains outstanding.

The first production Debug x64 run uses the regenerated isolated runtime
`build/preamble-production-runtime/bootstrap.cfg` (two Host workers and two batch
runners). The rebuilt engine exited 0. Its log
`build/preamble-production-runtime/development/logical-roots/test-logs/morphic_debug.preamble-production.p90788.log`
records 23 Rendering texture flows, successful real Audio playback, 50 sequential
and 32 concurrent asset operations, 12 filesystem cases, bundle and acquisition
acceptance, then affine child joins/retirement and worker-owned DLL unload. Expected
negative domain cases log errors; there is no framework failure in this run.
The build log `build/preamble-production-build.log` has zero warnings/errors.

Audio and Rendering now report their fatal callback transition once, allowing later
stop callbacks to report cleanup progress to the preamble after failure is latched.
The rebuilt DLL/preamble suite injects invalid Host-origin domain input and verifies
destruction and empty attribution: `--module-preamble --log-tag=preamble-fatal-cleanup`
passed 20,558 assertions. `--audio --log-tag=preamble-audio-fatal-cleanup` passed 442,
including device cleanup after an execution failure. These are incremental results;
legacy fixture/export removal and final version-1 configurations remain outstanding.

The portable Input and report-capture fixtures now invoke the facade/common handler
path. `--platform-input --log-tag=preamble-input-facade` passed 34,056 assertions.
The migrated report fixture exposed a capacity-resume bug: `runnable()` correctly
excluded blocked output, but `progress()` incorrectly used it to reject a later
async retry. `progress()` now admits active non-receipt state, and a newly generated
failure POD clears the old owning-output wait so it is eligible on its own pipe.
`build/preamble-report-recovery-build.log` is clean and
`--platform-report --log-tag=preamble-report-recovery` passed 1,429 assertions.
The report suite still contains legacy Host transport/handoff fixtures; these
counts do not claim their migration or native input qualification.

## Earlier incremental validation

Pressure diagnostics now sample existing state without changing failure or admission
decisions. Each sustained episode warns after one second and at most every five
seconds thereafter, with a recovery summary only after a warning. Repeated short
episodes remain quiet. Logs carry the endpoint address/token, connection or lane,
queue/storage counts, duration and observed progress. Host endpoint pressure uses
source bits for full output (1), fallback storage (2), blocked POD head (4), blocked
owning head (8) and lifecycle output (16). Eligible-input starvation tracks progress
separately; blocked/paused input is excluded. A parked serviced endpoint limits its
wait to the next diagnostic time, and ordinary capacity wakes still resume sooner.
Host sampling uses its existing bounded turn/cadence. Connection reuse resets
diagnostic identity, and quiesced descriptors are never inspected.

`build/preamble-diagnostics-final-build.log` records zero warnings/errors.
`--module-preamble --log-tag=preamble-pressure-diagnostics` passed 20,240 assertions
with no failures. Deterministic clocks cover transient refusal, repeated warnings,
recovery, renewed pressure, eligible progress and slot reuse. The common-preamble
fixture also holds an output pipe full across its diagnostic wake, checks nonfatal
retention and the five-second next wake, then returns capacity and confirms the
diagnostic timer is removed. This run predates the provider-facade adapter tests.

Incremental runtime integration evidence (separate from checkpoint 1):

- `build/preamble-runtime-build.log` records a Debug x64 MorphicTests project
  build with zero warnings/errors. `MorphicTests.exe --host-endpoints
  --log-tag=preamble-runtime-core` passed 71,178 checks, zero failures. This covers
  the common preamble, authenticated static facade (including a foreign native
  thread and a copied stale facade), self dispatch/cadence, capacity wakeups and
  private admission/seal frontiers using a same-image binding fixture.
- `build/preamble-driver-build.log` records the serviced-driver build.
  `MorphicTests.exe --host-endpoints --log-tag=preamble-serviced-replies-fixed`
  passed 75,471 checks, zero failures. This adds a real native serviced thread,
  affine create/handle/destroy, parking/capacity return and private shutdown, plus
  fallback storage saturation at limits 3 and 1,024 and dispatch-local reservation
  release. The first run found use of an invalid default deadline for an untimed
  wait; the driver now uses the existing untimed epoch-wait overload.
- `build/preamble-ingress-build.log` records the Host ingress build.
  `MorphicTests.exe --host-endpoints --log-tag=preamble-ingress-turns` passed
  79,343 checks, zero failures. Actual Host ingress fixtures cover retained heads
  with empty/full rings, storage blocking, unrelated source progress, capacity
  rescheduling, and a finite service-output retry turn admitting ingress while
  more results remain ready. Service command FIFO and final-result correlation
  survive one-capacity-slot interruptions. Producer-turn primitives additionally
  exercise persistent producer rotation under continuous ready work.
- `build/preamble-output-build.log` records the output-producer build.
  `MorphicTests.exe --host-endpoints --log-tag=preamble-output-rotation` passed
  79,454 checks, zero failures. Service, borrow, asset, schema, schema-process,
  report, module, filesystem and bundle producers now participate in finite
  output turns. Added tests cover operation-owned asset results under saturation,
  per-recipient slot reuse/rotation, retained report controls and unsent capture
  cancellation. The slot-reuse test found list-order iteration incorrectly used
  as numeric cursor order; selection now finds the lowest eligible numerical slot
  after the cursor, independently of collection list order.

These incremental project builds used `MSBuild.exe visual_studio/MorphicTests.vcxproj
/m:1 /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=D:\TheMorphicEngine\
/p:BuildProjectReferences=false /p:PreferredToolArchitecture=x64 /v:minimal`.
The test executable is `build/bin/x64/Debug/MorphicTests.exe`. Real DLL identity/
allocation attribution remains outstanding. The output-producer changes are
excluded from the earlier 79,343 result. These earlier runs predate private Host
lifecycle routing and are not acceptance of checkpoint 2 or final completion.
Diagnostics, consumer migration and removal of transitional paths remain unfinished.

Checkpoint 1 passed on 22 November 2026:

- `MSBuild.exe MorphicEngine.sln /t:MorphicTests /m:1 /p:Configuration=Debug
  /p:Platform=x64 /p:PreferredToolArchitecture=x64 /v:minimal` succeeded with
  zero errors. The build log is `build/preamble-checkpoint1-build.log`.
- `build/bin/x64/Debug/MorphicTests.exe --host-endpoints
  --log-tag=preamble-checkpoint1` reported `HostPump: 71150 passed, 0 failed`.
- `tools/check_line_endings.ps1`, `git diff --ignore-submodules=all --check`,
  and changed project/filter item correspondence checks passed.

An earlier direct project invocation lacked the solution directory and failed an
external Basis include; the corrected solution-target build above resolved it.
The coordinator accepted checkpoint 1's source review and validation for continued
integration. This evidence covers the execution boundary and initial pipe/FIFO/
selection primitives. It explicitly excludes the subsequently added
`module_channel`, `direct_endpoint`, `module_preamble`, `module_communication` and
`preamble_lifecycle` integration code and its tests, which are under development.

Subsequent checkpoints integrate the Host preamble and lifecycle accounting, then migrate
Audio, Rendering, Executive, Platform and lifecycle/input/schema fixtures. Final
work removes all old module execution/handshake paths, updates manifests and
architecture references, rebuilds consumers and regenerates affected artifacts.
Final version markers are 1. Coordinator review is required before completion.

## Platform legacy removal and callback status evidence

Platform's binding now exports only `morphic_module_execution_v1`; the production
runtime and report capture no longer accept `SHostPumpContext`, own a raw endpoint,
or use the old pump/progress types. Domain service uses `SExecutionProgress` and
leaves admission/seal mechanics to the preamble. The InputPlatform acceptance DLL
uses the same context/facade and thin class adapters. Its three report operations
retain ordinary refusal in named state. Stop cancels an unsent independent read;
an accepted read remains an obligation through result acquisition, release
publication and the release acknowledgement. Published-wake-failed acceptance is
never resubmitted.

The public execution header now states that callback success supplies valid
progress for that invocation and does not clear an earlier fatal failure. Window
and Input fixtures latch failed callback results across subsequent successful
cleanup and explicitly declare their intentional fatal cases. Their legacy
Host-side transport harnesses still require migration; this increment removes the
Platform consumer adapter, not every test/Host compatibility path.

Debug x64 evidence:

- `build/preamble-platform-current-tests-build.log` and
  `build/preamble-input-platform-current-build.log`: zero warnings/errors.
  The initial current-contract build exposed two GameInput fixtures using the
  removed constructor; both now use an execution context and facade.
- `MorphicTests.exe --platform-window --log-tag=preamble-platform-current-window`:
  2,918 passed, zero failed, including the common-preamble native restriction/
  shutdown fixture and intentional fatal callback latching through cleanup.
- `--platform-input --log-tag=preamble-platform-current-input`: 34,090 passed,
  zero failed. `--platform-report --log-tag=preamble-platform-current-report`:
  1,429 passed, zero failed.
- `build/preamble-platform-stop-final-build.log`: zero warnings/errors.
  `--platform-execution --log-tag=preamble-platform-stop`: 173 passed, zero failed.
  This separately invokes the rebuilt production Platform DLL through the common
  preamble and the rebuilt InputPlatform DLL through supplied facade/callbacks.
  The latter deterministically checks repeated busy, stop before read acceptance,
  stop after acceptance, delayed acquisition result, retained release under busy,
  acknowledgement before completion, accepted/wake-failed exactly once, and empty
  allocation attribution after destroy. It requires both Platform DLLs built.
  It does not claim native window/input acceptance or full Host report routing.

The production smoke remains accepted by the coordinator for that milestone only.
Legacy fixture/Host migration, pending/active Executive fatal shutdown coverage,
remaining native acceptance, final version-1 consumers/artifacts/configurations and
final coordinator review remain outstanding. No commit or push has been made.

## Pending and active fatal execution

The coordinator accepted the preceding Platform removal increment for its stated
scope. The next focused Host check uses the actual pending activation boundary,
common CPumpedClient/preamble and Host shutdown/retirement paths with same-image
callbacks. It injects the completed worker-binding boundary; it does not claim a
native loader/replacement-file test.

`build/preamble-global-failure-final-build.log` has zero warnings/errors.
`MorphicTests.exe --execution-failure --log-tag=preamble-global-failure-final`
passed 2,152 assertions, zero failures. Cases cover Executive and non-Executive
create refusal and failed startup callbacks during initial/pending replacement,
active callback failure, lost accepted reply storage with both ordinary rings full,
framework-wide stop/admission closure, delayed domain cleanup with state retained,
exactly-once affine destruction, and ordinary nonfatal Executive replacement
preserving another module and admitting a distinct new instance.

Pending activation now escalates a fatal pump result at that boundary, before its
cleanup/outcome can be deferred. Endpoint failure observed by the preamble also
latches its own failed state. The first focused run exposed that Host accounting
failure shut down the framework correctly but left the preamble's failed flag
clear; the corrected final run checks both. Failure flags remain latched while
subsequent successful callbacks deliver cleanup progress. Existing provider-borrow
and serviced join tests supply separate retirement evidence; the new checks do
not substitute for them or for final rebuilt production/native acceptance.

## Ordered common child retirement

The coordinator accepted the focused fatal-execution milestone above. The ordered
two-child Host maintenance fixture now uses a module-owned coordinator facade and
two serviced execution callbacks. It no longer implements an outer thread loop,
reads raw lifecycle messages, or publishes admission/seal acknowledgements. The
coordinator waits for child readiness, stops the first child, waits for its ended
notification before stopping the second, and reports work completion only after
both retirement notifications. The test checks both affine destructions and that
ordinary coordinator stop does not preempt its chosen child order.

`build/preamble-ordered-readiness-build.log` records zero warnings/errors;
`MorphicTests.exe --ordered-execution --log-tag=preamble-ordered-readiness` passed
15 assertions with no failures. An initial migration run exposed a missing Host
owner-wake endpoint in this old fixture setup; the current fixture installs it
before common execution initialization. This result covers this migrated case,
not the other still-legacy maintenance/real-DLL fixture paths.

## Audio legacy entry removal

The coordinator accepted the ordered-child milestone above. Audio now has only
its current execution class and `morphic_module_execution_v1` export; the old
service loop, raw direct client, thread entry and Host-pump export are removed.
The hardware-free service fixture runs real serviced Audio and Executive children
through Host's standard coordinators and common preambles. Commands and observed
results cross threads through explicit atomic publication; facade calls stay on
the execution owner. It covers idle parking, invalid requests and correlations,
1,025 replies against the 1,024-slot direct ring, retained-output parking and
capacity wake, active-run busy/success, connection cancellation/reconnection and
stop cancellation with exactly-once affine destruction. Domain tests also cover
refusal of initial participation followed by successful cleanup progress.

Debug x64 validation:

- `build/preamble-audio-current-tests-build.log`: zero warnings/errors;
  `MorphicTests.exe --audio --log-tag=preamble-audio-current`: 3,244 passed,
  zero failed. The initial migration build caught an invalid named Audio address;
  the fixture now constructs the exact module/thread identity and uses a serviced
  Executive peer, preserving production consent policy.
- `build/preamble-audio-current-dll-build.log` and
  `build/preamble-audio-current-dll-tests-build.log`: zero warnings/errors.
  `--audio-execution --log-tag=preamble-audio-current-dll`: 68 passed, zero failed.
  This rebuilt-DLL check covers pumped and serviced idle allocation/identity,
  caller-state preservation, affine release, and failed-callback cleanup. The
  actual backend acceptance remains separately recorded in the production smoke.

Remaining Rendering/fixture/Host legacy removal, native acceptance and final
current-contract configurations/artifacts are not covered by this increment.

## Rendering legacy entry removal and cleanup wait state

The coordinator accepted Audio's preceding removal milestone. Rendering now
exports only the current execution descriptor; its old outer loop, requested
thread function, Host-pump export, function-binding header and texture-flow raw
context/direct-client branches are removed. The TextureBatch protocol fixture
loads the real rebuilt DLL and creates serviced Rendering and Executive peers
through standard coordinators and the common preamble. Host handles private
lifecycle, frontiers, join and retirement. The test deliberately owns the ordinary
asset submissions/results to control their completion timing; each captured input
is accounted, reply credits are explicit, and immediate replies use a bounded
CDispatch scope. Retained asset-operation output is explicitly retried and has no
fallback storage reservation surviving its dispatch.

The fixture preserves wrong-module rejection, full positive correlation range,
active-run busy, failed setup and restart, then stops with an accepted owning
transfer outstanding. Its late successful asset is disposed before connection
quiescence and destruction. The first migration runs caught two fixture omissions
(dispatch-local fallback reservation and explicit operation-output publication)
and a runtime preservation regression: run_serviced was exposing Waiting during
shutdown. It now marks Exiting on the execution owner and uses the existing
preserving-state timed/untimed waits while stopping. Epoch capture and the
runnable-work recheck remain unchanged. The focused case verifies Exiting before
and after the late asset, no premature retirement, final joined children, no
cancelled conversation reply, and empty DLL allocation attribution.

Debug x64 evidence:

- `build/preamble-rendering-current-dll-build.log` and
  `build/preamble-rendering-current-output-build.log`: zero warnings/errors.
- `MorphicTests.exe --texture-batch --log-tag=preamble-rendering-current-output`:
  2,067 passed, zero failed. Includes the migrated protocol case and existing
  controlled batch, shader, saturation and drain cases.
- `--rendering-execution --log-tag=preamble-rendering-current-dll`: 363 passed,
  zero failed. Covers rebuilt DLL pumped/serviced execution, its 23 actual domain
  flows through Host, identity/allocation/caller-state checks and fatal callback
  cleanup. This remains separate from the controlled late-completion test.

Executive/remaining fixture/Host legacy removal, native acceptance, final affected
version-1 consumers/artifacts/configurations and final coordinator review remain.

## Executive legacy fixture and watchdog removal

The coordinator accepted the Rendering removal milestone above. Executive's
unused thread-function binding header and legacy module-test watchdog wrapper are
removed. ErasedOwner's budget checks now exercise TExecutionBudget, including
unchanged anchors across 5,000 coherent observations, expiry, fresh operation
starts and timing construction/read failures. Candidate/retry/unavailability
coverage remains in the previously accepted deterministic timing tests.

The 15 Executive startup-notice scenarios now load the real Executive DLL through
its execution descriptor and run its serviced child with actual Host common
preambles. Rendering/Audio peers are serviced module-owned facade contexts. Tests
retain loaded/already-loaded success, unavailable/wrong-implementation/load-error,
missing/wrong/duplicate acknowledgement, and Audio unavailable/failed/cancelled/
busy/bad-count/wrong-correlation/load-error outcomes. Test-owned Host completions
use explicit reply credits, input accounting and current-dispatch fallback
reservations. Fatal real-DLL domain outcomes must latch Host emergency shutdown;
remaining synthetic result credits are cancelled only after recipient execution
quiescence. All three child records retire, peer destruction is affine and DLL
allocation attribution is empty after every case. These checks inject module-load
results rather than exercising replacement files/native loader policy.

The separate initial-publication failure check invokes the real DLL with a supplied
facade returning invalid, verifies one refused owning publication, a failed start
callback followed by successful stop cleanup and destruction, and the current
retained-operation-publication diagnostic. It does not claim common transport
failure injection; actual Host fatal transport/pending paths remain separately
covered by the previously accepted failure checkpoint.

`build/preamble-executive-current-dll-build.log` and
`build/preamble-executive-current-tests-build.log` have zero warnings/errors.
`MorphicTests.exe --erased-owner --log-tag=preamble-executive-current` passed
1,217 assertions, zero failures, including the migrated real-DLL scenarios and
existing erased-owner/module-unload attribution gates. No legacy Executive
execution function or raw thread context remains in this consumer.

Other lifecycle, pump/provider, input and portable Host fixtures still require
migration before removal of shared legacy interfaces and final configurations.

## Platform Report Host transport fixture migration

The coordinator accepted the Executive removal milestone above. Platform Report's
capture-boundary and Host handoff cases now use the common fixed ordinary channel
and retained-reply storage. Synthetic immediate dispatches have explicit CDispatch
scopes; asynchronous report output stays in its report operation and is retried
explicitly. Capture cancellation before request consumption and terminal-before-
quiescent retention remain covered. The full-output case fills the 32-slot test
ring, arms allocation failure, and verifies no allocation, lost reply credit or
fatal state. Returning capacity publishes the same acquisition; its bytes remain
retained until the recipient's explicit execution-quiescence proof.

`build/preamble-report-common-retirement-build.log` has zero warnings/errors.
`MorphicTests.exe --platform-report --log-tag=preamble-report-common-final` passed
1,468 assertions, zero failures. The first run exposed a fixture assumption that
receipt publication also retires the capture immediately; the corrected test runs
the next Host retirement pass. These are controlled Host service/transport cases,
with synthetic quiescence; native capture and actual execution/retirement remain
separate evidence and acceptance work. No legacy raw endpoint or pump callback
remains in PlatformReport_test_cases.cpp.

## Portable service/provider/coordinator consumers

The coordinator accepted the preceding Report milestone. HostPump's router tests
now use fixed common channels, dispatch-local fallback reservations, and explicit
bounded operation-output retries. They preserve request ordering, authentication,
completion/cancellation precedence, operation limits, provider availability and
full-ring reply-credit/FIFO checks. Provider helper tests use the bound facade;
accepted-but-wake-failed publication remains exactly once, and the 256-operation
limit plus delayed notification binding retirement are covered. The old growable
queue poisoning fixture and raw service-thread-pump coordinator fixture are removed.

The standard coordinator's private-control fixture covers separate and coalesced
creation/readiness, failure, stop during pending creation, module stop, ended child,
wrong child tokens and duplicate/premature creation/readiness. Host publishes
creation before readiness on one private SPSC lane; historical cross-lane
ready-before-created tolerance is therefore replaced by rejection of that invalid
private sequence. The runtime now validates the created child token/kind and does
not advertise readiness when an ended notice is already pending in the same pass.
These controlled private records are separate from the actual serviced-child case,
which still verifies native join and affine destruction.

`build/preamble-service-fixtures-build.log` and
`build/preamble-service-coordinator-build.log` have zero warnings/errors.
`MorphicTests.exe --host-services --log-tag=preamble-service-coordinator` passed
3,227 assertions, zero failures. The earlier 2,992/0 run covered the service and
provider migration before adding the coordinator cases. This focused selector
avoids repeating unrelated endpoint/native matrices. Shared fixture adapters and
other legacy consumers remain outstanding; this is not final production validation.

## Borrow, worker-failure and Platform routing fixtures

The coordinator accepted the service/provider/coordinator increment. ServiceBorrow
now uses common channels throughout. Immediate open/request/notification/close
responses have dispatch-local storage scopes; prepared views, closing/quiescence
notices and terminal replies are retried from existing records. Tests preserve
full-ring retirement, two-sided quiescence, unpublished cancellation, independent
consumers, bounded long-lived borrows and delayed notification handles.

HostRuntime's module recipients also use common channels. Native Host worker loops
remain unchanged. The final-worker-publication test observes the retained scan
outcome after final worker drain and explicitly retries it for its controlled
unregistered recipient, proving sibling failure does not overwrite that outcome.
The pinned-notification shutdown test preserves retained package, provider, consumer
and dependency state until explicit notification/borrow retirement.

Platform Window/Input domain fixtures now connect their supplied facade to fixed
common channels, with distinct published/full/invalid/unavailable outcomes. Their
provider exchange helper preflights a current-dispatch reply slot before executing
an operation, retains an immediate result on full and accounts each consumed head.
It no longer implements raw admission/seal controls. Bounded Host output passes
retry service/borrow records and fallback replies; callback failure remains latched.
Window notification pressure fills the actual 1,024-slot ordinary ring while its
separate notification and retained view remain usable.

Debug x64 evidence:

- `build/preamble-borrow-fixtures-build.log`: clean; `--host-services
  --log-tag=preamble-borrow-fixtures`: 5,679 passed, zero failed.
- `build/preamble-host-runtime-fixtures-build.log`: clean; `--host-services
  --log-tag=preamble-host-runtime-fixtures`: 5,753 passed, zero failed, including
  the migrated worker-failure and retained-teardown cases.
- `build/preamble-platform-common-router-build.log`: clean;
  `--platform-window --log-tag=preamble-window-common-router`: 6,413 passed,
  zero failed; `--platform-input --log-tag=preamble-input-common-router`:
  34,931 passed, zero failed. These still use simulated native adapters and explicit
  execution invocations; they are not native acceptance or common-preamble affinity
  evidence. Those remain separately required.
- `git diff --check --ignore-submodules=all` and the tracked-text line-ending check
  pass. Common fixture adapters coexist with remaining legacy consumers during
  migration; all in-scope legacy paths still must be removed before completion.

## Schema Host consumer migration

The coordinator accepted the borrow/HostRuntime/Platform package. Schema service
fixtures now use a common module recipient and keep only their controlled Host
conditioning worker on the original worker transport. Immediate replies have
explicit logical credits and current-dispatch storage scopes; operation-held
acceptance/results are retried in bounded fixture output passes after those scopes
end. Internal consumers, replacement identities and owning ingress use the current
transport path. The owning queue test clears each captured owner before taking the
next, preserving the capture API's empty-destination requirement.

Old module-side growable-queue poisoning cases are replaced by full 32-slot ordinary
rings at process acceptance, process completion and prepared-schema completion.
They arm next-allocation failure, verify no allocation/fatal error/publication or
credit loss under full, return capacity, and check exactly one correlated outcome
and retained descriptor lifetime. Host worker uncertain-publication tests remain,
including conditioning and retirement publication faults and final-drain cleanup.
The focused schema selector now includes these prepared-schema/worker cases too.

`build/preamble-schema-common-owner-build.log` records zero warnings/errors.
`MorphicTests.exe --schema-process --log-tag=preamble-schema-common-owner` passed
1,221 assertions, zero failures. Initial migration runs exposed fixture issues in
captured-owner reuse and an assertion that conflated process credits with an
already-live disposal obligation; both are corrected. This remains controlled Host
service/worker evidence, separate from rebuilt real-DLL schema acceptance and the
remaining legacy fixture removal.

## Fixed-ring ordinary acquisition fixture

The coordinator accepted the Schema milestone. BundleService's former uncertain
ordinary-publication case now uses a 32-slot common recipient. A successful raw
asset acquisition under a full ring transfers its logical credit to the bounded
fallback FIFO without allocation or advancing the published ordinal. Capacity
recovery delivers exactly one correlated acquisition; disposal retains its bytes
until explicit release, then publishes the disposal result. Immediate operations
have dispatch-local storage scopes. This replaces the obsolete module-side
growable-queue poisoning expectation; Host worker fault coverage remains separate.

`build/preamble-acquisition-fixed-build.log` has zero warnings/errors.
`MorphicTests.exe --bundle-service --log-tag=preamble-acquisition-fixed` passed
561 assertions, zero failures. Other BundleService cases still use the legacy
combined Host-worker/module-recipient fixture and require migration. This scoped
case is not a claim that the complete bundle consumer or legacy removal is done.

## Host maintenance ingress, retention and subscription consumers

The coordinator accepted the fixed acquisition increment. Startup requests now
exercise the common retained owning head: no logical credits or fallback storage
are reserved during capture; a blocked head permits private-control progress and
does not keep the Host runnable. Startup, stop, terminal and emergency transitions
reschedule that head, including the next retained head after its ring is empty.
Early Executive self-change keeps the first owner and rejects stale/duplicate
requests through the same current dispatch path.

Seal tests capture both ordinary lanes during native maintenance without dispatch,
then account their independent prefixes and allow later settlement. Retained native
ingress tests keep one head plus queued owners through cancellation/deadline
failure, reject all 32 through normal dispatch after native return, and only then
release the subscription pin. Terminal batch tests retain outstanding work/returns
and escalate fatal loss of their owner. Preparation-failure tests use a valid common
execution descriptor with rejected binding provisioning, then prove failed join
retains execution/notification state without ever calling create or destroy.

The restriction subscriber is now a module-owned callback object driven by the
actual serviced preamble. Its deterministic notification race signals during a
callback, then requires a subsequent observation and stable quiet waiting; a
transient Waiting state before the pending epoch is consumed is not sufficient.
It also verifies restriction recovery, affine exactly-once destruction, and
subscription retention across an injected join failure. This uses same-image
callbacks, not a rebuilt-DLL boundary or native Platform event loop.

`build/preamble-maintenance-wake-build.log` has zero warnings/errors.
`MorphicTests.exe --host-maintenance --log-tag=preamble-maintenance-wake` passed
263 assertions, zero failures. Earlier focused runs covered startup (84/0) and
retention (234/0); the subscription migration initially exposed the transient-wait
fixture assumption described above. Other HostMaintenance child/direct scenarios,
remaining real-DLL consumers, native acceptance and final configurations remain
outstanding. The focused selector contains only the migrated/current cases.

## Remaining Host maintenance child/direct consumers

The coordinator accepted the preceding retention/subscription package. The rest
of HostMaintenance now uses current execution descriptors, private lifecycle
controls and module-owned callback classes. Duplicate address creation, exact
creation/ended/retired order, stale and foreign stop identities, provisioning
failure, and same-native-thread exactly-once destruction remain covered. A stopped
requester explicitly completes domain cleanup while its accepted child is still
live; the preamble holds the requester until child retirement and delivers each
correlated lifecycle event once. The fixture selects that requester by address and
recorded identity, since the Host child collection has unordered live traversal.

An Executive child publishes 64 ordinary settlement messages, then requests Host
stop through the installed execution context. Private creation is drained separately
so ordinary messages remain unread while native-maintenance stop polling closes
admission; cleanup remains held until explicitly released. Readiness cases cover
no readiness publication, module stop, stop before/after readiness observation,
seal guard, provider closure, failed preparation and closing direct state. Two
actual serviced peers establish their direct connection regardless of whether
Host publishes the creation outcome before consuming readiness; their controls
never count as ordinary domain dispatch. The seal-guard case restores its injected
Host guard before normal frontier processing rather than fabricating an early
destruction grant.

Pumped direct-pressure cases now use the actual 1,024-slot pipe and facade calls
only inside callbacks. Next-allocation failure stays armed while filling it and
retrying the extra message: no allocation, fatal state or busy loop occurs. A Host
message still dispatches while direct input is paused for retained output. Returning
capacity preserves all 1,025 messages in order; closing instead cancels unread
direct input and the retained response before retirement. The old growable-direct-
queue poisoning variant is removed because that transport no longer exists in the
common path. Accepted-obligation loss/global failure stays covered by the earlier
current transport failure checkpoint.

`build/preamble-maintenance-readiness-build.log` has zero warnings/errors.
`MorphicTests.exe --host-maintenance --log-tag=preamble-maintenance-readiness`
passed 587 assertions, zero failures. Earlier current-binary increments were
358/0 for child identity/outcomes, 379/0 with unread-input Host stop, and 429/0 with
direct pressure. The first direct build used an incorrect test payload type and
did not compile; an inadvertently launched old binary after that failure is
excluded from evidence. The current direct payload build and final readiness
build succeeded. Tracked line endings and `git diff --check` pass. These are
same-image actual serviced/pumped plus controlled Host cases, separate from the
outstanding real-DLL/native acceptance and final configuration builds. No legacy
module thread-entry, raw endpoint or Host-pump API remains in this test file;
the independent Host thread startup rollback fixture retains its worker path.

Coordinator review found the direct-pressure fixture armed allocation index
`attempts + 1` although the allocator increments its attempt counter after
capturing the index. It now arms `attempts` and restores `SIZE_MAX`. The existing
unchanged-attempt-count assertion already proved no allocation occurred; this
correction aligns fault injection with the stated next-allocation intent.
`build/preamble-maintenance-allocation-build.log` has zero warnings/errors;
`--host-maintenance --log-tag=preamble-maintenance-allocation` passed 587/0.

## BundleService common recipients

The coordinator accepted the corrected HostMaintenance checkpoint. BundleService
now separates its controlled common recipients from the actual echo Host worker
used for file-save jobs. Recipient responses never pass through that worker.
Immediate requests have explicit dispatch-local storage and logical credits;
bounded output passes retry existing asset/bundle operation results after the
dispatch scope. Worker completion remains on its original transport.

Ownership, bundle/root closure, retained file views, save/disposal interleavings,
dependency cleanup, acquisition release, same-address recipient replacement,
and allocation failure sweeps remain covered. Final bundle notification under a
full 32-slot ordinary ring retains its logical obligation in the bundle record;
capacity recovery delivers exactly one uncorrelated notification before a FIFO
barrier. Full acquisition output transfers to the fallback FIFO without failure.
The cancellation case preserves the existing raw/bundle rule: no remaining
execution or batch users permits acquisition cancellation at terminal state,
while the unread fallback result cannot publish and remains stored until final
ingress quiescence. Schema/report cases requiring the stronger quiescence gate
remain separate. An initial fixture expectation incorrectly required raw/bundle
acquisition records themselves to survive until that stronger proof; the fixture
was corrected without changing production retirement policy.

`build/preamble-bundle-retirement-build.log` has zero warnings/errors.
`MorphicTests.exe --bundle-service --log-tag=preamble-bundle-retirement` passed
760 assertions, zero failures. No combined legacy Host-client fixture or raw
module endpoint remains in BundleService_test_suite.cpp. Its recipient quiescence
is explicitly controlled; this is not new native module affinity evidence.
BundleRequests, resource helpers and remaining real-DLL consumers still require
migration before shared legacy removal and final rebuilt configurations.

## BundleRequests and Rendering resource consumers

The coordinator accepted the BundleService checkpoint. BundleRequests now uses
controlled common recipients with 32-slot ordinary rings; its native file-I/O and
conditioning workers retain their existing transport and execution paths. Immediate
requests reserve dispatch-local storage and logical credits. Deferred asset,
schema and bundle responses progress in bounded passes from their operation
records. Concurrent cache-load results are checked by their two distinct
correlations, since output rotation does not promise order across operations.
Cache reuse, bundle/save interleavings, retained schema views, worker failure,
ownership and teardown cases remain covered. The new focused selector excludes
the separately validated SchemaService suite.

Rendering's resource helper likewise uses a common recipient, scoped immediate
acquire/release/disposal requests and explicit operation-output progress. Its
actual batch owner settles before the controlled recipient becomes quiescent.
Existing busy, failed submission, held release, cancellation and texture resource
cases remain covered. The unused combined `CHostClientFixture` implementation and
its Visual Studio entries are removed.

`build/preamble-bundle-requests-correlation-build.log` and
`build/preamble-resource-common-build.log` have zero warnings/errors. Focused runs:

- `MorphicTests.exe -t1 --bundle-requests --log-tag=preamble-bundle-requests-correlation`:
  1,924 passed, zero failed.
- `MorphicTests.exe -t1 --texture-service --log-tag=preamble-resource-common`:
  162 passed, zero failed.

An initial BundleRequests migration run assumed submission order for simultaneous
cache results; the corrected fixture checks correlation without changing runtime
scheduling. These are controlled recipient/service and actual worker/batch tests,
not new real-DLL or native Platform acceptance evidence. Remaining direct/lifecycle
consumers, shared legacy paths, final version-1 rebuilds and native/configuration
validation are still outstanding.

Coordinator review found the resource destructor ignored its bounded cleanup
wait result. It now diagnoses and aborts the test process on timeout before
resetting requests, declaring quiescence or releasing asset storage. Intentional
rejected-release cleanup remains valid only once no batch jobs remain. The next
consumer build, `build/preamble-bulk-common-build.log`, has zero warnings/errors;
`--texture-service --log-tag=preamble-resource-cleanup` passed 162/0. The coordinator
accepted this correction and the BundleRequests/resource checkpoint. All remaining
endpoint probes and command receivers use common channels, so the unused legacy
`SEndpointFixture` and their raw-endpoint fallback branches are removed.

## Common-preamble bulk connection consumers

Bulk buffer, configuration, request validation, allocation and wake-failure tests
now run facade operations inside module-owned callback objects driven by actual
common preambles and Host lifecycle routing. The fixture action is a single
synchronous test command consumed inside a callback, not an outbound message
queue or an unauthenticated facade adapter. Both initiation directions and
reciprocal requests preserve one shared token and correlated results. POD and
bulk connections coexist; capacity/producer mismatches remain distinct failures.

The current path preserves three-buffer rotation, alignment, latest-state
acquisition after notification hints clear, and held bytes across repeated
publication. Invalid bulk requests and backing allocation failure settle once
without leaking state. Held-view closure retains backing and both notification
pins until the consumer relinquishes its view and reports quiescence. An injected
unavailable notification endpoint proves publication remains committed even when
the producer reports published-wake-failed; the peer acquires those bytes before
Host consumes closure, no retransmission occurs, and failure remains latched.
Both module objects destroy once on their creating thread after retirement.

`build/preamble-bulk-retention-build.log` has zero warnings/errors.
`MorphicTests.exe -t1 --common-bulk --log-tag=preamble-bulk-retention` passed
17,389 assertions, zero failures (the earlier buffer/configuration increment was
17,072/0). Migrated legacy bulk cases are removed from DirectConnections; its
normal entry invokes the replacements. Standalone bulk transport concurrency and
allocation tests remain unchanged. Remaining legacy partial-installation,
activation, native-join and multi-endpoint cases still require migration. This
checkpoint is same-image pumped execution/controlled Host evidence, not a claim
of new serviced native-join or rebuilt-DLL coverage.

## Common connection establishment races

The coordinator accepted the bulk checkpoint. Partial-installation and activation
cases now use the same actual preamble callbacks. A late installation cannot
override stop; pins and module destruction remain held until the other side
quiesces. Delaying one actual private installed acknowledgement keeps both sides
inactive. Completing it enables each side only after its availability callback;
the requester still waits for both activation acknowledgements. Expiry before
completion fixes the timed-out result and a late acknowledgement cannot reopen it.
These cases run for both POD and bulk connections.

Crossed bulk versions, unregistered participants and duplicate pending requests
retain their distinct results. A captured reciprocal request delivered during
closing is refused; after retirement it either creates a fresh token or observes
closed admission. Private lifecycle saturation holds the second activation and
all success results until capacity returns. Controlled padding is removed before
module invocation. A further request on the completed connection returns
already-connected without another availability notification. Obsolete equivalents
in DirectConnections are removed; its normal entry invokes the current cases.

`build/preamble-establishment-pressure-build.log` has zero warnings/errors.
`MorphicTests.exe -t1 --common-establishment --log-tag=preamble-establishment-pressure`
passed 748 assertions, zero failures (155/0 before the delayed-request and private
pressure cases). This extends controlled Host/same-image pumped evidence only;
native join retention, remaining multi-endpoint consumers, real-DLL migrations,
native acceptance and final configurations remain outstanding.

## Common direct POD consumers

The coordinator accepted the establishment checkpoint. Direct POD exchange now
checks payload/correlation and authenticated peer address, endpoint and connection
identity through the common callback. A stale token cannot target its successor.
The actual 1,024-slot ring fills with next-allocation failure armed without any
allocation or fatal state; both paused recipient and full-output producer remain
non-runnable. Capacity recovery admits the retained 1,025th message once and
preserves FIFO. Closure instead cancels the full unread prefix in bounded passes
without domain delivery. The repeated data/capacity notification regression is
included in the focused entry.

Injected notification failure after publication or after consumption latches the
appropriate endpoint failure while delivering the accepted POD once before Host
closure. Retirement closes admission for the old exact endpoint; an independently
controlled replacement can register at its logical address. The combined legacy
two-peer fixture and unused `SDirectPeerFixture` are removed. Module growable-queue
poisoning coverage is removed with that obsolete transport; current full-refusal,
accepted wake failure and earlier fatal-loss tests cover the replacement contract.

`build/preamble-direct-pod-retirement-build.log` has zero warnings/errors.
`MorphicTests.exe -t1 --common-pod --log-tag=preamble-direct-pod-retirement` passed
2,187 assertions, zero failures. The first migration run exposed two fixture
assumptions: cancellation of all 1,024 unread PODs takes more than eight bounded
turns, and a standalone registration outcome needs its retained control flushed.
Both are corrected without runtime changes. Remaining direct native-join and
multi-endpoint tests still use legacy helpers and remain to be migrated.

## Direct native-join retention consumer

The coordinator accepted the POD checkpoint. The direct native-retention test
now starts a real `CThreadPackage` through its common preamble. Its deliberately
held create callback publishes terminal progress before native return, then can
still signal the connection's notification target. Host participation, consent
and descriptor exposure are controlled test boundaries; the native side never
consumes its lifecycle lane or creates module-owned state. The callback eventually
refuses creation with a null state, so no destroy callback is permitted.

For both POD and bulk, terminal progress plus controlled empty ordinary ingress
does not reclaim backing or either notification pin while native execution is
live. An injected join failure also retains them after actual native return.
Successful join alone still leaves the pins until direct retirement progresses;
then the peer receives retirement and both pins release. A timeout terminates
the test process before any live native fixture storage could be destroyed.
The old raw module thread-entry version is removed. This is specifically actual
native lifetime proof at a controlled creation-failure boundary, not full native
connection establishment or normal affine module-state destruction coverage.

`build/preamble-direct-native-current-build.log` has zero warnings/errors.
`MorphicTests.exe -t1 --common-native-join --log-tag=preamble-direct-native-current`
passed 49 assertions, zero failures. An initial compile exposed a missing helper
include and a still-used legacy test wait helper removed too early; these were
corrected before running the current binary. Multi-endpoint legacy consumers and
the remaining real-DLL/native/final-configuration work remain outstanding.

## Controlled common endpoint populations

The coordinator accepted native-join retention. Multi-endpoint cases now install
current common descriptors in the Host endpoint collections. The fixture drives
all callbacks sequentially on its owner, including non-Host logical thread roles;
it sets their controlled preparation state without creating native threads. This
preserves address/role interleaving coverage while explicitly excluding native
driver/affinity qualification. Actual Host routing, consent, private transitions
and preamble authentication are used. Callback state retains one test action and
bounded observations; no generic message outbox is introduced.

Seven same/cross-module role combinations preserve authenticated direct identity,
third-party requester authority, exact-token closure and successor rejection.
Reciprocal and third-party requests share offers while duplicate pending requests
are refused. Explicit accept and reject decisions complete the expected results.
Requester limits retain eight pending obligations and refuse exactly one of nine;
terminal departure is modeled by an explicitly unactivated controlled requester,
so no live preamble loses an awaited result. Remaining requesters complete without
closing the pair. Per-endpoint and global connection limits remain enforced.
Private result pressure retains copied identities after participant retirement;
returning capacity delivers the single success. Repeated correlation reuse waits
for each generated result before the next request.

`build/preamble-direct-multi-order-build.log` has zero warnings/errors.
`MorphicTests.exe -t1 --common-multi --log-tag=preamble-direct-multi-order` passed
1,318 assertions, zero failures (310/0 for the initial pairing/consent increment).
The first limit run incorrectly assumed that Host's unordered endpoint traversal
refused the last inserted requester. The fixture now checks the one refusal and
all accepted correlations independently of traversal order. Remaining source
selection, in-flight signalling, deferred quiescence and address-replacement
legacy cases are still being migrated; this is not final consumer completion.

## Remaining direct consumers and fixture removal

The coordinator accepted the controlled endpoint-population checkpoint. The last
direct consumers now use the same common fixture. Host-shaped peer payloads retain
their authenticated direct source; paused direct input still permits Host input
and private closure, and all resumed ordinary sources preserve their own FIFO.
Pressure selection replaces the former exact round-robin expectation.

A real native operation holds the private admission guard across publication and
delayed signalling. Closure rejects new operations while retaining the descriptor,
backing and both notification pins until the operation returns and explicit peer
quiescence completes. The worker is the sole producer on that pipe during this
controlled test; it does not exercise foreign-thread facade access. A saturated
private output lane retains quiescence without runnable spin. Returning capacity
finishes retirement; stale installation/activation cannot reopen the connection,
and exact-token lookup rejects an obsolete descriptor before reading it. Logical
address reuse receives a new endpoint identity; controls from the retained old
record cannot close or quiesce its successor.

`DirectConnections_test_cases.cpp` retains only standalone Foundation bulk tests
and calls the current common groups. Its legacy endpoint/client fixture code is
removed. `direct_connection_fixture.hpp` now contains only private Host test seams
and controlled Foundation worker setup; the Schema worker call is explicitly named
`initialise_worker`. No raw module direct client remains in either direct test file.

`build/preamble-direct-current-build.log` has zero warnings/errors.
`MorphicTests.exe -t1 --common-multi --log-tag=preamble-direct-current` passed
2,039 assertions, zero failures. This extends sequential common callback/address
coverage and the explicitly controlled native signal lifetime case; it does not
claim native endpoint establishment or replacement-loader qualification. Remaining
real-DLL lifecycle/pump/input consumers, production legacy removal, native
acceptance and final version-1 rebuilt configurations are still outstanding.

## Lifecycle Service, passive and startup-failure DLL variants

The coordinator accepted the remaining direct-consumer checkpoint. The lifecycle
Service variant now owns a `CServiceExecution` class with its bound facade and
explicit transfer/acquisition/disposal/release state. Its old receive loop and raw
service entry are removed. A retained owning transfer is cancelled only before
acceptance; accepted transfer/acquisition results remain awaited during stop.
Held bytes remain valid until release publication, and domain completion requires
both release and disposal acknowledgement. The independent asset obligation does
not prevent quiescence of a completed direct test conversation. Its bounded count
of repeated test results retries under pressure; busy output is not runnable.
Ordered closure cancels unpublished test results. Startup/cleanup failure reports
are latched once so later valid cleanup progress remains visible.

The Service and deliberate RenderingFailure variants export the current execution
descriptor with a Host standard coordinator and serviced child. Passive exports
the empty standard coordinator. Thin adapters own allocation/destruction on the
execution thread; none of these variants exports the old host-pump entry. The
Executive and RenderingDisposal variants in the same source remain explicitly
unmigrated. Their old export remains only for those variants until removal.

`MorphicTests.exe -t1 --lifecycle-service --log-tag=preamble-lifecycle-notification`
passed 137 assertions, zero failures. Controlled facade cases stop at each custody
boundary, inject repeated busy and accepted-wake-failure outcomes, retain release
until acceptance, and require both completions even when disposal arrives first.
The actual Service DLL additionally runs through Host's standard coordinator and
native child, verifies image/thread identity, joined affine allocation/destruction
and empty attribution. This idle DLL case does not yet exercise its asset protocol
through the real Host; the full lifecycle Executive consumer remains to migrate.
The first DLL runs caught an omitted participation correlation and use of the
wrong notification status field; both are fixed, with the fake facade/notification
fixture corrected to match the public contract. Cleanup explicitly tolerates the
provider-admission-closed event.

`build/preamble-lifecycle-notification-tests-build.log` and
`build/preamble-lifecycle-notification-dll-build.log` have zero warnings/errors.
Passive and RenderingFailure were rebuilt cleanly in
`build/preamble-lifecycle-Passive-final-build.log` and
`build/preamble-lifecycle-RenderingFailure-final-build.log`. The placement policy
now classifies the fixture's existing memory-token allocation pattern.

`tests/module_lifecycle/run_tests.ps1 -Configuration Debug -Platform x64 -SkipBuild -Cases normal-bad-start`
passed with the rebuilt failure DLL and production Executive/Host. Its maintained
expectation now checks the common endpoint failure record, correlates the failed
Rendering endpoint with creation/retirement, and requires Executive join,
retirement and worker-side unload. The process exits with failure as expected and
does not reach asset acceptance. The existing script needed sandbox escalation
to create its isolated junction. Evidence is in
`build/lifecycle-runtime-ff25f9a2/development/logical-roots/test-logs/morphic_debug.lifecycle-ff25f9a2-normal-bad-start.p85408.log`.
Remaining lifecycle Executive/RenderingDisposal, pump/input consumers, legacy
removal, native acceptance and final configurations are not complete.

Coordinator review found that a consumed transfer/acquisition failure still left
its wait active. Recognized terminal refusals now settle that wait before reporting
the one-time failure: transfer refusal has no asset obligation; acquisition refusal
retains the original asset and disposes it during cleanup. Contradictory result
tokens or malformed/unmatched replies keep their unresolved custody. Recognized
release/disposal failures also end their result wait, but explicitly retain unsafe
state because success of the required retirement operation was not established;
they do not spin or await another copy of the consumed result. Existing Host fatal
retention remains responsible for such deliberately unproven cleanup.

Focused refusal cases verify the single failed callback followed by successful
cleanup invocations, transfer completion without a new reply, acquisition cleanup
only after disposal acknowledgement, and conservative release/disposal failure
retention. `MorphicTests.exe -t1 --lifecycle-service --log-tag=preamble-lifecycle-terminal`
passed 235 assertions, zero failures. Both
`build/preamble-lifecycle-terminal-tests-build.log` and
`build/preamble-lifecycle-terminal-dll-build.log` have zero warnings/errors. The
passing production startup-failure case was not repeated for this service-only fix.

## Lifecycle Executive module and asset cases

Binary-provenance correction (23 November): the process runs in this section,
the preceding `normal-bad-start` run and the following batch section used
`D:\TheMorphicEngine\build\bin\x64\Debug\MorphicEngine.exe` last built on
22 November at 19:26 UTC, before later common-lifecycle Host edits. Their observed
loader/native results remain valid for that binary and the rebuilt fixture DLLs;
the coordinator's scoped acceptance does not qualify current-source Host code.
Affected process coverage must be reconciled against the final current-source
build. The controlled tests compile their Host sources into `MorphicTests.exe`
and retain their separately recorded build provenance.

The coordinator accepted the Service terminal-result correction. The lifecycle
Executive now exports only the current execution descriptor, with a standard Host
coordinator and a module-owned `CExecutiveExecution` serviced child. Its old thread
entry/query mapping is removed. Named module, transfer, disposal and self-stop
operations retain their request until transport acceptance, then consume correlated
acknowledgement/completion without resubmission. A pending self-replacement waits
for stop rather than inventing an ordinary completion. Unexpected stop cancels
unpublished requests; existing Host quiescence accounts/cancels unread results.
No borrowed views or affine resources are owned by these initial scenarios.

The fifteen implemented scenarios are ordinary, shutdown, render-shutdown,
dependency, disposal, replace, replace-dependency, shutdown-dependency,
render-shutdown-dependency, render-replace-dependency, render-executive-replace,
pump-required, replace-missing, startup-failure and replace-startup-failure. The
ordinary case no longer requests the removed Rendering raw thread function on
its successful first load; its explicit unsupported-function rejection case remains.
The deliberately absent execution-export fixture retains its historical case name
temporarily. Batch, bundle, direct conversation and RenderingDisposal scenarios
still require migration. Their old helper bodies remain unreachable migration
reference in the shared fixture source and must be removed before completion.

`build/preamble-lifecycle-executive-build.log` and the subsequent deliberate-failure
diagnostic build `build/preamble-lifecycle-executive-failure-build.log` are clean.
The new controlled request test covers repeated busy, exactly-once publication
after accepted-wake-failure, acknowledgement before completion, no next operation
before completion, and cancellation of the next unsent request on stop.
`MorphicTests.exe -t1 --lifecycle-executive --log-tag=preamble-lifecycle-executive-retention`
passed 38 assertions, zero failures; its build log is
`build/preamble-lifecycle-executive-tests-build.log` (zero warnings/errors).

All fifteen cases passed via `tests/module_lifecycle/run_tests.ps1 -Configuration
Debug -Platform x64 -SkipBuild -Cases ...`, in scoped groups without repeating the
already passing cases. Runtime evidence directories are:

- `build/lifecycle-runtime-03178f78`: shutdown.
- `build/lifecycle-runtime-4f382d58`: render-shutdown, dependency, disposal, ordinary.
- `build/lifecycle-runtime-e573f99a`: the seven positive replacement/dependency and
  absent-export cases listed above.
- `build/lifecycle-runtime-34e7c88b`: replace-missing.
- `build/lifecycle-runtime-67a25b60`: startup-failure and replace-startup-failure.

The maintained checker uses current Rendering resource initialization/teardown
events, checks DLL/thread identity and teardown before join, and retains the old
custom RenderingDisposal event checks until that variant migrates. Current
Executive failures require both the common endpoint failure and lifecycle failure,
with the exact failed endpoint joined and retired. These are actual loader and
replacement-file cases, extending the earlier controlled-binding failure evidence.
Remaining consumers, native acceptance, complete legacy removal and final affected
version-1 configurations/artifacts are still outstanding.

## Lifecycle Executive batch ownership

The coordinator accepted the initial Executive module/asset checkpoint. Batch,
batch-inline and batch-exit now run as bounded asynchronous states in that same
module-owned object. The specialized `CBatchClient` path remains unchanged. Sixteen
normal items verify module, thread and memory attribution; inline completion is
distinguished from queued responses. The exit case holds the first native item
through the existing batch client's exit-request observation, submits seven more,
and asks Host to stop Executive. One item executes and seven are discarded under
the existing runner cancellation policy. All accepted item storage remains in the
object until its response is consumed. Domain completion and the success log are
held until the outstanding count reaches zero and observations are validated.
Waiting for the first held item uses a one-millisecond timed wake; response waits
use the existing batch notification path. No new worker or transport is introduced.

The former blocking batch helpers and their legacy scenario branches are removed.
`build/preamble-lifecycle-batch-current-build.log` has zero warnings/errors.
`tests/module_lifecycle/run_tests.ps1 -Configuration Debug -Platform x64 -SkipBuild -Cases @('batch', 'batch-inline', 'batch-exit')`
passed all three actual Host/DLL cases. Their logs are under
`build/lifecycle-runtime-5dc337b0/development/logical-roots/test-logs/`, with process
IDs 88024, 27604 and 26600 respectively. Earlier passing module/asset matrices were
not repeated. Bundle/direct/RenderingDisposal consumers and other outstanding
implementation/final-qualification work remain open.

## Replacement completion retention investigation

The current lifecycle Executive Service conversation exposed an unavailable
connection immediately after replacement readiness. Host and its dependencies
were rebuilt through `MorphicEngine.sln /t:MorphicHost /m:1 /p:Configuration=Debug
/p:Platform=x64 /p:PreferredToolArchitecture=x64`, with clean log
`build/preamble-lifecycle-host-current-build.log`. The failure persisted. Temporary
diagnostics, rebuilt in `build/preamble-lifecycle-host-diagnostic-build.log`, showed
the exact new Service simulation endpoint registered for direct communication but
already marked stopping. The diagnostic run is
`build/lifecycle-runtime-755e465c/development/logical-roots/test-logs/morphic_debug.lifecycle-755e465c-service-lifecycle.p57052.log`.
It used the Debug x64 binary directory above: `MorphicEngine.exe` (23 November
01:56:33 UTC), `MorphicLifecycleExecutive.dll` (01:52:31),
`MorphicLifecycleService.dll` (01:36:52) and `MorphicLifecyclePassive.dll` (01:29:51).
The script's isolated roots affect fixture data; this case loads these executable
directory binaries.

The retained completed module result kept its original replacement request and
previous-binding pointer alive. Host's pre-dispatch teardown check did not exclude
that result-publication phase, so it stopped the new successor. One shared
`needs_dispatch()` predicate now excludes retained completed results before
teardown, dependency disposal and worker dispatch. Output retry remains separate.
Temporary direct-connection trace logging is removed.

The regression injects the retained completed-replacement boundary after an actual
common pumped successor has become ready and participated. It executes twelve
`CHost::run_turn()` calls with a controlled recipient's 32-slot output ring full,
checking the same successor remains running/registered, no emergency or worker
redispatch occurs, and its result credit remains. Capacity recovery publishes one
correlated result; a successful disposal then proves its dependent asset was not
disposed again. Explicit stop still retires and destroys the successor on its
creating thread. The worker package is deliberately unlaunched, and the controlled
recipient retries its output explicitly outside the module registry. This is
Host-turn regression evidence, not a loader/native-worker replacement test.
`MorphicTests.exe -t1 --lifecycle-executive --log-tag=preamble-replacement-final`
passed 106 assertions, zero failures, including the earlier request-retention
cases. `build/preamble-replacement-retention-final-build.log` is clean.

The real `service-lifecycle` process now passes with the same fixture DLLs and
the corrected `MorphicEngine.exe` (23 November 02:04:02 UTC), rebuilt in clean
`build/preamble-replacement-result-host-build.log`. Evidence:
`build/lifecycle-runtime-e797358a/development/logical-roots/test-logs/morphic_debug.lifecycle-e797358a-service-lifecycle.p74004.log`.
It exercises direct requests, copied-result cancellation, acquisition/disposal
cleanup, another replacement with a dependent asset, fresh connection identity,
subsequent ordinary unload and final shutdown. Both real Service endpoints join
and retire before their DLL unloads. No earlier passing matrix was repeated.
The coordinator accepted this narrow runtime correction and its regression/process
evidence. Broader Executive direct-state review is recorded separately below.

## Lifecycle Executive direct Service cases

The Executive execution class now handles Service participation, consent,
availability/result correlation, direct source address/endpoint/token validation,
close/quiescence and retirement through the installed facade. Named connect, test
and retire operations retain busy submissions and never resubmit accepted or
accepted-wake-failed work. Each reconnect requires a fresh token. The lifecycle
case deliberately retains fifteen copied result records after receiving sixteen,
then cancels those copies on closure before reusing the connection state; this
preserves the old fixture's copied-result test, not a claim about physical unread
ring entries. Direct input pauses while those copies are retained. Stop/closure
can cancel these POD conversations; Service-owned acquired bytes remain protected
until the Service's own release/disposal acknowledgements permit retirement.

`service-shutdown` passed using the corrected Host and newly rebuilt Executive:
`build/lifecycle-runtime-a34372dd/development/logical-roots/test-logs/morphic_debug.lifecycle-a34372dd-service-shutdown.p19088.log`.
The startup-failure case publishes the Service load once, accepts its ordinary
acknowledgement if delivered and waits for global stop. Its completed failure may
be cancelled by Host quiescence; receipt never starts another operation. The
maintained checker now requires exactly one Service domain failure, its exact
common failed endpoint joined/retired, Executive joined/retired, and worker unload.
`service-start-failure` passed after that checker migration:
`build/lifecycle-runtime-c9bab842/development/logical-roots/test-logs/morphic_debug.lifecycle-c9bab842-service-start-failure.p11904.log`.
The prior run already produced the expected runtime behaviour but failed the old
diagnostic expectation. `build/preamble-service-executive-final-build.log` is clean.
All paths refer to `build/bin/x64/Debug` binaries; Service/Passive and corrected
Host provenance are as above. The old blocking `service_lifecycle_case` and its
unreachable legacy branch are now removed, with clean
`build/preamble-service-helper-removal-build.log`; this final rebuild changes no
selected execution behaviour. Audio/direct Rendering, bundle/RenderingDisposal,
other legacy consumers, native acceptance and final configurations remain open.

## Lifecycle Executive direct Audio case

The coordinator accepted the direct Service-state checkpoint. The same Executive
class now selects the Audio address and validates either one successful test or
the supported unavailable result. Two runs must complete before a third request
is published. That third accepted conversation pauses direct input while ordinary
Host unload proceeds, so even a queued direct result can be cancelled by closure.
Unload completion and connection retirement precede Executive self-stop. The
obsolete blocking Audio helper and unreachable branch are removed.

`build/preamble-audio-lifecycle-current-build.log` and
`build/preamble-audio-helper-removal-build.log` are clean. The actual corrected-Host
`audio-lifecycle` run passed at
`build/lifecycle-runtime-f25b9e54/development/logical-roots/test-logs/morphic_debug.lifecycle-f25b9e54-audio-lifecycle.p36556.log`.
It records two real WASAPI completions, a third device run cancelled during unload,
then Audio join/retirement and worker unload before Executive completion.
The focused supplied-facade cases cover repeated busy publication, accepted wake
failure without resubmission, both success/unavailable completions, stop before
the third request is accepted, and accepted-third-request cancellation while
unload itself is busy. `MorphicTests.exe -t1 --lifecycle-executive
--log-tag=preamble-audio-conversation-final` passed 164 assertions, zero failures
(including the existing Executive request and replacement-result cases).
`build/preamble-audio-conversation-notification-build.log` is clean. An initial
fixture compile typo and default failure-valued notification were corrected before
this passing result; they did not change the selected DLL behaviour. No passing
process matrix was repeated for removal of unreachable helper code.

For the remaining Rendering conversation case, the coordinator agreed to replace
the obsolete module-owned native-thread return trigger with explicit Executive
self-stop while a POD conversation is pending, and to rename the maintained case
accordingly. Callback return is now ordinary callback completion; Host owns the
native loop. Existing controlled held-create/join-failure evidence remains
complementary native lifetime coverage, not an active-handler abrupt-return or
crash claim. Active callback-failure/global-shutdown coverage remains separate.

## Rendering acquisition release and abandonment

The coordinator accepted the Audio checkpoint. RenderingDisposal now selects the
current execution descriptor for its three acquisition scenarios. The fixture's
module-owned acquisition state is shared with Service via a test-local scenario
enum; Service remains its default configuration. Rendering retains the original
asset through the test conversation. Explicit release on stop requires both
release and disposal acknowledgements before domain completion. Deliberate
abandonment requires accepted disposal, validates the bytes after stop, clears the
local acquisition/view and ceases byte access before reporting domain completion.
Busy disposal leaves completion false and work_pending false until a capacity wake.
This does not grant endpoint or DLL retirement: common admission/frontier and
direct-quiescence proofs still precede affine destruction/native completion;
Host then reclaims the raw acquisition and settles disposal/reply obligations
before endpoint retirement and unload. No public interface or runtime lifetime
policy changes were made for these scenarios.

The actual rebuilt-DLL cases `acquire-render-release`, `acquire-render-abandon`
and `acquire-render-replace` passed in
`build/lifecycle-runtime-8ff9ce3e/development/logical-roots/test-logs/`, process IDs
15040, 11176 and 16000 respectively. The replacement case holds Rendering's bytes
through Executive replacement until the new Executive requests shutdown. Build
logs `preamble-rendering-acquisition-dll-build.log` and
`preamble-rendering-acquisition-executive-build.log` under `build/` are clean.

Review caught a misleading later "drained" log after deliberate abandonment.
That log now requires explicit completed cleanup; abandonment emits a separate
view-relinquished marker. The strengthened Debug checker compares global event
ordinals rather than concurrent file line ordering: explicit cleanup precedes
destruction, while abandoned disposal completes after destruction and before
endpoint retirement/unload. The corrected abandonment case passed at
`build/lifecycle-runtime-edd0ef6e/development/logical-roots/test-logs/morphic_debug.lifecycle-edd0ef6e-acquire-render-abandon.p89348.log`.
`build/preamble-rendering-abandon-final-build.log` is clean. The previous release
and replacement cases were not repeated for this log-only correction.

Supplied-facade cases cover busy disposal preventing completion, accepted wake
failure without resubmission, explicit acknowledgement gating, and subsequent
callbacks after the abandoned local view is cleared. Together with the existing
Service custody cases and rebuilt Service DLL idle lifetime fixture,
`MorphicTests.exe -t1 --lifecycle-service --log-tag=preamble-render-acquisition-current`
passed 280 assertions, zero failures. The test build and Service rebuild logs are
`build/preamble-rendering-acquisition-tests-build.log` and
`build/preamble-rendering-acquisition-service-build.log`, both clean. Earlier
280/0 included the prior Service DLL; the stated current run uses its rebuilt
replacement. Old blocking acquisition helpers/branches are removed. Remaining
RenderingDisposal test-routing, malformed completion, final disposal and bundle
domains are explicitly unselected/unmigrated; their old code/entry remains
temporary migration reference until removed. No full RenderingDisposal or final
configuration qualification is claimed.

## Reverse Rendering conversations and cooperative self-stop

The coordinator accepted the acquisition checkpoint. Rendering's two reverse
requests now run through explicit asynchronous states: the first tests Executive
destination/correlation/counts, then Rendering transfers/acquires an asset,
publishes disposal and sends the second request. Normal completion validates the
failed test result and closes the conversation. Caller unload or Executive
self-stop cancels the pending second request. Rendering withholds connection
quiescence until its accepted acquisition release and disposal both acknowledge;
Executive may finish its own POD-only domain work earlier, but common connection
retirement keeps both native endpoint lifetimes gated by that cleanup.

The maintained case is now `tests-self-stop`. Its old raw-thread-return name and
blocking helpers/branches are removed. It invokes ordinary cooperative Executive
self-stop, as agreed, and makes no active-handler native-return/crash claim.
Executive retains a single named reply across busy output and validates exact
Rendering address, endpoint, connection and correlation. Rendering validates the
reciprocal source identity. Recognized connection refusal consumes its wait before
reporting failure, so subsequent cleanup does not await a duplicate result.
Stop cancels pending test publication even when it precedes the close event.

The three current Host/rebuilt-DLL cases `tests-routing`, `tests-self-stop` and
`tests-client-unload` passed with the final stop-order/source checks and strengthened
global-ordinal retirement assertions under
`build/lifecycle-runtime-921a136b/development/logical-roots/test-logs/`, process IDs
10996, 46956 and 92888 respectively. Both endpoints join/retire after dependent
conversation cleanup. Earlier passing runs in `lifecycle-runtime-3cf9ff14` preceded
those refinements. Final role builds have zero warnings/errors:
`build/preamble-routing-final-RenderingDisposal-build.log`,
`build/preamble-routing-final-Executive-build.log`, and
`build/preamble-routing-final-Service-build.log`.

Controlled cases cover busy first/second direct publication, busy asset transfer,
busy disposal and release, accepted-wake-failed exactly-once handling, both
completion/cancellation paths, stop before close, and withholding quiescence until
both cleanup acknowledgements. A refused connection reports failure once and then
permits cleanup completion. With earlier Service/acquisition cases and the current
Service DLL idle fixture, `MorphicTests.exe -t1 --lifecycle-service
--log-tag=preamble-render-routing-current` passed 345 assertions, zero failures.
`build/preamble-rendering-conversation-stop-build.log` is clean. Malformed Rendering
completion, final disposal/save and bundle domains, remaining consumer migrations,
legacy removal and final current-source configurations remain outstanding.

## Intentional malformed Rendering completion

The coordinator accepted the reverse conversation checkpoint. The malformed reply
fixture now uses the same module-owned class/facade and current execution export.
It retains one intentionally wrong-correlation completion across busy publication,
and logs only after acceptance. Its old blocking helper and entry branch are gone.
The production Executive detects the mismatch, reports its module-tests failure,
and common endpoint failure triggers global shutdown. The checker verifies exactly
one Executive endpoint failure and orders Rendering destruction, native join,
endpoint retirement and worker unload after that failure.

The current Host with rebuilt RenderingDisposal DLL passed `normal-tests-malformed`
at `build/lifecycle-runtime-8d92d791/development/logical-roots/test-logs/morphic_debug.lifecycle-8d92d791-normal-tests-malformed.p89136.log`.
`build/preamble-rendering-malformed-build.log` has zero warnings/errors. The first
run at `lifecycle-runtime-a4f6e634` had the correct runtime result but exposed the
checker's obsolete raw-native-failure expectation; the passing rerun uses the
current common-execution failure and retirement checks. This is production
Executive negative-protocol coverage, not final configuration qualification.

Coordinator review corrected an unsupported cross-thread ordering assertion:
the accepted-only marker must appear exactly once, but may log after the peer's
failure because submit already published/notified before returning. The checker
retains failure-to-destruction/join/retirement/unload ordering. Its corrected block
passed against the recorded p89136 log without repeating the process.

## Saves and final Rendering disposal ingress

`CSaveExecution` retains one pending owning request and a fixed 32-entry completion
set. Each callback publishes at most four requests; busy preserves the exact owner
and does not advertise runnable work. The module control request follows all saves
on the same owning lane. The explicit-disposal case waits for that admission ACK
before its two POD disposals, checks duplicate rejection, and tests refusal of a
new save after closure. All 32 correlated save results must arrive before successful
disposal or unload completion. No saved byte view is retained by Executive; an
unexpected stop cancels unsent requests and common quiescence cancels unread
accepted outcomes while Host retains the assets/worker obligations.

`CFinalDisposalExecution` reads two value identities before readiness, then retains
its two final POD disposal posts across busy output. It owns no byte view and
reports domain completion only after both posts are accepted. Host ingress/frontier
accounting and endpoint retirement settle the accepted disposal operations even
when their results are unread. An intermediate result may arrive while the second
post is blocked; it is validated without making completion depend on receiving
both replies. The old blocking save/disposal helpers and all remaining unreachable
Executive helper bodies are removed. Rendering's old entry now contains only the
unmigrated bundle reference path.

Focused supplied-facade coverage verifies four-publication turns, busy owner/POD
retention, accepted-wake-failed exactly once, control admission ordering, completion
correlations and duplicate rejection, both cleanup-result gates, and final disposal
with/without an intermediate acknowledgement. `MorphicTests.exe -t1
--lifecycle-executive --log-tag=preamble-final-disposal` passed 321 assertions,
zero failures (including prior Executive/Audio/replacement tests).
`build/preamble-final-disposal-tests-build.log` is clean.

The three actual current-Host/rebuilt-DLL cases `disposal-during-save`,
`render-drain`, and `render-exit-disposal` passed under
`build/lifecycle-runtime-828c1c2f/development/logical-roots/test-logs/`, process IDs
93436, 12916 and 13712. In the final-disposal case, Host admitted slots 600/601 at
ordinals 237/238, Rendering destroyed at 239 and joined at 240, both disposals
completed at 241/242, endpoint retired at 243 and worker unload followed at 247.
The 32 save results were consumed before the fixture completed its unload step;
the saved 1 MiB file retained its expected bytes. No deferral was observed in this
particular run, so it proves final ingress and retirement ordering, not an actually
held native save at the destruction boundary. Controlled pressure coverage remains
separate. Build logs `preamble-save-helper-removal-build.log` and
`preamble-final-disposal-rendering-build.log` under `build/` are clean. Bundle domains,
remaining consumers, legacy removal, and final current-source configurations remain
outstanding.

The final save/disposal removal build is
`build/preamble-disposal-removal-final-build.log` (zero warnings/errors). The unused
lifecycle `direct_service.hpp` adapter and project reference are removed. The
strengthened final-disposal ordinal checker passed against the recorded p13712
log; the declaration/brace/include cleanup did not justify repeating those passing
runtime cases.

## Bundle closure, replacement handoff and failure cases

The coordinator accepted the save/final-disposal checkpoint. `CBundleExecution`
now owns the facade, retained owning/POD publication, typed reply expectation and
bundle value/view state for late reference, replacement/resume, shutdown and failure
scenarios. Each advance publishes at most one request. Busy retains its exact
request without runnable spin; accepted wake failure is not resubmitted. A late
reference result is retained separately while the following closure/refusal travels
the same POD lane; the complete view is inspected only after that refusal proves
closure. Bundle disposal notifications are distinct from correlated request results
and may be retained before the explicit notification-wait step.

Replacement saves only value identities, closes the original root, relinquishes
local views and restores the module allocation baseline before requesting replacement.
The successor copies its handoff values before disposing the loaded handoff bytes,
proves the old root is closed, releases the retained sub-file, then reuses the root
without inheriting the outgoing endpoint's notification. Shutdown and unexpected
stop cease local view access and cancel unsent state; Host quiescence still owns
accepted unread outcomes and retained assets. The old blocking implementations of
these cases are removed; workflow/batch and Rendering ownership remain migration
reference and are not yet selected by this new class.

Current Host/rebuilt Executive cases `bundle-late-reference`, `bundle-replace`,
`bundle-shutdown`, and `bundle-failures` passed under
`build/lifecycle-runtime-5884ccd5/development/logical-roots/test-logs/`, process IDs
74032, 69348, 92624 and 93380. After moving late-view inspection explicitly behind
closure proof, that affected case passed again at
`build/lifecycle-runtime-b2122c6f/development/logical-roots/test-logs/morphic_debug.lifecycle-b2122c6f-bundle-late-reference.p12412.log`.
The unchanged other cases were not repeated. Build logs
`build/preamble-bundle-basic-executive-build.log` and
`build/preamble-bundle-basic-final-build.log` are clean.

Supplied-facade replacement-resume coverage forces busy at every owner/POD step,
returns accepted-wake-failed, checks exactly-once submission, overwrites the old
handoff bytes after disposal to verify copied identities, and withholds final
notification to gate completion. Separate cancellation cases stop before and after
first owner acceptance. With previous Executive tests,
`MorphicTests.exe -t1 --lifecycle-executive --log-tag=preamble-bundle-basic` passed
409 assertions, zero failures. `build/preamble-bundle-basic-tests-build.log` is
clean. These facade cases do not simulate Host retirement; actual replacement and
shutdown process coverage remains separately identified above. Remaining bundle
domains and final consumer/native/configuration qualification are still outstanding.

## Bundle persistence and held batch shutdown

The coordinator accepted the basic bundle checkpoint. The persistence workflow now
uses explicit operation states for document creation, bundle bake/save/load, root
refresh, cache identity, alignment/entry validation and closure. Two fixed JSON
load records correlate replies independently of completion order; their decoded
documents and the decoded image remain usable after the original bundle closes.
A module-owned snapshot verifies byte stability and identical persisted/rebuilt
bundles, then releases its allocation before completion.

The batch item and its borrowed view live in the module object. A queued callback
is deliberately held through root closure and rejection checks, then released and
its terminal response consumed before the sub-file is disposed. The inline path
uses the same byte checks without waiting. Unexpected stop releases a held callback,
keeps its item/views/snapshot alive until response consumption, and prevents domain
completion while that item remains active. This is an isolated one-item fixture;
the configured cases use one runner or no runners and do not claim queue-saturation
or mixed runner/inline scheduling coverage.

Current Host/rebuilt Executive `bundle` and `bundle-inline` passed under
`build/lifecycle-runtime-26c901c7/development/logical-roots/test-logs/`, process IDs
67052 and 92580. `build/preamble-bundle-workflow-executive-build.log` is clean.
The new maintained `bundle-batch-stop` case waits for release/acquire proof that
the native callback has started, requests ordinary Executive self-stop after
closure, then consumes the executed terminal response and checks the allocation
baseline before joining. It uses an existing 1 ms timed wake only while awaiting
that start publication; no runnable polling is advertised. The case passed at
`build/lifecycle-runtime-bbbc98a3/development/logical-roots/test-logs/morphic_debug.lifecycle-bbbc98a3-bundle-batch-stop.p90704.log`:
stop request 74, batch retirement 76, baseline 77, native join 80 and endpoint
retirement 81. `build/preamble-bundle-stop-executive-build.log` is clean.

A controlled post-acceptance boundary verifies the two parse replies in reverse
order and rejects duplicate/wrong correlations; worker/cache publication remains
qualified by the actual DLL cases. With existing focused cases,
`MorphicTests.exe -t1 --lifecycle-executive --log-tag=preamble-bundle-workflow-final`
passed 419 assertions, zero failures. `build/preamble-bundle-workflow-tests-build.log`
is clean. The old blocking workflow and obsolete Executive bundle entry/declaration
are removed. Rendering-owned bundle migration, remaining consumers/legacy removal,
native acceptance and final rebuilt configurations remain outstanding.

## Rendering-owned bundle and lifecycle legacy removal

The coordinator accepted the persistence/batch checkpoint. Rendering's bundle owner
now runs through the same module-owned operation class and bound facade. It becomes
ready before asynchronous bundle work, bakes/saves/closes its first bundle, loads
the saved bundle, obtains a sub-file and saves the value handoff. Executive observes
that test-only file once per timed invocation while it is unavailable, preserving
its existing operation deadline without a runnable poll. It verifies foreign-owner
disposal refusal, a valid complete reference and sub-file cache identity before
unloading Rendering. Rendering ceases local view access and retains its final root
closure across busy output; accepted closure is followed by domain completion,
while Host retains the sub-file/notification record through native retirement.
Executive then verifies closed-root refusal, busy rebake, saved-byte integrity,
sub-file disposal and successful root reuse after the old owner has unloaded.

The actual current Host/rebuilt Executive and RenderingDisposal case passed at
`build/lifecycle-runtime-b5320fca/development/logical-roots/test-logs/morphic_debug.lifecycle-b5320fca-bundle-rendering.p90164.log`.
Rendering ownership 67 precedes its restored allocation baseline 80, destruction
82, native join 83, endpoint retirement 84 and worker unload 88. Executive's
post-join sub-file release/root-reuse proof follows at 113. The 128 saved bytes
match their expected value. The strengthened ordinal checker passed against that
recorded log, without rerunning the process for checker-only changes.

Supplied-facade cases force busy at each Rendering operation and final closure,
verify accepted-wake-failed exactly once, and stop both after and before consuming
the accepted handoff's result. A deliberate final publication refusal reports
failure once and then supplies successful cleanup progress, relinquishing only
module-local state; this controlled case does not claim actual Host fatal cleanup.
Together with prior focused cases,
`MorphicTests.exe -t1 --lifecycle-executive --log-tag=preamble-bundle-rendering`
passed 521 assertions, zero failures. The clean build logs are
`build/preamble-bundle-render-client-build.log`,
`build/preamble-bundle-rendering-final-build.log`,
`build/preamble-bundle-rendering-tests-build.log` and
`build/preamble-lifecycle-legacy-removal-build.log` (final include/brace cleanup).

All lifecycle scenario implementations now use current execution contexts. The
old `bundle_cases.cpp/.hpp`, raw Rendering thread entry, old function-query branch,
pump export/Win32 export option and project references are removed. The deliberate
missing-capability fixture still omits the current execution export as its negative
test requires. This completes this consumer migration, not the overall task:
remaining pump/input consumers, framework/Host legacy removal, native acceptance,
final version-1 builds/artifact regeneration and older-Host evidence reconciliation
remain outstanding.

## Pump provider execution migration

The coordinator accepted the Rendering-owned bundle/lifecycle removal checkpoint.
The real PumpProvider fixture now exports `morphic_module_execution_v1` directly.
`tests/host_pump/provider_execution.hpp` owns its facade, bounded command records,
provider-operation client, borrows, notification publisher, credit assets and native
restriction state. The old provider `SRuntime`, image-global instance, raw inbox
pump, sealing and child-thread message machinery have been removed from
`tests/host_pump/fixture.cpp`. Thin byte-count/aligned allocation adapters construct
and destroy the class on the Host execution thread. The Executive fixture remains
legacy and is the next consumer migration; its old export is still explicitly
transitional in the project.

Named phases retain accepted provider operations through their acknowledgement.
Busy publication advertises no runnable retry; capacity/ingress notifications
resume it. Flood/disposal/release passes publish at most 64 messages. The two mixed
children use private create/stop intents and require creation, ended and retired
identity/order checks, exactly one destruction on their creation thread, and a
thread distinct from the Host callback. Notification retirement retains borrowed
storage until consumer quiescence, drains the actual publisher's final signal,
joins it, and settles retirement/completion acknowledgements before deallocation.
An accepted transfer or acquire remains pending during stop; unsent subsequent
creation/acquisition is cancelled, and acquired/owned storage still requires its
release/disposal outcomes.

`MorphicTests.exe -t1 --provider-execution --log-tag=preamble-provider-affinity`
passed 23,900 assertions, zero failures (process 16892). This binds the rebuilt
`build/bin/x64/Debug/MorphicPumpProvider.dll` into the current-source test Host and
uses its actual common pumped driver/static facade and private lifecycle. It
checks echo and contract routing, 1,024 ordinary output messages plus a held
completion with no full-pipe spin, ordered recovery, actual serviced children,
borrow cancellation, actual native notification publication/join, 512 controlled
acquisitions/disposals, and stop while transfer/acquire results are withheld.
Domain service/asset replies and consumer quiescence are deliberately controlled;
these are not end-to-end Host service/asset or native modal acceptance claims.
Module attribution is empty at destruction; owning transfers use the same
allocator with separate Host/module attribution, as production requires.

The first credit run exposed a fixture allocator mismatch, corrected before the
passing result. An initial stop assertion also checked for quiet before the
legitimate private admission-closed notification was delivered; the final test
allows those bounded control turns before checking the held operation is quiet.
Clean build logs are `build/preamble-provider-current-build.log`,
`build/preamble-provider-tests-build.log`, `build/preamble-provider-stop-build.log`
and `build/preamble-provider-affinity-build.log`. `git diff --check
--ignore-submodules=all` and `tools/check_line_endings.ps1` passed.

The old `invalid-seal` scenario is renamed `invalid-submission`: private sealing
is no longer a public message, so the migrated case checks invalid untyped POD
refusal and then deliberate callback failure. This case and remaining modal,
sequence/test-chain, failure/deadline and process-level pump scenarios still await
qualification with the migrated Executive. The flood marker now describes bounded
inbox/output traffic. Historical validation documents retain their old case names
as historical evidence, pending final current-source reconciliation. No production
runtime change or new execution API is introduced by this checkpoint. Remaining
input consumers, framework/Host legacy removal, native acceptance and final
version-1 configurations/artifacts remain outstanding.

The coordinator's provider review identified consumed transfer/acquire refusals
that left a reply wait live, and accepted child creation later refused without a
child lifetime to retire. Both are corrected. Credit and test-chain transfer
refusal ends its wait without an asset; acquire refusal ends its wait while
retaining the original asset for disposal, with no invented acquisition/release.
Contradictory identity/custody and unproven release/disposal success enter explicit
quiet fatal retention. Child creation outcomes are tracked separately: refused
outcomes settle the accepted create intent, while successful children still need
actual ended/retired notices and their affine destruction proof.

`MorphicTests.exe -t1 --provider-refusals
--log-tag=preamble-provider-refusals-final` passed 165 assertions, zero failures
(process 6396). The five rebuilt-DLL cases cover credit/test-chain transfer and
acquire refusal, plus actual Host child-creation attempts with native creation
refused at the existing test hook. The log has exactly five domain-failure
reports, five completed cleanup markers and five affine destruction markers.
Disposal acknowledgement gates completion where an original asset remains; no
extra acquire/release/transfer or impossible child-retirement wait remains.
Malformed/unproven-custody retention is source policy, not a newly executed unsafe
teardown case. This is controlled terminal-result/native-create-fault evidence;
full service routing and modal/process qualification remain outstanding.

The initial negative run also exposed a fixture failure-latch assignment that
could overwrite the failure set inside `receive`/`child_notice`/command progress.
Those callers now set failure only when their returned status requires it, leaving
an internally latched failure intact. The final negative log proves one report per
case and later accepted cleanup progress. Clean build logs are
`build/preamble-provider-refusal-dll-build.log`,
`build/preamble-provider-refusal-tests-build.log` and
`build/preamble-provider-refusal-final-build.log`. Passing positive cases were not
repeated for these focused refusal corrections. The draft
`tests/host_pump/executive_execution.hpp` is preparation only, not selected or
compiled yet, and carries no validation claim.

## Pump Executive basic migration and destroy-refusal retention

The preceding draft status is historical. `CExecutiveExecution` is now selected
by thin affine allocation adapters and the Host standard serviced coordinator in
the Executive and Replacement fixture DLLs. The coordinator accepted the Provider
positive/refusal checkpoint for its controlled scope; the complete Executive
package is still under review. Its named owner/POD operation retains busy posts,
accepts `published_wake_failed` once, validates acknowledgement and completion
separately, and keeps original per-operation deadlines. Unsupported remaining
borrow/notification/modal/schema/report/native cases explicitly fail rather than
claiming the basic route test as their pass. Old unreachable Executive helpers
remain migration references and must be removed before completion.

`MorphicTests.exe -t1 --pump-executive --log-tag=preamble-pump-executive`
passed 64 assertions, zero failures (90392): retained owning load/self-stop,
busy and accepted-wake-failed publication, acknowledgement/completion and private
admission-closed cleanup. That result predates the later sequence/delayed-domain
extensions. Clean incremental build logs are
`build/preamble-pump-executive-{basic,self,admission,tests,failure}-build.log`,
`build/preamble-pump-sequence-build.log` and
`build/preamble-pump-replacement-build.log`.

The temporary PowerShell process harness `build/run-current-pump.ps1 -Cases ...`
ran the following selected current-ABI cases. Each directory below is prefixed
`build/pump-current-` and contains its log and prelaunch Engine/Executive/Provider
path/size/time/SHA256 manifest. These runs used the corrected Host built on
23 November at 02:04 UTC, not the older Host covered by the earlier provenance
caveat. Replacement was rebuilt at 10:17:48 UTC, separately verified; its path was
not included in that earlier three-image prelaunch manifest. New manifests now
include all four images. Python is unavailable here; the maintained Python runner
was updated but was not executed.

| Scope | Case and runtime directory suffix |
| --- | --- |
| Service/pressure/credits | normal `d012dcb8`; mixed `4961a4de`; inbox-full `d71be0a2`; cleanup-credits `5cb2899d`; empty-provider `3e7b6bfe` |
| Execution contract rejection | bad-callback `34adef56`; bad-size `85ca07d1`; bad-version `cce09cec`; bad-flags `8c03289e` |
| Provider rejection | bad-provider-size `0049fff4`; bad-provider-version `8959b8dd`; bad-provider-flags `31822a3b`; bad-provider-count `e2fa7b22`; bad-provider-null `22f2c28f`; bad-provider-contract `27e85c82`; bad-provider-duplicate `24b4762c`; bad-provider-query `4675ce03` |
| Fatal/startup/deadline | activation-failed `133b761f`; activation-null `720ee56b`; activation-timeout `08a98614`; unauthorised-native-stop `4bebf758`; unauthorised-native-events `26137a96`; invalid-submission `814eab5e`; pump-failed `16bd7136`; stop-timeout `f9c714e9` |
| Sequence and lifetime | sequence `5db913fa`; shutdown-pending `8e033fb4`; replacement `ff84e60d`; emergency `fe9ad3e2` |

Successful cases exit zero; expected fatal/grace-expiry cases exit one. Logs show
Executive join/retirement/worker unload. Shutdown-pending retains its delayed
service result after stop (completion 51, receipt 53, Provider destruction 54,
Executive pass 55, join 56, retirement 57). These are scoped process results, not
final configuration qualification. An initial normal attempt exposed a missing
private admission-closed notification case in the Executive fixture; the selected
handler now accepts that notification and the focused test covers it. Earlier
failed attempts are not passing evidence.

The initial `deactivate-failed` process (`14a6d323`, 74616) exposed a preservation
regression: returned destroy refusal was treated like unsafe retained execution
and eventually fail-fasted. The coordinator approved the narrow pumped-only
correction. `CModulePreamble` records destroy refusal only after the Host grant
and callback return; `CPumpedClient` separately records successful outer-frame
unwind. `SClientRecord::process_retention_eligible` requires that exact state,
drained/accounted seal ingress, no active invocation/retained head/deferred record
and settled batch obligations. `execution_quiescent` is unchanged and remains
false. Failed create with state and other unsafe retention remain ineligible.

Host handles this proof before ordinary stopped/retirement processing, settles
the pending unload exactly once as `unload_failed`, and preserves the object,
subscription, endpoint, dependencies, binding and complete graph. Shutdown
enumeration can finish without success-retiring that endpoint. Workers and batch
runners still stop/join before process retention and exit failure. The public
callback contract documents no retry or resumption after destroy refusal.
Serviced refusal retains its existing conservative fatal deadline unless actual
native join and parent lifecycle proofs settle; no child-retired notice is
invented by this change.

`MorphicTests.exe -t1 --destroy-refusal --log-tag=preamble-destroy-refusal`
passed 19 assertions, zero failures (69112). The same-image actual Host-turn
fixture checks one affine destroy refusal, one correlated retained unload result,
no further callbacks, retained endpoint/subscription, unrelated live execution
blocking completion, a controlled outer invocation hold/unwind flag, unaccounted
ingress, and a distinct failed-create-with-state retention reason. Its fake binding
and test-owned callback state are dismantled explicitly; this is not native DLL
retention evidence.

The actual rebuilt process proof is
`build/pump-current-eaeee3a7/development/logical-roots/test-logs/morphic_debug.pump-eaeee3a7-deactivate-failed.p11660.log`.
It exits one with one refusal at ordinal 52, expected common fatal escalation 53,
one validated `unload_failed` receipt 54, fixture pass 55, Executive join/retirement
56/57 and worker unload 61, both Host workers exiting 66/68, and successful
worker/batch shutdown with whole-graph retention 69. There is no Provider
destruction, endpoint-retirement, DLL-unload or fatal-deadline marker. The
PowerShell checker enforces those positive/negative counts and rejects unexpected
diagnostics; the equivalent maintained Python checks were updated but not run.
The manifest records the exact loaded Engine (10:28:07 UTC), Executive (10:29:01)
and Provider (10:29:29) paths and hashes; Replacement is recorded but unused here.

Clean builds: `build/preamble-destroy-refusal-host-build.log`,
`build/preamble-destroy-refusal-unwound-build.log`,
`build/preamble-destroy-refusal-tests-build.log`,
`build/preamble-destroy-refusal-executive-build.log` and
`build/preamble-destroy-refusal-provider-build.log`, all zero warnings/errors.
The focused test preceded only the Executive refusal-result transition adjustment;
the runtime retention code was unchanged for the process proof. Diff whitespace
and tracked line-ending checks passed. The coordinator accepted this narrow
pumped retention correction after source, test, build and process-log review;
remaining consumer migration, legacy removal, native acceptance, final version-1
builds/artifacts and current-source coverage reconciliation remain outstanding.

## Pump serviced credit custody increment

`tests/host_pump/credit_execution.hpp` replaces the old blocking
`threaded_credits` helper. The module-owned Executive class owns this fixed
600-item operation state and its bound facade. Transfer/acquire waits, disposal
admission, the same-POD-lane marker and release/terminal accounting are explicit.
Each progress call makes at most four publication attempts; busy retains the
exact owner/POD operation without runnable spin, and accepted-wake-failed never
republishes. Original operation deadlines persist across busy retries. No new
Host transport, input gate or scheduling interface is introduced.

The marker verifies that all 600 disposals are admitted before any acquisition
release. Unique per-item correlations and identities validate release/disposal
results in either order. After all releases are posted, Executive publishes its
self-stop but cannot complete or log success until all 1,200 terminal outcomes
are consumed. Stop cancels an unsent transfer/acquire; accepted outcomes remain
awaited. Transfer refusal leaves no custody; acquire refusal preserves the
original asset for disposal without an invented acquisition. Malformed custody or
unproven release/disposal success stays in quiet fatal retention. The old helper
and its unreachable legacy call are removed; other old Executive helper bodies
still await their own migrations/removal.

`MorphicTests.exe -t1 --pump-credits --log-tag=preamble-pump-credits` passed
9,347 assertions, zero failures (66692). This supplied-facade component fixture
forces repeated busy and accepted-wake-failed, checks exactly one publication per
operation, withholds all cleanup results until stop and delivers them in reverse
order. Six small custody cases cover unsent transfer, accepted transfer,
accepted acquisition, transfer/acquire refusal and unproven disposal outcome.
This validates custody state separately from actual native scheduling. The first
compile caught an acquisition-token API mismatch; the corrected build is
`build/preamble-pump-credit-current-build.log`, zero warnings/errors. The later
pending-result observation getters/logs were exercised by the actual DLL run,
without repeating this passing component matrix.

The rebuilt Executive process case passed with exit zero:
`build/pump-current-22acd445/development/logical-roots/test-logs/morphic_debug.pump-22acd445-threaded-credits.p91944.log`.
Self-stop publication at ordinal 1249 has 1,200 results pending; stop delivery
1316 still has 1,136. Provider stops/deactivates 1381/1446; all mandatory credit
results settle before the domain pass at 1855, Executive pass 1856, actual native
join 1857, endpoint retirement 1858 and DLL unload 1862. Provider unload 1869 and
both Host worker exits follow. No error diagnostics occurred. The prelaunch
manifest records current Host 10:28:07 UTC, Executive 10:38:11, Provider 10:29:29;
Replacement was recorded but unused. Clean DLL logs are
`build/preamble-pump-credit-executive-build.log` and
`build/preamble-pump-credit-observation-build.log`.

Both checker sources now require pending results at stop and exactly one credit
completion/join/retirement/unload marker; those refinements were checked against
the recorded log without rerunning the process. The destroy-refusal check was
also corrected from the module spelling `rendering` to the actual mount spelling
`render` and passed against its recorded log. Python remains unavailable and its
maintained runner remains unexecuted. Diff whitespace/line endings passed.
The coordinator accepted this credit increment after source, focused-test and
real-process review; remaining Pump Executive
borrow/notification/modal/schema/report/native consumers, Input Host migration,
all in-scope legacy removal and final version-1/current-source qualification are
still outstanding.

## Pump borrowed-resource consumer increment

`CBorrowExecution` now holds the existing `CServiceBorrowClient` protocol state
inside the module-owned Executive context. It retains named request, cancel,
release and quiescence publications across busy, validates route/request/borrow
identity, and requires accepted quiescence plus actual retired notice before
normal domain completion. Late prepared views after closing never reopen byte
access. Shutdown preserves accepted obligations; an unsubmitted request is
explicitly abandoned. The deliberate abandoned-borrow case has no dependent
objects or asynchronous readers: local descriptor access ceases before its
self-stop/domain completion, while Host still needs native quiescence to retire
the consumer and provider storage. This local completion is not Host endpoint
quiescence. The old blocking `borrowed_resource` helper/call is removed.

`MorphicTests.exe -t1 --pump-borrow --log-tag=preamble-pump-borrow` passed
107 assertions, zero failures (75304). Supplied-facade cases exercise all five
borrow modes, retained busy/accepted-wake-failed cleanup, late prepared after
closing, and stop before request acceptance. The actual initial `borrow-normal`
process passed at `build/pump-current-8c2f9ebb` (24936). Its binary manifest scopes
that result to the pre-correction fixture binaries below; no unrelated positive
matrix was repeated.

The first `borrow-cancel` process (`bc801f4d`, 91832) exposed two asynchronous
fixture gaps. A cancelled service can report consumer borrow retirement while
its accepted provider operation still needs a late completion, so route close can
legitimately return a terminal `busy`. The Executive now consumes that outcome
and retries the same named close at a fixed one-millisecond due time, preserving
its original overall operation deadline. Unrelated callbacks neither publish
early nor move that due time; transport busy retains the unsent close, while an
accepted close is never retried until its terminal result. Exact route/request
checks and other expected statuses are unchanged.

Provider retirement must also remain in its finish phase if stop arrives after
finish publication or its acknowledgement but before the final completion ACK.
After consumer quiescence and publisher drainage, entering finish now records
the existing retiring flag. This prevents a later stop from starting retirement
again on an already-retired ID, including while the finish ACK itself is pending.
No pending begin exchange can exist at this phase: the existing stop gate settles
it before entering finish. Provider bytes remain owned until the late completion
exchange is acknowledged. These are fixture corrections; Host routing/retirement
policy and the public API are unchanged. The coordinator approved their scope.

Focused correction evidence:

- `--provider-retirement --log-tag=preamble-provider-retirement`: 107/0 (32608).
  Two actual Provider DLL/common-driver cases stop while finish ACK or late
  completion ACK is withheld. They prove no second begin/finish/complete post,
  no early destroy/free, valid retained bytes, and affine destruction/empty
  attribution only after the required acknowledgement. Host domain outcomes are
  controlled in these cases.
- `--pump-close --log-tag=preamble-pump-close`: 26/0 (27064). The controlled
  post-borrow close boundary consumes real terminal-busy parsing, checks the
  one-millisecond retry construction, then pins/expires only that due time for
  deterministic unrelated-callback interleavings. A separate five-second overall
  anchor does not restart. Transport busy and accepted-wake-failed retry preserve
  exact route/request/correlation and produce no duplicate accepted publication.

Rebuilt Executive/Provider actual Host runs all passed, with normal cases exiting
zero and provider failure exiting one with only its expected common fatal report:

| Case | Runtime directory suffix / process |
| --- | --- |
| borrow-cancel | `35c96809` / 81084 |
| borrow-abandoned | `3289cccd` / 72720 |
| borrow-shutdown | `9ef3f104` / 3600 |
| borrow-provider-failed | `a567a865` / 83116 |

Directories are `build/pump-current-<suffix>`; each has the complete logs and
prelaunch image hashes/paths. Host is the current 10:28:07 UTC build, Executive
10:57:03 and Provider 10:53:15; Replacement is recorded but unused. Cancellation
records consumer retirement 38, busy route-close wait 39, provider storage release
40, then successful unload and Executive join/retirement. Abandonment records
local departure 37, Executive pass 40, actual native join 41, provider quiescence
42/storage destruction 43, Executive endpoint retirement 46/unload 50. Shutdown
and provider-failure cases settle explicit quiescence/storage before Executive
join/retirement/unload. The maintained checker now asserts the abandonment native
retirement order; it remains unexecuted as Python is unavailable. Its equivalent
recorded-log check is used here.

Clean build logs: `build/preamble-pump-borrow-tests-build.log`,
`build/preamble-pump-borrow-executive-build.log`,
`build/preamble-pump-borrow-provider-build.log`,
`build/preamble-pump-borrow-retirement-build.log`,
`build/preamble-pump-borrow-close-build.log` and
`build/preamble-pump-borrow-retry-build.log`. Coordinator review accepted this
increment for the stated scope, including both held-acknowledgement boundaries,
fixed close retry deadline and local-access versus native retirement distinction.
Notification/modal/schema/report/native consumers, Input Host migration,
remaining legacy removal and final version-1/configuration/native qualification
remain open; no broader Executive acceptance or task completion is claimed.

### Pump notification consumer and terminal binding cleanup (23 Nov)

`tests/host_pump/notification_execution.hpp` now owns the notification domain
inside the common Executive context. It tracks the original borrow request,
registration and publisher command separately, retains one unsent publication,
and makes at most two publication attempts per invocation. Busy never recreates
registration state; accepted/wake-failed publication is not retried. An accepted
registration or publisher command must settle even after stop/borrow retirement.
Unsent registration is explicitly abandoned; unsent publisher work is cancelled.
Late registration after closure is copied/settled without dereferencing its
possibly expired reader. The 10-second operation deadline stays anchored at start.

Reader clear/durable-data recheck runs on the execution owner; common preamble
continues to own epoch/wait ordering. Cancel deliberately leaves the observed
notification pending to exercise coalescing of the final publisher signal. Other
modes clear it. Local reader/data access ceases before quiescence or deliberate
departure. Abandonment/replacement completes only that local domain obligation;
Host consumer native join and provider publisher join remain required before
storage/notification pin retirement. The old blocking `notified_resource` helper
and its unreachable routing branch are removed. The Replacement DLL is rebuilt
against this class, rather than using its earlier sequence-only binary.

Provider notification binding now advances past a consumed terminal refusal,
retains cancelled command/original completions and waits for their acknowledgements.
It does not bind again or create a publisher. Unexpected refusal reports failure
once; stop-time closing remains cleanup progress. A malformed binding result or
unexpected existing native thread is conservatively retained without runnable
spin. An invalid `CThread` whose `create` returns false has started no thread on
the implemented Windows/pthread backends; that proved failure uses the same
cancelled-completion path. Native create failure itself was source-audited, not
fault-injected in this increment.

Focused evidence (x64 Debug):

- `MorphicTests.exe --pump-notification --log-tag=preamble-pump-notification`:
  **94/0**, PID 46424. Supplied-facade tests cover repeated busy and accepted
  wake-failure at request/registration/publisher/release/quiescence boundaries,
  durable observation, all four modes, stop at five distinct acceptance boundaries,
  late registration after local closure, and registration/publisher refusal.
- `--provider-notification --log-tag=preamble-provider-notification-pressure`:
  **4,258/0**, PID 8188. Actual Provider DLL/common driver, controlled Host results:
  unexpected binding refusal and stop during an accepted binding exchange. Full
  1024-slot output (controlled same-owner padding, removed without accounting as
  ingress) prevents cancellation publication without increasing the publication
  ordinal or spinning. Recovery publishes exactly one correlated cancelled
  command; its held acknowledgement retains state and prohibits re-bind/cleanup.
  The original completion and actual affine destruction then settle through the
  normal borrow proof. No publisher/data signal is created on refusal. The earlier
  142/0 PID 85956 result preceded this pressure/held-completion refinement.

Actual current-Host runs all returned zero and contained no error diagnostics:

| Case | Runtime suffix / PID |
| --- | --- |
| notify-normal | `2578d58b` / 92348 |
| notify-cancel | `ab469a9d` / 13432 |
| notify-abandoned | `ee1dc076` / 22624 |
| notify-shutdown | `bbf8d1de` / 56928 |
| notify-replacement | `482e42a8` / 24764 |

Logs reside under `build/pump-current-<suffix>/development/logical-roots/test-logs/`
as `morphic_debug.pump-<suffix>-<case>.p<PID>.log`; each runtime includes
`binary-provenance.json`. Host remains the accepted 10:28:07.933 UTC binary
(`A85CA586...E334`). Executive is 11:14:25.910 UTC (`CC351CF9...587A`),
Provider 11:13:43.448 UTC (`6A34B8A3...414F`), and the used Replacement is
11:15:37.191 UTC (`846278BB...E325`), all from `build/bin/x64/Debug`.

Abandonment ordinals are local-access relinquishment 37, consumer native join 41,
provider quiescence 42, final publisher signal/join 43, storage destruction 44,
Executive endpoint retirement 47 and worker DLL unload 51. Replacement repeats
the native retirement chain for the outgoing consumer, then observes a second
fresh registered publisher and explicit retirement in the successor. The
maintained Python checker now requires those counts and retirement ordering;
Python remains unavailable, so an equivalent recorded-log PowerShell check is
used without rerunning the processes for checker-only edits.

Build logs `build/preamble-pump-notification-{tests,provider,executive,pressure,replacement}-build.log`
all end with zero warnings/errors. `git diff --check --ignore-submodules=all` and
the tracked line-ending checker pass. Coordinator review accepted this notification
checkpoint for the stated scope, including the source-audited (not injected)
native-create failure qualification. Modal/schema/report/native consumers, Input Host migration,
legacy removal and final version-1/configuration/current-source reconciliation
remain open; no broader Executive or final task acceptance is claimed.

### Pump modal consumer and common capture-pressure reporting (23 Nov)

`tests/host_pump/modal_execution.hpp` replaces the old blocking modal helper with
a module-owned incremental consumer. It retains the borrow acquisition, native
command and cleanup publications, plus at most one unsent owning refresh. Each
invocation attempts at most four publications. Shared ready/entry/pressure/progress
atomics are checked with a 1ms timed wake, including output-busy returns; no native
atomic wait becomes a runnable spin. Restriction snapshot retries are bounded
and retried through the same wake while the original 10-second operation deadline
remains anchored. The 50ms eligible budget must retain its full value during the
simulated nested native restriction held for at least 100ms and eight maintenance
passes. The full case publishes 512 POD releases and up to 64 owning refreshes,
retaining an owning post under busy until either accepted or the fixture has
proved at least 32 accepted owners plus native capture pressure. Only then may
that unaccepted owner be cancelled to leave the simulated native invocation.

No ordinary completion may cross the held native prefix. Accepted flood replies
are matched exactly once by correlation/type/status, with filesystem queue refusal
an explicit expected terminal outcome. Stop stores the native leave flag before
ceasing borrowed-data access, cancels only unsent flood/command work, and retains
the accepted native-command result, borrow retirement and every flood reply until
settled. Domain completion cannot hide those accepted obligations. Modal shutdown
uses the existing authenticated Host stop endpoint to unwind native maintenance.
`modal_resource` and its obsolete routing branch are removed from `fixture.cpp`.

The first normal run passed (`8e6eed4a`/53032) with the then-current Host 10:28:07
and new Executive. The first full run (`4e788e57`/86240) failed and is not passing
evidence. It exposed the missing timed wake on an owning-busy return and a Host
capture-pressure reporting omission: common lanes retain one ingress head rather
than occupying the old deferred array, but their capture branches did not report
queued work blocked behind that head. Coordinator review authorised the narrow
preservation correction. `host/runtime/host_maintenance.cpp` now ORs per-pass
pressure when a common retained head has readable transport data behind it and
returns without capturing another message. A lone head is not pressure. Existing
bounded source rotation and per-pass reset remain; this is a sampled signal, not
an exhaustive instantaneous occupancy guarantee. No dispatch/admission, accounting,
reservation, storage or wake protocol was changed.

Focused evidence (x64 Debug):

- `MorphicTests.exe --pump-modal --log-tag=preamble-pump-modal`: **778/0**,
  PID 76604. Supplied-facade normal/full/shutdown, unsent stop and stop after
  accepted native command; busy/wake-failed publication, timed wake while owning
  output is full, fixed counts, reverse-order flood settlement after stop, and
  command/retirement completion gates. A test-only friend expires the 100ms hold
  after the real budget has started; it does not claim native elapsed-time proof.
- `--maintenance-pressure --log-tag=preamble-maintenance-pressure`: **84/0**,
  PID 73848. Actual bounded maintenance passes over controlled common POD/owner
  lanes: retained and queued identity/order, no dispatch/accounting/reservations
  in the active invocation, positive pressure with a queued successor and cleared
  pressure once only the head remains. Test-owned capture of the successor is
  explicitly cancelled together with the head only after native unwind.

After both corrections, Host and dependencies plus the Executive fixture were
rebuilt. Actual processes all returned zero without error diagnostics:

| Case | Runtime suffix / PID |
| --- | --- |
| modal-full | `254045b0` / 37844 |
| modal-shutdown | `c5cf2d48` / 89584 |
| modal-activation | `e284757f` / 64680 |

The full run reaches the nested maintenance, deferred-reply and borrow-retirement
markers; shutdown also joins/retires Executive and unloads both DLLs after cleanup.
Activation retains its existing simulated restriction for more than five seconds.
These are simulated native restrictions with real Host/serviced Executive threads,
not OS modal-window acceptance. The recorded-log checker validates all required
markers once. The maintained Python runner is updated but remains unexecuted.

Each runtime has `binary-provenance.json`: Host is now 11:37:55.990 UTC,
SHA256 `8101F2B56472ABC82966081A2BE584CB8171DB5FE575F46D8C24AA2388E8B677`;
Executive is 11:37:29.947 UTC (`060D9698...740D8`). The Provider remains the
11:13:43 notification-qualified binary (unchanged modal provider behavior);
Replacement is unused. Normal's earlier passing evidence is not relabelled as
the later Host build; final current-source reconciliation remains required.
Logs use the existing `build/pump-current-<suffix>/development/logical-roots/test-logs/`
pattern. Clean logs are `preamble-pump-modal-{current,tests,wake}-build.log`,
`preamble-maintenance-pressure-build.log` and `preamble-modal-pressure-host-build.log`
under `build`, all zero warnings/errors. The initial modal Executive compile
reported a misspelled asset status enum, corrected before any modal process run.

Coordinator review accepted this modal checkpoint for the stated simulated-native
and controlled-fixture scope. Schema/report/native consumers, Input
Host migration, complete legacy removal and final version-1/all-configuration
rebuilds/artifacts/current-source qualification remain open. No task completion
or broader Executive acceptance is claimed.

### Pump schema replacement consumer (23 Nov)

`tests/host_pump/schema_execution.hpp` now owns the schema operation inside the
common Executive context. The old blocking `schema_process` implementation and
its obsolete routing branch are removed. Each invocation attempts at most one
named publication. Busy retains the parsed owning request or cleanup POD; an
accepted/wake-failed post awaits its exact correlated outcome. Acceptance and
terminal result remain distinct. A well-formed admission refusal settles without
requiring an impossible second result; malformed custody and failed release or
disposal remain conservative fatal retention, without runnable retry spin.

The successful result descriptor and every baked-data view are read only during
the receiving invocation. Subsequent callbacks hold value asset/acquisition IDs
while release waits for publication and acknowledgement. The first Executive
releases its acquisition, writes a copied declaration asset ID to the test-runtime
handoff file, and preserves that asset only after its self-replacement request
enters Host transport. Stop before that acceptance disposes it. The successor
copies the handoff ID into its own state, composes retained declarations with
authored instance/data roles, validates the remapped span/text, releases its
acquisition, then disposes both assets and waits for both outcomes before self-stop.
An accepted schema operation during stop retains acceptance/result custody;
cancellation is published only after its acknowledgement, and late successful
output is released/disposed before domain completion. The anchored operation
deadline is not restarted on busy or phase transitions.

Focused `MorphicTests.exe --pump-schema --log-tag=preamble-pump-schema` passed
**158/0**, PID 74012. Supplied-facade cases cover repeated busy, wake-failed
acceptance without duplication, stop before submission, stop before acceptance
and retained cancellation, late success, stop before committed replacement,
admission refusal, deliberately unsafe release refusal, and successor cleanup of
both input/output asset IDs. Overwriting the synthetic descriptor after receive
checks that later retry/cleanup uses no descriptor pointer. The isolated handoff
file uses the test output path; actual process handoff remains in its fresh runtime.
Synthetic IDs/bytes are explicitly controlled evidence; baked schema semantics
are qualified by the actual Host conditioning run below.

Actual `schema-process` run `build/pump-current-83d6a174`, PID **13064**, returned
zero with no error diagnostics. Its main log records first conditioning at 27,
outgoing pass/join/retirement/unload at 32/33/34/38, successor conditioning at 48,
remapped text/span validation at 52, disposal completions at 54/62, native schema
backing reclamation at 57/65, and successor pass/join/retirement/unload at
70/71/72/76. Both outputs are current code-generated data, within the isolated
runtime. `binary-provenance.json` records Host 11:37:55.990 UTC (`8101F2B5...B677`),
Executive 11:58:21.838 (`1EB58158...825C`) and Replacement 11:58:47.557
(`9911CBC2...D877`), all `build/bin/x64/Debug`. Provider was not used.

Clean build logs: `build/preamble-pump-schema-{executive,tests,current,replacement}-build.log`,
all zero warnings/errors. The maintained Python checker requires both native
retirements/unloads and both backing reclamations plus the exact disposal markers;
Python remains unexecuted, with an equivalent recorded-log check used instead.
Diff and tracked line endings passed for that increment. Coordinator review found
an Executive integration gap between `next(self_stop)` and replacement request
construction: stop could start disposal and then publish replacement from the
empty operation stage. The corrected whole-stage guard covers both that gap and
a prepared, transport-busy request. It cancels only the unaccepted replacement,
retains the output asset and waits for disposal acknowledgement. The handoff
commit also rejects stopped, failed or already-disposing state without erasing
its live asset obligation; the successful accepted handoff remains unchanged.

Focused `--pump-schema --log-tag=preamble-pump-schema-stop` now passes **240/0**,
PID 20408, with clean `build/preamble-pump-schema-stop-build.log` (zero warnings
and errors). The two new cases drive the actual Executive through schema results
and release acknowledgement to each stop boundary. Replacement transport is made
writable after stop, yet neither case attempts another replacement; busy disposal
stays retained, wake-failed acceptance occurs once, and repeated invocations remain
incomplete until its exact acknowledgement. They use the isolated handoff file.
This is supplied-facade integration evidence; the earlier positive process still
applies to its recorded binaries, before this correction. No unrelated process
matrix was repeated. Coordinator review accepted the correction and the schema
consumer for this split scope; final rebuilt-consumer reconciliation remains open. Report/native
consumers, Input Host migration, complete legacy removal and final version-1/
configuration/current-source reconciliation remain open.

### Pump Platform shell consumer (23 Nov)

The common `CExecutiveExecution` now loads the production Platform module for
`platform-shell`, repeats the two already-loaded requests, opens its window service
route and sends three deliberately unsupported commands. Each command separately
awaits its successful admission acknowledgement and `unsupported` completion.
The same route remains usable after the first two commands and after an unload
request correctly returns busy; closing it permits successful unload. The old
blocking `platform_shell` helper and routing branch are removed. No native window
is created by this case.

Focused `--pump-platform-shell --log-tag=preamble-pump-shell` passed **300/0**,
PID 89268. The actual Executive object is driven through all eleven named
operations with repeated transport busy, wake-failed acceptance, withheld replies,
and exact module/route/request/opcode checks. Admission alone never advances a
service operation; no accepted request is resubmitted. Final completion waits for
Executive stop after Platform unload.

The actual Host run `build/pump-current-3ae6976f`, PID **10460**, exited zero
without error diagnostics. The main log records Platform activation at 33,
unsupported-command route reuse at 36, Platform worker unload at 45, domain pass
at 51, Executive join/retirement at 52/53 and worker unload at 57. There is no
Platform service-thread start. Its in-flight initial report receives interruption
and its receipt before unload; this case does not qualify successful report bytes
or native window/input behaviour.

`binary-provenance.json` records the current Host (`8101F2B5...B677`), rebuilt
Executive 12:12:50.335 UTC (`538028C6...8930`), and production Platform
10:25:25.464 (`3A4B2E63...65A7`). The Platform build confirmed it was current and
did not relink unchanged output. Provider and Replacement were unused. Clean
`build/preamble-pump-shell-{tests,executive,platform}-build.log` reports all have
zero warnings/errors. The maintained Python checker adds the retirement and
no-service-thread checks; it remains unexecuted, with the recorded log inspected
directly. Diff and tracked line endings pass. Coordinator review accepted this
scoped increment; report/native consumers and final removal/configuration work
remain outstanding.

### Pump report snapshots, cancellation and requester departure (23 Nov)

`tests/host_pump/report_execution.hpp` supplies a fixed, named report operation to
the module-owned Executive object. It handles production snapshot acquisition and
historical reload, controlled pending-capture cancellation/shutdown, and replacement
with an accepted update still pending. The corresponding old blocking
`platform_report`, `cancel_report` and `report_departure` helpers/branches are gone.
Injected failure/overlap and visible native report/window consumers remain separate
unmigrated work. No Host/public contract changed in this increment.

Each invocation submits at most one owner/POD. Busy retains it, accepted-wake-failed
publication is settled once, and known acquisitions retain their exact release
until acknowledgement. Byte views are cleared before release publication or stop.
Current/cold/refresh/reload/late-departure records are fixed local custody slots,
not a message queue. Initial-report polling retains its eight-second deadline,
releases any interim acquisition before polling again, and uses a fixed 1ms wake.
Stop before replacement acceptance cancels the unaccepted owner, including the
empty stage before its construction. No replacement is published after stop.

Unread report results have no module-local reader. Stop deliberately leaves their
Host-owned acquisition/reply cancellation to existing final-ingress, quiescence
and endpoint-reclamation gates. A late result after stop never installs a local
view or reopens cleanup after `work_complete`; acquisitions already known locally
still require release acknowledgement. This preserves the explicit requester
departure test: accepted update is not a promise to deliver its result after
requester cancellation. Unproven release outcome retains unsafe state and reports
failure once, with subsequent callbacks quiet rather than spinning.

Focused `--pump-report --log-tag=preamble-pump-report-custody` passed **489/0**,
PID 92976. Actual Executive supplied-facade cases cover unsent stop, accepted
unread/late result cancellation, shutdown after the pending-capture query, interim
acquisition release before repoll, busy and accepted-wake-failed publication,
stop while release acknowledgement is withheld, stopped replacement before/after
owner construction and conservative release refusal. Additional component cases
retain three snapshot acquisitions through unload/reload, compare saved historical
bytes, and settle every token under repeated busy/withheld acknowledgements both
normally and after stop. Synthetic byte arrays prove custody, not schema validity.
The earlier **238/0**, PID 57368, predates the three-view extension. An initial
test compile used implicit brace assignment for explicit acquisition-ID
constructors; corrected before any focused execution.

Actual current-Host x64 Debug runs all exited zero without error diagnostics:

| Case | Runtime suffix / PID |
| --- | --- |
| platform-report | `4eb04d89` / 71468 |
| platform-report-cancel | `81adb9c5` / 74432 |
| platform-report-shutdown | `48cd3b80` / 89496 |
| platform-report-departure | `eac9d55f` / 87184 |

Snapshot uses the production Windows capture backend: three successful capture
receipts, both cold/refresh historical files byte-identical after unload/reload,
three historical backing retirements, two Platform unloads and Executive native
join/retirement/unload. Cancellation consumes its interruption receipt at 37 before
Platform unload 42; shutdown consumes it at 45 before Platform unload 54. The
departure run retires/unloads outgoing Executive at 43/44/48, then settles the
second capture at 57, records its receipt/publication marker at 60/61 and successor
acquisition at 62; successor joins/retires/unloads at 78/79/83. The injected
Platform backend delays that capture; this is actual Host/native requester
retirement evidence with controlled capture data, not a visible native window run.

Each `build/pump-current-<suffix>/binary-provenance.json` records exact loaded paths
and hashes. Host remains 11:37:55.990 UTC (`8101F2B5...B677`); Executive is
12:22:49.268 (`8936DB29...2BD4`), Replacement 12:23:07.292 (`7C6EB1C9...BF47`),
production Platform remains current at 10:25:25.464 (`3A4B2E63...65A7`), and the
rebuilt InputPlatform is 12:19:23.748 (`7625D3F2...0F36`). Provider is unused.
Clean logs are `build/preamble-pump-report-{platform,current,replacement,custody}-build.log`,
all zero warnings/errors. Maintained Python checks now require the exact Executive
retirements and Platform unload counts; Python remains unexecuted. Equivalent
recorded-log/file checks, diff and tracked line endings pass. Coordinator review
accepted this increment for the stated split scopes. Injected/native report and Input Host consumers,
legacy removal, final contract reset/artifacts/configurations and current-source
reconciliation remain open.

### Pump injected report failures and overlap (23 Nov)

`CReportExecution` now also handles the injected capture/validation failures and
three correlated overlapping requests. Each failed refresh leaves the original
snapshot acquisition readable; a fresh acquisition of that same backing is
released before the next phase. The foreign producer-failure injection remains
unauthorised and cannot settle the Executive request. The three overlapping
requests use fixed posted/seen state and a bounded pass of at most three
submissions, stopping immediately on busy. Their outcomes are matched by slot,
not arrival order. Successful views enter known custody before semantic checks so
ordinary test failure can still clean them up; duplicate/contradictory acquisition
identity remains conservative unsafe retention. Historical initial/update views
survive Platform unload/reload until all explicit releases are acknowledged.

The old `injected_report` helper/branch is removed. The shared one-way publication
continuation now explicitly schedules its next named phase after accepted output,
including recovery from busy; there is no new queue or Host scheduling mechanism.
`--pump-report-injected --log-tag=preamble-pump-report-injected` passes **641/0**,
PID 13064. Cases cover both refused updates, partial overlap publication with one
available slot at a time, wake-failed acceptance, reverse-order outcomes, stop
before/after successful overlap output, known-versus-unread cancellation and
duplicate-output unsafe retention. The earlier report custody suite was rerun for
this shared continuation change: **489/0**, PID 11060, tag
`preamble-pump-report-continuation`.

Actual run `build/pump-current-597dcf44`, PID **92556**, exits zero without error
diagnostics. It records capture refusal 39/41, validation refusal 43/45, overlapping
refresh success 47/49, Platform unload/reload, three backing retirements 72/73/75,
second Platform unload 79 and Executive pass/join/retirement/unload 88/89/90/94.
This uses the controlled native capture backend through real Host report routing;
it is distinct from the production Windows capture evidence above. The provenance
manifest records Executive 12:30:35.953 UTC (`C90C369F...4A54`), unchanged current
Host and InputPlatform, and unused Provider/Replacement binaries. Clean logs:
`build/preamble-pump-report-injected-build.log` and
`build/preamble-pump-report-injected-tests-build.log`, zero warnings/errors.
The maintained Python retirement checks now include this case, but Python remains
unexecuted. Recorded-log checks and diff/line endings pass. Coordinator review
accepted this narrow extension; native/window and Input Host consumers, full legacy
removal and final contract/configuration/artifact qualification remain open.

### Pump native window/report consumer and fixture entry removal (23 Nov)

`tests/host_pump/native_execution.hpp` now retains the create-window borrow,
notification registration, readiness and deferred ordinary command as named
operations in the common Executive context. Busy publication retains the exact
request; accepted requests await their own correlated outcomes. Stop abandons
only unsent registration/readiness, closes local notification access before
quiescence, and waits for accepted readiness/registration outcomes, borrow
retirement and actual native window destruction. Unexpected terminal failures
settle the consumed outcome before failure; ambiguous custody remains retained.
The original module-thread/native-operation affinity is preserved.

The old native test loops are now bounded callback steps. Restriction observations
and timed move/size/menu progression use 1ms wakes; the 150ms stationary hold starts
when its deferred command is accepted, and twenty size moves retain their 10ms
due times. Ordinary replies arriving before shutdown are a test failure. Snapshot
retry does not spin or reset the phase deadline. Normal completion additionally
checks that the Host restriction depth returned to zero.

The live-report mode reuses the report custody helper for two acquisitions and
their acknowledged releases. While refresh is pending, one window responsiveness
sample is taken per callback, retaining the existing bounded Windows
`SendMessageTimeout` operation and measuring its actual duration. No new native
thread or Host API is introduced. All old HostPump fixture raw-thread functions,
blocking helpers and old query-function entry have now been removed from
`tests/host_pump/fixture.cpp`; the current execution export is the only execution
entry used by Executive, Replacement and Provider fixture roles. Framework/Host
legacy paths and the separate Input Host consumers are still outstanding.

Focused `--pump-native-custody --log-tag=preamble-pump-native-custody` passed
**64/0**, PID 46252. Controlled, supplied-facade cases stop before create,
before/after registration and before/after readiness acceptance, retain full
cleanup output, reject duplicate publication, wait for late acknowledgements and
allow cleanup after a recognised registration refusal. The copied descriptor is
overwritten after quiescence. Synthetic OS identities create no window; actual
Windows lifetime is qualified separately below. A live-report component case
holds both acquisitions through their release acknowledgements. Initial test
compile errors were an incorrect quiescence enumerator and missing result-type
qualification; corrected before focused execution.

The Windows visible-window runs were explicitly launched with the native option
outside the sandbox. All exited zero without error diagnostics:

| Case | Runtime suffix / PID |
| --- | --- |
| platform-native-move | `06fc84ea` / 72024 |
| platform-native-size | `eb431a67` / 9480 |
| platform-native-menu | `ec616eba` / 87276 |
| platform-native-report | `c2626550` / 50088 |

Every case records exactly one authenticated native modal scope, borrow retirement
with destroyed window, Executive native join/retirement, and both DLL unloads.
The report run records two successful production capture receipts, **204** window
responses and a maximum measured response of **2,664,605,900ns**, then native modal
authentication at 44, window/borrow cleanup at 46, Executive pass/join/retirement
at 47/48/49 and Executive/Platform unload at 53/60. This is functional native
acceptance with the existing seven-second response timeout, not a latency target
or a claim of sub-millisecond Windows responsiveness. The current report bytes
were generated as `window-refresh.bsd` in that isolated runtime.

Runtime provenance records the current Host (`8101F2B5...B677`), production Platform
(`3A4B2E63...65A7`) and Executive 12:43:54.558 UTC (`F5359191...E034`). Provider,
Replacement and InputPlatform were unused. Subsequent brace-only style adjustments
were rebuilt without repeating the passing native runs. Clean build logs include
`build/preamble-pump-native-{build,tests-final-build,current-build,format-build}.log`
and `build/preamble-pump-legacy-{provider,replacement}-build.log`; all zero
warnings/errors. The maintained Python checker strengthens retirement counts but
remains unexecuted; equivalent recorded-log checks pass. This native/fixture-removal
checkpoint is coordinator-accepted for this scope. Input Host migration and final legacy,
version-1, artifact, configuration and current-source reconciliation remain open.

### Input Host source custody preparation (23 Nov)

`tests/input_host/source_execution.hpp` prepares the remaining consumer migration
with one named transfer/acquisition/installation/release/disposal operation.
It is fixture-private and not yet selected by the real Input Host consumer.
Each invocation attempts at most one publication. Busy preserves exact custody;
accepted-wake-failed publication is never repeated. Stop cancels unsent work but
harvests late accepted transfer/acquisition and installation results before
acknowledged cleanup. Installation success remains available to the owning
consumer for later configuration retirement, including success received after
stop. No acquired byte view is retained or accessed. Recognised transfer,
acquisition and diagnostic-bearing installation refusals settle their consumed
wait; contradictory custody or unproven release/disposal remains unsafe.

Focused `--input-source-custody --log-tag=preamble-input-source-custody` passed
**478/0**, PID 58088. The fifteen controlled cases cover all stop boundaries,
busy/wake-failed publication, withheld installation acknowledgement/completion,
known refusal cleanup and unsafe terminal results. This is supplied-facade
component evidence, not real Input Host/Platform acceptance. The first test build
found an incorrect test field name (`AssetAcquireRequest::asset` rather than
`source`); corrected before execution. `build/preamble-input-source-final-build.log`
is clean. Borrow/reader lifecycle, scenario conversion, actual common-context
selection and regenerated measurement output remain unfinished.

The coordinator accepted the private measurement rename for this migration:
`wait_requests` will count a module request to park, not actual Host parking;
`service_passes` will count bounded consumer service passes. Historical
`waits`/`rounds` are semantically different. WINDOW output, logs, `report.json`
and maintained documentation must share the precise new definitions. Existing
Host/thread or process evidence remains necessary for actual idle/spin claims.

### Input Host borrow and initial consumer selection (23 Nov)

The source helper preparation above was coordinator-accepted. The subsequent
`borrow_execution.hpp` retains the existing eight-borrow bound and uses the
existing `CServiceBorrowClient`. Acquisition, registration, cancellation, release
and quiescence have named, bounded publication state. A Host closing notice
removes helper view access but cannot publish consumer quiescence until the outer
owner explicitly closes every local reader. Accepted notification and acquisition
outcomes still gate forgetting the record after retirement; an expired late
notification never becomes accessible. A refused ordinary acquisition stops the
bounded pass so a younger routed request ID cannot overtake it. The outer consumer
also serializes preparation against retained ordinary output, including input
boundary acknowledgements, without pausing lifecycle or message handling.

`--input-borrow-custody --log-tag=preamble-input-borrow-order` passed **260/0**,
PID 88004, after the monotonic-request-order guard. The earlier 260/0 PID 82872
predated that guard. Controlled cases cover all acquisition/registration stop
boundaries, capacity refusal, accepted-wake-failed publication, Host closing
before local closure, late terminal/registration results, never-published late
retirement, safe slot reuse and the eight-slot limit. This is component evidence,
not native-thread or real-provider retirement qualification.

The new module-owned `CInputExecution` in `execution.hpp/.cpp` uses these helpers
for common setup, five input readers, frozen reporting and teardown. It keeps
the existing per-pipe input ownership and ordering checks. Reader service is
bounded to 32 steps per pipe; a retained validation retry uses a timed wake.
Boundary publication remains retained on busy and `reader.submitted()` is called
only after transport acceptance. No thread wait/read/post loop remains in this
selected consumer. `execution_fixture.cpp` supplies thin affine allocation,
handle and destruction adapters and the standard Host child coordinator.

Configuration installation tokens are copied to two separate outer slots as
soon as successful results arrive, including after shutdown, and before source
helper reuse. Normal teardown clears each only on a successful correlated
retire-configuration completion, then retires the remaining borrows/routes and
unloads Platform. Global shutdown keeps the token values in the consumer object;
consumer completion means source and reader/borrow access ended. It does not
claim that provider-owned configuration storage was destroyed. The existing
Platform `stopped()`/Host retirement gates provide that separate proof. This
path requires shutdown context or an already-reported fatal callback's agreed
framework-wide escalation, not a busy/closing reply alone.

`--input-installation-cleanup --log-tag=preamble-input-installation-cleanup`
passed **147/0**, PID 89980. These actual consumer invocations use a controlled
initial installation boundary and supplied facade. They hold source cleanup and
configuration retirement output, withhold acknowledgements, and inject global
shutdown before a late successful installation result. Normal cleanup retains
the token through retirement ACK and completion; shutdown completes local
custody with the value still recorded and no new configuration request on closed
admission. This is not a real Host shutdown or Platform storage-destruction test;
that integration proof remains outstanding. Initial compile errors (a missing
controller header and overloaded test-accessor name) were corrected before runs.
`build/preamble-input-installation-final-build.log` is clean.

The Input Executive/Secondary project source lists now select the new execution
adapter and implementation. Only Executive has been rebuilt/qualified at this
increment; old consumer/scenario/report sources remain temporary migration
references and must be removed after their remaining scenarios are converted.
The selected implementation currently qualifies only synthetic `lifecycle`;
other cases and the secondary consumer deliberately remain unqualified.

Actual current-Host synthetic lifecycle run `build/input-current-c3cb9184`,
PID **91840**, exits zero without error diagnostics. It records five independently
applied pipes at ordinal 62, frozen traces at 63, Platform unload 69, acceptance
73, affine consumer destruction 75, Executive join/retirement 76/77 and unload
81. Console output reports zero diagnostic/source/per-pipe losses and zero
missing/invalid/stale measurements. The ten applied records correlate with the
sixteen publication traces; six are boundary records. This is synthetic input
through real Host routing and rebuilt DLLs, not physical controller acceptance.

The manifest records Executive 13:35:59.030 UTC (`20691AE2...577B`), current Host
`8101F2B5...B677` and unchanged current InputPlatform `7625D3F2...0F36`; production
Platform is listed but unused. The initial DLL build exposed a missing placement
classification for the new affine adapter; adding that exact path to the existing
policy catalogue yields clean `build/preamble-input-lifecycle-policy-build.log`.
No production allocation mechanism changed. The subsequent counter refinement
excludes a failed callback's rejected progress from `wait_requests`; its positive
process evidence above is unchanged in scope. The maintained Python column names
and README now describe wait requests/service passes accurately; Python remains
unexecuted. Remaining scenario, secondary, shutdown/native, legacy removal and
final current-source/configuration/artifact qualification are still open.

The coordinator accepted this initial Input consumer checkpoint for the stated
260/0, 147/0 and `c3cb9184` scopes. That acceptance does not cover later scenario
conversion or the provider storage proof below.

### Input cleanup, recovery and Platform shutdown retention (23 Nov)

The selected consumer now also converts `source-cleanup`, `acquisition-cleanup`,
`history-freshness`, `ingress-loss`, `work-pressure` and `shutdown-blocked` into
bounded invocation stages. Source cleanup retains accepted transfer/acquisition
custody without byte access. Acquisition cleanup retires rejected and
prepared-only records before reusing the same pipe. History/ingress cases keep
the original loss and fresh-input recovery assertions. Work pressure checks the
65-output continuation, full-pipe fairness and sixteen ordinary service commands.
Shutdown fills an action ring, establishes a blocked boundary, then requests
Executive unload with every reader paused. The consumer closes local access and
retains copied configuration tokens for the existing Platform shutdown path.

A closing borrow control can precede both preparation and the stop callback.
`execution.cpp` now enters cleanup before a setup/scenario can dereference that
closed view, reporting an unexpected closure once. An expected terminal refusal
remains available to the negative acquisition scenario. Supplied-facade actual
consumer test `--input-unexpected-close --log-tag=preamble-input-closure-cancelled`
passes **19/0**, PID 57604, including quiescence and cancelled terminal settlement.
The earlier PID 48748 result used a synthetic successful terminal outcome; the
current fixture models cancellation explicitly.

`PlatformInput_test_cases.cpp` adds `installation_shutdown_retention` through the
real Platform runtime, common endpoint fixture and service router, with controlled
schema replies and native adapters. It holds an accepted preparation across stop,
delivers a late conditioned descriptor, and withholds its release ACK. It also
stops with a fully installed configuration and live input reader/borrow: repeated
passes retain the configuration and prohibit `work_complete`. After reader closure
and Host quiescence, a zero-time-budget invocation deliberately leaves retirement
storage pending; completion remains false until dispatch time permits its bounded
clear. Closed admission rejects a new installation. No production lifetime gate
or protocol changed. `--platform-input-shutdown
--log-tag=preamble-input-platform-shutdown` passes **1,806/0**, PID 79740. Clean
`build/preamble-input-platform-shutdown-build.log` covers both focused tests.
This complements the earlier consumer late-token test; it is controlled Platform
storage/borrow evidence, not a new real native conditioning-worker test.

All six actual Host synthetic processes exit zero, without error diagnostics,
using `build/run-current-input.ps1 -Case <name>` and the recorded manifest in each
runtime directory:

| Case | Runtime suffix | PID | Specific observed result |
| --- | --- | --- | --- |
| source-cleanup | `a8c79dfb` | 92804 | Late transfer/acquisition retired, marker 67 |
| acquisition-cleanup | `cb5becd9` | 16952 | Rejected/prepared-only acquisitions retired before reuse, marker 63 |
| history-freshness | `a7f3583e` | 90100 | One history loss, held baseline suppressed, neutral/fresh press recovery |
| ingress-loss | `af6c9fe5` | 66868 | 1,742 desktop ingress losses, fresh-input recovery, no source feed loss |
| work-pressure | `bb2f9842` | 57008 | Action occupancy 256, one action loss, independent input/service progress |
| shutdown-blocked | `c738d73a` | 27556 | Full unread output and pending boundary at global shutdown |

The first two use Executive 13:43:55.053 UTC (`9E78A37E...DF74`), clean
`build/preamble-input-cleanup-dll-build.log`. The latter four use Executive
13:49:01.892 UTC (`BD91E8C2...1C55D`), clean
`build/preamble-input-closure-dll-build.log`. Every run uses current unchanged
Host `8101F2B5...B677` and InputPlatform `7625D3F2...0F36` from the preceding
checkpoint. The manifests give full binary paths/hashes; listed production
Platform is unused. Non-shutdown consoles have zero measurement diagnostics;
losses above are intentional scenario results, not loss-free throughput claims.
Shutdown records its request at 64, local-access completion at 67, affine destroy
68, Executive join/retirement 69/70 and unload 74, Platform unload 81, then Host
worker exits 86/88. Consumer completion explicitly does not assert provider
configuration destruction; the separate Platform gate is exercised above.

Pressure/transitions/load/secondary/native scenario conversion, Secondary rebuild,
physical Input acceptance, maintained Python runner execution, legacy removal and
final current-source/version-1/configuration/artifact qualification remain open.

The coordinator accepted the cleanup/recovery and Platform shutdown checkpoint
for these exact controlled and process scopes; no further repeat was requested.

### Input load scenario conversion (23 Nov)

The load scenario now retains its warm-up/window counters in the module-owned
object and publishes at most one scheduled observation group per invocation.
Catch-up groups return through normal input/lifecycle service; future due times
request a timed wake. The original warm-up, three windows, source-loss checks,
total and per-window output counts, drained-input checks and independent awake
clock comparison remain. `wait_requests` and `service_passes` measure the new
callback behavior, not historical thread wait/cycle counts or actual Host parking.

Clean `build/preamble-input-load-dll-build.log` rebuilt Executive at 14:05:37.628
UTC, SHA `183690F0...D7F3F`. Actual `load-smoke` run `input-current-9f58f1f3`,
PID 85016, and full `load` run `input-current-e5b47040`, PID 83432, both pass
using `build/run-current-input.ps1 -Case <name>` with current Host/InputPlatform
provenance unchanged. Both use tracing, four controllers at 120 Hz and two Host
workers. No build/test work ran alongside the full timing window. Each full
ten-second window records 4,875 offered/admitted observations, exactly 2,400
applied records per controller and 75 desktop records, with zero missing,
invalid or stale records. Source and all input loss counters are zero. This is
one synthetic load configuration, not the pending final rate/pipe/worker/native
matrix. Maintained Python execution and final artifact regeneration remain open.

The coordinator accepted that exact load scope; broader configuration and
performance claims remain excluded.

### Input pressure and transition scenarios (23 Nov)

`pressure()` now advances the five action rings one observation/occupancy proof
at a time. It retains capacity-minus-one/full/overflow checks, another pipe's
press/release while full, cancellation and neutral recovery, desktop text ring
saturation/recovery, timed 25/100/500 ms selected-pipe pauses and a 100 ms all-pipe
pause. `advance_report()` preserves the original additional 30 ms of normal
service after provider freeze before freezing consumer metrics, so untraced
history escaping that handshake remains detectable by correlation.

`transitions()` preserves focus/modal/history boundaries with all four controllers
held, button rearming independently of a suppressed stick, device removal and
fresh identity, stale assignment refusal, local cancellation/context change with
the action ring full and its marker blocked, explicit application resumption,
and retirement with unread input before reacquisition. Reacquisition clears only
per-connection reader state, retaining cumulative counters. No Host or Platform
protocol changed.

The first pressure compile found action constants formerly supplied by the old
consumer header; the selected scenario file now declares those fixture constants
locally. Clean `build/preamble-input-pressure-final-build.log` produced Executive
14:10:05.501 UTC (`6200EAA4...E6790`); actual process
`input-current-98fdb71d`, PID 36316, passes. All five action high-water marks are
256, text reaches 64, and each deliberate overflow/recovery marker appears.
Requested pauses measure 25,706,900 / 100,854,100 / 500,600,200 ns. Intentional
history/ingress losses during burst pauses are reported explicitly; this case
does not claim loss-free throughput. Diagnostics and missing/invalid metrics
are zero.

Clean `build/preamble-input-transitions-final-build.log` produced Executive
14:13:03.978 UTC (`DF72396C...7F237`); actual `input-current-4c213692`, PID 90556,
passes with the transition marker at 63, Platform unload 70, consumer destroy
76, Executive join/retirement 77/78 and unload 82. Source/measurement diagnostics
are zero; controller action high-water reaches 256. Both processes use the same
current Host/InputPlatform as above, tracing/two workers and synthetic input.
Manifests and consoles reside in their runtime directories. No prior passing
load matrix was repeated. Secondary/native conversion and all final removal,
version-1/rebuild/artifact requirements remain outstanding.

The coordinator accepted the recorded pressure/transition scope without requesting
a repeat of earlier passing matrices.

### Input Secondary cleanup barrier (23 Nov)

The Secondary DLL now uses the same module-owned class/facade and common serviced
driver. It opens its own routes, installs its own controller configuration,
acquires one pipe and applies an independent press/release. Cleanup retires its
reader/borrow before the existing provider barrier request; configuration and
route retirement cannot advance until the primary releases that request. Once
cleanup settles, Secondary reopens the control route and retains the value-only
completion notice through busy publication. Accepted/wake-failed publication is
recorded in that same invocation, so the primary's unload may overtake the notice
ACK safely. The module waits for unload rather than issuing Executive self-stop.
An interrupted barrier is reported once as fixture failure, followed by valid
cleanup progress under existing global shutdown. An unsent completion is cancelled
on stop; it is not published by the stopping callback.

`--input-secondary-cleanup --log-tag=preamble-input-secondary-cleanup` passes
**191/0**, PID 81256. Actual consumer invocations use a supplied facade and a
controlled post-pipe/source cleanup boundary. They hold barrier acceptance/result,
configuration retirement publication/ACK, both route closures and the final notice,
including stop while awaiting the barrier, stop while final publication is busy,
and stop immediately after accepted-wake-failed notice publication. Global stop
retains the copied installation value instead of inventing an explicit retirement.
Clean build logs are `build/preamble-input-secondary-tests-build.log`,
`build/preamble-input-secondary-primary-build.log` and
`build/preamble-input-secondary-dll-build.log`.

Actual synthetic `second-consumer` run `input-current-2f9e201a`, PID 38136, exits
zero with zero measurement/source-loss diagnostics. Secondary independently applies
at ordinal 85; primary observes the retired pipe while cleanup is held at 86;
Secondary affine destruction/join/retirement/unload occur at 89/90/91/95. Primary
then reacquires a fresh connection, applies another press/release, and later joins,
retires and unloads at 112/113/117. The manifest records Executive 14:20:21.714 UTC
(`712F1832...69EAD`), newly rebuilt Secondary 14:21:19.364 UTC
(`761A53BE...66D59`), and unchanged current Host/InputPlatform. This qualifies the
real DLL barrier/unload path separately from the controlled stop/capacity cases.
Native observation conversion, physical confirmation, removal of the now-unselected
old consumer sources and final version-1/current-source/artifact qualification
remain open.

The coordinator accepted the Secondary checkpoint for those controlled and actual
DLL scopes. Native preparation was subsequently reviewed against the old loop
and accepted as compile-only preparation. `native_pass()` retains a separate
absolute 60/120-second end time, one named context/assignment operation, local
cancel before publication and terminal/boundary/resume ordering, key-edge state,
and the existing title observations. Ordinary observation requests a wake within
10 ms; command/boundary waits retain their own progress/deadline path. Clean
`build/preamble-input-native-dll-build.log` is not native runtime evidence.

The source audit preserved the detailed freeze-failure backlog/frontier/mask
diagnostics as a once-per-failure report, and removed the unselected
`tests/input_host/consumer.hpp`, `consumer.cpp`, `scenarios.cpp` and
`measurement.cpp`. Executive/Secondary filters now match their current source
lists, and MorphicTests maps its three consumer implementation files. Clean
`build/preamble-input-removal-primary-build.log` and
`build/preamble-input-removal-secondary-build.log` rebuild both DLLs after that
audit. No earlier process result is relabelled as validation of these later
diagnostic/native additions. The maintained Python runner remains unexecuted.

**User scope decision, 23 November:** physical Input testing is deferred to
**24 November as a separate follow-up**. Operator confirmation for instrumented
`native` and production `ordinary` Input is unverified and does not block the
remaining implementation, legacy removal, final builds, automated validation or
artifact regeneration. No interactive observation sessions will be launched or
availability re-requested today. The maintained README checklist remains: focus
the window; exercise keyboard/controller A, sticks and triggers; hold A/stick
across Alt+Tab and `C`, then release/centre before fresh input; test `U`/`R`
unassignment/reassignment; let each timed window finish and report observations
separately from automatic cleanup. Final binary/configuration hashes must be
recorded for that follow-up after the completion rebuild. This deferral waives
no other final validation requirement.

### Legacy adapter removal, 23 November — coordinator accepted

Removed the old `SHostPumpV1` query/contract, `TServiceThreadPump`, raw direct
client, and associated project entries. `CPumpedClient` now only drives the
common execution preamble, with `EExecutionStop` passed directly. Its invocation
unwind and sealed destroy-refusal retention proofs are unchanged. `CProviderClient`
now binds only the common facade; retained exchanges keep accepted wake-failed
publications exactly once and busy submissions remain unaccepted.

Validation: MSBuild `visual_studio/MorphicTests.vcxproj`, Debug x64,
`BuildProjectReferences=false`, followed by
`MorphicTests.exe --module-preamble --log-tag=preamble-legacy-adapter`:
**38,257 passed, 0 failed**, PID 47312. The clean build log is
`build/preamble-legacy-adapter-final-build.log` (zero warnings/errors). Its DLL
cases used the previously rebuilt DLLs; this is explicitly not final rebuilt
consumer provenance. The coordinator accepted this scoped increment. Remaining
ordinary lifecycle/transport compatibility removal, final version-1 contract
audit, current-source configurations and regenerated artifacts remain open.

### Private lifecycle and direct-path removal — coordinator accepted

Removed ordinary create/stop/readiness/seal controls, public endpoint/direct
descriptors and their type registrations. Requested-child records keep their
logical thread and copied execution descriptor; all created, ready, ended and
retired notices use the private lane. Module POD reads/posts no longer fall back
to growable queues. Failed setup remains an unexposed null-channel record whose
normal drain/retirement path is safe. Maintenance keeps module heads in their
own records and uses its 128 POD/32 owning capture slots only for Host workers.
The old module multi-capture, reservation and replay branches are gone.

`ThreadConfig` retains the worker entry/preparation path and removes the old
module entry/context fields. TimedWait now exercises worker resources rather
than the removed module entry; it is compile-reviewed at this checkpoint and
must execute during final core validation. Direct connections retain only their
private fixed-ring descriptors and admission gate. Consent, both activation
acknowledgements, establishment deadlines, quiescence and native join gates remain.

Both Debug x64 test builds have zero warnings/errors:
`build/preamble-private-lifecycle-removal-build.log` and
`build/preamble-direct-legacy-removal-build.log`. Focused runs used
`MorphicTests.exe --<group> --log-tag=preamble-private-lifecycle` for
`host-maintenance` **587/0**, `execution-failure` **2,152/0** and
`ordered-execution` **15/0**. With log tag `preamble-direct-removal`,
`common-bulk` passed **17,389/0**, `common-establishment` **748/0**,
`common-pod` **2,187/0**, `common-native-join` **49/0** and
`common-multi` **2,039/0**. Existing controlled/native-boundary scope distinctions
apply; these are not final rebuilt-DLL results. Coordinator review accepted this
removal increment without further findings.

Python 3.14 was located at
`C:/Users/ritch/AppData/Local/Python/pythoncore-3.14-64/python.exe`.
Its `-B tests/input_host/check_sources.py --root D:/TheMorphicEngine` run passed:
**46 runtime source/header entries and filters match**. Earlier Python acceptance
runners remain unexecuted historically; the interpreter discovery does not alter
their evidence. Ubuntu WSL is installed; final Linux toolchain validation is pending.

### Operation output compatibility removal — coordinator accepted

Asset/module completions, prepared borrows, service commands/results, schema and
report publications now use only their existing retained operation records and
bounded recipient output turns. Removed unreachable immediate legacy-output
branches and module TQueue poisoning/ambiguous-acquisition state. Host worker
queue poisoning and its conservative lifetime handling remain. Successful
fixed-ring publication still installs custody even when the subsequent wake
fails; busy is definite nonpublication. Terminal cancellation and recipient
quiescence gates remain independent of transport acceptance.

Clean Debug x64 build: `build/preamble-output-legacy-removal-build.log`.
Focused commands use `MorphicTests.exe --<group> --log-tag=preamble-output-removal`:
`host-services` **5,753/0**, `schema-process` **1,221/0**, `bundle-service`
**760/0**, `bundle-requests` **1,924/0**, `platform-report` **1,468/0**.
These controlled recipient/actual worker cases precede the final Host and DLL
rebuild. The coordinator accepted this scope without further findings. No final
completion or cross-configuration claim is made here.

### Final contract/reference audit and qualification in progress

The coordinator accepted removal of the unused old `SContract`/`SBulkPolicy`
containers and their helper; no active consumers remained. The missing-export
fixture is now `MorphicLifecycleMissingExecution`, with maintained cases
`execution-required` and `normal-missing-execution`. Its absence of the new
execution export remains the negative test. The obsolete scenario-migration
comment now describes unknown-case rejection.

Current Host/POD/bulk, bootstrap, asynchronous lifecycle, timing, restriction,
input and borrow references describe the common facade/private lifecycle path.
Earlier design and validation records are explicitly historical, including the
pre-implementation proposal audit. Active-source searches find no old pump export,
raw module entry, public endpoint/direct control headers or migration adapters.
Execution/context/invocation and private direct descriptor markers are version 1;
bootstrap and unrelated service/storage versions are unchanged. The distinct
`morphic_module_execution_v1` export prevents historical pump-layout acceptance.

The tracked line-ending check, changed/untracked C++ header/LF/final-newline and
documentation-file-link checks, diff whitespace check, Visual Studio filter check
(12 pairs; three projects without filters skipped), and Input source check
(46 entries) pass. Python 3.14 is available at the explicit path recorded above.
Ubuntu provides GCC 13.3, Clang 18.1.3, CMake 3.28.3, Ninja and Python 3.12.3;
current-source GCC Debug configuration succeeded.

Final build/validation records are being collected under `build/preamble-final/`.
`preamble-final-build.ps1` builds the solution, all six lifecycle fixture roles,
all three Pump roles, all three Input consumers and SchemaFixture for each
Windows configuration/architecture, retaining individual build logs and binary
SHA-256 manifests. The initial sandbox multi-node MSBuild start stalled and was
cancelled; final builds use a single MSBuild node outside the sandbox. The later
unused-header cleanup interrupted compilation before tests, and affected builds
were restarted. Those incomplete attempts are not qualification evidence.

Final Windows Debug/Development/Release x64/Win32, Linux builds/tests, matched-DLL
process validation, current artifact regeneration and coordinator final review
are still outstanding at this entry. The separately deferred 24 November physical
Input follow-up does not block these automated requirements.

The first GCC compile exposed Windows-only file/environment calls in the shared
lifecycle fixture headers and an ambiguous empty memory-view assignment.
`fixture_io.hpp` keeps the same fixed handoff path/modes and nonempty scenario
environment values, using the existing Windows calls or POSIX counterparts.
The empty view is now explicitly typed. Coordinator source review accepted these
fixture-only corrections. Compiler diagnostic excerpts are retained in
`build/preamble-final/linux-gcc-debug-portability-errors.txt`; the resumed build
log is separate qualification evidence.

The first Windows x64 Debug ordinary run (PID 67340) failed two TextureBatch
assertions because `ShaderBatch_cases.hpp::resource_groups` bypassed the migrated
fixture's disposal credit helper. It now calls `fixture.dispose` with the same
asset/correlation. TimedWait **328/0**, HostPump **110,580/0**, and all other groups
passed in that failed overall run; this is not a whole-suite pass. The first GCC
CTest run passed **29/30 entries**, including 22 synthetic Input cases, both Pump
matrices and both Host acceptance cases. Its ordinary entry also hit the disposal
fixture issue and two Audio rebinding assertions.

Contextual diagnostics and `LD_DEBUG=files` establish that glibc marks the first
Audio image `NODELETE`; later fixture bootstraps correctly refuse the already
installed image. The retained 21 November 15:50:15 UTC binary at
`build/linux-identity/bin/MorphicAudio.so` contains the same default-visible GNU
unique `std::from_chars` table. CMake visibility flags, native binding code and
once-only installation predate this refactor and are unchanged. This is an
observed Linux loader/bootstrap limitation: successful native close does not
prove image globals reset, so same-file Audio unload/rebootstrap is not qualified.
No production bootstrap or linker-policy change is included here.

The DLL fixture now keeps one installed binding/debug/memory lifetime while
creating fresh pumped-success, serviced-success and pumped-failure contexts.
It preserves all identity, cleanup, join and affinity assertions, and requires
empty attribution after the preceding graph retires before resetting observations.
Both ordinary and focused selectors use this sequence. Platform remains pumped
only, and the lifecycle Service remains serviced only. This qualifies execution
contexts within one installed image, not Linux image unload/reload. Corrected GCC
focused ModulePreamble passes **37,767/0** (PID 1048), with
`linux-gcc-debug-binding-build.log` and `linux-gcc-debug-binding-tests.log`.
The failed ordinary CTest entry was rerun separately; passing siblings were not
repeated. Windows and remaining configuration qualification continued as below.

### Final qualification corrections and split-run evidence

The disposal credit correction exposed a second fixture omission:
`SResourceFixture::progress_host` retried ordinary asset output but omitted the
separately retained bundle producer. Its existing eight-pass bound now retries
both, rechecking recipient capacity between them. ShaderBatch verifies the
uncorrelated disposal notification, empty inbox, zero credits and final asset
idle state. The coordinator accepted the source correction and GCC TextureBatch
**2,070/0**. Both unsuccessful ordinary attempts remain recorded. The subsequent
isolated GCC ordinary CTest passed in **74.70 seconds**, including shared
TextureService consumers. Together with the original 29 passing siblings, this
qualifies all **30 GCC Debug entries** with explicit split-run provenance.

Windows x64 Debug ordinary also passes after these fixture corrections:
TimedWait **328/0**, HostPump **110,387/0**, TextureService **162/0**, TextureBatch
**2,070/0**, with the full suite reporting success. Its 45 lifecycle cases are
split across the original 32 successes, a corrected checker applied to the
recorded `normal-unavailable` process, and the remaining 12 runs. The checker
now recognises the common stage-4 endpoint failure and still requires the exact
Executive domain error, matching endpoint join/retirement and DLL unload.
It no longer expects the removed requested-execution diagnostic. The coordinator
accepted the complete recorded-log check; the successful process was not rerun.
The x64 Debug 52-case Pump matrix also passes.

The first MSVC x64 Release and Clang Release Pump matrices stopped at a checker
requiring two `MV_DETAIL` disposal records. Release compiles those calls out.
The checker now treats those records as supplemental when detail events exist;
all configurations still require two schema passes, two output reclamations,
two Executive joins/retirements/unloads and remapped text/span validation. The
second schema pass remains gated by typed, correlated successful disposal
acknowledgements for both assets. Coordinator review accepted this correction.
`build/preamble-final/pump-release-recorded-checker.log` records the complete
maintained checker passing on the three affected Release runs and a Debug run
that exercises the supplemental branch. Only the unrun process cases continue.

The Development solution maps SuiteUTF to Release, whereas standalone project
references request their declared Development output. The first standalone
lifecycle link failed because that library had not been built. Final orchestration
now builds the existing SuiteUTF Development configuration before standalone
consumers. This changes no repository build policy or external source; the failed
link log is retained. Remaining configuration outcomes and final binary/artifact
manifests are still being collected. These entries do not claim final completion.

Clang Release then exposed a shutdown-pending fixture race: after the accepted
delayed service result settles, Host can retire its idle closing route before
Executive's final close arrives. The close parser now also accepts `stale` only
for `shutdown-pending` at `closing_after_stop`, with stop and successful delayed
completion already proven. Sent state, slot, route, request and completed notice
remain mandatory. Nine controlled actual-class paths cover success/stale, wrong
route/request/notice/slot and stale without the exact scenario/stop/completion
proof; busy retains publication and wake failure never duplicates acceptance.
Failure is reported once, with subsequent cleanup progress allowed. No Host
protocol changed. The coordinator approved this correction's scope; focused
PumpClose **142/0** and HostMaintenance **587/0** pass on both Linux toolchains
and the rebuilt Windows configurations recorded in `build/preamble-final/`.
The latter rebuild also restores the fixture allocation-attempt observation's
native `size_t` type, removing its new Win32 narrowing warning without changing
the injection index. The coordinator accepted the source and targeted validation,
including five Windows configurations and both Linux toolchains; Win32 Development
instead includes the correction in its first current ordinary/Pump runs.

Both corrected Clang shutdown process cases pass, completing its otherwise
successful 52-case Pump matrices for one and two workers. Together with its
untouched 28 CTest siblings, all 30 configured entries are qualified by split
evidence rather than a repeated full CTest invocation. GCC affected consumers
were also rebuilt and the focused correction plus shutdown process passed.
Linux CMake supplies ten module images and its 30-entry suite; the six standalone
lifecycle DLL roles and 45-case lifecycle runner are Windows qualification here.

The existing Win32 Debug native-report limit reproduced during final validation:
approximately 6.43 seconds of synchronous schema work exceeded the unchanged
five-second capture deadline. Host rejected the publication, delivered its receipt
and completed cleanup. The same prerequisite prevented its synthetic Input
lifecycle from starting; remaining Input scenarios were not run through that
known failed prerequisite. Other 51 Pump cases, ordinary core and all 45 lifecycle
cases pass in this configuration. The uncontended retry again failed: **6.346 s**
of synchronous schema work, with rejected publication, receipt, Executive join/
retirement and both DLL unloads recorded. Evidence:
`build/preamble-final/Win32-Debug/pump-idle/platform-report-1-l0key9qg`.
This remains an explicitly unsuccessful native-report/Input qualification.


## Physical follow-up correction checkpoint — 24 November

The separately approved fixture-only correction, physical observations, failed
native cleanup run and exact old/new binary provenance are recorded in the
[qualification ledger](module_preamble_validation.md#scoped-correction-checkpoint--24-november).
The original physical run remains failed; both operator checklists and ordinary
automatic cleanup remain accepted observations. The coordinator accepted the new
source and scoped x64 Debug evidence on 24 November. Affected-consumer builds and
narrow tests now pass across all six Windows and both Linux configurations.
No corrected native process or physical controls rerun is
claimed, and the 23 November implementation/automated qualification retains its
historical scope.

Changed implementation: `tests/input_host/platform_access.hpp` factors the existing
fixture friend access and makes native-only dormant-cutover freeze classification
concrete; `platform_fixture.cpp` retains its after-pump handshake; `control.hpp`
adds detailed atomic diagnostics. `execution.cpp`/`execution.hpp` distinguish only
coherent named native freeze expiry, close readers before cleanup publication, and
retain the accepted operation centrally through every cleanup clear path. Failure
remains latched; borrow retirement and real stop remain mandatory. No production
Host/Platform behavior, public contract, generic timeout policy or queue was added.

Changed tests/build metadata: `ModulePreamble_test_cases.cpp`,
`PlatformInput_test_cases.cpp`, focused selectors in `tests/main/MorphicTests.cpp`,
Input Platform project/filter entries and `tests/input_host/check_sources.py`.
The 16 consumer cases cover phase/expiry boundaries, busy and wake-failed
publication, retained late identities, invalid reads and unsafe/non-freeze
refusals. Runtime focus-loss tests preserve the production cutover and baseline,
including strict synthetic/pending-work refusals and post-focus raw publication.

All four affected x64 Debug targets build with zero warnings/errors. Focused/shared
suites total **38,797 passed, 0 failed**, with 271 standalone freeze/focus assertions
also included in the Platform suite. The actual three-DLL synthetic second-consumer
process passes in 4.560 seconds. Evidence and source/binary manifests are under
`build/input-freeze-24nov/`; all five original physical binaries were copied and
hash-checked before rebuilding. The ledger's separate affected-consumer table
records the remaining configuration builds, two narrow test results and synthetic
smokes. The original 23 November matrices are not relabelled as covering later
source. A test-only explicit-local baseline correction resolves the retained GCC
compile failure; only affected test binaries/focus checks were repeated afterward.
Final source/binary manifests distinguish both snapshots. Consolidated closure
review was accepted on 24 November. The scoped fixture follow-up is corrected and
review-complete, with no remaining implementation finding. Original native failure,
both operator confirmations, absent corrected physical/actual Host fatal-timeout
process evidence and the Win32 Debug prerequisite gap remain explicit. No further
code/test work was requested; no commits or pushes.
