# Real Host/consumer input acceptance

Implemented and validated 10 November 2026 following approval of this contract.
This integration and measurement pass preserves the public input contract. Its
private fixture controls and trace hooks are confined to the acceptance build.

The [desktop](desktop_input.md) and [controller](controller_input.md) records cover
Windows physical behaviour in a fixture that directly pumps Platform. This record
adds the actual Host scheduler, module boundaries and independently scheduled
consumer, including deliberate queue pressure. Latency measurements establish a
software-path baseline; they do not measure physical actuation or display latency.

## Execution and ownership

Use the existing `tests/host_pump` real-module fixture pattern in an isolated test
root. Run the actual engine/Host and load Platform normally. A fixture Executive
acts as an ordinary module consumer on its own service thread. It does not call
`CPlatformRuntime::pump`, sleep on the Host thread or submit arbitrary worker jobs.
Host workers retain their nominated schema-conditioning/binding responsibilities;
batch-runner fallback and Host scheduling policy are unchanged.

Publish/acquire the fixture schema asset through normal Host services, install it
through Platform, then acquire the window, controller catalogue, desktop connection
and controller pipes through public requests. Retain the source asset through
installation completion. Use `CServiceBorrowClient` and `CInputReader` for prepared
replies, notification registration, readiness and initial/subsequent boundaries.
Exercise one desktop connection and up to four independently assigned controller
pipes. A focused second-consumer case checks distinct ownership and retirement;
one designated reader owns each connection throughout its lifetime.

The consumer services lifecycle/control replies between bounded, fair input-drain
rounds, consumes cancellation before applying affected state and revalidates each
event immediately before application. Use the existing epoch/latch/drain/recheck/
wait discipline. Reader `progress` means more bounded work; `retry` is not failure,
and `empty` does not itself establish that every source is idle. Capacity returns
and boundary acknowledgements use the existing paths.

Close readers and stop endpoint/notification access before reporting generic
quiescence. Retire input before its dependent window and configuration, then unload
the provider through normal Host cleanup. Test timeouts report failure and preserve
unsafe resources; they never authorise forced reclamation or a successful-cleanup
claim after killing the process.

## Source modes and instrumentation boundary

1. **Native reference pass:** use the ordinary Windows Platform module, real
   keyboard/mouse messages and GameInput hardware. Repeat a short controls,
   context/focus recovery and disconnect/reassignment pass through the real
   consumer. Separately labelled posted-message cases can exercise Windows
   queue readiness; they are not physical keyboard or controller evidence.
2. **Deterministic pressure pass:** build an acceptance-only Platform variant from
   the same runtime/mapping/connection sources, with a private synthetic source
   adapter and bounded trace observer. Host still invokes the loaded module through
   the real pump ABI. Inject at the source/ingress boundary, never by writing action
   rings directly. Use production capacities, work budgets and conditioning.

The synthetic source is explicitly enabled only in the fixture build; it is not a
fallback for unavailable native input. Its bounded feed retains source identity,
order and timestamps, reports feed exhaustion/overflow, and is controlled by the
fixture service thread. The controller adapter retains truthful complete current
state independently of queued history, so history loss still permits a valid
neutral/recovery baseline. Source-feed loss must not masquerade as output-ring loss.
Trace hooks may also observe native sources in the acceptance build; compare that
run with the ordinary module and with tracing disabled.

Controller readiness does not gain an artificial wake per report. The polling
case makes reports available for normal Host pumping, recording the actual feed
handoff time separately from the intended schedule. Test legitimate early wakes
separately: Windows native-queue activity, service commands and capacity returns.
Keep the [anchored 120 Hz cadence](host_timed_wait.md) and skip-overrun policy intact;
8.333 ms is neither an input deadline nor a guaranteed response bound.

No public service opcode, descriptor or 48/40-byte record layout changes are
introduced. The private hook has explicit ownership, bounded work and shutdown
quiescence. Use existing containers for preallocated trace storage. Production
builds expose no input-injection control surface.

## Queue-pressure scenarios

Keep 256 usable action records per connection and 64 text records on desktop.
Measure actual source reports and published records separately: one report can
produce many actions, no action, or coalesced state. Use non-coalescing pulses,
alternating state changes and known synthetic text when an exact fill is required.
Inspect occupancy only through an authorised endpoint or private observer, without
adding a competing consumer. Concurrent occupancy is a sample, not a reservation.

