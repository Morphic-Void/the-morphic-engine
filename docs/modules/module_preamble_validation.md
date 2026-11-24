# Module preamble final qualification

23 November 2026. Implementation and automated qualification are complete within
the recorded scope, with final coordinator review accepted and no remaining
implementation completion finding. The user completed manual review and
authorised the commit on 24 November 2026. This is not a universal pass: Win32 Debug native report and dependent Input remain unqualified;
the Development lifecycle runner was not run; the pre-existing Linux same-image
Audio reload limitation remains. The separate 24 November physical follow-up
confirmed both operator checklists and passed ordinary Input cleanup. Instrumented
native Input cleanup failed after its confirmed observation. The fixture follow-up
is corrected and review-complete, with consolidated coordinator acceptance on
24 November and the separate affected-consumer qualification below. No corrected
visible/native process was rerun. The scoped 23 November
implementation and automated evidence remains unchanged.

The [design](module_preamble_design.md), [implementation decisions](module_preamble_proposal.md)
and [review checkpoints](module_preamble_implementation.md) define the scope.
Earlier checkpoint binaries retain their stated limitations; this ledger records
the final current-source builds and reconciles the earlier Host provenance gap.

## Final implementation and contract

Host owns the common pumped/serviced preamble, immutable invocation boundary,
authenticated two-pointer facade, private lifecycle rings, fixed ordinary POD
rings and bounded local queues. Module-owned classes hold their configuration,
state and facade. DLL adapters are thin; internal Host calls remain direct.
Lifecycle/frontier accounting, explicit consent, finite output turns, persistent
producer/operation and input rotation, retained-head eligibility, dispatch-scoped
storage reservations, rate-limited diagnostics and affine retirement have the
reviewed focused coverage recorded in the checkpoints. Specialized owning,
bulk, batch, native restrictions and existing domain custody remain separate.

Execution/context/invocation and private connection layout markers are version 1.
Unsupported layouts, flags and partial callback sets remain rejected. The only
execution export is `morphic_module_execution_v1`. Bootstrap and unrelated asset,
schema and storage formats are unchanged. Old module thread/pump adapters,
ordinary lifecycle controls, direct-client transport API and output compatibility
paths are removed. Worker-native preparation and genuine worker transport
poisoning remain because they serve the preserved worker paths.

Active-source searches find no old exports, `SHostPump`, `SPumpContext`,
`TServiceThreadPump`, `CreateThreadRequest`, `EndpointReady`, obsolete missing-pump
case names or their compatibility headers. The negative fixture is now
`MorphicLifecycleMissingExecution`. Obsolete MissingPump/MissingThread maintained
binary outputs were removed in all six Windows configurations; the obsolete
MissingPump object directory was removed. Historical runtime/log trees remain
evidence, not maintained compatible binaries.

## Toolchains, source and binary provenance

The 23 November qualification used shared checkout base
`039f49dd38467fbe0d9987dccf9e9459a469ffbb` plus the then-uncommitted implementation. Final source fingerprints are retained in
`build/preamble-final/source-files.json`. Documentation-only completion updates
do not change the built source. Each configuration directory contains full-path,
size, modification-time and SHA-256 records in `binaries.json`, and current module
symbols plus SHA-256 in `exports.json`.

- Windows: Visual Studio 18 Community MSBuild, v143 MSVC **14.44.35207**, SDK
  **10.0.26100.0**, Python **3.14.0**, Windows 11 build **26200**. The export reader
  is dumpbin 14.51.36231; that is not the compiler used for these builds.
- Linux: Ubuntu WSL2 x86_64, kernel **6.6.87.2**, GCC **13.3.0** Debug and Clang
  **18.1.3** Release, CMake **3.28.3**, Ninja **1.11.1**, Python **3.12.3**.
  `linux-toolchain.txt`, configure/build logs and compile databases retain details.

