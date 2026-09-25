Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   design-questions.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Sep 2026

# Schema design discussion agenda

This is a discussion backlog, not an approved implementation plan. Agreed
contracts belong in [design.md](design.md); recommendations below are proposals
for discussion. A worker brief should select a bounded stage, record the
answers it depends on, and state its acceptance criteria before work starts.

The [coherence audit](coherence-audit.md) records corrections and unresolved
boundaries found on 25 Sep 2026. Instance-error invalidation was clarified
during that audit: it invalidates the resolved schema only when the instance
and definitions share the same baked document. Remaining work includes
the remaining literal and descriptor rules for defaults. The nested layout
has removed the need for instance-reference scope and path rules. Defaults
are now confirmed to be validated during resolution;
explicit enum defaults accept only labels from the matching enum definition.
Settled material below is context, not a request to decide those
rules again.

## Settled boundaries

These do not need to be reopened to start implementation:

- Author schemas in live documents; resolve only from baked schema documents.
- Resolved schemas and their backing documents are immutable during use.
  The host may own the backing storage. Resolved schemas are never serialised.
- The schema owns resolved slots and contains a `CBakedDocument` view by value.
  Internal relationships use relocatable indices; the view contains the pointer
  to borrowed bytes. Only those backing bytes need to remain alive and unchanged,
  with no lifetime/address dependency on a host-owned view object.
- Construction/assignment are move-only; future copying would be an explicit
  function. Each resolution attempt immediately invalidates the previous
  result, and failure leaves an empty, unusable schema with no partial result.
- Instance failure also invalidates the resolved schema if the instance and
  definitions share a baked document. Separate instance-document failure does
  not invalidate the resolved schema.
- Resolved data is separate and frequently serialised: chiefly binary blobs,
  with CSV for review and limited document-form data, baked or text.
- Retain declaration name IDs into the baked document; resolve descriptive
  strings into explicit types and descriptors for fast access.
- A secondary mapping table maps baked-document slots to resolved-schema
  slots, with the same lifetime as the resolution. It identifies document
  occurrences, not just interned spellings.
- Schema access indices are unsigned; zero indicates failure or an unmapped
  reference. Named schema elements are the mapping baseline, including named
  components. Ordinary array elements need no individual mappings, only the
  reference to the array description. Resolved-schema efficiency takes priority
  over mapping coverage.
- Ordered named members support name/index access and positional instance
  construction. Physical offset order does not change declaration order.
- Accept supported input spellings, validate meaning, and normalise numeric
  representation using Morphic metadata.
- Normalising an existing schema creates a new document; the source remains
  unchanged. Normalisation uses a successful resolution and its backing document,
  reusing validation and computed metadata rather than duplicating that work.
  Generated and normalised documents include structure alignment
  and size. Both can be absent during code ingestion/schema creation or baked
  schema resolution, but every resolved type description contains them.
- Reject overlapping member ranges. Shared storage words are valid for
  disjoint bit ranges. Unions remain undecided and would need explicit syntax.
- Bulk remaps select subsets of source/destination members, reject type/size
  conflicts, and coalesce compatible contiguous transfers. Multiple source
  mappings can contribute to a destination; unselected members are untouched.
- Implement in `core/schema/`. Generate simple POD C/C++ declarations.
- Generate data declarations only, with no per-structure operational code.
  Serialisation/remapping use void pointers and resolved-schema references;
  typed template wrappers are optional later work.
- The first stage resolves and inspects schemas, with C++ structure generation
  as part of validation; it does not construct or serialise instances.
- Resolve all described type categories in the first pass. There is one logical
  definitions document, standalone or combined with instance documents.
- Each main instance branch is effectively a logical document/namespace.
  Logical documents may share one baked document or occupy separate baked
  documents. Instance evaluation requires the definitions document to be
  present in its evaluation context; instance-only input is not evaluatable.
- No mandatory document headers, document versioning, or permitted-name
  whitelist are required. Descriptors still need sufficient type information. Unknown
  properties and names that are invalid C++ identifiers are hard errors.