| Case | Proposed exercise and required observation |
| --- | --- |
| Normal operation | Sparse traffic, then sustained synthetic rates of 120, 500 and 1,000 reports/s per active controller, first alone and then together with desktop traffic. These are test loads, not claims about hardware rates. Record achieved rates, fair progress and any recovery. |
| Capacity boundaries | With reads gated, fill each output stream to one below capacity, capacity and a confirmed further publication attempt. Start from an acknowledged, drained boundary; account for markers separately. Observe actual occupancy and failure paths rather than assuming a timed pause filled a ring. |
| Slow consumer | Withhold data reads for a selected connection for nominal 25, 100 and 500 ms while servicing lifecycle/control and other connections. Then stall all data reads briefly. Record actual pause durations and ensure blocked reads do not create a busy-wait loop. |
| Recovery | Resume reads after confirmed output overflow. Observe capacity-return wake, durable loss/cancellation, current boundary acknowledgement and per-binding neutral rearming. Expect cancellation and recovery, not lossless replay of overflowed pulses or deltas. |
| Work pressure | Use a finite high-expansion mapping profile and bounded ordinary service traffic. Check mapper continuation, Host/service progress and fair progress of quiet pipes beside a noisy or blocked pipe. Keep conditioning work within existing nominated abilities. |
| Transitions under pressure | Context/text-target change, focus/modal suppression, device removal/reassignment and consumer release/replacement while data or boundary publication is blocked. Verify affected scopes and reject stale generations/tokens; do not demand every superseded boundary be delivered. |
| Retirement | Release one pipe while others continue, then stop with unread data and pending acknowledgements. Verify exact ownership, completed cleanup obligations and absence of callbacks/signals after retirement. No ring drain is required merely to retire it. |

Separate native/source-history loss, desktop-ingress overflow, output-ring overflow
and trace overflow. Generic loss counters are not exact counts of physical events
that the user lost. Record offered/admitted/published/read/applied counts, legitimate
coalescing, suppression, cancellation and stale discards with their respective units.
During a deliberate stall the consumer must still be able to observe closing and
perform safe cleanup; it need not consume a queued marker until data reads resume.

## Latency measurements

Use the existing `platform::system::CPerfCounter` and `CPerfCountConversion` for
all new performance samples, in one process and one validated counter domain.
Record counter conversion/frequency and reject failed samples or backwards deltas.
Use awake time only for the existing scheduling/deadline rules and diagnostics;
awake deadlines explicitly are not performance timestamps. Exclude runs interrupted
by suspend or timing discontinuity from baseline statistics, with the reason retained.

| Timestamp | Meaning |
| --- | --- |
| `offered` | Sample immediately before making an accepted synthetic feed item visible to Platform; failed admission is a separate outcome. This includes feed-publication overhead and avoids racing ingestion. Record intended due time separately so generator lateness is visible. Native physical passes have no equivalent hardware-actuation timestamp. |
| `ingress` | Platform first accepts the source observation: desktop native ingress, or a controller reading obtained from its adapter. Assign a private observation identity carried through pending mapping. |
| `publication_start` | Immediately before making that output sequence visible through the committed watermark. This sample precedes consumer visibility and includes subsequent publication/notification work. |
| `read` | The designated reader returns that record to its consumer. |
| `applied` | After final authority validation, the consumer applies the record to its test action/text state. Cancelled or stale records have an outcome instead of an application sample. |

Report offered-to-ingress for synthetic feeds, ingress-to-publication-start,
publication-start-to-read, read-to-applied and ingress-to-applied. For synthetic
traces also report offered-to-applied. The publication-start metric includes
publication/wake work; it is not an exact measurement of ring residence alone.
Sampling after publication returns is invalid for this purpose: a consumer may
already have read/applied the record before the producer returns.

Join traces after the run using provider instance, connection token, stream,
generation and sequence. An internal observation ID/ordinal links one source report
to zero or more outputs. Recovery markers and cancellation outputs have explicit
causes and are not fabricated as physical samples. Neither equal source timestamps
nor the latest `receipt_awake_ns` snapshot identifies a particular historical action.
GameInput timestamps retain their own opaque domain/epoch and must never be
subtracted from performance-counter or Host awake timestamps. Cross-clock native
source-age calibration is outside this initial pass.

Each writer owns a preallocated bounded trace buffer. No per-event allocation,
file/console output or cross-thread trace reader runs in the measured path. Join
and format after writers quiesce; do not place an unowned instrumentation pointer
in an endpoint. Trace overflow, missing correlations and invalid clocks explicitly
invalidate the affected measurement window; keep the diagnostic counts and never
replace missing times with zero. Functional observations may still be reported
separately when timing evidence is incomplete.