Windows orchestration is `build/preamble-final-build.ps1`: solution plus all six
lifecycle roles, three Pump roles, three Input consumers and SchemaFixture. All
six configuration/architecture combinations built successfully. Development's
standalone SuiteUTF dependency was built in its declared Development configuration
after the solution's Release mapping left that output absent; the initial failed
link remains recorded. The Linux CMake builds supply the engine, production
modules, Pump/Input consumers, SchemaFixture, tests and policy validator. Linux
does not supply the six standalone lifecycle DLL targets in this qualification.

All final build targets have zero errors. Existing Win32 vendor narrowing/source
encoding warnings and unrelated HostRestriction warnings remain recorded in
`build-warning-inventory.txt`. The new HostMaintenance narrowing warning was
corrected to the allocation counter's `size_t` type and its affected tests rebuilt.
No external source or unrelated warning cleanup was performed.

The last C++ correction is fixture-only shutdown-close handling and its tests.
The affected `MorphicTests`, Pump Executive and Pump Replacement were rebuilt in
every configuration. Earlier successful ordinary/process matrices were not
repeated: targeted tests qualify that final correction separately. Thus a final
SHA-256 does not retroactively identify an earlier pre-correction test executable.
Host, production modules, Input and lifecycle consumers did not change in this
last correction. Their final hashes apply to the recorded acceptance runs.

| Windows binaries | Host SHA-256 prefix | Test SHA-256 prefix |
| --- | --- | --- |
| x64 Debug | `6c525fd1c9a4` | `4154f6a273b3` |
| x64 Development | `5504af64b58f` | `cd568431e002` |
| x64 Release | `3a487e94d5c0` | `a32ca4070150` |
| Win32 Debug | `417470972457` | `e7140327e456` |
| Win32 Development | `848c36778c90` | `38683d518314` |
| Win32 Release | `ca321bf45c82` | `cf0f6679276c` |

These are display prefixes only; manifests contain complete digests.

## Windows results

Evidence root: `build/preamble-final/<architecture>-<configuration>/`.
`core-*/ordinary-console.log`, `lifecycle-*/`, `pump-*/` and `input-*/` contain the
commands' retained console output and isolated process artifacts. The maintained
commands are `MorphicTests.exe -t1`, `tests/module_lifecycle/run_tests.ps1
-SkipBuild`, `tests/host_pump/run_tests.py` and `tests/input_host/run_tests.py`.

| Configuration | Build / exports | Ordinary core | Lifecycle | Pump | Synthetic Input |
| --- | --- | --- | --- | --- | --- |
| x64 Debug | Pass / 16 DLLs | Pass | 45/45, split evidence | 52/52, 2 workers | 11/11, 2 workers |
| x64 Development | Pass / 16 DLLs | Pass | Not run: maintained runner supports Debug/Release | 52/52, 2 workers | 11/11, 2 workers |
| x64 Release | Pass / 16 DLLs | Pass | 45/45 | 52/52, 1 worker, split evidence | 11/11, 1 worker |
| Win32 Debug | Pass / 16 DLLs | Pass | 45/45 | 51 pass; native report deadline fails | Lifecycle prerequisite fails; other 10 not run |
| Win32 Development | Pass / 16 DLLs | Pass | Not run: maintained runner supports Debug/Release | 52/52, 1 worker | 11/11, 1 worker |
| Win32 Release | Pass / 16 DLLs | Pass | 45/45 | 52/52, 2 workers | 11/11, 2 workers |

Every Windows ordinary run includes TimedWait **328/0**, TextureService **162/0**
and TextureBatch **2,070/0**. HostPump counts, respectively in the table's order,
are **110,387 / 110,391 / 110,393 / 110,381 / 110,507 / 110,393**, all zero failures.
Counts include bounded interleavings and need not match across runs. Ordinary
includes the preserved direct/bulk/lifecycle/allocation/failure and domain tests.
The Win32 Development first ordinary run includes the final close-race tests.

