# Message and ownership simplification recommendations

Updated 20 October 2026 after review of the completed bundle work and later
schema persistence, preparation and composition work. The primary purpose is
improved maintainability and boilerplate reduction: fewer conventions to remember
and fewer places to edit when adding an operation. Reducing concrete message
types is secondary.

Stage 1 design is agreed and complete: responsibility boundaries, declaration
layout, catalogue separation, Host-local identities, caller-visible pending
messages, diagnostics and preservation coverage are recorded below. Code snippets
describe the intended interface; they are not implemented changes. Later optional
reductions remain subject to review. This document does not authorise code changes
or commits.

Executive and Rendering currently exercise service flows expected in the final
framework. Preserve those public contracts and their acceptance coverage even
where their current callers are scaffolding. Retiring a caller does not by itself
make its service contract private or redundant.

The existing [erased-owner contract](../system/erased_owner.md),
[asynchronous asset services](../assets/asynchronous_asset_services.md) and
[batch runner contract](../modules/batch_runners.md) remain the implemented
behaviour. Preserve their guarantees while simplifying construction, completion
handling and transfers between requests, operations and retained assets.

## Responsibility boundaries

I/O is explicitly owned by the Host. Host conditioning is limited to data and
transformations the Host can reasonably understand. TGA is the image format the
Host fully understands. Its handling supports debugging and diagnosis, but its
contracts belong under `core/image`, separate from Rendering textures; shared
byte storage does not make TGA and Basis processing one service responsibility.

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

The compile-time `TMessagePayload<TMessage>` trait defaults its payload type to
`TMessage`. The current differing pairs are `FileLoadResult` carrying `LoadedFile`,
`TgaEncodeResult` carrying `EncodedTga`, and `TgaDecodeResult` carrying `DecodedTga`;
only these pairs need specialisations. Existing identity registration and owner
eligibility remain required for both SYSTEM and LOCAL types; there is no new
runtime registry. Whether an empty message is permitted stays with the receiving
service and operation context.

Keep construction separate from posting. Failed posts must preserve the caller's
message and ownership, matching the transport. The current
`CExecutiveThread::post` moves an owner into a temporary message that is destroyed
on failure; migration must expose or retain the pending message instead of
hiding that loss in a Boolean-returning helper.

### Construction helpers

The following names and snippets specify the agreed interface for implementation.

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

### Caller-visible pending requests

Use the existing `threading::CErasedOwnerMsg` as the pending object. Do not add a
pending-message class, automatic retry queue or generic request state machine.
Operation-specific preparation functions remain beside their callers; the Core
construction helper knows only message identity, correlation and payload pairing.

Replace consuming request helpers with preparation functions accepting a mutable
caller-owned message. The Executive scenario dispatcher also becomes
preparation-only and passes that message through to its leaf helpers. Posting
occurs at the operation controller, where retry or abandonment can be decided.

```cpp
[[nodiscard]] bool prepare_load_request(
    threading::CErasedOwnerMsg& pending,
    const std::int32_t slot,
    const EAssetFileFormat format,
    const char* const file,
    const CDocumentParseOptions policy = {},
    const bool decode_top_down = true) noexcept
{
    if (pending.has_message_type() || pending.has_owner())
    {
        MV_ASSERT_REPORT("Load preparation would replace a pending message");
        return false;
    }

    auto candidate = make_owner_message<AssetLoadRequest>(slot);
    auto* const request = candidate.owner().payload<AssetLoadRequest>();
    if ((request == nullptr) || !request->file.set(file))
    {
        return false;
    }

    request->format = format;
    request->alignment = 32u;
    request->policy = policy;
    request->decode_top_down = decode_top_down;
    pending = std::move(candidate);
    return true;
}
```

Check both identity and owner before preparation: a typed empty completion is
still a message. Construct a local candidate and move it into the destination
only after every fallible preparation step has succeeded.

