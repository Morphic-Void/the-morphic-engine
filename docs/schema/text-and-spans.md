Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    text-and-spans.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    17 Nov 26

# Text and typed collection ranges

Text and spans are inline physical type descriptors. They require a complete
schema data-block context: the owning document supplies strings and the collection
directory supplies span targets. Product release versions are independent of this
pre-release schema format, whose current marker is **1**. The generic baked-document
format remains **5**. Historical schema layouts are rejected even when their marker
was also 1; a marker alone does not establish the current required metadata.

```json
{"kind":"text","nullable":true}
{"kind":"span","element":"Mode","nullable":false}
```

`nullable` is optional and defaults to false. Span elements are named physical
types, including primitive names. Inline element descriptors and non-storage
declarations are not span elements. Type dependency cycles reject. Existing fixed
arrays retain their `element`/`count` syntax and may contain text or span fields.

Text values are strings, including `""` for present-empty. Nullable text also
accepts `{"absent":true}`. Nonnullable text defaults to present-empty; nullable
text defaults to absent. Explicit string/absent defaults are permitted. Strings
retain exact case, non-ASCII and combining content; no Unicode normalization is
performed. Logical U+0000 uses the document's existing canonical `C0 80` storage.

Span values are `{"empty":true}`, nullable `{"absent":true}`, or
`{"collection":"modes","first":0,"count":2}`. Collection names resolve within
the declared element type and the same block. `first` is a nonnegative i32 index;
`count` is u32 with an inclusive 0x80000000 range limit. Bounds use wider arithmetic.
A targeted zero count still validates the target, type and boundary, then becomes
canonical present-empty. A nonnullable span defaults to empty and a nullable span
to absent. Explicit defaults may be empty/absent, never bound to a collection.

Reference values are atomic selection leaves. A positional raw `null` continues
to mean an inheritance hole, not explicit absence. Bulk records require complete
values. Fixed-array defaults may contain reference values and retain existing
short-default behavior.

## Storage and retention

| Descriptor | Bytes/alignment | Encoding |
| --- | --- | --- |
| `STextReference` | 4 / 4 | i32 string-value ID: -1 absent, 0 empty, positive retained string |
| `SSpanReference` | 12 / 4 | i32 collection ID, i32 first, u32 count |

Span absent is `{-1,0,0}` and empty is `{0,0,0}`. Nonempty spans use a positive
collection ID, nonnegative first and positive count. No pointer, byte-pool offset,
stride or alignment is encoded in a reference. Generated C++ declarations use
`schema::STextReference` and `schema::SSpanReference` from `reference_types.hpp`.

Every current runtime envelope requires `retainedText`, an array of distinct
nonempty strings in lexical order; reference-free blocks use an empty array. Its
array indices are not string IDs. Positive text slots identify entries in the
owning generic document's string-value domain. Effective defaulted, inherited and
bulk text is retained, independently of stripped authoring literals.

Every runtime bulk entry has a positive, unique `collectionId`. Its containing
type group and entry name define its typed authoring identity. IDs are block-local
and do not survive copying as application identities. The checked view validates
the directory, types, all descriptor slots, bounds and acyclic dependencies.
Generic envelope adoption validates framing and required root metadata; it does
not by itself establish typed-reference validity.

The generic baker optionally publishes `CStringValueRemap` (defined in
`data_model/string_value_remap.hpp`) together with its
document. Both caller outputs are preserved on failure. The map is valid for that
unchanged source and bake; detached/unretained nonempty IDs have no translation.
The map translates raw indices. Its public `initialise()` takes ownership of a
completed index table without allocation; typed identities remain document-issued.
Schema construction retains effective text before baking and patches only declared
text slots through this map. Generic document copying alone does not relocate
opaque schema payloads.

`CSchema::validate` constructs and checks a private reference-aware block under
the default operation limits. It performs the same relocation, inheritance,
reference and complete-output extent checks as baking, then discards the private
block without a final publication copy. Only baking allocates caller-lifetime
output storage and charges publication work. Reference-free validation keeps its
direct path without runtime payload construction or a bounded operation.

## Checked reading and independent copies

Include `schema/data_block_view.hpp` and bind a `CDataBlockView` to a completed
`CBakedDataBlock` with explicit limits. The view borrows the backing and owns its
resolved/index metadata. Keep the source bytes alive and immutable until clear or
destruction. Entry/value handles carry the view identity and generation. Clear,
rebinding, destruction or a different owner invalidates their use.

The view retains its operation context with the metadata so later destruction uses
the original allocator even when the ambient context has changed. Keep that parent
allocator alive until the view is cleared or destroyed. Ordinary checked queries
do not consume a lifetime allowance from the retained binding operation.

