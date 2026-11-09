# Portable Platform input mapper

The [desktop acquisition increment](desktop_input.md) extends this evaluator with
grouped transitions, bounded raw packets before semantic output, native ingress
gates; [controller acquisition](controller_input.md) adds input service/endpoint
version 4. Record framing and configuration
format remain unchanged.

Contract accepted and portable implementation added 8 November 2026. This
document refines the mapping behaviour in the broader
[Platform design](platform_host_pumped_design.md), using the implemented
[configuration installer](input_configuration_installation.md) and
[input connection protocol](input_connections.md). It records approved behaviour,
with a coordinated input service revision. Native Windows acquisition is described
in the desktop increment above.

## Ownership and preparation

Each input connection owns a portable, stateful mapper. Native adapters supply
normalized observations; the mapper evaluates installed bindings and publishes
semantic actions through the existing connection. It contains no native API calls.

The installed configuration owns immutable lookup and evaluation tables, built
incrementally during installation. Each connection owns its physical baseline,
per-binding activation and hysteresis state, and aggregate action state. Prepare
required mutable storage before connection readiness; normal evaluation allocates
no memory. The existing installation reference protects the tables through
connection retirement.

Mapping runs under Platform's Host-pumped execution. It introduces no Host worker
task or batch-runner work.

`CInputConfiguration` owns the resolved `SInputPlan`: binding instructions ordered
by profile/context/action/binding ID, modifier control indices and copied parameter
values. Installation resolves these incrementally after validation. Connections
prepare binding-state pages before exposing their borrowed endpoint. Retirement
reclaims those pages incrementally after reader quiescence, while the installation
token still protects the plan.

## Observations and ordering

Distinguish an ordered control transition from a complete device snapshot. Each
observation carries exact device-instance identity, an observation ordinal,
Platform awake-clock observation time, and an explicit baseline/discontinuity
indication.

A snapshot supplies a coherent before/after comparison: all its control changes
participate together. A primary button and modifier first appearing pressed in
the same report may qualify as a chord, without claiming their physical order.
Initial and recovery baselines establish state without generating presses,
release pulses or historical deltas. Missing information is not neutral input.

Accepted observations are copied into bounded mapper-owned storage. A `busy`
admission leaves ownership with the caller. Later observations cannot overtake
unfinished work; desktop text preserves its position in that ordering. Native
source timestamps remain separate unless conversion into the declared public
clock domain has been established. Equal timestamps do not merge observations.

The internal adapter interface is `CPlatformRuntime::input_observation` with
`SInputObservation`. It owns at most one pending observation per connection:
up to 274 sorted, unique `SInputControl` entries, or one decoded UTF-8 scalar.
Transitions contain exactly one control. Snapshots replace knowledge of the
device; omitted controls become unknown. Digital values are 0/1, sticks/triggers
are normalized, pointer positions use logical coordinates and wheel values use
fractional steps. Virtual modifier controls such as `either_shift` are supplied
by the adapter, including its platform-specific AltGr interpretation.

Admission returns `accepted`, `busy`, `invalid`, `stale`, `baseline_required` or
`closed`. Busy/invalid/stale admission does not advance ownership or watermarks.
Ordinals increase strictly; awake timestamps may repeat but cannot decrease.
The first baseline pins an otherwise unassigned device. `input_device` explicitly
changes that assignment; zero disconnects and rejects observations until another
assignment. A changed instance resets its ordinal, retaining the clock watermark.
`input_discontinuity` retains assignment and watermarks, cancels both desktop
streams (actions only for controller) and requires a newer snapshot baseline.
An unsolicited baseline is rejected. Interruption while copying/applying an
observation also requires a fresh baseline, avoiding partially retained snapshots.

## Activation and composition

| Activation | Behaviour |
| --- | --- |
| Regular | A held contribution starts with an eligible primary press and ends on normal release. |
| Chord | Starts on a primary press with every required modifier held. Primary or modifier release ends it; pressing a modifier later cannot activate an already-held primary. |
| Start press | One pulse per qualifying primary press. Eligible native editing-key repeats produce additional explicit pulses. |
| Release press | Arm on a qualifying primary press and emit on its normal release. Cancellation or earlier modifier loss clears that qualification. |
| Direct | Produce the configured scalar, vector or relative delta. |
| Threshold | Maintain a digital contribution using separate press and release thresholds. |

