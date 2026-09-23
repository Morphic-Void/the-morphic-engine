# Asynchronous asset services

This reference describes the implemented asset-operation, ownership and disposal
contracts. The [module lifecycle](../modules/asynchronous_module_lifecycle.md)
describes dependent-module cleanup and worker loading/unloading. Completion
history and validation are in [completed milestones](../project/completed_milestones.md).

## Requests and ownership

`RawAssetTransfer`, `ImageAssetTransfer`, `BakedAssetTransfer` and
`LiveAssetTransfer` transfer their backing resource and optional destination
filename to the Host. `save == false` requests admission only; `save == true`
combines admission, optional conditioning and persistence. The shared settings
select raw bytes, baked binary, JSON or TGA as appropriate to the resource.

Retention is explicit:

- `source`: retain the transferred representation until explicit disposal,
  dependent-module cleanup or application exit.
- `discard`: retain it only for the combined save, then release it on either
  success or failure. Admission without an operation cannot select discard.
- `baked`: for live input, bake and retain the resulting snapshot. This also
  requests optional baking for admission without a save. The transferred live
  document is released after conditioning; a baking failure leaves no retained
  asset. Other input types cannot select this option.

The transfer envelope accounts for both its filename and its backing resource.
The Host moves the resource into a separate asset carrier, allowing request
metadata to expire when the operation finishes. Retained assets and temporary
operation storage are separate collections.

`AssetSaveRequest` names an already retained `CAssetId`; saving never disposes of
that asset. The request owns its destination filename, but does not own the asset.
It captures image encoding and document-writing settings by value. JSON output
defaults to strict JSON, with the existing writer options available explicitly.

`AssetDisposeRequest` carries only a retained `CAssetId` and can be sent by any
connected thread. The Host rejects further saves using that ID once disposal is
accepted, waits for accepted operations using it to finish, and destroys the
asset. `AssetDisposeResult` returns the same ID and a status, with no views.
Invalid, stale and already pending disposal IDs return `invalid_asset`.
Callers must quiesce all borrowed views before requesting disposal. Save results
already in transit do not extend the asset's lifetime. POD and owning requests
use separate queues, so sending a save before a disposal does not itself prove
the save has been admitted; clients must establish that ordering when needed.

The existing owning transport rejects unsuccessful posts without consuming the
sender's message. Once accepted, resource ownership remains in the Host. Client
interfaces grant access, not ownership. A retained live document is baked afresh
for each save. Retaining a baked document instead selects an independent snapshot.

## Loads and conditioning

`AssetLoadRequest` owns the filename and selects the file format, requested byte
alignment, JSON parse policy and TGA decode orientation. Every successful load
returns a retained Host asset, possibly reusing the same cached identity.
Filenames now use logical roots, such as `dev-source:/test_input.tga` and
`test-output:/result.json`. The [development filesystem image](../../development/README.md)
defines lookup, write permission, refresh and cache-association rules. Loads
require an inventoried file; saves may create a new file below a discovered
writable directory. The non-inventoried log roots are write-only through this
interface, and their writes do not add file entries. Disposal removes the image's
matching cache IDs without removing the files.
TGA decode options deliberately do not distinguish cache entries in this stage.

| Format | Processing | Returned interface |
| --- | --- | --- |
| Raw | File I/O worker reads bytes | Borrowed byte view |
| Baked | File I/O worker reads aligned bytes; Host validates and adopts | `CBakedDocument` |
| JSON | File I/O worker reads bytes; conditioning worker parses locally and bakes | `CBakedDocument` |
| TGA | File I/O worker reads bytes; conditioning worker decodes | `image::CImageView` |

Baked loads request at least 32-byte alignment. Other loads preserve the file
primitive's alignment policy and 16-byte floor. The existing primitive rejects
empty files; these produce an explicit read failure.

All asset file reads and writes run on the Host file I/O worker. Asset live-document
baking, JSON parsing/writing and TGA encoding/decoding run on the conditioning
worker. Live-to-JSON saving is one conditioning job: bake, then write that baked
view without another Host round trip. The JSON writer's terminal zero is excluded
from the saved file.

Inputs remain owned until the worker finishes reading them. Converted bytes remain
owned until file writing completes. Temporary file bytes, parser state and encoded
output are released after their operation, rather than added to permanent storage.
Image views preserve the decoded description and map the chosen decode orientation
to logical top-left coordinates. `decode_top_down` selects the codec output order;
`storage_bottom_up` describes backing rows for the image view. The conversion is
explicit at decode completion. Image metadata is prepared once on the Host before
publication, and describing a retained asset does not modify it.

## Completion and failures

Every accepted request produces one correlated `AssetResult`, including failure.
The original client slot is returned; worker slots are private operation indices.
Admission completes when the requested representation is ready. Combined saves
complete after conditioning and file writing, or at the first failure.

The result contains status, the retained asset ID (when any) and borrowed views.
Raw bytes use a compact memory view; `byte_view()` supplies the byte-buffer view.
Baked documents are returned by value, live documents by borrowed pointer.
An image result points to the immutable published `CImageView` stored beside its
Host-owned buffer. The client calls `image_view()` to copy that view locally;
local metadata changes do not alter the published view, while texels remain shared.
Discard results and failures without a retained asset return no views.
Retained views remain valid until Host shutdown. The image view's address stays
stable when its owning envelope moves into the asset repository.

