# Desktop input acquisition

Contract accepted and implementation authorised 8 November 2026. This increment
connects native desktop acquisition to the [portable mapper](input_mapping.md),
[input connections](input_connections.md) and existing Host pump. Windows is the
first backend. Shared observations, ingestion, recovery and public records remain
platform-neutral. Native Linux, Android and macOS acquisition are later work.

## Ownership and readiness

One logical desktop source belongs to the window lifetime. It combines keyboard
and mouse; it is not a catalogue of physical devices. The connection retains its
own authority and cannot inherit a predecessor's queued input. Host owns native
acquisition, private ingress, translation and capture on the window thread.
There are no additional Host worker jobs, batch-runner tasks or input threads.

Reader readiness and publication eligibility are separate. Acquisition may
complete unfocused. Publishing additionally requires focus/modal eligibility,
cleared stream barriers and completed native cutover/baseline. Initial held
controls establish state without presses, release pulses or historical movement.
Bookkeeping continues while publication is suppressed.

## Ingestion and ordering

Retain at most 256 compact native transactions in preallocated Host-owned storage.
Callbacks perform bounded bookkeeping only. No external call occurs inside queue
mutation. Native callbacks can reenter; they cannot recursively run the mapper.
Repeat and Unicode expansion run incrementally in the ordinary pump.

The adapter retains a transaction while the mapper is busy. Later observations
cannot overtake it. Each transaction retains its applicable connection generations;
replacement/cutover cannot reinterpret old text or actions under new authority.
Publication remains allocation-free after preparation.

A grouped transition applies all supplied changes before evaluation. Omitted
controls retain their previous state. A full snapshot instead makes omitted
controls unknown. Physical and virtual modifier updates, and a button with its
position, must be coherent. Wheel deltas are occurrences and cannot replay when
an unrelated control changes or a baseline is established.

Raw packets precede semantic actions within an observation; same-observation
semantic actions retain action-ID/binding-ID ordering. All packets use the same
desktop sequence and watermark, including committed text. A published prefix is
never replayed if later output pressure cancels the remainder.

## Native cutover and recovery

Changing a local generation does not fence native messages already queued. A
cutover remains gated until affected retained input, native messages and in-flight
translation have been consumed/discarded and a coherent baseline is available.
Reentrant lifecycle changes invalidate an unfinished quiet-point sample. This is
an observation-order guarantee, not a claim about physical hardware simultaneity.

Context/focus/history loss cancels immediately. Native cutover and the existing
marker/acknowledgement protocol must both permit reopening; acknowledgement alone
cannot override native gating. Recovery runs in bounded slices. Continuous input
can prolong suppression; it cannot justify blocking Host or inventing a baseline.

Text changes are selective: clear old text authority, queued text and partial
decoder state while preserving mouse and allowed-during-text contributions.
Newly eligible keyboard bindings remain inhibited through cutover. Their latest
physical state determines rearming; neutral followed by pressed while gated does
not authorise activation after the gate opens. Partial native dead-key composition
must not contribute text to a different target.

On ingress overflow, publish durable loss, cancel once and enter recovery. Further
arrivals update accounting without repeatedly advancing generations. Loss and
lifecycle state are independent of queue capacity. Output overflow retains the
existing stream-specific recovery. Counter exhaustion closes instead of wrapping.

## Keyboard and text

Bindings name physical positions; Unicode text follows native layout translation.
Keep left/right modifiers and virtual either-side controls coherent. Recognised
AltGr sequences suppress the synthetic Ctrl contribution without modifying the
messages passed to native translation. Preserve established physical Ctrl state.
Windows recognises adjacent matching-timestamp Ctrl/Right-Alt messages, including
ordinary and system key families and paired release bookkeeping. This is native
message evidence, not physical-device identity. A held ambiguous Ctrl/AltGr or
shared virtual-key position at baseline remains unknown until disambiguated.

Translate each native key once. Retain an explicit translation turn across pump
slices and resolve its character messages before later keyboard/mouse turns.
Character-producing editing keys produce one configured action, never an additional
text control character. Navigation cannot overtake earlier unresolved text.
Arbitrary character messages are not assigned the last key's identity.

The Windows adapter uses separate ordinary and system character ranges; a single
range spanning both would include system key messages. Every retrieval can invoke
sent callbacks and must revalidate retained authority. TranslateMessage's return
value does not promise a character. An empty synchronous character drain ends the
turn; it never waits for a character that the key cannot produce.

Committed UTF-16 is decoded into complete UTF-8 scalars through SuiteUTF. Preserve
high surrogates across pumps, decode pairs before repeat expansion, discard malformed
sequences and continue. Diagnostics identify a window/category and rate-limit counts;
they never log typed text. Generation changes discard partial decoding.

