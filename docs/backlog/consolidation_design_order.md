# Consolidation design stages

Updated 16 September 2026. Current outline for discussion and specification.
This is not authorisation to implement production changes.

The baked-document storage, on-demand views, version-4 stored offsets and aligned
file loading are complete in commit `a75962f`, Refactor baked document storage
and embed section offsets. Coordinator implementation review, four-configuration
validation and the isolated engine smoke passed; Ritchie then completed review
and explicitly instructed the commit. The bounded commit contains 15 files.
Broader coordination/deferred documents were excluded from that implementation
commit and are preserved in a separate documentation commit. No push was performed
by the implementing task.

The uniform container attribution contract is complete in commit `42a908d`,
Unify container memory attribution and reattribution. It passed coordinator
implementation review on 16 September, with four fresh passing full
solution/core-test configurations, followed by Ritchie's review and commit instruction.
The [focused record](container_reattribution_specification.md) identifies the exact
validation runs. No push was performed. The dependent task can now use the
committed contract when finalising its remaining design with Ritchie.

Current sequence, 16 September: accounting is complete in commit `d1d804c`,
Make memory accounting diagnostic-only. Its [specification](diagnostic_memory_accounting_specification.md)
records the reviewed implementation, four passing Debug/Release x64/Win32 runs
and successful isolated engine smoke run. Do not repeat that prerequisite work.

The coordinator supports the subsequent split requested through the stage 1 task:

1. A dedicated container/aggregate/live-document reattribution task establishes
   the public infrastructure contract, complete aggregate hooks, diagnostic
   cumulative accounting and live-document composition.
2. The existing stage 1 task then implements its baked-document/view/block,
   storage-preparation and aligned-loading changes against that reviewed contract.

Both tasks work sequentially in the shared main checkout, with developed plans
and implementations reviewed here. The original stage task may continue design
and read-only inspection while the container task implements. The container task
was created on 16 September as `01a0aa63-8583-7b91-862e-b56199f9e039` (local),
using Astra High. Its [focused plan](container_reattribution_specification.md)
records the earlier implementation review and four passing configurations as
historical evidence. Ritchie subsequently authorised a uniform two-method contract:
memory_attribution returning a source-state/totals record, and unchecked context
replacement, with shared checked query/transfer helpers. This revised design passed
coordinator design and implementation review on 16 September, including its new
four-configuration validation, and was committed as `42a908d` after Ritchie's
review and instruction. Its handoff draws on the current sections of the
[stage specification](consolidation_stage_1_specification.md).

The container task covers the eleven inventoried container headers plus
CLiveDocument, with a narrow primitive observation adapter, baked-block forwarding
and typed erased-owner callback adaptations for the revised contract. It preserves transfer capability, exposes only the agreed
cooperating hooks, removes only demonstrably redundant friendships and adds no
erased registration. It must retain coherent allocated source contexts, reject
allocator incompatibility without partial changes, combine accounting once and
update empty member contexts. Cumulative overflow is reported without rejecting
valid transfer. Ritchie selected two checked-add helpers; the revised plan combines
empty/coherent/mixed source states independently of modulo totals and performs
complete recursive replacement after one accounting transfer. The baked block
received forwarding for this interface in the container task; its representation
and adoption changes were handled in the original stage task. The subsequent
[baked-document and aligned-loading plan](baked_document_storage_specification.md)
was subsequently settled with Ritchie and passed coordinator design review on
16 September. Its implementation subsequently passed coordinator review on
16 September, with four passing solution/core-test configurations and a successful
isolated Debug x64 engine smoke run. Ritchie's review and explicit commit
instruction followed; the final change is committed as `a75962f`.

The handoff back must identify signatures, source-discovery semantics, aggregation
and reporting behaviour, class/friendship changes and regression evidence. Preserve
the committed accounting and provisioned-logging contracts. Reload, telemetry,
module-unload policy, Host/TGA changes and new services remain outside this split.

Ritchie clarified the review boundary: unload/reload accounting checks are
tangential to the main counter-update change. With balanced accounting and no
remaining allocations, quiescent totals should already be zero. Leave existing
module-unload behaviour unchanged for this work; report and investigate a nonzero
balance found during validation rather than masking it or automatically adding
lifecycle changes. Resolving future unload/reload policy is not a prerequisite.
The accounting task still addresses accounting-only rejection in allocation,
deallocation, reattribution and their affected transport callers. This scope does
not authorise reload, counter-reset lifecycle or force-unload. Older Host asset retention,
TGA sequencing and new-service allocations are separate remaining consolidation
work; neither of these two tasks inherits them automatically. Reconcile that later
sequence against the Executive acceptance goal after the developed plans are reviewed.