If one snapshot contains simultaneous primary and modifier releases, allow a
release pulse when the cycle was qualified immediately before that snapshot.
No physical ordering within the report is invented.

Multiple held contributions combine with logical OR. Finish all contributions
to an action before publishing its new state: a same-snapshot contributor swap
must not generate a false release/press pair. Pulses remain separate occurrences.
Retain the configuration's rejection of ambiguous absolute scalar/vector
contributors. Relative deltas retain their configured units and accumulation
semantics.

Same-observation output uses stable action-ID, then binding-ID order. Publication
is not atomic across the whole observation: some outputs may already have been
consumed when later publication pressure requires recovery.

## Analog values, projection and scale

| Parameter | Default |
| --- | --- |
| Stick radial dead zone | 0.15 |
| Stick neutral enter / leave | 0.12 / 0.15 |
| Trigger dead zone | 0.05 |
| Trigger neutral enter / leave | 0.03 / 0.05 |
| Analog digital activation / release | 0.55 / 0.45 |

Preserve radial stick conditioning, direction-preserving unit-circle clamping
and linear trigger dead-zone rescaling from the existing design. Neutrality uses
raw normalized values; activation thresholds use conditioned values. Parameters
belong to bindings, so hysteresis and neutrality state are binding-specific even
when bindings observe the same physical control.

Direction is a signed projection: positive-X selects `x`, negative-X selects
`-x`, and similarly for Y. Direct scalar output preserves that sign; directional
threshold evaluation uses its positive portion. Output `scale` applies after
conditioning/projection and does not alter activation thresholds or physical
neutrality. Non-finite results cause source cancellation.

Validation now restricts normalized thresholds to sticks and triggers. Mouse
position and wheel thresholds are rejected. Direct
projected pointer coordinates retain logical coordinate units, including negative
coordinates, and wheel bindings retain fractional delta units. Neither receives
stick dead-zone treatment.

## Rearming and cancellation

Physical rearming belongs to each binding, not the action as a whole. A held,
inhibited binding must not prevent another independently rearmed binding from
activating the same action.

Rearming applies to the primary activator. Modifiers remain observed conditions:
a fresh primary press may qualify with an already-held modifier. A modifier's own
action binding retains its separate rearming requirement.

Tracking continues while publication is gated. A control that becomes neutral
and then held again during a barrier cannot use the earlier neutral observation
to activate after acknowledgement. Cancellation clears affected contributions,
pending pulses, release qualification and repeat eligibility. It never generates
a normal release activation. Absolute pointer coordinates establish a fresh valid
baseline; they need not return to coordinate zero.

The connection's former subject-level neutral gate has been removed. The
mapper owns physical eligibility; the connection continues enforcing subject
identity, generations, readiness and closure. Logical action inactivity must not
be passed off as evidence of physical neutrality.

## Output and eligibility integration

The mapper increment introduced input service number 2/version 2. Desktop
acquisition advances the service, generic borrowed descriptor and
`SInputEndpointV3` to version 3; controller acquisition advances these to
version 4 and `SInputEndpointV4`. All participants require a coordinated
rebuild; configuration source format remains version 1. `SActionInputV1` and
`STextInputV1` retain their 48/40-byte framing and sequence/barrier machinery.

| Action kind | Numeric value | Payload |
| --- | --- | --- |
| Digital | 0 | `SDigitalAction`: `u32 active`, exactly 0 or 1 |
| Scalar | 1 | `SScalarAction`: `f32 value` |
| Vector | 2 | `SVectorAction`: `f32 x, y` |
| Delta | 3 | `SDeltaAction`: `f32 x, y`; projected deltas use X and zero Y |
| Pulse | 4 | `SPulseAction`: `u32 binding`, identifying this occurrence's binding |
| Boundary | 9 | Existing `SActionBoundary` |

Unused payload bytes are zero. Values 5–8 identify raw desktop packets and are not
emitted by semantic mapping. Bit 0 (`k_action_cancelled`) marks an inactive/zero
state caused by selective cancellation; pulses and boundaries never carry that
cancellation flag. Bit 1 (`k_no_source_time`) marks controller output without
source time, including lifecycle markers. Consumers clear state without
normal-release activation when processing cancellation-marked records.
A cancellation-marked zero delta discards pending relative input; it does not
reverse movement already applied. Ordinary zero deltas remain silent.

