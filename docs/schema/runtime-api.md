Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   runtime-api.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Oct 2026

# Schema runtime API

This guide describes the implemented document model. See the
[specification](specification.md) for value/layout rules and the
[persistence guide](persistence-and-aggregation.md) for composition and conversion contracts.
No compatibility aliases, old output forms or version-1 runtime reader are provided.

## Header organisation

| Header | Responsibility |
| --- | --- |
| `schema/schema.hpp` | Binary-free `CSchema`, aggregate query/handles, composition, merging, validation and conversions. |
| `schema/types_wrappers.hpp` | Guarded live/baked types owners, editor and client bindings. |
| `schema/resolved_types.hpp` | Shared resolution, type observations, canonical types output and C++ generation. |
| `schema/document_query.hpp` | Separate types, instance and bulk query/handle families. |
| `schema/live_instances.hpp`, `baked_instances.hpp` | Binary-backed instances and retained selection hierarchy. |
| `schema/live_bulk_data.hpp`, `baked_bulk_data.hpp` | Complete bulk records, live creation and replacement. |
| `schema/baked_data_block.hpp`, `data_block.hpp` | Common .bsd storage, assembly and diagnostics. |
| `schema/data_remap.hpp`, `unused_storage.hpp` | Exact-type remapping and explicit padding clearing. |

`instance_selection`, `types_admission`, `document_construction`,
`document_merge`, `data_reconstruction` and `data_block_construction` are
internal support. General parsing, text writing, copying and generic document
baking remain in the data-model module. The old preparation, composition,
role-specific merge and output-form headers have been removed.

## Aggregate authoring

`CSchema` owns an editable `CLiveDocument` containing any combination of
`types`, `instances` and `data`, including none. It owns no runtime payload.
`CSchemaDocumentQuery` and `CSchemaHandle` refer to the aggregate; they are
distinct from `CTypesDocumentQuery` and `CTypesHandle`. Section queries use
the existing role families and retain the original physical root.

`initialise` creates empty authoring storage. `edit` exposes its live tree.
`try_adopt`, `copy_from`, `compose_from`, `merge_from` and `save`
check structural authoring admission, including allowed properties, identifiers,
section/category shapes and bounded depth. They permit unresolved references,
missing semantic fields and incomplete values. Runtime locators, payload lengths,
envelopes and stripped markers are not authoring content. Direct edits may leave
an invalid intermediate tree; the next operation rechecks it.

`compose_from(types, instances, bulk_data, diagnostic)` selects each named
section from its corresponding aggregate query. A default/unready query omits
that input; an explicit empty section remains present. A ready input must contain
its requested section and pass whole-root structural admission. No attached
types are copied implicitly. `merge_from(first, second, diagnostic)` joins
compatible content, preserving indivisible named definitions. These operations
stage privately, may replace an occupied destination, and allow destination/source
aliasing. Failure preserves all source/destination content and retained handles.

`validate` requires a contained `types` section, even when no data exists.
It resolves that catalogue and validates every contained data section against it.
There is no external types fallback. `save(CBakedDocumentBlock&, ...)` instead
saves the authoring tree in generic baked-document form without semantic
validation; output must be empty. Text output uses the ordinary document writer.

`materialise(types, instances, bulk_data, diagnostic)` validates and constructs
independent live owners. All outputs must be empty; absent data sections leave
their role outputs empty. The extracted live types owner must outlive the bound
roles. It never borrows types from the editable aggregate. Failure publishes none
of the owners. `bake(CBakedDataBlock&, diagnostic)` performs the same contained-
types validation and produces one complete runtime block in an empty destination.

These operations share an internal validation and construction plan. `validate`
allocates no runtime payloads. Materialisation resolves its independent types copy
once and constructs each domain in one payload allocation. Baking writes into the
final block's payload spans without creating temporary live roles. Authored type
content and source diagnostic locations are preserved; no validation state is
cached across edits to the aggregate.

`import_from` accepts each live/baked types, instance or bulk role and replaces
the aggregate with only that role's section; it does not merge with existing
content and leaves the source unchanged. Types import does not require a
resolved owner. Data import requires a loaded role, retains instance hierarchy
and selections, reconstructs literals from binary, and removes runtime metadata.
It excludes linked types. Unknown enum codes, noncanonical booleans or other
values that cannot round-trip under encoded-field comparison fail transactionally.
Explicit empty containers and equal-value pins survive. Literal spelling/enum
aliases and padding do not have a lossless text guarantee.

```cpp
schema::CSchema types_part, instances_part, combined;
schema::SSchemaDiagnostic error;
if (!types_part.import_from(types_owner, error) ||
    !instances_part.import_from(live_instances, error) ||
    !combined.compose_from(types_part.document_query(),
        instances_part.document_query(), {}, error) ||
    !combined.validate(error)) return;
schema::CBakedDataBlock block;
if (!combined.bake(block, error)) return;
```

`SSchemaDiagnostic` carries an operation stage/reason and source locations.
Merge conflicts retain first/second source handles. Composition admission also
sets `source` to types/instances/bulk_data to disambiguate its first location.
Nested types, role and block diagnostics preserve more specific failures. No
diagnostic exposes a discarded staging handle; borrowed source locations require
their original document to remain alive.

## Persistence envelope

The [common block contract](common-binary-block.md) specifies version 2 .bsd:
`fileType: "morphic-schema-data"`, `version: 2`, `stripped: true`, optional
sections and an exact payload length for every present data section. A present
empty domain requires a supplied zero-length binary. A missing runtime binary is
an error, not a request to reconstruct values. The generic baked-document header
and encoding remain unchanged.

`CBakedDataBlock` owns local storage or borrows an asset range. It retains no
types binding. `adopt_local`, `attach_asset`, `release_local`, `clear`,
`loaded_ready`, `backing`, section/availability queries, `document`, the
two payload views and `file_bytes` expose storage independently of typed use.
It is immovable; release/adopt transfers local ownership without relocating bytes.
Asset attachment acquires no asset reference. Callers manage asset/backing lifetime.

`assemble(description, SDataBlockPayloads, diagnostic)` copies supplied binary
ranges and refreshes metadata into a new block. It accepts live or baked input,
requires an empty destination, and performs no type resolution or value decoding.
Each present data section requires its matching payload pointer; a pointer to an
empty view explicitly supplies zero bytes. A pointer without its section is also
invalid. Preflight checks these relationships before staging allocation. Raw
assembly preserves payload gaps and trailing bytes, unlike live repacking.

Live descriptions copy directly to private live staging; baked descriptions
promote once. The internal iterative copy has no additional recursive-depth
limit. The final document is baked once. Common and typed diagnostics retain
their distinct boundaries; a generic document bake failure remains
`document_bake_failed` where the generic API provides no finer reason.