The final close-only suite is **142/0**, HostMaintenance **587/0**, and the actual
shutdown-pending process passes on the other five Windows configurations. Their
`close-*-build.log`, `close-*-tests.log` and `close-process.log` records complement
the preceding broad passes. The stale close allowance is limited to stopped
shutdown-pending after the accepted delayed completion; identity/correlation and
terminal-notice checks remain. Coordinator review accepted the correction and
targeted evidence. No Host/public protocol changed.

Lifecycle runtime directories are `build/lifecycle-runtime-28be29ae` and
`73bf3d39` for x64 Debug, `9fe0ddb7` for x64 Release, `df45d155` for Win32 Debug,
and `5a16a6ed` for Win32 Release. x64 Debug's first 32 cases passed; its recorded
normal-unavailable process was checked against the corrected current common
failure diagnostic, and the remaining 12 ran separately. MissingExecution and
normal-missing-execution are included. The recorded checker correction did not
repeat a successful process.

Release Pump's schema-process initially failed a diagnostic-only assertion
because `MV_DETAIL` is compiled out. The complete corrected checker passes on
the recorded x64 Release and both Clang schema logs, plus a Debug record exercising
the detail branch. Required domain success, typed disposal acknowledgement gates,
reclamation, joins, retirement and unload are unchanged. The first three passing
processes were not repeated; only the remaining cases ran. See
`pump-release-recorded-checker.log` and each `pump-remaining-console.log`.

## Linux results

| Configuration | Current build / module exports | Ordinary core | Maintained CTest scopes |
| --- | --- | --- | --- |
| GCC Debug | Pass / 10 images | Pass, 74.70 s corrected run | All 30 qualified by split runs |
| Clang Release | Pass / 10 images | Pass, 11.93 s | All 30 qualified by split runs |

The 30-entry configuration includes policy checks, ordinary and isolated fatal
reflection tests, 22 synthetic Input processes (11 each with one/two workers),
two 52-case Pump matrices and two production Host texture acceptance processes.
TimedWait is **327/0**, TextureService **162/0**, TextureBatch **2,070/0** and
HostPump is **108,791/0** on GCC, **108,796/0** on Clang. These are Linux results;
they do not claim Windows native-device/window or lifecycle-runner coverage.

GCC's first run passed 29 entries; the ordinary entry exposed disposal-fixture
omissions and repeated Audio image installation. The fixture corrections and
coordinator reviews are recorded in the checkpoints. The intermediate ordinary
attempt still failed until the separate bundle output producer was progressed.
The final ordinary entry alone passed; all unsuccessful logs remain retained.
Clang's initial run passed 28 entries; the two Pump entries stopped on the
Release-detail checker. After recorded-log validation, unrun cases continued.
One shutdown-pending run exposed the close race, corrected and tested as above.
Both corrected Clang shutdown processes pass. This is complete test-scope
coverage with explicit split provenance, not a claim that the initial CTest
commands returned success.

Both Linux builds then rebuilt affected consumers and passed PumpClose **142/0**,
HostMaintenance **587/0**, and actual shutdown-pending (GCC one worker, Clang both).
See `linux-*-close-*`, `linux-*-maintenance-tests.log`, original `linux-*-ctest.log`
and the separate continuation logs. No passing CTest sibling was rerun merely
because the fixture changed.

The observed pre-existing Linux same-file Audio reload limitation remains:
glibc marks an image containing a GNU-unique `std::from_chars` table `NODELETE`,
so successful close does not reset its once-installed bootstrap globals. The
historical 21 November binary has the same symbol. The corrected fixture proves
fresh pumped/serviced/failing execution contexts within one installed image,
including identity, allocation, affinity, join and empty attribution. It does
not qualify Audio unload/rebootstrap resetting image globals. Production loader,
bootstrap and linker policy were not changed to conceal this limitation.

