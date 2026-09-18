# Header survey: Vulkan and DirectX

This is a deliberately small survey against the working design in
`docs/schema/design.md`, not a proposal to parse either API wholesale. The
DirectX examples are from the installed Windows SDK 10.0.26100.0. A search of
the available SDK and Visual Studio include locations found no `vulkan_core.h`
or other Vulkan SDK header, so this report does not pretend to have sampled a
Vulkan declaration. Vulkan cases below are future tests once a pinned header is
available.

The model's intended role is configuration, not a copy of renderer state. The
survey therefore classifies source declarations by ownership: data-model
configuration, renderer execution state, or shader reflection/validation. A
declaration may supply names or purpose metadata without its physical API
layout being imported.

## Ownership seam

The useful initial boundary is inferred as follows.

| Concern | Owner | Data-model involvement |
| --- | --- | --- |
| Durable, user- or asset-authored choices: material options, render-pass intent, resource descriptions, vertex/instance record layouts, format choices and defaults | Data model | Define logical configuration and, where binary data is stored or uploaded, the selected physical representation. It may retain curated names, enum labels and purpose documentation from APIs. |
| Translation of those choices into API calls: device selection, object/handle lifetime, command recording, descriptor allocation, barriers, dynamic state and per-frame/transient state | Renderer | No general structural ingestion. The renderer owns API descriptors, pointers, chained structures and values derived from the current device/capabilities. |
| Shader stage inputs/outputs, resources actually used, binding locations, specialization requirements and compatibility diagnostics | Shader conditioning (reflection and validation) | Consume schema identities/layout declarations as contracts; emit validation metadata. Reflection must not make the data model a copy of shader compiler or API binding objects. |
| Byte-level format/packing rules shared by an asset or upload path | Data model, with renderer-selected implementation | Model an authored, stable packed representation when it is part of the asset/configuration contract. Do not infer it from an API format enum name. |

This is intentionally a seam rather than a claim that each value has one
permanent home. For example, a data-model texture intent can select a format,
the renderer can choose the supported API resource description, and reflection
can verify the shader expects a compatible sampled type. The first is durable
configuration; the latter two are derived execution and validation facts.

## DirectX samples and fit

| Header declaration | What it exercises | Fit with the current design |
| --- | --- | --- |
| `D3D12_RESOURCE_DESC` (`um/d3d12.h`: 3089--3101) | Ordered scalar, enum and nested-structure members; natural padding; `UINT64` fields | Structurally direct, but normally renderer-owned. The data model can define a smaller, stable resource-intent type and selected enum/purpose metadata; the renderer constructs this API descriptor after capability and allocation decisions. |
| `D3D12_GRAPHICS_PIPELINE_STATE_DESC` (`d3d12.h`: 2251--2271) | Fixed `DXGI_FORMAT RTVFormats[8]` in a large record | The array is structurally direct, but the complete descriptor is renderer-owned derived state. A configuration may specify compatible attachment/format intent; reflection validates shader output compatibility; the renderer supplies counts and API state. |
| `D3D12_CLEAR_VALUE` (`d3d12.h`: 3124--3132) | Format plus anonymous union of `FLOAT Color[4]` and `D3D12_DEPTH_STENCIL_VALUE` | Clear colour/depth intent may be data-model configuration, but the union API layout belongs to the renderer. Separate logical `clear-colour` and `clear-depth-stencil` configuration forms avoid importing the union. |
| `D3D12_ROOT_PARAMETER` (`d3d12.h`: 4052--4062) | Discriminant enum plus anonymous union of three unequal structures | Renderer/reflection boundary, not a data-model record to ingest. Reflection may validate that a configuration's named resource contract is satisfiable; the renderer creates root-signature specifics. |
| `XMCOLOR` (`um/DirectXPackedVector.h`: 41--53) | Union of four byte fields and `uint32_t c` | A canonical packed colour asset representation can belong in the data model, but retaining both C++ views is unnecessary. Choose one authored schema; do not ingest the union solely for API convenience. |
| `XMU565` and `XMDECN4` (`DirectXPackedVector.h`: 352--363, 636--646) | C++ bitfields over raw 16/32-bit words; signed/unsigned fields; normalized-value comments | A selected stable vertex/asset packed format can be modelled as authored bit ranges. It is not a reason to support general C++ bitfield ingestion. Normalisation is explicit semantic metadata. |
| `XMFLOAT3A` / `XMMATRIX` (`um/DirectXMath.h`: 164--172, 678--681, 517--535) | Macro-selected `alignas`/`__declspec(align)`; inheritance; vector/intrinsic union alternatives | Useful only as an alignment/layout test. These are renderer/math-library implementation types, not configuration-source types. |
| `DXGI_FORMAT` (`shared/dxgiformat.h`: 11--42) | Regular named enum including packed-format names such as `R10G10B10A2_UNORM` | Curated labels may be configuration metadata, while compatibility is validated by renderer/reflection. The enum name does not define texel bits; use an authored packed definition or format catalogue where physical bytes matter. |