Host loading, asset admission and binary saving operate on complete buffers.
Attach typed roles explicitly after storage loading; import a role into `CSchema`
explicitly to return to authoring. No implicit source conversion, stripping pass,
missing-binary construction or rebinding occurs during storage operations.

## Types wrappers and client bindings

`schema::CBakedTypes` copies a non-owning `CBakedDocument` view. The caller keeps
the immutable block bytes at stable addresses while the wrapper or its clients
use them, including when a host also has a mutable alias. `schema::CLiveTypes`
owns a `CLiveDocument` privately. Both wrappers expose `document_query()` over
the original physical root, `types_root()` for the schema section, and a nullable
`const CResolvedTypes*` through `resolved()`. `document_ready()` distinguishes
available document storage from `resolved_ready()`; client operations need the
latter. Queries are borrowed views and do not extend backing lifetime.

Use `set_document()` to bind an empty baked wrapper to a ready view. Use
`initialise()` to create an empty live document, or parse into a separate
`CLiveDocument` and `try_adopt()` it into an empty live wrapper. `resolve()` then
validates the current wrapped document. Failed resolution clears resolved tables
but retains the wrapper's document for diagnosis or an unchanged-input retry.
`reset()` stages fresh live storage before replacing the old document, so an
allocation failure leaves the old one intact. `try_take_from()` moves an
unreferenced wrapper into an empty destination and preserves a successful
resolution; live transfer repairs the resolver's pointer to the moved document.
Ordinary wrapper moves are disabled so referenced moves can fail explicitly.

`CBakedTypes::promote()` creates a separate live schema from the source's
`types` section, resolving the new copy before publication. The baked source may
be unresolved; its document and client bindings are preserved. Promotion requires
an empty destination; failure leaves it unchanged. A resolver failure reports
its reason and stage,
but clears occurrence handles and ranges because its temporary live document is
discarded. Translation failure reports `translation_failed`; invalid source or
occupied destination reports `invalid_input`.

`CLiveTypes::bake()` creates a separately owned `CBakedDocumentBlock` and an
unresolved `CBakedTypes` view over it. The source need only have a ready document,
so an incomplete editable schema can be saved without resolving it. Both outputs
must be empty. Keep the block's backing allocation alive at a stable address
while the baked wrapper or its clients use it; the owner object can move or be
transferred. The additional common-block bake overload accepts a `CBakedDataBlock` and emits
a types-only version-2 envelope. Neither overload performs full aggregate validation.
Both conversions retain only the `types` root member;
`instances` and `data` in a combined source are untouched and are absent from
the new document. Neither conversion carries client bindings into the result.

`CLiveTypes::edit()` returns a borrowed editor for the current owner. It can
append typed values, rename or erase descendants, change newline escaping, and
replace payloads. It does not expose a mutable `CLiveDocument`, detached values,
or reorder operations. It may create the missing `types` object at the root and
then edits only that subtree. The `types` entry itself cannot be renamed or
erased, but its payload can be replaced with an object to repair malformed input.
Other root sections remain available through queries and cannot be changed by
the schema editor. A successful or potentially mutating edit clears resolution;
call `resolve()` again before using resolved observations. The editor is a raw
borrow and expires when its owner moves, resets, adopts new storage, clears its
document, or dies.

`CTypesBinding` is a move-only link held inside a client. Binding requires a
resolved wrapper. Each schema counts its clients and holds an intrusive list of
their embedded links, without owning those clients. Every editor mutation and
document replacement checks that count at the time of the call. Referenced edits,
clear/reset/replacement and wrapper transfer assert in development builds and
return failure without changing state. Re-resolving the same unchanged input is
allowed while clients are bound. A failed re-resolution leaves them attached but
unusable: `resolved()` returns null until a successful retry. Client code must
check availability on each operation and not cache resolved pointers. Schema
destruction detaches and invalidates all links before releasing document state;
subsequent client operations reject safely, and `release()` remains safe. Binding
reassignment preflights the target, preserving the old link on rejection.

`core/schema/resolved_types.hpp` provides `schema::CResolvedTypes` and
`schema::generate_cpp`. Resolution consumes a validated `CLiveDocument` or
`CBakedDocument`;
it does not parse text, construct instances, or modify the backing document.
For text input, require `document_parser::parse(...).accepted()` under a policy
that excludes `EDocumentFinding::name_collision_extension`. The ordinary parser
policy already excludes collision extension. Baking is optional for resolution.

```cpp
schema::CResolvedTypes resolved;
schema::SDiagnostic diagnostic;
if (!resolved.resolve(block.document(), diagnostic))
{
    // Query the occurrence using CTypesDocumentQuery{ block.document() }.
    // The resolved owner is empty.
    return;
}
const auto vector = resolved.find_type(CStringView{ "Vector4" });
schema::SType type;
if (resolved.type(vector, type))
{
    const auto first = resolved.member_at(vector, 0u);
    schema::SMember member;
    if (resolved.member(first, member))
    {
        // member.offset, member.size, member.type and member.default_description are resolved.
    }
}
```

The owner is move-only. It embeds a baked view by value and borrows immutable
baked bytes, or borrows a live document. The source baked view object may
disappear, but its backing bytes must remain alive. A borrowed live document
must remain alive, unmoved and unedited while its resolution is used. The raw
query and resolver carry no client binding checks; use the wrappers for guarded
editing and client invalidation.

`resolve(query, diagnostic)` accepts an existing `CTypesDocumentQuery`.
`resolve(diagnostic)` uses the retained input for an unchanged re-resolution.
Every resolution attempt clears the previous tables immediately. A successful
unchanged re-resolution preserves existing `CTypesIndex` meanings. Changed
definitions can change those meanings, and failure or `clear()` invalidates
them. Failure releases every owned table and clears the input binding, so an
unready owner cannot re-resolve without a new input. Moving transfers the
binding, indices and tables and leaves the source empty. Indices have no
generation or owner identity.

Resolved schema record indices are 32-bit `CTypesIndex` values; zero is invalid.
Document occurrences use `CTypesHandle`, which preserves the full live node key
or baked value index and cannot be constructed from a raw key by callers. Both
handle types are distinct. Callers must not decode their numeric values. Named
lookup misses and unmapped occurrences return zero. Inspection methods return
false for wrong record kinds, wrong categories, absent indices or an unready
owner, leaving the output value unchanged. `SType.count` is the category's
member, label, field or element count.
`element_or_storage` is meaningful for arrays, enums and bit structures.
`SType.stride` is element stride for an array, and the type's size otherwise.
Array size is `round_up(count * stride, alignment)` and includes full element
slots plus any additional array tail padding. Repeated complete arrays advance
by size; size need not be divisible by element stride. The existing observations
also expose explicit array alignment and recursive padding without new categories.
Built-ins have no declaration occurrence or document name ID.
`primitive_name(EPrimitive)` returns their canonical schema spelling from static
storage, without allocation or a resolved owner. `none` and invalid enum values
return an empty string view. Resolver lookup and generated declaration comments
use the same authoritative name table.

