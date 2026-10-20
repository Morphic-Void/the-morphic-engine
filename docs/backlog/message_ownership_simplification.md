# Message and ownership simplification recommendations

Updated 20 October 2026 after review of the completed bundle work and later
schema persistence, preparation and composition work. The primary purpose is
improved maintainability and boilerplate reduction: fewer conventions to remember
and fewer places to edit when adding an operation. Reducing concrete message
types is secondary.

The responsibility boundaries and failed-post ownership rule below record the
agreed direction. Helper interfaces and the staged implementation outline remain
proposals for review; their names and file placement are not final. This document
does not authorise code changes or commits. Account for scaffolding and exercise
flows that may be removed before version 1.0.

The existing [erased-owner contract](../system/erased_owner.md),
[asynchronous asset services](../assets/asynchronous_asset_services.md) and
[batch runner contract](../modules/batch_runners.md) remain the implemented
behaviour. Preserve their guarantees while simplifying construction, completion
handling and transfers between requests, operations and retained assets.

## Responsibility boundaries

I/O is explicitly owned by the Host. Host conditioning is limited to data and
transformations the Host can reasonably understand. TGA handling belongs to debug
image infrastructure, separate from Rendering textures; shared byte storage does
not make TGA and Basis processing one service responsibility.

Rendering owns Basis Universal processing, input and output texture validation,
format interpretation, codec configuration, metadata generation, job dispatch
and codec startup/shutdown. The Host may retain storage on Rendering's behalf
while treating its bytes and texture metadata as opaque content.

| Boundary | Examples | Responsibility |
| --- | --- | --- |
| Client service contracts | Asset/bundle requests, asset results, disposal replies and notifications | Shared between clients and Host. |
| Rendering interface | Texture requests, work/results, formats, options and descriptions | Rendering-owned semantics with shared definitions where clients and Host need them. |
| Host worker exchanges | I/O, supported document conditioning, bundle build/preparation, module work and filesystem scans | Executable-internal contracts, including Host workers. |
| Host storage implementation | Bundle preparation records, borrowed bundle descriptors, controls and leases | Host bookkeeping rather than client protocol. |
| Debug image support | TGA encoding/decoding and their image descriptions | Separate from Rendering texture contracts. |

The Host resolves asset identities, protects input lifetimes, routes work,
retains output, maintains attribution and handles I/O/disposal. Rendering
interprets pixels, KTX2 content, mips, storage formats and HDR metadata. Shared
storage-envelope layout and destruction rules may remain known to the Host;
this does not require codec interpretation or DLL-owned deleters.

## Lifetime and protocol distinctions

Preserve three existing work arrangements:

| Arrangement | Example | Lifetime rule |
| --- | --- | --- |
| Ownership travels in a message | Client asset transfer; worker build result | Successful posting transfers ownership to the transport. |
| Worker reads borrowed input | File save, texture work, bundle build | Host input stays stable until completion. |
| Worker exclusively modifies Host-owned work storage | Bundle preparation and module work | Host retains storage but does not access the borrowed state until completion. |

These distinctions need clear declarations and consistent call sites, not a
generic job base class or runtime ownership-mode flag. In particular, bundle
preparation should retain its borrowed mutable state and small POD status reply.

Keep payload allocation ownership, module hazards and bundle backing leases
distinct. `BorrowedBundleAsset` owns only a view descriptor; the associated
`CAssetRecord` holds the lease protecting the bytes. Moving the erased owner
alone does not move that protection. Keep lease management on the Host thread,
and build on `retain_bundle_view()` to establish descriptor and lease together.
Bundle closure stops new admissions without immediately destroying storage still
protected by records or accepted operations.

Correlated replies and independent notifications are separate protocols.
Original bundle disposal produces a correlated error if rejected, but no
correlated success when accepted. Final disposal notification uses slot `-1`
after storage and binding removal and follows the original requester-instance
delivery policy. Helpers must not infer one reply per request or redirect
notifications to a replacement thread.

Schema blocks attached to Host assets borrow their ranges without acquiring a
reference. Caller lifetime obligations remain explicit. Preparation/composition
remain synchronous operations with staged publication of independent live
documents; no schema-specific message family or asynchronous API is selected.

