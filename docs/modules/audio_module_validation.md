Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    audio_module_validation.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    31 Oct 2026

# Audio implementation and validation

This record accompanies the initial implementation of the
[agreed Audio design](audio_module_design.md). The validation below was performed
in the shared checkout before the implementation commit. Hosted CI has not
validated this implementation.

## Implemented boundary

`MorphicAudio` is a bootstrap-v3 module with one service thread. Loading it leaves
the device closed. A `RunTestsRequest` on the Executive/Audio direct connection
opens the default real output and
plays middle C as float stereo: two seconds fading up, one second at peak 0.1,
two seconds fading down, then 250 ms of silence before stopping. The callback
fills every requested frame and publishes bounded atomic progress. Device
creation, start, stop, cleanup, logging and completion remain on the service.

The service waits indefinitely while idle. Active runs retain awake-time
deadlines for a 15-second overall budget, a two-second no-progress budget and
20-ms checks. Only advancing frame progress renews the no-progress deadline;
message wakeups do not renew either budget. Message drains are bounded. Clock
construction, query and wait errors fail the run. Cancellation does not depend
on another callback. All vendor execution is stopped before completion or a
terminal service state is published.

As of 5 November 2026, [direct POD connections](direct_pod_connections.md) carry
test requests/results through Host-owned growable queues. Host handles connection
controls, not test payloads. Normal runs consume their result before closing;
unload and shutdown cancel unfinished work and wait for device cleanup before
endpoint quiescence. The initial validation record below predates this migration.

Executive runs Rendering, then Audio, then the existing acceptance flow. Its
generic module-test watchdog uses a 30-second awake-time deadline. Audio success
requires exactly `success` with `1/0/0`; a backend that genuinely enumerates no
output (or returns `MA_NO_DEVICE`) can report `unavailable` with `0/0/1`. Allocation,
backend initialization, device-start, runtime, timing and cleanup failures do
not become skips. Generic device-open errors remain failures because they do
not establish absence of an output.

The module adds no public playback controls, codecs, capture, mixing, device UI,
Host special case or background playback. Native blocking calls remain outside
the service's ability to interrupt; process-level harness timeouts are still
required.

The Host's `k_expected_service_threads = 2` remains a scheduling estimate, not a
service count or capacity. Audio's control thread normally sleeps, and this
Executive finishes the tone before asset/batch acceptance starts. Reserving an
additional full CPU slot for that idle thread would reduce batch capacity without
representing the current workload. The existing heuristic is deliberately
retained and should be revisited if sustained concurrent Audio work is added;
there is no Audio-specific Host scheduling path.

## Pinned adapter and failure ownership