## Native, timing and known limits

Four current x64 Debug automated Windows cases pass: move, size, menu and live
report, using two Host workers. `native-2/native-console.log` identifies all four
runtimes. The live report recorded 206 window replies and **2.905 s** maximum
response under the existing seven-second timeout. This is functional native
window qualification, not a low-latency guarantee or physical Input confirmation.

The uncontended x64 Release input baseline passes with one Host worker, four
controllers at 1,000 reports/s each and tracing. It ran **32.483 s**, including
warm-up and three ten-second windows. Each window admitted **40,625** observations
and matched **80,625** expected outputs; source losses, missing/invalid/stale
samples and diagnostics are zero. Offered-to-applied p99 is **0.134–0.161 ms**;
maximum **0.420 ms**. This is one measured synthetic run, not a cross-machine
performance guarantee or a comparison with historical module-owned-loop metrics.
The `wait_requests` and `service_passes` counters retain their documented meanings;
a park request does not prove Host actually parked.

Baseline artifact:
`build/preamble-final/x64-Release/baseline-1/load-1-otm0co20/report.json`.
Debug synthetic runs use the documented 120 Hz qualification, not the previously
unqualified 1,000 Hz Debug rate. Intentional pressure-case losses remain distinct
from loss-free load acceptance.

Win32 Debug's production native report still fails its unchanged five-second
capture deadline. The uncontended final attempt took **6.346 s** of synchronous
schema work, returned the failure/receipt and cleaned up both modules. Evidence:
`Win32-Debug/pump-idle/platform-report-1-l0key9qg`. Its Input lifecycle failed the
same prerequisite; ten dependent cases were not repeated through that failure.
This preserves the [existing qualification gap](platform_report_validation.md),
previously measured at 6.208/6.363 s. The timeout and schema placement are unchanged;
neither this failed case nor the blocked Input scenarios is counted as passing.

## Artifacts and final audits

Current runners regenerated their isolated filesystem images, bundles, baked
schema/report bytes and Input JSON reports. `regenerated-artifacts.json` inventories
the retained artifacts with sizes and SHA-256; failed-run artifacts remain clearly
associated with their failed evidence. Lifecycle runtimes above also retain their
saved-byte integrity and value-handoff outputs. No unrelated stored format or
authored-data version was changed. Historical logs are retained, while maintained
obsolete missing-export binaries and object outputs are removed.

Final checks pass: tracked line endings; changed/untracked C++ headers, LF and
final newlines; documentation file links; diff whitespace; 12 VS filter pairs
(three projects have no filters); Input's 46 source/header entries; and the
previously recorded nine CMake source-extraction tests. The active-source legacy
reference search and 16 Windows/10 Linux module export checks pass.

## Physical Input follow-up — 24 November

These sessions used the final x64 Debug set in `build/bin/x64/Debug`, with two
workers and a connected controller. The coordinator verified all five hashes
unchanged before the runs against
`build/preamble-final/input-physical-follow-up.json`: Host, both Input consumers,
production Platform and instrumented Platform. A later rebuild requires separate
provenance; none of these results qualifies a proposed correction.

| Source / duration | Evidence under `build/input-physical-24nov/` | Operator observation | Automatic outcome |
| --- | --- | --- | --- |
| Instrumented native / 60 s | `lifecycle-2-opyqmvc3` | Incomplete; operator requested more time | Pass, exit 0, 63.643 s |
| Instrumented native / 120 s | `lifecycle-2-mvnnc3py` | Confirmed: “That all worked” | Failed cleanup, exit 3221227010 (`0xc0000602`), 143.577 s |
| Production ordinary / 120 s | `lifecycle-2-fscsvxrf` | Confirmed: “That also all worked” | Pass, exit 0, 123.558 s; no errors |