`entry_identity` checks an entry handle's owner and generation before returning
its kind and ordinal within that kind. Both outputs remain unchanged on failure;
the ordinal is local to the current binding and cannot validate another handle.

Find typed instances/collections, select a record, then traverse members/fixed
arrays. `text` distinguishes absent/empty and returns canonical stored UTF-8;
`copy_text_utf8` produces counted strict UTF-8 with literal zero bytes, using named
default limits or its explicit limits/diagnostic overload. `span` returns a typed
range; `element` checks an index within that range. No writable descriptor escapes.

`copy_data` takes checked source views, selected entry handles and optional explicit
name mappings. It includes required instance ancestors and complete transitive
collection dependencies. A collection is copied once per source context and source
collection identity, preserving record order and overlapping ranges. Equal numeric
IDs from different source blocks never imply shared identity. Name conflicts
require explicit mappings. `repack_data` retains all named entries and selections.
Completed outputs own independent backing; source contexts can then be released.

Copy/repack preserve raw non-reference bytes, including padding and scalar encodings
that have no faithful authoring literal. Only declared reference slots relocate.
`export_data` reconstructs a generic baked authoring document with logical strings,
absent/empty markers and typed named collection ranges. It rejects values that
cannot be reconstructed faithfully. Import that document with
`CSchema::copy_from(CSchemaDocumentQuery{ exported.document() }, diagnostic)` and
explicitly bake it to obtain a new runtime block. Export and subsequent construction
are separate bounded operations.

## Private edit transactions

`CDataBlockEdit::begin` privately copies a checked source. `begin_empty` starts
from an immutable type catalogue. Commands create instances/collections, replace
complete collections, write a typed record path, rename or erase entries.
Logical inputs are consumed synchronously. Collection inputs are nonempty arrays
of complete records. Missing instance declarations select defaults or inheritance.

Each command stages a candidate replacement. Failure preserves the previous
candidate but consumes attempted work. Forward collection references and temporarily
invalid relationships may exist between commands; commit validates the complete
candidate. Rename preserves entry identity. Erase invalidates its handle, and a
new entry with the same name has a distinct identity. Repair affected references
before commit. Instance writes require an explicit preserve/propagate descendant
policy, with the same selection semantics as reference-free instances.

`commit` publishes only to an empty destination. Failure leaves the candidate
available for correction if its remaining budget permits. Success ends the session;
reacquire all result handles. `cancel` and destruction discard private state without
publication. Source readers remain valid throughout. Installing a replacement into
Host remains the responsibility of Host publication/acquisition code.

## Bounded operation contract

| Shared allowance | Named default |
| --- | --- |
| Distinct inspected source-block bytes | 64 MiB |
| Peak owned storage, including staging, maps, indexes and publication | 64 MiB |
| Complete output | 16 MiB |
| Cumulative document-node admissions | 262,144 |
| Records/snapshots in a validated context or candidate, including zero-byte records | 262,144 |
| Work credits | 4,194,304 |
| Type/value/reference depth | 256 |

Trusted callers may supply other finite limits within hard allocator/format limits.
These allowances are operation limits, not new format maxima. Copy/repack share one
allowance across source validation, closure, construction, relocation and validation.
An edit session shares its allowance across begin, all commands and commit; retries
never reset it. Repeated actual passes consume work, including zero-sized visits.
Byte scanning/copying uses a conservative charge per started 64-byte chunk.

The outer call or edit session owns its operation; internal checked-state loaders
borrow it. A copy or export has its own operation even when its inputs are bound
views. Private metadata is destroyed before its operation, and publication places
independent outputs in the operation's parent allocator. Keep that allocator alive
until the session and its published outputs have released their storage.

`SDataOperationDiagnostic` stores copied details and distinguishes invalid input,
references, naming conflicts, stale handles, allocation failure and limit exhaustion.
It remains valid after private staging is destroyed. Operations are synchronous and
the caps are not time guarantees. Qualification and timing/placement evidence
are recorded in [the implementation record](reference-extension-validation.md).

## Existing API boundaries

Reference-free role behavior remains supported. Raw role `capture`, writable entry
access, independent role materialisation and the opaque `CDataRemap` executor cannot
supply a reference context and reject reference-bearing layouts. Baked role attachment
also rejects such payloads; use `CDataBlockView`. `CBakedDataBlock::assemble` rejects
reference-bearing descriptions because rebaking their document would invalidate
copied text IDs. Use bound copy/repack or `CSchema::bake` for those blocks.

Existing prepared-schema consumers that extract separate roles retain their
reference-free boundary. This extension does not implement Platform reports or
Rendering features. Those consumers must adopt the complete-block checked API as
part of their separately authorised work.