Preserve complete logical buffers, including appended instance/bulk payloads.
Document views and `bytes()` expose the validated prefix; `buffer_bytes()`
exposes complete storage, and bundle entries expose their exact ranges. Neither
prefix length nor spare allocation capacity substitutes for persisted length.
Storage independence from typed schema interpretation remains unchanged.

See [bundle lifetime and interface contracts](../assets/bundle_specification.md),
[asset records and leases](../../core/assets/asset_repository.hpp), and
[schema storage and persistence](../schema/persistence-and-aggregation.md).

## Typed construction and inspection

Introduce a small typed layer above `CErasedOwnerMsg` for constructing and
inspecting owning messages. Message identity, correlation and owner are currently
assigned separately, and their relationship is checked by individual receivers.
Asset requests require message and payload identities to match, whereas
`FileLoadResult` deliberately carries the different payload type `LoadedFile`.

A proposed compile-time `TMessagePayload<TMessage>` trait defaults its payload
type to `TMessage`. Only differing pairs need specialisations, such as
`FileLoadResult` carrying `LoadedFile`. Existing identity registration and owner
eligibility remain required; there is no new runtime registry. Whether an empty
message is permitted stays with the receiving service and operation context.

Keep construction separate from posting. Failed posts must preserve the caller's
message and ownership, matching the transport. The current
`CExecutiveThread::post` moves an owner into a temporary message that is destroyed
on failure; migration must expose or retain the pending message instead of
hiding that loss in a Boolean-returning helper.

### Proposed construction helpers

The following names and snippets illustrate the proposed interface only.

`make_pod_message(slot, payload)` combines correlation and payload assignment:

```cpp
// Before
threading::CErasedPodMsg message;
message.set_async_slot(slot);
message.assign_payload(result);

// After
const auto message = make_pod_message(slot, result);
```

Posting stays explicit. Notifications use the same constructor with a named
`k_uncorrelated_slot` constant representing `-1`; recipient selection, timing and
delivery-failure policy remain in the service.

`make_owner_message<TMessage>(slot)` sets the message identity and correlation,
and creates the associated payload inside the caller-owned message:

```cpp
auto completion = make_owner_message<FileLoadResult>(slot);
if (LoadedFile* const loaded = completion.owner().payload<LoadedFile>())
{
    loaded->buffer = platform::filesystem::loadFile(file, 0u, alignment);
}
if (!context.post(std::move(completion)))
{
    // completion retains ownership for caller-directed retry or abandonment.
}
```

This adds no allocation beyond the payload allocation already required.
Allocation failure leaves a typed message with its correlation and no owner.
Request construction must handle that failure before posting; reply protocols
may explicitly permit the empty completion.

| Outcome | Ownership |
| --- | --- |
| Construction succeeds | Caller-held message owns the payload. |
| Posting fails | The same message retains its payload and correlation; the post preserves attribution and hazards. |
| Posting succeeds | Transport owns the payload; the caller's message is moved from. |

There is no automatic retry storage. Leaving the message's scope destroys it
normally. A function promising caller-controlled retry must accept or return the
pending message, or retain it in caller-owned state. Payload pointers are borrowed
and must not be used after successful posting. Keep existing-owner forwarding
explicit initially; add an adoption helper only if repeated uses justify it.

### Proposed inspection helper

`inspect_owner_message<TMessage>(message)` returns a typed borrowed payload
pointer and one of three states, without allocating or moving ownership:

| State | Meaning |
| --- | --- |
| `present` | Message and payload identities match; the pointer is valid while its owner remains available. |
| `empty` | Message identity matches but no owner is present; the pointer is null. |
| `invalid` | Message identity or a present payload identity is wrong; the pointer is null. |

```cpp
const auto inspected = inspect_owner_message<TextureEncodeResult>(message);
if (inspected.state == EOwnerMessageState::invalid)
{
    // Service reports a protocol error.
    return;
}
if (inspected.state == EOwnerMessageState::empty)
{
    finish_operation(slot, EAssetStatus::allocation_failed);
    return;
}
TextureEncodeResult& result = *inspected.payload;
```