The confirmed checklist covers keyboard/controller A, sticks/triggers, Alt+Tab
and `C` held-input suppression followed by release/centre and fresh input, and
`U`/`R` unassignment/reassignment. The operator reported possibly switching to
ChatGPT to report success before the test exited. Ending unfocused is valid;
this is evidence consistent with focus loss, not a recorded exact runtime state
or an operator error.

The failed native run completed its observation window, then freeze request 58
timed out: requested true, frozen false, closing false, source failed false,
7,539 pump observations; feed and all four histories empty; desktop busy 1;
mapper/baseline/boundary/action/text masks zero, ingress mask 1. Executive reported
failure, followed by unresolved Platform window use 2 and input uses 4/5, each
with quiescence provenance 0. The engine log truncates at record 73 and no engine
process survived. The exit code is consistent with fail-fast termination; the
truncated log does not identify the terminating call.

**Follow-up failure and correction:** source inspection identifies a legitimate dormant
desktop cutover while ineligible that the fixture's freeze check treats as busy;
the aggregate diagnostics cannot prove which desktop flag was set in this run.
Separately, in the failed binary, freeze deadline expiry marked the consumer
unsafe and suppressed borrow cleanup publications, even after local readers closed.
A narrow fixture-only correction is implemented with the controlled validation
below; the coordinator accepted its source and x64 Debug evidence on 24 November.
Affected-consumer reconciliation is complete as recorded below.
The failed physical run remains failed;
no corrected visible/native process has been rerun. The ordinary operator/automatic
pass and the scoped 23 November results remain distinct.

### Scoped correction checkpoint — 24 November

The coordinator approved this follow-up scope before implementation. No production
Host/Platform runtime or public contract changed. The fixture's shared layout is
rebuilt consistently across its three affected DLLs; the original physical-run
binaries and manifest are preserved in `build/input-freeze-24nov/prior-binaries/`
and `physical-provenance.json`. The historical five-binary manifest is unchanged.

`tests/input_host/platform_access.hpp` shares the existing test-only friend access
with deterministic tests. Native trace freeze alone may accept an exact dormant
ineligible desktop cutover, with attached matching active connection/ingress and
no failure, queue, baseline, loss, runnable work or other pending input/output.
It never clears production cutover/ingress or supplies a native quiet point.
Synthetic freeze keeps its strict drain predicate. Detailed eligibility/cutover/
queue/baseline/loss/runnable diagnostics supplement the original raw masks.

Only coherent expiry of the named native freeze, with settled source custody and
no prior failure/unsafe/helper failure, permits failed-but-safe consumer cleanup.
Readers and local views close before release/quiescence publication. The existing
accepted freeze operation survives every cleanup `clear_operation` path and is
never resubmitted; late replies retain exact typed identity/notice validation.
Borrow retirement and actual stop still gate domain completion. An unread
value-only freeze result may be cancelled through existing Host quiescence after
stop. Invalid deadline reads, unrelated deadlines and malformed replies remain
conservative; no general timeout exemption was introduced.

Current x64 Debug MSVC builds for `MorphicTests`, `MorphicInputPlatform`,
`MorphicInputExecutive` and `MorphicInputSecondary` each report zero warnings/errors
in `build/input-freeze-24nov/*-current-build.log`. Focused/shared results:

| Selector | Passed / failed |
| --- | --- |
| `--input-freeze-cleanup` | 694 / 0 |
| `--input-source-custody` | 478 / 0 |
| `--input-borrow-custody` | 260 / 0 |
| `--input-installation-cleanup` | 147 / 0 |
| `--input-secondary-cleanup` | 191 / 0 |
| `--input-unexpected-close` | 19 / 0 |
| `--platform-input` | 37,008 / 0 |

