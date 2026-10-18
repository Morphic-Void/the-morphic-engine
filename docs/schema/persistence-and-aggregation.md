Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   persistence-and-aggregation.md
Author: Ritchie Brannan
Drafting and editorial assistance: OpenAI Codex
Date:   18 Oct 2026

# Schema persistence and aggregation revision

## Status and scope

This document records the persistence discussion of 14 October 2026 and the
merge discussion of 15 October, revised on 17 and 18 October. Persistence is
implemented. The schema role rework is user-reviewed and committed in `2b99f48`:
blocks retain document and binary storage, while data roles carry schema
associations and support atomic attachment and checked/trusted switching.
The earlier persistence stages and gap-zeroing follow-up ended in `dfe076d`.

The 18 October design revision makes type-schema merging the first remaining
focus. An absent explicit default expresses no preference during schema merging
and yields to an explicit default. Conflicting supplied defaults fail. Data
merging uses document-only working input and output, with other forms converted
at the boundary. Retained instance declarations contribute directly. Stripped
instances are promoted against their original schema before merging, recovering
minimal differences along the defaults, instance and specialisation chain.
Binary data is subsequently constructed from merged declarations using the
merged schema; adopting a default may intentionally change the resulting values.

This supersedes the previous binary-snapshot aggregation rules, matching-source
form selection, same-owner source-binding precondition, and specialised-role
source/destination overload proposal. Scalar and presentation merging, recursive
instance hierarchy merging, array rules, bulk data prefix matching, transactional
replacement and output ordering remain applicable as revised below. The existing
binary-preserving schema-switch and persistence contracts are unchanged.

Definition merging, instance/bulk data aggregation and complete document
composition remain unimplemented design work. Concrete interfaces, boundary
conversion details, diagnostics and an implementation plan are not yet settled.
This revision does not authorise implementation.

