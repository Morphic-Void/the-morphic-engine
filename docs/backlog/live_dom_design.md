Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   live_dom_design.md
Author: Ritchie Brannan
Date:   20 Aug 2026

# Live DOM design

## Status and purpose

This document records the intended design direction for TheMorphicEngine's
document substrate.  It is an implementation reference and a statement of
scope, not an implementation specification frozen against future evidence.

The immediate concern is a mutable, JSON-shaped live document object model.
Its principal workload is programmatic construction and editing: mutation,
addition, replacement, detachment, and pruning.  JSON parsing is a secondary
importer of that model, not the mechanism that defines it.

The document substrate will later support schema interpretation, shader and
pipeline configuration, consolidation, and construction of real-time data.
Those systems are consumers of this work.  They are not defined by this
document or implemented as part of the initial live-DOM work.

## Threading and quiescence

The live document has no internal synchronization.  It is modified by one
thread at a time.  Read access is permitted only while mutation is prohibited
and the document is quiescent; lifetime authority and any required external
coordination remain with the caller.

Read operations must not lazily create caches or otherwise mutate the
document.  Any such cache is mutation and is permitted only under the same
single-threaded mutation contract, or must be built before the document is
published for quiescent access.  The baked representation is immutable after
construction and likewise has no need for internal write synchronization.

## Representation lifecycle

The same durable document meaning has three deliberately different forms:

```text
JSON text
  durable human-readable audit, debugging, and archival representation
        |
        | parse or programmatic construction
        v
Live DOM
  mutable editor/build representation
        |
        | bake
        v
Baked DOM
  immutable binary distribution and construction representation
        |
        | instantiate
        v
Real-time data
  domain-specific execution data consumed by the distributed product
```

JSON is the long-term durable form.  The live form owns mutable construction
and relationship editing.  The baked form is an immutable, compact,
relatively fast structural form used mainly to construct real-time data; it
is not itself the final real-time representation.  A real-time domain may
retain useful baked indices as IDs, but that is a deliberate domain-local
choice rather than a general identity guarantee.

The live and baked forms are stateless across their lifecycles.  Neither live
keys nor baked indices are serialized as document identity.  Durable identity
and references must be recoverable from document content.

## Live DOM scope

The live DOM models ordinary JSON kinds:

- null
- boolean
- integer
- floating-point number
- string
- array
- object

All node kinds use one common tagged, trivially-copyable slot payload.  The
tag determines the interpretation of a small common payload field set.  Scalar
values are embedded in the slot.  Array and object nodes embed their child-list
header in the same payload footprint.  This avoids per-kind heap object
models, vtables, allocation chasing, and separate structural sidecars.

## Numeric semantic and lexical intent

Integer nodes retain both their value and the intent needed to reproduce a
Morphic JSON numeric spelling. The node flags encode signedness, the smallest
valid integer width (8, 16, 32, or 64 bits), notation (decimal, hexadecimal,
or binary), and prefix selection. Width is not an independently editable
hint: it is derived from the value and the selected signedness, and integrity
validation rejects a wider or narrower encoding.

Unsigned integers have no sign character. Signed integers use `-` for a
negative value and `+` for a non-negative value, so `127` is an unsigned 8-bit
integer while `+127` is a signed 8-bit integer. Floating-point nodes carry no
signedness metadata: IEEE binary64 already contains its sign, including the
sign of negative zero, and floating node flags are always zero.

Decimal is the default notation. Hexadecimal preserves either `0x` or `#`;
an imported `0X` is normalised to `0x`. Binary uses `0b`; an imported `0B` is
normalised to `0b`. No alternate binary prefix is presently defined. Thus a
future writer can preserve examples such as `+#7f` without retaining source
text as a separate string.

The existing one-byte node flag field is deliberately sufficient for this
metadata, so live slots remain 64 bytes and baked node records remain 32 bytes.

### Deferred numeric reader and writer policy

JSON itself defines one abstract number grammar; it does not define signed and
unsigned integer types or a floating-point storage type. Morphic JSON uses its
additional spelling rules to make the data-model intent deterministic:

- an unsigned integer has no sign character, for example `127`;
- a signed integer always has a sign character, for example `+127` or `-127`;
- a floating-point number contains either a decimal point or an exponent, so
  an integral-valued double remains distinguishable from an integer when
  written; its sign is encoded by binary64, with no separate signedness metadata
  and no leading `+` for non-negative values.