Total **38,797 / 0**. Commands use `MorphicTests.exe -t1 <selector>` and retained
`current-*.log` output. The freeze cleanup test covers seven phase/expiry cases
(never accepted, accepted/no ACK, ACK/no completion, completed command without
frozen handshake), five conservative cases and four malformed late replies.
It withholds actual stop, tests busy and accepted-wake-failed cleanup, crosses
configuration cleanup before late replies, and gates completion on a controlled
typed borrow-retired notice. This is actual consumer-class/supplied-facade evidence,
not a real Host fatal-shutdown process. The Platform suite includes the **271 / 0**
freeze/focus component: real runtime eligibility and boundary progression with a
controlled native adapter, retained cutover state, strict synthetic refusals and
fresh raw publication after the original eligible baseline. No physical controls
or visible Windows observation were repeated.

The maintained Python runner passes `--source synthetic --case second-consumer
--host-workers 2`, using all three rebuilt DLLs:
`build/input-freeze-24nov/process/second-consumer-2-mxyj1pyq`, exit 0, **4.560 s**.
This qualifies their shared private layout, strict synthetic freeze and normal
Secondary barrier/unload/reacquisition, not the failed native cleanup process.
Source/filter checks, source line endings and diff whitespace pass. Initial
unsuccessful new-test setup attempts remain recorded; corrected status/boundary,
attempt-count and raw-transaction setup precedes the passing evidence above.

New source and binary SHA-256 manifests are `current-sources.json` and
`current-binaries.json` under `build/input-freeze-24nov/`. Test binary prefix:
`948476b1d97e`; InputPlatform `f8addf85711ea`; InputExecutive `ffcb551a88cb`;
InputSecondary `0865e1261033`. These identify the initial accepted x64 Debug
checkpoint, before the test-only portability refinement below. Host and production
Platform retain the physical-run hashes. Source acceptance does not relabel the
original failed physical run.

### Affected-consumer reconciliation — 24 November

Only the four affected targets were rebuilt: `MorphicTests`, `MorphicInputPlatform`,
`MorphicInputExecutive` and `MorphicInputSecondary`. All six Windows configurations
and both Linux toolchains compile these paths. Every build completed successfully;
the Windows build summaries report zero warnings/errors and Linux builds report
no compiler diagnostics. The original GCC failure is retained separately: the
focused test's braced temporary did not bind to `SInputObservation const&`. An
explicitly typed const local baseline, accepted by the coordinator as a routine
test-portability correction, resolves it. No runtime or fixture policy changed.

| Configuration | Four affected targets | Freeze cleanup | Final focus regression | Synthetic three-DLL smoke |
| --- | --- | --- | --- | --- |
| MSVC x64 Debug | Pass | 694 / 0 | 271 / 0 | Pass, `second-consumer-2-mxyj1pyq` |
| MSVC x64 Development | Pass | 694 / 0 | 271 / 0 | Pass, `second-consumer-2-xg4g50se` |
| MSVC x64 Release | Pass | 694 / 0 | 271 / 0 | Pass, `second-consumer-2-n2nitqke` |
| MSVC Win32 Debug | Pass | 694 / 0 | 271 / 0 | Not repeated; unchanged startup-report prerequisite remains unqualified |
| MSVC Win32 Development | Pass | 694 / 0 | 271 / 0 | Pass, `second-consumer-2-o35v0gco` |
| MSVC Win32 Release | Pass | 694 / 0 | 271 / 0 | Pass, `second-consumer-2-i72lgf5u` |
| GCC 13.3 Debug x86_64 | Pass | 694 / 0 | 271 / 0 | Pass, `second-consumer-2-_m3guf1o` |
| Clang 18.1 Release x86_64 | Pass | 694 / 0 | 271 / 0 | Pass, `second-consumer-2-q0jr6cjw` |

Every smoke uses two workers, synthetic sources and tracing on. Windows consumer
builds, cleanup tests and smoke evidence preceded the explicit-local test edit;
only `MorphicTests` and its focus regression were then rebuilt/rerun in all six
configurations. Successful consumer/process checks were not repeated. GCC's
successful build continues the initial partial build (which had already linked
InputPlatform); Clang supplies one successful current build. No broad core,
lifecycle, Pump, Input or physical-control matrix was repeated.