Combined scope across this split, 16 September: preserve existing container reattribution,
organise its cooperating operations in a separate commented public section, and
add live-document reattribution following the aggregate-container model. Refactor
the baked document onto CMemoryConstView and retain a private CByteBuffer as its
owning block's sole member, constructing document views on demand,
with storage preparation and requested file-load alignment. Loaded alignment has
a 16-byte floor; baked blocks use 32-byte alignment and rounded capacity while
retaining exact serialized length. Include regression validation of these changes
and any related edits Ritchie makes. The [task specification](consolidation_stage_1_specification.md)
preserves the wider inventory and discussion. The narrower
[baked storage specification](baked_document_storage_specification.md) records
the final ownership and format decisions: version 4 has a 64-byte header with
stored section offsets and explicit counts. Its implementation passed coordinator
review on 16 September after the reported four-configuration validation and engine
smoke exercise. Ritchie's review and explicit commit instruction followed, and the
change is committed as `a75962f`.

This is not yet a completed implementation plan. Image ownership/manipulation,
Host/TGA sequencing and new services are not automatic assignments to this task.
The detailed stage list below awaits reconciliation with the developed task plan
at coordinator review; it must not drive broader implementation meanwhile. The
asynchronous data-model acceptance goal remains recorded; its minimum dependencies
and remaining stage order will be reviewed with that plan. Later JSON loading
still publishes a baked block, not a live document.

Further 16 September decisions: public hook exposure is limited to the container
set, live document and baked block; no live-document erased registration is added.
Failed baked-block adoption preserves source and destination. Baking explicitly
reallocates to rounded capacity and zeros unused storage as a mandatory step;
loaded storage is already rounded. Loader alignment uses the existing memory
conditioner with a 16-byte floor and rejection above the representational cap,
not the briefly considered upward rounding. The baked-document extent limit is
explicitly reduced to the common 2 GiB limit, including borrowed views. Each
aggregate should present a complete reattribution interface over its owned backing
storage: preflight source coherence and totals, transfer accounting once, then
replace contexts through unchecked hooks. The task specification records the
remaining concrete hook/API details. Accounting discrepancies are high-priority
diagnostic signals, including imbalances reported at quiescent module unload,
not inherently immediate application-fatal events. Accounting remains in release
builds; user-supplied logs may help diagnose local-setup or patch-combination gaps.
Telemetry is only a possible future option, not selected work. Do not expand the
aggregate interface into defensive accounting transaction/recovery machinery.

Latest clarification settles the semantics of wider simplification: accounting
errors must be logged without themselves causing rejection or immediate shutdown;
they may expose an earlier mistake rather than a fault in the current operation.
Reattribution across memory allocators remains forbidden. Current transport
escalation and accounting-gated operations need a concrete primitive/caller plan.
Accounting diagnostics report a high-bit change across an adjustment, using the
atomic operation's own before value and resulting value for each counter. Report
either transition, not every update while the bit stays set; a concurrent cluster
is acceptable. The diagnostic neither rejects the operation nor requests shutdown.
Reload does not exist yet: delayed updates and fresh accounting on reload are
future design requirements, not gaps in an existing feature or automatic additions
to this task. No force-unload or telemetry work is selected.

This outline incorporates the simplifications agreed or proposed on 15 September.
It replaces the earlier broad stage list. The [framework discussion](framework_consolidation_discussion.md)
and [filesystem notes](filesystem_asset_mapping.md) preserve the detailed reasoning,
earlier alternatives and unresolved questions. Their superseded proposals do not
become requirements merely because they remain in that record.

Consolidation comprises separate component specifications and implementation
reviews, coordinated by shared contracts and the acceptance coverage in the
[consolidation plan](consolidation_pass.md). A design stage may produce more than
one specification. The stage order is provisional; individual interface choices
remain open unless explicitly settled in discussion.

The former stages 2 and 3 (Host reference counting and broader Host
authority/asynchronous lifecycle design) are deferred. The active stages below
are renumbered. Specify only the ownership and completion rules required by the
concrete services and Executive exercise; a general redesign is not a prerequisite.

## Stage task workflow

Stage tasks work sequentially in the shared main-branch checkout at
`D:\TheMorphicEngine`, directly in the saved project. Do not create separate
branches or worktrees for these tasks. This is Ritchie's chosen workflow.