The [persistence milestone](../project/completed_milestones.md#schema-instance-and-bulk-data-persistence)
and [role milestone](../project/completed_milestones.md#schema-role-attachment-and-construction)
record completed work. The [schema specification](specification.md) and
[runtime API](runtime-api.md) describe implemented behaviour. This revision
remains the design reference for follow-on merging and composition; a combined
file containing types, instances and bulk data is not the mandatory runtime or
persistence form.

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
entity. Its document and binary ranges share backing storage. The storage block
exposes those views without a schema association; the caller attaches a separate
specialised data role to them and supplies that role's schema. The corresponding
storage classes are
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

The caller's block holds storage and, for Host-backed data, records the retained
asset ID. Typed roles are attached separately to its document and payload. Storing that ID or copying
a borrowed view does not acquire an additional reference. Host backing leases
remain Host-managed; the caller does not retain the containing bundle separately.

Block attachment validates the complete file range and publishes document and
payload views. Separate role attachment binds a schema and builds typed state. Construction finishes resizing and materialisation before publication.
Failure must not publish a partially usable entity. Adoption and attachment
require an empty destination; replacement in place is outside this interface.

For retained backing, the caller first quiesces all uses and borrowed entry views,
then clears borrowing roles and the block. When responsible for
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

`CBakedInstancesBlock` and `CBakedBulkDataBlock` are schema-independent storage
owners/borrowers. They expose `document()`, `payload_view()`, `file_bytes()`,
`backing()`, `asset_id()` and storage-only `loaded_ready()`. Typed operations
use a separate `CBakedInstances` or `CBakedBulkData` borrowing their views.

| Operation | Contract |
| --- | --- |
| `adopt_local(block, diagnostic)` | Validate the envelope and consume local ownership only on success. |
| `attach_asset(asset, bytes, diagnostic)` | Validate and borrow caller-protected bytes without acquiring a reference. |
| `clear()` | Release local storage or forget the borrowed asset; caller first quiesces borrowing roles. |
| `release_local(output)` | Transfer local storage without copying into an unallocated document block. |

Adoption/attachment require an empty destination. Failure preserves both inputs
and destination. No schema argument or load options are involved. Schema lifetime
cannot change block readiness. Clear/destruction sends no Host message; asset
disposal remains caller-managed. Both block types remain non-copyable/non-movable.
The [runtime API](runtime-api.md#completed-data-blocks) provides signatures and
the separate typed-attachment example.

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
`CBakedSchema&` or `CLiveSchema&` during construction; schema selection
and lifetime remain the caller's responsibility. `form` is the existing
`EDataOutputForm`, retaining its embedded, external and stripped meanings.
Diagnostic semantics are specified below; implemented overload declarations are
documented in the [runtime API](runtime-api.md#completed-data-blocks).

### Live baking and description materialisation

Live baking reuses the current output preparation to produce the description and
packed authoritative payload. It adds the persistence metadata, bakes the
document and allocates the final aligned file. Records copy directly into its
payload region before envelope validation and storage adoption.

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
destination is rejected unchanged. A failed construction leaves an initially empty destination empty and
cleans up temporary allocations and bindings. It preserves the source document,
source payload and any source asset association. No partially usable role or
intermediate buffer is published.

On success the destination owns one contiguous complete file allocation and
exposes its document and payload views. Typed roles attach separately after
all resizing and copying have finished. `file_bytes()` therefore always
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

Live-role contiguous baking shares description/record planning with separate
output but copies records directly into the final allocation, eliminating the
intermediate packed payload and final typed reload. Description materialisation
still constructs a temporary payload before assembly. Stripping copies unchanged
source bytes into independent output. Further optimisation of materialisation
does not change file, ownership or failure contracts.

## Live use, binary construction and retained document content

The existing specialised live documents remain the mechanisms for dynamic
creation, editing and consumption. They do not become construction-only helpers,
and this revision does not introduce a mandatory combined live editor.

Baked instance and bulk data facilities continue to support both supplied binary data
and construction of missing binary data from sufficient document values, under
the existing type, default, specialisation and bulk data record rules. Integrating
those facilities with the new persistence units should reuse their established
semantics. Live baking now writes records directly into final storage;
description materialisation retains a temporary payload owner.

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
document can have a different baked length and node indices; any destination
role must attach to those new storage views.
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

### Shared diagnostic structure

Block attachment has no schema or comparison options. Value comparison belongs
to the separate role attachment.

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

Block attachment checks preconditions and the format envelope. Schema-dependent
construction/stripping uses transient typed roles; ordinary block adoption does
not bind a schema or validate typed entries. Construction checks its preconditions and required schema
availability before preparing output; its diagnostic identifies the actual step
that failed, including final envelope validation. Input descriptions are not
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

An unresolved schema supplied for construction or stripping reports
`schema_unavailable`. Unknown types and incompatible layouts discovered
during specialised validation retain their role diagnostics. These errors do not
prove historical schema identity or introduce compatibility signatures.

Later schema invalidation makes a typed role's `loaded_ready()` false through
the existing binding machinery. It does not affect block readiness, discard
storage or retroactively change a successful operation's diagnostic.

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
| Instance aggregation | Merge instance declarations and their specialisation hierarchy in document form. |
| Bulk data aggregation | Merge bulk data groups and complete record descriptions in document form. |

### Agreed non-destructive merge rules

The merge core combines document content. A field present in only one source is
included in the result. Content supplied by both sources must agree under the
following rules or the entire merge fails. There is no automatic renaming or
first/last-source precedence for conflicting values. Schema-aware structural
checks still apply; generic object union must not change the layout or
interpretation of a same-name type.

| Content | Merge rule |
| --- | --- |
| Objects | Combine fields recursively, subject to the schema type rules below. |
| Scalar values | Require matching values under the document scalar equality rules. |
| Arrays other than the outer bulk data record sequence | Require equal lengths and recursively merge corresponding elements. No prefix extension. |
| Outer bulk data record sequence | Require the shorter sequence to match the longer prefix under the document value rules; retain the longer sequence. |

The equal-length rule includes type-definition arrays, defaults supplied by both
sources, instance declaration arrays, positional structure descriptions and
supported auxiliary-metadata arrays. An array field supplied by only one source
survives. Arrays are not matched by embedded names, treated as sets or implicitly
concatenated. A valid short instance initializer remains a short initializer;
its default/inheritance completion does not relax the merge length rule when
both declarations explicitly supply that array.

### Type-schema merging and defaults

Type-schema merging is the first remaining work item. Distinct type names are
included together. Same-name types must retain compatible layout and value
interpretation, including their referenced definitions. Members, member order,
array extents, enum definitions and bit-field interpretations cannot be silently
extended or changed by ordinary missing-field merging.

Each input must independently resolve as a valid type schema before merging.
One input cannot supply missing dependencies or repair invalid declarations in
the other. The complete merged result must also resolve successfully before
publication.

Compatibility is based on resolved layout and interpretation. Natural layout
and explicitly supplied layout metadata are compatible when they resolve to
the same layout. Explicit alignment that changes the effective layout conflicts;
the absent-default rule below does not make omitted layout a wildcard. Member
order, enum label order and referenced type identity remain significant, even
when a different declaration could occupy the same number of bytes.

Merge the original document declarations and their presentation, using resolved
information only to check compatibility. Do not first regenerate declarations
with `CResolvedSchema::prepare_output`: its normalisation can change presentation
and add layout metadata. Type references remain their declared type-name strings.
Resolving the completed result establishes its own local identities and indexes;
no persistent source-index translation table is required.

Explicit defaults use the following merge policy:

| Source defaults | Result |
| --- | --- |
| Neither source supplies a default | Leave the default unspecified. |
| Only one source supplies a default | Retain that explicit default and its presentation. |
| Both supply matching defaults | Merge them using the existing scalar, array and presentation rules. |
| Both supply conflicting defaults | Reject the merge. |

A missing default means no preference for this merge. It is not an explicit zero,
null or empty array. Runtime construction still gives an omitted default its
ordinary effective meaning: zero, the first enum value, or nested type defaults.
The merge must preserve whether a default was supplied until this policy has
been applied; comparing only effective defaults would wrongly reject an absent
zero default against an explicitly supplied nonzero default. An explicit null
is not absence and remains invalid wherever the schema disallows it.

The rule applies at each supported default declaration, including members and
bit fields. When only one source supplies an array default, retain that supplied
array. When both supply arrays, the existing equal-length and recursive-value
rules apply. Omitted tails are completed by normal construction after merging;
merging does not expand an array default to its declared extent.

Adopting an explicit default can intentionally change the binary reconstructed
for instances that omit that value. This is distinct from schema switching,
which preserves an already constructed binary package. Conflicting explicit
instance values still fail document merging; an explicit instance declaration
takes precedence over the resulting schema default.

For example, if one schema omits the default for `speed` and the other supplies
`10`, the merged schema supplies `10`. An instance declaration omitting `speed`
then constructs `10`; a declaration explicitly supplying `speed: 0` constructs
zero. Two explicit schema defaults of `0` and `10` conflict.

Presentation-only differences do not establish a type conflict. Preserve and
combine them under the presentation rules below. Existing resolved-type
comparison is a reusable foundation, but definition comparison alone is not
this merge policy: the merge needs supplied-default presence as well as resolved
layout and interpretation.

### Instance and bulk data document merging

Base instances collide by type and name. Merge their declarations under the
document rules and combine child specialisations recursively. Under a matching
parent, distinct specialisation names survive; matching names merge their own
declarations and children. A missing declaration contributes no explicit values.
Absence follows schema defaults for a base and parent inheritance for a
specialisation. It does not demand equality with another source's previously
materialised snapshot.

The resulting instance binary is constructed from the merged declarations and
merged schema, visiting each parent before its specialisations. New defaults or
merged parent declarations may therefore change inherited values. This is
intentional reconstruction, not preservation of source snapshots.

Bulk data entries collide by type and name. Merge supported metadata under the
document rules and apply the prefix rule to their complete record sequences.
Bulk data records must be fully specified; defaults do not fill missing record
values. Prefix matching is not concatenation, and arrays within each record keep
the shape required by the type schema. Instance and bulk data names occupy
separate domains.

Source handles, resolved type indices and payload offsets are not destination
identities. Translate references and parents. Storage-derived metadata such as
locators, payload size and record counts must be removed or regenerated at the
boundary rather than treated as conflicting user content. Old locators must
never make reconstructed output appear to borrow discarded source bytes.

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

Merging borrows two explicit sources and constructs independently owned document
output. The destination may be empty or occupied and may itself be explicitly
supplied as a source. An occupied destination does not implicitly contribute its
content: only the two source arguments determine the merge.

| Destination on entry | Successful operation |
| --- | --- |
| Empty | Publish the completed document result. |
| Occupied, not a source | Replace its old content with the merge of the explicit sources. |
| Occupied and explicitly a source | Read its original content, then replace it with the merge. |

Construct and validate the replacement privately. All fallible validation,
translation and allocation finish before destination publication; the final
replacement must not fail. Failure discards temporary state and preserves the
sources and destination, including any associated payloads, bindings, ownership
and existing handles/views. Success invalidates old destination handles/views;
other sources remain unchanged. Peak memory may include the old destination,
converted working inputs and the complete replacement.

The document-only merge result has no data-role binding and borrows no source
storage. A later binary-construction operation attaches a role to the selected
merged schema using the existing contracts. Failure of that separate operation
does not retroactively undo an already completed document merge. If a future
convenience operation combines conversion, merge and construction, it must stage
all requested output before publication; its interface is not yet selected.

Callers keep borrowed documents, any conversion payloads and the schemas used
for conversion/validation usable and unchanged throughout their operations.
They prevent concurrent mutation of sources and concurrent access to the
destination during replacement. This introduces no new synchronisation or
lifetime mechanism.

Host-backed sources remain borrowed. Merging does not overwrite assets or bundle
entries, acquire asset references, send disposal requests, admit output or save
it automatically. Displaced borrowed state follows the existing ownership
contract; callers release old asset retention only after remaining consumers
have finished. The type-schema destination and non-failing replacement mechanism
are specified below; instance/bulk data destination mechanisms remain design
work. Existing persistence APIs retain their empty-destination requirements.

### Separate validated schema-switch operation

Implemented on all four live/baked data roles as
`switch_schema(schema, ESchemaSwitchMode::checked/trusted, diagnostic)`, with
live and baked target-schema overloads. Checked switching validates the used
representation and runs shared construction rules against existing bytes.
Trusted switching skips that proof while resolving target type names and indices.
Both preserve documents, handles, indexes, payload bytes/addresses and storage
ownership; failures preserve the original binding. No description replacement,
default-use provenance tracking or runtime-record rebuilding is involved.

Initial `attach_schema()` either validates a supplied complete package or
constructs one from descriptions. There is no associated document-only state.
Zero-byte packages are complete attached states. A checked switch can reject
stale descriptions after raw binary editing; reconciliation is explicit.
Trusted switching can restore a known-compatible association after schema loss.
The full implemented contract is in the
[runtime API](runtime-api.md#initial-attachment-and-schema-switching).

This replaces the earlier document-only switching and contained-role proposals.
The document-only merge workflow below is a separate construction operation: it
can adopt defaults and rebuild binary, while switching preserves the existing
package. It neither requires a successful checked switch before merging nor
changes the implemented switching contract.

### Agreed source and result forms

The merge core has one semantic working form: document-only input and output.
Other forms are converted at the boundary. Type-schema merging accepts two
read-only `CSchemaDocumentQuery` views, each of which can refer to a live or baked
document, and produces an independently owned, validated `CLiveDocument`.
It consumes definitions, including explicit-default presence and presentation;
it does not aggregate instance or bulk data sections. Inputs need not be
physically converted to live storage merely to read them.

Adoption into a live schema and baking are separate boundary operations. The
result carries no schema-owner binding or retained resolved cache. Existing
restrictions on replacing schema owners with attached consumers still apply;
merging does not bypass them. The concrete public document representation for
instance and bulk data merging remains to be selected.

| Data input | Preparation before document merging |
| --- | --- |
| Ordinary instance declarations, with or without retained binary | Use the declarations directly. Omitted values remain omitted. |
| Stripped instances with complete binary | Promote against the original schema, then use the reconstructed declarations without the binary package. |
| Complete bulk data descriptions | Use the complete record descriptions. |
| Stripped or reference-only bulk data with complete binary | Reconstruct the missing complete record descriptions against the original schema before merging. |

A stripped input without its required binary is incomplete and cannot be
converted. Zero-byte types still require the explicit complete-package route;
a zero-length payload is not a request to invent missing data. Partially described
bulk data must become complete at the boundary; exact conversion APIs remain to
be designed. Record counts and shapes must validate rather than being silently
repaired.

For stripped instances, promotion must use the original schema, before its
association is lost or replaced. Base instances are reconstructed as differences
from that schema's effective defaults. Each specialisation is reconstructed as
differences from its immediate parent's complete snapshot, which already reflects
the preceding defaults/instance/specialisation chain. Promotion does not populate
every member. Unchanged nested members and wholly unchanged declarations are
omitted. Arrays retain the shortest prefix through their last changed element;
earlier equal elements appear only as needed to preserve positional meaning.

A child restoring a schema-default value still declares it when it differs from
the immediate parent; promotion compares against the parent, not directly against
the schema at every level.

For example, a stripped base storing `speed = 0` under an original implicit-zero
default promotes with `speed` omitted. If the merged schema adopts default `10`,
reconstruction from that declaration produces `10`, as intended. Promoting against
the merged schema instead would emit an explicit zero to preserve the old
snapshot, defeating this default-adoption policy.

Stripping has already erased whether a default-equal value was originally
explicit. The minimal reconstructed declaration becomes the merge input; that
lost distinction and value presentation cannot be recovered. Ordinary retained
instance declarations are not automatically promoted or reconciled: doing so
could remove explicit values or presentation that remain available to merge.
Binary edits intended to contribute must be explicitly reconciled first. The
merge does not silently refresh retained declarations from binary data.

Promotion currently produces a live role with its own document and payload.
The planned boundary conversion must extract independent document-only storage
and remove or invalidate storage-derived metadata before releasing temporary
binary ownership. It must not create a live/baked data role attached without a
complete binary package. Source blocks, roles, payloads and bindings remain
unchanged; dropping binary refers to the working representation, not destroying
source storage.

The workflow is: merge type definitions; prepare document inputs, using original
schemas wherever promotion is required; merge declarations/complete records;
then optionally construct binary with the merged schema. These first two steps
may be ordered independently provided original schemas remain available and
promotion is not redirected to the merged schema.

The previous matching-source-form policy and aggregation-specific output-form
selector are superseded. Live/baked storage, retained/stripped inputs and final
persistence forms belong to boundary conversion. Existing attachment,
materialisation and output facilities supply later binary/persistence output;
merging does not itself promise to preserve source binary bytes.

### Agreed pairwise aggregation interfaces

The operations remain separate and pairwise: type-schema merging, instance
aggregation and bulk data aggregation each combine two explicit sources into
one independently owned result. First-source then second-source encounter order
and transactional replacement remain applicable. Combining additional sources is
a sequence of pairwise operations.

The previous specialised-role overload matrix, prohibition on a schema argument,
and requirement that all sources already bind the literal same schema owner are
superseded by the document-only working form. Generic documents do not carry a
schema binding. Original schemas are needed for stripped-input promotion, while
the merged schema supplies the context for validation and later construction.
Already attached sources need not first undergo a binary-preserving switch to
the merged owner merely to contribute their declarations.

For type schemas, the two existing read-only views and independently owned live
document result are selected above. The agreed function, diagnostics and
publication mechanism are specified below. Conflicts identify both original
source locations, without retaining handles into discarded temporary output.

For instance and bulk data merging, concrete source/view types, destination
ownership and schema-context parameters remain to be specified. Prefer one merge
implementation over the standard document form, with live/baked and binary-backed
conversion at the boundary. No new public adapter, universal owner, attached
document-only role state or overload family is selected by this revision.

The earlier proposed `EAggregationOutputForm`, `SAggregationOptions` and
role-to-block `aggregate_instances(...)` / `aggregate_bulk_data(...)` signatures
are retired design proposals, not implemented APIs. Output form selection and
optional consistency checking belong to explicitly designed boundary operations.
The `bool` plus diagnostic-output convention applies to type-schema merging.
For instance/bulk data merging, diagnostics must distinguish conversion,
declaration conflicts and final validation/construction failures. Their exact
structures and location lifetimes remain open, including how diagnostics relate
temporary promoted nodes to original input.

### Agreed type-schema merge function and diagnostics

The following interface is agreed design, not an implemented API:

```cpp
namespace schema
{

enum class ESchemaMergeStage : std::uint8_t
{
    none = 0u,
    first_source_validation,
    second_source_validation,
    compatibility,
    document_merge,
    result_validation
};

enum class ESchemaMergeReason : std::uint8_t
{
    none = 0u,
    invalid_source,
    incompatible_type,
    conflicting_value,
    allocation_failed,
    storage_limit,
    construction_failed,
    invalid_result
};

struct SSchemaMergeDiagnostic
{
    ESchemaMergeStage stage{ ESchemaMergeStage::none };
    ESchemaMergeReason reason{ ESchemaMergeReason::none };
    CSchemaHandle first;
    CSchemaHandle second;
    SDiagnostic validation;
};

[[nodiscard]] bool merge_type_schemas(
    const CSchemaDocumentQuery& first,
    const CSchemaDocumentQuery& second,
    CLiveDocument& destination,
    SSchemaMergeDiagnostic& diagnostic) noexcept;

}   // namespace schema
```

Each view can refer to a live or baked document, including mixed input forms.
The result is an independently owned live type-schema document. Adoption into
a schema owner and baking remain separate operations. No output-form selector,
schema-owner argument or live/baked overload matrix is required.

The destination may be empty, occupied, or explicitly supplied as either source.
An occupied destination contributes only when supplied as a source. The same
document may supply both source arguments and follows the ordinary merge rules;
aliasing does not bypass validation or conflict checks.

Diagnostics follow the existing stage/reason and borrowed-location conventions:

| Failure | Diagnostic |
| --- | --- |
| Invalid first or second source | The corresponding source-validation stage and `invalid_source`, retaining the resolver's complete `SDiagnostic`. Its handles and ranges belong to that source. |
| Incompatible resolved types | `compatibility` / `incompatible_type`, with `first` and `second` identifying the original type declarations. |
| Conflicting supplied values, including defaults | `document_merge` / `conflicting_value`, with `first` and `second` identifying the conflicting original values. |
| Allocation or storage limit | The stage where it failed and `allocation_failed` or `storage_limit`, only where the underlying operation distinguishes that cause. |
| Other document construction failure | `document_merge` / `construction_failed`; do not guess a more specific resource cause. |
| Invalid completed schema | `result_validation` / `invalid_result`, retaining the resolver's reason and stage in `validation`. Clear all temporary-document handles and ranges. |

Resource failures during resolution retain the underlying resolver detail, with
the same source/temporary-location rules. For source validation, the corresponding
`first` or `second` location may identify the resolver's occurrence; the other
remains unset. Unavailable locations remain unset. For merge conflicts,
`validation` remains defaulted; the two source handles identify the conflict.

Report the first failure and clear the diagnostic on success. Diagnostics own
no document storage; source locations remain usable only while the corresponding
source document and its identities remain valid. Final-result diagnostics must
not expose handles into destroyed staging storage. No output-to-source provenance
map is required merely to retain those temporary locations.

### Agreed type-schema transactional publication

Use private staging and the existing non-failing `CLiveDocument` move assignment:

1. Independently resolve both source documents.
2. Check overlapping types using the existing representation comparison and
   merge original declarations and presentation into a private
   `CLiveDocument staged`. Apply the explicit-default policy to the declarations,
   not effective defaults from the resolved types.
3. Resolve the complete staged document before publication.
4. Release temporary resolvers and comparison state before replacing any document
   they borrow. Preserve only valid source diagnostics on failure.
5. Publish with `destination = std::move(staged)`.

All source reads, construction, validation and allocation finish before the final
move. Failure discards staging and preserves both sources and the destination,
including existing handles/views. Success invalidates previous destination
handles/views; a source that aliases the destination is replaced only at that
point. Other sources remain unchanged. The completed result borrows no source
storage and retains no resolved cache.

The existing move assignment transfers ownership without a fallible publication
step. No new transaction class, rollback mechanism, friend access or schema-owner
state is needed. Existing bound-owner replacement restrictions still apply to
separate adoption into a schema owner. This is transactional publication within
the existing caller-controlled lifetime/concurrency contract, not synchronisation
for concurrent readers.

### Agreed normalisation of descriptions and metadata

Document values determine merge compatibility after boundary preparation.
Retained declarations are not automatically replaced by binary-authoritative
reconstruction. If raw binary edits should contribute, the caller explicitly
reconciles them before merging. Optional consistency checking may reject a
mismatch at the boundary; it must not silently select another source of truth.
Ordinary persistence loading remains binary-authoritative under its existing
contract.

For instances, preserve explicit selections and omissions through document
merging. A missing selection adopts the merged schema default or merged parent
value during later construction. Do not expand every member into the working
document or minimise existing declarations against the merged defaults, since
either can change the intended default/inheritance behaviour. Stripped inputs
use the minimal declarations obtained under their original schema as described
above. Bulk data preparation produces complete records, not default-dependent
instance declarations.

| Content | Preparation and merge policy |
| --- | --- |
| Retained integer notation, minimum digits, prefixes and newline-escaping preferences | Preserve and combine under the agreed presentation rules. |
| Reconstructed values with no retained presentation | Use existing schema/output defaults; lost source presentation is unavailable. |
| Names, type associations and instance hierarchy | Preserve meaning and translate source-local identities. |
| Supported annotations and other non-value semantic fields | Preserve and merge recursively; conflicting supplied content fails. |
| Locators, payload size, resulting record counts and stripped-state markers | Remove or regenerate at the relevant conversion/construction boundary. |

Supported annotations remain limited by each existing document format. This
revision neither introduces arbitrary annotation fields nor permits unsupported
content to disappear silently. Validation must reject unsupported properties.
Presentation merging applies to equal supplied values; it cannot resolve a
semantic conflict or recreate preferences removed by stripping.

### Agreed binary equality and permitted values

The earlier rule requiring colliding instance snapshots to match is superseded.
Existing binary is input to explicit boundary reconstruction where descriptions
are unavailable; it is not a second equality condition on a document merge.
Schema-default changes and merged parent declarations can intentionally change
new instance snapshots. Bulk data prefix matching compares the complete document
records obtained at the boundary under the document scalar and array rules.

Conversion must validate that supplied binary values can be represented under
the applicable schema. Noncanonical booleans and enum values without declared
labels remain invalid conversion input. The permitted fp16 domain excludes
infinity and NaN; stripped input must not bypass that restriction. This does not
change ordinary persistence loading or select a new fp16 conversion policy.

Existing promotion and reconciliation verify that generated declarations
reproduce meaningful source values under the original schema. Reuse that
validation and its failure reporting at the conversion boundary. The existing
`compare_encoded` rules ignore padding and unused bits and equate NaN payloads
where NaNs are permitted. Those reconstruction checks do not replace the recorded
document scalar equality rules with a binary merge predicate.

Dropping binary representation can lose padding, unused bits and NaN payload
identities. These are not preserved by the document-only merge contract. Source
buffers themselves remain unchanged. Invalid or unrepresentable values and
allocation/size-limit failures reject conversion without partial publication.

### Agreed output ordering and binary layout

Named object members follow first-encounter order: process the first source,
merge matching names from the second at their existing position, then append
previously unseen names. Apply this recursively to groups, entries and instance
specialisations. Array order remains significant: ordinary arrays require equal
lengths, while the outer bulk data sequence uses the prefix rule and retains the
longer sequence in its existing order.

This output convention does not authorise reordering the members or enum labels
of a same-name schema type where order affects layout, positional construction
or implicit defaults. Type-schema validation must preserve that meaning.

The merge result itself has no binary layout. A later construction step creates
new snapshots/records from the merged document and merged schema, deriving fresh
locators and counts. Visit instance parents before descendants; each bulk data
entry occupies a contiguous record sequence. Pack entries in document traversal
order using their schema-required alignment, member offsets, array strides and
record sizes. Distinct entries have independent storage. Zero-sized entries
consume no payload storage.

Completed output retains a 128-byte-aligned payload base. Source gaps and trailing
slack are not copied. The payload ends at the last occupied extent, including
schema-required record tail padding, without extra trailing alignment rounding.
The existing complete-block and zero-payload format rules remain applicable.

The previously agreed policy for newly constructed aggregate binary remains:
zero structure/array padding, record tail padding, unused bit positions,
inter-entry alignment gaps and document-to-payload padding. Use the existing
unused-storage facility with `EUnusedBits::clear` on private constructed output
where necessary. A declared value is not unused storage because it is omitted,
inherited or zero. Reconstructed NaNs need not retain source payload bits.

All construction occurs in independent destination storage. Ordinary loading,
saving, stripping and schema switching retain their existing byte-preservation
contracts. Aggregation is not required to save an independent entity or package
separate entities into a bundle.

## Complete document composition

Composition remains a separate, unimplemented operation assembling compatible
`types`, `instances` and `data` sections into a document for inspection,
interchange or further preparation. Multiple sources within a domain are merged
first; composition does not resolve collisions, overwrite competing sections or
introduce another default policy.

Compose the merged type definitions with document-only instance declarations
and complete bulk data records. Preserve instance omissions and specialisation
hierarchy so later construction uses the merged defaults and inheritance chain.
Boundary preparation must recover missing descriptions before composition;
locators alone are not value descriptions. Construct binary afterwards when
required using the ordinary domain-specific construction paths.

The concrete composition interface and storage-metadata handling remain design
work. A composed document does not become the mandatory runtime owner or require
a new combined binary persistence format.

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
auxiliary runtime state. Completed blocks manage storage; separately attached roles manage typed state
under the caller-owned schema and existing lifetime contracts above.

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

Type-schema merging is now the first focus, ahead of instance/bulk data
aggregation and complete document composition. The 18 October revision settles
absent-versus-explicit default merging and document-only working input/output.
Stripped instances are promoted against their original schema to recover minimal
declarations before merging; later construction uses the merged defaults.

Type-schema inputs must each be independently valid. Resolved layout and
interpretation determine compatibility, including equivalent natural and explicit
layouts; the original declarations supply defaults and presentation. References
remain names and result resolution creates fresh local identities. The selected
interface shape is two existing read-only document views and an independently
owned, validated live document result, with adoption and baking kept separate.
The `merge_type_schemas` signature, structured diagnostics, same-source behaviour
and staged move publication are now agreed above. Their implementation remains
future work, with checkpoints in the
[implementation plan](../project/type_schema_merge_implementation_plan.md).

Scalar and presentation merging, ordinary-array equality, bulk data prefix
matching, recursive specialisations, first-encounter ordering and transactional
replacement remain applicable. The former binary-snapshot merge predicate,
matching-form selection, same-owner role precondition and specialised overload
matrix are superseded. Implemented attachment and switching remain unchanged.

Remaining decisions are:

- Concrete instance/bulk data document source/view and destination types,
  schema-context parameters, aliasing details and replacement machinery. Live
  versus baked public document storage for these operations is not yet selected.
- Boundary conversion for stripped instances and incomplete bulk data, independent
  document ownership after dropping temporary binary, and removal/invalidation
  of source storage metadata. Keep source backing and original schemas alive
  until conversion finishes.
- Instance/bulk data diagnostics for declaration conflicts, conversion and
  construction failures, including source locations after temporary promotion.
  Optional consistency checks must remain explicit. Type-schema diagnostics are
  settled above.
- Composition signatures and any convenience orchestration of conversion, merge
  and later construction, without adding merge-time representation states.

These remaining data-aggregation/composition decisions precede their implementation
plans. The type-schema plan is prepared separately; neither it nor the completed
schema work authorises follow-on implementation.

### Separate remaining capabilities

| Work | Remaining work |
| --- | --- |
| Definition merging | First priority: implementation under the prepared plan for the agreed merge function, independently valid document views, compatibility/default/presentation policy, structured diagnostics and staged publication of a validated live result. |
| Schema switching | Implemented with checked/trusted modes and unchanged storage/handles; distinct from merge/reconstruction. |
| Instance aggregation | Document declarations and recursive hierarchy merging, original-schema stripped-input promotion, diagnostics and transactional output. |
| Bulk data aggregation | Complete document record merging with prefix matching; conversion of missing descriptions, diagnostics and transactional output. |
| Complete document composition | Assembly of already merged document sections; no collision resolution or mandatory combined persistence owner. |

Boundary conversion and later binary construction reuse existing facilities.
Their concrete APIs and implementation plans remain future work.

### Optional later optimisations

Live baking already copies records directly into final payload storage. Extending
direct final storage to description materialisation remains optional; it is not
a missing persistence stage.

Selected subsets of type definitions may be useful for some live release data.
This remains a possible optimisation, not a requirement to duplicate or prune
shared schemas now. Persisted document size, resolved metadata size and navigation
cost should be assessed separately; existing resolved types and entry indexes
already avoid some repeated document traversal.

This revision does not select shader reflection, source ingestion, type conversion
or a new editor implementation. Those may produce or consume the facilities
described here, but retain their separate scope in the follow-on work.
