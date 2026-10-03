# Schema runtime API

This page describes the implemented resolver and generator introduced in
`5313c85` and extended with live/baked document queries. The [design](design.md)
defines the target system;
the [implementation contract](implementation-contract.md#staged-implementation-plan)
tracks delivery. Schema wrappers, their promotion/baking, and the baked and
live instance and bulk roles are implemented. The bounded direct-member remap
core works with their entry views, including mutable live destinations.
The lifetime, handle and default rules below describe current code.

`schema::CSchemaDocumentQuery` exposes read-only tree, name and scalar queries
through `CSchemaHandle` occurrences. Its internal adapter reads either a live or
baked document. A baked query copies the non-owning view, while a live query
borrows its document. `CInstanceDocumentQuery` and `CBulkDocumentQuery` offer the
same original-root read surface with their distinct role handles. Schema,
instance and bulk role-handle types are distinct.

## Header organisation

Include the header for the role or operation being used:

| Header in `core/schema/` | Responsibility |
| --- | --- |
| `schema_wrappers.hpp` | Live/baked schema ownership, editing and client bindings. |
| `resolved_schema.hpp` | Resolved layouts, defaults, diagnostics and C++ generation. |
| `baked_instances.hpp`, `live_instances.hpp` | Instance loading, observation and live editing/output. |
| `baked_bulk_data.hpp`, `live_bulk_data.hpp` | Bulk loading, observation and live capture/output. |
| `data_remap.hpp` | Remap planning and execution between binary views. |
| `unused_storage.hpp` | Explicit clearing of unused binary storage. |

`document_query.hpp` supplies the shared public query/handle interfaces, and
`data_output.hpp` supplies output policy. Role headers include their required
support; live data headers also include the corresponding baked header for shared
diagnostics and views.

`record_index.hpp`, `type_compatibility.hpp`, `value_codec.hpp` and
`value_conversion.hpp` are implementation support. Baked role headers include
`record_index.hpp` for their private index storage. The compatibility hash index
belongs to `type_compatibility.hpp`; it is separate from record lookup.
Consumers normally reach these details through the relevant public header.

General document copying lives in `core/data_model/document_copy.hpp`; include it
for detached subtree copies. `document_translation.hpp` exposes whole-document
and root-member promotion/baking. Subtree copying and promotion retain distinct
traversal policies while sharing shallow value construction.

Implementation sections group lifecycle, queries, editing, reconciliation and
output. Instance selection decoding and reconciliation use stack-local operation
objects to hold fixed traversal inputs; they add no persistent storage or heap
allocation.

The live instance implementation has three parts: `live_instances.cpp` owns
lifetime, bindings, queries and creation; `live_instances_edit.cpp` owns capture,
selection editing and descendant propagation; `live_instances_translation.cpp`
owns binary-authoritative declaration reconstruction, promotion, reconciliation
and baking. Creation and translation share private append/locator helpers on
`CLiveInstances`. Editing and translation use the shared scalar decoder with
their respective diagnostic policies. This split adds no public header.

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

`CLiveSchema::bake()` creates a separately owned `CBakedDocumentBlock` and an
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

## Internal value construction (stage 4a)

`core/schema/value_codec.hpp` exposes synchronous internal operations over a
ready `CResolvedSchema` and a representation-neutral `detail::CDocumentRead`:

```cpp
detail::construct_value(schema, document, type, declaration, destination,
    destination_size, detail::EConstructionMode::instance, diagnostic);
detail::construct_alternative(schema, document, type, declaration, base,
    base_size, destination, destination_size, diagnostic);
```

`complete_bulk` mode requires every recursively addressable member, bit field
and array position. Instance mode fills omitted values from schema defaults;
short positional input uses defaults, including an explicit array-default
prefix. An alternative copies its immediate parent's complete bytes into a
separate destination, then changes selected values. Positional input selects
its supplied prefix at every nesting level; omitted positions inherit. Named
input selects members or fields. Explicitly supplied values remain selections
even when equal to the parent. Neither operation changes the parent.

A structure with exactly one primitive or enum member also accepts a scalar
value as shorthand for selecting that member. The same rule applies to a bit
structure with one scalar field. For `Value { a: u8 }`, `1`, `[1]` and `{"a":1}`
are equivalent initialisers. This applies to roots, nested members, array
elements, instance selections and bulk records. It does not recursively unwrap
an array or compound member, and multi-member or empty compounds still require
aggregate input. Null remains invalid; scalar range, enum-label and bitfield
interpretation rules are unchanged. Capture and embedded output may emit the
equivalent explicit aggregate form while retaining the same selected member.

The internal `construct_value` singleton-element flag identifies a named
compound member unwrapped by the parser at an outer record-array position.
Bulk callers supply this context explicitly; names on primitive array elements
remain invalid. Named singleton records still undergo complete-record checks.

Both byte spans are bounded separately and must cover the resolved type size
and alignment. Positive overlap is rejected before copying. Zero-byte types
accept null, zero-length spans, while still validating supplied declarations.
Stored scalars and bit words are little-endian. Construction writes logical
values without clearing structure/array gaps. A newly constructed bit word is
initialised as a unit; an alternative retains all inherited bytes and bits
outside selected masks. On conversion failure the destination may be partially
changed and must be discarded; the source stays unchanged. The codec neither
allocates nor binds a schema. Role wrappers check their schema
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
and bulk erasure preserves the order of the remaining records. Promotion and
reconciliation create fresh entry keys in record order, even when document
groups or specialisation branches are interleaved. Whole-owner transfers
preserve that order. Future mutation paths must preserve this invariant.

Neither role performs lazy construction or allocation during entry lookup or
instance hierarchy navigation. Name lookup remains a separate search; rendering
consumers can resolve names during preparation and retain entry handles or
bounded payload views under the existing lifetime rules. A bulk collection
contributes one record-table entry regardless of its payload record count.

Live bulk records retain their resolved type index, so access by handle does
not repeat a schema type-name search. Binding availability is checked on each
operation. Bound schemas cannot be edited or replaced; successful unchanged
re-resolution preserves indices, while failed resolution makes access unavailable
until a successful retry. Promotion resolves indices in the destination schema.
Document and payload formats are unchanged.

Promotion/output and remap setup share an operation-local resolved-type
comparison context. Representation compatibility ignores defaults and the
`internal` marker; remapping definition matching also compares those properties
within the candidate member types. The enclosing member's own default is not
an additional remap matching condition. Hashed comparison keys retain both type
identities and, for definition matching, both default descriptions. Completed
matches and proven root mismatches can be reused within the operation; unfinished
comparisons are discarded on failure. No cache survives schema re-resolution.

Scalar decoding is shared by bulk output and instance capture, editing and
reconciliation. The codec also shares little-endian scalar bit access with
construction and comparison. Role-specific traversal and diagnostic mapping stay
with their callers, including the checks that generated literals reproduce
meaningful encoded values. This adds no decoding to stripped baking or baked
entry access.

Instance creation and editing use the data-model `document_translation::copy_subtree`
helper for live or baked sources, including copying within one live document.
It retains integer metadata, string newline-escaping flags, names and child order,
and removes partial copies on failure. The same helper group stabilises names
before document growth. Subtree copying and whole-document/selected-member
promotion share value construction and metadata handling through compile-time
source access. The subtree walk retains its 256-level depth bound and incremental
growth; promotion retains its iterative walk, node reservation and staged
publication. Baked access gains no additional storage or work.

## Baked bulk role (stage 4b)

`CBakedBulkData` attaches to an immutable `CBakedDocument` view or a
`CMutableBakedDocument` view. Both borrow their block bytes. Bind a resolved
`CBakedSchema` or `CLiveSchema` separately with `bind_schema()`. The schema
owner invalidates the binding on destruction; `loaded_ready()` then becomes
false. `clear()` releases the binding, records and payload view. The wrapper
never owns the external payload. `document_query()` reads the original root;
`data_root()` selects its bulk section. `find_entry()` and `entry()` expose
validated type, count, offset, stride, byte extent and borrowed byte pointer.

Each `data.<type>.<name>` entry is an object with a required `locator` and
optional embedded `data` array. The locator reserves unsigned 32-bit `offset`
and boolean `valid`; optional `count` and `size` must agree with the embedded
length and resolved type size. A positive count is required. Without embedded
data, count may be inferred from nonzero type size and supplied size; a zero-byte
type without embedded data needs an explicit count. Record stride is the resolved type size.
Outer array positions are unnamed, and embedded materialisation requires
complete values at every nested level.

`load_supplied(payload, compare_embedded, diagnostic)` validates the locators,
then borrows an existing payload. Nonempty payloads need a physically
128-byte-aligned base. Valid extents must be in document order, nonoverlapping,
within the supplied view and aligned for their type. With comparison enabled,
each embedded record is encoded and compared over addressable fields. Padding
and unused bit fields are ignored; distinct raw codes and signed zero remain
distinct, while all NaN encodings compare equal. With comparison disabled, the
entry and locator structure is still checked without encoding values.

`materialise(returned_owner, diagnostic)` requires embedded data for every
entry and an unallocated output `CByteBuffer`. It plans extents, allocates an
aligned payload and constructs complete records before updating any reserved
locator. Unset offsets are computed in document order and published through a
mutable document view with `valid: true`; a read-only view cannot publish them.
The caller receives the owner, while the wrapper keeps a non-owning view. A
zero-byte-only payload can use the canonical empty view. Moving the owner keeps
the payload address stable; destroying or reallocating it invalidates the view.
`SBulkDiagnostic` reports the first failure reason and a bulk occurrence when
available. Failure leaves the role unready.

## Live bulk role (stage 5a)

`CLiveBulkData` owns its live document and one 128-byte-aligned working payload.
`initialise()` takes a resolved baked or live schema explicitly and creates an
empty `data` section. `CBakedBulkData::promote(destination, schema, diagnostic)`
requires a loaded, usable source and an empty destination. It copies only the
bulk section into an independent live document and copies the complete associated
payload into independent storage. Existing embedded record arrays are regenerated
from binary; ordinary reference-only entries remain references. Stripped input
instead regenerates complete arrays, as described under [stripped data output](#stripped-data-output).
Each entry retains explicit
`count`, `size`, valid offset, type group and name. The binary
bytes, including padding, are authoritative and are never reconstructed from
embedded records during promotion. The source document, payload and connections
are unchanged. Promotion checks referenced types against the explicitly supplied
schema by recursive structure and value interpretation: names, member and label
order, physical layout, primitive and array types, enum values, and bit fields
must agree. Defaults and unused definitions may differ. An incompatible schema
reports `incompatible_schema` without publishing a destination.

`reconcile(diagnostic)` explicitly regenerates complete embedded records for all
live entries, including references, after raw writes or remapping. It stages a
replacement document and publishes only on success. Payload allocation, offsets,
bytes and schema binding remain unchanged; all document handles must be reacquired
after success. Failure preserves the existing document and handles. Values that
cannot be expressed faithfully report `unrepresentable_value`; record expansions
that cannot fit the document's node/storage limits report `invalid_range`.
Callers must initialise unpopulated storage before reconciling it.

`document_query()` reads the live document's original root through `CBulkHandle`
occurrences. `data_root()`, `find_entry()` and `entry()` mirror the baked role;
`mutable_entry()` returns matching metadata and a bounded `CByteView`. Zero-byte
entries have a canonical empty view and may have positive or zero record counts.
Both schemas' binding lifetimes follow the existing intrusive-link rules: schema
destruction or failed unchanged re-resolution disables entry access. A
successful unchanged retry restores access after failed re-resolution; schema
destruction detaches the binding and requires an explicit new association.
Document queries remain safe during
schema unavailability. Views borrow the wrapper; document views may expire after
document mutation and binary views may expire after buffer growth. A retained
entry handle remains meaningful while its entry node survives.

`create_records(type, name, source_query, records_array, diagnostic)` accepts a
record array in a baked or live `CBulkDocumentQuery` and constructs every record
in `complete_bulk` mode. Named singleton outer elements are interpreted as
compound members, consistently with nested arrays. Every declared
member and array element must be supplied, including zero-byte types; schema
defaults do not fill omissions. It rejects a
duplicate name in the type group. `capture(type, name, source, count, diagnostic)`
copies bounded raw bytes with the resolved type's physical alignment; it creates
or replaces a named array without interpreting values or applying defaults.
Successful replacement removes any existing embedded `data` array, including
one produced by promotion or reconciliation; the entry handle remains valid.
Reconciliation or embedded output can regenerate records from the new bytes.
The caller guarantees compatible little-endian physical representation. It is
safe to capture from the wrapper's own payload, including when growth moves the
buffer. `create_unpopulated(type, name, count, diagnostic)` rejects duplicates,
allocates an aligned extent without applying defaults or promising zero fill,
and permits count zero. The caller must populate required fields before reading
or exporting such an entry. Neither promotion nor later raw capture substitutes
destination-schema defaults for authoritative bytes.

For `Value { a: u8 }`, `[1,2]`, `[[1],[2]]` and `[{"a":1},{"a":2}]` each
supply two complete records. The outer array is the bulk collection; each
element initialises one record. A stated locator count must match that array's
length. A scalar cannot initialise a multi-member record, and a named singleton
record cannot omit other required members.

Larger replacements append an aligned extent and leave old bytes unreferenced;
same-size or smaller replacements may reuse the old extent. A zero-extent
replacement uses offset zero. `rename_entry()` checks sibling collisions and
preserves the entry handle; `erase_entry()` invalidates its handle without
compacting the payload. Failed construction or append publishes no failed locator
and leaves earlier entries usable. A post-mutation failure that cannot preserve
that state disables the live role and reports a critical event. `clear()`
releases its document, payload and schema link. Entry views also provide the
bounded storage used by the implemented remapping operations below.

## Bulk output and baking (stage 6a)

`CLiveBulkData::prepare_output(document, payload, destination_schema, form,
diagnostic)` creates a separate live document and 128-byte-aligned packed payload.
The destination schema is an explicitly resolved baked or live schema. Referenced
types must match the source in structure and value interpretation; defaults may
differ. Both destinations must be empty. `EDataOutputForm::embedded` adds complete
record arrays decoded from binary; `external` omits them. Both forms retain valid
reserved locators with positive counts. Text versus baked document encoding is a
separate caller choice. An entirely empty bulk collection is valid, while a named
zero-count entry is not output-ready.

Packing traverses the live document rather than record creation history, aligns
each nonempty extent to its resolved type and rewrites the output offsets. It
copies complete record bytes, including internal padding, and drops unreferenced
working extents without clearing newly created gaps. Zero-byte entries retain
their positive count and use offset zero, including between nonempty entries.
The output payload is independent of the source even in embedded form; a caller
writing embedded-only text need not save it as a sidecar.

Embedded records are constructed in the existing complete-value grammar and
checked by re-encoding them against the source under `compare_encoded` rules:
NaN encodings compare equal, while signed zero and distinct normalised integer
codes remain distinct; padding and unused bits are ignored. An unlabelled enum
code or noncanonical Boolean byte reports `unrepresentable_value` without
publishing output. One-member structures and bit structures in arrays continue
to use their explicit positional form; readers also accept the named and
eligible scalar forms described above.
External form retains the authoritative binary bytes without decoding them.

`bake(block, payload, role, destination_schema, form, diagnostic)` prepares
the output, bakes its document and returns a loaded `CBakedBulkData` role together
with separate document-block and payload owners. The role borrows both owners;
moving either owner preserves its allocation address and views, while destroying
or reallocating one invalidates the corresponding view. The source and all
destinations remain unchanged on failure. On success, the new role contributes
one binding to the explicit destination schema. Reload external output with
`load_supplied` and its associated payload; reload embedded-only output with
`materialise` after parsing and baking the document. The caller still ensures
unpopulated live arrays have all required addressable fields filled before output.

## Baked instance role (stage 4c)

`CBakedInstances` attaches a borrowed immutable or mutable baked document view
and separately binds a resolved `CBakedSchema` or `CLiveSchema`. One wrapper
loads the complete `instances` section into one associated payload; nested base
branches are naming scopes within that role. `CInstanceDocumentQuery` exposes
the original physical root, while `instances_root()` selects the role section.
`find_base(type, name)` selects a named base. `find_specialisation(parent, name)`,
`first_specialisation()`, `next_specialisation()` and `parent_instance()` navigate
the validated hierarchy. `entry()` exposes the type, immediate parent, retained
declaration handle, offset, byte extent and borrowed complete snapshot.

Each base and specialisation object reserves a `locator` with unsigned 32-bit
`offset` and Boolean `valid`. Optional `count` must be one; optional `size`
must equal the resolved type size, including zero. Optional `declaration` holds
the supplied values or selected modifications. Its absence means no selected
values: a base uses schema defaults and a specialisation inherits its complete
immediate parent unchanged. Explicit null is invalid. `specialisation` holds
named child objects, and every child inherits its enclosing instance's type.

`load_supplied(payload, compare_embedded, diagnostic)` borrows a physically
128-byte-aligned nonempty payload. Valid baked locators must describe extents
in parent-before-child depth-first hierarchy order, independent of the order
of `locator`, `declaration` and `specialisation` properties. Extents require
type alignment, no overlap and checked bounds. With
comparison disabled, the supplied complete snapshots are authoritative and
declarations are not converted. With comparison enabled, the loader constructs
base and descendant snapshots in separate scratch storage, parent before child,
then compares addressable encoded fields against the supplied payload. Expected
children never use supplied parent bytes. Comparison follows the encoded rules
described for the bulk role.

`materialise(returned_owner, diagnostic)` takes an unallocated output buffer,
constructs each base with defaults and each child as an independent alternative
from its completed immediate parent, then publishes unset reserved locators
through a mutable view. It returns the payload owner separately and borrows its
stable allocation. A zero-byte-only role may use an empty payload and null
entry byte pointers. `clear()` releases the binding and views. Schema loss or
failed re-resolution makes loaded entry access unusable while document queries
remain available. Load failure leaves this role unready and does not invalidate
the schema or another role sharing the physical block.

## Live instance role (stage 5b)

`CLiveInstances` owns a live `instances` document, a schema binding and one
128-byte-aligned payload of independent complete snapshots. `initialise(schema)`
creates an empty role against an explicitly resolved baked or live schema.
`CBakedInstances::promote(destination, schema, diagnostic)` requires a loaded
source and empty destination. It copies the instance section and the entire
associated payload into independent storage without rebuilding binary values
from declarations. The promoted document retains names and hierarchy, regenerates
declarations from those snapshots, and drops
legacy `locator.count`. Promotion compares the structure and value interpretation
of all declared type groups, including empty groups and built-in primitive groups,
with the destination schema; unrelated definitions may differ. Incompatible
promotion leaves both roles unchanged and reports `incompatible_schema`.

Different defaults are allowed. Reconciliation compares each base snapshot with
destination-schema defaults, and each specialisation with its immediate parent's
snapshot. Named values equal to that baseline are omitted; differences are
explicit. Arrays retain the shortest prefix through their last differing element,
including necessary equal scalar elements before it. Nested compounds follow
the same rules. Equal overrides and empty selections are removed regardless of
their old spelling. No snapshot is changed or propagated to descendants.
This applies to promotion, retained-value preparation/baking and both schema roles.
Old declaration values are not decoded or checked for agreement. Generated
declarations must reproduce meaningful encoded values or the operation fails
atomically with `unrepresentable_value`.

`reconcile(diagnostic)` applies these rules explicitly to the live role after
raw writes or remapping, using its bound schema. It stages the whole document;
success invalidates all document handles, which must be reacquired by name.
Payload views, offsets, bytes and schema binding remain unchanged. Failure leaves
the document, handles and payload unchanged. Repeated reconciliation without
binary changes produces the same declarations.

The live role has the baked role's `document_query()`, `instances_root()`,
`find_base()`, `find_specialisation()`, `first_specialisation()`,
`next_specialisation()`, `parent_instance()` and `entry()` observations.
`payload_view()` borrows the owner. Entry handles remain valid while their
nodes survive; payload pointers can change when the buffer grows. The live
document query stays available if the schema binding becomes unusable. Failed
unchanged re-resolution gates entry access until a successful retry. Destroying
the schema detaches the binding permanently; the role must be cleared and
initialised again with a schema.

`create_base(type, name, source_query, declaration, diagnostic)` constructs a
complete base from a declaration in a baked or live instance query; omitted
values use schema defaults. `create_specialisation(parent, name, source_query,
declaration, diagnostic)` starts with the immediate parent's complete snapshot
and applies only explicit selections. A missing declaration means no
selections. Both operations retain the declaration, reject duplicate sibling
names and append one independently addressed extent.

`capture_base(type, name, complete, diagnostic)` creates or replaces a base
from aligned physical bytes and emits a complete canonical declaration.
`capture_specialisation(instance, complete, diagnostic)` copies only the
parts selected by that specialisation's retained declaration; unselected
bytes, padding and bits stay as they were in the target snapshot. Binary bytes
remain authoritative, including floating NaN payloads and unused bitfield
bits. Unknown captured enum codes fail because declarations use
labels; aliases use the first declared label. Captured NaN declarations use
the canonical `nan` spelling. Both capture operations update descendants
parent before child: each descendant inherits its updated immediate parent's
complete bytes, including padding and unused bits,
then its previously selected binary values are overlaid and its declaration
is synchronised to those values. These authored edit operations retain explicit
equal selections and empty aggregate selections until reconciliation or conversion
derives the declarations afresh from snapshots.

`set_selection(instance, steps, count, source_query, value, diagnostic)` and
`remove_selection(instance, steps, count, diagnostic)` edit a specialisation.
Each step names a structure member or bitfield, or gives a fixed-array index.
An empty set path replaces the whole declaration. A nested edit retains other
selected binary values even when their old declaration text disagrees with
the snapshot. Positional structure declarations may become named objects for
an interior member edit. Fixed arrays permit adding only the next selected
prefix element and removing only the selected tail element. Edits then rebuild
the target and its descendants. Values being replaced or removed need not have
representable declarations: an unlabelled enum code can be discarded by that
edit. Retained selected values still require valid declarations, including in
descendants. Selection-path allocation failures report `allocation_failed`;
invalid paths report `invalid_declaration`. Failed validation or staging preserves
existing entries; an unrecoverable failure after publication disables the
role and reports a critical event. `clear()` releases the owned document,
payload and binding.

Descendant staging scans the remaining parent-before-child record sequence once,
using a temporary record-to-staged-edit ordinal table. Descendant membership and
staged-parent access are constant-time per candidate; unrelated later branches
are skipped without walking their ancestor chains. This does not change which
selections are retained or how inherited values are propagated.

## Live instance output (stage 6b)

`CLiveInstances::prepare_output(document, payload, destination_schema, form,
diagnostic)` accepts an empty live document and unallocated payload, with an
explicitly resolved baked or live destination schema. It checks every used
type for structural and value-interpretation compatibility, reconciles declarations
for retained-value forms, then packs complete
snapshots in document hierarchy order. Nonempty extents have type alignment;
zero-byte extents use offset zero. A failed preparation leaves both outputs
unpublished. Diagnostics identify a failing source occurrence where applicable.

Both `EDataOutputForm::external` and `embedded` regenerate declarations from
authoritative snapshots, with the same defaults, inheritance and array-prefix
rules. They copy complete snapshots exactly. Every generated declaration is
checked by reconstructing its meaningful values against defaults or the parent's
snapshot. If an encoded scalar cannot be represented by declaration values,
either form fails with `unrepresentable_value`. Comparison uses
the same encoded-field comparison rules as the baked loader, including NaN equality
and ignored padding and unused bits.

`bake(block, payload, role, destination_schema, form, diagnostic)` prepares,
bakes, binds and loads the output in temporary storage before publishing an
independent baked block and payload owner. The returned role borrows those
allocations and remains usable after the source live role is cleared. All
baking outputs must be empty; failure leaves all supplied destinations unchanged
and preserves the source. Success does not consume the source. Embedded output
can also be written as Morphic text or strict JSON, parsed, baked and
materialised without a supplied payload. A parser-normalised named singleton
inside an array or positional compound is interpreted as a selection of that
compound's named member. This is contextual to a compound element; named
scalar-array elements and unknown members remain invalid.

## Stripped data output

`EDataOutputForm::stripped` is available on both data roles' `prepare_output()`
and `bake()` overloads. The former `demote()` APIs are now named `bake()`,
including `CLiveSchema::bake()`; no compatibility aliases are provided.

Stripped output removes every instance/specialisation `declaration` and every
embedded bulk `data` array. It retains type groups, names, the specialisation
hierarchy and valid locators with offsets, sizes and bulk counts. Complete binary
snapshots are packed and copied exactly, including padding, without scalar
decoding. Schema baking still copies the complete `types` section, including all
definitions and defaults; the data output choice never reduces the schema.

The output root carries `"stripped": true`. This optional Boolean applies to
both data sections if they share a document; absent or false retains ordinary
document semantics. The schema resolver accepts it in combined documents.
Data loaders reject a non-Boolean marker and reject declarations/record arrays
in a section marked stripped. `values_stripped()` exposes the marker on either
baked data role without allocating.

Reload using `load_supplied()` and the associated binary payload. Navigation,
entry views, remapping and unused-storage clearing continue to use the retained
metadata. With stripped input, `compare_embedded=true` has no values to compare;
it still validates navigation, type and locator structure, ranges and overlap.
`materialise()` reports `binary_required` before attempting reconstruction,
including for empty collections and zero-byte types. Those cases may use the
explicit supplied route with a canonical empty payload.

Promotion reconstructs instance declarations against destination defaults and
parent snapshots, and reconstructs complete bulk arrays. The resulting live
documents have no stripping marker and support normal editing. A raw scalar
that cannot be expressed by the document grammar can be baked stripped and read
from binary, but promotion reports `unrepresentable_value` without publishing
a partial result. The existing embedded and external forms retain their previous
behaviour: both regenerate instance declarations, while external bulk output
omits arrays and its promotion keeps reference-only entries.

For example, a stripped instance document can contain:

```json
{"stripped":true,"instances":{"Pair":{"base":{
  "locator":{"offset":0,"valid":true,"size":2},
  "specialisation":{"child":{"locator":{"offset":2,"valid":true,"size":2}}}
}}}}
```

The associated schema describes `Pair`, and the accompanying payload holds each
instance's complete two-byte snapshot. Missing declarations here do not imply
default or inherited values.

## Bounded direct-member remap (stage 7a)

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
claim is made. Declaration reconciliation is a separate live-role operation;
call `reconcile(diagnostic)` after transfers when an updated document is needed.

## Role entry views for remapping (stage 7b)

`CBakedInstances::entry()`, `CLiveInstances::entry()`, `CBakedBulkData::entry()`
and `CLiveBulkData::entry()` provide type, bytes and byte extent for read-only
sources. Construct `CByteConstView` from the returned bytes and extent.
`CLiveBulkData::mutable_entry()` provides a bounded destination view, including
for entries created with `create_unpopulated()`. `CLiveInstances::mutable_entry()`
provides the same access for a base or specialisation snapshot, with type,
parent, declaration and offset metadata. A zero-byte snapshot has a canonical
empty mutable view. Failed mutable lookup leaves its output argument unchanged.
The primary destination remains an application-owned buffer; these role methods
only expose storage to the existing executor.

For example, after constructing a plan from the expected resolved types and
creating any live destination entry, read both sources afresh:

```cpp
SBulkEntryView a, b;
if (baked_bulk.entry(baked_handle, a) && live_bulk.entry(live_handle, b) &&
    (a.type == expected_a) && (b.type == expected_b))
{
    const CByteConstView sources[] = {
        CByteConstView{ a.bytes, static_cast<std::size_t>(a.byte_count) },
        CByteConstView{ b.bytes, static_cast<std::size_t>(b.byte_count) }
    };
    const bool filled_external = plan.execute(sources, 2u, application_buffer.view());
    SMutableBulkEntryView target;
    if (live_bulk.mutable_entry(target_handle, target) && (target.type == expected_destination))
    {
        const bool filled_live_bulk = plan.execute(sources, 2u, target.bytes);
    }
}
```

An instance entry uses the same source-view construction. A nonzero-byte
instance source contributes one complete record, so it limits a combined
transfer to one record; a zero-byte type imposes no byte-copy limit. A live
instance destination uses `SMutableInstanceEntryView::bytes` with the same
`execute()` call. Its binary snapshot changes without editing declarations,
selection intent or descendant snapshots, including when a mapped field was
previously unselected. Call `reconcile(diagnostic)` to update the live document
afterwards, or let promotion or retained-value output/baking reconcile the destination document.
Unrepresentable binary values still fail when document literals are required.

Keep the plan associated with the role objects, their bound schemas and the
type indices supplied at setup. Check entry types before execution, and rebuild
the association if a role is rebound. Type-index equality alone does not prove
that two different schemas describe the same type. Role entry lookup requires a
loaded role and a handle present in its current records, but handles do not
carry a globally unique role identity; retain each handle with its originating
role. Baked roles borrow their document and payload, while live roles own their
payload. A live role operation can relocate that payload, so retrieve all entry
views after creation, capture or another possible relocation and execute before
mutating the roles again. The executor itself performs no allocation, schema
traversal or document edit.

## Explicit unused-storage clearing (stage 8)

`clear_unused_storage(schema, type, writable_view)` clears byte gaps within
standalone schema-typed values, recursively through structures and arrays.
The view must contain complete values and be a multiple of the type's nonzero
stride; fixed-array roots must also contain complete declared arrays. A
partial trailing value fails without processing earlier values. A canonical
empty view succeeds for a zero-byte type; a nonempty view against such a type
is rejected because it has no typed bytes to classify.

`clear_unused_storage(schema, instance_query, writable_payload)` and the bulk
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
may invoke clearing on live backing, but packing can create new gaps or copy
aggregate padding, so deterministic binary output calls it on the final
output buffer as well. No construction, capture, remap or output operation
invokes it automatically.

The default preserves unused bits in bit-storage words. Pass
`EUnusedBits::clear` to zero only bits outside declared field masks; declared
bits retain their exact encoding, including noncanonical values. Primitive
and enum encodings, unselected fields, NaN payloads and signed zero are never
normalised. The operation does not apply defaults, edit declarations, compact
storage, move records or change locators. It returns `bool` and validates the
complete layout, locators, extents and overlaps before writing. Invalid input
or allocation failure leaves every destination byte unchanged; repeated
successful calls are idempotent.

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

Instance and bulk scalar decoding currently emits decimal integer metadata.
Whether their output should adopt the declared-type hexadecimal convention is
unresolved; the future schema-document rules above do not describe that current
data-role output behaviour.

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
resolution and C++ layout fixture. The role-loader tests exercise its instance
and bulk sections separately; those remain outside the resolver's validation scope.

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
