Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   building_and_testing.md
Author: Ritchie Brannan

# Building and Testing

## Visual Studio layout

Open `MorphicEngine.sln` from the repository root. The engine's project files,
shared items, filters, and policy build target live in `visual_studio/`.
External projects remain under `external/`. Build outputs remain under
`build/`, and the Host's Visual Studio debugger working directory is the
repository root.

Foundation uses the `foundation/` directory and `MorphicFoundation`
shared items. It contains reusable mechanisms and abstract identities.
`MorphicFramework` supplies configured catalogues, shared contracts and common
participation implementation. Each executable and DLL imports both shared-item
files and compiles their sources once, preserving binary-local registry and
reporting state. Host-private repository, filesystem image, native module and
thread ownership live under `host/`; only Host and the tests compile those subjects.
The policy validator enforces the Foundation and Framework include boundaries.

After changing project items or Solution Explorer groups, run
`python -B tools/check_vs_filters.py` from the repository root. It checks existing
project/filter pairs in `visual_studio/` for missing C++ mappings, stale or
duplicate items, and undeclared filter names without changing XML. Intentional
virtual groups, root resource entries and projects without filters are allowed.
Pass project or shared-item paths to check specific pairs. Its focused tests run
with `python -B -m unittest discover -s tools/tests -p test_vs_filters.py`.

### Module source organisation

Module binding and local catalogues remain under each module's `module/binding`
and `module/types` directories. Audio, Rendering and Executive expose their
execution descriptors from `runtime/*_execution.cpp/.hpp`; these implement module
execution contexts rather than native thread entry points. Platform retains its
small execution adapter beside its binding export and its domain runtime in
`platform/runtime`.

Host source is grouped by responsibility:

| Directory | Responsibility |
| --- | --- |
| `host/runtime` | Host orchestration, endpoint/ingress integration, cadence, restrictions, maintenance and shared Host protocol definitions. |
| `host/runtime/execution` | Common preamble, client records, channels, direct connections, retained replies and pressure handling. |
| `host/runtime/services` | Routed service operations, provider records and resource borrows. |
| `host/runtime/workers` | Host worker execution, batch runners and worker sizing. |
| `host/assets` | Asset storage/repository and asset-service orchestration, including schema and report operations. |
| `host/module` | Binding ownership and module load/unload/replacement service. |
| `host/threading` | Native thread-package lifetime and launch/wait support. |

`host_endpoints.cpp` and `host_ingress.cpp` implement parts of `CHost` alongside
`host.cpp`. `module_preamble_communication.cpp` and `module_preamble_lifecycle.cpp`
implement parts of `CModulePreamble` alongside `module_preamble.cpp`. These are
source divisions within their existing classes, not new runtime components.

Visual Studio keeps the existing component `include`/`src` grouping and mirrors
the relative source directories beneath it. Test fixture files use their matching
`Tests` subdirectories; Input fixture projects retain their intentional
`acceptance` groups. Lifecycle and Pump fixture projects have explicit filters.
The single-file Schema fixture remains at the project root. The filter checker
verifies correspondence and declared groups; it does not enforce semantic placement.

When moving a file, update its includes, project/filter entries, exact policy
classifications and current documentation links together. Linux CMake extracts its
source lists from the Visual Studio projects, so check that extraction and the
InputPlatform source/filter parity as well. Preserve dated validation paths as
historical evidence. Directory moves alone do not change ABI or stored-data versions.

### Execution test organisation

`ModulePreamble_test_cases.cpp` covers the common Host execution machinery and
real module integration. `LifecycleExecution_test_cases.cpp`,
`PumpExecution_test_cases.cpp` and `InputExecution_test_cases.cpp` hold their
respective acceptance-fixture regressions. The ordinary HostPump suite calls
these groups; existing focused selectors remain available. Common POD connection
cancellation runs once through the CommonPod group.

