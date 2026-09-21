# Consolidation before schema work

Status update, 21 September 2026: the selected parser, storage, image-view and
asset-service consolidation is complete and committed through `f74213f`.
The subsequent asynchronous module lifecycle and asset-disposal implementation
is committed as `491ce78` after user/coordinator review and the manual style pass.
The separate rendering DLL stub is now implemented and accepted.

Use [current scope](current_scope_backlog.md) and the
[coordinator handoff](consolidation_coordinator_handoff.md) for the remaining work,
and the [asset](../assets/asynchronous_asset_services.md) and
[module](../modules/asynchronous_module_lifecycle.md) documents for implemented
contracts. Filesystem images and the general job/lifetime framework remain deferred.

## Historical planning record: 16 September 2026

The remainder preserves the consolidated plan and its evolving decisions.
References to current/pending work below are dated history, not open assignments.
Permanent-until-exit retention and mandatory separate transfer/save proposals
were superseded by one-shot operations and explicit disposal.

Current implementation status, 16 September: diagnostic accounting is committed
as `d1d804c`, uniform container/live-document attribution as `42a908d`, and baked
storage/aligned loading as `a75962f`, following coordinator review, validation and
Ritchie's review and commit instructions. The final baked block owns only a
CByteBuffer, supplies views on demand and uses the version-4 format with stored
offsets. The [focused storage record](baked_document_storage_specification.md)
and [current outline](consolidation_design_order.md) take precedence over earlier
token-ownership proposals and pending-work statements below. The broader Host,
TGA, service and Executive work has not been completed by those commits. The
filesystem image remains deferred, including its new immutable-publication notes.

The following dated planning record preserves how that split developed; it does
not reopen completed work or schedule deferred features.

The diagnostic accounting prerequisite is complete in commit `d1d804c`, with
review and validation recorded in its [specification](diagnostic_memory_accounting_specification.md).
The coordinator supports a separate container/aggregate/live-document reattribution
task next, followed by the original stage task's baked-document/block, storage
preparation and aligned-loading work. Keep production work sequential in main and
review each developed plan and implementation separately. The container task was
created on 16 September as `01a0aa63-8583-7b91-862e-b56199f9e039` (Astra High,
local main checkout), starting with design. Its requirements and inventory are in the current
[stage specification](consolidation_stage_1_specification.md).

Coordinator review supports this accounting-first split and coordination of both
tasks, subject to their complete design and implementation reviews. Ritchie has
clarified that unload/reload accounting checks are tangential: leave existing
module-unload behaviour unchanged and expect zero quiescent totals for a balanced,
fully cleaned-up run. A nonzero balance is useful evidence to report and investigate,
not a reason to expand this task into lifecycle policy or suppress the check.
The main change covers accounting updates and affected operation/transport callers.
The proposed container task owns complete aggregate interfaces and live-document
reattribution; the original stage task retains baked-storage and aligned loading,
including application of the settled interface to the baked block. Older Host/TGA/
service changes remain separate consolidation work pending sequencing review.
No additional production implementation is authorised by this split review.

Combined scope across this split, 16 September: preserve container reattribution and
organise its cooperating interface in a separate commented public section; add
live-document reattribution; refactor the baked view onto CMemoryConstView and its
block onto direct CMemoryToken ownership; settle capacity preparation; and add
requested file-load alignment with matching capacity rounding. Include regression
coverage and validation of related edits Ritchie makes. The default JSON service
result remains a baked block. See the [task inventory and outstanding decisions](consolidation_stage_1_specification.md)
and [current stage outline](consolidation_design_order.md).

