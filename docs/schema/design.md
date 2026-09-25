# Schema design

Current working design for the schema system.  This document records decisions
made before implementation; it is not yet a public API or file-format
specification.

This is the primary record of schema semantics. The
[implementation contract](implementation-contract.md) defines runtime observations,
delivery boundaries, and acceptance checks; the header survey explores later use
cases. The reviewed sample illustrates the design without expanding stage scope.
The earlier audit and question agenda have been consolidated into these documents;
their historical findings remain in Git history.

## Purpose and scope

The schema system describes logical data types and their physical
representations.  Its intended scope includes ordinary structures, bit-packed
representations, rendering-resource layouts, serialisation, and directional
construction of one representation from values in another. Packed structure
layout is deferred; bit structures are included in the first stage.

Schema definitions and instance data are distinct.  A definition declares the
meaning, layout, size, alignment, and members of a type.  An instance supplies
values of a declared type.  Bulk instance data may be represented as binary or
CSV, with JSON used for definitions, metadata, and ordinary small instances.

The canonical input to schema operations is a resolved **schema
configuration** (also described as the schema catalogue).  It contains type
definitions and their default metadata, not ordinary instance data.  Once validated and resolved, it is the basis
for instance validation, physical codecs, CSV projection, remap validation and
execution, direct-copy remap setup, and structure-only code generation.

[`schema-example.json`](schema-example.json) expands the previously accepted
grammar with structure `detail`, explicit offsets, and short-input examples.
It uses `element`/`count` without redundant array `kind`, and `detail.internal`
for the internal-layout marker. Its [companion notes](schema-example-notes.md)
explain expected layouts and the initial-delivery boundary.
Neither document implies an implemented resolver.

The system is not intended to infer mappings automatically or become a general
C++ compiler.  Mappings may be authored manually, while their validation and
execution are automated.

Schema implementation code belongs in the new `core/schema/` directory.

## Authoring, resolution, and data

Schema creation and structural modification use a live document.  Schema
definitions may be distributed as Morphic JSON text or, more commonly, as
baked documents.  Resolution operates only on a baked schema document:

```text
Live schema document -> bake -> baked schema document -> resolved schema
Instance input + resolved schema -> resolved data
```

The **resolved schema** is constructed at runtime and is never serialised.
It contains explicit types, descriptors, and resolved references for fast
operations.  Both it and its backing baked schema document remain immutable
for its lifetime.  The schema retains a reference to the baked document, whose
storage may be owned by the host and must outlive the resolved schema.  Baked
content mutation is available for non-schema uses only; schema edits require
a new bake and resolution.

### Schema-document normalisation

An existing schema document can be normalised by creating a new normalised
document. The source document remains unchanged, including when it is baked
or backs an existing resolved schema. This is a separate document-producing
operation; resolving a baked document does not silently rewrite it or require
the caller to request a normalised document first.

Normalisation uses a successfully resolved schema and its backing document to
construct the new document. Validation, type-reference resolution, and layout
calculation belong to schema resolution; normalisation reuses those results
rather than implementing a second validator or layout calculator. A convenience
entry point accepting a document should use the same resolution path before
producing normalised output, rather than a standalone normalisation engine.

The output uses the agreed canonical document representation, including
structure `detail.alignment` and `detail.size` taken from the resolved
descriptions. It preserves declaration order and meaning, including positional
member order and enum-alias preference. Invalid supplied metadata fails
resolution and cannot be silently repaired by normalisation. The new document
can subsequently be baked and resolved through the ordinary lifecycle.

The output contains schema definitions and their document metadata, not a
serialisation of resolved slots, indices, or other runtime internals. Exact
normalisation APIs remain to be specified. A request to create a new normalised
document is outside the initial delivery. Any schema-document writer provided
at any stage must nevertheless emit the normal form; deferring this operation
does not permit non-normal output from schema generation.

### Runtime representation

Declaration names remain identifiers into the backing document's name table.
Descriptive vocabulary and type-reference strings become explicit runtime
types and descriptors rather than text reinterpreted during each operation.
The intended representation uses variable-length sequences of slots, with
member counts determined by the described structures.  Fixed record types
and common access operations will be defined without exposing the entire
storage arrangement as a fixed public structure. The implementation contract
defines the required observations and index relationships; exact private
record packing is chosen and reviewed during implementation.

### Resolved-schema ownership and access

One class owns the resolved slot storage and contains a `CBakedDocument` view
instance by value.  Internal schema relationships use indices rather than
stored pointers or references, allowing the resolved storage to be relocated
without repairing links.  The pointer held inside the baked-document view is
the exception: it borrows the backing bytes, not a host-owned view object.
The host must keep those bytes alive and immutable until schema use has ended;
the lifetime and address of the original view object are irrelevant.  The
schema releases its own slot storage and clears its contained view without
releasing the host's document bytes.  No shared ownership layer is required.

The resolved-schema class supports move construction and move assignment;
copy construction and copy assignment are disabled.  Any future copying
operation must be an explicit function.  Moving transfers the slot ownership
and contained baked-document view, leaving the source empty and unusable.  Internal
indices retain their meaning in the transferred resolution; they do not
depend on the old schema object's address.  Move assignment invalidates the
destination's previous resolution.

Every new resolution attempt immediately invalidates the existing resolution,
even if the new input fails validation.  Failure leaves an empty, unusable
resolved schema with no partial resolved records or usable document binding.
The previous resolution is not restored.  Only complete success publishes a
usable resolution.  Reset or destruction likewise ends use of the current
resolution.  Indices retained by callers belong to that resolution and must
not be reused with a replacement, even if numeric slot indices are repeated.