| Outcome | Required behaviour |
| --- | --- |
| Destination already has a message identity or owner | Report programmer misuse and return false without changing it. |
| Payload allocation or field preparation fails | Return false; leave the destination unchanged and release candidate resources. |
| Preparation succeeds | Caller-held message owns the complete request with its identity and slot. |
| Posting fails | Preserve that same message, payload address, slot, attribution and hazards. |
| Posting succeeds | Transport owns the request; the source message is canonically empty and payload pointers must no longer be used. |
| Caller abandons an unposted request | Scope destruction or explicit empty assignment releases it normally, once. |

The controller distinguishes preparation failure from posting failure:

```cpp
threading::CErasedOwnerMsg pending;
if (!prepare_load_request(pending, slot, EAssetFileFormat::json, file))
{
    // Report preparation failure with operation and resource context.
    return false;
}
if (!m_context.post(std::move(pending)))
{
    // Report submission failure. This controller chooses abandonment:
    // pending is released on return, after the caller's policy decision.
    return false;
}
return true;
```

The current Executive may retain its terminal-on-submission-failure policy.
Preserving a failed post does not require retry. A helper promising caller control
must not hide destruction in a temporary; the controller may deliberately
abandon the message.

Where retry is required, retain the carrier in operation-owned state and prepare
it once. A later controller-selected attempt posts the same message and slot;
advance to waiting for a reply only after success. Do not infer that every false
post is transient queue pressure or introduce busy retry loops. One member serves
one unsubmitted request; concurrent operations requiring retry need independent
storage. Explicit abandonment can use
`pending = threading::CErasedOwnerMsg{};`.

For transfers of existing storage, complete all fallible work before moving the
source resource into the candidate. Preparation failure preserves both source
and destination. Preparation success moves the source into the pending message;
a failed post preserves it there, not in the original source variable. Preserve
hazards when extracting from an existing erased owner, as described below.

```cpp
// After destination checking, allocation and any filename preparation:
request->settings.format = EAssetFileFormat::raw;
request->save = save;
request->retention = retention;
request->storage.value = std::move(bytes);
pending = std::move(candidate);
return true;
```

Requests require successful preparation before posting. Completion construction
retains its separate rule: allocation failure may yield a typed, correlated,
ownerless message where the receiving protocol explicitly permits it.

### Inspection helper

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

See [texture interface storage](../../rendering/interface/texture_data.hpp),
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

The agreed target layout is:

| File | Responsibility |
| --- | --- |
| `core/threading/messages/message_helpers.hpp` | POD/owner construction, payload association trait, non-consuming inspection and uncorrelated-slot constant. |
| `core/assets/asset_messages.hpp` | Public asset transfers, load/save/disposal, bundle requests, results and notifications. |
| `core/system/service_messages.hpp` | Public module and filesystem service contracts, unknown-message fallback and common storage registrations. |
| `rendering/interface/texture_data.hpp` | Formats, options and plain texture storage metadata moved from `core/image/texture_data.hpp`. |
| `rendering/interface/texture_messages.hpp` | Shared texture requests, work and results; no Basis implementation dependencies. |
| `core/image/image_messages.hpp` | Host-understood TGA image exchanges and storage for debug/diagnostic support, separate from Rendering texture contracts. |
| `host/runtime/host_payloads.hpp` | Private file/document/bundle/module/filesystem work and retained wrappers, including bundle preparation state. |
| `core/system/transported_types.hpp` | Internal definition aggregate for registration/operation generation; callers include their actual contract headers. |

Keep existing C++ names and meaningful namespaces during separation. Asset
messages may include texture data, not the texture message catalogue. Texture
messages obtain `CAssetId` from the repository definitions without including the
whole asset protocol. Public headers must not pull in Host work records or Basis
codec/job definitions.

