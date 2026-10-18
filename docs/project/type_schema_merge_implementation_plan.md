# Type schema merge implementation plan

Status: reviewed and revised on 18 October 2026; implementation has not started.

Implement the agreed
[type-schema merge contract](../schema/persistence-and-aggregation.md#type-schema-merging-and-defaults)
and [public interface](../schema/persistence-and-aggregation.md#agreed-type-schema-merge-function-and-diagnostics).
Two independently valid live/baked document views produce an independently owned,
validated live type-schema document. Preserve supplied-default presence and
presentation, and publish only after all fallible work succeeds.

This plan covers type-schema merging only. Instance/bulk data aggregation,
stripped-input promotion, composition, binary reconstruction and convenience
orchestration remain separate work. Existing schema attachment, switching and
bound-owner replacement rules remain unchanged. Preparing this plan does not
authorise implementation or commits.

## Code placement and reuse

| Area | Planned change or reuse |
| --- | --- |
| `core/schema/schema_merge.hpp` | New public declaration and the agreed merge diagnostic enums/structure. Include the existing query/resolver diagnostic types; no new document owner. |
| `core/schema/schema_merge.cpp` | Operation-local preflight, document merge, diagnostics and publication. Use static file-local helpers and private implementation state. |
| `core/schema/resolved_schema.hpp/.cpp` | Reuse `CResolvedSchema::resolve(const CSchemaDocumentQuery&, SDiagnostic&)` for each input and the completed result. Reuse indexed type lookup and source declarations. No resolver semantic changes are planned. |
| `core/schema/type_compatibility.hpp/.cpp` | Reuse one `CTypeCompatibility` with `ETypeMatch::representation` across overlapping definitions. Keep its operation-local cache; do not use effective-default `definition` equality. |
| `core/schema/document_query.hpp/.cpp` | Use the existing read-only view for mixed live/baked inputs and source-scoped handles. No public view extension is initially required. |
| `core/data_model/document_copy.hpp/.cpp` | Reuse scalar extraction, internal creation/import/append helpers and the bounded subtree-copy walk with an operation-local creation callback where needed. |
| `core/data_model/live_document.hpp/.cpp` | Reuse live ownership, integer metadata checks, string interning and non-failing move assignment. No new friend or public unchecked construction API. |
| `tests/test_suites/SchemaMerge_test_cases.hpp/.cpp` | New focused cases invoked from the existing Schema suite, following the separate data-block test-case arrangement. |
| Visual Studio items and filters | Register production files, test cases and documentation in the existing Core, Tests and Docs locations. Preserve CRLF, encoding/BOM and final-newline state. |

Keep new merge mechanics local to schema merging. Do not extract a generic public
document-merge framework in anticipation of future data aggregation. Reuse
`CResolvedSchema::prepare_output` only for its existing callers; it must not
normalise the merge inputs or result.

## Stage 1 Source validation and compatibility

1. Add the exact agreed API and diagnostic definitions, with `none = 0u` as the
   first enum member. Reset diagnostics on entry and leave them clear on success.
2. Resolve the first source, then the second, into separate temporary resolvers.
   Preserve detailed source diagnostics on failure. A missing dependency cannot
   be supplied by the other input. Do not add a second schema validator.
3. Locate each source's `types` section through the existing query. Resolution
   validates the schema and its permitted root shape; this operation does not
   validate or merge instance/bulk data content. Produce only the `types` section
   in the result, with no copied `instances`, `data` or `stripped` root fields.
4. Walk named declarations in document encounter order. Use the other resolver's
   indexed type lookup to find overlaps across the complete type-name domain,
   including collisions between categories. Do not use sorted resolver output
   to establish result order.
5. Compare each overlap with the shared representation comparator. Report both
   original type declarations for incompatibility. Treat comparison allocation
   failure separately from a mismatch. Source-local numeric identities are never
   compared across owners as though they were shared identities.

The gate preserves member/enum order, referenced type identity, array extents,
bit-field interpretation and resolved layout. Natural and explicit declarations
with the same effective layout pass. Semantic metadata and explicit defaults
are checked during document merging, not silently ignored because representation
comparison passed.

Checkpoint: tests demonstrate independent source rejection, cross-category name
conflicts, compatible natural/explicit layout and incompatible layout/reference
changes. No destination mutation occurs during preflight.

## Stage 2 Document merge and construction

Build one fresh staging document. Preserve first-source encounter order and append
second-only fields in their encounter order at each object. Copy one-sided
subtrees directly; merge shared fields once. Arrays supplied by both sources
require equal lengths and are traversed with paired sibling cursors, not repeated
ordinal searches. Resolve type-name references normally in the final document;
no persistent schema-index translation table is needed.

Apply the existing document rules precisely:

- Missing fields contribute no value. In particular, preserve an explicit default
  supplied by only one source and leave a default absent when neither supplies it.
  Keep defaults within the existing grammar: scalar defaults and nested arrays
  whose base type is primitive or enum. Two supplied arrays follow the recursive
  array rules; do not expand omitted tails or substitute effective defaults.
  Nested structures and bit structures obtain defaults from their own member/field
  declarations, not object-valued aggregate defaults on an enclosing member.
- Compare integers exactly across signed/unsigned domains without floating-point
  conversion. Keep negative values distinct from large unsigned values.
- Compare finite floating-point representations, including signed zero, using
  the existing bit conversion helpers. Do not use binary-data NaN equivalence or
  typed value conversion for document equality.
- Require equal scalar kinds apart from the permitted integer storage-domain
  distinction. Strings compare decoded contents exactly; special-value strings
  retain their case-sensitive document meaning.
- Combine integer notation, minimum digits and prefix choice under the agreed
  presentation rules. Keep storage-domain/width metadata valid independently of
  the winning presentation. For equal integers, retaining one valid source
  storage representation and applying the winning presentation avoids unnecessary
  canonicalisation. Newline escaping wins unless both strings suppress it.
- Preserve one-sided values and presentation unchanged. Conflicting supplied
  semantic metadata remains a conflict only where the existing schema grammar
  admits that property. Unsupported properties, including arbitrary descriptions,
  and unsupported aggregate defaults remain source-validation failures. Do not
  extend the resolver to make generic document-merge examples valid schemas.

Use `read_copied_value` for existing scalar admission/metadata handling and
`CDocumentConstruction::create` / `append` for freshly built nodes. The merge
establishes unique object names and disjoint child ownership before unchecked
append. Arbitrary public editing and reader-adapter paths remain checked.
Preserve object-entry identity independently of name text, including empty names
where the underlying format permits them. Use existing container/allocation limit
checks. Apply one document-depth budget to preparation, merging and subtree copying:
count the `types` subtree root as depth zero, consistent with existing schema
output, and reject depth 256 or greater. Pass the current depth to the existing
copy walker when taking a one-sided branch; never restart its budget at zero.
The resolver's dependency/default depth limits remain separate checks, not a
substitute for document-depth accounting. Report a detected document-depth limit
as `document_merge` / `storage_limit` without publishing any staged output.

### Avoid repeated lookup and string work

Handle empty/singleton shared objects directly without allocating a lookup table.
For small objects, use bounded linear matching under a small fixed child-count
threshold; apply that path only when both objects fit the bound. Keep consumed
state in fixed local storage. Preserve the same encounter order and conflict
rules on every path, with cases covering the threshold boundary.

For larger shared objects, collect the second source's children once into an
operation-local table, sorted by that source's property-name ID. Translate each
first-source name with the second query's logarithmic `find_property_name_id`,
then binary-search the table. Track consumed entries and emit second-only entries
in original order. IDs are compared only within their source domain. This avoids
unbounded repeated linear `object_child` searches without persistent indexing or
a new hash table. Small fixed root/group lookups can use the ordinary query.

The native copier's `preload_strings()` is for whole baked promotion into an empty
destination and assumes matching source/destination IDs. Do not invoke it twice
for a merge or assume it directly supports `CSchemaDocumentQuery`, which lacks
the native string-ID/rank accessors.

Instead, collect borrowed name and string views from the two selected `types`
subtrees, including generated root keys, in a bounded preparation walk. Sort and
deduplicate each string domain using the existing string-table ordering, then
import in order into the empty staging document through `import_name` and
`import_string`. This avoids repeated sorted-index shifts and normalisation.
Sources remain unchanged and alive until publication, so borrowed views remain
stable even when the destination aliases a source.

Release the preparation vectors immediately after preloading. Do not retain a
second text-to-destination-ID index: `import_name` and `import_string` already
find preloaded values logarithmically without normalising or inserting them again.
Use these helpers in the existing copy walker's creation callback, together with
scalar extraction and presentation selection. Both shared and one-sided branches
use the same value-construction helper. Do not write another subtree copier or
use the public arbitrary-reader copy path, which would reintroduce checked
construction and repeated normalisation.

The bounded prepass and merge traversal each visit nodes a constant number of
times; do not repeat preparation separately for every copied subtree. Names and
string contents are inserted once per distinct value, while construction performs
existing-table lookups per occurrence. No additional string lookup cache or cursor
abstraction is needed. Reuse neighbour-first lookup patterns only where useful
for the remaining operation-local record indexes, with logarithmic fallback.

The target for new merge work is O(N log N) comparisons and O(N) temporary storage
for N input nodes, with string-byte comparison costs accounted for separately.
Sibling walks and output construction should be linear apart from indexed
lookups. Existing resolver and compatibility costs remain separately attributable;
do not claim a new worst-case bound for their existing caches or containers.

Checkpoint: ordering, omitted defaults, scalar equality and presentation tests
pass, including wide objects, repeated strings and reverse lexical input order.
Review the construction invariants and confirm that strings are inserted once
per distinct value rather than per copied occurrence, and that small object
matching allocates no lookup tables.

## Stage 3 Final validation diagnostics and publication

Resolve the completed staging document using the existing resolver. All result
validation and allocations finish before publication. On failure, retain the
resolver reason/stage but clear every handle and range referring to staging,
including enclosing and related locations. Source-validation diagnostics retain
their original source locations. Conflict diagnostics retain both original
declarations/values and default the nested validation diagnostic.

Use the agreed stage/reason mapping. Propagate allocation/storage-limit reasons
only when the lower-level operation identifies them. Boolean construction failures
use `construction_failed` rather than an invented allocation diagnosis. Return
the first failure and release temporary ownership normally; no rollback edits
to the caller's destination are needed.

Release temporary resolvers, compatibility state and object lookup tables before
the final `destination = std::move(staged)`. String preparation vectors have
already been released after preloading. Do not return a retained
resolved cache or introduce a failing operation after that move. Same-document
inputs follow ordinary validation/merge rules; destination-as-source reads finish
before replacement. Adoption/baking are separate existing operations.

Checkpoint: every failure preserves source and destination content, existing
handles/views and ownership; successful replacement invalidates only the old
destination identities. The result remains usable after both sources are released.

## Stage 4 Regression coverage and integration

Add focused cases to the existing Schema suite using its allocator and document
fixtures. Exercise representative cases across live/live, live/baked, baked/live
and baked/baked input views rather than duplicating every fixture four times.

| Coverage | Required cases |
| --- | --- |
| Input validity | Unready/missing schema, unresolved reference that the other source could supply, invalid default/layout, unsupported structure/bit-structure aggregate defaults, arrays of aggregate defaults, arbitrary description properties, empty valid type catalogue and permitted combined-root input. |
| Structural policy | Same/different categories; changed member and enum order, referenced names, array extent, masks/interpretation; equivalent natural/explicit layout; alignment that changes layout. |
| Default policy | Neither/one/both supplied defaults; equal/conflicting defaults; scalar members, bit fields and partial/nested arrays with primitive or enum base types; defaults declared on members inside nested structures; supplied arrays of differing lengths; explicit zero versus absence. |
| Document equality | Signed/unsigned extremes, differing scalar kinds admitted for the same schema field, positive/negative floating zero, exact decoded strings and conflicting supported metadata. |
| Presentation | Every documented radix/digit precedence example, prefix ties, adaptive retention, valid integer storage metadata and untouched one-sided presentation, using values admitted by the schema grammar. |
| Ordering and identity | Reordered object properties, second-only types/fields, cross-category name collisions and source-local IDs with different meanings. References resolve in the new document. |
| Publication | Empty/occupied unrelated destination, destination as first/second/both source, same source twice with separate destination, preservation on conflict and independence after sources are destroyed. |
| Diagnostics | First/second source failures, both conflict locations, cleared diagnostics after success, no staging handles/ranges after final validation or allocation failure. |
| Failure cleanup | Reject each allocation attempt of a representative successful merge in turn using existing allocator hooks; include occupied/aliased destination cases and final-resolution allocations. Check no leaks or partial publication. |
| Depth | Last accepted/first rejected document depth, shared and one-sided subtrees at the same depth, and preservation on preparation/copy rejection. Keep resolver dependency-depth cases distinct. |
| Growth | Empty/singleton/small objects and both sides of the matching threshold; wide/reordered objects, many distinct types, shared dependency graphs and repeated/distinct strings. Check early release of string preparation storage. Use operation counts where available and scaling observations; avoid timing thresholds as correctness assertions. |

Generic document rules do not expand the accepted schema grammar. Where a value
form (such as arbitrary newline-bearing description text) has no valid schema
fixture, retain the existing document-layer coverage and test source rejection
where applicable; do not add a schema property or public helper solely to exercise it.

For final-result semantic validation, add a natural valid-input failure fixture
if reachable under the agreed policy; do not invent production test switches to
force an impossible invalid result. Allocation failure during final resolution
still verifies cleanup and temporary-location sanitisation.

Add one integration test that adopts/resolves and bakes the result using existing
boundary APIs. Construct instances under the merged schema to show that omitted
values acquire an adopted default while explicit values remain explicit. This
tests the default policy without implementing data aggregation. Confirm existing
bound-schema replacement restrictions remain intact.

Build and run ordinary suites (`MorphicTests.exe -t1`) on Debug x64 during
development. At the completed implementation checkpoint, run the ordinary suites
once on Debug/Release x64 and Win32 using the repository build guidance/script
(`-Platform x86` selects Win32). Broaden or repeat only for subsequent changes or
unresolved failures. No new graphics/Host harness matrix is required for this Core
operation. Check MSBuild source registration and the existing CMake source-list
extraction tests when adding translation units.

Update the runtime API, schema README, validation guide and relevant backlog
entries only when implementation is complete. Record validation evidence and move
completed delivery details into the project milestone record according to the
existing documentation practice. Run line-ending, whitespace, local-link and
Visual Studio item/filter checks. Review code for existing switch/bracing style,
const parameter consistency and declaration/definition order before completion.

## Completion criteria

- The agreed API and all semantic, diagnostic and publication cases above pass.
- Existing schema/data-model suites pass without changes to attachment/switching
  behaviour or public document editing guarantees.
- Implementation review confirms reuse of resolver, representation comparison,
  scalar extraction and construction helpers; no duplicate normaliser, subtree
  copier, transaction owner or persistent provenance index was introduced.
- New object matching and string preparation avoid repeated scans/insertion shifts,
  with no retained duplicate string index or small-object lookup allocations.
  Document any remaining measured costs rather than silently expanding scope.
- Documentation and project/filter registration describe the delivered behaviour.
  Commits and any separate implementing task require explicit user instruction.
