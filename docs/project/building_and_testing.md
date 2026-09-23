Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   building_and_testing.md
Author: Ritchie Brannan

# Building and Testing

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

The shared TGA input fixture is
`development/logical-roots/dev-source/test_input.tga`. The engine's logs use
`development/logical-roots/logs`, and policy-validator reports use its
`policy_validator` child. Run the engine from the repository root as before.
Its `--log-directory=<existing directory>` option redirects logs; the DLL
lifecycle harness selects `development/logical-roots/test-logs` this way.

## Sandboxed Windows builds

Windows sandboxing can prevent MSBuild's SDK locator and native file tracker
from inspecting per-user directories. It can also expose both `PATH` and
`Path`, which .NET Framework build tasks reject when launching the compiler.
Use the repository helper to select an installed Windows SDK explicitly,
disable file tracking, and normalise the child-process environment:

```powershell
.\tools\invoke_sandbox_build.ps1 -RunTests -TestMode 1
```

The helper defaults to `Debug|x64`, discovers the newest installed Windows SDK,
and uses the same development test-log and test-output roots. Configuration,
platform, SDK version, MSBuild path, test mode, and log tag can all be
overridden through the script parameters. Because sandbox-safe file tracking is
disabled, a build invocation recompiles sources rather than relying on
MSBuild's tracked inputs.

The helper first runs `tools/check_line_endings.ps1`. This verifies the
effective Git attributes, rejects mixed or incorrectly checked-out line
endings, and requires a final newline in every non-empty tracked text file. It
can also be run directly from PowerShell when checking a working tree without
building.

## Acceptance and lifecycle harnesses

The Executive runs 48 sequential and 32 concurrent acceptance operations
across raw, baked-document, JSON, and TGA asset services. The standalone DLL
lifecycle harness, `tests/module_lifecycle/run_tests.ps1`, builds real module
fixtures and checks bootstrap, replacement, shutdown, and asset disposal. See
the [asset](../assets/asynchronous_asset_services.md) and
[module](../modules/asynchronous_module_lifecycle.md) references for details.

The separate direct TGA round-trip helper is compiled into `MorphicTests` as a
manual test but is intentionally not registered or invoked.