`SType.primitive` directly describes the physical primitive for primitive, enum
and bit-structure types. The semantic category and `element_or_storage` reference
are retained. Structures and arrays report `none` for the whole object.
`SField.primitive` describes the logical field's primitive (including enum
underlying storage), independently of the containing bit-storage word.
`SMember.size` is the complete resolved member-type extent, including owned
padding. Public size/offset/alignment/stride observations remain 64-bit.

Physical definitions are enumerated in document order, including physical category-group order.
Members, labels and bit fields retain their declared order. Name-based and
ordinal access select the same records. `first_label_for_value` accepts exact
signed/unsigned integer values and returns the first alias in declaration order.
`named_components` identifies structures whose members all use the same
non-array primitive type. Layout observations always come from stored resolved
facts: size, alignment, offsets, strides and recursive gap presence.

`physical_member_at(type, ordinal)` visits the same member records by increasing
offset, with declaration ordinal breaking ties. It includes zero-byte members;
`member_at` and the existing indices still follow declaration order. Invalid
types, out-of-range ordinals and an unready owner return zero. Resolution retains
one 32-bit ordinal per member in a shared vector, with normal capacity overhead.
Natural layouts use declaration order directly; explicit layouts are sorted
once. Observation is constant-time and allocates nothing. The index follows the
resolved tables through moves, clearing and re-resolution.

An empty structure uses `members: []`, resolves with size and stride zero and
alignment one, and retains a queryable type index and document mapping. A
structure whose members are all empty is also empty. Its zero-byte members keep
their declaration order, names, indices, mappings and natural cursor offsets.
Arrays of empty elements require a positive count and have zero size and stride.
Generated C++ omits empty type definitions and zero-byte members; its physical
declarations therefore need not list every queryable member. Empty structures
accept canonical `detail.size: 0` and `detail.alignment: 1`; metadata requesting
storage or stronger alignment is invalid.

### Non-storage declaration observations

`find_declaration(name)` looks up built-ins, physical types, associations and
interfaces in one namespace. `declaration_count/at` enumerate authored physical
definitions first, then non-storage declarations, retaining document order within
each partition. Built-ins are available by name but excluded from enumeration.
Existing `find_type`, `definition_count/at/ordinal` and `type` remain physical-only.

`declaration(index, SDeclaration&)` observes category, name and source without
claiming physical layout. `EDeclarationCategory::interface_type` denotes interfaces;
physical category values correspond to `ECategory`. Built-ins retain their existing
empty name IDs/source handles and can be distinguished through `SType.primitive`.
`association(index, SAssociation&)` returns the immediate checked target index;
an invalid target denotes authored null, not a lookup failure. `interface_info`
returns `SInterface.count`, the number of declared members.

`interface_member_at`, `find_interface_member` and `interface_member` expose
`SInterfaceMember` with name, source, referenced `declaration` index and effective
positive `count`. Multiplicity never expands records. General observations and
decoration queries allocate nothing, borrow the unchanged source and follow
existing index lifetime/error conventions. Declaration and reference occurrences
map to their checked indices; interface member occurrences map to member indices.

Non-storage records use separate tables; existing physical record sizes are
unchanged. `type`, physical member/default queries and data operations reject their
indices. Empty physical types remain valid. Resolution checks all declarations,
including unused ones, and rejects mixed association/interface cycles. Moving,
clearing and re-resolution include the new tables.

`CDeclarationCompatibility` accepts `EDeclarationMatch::compatibility` or
`definition`. Both traverse checked relationships and preserve immediate identities,
ordered interface members and effective counts. Compatibility uses existing physical
representation checks and ignores decorations/defaults/internal markers; definition
includes them. `CTypeCompatibility` remains physical-only. Both use operation-local
graph caching with allocation-failure results and no cache across re-resolution.

### Decoration observations

`SDecoration` contains borrowed `key` and `value` string views and a `source`
handle identifying the original string node. `CResolvedTypes` provides:

- `find_decoration(owner, key, result)` for exact local lookup;
- `first_decoration(owner, result)` to start in document order;
- `next_decoration(current.source, result)` to continue within the same map.

The owner is a resolved named declaration, array, structure/interface member or bit-field index.
Built-ins and unsupported record kinds have no decorations. Missing keys, empty
maps, invalid inputs, end of enumeration and unready schemas return false without
changing the output. A successful query can return an empty string value. There
is no type-to-member fallback or other inheritance. Keys use document string
rules and exact comparison, independently of declaration identifier restrictions.

Queries allocate nothing and read existing document storage without enlarging
resolved layout records. They use document name lookup and sibling traversal;
they do not add a persistent decoration index. Returned strings and handles
require the original unchanged document, under the existing schema lifetime and
editing rules. Handles are scoped to their originating document. Existing
`CTypesDocumentQuery` operations also expose decoration nodes directly.

## Schema document output and integer presentation

`CResolvedTypes::prepare_output(CLiveDocument&, SDiagnostic&) const` creates a
document containing only the resolved source's `types` section, with canonical
integer metadata. The source can be live or baked. Both schema wrappers expose
this through `resolved()->prepare_output(...)`; resolution must be ready, and
the destination must be uninitialised. Failure leaves the destination untouched.
Output can be adopted into a live schema, baked with the data-model translator,
or baked and passed to the text writer. Resolution and existing wrapper
bake/promotion operations continue to preserve input metadata.

| Value role | Canonical metadata |
| --- | --- |
| Ordinary `i8`, `u8`, `i16`, `u16` | Decimal. |
| Ordinary `i32`, `u32`, `i64`, `u64` | Automatic-width hexadecimal, including small values. |
| Masks | Hexadecimal minimum of 2/4/8/16 digits for the containing 8/16/32/64-bit storage. |
| Counts, offsets, sizes, strides, alignments | Adaptive decimal/hexadecimal, decimal through 65535. |

The ordinary rules apply to enum definitions and integer defaults, including
nested array defaults, and to integers reconstructed for instance/bulk data documents.
Signed-domain values retain `+`/`-`; unsigned values have no sign. The canonical
hex prefix is `0x`. A small `u32` value prints as `0x01`, while a 32-bit mask
prints as `0x00000001`. Metadata storage width remains the measured minimum in
the selected domain, independently of declared width and padding. Floating-point
defaults spelled as integers retain their existing metadata.

The implementation in `types_output.cpp` derives a temporary rule list from
resolved records, sorts it once and uses binary search while reusing the document
subtree copier. No output index persists in resolved records. Output preserves
declaration order, names, strings and supplied default structure, and adds
omitted structure `detail.size` and `detail.alignment`, and array `stride` and
`alignment`, from the resolved layout.
Authored interface-member counts receive structural-integer formatting; omitted
counts remain omitted. Output does not materialise missing defaults or array elements.

