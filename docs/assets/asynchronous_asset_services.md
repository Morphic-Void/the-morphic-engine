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

`AssetDisposeRequest` carries only a retained `CAssetId`. Ordinary assets may be
disposed of by any connected thread; an original bundle ID is restricted to its
requesting thread instance. The Host rejects further saves using that ID once disposal is
accepted, waits for accepted operations and acquisitions using it to finish, and destroys the
asset. `AssetDisposeResult` returns the same ID and a status, with no views.
Invalid, stale and already pending disposal IDs return `invalid_asset`.
Callers must quiesce unprotected borrowed views before requesting disposal. Save results
already in transit do not extend the asset's lifetime. POD and owning requests
use separate queues, so sending a save before a disposal does not itself prove
the save has been admitted; clients must establish that ordering when needed.
Original bundle IDs use the closure and final-notification rules below instead
of a correlated successful `AssetDisposeResult`.

The existing owning transport rejects unsuccessful posts without consuming the
sender's message. Once accepted, resource ownership remains in the Host. Client
interfaces grant access, not ownership. A retained live document is baked afresh
for each save. Retaining a baked document instead selects an independent snapshot.

The Executive prepares requests through its private
`executive/runtime/request_preparation.hpp/.cpp` unit. The caller supplies a
pending `CErasedOwnerMsg` and posts it explicitly after successful preparation.
An occupied destination is diagnosed and left intact. Allocation or path-copy
failure leaves the destination and any supplied resource unchanged. Resource
transfer happens only after all fallible preparation steps succeed.

After successful preparation, the pending message owns the transferred resource;
the original resource variable is empty. A failed post preserves that pending
message, including nested storage, attribution and hazards. The caller may retry
the same message or explicitly abandon it. The current Executive exercise flows
retain their terminal failure policy and release the local pending message on
return; they do not introduce retries. Scenario selection and fixture generation
remain in the Executive thread, separate from reusable request preparation.

## Protected acquisition and release

`AssetAcquireRequest` selects a retained asset and a read-only byte or bundle
view. The correlated `AssetAcquireResult` supplies that view and a distinct
`CAssetAcquisitionId`. Acquiring is zero-copy and does not interpret rendering
formats. Byte acquisition uses the asset's existing byte representation; it
does not encode TGA images or serialise live documents. TGA remains shared core
functionality and retains its existing load, save and disposal contracts.

Each successful acquisition independently protects its source storage. Tokens
are monotonically issued, bound to the requesting thread-package instance and
not reused when an acquisition slot is recycled. Copied views and tokens do not
create additional protection. The client must keep the source immutable while
it is acquired; acquisition guarantees lifetime, not exclusion of mutation.

`AssetReleaseRequest` ends one acquisition. All users of the acquired view,
including batch jobs, must finish before release is posted. The correlated
`AssetReleaseResult` contains the token and status, with no view. Release does
not dispose of the underlying asset. Invalid, duplicate and wrong-client tokens
return `invalid_request` without changing another acquisition. Invalid source
IDs, pending disposal and closed bundle backing reject acquisition with
`invalid_asset`; unsupported view selections return `invalid_request`. Failed
acquisition returns no token or view. If a successful acquisition reply cannot
be delivered, Host removes that acquisition before entering its failure path.

Ordinary disposal closes admission immediately and waits for every acquisition
and accepted operation. Bundle closure still removes the original identity and
closes the root immediately; existing acquired bundle and sub-file views keep
the backing alive until release. Generic acquisitions use Host-managed backing
leases and do not create complete-bundle reference records or change their
existing closure diagnostics. Final bundle notification follows storage release.

Host distinguishes work in flight from acquisitions and deferred disposal.
Holding an acquisition does not prevent a client receiving an exit request.
After terminal state was observed before queue draining, and after batch work
and return publishers have drained, Host reclaims any abandoned acquisitions.
Pending disposal replies finish before the client's package is destroyed or
reused. Executive replacement preserves a live renderer and its independent
acquisitions. Full shutdown releases acquisition bookkeeping before assets and
bundle storage. Dependency cleanup waits for acquisitions of assets dependent
on the outgoing module; independent acquisitions do not block replacement.

## Loads and conditioning

`AssetLoadRequest` owns the filename and selects the file format, requested byte
alignment, JSON parse policy and TGA decode orientation. Every successful load
returns a retained Host asset, possibly reusing the same cached identity.
Filenames use logical roots, such as `dev-source:/test_input.tga` and
`test-output:/result.json`. The [development filesystem image](../../development/README.md)
defines lookup, write permission, refresh and cache-association rules. Loads
through these roots require an inventoried file; saves may create a new file below a discovered
writable directory. The non-inventoried log roots are write-only through this
interface, and their writes do not add file entries. Disposal removes the image's
matching cache IDs without removing the files.
TGA decode options deliberately do not distinguish cache entries in this stage.
Cache hits must also provide the requested representation and byte alignment.
Successful saves associate retained raw/raw, baked/baked and image/TGA pairs;
JSON output is parsed afresh on loading. Complete bundles and converted output
do not masquerade as ordinary raw or baked cached assets.