Shared support is limited to controlled Host setup/allocation observations in
`tests/support/module_preamble_fixture.hpp` and bounded submission capture in
`tests/support/execution_facade_fixture.hpp`. Domain-specific validation and
custody state remain with each fixture.

Six additional fixture groups run separately from the ordinary suite in Windows
CI and CTest. Build MorphicTests, MorphicPumpProvider, MorphicPlatform and
MorphicInputPlatform in the same configuration, then run locally:

```powershell
$cases = @('provider-execution', 'provider-refusals', 'provider-retirement',
    'provider-notification', 'platform-execution', 'pump-executive')
foreach ($case in $cases) {
    & .\build\bin\x64\Debug\MorphicTests.exe -t1 "--$case" "--log-tag=fixture-$case"
    if ($LASTEXITCODE -ne 0) { throw "Fixture group failed: $case" }
}
```

CMake builds the required modules with MorphicTests and registers the same
selectors with the `execution-fixtures` label:

```sh
ctest --test-dir <cmake-build-directory> -L execution-fixtures --output-on-failure
```

Each group has its own process. Within a Provider group, fresh execution contexts
share one installed DLL image; every scenario must retire its Host graph and
empty its allocation attribution before the next begins. This avoids assuming
that ELF `dlclose` resets an image retained by the loader. These are fixture and
lifetime regressions, not same-file unload/rebootstrap or physical Input checks.
The InputPlatform portion of `platform-execution` is Windows-only; its production
Platform lifetime check also runs on Linux.

## Shader compiler dependency

Before building, run `python tools/acquire_dxc.py` on Windows or
`python3 tools/acquire_dxc.py` on Linux. This acquires the official DXC
`v1.9.2609` package, checks its pinned SHA-256 and extracts matching headers,
libraries and licence files under `build/dependencies/dxc`. No system-installed
DXC or Vulkan SDK compiler is selected implicitly. CI performs the same step.

Visual Studio selects the x86 or x64 compiler DLL for the target. CMake uses the
Linux x86-64 shared library. Builds copy the compiler beside the executables and
retain package licence texts in `dxc-licences`. Deployment of shader compilation
requires those files; compilation does not require a GPU or graphics device.

