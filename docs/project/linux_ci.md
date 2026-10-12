Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Linux CI

`.github/workflows/linux-ci.yml` runs four x64 builds: Debug
and Release with GCC and Clang on Ubuntu 24.04. Pushes, pull requests, and manual
dispatch trigger it independently of Windows CI. Each job configures CMake, builds the
engine, modules, policy checker, and tests, then runs the ordinary `-t1` suites
and the real Host asset, texture, filesystem and bundle acceptance flow through
CTest if the build succeeds. Host acceptance requests one and two workers in
separate runs and checks the effective count against the hardware limit.

All four jobs must pass for the workflow to succeed. Build, policy, and test
failures fail the job and workflow. Read the stage results for each compiler and
configuration in the run summary and job logs. Later build/test stages are
skipped when their prerequisites fail; the other matrix jobs continue to run.
Requiring these checks before merging is controlled separately by the repository's
branch protection or ruleset settings.

Each job attempts to upload seven-day diagnostics artifacts even after a
failure: tool versions, source-extraction test results, configure/build/test
logs, generated source manifests, compile commands, CMake configuration logs,
CTest logs, test output, Host acceptance diagnostics, and policy reports. Checkout/setup failures may occur
before these files exist.

## Build definitions

The Visual Studio solution remains the Windows build entry point. The
root `CMakeLists.txt` supports Linux-only, single-configuration Debug and Release
builds for GCC and Clang. It does not replace or rewrite Visual Studio files.

At configuration time, `tools/cmake_sources.py` reads each target's `.vcxproj`
and follows shared `.vcxitems` imports. It writes source manifests into the
build directory. CMake watches the input project files and extractor so edits
cause reconfiguration on the next build. No generated manifests are committed.

The extractor handles unconditional source entries, relative paths, and the
`MSBuildThisFileDirectory` and `ProjectDir` path prefixes used by this repository.
It rejects conditional source selection, per-file compile metadata, wildcards,
unknown path variables, unrecognised custom imports, missing files, paths
outside the repository, and shared-import cycles. It does not evaluate arbitrary
MSBuild logic or translate compiler settings, include paths, target types, or
project dependencies; those are explicitly defined in CMake and must be reviewed
when their Visual Studio equivalents change.

Core sources are compiled into each executable/module, matching the existing
Visual Studio layout. Linux symbols default to hidden visibility, with module
entry points exported by the existing platform macros. Engine targets and
SuiteUTF disable exceptions; the policy checker retains exception support.
The rendering target also compiles Basis Universal and its Zstd C source.
Its private Basis include path and SSE/OpenCL-disabled, KTX2-Zstd-enabled
definitions match the Windows build. Exception flags apply only to C++;
both C and C++ symbols retain hidden visibility inside the rendering module.
The policy checker runs before engine target compilation and policy errors
stop the build.

Debug and Release use the same engine build definitions as their Visual Studio
counterparts. Debug enables development checks and information level 3;
Release disables development checks and uses information level 1. CMake's
standard Release flags enable optimization and define `NDEBUG` for all targets.

The module filenames retain `MorphicExecutive.dll` and `MorphicRendering.dll`
because current runtime paths and the ordinary module-loading test use those
names. Their contents are Linux ELF shared libraries loaded with `dlopen`,
not Windows binaries. Adopting conventional `.so` runtime paths is separate
portability work. CTest runs `tests/texture/run_tests.py` against the engine
with a fresh filesystem for each worker count, exercising LDR/HDR conditioning,
bundle bake/reference/save/close/reload and clean rendering shutdown. Ordinary
suites include bundle format, construction, lifetime and real-worker request
tests. The separate Windows DLL lifecycle harness is not included.

## Running locally on Linux or WSL

Install Git, Python 3.10 or newer, CMake 3.22 or newer, Ninja, GCC, and Clang.
On Ubuntu, the build tools can be installed with:

```sh
sudo apt-get update
sudo apt-get install build-essential clang cmake ninja-build python3
```

From a checkout with the SuiteUTF and Basis Universal submodules initialized:

```sh
python3 -B -m unittest discover -s tools/tests -p test_cmake_sources.py -v
cmake -S . -B build/linux-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build/linux-gcc --parallel 2
ctest --test-dir build/linux-gcc --output-on-failure --no-tests=error
```

For Clang, use a separate `build/linux-clang` directory and
`-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`. For Release, choose a separate build directory
(for example, `build/linux-gcc-release`) and use `-DCMAKE_BUILD_TYPE=Release`.
Run each following command only if the preceding command succeeded. The builds
use the current working-tree files, including
uncommitted changes. GitHub uses the revision checked out for the workflow.

