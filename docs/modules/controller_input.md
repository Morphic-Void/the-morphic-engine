# Controller acquisition

Contract accepted and implementation authorised 9 November 2026. This extends
[input connections](input_connections.md), [mapping](input_mapping.md) and
[desktop acquisition](desktop_input.md). The earlier single-controller limit is
superseded by four independently owned controller connections. Physical acceptance
is separate from implementation and synthetic evidence.

## Ownership and availability

Platform owns one desktop connection and up to four controller connections. Each
controller connection is a window-independent logical slot with its own mapper,
256-record action ring, notification, context, generations and watermark. One
consumer may own several slots. A device has at most one claimant; no connection
implicitly combines devices or transfers another connection's ownership.

Preparation and retirement count against capacity. Discovery does not allocate
pipes. An independent, nonexclusive catalogue subscription uses ordinary Host
borrow/notification protection and five-second eligible readiness. It has no
action ring or boundary handshake. Subscription success does not imply a usable
backend or an attached controller. Backend initialising, ready, unavailable and
failed are explicit states; ready with no attached devices is valid.

Retain at most 16 device entries. Tokens identify observed connection instances
within the provider lifetime; they never use native object addresses, model names,
USB positions or player indices as persistent identity. Reconnection gets a new
token even if the native API reuses its device object. Metadata is descriptive.
Overflow is durable and cannot evict an assigned device. Untracked devices are not
assignable. Discovery/history uncertainty invalidates affected identity rather
than silently inheriting an old assignment.

Catalogue enumeration is revision/cursor based and returns one bounded record per
query. Stale revisions require restart; verify the final revision before accepting
a complete scan. Queries return copies, never pointers into mutable native lists.
Notifications may coalesce; coherent state and its revision remain readable. Claim
changes also revise availability. Metadata is retrieved in bounded pages where it
does not fit the service reply. Completion results are retained unchanged under
delivery pressure.

## Acquisition and assignment

Selection is exact-device, default-available or explicitly unassigned. Default
chooses the first eligible unclaimed device by stable session discovery order.
With no eligible unclaimed device it creates an unassigned slot; no later arrival
automatically fills it. Exact stale, unsupported or claimed devices fail explicitly.
Reserve claims during preparation and release failed reservations through cleanup.

Use the existing input reader readiness and initial boundary handshake. An
unassigned slot can become ready; action publication additionally requires a current
assignment, native baseline, application eligibility and cleared barriers.

Assignment requests authenticate the exact consumer and carry the expected action
generation, which fences both assignment and context changes. The 24-byte request
contains connection, target device and expected generation. Invalid requests preserve the current assignment. Accepted
reassignment/unassignment cancels old input and crosses an action boundary before
new input can apply. Context/assignment transitions are serialised per slot. There
is no implicit stealing or atomic exchange between occupied slots.

Disconnect cancels only the affected slot, publishes unassigned/disconnected state,
and preserves its context and borrow. Reconnection is a new instance. The consumer
must explicitly request reassignment; Executive may implement policy using those
requests. Focus/modal suppression preserves claims. Text focus does not suppress
controller actions.

## Observations, time and ordering

Read complete standard gamepad states: buttons, two Cartesian sticks and independent
triggers. Apply a report coherently before mapping. Reuse portable conditioning and
the agreed dead zones, hysteresis, clamping and neutral detection. Do not infer an
ordering among changes contained in one report.

Walk source history in order, retaining an unfinished observation until mapping
finishes or recovery invalidates it. Equal timestamps do not identify duplicates.
Use source reading identity/order and a checked local ordinal. Each pipe has its
own publication sequence/watermark; there is no total order between pipes or with
desktop input. Consumer aggregation is explicit policy.

Controller action time is source time in nanoseconds, with an opaque clock-domain
identity and continuity epoch bound to the action generation. Domain identities
are scoped to the provider instance lifetime; compare times only within the same
provider, domain and epoch. Convert native units
with overflow checks. GameInput microseconds are not Host awake time. Keep receipt
awake time separately in retained observations/diagnostics; do not fabricate source
time or subtract unrelated clocks. Reader-returned events capture validated source
identity/clock metadata. A latest snapshot is not historical per-event metadata.

