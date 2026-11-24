Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    platform_report_review.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    18 Nov 26

# Platform report coordinator review

The focused stage-1 coordinator reviewed the uncommitted first pass based on
`fc3c95737d30d2abebedb712d50a0056e2f37854`. This record distinguishes correctness
findings, the existing placement acceptance gate and proposed design changes.
The review changed no production code. Subsequent user design decisions and the
user-authorised checkpoint clearance are recorded below. Follow-up implementation
stages remain separately scoped; the user performs pushes.

**Verdict: F1 is corrected and the checkpoint is cleared for commit. Full prerequisite
acceptance remains open.** Placement/deadline qualification and the agreed compact
inventory/Host service implementations remain outstanding. The user authorised a
checkpoint after F1 correction and coordinator review; that review is complete.
The known-field packing concern is resolved.

## Correctness finding

### F1 — P2, corrected: known facts could contradict their query outcome

In the original [semantic validator](../../framework/platform/report_validation.cpp),
`fact_query` checks a query's kind and subject, but not its outcome. The source
checks in `SReportTables::topology` accept nonzero `gdi_current.known_fields` when
the associated current-settings query has failed. `desktop_fields` checks masks,
encodings and zero values for unknown fields; it does not establish that the
referenced query actually supplied the known facts. This is inconsistent with
the report's explicit distinction between unavailable facts and successful claims.

A targeted probe used the retained Release cold report. The original passed
`validate_report`. The probe changed query 21 from success to failed/native_failure
with a tagged Win32 error and kept the source's known geometry, depth, refresh,
rotation and scaling (`known_fields == 61`). Coverage was limited. No values,
identities, ranges or schema declarations were otherwise changed. The modified
report also passed `validate_report`, returning reason `none`:

```text
original_reason=0
failed_query=21 known_fields=61 malformed_reason=0
retained_requested_bytes=0
```

The Host's [import/publication path](../../host/assets/asset_platform_report.cpp)
uses this validator, so its independent semantic admission does not reject this
contradiction. The native producer currently clears missing GDI settings correctly;
the finding concerns the independent admission boundary and protection against
an inconsistent candidate, not evidence that the captured desktop facts were wrong.

Require the corresponding successful query before accepting known current-setting
fields. Apply the same consistency rule to query-backed labels and other optional
facts, with correct kind/subject relationships. Preserve valid partial success:
a successful query may supply only the fields marked known. Add negative cases
for failed/blocked queries retaining facts, and positive cases for genuine partial
success and failed queries with absent/unknown facts.

Probe source, exact link command and result are under
`build/platform-report-review/`. It links the configured existing Foundation
objects and the production report validator, rather than rebuilding the passing
matrix. The earlier failed probe-link setup included stale pre-reorganisation
object files; the final command uses only the current compile-command entries.
Production sources were not changed by the probe.

#### F1 correction and independent checkpoint clearance

On 18 November 2026 the Platform implementing chat corrected present-fact admission
and added focused regressions. Present labels, known GDI current fields and known
source annotations require successful correctly scoped supporting queries; genuine
partial success and failed/blocked queries with unknown or absent facts remain
valid. Identity evidence checks its supporting query kind/context. DXGI output
description and interface acquisition now have distinct outcomes, preserving a
valid output name when a later interface lookup fails.

The coordinator reviewed the complete isolated seven-file patch at
`build/platform-report-review/f1-fix.patch`, including producer, validator, portable
and SDK-replay regressions and usage/validation records. Independent forward replay
of that patch from the saved before-files reproduced all seven current files.
No further blocking finding was identified for this checkpoint.

Recorded current-code verification was inspected: x64 Debug/Release solution builds
passed with zero warnings/errors; focused Windows tests passed 1,235 checks each
and GCC Debug passed 221, with zero failures. The coordinator's original cold report
still passes; the exact query-21 mutation now returns reason 2 (`invalid_data`), with
zero retained requested bytes. The rebuilt probe's original defect-expecting exit
is accounted for by explicit assertions on its expected corrected output.