The service still validates sender, correlation and active phase, decides whether
emptiness is permitted, and interprets domain status. The helper does not encode
another service state machine. Inspection preserves ownership so the service
chooses when to retain, forward or release the payload.

Relevant implementation:

- [Owning message carrier](../../core/threading/messages/CErasedOwnerMsg.hpp).
- [Asset request validation](../../host/runtime/asset_service.cpp),
  `CAssetService::request`.
- [Worker replies](../../host/runtime/host_worker_thread.cpp),
  `CHostWorkerThread::operate`.
- [Executive posting helper](../../executive/runtime/executive_thread.cpp),
  `CExecutiveThread::post`.

## Aggregate payloads by storage representation

This is an optional follow-on rather than a prerequisite for helpers. Share
storage representations where the message already identifies the
operation. `LoadedFile` and `EncodedTga` both contain only a `CByteBuffer`;
their completion tags distinguish loading from encoding. One neutrally named
byte-storage payload could serve both while preserving the separate completion
identities.

Be selective about further aggregation:

| Candidate | Recommendation |
| --- | --- |
| `LoadedFile` and `EncodedTga` | Share one byte-storage payload; retain distinct completion identities. |
| `DecodedTga` | Keep a dedicated image payload, but consider a representation-based name because it also stores directly transferred images. |
| Baked and live document wrappers | Low priority. They are small and provide straightforward nested-storage registration. |
| Raw, image, baked and live transfer requests | Keep distinct typed identities; their implementation is already aggregated through a template. |
| Texture encode and decode results | Share repetitive implementation where useful; their different storage types remain meaningful. |

Replacing every wrapper with its underlying resource would require extending
owner registration to support the resource itself as the accounting target.
That is viable, but the present member-based registration is simple. Removing a
one-line wrapper alone is a weak reason to change it. Sharing raw byte storage
must not combine debug TGA processing with Rendering texture processing. Leave
baked-storage wrappers unchanged in the first pass and preserve full-buffer
length and alignment through any later extraction changes.

See [transported types](../../core/system/transported_types.hpp) and the
[owner payload catalogue](../../core/system/system_erased_owner_payloads.def).

## Explicit resource extraction

Make resource extraction a named ownership transition, including dependency
propagation. `CAssetService::handle_transfer_if_type_matches` currently allocates
a destination owner, copies hazards, moves the resource, prepares image metadata
and derives views. Together these steps establish an important contract.

Separate that transition from request validation and save/retention orchestration.
A small set of typed extraction functions could own the rules for:

- Moving backing storage into retained or temporary asset storage.
- Preserving the appropriate module dependencies, and establishing a backing
  lease when retaining a borrowed resource.
- Preparing metadata whose address will be published.
- Leaving the request and destination in defined states on failure.

Moving a whole erased owner preserves its hazard mask. Moving a member into a
newly created owner does not automatically preserve that information. Borrowed
bundle retention additionally depends on its record lease. Prefer separate
functions for independently owned extraction and borrowed-resource retention
where their obligations differ, rather than one function with ownership flags.

Retain the separation between request metadata and retained storage. Keeping an
entire transfer request as the retained asset would avoid an allocation, but
would also retain filenames and save settings unnecessarily.

See [asset transfer admission](../../host/runtime/asset_service.cpp),
`CAssetService::handle_transfer_if_type_matches`.

## Consistent completion interpretation

The existing owning completions use several conventions:

- File and TGA completions use absent or unready storage to indicate failure.
- Texture and document completions carry explicit status when allocation succeeds.
- Empty texture and document replies indicate allocation failure.

Each operation therefore requires its own knowledge of failure representation.
Establish a common convention:

- A permitted empty owner represents an allocation-related inability to produce
  the result, which can include request-record setup as well as payload allocation.
- A present owner has the payload type declared by the protocol.
- Where callers need failure detail, the payload carries a domain-specific status.
- A wrong payload type is a protocol violation, distinct from ordinary operation
  failure.

