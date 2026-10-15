Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   persistence-and-aggregation.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   15 Oct 2026

# Schema persistence and aggregation revision

## Status and scope

This document records the revised direction and persistence discussion on
14 October 2026, and the merge discussion on 15 October 2026, with their reasoning.
Shared definitions, caller-supplied schema binding, a contained specialised
interface, and reuse of existing ownership
mechanisms are agreed direction. The version 1 persistence format and block
interface, construction, Host persistence and diagnostic contracts below are
also agreed.
All eight persistence stages are complete, user-reviewed and committed, including
the gap-zeroing follow-up. Stage 8 and that follow-up were committed in `dfe076d`.
The completed scope includes naming, format validation and diagnostics, ownership,
live baking, materialisation, stripping, Host complete-buffer preservation and
end-to-end acceptance. The remaining functional work is definition merging,
instance/bulk data aggregation and complete document composition, described
separately below. The merge rules, form conversion, permitted-value policies,
same-owner schema association, separate validated schema-switch contract,
aggregation ownership and replacement, scalar equality, presentation merging,
normalisation of retained descriptions and metadata, output ordering and binary
layout recorded below are agreed direction. Pairwise aggregation interfaces with
typed source references, destination overloads and output selection are also agreed below.
Remaining interface decisions and the implementation plan are not yet settled;
this revision does not authorise further implementation.