A fresh two-worker Release native lifecycle passed cold, refresh, unload/reload and
historical retention. The coordinator independently compared cold/historical and
refresh/historical-refresh bytes; both pairs are identical. New captures contain
43 outcomes over 72 calls and retain the initial 1,580 mode rows. These current-code
checks supplement the earlier evidence; they do not close the known placement or
Win32 deadline qualification, nor implement the compact schema/Host services.

The user-authorised checkpoint may include the existing Platform implementation,
the F1 correction, its tests and current design/review/usage/validation documents.
At checkpoint commit time the generic Host service contract was agreed and its
concrete messages/handoff were proposed. The user subsequently accepted those
messages/handoff and requested a dedicated Astra Extra-High implementation chat;
see the current staged-design handoff. Rendering remains gated.

## Existing acceptance blocker: placement and the native deadline

The [validation record](platform_report_validation.md) accurately leaves placement
open. Its repeated live results substantiate that decision:

| Observation | Consequence |
| --- | --- |
| Release refresh: 59.471 ms logical authoring/adoption and 78.280 ms bake | Moving only the bake leaves a substantial owner-thread phase. |
| Release Host validation: about 4.4 ms | Platform-only offloading leaves another synchronous Host phase to assess. |
| Release live response: 151.945 ms; Debug: 2531.708 ms | Capture-related owner-thread occupation is observable by a live window. |
| Idle Win32 Debug: 6.208 s schema work, rejected at the five-second deadline | Changing the execution thread alone does not make the capture meet its deadline. |

The two-millisecond pump allowance is a cooperative fairness target, not a hard
universal call deadline. Occasional startup/event/request delays are acceptable
under the user's direction. No numerical responsiveness threshold is selected by
this review. Nevertheless, the recorded placement has not been accepted, and the
Win32 native deadline is a concrete failed qualification case. Resolve the actual
costs or explicitly agree a policy change; neither increasing the timeout nor
adding a thread is an implicit remedy.

## Comment 1: Platform inventory versus Rendering inventory

The two inventories answer different questions. Similar format spellings do not
make the observations interchangeable.

| Observation | Question and consumers | Ownership/reuse |
| --- | --- | --- |
| Desktop sources, targets, active paths, availability, primary, geometry, rotation and refresh | What desktop/display state does the OS report? Useful for window placement, configuration, diagnostics and non-Rendering modules. | Keep in the shared Platform report. Rendering consumes that snapshot. |
| Native adapter identities and display ownership | Which OS adapter owns a source/output, and can it be correlated with another API? | Keep the identity/provenance needed by display relationships. Labels are descriptive, not a join key. |
| Format-qualified desktop mode catalogues | Which resolution/refresh/scan/scaling tuples does this output/source query report for a specified native format and flags? | Platform owns these native observations. Preserve catalogue scope and query outcome. |
| Vulkan physical devices, queues, memory, features, extensions, format/image limits and depth/stencil support | Can a Vulkan device provide a particular resource or workload? | Rendering queries these separately. Platform's display report does not establish them. |
| Vulkan surface formats/colour spaces, presentation modes and queue presentation support | Can this device present to this surface, and with which swapchain choices? | Rendering queries the actual surface as agreed; a desktop mode list cannot replace these answers. |

[DXGI mode enumeration](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-getdisplaymodelist1)
is scoped by output, colour format and enumeration flags.
[Vulkan format properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatProperties.html)
describe image-tiling/buffer features, while
[surface format queries](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceFormatsKHR.html)
describe swapchain format/colour-space pairs.
[Presentation modes](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfacePresentModesKHR.html)
are surface presentation choices, not an enumeration of monitor resolution/Hz tuples.

The actual capture contains six catalogues: **two formats for each of three
outputs**, not six formats. Counts are 67/67, 75/75 and 648/648. All three pairs
have identical observed tuples after removing row ID, catalogue ID and format.
This establishes duplication in this capture; it does not establish that the two
format queries are equivalent on every output or driver.