Scope clarification: only the container set, live document and baked block gain
the public cooperating hooks; live-document erased registration is excluded.
Failed adoption preserves both source and destination. Baking capacity preparation
is mandatory, with unused storage zeroed and ordinary allocation-failure handling.
Loading uses existing memory alignment conditioning, a 16-byte floor and above-cap
rejection. Exact document extent remains separate from rounded capacity.
The baked-document extent limit is reduced to 2 GiB, including borrowed views.
Aggregates present one complete reattribution interface for their owned backing
storage, with coherent source contexts, checked cumulative accounting and unsafe
context replacement after ownership/allocator preflight. Accounting discrepancies
are reported but do not themselves prevent replacement. Concrete
hook details remain to specify. Accounting discrepancies are high-priority
diagnostic signals for finding possible leaks or other gaps and their context,
not inherently immediately fatal events. Accounting remains in release builds;
user-supplied logs, including quiescent module-unload reporting, support diagnosis
of local-setup or patch-combination issues. Possible future telemetry is not
selected work. No
general accounting transaction/recovery framework or new consumer error protocol
is selected. The latest clarification requires accounting errors to be recorded
without themselves causing rejection or immediate shutdown: a discrepancy may
originate in a prior update. Reattribution across memory allocators remains
forbidden. The task specification records accounting-gated deallocation and
transport critical assertions for the concrete primitive/caller simplification
plan. Accounting reports high-bit transitions across adjustments of each counter,
using the atomic adjustment's before/after values. Repeated activity in the same
high-bit state does not repeatedly report; clusters from concurrent transitions
are acceptable. These diagnostics do not reject operations or cause shutdown.
Reload does not yet exist; delayed updates and fresh zeroed accounting on
reload are future requirements, not missing behaviour in an existing feature.
Reload implementation is not automatically added to this task. Actual ownership
and module-dependency safety remain separate from diagnostic counters.

Earlier generic wrapper/narrowed-transfer proposals are superseded. The broader
Host/TGA/service allocations below remain consolidation context, not automatic
coding assignments to the current task. Reconcile their scheduling with the
completed task plan at coordinator review. The plan is still being developed
with Ritchie; this documentation update does not start production implementation.

On 15 September Ritchie identified interest tokens as over-engineered and the
trust system as premature. The [design-order scope update](consolidation_design_order.md)
records this reassessment. Earlier detailed notes preserve discussion history;
they do not require implementing the token or trust machinery.

Detailed override/layer specification is also deferred as premature. Those
features remain expected later; the general filesystem image remains current
consolidation scope.

Stage 1 now includes permanent Host ownership: accepted assets are neither
disposed of, recycled, erased nor deallocated until application exit. Host asset
operations require an already owned asset ID. A client must transfer its buffer,
receive the Host's asset ID, then submit a separate operation request using that
ID. This replaces runtime reclamation and combined transfer/save proposals for
the current scope; the broader ownership and operations designs remain deferred.

The existing TGA flow must adopt that separate sequencing in stage 1, including
its affected callers and validation. Stage 3 builds further services on the
resulting contract. Moving a buffer out of its wrapper must make subsequent
view extraction from that wrapper fail.

The stage 1 admission direction distinguishes ordinary rejection before ownership
is consumed from fatal resource/invariant failure after valid transfer. The latter
enters shutdown with safe payload cleanup, rather than requiring a recoverable
ownership-return protocol. Accepted assets remain Host-owned even if receipt
delivery fails. Detailed checks and the acceptance point remain under design in
the [stage 1 specification](consolidation_stage_1_specification.md).

The first data-model implementation supplies live construction, immutable
baking, promotion, writing, parsing and an initial baked-block ownership
bridge. It exposed gaps in Host authority, lifetime, asynchronous operations
and parser reporting. Resolve the contracts needed by the concrete services and
Executive exercise before adding schema consumers; the broader Host lifetime and
asynchronous-operation redesigns are deferred.
The current code is a baseline to refactor, not an approved final interface.

[Framework discussion notes](framework_consolidation_discussion.md) record the
initial client-handle, retention, reattribution, operation and image-resource
concerns, separately from the explicitly agreed client–Host ownership boundary.
They retain open questions while the detailed design develops.

[Design and implementation order](consolidation_design_order.md) now provides the
provisional stage sequence, decision boundaries and targeted research docket.
It guides specification work and does not authorise implementation.

[Deferred consolidation design](consolidation_deferred_design.md) is the separate
resource for later work and superseded alternatives. Its proposals are not
requirements for the immediate specification or subsequent implementation.