The vendor remains miniaudio 0.11.25 at
`9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, with its source unchanged. Decoder,
encoder, resource manager, node graph, high-level engine, vendor waveform
generation and null backend are disabled. Every vendor allocation callback uses
the stable explicit engine memory context, including calls from vendor threads;
native system/driver allocations retain native ownership. Failed reallocations
preserve the old allocation, and vendor-observed allocation failures cannot be
misreported as unavailable.

Review found two ownership gaps in the pinned context initializer: it clears
native handles after exhausting backends, and it does not safely distinguish
partial mutex acquisition after a backend succeeds. The user approved a private
initializer, replacing the earlier proposed production cleanup guard.
`audio/runtime/miniaudio_context.inl` preserves the pinned native API setup and
backend preference/dispatch, requires Audio's owned external log, tracks each
acquired resource and stops at the first failed mutex. Cleanup releases only
acquired resources, once, and reports native teardown errors.

The private initializer records its upstream revision and function hash. A
compile-time version assertion and `tests/audio/check_vendor.py` require review
when the upstream initializer changes; the script also compares backend dispatch
against the pinned source. MIT No Attribution provenance is recorded in the
private source and [third-party notices](../../THIRD_PARTY_LICENSES.md); both build
systems stage `miniaudio-LICENSE.txt` beside the module.

Device-free tests use a private custom backend solely to exercise acquisition
and cleanup; production does not install it. It allocates a real owned block,
uses native API setup and real mutexes, and can inject failure at either mutex
acquisition. Assertions check exact acquisition/release counts, double-close
idempotence and engine allocation baseline. It never opens a device or reports
playback success.

## Automated coverage

The ordinary Audio suite covers sample continuity across callback partitions,
stereo equality, frequency, envelope endpoints and padding at 44.1/48/96 kHz;
allocation failure, overflow, realloc preservation and cross-thread freeing;
clock errors, overall/no-progress budgets, completion races and message storms;
and the actual service thread's idle behavior, invalid/busy requests, completion,
repeated cancellation and failed result delivery. Fake devices exercise failed
open/start/close and stalled/interrupted callbacks without depending on hardware.

The same private watchdog operations used by Executive are tested with a
controlled clock: a fresh 30-second budget per module, 5,000 checks without
renewal, the last nanosecond before expiry, exact expiry, and explicit
construction/query errors. Executive retains its check before message reads.
Executive tests cover ordering, module-load failure, correlation, exact Audio
counts and rejected failed/cancelled/busy results. The native DLL lifecycle case
loads Audio, runs it twice, requests unload during the second run, and checks
completion precedes unload and service join. Host harnesses require the
Rendering-to-Audio-to-assets sequence and distinguish success from explicit skip.

The final local matrix passed on 31 October 2026 against the uncommitted working
tree based on `9a3fa8b`, including the private initializer, watchdog tests,
cleanup-log ordering fix and generic startup diagnostics:

| Platform/configuration | Final result |
| --- | --- |
| Windows x64 Debug | Ordinary suites, six Host-worker scenarios, all 46 lifecycle cases with one and two requested workers passed. |
| Windows Win32 Debug | Ordinary suites, six Host-worker scenarios, all 46 lifecycle cases with one and two requested workers passed. |
| Windows x64 Release | Ordinary suites, six Host-worker scenarios, all 46 lifecycle cases with one and two requested workers passed. |
| Windows Win32 Release | Ordinary suites, six Host-worker scenarios, all 46 lifecycle cases with one and two requested workers passed. |
| Linux GCC Debug | All five CTests passed (43.71 seconds). |
| Linux GCC Release | All five CTests passed (10.65 seconds). |
| Linux Clang Debug | All five CTests passed (48.09 seconds). |
| Linux Clang Release | All five CTests passed (9.92 seconds). |

Each ordinary suite reported Audio `126 passed, 0 failed` and ErasedOwner
`1115 passed, 0 failed` (including the controlled Executive watchdog checks).
The Linux CTests are policy includes, ordinary suites, reflection-arena fatal
child-process checks, and Host acceptance with one and two requested workers.
Linux Host acceptance used the real backend's explicit no-device path described
below. Windows Host and Audio lifecycle checks used native playback.

Final logs are retained under `build/audio/` as
`windows-<Debug|Release>-<x64|Win32>-review-final-{build,tests,host,lifecycle-1,lifecycle-2}.log`
and `linux-<gcc|clang>-<debug|release>-review-final-{build,tests}.log`.
`windows-review-final-progress.log` and `linux-review-final-progress.log` record
driver completion. Full configuration builds and lifecycle fixture builds
preceded review; the final runs incrementally rebuilt the affected Audio,
Executive and test objects and reused unchanged fixtures. Earlier
`*-context-fix-*` results are intermediate evidence, not substitutes for these
final results. Generated build directories are not source files.

Supporting checks passed: seven source-extraction tests, the pinned-initializer
and backend-dispatch check, repository line-ending and diff checks, and matching
upstream licence hashes in all eight build outputs. Two concurrent private
PulseAudio servers also passed after their session D-Bus connections were
isolated; the tested command retains its original session bus environment.
No hosted CI run is claimed for this uncommitted tree.

## Listening and environment evidence

Native Windows WASAPI selected `Speakers (Realtek USB Audio)` at a 48-kHz callback
rate. The user confirmed audible tone and fades, and explicitly confirmed an
isolated replay was clean. Its retained Host output is under
`build/audio/native-isolated/host-1-d04fcplh`.

During final native Windows validation after the coordinator fixes, the user
reported hearing at least 18 tones, all clear, followed by another 54 clear
tones. This confirms at least 72 clear tones during repeated native playback,
beyond the earlier isolated replay.

WSLg PulseAudio selected `RDP Sink` and completed the automated playback flow,
but the user reported interference and crackle on the second fading tone. The
isolated native Windows replay was clean. The cause of the WSLg artifact has
not been established; this is not evidence of clean physical Linux playback.
Generated-frame completion alone does not establish audible quality.

Remaining Linux automated acceptance uses
`tests/audio/run_headless.py`, which starts a private PulseAudio server with no
sinks or sources and sets `PULSE_SERVER` only for its child command. It does not
alter the user's WSLg server, create a null output, or claim playback success.
The real backend enumerates zero outputs and produces the explicit unavailable
result while the rest of Host acceptance continues. Missing native runtime
libraries previously produced an initialization failure; an ALSA default-open
error likewise remained a failure rather than being hidden as a skip.

Physical Linux listening, other native output devices and suspend/resume during
real playback have not been manually validated. The automated timing and
lifecycle evidence and the native Windows listening confirmation are distinct.
