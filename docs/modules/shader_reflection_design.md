Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   shader_reflection_design.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   25 Oct 2026

# Shader reflection and component libraries

## Status and boundaries

This records the reflection design agreed through 25 October 2026. Reflection,
combined conditioning reports and component-library assembly are not implemented.
Generic schema composition and version-2 combined persistence are implemented.
The existing [compilation path](shader_compilation_design.md) remains the
implemented baseline. The final section identifies decisions required before
implementation. The version 1 decoration vocabulary and canonical stage shapes
below are agreed, with the compact naming and interpretation distinctions recorded
here superseding the earlier `Float3x4`/`Matrix3x4f` conventions. Additional
producer/consumer convention metadata, full library authoring grammar and generated
identity encoding remain to specify. The initial authoring form and implementation
starting policies below are agreed; this is a design baseline, not a completed
public API specification or authority to start implementation.

Compilation and reflection are separate operations coordinated inside rendering.
The first composed operation compiles and reflects successful members. Subsequent
conditioning may validate interfaces, prepare distribution artifacts and assemble
packages without publishing intermediate results to the Host. The Host supplies
I/O and lifetime services and retains results only at an explicit publication
boundary. A broad Host asset-service redesign is deferred until this composed
workflow has been exercised.

The initial execution scope covers vertex, hull/tessellation-control,
domain/tessellation-evaluation, geometry, pixel/fragment and compute stages.
Reflection describes compiled requirements; it does not establish device support,
vertex-buffer compatibility, cross-stage compatibility or PSO validity.
DirectX output, stream output, CPU readback and the compilation policy's deferred
features are not added by this work.

SPIRV-Reflect is the intended private rendering dependency. Core contains no
DXC, SPIRV-Reflect or Basis Universal headers or implementation. Public contracts
use engine-owned storage and abstractions. Initial reflection consumes artifacts
from the engine compilation path; independently loaded binaries would require
an explicit validation boundary. SPIRV-Reflect is not a SPIR-V validator.

## Existing schema foundation

Use the schema system as the authoritative representation of reflected physical
types and declaration relationships:

- [Array stride/alignment](../schema/specification.md#fixed-arrays) describes fixed
  arrays with independent element stride and whole-array alignment.
- [Decorations](../schema/specification.md#declaration-decorations) retain local string maps.
  Core preserves them without interpreting rendering vocabulary.
- [Associations and interfaces](../schema/specification.md#associations-and-interfaces) provide
  checked non-storage references and ordered declaration groups.

Association targets and interface members resolve within one complete schema.
Immediate targets retain their identities; chains are not aliases. Forward and
shared references are allowed, while association/interface dependency cycles
are rejected. Physical construction cannot consume non-storage declarations.
No additional generic schema feature is required merely to describe matrices,
resource interfaces or runtime buffer-element sequences.

### Authoring aggregates and deferred resolution

The implemented [document model](../schema/README.md) separates binary-free
authoring from runtime descriptions and payloads. `CSchema` owns an editable
aggregate containing any combination of `types`, `instances` and `data` sections.
Its values are literals; runtime locators, lengths and payloads do not belong in
that tree. `CTypesDocumentQuery`/`CTypesHandle` describe types, whereas
`CSchemaDocumentQuery`/`CSchemaHandle` describe the aggregate. Types wrappers and
resolution are documented in the [runtime API](../schema/runtime-api.md), including
`types_wrappers.hpp` and `resolved_types.hpp`.

`compose_from`, `merge_from` and generic authoring save permit structurally
admitted fragments with unresolved references, missing semantic fields or
incomplete values. Inputs no longer have to resolve independently. A target may
be supplied by another fragment before final validation; this is composition
into one document, not persistent cross-document referencing. Full `validate`,
live `materialise` and runtime `bake` require contained types, even for an otherwise
empty aggregate, and validate all contained data against them with no external
types fallback.

Named definitions remain indivisible. Merging cannot extend an existing
structure/interface's member list, complete its missing intrinsic definition
fields from another partial definition, or infer equivalence between differently
named targets. Compatible duplicates can combine permitted defaults, decoration
keys and presentation metadata. Conflicts retain original-source diagnostics.
If compatibility of omitted versus explicit layout metadata needs an unresolved
dependency, the proof fails rather than guessing. Association targets retain
immediate identities and interfaces retain ordered members/counts. See
[composition and fidelity](../schema/persistence-and-aggregation.md).

Reflection may therefore prepare fragments and shared declarations before final
resolution. Explicit shared-type consolidation still establishes compatible
identities and rewrites references deliberately. Successful standalone reflection
results must have all dependencies available for validation; admission of an
incomplete fragment is not successful reflection or a publishable library.

### Runtime roles, import and editing

Live and baked runtime data roles retain complete binary values with stripped
descriptions. Instance descriptions retain hierarchy and explicit selections;
bulk descriptions retain entries and locators. Runtime promotion preserves bytes
and selection masks. `CSchema::import_from` instead replaces the aggregate with
only the selected role's section and never implicitly imports its linked types.
Import types separately and then compose/merge when an authoring aggregate is
needed. Data import reconstructs selected instance literals or full bulk records;
it can reject encodings without faithful literals and does not preserve padding,
unused bits or original literal spelling as a general guarantee.

SPIR-V can enter live bulk backing directly, including a bounded mutable view of
an unpopulated array, without one literal node per word. Prepare types while
unbound: type editing is prohibited while any role is bound to the owner. Later
type changes require an independent replacement and explicit checked rebinding.
Complete-instance direct edits use explicit `begin_edit`/`complete_edit` rather
than destructor reconciliation. Instance changes have explicit preserve/propagate
policies; bulk writes have no inheritance cascade. Keep owners stable and alive
for all borrowers. These facilities do not supply concurrent mutation semantics.

### Semantic compatibility and physical representation

Reflection must supply enough information to construct or validate CPU-formatted
buffer data and to support later semantic binding checks. These are related but
distinct consumer concerns:

| Concern | Authoritative information |
| --- | --- |
| Stage-to-stage linkage | Compiled shapes, locations/components or built-ins, qualifiers and stage rules |
| Vertex-content binding | Attribute meaning, explicit semantic mappings, shader input shape and permitted vertex-format interpretation |
| Uniform/structured-buffer data | Concrete scalar encodings, offsets, sizes, alignment, array/matrix strides and interpretation of those bytes |

Equal physical layouts do not prove semantic compatibility. Equal semantic shapes
do not prove byte compatibility. Position and normal may both be `float3` while
requiring different bindings; a matrix can have one logical meaning and multiple
physical representations. Application meanings unavailable from reflection must
come from the supplied component description. Core validates schema structure,
layout and references; rendering validates shader semantics and binding rules.
Non-storage declarations cannot be used to construct physical buffer data.

## Library description, identity and naming

### Initial authoring form

Use one explicit entry per shader/variant in an ordered `shaders` list. Preserve
that order in member reports and the library catalogue. The agreed starting form
is:

```json
{
    "version": 1,
    "library": "surface",
    "shaders": [
        {
            "name": "surface_vertex",
            "source": "surface.hlsl",
            "entry_point": "VSMain",
            "stage": "vertex"
        },
        {
            "name": "surface_fragment",
            "variant": "textured",
            "source": "surface.hlsl",
            "entry_point": "PSMain",
            "stage": "fragment",
            "defines": {
                "USE_TEXTURE": "1"
            }
        }
    ],
    "shared_types": {}
}
```

An omitted variant is `default`; duplicate `(name, variant)` pairs reject.
Source and include paths refer to the retained source bundle. Reuse the existing
compilation options, defaults and define-precedence rules. Initially variants
are explicit: no permutation-expansion or inheritance language is introduced.
A future authoring tool may generate those entries. Unknown properties and
conflicting declarations reject with diagnostics. This description selects
library components; PSO descriptions remain future work.

The example settles the basic authoring shape. Full field constraints, optional
compilation-option spelling, catalogue records and shared-type selector syntax
remain contract details to finish before implementing their public surface.
The authoring `version` is distinct from the generic runtime block version.

### Identity and descriptive names

The library content description supplies a descriptive shader name and optional
variant name. An omitted variant selects `default`. A compiled component is
identified by library identity plus shader/variant; shader/variant pairs are
unique within a library. Multiple components may have the same shader stage.
Source path, entry point, stage and effective options are properties of the
component, not substitutes for its author-supplied identity.

Public names are exact, case-sensitive document strings, with no silent trimming.
A deterministic, collision-free, reversible encoding of qualified names produces
valid schema identifiers; identity must not depend solely on a hash.
The catalogue preserves the original names; consumers do not decode generated
identifiers to recover public identity. The exact encoding remains to specify.
A build fingerprint is distinct from identity: changing source or options need
not rename the component requested by a client.

Qualify private declarations by shader/variant. Preserve compiler names as
optional descriptive metadata, not durable identities. Do not automatically
share types because source names or physical layouts happen to match. Separate
physical representations of one logical type remain distinct when layouts or
required semantics differ.

Keep public component identity (library, shader, variant), qualified declaration
identity and compact descriptive type name separate. Private and explicitly
shared declarations occupy distinct scopes. A spelling such as `float3_` or
`float3x4` does not force distinct representations to share a declaration identity.

The library content description will eventually identify components for PSO
generation. Its catalogue links each shader/variant to its entry point, stage,
SPIR-V bulk entry and reflection root. Reflection does not infer pipeline
membership from compilation grouping.

## Explicit shared-type mappings

The library description names a shared physical identity and lists selections
from shader variants. Each selection identifies:

1. Shader and variant (`default` when omitted).
2. A resource by backend-qualified coordinates, initially Vulkan set/binding.
3. Its content role: fixed buffer contents or a repeated element type.
4. An optional typed structural path selecting nested members or array elements.

An illustrative authoring form is:

```json
"shared_types": {
    "Particle": {
        "sources": [
            {
                "shader": "particle_update",
                "resource": { "vulkan": { "set": 0, "binding": 2 } },
                "select": "buffer_element"
            },
            {
                "shader": "particle_vertex",
                "resource": { "vulkan": { "set": 1, "binding": 0 } },
                "select": "buffer_element"
            }
        ]
    }
}
```

A path uses steps such as `{ "member": "lighting" }` and
`{ "element": true }`. Element selection traverses a type, not a runtime array
index. Permit exact unique reflected member names and an explicit ordinal
alternative, optionally checked against an expected name. A failed name lookup
must not silently fall back to an ordinal. SPIR-V IDs, schema indices and
generated private names are not persistent authoring selectors.

Initially select named physical structures. Rendering validates complete layouts
and required semantics recursively, establishes corresponding dependencies,
assigns canonical shared identities and rewrites references as part of composition
and merging, then validates the complete aggregate. Merely renaming the outer
structure is insufficient.
Dependency sharing stays within the selected definition; it does not initiate
global structural deduplication.

All listed sources must resolve, including resources removed by compilation:
an absent selected resource is a diagnostic. A reflected declaration cannot
receive two different shared identities. Overlapping mappings must agree on
dependent identities. Conflicting layouts or semantics fail consolidation;
there is no implicit conversion, precedence or repacking. Keep source-specific
provenance in component reports/catalogue where needed so it does not conflict
on a shared declaration. Retain reports when consolidation fails.

## Reflected schema organisation and vocabulary

Each successful entry-point reflection has a validated type catalogue with a
root interface referring to its resources, inputs and outputs. Authoring fragments
may be incomplete during construction. Empty interfaces are valid. Store entry-point
execution properties on that root. Private declarations remain scoped to the
component even after library aggregation. Pipeline linking is a separate step.

### Version 1 vocabulary rules

Use `rendering.*` for renderer interpretation, `rendering.vulkan.*` and
`rendering.spirv.*` for backend facts, and `rendering.hlsl.*` for HLSL metadata.
Core preserves these strings without interpreting them.

- Keys and values are case-sensitive.
- Unsigned numbers use decimal without leading zeros, except zero itself.
- Booleans use `true` and `false`.
- Tokens use lowercase snake case, except exact SPIR-V enumeration names and
  explicitly listed tokens such as `1d`.
- Source names and HLSL semantics retain their exact original spelling.
- Decorations apply only to their owner; there is no inheritance, propagation
  or override through references or groups.
- Missing information is unavailable or inapplicable unless a key explicitly
  defines a default. Missing values must not silently become zero or false.
- Unsupported or unknown correctness-relevant metadata produces a reflection
  diagnostic rather than silently disappearing.

The tables below define exact version 1 spellings and attachment locations.
Schema references, member counts and physical layout fields remain authoritative;
decorations do not duplicate them. Sequence extent and stride below describe
non-storage associations which have no corresponding physical schema fields.

### Component and declaration roles

| Owner | Key | Values |
| --- | --- | --- |
| Component root | `rendering.schema.version` | `1` |
| Component root | `rendering.shader.stage` | `vertex`, `tessellation_control`, `tessellation_evaluation`, `geometry`, `fragment`, `compute` |
| Component root | `rendering.shader.entry_point` | Exact compiled entry-point name |
| Component root | `rendering.shader.target` | `vulkan_1_1`, `vulkan_1_2`, `vulkan_1_3` |
| Interface declaration | `rendering.interface.kind` | `component`, `resources`, `inputs`, `outputs`, `value_group`, `buffer_layout` |
| Value-group interface | `rendering.interface.domain` | `vertex`, `patch`, `fragment`, `target`, `system` |
| Shape declaration | `rendering.shape.kind` | `vector`, `matrix`, `array`, `structure` |
| Appropriate declaration/member | `rendering.source.name` | Original reflected name, when available |
| Interface occurrence | `rendering.hlsl.semantic` | Exact available HLSL semantic |

Every component root contains these three members in this order, each with count
one. Their target interfaces exist even when empty:

```text
resources -> resources interface
inputs    -> inputs interface
outputs   -> outputs interface
```

Library identity, shader name and variant belong in the catalogue; they need not
be repeated on every declaration. Shape interfaces have `rendering.shape.kind`;
they are distinct from interfaces classified by `rendering.interface.kind`.

The checked target supplies type identity; do not duplicate it in a decoration
string. Backend-specific coordinates stay qualified. Do not reconstruct DirectX
register information from Vulkan coordinates. Shader declarations remain
unchanged: reflection neither renumbers locations nor rewrites bindings.

## Resources, structures and buffer layouts

Physical buffer structures preserve member order, types, explicit offsets,
nested structures, array/matrix strides and the complete representable extent.
Import the compiled layout rather than recomputing it from HLSL names. One source
structure may require distinct physical definitions in different layout contexts.

An association describes the resource; an interface member describes a particular
resource occurrence and carries its binding. Uniform/fixed blocks target their
physical layout. Images and samplers may be explicitly targetless. Image sampled
scalar information is not a claim about CPU texel storage or a concrete image
format. Read-only and writable resources may share a physical content type.

| Owner | Key | Meaning / values |
| --- | --- | --- |
| Resource association | `rendering.resource.kind` | `uniform_buffer`, `storage_buffer`, `sampled_image`, `storage_image`, `sampler`, `uniform_texel_buffer`, `storage_texel_buffer` |
| Buffer resource association | `rendering.buffer.interpretation` | `typed`, `structured`, `raw` |
| Resource occurrence | `rendering.vulkan.set` | Descriptor set |
| Resource occurrence | `rendering.vulkan.binding` | Binding |
| Resource occurrence | `rendering.descriptor_array.rank` | Number of fixed descriptor-array dimensions |
| Resource occurrence | `rendering.descriptor_array.extent.N` | Positive dimension extent; `N` is a zero-based decimal index |
| Buffer-region occurrence | `rendering.buffer.offset` | Region byte offset |
| Runtime-sequence association | `rendering.sequence.kind` | `runtime` |
| Runtime-sequence association | `rendering.sequence.stride` | Element byte stride |

Other resource categories require an explicit support decision; do not map them
to an approximately similar category. Preserve access/memory qualifiers at their
actual attachment with `rendering.spirv.decoration.NonWritable`, `NonReadable`,
`Coherent`, `Volatile` and `Restrict` (each suffix uses the same full prefix).
An emitted flag has value `true`; absence means it was not emitted. Declared
restrictions are distinct from observed operations or runtime resource use.

Image associations use:

| Key | Values |
| --- | --- |
| `rendering.image.dimension` | `1d`, `2d`, `3d`, `cube`, `buffer` |
| `rendering.image.arrayed` | `true`, `false` |
| `rendering.image.multisampled` | `true`, `false` |
| `rendering.image.sampled_scalar` | `f32`, `i32`, `u32` |
| `rendering.spirv.image_format` | Exact SPIR-V `ImageFormat` name, including `Unknown` |
| `rendering.spirv.image_depth` | `non_depth`, `depth`, `unknown` |

An explicitly unknown image property is retained, not replaced with a guessed
format or depth interpretation. These keys do not describe CPU texel storage.

A structured buffer uses these conceptual relationships:

```text
Resources member (binding)
  -> storage-buffer resource association
    -> buffer-layout interface
      -> element-region occurrence (byte offset)
        -> runtime-sequence association (element stride)
          -> physical element structure
```

A buffer-layout interface can also describe a fixed prefix. Runtime extent is
explicitly unspecified until storage is supplied. The region declares one
sequence; interface-member count does not stand for its runtime element count.
Raw byte-address resources retain an explicitly raw interpretation.

Distinguish element schema size, sequence byte stride, runtime element count and
descriptor count. Existing bulk operations use physical type size as record
spacing; a sequence with another stride is not automatically a compatible bulk
collection. No variable-sized physical schema or new generic strided-data
operation is implied by reflection.

Fixed descriptor arrays use occurrence count for total descriptors. Emit rank
and contiguous indexed extents only for actual arrays, including arrays of one.
Check the dimension product against occurrence count with overflow detection.
Count does not increment binding coordinates. Runtime descriptor arrays remain
deferred.

Schema arrays own full stride slots and rounded tails. Validate representability
against those rules; do not truncate a slot or let another member occupy its
owned padding. SPIRV-Reflect's top-level storage-buffer size can be zero even
when its element type has a layout; inspect the member/element and stride data.
Do not interpret a reflection library's padded-size field as an independently
proven source-language ABI.

## Matrices and stage value shapes

### Canonical non-storage shapes

Composite stage values use non-storage interfaces, with primitive scalar leaves.
They describe values without inventing CPU offsets, alignment, vertex packing or
an inter-stage memory ABI. Physical buffer structures remain physical schema
types. Vertex formats, strides, offsets and conversions belong to future
component/PSO input descriptions and validation.

| Value | Canonical representation |
| --- | --- |
| Scalar | Reference to the corresponding schema primitive |
| Vector | Interface with `rendering.shape.kind=vector`; one member `element`, count equal to vector width |
| Matrix | Interface with `rendering.shape.kind=matrix`; one member `column`, count equal to column count, targeting a vector shape |
| Fixed array | Interface with `rendering.shape.kind=array`; one member `element`, count equal to array extent |
| Structure | Interface with `rendering.shape.kind=structure`; ordered members matching the reflected aggregate |

Multidimensional arrays nest one declaration per dimension. Arrays of one retain
their array identity. Primitive references identify scalar meaning in this
context; their physical sizes are not a claim about shader interface storage.
In particular, a logical Boolean is not evidence of one-byte buffer storage.

Use lowercase descriptive names. A matrix name such as `float3x4` describes
three rows and four columns in its recorded interpretation basis, not an array
of four vectors inferred from its spelling:

```text
float3                         [vector]
  element: f32, count=3

float3x4                       [matrix: three rows, four columns; f32]
  column: float3, count=4

ordinary array declaration     [shape.kind=array]
  element: float3, count=4
```

A distinct generic schema matrix category and a `Matrix` naming prefix are not
required. Retain matrix classification through `rendering.shape.kind=matrix` and
checked shape relationships. Matrix and ordinary-array classification remain
distinct even when their scalar counts agree. This distinction is descriptive;
stage linkage must apply backend rules to the actual interfaces rather than
automatically reject different source classifications. Half precision remains
outside the initial feature policy; compact terminology does not enable it.

These names sit within the qualified declaration identity scheme. Semantic shapes
and concrete storage representations must not acquire the same identity merely
because they have the same descriptive spelling. Names do not replace collision-
free encoding, and consumers inspect checked shape and decorations rather than
parse names to establish correctness.

Preserve aggregate structure exposed by reflection. Do not reconstruct an assumed
HLSL hierarchy after compiler flattening. Missing names receive deterministic
ordinal names without fabricated `rendering.source.name`. A later traversal can
flatten leaves for matching without storing a second authoritative representation.

### Compact physical-layout terminology

The notation describes faithfully reflected storage for CPU data construction
and validation. A single underscore following a vector means padded to four
scalar slots, not one unused element and not an array separator:

| Name | Meaning |
| --- | --- |
| `float2` | Two meaningful 32-bit float components, with no padding implied by the name |
| `float3` | Three meaningful 32-bit float components, with no padding implied by the name |
| `float2_` | Two meaningful floats in a four-float extent; two slots unused |
| `float3_` | Three meaningful floats in a four-float extent; one slot unused |
| `float3_x4` | Four padded three-component storage elements, each occupying four float slots |
| `float3x4` | Three-row, four-column matrix in its recorded basis; storage packing is separately described |

Use `float2_`, not repeated underscores to count individual unused slots. The
underscore describes storage extent, not alignment. A four-slot extent does not
by itself establish alignment, and alignment does not establish size or stride.
The schema's explicit layout and metadata remain authoritative. Other layouts
need unambiguous generated identities without forcing every property into this
compact notation; the complete identifier grammar remains to settle.

The padded storage notation does not by itself classify the repeated values as
a matrix or ordinary array. Preserve that interpretation separately. It also
does not require a dedicated padded schema primitive: existing physical
structures/arrays and explicit strides can express the representation. Padding
is not a fourth meaningful shader component.

For example, a uniform-buffer `float3` at offset 0 followed by a meaningful
`float` at offset 12 is two fields sharing a 16-byte slot. The vector is not
`float3_`, because the fourth slot belongs to the following field. Likewise two
`float2` fields can occupy offsets 0 and 8. Conventional HLSL constant-buffer
packing and the selected vector-relaxed std140 layout permit these examples;
they do not imply identical layouts for every structure, array or matrix. Reflect
the compiled offsets and strides rather than treating every uniform member as
owning a complete `float4`. Uniform-buffer array spacing and structured-buffer
spacing must be checked in their own layout contexts.

### Matrix interpretation, storage and mathematical conventions

Keep three distinct kinds of information:

| Information | Meaning |
| --- | --- |
| Logical interpretation | Rows/columns in the producing and consuming contexts, including known compiler transformations between them |
| Storage mapping | How those logical elements map to bytes, including major-vector order, strides and padding |
| Mathematical usage | Row-vector/column-vector multiplication and application conventions; these are not inferred from storage order |

Row/column terminology must retain its context. It is not necessarily a storage
description. Preserve facts available from the artifact and compilation context;
application conventions that cannot be recovered must be supplied explicitly.
Equal dimensions and byte layouts alone do not establish consistent binding.
Producer/consumer interpretations must agree or have an explicit validated
mapping/conversion. Reflection records these facts; it does not silently
transpose data, rewrite shader code or establish a multiplication convention.

Matrix interpretation remains in rendering. The initial importer describes the
compiled SPIR-V matrix with `rendering.matrix.basis=spirv`, on the matrix shape
or physical matrix descriptor. Rows and columns in names such as `float3x4`
refer to that basis, not inferred original HLSL dimensions.

A physical matrix descriptor additionally uses
`rendering.matrix.storage_major=row` or `column`. Nested physical arrays describe
stored major vectors in the stated basis. Derive dimensions from their counts
and that storage mapping; do not add redundant dimension or stride decorations.
Stage-value matrix interfaces have no storage-major decoration because they
describe values rather than storage. The `storage_major` key specifically
describes storage; it is not a general producer/consumer convention tag.

DXC transposes HLSL matrix types when emitting SPIR-V and reverses their
row/column-major storage decorations. Do not label compiled dimensions as HLSL
dimensions or reconstruct source matrices lowered to vectors/arrays without
sufficient evidence. Preserve known source-to-compiled interpretation mappings
with their context instead of conflating source and backend row/column labels.
Storage order does not establish multiplication convention, coordinate handedness
or application meaning. Exact additional convention/mapping metadata keys and
attachment rules remain to settle with the component authoring contract; the
existing basis/storage keys do not purport to encode all of those meanings.

The initial authoring direction permits explicit application interpretation on
selected declarations or occurrences. Missing application conventions mean
unspecified; they are not filled from a storage-major flag. Known compiled facts
and compiler mappings retain their provenance independently. Initially diagnose
incompatible interpretations rather than automatically transpose or convert
data. A known compiler mapping can explain consistency, but introducing a new
conversion is outside reflection.

## Per-vertex, per-patch and system declarations

Within each input/output interface, emit applicable nonempty groups in this
fixed order. Each member has count one and targets a `value_group` interface:

| Member name | `rendering.interface.domain` |
| --- | --- |
| `vertex_values` | `vertex` |
| `patch_values` | `patch` |
| `fragment_values` | `fragment` |
| `target_values` | `target` |
| `system_values` | `system` |

These are declaration groups, not memory regions. They do not impose one common
extent on all their members. Built-in identity is independent of grouping: a
per-control-point position is a vertex value, an invocation identifier belongs
with system values, and tessellation factors belong with patch values.

Preserve a stage-imposed outer vertex array through a non-storage association
targeting the value description for one vertex. Its decorations identify the
wrapper and its declared extent. The interface occurrence retains locations,
built-ins and qualifiers. Interface-member count remains one for this convention.

| Vertex-array association key | Meaning / value |
| --- | --- |
| `rendering.sequence.kind` | `vertex_array` |
| `rendering.sequence.extent` | Positive declared array extent |
| `rendering.sequence.source_dimension` | Zero-based dimension in the reflected declaration before wrapper extraction |

```text
vertex_values:
  position -> vertex_array(extent=3) -> float4
  weights  -> vertex_array(extent=3) -> array[4] of f32

patch_values:
  factors  -> array[4] of f32
```

Only the first two declarations have a stage-imposed wrapper. Their inner value
shape is preserved in full. A patch array is not indexed by vertex merely because
it is an array. Single-vertex declarations have no invented array-of-one wrapper.
Wrapper identification follows stage and declaration rules, not a blanket rule
that removes the first dimension from every array. Retain which reflected level
was classified as the wrapper so the original shape remains explainable.

| Stage | Input value groups | Output value groups |
| --- | --- | --- |
| Vertex | Vertex attributes; system values | Single-vertex values; system values |
| Tessellation control | Vertex arrays; system values | Vertex arrays; patch values; system values |
| Tessellation evaluation | Vertex arrays; patch values; system values | Single-vertex values; system values |
| Geometry | Vertex arrays; system values | Single emitted-vertex values; system values |
| Fragment | Fragment values; system values | Colour targets; system values |
| Compute | System values | Normally empty; resource writes belong under resources |

This table describes conventional user-value cases. Classify built-ins according
to their individual rules and retain backend patch decorations separately from
the renderer's semantic grouping. No qualifier inheritance is implied by groups.

Keep these quantities separate:

- Declared interface-array extent: vertex-wrapper description.
- Control-shader output vertex count: entry-point execution property.
- Input patch size: future pipeline configuration.
- Geometry input topology: execution property.
- Geometry maximum emitted vertices: emission limit, not an output value array.

Unavailable or stage-dependent information must remain explicit, not be guessed
from another count. Later PSO validation compares value shapes and separately
checks topology/patch constraints; it neither indiscriminately equates outer
extents nor ignores them. Preserve relevant tessellation modes, geometry modes,
compute local dimensions and fragment execution properties on the component root.

### Interface coordinates and qualifiers

Attach coordinates and qualifiers to the reflected occurrence, not its reusable
value shape:

| Key | Meaning |
| --- | --- |
| `rendering.vulkan.location` | Declared location/base location |
| `rendering.vulkan.component` | Declared component |
| `rendering.vulkan.index` | Declared fragment-output index, when present |
| `rendering.spirv.builtin` | Exact SPIR-V built-in name, for example `Position` |
| `rendering.hlsl.semantic` | Available original semantic |

Keep each decoration on the aggregate/member where it actually occurs. Later
traversal can calculate effective leaf locations; do not store both aggregate
coordinates and a second authoritative flattened layout. Backend validation
rules govern legal combinations, including built-ins versus locations.

Preserve interface qualifier flags with these exact keys:

```text
rendering.spirv.decoration.Flat
rendering.spirv.decoration.NoPerspective
rendering.spirv.decoration.Centroid
rendering.spirv.decoration.Sample
rendering.spirv.decoration.Patch
rendering.spirv.decoration.Invariant
rendering.spirv.decoration.RelaxedPrecision
```

An emitted flag has value `true`; absence means it was not emitted. Backend
rules interpret effective defaults. Keep combinations of flags rather than
collapse them into a lossy interpolation token. The importer must supplement
SPIRV-Reflect where its convenience flags omit required qualifiers.

### Execution properties

Execution modes belong on the component root under
`rendering.spirv.execution.<ExactModeName>`. Flag modes use `true`, single-literal
modes use unsigned decimal, and `LocalSize` uses named `x`, `y`, `z` operands:

```text
rendering.spirv.execution.LocalSize.x = 8
rendering.spirv.execution.LocalSize.y = 8
rendering.spirv.execution.LocalSize.z = 1
rendering.spirv.execution.OutputVertices = 4
rendering.spirv.execution.Invocations = 1
rendering.spirv.execution.Triangles = true
rendering.spirv.execution.SpacingEqual = true
rendering.spirv.execution.VertexOrderCw = true
rendering.spirv.execution.EarlyFragmentTests = true
```

These are independent spelling examples, not one valid component's combined
mode list. Use a checked table of permitted modes, operand forms and stage
combinations. Unsupported modes or unresolved symbolic operands produce
diagnostics; do not invent literal values. Stage-wrapper extent, tessellation
output count, geometry emission limit and future pipeline patch size remain
distinct.

### Initial feature boundary

Reject specialization-dependent shapes and execution dimensions initially, with
an identifiable unsupported-feature diagnostic. Do not silently freeze them at
default specialization values. This policy does not yet settle every other use
of specialization constants; acceptance beyond this boundary must be explicit.
Reject physical layouts that cannot be represented faithfully, including cases
where schema extent/padding ownership would misdescribe the compiled layout.
Neither unsupported semantics nor unrepresentable storage may produce a
successful but incomplete reflection result.

## Combined reports and publication

Support enclosing compile-only and compile-and-reflect operation modes. Jobs
remain independently reusable. One owning group result retains ordered member
reports, request/component identities, selected operation and group summary.
Each member retains source/entry point, effective options/provenance, compilation
status/diagnostics, available SPIR-V, reflection status/diagnostics and successful
reflection data. Do not replace compilation success with reflection failure.

| Outcome | Retention |
| --- | --- |
| Compile failure | Compilation diagnostics; reflection blocked by upstream failure |
| Reflection failure | Successful compiled artifact and diagnostics from both operations |
| Both succeed | Artifact, complete reflection data and both sets of diagnostics |
| Compile-only | Artifact/diagnostics; reflection explicitly not requested |
| Work not started/interrupted | Identifiable per-member reason |
| Storage/consolidation/bake failure | Separate finalisation failure; preserve earlier outcomes and available reports |

Do not expose a partial failed schema as usable. Distinguish not requested,
blocked by prior failure and infrastructure interruption. Reserve basic report
bookkeeping before dispatch. Diagnostics are bounded UTF-8 with truncation flags;
allocation failure can leave a minimal status without optional diagnostic text.
Ordinary member failures do not cancel siblings. Drain accepted jobs at shutdown.

Intermediate artifacts and schemas stay in rendering. Reflection borrows immutable
SPIR-V while its owner remains stable. Compilation may feed reflection before all
other compilations finish. Protect the source bundle for all accepted borrowers;
retained strings/results must survive release of the source bundle and compiler
objects. Do not concurrently mutate schema binding lists or relocate live payloads
while workers borrow them. Bind/resolve locally rather than transport resolver
indices, intrusive bindings or borrowed library pointers.

Publish the complete result once at the enclosing operation boundary. The initial
public operation transfers ownership to Host retention and returns an identity
with borrowed views; disposal invalidates them. Host need not interpret SPIR-V
or resolve reflection schemas to retain the result. A later operation can retain
the same intermediates and publish only a package plus selected diagnostics.

Keep service/publication status, group outcome and per-stage/finalisation outcomes
distinct. Even an all-failed group has a useful report. Publication success does
not imply shader success, successful library assembly or pipeline compatibility.

Extend the renderer-owned group report with separate compilation, reflection and
finalisation outcomes while retaining the existing compile-only operation.
Concrete public request/report/view structures and status spellings remain to
settle. Set configurable ceilings for declaration/member counts, nesting, strings,
diagnostics and retained/staging bytes before coding. Account for source
intermediates, packed payloads and final storage that coexist during assembly;
the generic block's 2 GiB ceiling is not an aggregate memory budget.

### Atomic production-library policy

Any required shader compilation or reflection failure poisons the production
library. Sharing, consolidation, packing or final validation failure likewise
prevents publication of a production asset. There is no partial-production mode
in the initial contract.

Continue independent jobs and checks after ordinary failures, including reflection
of successful compilations. Checks dependent on missing results are marked
blocked. Retain ordered reports for successful, failed and unexecuted members,
available artifacts and diagnostics, and a group/finalisation summary. The
completed operation result contains an optional validated production library;
failure leaves that optional artifact absent without discarding the reports.

Successful intermediate shaders in a failed group are diagnostic/conditioning
results, not a partially usable production library. A future diagnostic export
must identify itself separately. A failed rebuild leaves any previously valid
published library intact. Reporting a failed attempt and replacing a production
asset are separate actions.

## Live assembly and component-library output

Prepare reflection type descriptions and binary bulk data per shader/variant,
then consolidate into one component library containing types, optional instances
and bulk data. Distinguish the editable `CSchema` aggregate, which contains only
literals, from live runtime roles with stripped descriptions and complete binary
backing. Final runtime output uses the common block after aggregation. Independent
per-member baked snapshots are optional, not the required normal intermediate
representation.

Retain exact SPIR-V as named `u32` bulk collections with binary payload storage;
do not require a document node per instruction word or duplicate the artifact in
the reflection report. Its storage schema/access must remain independent of
reflection success. Artifact finalisation may copy compiler output; no zero-copy
construction guarantee is assumed. Preserve debug/reflection information at this
stage. Later distribution stripping must keep artifact requirements consistent.

The library catalogue links component identity to code and reflection entries;
ordinary instance/bulk data can hold it. Shared-type consolidation does not merge
stage interfaces into a pipeline interface. The future content description can
select these components for PSO construction, which remains outside this phase.

### Implemented combined persistence

The [version-2 common block](../schema/common-binary-block.md) is implemented as
`CBakedDataBlock` and persisted as `.bsd`. It uses the existing MBD2 encoding and
header with unchanged reserved-zero fields. Root metadata identifies
`fileType: "morphic-schema-data"`, `version: 2` and `stripped: true`, with optional
`types`, `instances` and `data` sections. Each present data section requires its
exact `instancePayloadSize` or `bulkPayloadSize` and complete supplied backing,
including explicitly supplied empty storage for a zero-length domain. Missing
backing is an error, not a request to materialise data later.

The file contains the baked description, instance binary and bulk binary in that
order. Its base and every nonempty domain are 128-byte aligned; absent/empty
domains consume no payload bytes or alignment padding. Inter-domain padding is
zero, and no final alignment tail is saved. Locators remain relative to their
own domain. Checked arithmetic, exact extent and an inclusive 2 GiB complete-file
ceiling apply. Payload-internal padding is distinct from container padding and
can be preserved byte-for-byte. Prior schema-data forms and superseded APIs have
no compatibility layer.

No separate rendering-specific binary header is required. The library identity
and library-format version live in the schema-defined catalogue; they are
distinct from the generic block version. Generic storage assembly does not
interpret rendering metadata. Types, instance and bulk typed interfaces remain
separate, borrowing stable backing. Envelope validation checks storage; typed
attachment checks supplied values/layout, followed by renderer validation. A
valid envelope alone does not establish a usable shader library.

### Library assembly routes and remaining packing work

The current APIs provide two distinct paths:

| Route | Contract and consequence |
| --- | --- |
| `CSchema` composition/merge followed by `bake` | Validates contained types and literals, then constructs complete runtime backing directly in the final block |
| Prepared runtime description and payloads followed by `CBakedDataBlock::assemble` | Copies supplied bytes into independent storage transactionally; performs no typed construction or automatic component relocation |

Importing runtime roles into authoring and then baking is possible, but requires
literal reconstruction and its fidelity/temporary-memory costs. Types must be
imported separately. It is not the required path for large SPIR-V arrays or for
preserving arbitrary payload bytes. Generic authoring `save` is also distinct
from runtime `bake`: saving an admitted authoring tree does not prove it valid or
create runtime payloads.

For binary-preserving library assembly, rendering must prepare the combined
runtime descriptions, correctly aligned domain payloads and relocated locators,
then assemble once and perform typed/rendering validation before publication.
Use deterministic payload placement and preserve request order in the catalogue.
Relocate each entry within its own payload domain and copy SPIR-V bytes without
literal reconstruction. Retain intermediates until finalisation succeeds or
fails so reports and available artifacts survive an assembly failure.
Neither authoring merge nor raw assembly concatenates component payloads or
rewrites their locators automatically. The detailed multi-source packing plan,
entry identity mapping and bounded temporary ownership remain to design. The
generic persistence mechanism itself no longer needs a new schema extension.

Both construction and assembly require empty output owners and publish only on
success. Keep source owners and borrowed views valid through the operation.
The library's atomic production policy additionally requires all component,
consolidation and final validation checks to pass before replacing an asset.

## Remaining decisions before implementation

The representation and starting policies above are agreed. The following are
remaining contract details, not a reopening of those policies:

1. **Multi-source library packing.** Specify the deterministic placement algorithm,
   relocation records, entry identity mapping and bounded staging around the
   implemented `CBakedDataBlock::assemble` contract. Request-ordered catalogues,
   binary preservation, one final assembly followed by validation and retained
   intermediates are agreed. Generic persistence and the atomic production-library
   failure policy are already settled.
2. **Authoring grammar and identity encoding.** Complete the ordered explicit-entry
   grammar above: field constraints, library/public-name boundaries, reversible
   qualified-name encoding, catalogue format and shared-type selector/ordinal
   syntax. Define deterministic dependency naming and collision diagnostics for
   overlapping shared mappings.
   Include the compact physical names above without conflating descriptive names,
   semantic identities and distinct physical layouts. Specify any additional
   producer/consumer matrix convention and mapping metadata, its provenance and
   attachment, without inferring application conventions from storage order.
3. **Public envelopes and limits.** Name the combined request/report/view types,
   stage/finalisation status enums and operation selector. Set numeric ceilings for
   declarations, members, depth, strings, diagnostics and total retained live/final
   bytes, including temporary finalisation overlap. Preserve Core dependency rules
   and existing compile-only behaviour during migration.
4. **Additional feature contracts.** Specialization-dependent shapes/execution
   dimensions and unrepresentable physical layouts fail visibly under the agreed
   initial policy. Settle any other specialization-constant support and the
   concrete representation of wider Boolean buffer values without mapping them
   blindly to `b8`.

The checked execution-mode/stage table, renderer semantic validation, layout
fixtures and failure tests are implementation/verification work. They must prove
the supported scope against full-slot/tail rules and context-qualified matrix
interpretation; they are not additional architectural decisions. New evidence
that a promised case cannot be represented must be raised explicitly.

Verification should cover all six stages, nested/fixed arrays, non-square matrices
in both storage orders and their context-qualified interpretations, packed
`float3` plus scalar versus padded `float3_`/`float2_`, CPU construction/validation
of concrete layouts, structured-buffer offsets/stride, fixed descriptor arrays,
patch versus vertex wrappers, built-ins and qualifiers, missing/conflicting shared
selections, standalone and consolidated resolution, retained diagnostics, member
and assembly failures with no partial production library, preservation of a prior
valid asset, fragment composition followed by contained-type validation,
multi-source binary relocation and exact SPIR-V preservation, shutdown,
source-bundle release and publication lifetimes. No runtime GPU or PSO validation
claim follows from these tests.

## Technical references

- [SPIRV-Reflect scope and validation boundary](https://github.com/KhronosGroup/SPIRV-Reflect#non-features).
- [DXC SPIR-V mapping](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst),
  including reflected-name limitations, layout contexts and structured buffers.
- [HLSL constant-buffer packing](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules).
- [SPIR-V physical representation](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#_physical_layout_of_a_spir_v_module_and_instruction).
- [Vulkan interfaces](https://docs.vulkan.org/spec/latest/chapters/interfaces.html),
  [shader stages](https://docs.vulkan.org/spec/latest/chapters/shaders.html) and
  [geometry shading](https://docs.vulkan.org/spec/latest/chapters/geometry.html).

These sources inform importer design; the engine's selected feature policy and
schema contracts determine the initially accepted subset.
