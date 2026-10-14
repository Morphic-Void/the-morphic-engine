Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   persistence-and-aggregation.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   14 Oct 2026

# Schema persistence and aggregation revision

## Status and scope

This document records the revised direction and subsequent persistence discussion
on 14 October 2026, with their reasoning. Shared definitions, caller-supplied
schema binding, a contained specialised interface, and reuse of existing ownership
mechanisms are agreed direction. The version 1 persistence format and block
interface, construction, Host persistence and diagnostic contracts below are
also agreed.
Remaining integration details and design decisions are identified separately.
The naming standardisation pass is implemented and user-reviewed. Shared
diagnostics and internal format validation are implemented for stage 2 review;
owning blocks, construction and Host integration are not yet implemented. This revision is not a
detailed implementation plan or authority to begin code changes. Required work
passes identified below must be included when implementation is planned.

The [persistence implementation plan](persistence-implementation-plan.md) defines
the delivery stages, concrete diagnostic representation, overload shapes and
validation work for instance and bulk data persistence. It does not schedule the
separate aggregation and composition work or authorise implementation to begin.

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

The existing baked asset path will preserve the complete logical buffer while
retaining a document view limited to its validated prefix. This broadens the
current Host contract; it does not introduce Host asset kinds specific to instances
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
Diagnostic types are specified below; final overload declarations remain to be
written against these contracts.

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

Existing binary data remains authoritative where supplied. Persistence and
aggregation must not require a round trip through document literals merely to
retain those bytes. Some binary values supported by stripped output cannot be
faithfully represented by the document value grammar. Producing a complete
document value representation remains subject to those representability limits.

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
role promotions can reconstruct literals and reject otherwise valid binary
values that have no faithful document representation. Generic document promotion
copies the description without decoding the authoritative payload, so that
representability limit does not prevent stripping a valid complete block.

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
The implementation plan specifies stage/reason enum names, field layout and
location discriminators against these reporting semantics.

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

The stage 1 implementation standardises the supporting types and domain prose
alongside the existing `CBakedBulkData`, `CLiveBulkData`, `baked_bulk_data.hpp` and
`live_bulk_data.hpp` names. Validation and review status are recorded in the
implementation plan.

The agreed convention is "bulk data" in domain prose and
`BulkData`/`bulk_data` in domain-specific public identifiers, matching the main
roles and the new `CBakedBulkDataBlock` and `SBulkDataBlockDiagnostic`. Include an
explicit standardisation pass in the implementation work plan, covering existing
supporting types, domain-specific helpers, call sites, tests and maintained
documentation. New persistence interfaces use the agreed naming from the outset.

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

The pass must inventory remaining domain-specific names, apply coherent renames
and update references together. It includes documentation headings and prose
that use "bulk" as shorthand for the schema domain. Validate with the applicable
build and existing schema tests, plus a search for stale names with any deliberate
exceptions explained. This is a naming pass; it does not change data semantics.
Do not introduce compatibility aliases: update repository consumers together
with the renamed declarations. The codebase and these facilities are still
settling and do not require legacy-name support. The implementation plan places
this pass first, separately reviewable from behaviour changes.

The diagnostic contract above reuses `SBulkDataDiagnostic`. Generic uses of
"bulk" for batched or contiguous operations
need not become "bulk data". The persisted `data` section, `.bbd` extension and
`morphic-bulk-data` identifier are unchanged by standardisation.

## Separate merge and aggregation operations

Merging remains needed, but its responsibilities belong to the individual domains.
The following operations are conceptually separate; exact APIs and whether they
share internal construction machinery remain undecided.

| Operation | Responsibility |
| --- | --- |
| Definition merging | Reconcile independently produced type definitions and their dependencies. |
| Instance aggregation | Combine instance groups and their binary snapshots while preserving specialisation relationships. |
| Bulk data aggregation | Combine bulk data groups and their binary records. |

Definition merging needs an explicit policy for duplicate names, equivalent
definitions and incompatible definitions. Representation compatibility alone does
not decide whether definitions with different defaults or other semantic metadata
should be treated as the same definition. Automatic renaming, precedence and
conversion policies have not been selected.

Instance and bulk data aggregation must establish the destination schema association,
handle entry-name collisions under an agreed policy, and produce valid locators
for the resulting payload layout. Source document identities and resolved type
indices cannot simply be carried across as destination identities. Instance
aggregation must also preserve parent/specialisation relationships and the
meaning of their snapshots.