This coordinating task reviews designs and implementations and aggregates
decisions, specification changes and progress. Each stage task finalises its
design before implementing it, validates its changes and returns them for
coordinator review before the next stage proceeds. Keep the shared documents
current as the handoff record and preserve existing uncommitted work in the
checkout. Commits still require explicit user instruction and coordinator review
under AGENTS.md. This workflow does not itself start a stage task.

Stage 1 task started on 15 September 2026: `01a0a4d6-e506-7912-8cf8-0ace7c96453a`
(local host), beginning with design and code inspection. Coordinator task:
`01a09fd4-73ad-7ce2-a3e4-efcdadb454dd`. Implementation awaits the finalised design
and coordinator review; no commit is authorised by the task handoff.

## 1. Baked-document and image storage, with permanent Host ownership

This is the current discussion and the first substantial foundation.

Earlier scope reassessment, 15 September: Ritchie's reading of the existing code
has changed the premise of the reattribution redesign. Preserve existing container
reattribution support and its flexibility. Reducing that support to byte/image
wrappers, removing rich-container transfer or relocating reattribution merely to
reduce friendship is no longer the working direction. Specific cleanup may remain
useful. The 16 September inventory above now explicitly includes organising the
public infrastructure interface and adding live-document reattribution; it
supersedes the earlier suggestion that all cleanup would be done by hand.

The baked-document representation and image wrapper remain design questions.
Ritchie is considering a different approach; the earlier generic byte-wrapper
and reattributable-wrapper proposals are not a settled solution. Develop these
components against the existing reattribution infrastructure, with substantive
design choices discussed directly in the stage task. Do not require filenames or
other existing consumers to migrate merely to support the earlier narrowing plan.

Preserve the erased payload shell, allocator compatibility, accounting and
source-preserving rejected transfers. Where the revised design supports moving
storage out of a wrapper, the emptied wrapper must no longer supply a view.
The choice of representations and their precise move/view APIs remains open.

Include primitive file-load alignment here: retain the 16-byte floor, support a
guaranteed 32-byte base for baked content, and read directly into the final owned
storage. Distinguish content length, requested padding and allocation capacity.
A content tag does not replace baked validation.

For the current scope, the Host takes permanent ownership of accepted assets.
It does not dispose of, recycle, erase or deallocate them until application exit.
Client completion, operation failure and module unloading do not retire these
assets. No reference counting or runtime asset reclamation is required.

Host operations on assets can only target assets the Host already owns, using
their asset IDs. Saving a client-owned buffer therefore requires this sequence:

1. Transfer ownership to the Host.
2. Receive the asset ID from the Host after acceptance.
3. Send a separate save request specifying that asset ID and the save options.

Do not combine ownership admission with an operation request. Ordinary rejection
occurs before ownership is consumed, leaves ownership with the client and does
not permit a dependent operation.
Loads create Host-owned assets and return their IDs; subsequent operations use
those IDs under the same rule. Shutdown must release retained assets while any
code, data and allocation contexts needed to destroy them remain available.

Admission failure direction clarified in the stage 1 discussion: resource or
repository-invariant failure after valid transfer leads to fatal shutdown, rather
than a recoverable asynchronous owner-return protocol. Preserve the received owner
for safe shutdown cleanup and issue no valid asset ID for failed insertion.
Posting and repository acceptance remain distinct; failed delivery of an ID for
an accepted asset does not undo Host ownership. The stage specification must still
settle the concrete pre-transfer checks and acceptance point.

Stage 1 also updates the existing TGA sequencing to comply with the separate
transfer/asset-ID receipt/operation request contract. This migration cannot wait
until stage 3. Include the affected callers and validation in the stage 1 design
and implementation; broader load/save service development remains in stage 3.

Revisit the component specification and implementation boundaries after the
baked-document and image approaches are settled. Primitive aligned loading and
the Host ownership/operation contract remain in scope. Retain relevant move/view,
validation, accounting and loading coverage without assuming the former generic
wrapper migration will be implemented.

## 2. Filesystem image and permitted storage

Define the minimum useful image/catalogue of sources within configured locations:
source identity, relative names/hierarchy, location, categories and basic source
provenance. Keep catalogue identity separate from loaded asset identity and from
document-local keys. Evaluate live construction and baked catalogue snapshots.

