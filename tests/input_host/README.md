# Real Host input acceptance

This fixture runs the engine/Host, a loaded Platform module and an independently
scheduled Executive consumer. A second consumer uses the Rendering module slot.
The production Executive is unchanged. See the
[contract and acceptance record](../../docs/modules/input_host_integration.md).

Build the ordinary solution first. On Windows, build/run the private fixture with:

```powershell
./tests/input_host/run_tests.ps1 -Configuration Debug -Platform x64 -WorkerCount 1
./tests/input_host/run_tests.ps1 -Configuration Debug -Platform x64 -WorkerCount 2 -SkipBuild
```

The wrapper accepts `-MSBuild` and `-Python` for explicit executable paths and
`-Platform Win32` for the 32-bit fixture. The three standalone Visual Studio
projects are `MorphicInputPlatform`, `MorphicInputExecutive` and
`MorphicInputSecondary`. They import the ordinary Core/Framework sources; the
Platform runtime files remain directly owned by `MorphicPlatform`, under its
`Platform/include` and `Platform/src` filters. The acceptance project mirrors
those source/header entries and adds its private adapter. `check_sources.py`
checks the two file lists and filter mappings before every acceptance run and
Linux CMake configuration, so additions cannot silently drift between variants.
Linux CMake builds these targets and registers the matrix as `MorphicInputHost-*`.

| Case | Coverage |
| --- | --- |
| `lifecycle` | Host asset publication/acquisition, installation, five pipes, readiness/boundaries, notifications, application and retirement. |
| `pressure` | Action/text capacity boundaries, overflow, pauses of 25/100/500 ms, recovery and independent-pipe progress. |
| `transitions` | Focus/modal/history loss, per-control neutral rearming, reconnect identity, context barriers and reacquisition. |
| `work-pressure` | A 65-action mapping continuation, a blocked noisy pipe, quiet-pipe progress and ordinary commands. |
| `second-consumer` | Independently scheduled owner, pipe retirement and replacement. |
| `shutdown-blocked` | Shutdown while a full ring blocks a context marker, with cooperative quiescence. |
| `ingress-loss` | Desktop source burst, ingress overflow and recovery. |
| `acquisition-cleanup` | Rejected acquisition and retirement after preparation but before reader readiness. |
| `source-cleanup` | Successful transfer/acquisition replies arriving after cleanup begins, retaining and retiring their ownership. |
| `load-smoke` | Short finite-load count/correlation checks; CI uses 120 reports/s. |
| `load` | Two-second warm-up and three ten-second measured windows. |

For one Release baseline run:

```powershell
python tests/input_host/run_tests.py --engine build/bin/x64/Release/MorphicEngine.exe --output-directory build/input-host-baseline --case load --host-workers 1 --rate 1000 --pipes 4 --trace on
```

Rates are 120/500/1000 reports/s **per controller**, with one or four participating
controllers and a desktop key change every 16 generator rounds. Each controller
report changes a button and left-stick value. `--trace off` retains functional
expected-output checks while disabling trace collection. Do not run builds or
other acceptance processes concurrently with a timing baseline.

The runner retains an isolated runtime, `console.log`, Host logs and `report.json`.
Reports include window durations/counts, generator lateness, expected/traced
outputs, stage percentiles in nanoseconds, Platform pump activity, queue high-water marks,
losses, counter frequency and process/build identification. Percentiles are
weighted by output records, so one report's button and stick outputs both count.
Whole-process CPU includes setup, warm-up, drain, trace sorting/reporting and
teardown; it does not isolate per-window instrumentation overhead. Trace-off
reports contain no latency percentiles.

`pump_work` measures a Host-invoked `CPlatformRuntime::pump` call's duration;
`pump_gap` measures the gap between those calls' starts. Neither measures the
entire Host iteration or total scheduler work.

Generation and application share the fixture Executive thread. Generator lateness
precedes actual handoff and is excluded from offered-to-applied latency; catch-up
can produce bursts. Four controllers are offered together each round. Private
`SOURCE.feed/history` counters measure synthetic buffer overflow, while public
per-pipe `ingress_loss` includes injected/native history recovery.

Use uninterrupted runs. The performance/awake-clock discrepancy check detects
Windows suspend divergence; both Linux clocks exclude suspend, so that check
cannot detect Linux suspension. Neither native GameInput timestamps nor display
refresh times are subtracted from the performance counter.

Interactive Windows checks require a connected controller and an operator:

```powershell
python tests/input_host/run_tests.py --engine build/bin/x64/Debug/MorphicEngine.exe --output-directory build/input-host-native --source ordinary --seconds 120
```

`ordinary` loads the production Platform; `native` loads the instrumented Platform
variant with native adapters. Both support the interactive lifecycle case only.
The title updates at approximately 60 Hz. Check keyboard A, controller A, sticks
and triggers; hold A/stick across Alt+Tab and `C` context changes, then release/
centre before fresh input. `U` unassigns and `R` requests assignment. Automated
completion does not establish physical acceptance: record the operator's result.

A watchdog failure retains diagnostics and does not authorize terminating a live
borrower or claiming successful cleanup. Inspect any surviving process before
starting another run.