The Morphic-preserving writer should retain integer notation and prefix choice
and emit the explicit sign required by the numeric domain. A separate strict
JSON mode may deliberately normalise this representation by dropping a leading
`+` and converting hexadecimal or binary integers to decimal. Such an export
is for interoperability rather than Morphic round trip, so loss of signedness
or lexical style is acceptable, but the writer should return observable result
flags or statistics describing transformations it actually performed.

Strict JSON output must still emit the exact decimal value of every supported
unsigned integer, including the full `uint64_t` range. It should not warn or
alter output merely to accommodate consumers whose numeric implementation is
less complete than the JSON grammar permits.

For finite IEEE-754 binary64 values, the writer need only preserve the stored
`double` state, not the author's original decimal spelling. A correctly rounded
shortest-round-trip conversion is preferred; emitting `max_digits10` decimal
digits is also sufficient. Negative zero requires deliberate preservation.
Finite floating-point values use the same spelling in Morphic-preserving and
strict modes; non-negative values do not receive a leading `+`. There is no
need for an exponentiated-integer node representation or additional floating
metadata to provide this guarantee. The live document may temporarily hold
NaN and infinities as a construction workspace, but baking rejects them
atomically. Builder integrity and baked validation independently require every
floating payload to be finite.

An illustrative shape is:

```cpp
struct CJsonSlot {
    NodeKey parent;
    NodeKey previous_sibling;
    NodeKey next_sibling;
    PropertyNameId name_in_parent;
    uint32_t type_and_flags;
    CJsonPayload payload;
};
```

The final layout must be selected from actual access and packing requirements.
It should use natural alignment, fixed-width fields, and a power-of-two record
size.  A 64-byte live-slot target is appropriate unless actual payload needs
show otherwise.  The power-of-two target is a low-cost consistency rule, not a
claim of a material automatic performance gain.

`CJsonPayload` is a POD union.  Scalars use an embedded value or stable string
ID.  Arrays and objects use an embedded child-list header:

```cpp
struct CChildList {
    NodeKey first;
    NodeKey last;
    uint32_t count;
    uint32_t revision;
};
```

Object and array children are a linear, key-linked list.  A child holds its
parent, previous sibling, and next sibling; an object child also stores its
`name_in_parent`.  Array children and the root use an invalid name sentinel.
Object and array order are semantic and must be preserved.

Lists are never circular.  `first.previous_sibling` and `last.next_sibling`
are invalid.  This makes termination and integrity validation explicit.  A
caller that needs wraparound can reach the parent and then its first or last
child without introducing circular sibling links.

The live representation favors inexpensive mutation over random positional
access.  Append, insertion relative to a known sibling, detach, and sibling
traversal are direct key-link operations.  Positional array lookup is O(N).
Callers performing sequential array access may retain a revision-bound cursor
containing the parent, current child, and current index; the next element is
then reached directly through `next_sibling`.  A parent child-list revision
invalidates such cursors after structural mutation.

## Live keys and slots

The live node registry is expected to use `TPodOrderedSlots` with an
externally generated monotonically increasing 64-bit key.  The key identifies
a node only during one live-document lifecycle.  Its purpose is to make
physical slot reuse safe:

```text
old node: slot 17, key 1001
erase old node
new node: slot 17, key 1002
lookup 1001: not found
lookup 1002: found
```

The ordered-key lookup provides this stale-key rejection without requiring a
generation field in every slot.  Slot position remains an implementation
detail.  `sort_and_pack()` may rearrange physical storage without changing the
meaning of live relationships held as keys.

`TPodUnorderedSlots` with a `{ slot_index, generation }` handle is a viable
alternative for direct mutable access, but it requires explicit generation
maintenance and remapping of all handles if a live store is packed.  The
ordered-key arrangement is the current preferred live model.

Keys are never written to JSON or baked binary output, never used as durable
references, and never preserved by bake, load, or promotion.  A promoted
document creates a fresh live-key universe.

## Names, strings, and efficient lookup

`CStableStrings` is suitable backing storage for document strings, but string
roles must remain separate:

```text
PropertyNames   object member names and canonical path name segments
StringValues    ordinary JSON string scalar content
CanonicalPaths  canonical reference path records and, where needed, locators
```