Lifecycle markers without an originating observation use zero time and an explicit
no-source-time flag. Missing source time is explicit too. Assignment or clock reset
invalidates old timing continuity behind a boundary; never hide it by clamping the
new time to an old epoch. Preserve 48-byte action framing. Exact per-action receipt
time is not added in this increment. Input descriptors and service use a coordinated
new contract version; configuration source format remains unchanged.

## Recovery, work and retirement

Disconnect, reassignment, lost history, invalid samples and broken clock continuity
cancel without normal-release pulses. Recovery needs a fresh trustworthy baseline
and current marker acknowledgement. Held controls must return to neutral before
reactivation. Lost pulses are not reconstructed. On focus/modal return use a fresh
baseline rather than replaying suppressed history. Queue-full conditions cannot
erase durable loss or device state. Local faults affect one pipe; backend-wide
failure/shared-clock reset can invalidate several.

Host owns acquisition/mapping. Native callbacks perform bounded bookkeeping only;
they do not run mappers or publish records. Keep the two-millisecond Platform
allowance, at most 64 controller readings per pump, and globally shared mapper
limits of 500 microseconds, 2,048 credits and 64 output attempts. Rotate devices,
connections and work classes with retained cursors. A blocked/noisy controller
cannot monopolise the pump. Discovery also progresses in bounded slices. No
unbounded backlog or new Host worker jobs are introduced.

Release closes the logical slot; disconnect alone does not. Closure invalidates
retained input and stops publication. The consumer ends all endpoint/latch access
before generic quiescence. Retire at an outer-pump safe point after Host borrow
obligations and native references are resolved. Catalogue subscriptions retire
independently. Callback unregistration must establish quiescence before freeing
context. Timeouts and disconnection do not prove safety. Backend shutdown stops
callbacks, retires affected pipes and releases native references before unloading.

## Windows and acceptance

Use pinned Microsoft.GameInput 4.0.306 behind the private native adapter. Validate
x64 and Win32 and stage the matching redistributable binaries; keep vendor headers
and binaries in ignored build dependency storage. Linux retains portable tests and
an explicitly unavailable native backend. No other backend is silently substituted.

One `TInstance<SGameInput>` owns the shared runtime, dispatcher and stable callback
context. `TOrderedSlots<SDevice, SDeviceKey>` owns movable device records keyed by
checked monotonic instance IDs. Slot reuse never reuses a device identity. Each
record owns its device and reading references; move transfers ownership and erase
releases those references. The slot container's minimum reservation of 32 does not
change the 16-device admission limit. Allocate its backing before registering native
callbacks. Shutdown unregisters callbacks before releasing queued references, device
records, dispatcher and runtime, then releases the shared owner. Allocation and
object lifetime use the existing containers without placement-new policy exceptions.

An expired GameInput history cursor does not imply loss when it is still the
device's current reading. On that error, compare the retained cursor with one
current-reading query by interface identity. An identical reading means no new
input; leave the ordinal and recovery generation unchanged. A distinct reading
still requires history-loss recovery, even if its timestamp and values match.

The app-local PC runtime is sideloaded. Microsoft documents that this deployment
ignores `EnableBackgroundInput(true)` and has limited, device-dependent focus
handling. Calling that method is therefore not proof that a current reading
contains physical state while unfocused or immediately after returning.

Freshness protection accepted 10 November 2026: on an aggregate focus/modal return,
capture the GameInput source clock synchronously, including a loss and return
between pumps. Each device must provide a reading strictly newer than that cutoff
before its pending recovery baseline can complete. Older or equal-time readings
are released without advancing the cursor, ordinal or recovery generation. The
first accepted report establishes suppression; it does not publish actions. Each
binding then rearms through its own neutral threshold, independently of other
controls and pipes. Delayed discovery inherits the latest return cutoff. Clock
rollback changes the source epoch and rebases still-pending cutoffs; retirement
never waits for physical input.

For an idle device that reports only changes, the first deliberate press after
return may establish the baseline and require release/repress. No timeout, repeated
read of the same snapshot, or neutral value bypasses the timestamp requirement. This
guard excludes the observed cached pre-return neutral state; a timestamp alone
cannot distinguish physical input from a synthetic report stamped after return.
Native acceptance must therefore include actual held controls and movement after
refocus. Startup order and a system-wide runtime installation are not prerequisites
introduced by this change.