- Duplicate declarations are hard errors. Forward type references within the
  baked document are allowed; circular references are rejected.
- Type names are unique across the definitions document, including its category
  groups. Instance names may
  recur in different subtrees when their paths identify them unambiguously.
  Branch roots act as namespaces for instance/override names, with no collisions
  within a branch. This does not create branch-local structure type names.
- Instance values are in `declaration`; named child specialisations are in
  `specialisation`. Nesting establishes each child's base as the enclosing
  instance, which is evaluated first. No explicit base reference is required.
  This supersedes the earlier mandatory target-reference rule. No forward
  override links or downward name searching are needed, and coincident names
  in other branches do not establish inheritance.
- "Specialisation" and "override" are synonymous, not separate concepts.
- References are implicit in containment or explicit to named types in the
  one definitions document. The earlier instance-reference path scheme and
  its scope/root questions are superseded by the nested layout.
- General arrays can be replaced in full. Arrays of schema-defined named
  components also support selected-component overrides by name.
- Structure definitions can supply explicit member defaults. Without one, a
  member defaults to zero or its first declared enum value. Instance omissions
  use defaults; named override omissions preserve established values. A supplied
  short positional replacement defaults its trailing members/elements instead.
  Gap zeroing is separate, and null placeholders are rejected.
- Explicit defaults and future explicit offsets belong in the named member's
  description object. Offsets are still calculated in the first pass.
- Resolution validates defaults. Explicit enum defaults must be labels from
  the matching enum definition, including its aliases; numeric defaults are
  rejected even if they match a named value.
- Defaults are local to the structure that declares them. Nested structures
  use their own defaults or implicit zero/first-enum defaults; enclosing
  defaults do not propagate into them.
- Ingestion can recognise same-type, non-array base members of a structure as
  a named-component form. Named bitfields may support analogous overrides
  through explicit bitfield definitions; that possibility remains to be detailed.
- Natural layout is the default. Initial explicit-offset/increased-alignment
  support follows the agreed retroactive-change criterion; packed layout will
  be considered only if selected ingested structures require it.
- The first pass calculates offsets and strides. Future explicit-offset
  definitions must supply an offset for every member, without mixing inferred
  and explicit offsets.
- Natural alignment follows the current compilation target. Simple members
  align to their size; compounds respect each member's effective alignment.
  Without explicit increases, this reduces to the largest atomic member
  recursively. Structure size is rounded to effective structure alignment.
- Validation initially stops at the first error and reports its cause, member,
  and structure where appropriate and available.
- Empty structures and zero-length arrays are rejected. A possible future
  ingestion exception would discard a trailing zero-length array, not retain
  it as a schema member.
- Enum names are unique; duplicate numeric values are allowed. Unnamed numeric
  values are illegal. Output uses the first declared name for a value; any
  declared alias matching that value is semantically acceptable.
- Bit fields use contiguous, non-overlapping hexadecimal masks, with gaps
  allowed. Their declared base type determines signedness. Ingestion converts
  source bitfields into this representation.
- Structure descriptors expose total size including natural-alignment padding.
  A common gap indication covers unused bits and padding, including nested
  gaps. Zeroing is external policy, not a schema-specified fill operation.
- `f16` uses the existing `fp16data_t` transport type. Non-finite spellings
  produce actual floating point infinities/NaNs in resolved data, without
  claiming preservation of an original NaN payload, subject to existing `f16`
  conversion behaviour. Its finite-clamping behaviour is noted, not a reason
  to introduce special handling to bypass it.
- Booleans normalise to physical 0/1 and canonical document booleans. Input
  permits JSON booleans or numeric zero/non-zero interpretation. Initial
  boolean storage is 8 bits.
- Specialisation/overrides never change underlying types or structure
  definitions. Compatibility is directional: an enum backed by `u8` may supply
  a `u8` member, but raw `u8` cannot override an enum member backed by `u8`.