A failed instance evaluation also invalidates and clears the resolved schema
if that instance and the definitions share the same physical baked document.
Failure in a separate instance document does not invalidate the resolved
schema. This is an additional invalidation trigger for combined documents;
it does not require the first-stage resolver to evaluate instance data.

Schema access indices are unsigned, with zero reserved for failure or an
unmapped reference. Valid resolved-slot indices are non-zero. The implementation
contract selects a 32-bit wrapper without a generation/identity mechanism.
Resolved indices remain distinct from
document node/name IDs.

Serialisation and remapping callers mainly need to locate the appropriate
resolved type description and pass it to a generic operation along with the
instance buffer.  That operation traverses resolved indices internally.
Temporary read-only references obtained for an operation do not introduce
stored pointer/reference links between schema records.  The public interface
need not expose all slot-storage details merely to invoke these operations.

The editor is likely to navigate primarily through the baked schema document,
then consult the corresponding resolved descriptions for types, sizes, offsets,
and access to instance data.  Its writes affect the instance, not the immutable
schema.  A secondary mapping table from baked-document slots to resolved-schema
slots provides the correspondence.  It is constructed with the resolution,
owned by the resolved schema, and follows the same move, invalidation, and
failure rules as its other resolved storage.  Mapping keys identify document
occurrences, not merely interned name strings. A lookup without a mapped
resolved counterpart returns zero.

The baseline is that named schema elements can be mapped. Named components
in the same-type named-component form are included in that baseline. Ordinary
array elements do not need individual mappings; only the reference to the
array description needs a mapping. The array's resolved description supplies
its element type, count, and stride without per-element mapping entries.

Mapping coverage is secondary to resolved-schema efficiency. Do not introduce
extra resolved records or a less efficient resolved layout solely to map every
named document element. Where efficient representation leaves an element
unmapped, lookup returns zero. The baseline is therefore a coverage aim, not
a requirement for a one-to-one document-to-resolved-slot representation.

Establish the efficient resolved representation first, then derive and document
the user-facing rules for access availability from that representation. Those
rules should explain which elements have mappings and when lookup returns zero;
they must not constrain the layout in advance merely to provide broader coverage.

A complete duplicate navigation interface over the resolved slots is not
assumed.  Editor-specific helpers may remain separate from the minimal runtime
access interface.

### Resolved data and generated declarations

**Resolved data** is instance data converted into the physical representation
described by the resolved schema.  It is distinct from the schema's runtime
machinery and will frequently be serialised.  Data formats include entirely
binary blobs as the principal bulk-data path, CSV for manual review, and, in
limited cases, document-form instances distributed as baked documents or JSON
text.  Baking a document alone does not make it resolved schema or resolved
data.

The system will also generate simple POD C/C++ data-structure declarations.
Generation does not produce per-structure operational code: no serialisers,
remappers, member functions, or generated access routines.  Serialisation and
remapping use generic operations over `const void*` source pointers and `void*`
destination pointers as appropriate, with references to the relevant resolved
schema descriptions.  Typed template wrappers may be added later, but are not
required for the first pass.

Generated C++ enums use `enum class` with an explicit underlying type matching
the schema's declared integer storage.  Boolean declarations use a `using`
alias for `std::int8_t`.  `f16` uses the existing `fp16data_t` type.  Bit-structure
descriptions emit one named `constexpr` declaration for each field, consisting
of the containing storage-word type and mask value; no additional field
descriptor object is generated. The logical field type separately determines
value interpretation and signedness; a mask constant is not an enum field value.
These constants describe the fields; instance storage remains the declared
base storage type.  No operational code is implied by these declaration forms.

JSON instance handling is expected before
binary serialisation and deserialisation; implementation order does not make
JSON the principal long-term bulk-data format.  Source-code ingestion remains
a late stage.  Development is iterative, with the definition document revised
as functionality is added.

The first implementation stage resolves and inspects schemas and generates
C++ structures as part of validation.  Instance construction and serialisation
are outside that stage.  Resolution covers all described type categories:
integers, floating point, booleans, enums, general/nested structures, fixed
arrays, and bit structures. Declaration validation compiles C++17 output and checks
size, alignment, member offsets, standard layout, and trivial copyability, as
specified in the implementation contract. Compiler-based checks belong to
validation tooling, separate from generated structures.

The initial delivery excludes a separate operation for creating a normalised
document. Explicit member offsets and increased structure alignment should be
included initially only if adding them later would require retroactive changes
beyond the local scope of those features. The implementation-contract assessment
defers them: resolved types and members already contain size, alignment, offsets,
and gaps, so later support changes layout calculation and export locally. The
first delivery rejects these unsupported layout requests explicitly.

## Editor use

The resolved schema configuration is also the technical type system for editor
data entry.  It supplies the durable facts needed to inspect and edit an
instance: primitive type, enum labels, fixed-array extent, nested structure
shape, defaults and validation constraints.  Packed data is normally presented
through its logical members rather than solely as a raw storage word; raw
storage visibility remains useful for diagnosis.

The private development editor begins as a functional, schema-driven technical
inspector, not an attempt to reproduce the scope of Unreal, Unity or Godot.
Its default presentation is deliberately utilitarian:

- structures are expandable member groups;
- fixed arrays are indexed lists;
- enums are labelled selections;
- primitives are direct typed inputs;
- bit structures expose their logical fields; and
- validation failures identify the affected member path.

Richer controls, visual editing, custom layouts, or workflow-specific helpers
are added only where a real repeated workflow demonstrates a material
development-speed or error-reduction benefit.