Consolidation comprises multiple component designs/specifications to be implemented
separately. This plan coordinates their shared contracts, dependencies and
integration acceptance; design stages need not correspond one-to-one with
specifications or implementation changes.

## Work and review boundaries

- Parser/reporting functional implementation and review are complete in the
  existing parser task. The parameter const and manual style checkpoints are
  reviewed and committed. Shared reporting and the byte-view parser API follow-up
  are implemented for review.
- Carry the remaining consolidation through sequential stage tasks working
  directly in the shared main-branch checkout at `D:\TheMorphicEngine`, without
  separate branches or worktrees. The coordinating task reviews and aggregates
  design decisions, implementation and progress; see the
  [stage workflow](consolidation_design_order.md#stage-task-workflow).
  Specify ownership and completion for the concrete services without
  requiring the deferred general Host reference-counting or authority redesigns.
- Add direct persistence checks and the full Executive exercise against those
  services. The former test-specific message sequence is superseded as an
  implementation plan; retain the coverage goals below.
- Begin a schema vertical slice only when these prerequisites are usable.
  Pause before each commit for review.

## Parser observations and caller policy

The [linter/parser refactoring specification](parser_refactoring_specification.md)
owns the settled requirements and implementation-review details. Its consistency
review is complete. Stage 1 has implemented and validated the linter, grouped
source findings, retained linter statistics, shared output-relative locations
and composed ingestion with linter-failure propagation. Its final review also
consolidated linter state and the source cursor and removed byte-to-string view
conversion. The specification's progress record identifies the completed scope
and validation.

Stage 2 implements native empty names, root kinds, per-string newline metadata,
grouped findings, shared failures, separate capacity estimates, revised grammar
and late caller policy. The final coordinated slice replaces recovery arrays
with public ordinary-array collision extension, removes the protocol and
obsolete reports, and advances the baked format to version 3. That slice is
reviewed and committed as `db85f31`. The following describes the agreed direction.

The agreed direction is caller-selected feature permissions, grouped parser
presence flags instead of statistics, retained linter aggregate statistics,
construction capacity estimates retained separately where useful, distinct
processing and policy outcomes, and shared 1-based line/code-point locations in
the linter's output. All supported
line-break forms are normalized to LF. The linter has no JSON or string awareness;
escaping belongs to the parser/writer within strings and names. Raw quoted line
breaks are accepted string content and reported as a feature. The writer escapes
newlines by default, with per-string metadata to suppress that escaping. The
parser sets this metadata automatically on strings containing at least one literal
source line break; escaped-only line endings do not trigger suppression.
Preserve the metadata through live/baked document transformations. It belongs to
each value occurrence, independently of shared interned text. Suppression covers
all newline forms, and
the writer normalizes every newline to LF before emitting literal or escaped
output. Strict JSON overrides suppression and escapes embedded LF as `\n`,
without modifying the document's metadata. Reparsing strict output derives
suppression as unset from its escaped-only spelling. Structural punctuation
outside strings/names must not be escaped. Names cannot contain newlines in text,
direct live entry or checked baked storage.
Structural errors identify both the beginning of the immediately malformed
element and its failure point. Linter errors identify the prospective line and
code-point column that could not be decoded. Linter failures
retain a location after producing code points, even if output is discarded;
failure before any output is identified explicitly. Both stages use the same
location representation. On linter failure, the parser report copies that
location into its failure-point field and leaves structure start unavailable.
Feature-policy rejection
occurs only after parsing has otherwise succeeded, so structural and other
processing failures take precedence. Undefined CP1252 bytes cause linter failure
without replacement.

Preserve the established logical-NUL storage form, exact Java-style `C0 80`,
without surrogate substitutes. The linter uses SuiteUTF to normalize valid
CESU-8 supplementary pairs to standard UTF-8, retaining source findings. CESU
bytes are not supported for direct document entry or output. Preserve NUL
admission without adding a generic raw-control permission requirement. Default
acceptance is conservative, allowing
Morphic hexadecimal, binary and explicit-positive numeric forms but excluding
relaxed syntax. CP1252 and the supported modified-UTF-8 exception are accepted by
default; undefined CP1252 still fails. Programmatic documents default to an object
root; changing root type in either direction requires an empty root. Detached
nodes and interned strings do not count as root contents. Comment-only input can
produce an empty object, subject to comment acceptance policy. Empty/whitespace-only
input produces an empty object. Root erasure preserves its type; reset restores
an object root.

Support native empty member names using a name-presence flag independent of the
name-string ID in live and baked values. Missing object-entry names or values
are structural errors; explicit empty quoted strings are valid in either role.
Preserve absent-versus-empty names through lookup, translation and writing.
No synthetic replacement name or special identifier prepopulation is required.
Replace the distinct recovered-array representation with ordinary arrays and a separate
public collision-extension operation available to the parser and ordinary users.
Normal insertion still rejects collisions. Array/array collisions create a new
array containing both arrays; incoming non-array values append to the top-level
array. Parser collision extension is a relaxed feature. Retire all old recovery
compatibility and replace its tests; no external consumers depend on it.

The current baseline remains under
[docs/data_model](../data_model/revised_data_model.md). Host cancellation and
optional retention of rejected diagnostic assets belong to the asynchronous
operation contract. They are not prerequisites to drafting or implementing the
low-level parser/report refactor, and no automatic logging is implied.

## Host worker authority and module lifecycle

Module load/unload migration follows the Executive exercise. The broader Host
authority and lifecycle design is deferred; the points below remain design
considerations, not prerequisites to the load/save services or exercise.

- Move DLL loading, binding, initialisation and unloading to the Host worker.
- Define Executive requests for module operations. Executive threads must not
  directly mutate module bindings or Host collections.
- Make the Host worker the authority performing state changes; settle how the
  main Host coordinates requests and receives results.
- Define safe unload while preserving Host assets until application exit. Drain
  outstanding uses and respect module-dependency hazards; unloading must not
  reclaim retained assets or remove code/data needed for their eventual destruction.

## Asset identity, lifetime and type erasure

General lifetime, broader identity and runtime retention proposals are recorded
in the [deferred design resource](consolidation_deferred_design.md). Current work
uses the permanent Host ownership and asset-ID operation boundary in stage 1.

- An asset surviving its originating module must retain no dependency on that
  module's code, type metadata, destructors or allocation context.
- Revisit the public/private split and friendship in memory attribution.
  Keep transfer mechanics private, expose semantic lifetime intent publicly,
  and give narrowly scoped access to erased carriers and necessary Host internals.
  The initial BakedDocumentAsset bridge is implemented; its API is under review.
- Review baked-block/buffer ownership as part of this boundary: whether
  `CBakedDocumentBlock` should adopt a `CByteBuffer`, or whether a general
  interpretation of owned byte storage could replace the distinct block type.
  This is an open design question from the [document style review](document_style_review.md).
- Keep mounting-point hazards separate from identity, ownership, permission
  and provisioning metadata.

## Asynchronous operations, loading and conditioning

The general asynchronous-operation design is deferred. Specify request correlation,
completion outcomes, compact reports and result ownership for the concrete load/save
workflows, including their failure and cleanup paths. A general state/cancellation
framework is not a prerequisite to those services or the Executive exercise.

File loading must carry caller-supplied alignment through the asynchronous
request and platform::filesystem::loadFile, retaining a 16-byte floor. Use
the effective alignment for allocation and capacity rounding, preserve existing
padding semantics, and cover larger alignments and failure handling. Baked
binary views currently require a 32-byte-aligned base.

JSON parsing and baking run in CHostWorkerThread using
thread_ids::bg_conditioning. Keep the live document and scratch on that
thread and retain the loaded text until conditioning completes. Publish only
the completed owned result accepted by the operation's policy. File I/O remains
a separate workload from conditioning.

Resolve the save/load API before encoding it in the functional test:

- Saving names an existing Host asset ID. A client-owned buffer must first be
  transferred and its asset ID received before a separate save request is sent.
- Accepted assets remain Host-owned until application exit, including after
  operation failure. Rejected ownership admission leaves the buffer with the client.
- How are binary output, JSON output and JSON writer options specified?
- Are multiple outputs serial operations or one combined instruction, and how
  are partial success and result ownership reported?
- Where does JSON writing execute under the service contract? The earlier
  proposal put writing in the Executive; do not treat that proposed test
  arrangement as a settled restriction on the consolidated save service.

## File association, discovery and resolution

[Filesystem asset mapping](filesystem_asset_mapping.md) develops configured roots,
discovery categories, editor catalogue use and rescan behaviour. It is a design
discussion; proposed representations and unresolved choices remain explicit.

- Associate an externally supplied source key or path with an identified asset.
  Keep file identity distinct from a loaded buffer or immutable content version.
  Do not assume all source keys were discovered by a scan.
- Add platform-agnostic directory scanning backed by platform implementations.
- Define configured storage roots, startup scans and filename/source-key indexes,
  plus explicit or incremental rescans.
- Define relative-to-requester resolution, canonical logical keys, search
  precedence and duplicate/shadowing behaviour across roots.
- Permit direct user-specified locations to bypass discovery while still using
  the normal Host identity, lifetime and reporting rules after loading.

Schemas and their C/C++ source/header inputs require hierarchy-aware dependency
resolution, so this service precedes schema work. A complete cache/remapping
implementation is deferred beyond the resolution contract needed initially.

## Persistence and Executive validation

The Executive exercise should validate the intended Host and worker services.
Settle the operation contracts first, then define messages and reviewable test
slices. Earlier plans assumed fixed save/load messages and repository IDs;
those mechanics must be reconciled with the consolidation decisions above.

Retain these coverage goals:

- Construct a live fixture with every supported node/payload type, repeating
  types in named, anonymous, array and object contexts as needed.
  Include empty placeholders, numeric intent and boundaries, Unicode/NULs,
  empty names and string values, singleton objects and collision-extension
  array shapes.
- Bake twice independently and compare complete bytes before transferring
  ownership of one copy. Retain the other as the Executive's reference.
- Save and reload binary through the Host, returning the agreed identity and a
  checked borrowed view. Compare complete loaded bytes against the reference.
- Exercise JSON saving, loading, conditioning and policy acceptance; compare
  against the reference using the agreed normalized semantics, including
  collision-array shape/order, singleton normalization and numeric intent/width.
  Account explicitly for strict-output numeric normalization where exercised.
- Cover direct file round trips before relying on the full asynchronous run.
  A loaded binary can remain owned by LoadedFile with a checked baked view;
  an additional baked-block adoption API is not inherently required.
- Correlate each completion and advance only when the preceding operation's
  result permits it. Exercise failures without losing the original failure.
- Verify the transfer receipt supplies the asset ID before the Executive submits
  an operation on it. Reject invalid IDs without affecting retained assets.
- The Executive finishes its operations and relinquishes borrowed views without
  requesting asset disposal. Verify transferred originals, loaded copies and all
  other Host-owned assets remain retained until application exit, including those
  whose IDs were not returned to the Executive and assets used by failed operations.
- Verify operation completion before reporting exercise success. As shutdown
  acceptance, wait for I/O and view users, then deallocate retained assets while
  required code/data and allocation contexts remain available. Release the
  Executive's local comparison copy before DLL shutdown. Runtime reclamation and
  disposal-induced stale IDs are no longer acceptance requirements.

## Order beyond this task

The current [design-stage outline](consolidation_design_order.md) starts with
the limited transferable wrappers, simplified reattribution and aligned loading.
The filesystem image and concrete load/save services lead to direct persistence
checks and the asynchronous Executive exercise. Moving module load/unload to the
Host worker thread follows that exercise. General Host reference counting and
authority/asynchronous lifecycle designs are deferred alongside override/layer
and trust-system detail. Parser/reporting implementation provides the existing
data-model baseline.

The first schema slice should resolve a schema/source and dependencies through
the Host filesystem service, parse under a caller's strictness profile, and
publish an accepted result safe for its requested lifetime. Broader C/C++
parsing, generation, serialisation and remapping follow that slice.