Split owning-payload lists into adjacent `asset_owner_payloads.def`,
`service_owner_payloads.def`, `texture_owner_payloads.def` and
`image_owner_payloads.def` fragments. Keep `system_erased_owner_payloads.def`
as the single aggregate used for SYSTEM eligibility and operation generation.
During the initial declaration-only split it also includes a Host fragment for
the registrations still classified as SYSTEM. The subsequent LOCAL conversion
removes the Host and debug-image private payloads from that SYSTEM aggregate.
Reuse fragments for declarations and operations rather than maintaining duplicate
hand lists. These are registration catalogues, not another dispatch mechanism.

Use `host/runtime/host_protocol_type_ids.def` for the reusable private identity
list and `host/runtime/host_owner_payloads.def` for private non-image owner
registrations. Host and test component wrappers compose those fragments and the
debug-image fragment into their own LOCAL lists. Keep component paths and
configuration out of Core's generic `local_type_ids.*.inl` machinery.

Local bindings are generated by the component catalogue, not installed as SYSTEM
bindings in private declaration headers. The internal SYSTEM aggregate may need
private definitions during the intermediate declaration-only stage; remove those
dependencies after localisation. Include helper trait specialisations alongside
the corresponding message declarations so each consumer sees the same pairing.

Update Visual Studio item/filter membership and the policy validator's exact
registration-location allowlist for the selected declaration headers. Include
private headers only while the declaration-only stage still gives their types
SYSTEM registrations; remove those permissions during localisation. Do not
loosen the policy to accept registrations anywhere. New `.def` files use ordinary
LF text policy; preserve the documented Visual Studio metadata exceptions.

### Agreed SYSTEM and LOCAL boundary

Host responsibility alone does not make a protocol LOCAL. Requests from Executive
or Rendering to the Host remain SYSTEM; work exchanged only inside the Host
executable and retained implementation wrappers become LOCAL.

| Public contract retained as SYSTEM | Host-confined identities converted to LOCAL |
| --- | --- |
| `AssetLoadRequest`, `AssetResult` | `FileLoadRequest`, `FileLoadResult`, `LoadedFile` |
| `AssetSaveRequest` and transfer save options | `FileSaveRequest`, `FileSaveResult` |
| Asset load/save and `ImageAssetTransfer` for debug images | `TgaEncodeRequest`, `TgaDecodeRequest`, `TgaEncodeResult`, `TgaDecodeResult`, `EncodedTga`, `DecodedTga` |
| Asset load/save, `BakedAssetTransfer`, `LiveAssetTransfer` | `DocumentConditionRequest`, `DocumentConditionResult`, `BakedDocumentAsset`, `LiveDocumentAsset` |
| `ModuleRequest`, `ModuleResult` | `ModuleWorkRequest`, `ModuleWorkResult` |
| `FilesystemRefreshRequest`, `FilesystemRefreshResult` | `FilesystemScanRequest`, `FilesystemScanResult` |
| Bundle bake/load/reference and asset disposal contracts | `BundleBuildWork`, `BundleBuildResult`, `BundlePrepareWork`, `BundlePrepareResult`, `BorrowedBundleAsset` |

This converts 24 identities, including nine eligible owner payloads:
`LoadedFile`, `EncodedTga`, `DecodedTga`, `BakedDocumentAsset`,
`LiveDocumentAsset`, `DocumentConditionResult`, `FilesystemScanResult`,
`BundleBuildResult` and `BorrowedBundleAsset`. Preserve their nested-storage
accounting registrations.

All raw/image/baked/live client transfer identities, asset results and disposal
notifications remain SYSTEM. All six texture request/work/result identities,
module entry-point identities, common storage identities and the unknown SYSTEM
message fallback remain SYSTEM. Host-to-Rendering work crosses a component
boundary. No additional module-local SYSTEM candidates were identified in the
current runtime paths.

Keep the existing 15-character LOCAL registry-name limit. Use concise tokens such
as `file_load_req` and `doc_cond_result` without shortening the C++ type names or
widening registry storage. Verify every name's length and uniqueness during
catalogue generation.

