Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   schema-example-notes.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Sep 2026

# Reviewed schema sample

[schema-example.json](schema-example.json) reconciles the original examples
with subsequent decisions. The expanded reviewed revision includes layout and
short-input examples, omits redundant array `kind`, and uses `detail.internal`
as the internal-layout marker. Its `types` section is an implemented resolver
and C++ compiler acceptance fixture. The instance and bulk sections illustrate
materialisation with reserved locators and are exercised by data-role tests,
including live instance baking and reload.
The working [design](design.md) records agreed semantics.

The instance and bulk entries use unset locators for materialisation. Named
declarations and inheritance form the intended embedded instance representation; no
additional flattened-value field is required. Current capabilities and delivery
status belong in the [runtime API](runtime-api.md) and
[implementation contract](implementation-contract.md), respectively.

The file uses Morphic JSON numeric forms: explicit `+` on non-negative signed
enum values, and numeric `0x` masks. These are not quoted strings. A strict
JSON parser will reject those spellings; their use is intentional.

Ordinary 8/16-bit integers use decimal; ordinary 32/64-bit integers use hexadecimal
according to their declared type. Thus the `u32` tag default is `0x01`, whereas the
`u8` alpha default is `255`. Masks use the full containing storage width, such as
`0x0001` for `SurfaceFlags` and `0x000003ff` for `ColourRgba10A2`, without C++
suffixes. Counts, offsets, sizes and alignments use decimal through 65535 and
hexadecimal above it. Hex digits use the writer's lowercase spelling; uppercase
input is also valid. Signed-domain `+` and `-` signs remain intact. Generated
C++ declarations instead choose ordinary integer notation by value; see the
[runtime API](runtime-api.md#c17-declarations-and-validation).

## Grammar illustrated by the sample

This is an authoring example, not a saved binary-backed output document. Its
`valid: false` locators reserve fields for materialisation, and its declarations
deliberately retain redundant selections to illustrate input and inheritance.
Reconciliation/output may remove those selections, add locator sizes and assign
valid offsets. The file uses Morphic JSON extensions despite its `.json` suffix;
strict-JSON tools should consume the writer's strict mode instead.

| Construct | Spelling and meaning |
| --- | --- |
| Definitions | `types` contains the original `enumerations`, `structures`, and `bit_structures` category objects. Categories organise definitions without adding type namespaces. |
| Enum | A named object with integer `storage` and a `values` object mapping labels to numbers. Property order is declaration order. |
| Structure | A named object with an ordered `members` array of singleton named descriptor objects. |
| Structure detail | Generated documents include `detail.alignment` and `detail.size`. Both are optional checked metadata for natural layout; explicit-offset layouts require an authoritative size. `detail.internal` identifies an internal layout for export purposes. |
| Member | `type` is a named type or an inline array descriptor. Optional `default` supplies a validated default for that member. If any member specifies `offset`, every member of that structure must specify it. |
| Fixed array | `{ "element": "f32", "count": 2 }`. Count identifies an array; the element descriptor can itself be an array descriptor. Count is positive and stride is computed. |
| Bit structure | Integer `storage` describes the containing word; ordered named `members` select fields with non-zero contiguous `mask` values. |
| Bit member | `type` gives the field's base/logical type: an integer, `b8`, or a named enum. Optional `interpretation` retains specialised meaning such as `unorm`; optional scalar `default` belongs to this field definition. |
| Instance | `instances` groups named instances by type. Each base or specialisation reserves a `locator`; `declaration` holds supplied values when present, and `specialisation` holds named children inheriting from that instance. An absent declaration selects no values: a base uses defaults and a child inherits unchanged. |
| Bulk collection | `data` groups named collections by type. Each collection is an object with a `locator` and optional `data` array of complete unnamed records. The sample's unset locator reserves `offset`, `valid`, and `count` for materialisation. Records have no specialisation mechanism. |

The category names, `storage`/`values`, and array `element`/`count` are
retained from the original sample. `types`, `mask`, `default`, and the uniform
bit-member `type` field reconcile later decisions into the reviewed syntax.
The `declaration`/`specialisation` names and parent-based inheritance are
already agreed. Leaf instances omit `specialisation` in this form.

The array simplification removes only `kind`: `type` still accepts
either a named type or an array descriptor containing both `element` and
`count`. A positive integral `count` identifies an array, including count 1;
zero is invalid, and an element without its count is incomplete. `count` is
not moved onto the member descriptor. This keeps nesting recursive:
`ArrayExamples.uv_rows` is an array of three arrays of two `f32` values.
`ArrayExamples.single` remains a one-element array, not a scalar. This revision
rejects the old `kind` spelling as an unknown property.

The singleton objects around members are text wrappers. The existing parser
normalises them into named array children with descriptor payloads; the resolver
reads that baked form. Schema text parsing rejects collision extension before
baking rather than relying on recovery of duplicate-property history later.

Named-component structures such as `Position` are recognised from their
same-type, non-array primitive members, without a new marker in this form.
`Vertex.uv` remains an ordinary fixed array. A short base declaration fills its
tail from defaults; a short specialisation retains the inherited tail. Schema
member order is preserved in either case.

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
`SurfaceFlags` leaves bits 7-15 unused. Its gap flag is true. Under the revised
design, an optional caller-invoked pass can clear these unused bits and alignment
padding; construction does not require blanket zeroing. `ColourRgba10A2` covers all 32 bits with disjoint
10/10/10/2 masks and has no gaps.

The `unorm` spelling is retained from the original sample. The revised design
uses nearest floating quantisation with ties away from zero and clamping for
`unorm` and `snorm`, raw integer codes for input/defaults/output, and conventional
decoding. Schema-default quantisation and the baked instance/bulk construction
codecs, live data roles and decoding are implemented. The type separation
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
  An aggregate `default` on `Vertex.colour` would be rejected; instance values
  and overrides may still set its components explicitly.
- `Position.origin` uses positional construction. Its child `raised` changes
  only `y`, producing `[0.0, 2.0, 0.0]`.
- Positional structure input and fixed arrays may be short. Base construction
  completes omitted members/elements from defaults; specialisations inherit
  them from their base. `Vector4.base.padded` supplies `[2.0, 1.0, 4.0]` and
  becomes `[2.0, 1.0, 4.0, 7.0]`. Its sibling `raised` supplies only named
  `y` and becomes `[2.0, 3.0, 4.0, 7.0]`, retaining the other values.
- `Vertex.base.short_uv` supplies the first array element with `[0.5]`;
  its effective `uv` is `[0.5, 0.75]`. The inherited
  `position` and `colour` are unchanged. The base's omitted `colour` uses
  `ColourRgba8` defaults, including alpha 255. Declared array count and
  structure size are unchanged by short input.
- Null is not an omission marker and is invalid anywhere in these values.
- `Material.base` gets `alpha`, `z`, and roughness 0.5 from defaults. `polished`
  changes roughness to 0.1; its child `hidden` preserves that roughness and
  changes only visibility. Sibling `rough` inherits from `base`, not `polished`.
- Bulk records must explicitly supply all members, including nested members
  and full fixed-array extents. Their nested `Position` values use positional
  form, ordinary `uv` arrays have exactly two elements, and colours use names.

## Expected layout

These expectations follow the agreed size-based atomic alignment rules. They
are checked by compiler-validation fixtures on x86 and x64. Structure `detail` values
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
shape. Bulk omission is now explicitly rejected. Baked instance and bulk payload
loading are implemented. The `offset` and structure `detail`
rules are recorded in [the design](design.md#explicit-offsets-and-structure-details).
Natural structure size is computed from members and alignment. An explicit-offset
structure instead requires a supplied size, validated for fit and alignment;
additional trailing space is allowed and belongs to the structure.
If `detail` or its `alignment` property is omitted, the agreed natural
alignment rules apply, respecting any explicit alignment of nested types.
`CResolvedSchema::prepare_output()` includes both structure `detail.alignment` and
`detail.size`, adding missing properties from the resolved layout.
Natural-layout input may omit both; explicit-offset schema creation
must supply size and baked resolution requires it. All structure
definitions here include them to illustrate that intended form. Omitting the details
from `Position`, for example, still resolves to size 12 and alignment 4; omitting
`InternalRecord`'s alignment would instead remove its explicit increase.
Resolved descriptions always contain alignment and size. A supplied natural-layout
size must match the computation. `InternalRecord` supplies its explicit extent of
32 bytes; increasing it to 48 would be valid with 16 extra owned tail bytes and
array stride 48 (and would require updating the enclosing `ArrayExamples` size).
The existing sample retains size 32. The details for
`Position`, for example, are `{ "alignment": 4, "size": 12 }`.
Normalising this sample would use its resolved schema and backing document to
create a new document containing the generated details and canonical
representations, leaving this input unchanged. Normalisation reuses the
resolver's validated types and computed layout rather than repeating that work.

The structure-detail extension also identifies internal structures. The later
decision removes review-only output: accepted explicit layouts must generate
faithful C++ under selected compiler settings or be rejected, with alignment
capped at 128. Padding members may account for offsets and total size but do not
become logical schema members. Fidelity requires target-specific validation. The
expanded sample illustrates the marker on `InternalRecord`. Its
`tag` occupies bytes 0-3; the generated declaration uses twelve bytes of
padding before `position` at byte 16, together with alignment 16 on the
structure. Padding belongs to `InternalRecord`, including when it is an array
element. It never becomes an extra positional value or a named override target.
The test suite generates and compiles the declarations, but the sample does not
establish export mechanics.

Layout validation resolves and compiler-checks `InternalRecord` and its dependent
`ArrayExamples` on x86 and x64. The baked loaders can materialise the instance hierarchy and
`Vertex.triangle`; the full
example deliberately covers more than schema resolution.

For tooling previews, use the existing standard-JSON writer option. For the
numeric extensions here it emits equivalent values without explicit `+` signs
or hexadecimal notation. Morphic output retains signed-domain and notation intent,
including a minimum hex width derived from parsed digit counts. A generic round
trip preserves `0x0001` and expands `0x1` to `0x01`; it does not select formatting
from schema types. `CResolvedSchema::prepare_output()` sets the data model's
presentation metadata for full-width masks and other schema-specific rules.
Instance and bulk reconstruction apply the same ordinary integer policy.

## Review checks performed

`test_schema_sample` in `Schema_test_suite.cpp` reads this file through the engine's
Morphic parser, bakes and resolves its schema, checks layouts, generates C++
validation fixtures, materialises its bulk and instance data, promotes both roles,
and exercises embedded/external baking and reload. Generated declarations are
compiled by the separate layout-validation checks.

The following records an earlier manual review, before that automated coverage:

An independent check read the saved sample, converted its numeric extensions
to strict JSON spellings in memory, and checked type references, default
values, array extents, supplied instance member names/values, mask contiguity
and overlap, enum field ranges, and the layouts above, including explicit offsets,
increased alignment, nested array strides, and the then-current short-input rules.
These checks passed for all eleven definitions, eleven instance declarations,
and the three bulk records. This was not a run of the engine's Morphic parser,
schema resolver, or C++ declaration generator. The user's 30 September
clarification supersedes that check's positional-specialisation expectations:
omitted values inherit, and every specialisation is an independent alternative
which leaves its base unchanged. The examples above reflect that clarification.