The DXC adapter uses the engine's exception-free settings and checked engine
containers for temporary storage. Compiler interfaces return HRESULTs; adapter
allocation failures return explicit engine statuses. The private SPIRV-Reflect
adapter uses a per-operation `CHybridAllocator32` with chained buddy managers
and tracked allocations, with no exception compiler allowance. Initial manager
preparation failure is recoverable; allocation failure during vendor execution
terminates with a best-effort diagnostic. Valid frees reclaim individual
allocations; cleanup auditing and an independent sweep precede result publication.
An invalid free or outstanding allocation prevents schema publication. See the
[reflection checkpoint](../modules/shader_reflection_design.md#initial-adapter-checkpoint).
Ordinary suites include shader compilation and transport cases, so the compiler
must be deployed when running them. See the
[shader design](../modules/shader_compilation_design.md) for request/result use,
defaults and implemented reflection/deferred library work.

## Windows controller dependency

Platform and test builds restore pinned `Microsoft.GameInput` 4.0.306 using
`tools/acquire_gameinput.ps1`. The script verifies the package SHA-256 before
extracting to ignored `build/dependencies/gameinput`; first restore needs network
access. Both Debug/Release and x64/Win32 builds use matching vendor libraries.
The shared build target stages `GameInputRedist.dll`, `GameInputRawInputProxy.exe`
and `gameinput-licences` beside the binaries. Deploy all of these together.
No system installer is run. Microsoft's linked loader may select a compatible
installed runtime; the native smoke establishes runtime availability, not which
deployment location supplied it. Linux builds use the explicit unavailable native
adapter and continue exercising portable controller tests without this package.

`MorphicTests --platform-controller-native` checks runtime startup, discovery and
four public pipe lifecycles; ready with zero devices is valid.
`--platform-controller-interactive` runs the 60-second physical harness described
in the [controller contract](../modules/controller_input.md#running-the-harness).

## Test executable

`MorphicTests` is the standalone engine test executable. It is built by default
with `MorphicEngine.sln`; running `MorphicEngine.exe` does not run it. Invoke
`MorphicTests.exe -t1` for the ordinary suites, `-t2` for the moderate harness
work (the default), or `-t3` for the full expensive modes. Test data and log
paths are resolved from the repository, so the executable can be launched from
Visual Studio, the repository root, or its build-output directory.

Every test log name contains the process ID. An optional validated tag can be
supplied for human or automation reconciliation, for example:

```powershell
MorphicTests.exe -t1 --log-tag=parallel-a
```

The runner prints the resulting absolute log-path pattern at startup.
Concurrent invocations therefore remain isolated even when the tag is omitted
or accidentally reused.

Test logs default to `development/logical-roots/test-logs`; non-log test output
defaults to `development/logical-roots/test-output`. Pass
`--output-directory=<path>` to override both locations; logs then use that
directory's `logs` child. Test output filenames retain the process ID and
optional tag.

To clear both default test directories while preserving their root `.gitkeep`
files, run `tools\clean_test_outputs.bat` (PowerShell 7 required). It works from
any current directory and removes nested output directories too. Add `-WhatIf`
to preview the targets. Run it after tests have finished; files still in use can
cause cleanup to fail. Explicit output-directory overrides and isolated harness
directories are not included.

General test output is not inventoried by the development filesystem image.
Host acceptance reads it back through `native:`. Image/cache and bundle-load
checks use a small fixture under `development/logical-roots/dev-workspace/acceptance/`;
its fixed filenames are reused across runs and are outside the cleanup script's
scope. Isolated Host harnesses stage their own copy of this directory.

The shared TGA input fixture is
`development/logical-roots/dev-source/test_input.tga`. The engine's logs use
`development/logical-roots/logs`, and policy-validator reports use its
`policy_validator` child. Run the engine from the repository root with a
[bootstrap configuration file](bootstrap_configuration.md), for example:

```powershell
.\build\bin\x64\Debug\MorphicEngine.exe development/bootstrap/flow-test.cfg
```

The configuration file is the Host's only command-line argument. Its
`log-directory` setting redirects logs; the DLL lifecycle harness selects
`development/logical-roots/test-logs` through generated files.

Set `host-workers=<positive integer>` to request the total number of Host I/O
and conditioning workers; the default is two. One worker handles both roles.
Two workers separate the roles: `bg_file_io` handles all I/O and
`bg_conditioning` handles all conditioning. Requests above the startup limit
are reduced and logged rather than rejected; at most two workers are created.

The limit permits two workers when at least eight logical hardware threads are
reported, and only the single shared worker below eight. The baseline allows for
five main threads (Host, Executive, debug, rendering and audio), accepting
one thread of OS headroom at eight reported hardware threads and two at nine.
Ten or more provide the preferred three threads of headroom. This is a sizing
heuristic without affinity or dedicated processor reservations.

## Audio acceptance and headless Linux

The normal Executive now runs Rendering tests, then loads `MorphicAudio` and
requests its finite tone test, then continues the existing acceptance cases.
This can produce a five-second middle-C tone on the default output. Loading the
Audio module alone does not open a device. Ordinary `MorphicTests -t1` Audio
checks use generated buffers and fake output; they never open a real device.

Linux needs a usable native audio runtime, such as PulseAudio or ALSA. Missing
backend libraries or a failing configured default output are failures. A real
backend reporting no output devices is explicitly unavailable with one skipped
test and permits the remaining acceptance cases to continue.

For silent headless acceptance, install `pulseaudio` and run:

```sh
python3 tests/audio/run_headless.py ctest --test-dir build/linux-ci --output-on-failure
```

The wrapper starts a private PulseAudio server with no sinks or sources, sets
`PULSE_SERVER` only for its child command, then stops that server. It does not
modify an existing user audio server or substitute a null playback device.
Linux CI uses this same path. Native playback uses the regular CTest/Host
harness commands without the wrapper. See the
[Audio implementation and validation record](../modules/audio_module_validation.md)
for the distinction between submitted frames and listening evidence.

## Sandboxed Windows builds

Windows sandboxing can prevent MSBuild's SDK locator and native file tracker
from inspecting per-user directories. It can also expose both `PATH` and
`Path`, which .NET Framework build tasks reject when launching the compiler.
Use the repository helper to select an installed Windows SDK explicitly,
disable file tracking, and normalise the child-process environment:

```powershell
.\tools\invoke_sandbox_build.ps1 -RunTests -TestMode 1
```

The helper defaults to `Debug|x64`, discovers the newest installed Windows SDK
and MSBuild executable by numeric version, and uses the same development test-log
and test-output roots. It reports the selected MSBuild path/version and tool host
architecture. Installation directory names are not version ordering: `2022`
would sort ahead of the newer Visual Studio `18` directory as text.

On 64-bit Windows the helper selects the x64-hosted compiler and linker through
`PreferredToolArchitecture=x64`, including when building x86 targets. This avoids
the sandbox failure observed with x86-hosted PDB tools (`LNK1101: incorrect
MSPDB140.DLL version`) despite matching installed DLL versions. It does not change
the project toolset, target architecture or debug-information settings.

Configuration, platform, SDK version, MSBuild path, test mode, and log tag can all be
overridden through the script parameters. Because sandbox-safe file tracking is
disabled, a build invocation recompiles sources rather than relying on
MSBuild's tracked inputs.

The helper first runs `tools/check_line_endings.ps1`. This verifies the
effective Git attributes, rejects mixed or incorrectly checked-out line
endings, and requires a final newline in every non-empty tracked text file. It
can also be run directly from PowerShell when checking a working tree without
building.

## Acceptance and lifecycle harnesses

The portable Host-pump harness is `tests/host_pump/run_tests.ps1` on Windows and
`tests/host_pump/run_tests.py` through CTest on Linux, with one and two requested
workers. It loads real DLL/SO fixtures and the Platform shell without opening a
window. Ordinary tests cover deterministic borrow, notification, restriction,
maintenance and window-runtime behaviour. See the
[Host foundation](../modules/host_pump_foundation.md) and
[first-window evidence](../modules/platform_first_window.md#implementation-evidence-4-november-2026)
for the delivered coverage and checkpoint-specific limits.

Windows native acceptance is explicit: `tests/platform/run_native.py` exercises
the real adapter in process, while `tests/host_pump/run_tests.py --native` runs
move/size/menu scenarios through the real Host and loaded Platform DLL. These
tests require a desktop session, may move the pointer, and are excluded from
ordinary/headless runs. A harness timeout terminates an isolated process; it is
not proof of safe runtime unwind. Linux's portable shell returns `unsupported`
for native window creation.

The Executive runs 73 sequential and 32 concurrent acceptance operations
across raw, baked-document, JSON, TGA and LDR/HDR texture asset services, followed
by filesystem checks and a bundle bake/reference/save/close/reload flow. The standalone DLL
lifecycle harness, `tests/module_lifecycle/run_tests.ps1`, builds real module
fixtures and checks bootstrap, replacement, shutdown, asset disposal and bundle
lifetimes, including queued and inline batch borrowers. See
the [asset](../assets/asynchronous_asset_services.md) and
[module](../modules/asynchronous_module_lifecycle.md) references for details.

Requested batch-runner counts remain subject to Host's hardware budget.
`bundle-batch-stop` explicitly skips its queued-cancellation workload when the
effective count is zero, while still checking clean Executive teardown. With a
runner available it requires the active borrow to settle before join/retirement.
The existing `batch-exit` case likewise reports an unavailable-runner skip;
ordinary bundle and batch cases continue exercising their inline fallback.

After building, `tests/host_workers/run_tests.ps1` checks invalid bootstrap settings,
default/shared/dedicated-worker acceptance, reduction diagnostics and worker
lifetime. Both scripts accept `-Configuration` and `-Platform`; the lifecycle
harness also accepts `-WorkerCount` to exercise transitions with a shared worker
or a dedicated conditioning worker. Both harnesses stage an isolated development
filesystem under `build/`, retaining its logs and output for inspection without
inventorying accumulated output from other runs.

Intentional rejection scenarios still produce error-level diagnostics. The Host
acceptance harnesses share `tests/support/host_expected_diagnostics.json` and
require each expected error exactly once, with its source, text and scenario
interval checked across the event and direct logs by incident ID. Unexpected,
duplicate or missing errors fail acceptance. The lifecycle harness separately
checks its deliberate bundle failures, service failures and assertion counts.
Required acceptance markers and codec entry breadcrumbs use the enabled `info`
level in Debug and Release. Per-texture progress uses `detail`; the Windows Debug
harness checks those records, while both platforms verify execution routing from
the codec breadcrumbs' producer identity in every configuration. Lifecycle load
failures are matched as specific errors, separately from contract assertions.
The debug-service suite verifies filtering and explicit immediate delivery.

Windows CI builds Debug/Release on x64 and x86. Lifecycle, Pump and Input acceptance
normally run with one and two requested workers. x86 Debug uses one complete
Pump/Input pass to reduce duplicated coverage. The Host-worker harness and focused execution
fixture regressions remain enabled in every configuration. All Input cases,
including the unresolved `work-pressure` and `load-smoke` checks, remain enabled.

Temporarily, while diagnosing the November 24 Debug Input failures, x86 Debug
skips the ordinary suites and lifecycle fixture compilation/tests that passed at
`5d7750a9`; its solution and Pump/Input fixtures still build. Restore the ordinary
suites and lifecycle fixture build/tests after diagnosis. Other configurations continue
running the ordinary suites and both acceptance worker counts.
Its diagnostics include the isolated Host-worker and lifecycle logs and output.
[Linux CI](linux_ci.md) runs GCC/Clang Debug/Release builds, ordinary suites and
the normal Host acceptance flow with one and two requested workers through CTest.

The [messaging closeout validation record](linux_ci.md#messaging-consolidation-closeout-20-october-2026)
distinguishes local validation of the messaging commits from the latest hosted
Windows/Linux runs. Hosted success applies to the run's checked-out revision;
local commits require a push before those workflows can validate them.

The separate direct TGA round-trip helper is compiled into `MorphicTests` as a
manual test but is intentionally not registered or invoked.

Reflection allocator fatal failures are tested only in disposable child processes:

```powershell
python tools/tests/test_reflection_arena_fatal.py --binary build/bin/x64/Debug/MorphicTests.exe --output-directory build/reflection-arena-fatal
```

On Linux, `ctest --test-dir build/shader-linux -R MorphicReflectionArenaFatal
--output-on-failure` runs the same harness. It injects failure when adding a buddy
manager or allocating the tracked index/payload. It also checks oversized
requests and `calloc` size overflow,
requiring the expected fatal exit and matching panic fields. Every invocation
retains logs and captured output in
a fresh UUID directory, so previous logs cannot satisfy a new run. Do not invoke
the `--reflection-arena-fatal` test executable switch in the ordinary runner.
The ordinary suites cover recoverable initial preparation failure, alignment and
zeroing, mixed buddy/tracked storage, chain growth, valid reclamation,
interior/foreign/double frees, cleanup omissions, audit failure preventing schema
publication, captured-context isolation, nested/concurrent routing and fallback
reclamation. Reflection cases also cover sparse IDs, nested/shared types, source
replacement and continuations, generated large shaders, oversized preflight
storage and invalid root references. Structural arena sizing, bump reservation
and aggregate estimated-working-memory admission are no longer production
contracts; reflection retains explicit artifact/count/depth/result and individual
allocation limits without an aggregate reflection memory cap.
