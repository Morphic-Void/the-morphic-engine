Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   schema-example-notes.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Sep 2026

# Reviewed schema sample

[schema-example.json](schema-example.json) reconciles the original examples
with subsequent decisions. The earlier grammar was accepted; this expanded
revision adds the agreed layout and short-input examples and proposes removing
the redundant array `kind` property. `detail.internal` illustrates the candidate
internal-marker spelling, which is still for review. It is not an implemented
resolver acceptance fixture or a specification of every future extension.
The working [design](design.md) records agreed semantics. Showing a later feature
here does not bring it into the initial delivery.

The file uses Morphic JSON numeric forms: explicit `+` on non-negative signed
enum values, and numeric `0x` masks. These are not quoted strings. A strict
JSON parser will reject those spellings; their use is intentional.

## Grammar illustrated by the sample

| Construct | Spelling and meaning |
| --- | --- |
| Definitions | `types` contains the original `enumerations`, `structures`, and `bit_structures` category objects. Categories organise definitions without adding type namespaces. |
| Enum | A named object with integer `storage` and a `values` object mapping labels to numbers. Property order is declaration order. |
| Structure | A named object with an ordered `members` array of singleton named descriptor objects. |
| Structure detail | `detail.alignment` and computed `detail.size` illustrate generated metadata. Both may be omitted on input. The candidate `detail.internal` marker identifies an internal layout for export purposes. |
| Member | `type` is a named type or an inline array descriptor. Optional `default` supplies a validated default for that member. If any member specifies `offset`, every member of that structure must specify it. |
| Fixed array (proposed simplification) | `{ "element": "f32", "count": 2 }`. Count identifies an array; the element descriptor can itself be an array descriptor. Count is positive and stride is computed. |
| Bit structure | Integer `storage` describes the containing word; ordered named `members` select fields with non-zero contiguous `mask` values. |
| Bit member | `type` gives the field's base/logical type: an integer, `b8`, or a named enum. Optional `interpretation` retains specialised meaning such as `unorm`. |
| Instance | `instances` groups named instances by type. `declaration` holds supplied values, and `specialisation` holds named children inheriting from that instance. |
| Bulk collection | `data` groups named arrays of complete records by type, without declarations or specialisation wrappers around each record. |

The category names, `storage`/`values`, and array `element`/`count` are
retained from the original sample. `types`, `mask`, `default`, and the uniform
bit-member `type` field reconcile later decisions into the reviewed syntax.
The `declaration`/`specialisation` names and parent-based inheritance are
already agreed. Leaf instances omit `specialisation` in this form.

The proposed array simplification removes only `kind`: `type` still accepts
either a named type or an array descriptor containing both `element` and
`count`. A positive integral `count` identifies an array, including count 1;
zero is invalid, and an element without its count is incomplete. `count` is
not moved onto the member descriptor. This keeps nesting recursive:
`ArrayExamples.uv_rows` is an array of three arrays of two `f32` values.
`ArrayExamples.single` remains a one-element array, not a scalar. This revision
does not define a compatibility alias for the old `kind` spelling.

The singleton objects around members are text wrappers. The existing parser
normalises them into named array children with descriptor payloads; the resolver
reads that baked form. Schema text parsing rejects collision extension before
baking rather than relying on recovery of duplicate-property history later.

Named-component structures such as `Position` are recognised from their
same-type, non-array primitive members, without a new marker in this form.
`Vertex.uv` remains an ordinary fixed array, replaced in full rather than
patched by index. Schema member order is preserved in either case.

## Bitfield interpretation

The sample distinguishes the containing word from each field's declared type.
`SurfaceFlags` has unsigned `u16` storage, while its `axis` field uses the signed
underlying type of `Axis`. Signedness comes from that field type, not from its
bit position or the unsigned container. The named enum itself supplies its
underlying type; there is no duplicate enum `interpretation` descriptor.

Masks use ordinary numeric bit positions, with the least significant bit
represented by `0x1`. They describe the selected range within the word, not
byte order in a serialized payload. For enum range validation, the selected
width determines the field's representable range using its underlying signedness.

`SurfaceFlags.axis` now has three bits (`0x0070`, bits 4-6), so the signed
range -4 through 3 includes `Axis.z` (2). The original two-bit field did not.
`SurfaceFlags` leaves bits 7-15 unused. Its gap flag is true, without assigning
any fill policy to those bits. `ColourRgba10A2` covers all 32 bits with disjoint
10/10/10/2 masks and has no gaps.

The `unorm` spelling is retained from the original sample to show the intended
interpretation descriptor. Its instance conversion and rounding rules remain
later work; no packed instance encoding is assumed here. The type separation
uses the containing storage type for generated mask constants,
so a shifted mask need not fit the logical field type or its enum value set.

## Defaults, aliases, and inheritance illustrated

- `BlendMode.solid` aliases `opaque`; output for their shared value uses the
  first declared label, `opaque`.
- `Material.blend_mode` defaults to `"alpha"`, and `Material.axis` to `"z"`.
  These labels belong to their respective enums; numeric enum defaults would
  be invalid. They are checked during schema resolution.
- `ColourRgba8.a` defaults to 255. This is a default of `ColourRgba8` itself,
  not a parent definition propagating a default into a nested structure.
- `Position.origin` uses positional construction. Its child `raised` changes
  only `y`, producing `[0.0, 2.0, 0.0]`.