For the initial Windows x64 Release baseline, use two seconds of warm-up and
three ten-second measured windows per selected load. Report sample count,
p50/p95/p99/max, achieved rates, queue high-water marks, losses/recoveries,
Host-invoked Platform pump duration/start gaps and consumer wait/read activity.
Identify build, hardware, source
mode, pipe count, worker count, load and trace configuration. Compare tracing enabled
and disabled for overhead; logging and the 60 Hz observation display stay outside
timed baseline runs. No automatic subtraction of estimated instrumentation cost.

## Acceptance and implementation sequence

Functional pass/fail covers ordering within each connection, no duplicate or stale
application, cancellation before further affected application, stream/pipe
isolation, wake/recheck correctness and safe retirement. A finite below-capacity
trace must finish with its expected outputs; an overflow trace must finish in the
expected recovered state after fresh neutral/input. A busy or full pipe must not
prevent other ready work from progressing. Bounded test watchdogs diagnose stalls
without becoming latency promises or shortcuts around lifetime obligations.

Timing results establish a reviewable baseline. Do not introduce a hard percentile
threshold before collecting it, or treat a correct manual display as evidence of
button-to-photon latency. Unexpected long tails, growing backlog or polling while
blocked require investigation before declaring the integration pass complete.

Implementation and acceptance order:

1. Implement the real consumer lifecycle and native Windows smoke, including idle
   waiting and normal retirement. Keep the production Executive's behaviour intact.
2. Add the acceptance-only source/trace hooks and deterministic queue/recovery cases.
   Use Windows x64 Debug first, then focused Win32 correctness and Linux synthetic
   controller/lifetime coverage. Linux results do not imply native GUI/controller
   support; native desktop cases remain Windows-only.
3. Exercise the relevant scenarios with one and two Host workers, using the existing
   fixture matrix conventions. Broaden validation only for changed surfaces or
   failures, not by repeating unrelated passing suites.
4. Collect the x64 Release baseline and a short real-hardware pass, then record
   results, limitations and any proposed scheduling/transport changes for review.
   No transport replacement, capacity tuning or new latency guarantee is pre-approved.

Wireless,
additional native platforms, window-mode shortcuts, IME/relative mouse, Steam Input
and advanced controller features retain their separate scope decisions.

## Fixture implementation

`tests/input_host` contains the independently scheduled Executive consumer, a
second consumer mounted as Rendering, the private Platform adapter and the runner.
Platform runtime files and filters belong directly to the ordinary module's
Visual Studio project. The acceptance project mirrors its runtime source/header
entries; a focused check compares both lists and their filters before acceptance
runs and Linux configuration. Only that variant defines `MV_INPUT_ACCEPTANCE`;
the production module has no private control contract, synthetic adapter or trace
fields.

The private feed/trace descriptor is itself a Host-tracked borrow. The Executive
is its sole feed producer and Host its sole feed consumer and trace writer. A
separate atomic complete-state snapshot per synthetic controller remains available
when queued history is lost. Device instances and source observation identities
are monotonic within a fixture run. The synthetic source clock is deliberately
artificial; all latency arithmetic uses performance-counter samples instead.

Before a latency run freezes its Host writer, a published drain frontier proves
the finite feed, source history, desktop ingress and mapper continuations have
finished. The designated readers must also have reached empty with no retained
validation retry. Synthetic measurement cohorts are selected by offer time, so
later ingress, publication and application remain included after the offer window
ends. The join checks both reader-to-publication and publication-to-reader
correlation for these lossless windows.

The runner preserves the isolated runtime, logs and a `report.json` containing
window counts, nanosecond percentiles, generator lateness, queue high-water marks
and separate source/output/ingress losses. Native observation windows require an
interactive Windows desktop and user confirmation; automated completion alone is
not physical acceptance. Their display updates are excluded from synthetic timing
baselines.

The fixture's [running instructions](../../tests/input_host/README.md) list each
case and the native/synthetic commands. Cleanup also retains late successful
source-asset transfer/acquisition results before releasing and disposing them;
pending replies do not make their conveyed ownership disappear.

## Deferred observation cleanup

Reviewed with the user on 10 November 2026: retain the current acceptance support
for this stage. Revisit its internal boundaries when the first real Executive/game
consumer is integrated, using that consumer's diagnostic needs to guide the work.
This is a deferred review, not an approved implementation or a general profiling
framework design.