An aggregation process may construct a live output document before baking. When
inputs include binary data, that process also needs the associated payloads;
documents alone are insufficient for stripped input. Requiring all such inputs
to pass through ordinary live-role promotion would be too restrictive, because
that promotion may reconstruct values which have no faithful document literal.

Aggregation is not a prerequisite for saving an independently created instance or
bulk data entity. Packaging several entities into a bundle likewise does not require
merging their documents or payloads.

## Complete document composition

It remains useful to produce a complete document containing `types`, `instances`
and `data`, for inspection, interchange or further preparation. This is a
composition operation over already compatible, reconciled inputs.

Composition does not resolve collisions. Multiple sources within one domain are
merged or aggregated before composition, leaving a coherent definitions section,
instance section and bulk data section to assemble. Their distinct root members can
then be placed into one document without choosing between competing definitions
or entries. An unmet input precondition must not imply silent overwriting or an
implicit conflict policy.

The precise treatment of retained versus reconstructed values is still to be
designed. A request for a full document value representation is subject to the
representability limits above; assembling navigation and locator metadata does
not recover omitted values. A composed document is useful output, but does not
become the mandatory runtime owner or persistence form.

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
- The live roles already prepare packed output, preserve binary snapshots and
  support stripped baking. Their current baking APIs return separate document
  and payload owners; the new persistence units need to integrate that output.
- Document subtree copying and translation, schema output, and recursive type
  compatibility checks provide ingredients for aggregation. They do not yet
  constitute a merge operation or select its conflict policy.
- The existing bundle implementation demonstrates appended storage, aligned
  payload ranges and full-buffer persistence without changing baked-document
  encoding.

These are foundations rather than completed implementation of this revision.
Ordinary Host baked-document loading currently adopts and exposes only the
document prefix. The agreed Host extension replaces that prefix-only persistence
contract with complete-buffer preservation; specialised attachment validates the
enclosing instance or bulk data format separately.

The single persisted entity also does not imply that all resolved metadata and
runtime lookup indexes are stored in its file. Existing resolution and role
loading build auxiliary runtime state. Their lifetimes and schema bindings need
to be considered when defining an owner or transfer API.

## Open design decisions

The persistence scope decisions are settled sufficiently to prepare a concrete
implementation plan. The agreed contracts include the version 1 format, binary
authority with opt-in comparison, block lifecycle and construction, contiguous
completed storage, document-based stripping, Host full-buffer persistence,
diagnostics, caller-supplied schemas, all-or-nothing materialisation and explicit
caller-side construction after parsing. Bulk data naming standardisation is a
required work pass without compatibility aliases.

Explicit bundle saving writes current storage. Automatic bundle persistence,
writes to bundle entries and compatibility signatures remain outside this work.

### Implementation planning details

The [implementation plan](persistence-implementation-plan.md) now addresses these
items. They remain implementation and review work, not unresolved persistence
scope decisions:

- Diagnostic stage/reason enum type names, exact field layouts and location
  discriminator representation, plus final declarations of the construction
  overloads. Shared load options, diagnostic type names, stage semantics and
  location lifetime rules are settled above.
- Delivery sequencing of the bulk data standardisation pass and the persistence
  changes. The naming pass is implemented and user-reviewed as stage 1.
- Concrete Host integration changes and an audit of consumers of prefix-only
  views, including load, transfer, result, cache and save paths.
- Initial staged allocation strategy, checked sizing and temporary-memory costs,
  using existing preparation facilities followed by final assembly.
- Focused validation coverage for complete-byte preservation, loose-file/bundle
  parity, ownership on failure and diagnostic reporting.

These details refine the agreed contracts rather than reopen persistence scope.
Direct construction into final allocation remains a likely later optimisation;
its timing and implementation need not block the initial persistence plan.

### Separate future design work

The following remain outside the instance and bulk data persistence plan:

- Definition and data collision policies, deterministic output ordering and the
  accepted combinations of live, baked, retained-value and stripped inputs.
- The interface and content policy for complete document composition.

Selected subsets of type definitions may be useful for some live release data.
This remains a possible optimisation, not a requirement to duplicate or prune
shared schemas now. Persisted document size, resolved metadata size and navigation
cost should be assessed separately; existing resolved types and entry indexes
already avoid some repeated document traversal.

This revision does not select shader reflection, source ingestion, type conversion
or a new editor implementation. Those may produce or consume the facilities
described here, but retain their separate scope in the follow-on work.
