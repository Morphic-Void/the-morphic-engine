Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   audio_module_design.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   30 Oct 2026

# Audio module: one requested tone test

## Status and agreed envelope

This is a design and implementation plan, based on the code at `f00b2eb` and
the bundled miniaudio source. No Audio implementation is introduced by this
document. Later implementation follows the scope authorised by the user.

The agreed first stage is deliberately small:

- Audio is an independently loaded module, using miniaudio for output.
- Executive sends one test request. Audio starts playback, fades a middle-C sine
  wave up, holds it for one second, fades it down, stops playback and returns
  completion.
- Audio owns that entire sequence. Executive does not send individual playback,
  gain or stop commands and does not interpret audio buffers or device details.
- No window, Platform module, host-pumped capability or visual fader is required.
- Human/AI command entry, scripting, files/codecs, capture, mixing, spatialisation,
  device-selection UI, streaming assets and a general audio service API are outside
  this stage. The fade is internal to the test, not a public gain-control contract.

The initial Audio behaviour and policies below were agreed through 31 October
2026. The design is ready for the separate timed-wait framework work. File layout
and adapter mechanics remain implementation guidance; the actual wait API and
check interval will be reassessed here after that prerequisite is delivered.

## Existing implementation and reuse

| Area | Current code | Planned use |
| --- | --- | --- |
| Module binding | [module_binding.hpp](../../core/module/module_binding.hpp), [module_binding_context.cpp](../../core/module/module_binding_context.cpp) | Reuse bootstrap v3 and the optional `morphic_module_service_thread_v1` export. |
| Working service | [rendering_binding.cpp](../../rendering/module/binding/rendering_binding.cpp), [rendering_thread.cpp](../../rendering/runtime/rendering_thread.cpp) | Follow binding, thread preparation, readiness, message handling and terminal-state conventions; do not copy texture/shader machinery. |
| Generic tests | [service_messages.hpp](../../core/system/service_messages.hpp), `RunTestsRequest` / `RunTestsResult` | Already express a module destination, correlation, status and counts. No new Audio-specific message types are needed. |
| Host routing | [host.cpp](../../host/runtime/host.cpp), `request_tests`, `complete_tests` | Routes to any registered module service thread. One run is active globally; caller and worker are retained until completion. |
| Unload | [module_service.cpp](../../host/runtime/module_service.cpp), `CHost::stop_module_thread` | Stop/join the service before dependency disposal and worker-side unbinding. Audio must stop all vendor callbacks before publishing terminal state. |
| Executive | [executive_thread.cpp](../../executive/runtime/executive_thread.cpp) | Extend the existing test flow to load Audio as it loads Rendering and run both modules' generic tests sequentially. |
| Waiting | [CThreadPackage.hpp](../../core/threading/CThreadPackage.hpp), [CWaitPredicate.hpp](../../core/threading/CWaitPredicate.hpp) | Idle services can park and are woken by Host messages/exit. There is no existing timed-wait argument in this predicate API. |
| Memory | [memory_context.hpp](../../core/memory/memory_context.hpp) | Explicit-context allocate/deallocate and allocation attribution; no direct realloc operation. |
| Vendor | [miniaudio.h](../../external/miniaudio/miniaudio.h), [sine example](../../external/miniaudio/examples/simple_playback_sine.c) | Low-level playback/device API and a standalone sine example already exist in the checkout. |

The pinned miniaudio gitlink is
`9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`; its header reports version 0.11.25.
It is already a submodule and has solution-folder visibility, but is not integrated
into an engine Audio project. No dependency update is proposed.

The sample opens an output device without creating a window. The DirectSound
context option has an optional window handle; the engine does not need to obtain
one from Platform. The agreed real-backend selection policy below does not
introduce an engine-window dependency.

The wider Platform connection/lifetime proposals are not prerequisites. Executive
already has the module-load route, and a normal Audio service thread already fits
Host's generic test routing.