Keep the real Host/consumer lifecycle, pressure, recovery, isolation and retirement
coverage as lasting regression tests. Retain narrow compile-time observation points
where timing semantics require them, particularly source ingress and publication
immediately before the committed watermark becomes visible. Synthetic sources,
test commands, trace storage and reporting should remain owned by the fixture.

The main cleanup candidate is `SInputAcceptanceAccess` in
`tests/input_host/platform_fixture.cpp`. It currently reconstructs drainage,
readiness and observation attribution from private desktop, connection and mapper
fields. Review whether public behaviour can replace each access without weakening
coverage. Where internal observation remains necessary, prefer small, explicitly
defined acceptance-only snapshots or queries owned by the relevant component over
duplicating the meaning of its individual recovery flags. Do not widen the public
input interface merely to replace a friend declaration.

Also review separating fixture control/borrow lifetime handling from observation,
and concentrating fixture registration/composition at the module-binding boundary
where practical. Preserve bounded work, precise timestamp placement and explicit
quiescence throughout any cleanup. A general observer framework or runtime callback
cost needs a separate concrete justification; hiding conditional compilation alone
is not a sufficient reason to introduce either.

The instrumented variant has additional private fields and work. Disabling tracing
retains fixture controls, counters and those layouts; it does not produce the
ordinary module. Continue ordinary-module physical acceptance alongside synthetic
measurement and preserve this distinction in future performance claims. Any cleanup
must retain the behavioural coverage and trace validity checks before replacing
the existing support. The broader
[testing-framework discussion](../backlog/testing_framework_consolidation.md#intrusive-tests-and-access-to-internals)
remains a separate review.

## Acceptance record: 10 November 2026

| Target | Evidence |
| --- | --- |
| Windows x64 Debug | All ten fixture cases passed with one and two Host workers. The ordinary portable input suite passed 46,278 checks with zero failures after rebuilding against the production runtime. |
| Windows Win32 Debug | Two-worker lifecycle, pressure, acquisition cleanup, source cleanup, second consumer and blocked shutdown passed. |
| Linux GCC Debug | The original nine-case matrix passed with one and two workers (18 cases). After late-asset cleanup was added, lifecycle, acquisition cleanup, source cleanup and load smoke passed with both counts (8 focused cases). Native Linux desktop/controller acquisition is not claimed. |
| Production Windows module | The real Host loaded the ordinary Platform module for an Xbox USB/keyboard observation pass. The operator requested a retry, then confirmed the two-minute retry performed as expected: prompt keyboard/controller input and held A/stick suppression across Alt+Tab and context changes until release/centre. |
| Repository checks | Policy validation reported zero errors/warnings and the existing negative-test suppression. Eight source-manifest tests passed. Tracked/new UTF-8 text, line endings and diff whitespace were checked. |

The hardware retry's local record is `build/input-host/lifecycle-1-knxyg25u`.
It establishes qualitative behaviour through the ordinary loaded module; it
does not provide a physical actuation timestamp or button-to-photon measurement.

All new synthetic loads run through the Host cadence and consumer notifications;
the fixture does not wake Host for each offered report. Expected-output counts
are checked independently of trace correlation. Overload cases intentionally
cause loss and then require cancellation/recovery; below-capacity load runs
require complete output and zero source, ingress or output loss.

Latency percentiles are weighted by output records, not independent physical
samples: a controller report's button and stick changes each contribute a sample.
The CPU field covers the entire process, including setup, warm-up, drain, trace
sorting/reporting and teardown. It supports a coarse tracing comparison but
does not isolate per-event overhead. Timing runs must remain uninterrupted. The
performance/awake-clock guard detects Windows suspend divergence; both Linux
clocks exclude suspend, so the same guard cannot detect Linux suspension.

The report names `pump_work` and `pump_gap` measure the duration and start-to-start
gap of `CPlatformRuntime::pump` calls. They do not measure the whole Host iteration,
total Host scheduler work or time spent in unrelated module/worker activity.

The fixture Executive both generates source reports and applies input. Its own
descheduling/work can delay the offer; overdue rounds can catch up in bursts.
Offered-to-applied starts at actual handoff and excludes pre-offer generator
lateness, which is reported separately. Four-controller runs emit all four reports
in each generator round, rather than simulating independently phased devices.
`SOURCE.feed/history` count private synthetic buffer overflow; public per-pipe
`ingress_loss` records native/injected source-history recovery. The private history
counter is not a GameInput lost-history measurement.

## Windows x64 Release baseline

Collected serially on 10 November 2026, Windows 11 build 26200, AMD Ryzen
Threadripper 7980X (64 cores / 128 logical processors), MSVC v143 and Windows SDK
10.0.26100.0. The performance counter frequency was 10,000,000 ticks/s. Each run
used two seconds of warm-up followed by three ten-second windows, without
concurrent builds or other acceptance runs. All source and consumers were in one
process. This is synthetic report-to-application timing, not hardware latency.

The ranges below are the minimum and maximum **per-window p99**, not pooled
percentiles. Maximum is the largest individual sample across all three windows.
[The retained CSV](input_host_latency_2026-11-10.csv) contains sample counts and
p50/p95/p99/max for every stage/window, including Platform pump metrics; all
values there are nanoseconds. Full local reports and logs remain under
`build/input-host-baseline/<run>/`.

| Controllers | Reports/s each | Workers | Applied samples | Achieved reports/s each | Offered-to-applied p99 (ms) | Maximum (ms) | Ingress-to-applied p99 (ms) | Action high-water |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 120 | 1 | 7,425 | 119.988–119.995 | 4.085–5.194 | 5.828 | 0.185–0.191 | 2 |
| 1 | 500 | 1 | 30,937 | 499.939–499.997 | 7.621–7.770 | 9.518 | 0.175–0.177 | 6 |
| 1 | 1000 | 1 | 61,873 | 999.815–1000.000 | 7.634–7.706 | 13.585 | 0.174–0.175 | 6 |
| 4 | 120 | 1 | 29,025 | 119.977–119.994 | 3.658–6.148 | 6.726 | 0.206–0.209 | 2 |
| 4 | 500 | 1 | 120,937 | 499.958–499.997 | 7.687–7.707 | 11.775 | 0.197–0.199 | 6 |
| 4 | 1000 | 1 | 241,867 | 999.812–999.875 | 7.576–7.711 | 13.303 | 0.188–0.191 | 6 |
| 4 | 1000 | 2 | 241,859 | 999.864–999.958 | 7.609–7.626 | 13.856 | 0.189–0.191 | 6 |

All nine runs passed, including the two trace-disabled comparisons. Every measured
window had its expected applied outputs, with zero missing correlations, invalid
clocks, stale applications or source/ingress/output losses. Traced action high-water
never exceeded six records; trace-disabled runs reached at most seven, against
capacity 256. Each pipe had only its initial cancellation. All finite loads drained
completely. The measured rates and counters show no sustained backlog.

The largest offered-to-applied sample was 13.856 ms in the two-worker run
`load-2-wdd1xmg5`, window 0. That window's offered-to-ingress maximum was 13.684 ms;
ingress-to-applied maximum was 0.679 ms. This localizes the long tail before ingress.
The largest Platform pump start gap in that window was 9.775 ms, so the tail cannot
be attributed simply to one long Host sleep. Polling/feed handoff and scheduling
variability are plausible contributors, but the retained summaries do not identify
the exact cause of that individual sample. The 120 Hz cadence is not an upper
latency bound. No transport, capacity or scheduling change is justified by this
initial evidence alone.

Whole-process CPU comparison at four controllers × 1,000 reports/s each:

| Workers | Tracing | Process CPU (s) | Wall duration (s) | Local run |
| --- | --- | --- | --- | --- |
| 1 | on | 4.718750 | 32.309 | `load-1-hyl47so8` |
| 1 | off | 4.859375 | 32.166 | `load-1-hhzyy5v5` |
| 2 | on | 5.421875 | 32.321 | `load-2-wdd1xmg5` |
| 2 | off | 4.312500 | 32.164 | `load-2-hovdzrjo` |

These single sequential process totals do not establish a precise tracing overhead:
one worker used less measured CPU with tracing on than off, while two workers
showed the opposite. They include offline reporting and ordinary scheduling
variation; no instrumentation cost is subtracted from latency.

The first Release smoke used an older Host executable and exited with an access
violation before producing measurements. Rebuilding Host against the same checkout
resolved it; both trace-on and trace-off smoke checks then passed before collecting
this baseline. That failed run is excluded. The subsequent reporting-only change
preserves detailed clock/overflow/source/pipe diagnostics on failure and does not
change sampling or the measured path. After that change, two-worker load smoke
passed on Windows x64 Debug/Release, Win32 Debug and Linux GCC Debug; the emitted
diagnostic counters were zero.