- Positional structure input and fixed arrays may be short: values fill from
  the start and remaining members/elements use defaults. `Vector4.base.padded`
  supplies `[2.0, 1.0, 4.0]` and becomes `[2.0, 1.0, 4.0, 0.0]`, replacing
  the inherited fourth value of 7. Its sibling `raised` supplies only named
  `y` and becomes `[2.0, 3.0, 4.0, 7.0]`, retaining the other values.
- `Vertex.base.short_uv` replaces the complete ordinary array with `[0.5]`;
  its effective `uv` is `[0.5, 0.0]`, not `[0.5, 0.75]`. The inherited
  `position` and `colour` are unchanged. The base's omitted `colour` uses
  `ColourRgba8` defaults, including alpha 255. Declared array count and
  structure size are unchanged by short input.
- Null is not an omission marker and is invalid anywhere in these values.
- `Material.base` gets `alpha`, `z`, and roughness 0.5 from defaults. `polished`
  changes roughness to 0.1; its child `hidden` preserves that roughness and
  changes only visibility. Sibling `rough` inherits from `base`, not `polished`.
- Bulk records explicitly supply all members, avoiding a decision about
  omitted values in `data`. Their nested `Position` values use positional
  form, ordinary `uv` arrays have exactly two elements, and colours use names.

## Expected layout

These expectations follow the agreed size-based atomic alignment rules. They
are review calculations, not compiler test results. Structure `detail` values
in the sample report these sizes and alignments; `InternalRecord` additionally
requests increased alignment and explicit member offsets.

| Type | Member byte offsets in declaration order | Size | Alignment | Array element stride | Gaps |
| --- | --- | --- | --- | --- | --- |
| `BlendMode` | n/a | 1 | 1 | 1 | No |
| `Axis` | n/a | 1 | 1 | 1 | No |
| `Position` | 0, 4, 8 | 12 | 4 | 12 | No |
| `Vertex` | 0, 12, 20 | 24 | 4 | 24 | No |
| `ColourRgba8` | 0, 1, 2, 3 | 4 | 1 | 4 | No |
| `Material` | 0, 1, 2, 4 | 8 | 4 | 8 | Yes: byte 3 |
| `Vector4` | 0, 4, 8, 12 | 16 | 4 | 16 | No |
| `InternalRecord` | 0, 16 | 32 | 16 | 32 | Yes: bytes 4-15 |
| `ArrayExamples` | 0, 24, 32 | 96 | 16 | 96 | Yes: bytes 26-31 and nested record gaps |
| `ColourRgba10A2` | bit masks within one word | 4 | 4 | 4 | No |
| `SurfaceFlags` | bit masks within one word | 2 | 2 | 2 | Yes: unused bits |

`Vertex` still refers forward to `ColourRgba8`. The fixed `uv` array occupies
8 bytes with a 4-byte element stride. `ArrayExamples.uv_rows` occupies 24 bytes:
its inner stride is 4 and outer stride is 8. `records` has stride 32, including
each `InternalRecord`'s padding. That member brings the enclosing structure's
natural alignment to 16 without a separate increase on `ArrayExamples`.
There are no format/version headers, bit offsets/widths, or gap-fill directives.

The grammar review accepted category objects beneath `types`, the `default`
spelling, bit-member type/interpretation separation, and the illustrated `data`
shape. This sample does not settle bulk default omission, normalised codecs,
or CSV/binary payload attachment. The `offset` and structure `detail`
rules are recorded in [the design](design.md#explicit-offsets-and-structure-details).
Structure size remains computed from members and alignment, rather than an
authored size or stride override.
If `detail` or its `alignment` property is omitted, the agreed natural
alignment rules apply, respecting any explicit alignment of nested types.
Generated schema documents include both structure `detail.alignment` and
`detail.size`. Neither value must be explicitly supplied during code ingestion
and schema creation, or during resolution from a baked document. All structure
definitions here include them to illustrate writer output. Omitting the details
from `Position`, for example, still resolves to size 12 and alignment 4; omitting
`InternalRecord`'s alignment would instead remove its explicit increase.
Resolved descriptions always contain alignment and size. A supplied size must match the computed
layout; it is not an independent size override. The details for
`Position`, for example, are `{ "alignment": 4, "size": 12 }`.
Normalising this sample would use its resolved schema and backing document to
create a new document containing the generated details and canonical
representations, leaving this input unchanged. Normalisation reuses the
resolver's validated types and computed layout rather than repeating that work.

The structure-detail extension also allows internal structures to be identified
for export purposes. Explicit-offset layouts remain exportable for review even
when ordinary source declarations cannot fully reproduce them. Generated
declarations may include explicit padding members to account for offsets and
total size. Those padding members do not become logical schema members; any
claim of layout fidelity requires target-specific layout validation. The
expanded sample illustrates the candidate marker on `InternalRecord`. Its
`tag` occupies bytes 0-3; a generated declaration would need twelve bytes of
padding before `position` at byte 16, together with alignment 16 on the
structure. Padding belongs to `InternalRecord`, including when it is an array
element. It never becomes an extra positional value or a named override target.
The sample does not provide generated source or establish export mechanics.

## Review checks performed

An independent check read the saved sample, converted its numeric extensions
to strict JSON spellings in memory, and checked type references, default
values, array extents, supplied instance member names/values, mask contiguity
and overlap, enum field ranges, and the layouts above, including explicit offsets,
increased alignment, nested array strides, and short positional replacement.
These checks passed for all eleven definitions, eleven instance declarations,
and the three bulk records. This was not a run of the engine's Morphic parser,
schema resolver, or C++ declaration generator.
