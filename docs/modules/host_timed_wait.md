# Host cadence and native event waiting

Implementation authorised 6 November 2026, following the direct POD connections.
The public contract is platform-neutral. Windows and the current Linux shell are
the initial implementation targets; Android and macOS native UI drivers remain
future work, with their requirements constraining this interface.

## Scheduling contract

Host retains scheduling policy: nominal 120 Hz housekeeping opportunities in awake
monotonic time, bounded fair work passes, operation deadlines and shutdown.
Cadence points are anchored to one origin. Early wakes preserve the next point;
overruns skip missed periods without catch-up passes. Suspend pauses awake time.
Timer precision, descheduling and native callback duration prevent any hard
8.333 ms response guarantee.

Host waits only after a complete pass and durable state/output checks. Exhausting
a drain budget requests another fair pass. Waiting for peer capacity, a borrow,
an asynchronous result or a deadline does not by itself prevent sleeping.
`SProgress::work_pending` means that another invocation can make progress now.
Native-event ownership is a separate registration, retained through stopping
until native resources and callbacks have relinquished it.

The coordinated pump ABI is `SHostPumpV2`, exported as
`morphic_module_host_pump_v2`. Legacy V1 exports are rejected. The
`k_host_pump_native_events` capability reserves exclusive thread-queue ownership
before activation; a second owner is rejected before invoking its callbacks.
`SProgress::native_events_active` enables native waiting while that owner can
service events. `stop_requested` is a durable application-stop request. Both
fields require the native-owner capability; ordinary providers do not gain
whole-Host stop authority. All three progress fields are Boolean values.

The wait is shortened to the earliest applicable Host-owned deadline. Existing
Host-eligible budgets retain restriction exclusions; a paused budget does not
become an expired native deadline. Ordinary idle sleep remains eligible time.
Every wait return rechecks work and time; wake reasons are hints, not exclusive
proof that a particular operation completed or expired.

## Aggregate wake and lifetime

One stable, non-owning endpoint accepts signals from all Host-relevant producers:
Host-bound POD/owner publication, initial thread readiness, termination/failure,
batch completion/quiescence, pumped-client notification, capacity returned to
Host output, and stop/debug shutdown requests. Direct data continues waking only
its participants. No payload or ownership travels in the wake itself.

Producers publish durable state before signalling. A coalesced pending word is
acknowledged once at the beginning of an owner cycle, before all durable checks.
On Windows the native event is reset before that acknowledgement. Nothing clears
the wake between the final checks and parking. A concurrent publication therefore
either participates in the acquired checks or leaves a durable pending wake.
Delayed native signals and spurious returns are harmless hints. Publication and
signal outcomes remain separate: a failed wake never authorises retransmission.

Create the target before publishing endpoints. Keep it alive through every final
signal and successful native join, including debug and batch publishers. Failed
joins retain the complete runtime graph. Release interrupts a waiter but does not
prove publisher quiescence. Setup errors fail startup; runtime clock/wait/wake
errors enter controlled failure cleanup, never a silent indefinite wait. The
existing nonblocking cleanup path remains available when waiting has failed.

## Native integration and portability

Low-level wait words keep their existing engine-only role. A separate owner wait
interface hides native handles and can integrate a native event loop. Windows
uses one aggregate event plus message-queue readiness; Linux currently uses its
existing timed wait-word backend because native window creation is unsupported.
No helper thread or Windows wake window is introduced.

Windows subscribes to native readiness only while a viable Platform pump owns
native event servicing. Selected thread-queue activity must be consumed or given
an explicit disposition under that owner's invocation/lifetime protection; an
unread thread message must not cause permanent ready spinning. Window/input
semantics remain in Platform. The existing native maintenance timer and modal
maintenance rules remain in force.

The Windows backend uses a manual-reset event and `MWMO_INPUTAVAILABLE` with
`QS_ALLINPUT`, so already-observed input remains visible. Platform drains the
whole owning thread queue under its invocation/restriction protection, including
sent callbacks dispatched by `PeekMessage`. `WM_QUIT` latches the Host stop
request; other windowless thread messages are explicitly consumed and discarded.
Window messages retain normal translation and dispatch. Every removed record
counts against the bounded count/time slice. A live idle window does not claim
immediately runnable work, and failed native teardown cannot leave a stale
pending flag driving an unbounded loop.

Future Linux GUI drivers must integrate both native descriptors and buffered
library events, including Wayland's prepare/read/cancel protocol. Android's
`ALooper_pollOnce`/wake and an AppKit-compatible macOS main-loop driver may execute
native callbacks while waiting. The common contract permits this under explicit
callback authority and module lifetime protection. An ordinary Host turn cannot
run recursively; work observed in a nested/modal loop stays pending. Protecting
callback lifetime must not place normal sleeping inside a Host restriction.
Native callback execution is not preempted by a wait timeout. Neither future UI
backend is claimed implemented or validated by this increment.

## Validation requirements

Cover coalesced and concurrent publication around acknowledgement and parking,
delayed native signals, deadline preservation, cadence overruns, capacity wakes,
idle providers, stop/terminal publication, and target lifetime. Exercise Windows
native activity and modal maintenance and Linux operation without a native UI.
Measure idle wait behaviour separately from deterministic correctness tests.

## Initial validation — 6 November 2026

- Windows Debug x64 and Linux Debug builds pass. Ordinary suites pass on both;
  the timed-wait suite includes 304 assertions covering the aggregate wake,
  publication/parking interleavings, release, adapters and cadence arithmetic.
- Real-module Host pump acceptance passes all 45 cases with one and two workers
  on each platform, including rejection of unauthorised native-stop/readiness
  claims. Linux policy, reflection-failure and both Host acceptance groups pass.
- Windows native-window acceptance passes 391 assertions, including bounded
  queue-wide thread-message draining and durable `WM_QUIT` translation. Real-Host
  move, resize and menu/modal acceptance also passes. Portable window tests cover
  idle/backlogged registration, stopping and failed teardown.
- An isolated Windows activation-timeout fixture records 622 Host turns and
  618 wait calls over approximately five seconds. Sampling the Host thread for
  3.02 seconds records 15.625 ms CPU time (about 0.52% of one core). This is a
  local observation, not a timing or performance guarantee; the fixture's
  Executive thread has its own polling behaviour.
- The former delayed-reply fixture counted 10,000 pump calls. It now uses a
  finite awake-time delay, retaining the shutdown-cleanup check without assuming
  a busy Host loop. Only the initially failing pump groups were rerun on Linux.

Native Linux UI, Android/macOS native drivers, and other build configurations
are outside this validation increment. Failed-join retention and final-publisher
lifetime were also reviewed; existing Host maintenance tests remain in the
ordinary suites. Source/manifest whitespace and line-ending checks pass.