Evidence root: `build/input-freeze-24nov/`. Windows directories are
`<architecture>-<configuration>`; Linux directories are `linux-gcc-debug` and
`linux-clang-release`. The initial x64 Debug build/shared tests/smoke remain at
the root. `reconcile-windows.ps1`, `portability-windows.ps1` and
`reconcile-linux.sh` retain exact commands. Linux's denied initial WSL launch was
a sandbox setup attempt, followed by an approved WSL execution; it was not an
engine/test failure. The original compile diagnostic is
`linux-gcc-debug/build-initial-failed.log`.

Final Windows test/build evidence is `portability-tests-build.log` plus
`current-platform-input-freeze.log`; final hashes are each directory's
`current-binaries.json`. Linux uses `build.log`, both focused test logs and
`binaries.json`. Initial root x64 Debug `current-binaries.json` remains historical;
the final x64 Debug test hash is `dd777a1ecbd3` in `x64-Debug/current-binaries.json`.
`current-sources.json` contains the 11 final follow-up source hashes;
`sources-before-portability.json` preserves the accepted earlier snapshot.
All current manifest hashes were verified, and Host/production Platform hashes
remain unchanged in every configuration. The five preserved original physical
binaries still match `physical-provenance.json`.

Source/filter, UTF-8/line-ending and diff whitespace checks pass. The accepted
source and completed affected-consumer checks establish the scoped fixture
correction; they do not claim a corrected native physical run or actual Host
fatal-timeout cleanup process. The recorded physical failure and Win32 Debug
prerequisite limitation remain explicit. Consolidated coordinator closure review
was accepted on 24 November: this scoped fixture follow-up is corrected and
review-complete, with no remaining implementation finding or further code/test
work requested. No commits or pushes.

The maintained commands used for the physical sessions were:

```powershell
python tests/input_host/run_tests.py --engine build/bin/x64/Debug/MorphicEngine.exe --output-directory build/input-physical-24nov --source native --seconds 60
python tests/input_host/run_tests.py --engine build/bin/x64/Debug/MorphicEngine.exe --output-directory build/input-physical-24nov --source native --seconds 120
python tests/input_host/run_tests.py --engine build/bin/x64/Debug/MorphicEngine.exe --output-directory build/input-physical-24nov --source ordinary --seconds 120
```

Use the installed Python 3.14 executable if `python` resolves to a Windows alias.
Focus the window; check keyboard A, controller A, sticks and triggers. Hold A or
a stick across Alt+Tab and `C` context changes, then release/centre and try fresh
input. `U` unassigns; `R` requests assignment. Let each window finish automatically
and record observed behaviour separately from automated cleanup. The operator
need not repeat the confirmed controls during diagnosis of the cleanup failure.

## Debris audit — 24 November

The post-completion audit reconciles the documentation index, completion milestone,
Host backlog and current subsystem references with the delivered preamble. The
implementation proposal is retained as a completed decision record; its original
audit and migration sequence are explicitly historical. Dated validation evidence
and genuinely deferred registration, topology, asset-abandonment and work-placement
choices are preserved. The current lifecycle reference now describes the execution
export, six fixture roles and disposal settlement before endpoint retirement,
without incorrectly requiring settlement before native join.

The code audit found no surviving old module pump/thread implementation, direct
client adapter or public lifecycle compatibility path. Its bounded cleanup removes
only the permanently zero `SClientRecord::deferred_records`, the duplicate
`SRequestedThread::result_sent` flag (keeping `creation_sent`), the unused worker
`EEndpointPost::closed` status and an uncalled direct-test `pins()` accessor.
Current retained-head, publication-frontier, invocation, native return/join,
provider, notification and batch proofs remain. Worker transports, current execution
classes and historical test artifacts are still required. Other unrelated redundant
state and convenience helpers are outside this migration-debris change.

