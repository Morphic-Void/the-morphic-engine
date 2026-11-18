# Host schema processing validation

The implementation baseline is `c818202c319c542f16cd948a2818996f23ff1ac8`. This record accompanies the version-1 request, reply and output descriptor. Implementation review is pending. [Service contract and usage](host-schema-processing.md)

## Evidence boundaries

**Real Host and loadable modules.** The `schema-process` pump case starts an Executive without Platform or Rendering, transfers an authored declarations tree, receives Accepted and a successful terminal result on slot 290, releases the acquisition and replaces the Executive. Only the process-local asset ID crosses the installation boundary via a fixture file. The replacement starts its own request sequence at 1 and supplies one retained declarations asset and one owned tree for both remaining roles. It attaches the returned complete block, follows the span to the second row and reads `Zulu`. It independently reads `Alpha` from the full collection, checks the 128-byte minimum alignment and appended bulk payload, ends its reads, releases the acquisition and disposes both assets. The runner checks the replacement’s success marker and process exit. Each case has its own runtime directory, Host event log and console log.

**Deterministic Host service boundaries.** `--schema-process` uses the production service, client records, owner and POD transports, backing leases, repository and worker conditioning function. The fixture chooses when to dequeue work, acknowledge completion, progress lifecycle and drain a recipient. Allocation failure, queue poison, eligible-time transitions and lost acknowledgements are injected. This makes observation of a possibly published pointer repeatable. The fixture also calls the internal Host posting entry point and checks the two-credit settlement.

| Property | Checks |
| --- | --- |
| Assembly and output | All six combinations, compatible declarations, incompatible declarations and conflicting values, missing selected roles, empty or unresolved runtime schemas, retained aliasing, declaration reuse, and independent results across operation-slot reuse |
| Admission and ownership | Bad contract versions, zero and stale request IDs, source/operation/output tags, dense inputs and canonical inactive fields, role indices, retained asset kind and accessibility, disposal while pinned, and a genuinely untransferred busy owning message |
| Budgets | Each caller ceiling above the default is rejected with its requested and allowed values. Tightening each allowance triggers its specific failure. A three-source runtime composition locates adjacent failing and passing shared-work budgets, checks late bake exhaustion, verifies the destination stays empty on failure and compares authored data to its original encoding. |
| Publication and lifetime | Accepted and terminal slot correlation, deferred pre-admission cancel intent, foreign cancellation and release, four occupied slots with bounded rejection retirement, protected replies, cancellation and expiry across dispatch and publication, late worker success, two acquisitions, disposal and producer replacement |
| Fault and drain | Failure of underlying allocation, ambiguous worker submission, poisoned Accepted or terminal publication, lost worker completion, exact recipient retirement and outstanding queue drain. Successful results remain readable through the acquired lifetime. |

One- and two-worker runs exercise `CAssetService::configure_workers` and the real Host worker message switch. A single worker supplies both file I/O and conditioning. With two workers, schema execution and heavy output reclamation use the conditioning worker. Cooperative cancellation can wait for an already entered synchronous phase.

## Results on 18 November 2026

| Build | Final focused checks | Tier one | Real Executive replacement |
| --- | ---: | --- | --- |
| Windows x64 Debug | 896 passed | Passed | One and two workers |
| Windows x64 Release | 896 passed | Passed | Two workers |
| Windows Win32 Debug | 895 passed | Passed | One worker |
| Linux x64 GCC Debug (WSL Ubuntu) | 896 passed | Passed | One and two workers |

Every focused suite reported zero failures. The exact-credit binary search checks each failed probe, so the number of assertions can vary with the target. Both Windows ABIs compiled the 48-byte terminal reply, 32-byte failure body, field offsets and descriptor layout assertions. The tier-one suites include the Host pump and workers, asset repository, bundle requests, prepared-schema service, Platform report replay, typed schema and text/span reference tests. The final additional checks for a negative async slot and named worker placement ran on every target.

Each placement run verified two conditioning events and two heavy reclamations on the selected thread. The checked tags were `executable:bg_file_io` for one worker and `executable:bg_conditioning` for two. The six summaries, including Win32 and Release, are under `build/schema-process-*-placement.log`. Focused totals are in `build/schema-process-*-final-focused.log`, and tier-one results are in `build/schema-process-*-tier1.log`.

The first incrementally linked x64 Release executable failed the saturation check for the rejection slot. A clean test build from identical sources recompiled all functions and passed. The Release Host was also rebuilt cleanly. Its final module run and final focused and tier-one tests passed. Build output is in `build/schema-process-rel64-clean.log`, `build/schema-process-rel64-host-clean.log` and the platform-specific `build/schema-process-*-publication-build.log` files.

The build-integrated source policy validator reported zero errors and zero warnings, with its existing negative-fixture suppression. The Visual Studio filter checker validated 12 project/filter pairs and skipped three projects without filters. The repository line-ending check and `git diff --check` passed.

## Reproduction

