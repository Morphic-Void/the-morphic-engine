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

The Host's [import/publication path](../../host/runtime/asset_platform_report.cpp)
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
