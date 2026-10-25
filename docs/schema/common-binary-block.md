Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   common-binary-block.md
Authors: Ritchie Brannan / OpenAI Codex
Date:   25 Oct 2026

# Common binary schema data block

## Version-2 runtime contract

`CBakedDataBlock` owns or borrows one complete .bsd runtime block. Types,
instances and bulk data keep separate typed interfaces. The generic MBD2 header,
reserved-zero words and document encoding are unchanged. The previous schema-data
envelope is replaced outright; no compatibility reader is provided.

The root is an object with required `fileType: "morphic-schema-data"`, unsigned
`version: 2` and `stripped: true`. Optional object sections are `types`,
`instances` and `data`, in any combination including none. Every present data
section requires its unsigned exact `instancePayloadSize`/`bulkPayloadSize`.
A length without its section is invalid; an empty section cannot claim nonzero
bytes. Zero means explicitly supplied empty storage. Runtime binaries are never
unavailable for a present domain. Unknown properties, wrong types, unsupported
versions and missing metadata reject. Root property order is not semantic.

Instance descriptions retain selections: selected scalar leaves are true markers,
compounds/arrays identify selected paths, null array holes inherit, and explicit
empty containers remain. Bulk entries contain locators/counts but no record literals.
Type defaults/decorations and non-storage declarations are unaffected by stripping.
Envelope validation is schema-independent; typed attachment checks the data grammar
and matching values/layout. A typed role may be unusable while storage stays ready.

The file consists of the baked document, instance binary and bulk binary, in that
order. Its base and each nonempty binary are 128-byte aligned. Align the cursor
only before a nonempty binary; all intervening container padding must be zero.
Absent domains and zero-length binaries consume no bytes/padding. No final alignment
tail is saved. Locators remain relative to their own binary. Checked arithmetic,
exact file extent and the inclusive 2 GiB complete-file ceiling apply. Extra/truncated
bytes are errors. Payload-internal gaps/padding may be nonzero.

## Ownership and typed use

Local adoption transfers a `CBakedDocumentBlock`; asset attachment borrows an
asset ID/range and acquires no reference. `release_local` transfers local storage
without moving its bytes. The common owner retains no resolved types or role binding.
Keep the backing alive and stable while any role or view borrows it. Host binary
saving/admission operates on the complete range, excluding spare capacity.

`CBakedTypes::set_block` and the data roles' `attach_block` provide typed views.
Data attachment takes an explicitly resolved types owner and validates the complete
supplied payload. It never constructs missing binary or edits locators. Lower-level
read-only description plus explicit payload attachment remains available. Separate
working binary owners may be rebound through the documented role APIs, then explicitly
assembled into a new common block; no implicit in-place replacement/rebinding occurs.

## Construction and assembly

`CSchema::bake` validates its contained types and values, materialises complete
binaries and assembles a stripped block. It requires contained types with no external
fallback. Absent sections remain absent, explicit empty data sections have zero-length
binaries. Live data-role bake produces only that domain, without linked types, using
its current binding. Types-only bake remains available through the types wrapper.

`CBakedDataBlock::assemble` is schema-independent copying. Each present domain
requires its corresponding explicitly supplied payload pointer; an empty view supplies
zero bytes. Missing pointers or pointers without matching sections fail preflight
before allocation. It refreshes envelope/length metadata, preserves supplied bytes
exactly and never constructs values. Invalid existing metadata rejects before replacement.
Both construction and assembly require an empty destination and publish independent
storage only on success. Inputs and borrowed owners remain unchanged on failure.

## Assembly staging

Live descriptions copy directly into private live staging. Baked descriptions
promote once. The internal iterative helper copies root-reachable content, child
order, scalar/integer metadata and string presentation; detached storage is irrelevant.
It imposes no additional recursive subtree depth limit. Final generic document/file
limits and live allocation ceilings still apply. The staged description bakes once,
then payloads copy into the final allocation. There is no intermediate bake of the
original description and no public copy helper.

Destination/root/section-payload preflight precedes copying. Live-copy failure reports
description preparation; baked-copy failure reports document promotion; final bake
and assembly retain their stages. Original-source locations survive preflight; private
staging locations do not escape. Generic copy/bake APIs may only report an operation
failure rather than an allocation-specific reason. Typed failures preserve their
specific nested reason. Byte-preserving assembly is distinct from live packing,
which can discard abandoned space and zero new inter-entry gaps.

## Validation and history

The 25 October document model replaces the prior optional-binary, optional-stripping
contract. Current regression coverage includes all eight section combinations and
every supplied-pointer combination, strict version/marker/length checks, zero-size
domains, both alignment gaps, exact extents, ownership transfer, typed attachment,
aggregate construction, raw-byte preservation and allocation-failure publication.
The [validation guide](validation.md) records current checks.

The 24 October common-block and direct-assembly work established physical framing,
local/asset ownership and the copy pipeline retained here. Its old construction and
output-form APIs are superseded; detailed earlier contracts remain in Git history.