Run from the repository root in PowerShell using Visual Studio MSBuild. The solution takes `x86` for a 32-bit target. The fixture projects take `Win32`.

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild MorphicEngine.sln /m:2 /p:Configuration=Debug /p:Platform=x64
& $msbuild MorphicEngine.sln /m:2 /p:Configuration=Release /p:Platform=x64
& $msbuild MorphicEngine.sln /m:2 /p:Configuration=Debug /p:Platform=x86
$repo = (Resolve-Path .).Path
foreach ($arch in @('x64', 'Win32')) {
    foreach ($role in @('Executive', 'Replacement')) {
        & $msbuild visual_studio/MorphicPumpFixture.vcxproj /m:2 /p:Configuration=Debug "/p:Platform=$arch" "/p:SolutionDir=$repo\" "/p:FixtureRole=$role"
    }
}
& $msbuild visual_studio/MorphicPumpFixture.vcxproj /m:2 /p:Configuration=Debug /p:Platform=x64 "/p:SolutionDir=$repo\" /p:FixtureRole=Provider
& $msbuild visual_studio/MorphicInputPlatform.vcxproj /m:2 /p:Configuration=Debug /p:Platform=x64 "/p:SolutionDir=$repo\"
```

The clean Release investigation used the following project rebuilds after the Release solution had built its dependencies. Rebuilding the solution afterwards also links the final test and worker changes.

```powershell
& $msbuild visual_studio/MorphicTests.vcxproj /t:Rebuild /m:2 /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$repo\" /p:BuildProjectReferences=false
& $msbuild visual_studio/MorphicHost.vcxproj /t:Rebuild /m:2 /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$repo\" /p:BuildProjectReferences=false
& $msbuild MorphicEngine.sln /m:2 /p:Configuration=Release /p:Platform=x64
```

The Linux GCC Debug build used the existing Ninja configuration under WSL Ubuntu.

```sh
cmake --build build/linux-identity --parallel 7 --target MorphicEngine MorphicHost MorphicTests MorphicPumpExecutive MorphicPumpReplacement MorphicPumpProvider MorphicInputPlatform MorphicInputExecutive MorphicInputSecondary MorphicSchemaFixture
```

For the portable suites, invoke the corresponding `MorphicTests` binary with one selector at a time, for example as follows.

```powershell
build/bin/x64/Debug/MorphicTests.exe --schema-process --log-tag=generic-schema-final --output-directory=build/schema-process/qualified-dbg64-focused
build/bin/x64/Debug/MorphicTests.exe -t1 --log-tag=generic-schema-tier1 --output-directory=build/schema-process/qualified-dbg64
```

These commands reproduce the real Host runs.

```sh
python tests/host_pump/run_tests.py --engine build/bin/x64/Debug/MorphicEngine.exe --host-workers 1 --cases schema-process platform-report-injected platform-report-cancel platform-report-shutdown platform-report-departure --output-directory build/schema-process/qualified-windows-one
python tests/host_pump/run_tests.py --engine build/bin/x64/Debug/MorphicEngine.exe --host-workers 2 --cases schema-process cleanup-credits threaded-credits modal-full invalid-prefix --output-directory build/schema-process/qualified-windows-two
python tests/host_pump/run_tests.py --engine build/bin/Win32/Debug/MorphicEngine.exe --host-workers 1 --cases schema-process --output-directory build/schema-process/qualified-win32-one
python3 tests/host_pump/run_tests.py --engine build/linux-identity/bin/MorphicEngine --host-workers 1 --cases schema-process platform-report platform-report-injected platform-report-cancel platform-report-shutdown platform-report-departure --output-directory build/schema-process/qualified-linux-one
python3 tests/host_pump/run_tests.py --engine build/linux-identity/bin/MorphicEngine --host-workers 2 --cases schema-process platform-shell cleanup-credits threaded-credits modal-full invalid-prefix --output-directory build/schema-process/qualified-linux-two
```

For the separate placement runs, select only `--cases schema-process` and use output directory `build/schema-process/placement-<platform>-<workers>`. For Release, build the Executive and Replacement fixture projects with `Configuration=Release`, then use the Release engine.

```powershell
python tests/host_pump/run_tests.py --engine build/bin/x64/Release/MorphicEngine.exe --host-workers 2 --cases schema-process --output-directory build/schema-process/placement-release-2
```

Each `build/schema-process-*-host.log` summary names the exact per-case directory. The directories under `build/schema-process/qualified-*` hold the corresponding detailed logs. Binary outputs and runtime fixtures are local build evidence.

## Contract and follow-up

The new message and descriptor contracts are version 1. The schema service introduces no persisted layout or maintained data conversion. The generic document marker is 5 and the schema data-block marker is 1. Test modules and consumers compile against the same current headers and construct fresh inputs and outputs in each run.

The compact Platform filter/layout, authored capture handoff, native capture threading and native Win32 five-second deadline qualification remain follow-up work. The injected report cases exercise the shared Host route with the current provider. [Platform qualification record](../modules/platform_report_validation.md)