Presentation and access policy remain separate from physical representation.
Schema-adjacent editor manifests may target resolved type and member paths to
provide labels, help text, grouping, ordering, specialised controls, or
visibility/read-only policy.  The private editor may expose technical fields,
layouts, packed members, formats and diagnostics.  A user-facing editor can
instead present curated higher-level choices while constructing and validating
the same schema instances.  Hiding a field in a presentation manifest is not
itself an authority or data-security boundary.

The schema inspection interface should be considered as a future reflection
source for constructing editor panes.  Editor-specific representation or
helpers may be separate if including them would substantially enlarge the
first implementation.  Editor pane construction is not part of that stage.

## Definition documents and aggregation

### Logical documents and physical packaging

The input comprises one logical structure-definitions document and zero or
more logical instance documents.  There is only one definitions document for
the evaluation context, whether standalone or combined with instance
definitions.  The definitions document can be resolved on its own; an instance
document cannot be evaluated without the applicable definitions document
being present in the evaluation context.

Instance definitions may occupy one or several documents.  Each main instance
branch effectively constitutes a logical document, with its root acting as
that branch's namespace.  These logical documents may share one baked document
or be stored in separate baked documents.  The physical baked-document count
therefore does not determine the number of logical documents or namespaces.
The earlier single-baked-document assumption does not constrain instance
packaging.

Reference ordering differs between the two logical document kinds.  Structure
definitions may refer to types declared later in the definitions document.
An instance's specialisations inherit from that enclosing instance, which is
evaluated first. There are no explicit forward instance-reference strings.
Combining definitions and instances in one baked document does not introduce
a different inheritance mechanism.

The resolved schema still describes the one definitions document and retains
the view of its backing baked storage.  A combined package must identify the
definitions portion separately from instance branches.  Multiple instance
inputs do not create multiple structure-definition catalogues.  Document-local
name, string, and node indices from separate baked documents must not be
compared as shared identities; explicit type references resolve against the
one applicable definitions document, and instance inheritance follows containment.

The accepted sample illustrates the combined sections; separate documents
retain their applicable sections. The layout requires no cross-instance-document base
lookup or path-root convention: references are implicit in containment or
explicit references to named types. The evaluator receives the resolved schema
and selected type explicitly for a separate instance document; document-local
identifiers remain document-local.

### Definition contents

The definitions section contains structure, enum, and bit-structure definitions.
The reviewed section name is `types`, containing category objects
`enumerations`, `structures`, and `bit_structures`, as in the accepted sample.
These categories are organisational only: names referenced as types must be
unique across the definitions document, not merely within a category.
Instance names may recur in different subtrees, provided each instance can be
identified unambiguously by its path.  A branch root acts as a namespace for
instance and override names.  A branch may contain multiple instances and
overrides, but their names must not collide within that branch.  This scoped
instance naming does not relax document-wide structure type-name uniqueness.
Built-in primitive spellings do not need to be redeclared.

### Reviewed document layout

The reviewed package separates three concerns:

- `types`: structure, enum, and bit-structure definitions.
- `instances`: entries grouped by type, with named base instances beneath each
  type and specialisations nested as children of their base. Further nesting
  can express a chain of specialisations.
- `data`: entries grouped by type, with each named entry holding an array of
  instances. These collections have no specialisation or override mechanism.

Within the instance tree, two container names and their relationship are now
settled: `declaration` contains the instance's supplied member values, and
`specialisation` contains named child specialisations. Each child has the same
container arrangement, allowing further specialisation.

Nesting establishes the base relationship: a child inherits from the enclosing
instance whose `specialisation` container holds it. Its `declaration` supplies
only its overrides; omitted members retain inherited values. A top-level base
instance instead applies its `declaration` to the type's defaults. No explicit
base name or path is required in this form. This supersedes the earlier rule
requiring every override to supply a target reference. Coincident names in
other branches still establish no relationship.

An ancestor base fits the existing prohibition on forward targets and targets
below an override. The parent is evaluated before its specialisations,
regardless of the textual ordering of the two containers. The surrounding
package and type-descriptor forms are illustrated by the accepted sample;
later extensions are recorded separately.

The bulk collection name identifies the array, not its individual
records; it does not introduce references between those records.
Bulk records require every member explicitly, recursively through nested records
and bit structures. Positional structures and fixed arrays must have their full
declared lengths here; short-input/default completion belongs to `instances`,
not `data`. Gaps are not members and still have no implicit fill policy.
CSV/binary payload association is a later encoding contract. This does not extend
the first implementation stage to
instance construction or bulk-data processing.

### Definition validation and assembly

Initially no mandatory document headers (such as `format` or `version`),
document versioning, or permitted-name whitelist are required.
Declaration names that are not valid C++ identifiers are hard errors, and
unknown descriptor/document properties are hard errors. Vocabulary properties
such as `default` are schema syntax, not C++ declaration names. This does not restrict
permitted numeric spellings.  Type descriptors still require enough
information to define their contents, such as an array's element type and
count. The reviewed sample establishes the descriptor forms; additional
validation details and extensions are recorded separately.

Duplicate declarations within the applicable naming domain are hard errors,
including duplicate structure type names and duplicate members within a type.
They are not merged or interpreted as overrides.  Repeated instance names in
different unambiguously identified subtrees are permitted.  Forward references to types
declared later in the baked document are allowed.  Circular type references
are rejected.  This is separate from the manually ordered code-ingestion list.

Schema text input must reject the document parser's `name_collision_extension`
finding before baking; the parser's default policy already excludes it.
Resolution still checks duplicate named declarations in the baked document,
including repeated named entries in a member array. It cannot reconstruct
source duplicate properties that have already been merged into an ordinary
array: the baked format does not preserve that source history. This distinction
does not authorise merging schema declarations.

The one definitions document may contain Morphic, DirectX, Vulkan, and other
selected definitions.  Any authoring-time assembly produces that single
definitions document before resolution; it does not create additional runtime
definitions documents or type namespaces merely because inputs came from
different files.