- Source-code ingestion is late, manually configured, and initially reads
  exact files in prerequisite order without include traversal.

## Current outstanding work

The accepted sample and subsequent clarifications close the basic grammar,
reference scope, index convention, mapping policy, computed size/alignment,
and normalisation approach. The detailed sections below retain settled context;
they are not all open questions.

Before the first implementation brief, concentrate on:

1. **Apply the delivery criterion:** exclude a separate normalised-document
   creation operation initially, while requiring any schema writer to emit
   normal form. Include explicit offsets/increased alignment only if deferral
   would require retroactive changes beyond adding those features. This is an
   assessment within the runtime design, not an unanswered preference question.
2. **Remaining value-validation rules:** the numeric conversions and range
   behaviour needed for defaults and the accepted non-finite spellings. Short
   positional/array inputs fill from the start and default the remainder;
   nulls are rejected. Named member
   omissions already use defaults or inherited values, and enum defaults are
   label-only, validated against the matching enum.
3. **A concrete runtime/API proposal:** efficient resolved records, minimal
   inspection operations, representation limits, diagnostics, and generator
   interfaces. Normaliser API work can wait. These are mostly implementation-design work to
   propose against existing codebase conventions. Mapping availability is
   documented after the efficient representation is established.

Later-stage questions remain about instance buffers and failure, bulk-record
omissions, named-bitfield patches, numeric/enum conversions beyond defaults,
remap compatibility/composition, serialisation formats, and constrained source
ingestion. They do not require reopening the accepted grammar or references.

### What needs user input now

No additional preference question is currently required before drafting the
first runtime design and implementation brief. The remaining initial-stage
items above can be developed as concrete proposals using the agreed semantics
and codebase conventions. This is authority to develop the design, not to
start implementation or create an implementing task.

Useful existing precedents include compact 32-bit document indices and typed
accessors, explicit allocation/limit failures, and one structured first-error
diagnosis carrying a reason and location. The schema's agreed zero sentinel
and invalidation rules take precedence over differing document API conventions.

Numeric conversion needs an explicit proposed contract rather than an assertion
that every existing subsystem has the same policy. Document numeric parsing
rejects overflow and non-zero underflow to zero; `fp16data_t` deliberately has
its own rounding/clamping behaviour, already accepted for schema use. For the
remaining scalar cases, the proposal should favour range checking and rejection
of fractional-to-integer truncation, permit necessary floating-point rounding,
and list the accepted non-finite spellings. Distinguish new inferred choices
from existing contracts when presenting that proposal.

