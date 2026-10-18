Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   data_aggregation_implementation_plan.md
Primary draft: OpenAI Codex
Date:   18 Oct 2026

# Instance and bulk data aggregation implementation plan

Status: revised after review; implementation has not started. This plan implements
the agreed [interfaces and diagnostics](../schema/persistence-and-aggregation.md#agreed-data-aggregation-functions-and-diagnostics)
and [validation/publication contract](../schema/persistence-and-aggregation.md#agreed-data-aggregation-validation-and-publication).
Preparing or accepting the plan does not authorise implementation or commits.

## Scope and boundaries

Implement `merge_instances` and `merge_bulk_data` over prepared live/baked document
views, with one ready `CResolvedSchema` and an independent `CLiveDocument` result.
Preserve declarations, presentation and encounter order; validate both sources
independently and the completed result; publish only after all fallible work.
Use exactly the agreed public stage/reason enums and typed diagnostic structures.

The merge core does not resolve or merge schemas, attach data roles, reconcile
retained declarations, read payloads, or construct a binary package. Original-schema
promotion of stripped instances and reconstruction of missing bulk records remain
separate boundary operations. Their public conversion interfaces, composition and
combined orchestration are not part of this implementation plan. Tests may prepare
ordinary document fixtures directly without inventing those interfaces.

Type merging is complete in `ca087e2`; data interfaces were agreed in `3e6b0cf`.
Refactoring shared internals must preserve type-merge behaviour and diagnostics,
and must not reopen attachment, switching, persistence or bound-owner contracts.

## Code placement and reuse

Proposed internal filenames may be adjusted during implementation without changing
the public contract; keep helpers narrowly scoped to the actual three consumers.

| Area | Change or reuse |
| --- | --- |
| `core/schema/data_merge.hpp/.cpp` | Both new public functions, shared merge enums, typed diagnostics, orchestration and publication. No new owner or output-form selector. |
| `core/schema/document_merge.hpp/.cpp` | Extract the existing type merger's private document traversal, equality/presentation, string preparation and construction into shared schema-internal machinery. No public generic document-merge API. |
| `core/schema/schema_merge.cpp` | Retain independent schema resolution, representation compatibility, schema-specific diagnostics and final resolution; delegate only document merging to the extracted machinery. |
| `core/schema/document_query.hpp/.cpp` | Provide narrow internal access from the selected typed queries to their existing `CDocumentRead`. It already supplies integer metadata, newline presentation and property-name lookup; reuse `SOccurrence` and existing typed handle bridges rather than adding duplicate public forwarding solely for the merger. |
| `core/schema/value_codec.hpp/.cpp` | Add complementary internal declaration validation sharing lower-level admission helpers with construction/comparison. Keep their public entry points and writer modes intact; do not add a third operation throughout the writer. |
| `core/schema/data_document_validation.hpp/.cpp` | A small prepared-document walker with narrowly shared entry/property and checked extent helpers. Share admission rules rather than forcing different storage contracts into one configurable loader. |
| Existing baked/live role paths | Change only genuine common-helper call sites; retain locator, payload, binding and record-gathering responsibilities and existing diagnostic precedence. Broad loader rewrites are not a prerequisite. |
| `core/data_model/document_copy.hpp/.cpp` | Reuse `read_copied_value`, `CDocumentConstruction` and `copy_subtree_impl<true>`. No new friend access to `CLiveDocument`, normaliser or subtree copier. |
| `tests/test_suites/DataMerge_test_cases.hpp/.cpp` | Focused cases invoked by the existing Schema suite; retain type-merging cases as extraction regressions. |
| Visual Studio projects/filters and documentation | Register new source/test files and the plan; update runtime documentation only when implemented. |

## Stage 1 Establish prepared-input admission and shared validation

### Accepted prepared-input and lifetime boundaries

Plan review settled these admission and lifetime details:

- Preserve the existing role convention that an absent selected section is empty;
  always emit the selected object section in the output. Reject a present section
  of the wrong kind. Empty groups/sections remain valid; a present bulk entry with
  an empty record sequence remains invalid, matching the current positive-count rule.
- Accept only the selected section in a prepared input root. Reject locators,
  stripped markers, persistence-envelope fields and unrelated root sections rather
  than silently perform conversion or discard unvalidated storage metadata.
  Callers prepare the selected section before invoking the merge core. Conversion
  should extract that section directly, not introduce an extra copy solely to
  normalise its root.
- The destination must not be the backing document of the supplied resolved
  schema. Source/destination aliasing remains supported; do not prohibit harmless
  read-only sharing generally. A narrow internal identity check rejects schema
  backing replacement as `preconditions` / `invalid_schema`, before any mutation.
  Do not add a hidden schema copy, new binding or general alias registry.

### Declaration-only value admission

1. Add an internal validation entry point using the existing document reader,
   resolved type, declaration occurrence, construction semantics and
   `SValueDiagnostic`. Keep raw handles internal and map them to typed diagnostics
   at the operation boundary. No public validation overload family is needed.
2. Implement a complementary declaration traversal. Keep `CValueWriter` responsible
   for constructing/comparing snapshots; do not add validation branches around
   payload access, inherited storage, default expansion or bit-word assembly.
3. Extract small common helpers for scalar/enum conversion, bit-field constraints,
   compound-form classification, named/positional selection and array/completeness
   rules. Split pure shape admission from writer-specific selection machinery.
   The declaration validator walks supplied fields and uses indexed schema lookup;
   it must not iterate every schema member for each sparse instance or build the
   writer's sorted selection index merely to visit declarations. Writer/comparison
   paths retain their existing selection order and diagnostic precedence.
   Validation never reads/writes payload pointers, compares encoded bytes, copies
   parent snapshots or allocates payload-sized scratch. Share schema/declaration
   preflight independently of pointer/span checks; never supply fake storage.
4. Validate only explicitly supplied instance selections. Resolved defaults are
   already valid; missing specialisation selections inherit from a separately
   validated parent. Do not materialise omissions or walk every omitted element
   of a large array merely to validate a short initializer. Complete bulk records
   must still supply every required component, including zero-byte components.
5. Preserve the existing named-element, singleton shorthand, enum-label,
   normalised-bit-field and null rejection rules. Retain the same diagnostic
   precedence for malformed supplied values in writing and comparison.
6. Retain inexpensive depth checks on explicit declaration paths already visited.
   Resolution returns early for previously resolved types, so a ready schema does
   not prove that every later implicit construction path fits the codec's depth
   budget. Leave implicit depth failures to later binary construction; do not add
   a depth-summary cache or schema-graph pass to predict them. Existing construction
   limits remain unchanged, and merge success does not guarantee later binary
   construction or allocation.

Checkpoint: parity cases exercise the same declarations through validation and
existing construction/checking, allowing their intentionally different storage
requirements. Invalid inputs produce the expected reason/location. Large omitted
array tails require no payload allocation or per-element expansion.

### Agreed construction-depth policy

Retain the current limit while implementing aggregation. The codec starts a value
at depth zero and rejects depth 256. This measures nested by-value construction,
not the number of records, array elements, sibling members or instance ancestors.
The selected document section has a separate document-depth budget.

Validate cheap depth limits on explicit declaration paths already visited, and
allow implicit construction-depth failures during later binary construction.
Do not add a depth-summary cache, extra schema-graph traversal or omitted-value
expansion solely to predict those failures. Cached type resolution does not prove
that every implicit path fits the codec's depth budget; a valid merged description
may therefore fail later construction on that implementation limit.

Binary construction is a separate fallible operation. A depth failure there can
discard its private partial output without undoing a successfully published merged
document. A later combined operation would stage its entire result before
publication under its own contract.

This accepts potentially wasted construction work on unusually deep inputs in
exchange for avoiding extra validation, scratch allocation and maintenance on
successful merges. Deep by-value nesting is expected to be unusual in ordinary
authored data; no representative measurements establish its frequency. Large
sibling/record counts do not imply deep construction.

Test a dependency-first chain that resolves but exceeds construction depth: a
shallow declaration omitting that subtree must merge successfully, then fail during
later binary construction without changing the merged document. Keep explicit
declaration-depth rejection tests and legal skipped zero-byte array paths, so the
validator neither misses visited-path limits nor rejects paths construction skips.

Removing or raising depth limits is not selected by this plan. It would require
separate consideration of recursive construction, comparison, reconciliation,
schema/document output and copying, rather than deleting this one guard.

### Entry, hierarchy and extent admission

Existing `CBakedInstances::prepare` and `CBakedBulkData::prepare` combine document
shape, schema lookup, locator admission and record gathering. They cannot be
called unchanged: they require baked storage, a binding and locators.

Use a small prepared-document walker. Extract common property checks and checked
count/extent arithmetic only where callers genuinely share the rule. Keep binary
locator validation and role record ownership in their existing callers. Different
prepared/loaded entry whitelists may remain local; do not make broad rewrites of
the loaders or live creation paths a prerequisite, or replace them with a universal
configurable traversal framework.

- Validate named type groups and entry shape, using resolved indexed type lookup.
  Carry a group's resolved type through its entries and specialisation subtree.
- Instances admit only declarations and specialisation containers in prepared
  entries. Traverse parents before children with an explicit bounded frame stack.
  Validate each declaration once; do not build effective parent snapshots.
- Bulk entries require an outer `data` record array and complete records. Derive
  count from that array; retain existing named-record/singleton interpretation.
  Nested arrays are ordinary typed values, never outer record sequences.
- Share checked count/extent/alignment arithmetic with existing paths. Check
  record/index limits and prospective packed payload bounds without allocating or
  emitting locators. Include overflow and zero-byte types. Validate the merged
  result again because its combined entry count/extent can exceed either input.
- Use native document uniqueness guarantees; do not reintroduce unbounded sibling
  name scans. Fixed-property entry checks remain small. Return source occurrences
  directly rather than baking/copying input merely to run a validator.

Checkpoint: prepared live/baked admission agrees; existing role loading,
construction and schema-switching tests retain their behaviour and diagnostics.

## Stage 2 Extract and reuse the document merger

Move the existing merge engine without first changing its semantics. Keep the
type-schema wrapper's validation/compatibility work outside it. Use one internal
read representation for traversal and return an internal failure with source
occurrences; thin wrappers map to schema, instance or bulk diagnostics. A narrow
internal bridge to each query's existing reader is preferable to a new public
adapter or three copied merge engines. Do not allocate translated input trees.

Retain these established mechanics:

- Mathematical signed/unsigned integer equality, exact finite-float bits,
  matching scalar kinds and exact decoded strings. Presentation preferences merge
  independently of stored integer domain/width. Preserve named-array entry identity.
- First-source object order followed by second-only encounter order. Empty-object
  direct copying, bounded small-object matching, and larger sorted second-source
  name-ID tables. Compare IDs only within the correct source domain.
- The explicit shared-subtree work stack, with the existing copy walker for
  one-sided subtrees. Both paths use the same scalar extraction/construction helper.
- One operation-wide name/string preparation pass over the selected subtrees.
  Include any generated section key, sort/deduplicate borrowed views by the string
  table's ordering, and preload into fresh staging. Release preparation vectors
  before node construction. Reuse `import_name`/`import_string` lookups afterwards;
  retain no second string index and do not preload whole unrelated documents.
- One depth budget across collection, merging and one-sided copying: selected
  section root is zero and depth 256 is rejected. Keep value-codec and hierarchy
  limits explicit as separate checks; never restart the copy budget at a branch.

Add a narrowly scoped bulk-prefix policy, using the validated section-relative
location and existing work-item depth to identify an entry's outer record array.
The fixed prepared grammar is section -> type group -> entry -> outer records;
no general traversal state machine or additional per-node context is needed.
Never decide this from the name `data` alone: a record member with that name must
use ordinary rules.
Instance hierarchy and declaration merging already follow recursive object union;
their interpretation remains in validation, not in a second merge algorithm.

Ordinary arrays still require equal lengths and merge positionally. For outer
bulk arrays, walk paired sibling cursors through the common prefix, merging each
record's values and presentation, then copy the longer tail. Preserve first-source
presentation contributions even when the second array is longer. Do not concatenate,
sort records or use repeated ordinal lookup. One-sided subtrees have already passed
input validation and can use direct copying without special traversal rules.

Checkpoint: all existing type-merge tests pass after extraction. New traversal
cases prove that prefix behaviour cannot leak into nested arrays or type schemas.

## Stage 3 Add orchestration, diagnostics and publication

For each public operation:

1. Clear diagnostics and check the ready schema and independent-destination
   backing preconditions.
2. Validate first prepared input, then second, under that schema. Preserve the
   corresponding typed domain diagnostic and original source occurrence.
3. Initialise private live staging; collect/preload strings and merge the selected
   section with the shared engine. Output contains only that section. If both
   inputs omit it, construct the empty section explicitly; the current type merger
   cannot simply be called with two absent handles. Include its generated name in
   string preparation. If one section is absent, copy the present section through
   the ordinary one-sided path.
4. Validate the complete staged result with the same prepared-document validator.
   Preserve diagnostic reason but clear every occurrence into temporary output.
5. Destroy merger/validator scratch and finish all borrowed reads, then publish
   with the existing non-failing live-document move. Clear diagnostics on success.

Implement the accepted diagnostic mapping exactly: invalid schema at preconditions;
invalid source at its validation stage; both source handles for conflicts; resource
reasons only where known; generic construction failure for opaque helper failures;
invalid result at final validation. Preserve internal `storage_limit` information
for the outer reason even where existing domain enums have no identical member.
Do not guess allocation failure from a boolean failure. No compatibility or
conversion stage belongs to these functions.

No rollback edits, published partial state, retained input references or resolved
cache. An occupied destination contributes only if explicitly passed as a source.
Keep typed diagnostics valid for caller-owned prepared conversion results; do not
introduce persistent provenance mapping or attempt to reconstruct original stripped
locations here. Subsequent binary construction is a separate transaction.

## Stage 4 Behaviour, failure and performance verification

Use the existing Schema test context, allocation-failure hooks and live/baked
fixtures. Test representative behaviour in all four input representation pairings.

| Area | Required coverage |
| --- | --- |
| Prepared-input admission | Unready schema/query; approved missing/empty-section rules; wrong kinds, unknown types/properties, rejected storage metadata; independently invalid input that another input could otherwise complete. |
| Instances | Same/different type and name; recursive specialisations under matching/distinct parents; absent declarations; disjoint/equal/conflicting selections; parent changes intentionally affecting omitted child values. |
| Value grammar | Named/positional/shorthand and named-array forms; valid partial instance arrays; complete bulk arrays/structures/bit fields; invalid null, enum label, scalar range and field width; zero-byte types and nested declarations. |
| Document equality | Signed/unsigned extremes, different kinds, signed floating zero, special-string spelling, object-order independence, equal-length array conflicts; equivalent named versus positional input still conflicts. |
| Bulk prefix | Both length directions, equal lengths, conflicting common record, merged common-prefix presentation, preserved tail, nested arrays retaining equal-length rules, named member `data` not selecting prefix mode. |
| Presentation | Radix/digit/prefix and adaptive retention; newline flag combinations; one-sided copy preservation; valid integer domain/width; live/baked mixtures. |
| Publication/lifetimes | Empty and occupied outputs; first/second/both-source aliasing; same input twice with separate output; rejection of destination-as-schema-backing without invalidating the resolver; unchanged inputs on failure; output usable after releasing sources and schema. |
| Diagnostics | Correct first/second validation attribution; both conflict handles; known allocation/storage reasons; clearing after success; no final-validation staging handles. |
| Allocation failure | Fail every allocation attempt in representative instance and bulk merges, including output aliasing and final validation; verify byte-equivalent original documents, retained handles and clean memory attribution. |
| Limits | Shared/one-sided document depth; explicit declaration-depth rejection; successful merging followed by implicit depth failure during later construction for cached dependency-first schemas; legal skipped zero-byte array paths; deep specialisations; checked count/extent overflow, aggregate output extent failures and zero-byte record counts without large allocations. |
| Growth | Wide/reversed groups, many instances/specialisations, long bulk prefixes, repeated/distinct strings, small-object threshold boundaries, large omitted array extents and shared schema dependency graphs. |

Use a naturally reachable final-result failure, such as checked combined extent,
or allocation failure in final validation. Do not add production test switches to
manufacture an otherwise impossible semantic failure.

Integration fixtures should consume merged declarations through existing instance
creation/alternative construction and bulk record creation. Show that omitted
instance values adopt a merged schema default, explicit default-equal values
remain explicit, and child omissions inherit merged parent values. Show that bulk
records remain complete and independent of defaults. Preparation of stripped input
belongs to later conversion API work; do not claim that workflow is delivered here.

New document merge work targets O(N log N) comparisons and O(N) temporary storage,
with string-byte comparisons accounted for separately. Count validation work
against explicit declarations plus necessary schema-shape checks, not imaginary
expanded payload size. Avoid repeated ancestor reconciliation, positional rescans,
per-subtree string preloading or duplicate type lookups per record. Use allocation
observations and scaling fixtures; timings are evidence, not pass/fail thresholds.
Keep existing resolver/container costs separately attributable.

Run Debug x64 ordinary `-t1` suites during development after meaningful changes.
At completion run Debug/Release x64 and Win32 builds and ordinary suites once;
repeat only after new changes or unresolved failures. Include the existing type
merge, construction, switching, reconciliation and data-model suites because their
shared helpers change. Check CMake source extraction when adding translation units.
No new Host/graphics validation matrix is required.

## Documentation, review and completion

- Keep the implementation small: complementary declaration traversal is intentional,
  but duplicate admission rules are not. No generic public merge framework,
  redundant input normalisation, payload-sized validation scratch or new transaction
  owner. Audit helper extraction and preserve existing writer/comparison behaviour.
- Preserve manual formatting. Follow existing bracing, parameter-const and member
  declaration/definition ordering. New test headers credit OpenAI Codex as primary
  implementer; add reviewer acceptance only when actually given.
- Register all added files in the appropriate project/filter locations. Preserve
  CRLF, BOM and final-newline state in Visual Studio-managed items/filter files.
- On completion, update runtime API, validation guide, schema README, design status
  and backlog; record measured costs and validation in completed milestones. Until
  then, these APIs remain documented as agreed but unimplemented.
- Verify local links, project/filter visibility, line endings and whitespace.
  Completion requires all agreed semantics and failure guarantees, plus no
  regressions in type merging or existing role behaviour. Do not implement the
  unresolved conversion/composition APIs or commit without explicit instruction.