This does not require a universal result structure or one large status enum.
File, texture, document and module statuses can remain distinct. The improvement
is consistent interpretation, particularly distinguishing malformed messages from
legitimate empty completions. Do not require owning replies for work whose output
already resides in Host storage: `BundlePrepareResult` remains a POD status.
Existing Boolean capture/adoption APIs cannot always distinguish allocation
failure from invalid input; do not claim greater diagnostic precision without
separately changing those APIs. Source-bound schema diagnostics remain local to
their existing lifetime contracts.

See [asset completion handlers](../../host/runtime/asset_service.cpp), from
`CAssetService::complete_file_load` through
`CAssetService::complete_document_conditioning`.

## Texture path simplification

Group texture formats, options, descriptions, requests and results as a
Rendering-owned shared interface. Basis headers, codec implementation and stable
job classes remain Rendering-private. The Host forwards encoding options without
interpreting them; decoding resolves and protects a source byte range while
Rendering determines whether it is supported texture content.

Rendering should establish texture validity before reporting success. Move the
current Host checks of texture-specific view readiness to that boundary; retain
message/payload validation and generic storage checks needed by the Host.

Propose one Host-private typed `finish_texture_result<TResult>(slot, message)`
helper for the encode/decode retention sequence: inspect the message, handle
permitted emptiness and domain status, check required storage, retain the owner,
describe the view and complete the client operation. Sender/phase validation
remains with dispatch. Keep distinct result types; do not force bundle publication
or document conditioning through this helper.

Update Rendering's response payload through `response.owner().payload<T>()`
instead of taking the owner out and reinserting it. A separate helper is not
needed for that change.

Consider using one Rendering-interface status enum for the codec adapter and
reply: `basis_codec::EStatus` and `ETextureStatus` currently express the same five
outcomes. Host translation to `EAssetStatus` remains legitimate service adaptation.

Retain engine-owned byte buffers and plain metadata with component-local
destruction authority. Completed texture storage must not gain a Basis object,
Rendering callback or DLL-owned deleter. Active jobs still require draining before
codec shutdown and release of their borrowed inputs. Preserve current saving
behaviour: encoded output can be saved as raw bytes; this pass does not add the
same generic save route for decoded output.

See [texture interface storage](../../core/image/texture_data.hpp),
[Rendering dispatch](../../rendering/runtime/rendering_thread.cpp),
[codec implementation](../../rendering/runtime/basis_codec.cpp), and
[texture conditioning contract](../modules/texture_codec.md).

## Shared transport transfer mechanics

As a separately reviewed follow-on, consider consolidating the common sequence in
[direct owner posting](../../core/system/erased_owner_transport.cpp) and
[owning-message posting](../../core/threading/messages/CErasedOwnerMsgTransport.cpp):
preflight, capacity checking, source-context capture, reattribution, movement into
the queue and restoration on failure.

A small internal helper could centralise that sequence and context compatibility
checks. Preserve the public wrappers' differences: direct owner transport rejects
emptiness, while an owning message may legitimately have no owner and has an
additional message identity to admit. Preserve admission and rejection ordering.

The value is a single implementation of an invariant-sensitive algorithm. Avoid
expanding this into a general transport policy framework.

## Contract organisation

Split `transported_types.hpp` into a few cohesive contract headers organised by
service and boundary, with declarations and their C++ type registrations together.
It currently combines public asset requests, retained storage, worker jobs,
texture processing, filesystem operations, module lifecycle and registrations.
The shared owner-operations implementation can include an aggregate header where
it genuinely needs the whole catalogue.

Use the responsibility groups above to select header boundaries. In particular,
`BundlePreparation` and Host worker exchanges should not enter client headers
merely because they participate in internal work. Shared SYSTEM operation-table
generation may still need an internal aggregate of concrete payload definitions.

Retain existing SYSTEM identities for the initial organisational pass. This makes
Host-private an interface/dependency boundary, not a new transport restriction.
Host-only identities such as `ModuleWorkRequest` and `BorrowedBundleAsset` are
candidates for later LOCAL conversion, with operation registration and test use
reviewed separately. Cross-component Rendering exchanges remain shared.

See [transported types](../../core/system/transported_types.hpp),
[system identities](../../core/system/system_type_ids.def) and
[owner operation generation](../../core/system/erased_owner_operations.cpp).