The seven-file delta is preserved in `build/debris-audit/source-cleanup.patch`,
with before/after source fingerprints. Coordinator source review accepted the
removal and the completed affected-consumer reconciliation on 24 November.
`build/debris-audit/reconciliation.json` indexes the commands, build/test logs and
separate binary manifests; `scope.json` records the consumer selection. All 18
affected Windows targets built in each of six configurations, and all 12 affected
CMake targets built under GCC Debug and Clang Release. CMake has no lifecycle
fixture role targets. Windows build summaries report zero warnings/errors; Linux
builds contain no compiler warning/error diagnostics.

| Configuration | Focused assertions passed | Failed |
| --- | ---: | ---: |
| Windows x64 Debug | 6,230 | 0 |
| Windows x64 Development | 6,230 | 0 |
| Windows x64 Release | 6,230 | 0 |
| Windows Win32 Debug | 6,229 | 0 |
| Windows Win32 Development | 6,229 | 0 |
| Windows Win32 Release | 6,229 | 0 |
| Linux GCC Debug | 6,230 | 0 |
| Linux Clang Release | 6,230 | 0 |

Each configuration ran HostMaintenance, OrderedExecution, ExecutionFailure,
DestroyRefusal, CommonPod, CommonNativeJoin and SchemaProcess: 49,837 assertions
passed in total, with zero failures. SchemaProcess includes assertions within a
budget-boundary search; its observed count is 1,220 on Win32 and 1,221 elsewhere.
The coordinator verified all 56 test summaries, the seven source fingerprints and
132 rebuilt binary hashes. All five preserved physical-run binaries still match
their original hashes, recorded in `preserved-physical-copies.json`.

This cleanup's evidence is separate from earlier broad and physical qualification.
No broad ordinary/process matrix or physical controls were repeated. Existing
failed/unrun qualifications remain unchanged, including the Win32 Debug native
report/Input prerequisite. No schema/data regeneration or historical artifact
deletion was required. The debris audit is complete; no commit or push is
authorised.

Documentation checks cover 20 edited references: local file links, UTF-8/LF/final
newlines and whitespace. Their inventory is
`build/debris-audit/documentation-files.txt`; this ledger also passes those checks,
and tracked line endings pass.

## Final manual review and commit preparation — 24 November

The user completed manual review and authorised committing the consolidation,
source layout and scoped follow-ups. Current completion headers reflect that
approval; dated checkpoints and failed/unrun qualifications remain unchanged.

The review moved execution-class and timing-helper function bodies out of class
definitions, distinguished module-invocation guards from native-callback guards,
and removed the obsolete Executive/Rendering thread-function identities. The
unsupported-function lifecycle test now uses a current message identity that is
not a callable function. `UnrecognisedMsg` remains a registered SYSTEM type.

The catalogue cleanup rebuilt all 18 Windows x64 Debug consumers and all 12 Linux
GCC Debug consumers together. Both platforms passed ErasedOwner and ModulePreamble;
Windows also passed its ordinary core suite (including SystemTypeIdentity 159/0)
and the real-DLL ordinary lifecycle case containing the unsupported-function
request. Commands, source snapshots and binary hashes are recorded separately in
`build/function-identity-cleanup`. These are current Debug qualifications, not a
repeat or extension of the earlier complete configuration and physical matrices.

The subsequent timing-header layout change rebuilt Executive and Tests in x64
Debug with zero warnings/errors. ModulePreamble passed 38,887 checks and ErasedOwner
1,217, both with zero failures. All eleven function bodies and constructor
initializers retain their previous logic; declarations retain defaults and
attributes, with explicit inline definitions outside the classes. Evidence is in
`build/execution-timing-layout`. Earlier class-layout and guard-renaming evidence
is in `build/execution-class-layout` and `build/callback-guard-names`.