## Module boundary and proposed layout

Add a backend-neutral Audio mount, module identity and service-thread identity.
Do not add speculative output backends or an `audio_thread_function` SYSTEM ID:
the existing optional service-thread descriptor is sufficient unless a concrete
caller needs an additional query later.

Proposed files:

- `audio/module/binding/audio_binding.*`: bootstrap and service advertisement.
- `audio/module/types/`: the normal component-local catalogue/registration setup.
- `audio/runtime/audio_thread.*`: generic test admission and service lifecycle.
- `audio/runtime/tone_test.*`: finite test state, signal generation and envelope.
- `audio/runtime/miniaudio_adapter.*`: private device/context, vendor build and
  allocation callbacks; keep miniaudio declarations out of public contracts.

Core gains only necessary catalogue entries and, if justified by review, a reusable
primitive. Host stays generic. Executive knows the selected Audio identity and
generic completion, not frequency, gains, duration, backend or callback state.
No Audio asset types, resource repository or batch jobs are required.

Keep all miniaudio implementation code in the Audio module (and an isolated test
target where needed), compiled once per binary. Disable unneeded decoding,
encoding, resource-manager, node-graph and high-level engine facilities where
supported by the pinned source. Do not copy or modify the upstream example into
Core or Host. Retain the vendor licence and use engine file/header conventions
for the adapter and tests.

## Single-request protocol

Module loading is setup, separate from the single request that runs the test:

1. Executive requests `MorphicAudio` through the existing native-filename helper
   and `ModuleRequest`, requiring a service thread.
2. It validates acknowledgement and completed readiness. The Audio service becomes
   ready without opening an output device or making sound.
3. Executive sends `RunTestsRequest{ module_ids::audio }` with one correlation.
4. Audio performs the complete finite sequence, including cleanup.
5. Audio sends one `RunTestsResult` with the same correlation and module identity.
   Executive accepts the result through generic status/count checks.

The successful result is `success`, one passed, zero failed and zero skipped.
That count represents the complete runtime tone exercise; internal deterministic
unit checks do not inflate it. The service remains loaded and idle after the test.
Stopping playback is distinct from unloading the module or exiting its service
thread. A later request can run a fresh test.

Retain Host's global busy behaviour, correlation checks and worker-exit fallback.
Audio also validates the destination/correlation and rejects an overlapping direct
request without resetting the active run. A malformed request, unavailable device,
interrupted run and failed completion delivery must not appear as a successful test.
Return completion only after the device and callback-owned state are quiescent.

Agreed device-result policy: no usable real output device gives `unavailable`
with one skipped test; start/device/runtime failure gives `failed` with one
failed test. Agreed shutdown cancellation gives `cancelled` with zero passed,
zero failed and one skipped test, after cleanup. Record specific native
diagnostics inside Audio. Current Executive success checks reject unavailable or
skipped runs. Extend the combined test flow with the agreed continuation rule
below so a headless run reports non-execution without claiming successful audible
playback.

## Tone and fade sequence

Use a finite, sample-count-driven envelope so timing is independent of Host or
service-thread scheduling. The following signal parameters, trailing silence and
deadlines are agreed as of 31 October 2026:

| Parameter | Value / status |
| --- | --- |
| Frequency | Middle C, approximately 261.625565 Hz with A4 at 440 Hz |
| Callback format | Interleaved float32, stereo with the same tone in both channels |
| Requested sample rate | 48 kHz; use the configured callback-side rate consistently |
| Peak gain | 0.1 linear amplitude; this does not define physical speaker loudness |
| Fade up | 2 seconds, starting at zero |
| Plateau | 1 second at peak gain |
| Fade down | 2 seconds, ending at zero |
| End padding | 250 ms of silence before requesting device stop, subject to native playback validation |
| Overall test deadline | 15 seconds from acceptance of the test; below the current Executive's 30-second operation deadline |
| No-progress deadline | 2 seconds without callback progress, measured from successful device start and refreshed only by actual progress |