Synthetic cases cover catalogue/claim races, distinct identical devices, all three
selection policies, capacity, shared-budget fairness, time ties/resets, stale
assignment/context requests, loss, held-state recovery, independent cancellation,
and catalogue/pipe shutdown under pending delivery and late quiescence.
Windows ownership cases additionally cover device/reading move and release,
monotonic keys across slot reuse, queued discovery reference retirement, and
rollback at each of the four startup allocations.

Native acceptance first exercises no-device startup and runtime availability. Then
use both Xbox controllers together with independent contexts and disconnect/reconnect
checks. Probe the PS5 controller through the same standard gamepad interface and
record actual compatibility. USB is the initial path; wireless is separate evidence.
Touchpad, gyro, adaptive triggers, haptics and vendor-specific extensions are deferred.
Hardware results remain pending until actually observed; charging devices do not
block implementation or synthetic validation.

### Public interface

Input service 2/version 4 and `SInputEndpointV4` supersede the desktop increment's
version 3. All participants require matching headers and a coordinated rebuild.
The catalogue has its own `SControllerCatalogueV1` borrowed descriptor, with up to
eight concurrent subscriptions. The consumer verifies descriptor size/version,
provider/consumer identity and state pointer before sending `catalogue_ready`.

| Command | Request / result |
| --- | --- |
| `acquire_catalogue` | Empty request; prepared catalogue borrow, followed by readiness and terminal completion. |
| `catalogue_ready` | Borrow, notification registration and original acquisition request ID. |
| `enumerate_device` | Borrow, expected catalogue revision and cursor (initially zero); one copied device/flags/next-cursor or end marker. |
| `device_metadata` | Borrow, current device token and page 0–7; vendor/product and eight display-name bytes per page. UTF-8 names are bounded to 63 bytes plus terminator. |
| `acquire_controller` | Installed configuration/profile/context and selection: zero for default, `UINT64_MAX` for unassigned, otherwise exact session token. |
| `assign_device` | Connection token, selection and expected action generation. |

Catalogue revision includes claims. A stale enumeration restarts from a newly read
revision; metadata pages must use the same live device token. Backend errors and
catalogue overflow are durable. Per-pipe state distinguishes unassigned, assigned,
disconnected, unavailable and invalid sample. Receipt awake time is the latest
observed report diagnostic, including unchanged reports, not per-action timing.

### Running the harness