The completed persistence implementation plan is retired. The
[persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence)
records all eight delivery stages, commit checkpoints and validation evidence.
Implemented interfaces and diagnostic representation are maintained in the
[runtime API](runtime-api.md#completed-data-blocks). This revision remains active
for aggregation, schema-switch and composition design and planning; that work is
not completed or authorised for implementation by the persistence milestone.

The [schema specification](specification.md) and [runtime API](runtime-api.md)
continue to describe implemented behaviour. This revision supersedes the earlier
proposal that a combined file exposing types, instances and bulk data should be
the mandatory normal persistence form. It also develops the packaging discussion
in the [follow-on notes](../backlog/schema_follow_on.md#document-and-binary-packaging).

## Expected use and reasoning

Shared type definitions are the normal foundation of the engine. Many independently
created instance and bulk data documents are expected to use those definitions. In a
release, most definitions will be fixed and most data will already be baked with
its binary payload. Some live instance and bulk data documents will still support
dynamic creation, updates and consumption.

Independent type creation is principally expected during data preparation,
compilation and reflection. Shader reflection may produce its own types and some
instance data, followed by reconciliation with existing definitions. Possible
release-time exceptions are not ruled out, but no specific exception has yet been
identified that should drive the normal design.

These uses favour separate document and schema lifetimes. Shared definitions
should not need to be duplicated into every independently produced data entity,
and changing one data entity should not inherently require rebuilding the shared
type document.

A release may benefit from loading related content together. The existing
[bundle format](../assets/bundle_specification.md) provides that packaging and
combined-load facility. A single load does not require a single schema document:
a bundle can hold separately meaningful files while preserving their boundaries.
Bundle packaging does not itself establish schema associations or reconcile types.

The close relationship that should become a persistence unit is therefore the
relationship between a data description and its binary payload. Their separate
ownership in the current API creates an association that callers must maintain;
keeping them together makes them referenceable as one entity without tying them
to every other user of their schema.

## Normal persistence units

The revised design retains these distinct roles:

| Unit | Responsibility |
| --- | --- |
| Type definitions | Describe and resolve the types shared by data consumers. |
| Baked instance entity | Keep an instance document and its associated binary payload together. |
| Baked bulk data entity | Keep a bulk data document and its associated binary payload together. |
| Bundle | Package related files for combined loading and access. |

An instance or bulk data entity is saved, loaded, owned and referenced as a single
entity. It contains and exposes its specialised interface rather than requiring
callers to maintain a separate interface attachment. Its document and binary
ranges share backing storage; callers should not have to maintain two unrelated
owners to keep the entity usable. The corresponding classes are
`schema::CBakedInstancesBlock` and `schema::CBakedBulkDataBlock`, following the
existing `CBakedDocumentBlock` naming convention. Their contracts are defined below.

Instance and bulk data content requires the applicable type definitions. Those
definitions may be shared and separately associated; they are not required to be
embedded in each data entity. The caller supplies the correct schema as specified
below. Physical co-location in a bundle alone is not evidence of compatibility.

There is no requirement for an instance entity to contain a bulk data section, or for
a bulk data entity to contain an instance section. The earlier proposal to expose all
three interfaces from every persisted file, using empty sections for absent
content, is superseded. The format below defines empty sections and zero-byte
payloads within each specialised entity.

## Agreed version 1 persistence format

The filename extensions are `.bid` for binary instance data, `.bbd` for binary
bulk data, and `.btd` for binary type data. The selected `.btd` spelling corresponds
directly to the existing `types` section. Extensions aid visual recognition;
readers identify and validate content independently of the filename.

The existing baked header's `MBD2` magic and encoding version identify a baked
document, not its application-specific content. The reserved header words must
remain zero. Following the bundle precedent, the `.bid` and `.bbd`
formats use metadata in their document root without changing that header:

| Root property | Meaning |
| --- | --- |
| `fileType` | Required string: `morphic-instance-data` for `.bid`, or `morphic-bulk-data` for `.bbd`. |
| `version` | Required unsigned persistence-format version, `1`, independent of the baked-document encoding version. |
| `payloadSize` | Required unsigned exact byte length of the associated binary payload. |
| `instances` or `data` | Required object containing the corresponding existing specialised description, even when empty. |
| `stripped` | Optional Boolean: absent or `false` retains ordinary document semantics; `true` forbids instance declarations or embedded bulk data values. |

The document root is an object. Version 1 permits only the properties above:
`.bid` requires `instances` and rejects `data`; `.bbd` requires `data` and rejects
`instances`. Both reject embedded `types` and unknown root properties. Required
metadata must have its specified document type; signed or floating-point values
are not substitutes for unsigned integers. Unsupported persistence or document
encoding versions are rejected. Strict root validation distinguishes missing
content from an intentionally empty section and makes future extensions explicit.

Readers accept root properties in any order. Writers emit a consistent metadata
order while preserving meaningful ordering within the specialised description.

The physical layout is one baked document followed, for a nonempty payload, by
zero padding to a 128-byte boundary and then the payload. Let `D` be the exact
document length from its baked header and `P` the declared payload length:

```text
P > 0: payload start = round_up(D, 128)
       file length   = payload start + P
P = 0: file length   = D
```

The loaded allocation or borrowed file range has a 128-byte-aligned base.
Locators remain relative to the payload start, as in the existing roles. The
file ends at the exact payload end; allocation capacity is not saved. A
zero-byte payload needs no dummy allocation or trailing alignment padding.
Zero length denotes an actually empty payload, including zero-byte data types;
it is not a marker for missing nonzero binary data.

All document-to-payload padding must be zero, and readers validate it. This
container padding occupies at most 127 bytes. Padding and unused bits inside the
payload remain preserved binary data under the existing contracts; they are not
subject to this container-padding rule.

When the file is a bundle entry, its own file extent excludes the bundle's
mandatory trailing zero and external padding. Those bundle storage rules do not
become part of the `.bid` or `.bbd` payload.

Loading validates the bounded document prefix, content identifier, supported
versions, exact total file length, zero container padding, and payload boundaries
with checked arithmetic and the existing 2 GiB allocation ceiling for the complete
entity. No bytes beyond the calculated file extent are accepted. Typed attachment
then uses the supplied schema and existing role checks for locators, alignment,
counts and extents.

### Empty content and payload coverage

An empty `instances` or `data` object is valid and requires `payloadSize: 0`.
Entries describing zero-byte types may also exist without payload bytes. Bulk data
entries retain the existing requirement for a positive record count: an empty
bulk data section is valid, but a zero-count bulk data entry is not.

Completed files require valid locators. Existing typed rules continue to require
type-aligned extents in document order, no positive overlap, and bounds within
the payload. Unreferenced gaps and trailing bytes within the declared payload
are permitted, as in the existing supplied-payload loaders. Those bytes form
part of the entity and are preserved when saving it. Ordinary saving must not
silently repack or truncate the payload; construction/output preparation may
produce packed storage.

New payload construction zeroes bytes before and between nonempty entry extents.
Output packing zeroes the inter-entry alignment gaps it creates while copying
complete records unchanged, including internal padding and unused bits. This
does not impose zero padding on existing payloads: loading, saving and stripping
preserve their gaps and trailing bytes exactly. Zero-byte entries do not reset
the preceding nonempty extent when clearing new gaps.

### Binary authority and comparison

Binary data is authoritative. Loading does not compare retained document values
against it by default. Comparison remains an explicit opt-in validation operation
using the existing role semantics. Document structure, locators and typed ranges
are always checked; opting out of value comparison does not disable those checks.
For stripped content there are no embedded values to compare.

Retained values are expected to agree with the binary in correctly produced
files. Bugs or corruption may cause disagreement, but routine loading does not
reconstruct values to establish agreement. Callers can request comparison during
validation or diagnosis; enabling it does not make the document values the
authoritative replacement for the payload.

A completed `.bid` or `.bbd` must contain its entire declared payload. Missing or
truncated binary data is an error even when retained values could reconstruct it.
Materialisation is a separate construction route for JSON or incomplete baked
descriptions, not a recovery action performed while loading a completed file.

### Type definition files

`.btd` is an ordinary types-only baked document with a `types` section, resolved
after loading. Persisting resolved lookup data is outside this increment;
it would require a separate portable representation and compatibility contract.
The `.bid`/`.bbd` metadata above is not automatically added to type documents.

## Caller-supplied schema

The caller remains responsible for supplying the correct resolved schema when
initialising typed access, whether the definitions are live or baked. There is
no automatic schema discovery or loading, and no persisted compatibility
signature in this work. The data entity does not take ownership of the schema.

Existing validation still applies: referenced types must resolve and locators,
sizes, counts and alignment must be valid against that schema. These checks do
not prove that it is the schema originally used to create the bytes. Definitions
with the same names and sizes can interpret data differently. The absence of a
signature is a deliberate continuation of caller responsibility, not a claim
that ordinary range validation establishes historical schema identity.

Schema binding follows the existing lifetime rules. The caller keeps the schema
usable while consuming the entity; schema invalidation makes typed access
unavailable through the existing binding machinery. A binding does not extend
schema ownership. A caller's binding and role lookup state must not be installed
as a globally cached mutable interface that another caller can rebind.

## Ownership and asset backing

Loose-file versus bundle-backed storage is transparent to consumption of the
specialised interface. Ownership uses the existing local-owner and Host asset
models, without adding another reference count or retention mechanism:

| Situation | Backing storage | Release responsibility |
| --- | --- | --- |
| Locally constructed entity | One locally owned document-plus-payload block. | Ordinary local destruction or explicit ownership transfer. |
| Retained loose-file entity | Host asset record owns the complete allocation. | Existing asset disposal request and completion. |
| Retained bundle-backed entity | Host sub-file record protects a range through its existing bundle backing lease. | Dispose the sub-file record; the Host releases its bundle protection. |

The caller's entity contains the specialised interface and, for Host-backed data,
records the retained asset ID that supplies its bytes. Storing that ID or copying
a borrowed view does not acquire an additional reference. Host backing leases
remain Host-managed; the caller does not retain the containing bundle separately.

Attachment validates the complete file range, attaches the document and payload,
binds the supplied schema and builds the role state before exposing a usable
entity. Construction finishes resizing and materialisation before publication.
Failure must not publish a partially usable entity. Adoption and attachment
require an empty destination; replacement in place is outside this interface.

For retained backing, the caller first quiesces all uses and borrowed entry views,
then clears the contained interface and its schema binding. When responsible for
the backing asset's disposal, the caller uses `AssetDisposeRequest` and its normal
completion. Clearing or destroying the local interface must not silently send an
asynchronous disposal request. Cache hits can share an asset ID; consumers still
coordinate disposal under the existing asset model. Attachment failure likewise
does not grant authority to dispose a shared backing record.

For local backing, the interface is cleared before its block is released.
Admission to the Host uses the existing owning-message transfer pattern. Once
the transfer is accepted, local storage ownership has been relinquished; a
successful retained result supplies the asset ID and backing views for caller
attachment. Local consumption does not require Host admission. Save and retention
outcomes continue to follow the existing asset-service contract.

An already retained bundle sub-file remains usable after closure of its bundle,
including for saving the entity to a writable destination. Its record protects
the bundle allocation until disposal. Closure blocks new admissions through the
bundle root. Saving either kind of completed entity writes its full file extent,
not merely the document prefix, and does not modify its backing storage.

The block interfaces below specify attachment, clearing and release of local
storage. Host loading, transfer and saving use the contracts below. No automatic
role movement or independently disposable duplicate asset IDs are implied. See the existing
[asset ownership contract](../assets/asynchronous_asset_services.md) and
[bundle lifetime contract](../assets/bundle_specification.md).

## Host loading, admission and saving

The baked asset path preserves the complete logical buffer while
retaining a document view limited to its validated prefix. It does not introduce
Host asset kinds specific to instances
or bulk data, a separate full-buffer load mode, or schema bindings in Host
records. The specialised blocks perform their validation and binding in the
caller context.

### Loading and returned views

For `.bid` and `.bbd`, the caller uses `AssetLoadRequest` with
`EAssetFileFormat::baked` and 128-byte alignment. The source determines ownership,
but not the resulting complete-file view:

| Source | Host preparation and retention |
| --- | --- |
| Loose file | Read the complete file, validate the bounded baked document prefix, and retain the entire allocation using `CBakedDocumentBlock::adopt_buffer()`. |
| Bundle entry | Validate the bounded baked prefix within the entry, then retain its complete file range through the existing bundle backing lease, without copying the payload. |

The existing baked `AssetResult` carries the retained asset ID and two views:
`document_view()` exposes only the validated document prefix; `byte_view()`
exposes the complete logical file extent. The latter excludes spare allocation
capacity and, for bundle entries, the bundle's external terminator and padding.
Host view construction uses `buffer_bytes()` for owned baked storage and the
exact entry range for borrowed storage. Cached results must preserve this same
contract and satisfy the requested alignment before reuse.

A successful Host load establishes a retained asset with a valid baked document
prefix. It does not validate application-specific appended data. The caller
passes the returned ID and complete byte view to `attach_asset()` on its
`CBakedInstancesBlock` or `CBakedBulkDataBlock`. Attachment validates the
persistence identifier, metadata, exact extent and padding, then the specialised
description and payload against the supplied schema. Only successful attachment
establishes a usable specialised block. Failure leaves that block empty without
modifying or disposing the retained asset; its disposal responsibility remains
with the caller under the existing sharing rules.

Ordinary baked documents without appended data retain the same byte extent.
Files with appended bytes now preserve those bytes through baked loading and
saving. Implementation must audit existing callers and tests that assume the
byte view and document view have equal lengths. The generic Host does not infer
schema compatibility or treat unvalidated appended bytes as validated payload.

### Admission and owning transfer

Locally constructed blocks use `release_local()` to move their complete storage
into a `CBakedDocumentBlock`, then transfer it with the existing
`BakedAssetTransfer`. The Host retains that complete allocation without trimming,
rebaking or copying its payload. Returned views follow the same full-buffer and
document-prefix distinction as loading. The caller can then attach its
specialised interface using the returned asset ID and bytes.

The bound specialised role, runtime lookup state and caller schema are not
transported. Retention `source` keeps the transferred representation;
`discard` is available for a combined save without subsequent retention.
Retention `baked` remains the existing live-input conditioning option, not an
additional step for these completed blocks.

An unsuccessful post preserves the sender's owning message. Once accepted,
ownership has passed to the Host under the existing transfer contract. Admission
without saving completes when the retained representation is ready; combined
saving completes after writing or failure. A save failure still returns any
successfully retained asset. Discard completion returns no retained views, and
temporary storage is released on either success or failure.

### Saving complete blocks

`AssetSaveRequest` with `EAssetFileFormat::baked` writes the retained asset's
complete current logical byte range unchanged. Combined transfer-and-save uses
the same extent. Document bytes, container padding and the entire declared
payload are preserved, including payload gaps and trailing unreferenced bytes.
Saving does not re-materialise, compare values, repack or require a usable schema
binding. JSON output remains an explicit export of the document view; it does
not persist the complete binary block or reconstruct stripped values.

All writes use the existing file I/O worker and an explicitly selected writable
filesystem destination. A bundle entry may supply the source bytes, but a bundle
binding is never a writable destination. Saving cannot insert or replace a file
inside a bundle. Existing destination resolution continues to reject such writes.

Retained sub-file IDs remain individually savable after bundle closure until
their own disposal. Saving them neither reopens the bundle nor changes its
content. Existing save/disposal ordering and operation protection apply; callers
must keep storage unchanged while accepted saves read it. Saving alone does not
dispose the source asset.

### Explicit bundle saves and automatic persistence

Bundles remain predominantly read-only access to files assembled from files and
folders in the filesystem image, usually backed by the physical filesystem.
Bundle creation remains a separate explicit operation. This work adds no
write-to-entry, insertion, replacement or automatic rebuilding facility.

An explicit whole-bundle save uses the existing raw save path and writes the
bundle's complete current logical buffer. If explicitly supported mutable
access has changed that backing storage, those changes are included. Saving
does not restore a snapshot from creation or loading. The existing whole-bundle
lifetime rules still apply: closure rejects new complete-bundle saves while
already accepted saves finish under their operation protection.

Mutation through explicit supported access is distinct from automatic
persistence. Dirty tracking, automatic invalidation, rebuilding and writing the
bundle back to disk are outside this design. Changes to independent documents
produced by parsing, promotion or copying do not update the source bundle.
There is no implicit write-back association between such a document and the
bundle entry from which it originated. Changes to the bundle's own backing
storage are reflected by a later explicit save of that current storage.

## Block interfaces and lifecycle contracts

`CBakedInstancesBlock` contains a `CBakedInstances`; `CBakedBulkDataBlock` contains
a `CBakedBulkData`. Both start empty and are non-copyable and non-movable, matching
their contained roles. Moving a completed storage block is a separate operation
from moving a bound specialised interface.

Every completed block has one contiguous document-plus-payload file range,
whether locally owned or Host-backed. The naming follows `CBakedDocumentBlock`,
but these blocks also manage their specialised role and backing association.
Private construction may use separate temporary document and payload owners;
no split or partially constructed state is exposed through the block interface.

### Specialised access and observations

The respective accessors are:

```cpp
const CBakedInstances& instances() const noexcept;
const CBakedBulkData& bulk_data() const noexcept;
```

Each returns a const reference to the contained role. Existing navigation, entry
and payload queries, and promotion are accessed through that role. The block does
not duplicate those methods. No mutable role accessor is exposed: callers cannot
independently clear the contained role, replace its document or rebind its schema.
An empty block exposes its empty role, whose normal readiness checks apply.

Both blocks share these observations:

```cpp
enum class EEntityBacking : std::uint8_t { none, local, asset };

EEntityBacking backing() const noexcept;
bool loaded_ready() const noexcept;
CAssetId asset_id() const noexcept;
CByteConstView file_bytes() const noexcept;
```

`backing()` distinguishes empty storage, a locally owned block and a retained
Host asset. There is no bundle-specific state; retained loose files and bundle
entries both use `asset`. `asset_id()` is invalid for empty or local blocks.

`loaded_ready()` follows the contained role's current typed readiness, including
schema binding validity. `file_bytes()` exposes the full persisted extent as a
read-only view, excluding spare allocation capacity and any enclosing bundle
padding. It is empty on an empty block. It remains available if the schema becomes
unavailable, because backing storage can still be saved or released independently
of typed interpretation. Neither query detects premature Host disposal; callers
must continue protecting borrowed storage under the existing asset contract.

### Adoption, attachment and failure

The lifecycle operation names and arguments are:

| Operation | Contract |
| --- | --- |
| `adopt_local(block, schema, options, diagnostic)` | Validate a complete `CBakedDocumentBlock` and take its ownership only on success. |
| `attach_asset(asset, bytes, schema, options, diagnostic)` | Validate and borrow a complete retained file range, recording its valid `CAssetId` without acquiring another backing reference. |
| `clear()` | Clear the role and binding, then release local storage or forget the Host association; leave the block empty. |
| `release_local(output_block)` | Clear the role and move local storage into an empty `CBakedDocumentBlock`, leaving the source block empty. |

Adoption, attachment and release report success or failure. Schema overloads
accept either a resolved `CBakedSchema&` or `CLiveSchema&`. Local adoption accepts
the source block by rvalue reference but consumes it only after validation and
role initialisation succeed. Attachment accepts a `CAssetId` and a complete
`CByteConstView` from the same successful asset result. The caller must supply the
correct pair: recording an ID does not verify ownership of an arbitrary pointer.

Both blocks use `SDataBlockLoadOptions`, containing only
`bool compare_embedded{ false };` initially. Diagnostics follow the shared
reporting contracts below while retaining specialised instance or bulk data
failure detail.

Both adoption and attachment require a completely empty destination and publish
a fully initialised role only on success:

- Failure on an empty destination leaves it empty.
- Failure of local adoption preserves the source block and its ownership.
- Failure of Host attachment neither modifies nor disposes the backing asset.
- Calling either operation on an occupied destination fails without changing it,
  its existing binding or its source input.

An occupied block whose schema has become unavailable is still occupied;
`loaded_ready() == false` does not make it eligible for adoption or attachment.
Replacement requires explicit clearing and subsequent initialisation. There is
no replacement-in-place operation in this interface.

### Clearing and local storage transfer

`clear()` first clears the contained role, releasing its schema binding and
lookup state. It then releases local storage, or forgets the Host asset ID and
borrowed range, and sets the backing state to `none`. Clearing an empty block is
harmless. Destruction performs the same cleanup order. Neither operation sends a
Host message or disposes a retained asset.

For Host-backed blocks, callers capture the asset ID before clearing if they
will need it for disposal. They submit `AssetDisposeRequest` separately when
responsible for that record, coordinating with any other consumers sharing its
ID. Clearing this local block alone does not establish that those consumers have
finished.

`release_local()` requires local backing and an empty output block. Host-backed
and empty sources are rejected; the operation cannot extract an allocation owned
by the Host or a containing bundle. Failed precondition checks preserve both
source and output. On success it clears the role and transfers the complete
allocation without copying its bytes. Releasing owned storage does not require
the bound schema to remain usable.

The local-to-Host transition is therefore explicit:

```text
CBakedInstancesBlock or CBakedBulkDataBlock with local backing
    -> release_local into a CBakedDocumentBlock
    -> owning transfer/admission through the existing Host mechanism
    -> attach_asset using the returned asset ID, complete bytes and caller schema
```

The bound role and its runtime lookup state are not transported. Until the
owning message is accepted, the extracted storage remains locally owned by the
block or message holding it. A rejected post does not automatically reconstruct
the original typed block; storage remains available under the existing transport
failure contract.

All uses of the contained role and borrowed entry/file views must be quiescent
before clearing, destruction or local release. No public mutable file-byte or
storage-resize accessor is introduced; construction completes before attachment.

## Construction interfaces and completed storage

The routes below converge on the same complete document-plus-payload representation:

| Source | Preparation route |
| --- | --- |
| Live instance or bulk data role | Prepare the description and authoritative payload, then assemble a complete block. |
| JSON file | Parse through the existing document facilities, materialise from sufficient values using the supplied schema, then assemble the block. |
| Baked description without binary | Prepare private construction storage, materialise from its values, then assemble the block. |
| Complete document-plus-binary file | Validate and attach directly to its existing ranges, whether loaded loose or borrowed from a bundle. |
| Complete block to be stripped | Promote/copy its document into a private live document, remove retained values, re-bake and assemble with the unchanged payload. |

The construction operations are:

| Operation | Contract |
| --- | --- |
| `CLiveInstances::bake(destination, schema, form, diagnostic)` | Produce a locally owned `CBakedInstancesBlock` from the live instance role. |
| `CLiveBulkData::bake(destination, schema, form, diagnostic)` | Produce a locally owned `CBakedBulkDataBlock` from the live bulk data role. |
| `block.materialise(description, schema, form, diagnostic)` | Construct the receiving block from a live or baked description with sufficient values. |
| `block.strip_to(destination, schema, diagnostic)` | Produce an independent stripped block of the same kind, preserving the source payload exactly. |

These operations report success or failure. Live baking adds overloads accepting
the corresponding block destination. Materialisation accepts either a
`const CLiveDocument&` or `const CBakedDocument&`. Baking and stripping leave their
source unchanged and are const operations. Schema overloads accept a resolved
`CBakedSchema&` or `CLiveSchema&` for the destination interface; schema selection
and lifetime remain the caller's responsibility. `form` is the existing
`EDataOutputForm`, retaining its embedded, external and stripped meanings.
Diagnostic semantics are specified below; implemented overload declarations are
documented in the [runtime API](runtime-api.md#completed-data-blocks).

### Live baking and description materialisation

Live baking reuses the current output preparation to produce the description and
packed authoritative payload. It adds the persistence metadata, bakes the
document and assembles the aligned complete file before initialising the
contained specialised role against that final allocation.

Materialisation prepares a private description and uses the existing type,
default, inheritance and bulk data record rules to construct the payload. Values
needed for construction are consumed before applying the requested output form.
Preparation establishes the persistence metadata and valid locators before
assembling the completed block. Neither operation changes the source document.

Binary materialisation is all or nothing: either supply the complete payload or
construct the whole payload from sufficient description values. There is no
partial materialisation mode combining supplied regions with reconstructed
missing regions. This is the selected contract, not a deferred feature. Existing
live-role snapshot/output preparation and byte-preserving stripping remain as
specified; completed-file loading never repairs an incomplete payload.

JSON parsing remains outside the block construction interface. Existing parsing
facilities supply a live or baked document; Host parsing can supply a baked
description. No additional JSON parser or file-loading entry point is introduced
inside the schema block API. The caller explicitly materialises the parsed
description, then optionally admits and saves the completed local block through
the existing Host operations:

```text
JSON load/parse -> description -> caller-side materialise -> local completed block
    -> optional Host admission and save
```

The caller keeps the source description available through materialisation and
manages its disposal separately. This increment adds no combined asynchronous
load/parse/bind/materialise/retain request or convenience orchestration service.

Source JSON and incomplete baked descriptions are construction inputs, not
necessarily already valid files of the persistence format. Preparation
must establish the format metadata as well as valid payload locators. A stripped
description lacking its required binary data cannot be materialised. Insufficient
values fail under the existing materialisation rules; zero-byte types retain
their established semantics.

### Construction ownership and failure

All construction operations require an empty destination. An occupied
destination is rejected unchanged, including when its schema binding is no longer
usable. A failed construction leaves an initially empty destination empty and
cleans up temporary allocations and bindings. It preserves the source document,
source payload and any source asset association. No partially usable role or
intermediate buffer is published.

On success the destination owns one contiguous complete file allocation and
exposes its initialised specialised role. All resizing and copying finish before
the role is attached to stable final storage. `file_bytes()` therefore always
describes one complete range, and `release_local()` transfers one allocation,
irrespective of the construction route.

Loading a completed `.bid` or `.bbd` is read-only. Constructing from JSON or an
incomplete document produces private storage; it must not modify a published
source asset or extend a bundle entry. Once construction has finished, the
independent result no longer depends on the source bytes. Any retained source
asset remains subject to its own disposal responsibility, including coordination
with other consumers. A failure cleans up temporary construction state without
modifying or implicitly disposing shared sources.

The result can be consumed locally or admitted to the Host for retention and
optional saving. The agreed threading boundary keeps schema-dependent
construction and interface binding in the caller's context. Existing Host workers
can provide file I/O and parsing. This avoids sending an unmanaged caller-schema
pointer into asynchronous Host work; such pointers would conflict with the
existing owning-input transport contract. Construction and stripping do not send
Host messages or dispose source assets. Loading, owning transfer and saving
follow the Host contracts above, with explicit caller-side orchestration as
specified here.

### Allocation strategy and later optimisation

The initial implementation may reuse existing baking and materialisation APIs
that produce separate document and payload owners, followed by a final assembly
copy into the contiguous allocation. Contiguity is a completed-block contract,
not a requirement that every private intermediate state share one allocation.

Direct construction into the final allocation is a likely later optimisation,
to be introduced at an appropriate point in the work to reduce copying and peak
temporary memory. It is not a prerequisite for the first implementation. The
public construction, failure and ownership contracts and persisted format remain
the same. Its timing, sizing strategy and bounds on temporary memory and
validation work remain implementation decisions.

## Live use, binary construction and retained document content

The existing specialised live documents remain the mechanisms for dynamic
creation, editing and consumption. They do not become construction-only helpers,
and this revision does not introduce a mandatory combined live editor.

Baked instance and bulk data facilities continue to support both supplied binary data
and construction of missing binary data from sufficient document values, under
the existing type, default, specialisation and bulk data record rules. Integrating
those facilities with the new persistence units should reuse their established
semantics. Private temporary payload owners are permitted initially; direct
construction into final storage is the later optimisation described above.

Document values may be retained or omitted. Stripped output retains the names,
type references, hierarchy and locators needed for navigation and binary access
while omitting instance declarations or embedded bulk data record values. Applicable
type definitions remain required, and the binary payload must be available when
document values are omitted. Stripped metadata alone cannot reconstruct missing
values.

Existing binary data remains authoritative where supplied. Persistence need not
round-trip through document literals merely to retain those bytes. The current
stripped persistence path can preserve raw encodings that reconstruction rejects,
including noncanonical booleans, undeclared enum values and fp16 nonfinite values.
The merge contract below excludes those encodings from permitted input data.
Conversion of permitted values between merge forms is a supported operation,
not a general document-representability exception. NaN payload distinctions are
not required to survive conversion for types that permit NaNs.

### Stripping previously baked input

Stripping must also be available for instance or bulk data input that did not originate
from a live-role bake, including parsed JSON and existing baked descriptions or
complete entities. It is an output transformation, not an in-place edit of a
published document or bundle entry. Required binary data must be supplied or
materialised before its document values are omitted.

For an existing complete block, `strip_to()` uses generic document promotion or
copying into a private `CLiveDocument`. It removes instance declarations or
embedded bulk data arrays, sets `stripped: true`, re-bakes the document and assembles
it with a byte-for-byte copy of the source payload in a new contiguous block.
Before removing values it preserves or derives all required navigation and
locator metadata, including bulk data counts previously inferred from embedded arrays.

This route does not promote through `CLiveInstances` or `CLiveBulkData`. Those
role promotions reconstruct literals and can reject raw encodings accepted by
the structural persistence checks. Generic document promotion copies the
description without decoding the authoritative payload. The stricter permitted
binary values agreed for merging do not retrospectively change this implemented,
byte-preserving stripping contract.

Payload-relative offsets, gaps and trailing bytes within the declared payload
are preserved; stripping does not repack or reconstruct binary data. The changed
document can have a different baked length and node indices, so the destination
role is initialised against the new document and final payload location.
Already stripped input produces an independent equivalent block under the same
contract. Local and Host-backed sources follow the same transformation, with no
source mutation or implicit asset disposal.

For JSON or baked descriptions without binary data, materialisation with
`EDataOutputForm::stripped` first constructs the required payload from sufficient
values and then omits those values in the output. It cannot strip away values
before they have served that construction step.

## Diagnostics and failure reporting

Host operation status and block diagnostics remain separate. Host I/O, parsing,
retention and saving use `AssetResult`, existing `EAssetStatus` values and parser
findings. Schema block operations use structured caller-side diagnostics without
enlarging the Host completion message or transporting schema bindings.

### Shared options and diagnostic structure

Both block types use the same load options:

```cpp
struct SDataBlockLoadOptions
{
    bool compare_embedded{ false };
};
```

`SInstancesBlockDiagnostic` and `SBulkDataBlockDiagnostic` each contain a shared
stage, a shared block-level reason, optional location information, and the
corresponding existing `SInstanceDiagnostic` or `SBulkDataDiagnostic` when a
specialised operation supplied the failure. The existing role reason is
preserved rather than flattened into a generic format or construction failure.
The [runtime API](runtime-api.md#completed-data-blocks) and
[`data_block.hpp`](../../core/schema/data_block.hpp) specify the implemented
stage/reason enums, field layout and location discriminators.

Operations retain the `bool` result plus diagnostic output convention. They reset
the diagnostic on entry. Success leaves it empty; failure always supplies a
reason. Processing stops at the first failure in the defined validation or
construction order, without accumulating a multi-error report. Unused detail
and location fields remain empty. Core block operations return diagnostics;
callers supply logging, filenames, operation context and user-facing messages.

### Stages and reason ownership

| Stage | Responsibility |
| --- | --- |
| Preconditions | Reject occupied destinations, invalid sources, views or asset IDs, and unsupported options. |
| Format validation | Validate the bounded baked encoding, persistence root metadata, base alignment, complete extent and container padding. |
| Schema binding | Establish the supplied resolved schema binding. |
| Specialised loading | Validate instance or bulk data structure, type references, locators and ranges, plus embedded values when comparison is requested. |
| Construction steps | Distinguish description preparation, payload materialisation, document promotion, document baking and final assembly. |

Attachment checks preconditions, the format envelope, schema binding and then
specialised loading. Construction checks its preconditions and required schema
availability before preparing output; its diagnostic identifies the actual step
that failed, including final specialised attachment. Input descriptions are not
required to be valid completed persistence files before construction.

Format reasons distinguish invalid baked documents, unsupported document
encoding, missing/unknown/incorrectly typed root properties, wrong content
identifiers, unsupported persistence versions, invalid alignment, arithmetic
overflow or storage limits, complete-file length mismatch and nonzero container
padding. Report the specific cause only when the validator can establish it.
An invalid header must not be traversed merely to obtain a more detailed location.

Errors within the specialised sections retain existing role meanings:

| Example | Diagnostic |
| --- | --- |
| Declared payload extends beyond the complete file | Format validation / file-length mismatch. |
| Instance locator extends beyond the declared payload | Specialised loading / instance `invalid_range`. |
| Referenced type is absent from the supplied schema | Specialised loading / `unknown_type`. |
| Requested comparison finds different retained values | Specialised loading / `embedded_mismatch`. |
| Materialisation lacks required binary data or sufficient values | Materialisation / the applicable existing role reason. |

A truncated completed file is a format error, not a materialisation request.
Comparison remains opt-in, and a comparison failure never replaces authoritative
binary data with document values.

An unresolved or invalidated supplied schema reports `schema_unavailable`.
Otherwise unsuccessful binding reports `schema_binding_failed` unless a more
specific cause is established. Unknown types and incompatible layouts discovered
during specialised validation retain their role diagnostics. These errors do not
prove historical schema identity or introduce compatibility signatures.

Later schema invalidation makes `loaded_ready()` false through the existing
binding machinery. It does not retroactively change the diagnostic returned by
a successful operation or discard the retained complete bytes.

Construction preserves lower-level reasons where available. Allocation failure
is reported when established, and storage-limit/overflow failures are distinct
where known. If a helper returns only `false`, use an honest step-level failure
such as `document_bake_failed`; do not infer allocation failure from that alone.
The stage still identifies whether the failure arose during preparation,
materialisation, promotion, baking, assembly or final attachment.

### Location lifetime and failure atomicity

Location information identifies its source explicitly and is optional:

- Attachment diagnostics may refer to the supplied document, whose existing
  owner retains the storage after failure. Handles are usable only while that
  document remains alive and its handle identities remain valid.
- Construction diagnostics refer back to the caller's source document when a
  reliable mapping exists. A handle into a private temporary document must be
  cleared before that storage is discarded. Stage and reason remain available
  even when no source location can be preserved.
- Known format byte offsets are measured from the start of the complete file,
  not from its payload. Presence is explicit; offset zero is a valid location.
- Diagnostics do not retain documents, extend asset lifetimes or allocate strings
  to preserve temporary locations. Diagnostic creation must not require another
  successful allocation in order to report failure.

The outer diagnostic and any nested role occurrence follow the same location
lifetime rules. A temporary occurrence must not survive solely because it was
copied into the nested diagnostic. Absence of a location is preferable to a handle
that appears to identify the caller's input but belongs to discarded output.

Reporting does not change ownership or failure atomicity: an empty destination
remains empty on failure, an occupied destination is preserved, construction
leaves sources unchanged, failed adoption does not consume its local owner, and
failed attachment does not dispose the Host asset. Temporary allocations and
bindings are cleaned up without publishing a partial destination.

A successful Host load followed by failed block attachment therefore has two
valid outcomes to report: bytes were retained, but the specialised interface
could not be established. Applications can combine Host status and block detail
with their filename and operation context without replacing either result.
Save failures continue to preserve successfully retained assets under the Host
contract; a diagnostic never implies automatic disposal or retry.

## Agreed bulk data terminology and standardisation pass

The completed stage 1 implementation standardised the supporting types and domain prose
alongside the existing `CBakedBulkData`, `CLiveBulkData`, `baked_bulk_data.hpp` and
`live_bulk_data.hpp` names. Completion and validation are recorded in the
[persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence).

The agreed convention is "bulk data" in domain prose and
`BulkData`/`bulk_data` in domain-specific public identifiers, matching the main
roles and `CBakedBulkDataBlock` and `SBulkDataBlockDiagnostic`. The standardisation
pass covered supporting types, domain-specific helpers, call sites, tests and
maintained documentation. Persistence interfaces use the agreed naming.

The public supporting-type changes are recorded here as a migration reference;
the previous names are not compatibility aliases:

| Previous name | Standardised name |
| --- | --- |
| `SBulkDiagnostic` | `SBulkDataDiagnostic` |
| `EBulkLoadReason` | `EBulkDataLoadReason` |
| `SBulkEntryView` | `SBulkDataEntryView` |
| `SMutableBulkEntryView` | `SMutableBulkDataEntryView` |
| `CBulkHandle` | `CBulkDataHandle` |
| `CBulkDocumentQuery` | `CBulkDataDocumentQuery` |

The pass updated domain-specific names and references together, including headings
and prose that used "bulk" as shorthand for the schema domain. Applicable builds,
schema tests and stale-name searches validated the changes. This mechanical
stage preserved data semantics and introduced no compatibility aliases.

The diagnostic contract above reuses `SBulkDataDiagnostic`. Generic uses of
"bulk" for batched or contiguous operations
need not become "bulk data". The persisted `data` section, `.bbd` extension and
`morphic-bulk-data` identifier are unchanged by standardisation.

## Separate merge and aggregation operations

Merging remains needed, but its responsibilities belong to the individual domains.
The following operations are conceptually separate; exact APIs and whether they
share internal construction machinery remain undecided. These are unimplemented
follow-on capabilities, separate from the completed persistence scope.

| Operation | Responsibility |
| --- | --- |
| Definition merging | Reconcile independently produced type definitions and their dependencies. |
| Instance aggregation | Combine instance groups and their binary snapshots while preserving specialisation relationships. |
| Bulk data aggregation | Combine bulk data groups and their binary records. |

### Agreed non-destructive merge rules

Merging combines supplied information without silently replacing conflicting
content. A field present in only one source is included in the result. A field
present in both sources must merge successfully under the following rules;
otherwise the entire merge fails. Absence is not a conflicting value, whereas
an explicit null or empty value is supplied content.

| Content | Merge rule |
| --- | --- |
| Objects | Combine fields recursively. |
| Scalar values | Require matching values; conflicting values or incompatible value kinds fail. |
| Arrays other than the outer bulk data record sequence | Require equal lengths and merge corresponding elements recursively when directly merging descriptions. No prefix extension. |
| Outer bulk data record sequence | The shorter sequence must exactly match the prefix of the longer under the agreed value-comparison rules. Retain the longer sequence. |

The bulk data prefix rule is an exception for the sequence of records in an
entry, not a rule for arbitrary arrays. Arrays inside binary records retain the
shape required by the common definition schema. Document scalar equality and
presentation merging follow the agreed rules below; compatible binary layout
alone is not definition equality.

The equal-length rule covers type-definition arrays (including default arrays),
instance declaration arrays, positional structure descriptions and supported
auxiliary-metadata arrays. An array field supplied by only one source survives
under the missing-field rule; when both supply it, the applicable length rule
applies. Array elements are not matched by embedded names or treated as sets,
and arrays are not implicitly concatenated. Arrays inside individual bulk data
records retain their schema-defined shape.

An instance declaration may validly supply only a prefix of a fixed-size array,
with omitted values supplied by defaults or inheritance. This existing construction
behaviour does not make the underlying value variable-length or extend the bulk
data prefix exception to instance arrays. Direct document-only merging still
requires equal lengths where both descriptions supply the array. Binary-backed
sources instead compare authoritative snapshots and normalise descriptions;
differing lengths in stale or redundant retained descriptions do not independently
create a conflict. An explicit conversion of document-only sources into binary
form may likewise establish their effective values before binary comparison.

Definitions with the same type name are combined under these recursive rules.
Defaults and semantic metadata participate: missing fields can be added, but
conflicting supplied values fail. Matching names do not require identical sets
of supplied fields.
Distinct type names are included together. Definition references and document
identities must be translated into the destination, and the combined definitions
must remain valid. Automatic renaming and first/last-source conflict precedence
are not part of this merge.

Instance and bulk data aggregation requires the literal same usable definition
schema owner for every source and the result. Independently loaded or resolved
owners do not meet that requirement merely because their definitions match.
Aggregation checks the association; it does not compare separate schemas,
switch bindings or reconcile definitions implicitly. Callers explicitly switch
sources to the chosen common owner using the operation below before aggregating.
Definition merging is separate: extending a definition does not automatically
make existing binary data compatible with its resulting layout.

Base instances collide by type and name. Matching bases must have matching own
content, with child specialisations combined recursively rather than compared
as one indivisible subtree. Under a matching parent, distinct specialisation
names are included; colliding names require matching own content and recursive
combination of their children. Own content follows the document merge rules and,
where binary data exists, the snapshot comparison rules below. Each complete
snapshot remains authoritative; merging children does not recompute it from
the merged parent.

Bulk data entries collide by type and name. Their metadata follows the document
merge rules and their record sequences follow the prefix rule. This is not
concatenation: two differing record prefixes fail rather than being appended.
Instance and bulk data names occupy separate domains.

Source handles, document identities, payload offsets and resolved type indices
are not destination identities. Aggregation must remap parents and references
and construct valid destination locators. Generated storage metadata such as
offsets, payload sizes and the resulting bulk data count is derived from the
output, rather than treated as conflicting user content merely because source
placements or sequence lengths differ. Output placement follows the agreed
ordering and binary layout rules below.

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

### Agreed aggregation ownership and replacement

Instance and bulk data aggregation borrow their sources for the duration of the
operation and construct an independently owned result. The destination may be
empty or occupied, and may itself be explicitly supplied as a source. An occupied
destination does not implicitly contribute its content to the merge: only the
two explicit source arguments determine the input.

| Destination on entry | Successful operation |
| --- | --- |
| Empty | Publish the completed result. |
| Occupied, not a source | Replace its old content with the aggregate of the explicit sources. |
| Occupied and explicitly a source | Read its original content during construction, then replace it with the aggregate. |

Construct the entire replacement privately, including its document, any binary
payload, schema binding and specialised runtime lookup state. Validate the source
associations, permitted values, merge compatibility and requested output form
before committing the replacement. The final commit must not fail: all fallible
validation, conversion and allocation finish before the destination is changed.
Only once the replacement is complete and ready may the displaced destination
state be released under its existing ownership rules. Peak memory may therefore
include both the old destination and the full replacement.

This staging permits destination/source aliasing without overwriting input still
needed by the merge. The result owns independent document and binary storage;
it does not borrow storage from any source or from the displaced destination.
Specialised data destinations bind to the same caller-owned schema as the sources
through the existing binding mechanism. A generic document owner supplies backing;
the specialised interface over that backing carries the schema association,
including for document-only data. Absence of a binary payload does not require
another association mechanism. The old destination's content and binding are not source
preconditions unless that destination is also one of the two sources.

Any failure before commit discards temporary construction state and preserves
every source and the destination, including their documents, payload bytes,
schema bindings, loaded state, backing ownership and existing handles/views.
Temporary allocations and bindings are released. No partial result is published,
and no failed operation requires the caller to reconstruct the prior destination.

On success, sources other than the destination remain unchanged. If the
destination is also a source, replacement of its original state is intentional.
Callers must treat handles and views obtained from the old destination as invalid
after successful replacement; they must reacquire them from the result.

The caller keeps all borrowed source documents, payload backing and the common
schema usable and unchanged during construction. It prevents concurrent mutation
of sources and concurrent access to the destination during replacement. This is
transactional replacement, not a concurrent-publication guarantee, and introduces
no new lifetime or synchronisation mechanism.

For Host-backed inputs or an occupied Host-backed destination, the caller retains
the existing assets through construction and replacement. Aggregation constructs
a new locally owned result; it does not overwrite the old asset, modify a bundle
entry, acquire asset references or send disposal requests. Clearing displaced
borrowed state follows the existing block contract. The caller releases any old
asset retention only once no remaining consumer needs it. Successful replacement
does not automatically admit or save the new result through the Host.

These are agreed aggregation contracts. The overload families are specified
below; the internal mechanism for committing fully prepared state remains to be
designed. Existing empty-destination requirements of persistence construction
APIs are unchanged.

### Separate validated schema-switch operation

Changing an instance or bulk data document's schema association is a separate,
explicit operation. It supports association with a different live or baked
schema owner while preserving the binary representation. Descriptions may change
where necessary to preserve that representation under the target schema; the
requested representation form and existing ownership model are retained. The
result uses the selected target owner literally, allowing subsequent aggregation
to check owner identity alone.

The operation requires a usable current association and a resolved, usable target
schema. It validates before replacing the current association:

- Every type used by the document must resolve in the target schema under the
  corresponding type name. Its complete dependency graph must match in layout,
  enum meanings and other non-default semantic properties. Defaults follow the
  compatibility rules below. Matching sizes or representation compatibility
  alone is insufficient.
- Validation covers the entire used type definition, not just members mentioned
  explicitly in retained descriptions. Stripping, defaults and inheritance do
  not remove types or dependencies from validation.
- Unrelated definitions may differ or exist in only one schema. A document can
  therefore switch to a larger merged schema containing compatible used
  definitions without requiring whole-schema equality.
- Apart from the agreed default exceptions, used definitions must already match;
  the permissive combination of missing fields allowed by definition merging is
  not a general schema-switch equivalence rule. A change to a used definition's
  interpretation or layout requires a separate explicit conversion, not a switch
  that silently reinterprets existing data.
- Build the source-to-target type mapping and validate the document and any
  supplied payload under the target association. Rebuild schema-dependent
  records and lookup state rather than reusing source resolved indices.

Default compatibility is evaluated against effective use in the data being
switched, including nested construction, array defaults and instance inheritance:

| Corresponding defaults | Compatibility rule |
| --- | --- |
| Present in both and unchanged | Accept. |
| Present in both and different | Accept only if the default is unused in the data being switched. |
| Present in only one schema | Accept the presence/absence difference. |
| Absent from both | No default conflict. |

These rules identify admissible default differences; none authorises a change
to the binary representation. Value equality alone does not establish default
use: an explicit value may equal a default without depending on it. Evaluate
effective construction and current dependence rather than guessing from payload
bytes or trusting stale descriptions. Hypothetical future entries and edits are
not data currently being switched. Stripped complete snapshots require no
recovery of historical default use; their existing bytes provide the representation
to preserve.

For binary-backed data, preserve the complete existing payload byte-for-byte,
including padding and unused bits. Do not repack it, normalise its encodings or
replace authoritative bytes with reconstructed values. Retained descriptions may
be adjusted so that they continue to describe that representation under the target
schema. Stripped data remains stripped. Any later reconstruction must respect the
preserved binary representation and the existing permitted-value semantics.

For document-only data, establish its effective binary representation under the
source schema, then verify that the prepared target description produces the
same representation under the target schema. Make formerly defaulted values
explicit where necessary. Private materialisation for this validation does not
change the requested document-only output form. If preservation cannot be
achieved, the switch fails without changing the source.

For example, if an omitted `speed` member has implicit value `0` in the source
schema and the target adds an explicit default of `12`, the default difference
is admissible but the existing value must remain `0`. The target description
must express `0` where necessary rather than silently acquiring `12`.

This refines the original unchanged-description contract: description content
and its baked encoding may change, but binary data must not. Presentation and
supported auxiliary metadata follow the agreed preservation rules. Schema
switching preserves existing padding and unused bytes; aggregation separately
zeroes them in newly constructed output.

The operation uses the existing local/Host asset ownership and caller-owned schema
binding mechanisms. It does not acquire ownership of either schema, introduce a
new retention mechanism or modify shared Host asset bytes implicitly. The caller
keeps the source backing and both schemas usable and unchanged during preparation,
and keeps the target usable afterwards. How replacement descriptions are stored
for immutable or Host-backed inputs remains part of the concrete interface design.

The new association, any adjusted description and rebuilt runtime state are
published together only after all validation and allocation succeed. Missing or
mismatching used definitions, incompatible defaults, failure to preserve the
binary representation, unavailable schemas, invalid target interpretation, and
allocation or size failures must not partially switch the document. Failure
preserves the original binding, loaded state, document, payload and backing ownership; an originally
usable source remains usable. Diagnostics identify the failing validation or
construction step without requiring the caller to discard the source. A failed
switch does not repair an association that was already invalid on entry.

Current baked-role `bind_schema()` is not this operation: it clears loaded state
before binding and does not compare the source and target definitions. Subsequent
loading validates against the new schema without proving equivalence to the old
interpretation. Existing promotion and output preparation provide type-mapping
and representation-checking foundations, but also construct new output and do
not supply the agreed switch contract. Live roles and completed blocks need an
explicit interface for validated switching.

Concrete overloads, success-time handle/view invalidation and quiescence rules,
diagnostic types, and the implementation of definition/default-use comparison and
binary-preservation checks remain open.
The source's specialised interface already carries its runtime association,
including before binary loading or materialisation. Access to that binding and
source state is an implementation concern; no persisted schema-owner identity
or additional document-only association mechanism is introduced.

### Agreed source and result forms

Without a requested result form, the merge requires every source to have the
same complete form from the table below and produces that form. With an explicit
result form, mixed sources may be normalised through promotion, materialisation
or stripping in private working storage. The source form is classified by the
available descriptions and binary content, including the stripped marker where
applicable, rather than by live/baked storage or loose-file/bundle backing.

| Result form | Required content |
| --- | --- |
| Document only | Descriptions sufficient to represent the data without a binary payload. |
| Document + binary | Complete descriptions and the authoritative binary data. |
| Stripped document + binary | Navigation, type associations and locators with the authoritative binary data; value descriptions omitted. |

A stripped document without its payload is not a complete merge source. An
occupied destination's existing content does not select or constrain the result
form. Its existing form participates in source-form checks only when it is
explicitly supplied as a source. The destination owner's type must support the
selected result form as specified below. Live and baked documents can share a
conceptual form, as can loose-file and bundle-backed sources.

Partially described or reference-only bulk data with a complete supplied payload
requires an explicitly selected result form. This includes existing bulk data
`EDataOutputForm::external` output that omits record descriptions without setting
the stripped marker, and sources mixing described and reference-only entries.
Such inputs are rejected by default matching-form operation, even if all sources
have the same incomplete descriptive coverage. Explicit selection permits
normalisation to full descriptions or stripped output; it does not introduce a
fourth, partially described result form. The required payload must still be
present and valid.

The selector and overload families below express the default matching-form
policy and explicit selection requirement for partially described bulk data.
Existing `EDataOutputForm` values are not a direct mapping to these three complete
forms; selection uses a separate aggregation enum. Internal mapping to existing
construction paths remains implementation work.

Document-only sources can be materialised using their associated schema. Retained
descriptions can be stripped while preserving binary data. Binary-backed sources
can be promoted to full descriptions, including document-only output, using the
authoritative binary values. Internal conversion is allowed where useful; an
unnecessary conversion need not be performed when direct comparison and copying
produce the required result. No partially described result form is introduced.

For example, a retained three-record source and a stripped five-record source
with a matching prefix fail the matching-form precondition. An explicit stripped
result retains five records; an explicit full-description result reconstructs
the needed descriptions and retains five records. Neither route silently drops
records or presents three descriptions as a complete five-record description.

### Agreed pairwise aggregation interfaces

Use synchronous free functions `aggregate_instances(...)` and
`aggregate_bulk_data(...)` taking exactly two borrowed source references and one
destination. Public overloads distinguish live and baked specialised source
types: `CLiveInstances` / `CBakedInstances` and `CLiveBulkData` / `CBakedBulkData`.
There are no public source adapters, source arrays or source-count arguments.
Combining more than two sources is the caller's sequence of pairwise merges.

Completed blocks provide their contained roles through `instances()` or
`bulk_data()`; local or Host-backed storage does not require another source kind.
The first source is processed before the second for first-encounter ordering.
The destination may alias either source, with replacement only after successful
construction. All source backing and schema lifetime requirements remain the
caller's responsibility through the operation.

There is no schema argument. Both sources must already expose an association
with the literal same usable schema owner. The operation observes those bindings,
rejects different or unusable owners, and binds a specialised result to the common
owner. An occupied destination's old association is irrelevant unless it is also
a source. Aggregation never establishes a substitute source association or performs
an implicit validated schema switch.

The specialised source interface, its schema binding and its backing storage are
distinct. Generic `CLiveDocument` and `CBakedDocument` objects do not themselves
carry the binding and are not direct public merge sources, but this does not make
document-only schema data unassociated. Instance and bulk data interfaces already
carry their schema association independently of whether binary data is present.

In particular, existing baked roles support `set_document()` and `bind_schema()`
before binary loading or materialisation. Aggregation must handle that associated
document-only state and validate sufficient descriptions under the existing form
rules, rather than impose binary-loaded readiness on every source. Access to the
existing binding identity and source state may require internal accessors or small
interface extensions; it is not a new ownership or schema-association design.

Source backing may be owned locally or retained through the Host. Aggregation
borrows the specialised views over that storage; observing the schema binding
does not transfer backing ownership or add retention. There remains no schema
parameter or public adapter at the merge boundary.

Internal adapters or promotion to a standard working form may share implementation
between overloads while preserving the agreed data and failure contracts. Mixed
live/baked pairs can be represented by explicit overloads; the supported mixed-pair
matrix remains to be specified. These choices introduce neither a universal
result owner nor another lifetime mechanism.

The existing destination types supply these backing and interface facilities:

| Destination facility | Backing and specialised interface | Supported result form |
| --- | --- | --- |
| `CLiveDocument` | Generic editable document backing; the specialised result interface is separate. | Document only. |
| `CBakedDocumentBlock` | Generic baked document backing; the specialised result interface is separate. | Document only. |
| `CLiveInstances` / `CLiveBulkData` | Own working document/payload storage and expose the specialised interface. | Full descriptions and binary data, editable. |
| `CBakedInstancesBlock` / `CBakedBulkDataBlock` | Own or attach complete backing and expose a contained specialised interface. | Completed document + binary or stripped document + binary. |

The generic owners in this table are backing facilities, not interchangeable
schema-associated source views. For overloads involving separate backing and
views, the remaining interface task is to specify which backing and specialised
view the operation replaces or returns. It is not to invent another binding for
document-only data. The specialised result uses the common schema owner obtained
from the sources, and its backing must remain valid for its lifetime.

Where new backing is required, prepare that storage and the corresponding view
before publishing the result. Schema switching that adjusts an immutable or
Host-backed description has the same backing/view distinction: a replacement
may be needed, while the old backing and caller retention remain valid until
successful publication. Shared Host storage is not implicitly rewritten.

Every destination overload supports the agreed empty/occupied and explicit
destination/source aliasing contracts through private construction and replacement.
Passing `destination.instances()` or `destination.bulk_data()` as either source
is a supported aliasing case. Existing owners containing a specialised interface
remain the natural destination arguments; exact signatures for separate
backing/view results remain to be refined.

The destination type constrains which selected forms it can hold; it does not
silently select a different form or authorise conversion. An incompatible
form/destination combination fails while preserving all inputs and the destination.
For example, a live specialised destination cannot receive stripped output.
`matching_sources` with document-only inputs cannot silently materialise a binary
result merely because a completed-block destination was supplied.

Use these aggregation-specific selection types:

```cpp
enum class EAggregationOutputForm : std::uint8_t
{
    matching_sources = 0u,
    document_only,
    document_and_binary,
    stripped_document_and_binary
};

struct SAggregationOptions
{
    EAggregationOutputForm form{
        EAggregationOutputForm::matching_sources };
    bool compare_embedded{};
};
```

`compare_embedded` requests the strict retained-description consistency check
before normalisation. Without it, binary-authoritative refresh applies. Matching
source form is the default; conversion of mixed forms and partially described
bulk data requires an explicit result selection as agreed above.

The agreed operation shape is illustrated by one overload:

```cpp
[[nodiscard]] bool aggregate_instances(
    const CLiveInstances& first,
    const CLiveInstances& second,
    CBakedInstancesBlock& destination,
    const SAggregationOptions& options,
    SInstancesAggregationDiagnostic& diagnostic) noexcept;
```

Provide corresponding overloads for baked specialised sources and each supported
destination. `aggregate_bulk_data(...)` mirrors this shape with two bulk data
role references, the bulk data destination owners and
`SBulkDataAggregationDiagnostic`. The schema is obtained from the source bindings
whether that owner is live or baked; it is not another public parameter.

The `bool` result and diagnostic-output shape are agreed. Diagnostic names above
remain provisional until their contents and location contracts are designed.
Separate result backing/view signatures, mixed live/baked source-pair overloads,
same-object-as-both-sources behaviour and the internal replacement mechanism
remain to be specified. The interface accepts a pair, so empty and single-source
lists are not public API cases. These are planned interfaces, not declarations
already implemented in the codebase.

### Agreed normalisation of descriptions and metadata

For binary-backed sources, binary values determine merge compatibility. Retained
value descriptions are a secondary representation and do not override those
values. Where the result needs descriptions, normalisation produces descriptions
consistent with the authoritative binary data. Consistent descriptions may be
reused; stale values are replaced in private result storage. A stripped result
omits value descriptions as requested. Neither route changes a source before
the agreed destination replacement commit.

For example, two colliding records whose binary `speed` member is `12` can merge
even if their retained descriptions say `10` and `12`; the resulting description
must represent `12`. If their binary `speed` values differ, the collision fails
even if their descriptions match. Here `speed` is an ordinary schema-defined
value member. Record counts and array lengths are structural constraints, not
stale scalar descriptions to repair: they must satisfy source validation and
the agreed array and bulk data prefix rules. Output counts are derived from the
successfully merged structure; inconsistent source counts are not silently fixed.

Stale retained values do not independently create conflicts after normalisation.
This is a consequence of binary authority, not permission to discard conflicting
semantic metadata. Document-only sources have no separate binary truth: their
descriptions are authoritative and follow the agreed document merge rules.

Description-versus-binary consistency checking remains opt-in. If explicitly
requested, a discrepancy fails validation before normalisation can replace or
strip the stale description. Otherwise, normalisation follows the authoritative
binary values. This does not weaken structural or permitted-value validation,
and it does not change routine persistence loading behaviour.

| Content | Normalisation policy |
| --- | --- |
| Integer notation, minimum digits, prefixes and newline-escaping preferences | Preserve where applicable to the corresponding reconstructed value; combine using the agreed presentation rules. |
| Names, type associations and instance hierarchy | Preserve their meaning and remap runtime identities as required. |
| Supported annotations and other non-value semantic fields | Preserve and merge recursively; conflicting supplied content fails. |
| Locators, payload size, resulting record counts and stripped-state markers | Regenerate for the output form and layout rather than merge source storage details. |

A stale value can still carry valid presentation intent. If a retained integer
is written as `0x000A` but its authoritative value is `12`, reconstruction can
produce `0x000C`, preserving its four-digit hexadecimal preference. Recalculate
integer storage-width metadata where necessary so that it remains valid for
the reconstructed value; presentation does not constrain storage to an invalid
width.

Where a reconstructed value has no retained presentation preference, use the
existing schema/output defaults. A generated default must not override an
explicit retained preference merely because reconstruction happened internally.
Presentation preferences from retained sources combine under the agreed radix,
digit, prefix and newline-escaping rules. Stripped output deliberately removes
value descriptions and their attached presentation preferences; no hidden
storage is introduced to retain them.

Auxiliary metadata preservation applies only to content supported at its
location. Current specialised entry formats and completed-file roots restrict
their permitted properties. Normalisation does not introduce arbitrary annotation
fields or silently accept unknown properties: unsupported content fails validation
rather than disappearing. Permitted non-value semantic content is not replaced
by binary reconstruction and remains subject to ordinary merge conflicts.

Concrete handling of value-path correspondence, presentation provenance during
reconstruction, consistency-check options and diagnostics remains implementation
design work. The normalisation policies themselves are settled.

### Agreed binary equality and permitted values

Binary comparison uses the common definition schema. Colliding instances compare
their own complete snapshots; bulk data entries compare corresponding records
over the shorter sequence. Stripped descriptions do not remove binary fields
from comparison. Binary values derived from defaults or inheritance still
participate even where no explicit document field was supplied.

Padding is ignored when deciding whether binary data matches. Storage placement
and gaps between entries likewise do not establish value conflicts. For types
that permit NaNs, NaN payload differences do not matter. Other addressable
encodings must match; the merge introduces no numerical tolerance. The existing
`compare_encoded` implementation already ignores padding and unused bit positions
and treats NaN payloads as equivalent, providing a comparison foundation.

Noncanonical boolean encodings and enum values without declared labels are
invalid merge input. The permitted fp16 domain excludes infinity and NaN.
Existing fp16 nonfinite encodings must not be admitted through stripped input.
Conversion from wider values into fp16 follows the intended finite-output
conversion policy; this is distinct from accepting an already encoded forbidden
fp16 value. The fp16 policy is deliberate for real-time use and must not be
changed to preserve infinity or NaN as part of this work.

Conversion between forms is expected to support permitted schema-defined values.
Failures include invalid data, incorrect or incompatible schemas, resource and
representation-size limits. Noncanonical booleans, undeclared enum values and
fp16 nonfinite encodings
are outside the agreed permitted input domain, not a general inability of
documents to represent valid merge data. NaN payload distinctions and padding
need not survive document-only reconstruction.

Retained and stripped sources must meet the same permitted-value rules. Where
these checks live, their diagnostics, and the exact fp16 conversion paths to
reuse remain implementation-design decisions. This agreement does not claim
that the completed persistence loaders already enforce every merge admission
rule or authorise changes to their existing contracts.

### Agreed output ordering and binary layout

Named object members follow first-encounter order. Process the first source and
then the second, retain the first occurrence of each name in place, merge a
matching name from the second source at that location, and append previously
unseen names as they are encountered. Apply this recursively to named groups, entries and instance
specialisations. Names, not ordinal positions, identify matching members: there
is no positional matching or pinning based on where a name appeared in a source.

Object order is a convenient output convention, not part of equality or conflict
handling. Reversing source order may change document order. Array order and
length rules are unchanged: definition arrays require equal lengths and recursive
element merging, while bulk data record arrays require the shorter sequence to
match the longer prefix. The surviving bulk data sequence keeps its record order.

Pack binary entries in resulting document traversal order. Instance traversal
visits each parent before its specialisations and traverses each subtree before
the next sibling. Each bulk data entry occupies one contiguous sequence of
records. Align each nonempty entry to its schema-required alignment, retaining
schema-defined member offsets, array strides and record sizes. Regenerate
locators for the resulting positions. Distinct entries have independent storage
even when their values match; packing does not introduce shared payload extents.

Completed files retain the existing 128-byte-aligned payload base. Individual
entries require their type alignment, not an additional 128-byte boundary each.
Source gaps and trailing slack are omitted. The logical payload ends at the last
occupied extent, including tail padding required by that extent's type, without
extra trailing alignment rounding. Zero-sized entries consume no payload storage.
Existing zero-payload and contiguous completed-block format rules still apply.

Zero padding and unused storage throughout the newly constructed aggregate,
including entries supplied by only one source, rather than selecting a source's
incidental bytes to preserve:

- Padding within structures and array strides, including record tail padding.
- Unused bit positions in bit structures.
- Alignment gaps between entries.
- The document-to-payload alignment gap in a completed file.

Unused storage is determined by the schema. A declared field is not unused merely
because its description is absent, its value is inherited, or its value is zero.
The existing `clear_unused_storage` facility with `EUnusedBits::clear` provides
a foundation for clearing padding and unused bits in private result storage.

NaN payload bits are part of value encodings, not padding. Since their differences
do not affect equality, preserve the encoding copied from the selected source
rather than introducing NaN canonicalisation: use the first matching instance
snapshot, or the longer bulk data sequence with the first source winning equal
lengths. Clear that copied storage's padding and unused bits. When output-form
conversion reconstructs values, the already agreed NaN equivalence still applies;
source NaN payload distinctions need not survive that conversion.

All packing and clearing occur in private destination storage. Sources remain
unchanged through construction, including when the destination is explicitly a
source; only the successful replacement commit releases the displaced state.
The zeroing policy is for newly aggregated output. Ordinary loading, saving and
stripping retain their existing byte-preservation contracts.

Aggregation is not a prerequisite for saving an independently created instance or
bulk data entity. Packaging several entities into a bundle likewise does not require
merging their documents or payloads.

## Complete document composition

It remains useful to produce a complete document containing `types`, `instances`
and `data`, for inspection, interchange or further preparation. This is a
separate, unimplemented composition operation over already compatible,
reconciled inputs.

Composition does not resolve collisions. Multiple sources within one domain are
merged or aggregated before composition, leaving a coherent definitions section,
instance section and bulk data section to assemble. Their distinct root members can
then be placed into one document without choosing between competing definitions
or entries. An unmet input precondition must not imply silent overwriting or an
implicit conflict policy.

The composition interface and its use of the agreed merge form-conversion
contract remain to be designed. Assembling navigation and locator metadata
alone does not recover omitted values; full descriptions require reconstruction
from the associated binary data where needed. A composed document is useful
output, but does not become the mandatory runtime owner or persistence form.

## Existing implementation foundations

Inspection of the current code identified the following reusable facilities:

- [`CBakedDocumentBlock`](../../core/data_model/baked_document.hpp) can retain a
  validated document prefix and appended bytes. `adopt_buffer()` preserves the
  complete logical buffer, `buffer_bytes()` exposes it for persistence, and
  `resize_buffer()` supports private construction. Ordinary document views remain
  prefix-only. Appended content validation belongs to the enclosing format.
- [`CBakedInstances`](../../core/schema/baked_instances.hpp) and
  [`CBakedBulkData`](../../core/schema/baked_bulk_data.hpp) already borrow document
  and payload views, bind a schema, validate supplied payloads and materialise
  missing payloads. Nonempty payload bases currently require 128-byte alignment.
- The live roles prepare packed output, preserve binary snapshots and support
  stripped baking. Their APIs provide both separate document/payload owners and
  completed `CBakedInstancesBlock`/`CBakedBulkDataBlock` outputs. The completed
  blocks also support materialisation, byte-preserving stripping and explicit
  local release or Host-backed attachment.
- Document subtree copying and translation, schema output, and recursive type
  compatibility checks provide ingredients for aggregation. They do not yet
  constitute the merge operation agreed above.
- The existing bundle implementation demonstrates appended storage, aligned
  payload ranges and full-buffer persistence without changing baked-document
  encoding.

These foundations supported the completed persistence delivery recorded in the
[milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence)
and provide reusable components for the active aggregation design.
Host baked-document loading now adopts and exposes the complete logical file
range, including exact bundle-entry ranges. Document navigation remains limited
to its validated prefix; specialised attachment validates the enclosing instance
or bulk data format separately.

The single persisted entity also does not imply that all resolved metadata and
runtime lookup indexes are stored in its file. Resolution and role loading build
auxiliary runtime state. Completed blocks manage their role state under the
caller-owned schema and existing asset lifetime contracts above.

## Open design decisions

The persistence scope decisions are settled and implemented, with delivery
recorded in the [milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence).
The contracts include
the version 1 format, binary authority with opt-in comparison, block lifecycle
and construction, contiguous completed storage, document-based stripping, Host full-buffer persistence,
diagnostics, caller-supplied schemas, all-or-nothing materialisation and explicit
caller-side construction after parsing. Bulk data naming standardisation is
complete without compatibility aliases.

Explicit bundle saving writes current storage. Automatic bundle persistence,
writes to bundle entries and compatibility signatures remain outside this work.

### Next discussion: shared-schema aggregation

The discussion has settled the non-destructive merge rules, recursive instance
specialisation merging, remaining array rules, bulk data prefix matching,
form-selection defaults and explicit selection for partially described bulk data,
form conversion, padding
comparison, permitted binary values, same-owner schema association, separate
validated schema switching with default compatibility and binary preservation,
aggregation ownership and transactional replacement,
document scalar equality, presentation merging, normalisation of stale
descriptions and auxiliary metadata, output ordering, binary layout, and
pairwise typed-source aggregation, source-derived schema association, destination
overloads and output-selection interfaces above.
These are design agreements, not implemented capabilities or authority to begin
implementation.

Remaining decisions are:

- Schema-switch interfaces for live roles, baked roles and completed blocks;
  result backing/view replacement; implementation of used-definition/default-use
  comparison and binary-preservation checks; replacement descriptions for
  immutable or Host-backed inputs; success-time handle/view invalidation,
  quiescence and diagnostics. Same-owner association, default compatibility,
  binary preservation with necessary description changes, used-type/dependency
  validation and failure preservation are settled.
- Result backing/view signatures where these are separate, the mixed live/baked
  source-pair overload matrix, same-object-as-both-sources behaviour, aggregation
  diagnostic contents and location contracts, and the internal mechanism for a
  non-failing replacement commit. Exactly two typed source references, no public
  adapters or schema argument, source-derived common-owner association,
  destination ownership, aliasing and failure preservation are settled. Specialised
  sources already carry their schema association; exposing existing binding
  identity and document-only readiness is implementation work, not a new lifetime
  or association mechanism.
- Internal mapping of selected forms to existing output construction paths.
  `EAggregationOutputForm`, `SAggregationOptions`, destination/form validation,
  default matching-form behaviour and explicit selection for partially described
  bulk data are settled.
- Normalisation implementation: value-path correspondence, presentation
  provenance and consistency-check options/diagnostics. Binary-authoritative
  refresh of stale descriptions, structural count validation, presentation
  preservation, semantic metadata merging and storage-metadata regeneration
  are settled.
- Definition dependency/reference translation, final combined-schema validation,
  permitted-value validation placement and conflict/conversion diagnostics.

### Separate remaining capabilities

| Work | Outstanding design |
| --- | --- |
| Definition merging | Dependency/reference mapping, combined-schema validation, interfaces and diagnostics under the agreed recursive, scalar-equality and presentation rules. |
| Validated schema switching | Concrete interfaces, definition/default-use validation, binary-preservation checks, description replacement, runtime-state rebuilding, handle/view contracts and diagnostics under the agreed atomic switch contract. |
| Instance aggregation | Separate result backing/view signatures, mixed-pair overloads, remaining boundary cases, diagnostics, replacement machinery and implementation of the agreed pairwise interfaces, normalisation and packing rules. |
| Bulk data aggregation | Separate result backing/view signatures, mixed-pair overloads, remaining boundary cases, diagnostics, replacement machinery and implementation of the agreed pairwise interfaces, normalisation and packing rules; no implicit concatenation. |
| Complete document composition | Interface and retained/reconstructed-value policy for already reconciled `types`, `instances` and `data` sections; no collision resolution. |

These capabilities do not reopen the completed persistence contracts. Internal
form conversion is allowed under the agreed merge contract; permitted values
are expected to support reconstruction without preserving padding or NaN payload
distinctions. Remaining interface details and an implementation plan remain
future work; the aggregation interface families above are agreed.

### Optional later optimisations

Direct construction into final allocation remains a likely later optimisation
of the completed staged construction path; it is not a missing persistence stage.

Selected subsets of type definitions may be useful for some live release data.
This remains a possible optimisation, not a requirement to duplicate or prune
shared schemas now. Persisted document size, resolved metadata size and navigation
cost should be assessed separately; existing resolved types and entry indexes
already avoid some repeated document traversal.

This revision does not select shader reflection, source ingestion, type conversion
or a new editor implementation. Those may produce or consume the facilities
described here, but retain their separate scope in the follow-on work.