The 1,580 rows occupy 107,440 bytes out of the 148,696-byte block, or about 72.25%.
There is a useful representation question here: format is already a catalogue
qualifier, and equal tuple sets could be shared while retaining separate query
associations. Doing that requires an explicit schema/ownership change because
current rows contain a single `catalogue_id` and the validator requires that
ownership. It must preserve exact tuple membership, known masks, raw native codes
and refresh precision. Do not union two lists and claim the result for both, or
generate cross-products of their components. The present 68-byte record is a
faithful first-pass representation; the duplication is not a false capability claim.

Adapter identity is cheap by comparison: the three adapter records occupy 84
payload bytes before their identity/query/string metadata. Two carry the same
NVIDIA label but distinct native identity evidence; a third is Microsoft Basic
Render Driver. Never merge adapters by label or assume each is a Vulkan GPU.
[Vulkan's valid Windows LUID](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceIDProperties.html)
can correlate a physical device with a DXGI adapter; its validity and node context
must be respected. An output-less, otherwise unreferenced DXGI adapter could be
omitted if the report is explicitly narrowed to display participants. That is a
scope choice with modest storage benefit, not a reason to drop display-owner keys.

The combined Rendering audit should reuse one Platform snapshot and its provenance,
join native identity evidence where valid, and add its own Vulkan facts. It should
not duplicate the Platform mode inventory for every GPU or re-run Windows queries
inside Rendering. Persistent combined data needs independently owned bytes/context;
a live acquisition token or capture-local row ID is not a persistent identity.

**Disposition:** keep desktop topology/current state, scoped modes and display-owner
identity. Consider factoring repeated mode representation and pruning unreferenced
adapter-only diagnostics explicitly. Rendering will add Vulkan-specific capability
facts; it cannot substitute them for native desktop facts. No inventory change is
selected by this review.

The user subsequently reopened first-pass capture scope, separately from the
generic Host service extension, which requires its own implementation chat. The
largest scope question is whether full advertised mode catalogues are needed now,
including both native colour formats and interlaced/scaled inclusion. Removing
those observations changes discovery coverage; sharing identical returned sets
changes representation while preserving coverage. Neither is selected yet.
Pruning output-less, unreferenced adapter diagnostics offers a much smaller saving.
Keep current state, discoverable inactive displays, display-owner identity and
query provenance tied to their actual consumers; the broader agreed audit purpose
is not limited to the minimum information needed to draw the first triangle.

Subsequent scope decision: the user requested removal of texel/rasterisation-format
capture supplied by Vulkan and condensation of retained data. The initial report
has no GPU resource-format feature tables; its repeated RGBA/BGRA values qualify
native mode catalogues. The user accepted a single RGBA8-filtered enumeration,
52-byte mode values and direct typed catalogue spans with identical-list sharing in the
[scope and condensation record](pso_creation_stage1.md#platform-report-scope-and-condensation).
This refines the earlier index-membership suggestion. Implementation of the revised
profile/layout is outstanding. The measurements above concern the initial layout
and cannot predict the revised processing cost. The generic Host service extension
has an agreed service contract and concrete messages/handoff in that design. The
user requested its dedicated implementation chat after the checkpoint commit.

## Comment 2: known-field packing concern resolved

The user's question concerned the seven `PlatformModeFields` known flags: extent,
refresh, format, depth, scan, scaling and rotation. The implementation already
packs these into one u32; inspection JSON expands the names into booleans for
readability. The user confirmed in the implementing chat that this explanation
resolved that concern. The agreed compact layout reduces the set to six known
flags in the same four-byte representation. No separate packing decision is pending.

The earlier review's analysis of packing five three-state topology properties
addressed different fields. It was not the user's known-mask request and is not
part of the agreed compact-layout revision. Unknown/no/yes semantics remain intact.
An eight-bit mask alone would not remove padding required by the following
four-byte-aligned fields.

The inspection JSON's verbosity is distinct from binary payload size and repeated
declaration preparation. The agreed generic Host service permits declaration reuse
by immutable asset identity while keeping completed runtime results self-contained.
An external persisted schema dictionary is not selected by that agreement.

## Comment 3: ownership transfer and execution placement

The existing mechanisms provide useful building blocks:

- Framework has an owning `LiveAssetTransfer` for `CLiveDocument`, including nested
  memory attribution. Platform can transfer a document without sharing a mutable
  borrow with Host.
- Host retains operation state and dispatches background work with explicit
  completion/retirement. Its generic `condition_document` calls
  `document_translation::bake`; it does not call `CSchema::bake` or produce the
  complete typed report payload. A generic retained-baked asset request is therefore
  not a drop-in report preparation operation.
- `configure_workers` routes the conditioning role to the I/O worker when there
  is only one worker. It still leaves the Host owner thread, but may delay unrelated
  I/O work. Queue time, shutdown and deadline behaviour need qualification there.

Transferring an already-authored `CLiveDocument` is a viable narrow boundary, but
it leaves some or all of the measured 59.471 ms authoring/adoption on Platform's
pump. Moving only the subsequent bake would not address the complete stall.

The user subsequently settled baking as a generic Host conditioning service
available to any module, with ownership of the authored input transferred from
Platform. This requires full schema baking, rather than only generic document
encoding. The existing transfer and worker paths are building blocks to extend,
not an already complete typed-schema baking service.

The earlier suggestion to transfer portable observations for Host-side report
authoring is superseded by this clarification. Platform owns report construction
semantics: which observations become records, their values, relationships and
provenance. Host may provide generic schema composition/merging of supplied
declarations and data without understanding display-report construction. The user
identified merging as a reasonable service; its concrete inputs, conflict policy
and integration with baking remain to be settled. A private CReportMemory context
still cannot escape without proper owning transfer and nested attribution.

Moving schema baking removes that phase from Platform's pump but does not move
the measured record-authoring cost automatically. Establish which construction
work remains in Platform and which work is generic composition before selecting
its execution placement. Independent report validation remains separate from
generic schema admission and baking. The Host owner thread retains
admission, exact provider/capture identity, reservations, deadline/lifecycle recheck,
atomic current-snapshot publication and replies. Independent validation must still
run; moving it to Host code on a worker does not mean trusting producer assertions.

The concrete job must retain its inputs, limits/accounting and completion capacity
through queueing, execution, cancellation and retirement. Requester departure does
not cancel Host maintenance. Obsolete provider/capture outcomes cannot publish.
Timeout is not worker quiescence, and the input/result must survive a queued or
running job until completion or proved drain. A registered shared payload does not
automatically eliminate a producer-compiled deleter or private allocator dependency:
retire that incoming dependency before unload, or protect it through job retirement.
Preserve the existing one-capture/busy and receipt rules unless an explicitly
reviewed protocol revision changes them. Do not reset budgets at the transfer or
drop the five-second eligible deadline's queue/processing time.

### Native capture thread choice remains open

The user's subsequent clarification is explicit: thread ownership and lifetime
are undecided, including whether any extra native-capture thread is appropriate.
Do not equate these independent choices:

1. Which phases leave the owner thread: native queries, logical construction,
   schema baking, independent validation, or some combination?
2. Who owns execution and module lifetime: Platform or a Host-managed module service?
3. How long the execution resource lives: an existing worker, a temporary helper
   for an entire capture, or a persistent dedicated thread?

The selected queries do not create a window or consume a window handle. From the
documented API contracts, this review identifies no requirement that they run on
the application's main/window-owning thread. That is an inference from those
contracts, not native qualification of a new thread. Preserve console/session/
desktop access, use and release the native objects under a clear execution owner,
and keep discovery outside callbacks and the load handshake.
[CCD documents session/desktop access restrictions](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig);
[DXGI factory creation documents its DllMain restriction](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-createdxgifactory1).

| Candidate | Assessment to carry into design |
| --- | --- |
| Retain stepped native queries on the existing pump; move expensive portable preparation | A credible option: the largest repeated measured cost is report preparation. Native cold calls still need an explicit responsiveness judgement. It requires no new native-capture thread. |
| Existing Host conditioning worker for portable preparation | Reuses scheduling/ownership machinery; needs a report-specific operation and one-worker I/O-fallback qualification. It does not imply running Platform native callbacks on that worker. |
| Temporary helper for one whole native capture | Plausible for infrequent startup/explicit refresh; requires asynchronous completion, cancellation and module retirement. Creating a thread and immediately joining it on the owner would retain the stall. |
| Persistent helper or Host-managed Platform service thread | Plausible if demand/reuse justifies it. Native code, module binding/lease, thread/COM/session ownership, stop/drain and cleanup must be designed explicitly; this is not already supplied by generic document conditioning. |
| Keep work on the owner and reduce its cost | Still an option, but needs measured improvements or explicit acceptance of the observed latency. A smaller payload alone does not prove acceptable processing time. |

The recorded native interval is about 8–17 ms over many calls in Release, versus
about 141–152 ms synchronous schema preparation. Those observations support
targeting portable preparation first; they do not rule out moving native queries
or select a permanent thread. Offloading also does not solve the Win32 five-second
failure if the same work still takes 6.208 seconds. Reassess cost and deadline
qualification after the chosen changes.

**Disposition:** generic Host schema baking on the conditioning thread, available
to any module through owning transfer, is settled. Generic merging is a reasonable
additional service to design. Host-side report construction is not selected.
The exact service/handoff contract and the native capture execution resource,
thread ownership and lifetime remain undecided. See the
[recorded Host schema service decision](pso_creation_stage1.md#host-schema-services-placement-decision).

## Review evidence and next decisions

The review inspected Framework schema/validation/ownership, Platform capture and
Windows query phases, Host publication/acquisitions/control credit/retirement,
existing document/schema worker paths and relevant injected/native tests. The
saved cold/refresh historical `.bsd` pairs independently matched the hashes in the
validation record. The observed mode-pair equivalence and layout arithmetic above
were checked from the retained capture and maintained generated declarations.
Existing build, native lifecycle, live-window and cross-platform layout/inspection
evidence was reused; no broad passing validation matrix was repeated.

No additional reader-backing, request/receipt retirement or publication-lifetime
defect was found in the inspected current paths. This is not acceptance of a future
worker path: that path introduces new queue/cancel/unload/drain cases needing tests.
Native multi-display success and safe deadline failure do not close the unresolved
placement decision or turn injected-only cases into native observations.

F1 and its focused regressions are reviewed and cleared for the user-authorised
checkpoint commit. The compact filter/layout and generic Host
service contract and concrete messages/handoff are agreed. The user requested the
separate Host service implementation chat. Native capture execution remains open.
After implementing the follow-ups, qualify the changed pipeline, including the
same desktop live session, Win32 deadline case and any selected one-worker path,
before seeking independent prerequisite acceptance. Rendering 1A remains outside
this checkpoint and review.

The generic Host schema service subsequently passed focused independent review
within its own scope, with no blocking findings. See
[the service review](../schema/host-schema-processing-review.md). Its acceptance
does not implement the compact Platform schema or authored capture handoff and
does not close this pipeline's native placement/deadline qualification. No commit
for that service has been authorised by this review.

## CI follow-up coordinator review, 18 November 2026

Independently reviewed the three-file CI follow-up against
`c1d4a6e1c900913d55ed40745e644e0eff1bb3ab`: `tests/input_host/measurement.cpp`,
`platform/native/windows/report_windows.cpp` and
`tests/test_suites/PlatformReportWindows_test_cases.cpp`.
**No blocking findings; cleared for the user-authorised CI follow-up commit**, with
this review record. No implementation changes, commit or push were performed by
this review.

The final measurement snapshot now retries only transient `EStateRead::retry`
outcomes within a ten-second awake deadline and bounded epoch waits, without
consuming input events or changing frozen measurement counters. Failed snapshots
stop immediately. Windows rejection diagnostics preserve the original failure
policy while identifying phase, field, values and cursor state. The replay suite
uses the real logger and checks readable messages and valid event transport.
No public layout or contract version changes.

Reused the supplied affected-build/test evidence rather than repeating passing
validation. Inspected the saved Linux pressure report under
`build/platform-ci-investigation/snapshot-retry/pressure-2-a36q28zw/`: success,
exit zero, two workers, five pipes, no reported errors, zero diagnostic failures
and no ingress loss. Inspected the three successful Windows x64 Debug affected
build logs, native replay rejection/failure/cursor logs and the native one-worker
`platform-report-1-ji9mps0h` lifecycle pass. The supplied focused test result was
1,247 passed and zero failed.

The historical Linux log does not establish whether its snapshot read returned
retry or failed. The historical Windows capture likewise lacks the precise
rejected field. This checkpoint improves retry handling and next-run diagnostics;
it does not establish those historical causes or close the compact-layout,
Host schema-service handoff, native placement/deadline or full prerequisite
acceptance work. Another CI run remains necessary evidence for the reported CI
environments; the user performs the push.

## Effective-worker and mode-link CI follow-up, 18 November 2026

Independently reviewed the four-file diff against
`2ec979d3c37b7c057bf84c0c4cac2ddbe400ec79`: the Host-pump Python runner, Linux CI
artifact upload, Windows native mode-link diagnostics and Windows SDK replay tests.
**No blocking findings; cleared for the user-authorised CI follow-up commit**, with
this review record. The review changed no implementation and performed no commit
or push.

The placement check uses the single effective Host startup configuration rather
than assuming the requested worker count was granted. It retains the exact two
conditioning/two reclamation events and their expected worker identity. This fixes
the four-core request-two/effective-one case without changing Host worker policy.
The added `build/linux-ci/host-pump/` artifact path matches CMake's runner output.

Windows logging is confined to existing rejection paths and records source,
target or desktop-image component, path flags, selected virtual-mode interpretation,
chosen index/count and raw/packed union fields. It changes neither decoding nor
rejection policy. The added SDK replays check target/desktop-image invalid indices
and a synthetic legacy-decoder case carrying the packed-looking word 131071.
That synthetic case does not establish what flags the CI driver returned.

Inspected retained evidence under `build/platform-ci-investigation/`:

- `effective-workers-four/schema-process-2-eaeoixja`: one effective worker on four
  reported CPUs, with exactly two conditioning and two reclamation events on
  `bg_file_io` and fixture success.
- `effective-workers-two/schema-process-2-vvadz6cq`: two effective workers, with
  the same exact event counts on `bg_conditioning` and fixture success.
- `mode-link-final-pump/schema-process-1-hcql6jpe`: Windows one-worker placement
  and fixture success. The native `platform-report-1-l5dcqahh` lifecycle also passed.
- The three affected Windows x64 Debug builds succeeded without warnings/errors.
  Saved real-logger replay output contains all three components, flags/decoder and
  raw/packed words, with no invalid transported event. The supplied focused test
  result was 1,279 passed and zero failed.

Passing validation was reused rather than rerun. The Windows CI rejection of index
131071 against two mode records still lacks its historical path flags/component;
the new logs prepare the next CI diagnosis and do not fix or establish that root
cause. Compact-layout, Host capture handoff, native placement/deadline and full
prerequisite acceptance remain outside this checkpoint.

## Drain/freeze and bounded legacy retry review, 18 November 2026

Independently reviewed the eight-file CI correction against
`ac0a01dbf5ef5c77163a540f6304304945d8b8e7`: input fixture handshake/measurement and
README, Windows cursor state/retry and SDK replays, and the two affected report
design documents. **No blocking findings; cleared for the user-authorised CI
correction commit**, with this review record. No implementation edits, commit,
push or delegated work were performed by this review.

The freeze request retains its reserved completion while synthetic feed/history,
mapper/desktop baseline work and both input streams drain. The consumer's existing
transaction wait continues input progress. Frozen trace state is release-published
before success is marked for delivery; overlap is busy and stopping settles a
pending completion as closing. The pressure regression continues progress for
30 ms after freeze while retaining strict publication/read correlation.

The Windows retry admits only an initial virtual-aware CCD fetch/translation
`invalid_data` rejection with a recorded field. It retains the original report-
scoped query as failed/profile-fallback with field/value text, clears derived
topology and replaces the entire native cursor before a fresh base-profile query.
The session's cumulative native calls, memory accounting and original eligible-time
budget remain in force. The base profile cannot re-enter this fallback; final
consistency uses the accepted profile and is excluded from the retry guard.
No public descriptor, schema, enum or version marker changed.

Reused and inspected the supplied validation under
`build/platform-ci-investigation/`, without rerunning passing matrices:

- The retained old-implementation controlled-freeze report records eight unmatched
  reads. The new four-CPU WSL one/two-requested-worker pressure reports and Windows
  one-worker pressure report pass with missing/invalid zero, no errors and zero
  trace/clock/source diagnostics, including the post-freeze regression.
- The retained Linux CTest log records all nine other affected input acceptance
  cases passing. The changed Windows test/module builds succeeded without warnings
  or errors.
- SDK replay code/logs cover partial virtual topology discarded before successful
  legacy capture, the retained original query/diagnostic, terminal legacy failure,
  cumulative native-call exhaustion and expiry of the original deadline. The
  recovered report is baked and checked by the independent report validator. The
  supplied focused result was 1,400 passed and zero failed.
- The native Windows one-worker `platform-report-1-2jo8z08o` lifecycle log records
  startup/refresh/reload/historical fixture success.

This clearance covers the CI correction and its explicitly authorised bounded
fallback policy. It does not establish the exact Windows CI driver defect or
close compact-layout, Host capture handoff, native placement/deadline qualification
or full prerequisite acceptance. The user performs the next push/CI run.

## Controller history and freeze diagnostics review, 19 November 2026

Independently reviewed the 14-file uncommitted diff against
`85e525d6563e3ca19f6ec6a7c005f3e3e8c7e98f`: retained synthetic pending work,
100 ms unread-history expiry, atomic freeze diagnostics, SDK/Host acceptance
coverage and the associated registrations/documentation.
**Initial disposition: not cleared pending H1. Re-review below resolves H1 and
clears the final scoped diff for commit.** This review changes only this record;
no implementation edits, commit, push or delegation were performed.

### H1 [P2]: retain read-side source-clock observations

In [the GameInput read path](../../platform/native/windows/controllers_windows.cpp),
the new `GetCurrentTimestamp()` sample is compared with `state->last_clock`, but
that watermark is never updated after a nondecreasing read-side sample. Dispatch
and eligibility update it; multiple reads within one pump do not. For example,
dispatch can leave it at 100,000 microseconds, a read observes 300,000, and a later
read observes 250,000. The rollback is missed because 250,000 still exceeds the
dispatch watermark. A subsequent dispatch at 260,000 also misses it. The adapter
can advance a reading/cursor/ordinal in the old clock epoch despite having observed
the source clock go backwards, contrary to the agreed epoch-recovery rule.

Record each accepted read-side clock observation in the common watermark before
the subsequent early-return paths, while preserving rollback latching, COM release
and cursor/ordinal protection. Extend the SDK replay with a forward read sample
followed by a rollback that remains above the prior dispatch watermark. Check that
the second reading is released, cursor/ordinal remain unchanged, recovery stays
latched and the existing dispatch recovery advances the epoch. The current new
rollback test manually assigns 500,000 to `last_clock` before reading 400,000; it
does not cover this case. Only affected validation needs repetition after the fix.

No additional blocking findings were identified in retained-history scheduling,
age admission at exactly 100 ms versus expiry above it, idle-current baseline
handling, rejected-reading ownership, atomic best-effort blocker masks or fixture
version/consumer coordination. Freeze diagnostics read atomic observations while
unfrozen, rather than racing the raw trace arrays. Eligible/claimed history requests
pump progress; suppressed/unclaimed history expires through ordinary opportunities.
The existing count cap, pump budgets and request timeout are retained.

Reused the supplied validation rather than repeating passing matrices. Inspected
the eight final Win32/x64 pressure/history-freshness/load-smoke/transitions reports
(all passed), Linux Clang focused result (46,191 passed), and the 12 plus 10 Linux
acceptance results (all passed). The retained real-ring timeout probe correctly
reports admitted request 31, frozen false, 1,202 pumps, action mask 2 and pause
mask 2, followed by cooperative failure/cleanup. Win32/x64 focused SDK evidence
was supplied as 46,290 checks each. These passes do not cover H1's missed-clock-
watermark sequence. No broad prerequisite acceptance or unrelated native-report
placement qualification follows from this CI correction review.

### H1 re-review and final commit clearance, 19 November 2026

**H1 is corrected. No blocking findings remain; the final 15-file diff against
`85e525d6563e3ca19f6ec6a7c005f3e3e8c7e98f` is cleared for the user-authorised
controller-history/freeze-diagnostic commit**, including this review record.

The GameInput read path now assigns `state->last_clock = now` immediately after
the rollback guard, before age, freshness or quality early returns. A detected
rollback retains the former watermark/cursor/ordinal, releases the candidate
reading and latches recovery. The new `read_clock_rollback` SDK replay exercises
real adapter dispatch at 100,000 microseconds, a forward read at 300,000, then
rollback at 250,000. It checks rejected-reading release, unchanged cursor/ordinal,
the retained 300,000 watermark, continued blocking after time advances to 400,000,
and dispatch recovery into epoch 2 before baseline ordinal 1. The age replay also
checks watermark retention when an over-age reading returns history loss.

Compared file hashes with the original review: only the native source, SDK tests
and input Host validation record changed among the 14 previously reviewed files.
Those changes were reviewed together with the unchanged remainder and this review
record. No temporary probe or unrelated scope expansion was introduced.

Inspected the retained H1 Win32/x64 Debug build logs for MorphicPlatform,
MorphicInputPlatform and MorphicTests: all succeeded without warnings/errors.
The raw `H1-green-Win32-console.log` and `H1-green-x64-console.log` each record
46,303 passes and zero failures. The pre-fix result of 46,294 passes/nine failures
is explicitly retained as an observation summary in `H1-red-observation.txt`, not
as a captured raw log. Existing Linux Clang acceptance, synthetic Windows Host
acceptance and real-ring diagnostic evidence remain applicable to their unchanged
paths; no broad passing matrix was repeated for this Windows-only correction.

This closes H1 and the scoped commit review, not full Platform-report prerequisite
acceptance or the separate native-report placement/handoff work. No implementation
edits, commit or push were performed by the reviewer; the user performs pushes.

## Platform handoff pause documentation review, 19 November 2026

Independently reviewed the documentation-only diff against
`0dad3b901a77f9e70b01ef5bf10832c77e96568d`: the overarching stage plan,
Platform report usage/validation records and focused stage-1 design.
**No blocking findings; all four reviewed documentation files plus this review
record are cleared for the user-authorised documentation commit.** No commit or
push was performed by the reviewer.

The changes correctly pause revised Platform report handoff design/integration
while the user arranges independent Host communications review/refactoring in
other chats. They retain the former authored-payload/declaration/bootstrap flow as
an unsettled proposal and describe Platform-led orchestration through generic Host
services only as a direction to reassess. Neither replacement architecture nor
Host refactoring is selected or authorised by the pause.

The compact layout/native-query requirements, accepted schema services, immutable
Host snapshot and reader-lifetime requirements, independent semantic-validation
obligation and existing capture limits remain recorded. Native placement/deadline
qualification and full Platform acceptance remain open. Resumption requires the
user's return and inspection of the actual revised Host contracts and review/
validation evidence; another chat finishing work does not automatically resume
this handoff. The deferred Rendering/Vulkan design and open decisions remain for
a later coordinator-created design chat. The user-confirmed green CI checkpoint
for `0dad3b9` is kept separate from prerequisite acceptance.

Source inspection confirmed the referenced report-specific fields and branches in
Host client/pumped transport; it does not select their replacement. Documentation
whitespace, LF/final-newline and the new pause/handoff cross-references were checked.
No source changes, builds, runtime tests, new chats or refactoring were performed
for this review.
