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

The DXC adapter enables exceptions (`/EHsc` or `-fexceptions`). Its compiler
objects and temporary allocations remain local to that boundary. The private
SPIRV-Reflect adapter uses a growing per-operation `CBumpBuffer` and has no exception
compiler allowance. Initial arena allocation failure is recoverable; a failed bump
during vendor execution terminates with a best-effort diagnostic. See the
[reflection checkpoint](../modules/shader_reflection_design.md#initial-adapter-checkpoint).
Ordinary suites include shader compilation and transport cases, so the compiler
must be deployed when running them. See the
[shader design](../modules/shader_compilation_design.md) for request/result use,
defaults and deferred reflection work.

## Test executable

`MorphicTests` is the standalone Core test executable. It is built by default
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
five main threads (Host, Executive, debug, rendering and planned audio), accepting
one thread of OS headroom at eight reported hardware threads and two at nine.
Ten or more provide the preferred three threads of headroom. This is a sizing
heuristic without affinity or dedicated processor reservations.

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

The Executive runs 73 sequential and 32 concurrent acceptance operations
across raw, baked-document, JSON, TGA and LDR/HDR texture asset services, followed
by filesystem checks and a bundle bake/reference/save/close/reload flow. The standalone DLL
lifecycle harness, `tests/module_lifecycle/run_tests.ps1`, builds real module
fixtures and checks bootstrap, replacement, shutdown, asset disposal and bundle
lifetimes, including queued and inline batch borrowers. See
the [asset](../assets/asynchronous_asset_services.md) and
[module](../modules/asynchronous_module_lifecycle.md) references for details.

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

Windows CI runs the ordinary suites, Host-worker harness and all 33 lifecycle
cases with one and two requested workers in Debug/Release on x64 and x86.
Its diagnostics include the isolated Host-worker and lifecycle logs and output.
[Linux CI](linux_ci.md) runs GCC/Clang Debug/Release builds, ordinary suites and
the normal Host acceptance flow with one and two requested workers through CTest.

The [messaging closeout validation record](linux_ci.md#messaging-consolidation-closeout-20-october-2026)
distinguishes local validation of the messaging commits from the latest hosted
Windows/Linux runs. Hosted success applies to the run's checked-out revision;
local commits require a push before those workflows can validate them.

The separate direct TGA round-trip helper is compiled into `MorphicTests` as a
manual test but is intentionally not registered or invoked.

Reflection arena fatal allocation failures are tested only in disposable child processes:

```powershell
python tools/tests/test_reflection_arena_fatal.py --binary build/bin/x64/Debug/MorphicTests.exe --output-directory build/reflection-arena-fatal
```

On Linux, `ctest --test-dir build/shader-linux -R MorphicReflectionArenaFatal
--output-on-failure` runs the same harness. It injects failure when growing the
pointer directory or allocating another block, including an alignment-induced
block transition. It also checks oversized bumps and `calloc` size overflow,
requiring the expected fatal exit and matching panic fields. Every invocation
retains logs and captured output in
a fresh UUID directory, so previous logs cannot satisfy a new run. Do not invoke
the `--reflection-arena-fatal` test executable switch in the ordinary runner.
The ordinary suite covers recoverable initial allocation failure, no-op frees,
whole-arena reclamation, nested/concurrent routing and usage measurements.
It also covers structural sizing, power-of-two block reservation and full usable
capacity, successful growth with stable pointers and zeroing, reclamation of all
blocks, real vendor reflection with an underestimated initial block, bootstrap and
final working-memory rejection before arena allocation, 32-bit counter limits,
source-copy accumulation, sparse IDs and repeated nested type expansion.