Documents that feed or act upon the configuration are logically distinct from
its type definitions, even when packaged in the same baked document:

- ingestion manifests select and profile source declarations which may create
  proposed definitions or validate existing ones;
- code-generation manifests select resolved configuration types and output
  options; and
- instance, descriptor, CSV, and remap documents consume the resolved
  configuration without redefining its types.

Source ingestion has two non-interchangeable modes.  **Import** creates a
proposed definition from an allow-listed source declaration.  **Validation**
compares an existing canonical definition with such a declaration under a
specified target ABI/profile.  Validation is non-mutating: changed headers
produce diagnostics rather than silently rewriting the configuration.  A
manifest may also validate an explicit projection, recording source members
deliberately excluded from a durable configuration type.

### Development-time structure ingestion

Source-code ingestion is a late implementation stage, after the schema model
and its supported features are established.  It is a development task whose
performance needs to be workable on a fast development machine; it does not
need runtime-path optimisation or a full C/C++ parser.

Ingestion is expected to be configured infrequently, initially by the project's
developer.  Keeping the implementation small limits code and maintenance cost
and allows the operating instructions to remain short: identify each file and
structure, list prerequisites first, run ingestion, and address diagnostics.
Manual configuration is an intentional tradeoff.  Additional automation should
be justified by a demonstrated recurring need rather than convenience alone.

A JSON ingestion manifest contains an ordered list of requests, each naming
the exact source file containing a definition and the structure to extract.
The author orders requests so prerequisite member structures are ingested
before structures which depend on them.  Dependencies may also refer to types
already available in the schema.  The first implementation does not discover
or reorder dependencies automatically; unavailable prerequisites produce a
descriptive diagnostic.

Ingestion can recognise a structure whose members are all non-array base
members of the same type as a named-component form, retaining the declared
component names, order, and layout.  For example, an ordinary structure with
`f32` members `x`, `y`, and `z` supplies named components as well as positional
construction.  This recognition is based on the declared member types, not
on naming conventions alone. Named bitfields support analogous
selected-field overrides through their explicit bitfield definitions, preserving
unselected fields and unused bits in an existing value.

The initial pass reads the explicitly selected files without following
`#include` directives or searching other files for definitions.  Include
handling should be considered only if practical use demonstrates a need.
Extraction handles a deliberately limited declaration subset and reports
unsupported or ambiguous constructs rather than requiring a general parser
or silently guessing their meaning.  A preprocessed header view is not a
prerequisite for this initial direct-file workflow.  Physical layout claims
still require known layout rules and, where source ABI details matter, an
explicit target profile.

The manifest's exact field names and the initial supported declaration subset
remain to be specified.

## Declaration and reference identifiers

The document model has separate interned domains for property names and string
values.  The schema loader preserves that distinction:

- a property-name identifier declares a schema entity or member name;
- a string-value identifier can supply reference text; and
- context determines what kind of declaration or reference is permitted.

During initial loading, the loader can build a one-way, configuration-local
mapping from common string-value identifiers to identical property-name
identifiers.  This permits a reference such as the string `"Vertex"` to be
resolved to the declared-name identity without repeated textual comparison.
Textual equality alone is not semantic resolution; the surrounding schema
position determines whether the name may denote a type, enum label, member, or
other entity.  No reverse map is normally needed.

Interned name identity alone does not identify a declaration occurrence:
different structures may declare the same member spelling, and a name may
also occur in an enum or other context. A reference or label string resolves
within its known type context: named types use the one definitions catalogue
and enum labels use the matching enum. The name-domain map does not perform
this semantic selection. After target selection, a mapped baked occurrence can
be connected to its runtime description. This auxiliary table does not guarantee
a resolved slot for every occurrence or replace the resolver's own type
relationships and lookup operations.

The current reference model is:

- an explicit type reference identifies its named definition in the one
  definitions document; and
- an instance specialisation derives its base from containment and overrides
  selected values, potentially through a chain of specialisations. It does not
  need a reference string to establish this relationship.

The second use builds on the named-overlay rules below.  Instance names may
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

The loader also discovers fixed vocabulary such as `u8`, `f16`, section names,
and layout keywords once, resolving them to compact internal tokens.  These
document-local IDs are never durable cross-document identities.  After
aggregation, consumers use resolved type handles, member indices, offsets, and
internal tokens instead of reparsing strings.

### Reference scope under the nested layout

The nested layout supersedes the earlier instance-reference path scheme.
There is no current requirement for relative or absolute instance-reference
strings, enclosing-namespace search, or cross-document instance-reference
visibility. The previous `../`, `/`, and array-traversal rules are therefore
not part of the current schema reference contract. Type lookup uses the one
definitions document; specialisation inheritance uses the enclosing instance.

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
prefix.

The provisional output preference is also hexadecimal notation for unsigned
values whose schema type is wider than `std::uint16_t`, initially `u32` and
`u64`.  This refers to the declared schema type, not the smallest width of the
particular value stored by the document model.  Numeric notation does not
itself select a schema type or change its physical width.  These conventions
govern construction and normalisation, not input spelling restrictions.
Input accepts valid JSON and supported Morphic extensions under the document
parser's acceptance policy without requiring canonical schema notation.
For example, a decimal mask is not rejected merely for lacking a `0x` prefix,
and a non-negative integer need not carry an explicit `+` on input to a signed
schema field if its value is representable in that field.  Schema processing
validates the value against its expected type before normalising its metadata.
Canonical document metadata is emitted when constructing a new document;
resolution does not rewrite the immutable baked input in place.