Each binary generates and installs one LOCAL registry and owner-operation table.
The Host catalogue under `host/module/types/` and the test catalogue under
`tests/environment/` compose the reusable protocol fragments separately.
MorphicTests compiles Host service/worker code directly, so those translation
units and the ordinary tests must all use the test binary's composed catalogue.
Do not include two component catalogues in one translation unit or link a second
Host registry into the test binary. LOCAL ordinal values need not match between
the Host and tests; identity is meaningful within the owning binary.

Shared Host sources include `<local_type_ids.hpp>` through a target-private
include directory in Visual Studio and CMake. Declaration headers remain
independent of the selected catalogue. Lifecycle fixture DLLs use their own
catalogue under `tests/module_lifecycle/`; they do not inherit the ordinary
tests' Host-private protocols or owner operations.

The former Executive `file_load_request` registry probe was not a request using
that protocol. Stage 3 replaces it with public `asset_load_request`, preserving
registry-authority verification. Tests that previously transported
`BakedDocumentAsset` or `LoadedFile` across simulated module boundaries now use
public transfer payloads, retaining ownership/failure coverage. Private wrappers
are tested within the owning component and for rejection at module boundaries.

Removing SYSTEM entries renumbers generated IDs. Check numeric-ID persistence
and compatibility assumptions before conversion and rebuild Host, modules, test
binaries and lifecycle fixtures together. An external compatibility dependency
would require review rather than silently invalidating stored IDs or adding a
new compatibility scheme. No change to identity encoding or transport admission
rules is intended.

The pre-conversion audit found no persisted transport type IDs in bundle,
baked-document or schema data-block formats. Raw ID uses are encoding tests,
dispatch, registry lookup and diagnostics, rather than saved asset identities.
The DLL function queries and messages do depend on compiled SYSTEM ordinals.
Registry validation checks the supplied table's internal consistency, not its
agreement with an older binary's compiled catalogue; the existing binding ABI
version does not detect this catalogue change. All participating binaries must
therefore be rebuilt and deployed together. Mixed pre/post-conversion binaries
are unsupported. No identity encoding, persistence format or ABI-version scheme
is changed by this stage.

See [transported types](../../core/system/transported_types.hpp),
[system identities](../../core/system/system_type_ids.def) and
[owner operation generation](../../core/system/erased_owner_operations.cpp).

## Diagnostics and failed-flow visibility

Use the existing logging and assertion facilities. Diagnostics must identify the
operation and failure boundary without introducing another event framework or
changing ownership, status or cleanup semantics.

| Situation | Required diagnostic treatment |
| --- | --- |
| Missing registration, duplicate binding, invalid LOCAL name, missing catalogue/header/project item | Prefer compile-time generation checks, policy checks and full affected-component builds. Registry installation must retain its runtime validation. |
| Preparation would overwrite an occupied destination | `MV_ASSERT_REPORT` or the equivalent existing non-consuming assertion path, followed by failure with the original message intact. |
| Wrong message/payload pairing, unexpected sender, slot, phase, duplicate completion or an impermissible empty payload | Report a protocol invariant violation at the validating boundary; include available actual/expected identity and operation context. Preserve established rejection and cleanup behaviour. |
| LOCAL identity crosses a component boundary or is unregistered | Retain existing transport admission diagnostics and rejection ordering; do not emit a redundant copy in construction helpers. |
| Allocation failure, queue capacity failure or permitted empty completion | Ordinary failure handling, not an invariant assertion. The operation controller reports terminal failure or its explicit retention/abandonment decision as appropriate. |
| Missing input file/root/entry, invalid document element or unsupported content | Preserve domain status and existing parser/codec diagnostics; report the resource and operation at the service/controller boundary that has that context. Expected negative tests remain ordinary failures. |
| Request preparation, submission, completion delivery or a whole acceptance flow fails | Log the failed stage and correlation/scenario context, including the resource or asset identity where available. Do not allow a terminal flow to disappear behind an unexplained Boolean. |
| Deferred disposal, accepted worker borrow or delayed notification | Preserve useful existing detail-level lifecycle traces so a stalled flow can be related to its outstanding operation and lifetime obligations. |