Selective cancellation requires an explicit representation. Enabling text
suppresses primary keyboard bindings without `allow_during_text`, preserving
mouse, permitted keyboard and controller contributions. An action receives a
cancellation-marked inactive/zero state only when surviving contributions no
longer sustain it. If that cancellation cannot be published, use the existing
action-overflow recovery barrier. Neither silent loss nor a whole-action-stream
reset on every text change implements this policy.

Context changes, history loss and broad suppression retain the generation,
marker and acknowledgement protocol. Application focus loss and native modal
interaction suppress desktop and controller actions while retaining ownership
and device assignment. Acknowledgement cannot override continued ineligibility.

Raw pointer/button/modifier packet definitions, capture operations and native
text decoding remain separate from this semantic-mapper increment. Already
decoded text still participates in desktop ordering and eligibility.

Text-target controls return `busy` while an observation or selective pass remains
unfinished. An accepted control updates text authority and schedules a resumable
selective pass before accepting more observations; unrelated held contributions
are preserved. Context, device/history loss, focus loss and closure can supersede
unfinished work immediately. `input_focus` and aggregate native modal depth govern
both connections. Ordinary native dispatch restrictions do not suppress input.
Focus event acquisition is left to the later native input adapters.

## Work budgets and pressure

The initial mapper allowance is 500 microseconds, 2,048 elementary work credits
and 64 output attempts per pump, shared fairly between connections and contained
within Platform's existing two-millisecond budget. These are cooperative bounds;
the timing allowance is not a hard scheduling guarantee.

Credits include modifier checks, contribution processing and output work, not
merely observation count. Evaluation retains continuation cursors across pumps.
Lifecycle cancellation may supersede unfinished evaluation.

The pump alternates one continuation step between connections, sharing all three
limits. Steps copy/apply one control, visit one binding, check one modifier, evaluate
one contribution, or attempt one output. Setup charges page growth conservatively
and shares this allowance. Busy callers retain their observation; adapters that
cannot retain it must report history loss. Waiting solely for new input or an
acknowledgement does not report runnable mapper work.

Ingestion overflow or lost source history requires explicit discontinuity and a
trustworthy fresh baseline. Output overflow uses the existing recovery protocol.
Never replay already-published records or reconstruct lost pulses. Pressure must
not create an unbounded observation or output backlog.

## Synthetic acceptance

Synthetic traces cover contributor swaps, chord qualification, simultaneous releases, cancelled
release pulses, independent rearming, exact hysteresis boundaries, projection and
scaling, text suppression, focus/device loss, and observation ordering.

They verify equivalent output under different work-budget partitions, no steady-state
allocation, and recovery when one observation expands beyond transport capacity.
They include the case where an inhibited held binding and a fresh eligible binding
target the same action, and selective cancellation under a full action queue.

`InputMapper_test_cases.cpp` exercises these traces and rejected allocation after
preparation. `PlatformInput_test_cases.cpp` installs a real 1,501-binding pulse
profile, checks preparation before borrow publication, fair shared output limits,
nested modal/focus cancellation and late native-failure shutdown. The prepared
schema tests verify resolved plans after the Host intermediate has been released,
modifier ranges, source defaults and pointer/wheel threshold rejection.

This acceptance establishes the portable evaluator. Native acquisition, device
acceptance and real consuming-module integration require their later evidence.

Validation on 8 November 2026:

| Target | Evidence |
| --- | --- |
| Windows Debug x64 | Full solution build and ordinary `-t1` suite passed. Final focused `--platform-input` suite: 39,609 checks, zero failures. |
| Windows Debug Win32 | Full solution build and final focused input suite passed: 39,609 checks, zero failures. |
| Linux Clang Debug | Existing `build/linux-bundle-clang-debug` rebuilt; ordinary CTest passed. Final focused input suite: 39,609 checks, zero failures. |
| Repository checks | Policy validation, tracked/new text line endings, documentation file links and diff whitespace passed. |

The ordinary suites passed before the last narrow fixes; the focused suite was
rerun on all three targets after the final delta-cancellation correction. Win32's
initial build reported an unchanged vendor source-encoding warning. No Android,
macOS or native device acceptance is claimed by these portable synthetic checks.
