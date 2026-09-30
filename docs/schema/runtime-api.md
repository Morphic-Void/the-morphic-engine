# Resolved schema API

This page describes the implemented resolver and generator introduced in
`5313c85` and extended with live/baked document queries. The [design](design.md)
defines the target system;
the [implementation contract](implementation-contract.md#staged-implementation-plan)
tracks delivery. Schema wrappers and their promotion/demotion are implemented;
instance/bulk wrappers remain later work. Its lifetime, handle
and default rules below describe current code.

`schema::CSchemaDocumentQuery` exposes read-only tree, name and scalar queries
through `CSchemaHandle` occurrences. Its internal adapter reads either a live or
baked document. A baked query copies the non-owning view, while a live query
borrows its document. Schema, instance and bulk role-handle types are distinct.

## Schema wrappers and client bindings

`schema::CBakedSchema` copies a non-owning `CBakedDocument` view. The caller keeps
the immutable block bytes at stable addresses while the wrapper or its clients
use them, including when a host also has a mutable alias. `schema::CLiveSchema`
owns a `CLiveDocument` privately. Both wrappers expose `document_query()` over
the original physical root, `types_root()` for the schema section, and a nullable
`const CResolvedSchema*` through `resolved()`. `document_ready()` distinguishes
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

`CBakedSchema::promote()` creates a separate live schema from the source's
`types` section, resolving the new copy before publication. The baked source may
be unresolved; its document and client bindings are preserved. Promotion requires
an empty destination; failure leaves it unchanged. A resolver failure reports
its reason and stage,
but clears occurrence handles and ranges because its temporary live document is
discarded. Translation failure reports `translation_failed`; invalid source or
occupied destination reports `invalid_input`.

`CLiveSchema::demote()` creates a separately owned `CBakedDocumentBlock` and an
unresolved `CBakedSchema` view over it. The source need only have a ready document,
so an incomplete editable schema can be saved without resolving it. Both outputs
must be empty. Keep the block's backing allocation alive at a stable address
while the baked wrapper or its clients use it; the owner object can move or be
transferred. Both conversions retain only the `types` root member;
`instances` and `data` in a combined source are untouched and are absent from
the new document. Neither conversion carries client bindings into the result.

`CLiveSchema::edit()` returns a borrowed editor for the current owner. It can
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

`CSchemaBinding` is a move-only link held inside a client. Binding requires a
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

`core/schema/resolved_schema.hpp` provides `schema::CResolvedSchema` and
`schema::generate_cpp`. Resolution consumes a validated `CLiveDocument` or
`CBakedDocument`;
it does not parse text, construct instances, or modify the backing document.
For text input, require `document_parser::parse(...).accepted()` under a policy
that excludes `EDocumentFinding::name_collision_extension`. The ordinary parser
policy already excludes collision extension. Baking is optional for resolution.

```cpp
schema::CResolvedSchema resolved;
schema::SDiagnostic diagnostic;
if (!resolved.resolve(block.document(), diagnostic))
{
    // Query the occurrence using CSchemaDocumentQuery{ block.document() }.
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

`resolve(query, diagnostic)` accepts an existing `CSchemaDocumentQuery`.
`resolve(diagnostic)` uses the retained input for an unchanged re-resolution.
Every resolution attempt clears the previous tables immediately. A successful
unchanged re-resolution preserves existing `CSchemaIndex` meanings. Changed
definitions can change those meanings, and failure or `clear()` invalidates
them. Failure releases every owned table and clears the input binding, so an
unready owner cannot re-resolve without a new input. Moving transfers the
binding, indices and tables and leaves the source empty. Indices have no
generation or owner identity.

Resolved schema record indices are 32-bit `CSchemaIndex` values; zero is invalid.
Document occurrences use `CSchemaHandle`, which preserves the full live node key
or baked value index and cannot be constructed from a raw key by callers. Both
handle types are distinct. Callers must not decode their numeric values. Named
lookup misses and unmapped occurrences return zero. Inspection methods return
false for wrong record kinds, wrong categories, absent indices or an unready
owner, leaving the output value unchanged. `SType.count` is the category's
member, label, field or element count.
`element_or_storage` is meaningful for arrays, enums and bit structures.
`SType.stride` is element stride for an array, and the type's size otherwise.
Built-ins have no declaration occurrence or document name ID.

`SType.primitive` directly describes the physical primitive for primitive, enum
and bit-structure types. The semantic category and `element_or_storage` reference
are retained. Structures and arrays report `none` for the whole object.
`SField.primitive` describes the logical field's primitive (including enum
underlying storage), independently of the containing bit-storage word.
`SMember.size` is the complete resolved member-type extent, including owned
padding. Public size/offset/alignment/stride observations remain 64-bit.

Definitions are enumerated in document order, including category-group order.
Members, labels and bit fields retain their declared order. Name-based and
ordinal access select the same records. `first_label_for_value` accepts exact
signed/unsigned integer values and returns the first alias in declaration order.
`named_components` identifies structures whose members all use the same
non-array primitive type. Layout observations always come from stored resolved
facts: size, alignment, offsets, strides and recursive gap presence.

An empty structure uses `members: []`, resolves with size and stride zero and
alignment one, and retains a queryable type index and document mapping. A
structure whose members are all empty is also empty. Its zero-byte members keep
their declaration order, names, indices, mappings and natural cursor offsets.
Arrays of empty elements require a positive count and have zero size and stride.
Generated C++ omits empty type definitions and zero-byte members; its physical
declarations therefore need not list every queryable member. Empty structures
accept canonical `detail.size: 0` and `detail.alignment: 1`; metadata requesting
storage or stronger alignment is invalid.

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
Explicit `unorm` field defaults are floating values in [0,1]; interpretation
remains on the field. Its implicit numeric zero has the same logical meaning.
Quantisation and instance construction are outside this API.

## Occurrence coverage

The mapping is a sorted sparse table keyed by full live or baked occurrence
identity, independent of interned spelling. Its lifetime follows the owner.

| Document occurrence | Resolved result |
| --- | --- |
| Named enum, structure or bit-structure definition | Type |
| Named structure member, including a named component | Member |
| Enum label | Label, including each alias |
| Named bit field | Field |
| Inline array descriptor in `type` or recursive `element` | Array type |
| String `type`, string `element`, integer `storage` reference | Referenced type, including built-ins |
| Package/category objects, `members`, `values`, `detail` | Zero |
| Counts, masks, alignment, size, internal marker, interpretation | Zero |
| Defaults, their scalar/array children, enum default labels | Zero |
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
This deliberately tightens the former `SIZE_MAX`-only acceptance. Private
offsets/sizes/strides are unsigned 32-bit, with checks before narrowing; schema
strides are not restricted to the memory token/view's 16-bit field. Supplied
size/alignment detail must fit the ceiling, while natural-layout and unsupported
explicit-layout rules remain unchanged. File positions are a separate future
domain. Recursive processing is bounded at 256 traversal
levels; extremely deep definitions/defaults receive `storage_limit`. Export
also bounds dependency traversal. Allocation failures require no allocation to
report, and never leave a usable partial result.

`SDiagnostic` contains the first terminal reason/stage, offending occurrence,
enclosing definition/member occurrences where available, and an optional related
occurrence. Locations use `CSchemaHandle::is_valid()`, whose missing value is
distinct from schema index zero. Bit overlaps additionally report both half-open
bit ranges with `ranges_available`. Duplicate diagnostics identify both surviving
declarations. Diagnostics retain no document view; keep the caller's input
alive and use `CSchemaDocumentQuery` to inspect names and paths after failure.
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
members remains size zero and alignment one. Only package shape and
`types` are validated in a combined document; `instances` and `data` values are
deliberately not evaluated.

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
with schema names. Schema member order and lookup remain in declaration order.
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

These are C++ source spelling rules. Future schema-document output uses the
existing `CIntegerMetadata` domain, notation and prefix flags with the document
writer. Ordinary 8/16-bit schema integers use decimal; 32/64-bit schema integers
use hexadecimal according to their declared type, even for small values. Counts,
offsets, sizes and alignments use the value threshold; masks use full storage-width
hexadecimal without C++ suffixes. The generic document writer preserves numeric
intent but does not preserve leading-zero display padding. No schema-document
normalisation/output API is provided by this delivery.

The namespace is one identifier, not a qualified namespace expression. It must
use the initial ASCII subset of C++17 identifiers, as do schema declarations.
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
validation for the target ABI. The full design sample is a positive schema
resolution and C++ layout fixture; its instance and bulk content is still outside
this resolver's validation scope.

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

The owner stores twelve primitive records plus one record per named definition
and inline array descriptor. Only explicit supplied defaults allocate default
nodes. Named lookup currently scans the relevant ordered range; occurrence
lookup uses binary search. No fixed private slot layout is exposed as an ABI.

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
