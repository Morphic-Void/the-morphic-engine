# Schema design

Current working design for the schema system.  This document records decisions
made before implementation; it is not yet a public API or file-format
specification.

## Purpose and scope

The schema system describes logical data types and their physical
representations.  It must support ordinary structures, packed representations,
rendering-resource layouts, serialisation, and directional construction of one
representation from values in another.

Schema definitions and instance data are distinct.  A definition declares the
meaning, layout, size, alignment, and members of a type.  An instance supplies
values of a declared type.  Bulk instance data may be represented as binary or
CSV, with JSON used for definitions, metadata, and ordinary small instances.

The canonical input to schema operations is a resolved **schema
configuration** (also described as the schema catalogue).  It contains types
only: no ordinary instance data.  Once validated and resolved, it is the basis
for instance validation, physical codecs, CSV projection, remap validation and
execution, direct-copy remap setup, and structure-only code generation.

An illustrative source document is provided in
[`schema-example.json`](schema-example.json).  Its JSON shape is provisional;
the examples communicate the intended categories and relationships, not a
finished file-format contract.

The system is not intended to infer mappings automatically or become a general
C++ compiler.  Mappings may be authored manually, while their validation and
execution are automated.

## Definition documents and aggregation

A source definition document is expected initially to contain category objects
for `enumerations`, `structures`, and `bit_structures`.  These categories are
organisational only.  All declared type names occupy one global type-name
domain: a collision within or across categories is invalid.  Built-in primitive
spellings do not need to be redeclared.

One document may initially contain Morphic, DirectX, Vulkan, and other selected
definitions.  It may later be assembled from separate sub-schema documents.
Aggregation produces one resolved configuration under the same collision rule;
sub-schemas do not create separate namespaces merely by residing in different
files.

Documents that feed or act upon the configuration remain separate from it:

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

## Declaration and reference identifiers

The document model has separate interned domains for property names and string
values.  The schema loader preserves that distinction:

- a property-name identifier declares a schema entity or member name;
- a string-value identifier is a reference; and
- context determines what kind of declaration or reference is permitted.

During initial loading, the loader can build a one-way, configuration-local
mapping from common string-value identifiers to identical property-name
identifiers.  This permits a reference such as the string `"Vertex"` to be
resolved to the declared-name identity without repeated textual comparison.
Textual equality alone is not semantic resolution; the surrounding schema
position determines whether the name may denote a type, enum label, member, or
other entity.  No reverse map is normally needed.

The loader also discovers fixed vocabulary such as `u8`, `f16`, section names,
and layout keywords once, resolving them to compact internal tokens.  These
document-local IDs are never durable cross-document identities.  After
aggregation, consumers use resolved type handles, member indices, offsets, and
internal tokens instead of reparsing strings.

## Primitive physical types

The initial primitive vocabulary is deliberately small:

| Category | Types |
| --- | --- |
| Signed integer | `i8`, `i16`, `i32`, `i64` |
| Unsigned integer | `u8`, `u16`, `u32`, `u64` |
| Floating point | `f16`, `f32`, `f64` |
| Boolean storage | `b8`, `b16`, `b32` |

Boolean types are sized physical storage with boolean semantics.  `b64` is not
initially needed.  `f16` is IEEE-754 binary16 physical storage and conversions
must reuse the existing `fp16data_t` conversion machinery rather than introduce
a schema-specific implementation.

An enumeration is a logical type with one of the integer types as its declared
physical storage type.  Its symbolic values are part of the enum definition;
the underlying storage supplies size, alignment, range, and binary encoding.

## Structures and layout

A structure has one canonical declared sequence of members.  This declaration
order is its semantic positional order.  A structure also has resolved size and
alignment, and every member has a resolved physical offset.

Offsets do *not* determine positional construction order.  They may be
non-monotonic in an explicit layout, may leave padding, and may overlap when
bit ranges are used.  Padding is not a member and never consumes a positional
initializer position.

The initial layout modes are expected to include natural, packed, and explicit
layout.  Each resolved definition must nevertheless provide fixed size,
alignment, offsets, and where relevant stride.  The member-list sequence is
sufficient to establish ordinal meaning; a separate ordinal property is not
needed unless a later source format cannot preserve declaration order.

## Fixed arrays

An array is a normal type constructor, usable as a member type or nested inside
another array.  A fixed array declares an element type and count.  Its default
physical stride, size, and alignment derive from its element type; an explicit
element stride can be added when an API layout needs inter-element padding.