The callback advances oscillator phase continuously across buffers and applies
the gain envelope per frame. Define the endpoint/sample-count convention explicitly
so block size does not change duration or omit the final zero. Handle callback
buffers spanning fade-up, plateau, fade-down and silence boundaries; always initialise all
output samples. Once complete, continue producing silence until stopped.

Agreed on 31 October 2026: Audio owns the small oscillator and finite envelope,
with miniaudio providing device output. This makes exact frame/envelope behaviour
directly testable without an output device. There is no second production signal
path using `ma_waveform`.
There is no runtime parameter editing or gain-sharing protocol in this stage:
the immutable test plan is prepared before start and the callback owns phase,
frame cursor and envelope progression.

Submitted frames are not proof that a listener heard them. In the pinned API,
stopping can pause rather than fully drain output. End padding reduces the chance
of cutting the fade, but its adequacy needs native playback validation against
the chosen buffering policy. Do not equate a generated final frame with an exact
hardware presentation timestamp or claim an acoustic test from counters alone.

## Service and callback responsibilities

The Audio service thread owns context/device creation, start, stop and teardown.
Its state sequence is `idle -> preparing -> playing -> stopping -> reporting -> idle`,
with failures and cancellation converging through cleanup before reporting.
Partial initialisation must track which resources actually need teardown.

The miniaudio data callback performs only sample generation and bounded progress
publication. It must not allocate through engine code, log, post ordinary engine
messages, take engine locks or call device lifecycle operations. The pinned vendor
documentation prohibits init/start/stop/uninit from the callback because these can
deadlock. Keep notifications equally small: publish terminal/interruption flags
and let the service thread interpret them. An expected stop notification is not a
device failure.

Vendor callbacks run on vendor-managed execution paths, not the Audio service
thread. They must not borrow its SPSC producer role or assume its engine TLS has
been installed. Pass stable explicit callback state and allocator context. Perform
engine diagnostics and message publication on the service thread. If later code
requires engine operations from vendor threads, design that provisioning separately.

Use release/acquire publication for terminal progress and service-to-callback
cancellation, with callback-owned counters otherwise private. Prefer fixed-width
atomic flags verified lock-free on both Win32 and x64; do not assume that arbitrary
64-bit or floating atomic state has identical properties on every build. Reset
state only after all callbacks from the preceding run have stopped. State and
notification storage stay alive through device and context uninitialisation.

### Completion observation and liveness

An indefinite service wait is insufficient: a callback setting a boolean does not
wake `CWaitPredicate`, and a stalled device may never invoke another callback.
Do not add a callback producer to the existing service-to-Host transport.

Agreed on 31 October 2026: use timed predicate waiting while playback is active,
replacing the earlier proposal to spin on progress. Host messages and exit requests
can wake the service through its existing predicate; timeout expiry allows it to
check callback-published progress, completion and stalled output. Park indefinitely
when idle. The callback only publishes atomic state and does not wake the service
or enter its ordinary message transport.

A nominal 20 ms progress-check interval is a proposal to reassess after the
framework support exists. This interval controls completion observation and
housekeeping, not sample generation or fade timing. Scheduling can delay a wake;
the interval is not a hard maximum response latency. Keep the periodic check,
overall test deadline and no-progress deadline distinct. Ordinary wakeups must
not restart the overall deadline or count as evidence of audio progress.

The existing wait API has no timeout parameter. Its expansion is separate framework
work, to be discussed and implemented in a dedicated chat after the Audio design
is completed. This Audio chat can then resume and reassess the actual API,
semantics and validated platform support before implementation begins. This
document does not claim that timed waiting exists or fix its final signatures.
The work does not depend on Platform's future Host event-loop/wait integration.

The framework handoff should include:

- Extend the existing [wait-word layer](../../core/platform/threading/wait_word.hpp)
  and [predicate abstraction](../../core/threading/CWaitPredicate.hpp), preserving
  current indefinite-wait behaviour and message/exit wakeups.
- Define monotonic deadline semantics and remaining-duration calculation so
  spurious wakes or interruptions cannot extend a wait indefinitely. Review
  observed-value, timeout, control-release and native-error results explicitly.
- Account for native clock domains: Linux's current performance counter uses
  `CLOCK_MONOTONIC_RAW`, whereas the usual absolute futex deadline uses
  `CLOCK_MONOTONIC`. Their absolute timestamps must not be interchanged.
- Review Windows millisecond timeout conversion, Linux/Android futex behaviour,
  macOS timed address-wait availability and any fallback path. Do not silently
  implement a timed request as an indefinite wait on an unsupported path.
- Validate notification-before-wait races, timeout/completion races, spurious
  wakes, control release, deadline preservation and safe wait-word lifetime.

After that work, confirm Audio's wait interval and progress checks against the
delivered contract. A callback-driven wake can remain a later optimisation; it
is not required for the initial tone test.

Native init/start/stop calls may themselves block. A deadline checked by the same
service thread cannot interrupt such a call. Bound acceptance tests with an outer
process timeout and report a hung backend; never claim safe forced DLL unload or
thread termination as timeout recovery.

## Allocation and vendor containment

Use an explicit miniaudio context configured with `ma_allocation_callbacks`, then
initialise the device against that context. Do not use the sample's default-context
shortcut and silently accept default heap routing. The adapter must supply
malloc/realloc/free semantics through Audio's stable `CMemoryContext`.

The engine allocator needs sizes/alignment at deallocation while the vendor free
callback supplies only a pointer. Plan an aligned private allocation header holding
the necessary metadata. Check header/padding arithmetic and alignment. Implement
reallocation as allocate/copy/free, preserving the original block on failure, and
define null-pointer and zero-size cases against vendor usage. Keep metadata private
and ensure allocation/free remain correct when called from different vendor threads.

Audit the adapter for paths bypassing configured callbacks, including optional
features and log formatting. Contain native backend allocations within their
required native lifetime rather than claiming every OS/driver allocation passes
through the engine allocator. No library update or broad allocation-policy exception
is implied. Context uninitialisation must occur before its allocator bridge dies;
the Audio allocation baseline should be restored after each complete test.

## Shutdown and failure behaviour

On a normal run, finish the envelope, produce trailing silence, stop/uninitialise
the device, uninitialise its context, then send the correlated result. No miniaudio
thread, notification, callback, log hook or allocation may retain Audio code/state
when the service publishes `Exited` and Host proceeds to unbind.

If exit is requested during a test, stop accepting new work and perform service-side
cleanup promptly, without an additional cancellation fade. Shutdown must not
require future audio callbacks. Report `cancelled` with zero passed, zero failed
and one skipped test after quiescence. Repeated exit requests and partial start
failures must be harmless. Normal completion retains its full fade and trailing
silence; cancellation does not wait for that sequence to finish.

Host currently protects an active test caller/worker from ordinary unload, and its
full-shutdown progression waits for the active test to finish before requesting
other service exits in some paths. Consequently the finite test must finish or
fail independently; do not rely solely on receiving an exit request to end a
stalled run. Review these interactions in lifecycle tests without adding Audio
special cases to Host.

Completion publication failure marks the service failed after device cleanup, so
Host's terminal-worker handling can resolve the outstanding test. Never publish
a terminal service state before vendor activity is stopped. If safe uninitialisation
cannot be established, use the existing unsuccessful shutdown/retained-binding
principle; retaining a DLL alone does not make freed callback state safe.

## Hardware policy and test selection

Do not silently accept miniaudio's null backend as a successful audible run. The
pinned source supports it and includes it in backend enumeration; exclude it from
the real-device selection or explicitly reject it. A fake/null device used in
automated validation must be labelled as such and must not stand in for listening.

