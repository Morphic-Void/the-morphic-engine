Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   specification.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   4 Oct 2026

# Schema specification

This document defines the current schema grammar, value semantics, ownership and
operation contracts. The [runtime API](runtime-api.md) describes concrete entry
points, file responsibilities and implementation limits; [validation](validation.md)
describes their checks. Read the [sample notes](schema-example-notes.md) alongside
the authoring example.

Future work and unresolved proposals live in the
[follow-on notes](../backlog/schema_follow_on.md), not in this specification.
Completed delivery history is summarised in the
[project milestones](../project/completed_milestones.md#schema-delivery-status).

Runtime persistence uses the version-2 [common .bsd block](common-binary-block.md).
It contains any combination of types, instances and bulk data; every present data
section has its complete binary, including explicit zero bytes. Runtime instance
and bulk values are always stripped from descriptions. Selections, hierarchy,
type defaults, decorations and non-storage declarations remain.

`CSchema` is the separate binary-free aggregate authoring form. It permits
incomplete documents for copying, merging, composition and saving. Full validation,
live materialisation and baking require contained types as the sole type authority.
Runtime attachment uses existing complete binaries and an explicitly bound types
owner; it never reconstructs a missing binary. No version-1 compatibility is provided.

Storage and typed readiness remain independent. Live roles own working binaries;
baked roles borrow stable storage. Generic Host persistence retains complete bytes.
Raw block assembly preserves supplied ranges, while live output may repack entries
and zero newly introduced inter-entry gaps. Meaningful record bytes, internal
padding and unused bits survive runtime copying. The complete file ceiling remains
2 GiB, with 128-byte base/nonempty-binary alignment and no final alignment tail.

## Purpose and scope

The schema system describes logical data types and their physical
representations. It supports ordinary structures, enums, fixed arrays and
bit structures, with instance/bulk data materialisation and exact-type remapping.
It also supports non-storage associations and interfaces with checked declaration
relationships. Packed structure layout is not supported.

Schema definitions and instance data are distinct. A physical definition declares
the meaning, layout, size, alignment, and members of a type. An instance supplies
values of a declared type. Bulk data may be represented as binary or
document values, with JSON used for definitions, metadata, and ordinary small
instances. CSV is not a core encoding.

The canonical input to type-dependent data operations is a resolved **schema
configuration** (also described as the schema catalogue).  It contains type
definitions and their default metadata, not ordinary instance data.  Once validated and resolved, it is the basis
for instance validation, physical codecs, remap validation and
execution, direct-copy remap setup, and structure-only code generation.

[`schema-example.json`](schema-example.json) illustrates the grammar, including
structure `detail`, explicit offsets and short-input examples.
It uses `element`/`count` without redundant array `kind`, and `detail.internal`
for the internal-layout marker. Its [companion notes](schema-example-notes.md)
explain expected layouts and materialisation rules. Current resolver and generator
capabilities are recorded in the [runtime API](runtime-api.md).

The remapper automatically matches direct members by name and type.
It does not search recursively or infer conversions.

Schema implementation code belongs in `core/schema/`.

## Authoring, resolution, and data

There are three runtime roles: types (`CLiveTypes`/`CBakedTypes`), instances
(`CLiveInstances`/`CBakedInstances`) and bulk data (`CLiveBulkData`/
`CBakedBulkData`). `CSchema` is the aggregate authoring owner for any combination
of their declarative sections. Resolution uses a shared `CResolvedTypes` over
live or baked definitions and is never serialised.

Runtime role queries may expose a combined physical root, but operations act on
the selected section. Each live data role owns its own document and binary. Baked
roles borrow their description and payload; clearing a role does not free them.
The backing owner must preserve byte addresses and lifetime. Promotion copies to
independent live storage under the current types binding; baking creates independent
runtime storage. Neither operation consumes or changes its source.

Type wrappers protect editing while clients are bound. Bindings are non-owning
intrusive links; owner destruction invalidates clients safely. Edits and replacement
of referenced types fail. Unchanged re-resolution is allowed, but failed resolution
makes clients unusable until successful retry. Unversioned resolved indices retain
meaning only for that owner and unchanged definitions. Raw borrowed views still
require caller-managed lifetime and single-threaded use.

Aggregate, types, instance and bulk document handles are distinct families. Handles
do not identify an individual document; retain them with their originating owner.
Live handles survive only mutations preserving their underlying tree node. Borrowed
strings expire on document mutation and binary views can expire on payload growth.

### Non-destructive promotion, baking and output

Type promotion and canonical output retain type definitions/defaults/metadata.
Runtime data promotion and baking retain hierarchy and selections without decoding
or minimising them. Equal-value overrides and explicit empty selections persist.
Raw encodings without literal representations remain usable in binary.

Explicit `CSchema::import_from` reconstructs selected instance literals or complete
bulk literals and imports only the source section. It never includes linked types
unless the source is a types owner. Import rejects values that cannot reproduce
meaningful encoded fields, including unknown enum codes and noncanonical booleans.
NaNs compare equal; the sign of zero matters; padding/unused bits do not participate.
Source textual spelling, enum alias choice and padding are not preserved by literal
import. A later compose/merge may change types; full validation uses only the final
contained catalogue.

### Locators, loading and buffers

Runtime locators contain `offset` and `valid: true`; bulk also supplies a positive
count or an exact size from which count can be derived. Optional instance count
must be one and optional size must match the type. Count/size must agree when both
are present. Locators are relative to the role's binary; extents, alignment, ordering
and overlap are checked before publication. Zero-byte records permit empty views.
Unset locators and retained literal values are not runtime construction requests.

Unframed runtime descriptions require `stripped: true` and an explicitly supplied
payload. Common-block descriptions additionally require the full envelope and
matching lengths. An absent selected domain yields an empty role; a present empty
domain has an explicit zero-byte binary. Invalid attachment leaves the role unready
without damaging its types owner or another role over the same block.

Checked rebinding validates used representations against the available old owner.
Live instances then rebuild unselected values from new defaults and immediate
parents, retaining selected bytes and existing selection intent. The live allocation
and handles survive. Baked instances require an explicit empty replacement owner if
inherited bytes would change; otherwise a read-only rebind rejects. Bulk bytes do
not change. Failure preserves the original association. Asserted recovery is
available only when the previous types are unavailable, and still validates the
target and reconstructs inheritance. It cannot bypass checks with a usable owner.

### Schema-document normalisation

An existing schema document can be normalised by creating a new normalised
document. The source document remains unchanged, including when it is baked
or backs an existing resolved schema. This is a separate document-producing
operation; resolving a live or baked document does not silently rewrite it or require
the caller to request a normalised document first.

Normalisation uses a successfully resolved schema and its backing document to
construct the new document. Validation, type-reference resolution, and layout
calculation belong to schema resolution; normalisation reuses those results
rather than implementing a second validator or layout calculator.

The output uses the canonical document representation, including
structure `detail.alignment` and `detail.size` taken from the resolved
descriptions. It preserves declaration order and meaning, including positional
member order and enum-alias preference. Invalid supplied metadata fails
resolution and cannot be silently repaired by normalisation. The new document
can subsequently be baked and resolved through the ordinary lifecycle.

The output contains schema definitions and their document metadata, not a
serialisation of resolved slots, indices, or other runtime internals.
`CResolvedTypes::prepare_output()` provides this operation. It applies
canonical integer presentation and fills omitted structure size/alignment and
array stride/alignment from resolution. Supplied defaults retain their shape;
omitted defaults and array elements are not materialised.

### Runtime representation

Declaration names remain identifiers into the backing document's name table.
Descriptive vocabulary and type-reference strings become explicit runtime
types and descriptors rather than text reinterpreted during each operation.
The owner uses variable-length sequences of fixed private records, with member
counts determined by the described structures. Public observations are distinct
from private storage and may expose wider quantities.

Current private record sizes, fields and internal range access are documented in
the [runtime API](runtime-api.md). They are implementation facts, not a public ABI
or serialised format and not constraints on representing live document identities.

Primitive, enum and bit-structure records directly supply their physical
primitive. Enums and bit structures retain their semantic category, storage
reference and ordered children. Structures and arrays have whole-object
primitive `none`; their member or element descriptions supply the next level.
Bit fields directly supply their logical primitive, including enum storage,
separately from the containing word's physical primitive.

Members expose their full byte extent, including padding owned by their type.

Layout arithmetic remains checked unsigned 64-bit arithmetic. An individual
described allocation, including nested member ends, array products and tail
padding, must fit `memory::k_byte_size_ceiling` (0x80000000 bytes, inclusive)
before narrowing. The limit is independent of the target `size_t`
range. The exact 2 GiB extent requires unsigned storage. Schema strides remain
32-bit; the memory token/view's 16-bit stride field imposes no schema limit.
Counts, document IDs and schema handles retain their existing widths. Whole-file
positions and streamed totals are a separate future domain, not in-allocation
offsets subject to this representation.

Internal operations validate their schema/type once and use transient typed
first/count ranges over the existing member, label or field vectors. Sequential
access borrows the records directly without repeated parent decoding or copying
public observations. Related type records can be reused while processing a
member or repeated array elements. These ranges allocate no persistent side
table and store no pointers into allocations. Clear, move, re-resolution and
owner destruction invalidate transient access. Checked public inspection and
instance/buffer validation remain required.

### Resolved-schema ownership and access

Wrapper ownership, handle and re-resolution rules are specified under
authoring and resolution above. The implemented live/baked wrappers and their
client-binding invalidation are documented in [runtime-api.md](runtime-api.md).

Serialisation and remapping callers mainly need to locate the appropriate
resolved type description and pass it to a generic operation along with the
instance buffer.  That operation traverses resolved indices internally.
Temporary read-only references obtained for an operation do not introduce
stored pointer/reference links between schema records.  The public interface
need not expose all slot-storage details merely to invoke these operations.

The editor navigates the document through the common query interface and
consults resolved descriptions for types, sizes, offsets and instance access.
Instance edits affect instance data; callers perform definition edits separately
and explicitly arrange resolution and any later use with data.
The mapping table supports live and baked occurrences through the shared query
boundary. Schema promotion recreates this correspondence by resolving the live
document. Mapping keys identify occurrences, not merely interned name strings.
A lookup without a mapped resolved counterpart returns zero. Public role handles
and diagnostic expectations follow the rules under authoring and resolution.

The [occurrence coverage table](runtime-api.md#occurrence-coverage) specifies which
document occurrences map to resolved records and which return zero. Ordinary array
positions have no per-element mappings. The array's resolved description supplies
its element type, count and stride. Occurrence mapping does not add resolved
records solely to duplicate document navigation.

### Resolved data and generated declarations

**Resolved data** is instance data converted into the physical representation
described by the resolved schema.  It is distinct from the schema's runtime
machinery and will frequently be serialised.  Data formats include entirely
binary blobs as the principal bulk data path and, in
limited cases, document-form instances distributed as baked documents or JSON
text.  Baking a document alone does not make it resolved schema or resolved
data.

CSchema materialisation validates contained types and values, then constructs
independent live data roles with complete buffers. Baked typed attachment instead
requires complete existing payloads and validates their layout, masks and inheritance;
it never reconstructs missing storage. Live data wrappers own buffers, while baked
wrappers borrow them. Named instances expose contiguous complete representations
without reinterpreting document values on each read. Runtime capture/overwrite and
component specialisation update binary and selection masks using their explicit
cascade rules. Bulk capture updates complete record arrays. These ownership and
operation contracts are specified in the runtime guide.

The system also generates simple POD C++ data-structure declarations.
Generation does not produce per-structure operational code: no serialisers,
remappers, member functions, or generated access routines.  Serialisation and
remapping use generic operations over bounded byte views and the relevant
resolved schema descriptions. Typed template wrappers remain separate future work.

Generated C++ enums use `enum class` with an explicit underlying type matching
the schema's declared integer storage.  Boolean declarations use a `using`
alias for `std::int8_t`.  `f16` uses the existing `fp16data_t` type.  Bit-structure
descriptions emit one named `constexpr` declaration for each field, consisting
of the containing storage-word type and mask value; no additional field
descriptor object is generated. The logical field type separately determines
value interpretation and signedness; a mask constant is not an enum field value.
These constants describe the fields; instance storage remains the declared
base storage type.  No operational code is implied by these declaration forms.

Instance and bulk data implementations use binary buffers as the common runtime
representation, with document conversion at load and output boundaries. Binary
file packaging is separate from in-memory materialisation. Source-code ingestion
remains deferred.

The implementation resolves and inspects schemas, generates C++ declarations,
and supports instance and bulk data construction, editing and output. Resolution
covers all described type categories:
integers, floating point, booleans, enums, general/nested structures, fixed
arrays, and bit structures. Declaration validation compiles C++17 output and checks
size, alignment, member offsets, standard layout, and trivial copyability, as
described in [validation](validation.md). Compiler-based checks belong to
validation tooling, separate from generated structures.

Explicit member offsets and increased structure alignment are implemented.
`CResolvedTypes::prepare_output()` creates a separate normalised schema document
with canonical integer presentation and resolved structure details. It preserves
the source and does not materialise omitted defaults or array elements.

## Definition documents and aggregation

### Logical documents and physical packaging

An authoring `CSchema` root accepts optional object sections `types`, `instances`
and `data`, including none. An absent section differs from an explicit empty one.
No runtime locators, envelope, stripped marker or payload lengths belong here.
Generic document save/load and text writing do not require semantic completeness.

Structural admission rejects unknown properties, invalid shapes/identifiers and
over-depth trees while allowing incomplete definitions and values. Composition
selects one section from each supplied aggregate; merging combines compatible
content and rejects conflicts. Neither automatically resolves the complete input
or result. Full validation, materialisation and bake require a contained types
section, resolve it, and validate all contained values against it. Missing or invalid
contained types fail; there is no external fallback or partial construction.

Named definitions are indivisible. Overlapping definitions must agree on intrinsic
shape, including member order; they cannot contribute different members to a new
type. Defaults, decoration maps and presentation merge separately. Layout equivalence
may be proved from available relevant dependencies; unrelated incompleteness does
not invalidate that proof. Missing relevant definitions can leave compatibility
unestablished, in which case merging rejects. General references may remain unresolved
until explicit validation. See [aggregation](persistence-and-aggregation.md).

Materialisation publishes an independent types owner and the roles for present
sections; destinations must be empty and remain unchanged on failure. Runtime
import produces only the selected section; composition must add types explicitly.
Runtime persistence uses a distinct complete stripped common block.

### Definition contents

The definitions section contains physical types and non-storage declarations.
The section name is `types`, containing category objects
`enumerations`, `structures`, `bit_structures`, `associations` and `interfaces`.
These categories share one namespace: names referenced as declarations must be
unique across the definitions document, not merely within a category.
Instance names may recur in different subtrees, provided each instance can be
identified unambiguously by its path.  A branch root acts as a namespace for
instance and override names.  A branch may contain multiple instances and
overrides, but their names must not collide within that branch.  This scoped
instance naming does not relax document-wide structure type-name uniqueness.
Built-in primitive spellings do not need to be redeclared.

### Document layout

The authoring aggregate separates three concerns below. Runtime instance documents
instead hold hierarchy, selection masks and locators; runtime bulk descriptions hold
locators/counts without literals. Their values exist only in binary. Locator syntax
is specified under loading and buffers and is absent from authoring input.

- `types`: structure, enum and bit-structure definitions, associations and interfaces.
- `instances`: entries grouped by type, with named base instances beneath each
  type and specialisations nested as children of their base. Further nesting
  can express a chain of specialisations.
- `data`: entries grouped by type, with each named authoring entry holding a required
  complete `data` record array. These collections have no specialisation mechanism.

For embedded-value authoring input, two container names and their relationship are
settled: `declaration` contains the instance's supplied member values, and
`specialisation` contains named child specialisations. Each child has the same
container arrangement, allowing further specialisation.

Nesting establishes the base relationship: a child inherits from the enclosing
instance whose `specialisation` container holds it. Its `declaration` supplies
only its overrides; omitted members retain inherited values. A top-level base
instance instead applies its `declaration` to the type's defaults. No explicit
base name or path is required in this form. Coincident names in
other branches still establish no relationship.

The parent is evaluated before its specialisations,
regardless of the textual ordering of the two containers. The surrounding
package and type-descriptor forms are illustrated by the accepted sample;
later extensions are recorded separately.

The bulk data collection name identifies the array, not its individual
records; it does not introduce references between those records.
Embedded bulk data records require every member explicitly, recursively through nested records
and bit structures. Positional structures and fixed arrays must have their full
declared lengths here; short-input/default completion belongs to `instances`,
not `data`. Gaps are not members; the caller may explicitly clear them through
the unused-storage operation.
Separately, the live bulk data API can allocate an unpopulated array to fill later
through remapping or other explicit writes; it requires no embedded record array.
Binary payload association, instance construction and bulk data processing are
implemented through the live/baked roles described in the runtime API guide.

Authoring entries contain declarations and hierarchy or complete bulk records,
without locators. Instances merge by matching names and specialisation paths.
Positional selection arrays merge inherited null holes and compatible overlapping
selections; a selected conflict fails. Outer bulk arrays merge an equal common
prefix and retain a longer tail. Nested complete arrays require equal lengths.
Section copying, composition, merging and import stage privately and permit
destination/source aliasing; failure preserves existing document contents.

### Definition validation and assembly

Allowed properties are listed below; `*` marks required properties:

| Object | Properties |
| --- | --- |
| Authoring aggregate | `types`, `instances`, `data` (types required for full validation/construction) |
| `types` | `enumerations`, `structures`, `bit_structures`, `associations`, `interfaces` |
| Enum definition | `storage`*, `values`*, `decorations` |
| Structure definition | `members`*, `detail`, `decorations` |
| Structure member descriptor | `type`*, `default`, `offset`, `decorations` |
| Array descriptor | `element`*, `count`*, `stride`, `alignment`, `decorations` |
| Structure detail | `alignment`, `size`, `internal` |
| Bit-structure definition | `storage`*, `members`*, `decorations` |
| Bit-field descriptor | `type`*, `mask`*, `interpretation`, `default`, `decorations` |
| Association definition | `target`*, `decorations` |
| Interface definition | `members`*, `decorations` |
| Interface member descriptor | `type`*, `count`, `decorations` |

Package sections and categories are objects; absent categories are empty.
Runtime descriptions instead require `stripped: true`; common blocks require
their envelope as well. Type resolution validates package shape and `types`, not
instance/bulk values. Full aggregate validation validates all contained sections. Definitions and descriptors
are objects. Enum `values` is a non-empty named object. Structure `members` is an
ordered array and may be empty; bit-structure `members` must be non-empty.
Singleton member wrappers follow the document model's normalisation rules.
Storage types for enums and bit structures are integer primitives. Bit-field
logical types are integer primitives, `b8` or named enums. `internal` is Boolean.
Defaults and explicit-layout metadata obey their additional rules below.

Authoring documents have no runtime envelope/version; persisted runtime blocks
use the common version-2 envelope. No permitted-name allow-list is required. Declaration names use the ASCII subset
of C++17 identifiers, with applicable keyword and implementation-reserved-identifier
checks. Non-ASCII
declaration names are rejected, not transliterated or renamed. This is a syntax
restriction, not a permitted-name allow-list; general document text remains
Unicode-capable. Generated namespace identifiers use the same subset.
Declaration names outside these rules are hard errors, and
unknown descriptor/document properties are hard errors. Vocabulary properties
such as `default` are schema syntax, not C++ declaration names. This does not restrict
permitted numeric spellings.  Type descriptors still require enough
information to define their contents, such as an array's element type and
count. The sample illustrates these descriptor forms.

Duplicate declarations within the applicable naming domain are hard errors,
including duplicate structure type names and duplicate members within a type.
They are not merged or interpreted as overrides.  Repeated instance names in
different unambiguously identified subtrees are permitted.  Forward references to types
declared later in the definitions document are allowed. Circular type references
are rejected.

Schema text input must reject the document parser's `name_collision_extension`
finding before resolution or baking; the parser's default policy already
excludes it. Resolution checks surviving duplicate named declarations in either
document form, including repeated named entries in a member array. It cannot
reconstruct source duplicate properties already merged into an ordinary array.
Direct live input does not waive schema shape and declaration validation.

The one definitions document may contain Morphic, DirectX, Vulkan, and other
selected definitions.  Any authoring-time assembly produces that single
definitions document before resolution; it does not create additional runtime
definitions documents or type namespaces merely because inputs came from
different files.

Source ingestion and manifest-driven generation are separate future work, recorded
in the [follow-on notes](../backlog/schema_follow_on.md). Instance and bulk data
documents consume the resolved configuration without redefining its types.
Automatic remap setup receives source and destination structure definitions;
it does not require an authored member-pair document.

## Associations and interfaces

`types.associations` and `types.interfaces` contain named non-storage declarations.
Their names share the namespace and identifier restrictions of physical types;
built-in names cannot be redeclared. These declarations describe relationships,
without embedding values or implying pointers, handles, ownership, runtime object
references or asset identities. They have no size, alignment, stride or defaults.
They are distinct from genuine empty physical structures.

An association requires `target`, a declaration-name string or explicit `null`.
Null means deliberately targetless; omission is invalid. The target may name a
built-in primitive, a physical declaration, another association or an interface.
Inline array descriptors are not targets. Immediate target identity is retained;
chains are not flattened into aliases. Each declaration keeps its own decorations.

An interface requires `members`, an ordered array of singleton named member
descriptors, using the same named-array convention as structure members. Names
must be unique within the interface; an empty list is valid. Every member requires
a declaration-name `type`, accepting the same categories as association targets.
Optional `count` is a positive integer through UINT32_MAX and defaults to one.
This is one-dimensional occurrence multiplicity, without physical layout or
automatic binding arithmetic; it does not expand resolved records. Offsets,
stride, alignment and defaults are invalid on these descriptors.

```json
{
  "types": {
    "structures": { "CameraData": { "members": [{ "exposure": { "type": "f32" } }] } },
    "associations": {
      "CameraBuffer": { "target": "CameraData", "decorations": { "rendering.resource": "uniform_buffer" } },
      "Sampler": { "target": null }
    },
    "interfaces": {
      "SceneResources": { "members": [
        { "camera": { "type": "CameraBuffer", "decorations": { "rendering.vulkan.binding": "0" } } },
        { "samplers": { "type": "Sampler", "count": 4 } }
      ] }
    }
  }
}
```

All targets must exist within the resolved input document. Forward references and
shared targets are valid. Association-target and interface-member edges together
must be acyclic, including cycles through interfaces. Existing physical dependency
rules still apply; physical members and array elements cannot reference non-storage
declarations. A chain need not terminate at physical storage. Reference diagnostics
identify the closing edge and its target declaration for cycles.

General declaration compatibility requires matching category/identity, target
presence and immediate identity, ordered interface member names/references and
effective counts. Dependencies are compared recursively. Strict definition
comparison additionally includes decorations and physical defaults/internal markers.
Equal layouts cannot substitute differently named targets. Aggregate merging admits
overlapping named definitions only as compatible whole definitions, then merges
eligible document content and decorations transactionally. It cannot extend a
matching interface's member list or supply an association target where the other
declaration explicitly has null. Inputs may contain unresolved references and be
composed before full validation; all references must resolve within the final
contained types at validation, materialisation or baking. Persistent cross-document
references remain unsupported.

Physical queries, default construction, instance/bulk data, remapping and unused
storage clearing reject non-storage declarations. Schema copying, promotion, baking,
canonical output and composition retain them; data-only operations remain data-only.
C++ generation emits deterministic comments for their names, targets and interface
members/counts, and continues generating all physical definitions under the existing
empty-type rules. It emits no C++ storage types or aliases for associations/interfaces.
Core interprets no rendering vocabulary. Matrix semantics, runtime bindings,
multidimensional/runtime multiplicity and external imports remain outside scope.

## Declaration decorations

Named structures, enumerations, bit structures, associations and interfaces,
structure and interface members, bit-structure fields and fixed-array descriptors may carry an optional
`decorations` object. Each nested array descriptor owns its own object. For
example, a member descriptor can contain:

```json
{
    "type": {
        "element": "f32",
        "count": 4,
        "decorations": { "numerical.shape": "vector" }
    },
    "decorations": { "editor.label": "Samples", "measurement.unit": "metres" }
}
```

Every key must be nonempty and unique within its object, and every value must be
a string. Keys follow document string rules, not C++ identifier rules. Empty
strings are valid and distinct from absent keys. An empty object is valid.
Comparison uses exact decoded string contents, including case and whitespace;
there is no trimming, case folding or Unicode normalization. Escape spelling is
not content. Prefixes are consumer conventions; Core registers no vocabulary and
preserves unknown keys. Non-string values and malformed objects are errors.

Decorations describe the declared contract and have no implicit inheritance,
propagation or precedence. A member and its referenced type may independently
carry the same key. Core does not interpret decorations or let them change
layout, defaults, value validation or construction. Consumers apply semantic
checks appropriate to their vocabularies. C++ generation ignores decorations.

Sections, structure `detail`, enumeration labels, instance/bulk entry descriptors
and value initializers gain no decoration facility. Built-ins have no authored
declaration to decorate; decorate their member/array occurrences instead.
Ordinary types, members and enum labels named `decorations` remain legal.
Unknown grammar properties outside the supported attachment objects remain errors.
Existing schemas remain valid. Older readers reject the new descriptor property
through their existing strict grammar; the document encoding is unchanged.

Copying, schema promotion, baking and canonical schema output preserve decoration
maps and attachment locations. Canonical output does not add omitted decoration
objects. Composition and whole-document baking retain the selected types section.
Data-only import, promotion and persistence continue to select their data
domain and do not embed a schema or manufacture metadata from bytes.

Representation compatibility and exact remapping ignore decorations. Strict
definition comparison includes maps at every supported attachment point, ignoring
key order and treating omitted and empty objects as equivalent. Remapping retains
its existing default/internal-marker checks while ignoring decorations.

After whole-definition compatibility is established, schema merging retains keys present in
only one source, keeps one copy of equal values, and rejects differing strings
under the same key with locations in both sources. This is a transactional
document-content conflict even when the layouts match. There is no implicit
concatenation or precedence; output uses existing deterministic merge order.

## Declaration and reference identifiers

The document model has separate interned domains for property names and string
values.  The schema loader preserves that distinction:

- a property-name identifier declares a schema entity or member name;
- a string-value identifier can supply reference text; and
- context determines what kind of declaration or reference is permitted.

Interned name identity alone does not identify a declaration occurrence: different
structures can declare the same member spelling. Type references resolve against
the definitions catalogue; enum labels resolve within their declared enum.
Text equality across documents does not make their local identifiers interchangeable.
The resolver records type relationships and occurrence mappings during resolution.
Resolved name lookup uses the document's logarithmic interned-name lookup followed
by binary search of a scoped ordinal index. Declaration order is preserved;
callers can retain resolved indices for repeated access. Name IDs from different
documents are never compared directly.

The current reference model is:

- an explicit type reference identifies its named definition in the one
  definitions document; and
- an instance specialisation derives its base from containment and overrides
  selected values, potentially through a chain of specialisations. It does not
  need a reference string to establish this relationship.

The second use builds on the specialisation rules below. Instance names may
recur in different subtrees; their containing branches distinguish them.
An override's enclosing instance establishes its base through the
`specialisation` container; `declaration` holds changed values. No base-name
lookup or target path is needed for this nested form. There is no forward
override link or downward search for a matching name. This differs from
forward type references, which remain permitted. Coincident declaration names
alone do not create an override relationship. Each named specialisation can
be independently selected as an instance. Arbitrary
record links are not a current requirement.  A selectable instance specialisation supplies inherited
values and overrides; its referenced type determines the permitted members
and physical layout.  Selectability does not by itself require a distinct
physical layout or generated C++ structure for each specialisation.  The
remaining package details are described in the document-layout discussion.

Resolved records retain type categories, primitive tags, member indices and
offsets for runtime operations. Document-local IDs are never durable
cross-document identities.

### Reference scope under the nested layout

The nested layout defines no relative or absolute instance-reference strings,
enclosing-namespace search or cross-document instance-reference visibility.
Type lookup uses the one definitions document; specialisation inheritance uses
the enclosing instance.

Document navigation, diagnostics identifying member locations, and selecting
a named instance remain useful operations. They do not imply a path-based
reference language inside the schema or instance documents. Any future need
for such a language should be considered separately when a consumer requires it.

## Morphic JSON and numeric metadata

Schema documents and document-form instance data use the Morphic JSON
extensions and document metadata where appropriate.  The existing
[document numeric contract](../data_model/document_text_format.md#value-classification-and-numbers)
supplies integer signedness and notation; these should remain numeric values,
not strings introduced solely to preserve formatting.

Signed integers carry signed-domain metadata.  In text, non-negative signed
integers use an explicit `+` (including `+0`), while negative integers use `-`.
Unsigned integers have no sign.  Bitfield masks and similar bit-oriented values
use hexadecimal notation with the `0x` prefix rather than the alternate `#`
prefix. Masks use the full width of their storage type, including leading zeroes:
two hexadecimal digits for 8-bit storage, four for 16-bit, eight for 32-bit, and
sixteen for 64-bit storage. This is the containing storage width, not the width
of the selected field. Document numbers have no C++ literal suffixes.

Ordinary schema values of types `i8`, `u8`, `i16`, and `u16` use decimal notation.
Values of types `i32`, `u32`, `i64`, and `u64` use hexadecimal notation by default,
even when the current value is small. This refers to the declared schema type,
not the smallest width of the particular value or the document model's internal
numeric storage. Signed hexadecimal values retain the signed-domain sign, such
as `+0x01` or `-0x00010000`; mask notation takes precedence over ordinary value rules.
Structural quantities such as counts, offsets, sizes, and alignments use decimal
within the inclusive range -65535 through +65535, and hexadecimal outside it.
Numeric notation does not itself select a schema type or change its physical
width. These conventions govern construction and normalisation, not input
spelling restrictions.
Input accepts valid JSON and supported Morphic extensions under the document
parser's acceptance policy without requiring canonical schema notation.
For example, a decimal mask is not rejected merely for lacking a `0x` prefix,
and a non-negative integer need not carry an explicit `+` on input to a signed
schema field if its value is representable in that field.  Schema processing
validates the value against its expected type before normalising its metadata.
Canonical document metadata is emitted when constructing a new document;
resolution does not rewrite its live or baked input in place.

Schema-document representation uses the existing `CIntegerMetadata` domain,
notation and prefix flags and the document writer. It must not reparse formatted
numeric strings or introduce a separate schema numeric formatter. The metadata's
width remains the document model's smallest valid width in the selected domain;
the declared schema type selects notation and must not be encoded by overriding
that width. Generated C++ source has its own literal spelling requirements.
The generic document writer supports minimum hexadecimal widths and adaptive
decimal/hex notation, with parsed padding retained as presentation metadata.
`CResolvedTypes::prepare_output()` copies the `types` section and applies these
rules to enum values, integer defaults (including nested arrays), masks and
structural quantities. The source may be live or baked and is not modified.
The destination must be uninitialised; failure publishes no partial document.
Definition order, spelling of names, value structure and non-integer content are
preserved. Omitted structure `detail.size` and `detail.alignment` are added from
the resolved layout; missing defaults are not materialised.
Plain document copying and schema wrapper baking/promotion preserve existing
metadata; callers explicitly request canonical schema output through this API.

Instance and bulk data scalar reconstruction applies the same declared-type rules as
schema defaults. This applies to explicit value import. A bit-field value uses its logical declared type for notation,
not the narrower field width; its mask uses the containing storage width.
Rebuilt locators use adaptive decimal/hex metadata. Copied input declarations keep
their spelling metadata until reconstructed. Enum data remains label strings,
and floating-point/Boolean data retains its existing representation. Binary
notation is available in the data model but is not selected by schema policy.

Permissive spelling does not permit invalid schema definitions or instance
values.  Malformed syntax, invalid layouts, and values incompatible with their
expected types are errors.  Validation should produce descriptive diagnostics
identifying the affected type/member path and the reason for rejection.
Validation stops at the first terminal error and returns an allocation-free
structured diagnostic with document locations and related ranges where available.
The caller formats or logs it while the input document remains alive; the
[runtime guide](runtime-api.md#limits-and-diagnostics) describes its fields.
Schema-resolution failure
already has the empty-result contract specified under ownership and access.
Instance/bulk data load failure is confined to that logical document, including when
its baked input shares a block with schema definitions.

## Primitive physical types

### Literal validation

The following conversion rules define default validation, using the document
numeric contract, permissive input policy and existing `fp16data_t` conversion.
The same rules apply when instance construction consumes document values.

- Integer destinations accept signed or unsigned integer payloads in range,
  regardless of source notation. Finite floating-point payloads are accepted
  only if integral and in range; reject fractional truncation. Check exact
  signed/unsigned boundaries before casting, without routing 64-bit integer
  payloads through `double`. Floating negative zero converts to integer zero.
- `f32` and `f64` accept integer or finite floating-point payloads, allowing
  precision rounding. Reject finite overflow and non-zero underflow to zero;
  representable subnormals are allowed. Preserve floating signed zero.
  The document has already rounded numeric text to its stored value; the
  schema cannot recover digits lost in parsing.
- `f16` uses the existing transport conversion, including its rounding,
  underflow, and finite-clamping behaviour. Do not impose the `f32` rejection
  policy around that conversion or bypass it with schema-specific bit packing.
- `b8` accepts document booleans or finite numeric payloads: either sign of
  zero becomes false and every other finite value becomes true. Do not accept
  numeric strings or floating special strings as Boolean input.
- Enum values use matching labels. Declaration values themselves must be
  integral and fit the enum's integer storage. An enum must contain a label;
  duplicate values are aliases, while duplicate labels fail.
- Strings are not general numeric coercions. Only the explicitly listed
  floating special spellings and enum labels have typed meaning. Null is invalid.

Masks and extent metadata must represent non-negative integers and satisfy
their additional width/count/alignment constraints. A bit field's integer
domain must fit both its logical type and its mask width; enum labels must all
fit that width using the enum's signedness. Boolean fields have the logical
domain 0/1. `unorm` requires an unsigned integer primitive; `snorm` requires a
signed integer primitive and at least two mask bits. Neither interpretation
applies to enums or Boolean fields. Input and defaults accept raw integer codes
or normalised floating values under the codec rules below; output uses stored
integer codes.

Absent interpretation means the declared type's ordinary value semantics.
Other interpretation spellings require an explicit extension, not silent fallback.

### Physical vocabulary

The primitive vocabulary is:

| Category | Types |
| --- | --- |
| Signed integer | `i8`, `i16`, `i32`, `i64` |
| Unsigned integer | `u8`, `u16`, `u32`, `u64` |
| Floating point | `f16`, `f32`, `f64` |
| Boolean storage | `b8` |

Boolean storage is 8 bits.

Resolved physical values are normalised to 0 or 1.
Input accepts JSON booleans and relaxed numeric interpretation: zero is false
and non-zero is true.  Canonical document-form values, including baked
documents, are JSON booleans rather than numeric 0/1.  This normalisation is
schema-aware and does not change the general document model's numeric values.

`f16` uses the existing IEEE-754 binary16 transport type
[`fp16data_t`](../../core/types/fp16data_t.hpp), including in generated C++
declarations.  Conversions reuse its existing machinery rather than introduce
a schema-specific implementation.  Note that its default conversion can clamp
infinities to finite values.  This is existing transport-type behaviour to
document, not a requirement for schema-specific work to bypass or change it.

An enumeration is a logical type with one of the integer types as its declared
physical storage type.  Its symbolic values are part of the enum definition;
the underlying storage supplies size, alignment, range, and binary encoding.
Enum names must be unique within their declaration, but different names may
have the same numeric value.  Values not named in the enum definition are
illegal.  Output uses the first declared name for a value; any declared alias
matching that value is semantically sufficient and must not be rejected merely
because it is not the preferred output name.

## Structures and layout

A structure has one canonical declared sequence of members.  This declaration
order is its semantic positional order.  A structure also has resolved size and
alignment, and every member has a resolved physical offset.
Empty types are permitted in live and saved schemas. They retain resolved
identities and contribute zero-byte members. Generated C++ omits empty types
and their members; non-empty containing structures retain their physical layout.

The declaration form is an array of named members, each expressed as
a single-member object whose property name declares the member and whose
value describes it:

```json
"members": [
  { "x": { "type": "f32" } },
  { "y": { "type": "f32" } },
  { "z": { "type": "f32" } },
  { "w": { "type": "f32" } }
]
```

Array order establishes member indices, while names support named access.
An explicit default and an optional explicit offset belong in the named member's
description object, alongside its type.
`schema-example.json` uses this ordered named-member text form.
Under the document model's singleton normalisation, these wrappers become
named children of the `members` array, retaining the descriptor payload.
Resolution reads that baked representation; it must not require a redundant
anonymous wrapper object to survive parsing. See the
[document interpretation rules](../data_model/document_text_format.md#collision-extension-and-singleton-objects).

Offsets do *not* determine positional construction order.  Natural layout may
leave padding; padding is not a member and never consumes a positional
initializer position. Non-monotonic explicit offsets do not change schema
declaration order.

Overlapping member storage ranges are rejected.  Bit-structure members may
share a storage word, but their actual bit ranges must not overlap.  An overlap
diagnostic should identify both conflicting members and their byte or bit
ranges. Overlapping members do not implicitly create a union; union support is
outside current scope.

Natural layout is the default. Explicit offsets and increased alignment obey
the rules below. Packed structure layout is unsupported.

Natural layout calculates member offsets and default array strides. Explicit
layout retains the same resolved observations; arrays may specify independent
stride and alignment as described below. If any member supplies an explicit
offset, every member of that definition must supply one; explicit and inferred
offsets are not mixed.

Natural alignment follows the schema's size-based rules. Simple members
align to multiples of their size; compounds use their members' effective
alignment recursively. Without explicit increases, this is the size of the
largest atomic member. Thus a nested 12-byte structure containing three `f32`
members naturally aligns to 4 bytes, not 12. An explicit increase on a nested
type propagates to its containing layout. Natural-layout structure size is rounded
to the effective structure alignment; an explicit-layout size must already be a
valid multiple of that alignment. Each resolved definition must
provide fixed size, alignment, offsets and, where relevant, stride. The member-list
sequence establishes ordinal meaning; there is no separate ordinal property.

The structure descriptor includes its total size, taking natural alignment
and tail padding into account.  This describes the structure as a whole; it
does not introduce an instance member or positional initializer slot.

### Explicit offsets and structure details

The explicit-layout form adds `offset` directly to each named member descriptor,
without a new nesting level or a separate layout-mode field. If any member has
an offset, every member of that definition must have one. Offsets are relative
to the start of that structure, regardless of where a containing structure
places it. The member's placement must respect its type's alignment, and
overlapping member extents remain invalid. Declaration order remains semantic
member order even when offset order differs.

The `detail` object beside `members` contains structure-level metadata.
It is optional for natural layout and
required to supply size for explicit layout. Authored
`alignment` may increase the structure's alignment requirement above
its natural requirement, not reduce it. A containing layout must respect the
effective alignment of a nested type, including explicit increases; the
natural largest-atomic-member rule alone is insufficient in that case.

If `alignment` is absent, including when a natural-layout definition omits
`detail`, use the natural alignment rules. Omission does not select packed layout
or byte alignment. Explicit alignment requirements of nested member types still apply.

Generated schema documents include both `detail.alignment` and `detail.size`,
recording the resolved effective alignment and total extent. For natural layout,
both remain optional on input: size is calculated and a supplied size must match
that calculation. Normalisation reuses the resolved facts.

For explicit-offset structures, `detail.size` is required and authoritative.
Resolution validates the supplied extent rather than deriving the structure size
from its members. All member ranges must fit, offsets must respect member
alignment, and ranges must not overlap. The size must be a multiple of effective
alignment so consecutive array elements remain aligned. Extra trailing space
belongs to the structure and contributes to its gap flag. Omitted alignment still
uses the rules above. Resolved runtime records themselves
remain non-serialisable.

Only positive-size member ranges participate in overlap checks. An empty member
retains its authored offset between zero and the structure size, inclusive, but
occupies no bytes and does not introduce a gap.

Accepting and validating generated `detail` metadata for natural layouts is
part of the base schema contract. Schema output preparation writes these computed
values into a separate document. A supplied alignment can
equal the natural requirement or increase it, subject to the alignment rules.

Every successfully resolved type description contains its effective alignment
and size regardless of whether the input supplied them. Optional document
metadata for natural layouts does not mean optional runtime metadata. A normalised
document records validated structure details without modifying the input document.

For example, generated details for an aligned position would be:

```json
{
  "detail": { "alignment": 16, "size": 16 },
  "members": [
    { "x": { "type": "f32", "offset": 0 } },
    { "y": { "type": "f32", "offset": 4 } },
    { "z": { "type": "f32", "offset": 8 } }
  ]
}
```

For natural layouts the size calculation remains:

```text
size = round_up(max(member.offset + member.type.size), structure.alignment)
```

Member ends use each member type's full resolved size, including its own
padding; this applies recursively to nested structures and arrays. The explicit
example supplies size 16 for a member extent of 12 bytes. Bytes 12-15
are padding owned by the structure, including when it is embedded. Consecutive
elements in an ordinary array start 16 bytes apart, and a containing structure
cannot place another member inside that padding. The gap indication includes
this extra tail padding. A supplied size of 32 would also be valid for this
explicit layout: bytes 12-31 would belong to it and its array stride would be 32.

The resolved descriptor exposes `size` as a precomputed extent for operations
such as copying or clearing the whole structure. It does not impose automatic
zeroing during construction. Default array element stride is derived from that
size; `stride` is not a separate structure property. An explicit-layout size includes all owned
spacing, rather than introducing a separate stride override. A supplied alignment
sets a layout requirement; a supplied explicit-layout size sets the total extent.
Explicit structure alignment is capped at 128 bytes. Accepted explicit layouts
must produce faithful C++ declarations under the selected compiler settings;
reject layouts that cannot meet that contract. Generated padding can account for
member offsets and total size, but it is not a logical schema member or an
initialisation policy. Compiler checks establish offsets, alignment and size.
Generated C++
fields may be emitted in physical offset order when offsets are non-monotonic
in the schema. Schema queries, member ordinals and document positional values
retain schema declaration order. C++ aggregate initialiser order therefore need
not match document positional order. Reject layouts that cannot meet physical
fidelity instead of falling back to approximate output. Compiler evidence is
required for the accepted layout subset.

The internal marker is `detail.internal: true`; omission is equivalent to false.
It permits neither bypassing validation nor falling back to review-only output.
Natural-layout declaration generation remains
unchanged, and padding declarations do not add generated operational code.

This section records the full layout rules. The expanded sample includes both
natural and explicit layout examples. Resolution, declaration generation and
normalised-document creation support these rules; see the
[runtime API](runtime-api.md) for their interfaces.

### Gaps and storage initialisation

The resolved description records whether any gaps exist, without separate
categories for unused bitfield bits and alignment padding.  The indication
covers internal and tail padding and gaps contained in nested member types
or array elements, so an enclosing structure exposes whether any of its
storage contains gaps. The flag occupies bit zero of the private type record's
control byte.

Clearing unused storage is an optional, explicit operation named
`clear_unused_storage`; its typed-buffer, document-buffer and live-role entry
points are described in [runtime-api.md](runtime-api.md). It uses resolved
layouts to zero internal and tail padding recursively
through structures and arrays. Unused bitfield bits are preserved by default;
clearing them is a separately enabled option, which preserves every declared
field bit. At document-buffer scope, locator extents also identify gaps
between records/collections and other unreferenced bytes within the used payload
range. Unused allocation capacity is outside that range. The pass does not
compact storage, move records or change locators.

A standalone typed-buffer call requires its byte-view size to be an exact
multiple of the nonzero type size. A remainder makes the entire view invalid;
reject it without processing any records or changing bytes. For a standalone
fixed-array type this includes all slots and additional array tail padding;
divisibility by element stride is neither required nor sufficient. Document-buffer
calls use locator extents to distinguish occupied storage from unreferenced bytes
within the supplied used range. Preflight, including any needed allocation,
completes before writes so failed clearing leaves the payload unchanged.

Every addressable field is preserved bit-for-bit, including fields not selected
by a particular remap. The operation neither applies defaults nor populates
uninitialised fields. It is idempotent and requires writable storage with valid
layout and range information. Like remapping, it can use a layout-driven traversal;
it does not need a second schema representation or source-value buffer.

Unpopulated bulk data arrays have
no zero-fill guarantee. Raw copies and whole-aggregate remaps continue to copy
source padding; running the optional pass afterward removes byte padding and,
when explicitly enabled, unused bitfield bits.
For stable file output, run it after final packing/copying on the output buffer.
Non-zero gaps remain legal when the caller elects not to clear them. Logical
field initialisation remains separate from unused-storage clearing.

## Fixed arrays

An array is a normal type constructor, usable as a member type or nested inside
another array. A fixed array declares an element type and count, with optional
independent element `stride` and array `alignment`.

Arrays can therefore contain primitive values, structures, or further fixed
arrays.  A structure may contain fixed arrays in the same way as any other
member type.

The array descriptor is `{ "element": "f32", "count": 2 }`. Both properties are
required; `element` accepts a named type or another array descriptor. Count 1
remains an array. No `kind` property is accepted.

Omitted stride is the element's complete size. An explicit stride must cover
that size and be a multiple of the element's alignment; it need not be a power
of two. Omitted alignment is the element's alignment. Explicit alignment must
be a power of two, at least the element's alignment, and at most 128 bytes.
Alignment applies to the array's start, not to every element: four contiguous
`f32` values can have stride 4 and array alignment 16.

The complete size is `round_up(count * stride, alignment)`. Every element owns
a full stride slot, including the last, followed by any additional array tail
padding. Both kinds of padding belong to the array and contribute to its gap
flag. There is no authored array `size` property. Arithmetic and rounding must
fit the existing storage ceiling. Each nested array applies these rules
independently, using the inner array's complete rounded size as its default
element stride. Arrays of empty elements retain size/stride zero and alignment
one; explicit metadata cannot introduce storage or stronger alignment.

For example, `{ "element": "u32", "count": 2, "stride": 12, "alignment": 16 }`
has values at offsets 0 and 12, two complete slots occupying 24 bytes, and eight
additional tail-padding bytes, for a total size of 32. Repeated complete array
values start 32 bytes apart. Complete size need not divide evenly by element
stride. Canonical schema output records effective stride and alignment even
when omitted on input; source-preserving copying/baking retains authored omission.

C++ generation retains ordinary arrays where possible. Additional stride uses
a slot wrapper containing one `value`, or an `array` payload when the element
is itself an array, followed by explicit padding. Increased array alignment
uses an `alignas` wrapper with an `array` member and explicit additional tail
padding where needed. Thus an aligned array of padded scalar slots is accessed
as `member.array[i].value`. Wrappers are data-only, standard-layout and trivially
copyable. They introduce no schema members or extra document initializer levels.
Helper names are deterministic and collision-safe, but not a permanent naming
API. Generation remains complete-catalogue output, useful for visual inspection
and manual validation; selective output and shader-type aliases remain future work.

Zero-length arrays are rejected. There is no current exception for a trailing
array. This restriction concerns the declared array count, not the length of
a short initializer completed with defaults.

### Named-component forms

A fixed array accepts positional values, with no named element or sparse-index
syntax. A structure supplies named components as well as positional construction.
The schema establishes their names and order; names are not inferred from
arbitrary array contents. A structure of
same-type, non-array base members can supply this form, as in a vector with
named `x`, `y`, and `z` components.  Recognising that form does not change its
declared physical layout.

For base construction, an omitted positional tail is completed from defaults.
For a specialisation, supplied positions select values in the independent
alternative and the omitted tail inherits from its base. A named declaration
can select only `y`, retaining the other inherited components. Neither form
modifies the base. This does not introduce placeholder values or a general
indexed-array patch syntax. The accepted sample recognises
the form from same-type, non-array primitive members without an extra marker,
and uses a named `declaration` object for selected-component overrides.

## JSON instance data

Null is an inheritance hole only at a positional instance/selection-array element.
It is not a literal null value, a named-field value or a whole declaration. Holes
can occur inside positional structures and fixed arrays, including nested selections.
Defaults and complete bulk records still reject nulls. Named omissions and positional
holes retain the immediate-parent/default value without adding a selection.

The type comes from the enclosing type group in `instances` or `data`, or from
an explicitly supplied resolved type description in a value-construction operation.
The named-instance form is:

```json
{
  "instances": {
    "Position": {
      "example": {
        "declaration": { "x": 0.0, "y": 0.2, "z": 0.7 }
      }
    }
  }
}
```

A base instance accepts either named-object or positional-array construction:

```json
{ "x": 0.0, "y": 0.2, "z": 0.7 }
```

```json
[0.0, 0.2, 0.7]
```

The array maps index zero onward to the structure's declared member sequence,
not its offset order. A shorter positional input is valid: supplied values
fill from the first member and remaining members use their defaults. The same
rule applies to fixed arrays, whose remaining elements use their applicable
defaults. Excess values are an error; a shorter initializer does not change
the declared member count, array extent, or physical size.

For a four-component vector, `[2.0, 1.0, 4.0]` supplies `x`, `y`, and `z`;
`w` uses its declared default or implicit zero. This supports a fourth component
used for padding without requiring an author to spell it out. It remains a
logical member with a default, distinct from unnamed alignment or bitfield gaps
which the optional unused-storage pass can clear.

An array is therefore a compact construction form, including default completion
for a base instance. In a specialisation, its supplied prefix instead selects
alternative values and null/omitted positions inherit. An object identifies selected
values by member name. The specialisation rules below apply recursively.

Small sequences of complete instances may be represented as a JSON array, but
large collections are normally expected to use a binary payload with a
separate schema-aware descriptor.

## Floating-point special spellings

The document model does not acquire native numeric NaN or infinity values.
Instead, schema conversion recognises predefined strings when the expected
destination type is floating point.  Relaxed document parsing may accept such
spellings unquoted, but they still reach schema processing as document strings;
quoted and unquoted source spellings are therefore equivalent at that boundary.
Schema conversion produces actual non-finite values in the resolved floating
point storage, subject to the existing `fp16data_t` conversion behaviour noted
above for `f16`.  This does not add native non-finite numbers to live or baked
documents.

Recognition is ASCII case-insensitive and otherwise exact. Accept `nan`, `inf`,
`+inf`, `-inf`, `infinity`, `+infinity`, and `-infinity`. Canonical output uses
`nan`, `inf`, and `-inf`. No signed or payload-bearing NaN syntax is defined.
Matching does not trim whitespace or use locale-sensitive case conversion.

Strict JSON output writes these values as strings.  Relaxed diagnostic or
authoring output may write the selected spellings unquoted.  Textual `NaN`
constructs a canonical destination NaN; exact NaN payload preservation belongs
to raw binary handling or an explicit future raw-bit form.

## Layering and partial overrides

"Specialisation" and "override" denote the same concept throughout this
specification; they are not separate operations or kinds of declaration.

Structure definitions can provide explicit default member values.  A member
without an explicit default uses zero, or the first declared enum value when
its type is an enumeration.  "First" means declaration order, not the smallest
numeric value; an enum's default therefore need not be numerically zero.
Boolean zero corresponds to false.  Explicit defaults must be valid values
of the member's declared type.

Schema resolution validates default values before publishing a usable schema.
An explicit default for an enum member must be a label declared in that member's
matching enum definition; numeric values are not accepted as enum defaults,
even when a label has that numeric value. Any declared alias in that enum is a
valid label. An invalid default is a schema-resolution error and leaves the
resolved schema empty. The implicit first-declared-enum default is unchanged.
Use labels from the matching enum for ordinary document-form instance values too.
Numeric literals do not implicitly acquire enum identity.

Defaults apply only within the specific structure definition that declares
them.  They do not propagate from an enclosing structure into nested
structures.  Each nested structure uses its own declared defaults, or the
implicit zero/first-declared-enum defaults for members without one.  This is
separate from an instance or override explicitly supplying nested member values.
An enclosing member whose type is a structure cannot supply an aggregate
`default`; reject that property rather than treating it as a local override of
the nested type. Arrays of structures likewise use the element structure's own
defaults. This applies to nested bit structures as well: field defaults belong
to their own field declarations, not an enclosing aggregate default.
Primitive/enum members and arrays of those types may supply explicit defaults;
short array defaults complete the tail with element defaults. A bit field may
have an explicit scalar default in its named field descriptor, validated against
its logical type, mask width, and interpretation.

Base construction starts from these schema defaults and applies the values
supplied by the instance. An omitted member uses its default. A specialisation
instead starts from an independent copy of the values established by its
enclosing base: omitted members or positional elements inherit, and defaults
are not reapplied at each step. This applies recursively to nested structures
and fixed arrays. The effective

values follow the chain from the structure definition through the base
instance and each subsequent specialisation; creating an alternative does
not modify any earlier instance in that chain.

An omitted `declaration` means no local selections in both authoring and runtime
forms. A base uses defaults; a specialisation inherits its complete immediate parent.
Runtime selection leaves are `true` markers and binary holds their actual values.
Named compounds, positional array holes and explicit empty containers preserve
selection intent. Omission never means unknown or discarded intent.
An empty object or array selects no leaves. A partial compound selects only its
supplied leaves, so unselected siblings continue to inherit. Specialising a
complete root selects all supplied meaningful leaves, including equal values;
complete overwrite instead retains old selections and adds only differences.

Default values apply to logical members; clearing gaps is a separate optional
operation. Defaults cannot generally be implemented merely by
zeroing storage because enum or explicit defaults may be non-zero. Explicit
defaults use `default` in the named member descriptor, as in the accepted sample.

Containment selects the base instance for specialisation. Each
specialisation has exactly one base, which may itself be a specialisation,
forming a chain. The base's declared type determines the permitted members;
this does not require an override to redeclare that type. Earlier
specialisations do not restrict which of those members later steps may change.
Missing bases, invalid members, and circular specialisation are hard errors.

Instance/bulk data load errors invalidate that logical document and leave its schema
valid, regardless of whether the views share a physical baked block.

Specialisation and overrides change values only.  They never change the
target's underlying types or modify structure definitions. Document values are
validated against the target member's declared type using the literal and codec
rules in this specification. Raw capture uses the supplied binary representation;
it does not convert arbitrary C++ types.

An override lives in its base instance's `specialisation` container, with
supplied authoring values in its own `declaration` container; the corresponding
runtime declaration holds selection masks. The enclosing base
is evaluated first. A shared name in another subtree neither selects a base
nor creates an override relationship. Parent-based nesting cannot form an
inheritance cycle in a document tree.

Every named specialisation is independently selectable as an instance,
including intermediate steps in a chain.  Selection uses its inherited values
and its own overrides, while the referenced type determines valid members.
Each specialisation owns a complete independent binary snapshot. Immediate-parent
inheritance supplies unselected values and never skips back to an older matching
ancestor. Equal explicit selections and explicit empty containers persist until
an explicit removal; conversion does not minimise them.

Complete overwrite retains existing selections and adds meaningful differences
against the current immediate baseline. Component specialisation selects only the
specified paths and pins supplied values even when equal. Both operations, and
selection removal, explicitly choose preserve or propagate for descendants.

Preserve leaves descendant bytes unchanged and adds necessary direct-child
compensation selections within the update scope. Deeper descendants need no new
selections because their parent bytes remain unchanged. Propagate rebuilds scoped
unselected descendants parent before child, retaining all their local selections.
Removal makes the target scope inherit again and applies the chosen policy below it.
Whole-instance edits include all meaningful fields; component edits restrict scope.
Padding-only differences never add selections.

Direct binary editing uses an explicit scoped edit token for a complete instance.
While pending, typed access, other mutations, rebinding and conversion reject.
Successful completion applies complete-overwrite selection/cascade rules. Failure
retains edited target bytes and pending state for retry, while preserving old masks
and descendant bytes. There is no automatic destructor completion, persistent
backup, implicit cancellation or unrestricted public mutable instance accessor.
Bulk data remains directly writable because it has no inheritance cascade.

Base construction and specialisation are separate operations.

- Complete construction establishes every member from supplied values or the
  explicit/implicit schema defaults.  Omission alone does not make an instance
  incomplete.
- A specialisation creates an independent alternative from an already complete,
  validated base instance. Its declarations never modify that base.
- Named objects select members; positional arrays select non-null supplied positions in semantic
  declaration order. Omitted members/elements retain their inherited values.
  The same rules apply recursively within nested structures and fixed arrays.
- Schema-defined named components may be selected by name. Named bitfields
  support selected-field overrides through explicit masks. Writing a selected
  field in the alternative preserves every bit outside its mask, including
  unused bits.

For example, an alternative differing in only one coordinate declares:

```json
{ "position": { "y": 0.2 } }
```

This declaration selects only the alternative's `y` member. A positional
specialisation `[2.0, 1.0, 4.0]` of base `[2.0, 1.0, 4.0, 7.0]` retains
the inherited fourth value, producing `[2.0, 1.0, 4.0, 7.0]`. The supplied
positions remain explicit selections even when equal to their base values.
Selected component overrides use the names supplied by the schema. Positional
null placeholders inherit without selecting; trailing inherited holes may be omitted.

Semantic coordinate names such as `x`, `y`, and `z` should be represented by a
named `Position` structure rather than only as `array<f32, 3>`.  That structure
may still be constructed positionally, while retaining named specialisation paths.

## Bit ranges and packed formats

The schema describes deterministic bit selection independently of C++ bitfield
layout.  A bit structure has a fixed storage unit and named members described
by hexadecimal masks and an interpretation.  Each mask selects one non-empty
contiguous bit range within the storage unit.  Masks must not overlap, but
gaps between fields and unused bits elsewhere in the storage unit are allowed.
Offsets and widths can be derived from masks during resolution; they are not
the authored selection form.

The logical field type determines signedness independently of containing storage.
Interpretations may include signed or unsigned integers, enums, normalised
values, and raw bits.  Unused bits contribute to the common gap indication;
the optional unused-storage pass preserves them by default and clears them only
when its unused-bit option is enabled. Byte padding is cleared in either mode.

Packed texel formats use this general bit-range machinery. Thus an
RGB10A2 format is a named packed `u32` representation with four bit-range
members, while RGBA8 can be a structure of four `u8` members. These formats are
named definitions, not additional primitive types.

The sample uses `mask` and `type` on each field, with a separate containing-word
`storage` and optional `interpretation`. Its companion notes explain that separation.

## Encodings and remapping

### Normalised field codecs

Normalised fields use scaling and deterministic nearest-integer rounding,
with halfway cases rounded away from zero.

For an `n`-bit `unorm` field, integer input/defaults are raw codes in
`[0, 2^n - 1]`. Floating input/defaults encode as
`round(clamp(f, 0, 1) * (2^n - 1))`. Decoding is `code / (2^n - 1)`.
UNORM supports mask widths from one through 64 bits.

For an `n`-bit `snorm` field, integer input/defaults are raw signed codes in
`[-2^(n-1), 2^(n-1) - 1]`. Floating input/defaults encode as
`round(clamp(f, -1, 1) * (2^(n-1) - 1))`. Decoding is
`max(code / (2^(n-1) - 1), -1)`. SNORM supports mask widths from two through 64
bits. Preserve the most-negative raw code: for eight bits, both -128 and -127
decode to -1.0, while floating -1.0 encodes to -127.

NaN becomes zero before clamping; infinities clamp to the corresponding endpoint.
Both raw and encoded codes must fit the logical primitive as well as the mask
width; reject out-of-range codes. Endpoint and rounding arithmetic must avoid
out-of-range casts and signed overflow, including at 64 bits where the positive
integer maximum is not exactly representable in binary64.

Resolved defaults and document output use unshifted integer codes, unsigned for
UNORM and signed for SNORM. Thus integer `1` means raw code one, while `1.0`
means normalised maximum; integer `-1` is a raw SNORM code, while `-1.0` means
the negative normalised endpoint. Implicit zero retains the logical signedness.
Integer raw-code and floating normalised inputs are distinguished by document
value kind. Floating special-string handling supplies non-finite document input; the data
model itself retains finite numeric payloads.

The scaling follows the GPU normalised representations described by
[Vulkan fixed-point conversions](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#fundamentals-fixedfpconv).
Our explicit tie rule follows
[Direct3D normalised conversions](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm);
it defines this encoder's deterministic result without promising bit-identical
results from every hardware conversion permitted by those APIs.

### Instance and bulk data operation boundaries

Runtime data roles always have complete binary backing. Live instance creation
encodes literal selections against defaults or an immediate parent, then retains
only masks. Complete overwrite and component specialisation use the explicit
policies above. Bulk capture creates/replaces complete arrays, preserving entry
identity while allowing byte relocation. No duplicate literal value store or global
reconciliation exists.

Live bulk `create_unpopulated` allocates a named record array for direct writes or
remapping, applying no defaults. The caller must populate logical fields before
reading or importing. Zero-count placeholders remain live-only; positive counts
of zero-size records are valid. Clearing unused storage does not populate fields.

Public operations validate/stage before publication. Failed append publishes no
entry and preserves existing records. Failed scoped completion keeps the edited
target pending, as specified above. Sources, type owners and unrelated roles survive
rejected loading/conversion. Borrowed views require stable backing and fresh lookup
after growth. Raw data is little-endian and is not an automatic cross-layout or
cross-endian conversion.

### Representations and bulk copies

Runtime promotion/baking copies complete bytes and retains selections. Live output
packs active entries and creates zeroed alignment gaps; raw block assembly preserves
the supplied complete payload ranges. Explicit value import reconstructs only selected
instance values and all bulk fields into a binary-free section, without runtime
locators or linked types. It validates encoded fidelity and never changes the source.

The parser's named singleton normalisation remains contextual: `[{b:9}]` selects
only member `b` in a compound instance element, even if represented as a named
array child. Named primitive array elements remain invalid. A one-member scalar
structure/bit structure also accepts scalar shorthand; arrays and multi-member or
empty compounds are not recursively unwrapped. Complete bulk records still require
every field, even in singleton form.

Unknown enum codes and noncanonical boolean bytes may survive runtime operations
but fail literal import. Floating NaNs compare equal, while signed zero remains
distinct; padding and unused bits do not participate. Import may choose a canonical
alias or literal spelling. Bit-field component writes preserve bits outside their
scope; the optional unused-storage operation can clear unused bits separately.

### Automatic direct-member remapping

The runtime remapper covers fat to
thin and thin to fat vertex or structured records only where matched members
have identical physical representation.  A validated runtime plan resolves to
a small sequence of strided copies.  Each copy identifies source and
destination byte offsets, a fixed contiguous compatible byte-range size,
source and destination strides; execution derives the element count from its
bounded views. A range may cover one
member or several consecutive physically compatible members in either record;
remap setup coalesces adjacent copies where doing so is safe.  It performs no
numeric conversion, enum translation, packing, unpacking, or general
per-record interpretation.

Each bulk mapping is based on two resolved structure types describing source
and destination array records. Setup automatically matches direct members by
name and type; there is no authored member-pair list or recursive member search.
It need not use every source member or fill every destination member. Members
without a matching name or exact type remain unmapped. This is expected, not a
semantic failure: partial plans and plans with no matches are valid. The same
rule applies to every type category; enums need no separate mismatch policy.
Matching enums can be copied unchanged; no enum translation is performed.
Primitives must have the same primitive type and size. Matched named structures,
enums, and bit structures require the same
type name and matching resolved definition; representation equality alone is
insufficient. Compare names by text across documents, not document-local IDs.
Array matches require matching counts, element strides, independent alignments,
complete sizes and compatible element types.
A compound already excluded by a difference in type category, name, size or
layout is skipped whole, without examining its contents to classify the mismatch
or find partial transfers. A compatible compound direct member is copied whole.
When equality of otherwise possible matches requires comparing definitions,
stop at the first difference; this is an equality check, not nested selection.
Definition matching includes member/label order, names, referenced types,
layout, defaults, masks, and interpretations as applicable. Remapping ignores
decorations, unlike strict definition comparison; numeric formatting metadata
also does not change matching. Enum aliases must agree in declaration
order. Different schemas may participate if this comparison succeeds.

The enclosing source and destination record types need not have the same name:
compatibility applies to the automatically matched members. Thus different vertex record
types can transfer matching primitive or named-member types. Enum-to-raw-integer
conversion is not supported.

Coalescing requires contiguous corresponding source and destination ranges and
must not overwrite unselected destination members. Multiple sources contribute
through multiple mappings, but a combined plan rejects overlapping destination
writes during setup, even when they would write identical bytes. Separate calls
are independently validated operations, not an implicitly ordered combined plan.

Setup selects prewritten contiguous, contiguous-source, contiguous-destination
or strided copy paths and stores their offsets, sizes and strides. Execution
uses those paths without traversing schema definitions or generating code.
Non-bulk paths use fixed-size copies where possible and a runtime-size fallback;
this makes no platform-specific vectorisation guarantee.

A mapping leaves unselected destination members untouched.  Destination
initialisation is separate from the partial transfer; a caller may initialise
storage, apply defaults, or populate it through other mappings as appropriate.
An unpopulated array created by `CLiveBulkData` is one such destination; remapping
only a subset of its fields or records leaves the rest untouched for the caller
to populate as required. The unused-storage pass does not clear unselected fields.
A whole selected aggregate copies its full extent, including owned padding.
Selecting individual members grants no permission to bridge intervening padding
or unselected fields when coalescing. Destination initialisation remains separate;
unused storage may be cleared explicitly after the transfer.
Individual packed fields require bit operations and are excluded from
this byte-copy remapper; a compatible complete bit-storage value can be copied.
Source/destination memory overlap for active copies is unsupported and must
be rejected before writes; it must not accidentally acquire `memmove` semantics.

Execution receives bounded current source/destination views and derives their
complete record capacities from the stored sizes and strides. A single `execute`
operation processes the minimum capacity; callers narrow views to restrict the
transfer. Every source and destination view for a nonzero-sized type must have
a byte size exactly divisible by its stored type stride. A remainder rejects
the entire operation before any writes, including when another view is empty
or the plan has no matching members. Remap roots are structures; fixed arrays
are matched members whose complete extents are already part of their type.
There are no supplied record counts, copied-count output or execution
diagnostic: execution returns success/failure after checking memory validity
before any writes. Zero-byte types impose no storage limit, and an entirely
zero-byte transfer is a successful no-op without a logical count. Setup retains
diagnostics for actionable construction failures. Instance/bulk data callers extract
entry views and use the same executor without separate handle-based overloads.

The primary destination is application-owned storage described by the destination
resolved layout. Live bulk mutable views and scoped instance edit views are additional
destinations. Execution changes only the supplied bytes; instance completion then
publishes selections and applies the explicit preserve/propagate policy. While an
instance edit is pending, ordinary typed reads and further mutation reject.

Value import into CSchema is a separate operation. Bulk data requires no reconciliation
after a direct write. Neither runtime conversion nor the binary executor silently
infers a cascade policy or removes equal selections.

Authored renaming, nested selection, numeric conversion, bitfield transforms and
enum translation are outside this delivery, not implicit requirements for a
general remapping engine.

Logical JSON is not necessarily a lossless representation of every physical
bit pattern.  Raw binary remains the lossless representation where reserved
bits, padding, or floating-point payloads matter.

## Scope boundaries

The current remapper performs exact-type binary copies. It does not rename or
recursively select members, convert numeric values, translate enums or transform
individual bitfields. Schema roles retain separate working document and payload
owners. The versioned [common `.bsd` container](common-binary-block.md) combines
the document with required complete binaries for each present data domain. It does not
establish persistent schema identity. Resolved runtime records are never serialised.

Unions, pointers, variable-sized fields, opaque handles/blobs, packed structure
layout and generated C output are unsupported.
Source ingestion, typed C++ serialisation helpers, shader representations and
editor construction are future work. Their intended uses and open design
questions are preserved in the [follow-on notes](../backlog/schema_follow_on.md).
