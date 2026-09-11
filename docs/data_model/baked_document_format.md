Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   baked_document_format.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   6 Sep 2026

# Baked-document format

## Scope

This document is the normative physical specification for the replacement
baked artifact. The semantic contract remains in
[the data-model specification](revised_data_model.md). Implementation rationale
belongs in [design notes](data_model_design_notes.md). Planned parser and Host
consolidation is tracked separately in the
[active plan](../backlog/consolidation_pass.md).

The format is an immutable, self-contained byte block smaller than 4 GiB. All
multibyte integers are little-endian and floating payloads are IEEE-754
binary64. The owning block uses framework allocation and consists of exactly
one allocation. Baking scratch is external to that allocation. A bound block's
base address is 32-byte aligned; its total size need not be. The value-record
section is likewise 32-byte aligned.

The initial replacement format has magic bytes `MBD2` and version 1. It is
incompatible with every archived baked format.

## Indices

A baked value index is a `uint32_t`. Values occupy the dense range
`[0, value_count)`, the root is index zero and `UINT32_MAX` is invalid.

Property-name and string-value indices are separate `uint32_t` domains. Each
occupies a dense range beginning at zero. Index zero is the canonical empty
string and `UINT32_MAX` is invalid.

## Header and section order

The 32-byte header contains these fields in order:

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | `uint32_t` | magic, `0x3244424d` |
| 4 | `uint16_t` | version, 1 |
| 6 | `uint16_t` | header size, 32 |
| 8 | `uint32_t` | total byte size |
| 12 | `uint32_t` | value count |
| 16 | `uint32_t` | property-name reference count |
| 20 | `uint32_t` | property-name byte count |
| 24 | `uint32_t` | string-value reference count |
| 28 | `uint32_t` | string-value byte count |

Sections immediately follow the header in this fixed order:

1. value records;
2. property-name references;
3. string-value references;
4. property-name bytes; and
5. string-value bytes.

Offsets are derived from the preceding counts and record sizes; they are not
stored. The 32-byte header places the 32-byte value records on a 32-byte
boundary. Their size and the 8-byte string references make every following
fixed-width section naturally eight-byte aligned without padding. There is no
trailing padding. The computed end of the final byte section must equal
`total_size` and the supplied block size.

`value_count` is at least one. Both string-reference counts and both string-byte
counts are at least one because each table physically contains its empty
string.

## Value records

Each value is one 32-byte record:

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | `uint64_t` | payload bits |
| 8 | `uint32_t` | parent index |
| 12 | `uint32_t` | first-child index |
| 16 | `uint32_t` | child count |
| 20 | `uint32_t` | property-name index |
| 24 | `uint8_t` | value type |
| 25 | `uint8_t` | value flags |
| 26 | `uint16_t` | reserved, zero |
| 28 | `uint32_t` | reserved, zero |

Value-type encodings are null 1, Boolean 2, integer 3, floating point 4,
string 5, array 6, object 7 and recovered array 8. Zero and all other values
are invalid. Empty is not encoded; baking substitutes null while retaining the
value's name and position.

The root is an anonymous object with an invalid parent. Every other value has a
valid parent whose index is lower than its own. A value's property-name index
is always valid; zero means anonymous.

For a non-container, `first_child_index` is invalid and `child_count` is zero.
For an empty container the same canonical pair is used. A non-empty container
has a valid first-child index and a nonzero count.

Containers occur in value-index order. Their non-empty direct-child ranges,
also in that order, partition values `[1, value_count)` without gaps or
overlap. This is the canonical breadth-first layout. Every child in a range
names that container as its parent. Child order within each range is semantic
order.

An object range contains only named values with unique immediate name indices.
A recovered-array range contains only anonymous values. An ordinary array may
contain either.

Payload and type-specific flag bits are canonical by type:

| Type | Payload | Type-specific flags |
| --- | --- | --- |
| null | zero | zero |
| Boolean | zero or one | zero |
| integer | signed bit pattern or unsigned value | integer metadata |
| floating point | finite binary64 bits | zero |
| string | string-value index in low 32 bits; high 32 bits zero | zero |
| any container | zero | zero |

Integer flags use bit 0 for unsigned domain, bits 1-2 for width, bits 3-4 for
notation and bit 5 for alternate prefix. Width encodings are 8, 16, 32 and 64
bits as 0 through 3. Notation encodings are decimal, hexadecimal and binary as
0 through 2; 3 is invalid. Alternate prefix is valid only for hexadecimal. The
stored width must be the smallest width valid for the payload and domain.

For every value type, bit 6 marks the first value in its parent's direct-child
range and bit 7 marks the last. A sole child carries both bits. The root carries
neither. These bits must agree exactly with the canonical child ranges.

## String tables

Each string reference is `{ uint32_t offset, uint32_t length }`; its offset is
relative to its own byte section. References are in unsigned-byte,
content-first lexical order and therefore define the baked IDs. Adjacent
non-empty strings compare strictly increasing.

Reference zero is `{ 0, 0 }` and byte zero is NUL. Every reference points to
the next densely packed string and each string is followed by one NUL byte.
There are no gaps or unreferenced bytes.

Every non-empty entry is referenced by at least one baked value. String bytes
must satisfy the canonical encoding rules in `revised_data_model.md`; literal
zero occurs only as a terminator.

## Baking scratch and emission

Baking begins with a successful live integrity check and root-reachable
analysis. The existing two reference-count vectors determine which live
strings are emitted. Iterating the stable stores by lexical rank supplies the
baked order and byte totals. After measurement, those vectors may be reused as
live-ID-to-baked-ID maps.

One additional vector maps each baked value index to its live key. It drives
breadth-first emission and makes each direct-child range contiguous. No
live-slot-to-baked-index map or mutable baked builder is required.

After sizes are checked with 64-bit arithmetic, the final block is allocated
once and filled directly. Failure must be reported and must not expose the
partial bytes as a ready baked document.

## Validation and views

`CBakedDocumentBlock` owns the byte allocation. `CBakedDocument` is a copyable,
non-owning immutable checked view. Public binding of arbitrary bytes performs
full validation and leaves the view not ready on failure. There is no public
unchecked baked view and no public mutable baked builder.

The view exposes the established read surface: readiness and integrity, root
and counts, value type and name, typed scalar payloads and integer metadata,
parent and sibling relationships, child ranges and ordinal array access, object
lookup by property name, and both string domains. Canonicality and recovered
content are observations, not stored fields. Exact C++ spelling remains an
implementation choice.

Object lookup by an already resolved property-name index scans only the direct
child range. Lookup from bytes first binary-searches the sorted property-name
table, then scans that range. No auxiliary object index is present.

Full validation checks:

- 32-byte base and value-section alignment, header identity, version, sizes and
  overflow-safe derived section bounds;
- every record's type, reserved fields, canonical unused fields and payload;
- root identity, sibling-position flags and the complete reciprocal
  parent/range structure;
- object naming and uniqueness and recovered-array anonymity;
- integer metadata, finite floating values and string indices; and
- both string tables' dense layout, terminators, encoding, lexical order and
  reference coverage.

Validation may use one transient framework vector as reusable marks for string
coverage and object-name uniqueness. The view retains no validation scratch;
allocation failure leaves it not ready.

The backing bytes must remain immutable and alive for the view's lifetime.
Copying an already checked view does not repeat validation. An explicit
integrity check may revalidate the bytes.

The format contains no checksum and no cached semantic flags. Structural
validation is not authentication, while checksums and semantic summaries can
be added by an enclosing persistence or publication protocol if a concrete
consumer requires them. Canonicality and recovered-content queries scan the
immutable value table on demand.