Property-name interning turns repeated structural comparisons into integer-ID
comparisons.  An arbitrary author string is looked up lexically once at the
text boundary; subsequently, object member lookup operates on a
`PropertyNameId`.  The initial live implementation may scan an object's
linked children, comparing integer IDs.  A revision-bound lookup cache is a
future optimization only if workload evidence justifies it.

Each live string-related table has a companion use-count table.  Counts are
updated atomically with document mutation and identify semantically live
entries for baking.  A zero-use value is not removed from the live
`CStableStrings` table: live IDs remain stable for the document lifecycle.
During baking, only live entries are copied into compact baked tables and
rewritten through live-to-baked string/name maps.  A structural audit verifies
that recorded use counts match actual graph use.

Live string storage is length-aware and physically zero-terminated. It may
contain literal zero bytes or the exact Java-style modified-UTF-8 encoding
`C0 80` for U+0000. The live-to-baked boundary validates all reachable names
and string values, promotes each embedded literal zero byte to `C0 80`, and
otherwise requires strict UTF-8. Every other malformed, overlong, surrogate,
extended, or truncated form is rejected. Normalisation precedes interning, so
raw spellings which become equal after promotion collide as the same name.

Baked strings use strict UTF-8 with one deliberate exception: logical U+0000
is canonically stored as `C0 80`. Literal zero is forbidden within the payload.
Each baked string is followed physically by one literal zero terminator, while
its reference length counts payload bytes only. Thus `string[length]` is zero,
but the terminator is not part of comparison, interning, or JSON output. String
table byte sizes and the baked payload CRC include these physical terminators.
Baked validation independently verifies encoding, terminators, dense reference
layout, and byte-wise uniqueness of interned entries.

## Navigation and references

Navigation has two forms.

Traversal paths are operational and transient.  They may use object names and
array indices, for editor selection, diagnostics, and mutation operations.

Durable reference paths may ascend through structural parents and descend only
through object member names.  They must not descend through arrays using an
array index.  Array insertion or deletion changes positional indices and must
not silently retarget a durable reference.

```text
transient:  #/materials/3/states/0
durable:    ../sharedMaterials/default
```

Canonical durable paths are represented as interned property-name ID steps and
parent steps, optionally prefixed with a canonical asset/document locator.
They are reconstructable as a long textual form.  Resolution to a live key or
baked index is only a lifecycle-local cache.

If an array element needs durable direct identity, it must be represented by a
named object member or a future schema-defined semantic-ID lookup.  It must
not be identified by array ordinal position.

## Object-name uniqueness

Within an individual object, immediate member names are unique by exact byte
sequence.  The same name may occur freely in different objects at different
levels of the document.

```json
{
  "render": { "settings": {} },
  "audio":  { "settings": {} }
}
```

Duplicate `settings` members in one object are invalid.  Normal mutation
operations reject duplicate creation or a colliding rename atomically; they do
not silently overwrite or transform data.  Replacement is a distinct
operation and requires an existing member.

## Diagnostics and malformed JSON recovery

Document diagnostics are metadata held by the document instance, not semantic
JSON nodes.  They record status, failure code, severity, relevant property
name, and source locations where available.

Normal JSON import rejects duplicate object members and reports both source
locations.  An explicit diagnostic recovery importer may instead construct a
distinct `RecoveredDuplicateArray` node.  It has ordinary array traversal
mechanics but is a separate node variant that records collision metadata.  It
is created only by recovery import and cannot be created by ordinary mutation.

Recovered documents are non-standard.  They exist solely to inspect and
repair malformed input.  Their recovery node variants may propagate into a
diagnostic baked artifact, which carries an explicit non-canonical header flag.
The normal runtime loading path rejects such an artifact.  A diagnostic JSON
writer may serialize it with an explicit named recovery object, for example
under `$morphic.recovery`; this is not a valid engine document specification
and normal parsing does not assign it special meaning.

## Purity and baking

Canonical baking requires a pure live document.  A pure document contains only
standard node kinds and has no blocking diagnostics or recovery state.

```text
pure live document                 -> canonical bake permitted
recovered/non-standard document    -> diagnostic non-canonical bake permitted
fatal/invalid document             -> bake rejected
```