Define permitted read/save locations consistently across platforms, including
core content, UGC, configuration/platform settings, saved games and temporary
storage as applicable. Do not assume these purposes form one settled enum or
all require identical access. Resolve new save destinations as well as existing
sources.

Specify initial scanning, missing/unavailable sources and minimum image updates.
Game refresh is expected mainly for saved-game lists; major external content
changes wait for restart. Internal exports can update the image from known results
without rescanning the device. The editor permits user-directed refresh choices.
Preserve backing lifetimes for already loaded assets.

Research platform constraints before fixing dependent interfaces. Detailed
override/layer selection and composition are not prerequisites to this image.

Output: filesystem mapping/discovery and image-update contracts, with platform
requirements and file-picker use cases.

## 3. Concrete load/save and conditioning services

Compose the earlier contracts into the actual services:

- Load raw bytes with the requested alignment.
- Load a baked document and expose a checked view over the loaded storage.
- Load JSON text, parse under caller policy and bake before successful publication.
- Decode TGA into the image wrapper. Reconcile the earlier optional source-retention
  request with the rule that any Host-owned source remains until application exit.
- Save Host-owned content as binary, JSON text or encoded TGA, addressed by asset ID.

The file-writing primitive receives bytes. Define JSON writing and image encoding
placement over the limited document/image representations, without a generic
asset-capability registry. Keep I/O and conditioning workloads separate, with
live documents and scratch local to their processing thread.

Complete filename storage, options, request correlation, compact reports/findings,
failure cleanup, result publication, required lifetimes and separate versus combined saves.
The last remains open. Image editing algorithms are outside the initial service.

Apply stage 1's permanent ownership and separate transfer/receipt/request sequence.
Operation success or failure leaves the asset owned by the Host until application
exit. Multiple output requests may use the same returned asset ID; whether those
outputs can be bundled in one operation remains open. Do not introduce runtime
asset disposal through service completion or failure handling.

Output: concrete service specifications and direct persistence test designs before
full asynchronous integration.

## 4. Executive integration acceptance and implementation slices

Specify the deferred Executive data-model exercise as an explicit acceptance gate:
construct all-node fixtures, bake deterministically, transfer ownership, retain a
reference, save/reload binary, exercise JSON save/load/conditioning and compare
the agreed bytes or normalised semantics.

Cover failed admission, operation failure, policy rejection, outstanding operation
and view uses, intermediate/source buffers and invalid asset IDs. Verify that
operations wait for the transfer receipt and target the returned ID. Confirm
Host-owned assets remain available after operations and after clients finish
using them. Runtime reclamation and stale IDs caused by disposal are outside
this scope. Verify retained-asset deallocation at application exit as shutdown
acceptance, with operation completion checked during the exercise. Add TGA/image
and filesystem-image cases for their scopes.

Use the exercise to expose missing contracts rather than inventing test-specific
infrastructure. Each component receives its own implementation/review boundary
and validation criteria. Direct persistence precedes the full asynchronous run.
The data-model Host exercise remains a prerequisite to schema work.

Component designs can become ready separately once their dependencies are settled.
Implementation still requires explicit user instruction, and commits require
explicit instruction plus applicable coordinator review under AGENTS.md.

## 5. Follow-up: module load/unload on the Host worker

After the Executive exercise, design the move of module loading and unloading
to the Host worker thread. Include binding, initialisation and shutdown coordination
where required by that move. Preserve compatibility checks and the existing
module-dependency hazard flagging, and ensure outstanding uses complete before
module code or data becomes unavailable.

This is a separate follow-up specification and implementation slice. It does not
reinstate the entire deferred Host authority and asynchronous-lifecycle redesign.

## Deferred or superseded work

The separate [deferred design resource](consolidation_deferred_design.md) preserves
reference counting, broader authority/operations, overrides/layers, trust and later
image capabilities. It distinguishes future candidates from superseded approaches.
Those notes do not add requirements to these active stages or their implementation.
The stage 5 module migration remains the explicit follow-up after the exercise.

## Research and out-of-order observations

Research is a supporting activity, not a prerequisite survey blocking stage 1.
Use current primary sources for bounded questions when their answers affect a
contract: Android storage including Quest 3+, Windows/Linux storage mapping, and
the actual Steam/Epic services intended for UGC. Do not assume providers expose
equivalent APIs. Separate platform facts from application policy and record
verification dates and remaining uncertainty.

Capture new thoughts in the relevant discussion note, assign them to a stage and
revisit earlier contracts when necessary. This outline coordinates the design;
it does not constrain the order in which useful observations may be discussed.