Arrays can therefore contain primitive values, structures, or further fixed
arrays.  A structure may contain fixed arrays in the same way as any other
member type.

## JSON instance data

The expected type may be supplied by an enclosing asset or descriptor.  Where
it is not otherwise known, an instance wrapper can identify the type:

```json
{
  "type": "Position",
  "value": { "x": 0.0, "y": 0.2, "z": 0.7 }
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
not its offset order.  An array is consequently a compact complete
construction form.  An object identifies values by member name and is also the
form used for partial updates.

Small sequences of complete instances may be represented as a JSON array, but
large collections are normally expected to use a CSV or binary payload with a
separate schema-aware descriptor.

## Floating-point special spellings

The document model does not acquire native numeric NaN or infinity values.
Instead, schema conversion recognises predefined strings when the expected
destination type is floating point.  Relaxed document parsing may accept such
spellings unquoted, but they still reach schema processing as document strings;
quoted and unquoted source spellings are therefore equivalent at that boundary.

Recognition is ASCII case-insensitive and otherwise exact.  The accepted set
is to be specified explicitly, expected initially to include `nan`, `inf`,
`+inf`, and `-inf`; whether longer `infinity` spellings are accepted remains an
open choice.  Matching does not trim whitespace or use locale-sensitive case
conversion.

Strict JSON output writes these values as strings.  Relaxed diagnostic or
authoring output may write the selected spellings unquoted.  Textual `NaN`
constructs a canonical destination NaN; exact NaN payload preservation belongs
to raw binary handling or an explicit future raw-bit form.

## Layering and partial overrides

Construction and overlay are separate operations.

- Complete construction supplies every required member, either explicitly or
  through a declared default.
- An overlay applies to an already complete, validated instance.
- Overlays use named objects.  Omitted members retain their base values;
  supplied members replace them.  Nested named objects can patch nested
  structures.

For example, an overlay changing only one coordinate is:

```json
{ "position": { "y": 0.2 } }
```

This is not a partially specified complete instance.  Positional arrays do not
provide partial-update semantics, avoiding placeholder or sentinel rules.

Semantic coordinate names such as `x`, `y`, and `z` should be represented by a
named `Position` structure rather than only as `array<f32, 3>`.  That structure
may still be constructed positionally, while retaining named overlay paths.

## Bit ranges and packed formats

The schema must describe deterministic bit ranges independently of C++ bitfield
layout.  A packed representation has a fixed storage unit and members with bit
offset, bit width, and interpretation.  Interpretations may include raw signed
or unsigned integers, enums, normalised values, and raw bits.  The definition
also needs a policy for unused or unspecified bits: for example, force zero,
write a constant, or preserve existing destination bits.

Packed texel formats should initially be constructed from this general
bit-range machinery rather than introduced as primitive schema types.  Thus an
RGB10A2 format is a named packed `u32` representation with four bit-range
members, while RGBA8 can be a structure of four `u8` members.  A later
convenience catalogue may name standard API formats without making them
fundamental types.

## Encodings and remapping

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
numeric conversion, enum
translation, packing, unpacking, or general per-record interpretation.

At setup, the remap bakes a configuration that selects pre-written fast kernels
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

Destination padding and fields not supplied by a segment need an explicit
construction policy, such as zero initialisation, declared compatible defaults,
or provision by another direct-copy source.

This admits interleaved records and simple structure-of-arrays rearrangement
through explicitly named streams, while retaining a minimal, predictable
runtime cost.  More complex remaps remain supported and may be run at runtime
through the general conversion path, but are expected primarily to condition
data offline.  Operations such as `pack_unorm8x4`, bitfield transforms, and
enum translation are not initially candidates for specialised bulk runtime
compilation or generated conversion code, even where an eventual optimised
path would be desirable.  Their higher cost is an accepted escape hatch rather
than a reason to exclude them from the schema model.

Logical JSON is not necessarily a lossless representation of every physical
bit pattern.  Raw binary remains the lossless representation where reserved
bits, padding, or floating-point payloads matter.

## Open design questions

- Exact JSON grammar for definitions, instances, and descriptors.
- The complete accepted set of floating-point special strings.
- Defaults, nullability, and the distinction between an omitted value and an
  explicitly supplied null where a type permits null.
- The initial layout rules and whether explicit array stride is selected in the
  first implementation slice.
- Binary descriptor association, identity/versioning, hashing, and packaging.
- CSV member-path and fixed-array flattening conventions.
- Source declaration ingestion, including the explicit ABI assumptions needed
  to convert C++ bitfields into fixed physical layouts.
