# Platform report implementation and validation

The version-1 Platform report checkpoint is based on `fc3c95737d30d2abebedb712d50a0056e2f37854`. The focused review identified F1 in independent Host admission. **Placement and the Win32 Debug native deadline remain open.** The single-RGBA8 profile, 52-byte shared mode values and generic Host schema composition/baking on conditioning are agreed follow-up work. Prerequisite acceptance and Rendering 1A remain gated. [Coordinator review](platform_report_review.md) · [Staged design](pso_creation_stage1.md)

**Follow-up handoff paused, 19 November 2026.** Independent Host communications
review/refactoring is being arranged in other chats. The user will return to
re-evaluate integration afterward; the evidence below remains historical checkpoint
evidence and does not accept the proposed revised handoff. See the
[pause and resumption record](pso_creation_stage1.md#platform-report-handoff-paused-for-host-communications-review).

## F1 checkpoint verification

Host now requires a correctly scoped successful observation for present adapter/display labels, known GDI current fields, source annotations (including known false) and native identity evidence. A query with only depth available still supports that field. Failed or blocked enrichments remain admissible with their facts absent. DXGI output description and interface acquisition now have independent query outcomes. A failed interface lookup blocks its mode lists while preserving the earlier successful description and available GDI fallback.

The reviewer's exact Release cold sample still passes. After changing query 21 to failed/native-failure while retaining `known_fields=61`, the corrected validator returns `validation/invalid_data` (reason 2), with zero retained requested bytes. The original probe expected the defect and therefore exits 1 when the correction is present. The verification checked the exit code and all three output lines. The log is `build/platform-report/F1-query-provenance-probe.log`.

Windows x64 Debug and Release solution builds and the Linux GCC Debug Host/test build passed. Running `MorphicTests --platform-report --log-tag=platform-report-f1` on these builds yielded 1,235 passing checks on each Windows build and 221 on Linux. The portable cases cover failed, blocked, successful partial and wrong-subject facts. The Windows SDK replay also passed missing names, partial current fields and output-interface failure through the full bake and independent validator.

The two-worker x64 Release `platform-report` native lifecycle passed cold, refresh, unload/reload and historical byte comparison in `build/platform-report/F1-native/platform-report-2-kw0ngi5u/`. The three 148,912-byte captures each contain 43 query outcomes covering 72 native calls and 1,580 modes. GCC reconstructed all three. These extra three outcome rows distinguish the two operations for each output. The broader placement measurements and 40-outcome captures below are retained evidence from before F1.

## Actual Windows captures

The x64 Host loaded the production Platform DLL after Executive startup. Executive read current, requested a whole update and held both acquisitions across Platform unload and reload. The fixtures compared each historical acquisition with its original complete bytes, then released it through Host. Cold and refresh used Platform instance 2 and capture IDs 1 and 2. Reload used instance 3 and capture ID 3. Discovery ran without a window. The separate live-window case sent mouse input to a controlled window and sampled its message turnaround during update.

Final measurements below are in milliseconds except where indicated. The native interval comes from the report header. The longest Platform pump includes synchronous schema work and transfer preparation. Host settlement uses awake time from admission through incoming-owner destruction. Interrupted native or schema work can occupy a pump until the call returns.

| Build and capture | Native interval | Longest native call | Platform schema | Longest Platform pump | Host copy / adopt / validate | Host incoming retirement | Admission to settlement |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| x64 Release cold | 17.301 | 8.729 | 142.058 | 142.147 | 0.038 / 0.077 / 4.425 | 20.5 µs | 164.115 |
| x64 Release refresh | 8.188 | 0.977 | 140.803 | 140.890 | 0.034 / 0.064 / 4.401 | 31.1 µs | 157.734 |
| x64 Release reload | 15.488 | 6.860 | 144.918 | 145.016 | 0.035 / 0.074 / 4.391 | 25.3 µs | 165.129 |
| x64 Release live refresh | 9.363 | 1.079 | 151.965 | 152.066 | 0.046 / 0.066 / 4.490 | 14.0 µs | 166.281 |
| x64 Debug cold | 21.382 | 9.476 | 2553.384 | 2553.924 | 0.030 / 0.855 / 68.479 | 32.9 µs | 2645.368 |
| x64 Debug refresh | 10.847 | 1.062 | 2543.992 | 2544.491 | 0.044 / 0.779 / 64.557 | 41.7 µs | 2627.050 |
| x64 Debug live refresh | 11.910 | 0.935 | 2532.114 | 2532.618 | 0.039 / 0.793 / 64.826 | 37.3 µs | 2611.209 |

Release refresh without a window spent 2.848 ms preparing declarations, 59.471 ms authoring and adopting logical records, 78.280 ms baking, and 0.205 ms retiring schema staging. Historical Host backings took 17.9–31.2 µs each to retire in the three-capture Release run. Platform recorded 6,996,120 peak staged bytes and Host recorded 529,424 peak import bytes. Each complete block occupied 148,696 bytes.

The live window's maximum sampled response was **151.945 ms** over seven responses in Release and **2531.708 ms** over nine in Debug. Earlier live runs measured 143.616 ms and 2552.801 ms, respectively. The earlier window-independent Release samples that prompted the user decision spent 145.800–151.088 ms in schema work and 4.384–4.522 ms in Host validation. Both builds reproduced the stall after the instrumentation was split by phase.

**Win32 Debug native publication failed qualification on this desktop.** Two real attempts exceeded the five-second eligible deadline on their first capture. The second ran after build activity ended and spent 6.208 s in synchronous schema work, including 2.232 s in logical records and 3.861 s in baking. Host recorded a deadline failure, returned current as `not_ready` and released the producer through its receipt. The success-path runner exited 1 as expected for that observation. The failed runs are preserved in `windows-Win32-Debug/platform-report-1-8e6f53xf` and `windows-Win32-Debug/platform-report-1-9_z436o6` under the evidence root.

The targeted placement decision concerns Platform logical construction and baking and Host semantic validation. The next design review needs to account for these costs, the validated immutable handoff, and producer retirement during shutdown. Qualification should repeat the same three-display live session and the Win32 deadline case. The existing two-millisecond fairness target and five-second eligible deadline still apply.

## Desktop observations and retained evidence

The final cold report recorded 72 native calls, 73 possible CCD paths, three adapter rows, three sources, three displays and three active paths. Its six format-qualified catalogues held 1,580 mode rows. The report carried 25 native identities, 96 identity bytes and 40 query outcomes. Query provenance and the native keys identify what each adapter row represents.

An independent Windows Forms `Screen.AllScreens` check matched all three GDI source names, oriented rectangles and primary flags. The portrait display demonstrates why the separate observations matter.

| Source | GDI / Windows Forms desktop rectangle | CCD source-mode extent | CCD path rotation | GDI current refresh |
| --- | --- | --- | --- | ---: |
| DISPLAY1, primary | (0, 0), 3840 × 1600 | 3840 × 1600 | identity | 75 Hz |
| DISPLAY2 | (3840, −445), 1080 × 2560 | 2560 × 1080 | counterclockwise 90° | 60 Hz |
| DISPLAY3 | (−3840, −294), 3840 × 2160 | 3840 × 2160 | identity | 60 Hz |

WMI reported the NVIDIA GeForce RTX 4080 SUPER at integer 74 Hz and a Meta Virtual Monitor without a rate. The report preserves CCD/DXGI rational observations and GDI integer observations with their own provenance. The independent desktop comparison is reproducible with [check_report_desktop.ps1](../../tests/host_pump/check_report_desktop.ps1).

The local evidence root is `build/platform-report/`. The two lifecycle directories contain `cold.bsd`, `refresh.bsd`, `reload.bsd`, their reconstructed JSON and both historical byte copies. The live directories contain `window-refresh.bsd`, reconstructed JSON and the window event log. They are machine observations kept with the qualification run.

| Run | Directory under the evidence root |
| --- | --- |
| Release native lifecycle | `windows-x64-Release/platform-report-2-o3g0y_o4/` |
| Debug native lifecycle | `windows-x64-Debug/platform-report-2-ti2ng9om/` |
| Release live window | `final-live-release/platform-native-report-2-i0tu_bz0/` |
| Debug live window | `final-live-debug/platform-native-report-2-_fqhd9it/` |

Release cold and historical cold SHA-256 both equal `6f77e35e25b072fd9e5263f751bf6b2e6bb379a478db34324835fd9e589afa4d`. Release refresh and historical refresh both equal `b53006a7191ebf0fa7d26002c8feba498f40e12722a662c952b465803f763c7f`. The hashes survived the machine shutdown. All eight final acquired snapshots were reconstructed with GCC. The Win32 reader reconstructed the final x64 Release cold file byte-for-byte identically to GCC, SHA-256 `ff1752fd0f5e8de4000203317362463cfe88eb3951dafa15ca9df5b56daa1591` for the JSON. Both generators reproduced the maintained layout exactly, SHA-256 `e8d8bf2396a00e2aee7ebbaf3f0d6be2c7fe777e12c073fa7758db50619235c2`.

## Build and regression coverage

| Surface | Exercised result |
| --- | --- |
| Windows x64 Debug | Solution and affected fixture DLLs built. Ordinary `MorphicTests -t1` passed, including report, Host pump, input, window, schema and existing transport suites. Actual two-worker native capture, live-window refresh and historical reads passed. |
| Win32 Debug | Solution and affected fixtures built. Focused report had 1,172 passing checks and schema references had 4,127. One-worker injected Host capture, cancel, shutdown and requester replacement passed. The real native deadline failure is recorded above. |
| Windows x64 Release | Solution and affected fixtures built. Focused report had 1,172 passing checks and schema references had 4,127. Two-worker injected lifecycle, real lifecycle and live window passed. |
| Linux GCC Debug | Host, Platform, tests, schema inspector and affected fixture modules built. Ordinary `MorphicTests -t1` passed. The focused portable report run had 164 passing checks. Real backend returned `unsupported_backend` across activation, update and reload. |
| Authenticated Host lifecycle | Windows x64 Debug, Win32 Debug and Linux GCC Debug passed the one-worker injected cases. Executive and Platform independently acquired and released current. Host preserved earlier bytes across producer failure, semantic rejection and reload. Requester replacement completed across the accepted update. Unload and shutdown consumed the interruption receipt. |
| Existing Host / input behaviours | Windows x64 Debug and Linux GCC Debug passed two-worker `platform-shell`, `cleanup-credits`, `threaded-credits`, `modal-full` and `invalid-prefix`. Linux one-worker injected input `lifecycle` and `pressure` passed. |
| Integration | Solution/CMake policy checks passed. Nine source-extraction regressions passed, the input fixture matched 46 production entries, and the Visual Studio filter and line-ending checks passed. Linux exercised policy include-resolution symlinks. |

The Windows private SDK replay traverses the production CCD/GDI/DXGI state machine. It checks per-pump query counts, profile fallback, bounded count/fetch retries, final topology changes and reordered observations, clones and available inactive targets, invalid mode links, desktop image, unknown enumerants, rational refresh, ambiguous correlation, GDI field validity, zero modes, scoped fallback, denial, and global limit/deadline failures. The driver failure, buffer-growth, profile rejection and unstable-topology paths are injected observations. Native qualification observed the actual three-display configuration without changing it.

The portable capture fixture exercises busy ownership transfer, retry exhaustion, exact and backwards clock boundaries, external restriction, cancellation, and stale receipts. Host tests inject allocation failures into actual copy/adoption, check authenticated capture outcomes, receipt retention and zero residual control credit, and force a poisoned threaded POD reply after insertion. The recipient read that acquisition, its backing survived provider closure while the recipient was live, and Host retired it after terminal drain. The real-module runner separately covers stale/foreign outcome IDs, overlapping update and current reads through authenticated routes.

## Reproduction

Use `MSBuild MorphicEngine.sln /m /p:Configuration=Debug /p:Platform=x64`, substituting solution platform `x86` for Win32 Debug and configuration `Release` for x64 Release. The fixture projects take project platform `Win32` for the 32-bit build. Build `MorphicInputPlatform.vcxproj` and `MorphicPumpFixture.vcxproj` with each of `/p:FixtureRole=Executive`, `Provider` and `Replacement` for the pump fixture. The corresponding commands and solution-directory setting are in [the PowerShell runner](../../tests/host_pump/run_tests.ps1). Build `MorphicSchemaFixture.vcxproj` for independent layout/inspection. The Linux qualification build used these targets.

```text
cmake --build build/linux-identity --parallel 8 --target MorphicHost MorphicTests MorphicPlatform MorphicPumpExecutive MorphicPumpReplacement MorphicPumpProvider MorphicInputPlatform MorphicInputExecutive MorphicInputSecondary MorphicSchemaFixture
```

From the repository root, run the relevant binary with `-t1`, `--platform-report` or `--schema-references` and an output directory. The following commands reproduce the main runs.

```text
build/bin/x64/Debug/MorphicTests.exe -t1 --log-tag=platform-report-validation --output-directory=build/platform-report/windows-x64-Debug-unit
build/bin/Win32/Debug/MorphicTests.exe --schema-references --log-tag=platform-report-validation --output-directory=build/platform-report/windows-Win32-Debug-unit
build/bin/x64/Release/MorphicTests.exe --platform-report --log-tag=platform-report-validation --output-directory=build/platform-report/windows-x64-Release-unit
build/linux-identity/bin/MorphicTests -t1 --log-tag=platform-report-validation --output-directory=build/platform-report/linux-x64-Debug-unit
python tests/host_pump/run_tests.py --engine build/bin/x64/Release/MorphicEngine.exe --cases platform-report --host-workers 2 --output-directory build/platform-report/windows-x64-Release
python tests/host_pump/run_tests.py --engine build/bin/x64/Release/MorphicEngine.exe --cases platform-native-report --native --output-directory build/platform-report/final-live-release
python tests/host_pump/run_tests.py --engine build/bin/x64/Debug/MorphicEngine.exe --cases platform-report-injected platform-report-cancel platform-report-shutdown platform-report-departure --host-workers 1 --output-directory build/platform-report/windows-x64-Debug
python3 tests/host_pump/run_tests.py --engine build/linux-identity/bin/MorphicEngine --cases platform-report platform-report-injected platform-report-cancel platform-report-shutdown platform-report-departure --host-workers 1 --output-directory build/platform-report/linux-x64-Debug
python3 tests/host_pump/run_tests.py --engine build/linux-identity/bin/MorphicEngine --cases platform-report-injected platform-shell cleanup-credits threaded-credits modal-full invalid-seal --host-workers 2 --output-directory build/platform-report/linux-x64-Debug
python3 tests/input_host/run_tests.py --engine build/linux-identity/bin/MorphicEngine --output-directory build/platform-report/input-linux --host-workers 1 --case pressure
build/linux-identity/bin/MorphicSchemaFixture platform-layout build/platform-report/final-layout-gcc.inl
build/linux-identity/bin/MorphicSchemaFixture inspect-platform <capture.bsd> <capture.json>
pwsh -File tests/host_pump/check_report_desktop.ps1 -ReportJson <capture.json>
```

The offline inspector uses the shared schema export and strict JSON writer. Reconstructing all 1,580 mode rows requires an increased offline work budget of 67,108,864 credits. Production capture and publication enforce the version-1 effective limits recorded in each report.