Build products stay in the chosen build directory. Policy reports are written
under `development/logical-roots/logs/policy_validator`; ordinary test logs and
outputs use the build directory's `test-output` child. The module test finds
the executive library next to the `MorphicTests` executable in `bin`.
Each Host acceptance run stages a fresh directory under `host-acceptance`,
retaining the console output, engine logs, bootstrap file and generated assets.

## Local validation

### Bundle integration follow-up, 12 October 2026

All four Linux configurations pass the build, policy checks, ordinary suites and
Host acceptance with one and two requested workers on Ubuntu 24.04 in WSL:

| Compiler | Configuration | Evidence directory under `build/` |
| --- | --- | --- |
| GCC 13.3 | Debug | `linux-texture` |
| GCC 13.3 | Release | `linux-identity-release` |
| Clang 18.1 | Debug | `linux-bundle-clang-debug` |
| Clang 18.1 | Release | `linux-texture-clang-release` |

Each directory contains `configure-bundle.log`, `build-bundle.log` and
`tests-bundle.log`. GCC Debug and Clang Release initially exposed a test assertion
issue; their successful ordinary-suite reruns are in `tests-bundle-ordinary.log`.
The Host runs had already passed and were not repeated. Each ordinary run includes
226 BundleRequests, 344 BundleService, 1997 Bundle and 2080 BundleBuilder checks.

Two test portability issues were corrected: empty-file creation now uses the
platform file helper instead of `fopen_s`, and rejected-insertion lease counts
are checked after the calling full expression has finished destroying parameters.
No production changes were required. Clang still reports the existing
`schema_declarations.cpp` comparison warning; it does not fail the build.

The five source-extraction tests, workflow YAML/structure and shell syntax checks,
line-ending checks and diff checks pass. Windows Debug x64 also passes the build
and ordinary suites with both test corrections (`bundle-ci-portability-final`,
process 82396). The newly added Windows CI Host-worker step passes Debug Win32
(`host-workers-8c669119`), Release x64 (`host-workers-e6b202b0`) and Release Win32
(`host-workers-566acfb6`); the existing Stage 6 Debug x64 result remains valid
(`host-workers-6324c4aa`). Windows CI retains all 32 lifecycle cases for each
requested worker count. These are local results; hosted CI requires a subsequent
commit and push.

### Texture integration follow-up, 10 October 2026

GCC 13.3 Debug and Clang 18.1 Release on Ubuntu 24.04 in WSL passed the
updated build, ordinary suites and Host texture acceptance. Each Host run
completed 71 sequential asset cases (including 23 texture cases), 32 concurrent
saves and 12 filesystem cases, with clean rendering shutdown. The ordinary
`TextureService` suite passed all 39 admission/disposal checks. Source-extraction
tests and policy checks also passed. These are local results; the updated
four-job hosted matrix has not yet run.

### Initial portability validation

With SuiteUTF revision `fc51720c5ec3f1c0fd2face660f5fc6919ae1288`, the engine,
modules, policy checker, and ordinary tests pass the following local checks:

| Platform | Compiler | Configuration | Build, policy checks, and `-t1` |
| --- | --- | --- | --- |
| Ubuntu 24.04 in WSL, x64 | GCC 13.3 | Debug | Passed |
| Ubuntu 24.04 in WSL, x64 | GCC 13.3 | Release | Passed |
| Ubuntu 24.04 in WSL, x64 | Clang 18.1 | Debug | Passed |
| Ubuntu 24.04 in WSL, x64 | Clang 18.1 | Release | Passed |
| Windows, x64 | MSVC v143 | Debug | Passed |

Validation used a snapshot of committed engine source plus the portability
fixes and the updated SuiteUTF revision, excluding unrelated in-progress schema
work. The source-extraction tests and workflow syntax checks also passed during
CI setup. Hosted Linux runner results still need to be confirmed after push.

Separate local engine acceptance runs passed for both Debug compilers and GCC
Release. Clang Release completed the 48 sequential and 32 concurrent asset
operations, then exited with code 1 during the filesystem image exercise on
one run; three subsequent runs passed and exited cleanly. The cause of this
intermittent failure was unresolved at that stage. Those historical engine
acceptance runs were separate from the original CI ordinary test suites;
the workflow now includes the isolated Host acceptance harness described above.

The initial build exposed non-portable attribute placement in SuiteUTF and
Core declarations. SuiteUTF's update addresses its compatibility issues; the
engine fixes move `[[nodiscard]]` before declaration specifiers, explicitly
include `<cstring>`, correct two Windows platform guards, use standard C++17
aligned allocation in the queue/ring tests, and make bounded TGA header byte
conversions explicit. The existing ordinary suites, including transport and
image tests, were run without changing the test runner or its registered suites.
