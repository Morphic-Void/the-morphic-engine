# Message and ownership simplification recommendations

Updated 12 October 2026. These recommendations concern message construction,
completion interpretation and ownership transfer in the current engine. They
preserve the bounded engine scope and account for scaffolding and exercise flows
that may be removed before version 1.0. They are discussion material, not an
approved implementation plan or authority to begin implementation.

The strongest opportunity is to consolidate the rules around messages and
ownership, with a smaller reduction in concrete message types. The underlying
transport design already provides useful guarantees. Most of the maintenance
burden sits in constructing messages, interpreting completions and moving
storage between requests, operations and retained assets.

The existing [erased-owner contract](../system/erased_owner.md),
[asynchronous asset services](../assets/asynchronous_asset_services.md) and
[batch runner contract](../modules/batch_runners.md) remain the implemented
behaviour. The choices below require discussion before changing those contracts.

## Typed construction and inspection

Introduce a small typed layer above `CErasedOwnerMsg` for constructing and
inspecting owning messages. Message identity, correlation and owner are currently
assigned separately, and their relationship is checked by individual receivers.
Asset requests require message and payload identities to match, whereas
`FileLoadResult` deliberately carries the different payload type `LoadedFile`.

A small protocol trait could declare the payload associated with each owning
message and whether an empty owner is permitted. Construction and extraction
helpers could enforce those associations consistently, while leaving the general
carrier flexible. This would remove repetitive setup and scattered conventions
without introducing a message framework.

Keep construction separate from posting. The owning transport preserves a
rejected message, but `CExecutiveThread::post` moves the caller's owner into a
temporary message which is destroyed when posting fails. Either behaviour can
be intentional. A reusable helper should make that ownership consequence
explicit, rather than obscuring it behind the same Boolean success convention.

Relevant implementation:

- [Owning message carrier](../../core/threading/messages/CErasedOwnerMsg.hpp).
- [Asset request validation](../../host/runtime/asset_service.cpp),
  `CAssetService::request`.
- [Worker replies](../../host/runtime/host_worker_thread.cpp),
  `CHostWorkerThread::operate`.
- [Executive posting helper](../../executive/runtime/executive_thread.cpp),
  `CExecutiveThread::post`.

## Aggregate payloads by storage representation

Share storage representations where the message already identifies the
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
one-line wrapper alone is a weak reason to change it.

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
- Preserving the appropriate module dependencies.
- Preparing metadata whose address will be published.
- Leaving the request and destination in defined states on failure.

Moving a whole erased owner preserves its hazard mask. Moving a member into a
newly created owner does not automatically preserve that information. This
distinction deserves more attention than shortening individual `take_owner()`
calls.

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

- A permitted empty owner means the completion payload could not be allocated.
- A present owner has the payload type declared by the protocol.
- Where callers need failure detail, the payload carries a domain-specific status.
- A wrong payload type is a protocol violation, distinct from ordinary operation
  failure.

This does not require a universal result structure or one large status enum.
File, texture, document and module statuses can remain distinct. The improvement
is consistent interpretation, particularly distinguishing malformed messages from
legitimate empty completions.

See [asset completion handlers](../../host/runtime/asset_service.cpp), from
`CAssetService::complete_file_load` through
`CAssetService::complete_document_conditioning`.

## Shared transport transfer mechanics

Consolidate the common ownership-transfer sequence in
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

Host-only work such as `ModuleWorkRequest` is a candidate for Host-local
declaration and identity, rather than exposing a pointer to `host::SModuleWork`
through the system-wide contract. Cross-component texture work would remain
shared. Separate headers first and reconsider identity categories independently;
combining both changes would make review harder.

See [transported types](../../core/system/transported_types.hpp),
[system identities](../../core/system/system_type_ids.def) and
[owner operation generation](../../core/system/erased_owner_operations.cpp).

## Lifetimes and boundaries to preserve

Do not aggressively compress the asset operation's ownership fields. They
represent overlapping lifetimes: document conditioning can produce a retained
baked asset and temporary JSON bytes that must survive a subsequent save. A
single current-owner field would obscure that relationship.

Prefer explicit transition functions and a short phase/lifetime table identifying
who owns each input and when borrowers release it. Keep the address stability
required by borrowed worker inputs and published views.

Preserve the POD/owning-message distinction and the separate batch-work protocol.
They express different ownership contracts. Aggregation should reduce repeated
rules without hiding ownership or forcing unrelated operations into one structure.

See [asset operation storage](../../host/runtime/asset_service.hpp),
[document completion](../../host/runtime/asset_service.cpp) and
[batch work](../../core/threading/CBatchWork.hpp).

## Suggested order for discussion

1. Typed message helpers, explicit failed-post semantics and consistent completion
   validation.
2. Shared byte-storage payloads and explicit resource extraction.
3. Shared transport mechanics and contract header organisation, with identity
   category changes considered separately.

Reassess candidates as scaffolding and exercise flows are retired. The target is
fewer places that must change together when adding a service, while preserving
singular ownership, attribution, module dependencies and stable borrowed storage.
