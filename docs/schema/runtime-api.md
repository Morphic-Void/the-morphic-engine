# Resolved schema API

`core/schema/resolved_schema.hpp` provides `schema::CResolvedSchema` and
`schema::generate_cpp`. Resolution consumes a validated `CBakedDocument`;
it does not parse text, construct instances, or modify the backing document.
For text input, require `document_parser::parse(...).accepted()` under a policy
that excludes `EDocumentFinding::name_collision_extension`, then bake the live
document. The ordinary parser policy already excludes collision extension.

```cpp
schema::CResolvedSchema resolved;
schema::SDiagnostic diagnostic;
if (!resolved.resolve(block.document(), diagnostic))
{
    // Format diagnostic using block.document(); the resolved owner is empty.
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
document bytes; the source view object may disappear. Every resolution attempt
clears the previous resolution immediately. Failure releases every owned table
and clears the binding. Moving transfers indices and tables and leaves the
source empty. Indices from a replaced resolution must not be reused; they have
no generation or owner identity. `clear()` releases the resolution.

All schema handles are distinct 32-bit `CSchemaIndex` values; zero is invalid.
Callers must not decode their numeric values. Named lookup misses and unmapped
occurrences return zero. Inspection methods return false for wrong record kinds,
wrong categories, absent indices or an unready owner, leaving the output value
unchanged. `SType.count` is the category's member, label, field or element count.
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

The mapping is a sorted sparse table keyed by baked value occurrence, independent
of interned spelling. Its lifetime follows the owner.

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
occurrence. Locations use `CBakedValueIndex::is_valid()`, whose missing sentinel
is distinct from schema zero. Bit overlaps additionally report both half-open
bit ranges with `ranges_available`. Duplicate diagnostics identify both surviving
declarations. Diagnostics retain no document view; keep the caller's input
alive to format names/paths after failure. Baked locations promise no source
line/column information.

Natural layout is supported. Matching `detail.alignment` and `detail.size` are
checked metadata; `detail.internal` is an export marker. Explicit member offsets
and increased alignment fail with `unsupported_feature`. Only package shape and
`types` are validated in a combined document; `instances` and `data` values are
deliberately not evaluated.

## C++17 declarations and validation

`generate_cpp(resolved, namespace_name, output, diagnostic)` returns a complete
NUL-terminated UTF-8 `CByteBuffer`. The final NUL is excluded when writing source.
Output uses dependency order, enum classes, nested C arrays, an int8 `b8` alias,
the existing global `fp16data_t`, and typed bit-mask constants in a namespace per
bit structure. Data members representing bit structures use their storage type.
No constructors, functions, assertions or default member initialisers are emitted.
Structures and members use implicit native C++ alignment, without `alignas`
annotations. Compiler validation must establish that the target's native layout
matches the resolved schema. The internal marker does not suppress declarations
or validation.

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
translation units for the runtime, representation and name-collision fixtures
to the normal process/tag-qualified test-output directory. It prints each exact
source path. Compile each file with:

```powershell
.\tools\validate_schema_layout.ps1 -Source '<printed schema_layout.cpp path>'
```

The tool compiles independently for x86 and x64 with C++17 and warnings as errors.
Assertions compare resolved size, alignment, member offsets/sizes, array extents,
enum underlying types/values, mask types/values, standard layout and trivial
copyability, including `fp16data_t`. Support requires successful compiler fidelity
validation for the target ABI; an ABI whose native layout differs remains
unsupported pending separate handling. Explicit layout is still deferred.
The full design sample remains an unsupported-layout fixture;
the runtime suite's natural-layout catalogue supplies positive compiler checks.

Record sizes are available through `record_sizes()` for review and are printed
by the suite. They are measurements, not a serialization format or promised ABI.

| Private record | Measured bytes, MSVC x86 and x64 |
| --- | ---: |
| Type | 32 |
| Structure member | 24 |
| Enum label | 24 |
| Bit field | 32 |
| Explicit default node | 32 |
| Occurrence mapping pair | 8 |

The owner stores twelve primitive records plus one record per named definition
and inline array descriptor. Only explicit supplied defaults allocate default
nodes. Named lookup currently scans the relevant ordered range; occurrence
lookup uses binary search. No fixed private slot layout is exposed as an ABI.

The type record contains seven 32-bit words (size, stride, related type,
first child, count, name ID, source occurrence) and four bytes (category,
physical primitive, log2 alignment, packed flags/state). Alignment decoding
uses an unsigned shift; exponent zero means one, and exponents above 31 are
invalid. The member record contains six 32-bit words (offset, full byte size,
type, default index, name ID, source occurrence). Other record sizes do not grow.
Actual layout assertions are compiled on x86/x64.

Internal named lookups validate the starting schema/type once, then use transient
typed 8-byte first/count ranges over existing vectors. Records are accessed
directly without repeated parent decoding or copied public observations. No
range table or persistent allocation pointer is added. Operation access expires
on clear, move, re-resolution or owner destruction; public checked inspection
continues to reject invalid inputs.

Future instance operations select individual named instances: document values
are read/converted through the schema into caller storage; given matching physical
representation, binary values use contiguous copies after buffer bounds checks.
Bulk data
follows the same model. Definitions and instances can share or use separate
documents. There is no application-wide population wrapper, and CSV is not a
planned core encoding (a future review export remains possible). These are
documented boundaries, not implemented instance, normalisation, remap or I/O APIs.
