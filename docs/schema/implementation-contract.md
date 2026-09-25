Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   implementation-contract.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Sep 2026

# Schema implementation contract

This document turns the [design](design.md) into bounded implementation work.
The design owns schema semantics; this file owns delivery boundaries, runtime
observations, and acceptance checks. It replaces the implementation questions
and test suggestions formerly mixed into the design discussion agenda and audit.
It does not authorise starting an implementing task.

## Initial delivery

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
checked addition, multiplication, and alignment rounding. Counts and slot ranges
use 32-bit fields. Check every narrowing and target `size_t` limit before an
allocation or address calculation; a wider calculation is not permission to
construct an object larger than the compilation target can address. Capacity
exhaustion and arithmetic overflow are errors, never wrapped or truncated values.

The owner holds sequences of type, member, enum-label, and bit-field records.
Relationships are indices and counted ranges, never stored pointers or references
into another owned allocation. A type records its category, size, alignment,
gap flag, and category-specific facts:

| Category | Resolved facts |
| --- | --- |
| Primitive | Exact primitive tag, including width and signedness. |
| Enum | Integer storage type and ordered label/value records. |
| Structure | Ordered member range; each member has its name, type, offset, and default description. |
| Array | Element type, positive count, and element stride. No record per ordinary array element. |
| Bit structure | Integer storage type and ordered fields with masks, logical types, signedness, and interpretation tags. |

Defaults need validated typed scalar values and index-based aggregate descriptions,
not an eagerly expanded instance-sized byte buffer. Retain source document indices
where useful for diagnostics and later normalisation. A large defaulted array
must not force one resolved slot per element. Gap bytes are not default values.

Exact record packing, private tagged unions, auxiliary lookup tables, and scratch
storage are implementation choices. Measure their sizes in the implementation
review before making a particular physical slot layout part of a public contract.
Any change to the observations or lifetime rules below requires design review.

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
and keyword rules for their emitted C++17 scopes; do not silently rename schema
symbols. A type cannot redeclare a built-in schema spelling. The generator must
avoid helper-name collisions rather than imposing a new user-name allow-list.
Use integer literals that preserve signed minima and unsigned 64-bit maxima.

Match the schema's size-based atomic alignment explicitly where the compiler's
default member alignment differs. Compiler validation is required; no packing
pragma or changed schema layout may hide a mismatch. A target that cannot express
the layout must be reported as unsupported. Later explicit-layout review output
may include padding members, but must distinguish approximate output from a
layout verified for the target.

## Acceptance checks and task handoff

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

Implementing tasks use Astra with High reasoning, work directly in the shared
main checkout, and run serially. The coordinator supplies a bounded brief and
reviews each result before dispatching the next. Commit only on user instruction
after coordinator review; pushes remain the user's responsibility.

## Subsequent deliveries

The following are future work boundaries, not permission to implement them now:

| Delivery | Contract to complete when scheduled |
| --- | --- |
| Explicit layout | Offset validation, supported increased alignments, non-monotonic member placement, padding declarations, and target fidelity checks. |
| Instance construction | Apply the design's baked-input, caller-owned memory, partial-output failure, local-default, complete-bulk-record, and named-bitfield rules; define concrete function signatures and bit codec quantisation. |
| JSON output and normalisation | Use existing strict/Morphic writer options; new schema documents reuse resolved facts. Choose instance named/positional output and omission options. |
| Bulk remapping | Apply the design's named-type matching, overlap rejection, and padding rules; define selected nested member/array addressing, checked buffer bounds, and execution-time extents/counts. |
| Binary and CSV | Byte order and schema association, counts/strides, padding transfer, direct-view prerequisites, CSV paths/columns/quoting, and whether review CSV is also input. |
| Source ingestion | Exact-file ordered manifest, relative-path base, supported declarations/aliases, source ABI evidence, and diagnostics for unsupported constructs. |

Strings, variable-sized fields, pointers, handles, blobs, unions, packed layouts,
independent array strides, editor presentation manifests, and generated C output
are outside the current supported model. Adding one requires a concrete use case
and its own design; they are not missing mandatory first-stage functionality.