The canonical baked representation contains no live keys, slots, free-list
state, or recovery metadata.  A diagnostic baked representation may contain explicit
recovery node variants and carries a non-canonical header flag; it is never a
runtime distribution asset.  Both forms assign dense baked indices and rewrite
linked live children into dense contiguous array/member ranges for O(1)
indexed access.  They strip unused strings and names.  Their node records
should use natural alignment, fixed-width fields, and a deliberate power-of-two
size, normally 32 or 64 bytes as the actual binary payload and query profile
require.  Loading binary creates a baked document; editing requires explicit
promotion into a new live document.

Baked artifacts derive header flags from reachable nodes. In addition to the
recovered-duplicate-array flag, one flag records whether any numeric node
requires Morphic JSON syntax, whether because it is a non-negative signed
integer or uses non-decimal notation. A document is canonical only when neither
condition is present. Numeric metadata is copied through bake and promotion and
checked against the stored value at every boundary.

The extension flag is integer-only. Floating-point values never contribute to
it. Embedded logical NULs are valid canonical content and do not require a
persistent header flag; a writer necessarily observes them while sizing and
escaping strings.

The header remains 64 bytes and baked node records remain 32 bytes. Baked
format version 3 records zero floating flags, finite floating payloads, the
modified-NUL string encoding, and physical per-string terminators. Older
artifacts are rejected rather than inferred or migrated.

The header groups the node table layout and the two repeated string
table layouts (property names and string values) into trivial standard-layout
substructures. This is a source-level decomposition only: field offsets and the
64-byte wire representation remain unchanged. Version 3 has one precise
payload layout—aligned nodes, aligned property-name references, property-name
bytes, aligned string-value references, then string-value bytes. A checked
view recomputes these offsets and the total size rather than accepting alternate
or overlapping section arrangements. The existing reserved node field remains
zero and is not repurposed; live integrity, builder integrity, and baked
validation reject nonzero values.

## Interface boundaries

Live and baked documents do not inherit from a common document class and do
not use virtual functions for DOM access.  They expose compatible read method
signatures so common traversal and validation logic can use static dispatch or
parallel implementations without confusion.

The Stage 1 JSON writer consumes `CBakedDocument` only, with no live-document
overload or virtual write-source adapter. The JSON parser targets the live
document only and will consume the text-ingestion/normalisation layer. Writing
does not use the text linter or an input prepass.

The parser promotes an escaped or decoded embedded U+0000 to `C0 80` in its
live string payload. The writer recognizes that exact sequence as logical
U+0000 and always emits it as the JSON hexadecimal escape `\u0000`; it never
emits a baked string's final physical terminator.

## Stage 1 writer contract

`core/text/json_writer.hpp` exposes `json_writer::write`, a synchronous,
`noexcept`, memory-only operation. The source view must already be validated
and its bytes must remain alive and immutable throughout the call. Entry checks
readiness and root presence, not `is_canonical()`. Construction/reset has
already checked CRC, layout, structure, finite floating values, and string
encoding; the writer does not repeat `is_valid()`/`check_integrity()` (which
revalidate the entire artifact). It is not a validator for bytes changed after
view construction, nor can it diagnose dangling storage.

`CJsonWriteOptions` has independent `strict_json` and `escape_non_ascii`
booleans, both defaulting to false. Default Morphic integer output preserves
sign intent, notation and prefix choice, using lowercase prefixes/digits and
no width padding. Negative hexadecimal/binary integers use a sign followed by
the magnitude, not two's-complement digits. Strict output converts non-decimal
integers to exact decimal and drops signed-positive `+`; it never narrows an
unsigned integer to binary64. Finite floating output uses locale-independent
`std::to_chars` shortest-round-trip general formatting, appending `.0` when
there is no decimal point or exponent. Positive zero is `0.0`, negative zero
is `-0.0`, and floats have identical spelling in both modes.

All string values and object names are double-quoted. Quote/backslash and
control characters are escaped; backspace, form feed, LF, CR and tab use their
short JSON escapes, while other controls use lowercase `\u00xx`. Logical NUL
(`C0 80` in the baked payload) always becomes `\u0000`. Slash is not escaped.
Normally other valid UTF-8 is copied unchanged. With `escape_non_ascii`, every
scalar above U+007F uses lowercase `\uXXXX`, or a UTF-16 surrogate pair for a
supplementary scalar. No Unicode normalization or additional validity pass is
performed during writing.

