# Proposed real Host/consumer input acceptance

Draft for review, 10 November 2026. This is a proposed integration and measurement
pass, not an approved implementation or a new public input contract. Documentation
reconciliation does not authorise its test hooks or runtime changes.

The [desktop](desktop_input.md) and [controller](controller_input.md) records cover
Windows physical behaviour. Their current interactive fixture directly pumps
Platform. The next question is whether the same input contracts behave correctly
with the actual Host scheduler, module boundaries and independently scheduled
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
proposed. The private hook must have explicit ownership, bounded work and shutdown
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

For the initial Windows x64 Release baseline, propose two seconds of warm-up and
three ten-second measured windows per selected load. Report sample count,
p50/p95/p99/max, achieved rates, queue high-water marks, losses/recoveries, Host
pump gaps/work and consumer wait/read activity. Identify build, hardware, source
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

After approval, the proposed order is:

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

Review requested: the fixture consumer and private acceptance build, the queue
pressure cases, and the proposed measurement boundaries/reporting. Wireless,
additional native platforms, window-mode shortcuts, IME/relative mouse, Steam Input
and advanced controller features retain their separate scope decisions.