Permissive spelling does not permit invalid schema definitions or instance
values.  Malformed syntax, invalid layouts, and values incompatible with their
expected types are errors.  Validation should produce descriptive diagnostics
identifying the affected type/member path and the reason for rejection.
Initially validation stops at the first error and outputs its cause, along
with the affected member and structure where appropriate and available.  The
implementation contract specifies an allocation-free structured diagnostic with
document locations and related ranges where available. Schema-resolution failure
already has the empty-result contract specified under ownership and access.
Instance failure also invalidates the schema when instances and definitions
share the same baked document; failure in a separate instance document does not.

## Primitive physical types

### Literal validation

The following conversion rules make default validation concrete. They are
implementation choices derived from the existing document numeric contract,
the agreed permissive input policy, and the explicit `fp16data_t` exception.
The same rules apply when later instance construction consumes document values;
typed override compatibility is a separate check.

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
domain 0/1. `unorm` is recognised as an unsigned-integer field interpretation;
its explicit default is a finite value in [0, 1]. Resolution validates that
domain and retains the interpretation; quantisation is a later codec operation.
Absent interpretation means the declared type's ordinary value semantics.
Other interpretation spellings require an explicit extension, not silent fallback.

### Physical vocabulary

The initial primitive vocabulary is deliberately small:

| Category | Types |
| --- | --- |
| Signed integer | `i8`, `i16`, `i32`, `i64` |
| Unsigned integer | `u8`, `u16`, `u32`, `u64` |
| Floating point | `f16`, `f32`, `f64` |
| Boolean storage | `b8` initially |

Boolean storage is initially assumed to be 8 bits.  Wider boolean storage
types from earlier drafts are not an initial implementation requirement.
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
Empty structures are not supported.