Use nonfatal diagnostic assertions where failure must still return with ownership
preserved. Do not turn ordinary invalid input or unavailable files into assertions.
Include operation/message identity, slot, status and relevant source/destination,
phase, asset ID or path where known. Do not claim an exact cause when a Boolean API
cannot distinguish allocation failure from invalid input; report the failing step
and available context. Add finer status reporting only through separate review.

LOCAL identities and component-owned strings must not be exported through
SYSTEM-only diagnostic fields. Use the existing boundary-failure reporting form
or safely copied bounded text supported by the diagnostic transport. Diagnostic
data must not borrow a filename or payload beyond its lifetime.

Report an invariant at the boundary that detects it; a controller may add distinct
terminal-flow context. Avoid duplicate low-level logs, success-path noise and
logging on every retry attempt. Diagnostic failure must not move an owner, suppress
a required reply, fabricate successful completion or skip worker draining.

Exercise diagnostics through supported observation points. Check the identifying
context and severity/category, not incidental wording. Expected rejection tests
must distinguish intentional reports from unexpected assertions in otherwise
successful acceptance runs.

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

## Staged implementation plan

Each stage is a reviewable unit; implementation and commits require separate user
instruction. Stage 1 is complete as a design/documentation stage. Stages 2 and 3's
declaration split and LOCAL conversion were implemented on 20 October 2026.
Stages 4 onward remain future work; the message helpers have not been implemented.

### Stage 1 Placement, interfaces and coverage (complete)

The file map, split catalogues, 24 LOCAL conversions, nine LOCAL owner payloads
and three differing message/payload pairs above are the implementation baseline.
Caller-visible pending requests, explicit posting, permitted empty completions
and diagnostic responsibilities are agreed. No new message framework, ownership
mechanism or automatic retry policy is required.

Use the following coverage map. Existing suites are starting points, not a claim
that each new assertion already exists. Extend functional coverage only where a
changed contract or identified gap requires it; do not perform the broader testing
framework consolidation here.

| Area | Existing coverage / implementation anchor | Required preservation or focused addition |
| --- | --- | --- |
| Identity and catalogue composition | `SystemTypeIdentity`, `AsyncState`, `ErasedPod`, component registry bootstrap | Correct category and eligibility; unique bounded LOCAL names; one registry per binary; no public-header dependence on Host protocols; declaration/operation lists agree. |
| LOCAL admission and ownership | `ErasedOwner`, `ErasedPod`, `HostWorkers` | Same-component worker posts succeed; cross-component LOCAL tags and LOCAL owners are rejected independently, preserving message, attribution and hazards. |
| Construction and inspection | Erased-message suites; add a focused helper suite if needed | Same-type pairing plus all three differing pairs; allocation failure preserves tag/slot; present/empty/invalid inspection does not consume; POD correlation and uncorrelated notification construction. |
| Caller preparation | Executive request preparation and focused helper coverage | Occupied destination unchanged, including typed-empty and owner-only cases; allocation/string preparation failure unchanged; source buffer untouched until all fallible preparation succeeds. |
| Caller posting and abandonment | `ErasedOwner`, `TOwningTransport`, migrated Executive controller boundary | Actual wrapper failure preserves payload address, contents, type, slot, attribution and hazards; retry sends the same request once; success empties the source; abandonment releases exactly once. A transport-only test is insufficient. |
| Cross-module public transfers | `BakedDocumentTransfer`, `AssetRepository`, Executive acceptance | Replace private-wrapper cross-module examples with public payloads while retaining transfer, failure and accounting coverage; keep private retained-wrapper coverage local. |
| File and debug image flows | `HostWorkers`, `File`, `ImageView`, Executive asset scenarios, `tests/host_workers/run_tests.ps1` | Load/save and TGA paths remain available through public asset contracts; missing/invalid input is diagnosed; borrowed storage stays alive until accepted work completes. |
| Document storage and schema | `BakedDocumentTransfer`, `BundleRequests`, schema data-block cases | Preserve full `buffer_bytes()` including appended payloads, alignment and exact entry ranges; schema attachment remains borrowed with existing lifetime obligations. |
| Bundle preparation and disposal | `BundleBuilder`, `BundleService`, `BundleRequests`, lifecycle bundle cases | Exclusive mutable preparation, retained owners and backing leases; closure during accepted work; no correlated disposal success; final notification only after bytes/binding release and only to the valid requester instance. |
| Texture processing | `TextureService`, `TextureBatch`, Executive acceptance, lifecycle fixtures | Encode/decode success/failure and out-of-order replies; source disposal while work is accepted; bundle-backed source protection; correct storage after Rendering unload; drain jobs before codec exit. |
| Module and filesystem service contracts | `tests/module_lifecycle/run_tests.ps1`, Executive filesystem/module scenarios | Public requests and acknowledgement/completion ordering survive private worker-ID conversion; real Rendering bundle load/save/handoff remains covered. |
| Diagnostics | `DebugService`, existing transport rejection tests, failure-path acceptance logs | Expected failures have useful context without invariant assertions; malformed protocol reports are observable; no extra unexpected assertions or silent terminal flows; LOCAL diagnostics do not export invalid SYSTEM identities. |
| Build and policy integration | Solution/project files, policy validator, line-ending check, existing CI | Headers and `.def` fragments present in every consuming target; narrow registration allowlist; correct metadata format; no stale mixed-ID binaries. |

