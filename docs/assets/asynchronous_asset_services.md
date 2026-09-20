# Asynchronous asset services

19 September 2026. Implements the asset-operation contracts settled after the
storage and image-view consolidation. Module loading/unloading on the Host worker
remains a separate follow-up.

## Requests and ownership

`RawAssetTransfer`, `ImageAssetTransfer`, `BakedAssetTransfer` and
`LiveAssetTransfer` transfer their backing resource and optional destination
filename to the Host. `save == false` requests admission only; `save == true`
combines admission, optional conditioning and persistence. The shared settings
select raw bytes, baked binary, JSON or TGA as appropriate to the resource.

Retention is explicit:

- `source`: retain the transferred representation until application exit.
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

The existing owning transport rejects unsuccessful posts without consuming the
sender's message. Once accepted, resource ownership remains in the Host. Client
interfaces grant access, not ownership. A retained live document is baked afresh
for each save. Retaining a baked document instead selects an independent snapshot.

## Loads and conditioning

`AssetLoadRequest` owns the filename and selects the file format, requested byte
alignment, JSON parse policy and TGA decode orientation. Every successful load
creates a retained Host asset.

| Format | Processing | Returned interface |
| --- | --- | --- |
| Raw | File I/O worker reads bytes | Borrowed byte view |
| Baked | File I/O worker reads aligned bytes; Host validates and adopts | `CBakedDocument` |
| JSON | File I/O worker reads bytes; conditioning worker parses locally and bakes | `CBakedDocument` |
| TGA | File I/O worker reads bytes; conditioning worker decodes | `image::CImageView` |

Baked loads request at least 32-byte alignment. Other loads preserve the file
primitive's alignment policy and 16-byte floor. The existing primitive rejects
empty files; these produce an explicit read failure.

All file reads and writes run on the Host file I/O worker. All live-document
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
works. Thread packages and retained assets are destroyed before module unbinding.

## Acceptance and review map

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
correlation and source lifetime. Files are written under `build/`; run the engine
from the repository root with its built Executive DLL available.
Diagnostic fixtures also cover undefined CP1252 input, numeric overflow,
unterminated text and invalid policy bits, with compact failure metadata checked
on the client and worker reports inspected in the logs.
The original 47 scenarios remain; an additional bottom-up TGA load checks the
same logical texels as top-down decoding. The greyscale transfer also exercises
bottom-up source storage.

Core regression coverage also checks compact POD message transport, image-view
copy independence, document views and borrowed conditioning options, plus nested
allocation attribution, including rejection across incompatible allocators.

## Coordinated readability review, 20 September

This pass changes the handling introduced by the asset-service diff. Existing
data-model types, reports and algorithms remain unchanged.

| Review items | Revision |
| --- | --- |
| R1–R2 | Named scenarios have explicit status, retained kind, policy and finding expectations. Helpers receive settings and correlation slots explicitly. Concurrent progress has its own phase, slots and completion tracking. |
| R3 | `handle_transfer_if_type_matches` names its recognition contract; acceptance remains a separate status checked by its caller. |
| R4 | Operation owners name the storage they keep alive. Temporary working views and the retained identity are separate; `finish_operation` alone constructs publishable replies. Writer-option borrows document the address-stable operation requirement. |
| R5 | Image preparation occurs at admission/decode completion before publication; `describe_views` only describes prepared resources. |
| R6 | Each phase/message association appears once in completion dispatch, with a focused transition handler. |
| R7 | Document conditioning validates, parses, bakes and writes through explicit outcomes and early failure returns. Completion posting remains common. |
| R8–R9 | Decode orientation and view addressing have distinct names. Source/view setters establish union alternatives and tags together. Image-only transfer metadata and retention rules are labelled beside their fields. |
| R10 | Initial submission failure records a nonzero failure code and Failed state. An integration regression disables the outbound queue and checks the real Executive, its diagnostic and Host-side startup rejection. |
| R11 | The legacy client TGA requests, replies, registrations and Host/Executive state identities are removed. Catalogue ordinals may change; all components are rebuilt together. Ownership tests use the current asset requests, and the remaining local state fixtures model asset loads and saves. TGA codec worker messages remain active. |

Scenario order still expresses fixture dependencies. Expected behaviour is never
derived from position. A failed check names its scenario and differing property;
status, kind, policy and findings diagnostics include actual and expected values.

## Validation

The implementation passed solution builds, policy validation, ordinary `-t1`
core suites and the Executive acceptance run in Debug/Release x64/x86. Each engine
run completed all 80 operations and exited successfully. The 32 concurrent output
files were identical and included the later mutation of the retained live source.
Engine logs contained no error or critical events. Core suites include 565
ErasedOwner checks, 187 ErasedPod checks and the unchanged 314,179 ImageView checks.

Build/test evidence for the coordinated review pass is under
`build/asset-review-{dbg64,rel64,dbg32,rel32}-*`;
engine log tags are `asset-review-dbg64-final` and `asset-review-{rel64,dbg32,rel32}`. Line-ending and diff
whitespace checks pass.

After retiring the legacy TGA protocol and compacting its catalogues, all four
configurations passed the same build, policy, core-suite and 80-operation checks.
This validation is recorded in `build/asset-retire-{dbg64,rel64,dbg32,rel32}-*`
with matching `asset-retire-{dbg64,rel64,dbg32,rel32}` engine log tags. The owning
request transport regression now checks the asset file format as well as decode
orientation, increasing its suite count from 564 to 565.

The user completed the manual review and style pass on 20 September. Explicit
defaults now document the otherwise exhaustive scenario and document-source
switches. The scenario index remains bounded by the sequential-to-concurrent
phase transition. Final Debug x64 build, policy, core-suite and 80-operation
validation passed under `build/asset-defaults-dbg64-*` and engine log tag
`asset-defaults-dbg64`. Coordinator review has no outstanding findings, and the
user has authorised the commit.
