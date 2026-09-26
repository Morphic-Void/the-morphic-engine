Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Linux canary

`.github/workflows/linux-canary.yml` runs experimental x64 Debug builds with
GCC and Clang on Ubuntu 24.04. Pushes, pull requests, and manual dispatch
trigger it independently of Windows CI. Each job configures CMake, builds the
engine, modules, policy checker, and tests, then runs the ordinary `-t1` suites
through CTest if the build succeeds.

The jobs use `continue-on-error: true`: Linux failures are diagnostic and do
not fail the overall workflow. A green workflow alone therefore does not mean
Linux passed. Read each compiler's stage results in the run summary and its
job logs. Failed stages produce a warning annotation, and later build/test
stages are skipped when their prerequisites fail. Do not make these
experimental jobs required branch checks.

Each job attempts to upload seven-day diagnostics artifacts even after a
failure: tool versions, source-extraction test results, configure/build/test
logs, generated source manifests, compile commands, CMake configuration logs,
CTest logs, test output, and policy reports. Checkout/setup failures may occur
before these files exist.

## Build definitions

The Visual Studio solution remains the Windows build entry point. The new
root `CMakeLists.txt` is a Linux-only, single-configuration Debug build for
GCC and Clang. It does not replace or rewrite Visual Studio files.

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
The policy checker runs before engine target compilation and policy errors
stop the build.

The module filenames retain `MorphicExecutive.dll` and `MorphicRendering.dll`
because current runtime paths and the ordinary module-loading test use those
names. Their contents are Linux ELF shared libraries loaded with `dlopen`,
not Windows binaries. Adopting conventional `.so` runtime paths is separate
portability work. The engine acceptance run and separate Windows DLL lifecycle
harness are not included in this canary.

## Running locally on Linux or WSL

Install Git, Python 3.10 or newer, CMake 3.22 or newer, Ninja, GCC, and Clang.
On Ubuntu, the build tools can be installed with:

```sh
sudo apt-get update
sudo apt-get install build-essential clang cmake ninja-build python3
```

From a checkout with the SuiteUTF submodule initialized:

```sh
python3 -B -m unittest discover -s tools/tests -p test_cmake_sources.py -v
cmake -S . -B build/linux-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
cmake --build build/linux-gcc --parallel 2
ctest --test-dir build/linux-gcc --output-on-failure --no-tests=error
```

For Clang, use a separate `build/linux-clang` directory and
`-DCMAKE_CXX_COMPILER=clang++`. Run each following command only if the preceding
command succeeded. The builds use the current working-tree files, including
uncommitted changes. GitHub uses the revision checked out for the workflow.

Build products stay in the chosen build directory. Policy reports are written
under `development/logical-roots/logs/policy_validator`; ordinary test logs and
outputs use the build directory's `test-output` child. The module test finds
the executive library next to the `MorphicTests` executable in `bin`.

## Initial local validation

On Ubuntu 24.04 in WSL, GCC 13.3 and Clang 18.1 both configured the committed
source snapshot successfully. Source-extraction tests passed, and the policy
checker built and ran with both compilers with zero errors or warnings.

Both full builds stopped in the pinned SuiteUTF dependency
(`5ddce3a2d8f1fc4785735deeab8905b9033c1d44`). Its headers and implementations
place `[[nodiscard]]` after `inline` or `virtual`, which both Linux compilers
reject; the first reported example is `include/utf_std.h:141`. Consequently,
engine compilation and ordinary test execution have not yet been validated
on Linux. The canary records this failure until the dependency is corrected
and its pinned revision updated. No dependency or engine source was changed
as part of setting up the canary.

A subsequent syntax-only GCC check also confirmed the same attribute-placement
error in the engine's `core/types/fp16data_t.hpp:62` (`constexpr [[nodiscard]]`).
Similar declarations occur in container headers. Fixing SuiteUTF will therefore
remove the first dependency blocker, but is not sufficient to establish a
passing Linux engine build. These engine changes remain separate work.