For code stages, run the relevant focused suites and build all affected components.
The LOCAL conversion requires rebuilding all participating binaries and running
the real Host-worker and module-lifecycle harnesses, including one-worker and
two-requested-worker configurations. Complete the existing Debug/Release and
x64/Win32 validation requirements for changes affecting layouts or shared
registrations, and retain Linux build/policy coverage through the existing CI
path. Record what was run and any unavailable checks; do not imply a local
Windows run proves Linux compatibility.

Review exit: design choices and coverage obligations are settled. Numeric-ID
compatibility auditing and exact diagnostic gaps are explicit implementation
checks, not permission to broaden scope. If they reveal a conflict with the
agreed contracts, bring that concrete conflict back for discussion.

### Stage 2 Organise declarations (implemented)

Separate public service, Rendering-interface, Host-internal and debug image
declarations. Keep identities, carrier layouts and runtime behaviour unchanged.
Update includes, operation generation and Visual Studio item/filter entries where
needed, preserving their repository-specific formatting rules.

Validation: build affected binary components and test targets to verify complete
registration visibility and dependency boundaries; run applicable policy checks.

The split preserves all 53 existing C++ type bindings, all 23 owner registrations
and their nested-storage members, declaration bodies, texture storage definitions
and SYSTEM catalogue ordinals. Ordinary callers now include their contract
headers; only SYSTEM owner-operation generation includes the full definition
aggregate. Private registrations remain SYSTEM pending stage 3.

Stage 2 validation passed: Windows Debug/Release builds and ordinary tests on
x64/Win32, Debug x64 Host-worker acceptance and module lifecycle with one and two
requested workers, standalone contract-header compilation, policy checks and
project/text-format checks. Linux build/runtime verification remains for CI.

### Stage 3 Localise Host protocols (implemented)

Converted the agreed 24 private identities and nine owner registrations. Host
and MorphicTests compose `host/runtime/host_protocol_type_ids.def` and the Host
and image owner fragments into their separate LOCAL catalogues. The lifecycle
DLL fixtures have an independent catalogue. Public client contracts and all six
Rendering exchanges remain SYSTEM. LOCAL names fit the existing 15-byte limit.
The Executive registry probe now uses public `asset_load_request`; cross-module
ownership tests use `RawAssetTransfer` and `BakedAssetTransfer`.