Explicit instance/bulk value import shares the scalar policy. Bit-field values follow their logical declared type;
enum values remain label strings. Rebuilt locator integers use the structural
policy in runtime output. Raw bulk data capture still
constructs locators without decoding records. Input declarations copied without
reconstruction retain their metadata until a reconstruction operation is requested.

Live/baked translation preserves the selected metadata. Text round trips retain
emitted padding but cannot recover an adaptive policy; preparing schema output
again restores that policy. Strict JSON writing emits decimal values using the
existing writer option. See the
[data-model numeric contract](../data_model/revised_data_model.md#numeric-values).

## Composition and merging

Aggregate admission and semantic validation are separate operations. An overlapping
named type is an indivisible definition: members, order, category and intrinsic
shape must agree. Defaults, decorations and eligible presentation metadata merge
without precedence; conflicting supplied values fail. Missing intrinsic fields
cannot be filled from a different partial definition. Omitted versus explicit
layout metadata is accepted only when equivalence can be established from the
definition's available dependency closure. Unrelated unresolved declarations do
not block that proof; unresolved relevant dependencies report unestablished
compatibility. The final merged document is not automatically resolved.

Instance objects merge by name and hierarchy. Positional instance declarations
allow inherited null holes, short tails and recursive compound selections. A
hole contributes no override; two selected unequal values conflict. Complete
bulk arrays merge an equal common prefix and retain a longer tail; nested complete
value arrays retain their ordinary equal-length rules. See
[persistence and aggregation](persistence-and-aggregation.md) for scalar equality,
presentation and diagnostic rules. Authoring operations impose a 256-level tree
bound, including opaque/incomplete values; runtime hierarchy traversal may be deeper.

## Defaults

Member/field `default_description` is zero when no explicit default was supplied.
Pass its type and description to `default_value`: zero descriptions synthesize
primitive zero, the first enum value, or an aggregate description. Structure
and bit-structure defaults are obtained by inspecting their own members/fields.
An enclosing aggregate default is rejected, including arrays of structures.

An explicit array description stores only its supplied prefix.
Every supplied position must be unnamed; named children are rejected even when
their property name is explicitly empty, including within nested array defaults.
`default_element` returns a description for a supplied element and zero for an
omitted tail element; both are successful results within the declared extent.
Call `default_value` again with the element type. This composes for nested
arrays without materialising the declared extent. No gap bytes are defaulted.

`SScalar.kind` selects the active member of its named `SScalarValue value` union:
`value.signed_value`, `value.unsigned_value`, or `value.floating_value`.
Boolean and half values use `value.unsigned_value` for 0/1 or the
existing `fp16data_t` bits. Finite `f32` values are rounded to float and observed
as double. Enum defaults carry their underlying signed/unsigned integer domain.
Explicit `unorm` and `snorm` field defaults accept integer raw codes or
normalised floating values. Floating values clamp to [0,1] or [-1,1], then round
to the nearest integer after scaling by `2^width - 1` or `2^(width-1) - 1`;
ties round away from zero. NaN becomes zero, and infinities clamp to endpoints.
UNORM requires an unsigned integer primitive and at least one mask bit; SNORM
requires a signed integer primitive and at least two mask bits. Enums and `b8`
cannot carry either interpretation.
These scales match eventual reconstruction as `code/(2^width-1)` for UNORM and
`max(code/(2^(width-1)-1),-1)` for SNORM.
Integer `1` remains raw code one, while floating `1.0` reaches the positive
maximum. A raw SNORM signed minimum is valid, although floating `-1.0` encodes
the negative of the positive maximum. `SScalar` stores an unshifted unsigned
integer for UNORM or signed integer for SNORM, checked against both the mask
width and logical primitive. The source occurrence and field interpretation
remain available; implicit default is signed or unsigned zero as appropriate.
Public instance-role construction and decoding remain outside the resolved
schema API.

## Internal value construction

`core/schema/value_codec.hpp` exposes synchronous internal operations over a
ready `CResolvedTypes` and a representation-neutral `detail::CDocumentRead`:

```cpp
detail::construct_value(schema, document, type, declaration, destination,
    destination_size, detail::EConstructionMode::instance, diagnostic);
detail::construct_alternative(schema, document, type, declaration, base,
    base_size, destination, destination_size, diagnostic);
```

`complete_bulk_data` mode requires every recursively addressable member, bit field
and array position. Instance mode fills omitted values from schema defaults;
short positional input uses defaults, including an explicit array-default
prefix. An alternative copies its immediate parent's complete bytes into a
separate destination, then changes selected values. Positional instance input selects supplied non-null positions at every nesting
level; null holes and omitted positions inherit. Named
input selects members or fields. Explicitly supplied values remain selections
even when equal to the parent. Neither operation changes the parent.

A structure with exactly one primitive or enum member also accepts a scalar
value as shorthand for selecting that member. The same rule applies to a bit
structure with one scalar field. For `Value { a: u8 }`, `1`, `[1]` and `{"a":1}`
are equivalent initialisers. This applies to roots, nested members, array
elements, instance selections and bulk data records. It does not recursively unwrap
an array or compound member, and multi-member or empty compounds still require
aggregate input. Null is valid only as a positional instance-inheritance hole; scalar range, enum-label and bitfield
interpretation rules are unchanged. Explicit value import may emit the
equivalent explicit aggregate form while retaining the same selected member.

The internal `construct_value` singleton-element flag identifies a named
compound member unwrapped by the parser at an outer record-array position.
Bulk data callers supply this context explicitly; names on primitive array elements
remain invalid. Named singleton records still undergo complete-record checks.

Both byte spans are bounded separately and must cover the resolved type size
and alignment. Positive overlap is rejected before copying. Zero-byte types
accept null, zero-length spans, while still validating supplied declarations.
Stored scalars and bit words are little-endian. Construction writes logical
values without clearing structure/array gaps. A newly constructed bit word is
initialised as a unit; an alternative retains all inherited bytes and bits
outside selected masks. On conversion failure the destination may be partially
changed and must be discarded; the source stays unchanged. The codec does not bind a types owner; shuffled named selections can allocate
a temporary lookup index. Role wrappers check their schema
binding before each call. Callers and owners must keep borrowed baked document
and payload backing alive at stable addresses under each role's ownership rules.

## Entry indexes and read allocation

Baked roles build a sorted array of 32-bit record ordinals during loading or
materialisation, before publishing records or locators. This uses four bytes
per entry for the index elements and gives bounded logarithmic lookup. Transfers
carry the index with its records; clearing releases it.

Live roles binary-search their record vectors directly, without auxiliary index
storage or maintenance. Entry keys are strictly increasing in record order:
creation appends a fresh monotonic key, replacement and renaming preserve it,
and bulk data erasure preserves the order of the remaining records. Promotion creates fresh entry keys in record order, even when document
groups or specialisation branches are interleaved. Whole-owner transfers
preserve that order. Future mutation paths must preserve this invariant.

Neither role performs lazy construction or allocation during entry lookup or
instance hierarchy navigation. Name lookup remains a separate search; rendering
consumers can resolve names during preparation and retain entry handles or
bounded payload views under the existing lifetime rules. A bulk data collection
contributes one record-table entry regardless of its payload record count.

Live bulk data records retain their resolved type index, so access by handle does
not repeat a schema type-name search. Binding availability is checked on each
operation. Bound schemas cannot be edited or replaced; successful unchanged
re-resolution preserves indices, while failed resolution makes access unavailable
until a successful retry. Promotion retains the current types association; explicit rebinding resolves
indices in the target owner.

Rebinding and remap setup share an operation-local resolved-type
comparison context. Representation compatibility ignores defaults, the
`internal` marker and decorations. Strict definition comparison includes all
three, comparing decoration maps independently of order and equating absent and
empty maps. The remapping policy retains default/internal checks but ignores
decorations within the candidate member types. The enclosing member's own default
is not an additional remap matching condition. Hashed comparison keys retain both
type identities and, for definition/remapping matching, both default descriptions.
Completed matches and proven root mismatches can be reused within the operation;
unfinished comparisons are discarded on failure. No cache survives schema
re-resolution.

Scalar decoding is shared by instance and bulk value import. The codec also shares little-endian scalar bit access with
construction and comparison. Role-specific traversal and diagnostic mapping stay
with their callers, including the checks that generated literals reproduce
meaningful encoded values. This adds no decoding to stripped baking or baked
entry access.

Instance construction and explicit value operations use the data-model `document_translation::copy_subtree`
helper for live or baked sources, including copying within one live document.
It retains integer metadata, string newline-escaping flags, names and child order,
and removes partial copies on failure. The same helper group stabilises names
before document growth. Subtree copying and whole-document/selected-member
promotion share value construction and metadata handling through compile-time
source access. The subtree walk retains its 256-level depth bound and incremental
growth; promotion retains its iterative walk, node reservation and staged
publication. Baked access gains no additional storage or work.

## Initial attachment and types rebinding

`CBakedInstances` and `CBakedBulkData` borrow read-only descriptions and
complete binaries. Use `attach_block(block, types, diagnostic)`, or
`set_document(document)` followed by `attach_types(types, payload, diagnostic)`.
The supplied payload is explicit, including an empty view for zero bytes. An
absent selected section yields an empty role. A raw runtime description must have
`stripped: true`; if it declares a common envelope, the complete version-2
metadata contract applies. Baked attachment never creates binary or mutates locators.

Attachment validates type/layout, selection grammar, inherited instance values,
counts, alignment, extents, ordering and overlap. There is no compare-disable
option. Selected binary values may contain raw encodings that cannot be imported
as literals. Embedded types, when present during initial common-block attachment,
establish source interpretation before checked binding to the requested owner.
Otherwise the supplied owner establishes that interpretation.

Checked replacement first validates the source against its active type context,
then proves compatibility and applies replacement defaults. It cannot repair an
invalid persisted source by silently rebuilding it: an unselected stored value
of 9 under an active default of 1 must fail even if the requested new default is 2.
Without embedded or prior types, initial external attachment validates the supplied
interpretation; it cannot prove which original layout produced the bytes.

`rebind_types(target, ETypeBindMode::checked, diagnostic)` compares used type
representations, including empty used groups, with the still-available old owner.
Defaults and decorations do not change physical compatibility. Live instances
rebuild unselected values from the target defaults and immediate-parent chain,
retaining local selections and selected bytes. Handles and the live allocation
remain stable; inherited bytes may change. Bulk rebinding does not alter records.
Failure preserves the old association and bytes.
Resolved type indices and type-dependent plans belong to their original context;
reacquire or rebuild them after successful rebinding. Default application follows
propagation and does not add compensation selections. Bulk records remain complete
authoritative values and receive no replacement defaults.

A baked instance rebind without replacement accepts only changes needing no
meaningful byte updates. The overload accepting an empty `CByteBuffer&`
always publishes independently rebuilt storage on success, even when the values
already match. The role then borrows that buffer; keep it alive until the role
releases or replaces the association. The original common block/header remains unchanged. Subsequent promotion,
output and import use the current binding, not stale embedded defaults.

`asserted_recovery` is available only after the old owner/resolution becomes
unavailable. The caller asserts compatibility with the lost context; the operation
cannot prove that compatibility or check inheritance against lost defaults. It is
not a bypass while a usable old binding exists, nor an automatic fallback after
checked replacement fails. A stale embedded catalogue beside working bytes does
not replace their lost active context. Invalid modes and pending edits reject rebinding.

Read-only baked recovery checks existing inheritance against the supplied types
without changing bytes. Live recovery and baked recovery with a replacement buffer
retain selected encodings and rebuild unselected values from the supplied defaults
and immediate parents. Paths, ranges and layout are checked in either case; bulk
recovery preserves complete records. Failure leaves the role unusable with its
document and bytes unchanged. Selected values need not be representable as literals.

## Live instances and scoped editing

`initialise(types)` establishes a live role. `create_base` and
`create_specialisation` consume literal declarations, apply defaults or the
immediate parent, and retain stripped selections. `capture_base` creates a new
base from complete binary and selects meaningful differences from defaults.
Use `overwrite` for existing instances; capture does not replace them.

Runtime selections use named compound members, positional fixed arrays with
`null` inheritance holes, and `true` selected scalar leaves. Explicit empty
objects/arrays and selected leaves equal to their inherited values survive
promotion, baking and import. Actual instance values exist only in binary.

Every update takes `EInstanceDescendantPolicy::preserve` or `propagate`:

| Operation | Target selection effect |
| --- | --- |
| `overwrite(instance, complete, policy, diagnostic)` | Retain existing selections and add meaningful differences from the immediate baseline. Replace complete bytes, including supplied padding. |
| `specialise(instance, steps, count, literal_query, value, policy, diagnostic)` | Select/write the supplied component paths, retaining existing selections elsewhere. Equal values still pin. |
| `specialise(instance, steps, count, complete_component, policy, diagnostic)` | Select/write meaningful fields in the component while preserving destination padding and unused bits. |
| `remove_selection(instance, steps, count, policy, diagnostic)` | Remove the selected scope and rebuild it from the immediate parent/defaults. Interior array removal creates holes. |

A `member` path step names a structure member or bit field; an `element` step
selects a zero-based fixed-array index. Zero steps address the whole instance.
Binary component input uses the addressed type's representation and alignment.
A bit-field input is its unshifted logical scalar representation, with valid sign
extension and mask-width range. Supply that scalar value rather than its containing
packed word. Component writes preserve destination padding and unrelated bits.

Preserve leaves descendant binaries unchanged and adds only necessary direct-child
compensation selections within the update's scope. Those selections persist like
any other override. Propagate walks parent before child and copies only unselected
values in scope; descendant selections remain unchanged. Component operations
affect only their scope. Bit-field updates preserve unrelated fields and unused bits.
Inheritance always follows the immediate parent, including an explicit child value
equal to an older ancestor/default.

For example, start with `A.x = 10`, B inheriting A and C inheriting B. Writing
`A.x = 20` with preserve leaves B/C at 10 and adds B's selection of x. A later
propagating write of `A.x = 30` still leaves B/C at 10: that compensation is a
persistent override. Removing B's x selection with propagate makes B/C inherit 30.

Propagation, selection removal and default rebinding update meaningful fields while
preserving each existing destination's padding and unused bits. Complete overwrite
accepts every supplied target byte; its propagating descendant updates still follow
the field-only rule. Padding is never a selectable component.

Ordinary buffered instance operations accept input borrowed from their own payload,
including the target or a descendant. Aliased input is staged before mutation or
relocation can invalidate it. Failures preserve the ordinary transactional contract;
the already-written target bytes of a pending direct edit are the explicit exception.

`begin_edit(instance, token, view, diagnostic)` provides a bounded mutable view
of the complete existing snapshot. Only one edit per role may be pending. Typed
access, further mutations, binding, promotion, import and output are unavailable
until `complete_edit(token, policy, diagnostic)` succeeds. Completion has the
same selection/cascade semantics as complete overwrite. It is explicit: token
destruction does not complete or cancel an edit. Tokens bind owner and generation;
stale tokens, including after clear/reinitialisation or address reuse, are rejected.

Successful completion ends writable access. Do not write through a retained view
again without beginning another edit, even when its address is unchanged. Entry
handles and the allocation survive same-layout edits, but handles into replaced
selection subtrees must be reacquired.

On completion failure the edited target bytes and pending state remain available
for correction/retry; prior selections and descendant bytes remain unchanged.
No persistent backup or implicit cancellation is provided. The caller may clear
the whole role to abandon it; this invalidates the view. Borrowed views expire under the normal mutation and
lifetime rules; there is no concurrent editing or held-pointer tracking.

## Live bulk data

`capture(type, name, bytes, count, diagnostic)` creates or replaces a complete
record array. Replacement preserves the entry handle and may relocate its bytes.
`create_records` encodes complete literal records. `create_unpopulated` reserves
binary for callers to fill; it applies no defaults. `mutable_entry` exposes the
bounded writable extent without reconciliation because no bulk literals are retained.
Rename and erase affect the live entry. Self-buffer capture and borrowed names are
stabilised before mutation. Retrieve fresh entry views after growth/replacement.

Zero-count placeholders are useful during authoring but cannot be imported or
baked as complete bulk entries. A positive count of zero-size records remains
valid. The record count is one-dimensional and bounded by `UINT32_MAX`.

## Promotion and runtime output

`baked.promote(live, diagnostic)` produces independent live storage under the
current types owner; destination must be empty. It preserves selections and raw
bytes rather than deriving new literals. Rebind explicitly if a different types
owner is desired. It remains possible to promote raw values that cannot be represented as literals.

`live.prepare_output(document, payload, diagnostic)` requires two empty outputs
and produces a stripped role-only description plus a packed binary. It retains
all complete record bytes, including padding/unused bits, while zeroing newly
created inter-entry alignment gaps and dropping abandoned storage/trailing bytes.
It does not change source handles, selections, bytes or binding.

`live.bake(CBakedDataBlock&, role_block_diagnostic)` produces complete version-2
role-only storage with no linked types. It writes records directly into final
storage after preparing the description. Output is published only on success.
Runtime output never decodes values or minimises selections. Use section import
into `CSchema` for document-valued output; there are no embedded/external/stripped
form flags and no global reconcile operation.

## Bounded direct-member remap

`CDataRemapPlan::initialise()` accepts one or more source resolved structure
types and a destination resolved structure type. It matches direct members by
name and exact type; the root structure names may differ. Exact type comparison
includes category, physical layout, array count, named compound definitions,
ordered fields or labels, and effective defaults. A same-name member with any
type difference is left unmatched. This includes primitive, enum, array and
compound differences; no conversion or nested partial match is attempted.
The comparator skips a differing aggregate as soon as its own type identity or
layout differs, without inspecting its descendants. Both partial and entirely
empty plans are valid. A zero-byte exact member contributes to
`matched_member_count()` but creates no copy range. `copy_range_count()` reports
ranges after safe adjacency coalescing. Genuine invalid input and allocation
failures, and overlapping destination writes from combined sources, fail setup.

Setup builds a temporary sorted member-name table for each source, reusing its
storage across source slots. Matching compares names across the two schemas,
then uses the shared type-compatibility cache. A temporary byte per destination
member detects repeated claims on nonempty members: resolved direct-member
ranges already cannot overlap. Empty matches remain valid across multiple
sources. Source-slot and destination-member processing order, including overlap
diagnostics, is preserved. Both temporary tables are released when setup ends.

The plan owns the root layouts and ranges required for execution. It retains no
schema or document pointer, so resolved schemas may be cleared or destroyed
after setup. Reinitialising clears an existing plan first; failed setup leaves
it unready. The primary destination is an application-owned `CByteView`; it
need not be a schema role or document.
`execute()` takes an array of bounded source byte views in plan slot order and
one bounded mutable destination byte view. It derives each nonempty type's
complete-record capacity from its byte extent. Every view for a nonzero type
must have a byte size exactly divisible by that type's stride; an incomplete
trailing record rejects the whole call before writes. A valid empty view has
zero capacity; otherwise capacity is `1 + (bytes - size) / stride`. It copies the
minimum capacity across the destination and all nonzero-sized source types.
Narrow a view to limit the work. Zero-byte types do not constrain this byte-copy count;
an all-empty plan succeeds as a no-op without a logical record count.

Supply buffers in the source-slot and destination type representations selected
at setup; preflight checks memory, alignment and bounds, not the semantic
identity of arbitrary bytes. All supplied views are checked before writes, and
active source/destination storage overlap is rejected when copies are planned.
A failed preflight leaves the destination untouched. `CByteView` and
`CByteConstView` reset an invalid raw construction, including a null pointer
with a nonzero extent, to a canonical empty view. Execution sees that empty
view as zero capacity for a nonempty type and cannot recover the discarded
construction request. Execution has no diagnostic output and does not allocate,
traverse schemas, apply defaults, change selections, or update documents.
Unmatched destination bytes and records beyond the derived count remain
untouched. Whole matched aggregates copy their owned padding; gaps between
matched members are not copied. No-match plans are valid no-ops after view
preflight.

Setup selects contiguous bulk, contiguous-source, contiguous-destination or
strided copy paths after coalescing. Non-bulk paths use fixed 1, 2, 4, 8 and
16-byte copies where possible, with a runtime-size fallback. These are
prewritten scalar copy paths; no benchmark or platform-specific vectorisation
claim is made. For live instance destinations, explicit scoped completion publishes
selection changes and applies the requested descendant policy after the transfer.

## Role entry views for remapping

All four data roles provide `entry` with resolved type, bytes and extent.
Construct bounded `CByteConstView` inputs for `CDataRemapPlan::execute`.
Use application-owned storage, a bulk `mutable_entry` view, or an instance
`begin_edit` view as destination. Complete the instance edit explicitly with a
descendant policy after execution; ordinary access is unavailable while pending.
Execution itself performs no allocation, resolution or selection publication.

Plans retain no types owners and can outlive them. Apply them only to matching
representations and rebuild when those representations change. Entry type indices
carry no global type identity. Retrieve fresh views after any
operation that can relocate a live payload. Remapping a complete instance can
leave unmatched members unchanged; completion retains existing pins and selects
new meaningful differences. A zero-size type uses an empty view.

## Explicit unused-storage clearing

`clear_unused_storage(schema, type, writable_view)` clears byte gaps within
standalone schema-typed values, recursively through structures and arrays.
The view must contain complete values and be a multiple of the type's nonzero
size, including all array slots and additional array tail padding. Divisibility
by array element stride is neither required nor sufficient. A
partial trailing value fails without processing earlier values. A canonical
empty view succeeds for a zero-byte type; a nonempty view against such a type
is rejected because it has no typed bytes to classify.

`clear_unused_storage(schema, instance_query, writable_payload)` and the bulk data
query overload clear typed gaps in every located extent, plus all unreferenced
bytes within the supplied view's logical used range. This includes alignment
gaps and holes left by live replacements or erasure. Zero-byte or zero-count
entries occupy no bytes; their presence does not prevent clearing the rest of
the used range. Allocation capacity beyond the view is untouched. The query
and writable payload must describe the same document and schema. These calls
also work on a `prepare_output()` document and buffer after packing. A baked
role can supply the query while its host supplies a writable view of the
separately owned payload; the role itself remains read-only. No pointer identity
with a role is required for a directly supplied query and buffer.

`CLiveInstances::clear_unused_storage()` and
`CLiveBulkData::clear_unused_storage()` use their current document, bound
schema and owned payload through the same document-buffer operation. Callers
may invoke clearing on live backing. Packing zeroes new inter-entry gaps but
preserves aggregate padding and unused bits copied inside records; callers
requiring those to be canonical can clear the final output buffer as well.
No construction, capture, remap or output operation invokes this broader
unused-storage operation automatically. Loading, ordinary saving and raw block
assembly preserve all existing payload bytes, including nonzero gaps.

The default preserves unused bits in bit-storage words. Pass
`EUnusedBits::clear` to zero only bits outside declared field masks; declared
bits retain their exact encoding, including noncanonical values. Primitive
and enum encodings, unselected fields, NaN payloads and signed zero are never
normalised. The operation does not apply defaults, edit declarations, compact
storage, move records or change locators. It returns `bool` and validates the
complete layout, locators, extents and overlaps before writing. Invalid input
or allocation failure leaves every destination byte unchanged; repeated
successful calls are idempotent.

Structure validation and clearing use the resolved physical-member index. Each
walk visits a structure's members in linear time, including empty members at
equal offsets. Preflight retains operation-local results for shared types,
including the subtree height needed to enforce the depth limit. Clearing uses
those results without allocation or repeated recursive validation. It does not
rebuild member order for each record or array element. Complete preflight,
including any scratch allocation, precedes every write.

## Occurrence coverage

The mapping is a sorted sparse table keyed by full live or baked occurrence
identity, independent of interned spelling. Its lifetime follows the owner.

| Document occurrence | Resolved result |
| --- | --- |
| Named enum, structure or bit-structure definition | Type |
| Named association or interface definition | Non-storage declaration |
| Named interface member | Interface member |
| String association `target` or interface-member `type` | Referenced declaration, including built-ins |
| Explicit null association `target` | Zero |
| Named structure member, including a named component | Member |
| Enum label | Label, including each alias |
| Named bit field | Field |
| Inline array descriptor in `type` or recursive `element` | Array type |
| String `type`, string `element`, integer `storage` reference | Referenced type, including built-ins |
| Package/category objects, `members`, `values`, `detail` | Zero |
| Counts, masks, alignment, size, internal marker, interpretation | Zero |
| Defaults, their scalar/array children, enum default labels | Zero |
| Decoration objects and their string values | Zero; decoration queries expose original handles |
| `instances`, `data`, and all their descendants | Zero |

There are no resolved slots or mapping entries for ordinary array positions.
Equal references can map to the same type. Different inline array descriptors
have separate array records; no descriptor interning is assumed.

## Limits and diagnostics

Private POD vectors use the existing ambient framework allocator. Relationships
are indices and counted ranges. The current handle encoding allows up to
536,870,911 records in each kind; this is a checked implementation limit, not a
public encoding contract. Counts use 32 bits. All physical size, offset and
stride arithmetic uses checked unsigned 64-bit operations. Each described
allocation is limited to `memory::k_byte_size_ceiling` (0x80000000 bytes,
inclusive), including nested member ends, array products and tail padding.
The ceiling is independent of the target `size_t` range. Private
offsets/sizes/strides are unsigned 32-bit, with checks before narrowing; schema
strides are not restricted to the memory token/view's 16-bit field. Supplied
size/alignment detail must fit the ceiling and the layout rules below.
File positions are a separate future
domain. Recursive processing is bounded at 256 traversal
levels; extremely deep definitions/defaults receive `storage_limit`. C++ export
also bounds dependency traversal. Allocation failures require no allocation to
report, and never leave a usable partial result.

`SDiagnostic` contains the first terminal reason/stage, offending occurrence,
enclosing definition/member occurrences where available, and an optional related
occurrence. Locations use `CTypesHandle::is_valid()`, whose missing value is
distinct from schema index zero. Bit overlaps additionally report both half-open
bit ranges with `ranges_available`. Duplicate diagnostics identify both surviving
declarations. Diagnostics retain no document view; keep the caller's input
alive and use `CTypesDocumentQuery` to inspect names and paths after failure.
Live and baked handles belong to their originating document. Baked locations
promise no source line/column information.

Natural layout is supported. A supplied `detail.size` must match its computed
extent; `detail.internal` is an export marker. Optional increased
alignment must be a power of two no greater than 128 and no smaller than the
natural member alignment. An explicit-offset structure must provide an offset
for every member and `detail.size`. The resolver checks alignment, bounds, and
positive extent overlap, including padding owned by nested types. Zero-byte
members may sit at the declared size. Member records stay in declaration order;
their physical offsets may be nonmonotonic. A structure with only zero-byte
members remains size zero and alignment one. CResolvedTypes validates only package
shape and `types` in a combined document; it does not evaluate instance/bulk values.
Full CSchema validation also checks every contained data section.

Explicit-layout overlap checking uses a sorted sweep with a temporary ordinal
heap during resolution: O(n log n) work and O(n) scratch storage for n members.
The first conflicting declaration pair and its precedence relative to alignment
and bounds errors remain in declaration order. Named array declarations use sorted
name/ordinal pairs to find duplicates while retaining the first diagnostic in
declaration order.

## C++17 declarations and validation

`generate_cpp(resolved, namespace_name, output, diagnostic)` returns a complete
NUL-terminated UTF-8 `CByteBuffer`. The final NUL is excluded when writing source.
Output uses dependency order, enum classes, nested C arrays, an int8 `b8` alias,
the existing global `fp16data_t`, and typed bit-mask constants in a namespace per
bit structure. Data members representing bit structures use their storage type.
No constructors, functions, assertions or default member initialisers are emitted.
Natural structures whose compiler layout already matches use implicit C++
alignment. For other accepted layouts, the generator emits `alignas` when the
structure needs increased alignment, byte-array padding for gaps and tail space,
and fields in physical offset order. Synthetic padding names avoid collisions
with schema names. Physical-order emission reuses the resolved member index;
it no longer searches all members for each next field. Schema member order and
lookup remain in declaration order.
Compiler validation establishes that the target layout matches the resolved
schema. The internal marker does not suppress declarations or validation.

Generated C++ integers use decimal for values from -65535 through +65535,
and hexadecimal outside that range. Unsigned initialisers use `u`; signed
values retain a minus sign where needed. A signed suffix or boundary expression
is used only when needed to preserve the value, such as `-0x80000000ll` and
`(-0x7fffffffffffffff - 1)`. Array extents use the same value threshold without
suffixes. Masks always use full storage-width hexadecimal: `0x01u`, `0x0001u`,
`0x00000001u`, or `0x0000000000000001u` for unsigned storage. Signed masks omit
`u` when the high storage bit is clear; otherwise the complete unsigned pattern
is explicitly cast, for example `static_cast<std::int32_t>(0x80000000u)`.

Related single-line declarations form a group; each enum, structure and namespace
is a separate group. Groups have exactly one blank line between them. Namespaces
have inner blank lines unless their entire body is one group of single-line
declarations, as with mask constants or the alias-only empty schema.

These are C++ source spelling rules. Schema document output instead sets
`CIntegerMetadata` for the document writer, as described under
[integer presentation](#schema-document-output-and-integer-presentation).

The namespace is one identifier, not a qualified namespace expression. It must
use the ASCII subset of C++17 identifiers, as do schema declarations.
Keywords and implementation-reserved spellings are rejected in their emitted
scopes. Non-ASCII names receive `invalid_identifier`; no transliteration occurs.
This syntax boundary does not affect Unicode document text. The namespace must
not collide with the required global `std` namespace or `fp16data_t` class.
Standard integer types use `std::` without a routine leading `::`. Global
qualification is retained when a schema type or namespace named `std` hides the
standard namespace. Members and mask constants named `std` still allow ordinary
`std::` type spelling. Types in the generated namespace, including `b8` and array
element types, use unqualified names. A reference is qualified with that namespace
only when a member in the same structure shares its local type or alias name.
This includes references before, within and after the homonymous declaration,
so C++17 lookup does not depend on compiler-specific acceptance. The global half
type retains its `::fp16data_t` spelling. Failures
release all output. Output storage must not own the borrowed schema bytes.

`Schema_test_suite` writes generated headers and separate compiler-validation
translation units for the runtime, explicit layout, reviewed sample,
representation and name-collision fixtures
to the normal process/tag-qualified test-output directory. It prints each exact
source path. Compile each file with:

```powershell
.\tools\validate_schema_layout.ps1 -Source '<printed schema_layout.cpp path>'
```

The tool compiles independently for x86 and x64 with C++17 and warnings as errors.
Assertions compare resolved size, alignment, member offsets/sizes, array extents,
enum underlying types/values, mask types/values, standard layout and trivial
copyability, including `fp16data_t`. Support requires successful compiler fidelity
validation for the target ABI. The full authoring sample is a positive schema
resolution and C++ layout fixture. The role-loader tests exercise its instance
and bulk data sections separately; those remain outside the resolver's validation scope.

Record sizes are available through `record_sizes()` for review and are printed
by the suite. They are measurements, not a serialization format or promised ABI.

| Private record | Measured bytes, MSVC x86 and x64 |
| --- | ---: |
| Type | 48 |
| Structure member | 40 |
| Enum label | 40 |
| Bit field | 48 |
| Explicit default node | 48 |
| Occurrence mapping pair | 24 |

The owner stores twelve primitive records plus one physical record per named
physical definition and inline array descriptor. Associations/interfaces and their
members have separate non-storage tables and four-byte name-order entries. These
tables carry checked relationships and member counts without physical layout.
Only explicit supplied defaults allocate default
nodes. Named lookup resolves the string through the document's existing intern
table, then searches an ordinal permutation using document-local name IDs.
Types, members and fields need four index bytes each; enum labels need eight
for separate name and numeric-value orders. Numeric lookup preserves the first
declared alias. Occurrence lookup also uses binary search. No fixed private slot
layout is exposed as an ABI.

The type record contains a 16-byte source occurrence, six 32-bit words (size,
stride, related type, first child, count, name ID), and four bytes (category,
physical primitive, log2 alignment, packed flags/state). The three flags occupy
bits 0-2 and resolution state bits 3-4. Alignment decoding
uses an unsigned shift; exponent zero means one, and exponents above 31 are
invalid. The member record contains a 16-byte source occurrence and five 32-bit
words (offset, full byte size, type, default index, name ID).
Actual layout assertions are compiled on x86/x64.

Internal named lookups validate the starting schema/type once, then use transient
typed 8-byte first/count ranges over existing vectors. Records are accessed
directly without repeated parent decoding or copied public observations. No
range table or persistent allocation pointer is added. Operation access expires
on clear, move, re-resolution or owner destruction; public checked inspection
continues to reject invalid inputs.

Schema switching inherits staged types from parent records rather than walking
each specialisation's ancestors. Consecutive base entries can reuse the current
group's type. Promotion and explicit value import retain temporary group mappings;
their lookup cursors check the current and adjacent entries before binary search.
Interleaved group access therefore remains bounded.

Named value construction, checking and live selection editing share a temporary
selection cursor. Objects already in schema order need no scratch allocation;
shuffled objects use a sorted index of supplied selections. Both paths retain
schema-order validation and defaults, without repeated object-child scans.
Allocation failures remain distinct from invalid declarations and preserve the
caller's existing role state. Remap planning visits source members and uses the
destination's name index, sorting matches to preserve diagnostic order.