The evidence for these precedents is in
[the baked view API](../../core/data_model/baked_document.hpp),
[document diagnostics](../data_model/document_parsing.md#failure-diagnosis-and-logging),
[numeric parsing](../data_model/document_text_format.md#value-classification-and-numbers),
and [fp16data_t](../../core/types/fp16data_t.hpp).

Later behaviour with multiple plausible outcomes should stay open until its
feature is designed: for example distinct-enum/remap compatibility, conflicting
destination writes, and partial instance output after failure. Those decisions
are not prerequisites for the initial resolver and declaration generator.

## First resolver: settled context and remaining contract details

### Q1. First-stage scope (settled)

The first delivery resolves and inspects schema definitions and generates C++
structures for validation. Instance construction is excluded. All described
categories are included: integer, floating point, boolean, enum, structure,
nested structure, fixed array, and bit structure. C++ generation details are
covered by Q10. Boolean storage is initially 8 bits.

Creating a new normalised document on request is excluded from this delivery;
any schema-document output still uses normal form. Explicit offsets and
increased alignment enter the initial scope only if postponement would require
retroactive changes beyond adding those features. Otherwise they are deferred.

### Q2. Definition grammar (accepted sample)

No mandatory document headers (such as `format`/`version`), versioning, or
permitted-name whitelist are required. Unknown properties and names invalid as
C++ identifiers are hard errors. Essential descriptor information, such as
array element type/count, remains necessary. The sample grammar has been
accepted; remaining validation details and extensions need their own contracts.

The member-array form is established. Its singleton text wrappers become named
array entries under the document parser's normalisation rules. Reject parser
`name_collision_extension` before baking schema text, and reject duplicate
declarations still present in the baked representation. Do not promise to
recover source duplicate properties from already-collapsed ordinary arrays.
Complete required/optional descriptor validation within the accepted grammar.
The [replacement sample](schema-example.json) and its
[review notes](schema-example-notes.md) record array, enum, bitfield,
default, and instance forms. These include categories beneath `types`, the
`default` spelling, and bit-member `type`/`interpretation` versus containing-word
`storage`. The expanded sample proposes omitting redundant array `kind`, retaining
recursive `element`/`count` descriptors; this simplification is for review.
Explicit defaults and offsets belong in the
named member's descriptor; the sample now includes explicit offsets as well as
computed natural layouts. Editor
metadata remains later work. C++ reserved words and collision
with built-in schema spellings must be handled when defining identifier checks.

### Q3. Logical documents and packaging (shape settled)

The reviewed layout uses `types` for definitions, `instances`
grouped by type with specialisations nested under their named base, and `data`
grouped by type with named arrays of records and no specialisation mechanism.
The concrete shape is accepted as illustrated in the sample and
[the design](design.md#reviewed-document-layout).
Nesting is now agreed to identify the base. `declaration` contains supplied
member values and `specialisation` contains named child specialisations, each
with the same container arrangement. Remaining choices include bulk-record
defaults and later payload association.

There is exactly one logical definitions document per evaluation context,
standalone or combined with instance definitions. Each main instance branch
effectively constitutes a logical instance document/namespace. One or several
baked documents may carry these logical documents. An instance document cannot
be evaluated without the definitions document being available. Definitions can
be resolved independently of instance data.

The resolved schema retains its definitions-storage view; the presence of
separate instance documents does not require multiple definition catalogues.
How the evaluator is supplied a separately stored definitions context remains
an API detail, not an unresolved reference-scope rule. Document-local IDs from
different baked documents are not interchangeable.

Forward type references are allowed and circular type references are errors.
Specialisations inherit from their enclosing instance, which is evaluated
first; there are no forward base-reference strings. Physical packaging does
not change this relationship. The type-reference rule preserves the sample's
`Vertex` reference to the later `ColourRgba8`. Manual prerequisite ordering for
code ingestion is a separate rule and need not restrict schema declarations.

### Q4. Layout and normalisation (rules and delivery criterion established)

The current extension discussion adds per-member `offset` with an all-or-none
rule, relative to that structure, and the `detail` section beside
`members`, replacing `config`. Alignment can be increased; placements must
respect nested types' effective alignment. Structure `size` is generated from
the maximum member end rounded to effective alignment, not an authored stride
override. Padding belongs to the structure, including embedded uses, and
ordinary array element stride derives from its size. Exposing the computed
size supports whole-structure copy/clear operations. Structure `detail` can
identify internal explicit-offset structures; they remain exportable for review
even when ordinary generated source cannot reproduce their layout. See the
[layout rules](design.md#explicit-offsets-and-structure-details).
Apply Q1's retroactive-change criterion when deciding whether the explicit
layout extension must be included initially.

Omitting `detail` or its `alignment` property selects the agreed natural
alignment rules, while still respecting explicit alignment of nested types.
Generated schema documents include structure `detail.alignment` and
`detail.size`, including documents created from code or by normalisation.
Neither must be explicitly supplied during code ingestion/schema creation or
baked-document resolution. Omitted size is computed, and a supplied size must
match the computed layout rather than override it. Resolved descriptions always
contain effective alignment and size. Normalisation uses the resolved schema
and its backing document to produce a new document without modifying the source.
It reuses resolution's validation and layout results. The normalised-document
creation operation is deferred beyond the initial delivery; its API can wait.
Schema-document generation is distinct from C++ declaration generation or
serialisation of resolved-schema internals.

Natural layout is the default and initial focus. Explicit offsets and increased
alignment follow Q1's inclusion criterion; packed layout remains conditional
on a demonstrated need in ingested structures,
potentially informed by the user's planned Vulkan header review. For natural
layout, the first pass calculates offsets and strides. Separate array-stride
overrides remain deferred. Explicit-offset definitions must specify an
offset for every member, without mixing explicit and inferred offsets.

Natural alignment follows the current compilation target: simple members
align to multiples of their size and compounds respect their members' effective
alignments recursively. With no explicit increases, the largest atomic size
determines compound alignment. A nested 12-byte record of three `f32` members
therefore naturally aligns to 4 bytes. Total size is rounded to effective
alignment. These rules are settled.

Empty structures and zero-count arrays are rejected. Storage limits and
overflow reporting remain implementation-contract details. These affect every
resolved record and the C++ declarations generated in the first stage. We can
defer modes, but a supported mode needs complete rules. Binary byte order can
be settled with binary I/O, provided the first stage does not imply an
undocumented wire format.

### Q5. What must the resolved-schema interface expose?

Ownership and lifecycle are settled: one class owns the slots and contains a
`CBakedDocument` view by value. Internal relationships use indices, not stored
pointers/references. The view itself contains the pointer to borrowed bytes;
those bytes must remain alive and immutable during use. The original view
object need not remain alive or at a stable address. Moves transfer slot
ownership and the contained view, leaving the source empty. Copy
construction/assignment are disabled; future copying would be explicit.

Every resolution attempt immediately invalidates the old result. Failure
leaves no partial resolved records or usable document binding. Reset,
destruction, and replacement end the validity of the affected resolution's
indices; a move transfers their meaning with the resolved storage. Callers
must not reuse old indices with a new resolution just because numbers match.

Schema access indices are unsigned, with zero indicating failure or an unmapped
reference. Valid indices are non-zero. The bit width remains an implementation
detail; no more elaborate identity system is required.

Serialisation/remapping callers mainly need to locate the appropriate resolved
type description and supply it with instance memory. Operations follow the
resolved indices internally. A temporary const reference for a call does not
require stored pointer/reference links between records.

The editor is likely to navigate the baked schema document and use resolved
descriptions for physical type, size, offset, and instance editing. A secondary
baked-slot to resolved-slot mapping table is required; this is no longer an
optional editor-helper lookup. Named schema elements are the mapping baseline,
including same-type named components. Ordinary array elements need no separate
mappings; only the reference to the array description needs one. Coverage must
not force a less efficient resolved representation; unmapped lookups return
zero. The table moves and is cleared with the resolution. Names alone cannot
identify members in different types. Establish the efficient resolved
representation first, then derive and document the user-facing access-availability
rules from its concrete mapping coverage. A complete coverage rule is therefore
an outcome of that design work, not a prerequisite that constrains the layout.

Temporary construction storage can remain local to resolution. Exact packing
and container choices can follow these contracts rather than become public
API. Use existing infrastructure; any necessary extension needs separate
discussion.

The public runtime observations and an optional editor helper need not expose
the entire physical slot layout or duplicate document navigation. Instance
editing leaves the resolved schema immutable. Pane construction itself remains
outside the first stage; preserving the needed correspondence is the immediate
interface-design consideration.

### Q13. Reference relationships (settled for the current layout)

Structure type names uniquely identify definitions within the document.
Instance names may recur in different subtrees, distinguished by their
containing branches. A branch root acts as a namespace, and instance/override names
must not collide within that branch. "Override" and "specialisation" mean
the same thing. A child in an instance's `specialisation` container inherits
from that enclosing instance; its `declaration` supplies changed member values.
No explicit target reference is required. The base is evaluated first:
forward links and downward name searching are excluded. Forward type references
remain allowed. Arbitrary record links and general array-index patching are
not current requirements.

Each specialisation has one base and may override any member permitted by
that base's declared type, without being restricted to the members changed by
earlier specialisations. Missing bases, invalid members, and circular
specialisation are hard errors. They invalidate the resolved schema only when
the instance and definitions share the same baked document; failure in a
separate instance document leaves the resolved schema valid.

Overrides do not change the underlying type or modify a structure definition.
They require directional compatibility with the declared destination member:
an enum backed by `u8` may supply a `u8` member, while raw `u8` cannot override
an enum member backed by `u8`. The destination keeps its declared type.
The enum is stronger than its underlying integer type. Supplying a stronger
value never promotes a weaker destination; subsequent overrides remain checked
against the destination's original declared type.

The earlier wording "each specialisation is a new type" has been clarified:
each named specialisation can be independently selected as an instance,
including intermediate steps in a chain. Its referenced type determines valid
members; selection supplies inherited values and overrides. This does not by
itself require a distinct physical layout or generated C++ declaration.

The new layout removes the earlier instance-reference scope questions:
references are implicit in containment or explicit to a named type. There is
no required instance-reference path grammar, root convention, enclosing-scope
search, or cross-document base visibility. Navigation and instance selection
do not by themselves reintroduce such a reference language.

Related remaining work, separate from reference scope:

- How does evaluation receive a separately stored definitions context? The
  document sections and nested inheritance no longer need a scope decision.
- General array replacement and named-component patching are settled and
  illustrated by the sample. The same-type component form needs no extra
  marker. Only the possible analogous named-bitfield patch remains open.
- Does construction produce independent resolved data, or retain a dependency
  requiring reconstruction when an editor changes a base? Which provenance
  is retained in authoring documents versus resolved instance data?

Reference syntax should follow these semantics. Keep the mappings distinct:
string-value ID to property-name ID recognises equal text; contextual reference
resolution selects a declaration, while containment establishes a specialisation's
base; baked-schema slot to resolved-schema
slot supplies a type description. That schema mapping does not itself identify
or contain a default instance. Remaining package details and the resolved
representation of named specialisations/defaults still need to be specified,
while retaining the distinction between type descriptions and instance values.

### Q6. What is the validation/reporting contract?

Stopping at the first error and reporting its cause, member, and structure
where appropriate and available are settled. Remaining API choices are how
this information is represented, whether reporting allocates, and how
allocation failure is reported.

Schema-resolution failure already clears the resolved schema. Later instance
failure also clears it when the instance and definitions share a baked
document; failure in a separate instance document does not. The instance
output's own failure contract remains part of Q7.

Suggested starting point: one descriptive failure with a structured reason and
document/member identity, including both ranges for overlap. Never expose a
partially validated schema as ready for use. Baked input does not inherently
provide original text line/column locations; source-coordinate reporting would
require additional provenance and should not be promised accidentally.

### Q10. Generated POD C++ declarations for first-stage validation

C++ structure-declaration generation is part of the first stage; no
per-structure serialisation, remapping, or other operational code is generated.
The worker brief should use the codebase's compiler/language conventions and
identify appropriate target validation. Names invalid as C++ identifiers are
hard errors. Enums are typed
`enum class` declarations with the schema's underlying integer storage type;
booleans use a `using` alias for `std::int8_t`; and `f16` uses `fp16data_t`.
Bit structures emit one named `constexpr` per field, consisting of the containing
storage-word type and mask value, without a descriptor object or access functions.
Generated constant naming
and scope should avoid collisions between fields in different bit structures.
Definitions will need a valid dependency order despite forward references in
the source schema.

Suggested validation in separate test tooling: compile generated declarations and compare their
`sizeof`, `alignof`, and member `offsetof` values with the resolved schema;
also check standard-layout and trivially-copyable properties. Confirm the
checks and the declaration generator's invocation/output interface. Generated
output remains data declarations only. C output remains a later scope choice.
Packed layout remains deferred. Explicit-offset review generation follows
the conditional inclusion of those structures under Q1.

For the explicit-offset extension, `detail` can identify internal structures
whose layout is not fully source-exportable. They must still have review
output showing members and layout metadata; an approximate declaration must
not be presented as layout-faithful. Explicit padding members in generated
declarations account for gaps and tail padding where needed; they do not become
logical schema members. Layout fidelity requires checking member offsets,
alignment, and size for the selected target. The internal marker spelling
(candidate: `internal`) and generator mechanics remain to be finalised. This
replaces blanket exclusion of explicit-offset structures from source review.

## Questions before the corresponding later feature

### Q7. Instance construction, defaults, and resolved-data ownership

Generic operations use void pointers and references to resolved schema
descriptions. This access approach is settled; typed wrappers are deferred.
Buffer extent, alignment, ownership, and failure behaviour still need contracts.

Do constructors initially consume live documents, baked documents, both, or
parse text directly into resolved data? The existing document path is easy to
compose; direct text construction could avoid an intermediate document, but
should be selected explicitly. Does the caller supply aligned destination
storage, does a schema data owner allocate it, or are both needed?

Default semantics are settled: the structure definition supplies explicit
member defaults, with implicit zero/first-declared-enum defaults otherwise.
Omitted instance members use those defaults; omitted override members preserve
inherited values rather than reapplying defaults. Each nested structure uses
its own defaults or implicit defaults; enclosing defaults do not propagate
into it. Logical defaults do not determine padding contents. The `default`
property and named patches inside `declaration` are settled. Short positional
structure initialisers and fixed-array inputs fill from the first element;
remaining members/elements use their defaults. Declared extents stay unchanged
and excess elements are errors. A supplied positional replacement defaults
its omitted tail rather than retaining the previous tail; named patches still
preserve unselected values.
Nulls are rejected, including in defaults; they are not omission markers.
General arrays are replaced
in full; schema-defined named components can also be overridden selectively by
name. Bulk-record omission rules and selected-bitfield patches remain open.
On conversion failure, must the destination be unchanged,
or may it contain partial output? These rules need to precede instance APIs.

### Q8. Numeric, enum, and bit-field semantics

These questions are grouped with later instance conversion, but definition
validation depends on part of them. Resolution validates defaults, and explicit
enum defaults accept only labels from the matching enum definition. Enum
interpretations must also fit their selected bit ranges. Before the first
worker handoff, specify the remaining default forms and literal/range rules
needed to validate them. Full instance construction remains outside that stage.

Which conversions are legal beyond representation normalisation: integer to
float, integral float to integer, and narrowing floats?
How are range loss, rounding, overflow, underflow, and signed zero handled?
For `f16`, reuse and document the existing transport-type conversions; no
schema-specific workaround for their finite-clamping behaviour is required.
Which non-finite strings are accepted, including longer `infinity` forms?

Enum aliases are allowed and unnamed values are illegal. Output uses the first
declared name; other matching aliases remain valid. Explicit enum defaults are
label-only. For ordinary instance input, are labels, integers matching named
values, or both accepted?
Typed override compatibility is separate: enum-to-matching-raw-underlying-type
is allowed, but raw-to-enum is rejected. If distinct enum types can supply one
another, their identity/value-domain compatibility still needs an explicit
rule; the raw/enum example alone does not decide that case.
Do flag combinations need a distinct construct, given that an ordinary enum
requires each accepted numeric value to be named?

Masks are contiguous, gaps are allowed, and base types determine signedness.
The reviewed descriptors express the field type and optional normalised
interpretation separately from containing-word storage, with masks selecting
numeric bit positions. Specify normalised-value rounding/range rules before
implementing bit codecs. Unused-space zeroing is
external policy; there is no schema-level unused-bit fill choice to settle.

Boolean normalisation is settled as 0/1 in resolved data and JSON booleans in
canonical documents, accepting numeric zero/non-zero on input. The exact
numeric input domain and treatment of non-finite input can be specified with
the conversion contract rather than inferred from C++ casts.

The replacement fixes the historical signed-enum range defect:
`SurfaceFlags.axis` uses three bits (`0x0070`), allowing `Axis.z` (2) with its
signed `i8` underlying type. This exemplifies field-range validation without
truncating an enum value.

### Q9. Bulk remap compatibility and composition

What precisely constitutes a type match: the same resolved type identity, or
equivalent recursively checked representation? Are separately named enums or
structures interchangeable merely because their storage matches? Can types
from different resolved schemas participate? Equal byte size alone is not
sufficient to settle these questions.

How are nested members and fixed-array selections named? Are packed fields
excluded unless their complete storage representation is copied? Can a whole
selected structure transfer padding? Are conflicting writes from multiple
mappings rejected or explicitly ordered? Is overlapping source/destination
memory supported? Which strides/counts are fixed at setup versus supplied at
execution, and how are available buffer extents checked?

### Q11. JSON, CSV, and binary serialisation contracts

For JSON-shaped output, choose named or positional form, defaults omission,
and strict versus Morphic text policy. Enum-label output uses the first declared
alias, as already agreed. For CSV, decide flattening
paths, fixed-array columns, quoting, headers, and whether review output must
also be accepted as input.

For binary, decide byte order, record stride, external schema
identity/version association, and where counts and lengths live. Raw payloads
need not contain an embedded descriptor. Specify when loaded bytes can be used
directly as resolved data and when alignment or representation requires a copy.
Zeroing gaps is already external policy; the I/O contract still needs to say
whether padding bytes are transferred verbatim or skipped by a given operation.
Resolved-schema internals remain excluded from every serialisation format.

### Q12. Explicitly deferred scope

Unions need a support decision before syntax or implementation. Strings,
variable-sized fields, pointers, opaque handles, and blobs likewise need an
explicit scope decision if requested; they are not implied by the fixed-size
record model. Editor presentation manifests can be designed when consumed.

Ingestion still needs exact manifest fields, file-path resolution rules, the
supported declaration subset, alias/ABI handling, and import/validation output
behaviour. Its manual order and lack of include traversal are settled. These
details should not block the first resolver.

## Suggested discussion order and worker handoff

Q1-Q3 establish the original delivery and the accepted document grammar.
Q4 adds the agreed layout/normalisation direction and the delivery criterion
to apply during design. Q5/Q6/Q10 need concrete runtime/API and validation proposals for
the worker brief. Only the default-validation subset of Q8 precedes that work.
Q13's reference relationships are settled by named types and nested inheritance;
the remaining package and instance-construction details belong to their stages.
The remaining questions become prerequisites only when their features enter a
stage; they are not a request to finish the whole system design before coding.
Question identifiers are retained as Q10 moves into the first-stage group.
Exact slot packing and optimisation can
remain implementation choices within the agreed access and lifetime contract.

Before handing the first stage to a worker, record its supported type/layout
scope and public operations, retain the accepted sample grammar, and provide
small expected-result examples. Useful candidates are an ordered four-member
vector, a nested record with a fixed array, and a padded record. Invalid cases
should cover duplicate members, unknown types, dependency cycles, overlaps,
invalid extents, and arithmetic overflow where applicable. Verify that names,
indices, offsets, size, and alignment agree with the chosen rules, and that
failed resolution cannot publish a usable partial result. Exercise failure
after a successful resolution to verify immediate invalidation and an empty
result; also verify that moves preserve index relationships and empty the source.
Include generated C++ compilation and agreed layout comparisons in the worker
acceptance criteria. Overlap fixtures apply when the supported grammar can
express conflicting ranges; deferred layout modes need not be implemented
solely to create such a fixture.

The replacement sample's original grammar was accepted. Its expanded revision
also illustrates `offset`/`detail`, nested arrays, and short positional values,
and proposes the array simplification described in Q2. The `internal` marker
spelling remains for review. Its notes give expected layouts and distinguish
these review points from agreed semantics. Complete the remaining API and
validation contract before handing implementation to a worker. Examples of
later features do not expand initial delivery scope.