Formatting defaults to `pretty_print = true`, `indent_width = 2` spaces,
`line_ending = EJsonWriteLineEnding::lf`, and `trailing_line_ending = true`.
Pretty output places each member/element on its own line, with one space after
an object colon; empty containers remain `{}` and `[]`. CRLF is selectable.
Compact mode omits optional whitespace but independently honors the trailing
line-ending setting. Indentation width zero is valid. All these choices
preserve baked child order and produce deterministic bytes.

Recovery handling is automatic from the baked recovery flag. Ordinary input
writes its root directly; recovered input writes the following envelope, whose
fixed property order is shown here in compact notation:

```text
{"$morphic.recovery":{"format":"diagnostic-document","version":1,"document":<root>}}
```

Each recovered duplicate array is represented by:

```text
{"$morphic.recovery":{"kind":"duplicate-member-array","values":[<children>]}}
```

The original colliding property name stays in its original object position;
`values` is a fixed recovery field holding the competing values in order.
Nested recovery uses the same rule. There are no duplicate output property
names, no fabricated source-location metadata, and no option to suppress
recovery markers. Numeric/ASCII/formatting options apply inside the envelope.
This is diagnostic output, not an engine document specification. An ordinary
parser assigns no special meaning to the marker; the label is not a schema
restriction on arbitrary user objects that happen to have the same shape.

The move-only result owns a `CByteBuffer` with one final zero included in its
`size()`. `report.logical_text_byte_size` excludes that zero but includes a
requested trailing line ending. Failure returns no output allocation and all
output counters zero. Status distinguishes unready input, observed source
contract violations, engine size limits, allocation failure, and internal
errors. Invalid line-ending enum values return `internal_error`.

The report counts actual emitted occurrences: non-decimal integer conversions,
omitted integer positive signs, non-ASCII scalar escapes, logical NUL escapes,
and recovery nodes, plus whether the diagnostic envelope was written. Shared
interned strings are counted once per output occurrence. A surrogate pair is
one escaped scalar. These counters describe completed output, not attempted
work. No warnings are invented for strict JSON consumers with limited integer
precision.

Traversal is iterative using baked parent/sibling links, so writing does not
consume a call-stack frame per nesting level. A single traversal emits into an
ambient engine buffer using checked size arithmetic and its default growth
policy. Each run resets its output, counters, status and depth; completed output
is transferred to the result with one physical terminal zero. Spare capacity
is retained rather than requiring a shrink allocation. Any allocation failure,
including later growth or final termination, discards partial output and all
output counters. There is no scratch allocation, file I/O, logging,
host/executive work, or ownership transfer of the source. Stage 2
must recognize the agreed numeric grammar and normalize decoded U+0000 to
`C0 80` before interning; Stage 3 must write only the reported logical text
extent, not the output buffer's final zero.

## Deferred layers

Schema and high-level domain systems sit above the live DOM.  Schema may be
JSON-described and may interpret a JSON string as a domain value, such as an
exact float bit representation, vector, matrix, or compact array.  Such
interpretation is schema-directed; the base DOM does not infer it globally or
gain special node kinds for it.

The future real-time data stage is outside this project's scope.  This layer
must provide it a reliable structural surface, not decide its final resource,
renderer, or execution-object design.

## Development roadmap

1. Define the common live slot record, live key wrapper, and ordered slot
   registry contract.
2. Implement embedded linear child lists, structural read traversal, and
   revision-bound sequential access cursors.
3. Implement creation, attachment, replacement, detachment, and subtree
   pruning.
4. Define structural ownership: object/array edges own children; canonical
   references are non-owning and may become unresolved after pruning.
5. Implement separate stable string pools and transactional use-count updates.
6. Implement integrity audits and tests for slot reuse, stale-key rejection,
   ordering, pruning, collision rejection, and count consistency.
7. Design baking from the proven live-DOM behaviour, then add pure-document
   validation, dense index construction, compaction, binary streaming, and
   promotion.
8. Add the baked-document-only JSON writer, including strict/ASCII options,
   configurable formatting, and automatic diagnostic recovery output.
9. After the low-level UTF ingester and normalisation layer is available, add
   the JSON parser targeting the live document.

The first implementation milestone is not JSON parsing.  It is a robust live
document that can be constructed, mutated, navigated, pruned, and audited
without violating its ownership, identity, string-use, or object-name
invariants.
