Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   persistence-and-aggregation.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Oct 2026

# Schema persistence and aggregation

## Status and scope

This guide describes the implemented aggregate/runtime split. It supersedes the
previous separate preparation/merge APIs and optional retained-value runtime forms.
The [specification](specification.md) defines the current contracts; the
[runtime API](runtime-api.md) supplies signatures and lifetimes. Completed plans
are retired into the [delivery record](../project/completed_milestones.md#schema-document-model-and-runtime-construction).

## Runtime storage and authoring storage

Runtime .bsd uses the [version-2 common block](common-binary-block.md). Every
present data domain has its binary; descriptions are always stripped. Storage
loading/assembly neither requires nor retains a types owner. Typed attachment
validates an existing payload and does not construct missing storage. Explicit
rebinding handles changed compatible types/defaults; asserted recovery requires
an unavailable previous owner. No old reader or automatic format migration is provided.

`CSchema` contains only authoring sections, without runtime envelope/locators.
It may be saved in generic baked-document form or written as text before semantic
validation. Its full validation, live materialisation and runtime bake require
contained types and validate every contained data section against those types.
There is no external fallback. Materialisation copies an independent types owner
and creates the present roles; no runtime binding borrows from the editable aggregate.

## Section-only import and composition

`CSchema::import_from` replaces the receiving aggregate with only the source
role's section, leaving the source unchanged; it does not merge existing content.
Import from a types owner copies types only; import from an instance or bulk role
copies only its own section. Linked types are never implicit. Instance import
reconstructs selected literals from the current binary, retaining hierarchy, equal
pins, empty selections and sparse array holes. Bulk import reconstructs complete
records. Both remove storage metadata and require faithful literal representation.
Runtime promotion and bake instead preserve raw bytes and masks without decoding.

Compose explicitly from selected section queries, then merge further documents
as required, validate at the chosen boundary, and materialise or bake. Composition
can contain no types or data, including no sections at all. Ready sources undergo
whole-root structural admission; an omitted source differs from an explicit empty
section. Sources and occupied aggregate destinations may alias. A failure preserves
them; successful replacement creates independent storage and invalidates old
destination handles. Saving/runtime construction require empty output owners.

## Structural admission and deferred validation

Admission checks allowed root/category/descriptor fields, declaration-name rules,
container shapes and a 256-level authoring tree bound, including opaque values.
It rejects runtime metadata. Missing semantic fields, unresolved targets and
incomplete values may remain until full validation. Structural admission does not
certify resolved layouts or data completeness. Full validation requires a contained
types section even for an otherwise empty document and resolves all contained types.

## Non-destructive merge rules

Objects match by name, preserve one-sided fields and recursively merge shared
content. No source precedence resolves semantic conflicts. Destination publication
is transactional, including when it aliases either source. Source handles in errors
remain tied to the original live/baked documents; discarded staging handles never escape.

A named type definition is indivisible. Duplicate names must agree in category,
member order and intrinsic definition shape. Missing intrinsic properties cannot
be supplied by a different partial definition. Defaults, decorations and eligible
presentation metadata merge separately, so one-sided defaults/keys survive and
conflicts reject. This merge rule is distinct from strict whole-definition comparison,
which includes defaults, internal markers and decorations; representation comparison
ignores them. Exact member remapping retains default/internal checks but ignores
decorations.

Omitted and explicit computed layout metadata may match where representation
equivalence can be proved using each definition's available dependency closure.
Unrelated unresolved types do not impede that proof. Relevant unresolved dependencies
report unestablished compatibility rather than guessing, and intrinsic mismatches
report incompatible definitions. Interface count omission is equivalent to count one.
Associations/interfaces remain indivisible non-storage definitions with their existing
target rules; cross-document reference resolution is deferred until final composition.

Instance declarations and specialisation trees merge recursively by matching names.
Positional selection arrays allow different lengths, inherited null holes and recursive
compound selections. A hole supplies no selection, selected values survive, and
incompatible overlapping selected values conflict. This does not change default-array
or complete-value array rules. Outer bulk record arrays merge an equal common prefix
and retain a longer tail; nested complete arrays require equal lengths. Named and
positional encodings are not generally coerced during document merging.

### Agreed document scalar equality

Compare stored document values independently of presentation preferences, without
coercion between scalar kinds. Presentation differences do not cause conflicts;
they are combined under the rules in the next section.

| Scalar kind | Equality rule |
| --- | --- |
| Null | Matches null only; an absent field is not an explicit null. |
| Boolean | Require the same boolean value; no equivalence with integers. |
| Integer | Require the same exact mathematical integer, independent of signed/unsigned storage domain and decimal, hexadecimal or binary presentation. |
| Floating point | Require the same stored representation, including the sign of zero; no numerical tolerance. |
| String | Require identical decoded contents, including case and actual whitespace; source escape spelling does not participate. |
| Different scalar kinds | Conflict, even where a conversion could produce equal values. |

For example, signed `+12` and unsigned `12` match, but `-1` and `UINT64_MAX` do
not. Integer comparison must retain full integer precision rather than convert
through floating point. Integer `1`, floating-point `1.0`, string `"1"` and
boolean `true` are distinct. Ignoring document integer storage-domain metadata
does not ignore schema type declarations: `"i32"` and `"u32"` remain different
string values and describe different types.

Document floating-point nodes contain finite values. Schema-aware interpretation
of special-value strings is separate from document scalar equality: `"nan"` and
`"NaN"` conflict as document strings, although typed conversion may recognise both
as NaN. Once binary values are being compared, the agreed NaN equivalence and
permitted-value rules apply.

Object fields are matched by name; different object member order is not a
conflict. Array order remains significant under the agreed array rules. Defaults,
labels, descriptions and other semantic metadata stored as document fields are
ordinary merge content: missing fields survive, and conflicting supplied values
fail. Textual descriptions are content even when they do not affect binary layout.

Type-schema `decorations` are string maps on named types, members/bit fields and
array descriptors. Their keys merge by the same object rules: one-sided keys
survive, equal strings collapse and differing strings conflict with locations in
both sources. There is no inheritance or precedence. Strict definition comparison
includes these maps, whereas representation compatibility and exact remapping
ignore them. Remapping still checks defaults and internal markers as before.

Schema copying, promotion, baking, canonical output and composition preserve
decorations in retained type sections. Role-specific data import, promotion
and persistence remain data-only and do not embed their attached schema.
Decorations on instance/bulk entry descriptors and section roots are unsupported;
ordinary application fields named `decorations` retain their existing meaning.

### Agreed presentation merging

Presentation merging preserves likely intent when equal values have different
writing preferences. It does not select between conflicting values. Fields
present in only one source retain their presentation. For duplicate integers:

- Explicit hexadecimal or binary presentation wins over adaptive
  decimal-or-hexadecimal or ordinary decimal presentation.
- Within the same explicit radix, retain the largest minimum digit count.
- Between explicit hexadecimal and binary, compare requested minimum widths in
  bits: hexadecimal digits multiplied by four versus binary digits. The larger
  width wins; binary wins equal widths.
- Adaptive decimal-or-hexadecimal wins over ordinary decimal. Two adaptive
  preferences remain adaptive; preserve the policy rather than replacing it with
  whichever radix displays the current value.
- Use the prefix style belonging to the winning presentation, including when
  the minimum digit count decides the winner. If the presentations otherwise tie
  completely, either source prefix is acceptable.

| Competing presentations | Result |
| --- | --- |
| Decimal and adaptive decimal-or-hexadecimal | Adaptive decimal-or-hexadecimal |
| Adaptive and explicit hexadecimal or binary | The explicit radix and its digit preference |
| 2-digit and 8-digit hexadecimal | 8-digit hexadecimal |
| 4-digit hexadecimal and 16-digit binary | 16-digit binary |
| 8-digit hexadecimal and 16-digit binary | 8-digit hexadecimal |
| 4-digit hexadecimal and 32-digit binary | 32-digit binary |

Decimal digit counts do not enter the bit-width comparison. These preferences
select a minimum display width, never a truncation width; the stored value still
determines any additional digits needed for safe representation. Integer storage
width metadata must remain valid for the value and is distinct from presentation
minimum digits. Adaptive presentation retains the existing writer policy of
decimal within -65535 through +65535 and hexadecimal outside that range.

For equal string values, escaped newline output wins whenever either source
requests escaping. Suppression of newline escaping survives only when both
sources suppress it. The decoded string contents are unchanged; actual newline
content and literal backslash characters are not made equivalent by this rule.

Number form, minimum digits and newline-escaping outcomes are independent of
source order and merge grouping. Prefix choice on a complete presentation tie
is deliberately unspecified. These rules replace source-precedence selection
of presentation and do not grant precedence for semantic conflicts.

## Binary equality, selections and output

Meaningful encoded-field comparison ignores padding/unused bits and treats NaNs as
equal, while retaining signed-zero and raw boolean/integer/enum distinctions.
Complete overwrite retains all old selections and adds meaningful differences;
component specialisation explicitly pins the addressed fields. Preserve/propagate
are caller choices for both operations and for removal. Preserve adds necessary
direct-child compensation without changing descendants' bytes; propagate updates
only unselected descendants in scope. Conversion never minimises these selections.

Runtime copying preserves arbitrary selected/raw encodings. Literal import rejects
unknown enums, noncanonical booleans or any other value failing encoded round-trip
checks. Existing padding may survive runtime copying but is not represented in
authoring literals. Live output repacks active entries and zeros new alignment gaps;
raw block assembly preserves whole supplied ranges, including gaps/trailing bytes.

## Diagnostics and publication

`SSchemaDiagnostic` supplies stage, reason, first/second source handles and nested
types/instance/bulk/block diagnostics. Composition admission additionally identifies
which selected source carried the first location. Storage errors keep exact format,
range, alignment and size reasons. Generic document-copy/bake failures retain their
operation-level reason where that subsystem offers no finer diagnostic. All errors
reset at operation start and clear on success; temporary source handles are cleared
before private staging is destroyed.

Type ownership, raw views and destination publication are explicit. No operation
implicitly changes another role's binding, applies a merge to a live runtime tree,
or updates common-block storage in place. Separate working binaries require explicit
replacement assembly to produce a new common block.

## Scope boundaries

No persistent cross-document references, schema identities/checksums, compression,
automatic layout migration, concurrent edit protocol, resource interfaces, section
decorations or selective C++ output are added. Bulk counts remain one-dimensional.
Direct raw views require caller-managed lifetime; scoped instance completion is
explicit and has no automatic cancellation/rollback of bytes already written.