Build normally with the pinned dependency described in
[building and testing](../project/building_and_testing.md#windows-controller-dependency).
`MorphicTests --platform-input` includes portable controller synthetic cases.
`MorphicTests --platform-controller-native` starts the real GameInput runtime,
enumerates the catalogue and opens/retires four pipes through the public protocol.
It accepts zero attached devices and prints backend status and any native error.

`MorphicTests --platform-controller-interactive` opens a 120-second observation
window. Its title targets 60 Hz refresh independently of the once-per-second
periodic console log; actual refresh depends on scheduling. Input acquisition and
pipe draining continue between display updates. Initial acquisition selects
available devices in discovery order; empty pipes stay unassigned. Keys 1–4
select the displayed pipe, C toggles its context,
U unassigns it, R explicitly assigns available devices to empty pipes, and Escape
finishes. The title shows the selected device, context, standard buttons, both
sticks and triggers, plus record counts for all pipes. The console reports source
time/domain/epoch and separate receipt time. Neutralise controls before starting.

Check each controller alone, then both Xbox controllers together and finally the
PS5 through the standard interface. Move every control and confirm independent
pipe counters/state. Change one context while another controller remains active.
Alt+Tab with a held control: returning must cancel old input and require neutral
before reactivation. Disconnect one active device: only its pipe cancels and stays
unassigned. Reconnect, use R to request reassignment, then verify the new token.
U followed by R exercises explicit release/reclaim. Record model, USB/wireless
transport and observed behaviour; a successful harness exit alone is not physical
acceptance. Haptics and controller-specific extensions are not exercised.

### Validation record: 9 November 2026

Windows Debug x64 and Win32 solution builds and the Linux Clang Debug build passed.
After the container ownership refactor, the focused suite reported 46,142 checks
on each Windows target, including 39 native ownership checks, and 46,103 portable
checks on Linux, all with zero failures. Windows x64 and Win32 native startup/pipe
smoke each reported 1,274 checks with zero failures, backend ready and zero devices
attached.
Policy validation and repository line-ending/diff checks passed.

The broader Windows x64 ordinary run found one overly strict timing assertion in
the new catalogue timeout test; all other ordinary suites passed. The test now
allows delivery of the retained terminal reply on the following pump, and the
corrected case passed in all three focused runs. Passing unrelated ordinary suites
were not repeated after that test-only correction.

As of 9 November, physical acceptance for the two Xbox controllers and PS5 was
pending; no controller model or wireless compatibility is inferred from the
zero-device run.

### CI follow-up: 10 November 2026

The operator confirmed that all Windows and Linux CI jobs passed after rerunning
the transient GitHub internal-server failures. This includes the Windows restore
module-path fix in `1e91586`; the controller implementation is `ad7e498`.

### USB physical acceptance: 10 November 2026

Controller A is connected by USB and Windows identifies it as an Xbox One
Controller (vendor `045E`, product `02EA`). Initial discovery completed after pipe
acquisition; explicit R assignment selected device token 1 in pipe 1.

The operator confirmed all 14 standard button masks and return to zero on release:
A/B/X/Y, both shoulders, both stick clicks, four D-pad directions, Menu and View.
Guide/Share are outside this mapping.
After the history correction, the operator also confirmed both stick directions,
return to centre, independent and simultaneous trigger travel, and return to zero.
That physical pass completed with 93,753 harness checks and zero failures.
The operator subsequently confirmed fresh A input after five seconds at neutral,
and context changes while holding A, a stick and a trigger: each cleared, stayed
suppressed while held, and reactivated only after neutral and fresh input. That
pass completed with 93,020 harness checks and zero failures.

One retry overflowed the acceptance fixture's 512-entry service-reply history.
A later diagnostic captured repeated native-history recovery generations. An
expired cursor could still be the latest reading; the adapter now distinguishes
that idle case by reading identity instead of repeatedly cancelling input. The
synthetic regression failed against the old implementation. After the correction,
Windows Debug x64 and Win32 builds and focused input suites passed, each reporting
46,166 checks with zero failures. The subsequent analog and idle/context passes
did not reproduce the recovery storm.

Initial focus recovery failed. With A held continuously and a stick
off-centre across Alt+Tab, a native baseline returned all-zero controls with a
source timestamp from focus loss. A later complete report included the still-held
A button, which the mapper then treated as a new press. Slight stick movement can
produce that report; a quiet return is not sufficient evidence of correct recovery.
The operator initially saw the failure on the first cycle but not a repeated
cycle, so acceptance must distinguish first and later cycles and include stick
movement while A remains held after return.
In the subsequent two-minute retry, the operator confirmed held-A reactivation on
both first and later cycles while moving the stick after return. The harness
completed with 182,351 checks and zero assertion failures; those checks do not
establish correct physical focus recovery.

The loaded runtime was the staged GameInput 4.0.306.0 DLL. Reapplying background
input policy on discovery and establishing the Windows message queue before
GameInput did not resolve the failure. Creating the window before GameInput
initially appeared to work, but a traced repeat again captured the false neutral
baseline and held-A reactivation, including on later focus cycles. That ordering
is not accepted as a fix and does not change the window-independent contract.
Temporary ordering changes and tracing were removed. The operator subsequently
approved the conservative freshness guard and its possible first-press suppression.
Windows Debug x64 and Win32 solution builds passed; their focused input suites each
reported 46,278 checks with zero failures. The Linux Clang Debug build and focused
suite passed with 46,191 checks and zero failures. Coverage includes pre-cutoff and
equal-time snapshots, independent devices, delayed discovery, return between pumps,
startup while ineligible, nested modal suppression, unavailable-backend transitions,
clock rollback and retirement while freshness remains pending. Line-ending and
diff checks passed, and read-only review found no remaining blocker.
With the guard enabled, the operator confirmed both first and repeated focus
cycles with A and a stick held throughout: moving the stick after return did not
reactivate either held control, and neutral followed by fresh input worked. The
physical pass completed with 92,183 harness checks and zero failures.

The operator also confirmed U/R unassignment and reassignment while controls were
held. R left them suppressed; releasing and pressing A again rearmed the button
while the off-centre stick stayed suppressed. Centring and moving the stick then
restored stick input. This confirms binding-local rearming rather than requiring
the whole controller to become neutral at once. The two-minute retry completed
with 181,975 checks and zero failures. The interactive controller harness now allows
120 seconds for multi-step checks and still supports early exit with Escape.
The operator confirmed USB disconnection while holding A cleared the pipe.
Reconnection remained unassigned until R, which selected new device token 2 in
place of token 1; fresh buttons, sticks and triggers then worked.

Controller B is connected alone by USB and Windows identifies it as Xbox
Controller (vendor `045E`, product `0B12`). The operator confirmed all 14 mapped
buttons and return to zero, both stick directions and return to centre, and both
triggers independently/together with return to zero.

In a new harness/provider run with both Xbox controllers connected by USB, R
assigned distinct device tokens 1 and 2 to pipes 1 and 2. The operator confirmed
each controller's buttons, sticks and triggers affected only its own pipe. The
isolation pass completed with 183,413 checks and zero failures.
The following pass confirmed simultaneous stick input on one controller and button
input on the other. It completed with 185,371 checks and zero failures. Context
isolation was confirmed separately in the requested retry.
In that retry, the operator confirmed changing pipe 1's context left pipe 2's
context and held A unchanged; releasing and pressing A again continued to work on
pipe 2. Context changes are therefore physically confirmed to remain local to the
selected pipe.
The operator confirmed disconnecting pipe 1's controller left pipe 2's assignment,
context and held input intact. Reconnection stayed unassigned until explicit R,
then received new token 3 and accepted fresh input.
For shared-focus recovery, the operator used analogue sticks on both controllers,
holding them across Alt+Tab and rearming them independently. Behavior appeared as
expected, but coordinating both sticks with one hand and the keyboard with the
other limited the precision of the physical check. Triggers were not used in this
focus pass; the earlier trigger travel/neutral checks are separate evidence.

The PS5 controller is connected alone by USB. Windows reports Wireless Controller
with vendor `054C`, product `0CE6`; the native harness successfully assigns it to
pipe 1. The operator confirmed all 14 standard button masks, including Options
and Create, with return to zero; both stick directions and return to centre; and
independent/simultaneous L2/R2 travel with return to zero.
The operator subsequently confirmed held-Cross recovery through both C context
change and Alt+Tab. After refocus, moving a stick while Cross remained held did
not reactivate Cross; releasing and pressing Cross again restored normal input.

With all three controllers connected by USB, the operator confirmed separate
controller pipes, individual input isolation and simultaneous input from the PS5
and an Xbox controller. Pipe 4 remained unassigned. Unplugging the PS5 left both
Xbox pipes working; reconnecting it required explicit R assignment, received a
new device token and restored working controls.

The planned USB acceptance passes are complete, with the shared-focus observation
qualified above. These physical results apply to the two identified Xbox models
and the PS5 controller on this Windows host using the x64 Debug harness. Win32
coverage includes builds, synthetic tests and zero-device native smoke; Linux
coverage is builds and synthetic tests. Connected-controller physical acceptance
is x64 only. Wireless operation was not tested; the PS5 device's Windows name does
not imply a wireless connection.

The operator noted apparent latency during acceptance. The controller title had
shared the once-per-second console log timer. Its refresh now independently targets
60 Hz while periodic logging stays at once per second. In the subsequent short
responsiveness pass, the operator confirmed that button presses, stick movement and
the trigger followed promptly in the display. This is a qualitative observation,
not an end-to-end latency measurement. The updated x64 Debug harness built cleanly;
read-only review and repository diff/line-ending checks passed.

Native API references:
[GameInput v4](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/gameinput-overview),
[reading lifetime and identity](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-fundamentals),
[background input policy](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/interfaces/igameinput/methods/igameinput_enablebackgroundinput?view=gdk-2604),
[sideloading limitations](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-agility-redistributable?view=gdk-2604),
[manual dispatch](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/interfaces/igameinputdispatcher/methods/igameinputdispatcher_dispatch),
[callback removal](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/interfaces/igameinput/methods/igameinput_unregistercallback).