The numeric compatibility audit above preceded conversion. Rebuild every
participating binary and fixture together; old numeric message IDs are not
compatible. SYSTEM operation generation no longer includes private declarations.

Validation passed: Windows Debug/Release builds and ordinary tests on x64/Win32;
Debug x64 Host-worker acceptance; all 33 lifecycle cases with one and two
requested workers. Focused checks cover all 24 identity categories and all nine
private owners, same-component transfer, independent message/owner rejection,
nested-storage accounting and retained message/owner state after rejection.
Existing public-transfer, diagnostic, bundle and schema coverage also passes.
Policy, project/XML, text-format and public-header dependency checks passed.
CMake includes select the corresponding component catalogue; Linux build/runtime
verification remains for CI.

### Stage 4 Introduce helpers and migrate construction

Implement the payload association trait and the three agreed helpers, plus a
named uncorrelated-slot constant. Migrate representative POD replies, owning
requests and owning replies, then remaining applicable sites. Replace the
Executive's hidden consuming-post pattern with caller-visible pending messages.
Keep posting explicit and leave terminal delivery policy at the service boundary.
Use preparation-only scenario dispatch, atomic preparation into a caller-owned
destination and explicit controller decisions on failed posts. Add contextual
failure reporting and misuse assertions according to the diagnostics section,
without introducing retries or changing service failure status conventions.

Validation: cover same/different message-payload types, allocation failure with
preserved tag/correlation, present/empty/invalid inspection, and rejected posts
preserving owner, payload address, attribution and hazards. Exercise the migrated
caller boundary as well as the transport; a passing transport test alone does
not prove that a wrapper preserves ownership.

Implementation (20 October 2026): construction and inspection are in
`core/threading/messages/message_helpers.hpp`, with the three differing payload
associations beside their Host/image declarations. Executive preparation is in
the separate private `request_preparation.hpp/.cpp` unit. The scenario dispatcher
and fixture builders now prepare into a caller-owned message; controllers post
explicitly and retain their existing terminal failure policy.

POD construction and applicable owning request/reply construction have migrated
in the Executive, Host services/workers and Rendering. Rendering still acquires
its request record before allocating the result payload, preserving the typed
empty reply when record allocation fails. Existing-owner forwarding and local
asset-owner creation remain explicit. No identities or transport rules change.
Completion validation, texture retention and in-place completion updates remain
stage 5 work.

The `MessageHelpers` suite compiles the actual Executive preparation unit.
Coverage includes same/different payload pairs, typed empty allocation failure,
const inspection and temporary-carrier rejection, occupied destinations, partial
path preparation failure, storage preservation on failed preparation, rejected
post and retry of both path-owning and transferred-storage requests, attribution,
hazards, correlation and exactly-once cleanup. Existing functional suites and
Host/lifecycle acceptance cover the migrated flows.

Validation passed: Debug and Release solution builds and ordinary tests on x64
and Win32, with 205 `MessageHelpers` checks in each configuration; Debug x64
Host-worker acceptance; and all 33 lifecycle cases with each of one and two
workers. Policy validation reported no errors or warnings. Project XML and text
format checks passed, including the new files and Visual Studio metadata
exceptions. Linux build/runtime verification remains for CI.

### Stage 5 Consolidate completion handling and texture code

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

### Stage 6 Select remaining reductions

Review actual duplication after migration before selecting byte-storage
aggregation, extraction functions or shared transport transfer mechanics. Give
each selected change its own rationale and reviewable diff. Preserve admission
ordering, empty-owner differences, attribution rollback, hazards and leases.
Broader asset-result redesign and new schema messages remain outside this pass.

For selected changes, run targeted ownership/retention tests and the repository's
required checks. Do not repeat passing validation matrices without a new change
or unresolved concern. Update implemented contract documentation at closeout.

Reassess optional reductions as scaffolding and exercise flows are retired while
preserving the underlying public contracts. The subsequent
[testing consolidation](testing_framework_consolidation.md) remains separate.