Document parsing returns only `document_findings` and `document_policy` alongside
the asset status. These describe this operation, not persistent asset metadata.
Findings survive failure without a retained asset; `unexamined` policy status
means no policy decision was reached, or this operation did not parse text.
The full `CDocumentReport` stays on the conditioning thread.

Failures are logged there with `MV_REPORT`: stage and reason, the failure location
when available, and an available element start only when it differs. Linter
failures omit element information. Processing failures omit policy data; policy
rejection reports only disallowed features, and invalid options report only unknown
policy bits. Baking and JSON-writing failures report their own operation failure.
The file I/O thread reports unsuccessful loads and saves with the affected path.

A file-save failure still returns any successfully retained asset. A successfully
baked snapshot also survives failure of subsequent JSON conversion. Failed baking
with baked retention releases the transferred live input and returns no ID.

Callers must not mutate a resource between submitting an operation that reads it
and receiving completion. Multiple concurrent read/save operations are permitted;
mutation waits for all applicable completions. This is a convention, without locks,
version tracking or automatic snapshots of retained mutable assets.

POD messages retain their original 48-byte payload and 64-byte, 16-byte-aligned
carrier. Asset completions use a tagged union of mutually exclusive resource
views. Document conditioning requests likewise carry one source: text, a baked
view or a live-document pointer. Write options are borrowed from the address-stable
Host operation until conditioning completes. Union alternatives are constructed
explicitly before publication; access follows the accompanying discriminator.
All binaries must be rebuilt together for the updated payloads and system catalogue.

Asynchronous data pointers sent into the Host must accompany an ownership transfer;
retained-asset requests use IDs. Borrowing travels from the Host to workers and
clients. Infrastructure binding remains a separately established lifetime exception.

Completion transport failure is a terminal runtime error, never a silent success.
On terminal shutdown, workers are stopped before pending input storage is released;
remaining operations receive failure replies where the client transport still
works. During normal module teardown, the affected thread is joined and its
dependent retained assets are disposed of before unbinding. Independent assets
may survive module replacement. Terminal worker failure retains DLL references
until process exit; see the module lifecycle notes for that fallback.

## Acceptance and source map

- `core/system/transported_types.hpp`: requests, results, retention and aggregate
  memory-attribution hooks; system catalogues register their identities/ownership.
- `host/runtime/asset_service.*`: admission, retained storage, operation lifetimes,
  worker dispatch and correlated completion.
- `host/runtime/host_worker_thread.cpp`: file and conditioning execution.
- `host/runtime/host.cpp`: runtime routing, draining and shutdown order.
- `executive/runtime/executive_thread.cpp`: 48 named sequential scenarios plus 32
  concurrent saves through the actual Host, file and conditioning threads.

The Executive exercise checks raw byte equality and alignment, TGA pixel equality,
8/32-bit image transfers, all document value kinds, deterministic binary round trips,
normalised JSON semantics, repeated saves after live mutation, retained/discard
inputs, optional admission-time baking, retained results after save/JSON failures,
invalid IDs, malformed files and parse-policy rejection. Concurrent saves check
correlation and source lifetime. Files are written under `development/logical-roots/test-output/`;
the TGA source is `development/logical-roots/dev-source/test_input.tga`. Run the engine
from the repository root with its built Executive and Rendering DLLs available.
The normal Executive first requests its Vulkan rendering module and validates
acknowledgement and readiness before submitting the first asset operation; see
[module startup policy](../modules/asynchronous_module_lifecycle.md).
Diagnostic fixtures also cover undefined CP1252 input, numeric overflow,
unterminated text and invalid policy bits, with compact failure metadata checked
on the client and worker reports inspected in the logs.
The original 47 scenarios remain; the additional bottom-up TGA request now
checks reuse of the cached top-down representation and the same logical texels,
documenting the deliberately deferred cache-option distinction. The greyscale
transfer still exercises bottom-up source storage. Twelve additional operations
exercise queued root refreshes with concurrent file access, cached identities,
disposal/reload and read-only destination rejection.

Core regression coverage also checks compact POD message transport, image-view
copy independence, document views and borrowed conditioning options, plus nested
allocation attribution, including rejection across incompatible allocators.

## Validation

Run the built engine from the repository root with its Executive and Rendering
DLLs available to exercise 48 sequential and 32 concurrent asset operations,
followed by 12 filesystem refresh/cache operations. Core `MorphicTests -t1`
additionally checks transport, ownership, views and the filesystem-image
discovery/reconciliation contract. The filesystem suite writes a process- and
tag-qualified JSON image beneath `development/logical-roots/test-output/` for
inspection.
The [module lifecycle harness](../modules/asynchronous_module_lifecycle.md#validation)
covers disposal during outstanding saves and module teardown.

Historical build matrices and completion commits are recorded in
[completed milestones](../project/completed_milestones.md). Protocol and
implementation changes should extend the relevant coverage above; old check
counts and log filenames are evidence for their checkpoints, not current APIs.