Agreed on 31 October 2026: use the default output device in shared mode and let
miniaudio select an available real backend using its normal preference order,
excluding the null backend. Do not introduce an engine-specific backend preference
list or exclusive mode for this stage. Confirm the selected backend/device in
service-thread diagnostics. Linux audio-session availability is independent of
window availability; native output still needs suitable runtime services/devices.

No usable output device produces an explicit unavailable/skipped result. An
audible acceptance run treats that as unsuccessful; headless validation reports
the intentional non-execution. Unexpected playback interruption or device failure
ends the run with cleanup and a failed result. Do not automatically reconnect,
switch devices or recover the run. Audit backend notification/rerouting behaviour
when implementing the adapter so implicit recovery cannot conceal a failed test.

Agreed on 31 October 2026: use the existing Executive test process that triggers
Rendering tests. Executive explicitly loads Audio through the same module-load
mechanism and sends its generic test request. Run Audio and Rendering tests
sequentially; either order is acceptable. Do not add a dedicated Audio Executive
or require a separate opt-in application mode for this stage.

Use Rendering first, then load and test Audio, then continue the existing general
asset/filesystem/bundle acceptance flow as the initial sequencing choice. This
preserves the existing Rendering startup prefix and fits Host's one-active-test
contract. Each module load retains its acknowledgement/completion checks; each
test has a fresh correlation and operation deadline. Executive only selects
modules and interprets generic results; all tone/fade/device work stays in Audio.

The combined flow can therefore produce the tone whenever a real output device
is available. The standalone ordinary unit-test executable should still exercise
the deterministic generator/adapter seam without opening an output device.

Agreed no-device continuation rule: Executive logs Audio's
`unavailable` result with one skipped test and continues the remaining acceptance
flow. This is a recognised non-execution, not an Audio pass. All actual Audio
test failures still fail the flow. The same executable/process can serve headless
CI and audible validation: the harness reports the skip in headless runs, while
an audible acceptance check requires an Audio success result and rejects a skip.
This changes result interpretation in the current Executive; it does not require
Host audio knowledge, a new production request type or another Executive binary.
Only a correctly correlated Audio result identifying `unavailable`, zero passed,
zero failed and one skipped test permits this continuation. Cancellation, malformed
results, load failures and actual test failures must not be reclassified as a
missing-device skip. Rendering retains its existing success requirements.

## Implementation sequence

| Stage | Changes | Required evidence |
| --- | --- | --- |
| 1. Deliver wait prerequisite and reassess | Audio behaviour and policies are settled. Complete framework wait support in a dedicated chat, then review its delivered API and the proposed Audio check interval here. | Required timed waits implemented and validated; integration confirmed before Audio implementation. |
| 2. Audio module shell | Add binding, service thread, actual catalogue entries and build/policy wiring. Reuse generic test routing. | Load/readiness/unload/replacement work without opening a device; malformed requests and missing capability cases retain existing semantics. |
| 3. Signal and private adapter | Implement finite oscillator/envelope, allocation bridge and one private miniaudio implementation unit. | Deterministic signal checks, allocator failure semantics and callback-state lifetime verified without hardware. |
| 4. End-to-end test | Service prepares the device, runs the envelope, observes progress, stops and replies. Extend the existing Executive flow with sequential Rendering/Audio loads and tests. | Exactly one correlated completion after cleanup, one pass on real success, repeatability and explicit unavailable/failure handling. |
| 5. Qualification | Exercise shutdown, callback/notification races and partial failures; clean-build Windows/Linux matrices and run existing acceptance. | No callback after unbind, no retained allocations, no new Platform/window dependency, native playback distinguished from headless checks. |