`AssetLoadRequest` additionally accepts `native:` followed by an absolute UTF-8
native filename, such as `native:C:\Pictures\example.tga` or
`native:/home/user/Pictures/example.tga`. This reserved mount uses the same I/O,
conditioning, options, results and retained-asset lifetime, without requiring,
consulting or updating the filesystem image. Each request reads afresh, including
when the same physical file has a cached logical identity. The prefix is
ASCII-case-insensitive; filename spelling is preserved. Relative paths report
`invalid_request`; missing files report `read_failed`. Existing native-path
restrictions remain, including no Windows UNC or extended-length support.
The mount is specific to asset loads; saves and bundle load-and-bind requests
continue to require logical paths. See the [native mount contract](../../development/README.md#native-mount).

| Format | Processing | Returned interface |
| --- | --- | --- |
| Raw | File I/O worker reads bytes | Borrowed byte view |
| Baked | File I/O worker reads aligned bytes; Host validates and adopts the complete buffer | Full byte view and prefix `CBakedDocument` |
| JSON | File I/O worker reads bytes; conditioning worker parses locally and bakes | `CBakedDocument` |
| TGA | File I/O worker reads bytes; conditioning worker decodes | `image::CImageView` |

For virtual bundle roots, raw and baked views borrow the retained bundle storage
directly; JSON and TGA still use the conditioning worker and produce independent
results. Requests above 128-byte alignment are rejected before cache reuse.
Baked `byte_view()` covers the complete logical file; `document_view()` covers
only the validated document prefix. Bundle entries expose their exact stored
range, excluding separators and alignment padding. Admission protects bundle
storage through conversion and result publication, and late completion cannot
repopulate a closed binding's cache.

Loose baked loads use `adopt_buffer()`; owning baked transfers and binary saves
preserve that complete extent, excluding spare allocation capacity. JSON saves
serialise only the document. Appended bytes are opaque to the generic Host;
consumers validate any enclosing format and attach specialised interfaces.
Completed schema `.bsd` consumers request 128-byte alignment and bind
their separately owned schema after loading. Host I/O status and caller-side
format/schema diagnostics remain separate; attachment failure does not dispose
the successfully loaded asset. The
[schema runtime API](../schema/runtime-api.md#persistence-envelope)
describes admission, construction, attachment and explicit release sequences.

Baked loads request at least 32-byte alignment. Other loads preserve the file
primitive's alignment policy and 16-byte floor. The existing primitive rejects
empty files; these produce an explicit read failure.

All asset file reads and writes run on the Host file I/O worker. Asset live-document
baking, JSON parsing/writing and TGA encoding/decoding run on the conditioning
worker. A single configured background worker shares I/O and conditioning;
otherwise one worker handles I/O and one dedicated worker handles all conditioning.
The `host-workers` bootstrap setting and hardware sizing policy are documented in
[building and testing](../project/building_and_testing.md).

The Host fixes the conditioning route at startup: `bg_conditioning` when the
dedicated worker exists, otherwise `bg_file_io`. Each worker retains its own
SPSC request and completion routes. Completion validates the worker, operation
phase, message identity and present payload type before taking ownership.
Malformed payloads, unexpected senders/phases and unknown correlations are
terminal protocol violations. Rejection leaves the supplied owner and pending
operation storage intact; final shutdown joins workers before failing pending
operations and releasing their inputs or bundle reservations.

Permitted empty replies keep their ordinary failure mapping: file and bundle
file loads report `read_failed`; TGA replies report `conditioning_failed`;
document, bundle-build and texture replies report `allocation_failed`.
Wrong present payload types never masquerade as these allocation-related empty
replies. Bundle preparation still returns a POD status against Host-owned work.

Node lookups keep query state local to each call, and every bake owns separate
analysis and emission scratch, supporting concurrent read-only document access.

The document must remain unchanged and alive until all accepted operations
borrowing it have completed.

Live-to-JSON saving is one conditioning job: bake, then write that baked
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

## Bundles

`BundleBakeRequest` owns `paths.source_root`, `paths.subdirectory` and `paths.root`.
It captures a directory-backed inventory and builds on the I/O worker. The
conditioning worker prepares a private binding directory, then the Host publishes
an original bundle ID bound under the destination root. An empty
subdirectory selects the entire source root. `BundleLoadRequest` owns the logical
physical-file path `paths.file` and destination `paths.root`, reads at 128-byte
alignment, validates on the conditioning worker and publishes in the same way.
Both return `AssetResult` with `bundle_view()`. Roots are reserved before dispatch
and released on failure.
The Host commits only the prepared subtree, preserving unrelated image updates.
Built bundles reuse their validated immutable view during preparation.
Empty files and unsupported inventory entries are skipped; empty directories are
pruned. An empty result is rejected, and a genuine file-size or read failure fails
the operation. The requester keeps source content unchanged throughout baking.

`BundleReferenceRequest { source }` is a POD request accepting an open original
or complete-view bundle ID. Each success returns a distinct retained ID and the
same complete view. Save a complete bundle with the existing `AssetSaveRequest`
using raw format and an independently chosen destination filename.

The requesting thread instance owns management of the bundle; other threads may
use its view and acquire references but cannot dispose of the original ID.
An unauthorised disposal returns `invalid_request`. The owner coordinates
complete-view quiescence and release before disposal. Copying a view or reusing a cached ID
does not add a reference; newly retained records borrowing bundle bytes do.

Owner disposal of the original ID immediately closes new admissions. Retained sub-files
and accepted operations protect storage until released; independent conversion
outputs keep their own lifetimes. Existing sub-file IDs remain usable and
individually savable after closure. Clients must release complete-view references
before requesting original-ID disposal. The Host removes the drained virtual
root and storage before sending `BundleDisposedNotification { asset }` at slot
`-1` to the management owner. Complete-bundle saves and reference acquisitions
are rejected after closure, including those using additional complete-view IDs;
previously accepted operations finish normally. A late complete-view reference
is diagnosed and must still be released before storage is destroyed.
The Host retires notification routes before destroying any requester package,
including Rendering. Replacement threads never inherit them. Invalid disposal
requests still receive immediate correlated rejection responses. See the
[bundle specification](bundle_specification.md) for validation, lifetime and
notification delivery details.

## Completion and failures

Every load, transfer, save or bundle-reference request produces one correlated
`AssetResult`, including failure. Disposal uses the separate results described above.
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
Views borrow their retained record's storage and must be quiesced before its
disposal or cleanup; complete-bundle use must also cease before bundle closure.
The image view's address stays
stable when its owning envelope moves into the asset repository.

Document parsing returns only `document_findings` and `document_policy` alongside
the asset status. These describe this operation, not persistent asset metadata.
Findings survive failure without a retained asset; `unexamined` policy status
means no policy decision was reached, or this operation did not parse text.
The full `CDocumentReport` stays on the conditioning thread.

Failures are logged there with `MV_LOG_RICH(error, ...)`: stage and reason, the
failure location when available, and an available element start only when it differs. Linter
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

- `core/assets/asset_messages.hpp`: public requests, results, retention and
  aggregate memory-attribution hooks.
- `core/image/image_messages.hpp`: Host-understood TGA work and storage, supporting
  debug and diagnostic image flows.
- `core/image/texture_messages.hpp`: shared texture requests and work.
- `host/runtime/host_payloads.hpp`: private worker exchanges and retained wrappers.
  These and the TGA worker protocols use component-local identities. Adjacent
  owner fragments feed the Host and service-test LOCAL operation tables.
- `host/runtime/asset_service.*`: admission, retained storage, operation lifetimes,
  worker dispatch and correlated completion.
- `host/runtime/host_worker_thread.cpp`: file and conditioning execution.
- `host/runtime/host.cpp`: runtime routing, draining and shutdown order.
- `executive/runtime/executive_thread.cpp`: 71 named sequential scenarios plus 32
  concurrent saves through the actual Host, file, conditioning and rendering threads.

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
The 23 texture cases cover LDR/HDR encoding, all supported storage formats,
mips, quality profiles and rejected inputs; see [texture conditioning](../modules/texture_codec.md).
The ordinary `TextureService` suite checks concurrent texture admission and deferred disposal
with renderer completions controlled by the test.
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
DLLs available to exercise 71 sequential and 32 concurrent asset operations,
followed by 12 filesystem refresh/cache operations and a bundle
bake/reference/save/close/reload flow with exact-byte comparison. Core `MorphicTests -t1`
additionally checks transport, ownership, views and the filesystem-image
discovery/reconciliation contract. The filesystem suite writes a process- and
tag-qualified JSON image beneath `development/logical-roots/test-output/` for
inspection.
The [module lifecycle harness](../modules/asynchronous_module_lifecycle.md#validation)
covers disposal during outstanding saves and module teardown, bundle root reuse,
surviving sub-file references, batch borrowing, late complete-view replies and
notification retirement across Executive replacement and Rendering exit.
Its bundle cases restore the Executive's allocation-attribution baseline before exit.

Historical build matrices and completion commits are recorded in
[completed milestones](../project/completed_milestones.md). Protocol and
implementation changes should extend the relevant coverage above; old check
counts and log filenames are evidence for their checkpoints, not current APIs.
