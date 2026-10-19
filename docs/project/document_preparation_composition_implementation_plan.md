Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   document_preparation_composition_implementation_plan.md
Primary draft: OpenAI Codex
Date:   19 Oct 2026

# Document preparation and composition implementation plan

Status: reviewed and corrected on 19 October 2026; implementation is not
authorised by this plan. Storage-metadata admission is now agreed and recorded
in the design reference. These additions preserve the agreed public interfaces
and existing loading/materialisation behaviour.
Implements the agreed [preparation contract](../schema/persistence-and-aggregation.md#agreed-document-preparation-interfaces)
and [composition contract](../schema/persistence-and-aggregation.md#complete-document-composition),
recorded in `335e4b9` and `62e2ea0`. Prepared instance/bulk merging is complete in
`36ef3d9`. This plan adds its boundary operations without reopening merge,
attachment, switching or persistence contracts. Commits require separate approval.

## Scope and delivery order

1. Establish shared admission and direct reconstruction helpers, preserving
   existing callers and diagnostics.
2. Implement the four agreed preparation overloads and their tests.
3. Implement the three agreed composition overloads and their tests.
4. Verify integrated preparation/merge/composition/construction workflows and
   update documentation to distinguish delivered APIs from future work.

All new public signatures, stages and reasons are those in the design reference.
Preparation receives the original resolved schema; composition resolves its
types document once. Both publish independently owned live documents. No new
owner, schema binding, output-form selector, implicit reconciliation, binary
package creation, general transaction framework or combined convenience API.

## Code placement and concrete reuse

Internal filenames may change during review; public contracts must not.

| Area | Planned work |
| --- | --- |
| `core/schema/data_preparation.hpp/.cpp` | Four public preparation overloads, agreed diagnostics and thin orchestration. |
| `core/schema/document_composition.hpp/.cpp` | Three public composition overloads, agreed diagnostics and one shared implementation. |
| `data_document_validation.hpp/.cpp` | Keep strict prepared validation for merging, composition and preparation's final result check. Share declaration/property admission and checked extent/alignment arithmetic with a separate preparation walk. |
| `baked_instances.cpp`, `baked_bulk_data.cpp` | Extract small locator/count/range admission helpers from `plan`; keep role binding, record ownership, mutable locator publication and loading semantics in their current callers. |
| `live_instances_translation.cpp` | Extract `CInstanceReconciler` and the value-level part of `output_declaration` into a narrow schema-internal reconstruction unit. |
| `live_bulk_data.cpp` | Move `CBulkDataReconstructor` into the same internal reconstruction unit, retaining its existing output and representability checks. |
| `value_codec.hpp/.cpp` | Reuse `validate_declaration`, scalar decoding, `check_value`, `check_alternative` and encoded comparisons. No new writer mode or duplicate codec. |
| `document_merge.cpp` | Extract its small string collection/preload primitives for reuse by copying operations; leave merge equality, matching and presentation selection unchanged. |
| `data_model/document_copy.hpp/.cpp` | Reuse `read_copied_value`, `copy_subtree_impl<true>` and `CDocumentConstruction`; do not add another copier or public editing bypass. |
| `document_query.hpp/.cpp`, `resolved_schema.hpp/.cpp` | Reuse `SDocumentQueryAccess`, typed handle bridges, indexed type lookup, backing identity check and ordinary resolution. |
| `data_block_format.hpp/.cpp` | Reuse format constants and, where useful, factor small metadata-value checks; complete-file admission remains a separate responsibility. |
| `tests/test_suites/` | Add focused preparation/composition cases called from the existing Schema suite; preserve existing regression coverage. |

Prefer a few free internal functions to a configurable traversal framework.
Shared helpers return existing domain/internal diagnostics; public boundaries
translate them into the agreed operation diagnostics. File-local production
helpers use `static`; preserve repository formatting and const conventions.

## Stage 1 Share admission without changing existing behaviour

### Root and selected-section admission

Keep three distinct policies explicit rather than weakening the existing merge
validator:

- Prepared inputs: strict selected-only root, no persistence metadata; used by
  merging and composition data inputs.
- Preparation inputs: recognised section containers plus supported storage
  metadata. Shallow-check sibling section container shapes, but do not traverse
  their contents or resolve embedded types. The supplied schema is authoritative.
- Composition types input: require the types-only root and `types` object before
  ordinary resolution. Do not rely on the resolver implicitly enforcing this
  narrower composition contract.

Keep the existing `validate_data_document` strict, with its failure precedence
and complexity unchanged. Separating its root check is insufficient for general
preparation: its entry checks also reject locators and absent bulk descriptions.
Use a separate, small preparation admission walk sharing leaf validation rules;
do not add switches throughout the prepared walker to admit every storage state.
Do not copy a combined document just to pass the prepared validator. Composition
and preparation's final result validation use it unchanged.

The section traversal retains one indexed type lookup per group, fixed-size
property checks, cheap visited-depth checks and checked packed extent arithmetic.
Instance declarations remain sparse; bulk descriptions must be complete. Use
original source occurrences throughout validation for direct diagnostic mapping.

### Storage metadata and whole-entry rules

Implement a small preparation admission walk using the existing reader. Share
locator field parsing and checked count/size/alignment/range rules with baked
role planning, but do not route through attachment or temporary baking. Existing
role callers retain their own record types and mutable-materialisation behaviour.

| Input route | Admission and action |
| --- | --- |
| Document-only, no storage metadata | Validate retained descriptions directly; absent instance declarations inherit, absent bulk descriptions fail with binary required. |
| Document-only, supported preconstruction locators | Validate field kinds, counts, sizes and existing placeholder rules; do not publish or repair locators. Reject claims requiring an unavailable binary package. |
| Supplied binary | Validate applicable source locators and payload spans before reading bytes, including zero-size entries, positive bulk counts, ordering/overlap and size ceilings. Do not invent offsets for unlocated bytes. |
| Stripped instances | Require the supplied route even for zero bytes; reconstruct against original defaults and immediate parents. |
| Bulk entry with a retained array | Validate every record and count; preserve the array. Incomplete records, empty arrays and count mismatches follow existing rejection rules. |
| Bulk entry without an array | Require supplied binary and adequate validated count/size metadata; reconstruct the whole sequence. |

Preserve existing count derivation where supported, including counts inferred
from nonzero record size; zero-size records cannot derive a count from bytes.
Mixed complete and reference-only bulk entries are allowed. A shorter retained
array is not a prefix to fill, and missing members are not holes to reconstruct.
Stripped-marker restrictions still apply to the whole selected domain.

The agreed detailed rules linked below settle which root and locator metadata
can be removed. Extract value/extent checks
from existing callers, not their whole admission policy: preparation never needs
mutable source storage or publication of calculated locators.

A query and payload view do not expose a complete file allocation: physical file
length, document-to-payload padding and full-buffer validation stay with block
loading. Preparation validates the supplied description and payload relationship;
it must not invent an enclosing file or re-run whole-file validation on a prefix.

After structural and storage admission, validate each retained value through one
source-value path: `validate_declaration` when comparison is disabled, or
`check_value`/`check_alternative` when enabled. The comparison functions already
validate supplied values; do not first repeat declaration-only validation. Reuse
record-level wrappers only where the record shape and diagnostic ordering fit.
For specialisations, compare against the immediate parent binary snapshot,
matching existing checks. Never reconstruct retained declarations to compare them.
Unrepresentable values used in reconstruction fail regardless of the comparison
flag. The agreed final prepared-result validation remains a separate required pass.

Keep operation-local entry metadata only where later comparison, reconstruction
or parent access needs it. Use traversal order and parent ordinals rather than
name searches or rescanning ancestors. No persistent lookup cache is needed.

Checkpoint: existing attachment, materialisation, switching and persistence tests
retain their results and diagnostics after helper extraction. Add parity cases
for shared metadata helpers before using them in the new public operations.

### Agreed explicit storage-metadata admission

The [storage-metadata admission contract](../schema/persistence-and-aggregation.md#agreed-preparation-storage-metadata-admission)
was agreed on 19 October 2026. Implement its root/envelope and locator tables,
count derivation, payload checks and diagnostic precedence as specified. The
design reference is authoritative; do not infer a broader policy from existing
loader or materialisation entry points. Descriptions and unbound placeholders
can stand alone; valid locators or envelope claims require an explicit package.

Add admission-table tests covering both input representations, all envelope
presence combinations, false locators in immutable baked documents, nonzero
placeholder offsets, valid zero-byte locators, gaps/trailing slack, wrong-domain
identifiers and ignored sibling contents. Reconfirm that existing loaders retain
their original whitelist, mutability rules and diagnostics after shared extraction.

## Stage 2 Share reconstruction and build prepared documents directly

Extract the existing algorithms with their behaviour unchanged:

- Instance reconstruction receives the target document, resolved type/schema,
  source bytes and original-default or parent baseline. Preserve the single-pass
  lazy-container/pending-array-prefix algorithm; do not compare a subtree twice
  merely to decide whether to emit it.
- Separate baseline preparation from the role member access in
  `output_declaration`. Borrow parent bytes directly. Construct base-default
  scratch lazily when a stripped base actually requires reconstruction, then reuse
  it within that type group. Retained-only preparation allocates none. Do not add
  a persistent cache or hold snapshots for all types.
- Keep `check_value`/`check_alternative` verification of reconstructed declarations.
  An equal subtree must not bypass representability checks. Return an absent
  declaration with no error when all meaningful values match the baseline.
- Bulk reconstruction retains complete records, positional singleton output,
  scalar decoding, expansion bounds and per-record reproduction checks.
- Route existing promotion/reconciliation/output callers through these same
  helpers; they retain their original ownership and publication responsibilities.

Single-pass reconstruction means no repeated subtree comparison to decide what
to emit. It does not remove the subsequent reproduction checks. Even an absent
reconstructed declaration must pass them: final document validation cannot detect
all invalid source encodings that omission would otherwise conceal.

Preparation copies retained declaration/record subtrees verbatim using the native
copy helpers. Build group, entry and specialisation containers directly in staging,
omitting storage fields from the outset. Do not copy locators and erase them later,
minimise retained descriptions, reconstruct complete retained entries, or promote
into a temporary role whose payload would immediately be discarded.

Retain empty groups and specialisation containers and their supported presentation.
Create the selected root section even when absent in the source. Missing and empty
declarations remain distinct wherever the source format distinguishes them.

### String preparation and construction costs

Extract the merge engine's collection and sorted preload primitives without its
merge policy. Collect property names and string values only from copied subtrees
and structural names being emitted; preparation must not scan unrelated sections
or import their entire string tables. Composition collects across its two or three
selected sections in one operation-wide batch.

Reconstruction also emits schema member/field names, enum label strings and the
codec's fixed exceptional floating-point spellings. Include these candidates
before the single sorted preload. Collect relevant reconstruction type metadata
once per distinct reachable type, not once per entry/record/array element. Use an
iterative worklist and an existing ordered container keyed by `CSchemaIndex`
identity to visit each type once. Include internal array types and referenced
types: `definition_count`/`definition_ordinal` cover named definitions, not every
type the reconstructor can reach. Traverse array element type metadata once,
never its element count. This is string preparation, not a depth-prediction graph
pass; do not apply a recursive construction-depth limit to this metadata walk.
Retained-only preparation skips this collector entirely. Keep the copied-subtree
collector and reconstruction-metadata collector as two small paths feeding the
same preload, without a generic visitor framework or new hash table.

Combine retained and generated candidates before sorting, deduplicate, then import
each domain in lexical order. Successive independently sorted batches can still
shift the destination's existing sorted index and must not replace this combined
batch. Release candidate vectors before node emission. Borrowed strings remain
valid because sources and schema outlive staging construction; retain no second
text-to-ID map after preload. Extra unused candidate names from reconstruction
metadata are acceptable; reconstructing values twice to find exact strings is not.

Use native unique-name/disjoint-subtree invariants with `CDocumentConstruction`
and `copy_subtree_impl<true>`. The latter's creation callback can reuse prepared
string IDs/imports and preserve integer and newline presentation. Cross-document
IDs are local: translate through the destination's existing lookup rather than
comparing unrelated IDs or retaining source IDs in output.

Carry enclosing section-relative document depth through copied subtrees; do not
restart at each declaration or record array. Apply cheap emitted-document depth
checks during reconstructed node creation as well, independently of the codec's
value-depth accounting. This retains the merge cores' selected-section budget
without an extra maximum-depth scan or a schema-graph construction preflight.

Aim for linear traversal/node emission plus sorting and logarithmic string/type
lookups. A bounded string collection pass is intentional; repeated sibling scans,
per-record schema scans and full payload copies are not. Do not promise linear
time in node count alone while ignoring string lengths or existing resolver cost.
Do not add new hashes, cursor infrastructure or data-model friend access.

### Preparation publication and diagnostics

1. Check schema readiness and reject destination backing `source_schema` with
   the existing identity bridge before any mutation.
2. Admit source metadata and retained descriptions, with optional embedded checks.
3. Prepare strings and build an independent selected-only document, reconstructing
   only descriptions required by the agreed rules.
4. Validate the staged document with existing strict prepared-data validation.
   This required result pass also checks packed output extent after source gaps
   and storage metadata are removed.
5. Publish through non-failing move assignment after all source reads finish.

Preserve occupied destinations and all handles on failure; source/destination
aliasing is supported. Keep source locations for admission/reconstruction errors;
map reconstruction failures to the original entry if no finer source node exists.
Clear temporary result occurrences, retain domain reason detail and report resource
causes only where known. No provenance map or rollback actions. Use the shared
recursion-depth constant with existing operation-relative counting; no new
automatic maximum-depth query or implicit construction-depth preflight.

## Stage 3 Compose already prepared sections

Implement the overloads through one private helper with optional internal query
pointers for omitted data domains. Keep the public interface as the three agreed
reference overloads; do not expose optional state or a new source aggregate.

1. Enforce the types-only root, then resolve once into a local `CResolvedSchema`.
2. Validate instances and bulk data, when supplied, using the existing strict
   prepared validator and this same resolution, in that order.
3. Preload strings once across all supplied sections, then copy each section once
   into staging in `types`, `instances`, `data` order. Emit empty supplied data
   sections and omit unsupplied domains. Use copying, not a merge with an empty
   source or three independently built documents followed by another copy.
4. Release the local resolver and other borrowed construction state before the
   final move into the possibly source-aliased destination.

No second resolution or semantic result-validation pass: copied sections have
already been validated together. Checked copying still reports storage/depth and
allocation failures. Type-local identities are not persisted; all document names
and strings must belong to the output. There is no caller-supplied resolved schema
to protect with the preparation alias guard; external borrowers still follow
ordinary caller-managed replacement rules.

Populate only the corresponding diagnostic detail field, preserve original
source occurrences where available, and clear the diagnostic on success. Never
retain temporary handles. Do not add merge conflict reasons, binary validation,
default expansion or result-validation stages to the composition contract.

## Stage 4 Behaviour, failure and performance verification

Use focused test files registered with the existing Schema suite and normal build
manifests. Reuse merge fixtures, allocation-failure injection and live/baked
document helpers instead of another test framework.

### Preparation cases

- Exercise all four overloads with live and baked documents, empty/missing selected
  sections, supported metadata and invalid roots/properties/section containers.
- Change embedded type definitions/defaults and invalid semantic content inside
  sibling sections without changing selected output. Reject unknown root fields
  and malformed sibling section containers. Large unrelated sibling contents
  must not add traversal, string candidates or reconstruction work.
- Preserve retained default-equal declarations, omissions, specialisations,
  empty containers, integer notation/prefix/digits and newline-escaping choices.
- Reconstruct stripped bases and children against original defaults/immediate
  parents; include children restoring a schema default, changed nested members,
  shortest necessary array prefixes and unchanged declarations.
- Cover mixed described/reference-only bulk entries, full reconstruction, absent
  versus empty arrays, short/long counts, incomplete individual records, nested
  arrays, singleton forms and zero-size records with explicit counts.
- Reject malformed locators, overlap, invalid alignment/ranges/size claims,
  stripped-without-binary and metadata that would otherwise disappear unchecked.
  Distinguish an explicitly supplied empty package from the no-binary overload.
- Retained mismatch fails only when comparison is requested; raw edits do not
  silently replace descriptions. Reconstructed noncanonical booleans, unknown
  enum codes and disallowed fp16 values fail even when comparison is disabled.
  Preserve existing permitted NaN, padding and unused-bit comparison semantics.
- Test explicit depth boundaries and implicit default-construction failure during
  stripped reconstruction; no stack overflow or partially published result.
- Exercise a declaration near the section-relative depth boundary so nested
  copying cannot reset its budget. Cover internal arrays and shared type dependency
  graphs in string collection, with each type visited once and no array expansion.

### Composition and integration cases

- Exercise all three overloads and every live/baked combination (four for each
  two-source overload and eight for the three-source overload).
- Check types-required admission, invalid schema, strict domain roots, storage
  metadata rejection, unknown data types, incomplete bulk records and ordering.
- Verify fixed section order, omitted versus supplied-empty sections, exact
  presentation preservation and independence after sources are destroyed.
- Confirm no default expansion: a shallow omitted deep value can compose even
  if later binary construction fails at the existing implicit depth boundary.
- End-to-end: prepare under original schemas, merge types, merge prepared data
  under the merged schema, compose, then construct via existing domain paths.
  Include adopted defaults, retained explicit default-equal selections, stripped
  specialisations and reconstructed bulk records. Use preparation to extract
  composed sections where downstream strict inputs require it.

### Transactional and scaling evidence

Inject failure at successive allocation points until success, for empty and
occupied destinations and each admissible source alias. Verify source bytes,
payloads, bindings and destination handles remain unchanged on failure. Test
preparation's schema-backing rejection and composition aliasing its types source.
Check failure stages/detail lifetimes and clearing on subsequent success.

Measure geometrically increasing entry counts, wide/reversed/repeated strings,
sparse retained declarations and repeated reconstructed types. Allocation and
work counts should expose repeated copying, per-record schema walks or sorted
index shifts; timings are supporting evidence, not brittle test thresholds.
Composition performs one resolution, one validation per supplied domain and one
node copy per section; a separate string-collection traversal is expected.
Preparation may perform required source and result validation, but never copies
the supplied payload. Default/reproduction scratch remains bounded by the value
being reconstructed rather than the sum of all record bytes.

Run targeted Schema cases during each stage. Once integrated, build and run the
ordinary suites in Debug/Release x64 and Win32, including existing merge,
reconciliation, switching, persistence and data-model regressions. Verify Debug
depth cases after helper extraction, keeping recursive frames small. Run CMake
source extraction checks when source registrations change. Do not repeat a
passing matrix without a new change or unresolved concern.

## Documentation and review checkpoints

- Register this plan and all eventual sources/tests in Visual Studio item/project
  files and appropriate filters; preserve CRLF, BOM and terminal-newline state of
  Visual Studio-managed item/filter files.
- The plan review narrowed shared admission, removed duplicate source-value
  validation, made reconstruction scratch lazy and specified generated-string
  collection and cumulative document depth. Storage-metadata admission is agreed.
  Check implementation against these decisions; resolve any newly uncovered
  contract ambiguity explicitly rather than inventing input semantics.
- After delivery, update runtime API, specification, validation notes, milestones
  and backlog to mark preparation/composition implemented; retain this plan as
  delivery history with actual test and performance evidence.
- Check documentation links, filter visibility, whitespace and line endings.
  Preserve user formatting and keep manual review and commits separate from
  implementation authorisation.