## Lifetimes and boundaries to preserve

Do not aggressively compress the asset operation's ownership fields. They
represent overlapping lifetimes: document conditioning can produce a retained
baked asset and temporary JSON bytes that must survive a subsequent save. A
single current-owner field would obscure that relationship.

Prefer explicit transition functions and a phase/lifetime table identifying
storage ownership, hazards, leases and borrower release points. Keep the address
stability required by worker inputs and published views. Bundle reference-only
waits must not block module cleanup that releases those references. Stop workers
before releasing borrowed input on terminal failure.

Preserve the POD/owning-message distinction and the separate batch-work protocol.
They express different ownership contracts. Aggregation should reduce repeated
rules without hiding ownership or forcing unrelated operations into one structure.

See [asset operation storage](../../host/runtime/asset_service.hpp),
[document completion](../../host/runtime/asset_service.cpp) and
[batch work](../../core/threading/CBatchWork.hpp).

## Staged implementation outline for review

Each stage is a reviewable unit; implementation and commits require separate user
instruction. Prepare concrete file placement and call-site changes before starting.

### Stage 1 Finalise placement and coverage

Inventory message/payload pairs and identify the exceptions to same-type pairing.
Propose exact header groups and the operation-table aggregate, keeping current
SYSTEM identities. Map affected paths to existing coverage and record the lifetime
table, including bundle preparation, closure, source leases and schema full-buffer
persistence. Confirm where failed messages will remain available to callers.

Review exit: the helper contracts, file map and behavioural preservation checks
are explicit; no new message framework or ownership mechanism is required.

### Stage 2 Organise declarations

Separate public service, Rendering-interface, Host-internal and debug image
declarations. Keep identities, carrier layouts and runtime behaviour unchanged.
Update includes, operation generation and Visual Studio item/filter entries where
needed, preserving their repository-specific formatting rules.

Validation: build affected binary components and test targets to verify complete
registration visibility and dependency boundaries; run applicable policy checks.

### Stage 3 Introduce helpers and migrate construction

Implement the payload association trait and the three proposed helpers, plus a
named uncorrelated-slot constant. Migrate representative POD replies, owning
requests and owning replies, then remaining applicable sites. Replace the
Executive's hidden consuming-post pattern with caller-visible pending messages.
Keep posting explicit and leave terminal delivery policy at the service boundary.

Validation: cover same/different message-payload types, allocation failure with
preserved tag/correlation, present/empty/invalid inspection, and rejected posts
preserving owner, payload address, attribution and hazards. Exercise the migrated
caller boundary as well as the transport; a passing transport test alone does
not prove that a wrapper preserves ownership.

### Stage 4 Consolidate completion handling and texture code

Apply non-consuming inspection while retaining sender/phase/correlation checks.
Distinguish malformed payloads from permitted empty completions. Implement the
shared texture retention sequence, move texture-validity checks to Rendering,
update response payloads in place, and review the single texture-status proposal.
Keep bundle POD preparation and final-disposal notifications on their own paths.

Validation: exercise texture success/failure and out-of-order completion, pending
source disposal, bundle-backed source protection, and Rendering exit with accepted
jobs. Reuse `TextureService`, `TextureBatch`, relevant bundle suites and lifecycle
coverage. Verify complete baked-buffer preservation and schema attachment through
the existing `BundleRequests` scenarios. Add focused coverage only for gaps or
changed contracts; do not consolidate the testing framework in this stage.

### Stage 5 Select remaining reductions

Review actual duplication after migration before selecting byte-storage
aggregation, extraction functions or shared transport transfer mechanics. Give
each selected change its own rationale and reviewable diff. Preserve admission
ordering, empty-owner differences, attribution rollback, hazards and leases.
LOCAL identity migration, broader asset-result redesign and new schema messages
remain outside the initial pass.

For selected changes, run targeted ownership/retention tests and the repository's
required checks. Do not repeat passing validation matrices without a new change
or unresolved concern. Update implemented contract documentation at closeout.

Reassess candidates as scaffolding and exercise flows are retired. The subsequent
[testing consolidation](testing_framework_consolidation.md) remains separate.