## Representational conclusions

The core model already directly covers fixed-size scalar records, nested
records, fixed arrays, resolved padding/offsets, named integer enums, explicit
alignment, and fixed-storage packed bit ranges. This does not imply that every
representable API record belongs in the model: its primary targets are durable
configuration and selected physical asset/upload records. Renderer descriptors
and reflection objects are deliberately allowed to remain outside it.

The material gaps are:

- A discriminated union type, or an explicit policy to model each alternative
  as a separate schema. If supported, it needs storage size/alignment, named
  alternatives, optional active-tag association, and construction/JSON rules.
  Anonymous unions need a stable generated or author-supplied name.
- A raw-byte/blob field and opaque handles/pointers, or a clear exclusion.
  `D3D12_SHADER_BYTECODE` (`d3d12.h`: 2196--2200) is a compact example: a
  pointer plus host-size length is an API descriptor, not portable instance
  data.
- Target-ABI facts: data model, pointer width, endianness, compiler/version,
  packing pragmas/attributes, enum policy and bitfield allocation policy.
  Resolved size/alignment are the right output, but cannot safely come from
  source text alone.
- A controlled target-specific alias map (`UINT`, `UINT64`, `SIZE_T`,
  `FLOAT`, `HALF`, etc.). `SIZE_T` must not silently become fixed-width.
- A status separate from physical schema semantics for typedefs, qualifiers,
  pointers, SAL annotations, macro-selected branches, anonymous members,
  inheritance, and C++ methods/conversions.

## Requirements for constrained header ingestion

An initial importer should accept only a preprocessed, pinned header view plus
an explicit target profile. It should only be used for an allow-listed set of
configuration or physical-data types; name/purpose extraction has a separate,
less ambitious path. It should:

1. Accept selected named C-style `struct`/`enum` declarations and
   fixed-bound arrays after typedef resolution.
2. Preserve declaration order and source provenance while emitting fully
   resolved size, alignment and member offsets.
3. Require an ABI profile and reject, rather than guess, unresolved macros,
   incomplete types, variable-length arrays, dependent expressions,
   pointer-sized aliases and compiler-specific layout features.
4. Require a per-type allow-list (or annotations) and diagnose every excluded
   member/type.
5. Lower bitfields only through a target/compiler-specific adapter with tested
   allocation rules. Emit schema bit offsets/widths, never C++ bitfield syntax
   as a portable layout claim.

For name/purpose extraction, the importer may retain an allow-listed declared
name, enum labels and attached documentation without claiming to have imported
the type's layout. This is the appropriate route for renderer/reflection
concepts that users need to select or understand but which do not form durable
data-model instances.

Explicit non-goals: a C++ parser/compiler; automatic preprocessor-configuration
selection; general COM/class/function parsing; following pointer graphs;
deriving pointer ownership or SAL contracts; mirroring renderer API descriptors
or shader-reflection objects; and inferring union discriminants, format-channel
layouts, normalisation or semantic remaps from names/comments.

## Recommended representative tests

1. `D3D12_RESOURCE_DESC`: enum + `u64`, `u32`, `u16` and nested
   `DXGI_SAMPLE_DESC`, with expected natural offsets and tail padding.
2. A reduced `D3D12_GRAPHICS_PIPELINE_STATE_DESC` containing
   `DXGI_FORMAT formats[8]` and its count: verify the array is not coupled
   to the count member.
3. `D3D12_CLEAR_VALUE`: first assert a precise unsupported-union diagnostic;
   later test both alternatives and required tag-selection rules.
4. `XMCOLOR`: test a canonical byte-field schema and raw-word view; ensure
   the importer never fabricates channel order from the type name.
5. `XMU565` and `XMDECN4`: lower through an explicit MSVC/x64 fixture to
   exact bit ranges; reject an unknown ABI and test signed-field handling.
6. A 16-byte-aligned, four-`f32` record patterned after `XMFLOAT3A`:
   verify alignment and stride/tail padding, and reject inheritance initially.
7. When a pinned Vulkan SDK header is available, add one ordinary
   `Vk*CreateInfo` record (reject pointers as appropriate), one
   `Vk*FlagBits` enum, and one authored packed format definition. Do not
   infer a packed physical layout from a format name.