The working declaration form is an array of named members, each expressed as
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
An explicit default belongs in the named member's description object, alongside
its type.  Future explicit offsets also belong in that same object.  The first
pass still calculates offsets; recording their future location does not add
explicit layout to its scope.
The replacement `schema-example.json` uses this ordered named-member text form.
Under the document model's singleton normalisation, these wrappers become
named children of the `members` array, retaining the descriptor payload.
Resolution reads that baked representation; it must not require a redundant
anonymous wrapper object to survive parsing. See the
[document interpretation rules](../data_model/document_text_format.md#collision-extension-and-singleton-objects).

Offsets do *not* determine positional construction order.  Natural layout may
leave padding; padding is not a member and never consumes a positional
initializer position.  If explicit layout is introduced later, non-monotonic
offsets must not change declaration order.

Overlapping member storage ranges are rejected.  Bit-structure members may
share a storage word, but their actual bit ranges must not overlap.  An overlap
diagnostic should identify both conflicting members and their byte or bit
ranges. Union support is outside current scope; if introduced, it will be an explicit
schema construct with defined rules permitting overlap among its alternatives,
not an implicit interpretation of otherwise invalid overlapping members.

Natural layout is the default and initial implementation scope. Explicit offsets
and increased alignment are deferred under the assessment above; their eventual
semantics are defined here. Packed layout will not
be used unless selected ingested structures demonstrate a need; the planned
review of candidate Vulkan structures may inform that decision. The original
sample's explicit layouts were superseded; the revised sample now illustrates
both natural layout and the subsequently agreed explicit-offset rules.

The first pass calculates member offsets and array strides from the natural
layout rules. Explicit offsets and increased alignment can be added without
changing the resolved observations; independent array-stride
overrides remain deferred. When explicit offsets are supported, every member of that
definition must supply an offset; explicit and inferred member offsets are
not mixed within one definition.

Natural alignment follows the current compilation target. Simple members
align to multiples of their size; compounds use their members' effective
alignment recursively. Without explicit increases, this is the size of the
largest atomic member. Thus a nested 12-byte structure containing three `f32`
members naturally aligns to 4 bytes, not 12. An explicit increase on a nested
type propagates to its containing layout. Structure size is rounded to the
effective structure alignment. Each resolved definition must
provide fixed size, alignment, offsets, and where relevant
stride.  The member-list sequence is sufficient to establish ordinal meaning;
a separate ordinal property is not needed unless a later source format cannot
preserve declaration order.

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

An optional `detail` object beside `members` contains structure-level metadata,
replacing the earlier name `config`. Authored
`alignment` may increase the structure's alignment requirement above
its natural requirement, not reduce it. A containing layout must respect the
effective alignment of a nested type, including explicit increases; the
natural largest-atomic-member rule alone is insufficient in that case.

If `detail` is absent, or is present without `alignment`, use the agreed
natural alignment rules. Omission does not select packed layout or byte
alignment. Explicit alignment requirements of nested member types still apply.

Generated schema documents include both `detail.alignment` and `detail.size`
for structures, recording their effective alignment and computed total size.
This includes documents created from source code and newly normalised schema
documents. The input need not supply either value: that applies both to
source-code ingestion/schema creation and to resolving a baked schema document.
Source-code ingestion derives missing layout facts under its supported target
layout rules; baked-document resolution derives them from the type definitions.
Omitted alignment uses the rules above; omitted size is computed from the layout.
A supplied size is checked against that computed result and a mismatch is a
validation error, not a request for additional padding. These are generated
schema-document fields, distinct from emitting C++ structure declarations;
the resolved schema itself is still never serialised.

Accepting and validating generated `detail` metadata for natural layouts is
part of the base schema contract. Deferring the separate normalisation operation
does not defer that input form or computing its values. Support for alignment
increases beyond the natural requirement follows the explicit-layout delivery
criterion; it must not be confused with an explicit value equal to natural alignment.

Every successfully resolved type description contains its effective alignment
and size regardless of whether the input supplied them. Optional document
metadata does not mean optional runtime metadata. A normalised document makes
those derived structure details explicit without modifying the input document.

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

Structure `size` is generated metadata, computed from the members and effective
alignment, not an independent authored extent or stride override:

```text
size = round_up(max(member.offset + member.type.size), structure.alignment)
```

Member ends use each member type's full resolved size, including its own
padding; this applies recursively to nested structures and arrays. The example
has a member extent of 12 bytes and a computed size of 16 bytes. Bytes 12-15
are padding owned by the structure, including when it is embedded. Consecutive
elements in an ordinary array start 16 bytes apart, and a containing structure
cannot place another member inside that padding. The gap indication includes
this extra tail padding.

The resolved descriptor exposes `size` as a precomputed extent for operations
such as copying or clearing the whole structure. This does not prescribe a
zeroing policy. Array element stride is derived from that size; `stride` is
not a separate structure property. The earlier configurable 32-byte-stride
interpretation is superseded. A supplied alignment sets a layout requirement;
the supplied size records the computed result and is validated against it.
Which explicit alignment values the target supports remains a
validation-contract detail.

The `detail` section can identify internal structures whose explicit offsets
cannot be fully represented by ordinary generated source declarations. Such
structures must still be exportable for review. This supersedes the earlier
expectation that explicit-offset structures would simply be excluded from
declaration output.

Internal status concerns source export, not schema validity or runtime use.
Review output should retain the declared members and expose their resolved
offsets, alignment, and size; it must not imply that an approximate C++
declaration reproduces that layout. Generated source can include explicit
padding members between fields, and tail padding members where needed, to account for
the declared offsets and total size. These are generated storage members, not
logical schema members: they do not add names or positional elements to the
schema's instance model, nor do they impose a zeroing policy. Padding does not
by itself establish layout fidelity; any claim of faithful source export must
check resulting member offsets, alignment, and size for the selected target.

The internal marker is `detail.internal: true`; omission is equivalent to false.
It identifies export intent, not permission to bypass layout validation.
Natural-layout declaration generation remains
unchanged, and padding declarations do not add generated operational code.

This section records the full layout rules. The expanded sample includes both
natural and explicit layout examples. Explicit offsets and increased alignment
are later additions under the implementation-contract assessment.
Normalised-document creation is also a later operation.

### Gaps and external initialisation policy

The resolved description records whether any gaps exist, without separate
categories for unused bitfield bits and alignment padding.  The indication
covers internal and tail padding and gaps contained in nested member types
or array elements, so an enclosing structure exposes whether any of its
storage contains gaps.  Exact flag placement in the resolved records remains
an implementation choice.

Zeroing unused storage is an external policy.  The schema exposes gap presence
so a caller can decide whether to zero a structure or buffer; it does not
prescribe zero, constant-fill, or other unused-storage values.  Non-zero gaps
are not themselves invalid instance values.  Gaps are distinct from named
members left unselected by a partial remap.

## Fixed arrays

An array is a normal type constructor, usable as a member type or nested inside
another array.  A fixed array declares an element type and count.  Its default
physical stride, size, and alignment derive from its element type.  The first
pass calculates stride; an explicit element stride may be considered later
when an API layout needs inter-element padding.

Arrays can therefore contain primitive values, structures, or further fixed
arrays.  A structure may contain fixed arrays in the same way as any other
member type.

The array descriptor is `{ "element": "f32", "count": 2 }`, removing the
previously illustrated `kind: "array"`. `count`
identifies the array and both properties are required; `element` accepts a
named type or another array descriptor. Count 1 remains an array. The old `kind`
property is not an alternative spelling; it is rejected as an unknown property.

Zero-length arrays are rejected. There is no current exception for a trailing
array. This restriction concerns the declared array count, not the length of
a short initializer completed with defaults. If source ingestion ever revisits
this rule, the contemplated case is
only a trailing zero-length array which is discarded from the extracted
schema, not retained as a schema array type.

### Named-component forms

A general array supports replacement in full.  An array of schema-defined
named components supports either replacement in full or overrides of selected
components by name.  The schema establishes the component names and their
order; names are not inferred from arbitrary array contents.  A structure of
same-type, non-array base members can supply this form, as in a vector with
named `x`, `y`, and `z` components.  Recognising that form does not change its
declared physical layout.

For example, a complete positional vector value can be replaced by a new
array, with any omitted tail completed from defaults, or a named override can
change only `y`, retaining the other components. This does not introduce
positional patch operations, placeholder values, or a general indexed-array
patch syntax. The accepted sample recognises
the form from same-type, non-array primitive members without an extra marker,
and uses a named `declaration` object for selected-component overrides.

## JSON instance data

Nulls are not allowed in schema-defined values, including defaults and instance
input. An explicit `null` is an error, not an omitted value or an instruction to
use a default. This schema rule does not change the general document parser's
ability to represent nulls. Named omissions retain their established default
or inheritance semantics.

The type comes from the enclosing type group in `instances` or `data`, or from
an explicitly supplied resolved type description in a later construction API.
The reviewed named-instance form is:

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

A structure accepts either named-object or positional-array construction:

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
whose initialisation is an external policy.

An array is therefore a compact complete construction form, including default
completion. An object identifies values by member name and is also the form
used for partial updates.

Small sequences of complete instances may be represented as a JSON array, but
large collections are normally expected to use a CSV or binary payload with a
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
design; they are not separate operations or kinds of declaration.

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
Numeric literals do not implicitly acquire enum identity. Typed enum values may
supply that same enum or their matching raw underlying integer type; distinct
enum types are not interchangeable merely because their storage or values agree.

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

Construction starts from these schema defaults and applies the values supplied
by the instance. An omitted member uses its default. A named override instead
starts from the values already established by its enclosing base: omitted
members are unchanged, and defaults are not reapplied at each step. If a supplied
member value is a positional replacement, its missing tail uses defaults under
the short-input rule; omission from the named override is a different operation. The effective
values follow the chain from the structure definition through the base
instance and each subsequent override.

Default values apply to logical members.  They do not impose zeroing of gaps,
which remains external policy, and cannot generally be implemented merely by
zeroing storage because enum or explicit defaults may be non-zero. Explicit
defaults use `default` in the named member descriptor, as in the accepted sample.

Containment selects the base instance for specialisation. Each
specialisation has exactly one base, which may itself be a specialisation,
forming a chain. The base's declared type determines the permitted members;
this does not require an override to redeclare that type. Earlier
specialisations do not restrict which of those members later steps may change.
Missing bases, invalid members, and circular specialisation are hard errors.

These instance errors invalidate the resolved schema only when the instance
and definitions are in the same baked document. A failed evaluation of a
separate instance document leaves the resolved schema valid. When invalidation
does apply, it clears the resolved schema and its document binding under the
same empty-result contract as a failed schema-resolution attempt.

Specialisation and overrides change values only.  They never change the
target's underlying types or modify structure definitions.  Compatibility is
directional from the supplied value's type to the target member's declared
type and requires effectively the same underlying type; equal byte size alone
does not establish compatibility.

An enum is a stronger type than its underlying integer type.  A stronger
typed value can supply a weaker destination with that same underlying type,
but a weaker typed value cannot supply the stronger destination.  This
relationship governs acceptance of the supplied value, not type promotion:
the destination retains its weaker declared type after the override.  A later
override is still checked against that original destination type, not the type
of the value supplied by an earlier override.

For example:

- An enum with underlying type `u8` may supply an override for a `u8` member.
  The target remains a `u8` member.
- A raw `u8` may not override a member declared as an enum backed by `u8`.
  Matching storage does not remove the target enum's type restrictions.

The same principle applies along the entire specialisation chain.  These
rules concern typed override compatibility; they do not by themselves specify
document-literal conversion rules or the compatibility predicate for bulk
remapping.

An override lives in its base instance's `specialisation` container, with
changed member values in its own `declaration` container. The enclosing base
is evaluated first. A shared name in another subtree neither selects a base
nor creates an override relationship. Parent-based nesting cannot form an
inheritance cycle in a document tree.

Every named specialisation is independently selectable as an instance,
including intermediate steps in a chain.  Selection uses its inherited values
and its own overrides, while the referenced type determines valid members.
Constructed data is an independent value, with no live dependency tracking.
The authoring document retains the inheritance tree; after an edit, a caller
may explicitly reconstruct the affected instance. Resolved schema metadata
does not become mutable instance provenance.

Construction and overlay are separate operations.

- Complete construction establishes every member from supplied values or the
  explicit/implicit schema defaults.  Omission alone does not make an instance
  incomplete.
- An overlay applies to an already complete, validated instance.
- Overlays use named objects.  Omitted members retain their base values;
  supplied members replace them.  Nested named objects can patch nested
  structures.
- General arrays can be replaced in full.  Arrays of schema-defined named
  components can also have selected components overridden by name.  Named
  bitfields support selected-field overrides through explicit masks. Updating
  a selected field preserves every bit outside its mask, including unused bits.

For example, an overlay changing only one coordinate is:

```json
{ "position": { "y": 0.2 } }
```

This named overlay changes only the selected member. Positional arrays instead
construct or replace a complete value, with a short input completed from
defaults; they do not retain previous trailing values as a partial update would.
Selected component overrides use the names supplied by the schema. Null
placeholders remain invalid.

Semantic coordinate names such as `x`, `y`, and `z` should be represented by a
named `Position` structure rather than only as `array<f32, 3>`.  That structure
may still be constructed positionally, while retaining named overlay paths.

## Bit ranges and packed formats

The schema describes deterministic bit selection independently of C++ bitfield
layout.  A bit structure has a fixed storage unit and named members described
by hexadecimal masks and an interpretation.  Each mask selects one non-empty
contiguous bit range within the storage unit.  Masks must not overlap, but
gaps between fields and unused bits elsewhere in the storage unit are allowed.
Offsets and widths can be derived from masks during resolution; they are not
the authored selection form.

The declared base type determines signedness.  Ingested C/C++ bitfields are
converted into this mask-based description with their base-type signedness
retained; interpreting their source allocation still requires known ABI rules.
Interpretations may include signed or unsigned integers, enums, normalised
values, and raw bits.  Unused bits contribute to the common gap indication;
zeroing them remains external policy, as for alignment padding.

Packed texel formats should initially be constructed from this general
bit-range machinery rather than introduced as primitive schema types.  Thus an
RGB10A2 format is a named packed `u32` representation with four bit-range
members, while RGBA8 can be a structure of four `u8` members.  A later
convenience catalogue may name standard API formats without making them
fundamental types.

The reviewed sample uses `mask` and `type` on each field, with a separate
containing-word `storage` and optional `interpretation`. Its companion notes
explain that separation. The original `bit_offset`, `bit_width`, and
`unused_bits` fields are removed.

## Encodings and remapping

### Instance operation boundary

The initial instance-construction API will consume baked document values plus
an explicit resolved-schema/type context. Text and live documents can compose
through the existing parse/bake path; a direct text-to-instance parser or owning
instance allocator is not required. The caller supplies destination memory with
an explicit byte extent and alignment adequate for the resolved type.

Validate the schema/type, extent, and alignment before writing. Later value
conversion failure may leave partial destination output; the caller must discard
it. There is no rollback requirement for construction or overrides. This is
independent of schema invalidation: the same-document rule still applies, and
failure in a separate instance document leaves the schema usable. No implicit
zeroing of padding or unused bits is performed. A named-bitfield update is a
read/modify/write under the selected mask and preserves every unselected bit.
The caller supplies initialised containing words wherever masked writes must
preserve existing bits; whether to clear those words first is an external policy.

The operation does not retain instance memory, instance-document storage, or
live inheritance dependencies. A caller keeps schema backing bytes alive while
using schema operations; constructed data has the lifetime of its own storage.

### Representations and bulk copies

JSON, CSV, and binary are encodings of the same typed instance model, not
separate data models.  CSV provides a predictable flattened projection of
arrays of records; binary provides the exact physical records, offsets, stride,
and alignment required by the schema.

Structural remaps are directional construction rules.  They may rename or
reorder members, select new offsets, translate enum values, apply defaults,
perform numeric conversions, traverse nested structures and arrays, combine or
split fields, draw from multiple source records, and write packed bit ranges.
The existence of a forward map does not imply a reverse map.

The initial runtime remap scope is intentionally narrower.  It covers fat to
thin and thin to fat vertex or structured records only where selected members
have identical physical representation.  A validated runtime plan resolves to
a small sequence of strided copies.  Each copy identifies source and
destination byte offsets, a fixed contiguous compatible byte-range size,
source and destination strides, and an element count.  A range may cover one
member or several consecutive physically compatible members in either record;
remap setup coalesces adjacent copies where doing so is safe.  It performs no
numeric conversion, enum translation, packing, unpacking, or general
per-record interpretation.

Each bulk mapping is based on two resolved structure types describing source
and destination array records, plus a separate description of selected member
pairs.  It need not use every source member or fill every destination member.
Type or size conflicts in any selected pair reject the whole mapping during
setup, before data transfer. Primitives must have the same primitive type and
size. Selected named structures, enums, and bit structures require the same
type name and matching resolved definition; representation equality alone is
insufficient. Compare names by text across documents, not document-local IDs.
Array matches require matching counts, strides, and compatible element types.
Definition matching includes member/label order, names, referenced types,
layout, defaults, masks, and interpretations as applicable; numeric formatting
metadata does not change type identity. Enum aliases must agree in declaration
order. Different schemas may participate if this comparison succeeds.

The enclosing source and destination record types need not have the same name:
compatibility applies to the selected member pairs. Thus different vertex record
types can transfer matching primitive or named-member types. Enum-to-raw-integer
override compatibility does not grant a bulk-copy conversion.

Coalescing requires contiguous corresponding source and destination ranges and
must not overwrite unselected destination members. Multiple sources contribute
through multiple mappings, but a combined plan rejects overlapping destination
writes during setup, even when they would write identical bytes. Separate calls
are independently validated operations, not an implicitly ordered combined plan.

At setup, the remap constructs an execution plan that selects pre-written fast kernels
from this mechanical shape and supplies their offsets, range sizes, strides,
and counts.  It does not generate code for an individual schema.  The important
kernel cases are a source stride equal to the
range size (sequential source loads and strided destination writes), or a
destination stride equal to the range size (strided source reads and
sequential destination stores).  For example, a 16-byte `vec4` source with a
16-byte stride can be copied into 16-byte destination fields in records whose
stride is a multiple of 16.  The contiguous side can use bulk/vector-register
loads or stores where alignment and platform support allow; the other side may
still require individual transfers.  If neither side is contiguous for the
element, the operation falls back to ordinary individual strided copies.

A mapping leaves unselected destination members untouched.  Destination
initialisation is separate from the partial transfer; a caller may initialise
storage, apply defaults, or populate it through other mappings as appropriate.
A whole selected aggregate copies its full extent, including owned padding.
Selecting individual members grants no permission to bridge intervening padding
or unselected fields when coalescing. Gap initialisation remains the caller's
policy. Individual packed fields require bit operations and are excluded from
this byte-copy remapper; a compatible complete bit-storage value can be copied.
Source/destination memory overlap is unsupported by the first executor and must
be rejected before writes; it must not accidentally acquire `memmove` semantics.

This admits interleaved records and simple structure-of-arrays rearrangement
through explicitly named streams, while retaining a minimal, predictable
runtime cost. More complex remaps are a later design direction and could run
at runtime through a general conversion path, but are expected primarily to
condition data offline. Operations such as `pack_unorm8x4`, bitfield transforms, and
enum translation are not initially candidates for specialised bulk runtime
compilation or generated conversion code, even where an eventual optimised
path would be desirable.  Their higher cost is an accepted escape hatch rather
than a reason to exclude them from the schema model.

Logical JSON is not necessarily a lossless representation of every physical
bit pattern.  Raw binary remains the lossless representation where reserved
bits, padding, or floating-point payloads matter.

## Delivery and remaining format work

The [implementation contract](implementation-contract.md) records the initial
scope, runtime/access contract, limits, diagnostics, generator behaviour, and
acceptance checks. Exact private slot packing and the resulting occurrence-map
coverage are implementation-review deliverables, not unresolved authoring rules.

Later stages must define their external formats before implementation: binary
byte order and schema association; CSV paths, columns, and input policy; the
ingestion manifest and supported source declarations; and normalised-bitfield
codec rounding. These contracts are deliberately not invented by the first
resolver delivery. They are listed with their stage in the implementation contract.
No wire version, hash scheme, schema-owned instance allocator, or editor dependency
system is implied by the current design.

Unions, pointers, variable-sized fields, opaque handles/blobs, packed layout,
independent array strides, and generated C output are unsupported in the current
scope. Reconsider one when a concrete use case requires it; it is not a pending
question that an initial implementation must answer.