Native dead-character state is tracked separately from UTF-16 decoding. If it
crosses a cutover, suppress the composition's result until the native translation
turn resolves it; never probe the same state incidentally with ToUnicodeEx. Native
repeat is retained, with per-message counts and bounded continuation. Editing
repeat remains opt-in for the existing permitted bindings. IME pre-edit and
layout-dependent printable-character bindings remain deferred.

## Pointer packets and capture

Input service number 2 and its endpoint use version 3. Matching participants must
rebuild together. Configuration source format stays 1; action/text framing stays
48/40 bytes. Raw subjects identify portable controls, not configured action IDs.

| Kind | Subject | Payload |
| --- | --- | --- |
| Pointer (5) | mouse_position (133) | logical x/y and observed state |
| Button (6) | changed mouse control (128-132) | logical x/y and resulting observed state |
| Wheel (7) | mouse_wheel (134) | logical x/y, horizontal/vertical fractional steps and observed state |
| Modifiers (8) | zero, aggregate state | observed state |

Observed state contains u16 modifier value/known masks, u8 button value/known
masks and a zero u16 reserved field. Modifier bits are left/right Shift, Ctrl,
Alt, Meta, then AltGr. Button bits follow the five portable mouse controls. Unknown
is distinct from released. Unused payload bytes are zero. Wheel payload fits the
existing 24 bytes. The button mask carries the changed button's resulting state.

Coordinates use client logical units, +X right/+Y down, including outside the
client area during capture. Windows converts using the observation's DPI and
preserves signed coordinates. Wheel units are fractions of WHEEL_DELTA, positive
vertical away from the user and positive horizontal right. Consumers decide
scroll/zoom policy and choose semantic or raw wheel handling without double-applying it.

SetCapture carries connection, transition and expected action generation (three
u64 values). Admission requires the exact desktop owner, active and unblocked
authority, focus/modal eligibility and no existing grant. Native success is
confirmed, then authority rechecked after potentially reentrant calls. The
successful request ID is the capture grant token.

ReleaseCapture carries connection, transition and grant (three u64 values). It
cannot release a newer grant. Durable state exposes the current grant, capture
revision, revocation count and last revocation reason. A later grant does not erase
the prior revocation evidence. Capture is never automatically reacquired.

Action-invalidating barriers and closure revoke capture. Unexpected native loss
creates an action-only desktop barrier: all desktop actions, including keyboard,
cancel, while text and controller generations survive. Expected release callbacks
do not create a second cancellation. Native release precedes retirement/destruction;
failure cannot be mistaken for completed cleanup.

## Scheduling and acceptance

Keep the existing two-millisecond Platform allowance and shared mapper limits of
500 microseconds, 2,048 work credits and 64 output attempts per pump. Raw attempts
count too. Bound native dispatch and rotate service opportunities so runnable
mapping cannot be perpetually displaced by native input. Cooperative work limits
do not bound arbitrary operating-system callback duration.

Synthetic tests cover grouped modifiers, omitted controls, wheel non-replay,
raw/semantic ordering under split budgets, Unicode/repeat expansion, stale epochs,
text selectivity, early acknowledgements, overflow, capture loss and stale grants.
Use the public installation/acquisition/reader/notification/retirement path in a
Windows acceptance harness. Separate injected native-message evidence from actual
physical layouts, AltGr, dead keys, capture interaction and cross-monitor DPI.

Windows x64/Win32 builds validate native layout; Linux continues exercising shared
ingestion and protocol tests. Validation results belong in the implementation
report; this contract alone does not claim hardware acceptance.

### Running the harness

`MorphicTests --platform-input` runs the portable synthetic suite. On Windows,
`MorphicTests --platform-input-native` creates an owned window and exercises
installation, acquisition, the public reader, injected text/edit ordering, raw
pointer/button/wheel packets, UTF-32 validation and capture loss. Adapter probes
also cover AltGr/Ctrl release orders and ambiguous held-key baselines. These
injected/probed results do not establish physical keyboard-layout acceptance.

`MorphicTests --platform-input-interactive` provides a 30-second manual session.
Its window title displays transient decoded text, Backspace editing, pointer and
wheel values, modifier/button value/known masks and the capture grant. Hold the
left button to request capture, drag beyond the client and release. Change focus
and monitors to inspect recovery and coordinate behaviour. Typed text is not
logged and the title is cleared on exit. This mode reports liveness assertions;
the operator must assess physical layout, dead-key/AltGr, editing, capture and DPI
results. IME pre-edit and controller acquisition remain outside this increment.

Native references: [TranslateMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-translatemessage),
[WM_CHAR](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-char),
[ToUnicodeEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-tounicodeex),
[WM_CAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged).
