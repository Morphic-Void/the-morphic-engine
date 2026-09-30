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
Promotion/demotion form the following package. No stage 3 semantics or
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
promotion/demotion and narrow root-member selection in the existing data-model
translators. Whole-document
translation behavior is preserved. Outputs are independent copies; source
bindings survive, occupied outputs are rejected unchanged, and demotion produces
a separate owned block and unresolved baked wrapper. Stage 3 semantics and
instance/bulk conversions are not dispatched.

The conversion package passed coordinator and user review on 29 September, and
the user authorised its separate commit. The user's whitespace edits and rename from
`analyse_impl` to `analyse_common` were preserved; the coordinator checked the
declaration, definition and call sites without repeating the passing test matrix.
Review checked staged publication, independent
string ownership, safe failure diagnostics and preservation of source bindings.
The added combined-live demotion test destroys the source before resolving and
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
Explicit layout remains unimplemented. Its later contract requires faithful C++
with padding under selected compiler settings, alignment at most 128, and rejection
of layouts that cannot meet fidelity. The review-only fallback is removed.

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
- Implementation chats use Astra with High reasoning. Create a separate chat
  only when requested and give it a bounded brief with acceptance criteria.
- Existing implementing chat: **Schema stage 1**, thread
  `01a0d8ed-9e3e-71a2-91b7-2346a0791287`, host `local`. Retain it as an advisory
  query resource for the follow-on coordinator, idle between questions. The user
  authorises consultation about stage 1 implementation choices, code locations,
  conventions and validation. Check current status before messaging it.
- Advisory answers explain implementation history; current documentation and code
  remain authoritative. The stage 1 chat's earlier future-work assumptions predate
  this pivot. Route any proposed edits through the active coordinator and do not
  let advisory queries start implementation or concurrent shared-checkout edits.
- Authoritative coordinator: this requirements/coordination chat,
  `01a0e758-4731-73d2-8c97-fd0eacf7aec1`, host `local`. Substantive design changes
  return here; implementation chats do not independently redefine requirements.
- Originating coordinator thread: `01a0d361-ff3e-70c0-9bd8-4b51b7fc8b5e`.
  Thread titles can change; use IDs for coordination.
- Follow `AGENTS.md`. Commits require explicit user instruction and coordinator
  review. The user performs pushes. Preserve unrelated and manual changes.
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
| [ByteBuffers.hpp](../../core/containers/ByteBuffers.hpp) | Reuse framework copying, allocation, alignment and ownership support. Demotion's document-order packing must rewrite locators in its new output. |
| [schema_declarations.cpp](../../core/schema/schema_declarations.cpp) | The reviewed stage 2 package uses representation-neutral names and diagnostic locations. Generated output and established spelling/layout checks remain unchanged. |
| [baked_document.cpp](../../core/data_model/baked_document.cpp), [BakedDocument_test_suite.cpp](../../tests/test_suites/BakedDocument_test_suite.cpp) | Stage 1 updates canonical integer width in both directions; reviewed, validated and committed separately as `d92dddd`. Reserved locator nodes can use this prerequisite once schema loaders are implemented. |
| [Schema_test_suite.cpp](../../tests/test_suites/Schema_test_suite.cpp) | Reuse fixtures and compiler checks, revising expectations only where later decisions change semantics. The full schema sample predates locators and is not a new-loader acceptance fixture. |

## Staged implementation plan

Stage 2's read-adapter and resolver/generator packages are committed as `0addf71`
and `82d4974`. The schema-wrapper package, including guarded editing and intrusive
client bindings, is committed as `0289c6b`. Schema promotion/demotion completed
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
repeating the passing validation. Revised unorm defaults remain a separate
package and are not yet dispatched.
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

Stages 4-8 remain planned.
Stage 1 is complete and committed independently as
`d92dddd`; its code, tests and data-model documentation exclude schema planning.
Implementing chats receive bounded briefs from the coordinator and return changes
and validation evidence for review. This consolidated plan is the documentation
baseline for the upcoming schema integration.

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
generator name/occurrence access, and add schema promotion/demotion. Include
borrowed-string lifetimes, schema reference protection through a count and
intrusive client-binding list, safe binding invalidation on schema destruction,
and stable meanings of
existing handles after successful unchanged re-resolution.

Completion evidence: equivalent live/baked schema queries and resolution,
preserved existing generator output for unchanged supported inputs, independent
schema copies, unresolved schema demotion, and the agreed resolution-failure
and reference-protection behaviour. Reuse the current resolver and generator tests.
Actual instance/bulk consumers exercise the reference integration in stage 4.

### 3. Resolved-schema semantic and layout extensions

Add empty types and their omission from generated C++, authoritative explicit
layouts with alignment up to 128, and the revised unorm default interpretation.
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

### 6. Output preparation, data demotion and reload

Build separate output documents and packed payload buffers in document traversal
order, rewriting output locators. Implement data demotion with separately returned
document-block and payload owners. Support the agreed embedded/external choices
independently of text/baked encoding, retaining instance hierarchy and declarations.

Completion evidence: output/reload value and override-intent preservation,
source preservation on success/failure, correct packed locators, and ownership
transfer without invalidating views when allocation addresses remain stable.
The current sample becomes an end-to-end fixture for its supported features.

### 7. Remapping into instances and bulk arrays

Implement compatible direct-member mapping, bounded-view execution and role-handle
adapters using the same executor. Support populating stage 5's unpopulated arrays
through one or more mappings; retain exact type/enum, record-count, overlap and
aggregate-padding rules. This stage depends on resolved layouts and live data
access, not inherently on output, but follows stage 6 in the proposed serial plan.

Completion evidence: untouched unselected fields/records, enum-containing compound
mismatch, explicit/minimum counts, rejected overlapping/out-of-range views before
writes, and identical results through bounded-view and handle APIs.

### 8. Optional unused-storage clearing

Implement the explicit layout-driven pass, provisionally `clear_unused_storage`,
covering nested/tail padding, unused bits and unreferenced bytes in the used buffer
range. Preserve all addressable fields and locators; introduce no automatic
construction zeroing. Reuse layout traversal support where appropriate.

Completion evidence: idempotence, bit-for-bit preservation of addressable values,
clearing gaps imported by aggregate copies, and identical final unused bytes after
clearing outputs that began with different padding contents. Exercise it after
stage 6 packing and stage 7 transfers before binary output.

### Delivery discipline

Each stage leaves a usable, tested boundary; stages 4-6 should not become one
unreviewed integration change. Run checks appropriate to each change and broader
integration checks when new boundaries are connected, without repeating passing
matrices absent a new change or unresolved concern. Keep implementation and its
current-API documentation together. Each implementation package needs coordinator
review and user-authorised commits; the user pushes. Source ingestion remains
outside this sequence.