Build integration includes `MorphicAudio.vcxproj`, its items/filters and solution
configurations; Linux shared-library classification in `CMakeLists.txt`; deployment
and isolated runtime manifests; native symbol exports including Win32 aliases;
and documentation of the extended existing acceptance flow. Miniaudio's bundled
Linux instructions name pthread, dl and m linkage; verify what the adapter target
needs instead of copying Rendering's dependency list.

Add Audio to policy scan roots, component-relative include resolution and its
regression checks. Restrict vendor inclusion/allocation adaptation to the private
adapter; preserve Core's Core/SuiteUTF boundary. Audit explicit component lists in
source extraction/test tooling. Preserve source opening comments and Visual Studio
metadata conventions. Clean-build all components/fixtures when identities change.

Host's `k_expected_service_threads` is currently a scheduling estimate of two,
not a hard capacity. Review it against the agreed test composition of Executive,
Rendering and Audio. Vendor audio threads also consume execution
resources; do not invent catalogue identities for their internal implementation.

## Validation plan

- Deterministic checks: correct frequency within numeric tolerance, continuous
  phase across varying buffer sizes, fade endpoints/durations, channel consistency,
  finite bounded output, complete buffer writes and silence after completion.
- Adapter checks: allocation alignment/realloc preservation/failure cleanup,
  init/start failure, no callback progress, unexpected stopped/interruption
  notification, and notification during teardown. Use a test-only device seam;
  do not add production test switches or a general backend framework.
- Lifecycle checks: repeated test requests, busy/rejected requests, exact
  correlation, completion delivery failure, exit during playback, normal unload
  after completion and no vendor code/state remaining after service join.
- Combined-flow checks: sequential load/test correlations and per-operation
  deadlines, continuation after a recognised no-device skip, failure
  propagation, and preservation of the subsequent general acceptance cases.
  Update existing harness log/count assumptions and stage the Audio library in
  every isolated runtime using this Executive; a missing module is a load failure,
  not a missing-device skip.
- Windows: Debug/Release x64/Win32 clean builds, ordinary suites and existing
  Host/lifecycle acceptance with one/two workers, plus new Audio cases. Run the
  audible test explicitly on a real output device and check the fade by listening.
- Linux: GCC/Clang Debug/Release builds and existing CTest groups, plus portable
  Audio deterministic/lifecycle checks. Keep absent-device results explicit.
  Physical-device playback remains pending until the Linux machine is available.

Automated signal/lifecycle checks must be useful without audio hardware. A passing
null/fake-device exercise demonstrates scheduling and cleanup only. Real backend
initialisation and submitted frames demonstrate an output path; manual listening
establishes that the expected tone/fade was actually audible.

## Readiness for timed-wait work

No further Audio behaviour or scope decision is required before moving to the
framework wait work. The tone/envelope, ownership, sequential Executive flow,
backend selection, no-device continuation, cancellation and deadlines are settled.

After the separate timed-wait work, return to this design to confirm:

- The actual timed predicate API, result handling, clock semantics and supported
  platform validation, using the handoff requirements above.
- The proposed 20 ms active progress-check interval and its integration with the
  15-second overall and 2-second no-progress deadlines. Idle waiting stays
  indefinite, and the callback does not perform an OS wake.

Native buffering and the adequacy of the agreed 250 ms trailing silence remain
playback-validation considerations. Sample-boundary conventions, allocator metadata
and notification mapping remain implementation details to verify within this
scope. Physical Linux playback validation remains dependent on the Linux machine
being available. These do not require broader Audio functionality or prevent
starting the independent framework work.

This checkpoint does not authorise Audio implementation or claim that timed-wait
support has been added. Complete that prerequisite, then reassess here before
starting Audio code changes.

## Documentation review record

Reviewed current binding/service lifecycle, generic test routing, Executive
completion/deadline handling, memory callbacks, wait primitives, the pinned
miniaudio example/API and build/policy integration points. This pass changes
documentation only. It preserves the separately drafted Platform plan. No Audio
code, dependency, project or runtime configuration was changed, and no build or
audio playback test was run for this document.
