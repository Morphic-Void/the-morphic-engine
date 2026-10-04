Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   implementation-contract.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Sep 2026

# Schema implementation contract

This document owns delivery scope, acceptance checks, coordination and progress.
[design.md](design.md) owns agreed semantics; [runtime-api.md](runtime-api.md)
describes the implemented schema API. The initial-delivery sections describe the
original baked-only resolver. The [staged implementation plan](#staged-implementation-plan)
covers its live/baked refactor and additions, whose private layouts and public
interfaces may change. A written plan alone does not authorise implementation.

## Delivery status

As of 3 October, integration stages 1-8 and the subsequent exact-stride remapping
correction (`0f4612b`) are complete. The stage 9 review was completed on 2 October.
The user authorised its first repair package on 3 October: primitive instance
promotion, compatibility checks for empty instance groups, replacement/removal
of unlabelled enum selections, selection-path allocation diagnostics, and current
documentation. The fixes preserve public APIs and existing input rules.

The user subsequently authorised consistent named singleton bulk records and
scalar shorthand wherever a compound with one scalar member is expected. This
bounded input extension is separate from the four-defect repair package below.

The user also authorised configuration preservation across differing defaults,
then general binary-authoritative reconciliation. Both are implemented; the
reconciliation rule supersedes the intermediate changed-default-only rule.
Promotion and output reconcile instance declarations, and both live data roles
provide explicit reconciliation after raw binary edits or remapping.
The user authorised committing the completed review repairs, singular-input
consistency, reconciliation, bulk replacement correction and documentation
updates together on 3 October, before continuing the remaining discussions.

The user subsequently authorised entry-lookup improvements, removal of quadratic
record/descendant bookkeeping and shared compatibility checking. Baked roles use
persistent sorted ordinal indexes; live roles now binary-search their ordered
record vectors directly, without auxiliary index storage. Live bulk records cache
resolved type indices, and remap setup uses temporary member-name indexing and
destination-member claim tracking. Entry access allocates nothing after loading.
The implementation and validation are recorded below.

The user then authorised renaming `demote()` to `bake()` and adding optional
stripped data output before the remaining consolidation. `EDataOutputForm::stripped`
now omits instance declarations and bulk arrays, retaining navigation and locator
metadata alongside independent binary payloads. A root Boolean marker prevents
document-only materialisation. Both baked roles expose `values_stripped()` and
retain their binary access interfaces. Promotion reconstructs document values;
schema definitions and defaults are preserved. Existing embedded/external forms
retain their behaviour. Terminology below uses baking for the former operation.

Regression coverage includes hierarchy and binary access after stripping, both
schema bindings, changed defaults, JSON/Morphic text reload, reconstruction after
promotion, preserved schema defaults, empty and zero-byte payloads, malformed
markers and contradictory values, unused-storage clearing, unrepresentable raw
values, and allocation-failure preservation for both data roles.
Full Debug x64, Release x64 and Debug Win32 builds and `-t1` runs passed,
each with 18,183 schema checks and no failures (`schema-baking-final-dbg64`,
`schema-baking-final-rel64`, `schema-baking-final-dbg32`). Policy validation
reported no errors or warnings; diff and line-ending checks passed.

The initial bounded consolidation shared scalar decoding, scalar bit access,
document subtree copying and name stabilisation within the schema implementation.
Instance creation and editing now both preserve copied string-formatting flags. Existing
role APIs, diagnostics, bulk completeness, instance inheritance and output
representability checks are retained. Baked access and stripped output acquire
no additional decoding or persistent storage.

Regression checks cover live/baked and same-document copies, integer metadata,
string flags, allocation-failure cleanup, scalar boundaries, enum aliases and
floating-point encodings. The existing fp16 infinity-to-finite encoding policy
is preserved; embedded output still rejects its non-faithful reconstruction.

Validation passed full Debug x64, Release x64 and Debug Win32 builds and `-t1`
runs, each with 18,457 schema checks and zero failures: `schema-copy-codec-final-dbg64`,
`schema-copy-codec-clean-rel64` and `schema-copy-codec-final-dbg32`. The incremental
Release binary initially reported three collection destructor-count failures;
a clean Release rebuild passed those checks without collection source changes.
Policy validation reported no errors or warnings, and diff/line-ending checks passed.

The completed follow-up moves subtree copying and name stabilisation
to `data_model/document_copy.hpp/.cpp`. Subtree copying and generic document
promotion share value construction, integer metadata and string formatting.
Compile-time source access keeps promotion's direct baked queries and avoids
runtime adapters or temporary storage. The recursive subtree walk retains its
256-level bound, incremental growth and partial-copy cleanup. Whole-document and
selected-member promotion retain their iterative walk, node reservation and
staged publication. Neither the baked writer nor schema scalar policies change.

A temporary Release x64 probe used a mixed document with 96 distinct string rows.
Whole promotion, selected-member promotion, live subtree copying and baked subtree
copying respectively made 26, 30, 37 and 37 allocation requests, requesting 37,824,
43,584, 51,200 and 51,200 bytes in total, identical before and after the refactor.
All temporary allocations were released. Nine batches of 1,024 operations per
route showed timing variation between runs, with no consistent slowdown observed;
this is a representative regression check, not a general performance guarantee.
The temporary probe is not part of the runtime or normal test suite.

Validation passed full Debug x64, Release x64 and Debug Win32 builds and `-t1`
runs (`generic-copy-final-dbg64`, `generic-copy-checked-rel64`,
`generic-copy-final-dbg32`), each with 3,340 baked-document checks and 18,457
schema checks, zero failures. New checks compare copied and promoted output,
preserve absent/empty names and live placeholders, exercise the subtree depth
boundary, and sweep allocation failures through both promotion routes. Existing
same-document and subtree-failure checks also pass. Policy validation reported
no errors or warnings; diff and line-ending checks passed.

The first organisation pass retains existing file boundaries, groups role
operations and places helpers before their callers. Compatibility hashing now
belongs to `type_compatibility.hpp`, separate from live/baked record lookup.
Instance selection decoding and reconciliation carry fixed inputs in stack-local
operation objects, reducing recursive parameter forwarding without adding heap
allocation or persistent state. Scoped headers identify their audience and major
sections; template declarations consistently use `template<...>`. The runtime
guide maps public entry points and implementation support.

Organisation validation passed full Debug x64, Release x64 and Debug Win32
builds and `-t1` runs (`schema-organisation-dbg64`, `schema-organisation-rel64`,
`schema-organisation-dbg32`), each with 18,457 schema checks and 3,340
baked-document checks, zero failures. Policy validation reported no errors or
warnings; diff and line-ending checks passed.

The subsequent instance split moves declaration reconstruction, promotion,
reconciliation and baking into `live_instances_translation.cpp`. Ownership,
queries and creation remain in `live_instances.cpp`; capture, selection editing
and propagation remain in `live_instances_edit.cpp`. Private static append and
locator helpers share document construction without an additional header.
Translation maps shared scalar-decoder failures directly to output diagnostics;
editing retains its declaration diagnostics. Public APIs, object layouts and
allocation behaviour are unchanged. The Visual Studio shared project includes
the new source, which also feeds the Linux CMake source manifest.

Split validation passed full Release x64, Debug x64 and Debug Win32 builds and
`-t1` runs (`instance-split-rel64`, `instance-split-dbg64`,
`instance-split-dbg32`), each with 18,457 schema checks and 3,340 baked-document
checks, zero failures. Policy validation reported no errors or warnings; diff
and line-ending checks passed. Visual Studio item/filter files retain their
original BOM state, CRLF endings and absence of a final newline.

The conditional and parameter review's first implementation package introduces
operation-local selection overlays and bulk reconstruction, retaining fixed
schema/document inputs without extra allocation or persistent state. Value
construction shares compound-input preparation and holds its mode in the writer.
Structure and bit-structure positional rules and diagnostic precedence are
preserved. Local guards now separate instance creation preconditions and baked
locator overlap/range checks without changing their order or diagnostics.

Validation passed full Release x64, Debug x64 and Debug Win32 builds and `-t1`
runs (`schema-traversal-rel64`, `schema-traversal-dbg64`,
`schema-traversal-dbg32`), each with 18,477 schema checks and zero failures.
Twenty added checks cover first-error reason and occurrence for conflicting
compound-input errors on live/baked documents, including instance, complete bulk
and alternative construction. Existing selection propagation, reconstruction and
allocation-failure checks also pass. Policy validation reported no errors or
warnings; diff and line-ending checks passed.

The alignment-constant follow-up passed the Release x64 build and full `-t1`
suite (`schema-alignment-rel64`, PID 72144), including all 18,477 schema checks.
The 128-byte rule is unchanged; the earlier platform matrix was not repeated.

The physical-member cursor cleanup is implemented in `unused_storage.cpp`.
Validation and clearing now keep their shared schema/type/count and previous
offset/ordinal in a local cursor, with no auxiliary allocation or public API
change. The counted traversals, offset/ordinal ordering and complete preflight
before writes are preserved; the repeated scan retains its quadratic cost.
The existing nested padding test also covers empty members before and after a
nonempty member at the same offset, inside another extent and at the type end.
The Release x64 solution build and full `-t1` suite passed
(`schema-cursor-rel64`, PID 10172), including all 18,477 schema checks. Policy
validation, diff and line-ending checks passed; the platform matrix was not
repeated for this local refactor.

The final style/comment audit remains subsequent work. Integer output notation
and further performance work remain discussion items. Combining the traversal
strategies is outside this refactor.
Fp16 workflow changes and source ingestion also remain deferred. A completed
review does not authorise those changes.

### Historical delivery record

The following entries retain the delivery sequence and validation evidence.
Statements about a package's next stage or then-active coordinator describe
that point in delivery; current scope and working arrangements take precedence.

The original schema resolver/generator baseline is commit `5313c85`, completed
27 September. It is distinct from integration stage 1 below. Private record-layout
decisions accepted on 27 September are reflected in the runtime records and limits
contract and measured in the API guide; no separate proposal remains active.

Integration stage 1 was dispatched on 29 September to implementation chat
`01a0ecd6-4b36-7c72-8258-f9570e84a89e` (Mutable baked integer-width updates).
Its implementation, tests and existing data-model API documentation are in scope;
schema changes are not. Coordinator review passed on 29 September with no
blocking findings. The user authorised its separate commit after making trivial
whitespace-only changes: `d92dddd` (Allow baked integer mutations to update canonical width).

Accepted changes are limited to `core/data_model/baked_document.cpp`,
`core/data_model/baked_document.hpp`, `tests/test_suites/BakedDocument_test_suite.cpp`
and `docs/data_model/baked_document_format.md`. Coordinator inspection confirmed
domain checks before mutation, value-derived width, preservation of other flags,
boundary/rejection coverage and the checked-view/promote/rebake regression.
The implementation chat's Debug x64 build and ordinary test run completed with
exit code zero: 2,793 BakedDocument assertions and all other ordinary suites passed.
The recorded command was
`.\tools\invoke_sandbox_build.ps1 -RunTests -TestMode 1 -LogTag=mutable-baked-width-stage1`;
actual runner output used `development/logical-roots/test-logs/*.sandbox.p20572.log`.
Line-ending/policy checks and `git diff --check` passed. The coordinator inspected
the actual build/test output and did not repeat the passing matrix. The user's
whitespace changes were preserved without retesting, as instructed. Only the four
stage 1 files were included in that commit. The consolidated schema documentation
forms a separate baseline for the subsequent schema integration work.

Recorded validation for the original schema baseline: Debug x64 build and ordinary `-t1`
passed, including 1262 schema checks. Actual private layouts were asserted on
x86/x64, and five generated fixture translation units compiled on both targets
under C++17 with warnings as errors. The final named-union pass left generated
headers unchanged. Ignored logs include `build/schema-scalar-union-build64.txt`,
`build/schema-scalar-union-tests64.txt`, `build/schema-scalar-union-layout.txt`,
and `build/schema-record-layout-generated.txt`. Logs may not exist in another
checkout; they are evidence pointers, not inputs required to resume.

Integration stage 2 began on 29 September with a read-only API proposal assigned
to chat `01a0ecea-0053-73a3-b945-cf04a6399533`
(Schema stage 2 — query and resolver refactor). After reviewing and refining the
proposal, the coordinator authorised the first bounded code package: the internal
live/baked read adapter, three public document-role handle types and parity tests.
Baked adapters retain a view by value; live adapters borrow their live document.
The full live key is preserved, and invalid handles are well-defined. Resolver
migration, wrappers and conversions are not part of this first package.
The adapter package is complete and passed coordinator and user review on
29 September. It adds `core/schema/document_query.hpp/.cpp`, parity/invalid-query
tests in the schema suite, and a greater-than-32-bit live-key test using the
existing live-document test access. The Debug x64 build and ordinary suites
passed: 1,342 schema checks and 4,614 live-document checks, with zero failures.
Final test invocation was `MorphicTests.exe -t1 --log-tag=stage2-query-boundary-final`;
runner logs use `development/logical-roots/test-logs/*.stage2-query-boundary-final.p22080.log`.
Coordinator review verified the implementation and actual test output. The user's
subsequent whitespace/line-break edits were preserved without repeating tests.
The package was committed separately as `0addf71` (Add shared schema document
queries and role handles). The user then authorised the next bounded package,
dispatched to the same implementation chat: adapt resolver traversal, source
occurrences, diagnostics, mappings and generator name access to the shared read
adapter, with live/baked resolution parity and unchanged generated output.
Wrappers, conversions, guarded edits and reference lifetimes remain outside this
package. Coordinator review is required before a separate user-authorised commit.

The resolver/generator package passed coordinator and user review on 29 September;
the user authorised its separate commit, `82d4974` (Resolve schemas through shared
live and baked queries). `CSchemaDocumentQuery` provides the public
borrowed schema-role read view, including diagnostic navigation after failure.
The resolver accepts live documents, baked documents or that view; source
observations and diagnostics use `CSchemaHandle`, while resolved slots retain
`CSchemaIndex`. Baked views are copied by value and live documents remain borrowed.
Unchanged re-resolution preserves slot meanings; failure clears tables and input.

Coordinator review caught the missing public diagnostic query path and a
quadratic live array-default walk. The final implementation supplies that query
path and walks defaults sequentially. Wider handles also exposed native Debug
stack overflows: iterative inline-array reference traversal and const-reference
internal handle parameters preserve the original depth limit, validation order
and inner-before-outer record allocation. No dependency prepass, stack-setting
change or C++ exception handling was introduced.

Debug x64 and x86 builds and ordinary tests passed with 2,478 schema checks and
4,626 live-document checks on each target, with zero failures. The exact accepted
and rejected nesting boundaries (128/129 named definitions and 254/255 inline
arrays) are covered in both representations, including accepted nested-default
leaf access. Final test tags were `stage2-depth-boundaries` (PID 80096, x64) and
`stage2-depth-boundaries-x86` (PID 23076); runner logs remain under
`development/logical-roots/test-logs/`. All five generated headers match the
`stage2-query-boundary-final.p22080` baseline byte for byte, and their C++17 layout
translation units compile on x86 and x64. Private record-size assertions pass on
both targets. Line-ending checks and `git diff --check` passed. The coordinator
inspected the actual validation output without repeating the passing matrix.

The user's subsequent review requested namespace grouping changes and included
minor whitespace edits. Those edits were preserved. The query header now has
one `detail` block, and the implementation explicitly nests `detail` inside a
single outer `schema` block. Coordinator review, a Debug x64 project compile and
text-policy checks passed; the runtime matrix was not repeated for this cleanup.
The subsequent header section banners were reviewed as comment-only changes.

After that commit the user authorised continuation. The same stage 2 chat refreshed
the schema-wrapper, guarded-edit and intrusive-binding API proposal against the
implemented query/resolver boundary. Coordinator review is complete, and the
bounded wrapper package is authorised for implementation: `CBakedSchema` and
`CLiveSchema`, document/resolution queries, guarded editing, fallible adoption and
transfer, and non-owning intrusive client bindings. The user confirmed that every
role wrapper may expose a combined input's original root while operating only on
its specialised content; schema queries also provide `types_root()`. Each live
wrapper owns its own document. The editor rechecks bindings on every mutation;
it remains a raw borrow rather than introducing editor-generation tracking.
Adoption/transfer require empty destinations and preserve both inputs on failure.
Promotion/baking form the following package. No stage 3 semantics or
instance/bulk wrappers are dispatched.

The wrapper package passed coordinator and user review on 29 September and was
committed as `0289c6b` (Add schema wrappers with guarded editing and client bindings).
The user authorised that separate commit after adding only leading blank lines
to the two new wrapper files. Those formatting edits were preserved without
repeating the passing validation. The new
`core/schema/schema_wrappers.hpp/.cpp` provides the two schema wrappers,
`CLiveSchema::CEditor` and `CSchemaBinding`. The resolver has only a private
live-document rebind hook for transfer. Schema edits stay within `types`, with
object-payload replacement of the section itself supported to repair malformed
input without changing the other sections. Document readiness and resolved
readiness are separate. Failed unchanged re-resolution leaves clients attached
but unusable until a successful retry. Destruction invalidates and detaches
clients before releasing schema state; no C++ exception handling or mandatory
panic path was added.

Debug x64 and Release x64 solution builds and ordinary `-t1` tests passed, each
with 2,657 schema checks and zero failures across the ordinary suites. Test tags
were `stage2-wrapper-growth` (PID 16132, Debug) and `stage2-wrapper-release`
(PID 84752, Release). Coverage includes retained-editor guards, referenced
clear/reset/move rejection, nonempty destination rejection, binding moves and
rebinding, destruction of live/baked schemas with surviving clients, unchanged
re-resolution failure/retry, live pointer repair after transfer, role boundaries,
and allocation failure during reset and payload replacement. A fixed-capacity
fixture fills the last node slot with the replacement candidate, then fails
allocation when detaching the old payload and verifies that the old value remains.
The coordinator inspected actual build/test output without repeating the passing
matrix. Release disables development assertions and exercises the explicit
failure returns. Final indentation/comment cleanup did not require a repeated
Debug run. Text-policy and diff checks passed; Visual Studio item/filter encoding,
CRLF and existing missing-final-newline form were preserved. No generated-layout
matrix was repeated because layout/generator semantics did not change.

The user then authorised continuation and confirmed that schema conversion copies
only `types` and always resolves the promoted live result before publication.
The same implementation chat completed the remaining stage 2 package: schema
promotion/baking and narrow root-member selection in the existing data-model
translators. Whole-document
translation behavior is preserved. Outputs are independent copies; source
bindings survive, occupied outputs are rejected unchanged, and baking produces
a separate owned block and unresolved baked wrapper. Stage 3 semantics and
instance/bulk conversions are not dispatched.

The conversion package passed coordinator and user review on 29 September, and
the user authorised its separate commit. The user's whitespace edits and rename from
`analyse_impl` to `analyse_common` were preserved; the coordinator checked the
declaration, definition and call sites without repeating the passing test matrix.
Review checked staged publication, independent
string ownership, safe failure diagnostics and preservation of source bindings.
The added combined-live baking test destroys the source before resolving and
reading the output's type name and default. Allocation sweeps cover translation
and resolution failure without leaks or partially published destinations.
Debug x64 and Release x64 solution builds and ordinary `-t1` suites passed:
`stage2-schema-conversion-debug-reviewed` (PID 80792) and
`stage2-schema-conversion-release-reviewed` (PID 85424), each with 2,827 schema
checks and zero failures throughout. Layout/generator semantics are unchanged,
so the generated-layout matrix was not repeated.

The 29 September user review rejects mandatory no-return panic as the standard
schema lifetime response. The agreed replacement is a schema reference count plus
an intrusive linked list with links stored in each client binding. Destruction
invalidates and detaches those bindings before releasing schema state; later
schema-dependent operations fail safely. Referenced edits/moves remain rejected
unchanged. Implementation follows in the wrapper/reference package, not the first
adapter package. Other agreed contracts remain in place. Source ingestion remains
a later design stage; [header-survey.md](header-survey.md) retains its research.

## Initial delivery

Completed in commit `5313c85` on 27 September 2026. This section describes that
implemented boundary. The 28 September data-model revision changes subsequent
deliveries, not the capabilities claimed for this baseline; see the
[staged implementation plan](#staged-implementation-plan).

Resolve and inspect every currently described type category from one baked
definitions document, validate defaults, and generate C++17 declarations for
compiler validation. Resolution may consume a combined document, but evaluates
only `types`. Recognise the `instances` and `data` section names without claiming
their values have been validated. Instance-only documents cannot resolve types.
Unknown top-level sections remain errors.

Implement natural layout, including calculated member offsets, array strides,
size, alignment, and recursive gap reporting. Accept optional natural-layout
`detail.alignment` and `detail.size` and validate supplied values against the
calculation. Retain the Boolean `detail.internal` export marker, defaulting to
false; it never relaxes validation. Support the reviewed recursive
`element`/`count` array grammar.

Explicit offsets and alignment increases are deferred under the user's
retroactive-change criterion: the runtime already stores offsets, size, alignment,
and gaps, and consumers use these resolved facts rather than recalculating natural
layout. The later extension changes layout validation/calculation and declaration
export, without changing type identity, access, ownership, or the stored facts.
Do not add a natural-layout assumption to an accessor or generated consumer.
Initially report an unsupported-feature error for explicit offsets or increased
alignment, rather than ignoring them or silently treating them as natural.
An explicit alignment equal to natural alignment is ordinary checked metadata.

The full sample intentionally includes later layout features. Initial positive
fixtures use its natural-layout definitions; the full sample is not an initial
success fixture. Its extended definitions supply unsupported-feature tests now
and positive layout tests when that extension is delivered.

Creating a new normalised document, constructing instances, serialisation,
remapping, editor panes, and source-code ingestion are outside this delivery.
Any operation that writes a schema document must nevertheless write normal form.

## Definition shape checks

The sample is illustrative; these allowed-property sets make unknown-property
rejection executable. Required properties are marked with `*`:

| Object | Properties |
| --- | --- |
| Schema package | `types`*, `instances`, `data` |
| `types` | `enumerations`, `structures`, `bit_structures` |
| Enum definition | `storage`*, `values`* |
| Structure definition | `members`*, `detail` |
| Structure member descriptor | `type`*, `default`, `offset` |
| Array descriptor | `element`*, `count`* |
| Structure detail | `alignment`, `size`, `internal` |
| Bit-structure definition | `storage`*, `members`* |
| Bit-field descriptor | `type`*, `mask`*, `interpretation`, `default` |

Package sections and category groups are objects; absent categories are empty.
Definitions and descriptors are objects. Enum `values` is a non-empty named
object. `members` is a non-empty ordered array of named descriptors, using the
existing singleton-wrapper normalisation at the text/baked boundary. A member
type is a built-in/named type string or a recursive array descriptor. Enum and
bit-structure storage names must be integer primitives. Bit-field logical types
are integer primitives, `b8`, or named enums; they are not arrays or structures.
`internal` is a Boolean property, not a numeric coercion site.

The design's literal/default rules determine whether `default` is legal for a
particular type. A recognised but deferred `offset` or increased `alignment`
receives an unsupported-feature error, not an unknown-property error. Enum labels
and member names belong to their declaration's namespace; type names share the
whole catalogue's namespace. Report surviving duplicate declarations before
collapsing anything into a lookup table. Text parsing must already have rejected
the document parser's collision-extension policy before baking.

## Runtime records and limits

Use a move-only resolved-schema owner with its baked view stored by value.
The lifecycle and invalidation rules in the design apply to all owned tables.
Use existing allocation/container infrastructure; do not add a new generic
container or allocator solely for schema work.

Use a distinct unsigned 32-bit schema index wrapper, with zero invalid and a
Boolean validity query, following the document APIs' wrapper conventions while
retaining the schema's chosen sentinel. Do not expose an implicit conversion
between document indices, name IDs, ordinals, and schema indices. Indices do not
carry generations and are meaningful only in their owning resolution.

Keep size, offset, and stride calculations in unsigned 64-bit arithmetic, with
checked addition, multiplication, and alignment rounding. Before narrowing,
enforce `memory::k_byte_size_ceiling` (0x80000000 bytes, inclusive) for each
described allocation, nested offset/member end, array product and padded extent.
Supplied size/alignment detail is also bounded. This replaces `SIZE_MAX`-only
acceptance. Preserve the exact 2 GiB extent with unsigned storage. Counts and
slot ranges use 32-bit fields, as do stored offsets, sizes and schema strides;
do not import the memory token/view's 16-bit stride limit. Allocation and address
calculations must still fit their target types. File positions and streamed totals
are separate future quantities. Capacity exhaustion and arithmetic overflow are
errors, never wrapped or truncated values.

The owner holds sequences of type, member, enum-label, and bit-field records.
Relationships are indices and counted ranges, never stored pointers or references
into another owned allocation. A type records its category, size, alignment,
gap flag, and category-specific facts:

| Category | Resolved facts |
| --- | --- |
| Primitive | Exact primitive tag, including width and signedness. |
| Enum | Cached physical primitive, integer storage type and ordered label/value records. |
| Structure | Ordered member range; each member has its name, type, offset, cached full byte size, and default description. |
| Array | Element type, positive count, and element stride. No record per ordinary array element. |
| Bit structure | Cached physical primitive, integer storage type and ordered fields with masks, logical types/primitives, signedness, and interpretation tags. |

Defaults need validated typed scalar values and index-based aggregate descriptions,
not an eagerly expanded instance-sized byte buffer. Retain source document indices
where useful for diagnostics and later normalisation. A large defaulted array
must not force one resolved slot per element. Gap bytes are not default values.

The accepted private type layout is 32 bytes: size, stride, related type, first
child, count, name ID and source occurrence occupy seven 32-bit words, followed
by category, physical primitive, log2 alignment and control bytes. The three
flags occupy control bits 0-2 and resolution state bits 3-4. Decode alignment
with an unsigned shift over exponents 0-31. Structures and arrays retain primitive
`none`; enum/bit categories and storage relationships remain distinct.

Members occupy 24 bytes: six 32-bit words for offset, full byte size, type,
default index, name ID and source occurrence. Size includes owned padding.
Labels/fields/defaults/mapping pairs remain 24/32/32/8 bytes. The field's logical
primitive occupies existing padding, independently of the containing storage
word. Assert the actual layouts on both ABIs. Public observations remain separate
and wide where useful; no private layout is promised as an ABI.

Internal typed ranges borrow existing first/count child spans, with no side
table, duplicate schema or persistent pointers. Validate the schema/type at
operation entry, then access sequential records directly without repeated parent
decoding or observation copies. The named member/label/field lookups exercise
this path. Preserve checked public access and all lifetime/invalidation rules.

## Read-only access

Provide these operations without requiring callers to know physical record layout:

- Resolve, clear, and query readiness; borrow the associated baked view while ready.
- Look up a built-in or named type by name; enumerate named definitions in document order.
- Query a type's category, size, alignment, and whether it contains gaps.
- Inspect structure members by zero-based ordinal or name, including their type,
  offset, name ID, and default description.
- Inspect an array's element type, count, and stride.
- Enumerate enum labels in declaration order, look up a label, and obtain the
  first label for a value. Preserve signed/unsigned 64-bit domains exactly.
- Enumerate bit fields and query their masks, logical types, and interpretations.
- Map a baked-document occurrence to its resolved index, returning zero if unmapped.

Lookups return zero on absence. Queries with an invalid index or wrong category
fail explicitly; an empty result is not a successful zero-sized type. Returned
observations are values or transient const views, never permission to mutate the
schema. Document-backed names require the backing bytes to remain alive.

Create the occurrence mapping for records that actually exist: named definitions,
members, enum labels, and bit fields are the baseline. Map an inline array
description to its array type where represented. Do not create slots for numeric
properties, default scalar occurrences, or ordinary array elements merely to make
them mappable. References to a named type can reuse its type index; occurrence
mapping is not a substitute for reference resolution. The implementation review
must publish the exact coverage table after choosing the efficient representation,
including legitimate zero results. This is an implementation deliverable, not an
unanswered schema-author preference.

## Failure reporting

Follow the existing [document failure contract](../data_model/document_parsing.md#failure-diagnosis-and-logging):
return status and one fixed-size, allocation-free diagnostic, leaving formatting
and logging to the caller. Clear the previous resolution at the start of every
attempt; publish readiness only after all validation succeeds.

The diagnostic contains a reason, processing stage, offending document index,
enclosing type/member indices when available, and an optional related occurrence.
Overlap errors include both byte or bit ranges; duplicate-name errors identify
both declarations. Availability must be explicit: a missing document location
is not the schema index-zero convention imposed on the document's index type.
Reason categories cover invalid input, unknown/missing properties or types,
duplicate/invalid declarations, cycles, invalid defaults or ranges, invalid layout,
unsupported features, arithmetic/storage limits, and allocation failure.

Detection of the first terminal error stops resolution; there is no requirement
to choose a globally earliest source error across dependency traversal. No usable
partial schema or document binding survives. Diagnostic indices refer to the
caller's input document, which must remain alive for later name/path formatting;
the diagnostic does not retain ownership or a hidden view of it. Baked input has
no guaranteed source line/column provenance. Out-of-memory reporting must itself
require no allocation.

## Declaration generation

Generate a complete C++17 declaration text buffer from a successful resolved
schema; file selection and writing belong to the host/tool. Failure returns no
usable partial output and reports a diagnostic. The generator does not emit
operational functions, constructors, or default member initialisers. Defaults
remain schema metadata.

Emit types in dependency order while preserving each structure's member order
and each enum's label order. Fixed arrays use nested C array extents. Enum classes
use their explicit integer storage, booleans use an alias of `std::int8_t`, and
`f16` uses the existing `fp16data_t`. Bit structures use their integer storage
in data members and expose masks as typed `inline constexpr` constants in a
namespace named after the bit structure. Thus fields in different bit structures
do not collide and a mask never requires conversion to its logical enum type.

Keep generated declarations in a host-selected namespace. Validate identifier
and keyword rules for their emitted C++17 scopes. The initial identifier syntax
is the ASCII subset, including applicable implementation-reserved-name checks;
reject non-ASCII declaration and namespace identifiers with `invalid_identifier`.
This does not restrict the general document model's Unicode text and does not
introduce a permitted-name allow-list. Do not transliterate or silently rename schema
symbols. A type cannot redeclare a built-in schema spelling. The generator must
avoid helper-name collisions rather than imposing a new user-name allow-list.
Use integer literals that preserve signed minima and unsigned 64-bit maxima.

### Numeric and type spelling

Generated C++ uses the known integer value to choose notation: ordinary values
in the inclusive range -65535 through +65535 use decimal, and values outside
that range use hexadecimal with a `0x` prefix. Ordinary negative values retain
their minus sign. This differs from schema-document output, whose ordinary
32-bit and 64-bit values default to hexadecimal according to their declared
schema type; see the design's numeric-metadata rules.

Unsigned C++ initialisers retain an unsigned suffix, using the simplest suitable
literal spelling rather than unconditional `ULL` literals and casts. Signed
initialisers omit the unsigned suffix when directly representable. Add suffixes
or expressions only where required for correct literal typing, particularly
negative hexadecimal values and signed minima. Array extents are structural
quantities rather than typed initialisers and do not need decorative suffixes.

Flags and masks always use hexadecimal, padded to the full storage width (2, 4,
8, or 16 hexadecimal digits for 8, 16, 32, or 64 bits). An unsigned 32-bit mask
with value one is `0x00000001u`. A signed mask whose high storage bit is clear
uses a direct signed-representable literal without `u`. If that bit is set, keep
the visible full-width bit pattern and explicitly cast it to the signed storage
type, for example `static_cast<std::int32_t>(0x80000000u)`.

Use `std::uint8_t` and the other `std::` type names without routine leading
global `::` qualification. Preserve qualification where an actual name collision
requires it; do not reject otherwise valid schema names to simplify generation.
Types declared in the generated namespace, including `b8` and array element
types, use unqualified local names. If any member in a structure shares the
required type or alias name, qualify references to that name within the structure
with the generated namespace. Consider the complete member list, including
members declared later, to preserve portable C++17 name lookup. Keep
`::fp16data_t` for the global half type.

### Declaration spacing

Consecutive related single-line declarations form one group. Each structure,
enum, or namespace forms its own group. Separate groups with exactly one blank
line; adjacent groups share that separator rather than each adding their own.

A namespace has one blank line immediately inside each brace unless its contents
are just one group of related single-line declarations. A namespace containing
structures, enums, or mixed declaration kinds therefore has inner padding,
including before an initial alias group. Determine this from its complete
contents before emitting the first declaration. A namespace containing only a
group of mask constants or only a group of aliases remains compact inside.
Namespace outer spacing follows the same group rule as structures and enums.

### Layout fidelity

For the initial natural-layout delivery, use implicit native C++ alignment for
structures and members without unconditional `alignas` annotations. Compiler
validation must establish fidelity to the schema's resolved layout for each
target ABI; no packing pragma or changed schema layout may hide a mismatch.
Targets whose native layout differs remain unsupported pending separate handling.
Explicit layout was deferred in this initial delivery. It was subsequently
implemented with padding under selected compiler settings, alignment at most 128,
and rejection of layouts that cannot meet fidelity; see the current runtime guide.
The review-only fallback is removed.

## Baseline acceptance checks

Use the repository's test/build conventions. Tests should exercise outcomes and
failure boundaries rather than mirror private record organisation:

- Ordered and named access agree for a four-component vector, a nested record,
  a fixed array, and a record with padding. Arrays of arrays and count 1 work.
- Forward references resolve, cycles fail, aliases preserve declaration order,
  and bit masks pass contiguity, range, disjointness, and signed-enum checks.
- Duplicates, unknown properties/types, invalid identifiers, empty structures,
  zero counts, invalid defaults, nulls, excess values, and overflow fail.
- Metadata can be omitted or match calculated layout; mismatches and unsupported
  layout modes fail. Recursive gap flags include nested bit gaps and padding.
- Default checks cover boundary integers, floating-point rounding/range policy,
  signed zero, non-finite spellings, existing `f16` conversion, enum labels, and
  short positional input under the design's default rules.
- A failed attempt after success leaves an empty schema; moves transfer index
  relationships and leave their source empty; mapping misses return zero.
- Compile generated declarations in separate validation translation units and
  compare `sizeof`, `alignof`, `offsetof`, enum storage/values, masks, and array
  extents. Check standard layout and trivial copyability, including `fp16data_t`.
  Assertions belong to validation tooling, not generated data declarations.
- Exercise supported repository target configurations in the implementation
  validation plan; verify both 32-bit and 64-bit layout where supported.

## Working arrangement

- Repository: `D:\TheMorphicEngine`, shared `main` checkout. Work serially;
  do not automatically create a branch or worktree.
- Stage 9 work is performed directly in the current review/implementation chat.
  The previous coordinator no longer supervises it. Create a separate
  implementing chat only when the user requests one; historical task references
  are not instructions to start or message other chats.
- Follow `AGENTS.md`. Commits require explicit user instruction and, when an
  active coordinator exists, its review. The user performs pushes. Preserve
  unrelated and manual changes.
- Discuss substantive design choices. A consolidation plan does not authorise
  future implementation. Avoid repeating passing test matrices without a new
  change or unresolved concern.

Code review must preserve the established style: named namespaces rather than
anonymous namespaces; static linkage justified separately; const on unmodified
parameters in declarations and definitions; multiline method bodies outside the
class; readable multiline control flow; restrained `auto`; normal file headers.
Keep related declarations together in a single namespace block where practical.
Nest `detail` explicitly inside the enclosing namespace rather than opening
separate `schema::detail` and `schema` blocks in the same implementation file.
C++ remains exception-free; failures use return values and diagnostics.
Visual Studio `*.vcxitems` and `*.filters` retain their original encoding/BOM,
CRLF and optional missing final newline. Other text uses LF and a final newline.

## Existing implementation to reuse or revise

| Evidence | Implication |
| --- | --- |
| [live_document.hpp](../../core/data_model/live_document.hpp), [baked_document.hpp](../../core/data_model/baked_document.hpp) | Already expose similar value/tree queries. Adapt those APIs without creating a second data model. |
| [resolved_schema.cpp](../../core/schema/resolved_schema.cpp), [resolved_schema.hpp](../../core/schema/resolved_schema.hpp) | The reviewed stage 2 package routes input binding, observations, diagnostics and occurrence mapping through the common query boundary. Reuse that resolver in the schema wrappers while retaining validation and layout behavior. |
| [data_model_types.hpp](../../core/data_model/data_model_types.hpp) | Live keys are 64-bit, baked indices 32-bit. Role handles must represent both without truncation; equivalent method names do not make raw identities interchangeable. |
| [document_translation.hpp](../../core/data_model/document_translation.hpp), [document_promotion.cpp](../../core/data_model/document_promotion.cpp) | Existing non-consuming promote/bake fit the copy lifecycle. New schema resolution can bind to the promoted document directly. |
| [ByteBuffers.hpp](../../core/containers/ByteBuffers.hpp) | Reuse framework copying, allocation, alignment and ownership support. Baking's document-order packing must rewrite locators in its new output. |
| [schema_declarations.cpp](../../core/schema/schema_declarations.cpp) | The reviewed stage 2 package uses representation-neutral names and diagnostic locations. Generated output and established spelling/layout checks remain unchanged. |
| [baked_document.cpp](../../core/data_model/baked_document.cpp), [BakedDocument_test_suite.cpp](../../tests/test_suites/BakedDocument_test_suite.cpp) | Stage 1 updates canonical integer width in both directions; reviewed, validated and committed separately as `d92dddd`. Reserved locator nodes can use this prerequisite once schema loaders are implemented. |
| [Schema_test_suite.cpp](../../tests/test_suites/Schema_test_suite.cpp) | Reuse fixtures and compiler checks, revising expectations only where later decisions change semantics. Stages 4b and 4c integrate the sample's bulk and instance locators and exercise their materialisation. |

## Staged implementation plan

Stage 2's read-adapter and resolver/generator packages are committed as `0addf71`
and `82d4974`. The schema-wrapper package, including guarded editing and intrusive
client bindings, is committed as `0289c6b`. Schema promotion/baking completed
stage 2 in `dd553fa`, after coordinator review, validation and user review. The
user reported pushing all pending commits on 29 September.
On 30 September the user authorised continuation. Stage 3 design assessment with
the existing implementing chat is complete, and its first package has passed
coordinator review and validation: empty type identities and zero-byte members,
array propagation, generator omission and compiler-checked tests. The user
completed manual review on 30 September and authorised the separate commit.
Their whitespace and line-break edits were preserved without repeating the
passing validation. This package is committed as `9e0d596` (Support queryable
empty schema types without physical storage). The user then authorised
continuation, and the same implementing chat completed the explicit-layout
package: authored offsets and extent validation, effective alignment up to 128,
semantic member-order preservation, physical-order C++ declarations, and
compiler-checked padding/alignment. Coordinator review and validation passed.
The user completed manual review on 30 September and authorised the separate
commit after making only whitespace changes. Those edits were preserved without
repeating the passing validation. The package is committed as `271d519`
(Support explicit schema layouts and faithful C++ declarations).
The user then authorised continuation, and the same implementing chat completed
revised unorm schema defaults as the final bounded stage 3 package. Coordinator
review and validation passed. The user reviewed the code on 30 September and
requested only a blank line before the conversion comment, which was added.
Before commit, the user reopened the quantisation decision to discuss GPU
normalised-format conventions and snorm support. The user then approved nearest
rounding with ties away from zero, scaling by the normalised maximum, and signed
normalisation with preserved raw signed minima. The same implementing chat
completed this revision within the same package. Coordinator review and
validation passed. The user completed manual review on 30 September with no
changes and authorised the separate commit. Passing validation was not repeated.
The package is committed as `a53c762` (Support GPU-style UNORM and SNORM schema
defaults), completing stage 3.
Integer defaults are raw codes; floating defaults use the agreed quantisation
and clamping rules, with resolved defaults stored as unsigned UNORM or signed
SNORM integer codes. SNORM requires a signed primitive and at least two mask bits.
Resolver changes, tests and current API documentation are in scope. Instance/bulk
codecs, wrappers, setters and output remain later work.
The user confirmed that empty types retain queryable identities and zero-byte
member records, preserving names, semantic order and document-to-schema mappings
while contributing no physical storage or generated C++ declarations.
Empty types have size and stride zero and alignment one; metadata requesting
positive size or stronger alignment is rejected. For explicit layouts,
the user permits C++ field declarations in physical offset order while
schema queries and document positional values retain schema declaration order.
The first package also aligns generated-C++ validation with the project's
exception-disabled compiler settings. No C++ language exceptions are permitted.
Debug x64 and Release x64 solution builds and ordinary `-t1` suites passed:
`stage3-empty-debug-reviewed` (PID 38536) and `stage3-empty-release-reviewed`
(PID 69168), each with 2,930 schema checks and zero failures throughout.
All six generated assertion fixtures from `stage3-empty-debug` (PID 59452),
including `schema_empty_layout`, compiled for x86 and x64 with the updated
validation script. Subsequent additions tested live/baked parity and invalid
metadata on all-empty containing types without changing resolver, generator or
assertion emission, so the passing compiler matrix was not repeated.
Coordinator review confirmed lookup/mapping and semantic member preservation,
zero-size array arithmetic, omission of empty declarations and correct handling
of an omitted type named `std`. Diff and text-policy checks passed.

The explicit-layout package preserves declaration order for member indices,
defaults, lookup and source mappings. It validates all-or-none offsets, supplied
extents, alignment, nested padding ownership and overlapping positive ranges;
overlap diagnostics identify both members and byte ranges. Zero-byte members
retain their authored offsets without occupying storage. The full sample's
`types` section now resolves and generates faithful C++; its instance/bulk
grammar and payload validation remain later work.

Review and validation addressed two regressions. The nonrecursive layout pass
is separate from recursive member resolution so the existing Debug nesting
boundaries remain usable. Generated declarations explicitly fill gaps and tail
space caused by inherited alignment, including increases below eight bytes,
without suppressing compiler warnings. The generator records inherited
alignment in its existing per-run emission state instead of repeatedly walking
shared dependency subtrees. Public APIs and resolved record layouts are unchanged.

Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed, each with 3,195 schema checks and zero failures throughout. Final tags
were `stage3-layout-cached-dbg64` (PID 57464), `stage3-layout-final-rel64`
(PID 70292) and `stage3-layout-final-dbg32` (PID 75352). All eight generated
assertion fixtures from the final Debug x64 run compiled on x86 and x64 under
C++17 with warnings as errors and C++ exceptions disabled. These include
leading/tail gaps, nonmonotonic offsets, nested array stride, padding-name
collisions, alignment 128, and low-alignment inheritance. The coordinator
inspected actual validation output; all six existing generated headers remain
byte-for-byte identical to the committed empty-type baseline. Diff and
text-policy checks passed. No passing validation matrix was repeated after
the final review.

The initially reviewed unorm package distinguished integer raw codes from normalised floating
defaults and stores canonical unshifted unsigned codes. Quantisation uses the
mask width; the result must fit both that width and the logical unsigned
primitive. Endpoint checks precede integer conversion, including at width 64.
Floating special strings reuse the existing parser: NaN and negative infinity
produce zero, while positive infinity produces the mask-width maximum subject
to the logical-type range check. Source occurrences, field interpretation and
implicit zero remain available without changing public APIs or record layouts.

Coordinator review confirmed live/baked acceptance and rejection parity,
quantisation thresholds, shifted masks, 1-bit and 64-bit boundaries, logical-type
range checks and preservation of non-unorm field behaviour. The existing
allocation-failure fixture includes a unorm default and exercises the unchanged
publication path. Debug x64 and Release x64 solution builds and ordinary `-t1`
suites passed, each with 3,432 schema checks and zero failures throughout:
`stage3-unorm-dbg64` (PID 30644) and `stage3-unorm-rel64` (PID 77080).
The coordinator inspected actual build/test output and header comparisons.
All eight generated headers are byte-for-byte identical to
`stage3-layout-cached-dbg64.p57464`, so the passing compiler layout matrix was
not repeated. Diff and text-policy checks passed. Current API and sample notes
distinguish schema-default quantisation from later payload codecs. This evidence
predates the rounding revision and SNORM extension described below.

The revised package scales floating defaults by the normalised maximum and
rounds to nearest with ties away from zero. It adds SNORM for signed integer
primitives with at least two mask bits, retaining raw signed minima while
encoding floating -1 as the negative normalised maximum. Canonical defaults
remain unshifted integer codes, with signedness matching the interpretation.
Code comments explain GPU reconstruction, the duplicate SNORM negative endpoint,
the chosen rounding policy and the exact 64-bit arithmetic, with links to the
Direct3D and Vulkan specifications. Existing resolved record sizes and the
allocation/publication path are unchanged.

Coordinator review covered live/baked parity, raw and floating defaults, shifted
masks, signed fields within unsigned storage, logical-type limits, special
values, invalid interpretations, and 64-bit rounding boundaries. An independent
exact-rational diagnostic checked the rounding algorithm against 65,341 directed
and random binary64 cases across widths 1-64. Debug x64, Release x64 and Debug
x86 solution builds and ordinary `-t1` suites passed, each with 3,794 schema
checks and zero failures throughout: `stage3-normalized-final-dbg64` (PID 55308),
`stage3-normalized-final-rel64` (PID 15548) and
`stage3-normalized-final-dbg32` (PID 41200). The coordinator inspected actual
build/test output. All eight generated headers remain byte-for-byte identical
to `stage3-layout-cached-dbg64.p57464`, so the passing compiler layout matrix
was not repeated. Diff and text-policy checks passed.

The user confirmed the scope of stage 4 on 30 September and authorised
continuation. The coordinator and existing implementing chat divided the work
into bounded packages for shared value construction, baked bulk loading, and
baked instance loading. The first package is implemented and coordinator-reviewed:
internal value construction and independent-alternative codecs, sharing scalar
conversion with schema resolution. User manual review is complete and the package
was committed on 30 September as `d2d1a96` (Add shared schema value construction
and alternative codecs), including the user's whitespace/line-break edits.
On 1 October the user authorised continuation. Coordinator review approved the
baked bulk loading proposal and dispatched it to the existing implementing chat:
`CBakedBulkData`, shared role queries, explicit schema binding, supplied payload
views, separately returned materialised owners, reserved locators and optional
encoded-field comparison. The user confirmed positive explicit counts for
zero-byte types without embedded records and encoded comparison excluding unused
storage, with all NaNs equal. The baked bulk package passed coordinator and user
review and was committed on 1 October as `46a37b7` (Add baked bulk loading with
external payload ownership). The user's line-break and explicit expression-grouping
changes were preserved without repeating the
passing validation matrix. The user then authorised continuation into baked
instance loading. Coordinator review approved the hierarchy/API proposal and
dispatched the instance package after the user confirmed that omitted declarations
mean defaults for bases and unchanged inheritance for specialisations. One wrapper
covers the complete `instances` section. Stage 4c passed coordinator review,
validation and user manual review and was committed on 1 October as `3cccf6d`
(Add baked instance loading and specialisation snapshots), including the user's
final formatting changes. The user then authorised continuation and explicitly
requested a fresh implementing chat for stage 5. Coordinator review approved its
bounded proposal for live bulk ownership, construction and capture (stage 5a).
The user confirmed that bulk promotion against a different schema requires
matching structure and value interpretation, while allowing different defaults.
With that decision recorded, the coordinator dispatched the implementation.
The coordinator and user completed the requested promotion/default debrief;
the user confirmed that the implementation matches the intended behaviour.
Stage 5a was committed as `f3bc3ac` (Add live bulk construction and non-consuming
promotion). The user authorised continuation into stage 5b with the existing
implementing chat. Its API proposal passed coordinator review. The user confirmed that
instance promotion requires matching defaults as well as structure and value
interpretation for referenced types; copied snapshots are not rebuilt. The
3 October default-preservation decision below supersedes the matching-default
requirement while retaining structural compatibility and copied snapshots.
The user also confirmed that descendant updates preserve selected binary values
and synchronise declarations, while unselected values inherit the updated parent.
The coordinator dispatched the bounded live-instance implementation, including
construction, promotion, capture and coordinated selection edits. Base capture
records a complete declaration; specialisation capture retains existing selection
shape. Positional structures may become named declarations for interior edits;
fixed arrays retain prefix-only selections. Stage 5b has passed coordinator review,
validation and user manual review; the user authorised its commit on 1 October.
Stage 5b was committed as `4386e06`. The user authorised continuation into stage 6;
the existing implementer's bounded output/baking proposal passed coordinator
review. Stage 6a bulk output and baking has passed coordinator review,
validation and user manual review and was committed as `80c5681` on 1 October.
Stage 6b instances was authorised to continue on 2 October and has passed
coordinator review, validation and user manual review. The user made only
line-break changes and authorised the commit, including the later review notes,
on 2 October. Stage 6b was committed as `1fdd9e8`.
The user
confirmed that embedded instance output must reject inconsistent unselected
inherited values rather than change selection intent, and embedded output uses
the existing encoded-field comparison rule (NaNs equal, padding/unused bits ignored).
Values without a faithful embedded spelling are rejected; external output remains
available. The user authorised continuation into stage 7 on 2 October. The
coordinator approved a first bounded slice covering direct-member plan setup and
bounded-view execution, followed by review before adding role-handle adapters.
The user clarified that all missing or differently typed members remain unmapped,
and zero matches form a valid plan. The authorised stage 7a correction is complete
and has passed coordinator review and validation. During usage review the user
authorised simplifying execution to bounded views and a Boolean result, deriving
the minimum complete record capacity instead of accepting counts. Construction
diagnostics remain; execution diagnostics, copied-count output and `execute_min`
are removed. This simplification has passed coordinator review and validation.
Stage 7a was committed after user review as `3425e71`. The user authorised
continuing through stage 7b and stage 8 on 2 October, retaining the staged
coordinator review, user manual review and explicit commit checkpoints. Stage 7b
has completed the coordinator-reviewed minimal adapter package: reuse existing
entry views and add mutable live-instance access. It was validated and committed
after coordinator and user review as `bbaa4bd`. Stage 8 has implemented the
explicitly user-invoked unused-storage operation, with no automatic clearing.
Coordinator review is complete; Debug x64, Release x64 and Debug x86 builds
and ordinary test runs passed, each with 6,014 schema checks and no failures.
The user completed manual review and authorised the commit on 2 October.
Their final line-break and bracing edits passed an incremental Debug x64 build
and full test run, again with 6,014 schema checks and no failures.
Stage 8 was committed as `05b428d`. The separately authorised remapping stride
correction is implemented and coordinator-reviewed: reject any supplied
nonzero-type view with a partial stride before writes, retaining existing
zero-byte-type behavior. Debug x64 build and full tests passed with 6,019 schema
checks and no failures. The user completed review and authorised its commit
on 2 October.
The correction was committed as `0f4612b`, followed by handover and the stage 9
review on 2 October. Current follow-up scope is recorded under Delivery status.
On 2 October the user confirmed that embedded output must also reject omitted
base fields whose saved values disagree with schema defaults, preserving omission
rather than adding selections. That decision was superseded by the 3 October
binary-authoritative reconciliation rule recorded below.
The user also approved recognising parser-normalised singleton compound
selections in array/positional contexts within schema handling. Preserve the
selected member or bitfield through reload and subsequent edits without changing
the parser, widening selections or restricting output to baked documents.
The stage 5 handoff used a fresh implementing chat after completion of the baked
loaders, with the then-active coordinator retaining requirements and review.
During that review, the user clarified that specialisations are independent
alternatives and short positional declarations inherit omitted values from
their base. Base construction still completes omitted values from defaults;
bulk records remain complete. The design and sample notes now reflect this
distinction, including positional selection intent for later capture and edits.

Stage 4a validation covers live/baked declaration input, nested default completion,
complete bulk records, multi-level alternatives, explicitly selected values equal
to their original parent, source preservation, bounds/alignment/overlap rejection,
and named positional-entry rejection. Physical checks compare a complete
little-endian byte image for a nonmonotonic explicit layout, including preserved
padding, signed integers, half and double values. Bit-field checks include signed
64-bit storage, shifted UNORM/SNORM codes, raw signed minima and preservation of
unselected bits. Empty-type tests retain declaration validation and avoid iterating
over a large omitted zero-byte array. Positional traversal uses sibling cursors
instead of repeated indexed lookup on live documents. No codec allocation,
ownership wrapper, live editing, output or remapping API is introduced.

Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed with 3,873 schema checks and zero failures throughout:
`stage4a-final-dbg64` (PID 70968), `stage4a-final-rel64` (PID 72948) and
`stage4a-final-dbg32` (PID 84104). The coordinator inspected actual build/test
output. The x86 build initially exposed test-helper size-conversion warnings;
checked fixture-size narrowing removed them, and the final x86 build has no new
warnings. The passing x64 runs were not repeated for that test-only correction.
Final review also made the exercised fixture-size guard an explicit test assertion;
the recorded runtime counts precede that assertion, without a production-code change.
Generated declarations and resolved record layouts are unchanged, so the compiler
layout matrix was not repeated. The existing GPU explanation accompanies the
shared conversion helper. Visual Studio item/filter files retain their original
CRLF, BOM and final-newline state.
Diff and tracked text-policy checks passed; the three new source/header files
were separately checked for LF, final newlines and trailing whitespace.

The user's review also aligned declaration/definition const decoration and removed
the codec's anonymous namespace. The existing meaningful `schema::detail` grouping
remains, with translation-unit helpers declared `static`. Coordinator review,
a targeted Debug x64 build and text-policy checks passed for these corrections;
the runtime matrix was not repeated. Subsequent user edits were whitespace only.

Stage 4b adds `CBakedBulkData` and `CBulkDocumentQuery`. It keeps original-root
document navigation separate from validated bulk entry access, binds either
baked or live schema wrappers, and invalidates schema-dependent access safely
when that schema is destroyed. Supplied payloads remain borrowed; materialisation
returns an independent `CByteBuffer` owner and borrows its stable address. The
loader checks grammar, counts, sizes, alignment, ordered nonoverlapping extents
and reserved locators before publishing usable data. Materialisation performs
allocation and complete-record conversion before updating unset locators.

Coordinator review added rejection of named outer record-array positions,
including empty names, and corrected stale fixed-array and zero-byte-count prose.
Tests cover supplied and materialised backing, immutable valid locators, mixed
valid/unset locators, widening past 255 and narrowing reserved offsets, zero-byte
count inference, malformed inputs, alignment/range/count rejection, ownership
transfer and schema invalidation. Allocation failures at record planning and
payload allocation leave no usable partial result or outstanding attribution.
Encoded comparison covers f16/f32/f64 NaN variants, signed zero, distinct b8 and
SNORM codes, structure padding and unused bit positions. The updated sample's
`Vertex.triangle` bulk entry is materialised by the schema suite.

Final Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1`
suites passed with 3,993 schema checks and zero failures throughout:
`bulk-stage4b-final-dbg64` (PID 79432), `bulk-stage4b-final-rel64` (PID 49956),
and `bulk-stage4b-final-dbg32` (PID 75768). Coordinator review inspected actual
build/test output; no new warnings appeared. Generated layouts and resolved
record sizes are unchanged, so the compiler layout matrix was not repeated.
Diff and text-policy checks passed, including separate checks of the new files
and preserved Visual Studio CRLF, BOM and final-newline state.

A later review may reconsider inlining small document-query forwarding functions
and splitting the query files once the instance interface is present. These are
deferred observations, not changes required for the baked bulk package.

Stage 4c adds `CBakedInstances` and `CInstanceDocumentQuery`. One wrapper covers
the complete `instances` section and borrows one payload buffer. Its original-root
queries coexist with validated base, child, sibling and parent navigation. Each
entry exposes its retained declaration and independent completed snapshot.
An omitted declaration selects no values: bases use defaults and children inherit
their complete immediate parent. Both baked and live schema bindings are supported.

Planning traverses the hierarchy with an explicit frame stack, producing parents
before children independently of the placement of declaration and locator
properties. Materialisation stages allocation and all conversions before publishing
reserved locators. Optional supplied-payload comparison constructs a separate
expected hierarchy; it never derives expected children from supplied parent bytes.
The shared codec retains its encoded-field comparison and unused-storage rules.

The sample now reserves instance and specialisation locators and is materialised
by the suite. Focused checks cover inherited positional tails, independent siblings,
repeated names in distinct branches, omitted declarations, empty snapshots, deep
hierarchies, ownership transfer, schema invalidation, invalid input, locator-width
changes and staged failure. Coordinator review added frame-stack and comparison
allocation failures and a bad descendant after a valid base, checking that failure
publishes neither locator. These checks extend the existing shared-codec coverage.

Final Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1`
suites passed with 4,190 schema checks and zero failures throughout:
`instance-stage4c-targeted6` (PID 80636, retained as the final Debug x64 run),
`instance-stage4c-final-rel64` (PID 73280), and
`instance-stage4c-final-dbg32` (PID 78508). Coordinator review inspected actual
build/test output; no new warnings appeared. Combined-document failure isolation
and failed/successful unchanged schema re-resolution are covered. Generated layouts
and resolved record sizes are unchanged, so the compiler layout matrix was not
repeated. New sources retain LF and final newlines; Visual Studio item/filter
files retain CRLF, their original BOM state and no final newline.
Diff and tracked text-policy checks passed, with separate checks of the new files.
The user's final line-break and expression-grouping changes were reviewed and
preserved. They do not change behaviour; the passing validation matrix was not
repeated. The final diff check passed.

Stage 5a introduces `CLiveBulkData` with an owned live document, an owned aligned
working payload and the existing client-held schema binding. The reviewed API
supports complete-record construction, raw binary capture/replacement, unpopulated
arrays, bounded mutable entry views, rename and erase. Creation rejects duplicate
names within a type group; capture replaces a matching entry while preserving its
handle. Larger replacements append storage; smaller replacements can reuse it.
Zero-count live entries and positive counts of zero-byte records retain explicit
metadata. No operation implicitly compacts or clears unused storage.

Stage 5a promotion originally built only live metadata and independently copied
the authoritative binary payload, omitting embedded records and making counts
explicit. The 3 October reconciliation follow-up regenerates existing embedded
records from binary while retaining reference-only entries. The supplied schema
is bound independently and referenced types
are checked for the user-approved structure/interpretation compatibility, ignoring
defaults and export-only internal flags. Promotion and capture apply no defaults;
complete-record construction rejects omitted fields, and unpopulated creation
leaves logical fields for the caller to populate. These facts form the requested
promotion/default debrief before proceeding to live instances.

Initial coordinator review corrected overstated alignment in mutable subviews,
repeated comparison of shared type pairs, and detached-node cleanup after failed
construction, including an early range-validation exit. Final coordinator review
has no outstanding findings. Focused tests cover independent promotion ownership,
authoritative binary values, different destination defaults, incompatible schemas,
live/baked record input, zero counts and zero-byte types, bounded alignment,
self-capture across buffer growth, schema lifetime and allocation-failure
preservation for creation, replacement and promotion.

Final Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1`
suites passed with 4,552 schema checks and zero failures throughout:
`stage5a-final-dbg64` (PID 82084), `stage5a-final-rel64` (PID 81336), and
`stage5a-final-dbg32` (PID 79236). The coordinator inspected actual native build
exit statuses and test output; no new compiler diagnostics appeared. Generated
layouts and resolved record sizes are unchanged, so the compiler layout matrix
was not repeated. Diff and tracked text-policy checks passed, with separate
checks of the new sources and preserved Visual Studio file conventions.
Stage 5a passed user manual review and the requested promotion/default debrief.
The user's final line-break and ternary-parenthesisation changes were reviewed
and preserved without repeating the passing validation matrix. The user authorised
the commit on 1 October. Stage 5b is implemented and has passed user manual review.

Stage 5b adds `CLiveInstances` with an owned document and aligned payload, common
instance queries, independent promotion, base/specialisation construction,
binary capture and coordinated selection edits. Promotion retains hierarchy,
strips instance locator counts, copies authoritative snapshots and checks referenced
types. The 3 October reconciliation follow-up regenerates declarations from
snapshot differences against destination defaults or immediate parents. Base capture records
a complete declaration. Specialisation capture preserves the existing selection
shape and copies only selected fields. Descendant updates inherit their immediate
parent and retain their own selected binary values, synchronising declarations.
Selection edits preserve other selected values even when their original literals
are stale, without losing exact binary encodings through document conversion.

Coordinator review corrected initialisation state, effective-default comparison,
failed-append cleanup, unintended allocation zeroing, positional edits inside
arrays, temporary diagnostic handles and selected-value preservation. Fixed
arrays retain prefix-only selection; positional structures may be converted to
named declarations without moving their enclosing array position. Final review
has no outstanding findings. Boundary tests cover promotion, append and coordinated
edit allocation failures; explicit equal-to-parent selections; stale literals;
exact NaN and Boolean encodings; bit masks; nested positional edits; rejected
fixed-array holes and interior removals; empty selections; effective scalar and
array defaults; self-capture across buffer growth; and schema lifetime/re-resolution.

Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed with 4,984 schema checks and zero failures throughout:
`stage5b-boundaries-1515` (PID 77240, retained as the final Debug x64 run),
`stage5b-final-rel64-1524` (PID 81520), and
`stage5b-final-dbg32-1525` (PID 81172). The coordinator inspected actual native
build/test exit statuses and output; no new compiler diagnostics appeared.
Generated layouts and resolved record sizes are unchanged, so the compiler layout
matrix was not repeated. Diff and tracked text-policy checks passed, with separate
checks of the new sources and preserved Visual Studio CRLF, BOM and final-newline
state. User review requested explicit condition grouping, braced control-flow
bodies and a simpler compatibility loop. These presentation changes were reviewed
against saved source copies; the loop now uses a named child count. A targeted
Debug x64 solution build passed, with final parenthesis-only touch-ups checked by
diff review. The passing runtime/platform matrix was not repeated. The user's
final formatting changes and the parameter-grouping consistency pass preserve
behaviour. User review is complete, with commit authorised on 1 October.

Stage 6a adds independent bulk output preparation and non-destructive baking,
with an explicit destination schema, a live output document or owned baked block,
and a separately owned packed payload. Both embedded and external forms retain
reserved locators. Packing follows document traversal order, preserves internal
record padding and leaves new alignment gaps for the optional stage 8 pass.
Embedded records are decoded and re-encoded to check the agreed field fidelity.
Source state and caller destinations are preserved on failure; successful baking
publishes a loaded non-owning role only after all staged work succeeds.

Integration review found two boundaries: zero-byte extents at offset zero must
not reset the loader's packing cursor, and anonymous singleton compounds inside
record arrays must use positional form to survive the parser's existing singleton
normalisation. Tests cover both structures and bit structures recursively through
arrays; no parser grammar change is required. Tests also cover interleaved type
insertion, replacement/erasure holes, alignment gaps, internal padding, empty
collections versus zero-count entries, both schema forms, default differences,
incompatible enum meanings, occupied destinations, owner transfer, source
independence, and 48 preparation / 80 baking allocation-failure points. Strict
JSON and Morphic output are parsed, baked and reloaded, including signed zero,
NaNs, raw SNORM codes and enum aliases; noncanonical Boolean and unlabelled enum
codes are rejected only for embedded output.

Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed with 5,477 schema checks and zero failures throughout:
`stage6a-final-dbg64` (PID 83944), `stage6a-final-rel64` (PID 77204), and
`stage6a-final-dbg32` (PID 82792). The coordinator inspected actual native build
and test exits and output; all returned zero with no new compiler diagnostics.
The delayed x86 tool result was recovered without repeating its passing run.
Resolved record sizes and generated layouts are unchanged, so the compiler layout
matrix was not repeated. Final expression-parenthesisation changes were reviewed
without repeating the passing platform matrix. Diff/text checks passed, including
the new header and preserved Visual Studio CRLF, BOM and final-newline conventions.
The user's final changes were line breaks only. Stage 6a has passed user manual
review, with commit authorised on 1 October, and was committed as `80c5681`.

Stage 6b adds independent instance output preparation and non-destructive baking
against an explicit baked or live destination schema. The original matching-default
requirement is superseded by the 3 October reconciliation step below. Complete
snapshots are packed in parent-before-child document
order, with separate returned document-block and payload owners. Both instance
output forms now reconcile declarations from binary and verify generated values.
Omissions and stale literals no longer constrain reconstruction. Noncanonical
Boolean codes and unlabelled enum values fail when they cannot be expressed
faithfully, including external instance output.

The agreed singleton-selection extension uses explicit traversal context in the
value codec and live selection helpers. Named compound selections simplified by
the existing parser retain their meaning inside fixed arrays and positional
structures, including nested object/array values and bitfields. Named scalar
entries remain invalid. No parser change is required. The baked instance loader
accepts canonical zero-byte extents at offset zero without resetting its packing
cursor, while retaining previously valid nonzero empty locators.

Review and tests cover document order differing from insertion order, same-named
branches, alignment gaps, mixed nonempty/empty extents, shifted destination type
indices, stale selected literals, equal-to-parent and empty selections, omitted
base and inherited-child mismatch diagnostics, both writer dialects, embedded
materialisation without supplied bytes, and subsequent parent/selection edits.
The schema example exercises instance baking and reload. Owner moves and source
disposal leave returned roles usable. Allocation-failure sweeps reach success
while verifying unchanged source declarations/snapshots, empty failed outputs,
released allocations and restored binding counts. Empty documents, zero-byte-only
roles and occupied destinations are covered.

Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed with 5,682 schema checks and zero failures throughout:
`stage6b-final-review` (PID 40572), `stage6b-rel64` (PID 80380), and
`stage6b-dbg32` (PID 80836). The coordinator inspected actual native build/test
results, all returning zero. An x86 warning in a sample assertion was corrected
with explicit size conversions and the affected builds were clean. The already
passing Debug x64 suite was not repeated for that conversion. Resolved layouts
and record sizes are unchanged, so the generated-layout compiler matrix was not
repeated. Final diff and line-ending checks passed. The user completed manual
review with only line-break changes and authorised the commit on 2 October.

Stage 1 is complete and committed independently as
`d92dddd`; its code, tests and data-model documentation exclude schema planning.
Stages 2-8 below record the completed integration and its acceptance boundaries.
Their implementation used bounded briefs and coordinator review. Stage 9 follows
the current working arrangement above.

### 1. Mutable baked integer metadata — completed

Change signed/unsigned integer mutation to recompute canonical width from the
replacement value, preserving domain, notation, prefix and structural flags.
Keep existing fixed-size payload storage and binary64 float behaviour. Update
the existing data-model API documentation with the implementation.

Completion evidence: signed and unsigned widening/narrowing across width
boundaries, including unsigned 0xffffffff to zero; signedness rejection and
unchanged-on-rejection behaviour; preserved formatting/structural metadata;
successful validation and value round trips after mutation. Run the applicable
existing data-model checks. This stage contains no schema-wrapper, locator or
schema sample changes. Review and commit it independently before schema integration.

### 2. Common query boundary and schema resolver refactor

Review concrete query/handle, resolver-adapter and ownership-result signatures
for the six wrappers before implementing the shared boundary. Separate resolution
from baked-document ownership, introduce `CBakedSchema`/`CLiveSchema`, adapt
generator name/occurrence access, and add schema promotion/baking. Include
borrowed-string lifetimes, schema reference protection through a count and
intrusive client-binding list, safe binding invalidation on schema destruction,
and stable meanings of
existing handles after successful unchanged re-resolution.

Completion evidence: equivalent live/baked schema queries and resolution,
preserved existing generator output for unchanged supported inputs, independent
schema copies, unresolved schema baking, and the agreed resolution-failure
and reference-protection behaviour. Reuse the current resolver and generator tests.
Actual instance/bulk consumers exercise the reference integration in stage 4.

### 3. Resolved-schema semantic and layout extensions

Add empty types and their omission from generated C++, authoritative explicit
layouts with alignment up to 128, and GPU-style unorm/snorm default interpretation.
Keep these changes reviewable separately from the input/ownership refactor.

Completion evidence: empty/all-empty/mixed containing types, compiler-verified
offsets/alignment/size for accepted layouts and rejection of unrepresentable
layouts, and raw-code versus normalised-float default boundary cases. Establish
the final resolved facts needed by payload codecs without a review-only fallback.

### 4. Baked instance/bulk loading and binary backing

Introduce `CBakedInstances`/`CBakedBulkData`, explicit schema binding, role queries,
locator validation and per-document binary views. Implement the value codecs and
materialisation from embedded declarations/hierarchy or complete bulk records.
Bind supplied payload views or return newly materialised owners separately.
Reconcile the sample's locator and bulk-entry grammar in this schema stage.

Deliver this stage in three bounded packages:

1. Shared internal value codecs: construct base values with defaults, require
   complete bulk records, and construct independent alternatives from completed
   parent snapshots. Share scalar conversion with resolution; check bounded
   storage, alignment and alternative-source overlap, including zero-byte types.
   Preserve source data and schema semantic order; leave byte padding untouched
   during construction and preserve inherited padding/unselected bits in alternatives.
   On conversion failure, partial low-level destination data must be discarded.
2. Baked bulk loading: role queries, explicit schema binding, locator validation,
   supplied payload views and separately returned materialised payload owners.
   Use the agreed zero-byte count and encoded-field comparison rules; reconcile
   the sample's bulk-entry grammar.
3. Baked instance loading: hierarchy traversal and independent snapshots built
   parent before child, role queries and the same locator/ownership rules.
   Reconcile the sample's reserved instance and specialisation locators.

Completion evidence: supplied-binary and embedded-only loading, reserved mutable
locator updates using stage 1, alignment/range/count/overlap checks, independent
specialisation snapshots, optional embedded/binary comparison, externally owned
payload lifetimes, and failure confined to the affected logical document.

### 5. Live data ownership, construction and editing

Deliver this stage in two bounded packages: live bulk construction/capture first,
then live instances and coordinated specialisation editing. Introduce the live
wrappers with owned buffers and non-destructive promotion from baked data. Cover
aligned appends, replacement extents, failed-append publication, unpopulated bulk
arrays, retained instance declarations and parent-before-child update traversal.
Add/remove override selections and capture must preserve explicit override intent.

Completion evidence: independent source/destination buffers, live query parity,
bulk resize/reuse rules, unchanged prior entries after failed append, capture of
selected fields only, and propagation through multiple specialisation levels.
Exercise schema-reference protection with real live consumers and integrate
application-critical allocation-failure handling without a rollback framework.

### 6. Output preparation, data baking and reload

Deliver bulk output/baking/reload first (6a), then the corresponding instance
hierarchy and selection-preservation work (6b), each with its own review boundary.

Stage 6b includes the agreed schema interpretation of parser-normalised singleton
compound selections in fixed arrays and positional structure declarations.
Verify nested compound values without interpreting their outer member name twice,
reject named scalar entries, and exercise reload followed by ancestor edits to
verify resulting inheritance. Both output forms now reconcile declarations from
snapshots, adding differences and removing equal values, subject to array-prefix
requirements. Destination compatibility permits differing defaults through the
shared reconciliation step, as for instance promotion (3 October follow-up).

Build separate output documents and packed payload buffers in document traversal
order, rewriting output locators. Implement data baking with separately returned
document-block and payload owners. Support the agreed embedded/external choices
independently of text/baked encoding, retaining instance hierarchy and declarations.

Completion evidence: output/reload value and override-intent preservation,
source preservation on success/failure, correct packed locators, and ownership
transfer without invalidating views when allocation addresses remain stable.
The current sample becomes an end-to-end fixture for its supported features.

### 7. Remapping into instances and bulk arrays

Stage 7a provides `CDataRemapPlan`, owning only its execution layouts and copy
ranges. Setup matches direct members, compares exact referenced definitions,
rejects overlapping destination writes, coalesces physical adjacency and selects
copy paths. Execution takes current bounded views and performs no allocations,
schema traversal, type conversion or document mutation. All missing or differently
typed direct members remain unmapped; partial and empty plans are valid. There is
no special enum policy and no failure for ordinary type mismatches. A compound
already excluded by its type is skipped without inspecting descendants. Only
otherwise possible matches need definition equality checks, which stop at the
first difference. Execution derives the minimum complete record capacity from
the supplied byte views and stored layouts, with no supplied record counts,
copied-count output or diagnostic. Callers narrow views to complete type strides
to limit work; any remainder in a nonzero-type source or destination view rejects
the operation before writes, even for no-match or zero-capacity transfers. A single
`execute` returns success/failure; setup retains diagnostics. Zero-byte types
impose no storage limit and all-empty transfers are successful no-ops.
Role access is completed in stage 7b below.

Coordinator review verified immediate exclusion of incompatible types, successful
partial and empty plans, removal of enum-specific mismatch policy and unchanged
binary-copy execution. It also covered repeated traversal of shared definitions,
moved-from plan state, physical coalescing and setup-selected copy paths.
Tests cover external buffers, capacities derived from bounded views, untouched fields
and padding, aggregate padding copies, preflight failures before writes,
allocation failures during setup, allocation-free execution, empty types,
no-match plans, schema disposal and 16-byte fields in larger destination records.
Additional regressions cover same-size composite/scalar exclusion while retaining
other matches, differently sized compounds skipped before descendant comparison,
and successful empty plans for former semantic-failure cases.

Usage review simplified the executor to a single Boolean operation with no
record-count arguments or execution diagnostics. Regressions cover narrowed
views and mixed zero-byte and populated sources. The original implementation
accepted incomplete trailing records and shorter-than-one-record no-ops;
the post-stage-8 correction supersedes those cases with whole-view rejection
for nonzero-type stride remainders. Existing byte views reset invalid raw
construction to empty; the executor sees zero capacity for a nonempty type
and cannot recover that discarded construction request. All views are checked
before writes; active source/destination overlap is rejected when copying.

The simplified implementation passed Debug x64, Release x64 and Debug x86
solution builds and ordinary `-t1` suites with 5,907 schema checks and zero
failures throughout: `remap-view-final-dbg64` (PID 49488),
`remap-view-final-rel64` (PID 33224), and `remap-view-final-dbg32` (PID 7636).
The coordinator inspected native build/test
results, all returning zero. Diff and line-ending checks passed; Visual Studio
file encoding and EOF conventions are preserved. Resolved layouts are unchanged,
so the generated-layout compiler matrix was not repeated. The first pass was
committed after user review.

Stage 7b reuses the existing const instance/bulk entry APIs and mutable bulk
entry API, adding `SMutableInstanceEntryView` and `CLiveInstances::mutable_entry`
for the missing live-instance destination path. Callers obtain current bounded
views and invoke the existing executor directly; no remap-specific wrapper or
parallel validation layer is required. Keep each plan associated with its source
and destination schema representations. Type indices are local to their resolved
schema, and handles require their originating role; existing lookup does not
provide globally unique document provenance. Reacquire entry views after any
operation that may relocate payload storage and do not mutate roles between view
acquisition and execution. Test these paths and document representative use.

Stage 7b coordinator review is complete. The new mutable accessor follows the
existing bulk pattern, preserving its output argument on failure and returning
an empty view for zero-byte snapshots. Tests cover combined baked/live bulk and
instance sources, external and live destinations, fresh views after payload
growth, and binary-only base/specialisation writes. Saved descendant bytes and
declaration-content checks verify that neither descendants nor selection intent
change. Invalid local handle state and access after clear fail as expected;
these tests do not imply global handle provenance.

The final Debug x64 solution build and ordinary `-t1` run passed with 5,937
schema checks and zero failures (`remap-role-final-dbg64`, PID 8396). The
coordinator inspected native build and test results, both returning zero.
Policy, diff and line-ending checks passed. No remap layout/arithmetic change
required repeating the earlier Release x64/Debug x86 matrix. The package was
committed after user review.

The primary path reads baked or live instance
or bulk data into application-owned destination buffers, with no destination
document required. Live instance and bulk destinations are additional adapters.
Support populating stage 5's unpopulated arrays
through one or more mappings; retain exact type/enum, view-capacity, overlap and
aggregate-padding rules. This stage depends on resolved layouts and live data
access, not inherently on output, but follows stage 6 in the proposed serial plan.

The user confirmed that instance destinations, both bases and specialisations,
receive binary-only writes. Declarations, selection intent and descendants remain
unchanged, including when a mapped field was previously unselected. Handle
adapters use the common executor without invoking capture or coordinated editing.
Subsequent output reconciles declarations from the resulting snapshots; it
rejects values that cannot be faithfully represented, rather than old omissions.

Fast runtime updates are the primary use case. The user agreed that document
refresh after remapping is a separate operation for the secondary document-form
workflow. Stage 9's 3 October follow-up implements that explicit operation.
The binary remapper itself does not perform reconciliation.

Completion evidence: untouched unmapped fields and records beyond the chosen count,
mixed-match and no-match plans across type categories, capacities inferred from
views including valid narrowed views, rejected incomplete trailing records and invalid
or overlapping views before writes, and identical results through bounded-view
and handle APIs.

### 8. Optional unused-storage clearing

Implement the explicit layout-driven pass, provisionally `clear_unused_storage`,
covering nested/tail padding and unreferenced bytes in the used buffer range.
Preserve unused bitfield bits by default; clear them only through an explicitly
enabled option, independently of byte-padding clearing. Preserve all addressable
fields and locators; introduce no automatic construction zeroing. Reuse layout
traversal support where appropriate.

The reviewed implementation scope is a shared typed-buffer operation and
instance/bulk document-query operations over caller-supplied writable used-payload
views, plus live-role methods over owned backing. Baked roles remain read-only
views; callers supply writable backing separately. Preflight schema/layout,
view, locator and overlap checks precede writes, including any required
allocation. Failure leaves bytes unchanged. The user confirmed that standalone
typed views must have a byte size exactly divisible by the nonzero type stride.
A remainder is invalid and rejects the entire view without processing a prefix.
Standalone fixed-array types additionally require complete declared array values;
the user confirmed that element-stride divisibility alone is insufficient.
Document scope additionally clears unreferenced bytes within
the supplied used range, without touching spare allocation capacity.

Completion evidence: idempotence, bit-for-bit preservation of addressable values,
clearing gaps imported by aggregate copies, and identical final unused bytes after
clearing outputs that began with different padding contents. Test default
preservation and explicit clearing of unused bits. Exercise it after
stage 6 packing and stage 7 transfers before binary output.

The requested post-clearing verification found that the original stage 7
executor accepted partial trailing records. The user authorised correcting this
after stage 8: require exact nonzero-type stride divisibility for every source
and destination view before any writes. Preserve minimum-capacity copying for
valid views, existing zero-byte-type handling and canonical-empty-view behavior.
No-match plans and transfers limited to zero records must still validate every
view. This is a separate review and commit checkpoint before coordinator handover.

Coordinator review covered the shared preflight guard, valid narrowed views,
partial source/destination rejection, no-match and zero-capacity preflight, and
unchanged destinations using distinct sentinel bytes. Overlap and alignment
fixtures remain stride-valid to isolate their intended failures. The final
incremental Debug x64 build and full `-t1` run passed with native exit zero:
`remap-exact-stride-review-dbg64` (PID 59732), 6,019 schema checks and no failures.
Diff, policy and line-ending checks passed. No broader validation matrix was
repeated for this narrow guard and regression-test correction.

### 9. Post-implementation review and consolidation

After the optional unused-storage stage, review and consolidate the completed
implementation for consistency, coherence and code quality. This concerns the
resulting design, code, APIs and documentation; it is not an investigation of
the development process. Pay particular attention to the authority of binary
versus document data across loading, promotion, editing, capture, output,
baking and reload, including defaults and specialisation selection intent.
Other provisional areas include code style, API consistency and agreement between
implementation and documentation. Review file organisation and responsibility
boundaries as well, including whether small shared headers such as
`data_output.hpp` justify a separate file or belong with related declarations.

The repeated schema byte-buffer alignment values now share the public
`schema::k_max_alignment` constant in `resolved_schema.hpp`. It retains the
128-byte limit and governs type alignment validation, buffer allocation,
supplied-payload checks and staging masks. Review functions with long parameter
lists for opportunities to improve decomposition and responsibility boundaries,
or to hold common context
in an appropriate class or structure. The aim is to identify shared context and
coherent responsibilities, rather than merely bundle arguments together.

The review found inconsistent parser-normalised singleton handling: bulk
loading rejected `[{"a":1},{"a":2}]` for a single-member structure, although
nested compound arrays accepted the same named form. The probe established
that `[[1],[2]]` worked and `[1,2]` failed before the input extension. On
3 October the user authorised the consistency fix and scalar shorthand for
singular compounds, including ordinary instances and nested values. The agreed
rule accepts all three record forms when the sole member is scalar, retains
complete-record checks and does not recursively flatten arrays or compounds.

On 3 October the user brought reconciliation forward and authorised its
implementation. Binary snapshots are definitive: bulk records are regenerated
completely; instance bases compare against defaults and specialisations against
their immediate parents. Equal named values are removed, differences become
explicit, and arrays retain the shortest necessary prefix without sparse grammar.
The same instance reconciliation applies in promotion, both output forms and
baking, with an explicit live-role operation after remapping. Old declaration
agreement checks are removed; structural safety, schema compatibility, generated
document fidelity and optional load-time integrity comparison remain. Snapshots
are never changed or propagated during reconciliation. This supersedes earlier
selection-preserving output decisions; authored construction/editing retains its
own selection semantics until reconciliation. External application buffers need
no document reconciliation, and remapping remains a separate binary operation.

Discuss float-to-fp16 conversion as a concrete user workflow. Inventory the
existing paths: explicit conversion through `fp16data_t` into destination
buffers, construction from numeric document values under an `f16` schema, and
capture of already converted buffers. Assess the ergonomics of converting
strided fields or arrays and whether a separately explicit schema-guided
conversion facility is justified. Review conversion policy, including rounding
and exceptional values, against the existing codec before choosing an API.
This discussion does not authorise implementation or relax the exact-type,
binary-copy-only contract of `CDataRemapPlan`.

The full review is complete. The user authorised the four defect corrections
and documentation first, followed by the bounded singular-input extension.
The remaining findings require discussion; broader refactoring is not authorised.

#### First repair package — implemented

Promotion validates declared instance groups by their document type names,
covering primitive types and retained empty groups. It preserves the existing
default-sensitive compatibility policy and leaves failed destinations empty.
Selection editing decodes retained values from authoritative bytes while copying
the declaration subtree that will be discarded, then applies the edit and
validates the resulting snapshots. Whole-declaration replacement bypasses the
old declaration. Selection-path allocation failures now report their allocation
cause without changing invalid-path diagnostics.

Regression coverage includes primitive base/child promotion into baked and live
schemas, missing or default-incompatible empty groups, empty-group preservation
through output, and enum repair through members, nested members, arrays and
bitfields. It checks retained binary values, rejection of retained unlabelled
enums, failure atomicity, whole-declaration replacement, and allocation failures
in both selection operations. Existing stale-declaration edit tests also pass.

Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed on 3 October, each with 6,086 schema checks and zero failures. Test tags
and process IDs are `schema-review-final-dbg64` (77876),
`schema-review-final-rel64` (71284), and `schema-review-final-dbg32` (78196);
runner logs use `development/logical-roots/test-logs/`. Policy validation reports
zero errors or warnings, with the existing unrelated negative-test suppression.
Line-ending and diff checks pass. Resolved layouts and generated declarations
did not change, so the separate generated-layout compiler matrix was not repeated.
Broader stage 9 decisions remain open.

#### Singular input and bulk consistency — implemented

The 3 October follow-up adds scalar shorthand for structures with one primitive
or enum member and bit structures with one scalar field. It applies through
the shared codec to instance roots, nested members, array elements and bulk
records. Instance selection decoding and overlays retain the sole member's
selection through parent edits, capture, replacement/removal and output.
Explicit output forms remain supported; syntax may expand without adding any
selected members. Arrays and compound members are not recursively unwrapped.

Bulk loading, comparison, live creation and unused-storage validation now accept
parser-normalised named compound records at the outer record-array boundary.
The value codec receives that context explicitly. Scalar array elements cannot
acquire names, unknown members remain errors, and complete bulk construction
still rejects omitted members and incomplete arrays.

Regression tests cover scalar/named/positional equivalence, live and baked
queries, integer/float/Boolean/enum and bitfield conversion, invalid ranges and
shapes, all three bulk record forms, unused-storage clearing, top-level scalar
instance loading, nested selection edits, capture, and text output/reload.
Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed with 6,261 schema checks and zero failures. Final test tags and PIDs are
`singular-final-dbg64` (36740), `singular-final-rel64` (80724) and
`singular-final-dbg32` (80460). Policy, line-ending and diff checks pass.
Resolved layouts and generated declarations are unchanged; their separate
compiler matrix was not repeated.

#### Instance default preservation — implemented, subsequently generalised

This intermediate rule was subsequently generalised by binary-authoritative
reconciliation below; its validation record is retained as delivery history.

The 3 October decision retains the intentional distinction between complete
bulk data and partial instance configurations. Instance conversion now accepts
different defaults when structure and value interpretation match. Promotion,
embedded/external preparation and baking use one preservation step for both
live and baked destination schemas. Affected omitted base values become explicit
snapshot literals; other omissions remain intact, and specialisations inherit
through their preserved parents. Array prefixes extend only as necessary.

New literals are checked for lossless encoding. Unlabelled enum codes and
noncanonical Boolean values that require new selections fail atomically with
`unrepresentable_value`. No source document or snapshot is changed. This bounded
conversion change does not implement general raw-write reconciliation or alter
bulk completeness or exact remapping compatibility.

Regression coverage exercises nested structures, singleton selections, array
prefixes, bitfields, signed floating zero, unchanged selections, target defaults
already matching snapshots, empty groups, inherited edits, all conversion routes,
text reloads, unrepresentable values and allocation failures.

Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed with 6,824 schema checks and zero failures. Final test tags and PIDs are
`defaults-verified-dbg64` (39564), `defaults-verified-rel64` (40892) and
`defaults-verified-dbg32` (15068). Policy validation has zero errors or warnings;
diff and line-ending checks pass. Generated layouts are unchanged, so their
separate compiler matrix was not repeated.

#### Binary-authoritative reconciliation — implemented

Promotion, embedded/external instance preparation and baking now derive
declarations from binary, using destination defaults for bases and immediate
parent snapshots for specialisations. Old declarations are not decoded, copied
or validated during instance promotion. Equal overrides and empty selections
are removed; array prefixes retain necessary equal scalar values. The special
changed-default pass and default-comparison bookkeeping have been removed.
Generated values still round-trip through the codec, including signed zero and
NaN comparison rules. Unlabelled enum codes and noncanonical Boolean encodings
fail whenever a faithful declaration is required.

Both live data roles expose `reconcile(diagnostic)`. They stage a replacement
document and record table, preserving payload allocation, offsets, all bytes and
schema bindings. Success invalidates document handles; failure preserves them.
Bulk reconciliation emits complete records. Bulk promotion refreshes existing
embedded data and retains reference-only entries; external bulk output remains
reference-only. Impossible explicit record counts are rejected before expansion.

Tests cover stale and invalid old literals, default and inherited differences,
removal of equal overrides, retained array prefixes, nested compounds, bitfields,
signed zero, padding and unused bits, repeated reconciliation, parent edits,
live/baked destination schemas, both output forms and text reloads. Actual remap
tests verify declaration refresh without changing any snapshots. Allocation and
representability failures leave destinations or live documents unchanged.

Final Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1`
suites passed with 7,044 schema checks and zero failures. Tags and PIDs are
`reconcile-final-dbg64` (83588), `reconcile-final-rel64` (22996) and
`reconcile-final-dbg32` (76832). Policy validation reports zero errors or warnings;
diff and tracked line-ending checks pass. Generated layouts are unchanged, so
their separate compiler matrix was not repeated.

#### Bulk replacement after reconciliation — follow-up

Review found that raw capture replacing a bulk entry retained any embedded
record array produced by reconciliation or promotion. A changed count then
disagreed with that array and caused unused-storage clearing to reject the
document. Successful replacement now removes the old array after staging and
locator replacement, leaving the entry as a reference to its new binary data.
It does not decode captured values. The entry handle is retained; failed staging
preserves the old embedded records.

Regression tests cover growth, shrinkage, equal-count and zero-count replacement
after both reconciliation and promotion, unused-storage clearing, embedded
baking/reload and allocation-failure preservation. The new checks reproduced
the defect before the fix. The design, API guide, delivery status and general
backlog descriptions have also been aligned with the implemented schema roles
and reconciliation behaviour.

The Debug x64 solution build and ordinary `-t1` suite passed, including 7,113
schema checks with zero failures (`bulk-replace-final`, PID 79864). Policy,
diff and tracked line-ending checks passed. The broader platform and generated
layout matrices were not repeated for this document-subtree removal.

#### Entry indexing and compatibility consolidation — implemented, review comments addressed

Baked loading/materialisation builds a compact sorted ordinal table before
publication. Live roles binary-search the record vector directly: fresh entry
keys are monotonic, replacement retains them, ordered erasure preserves their
order, and promotion/reconciliation create keys in record order. There is no
live hash-table allocation or maintenance. Both paths provide logarithmic,
allocation-free entry lookup. Entry identities, document order, payload
lifetime and public signatures remain unchanged.

Live bulk records retain the resolved type index established at creation or
promotion, eliminating type-name searches during access. Bound schemas cannot
be edited; unchanged successful re-resolution preserves indices, and failure
makes bindings unusable until a successful retry.

Remap setup sorts a temporary source-member name table and binary-searches it
for each destination member. Destination-member claim flags replace pairwise
range-overlap scans, using the resolver's guarantee of disjoint direct members.
Only repeated nonempty claims conflict; empty matches retain their behaviour.
These setup tables are temporary, and remap execution is unchanged.

Bulk promotion and both live output paths use logarithmic record association instead
of rescanning record tables. Coordinated instance editing uses one temporary
ordinal map for descendant membership and staged-parent lookup, preserving
parent-before-child updates even when unrelated branches are interleaved.

The duplicated instance/bulk compatibility traversals and remapper comparison
now share an operation-local context with hashed comparison keys. Representation
matching ignores defaults and `internal`; remapping definition matching retains
both checks and its ordinary-mismatch exclusion behaviour. Successful comparisons
are reusable across roots; a failed traversal discards unfinished pairs and may
retain only its proven root mismatch. Allocation failure cannot cache a partial
success. Root-header mismatches are rejected before allocating comparison state.

Review cleanup places the new index and compatibility helpers directly in the
schema namespace and brings their file headers into the established style.
Payload cursors, remap layout/range extents and bucket hashes use 32-bit values;
allocation arithmetic checks the 2 GB ceiling. Compatibility hashing mixes
pairs of 32-bit identities; node keys and scalar bit patterns retain full width.
Existing public observation types and container-facing `size_t` counts retain
their signatures. Boundary tests cover remap layouts exactly at the allocation
ceiling without allocating their payloads, and reject oversized index capacity.

Focused tests count key observations for 4,096 entries, exercise colliding hash
rollback, verify allocation-free baked/live reads and hierarchy navigation, and
cover erasure, renaming, reconciliation, reload and interleaved descendant edits.
A 512-type graph exercises shared comparison results, default/internal policy
differences and allocation-failure retry. Existing role allocation sweeps cover
baked index staging alongside document/payload publication. Further tests cover
mixed-type live bulk records across successful and failed schema re-resolution,
interleaved creation and reconciliation, duplicate empty remap matches, and a
512-member reversed-order remap with allocation-failure retries.

A subsequent legacy-code audit removed the unread baked-bulk record alignment
field and unused hash-index clear method. Hash rebuilding is private, and the
hash mixer is local to the compatibility implementation. Scalar decoding and
document-copying helpers remained active at this stage; their subsequent bounded
consolidation is recorded in the delivery status above.

Debug x64, Release x64 and Debug x86 solution builds and ordinary `-t1` suites
passed after the lookup/remap follow-up with 17,963 schema checks and zero failures
in each configuration: `schema-lookup-final-dbg64` (PID 82548),
`schema-lookup-final-rel64` (PID 8712), and `schema-lookup-final-dbg32` (PID 23608).
Policy validation reports zero errors
or warnings; the existing negative-test suppression is unchanged. Diff and
line-ending checks pass, including preserved Visual Studio CRLF, BOM and
final-newline conventions. Generated layouts are unchanged, so their separate
compiler matrix was not repeated. The user authorised committing this refinement
package, including the legacy-code cleanup below, on 3 October.

After the legacy-code cleanup, the Debug x64 solution build and ordinary `-t1`
suite passed again with 17,963 schema checks and zero failures:
`schema-cleanup-dbg64` (PID 82536). Policy, diff and line-ending checks pass.
The earlier passing platform matrix was not repeated for this small cleanup.

### Delivery discipline

Future implementation briefs should retain the user's clarified style rules:
brace control-flow bodies, explicitly group compound subconditions and keep
complex loop conditions readable through named local values where appropriate.
In multi-parameter calls, parenthesise arguments containing operations, especially
ternaries, to distinguish the arguments visually. Do not add redundant outer
parentheses to single-parameter calls, whose call delimiters already suffice.
Preserve the user's manual line breaks and spacing.

Each stage leaves a usable, tested boundary; stages 4-6 should not become one
unreviewed integration change. Run checks appropriate to each change and broader
integration checks when new boundaries are connected, without repeating passing
matrices absent a new change or unresolved concern. Keep implementation and its
current-API documentation together. Each implementation package needs review by
its coordinator when one is active, and commits require user authorisation;
the user pushes. Source ingestion remains
outside this sequence.
