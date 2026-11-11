# Core to Foundation reconfiguration plan

Consolidated 11 November 2026.

This plan records the agreed separation of the current Core into Foundation,
Framework and Host. It covers all 272 files currently under
`D:/TheMorphicEngine/core/`, including required splits, consumer changes and build integration.
Implementation requires a subsequent instruction; agreement with this plan does
not authorise moving source files, changing behaviour or committing.

Foundation is the agreed conceptual role. The proposed final directory name
`foundation` and project name `MorphicFoundation` express the rename under
consideration; the rename should follow the separation, subject to confirmation
when implementation is selected. New filenames below are proposed implementation
names, not additional architectural decisions. Existing filenames are retained
where their responsibilities remain coherent.

## Agreed responsibilities

| Layer | Responsibility |
| --- | --- |
| Foundation | Reusable structures and mechanisms: containers, memory, abstract identity and attribution, registration, erased ownership, transport and synchronisation, diagnostics, native platform facilities, documents, schemas, asset identity, bundle format, image views and codecs. |
| Framework | Morphic contracts and common participation implementation: configured identity/type catalogues, messages, module binding support, service clients, worker contexts, batch clients, restriction readers and shared operation-table generation. |
| Host | Composition and operational ownership: canonical identity tables, configuration, native module/thread instances, resource provisioning, routing, repositories, mounts, worker scheduling, retirement and unloading. |
| Individual modules | Domain implementation and component-local catalogues. No current Core file has been selected for direct relocation into Executive, Rendering, Audio or Platform. Existing module implementations remain in their components. |

Framework depends on Foundation. Host and modules use both as appropriate.
Foundation must not include Framework, Host or module implementation headers.
Module access to Host-provided resources goes through the shared Framework
representation or contract; a concrete shared resource structure is acceptable
and does not require a virtual service interface. Native platform mechanisms
in Foundation are distinct from the application Platform module.

Foundation may understand abstract module, thread, mount and combined system
identities, including their encoding, attribution, ambient context and memory
ownership. Named configured identities and the Morphic topology belong above
it. The same distinction applies to abstract SYSTEM/LOCAL type categories
versus the actual registered payload catalogue.

## Required splits and preserved responsibilities

### S1 Asset identity and repository

Split [asset_repository.hpp](D:/TheMorphicEngine/core/assets/asset_repository.hpp):

- Foundation `asset_id.hpp`: CAssetId, invalid state, representation,
  comparisons and explicit raw-value construction. Remove the dependency on
  friendship with CAssetRepository. Construction does not confer ownership,
  access, registration or live validity.
- Host `asset_repository.hpp`: CAssetRepository, CAssetRecord, backing roles,
  state and leases. Preserve repository-local monotonic issuance and resolution;
  do not reinterpret the runtime ID as a persistent asset name.
- Framework `asset_messages.hpp`: public requests, settings, results, views,
  status and CAssetAcquisitionId. Acquisitions remain the specific client
  acquisition/release protocol, separate from neutral asset identity.
- Host `asset_storage.hpp`: ByteStorage and its component-local eligibility.

Change Framework asset messages and Foundation baked_data_block.hpp to include
the neutral identity header instead of the Host repository.
CBakedDataBlock may retain CAssetId provenance and attach_asset(); this does not
acquire or pin backing storage. Copying a view also does not extend its lifetime.

Keep repository storage separate from CAssetService orchestration. Host
backing counters remain Host-thread state; lease release alone does not destroy
the backing. Preserve destruction of the owned payload before release of its
backing lease, and preserve exact acquisition/client matching.

### S2 Debug reporting and service administration

Split [service.hpp](D:/TheMorphicEngine/core/debug/service.hpp) into a lightweight
Foundation reporting surface and the Foundation service administration/state
surface. Separate implementation bodies in service.cpp as needed to match
that boundary; a new reporting.cpp is proposed, not a requirement to duplicate
state or dispatch.

Foundation retains reporting macros, event encoding, formatting, shared service
state, transport, immediate/panic paths, the writer loop and native start/stop
mechanics. macros.hpp should include the reporting surface without exposing
the complete service class to containers and other low-level users.
event_arguments.hpp/.cpp and generic log_path.hpp/.cpp remain foundational.

Host owns the instance, chooses paths and configuration, supplies the assigned
debug thread identity/name, installs and starts the service, and orders shutdown
after producers are quiescent. Replace the hardcoded thread_ids::debug_service
use inside service.cpp with supplied configuration. Abstract registry lookup
for formatting names is still allowed in Foundation.

Framework module binding installs access in each participating binary.
Preserve reporting before service installation, during teardown, and the
existing immediate/panic and shutdown-wake guarantees. The debug service is
not moved into Framework or reimplemented in Host.

### S3 Paths and native module loading

Split [logical_path.hpp](D:/TheMorphicEngine/core/filesystem/logical_path.hpp):
ASCII folding/comparison and reusable component/relative-path validation stay
in Foundation. Reserved `native:` mount recognition moves to Host, proposed
as `host/filesystem/mount_rules.hpp`. Host filesystem resolution owns its use.

Split [binding.hpp](D:/TheMorphicEngine/core/platform/module/binding.hpp) and
[binding.cpp](D:/TheMorphicEngine/core/platform/module/binding.cpp):
CPlatformModule, native handle ownership, load/unload, symbol lookup,
get_modules_std_ext() and make_module_filename() stay in Foundation.
expand_module_extension(), which interprets the configuration placeholder
`${module_extension}`, moves to Host, proposed as
`host/config/module_extension.hpp/.cpp`. Update both bootstrap configuration
and filesystem-image consumers; native filename construction remains reusable.

Bundle views, validation, synchronous construction and the internal builder
I/O seam stay in Foundation. Host owns configured roots, discovery, logical
resolution, mount/cache associations and asynchronous work around bundles.
Image views and TGA codecs also stay in Foundation; this move does not require
a new codec-neutral image API.

### S4 Thread package and module binding

Split [CThreadPackage.hpp](D:/TheMorphicEngine/core/threading/CThreadPackage.hpp) and
[CThreadPackage.cpp](D:/TheMorphicEngine/core/threading/CThreadPackage.cpp):

| Responsibility | Destination |
| --- | --- |
| CThreadPackage, launch ThreadConfig and FThreadPrepare | Host |
| Native CThread, created flag, priority, entry point, prepare callback/context, create/join/teardown | Host |
| Owned TInstance of CBatchClient, notification pins, execution pins and owner bookkeeping | Host |
| Owner-side channel operations, exit requests and lifecycle queries | Host |
| CThreadResources shared representation | Framework |
| CThreadContext worker-facing operations | Framework |
| Native threading, wait predicate, parking, notification, control-state and transport primitives | Foundation |

Framework resources contain assigned module/thread IDs rather than the complete
launch configuration, immutable performance conversion, parking ticket,
wait predicate, the three Host/worker message channels, control state,
a borrowed batch-client pointer, a const Host-execution view and owner wake.
They do not own the native thread, launch callbacks or Host lifecycle bookkeeping.
Host owns the shared object's lifetime and supplies endpoint identities instead
of relying on module_ids::executable in its constructor.

The native trampoline may receive the Host package; preparation and the worker
entry receive the shared-resource pointer. Preserve the exact provisioning
pointer checked by is_thread_context_ready().

Add a worker-context wait operation that preserves the current lifecycle state.
Use it for exiting/draining paths that currently reach directly into the wait
predicate/ticket. Preserve the existing Waiting/Running behaviour of ordinary
context waits. Expose assigned identity and batch-client access through the
context where consumers need them. A possible startup-to-mark_startup rename
is optional naming work, not an agreed behavioural change.

Keep terminal-state publication, native execution join, late notification and
batch-publisher quiescence, and final resource/pin release as distinct steps.
A terminal state or completed join alone is not proof that all publishers have
stopped touching the shared resources.

CBoundModule moves to Host; it owns binding and native module lifetime.
Module contracts, CModuleBindingContext and bootstrap/install participation
remain Framework. Generic native library loading stays in Foundation.

### S5 Abstract identities and configured catalogues

Split [system_ids.hpp](D:/TheMorphicEngine/core/system/system_ids.hpp):

- Foundation retains strong wrappers, abstract categories, encoded layouts,
  capacity limits, validity checks, encode/decode and combined-ID operations.
- Framework receives generated named IDs, configured counts, catalogue
  expansion and named combined identities such as system_ids::host.
- runtime_ids.def, thread_ids.def and system_type_ids.def move to Framework.

system_id_registry.hpp and the generic implementation currently in
system_ids.cpp remain Foundation. Installation, structural validation,
lookup and name lookup operate on supplied registry data.
Host's system_id_definitions.hpp/.cpp retain canonical immutable table
instances and Host installs those views through module binding.

system_context.hpp/.cpp and memory attribution stay Foundation.
TStaticLookup moves into Foundation's containers directory.

Distinguish structurally valid IDs from IDs present in an installed registry.
Do not weaken registration checks while removing compile-time knowledge of
the configured catalogue.

### S6 Erased ownership and type registration

Foundation retains erased POD/owner carriers, owner eligibility machinery,
type-to-ID registration, SYSTEM/LOCAL category machinery, local-registry
installation/lookup, generic operation factories and operation-registry
validation/installation/lookup.

Split [erased_owner_operations.hpp](D:/TheMorphicEngine/core/system/erased_owner_operations.hpp)
and [erased_owner_operations.cpp](D:/TheMorphicEngine/core/system/erased_owner_operations.cpp):
move the configured SYSTEM table, catalogue expansion and
system_operations_view() declaration/definition to Framework, proposed as
`system_erased_owner_operations.hpp/.cpp`.
Keep generic machinery in Foundation.

Move storage_type_bindings.hpp, transported_types.hpp,
system_erased_owner_payloads.def and the shared domain payload catalogues
to Framework. Keep local_type_ids.hpp.inl and local_type_ids.cpp.inl in
Foundation as generic generators supplied with component catalogues.
The actual Host/module/test local catalogues stay with their components.

Preserve three distinct requirements: type identity registration, explicit
owning-payload eligibility, and a complete runtime operations entry.
Do not infer owning eligibility from type registration alone.

Each executable/DLL compiles its own concrete operation tables and installs its
own view. Framework is shared source here, not a Host-owned executable
dispatch table. Preserve per-binary Foundation state as well. Host may provide
shared immutable identity/name metadata; executable operation pointers must
remain valid in their executing component. CErasedOwner continues to carry
memory/type/dependency state rather than callable pointers.

CErasedOwner retains abstract mount-dependency tracking in Foundation:
mask storage, add/remove/query and move preservation. Replace dependence on
mount_point_ids::k_count with the foundational 32-bit mask capacity and
validation through abstract identities and installed registry data.
Framework checks that its configured mount catalogue fits the supported
capacity; Host decides retirement/unload ordering and when unloading is safe.
Preserve invalid/unregistered dependency rejection.

erased_transport_admission.hpp/.cpp, erased_owner_transport.hpp/.cpp and
erased_owner_transfer.hpp remain Foundation: registered source/destination
admission and ownership/attribution transfer do not require concrete topology.
Morphic message envelopes, payload conventions and message-channel wrappers
move to Framework.

### S7 Common participation and operational ownership

CBatchWork.hpp moves intact to Framework: work records, shared channels and
CBatchClient are common Morphic participation. Host's batch_pool owns
provisioning, runners, scheduling and operational lifetime.

host_restriction.hpp/.cpp, host_eligible_budget.hpp/.cpp and
host_execution_context.hpp belong in Framework. Shared publication, reading
and eligible-time calculation are available to participating modules; Host's
host_restrictions manages actual restriction state and availability.

module_identity.hpp remains Framework: SModuleInstance identifies one installed
instance in the Morphic lifecycle/service protocol. This is distinct from
Foundation's abstract module attribution ID.

Service protocol, borrow, notification and reusable borrow-client files move
to Framework. Host's router, borrow registry and lifecycle management remain
Host-owned. Host-specific names on shared contracts do not imply private
Host implementation.

CASyncState/CASyncStates stay Foundation. They provide erased POD slot storage;
the caller governs operation completion and terminal slot release.

### S8 Umbrellas and dependency cleanup

Split the threading.hpp umbrella: Foundation exposes only foundational
primitives and transports. A Framework umbrella may aggregate participation
types and message contracts, without importing Host's package.
Update consumers to include the narrower layer they use.

Foundation containers and generic type-registration headers must not pull in
Framework's concrete storage bindings or transported-types aggregate.
Include concrete bindings at the Framework/component use site.

All data_model and schema files remain Foundation, including preparation,
validation, resolution, editing, conversion, materialisation, baking,
live/baked data and owned/borrowed storage. The only identified Host dependency
in that area is the repository include used for CAssetId; replace it as in S1.

## File by file inventory

Every current Core file appears once below. A split row lists all destination
surfaces supplied by that source; repeated destination names across a header
and implementation row describe one resulting file, not duplicate copies.
“Move” means placement in the final conceptual layout; Foundation files can
stay under the current Core directory until the eventual rename.
Destination paths are proposed. Routine include updates apply to every move.

### algo

Permutation validation remains reusable.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [validate_permutations.hpp](D:/TheMorphicEngine/core/algo/validate_permutations.hpp) | `D:/TheMorphicEngine/foundation/algo/validate_permutations.hpp` | Move | Preserve existing responsibilities. |

### allocators

Allocation mechanisms and abstract attribution remain foundational.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [CTrackedAllocator.hpp](D:/TheMorphicEngine/core/allocators/CTrackedAllocator.hpp) | `D:/TheMorphicEngine/foundation/allocators/CTrackedAllocator.hpp` | Move | Preserve existing responsibilities. |
| [TBuddyAllocator.hpp](D:/TheMorphicEngine/core/allocators/TBuddyAllocator.hpp) | `D:/TheMorphicEngine/foundation/allocators/TBuddyAllocator.hpp` | Move | Preserve existing responsibilities. |
| [TBuddyAllocatorChain.hpp](D:/TheMorphicEngine/core/allocators/TBuddyAllocatorChain.hpp) | `D:/TheMorphicEngine/foundation/allocators/TBuddyAllocatorChain.hpp` | Move | Preserve existing responsibilities. |
| [THybridAllocator.hpp](D:/TheMorphicEngine/core/allocators/THybridAllocator.hpp) | `D:/TheMorphicEngine/foundation/allocators/THybridAllocator.hpp` | Move | Preserve existing responsibilities. |
| [buddy_index.hpp](D:/TheMorphicEngine/core/allocators/buddy_index.hpp) | `D:/TheMorphicEngine/foundation/allocators/buddy_index.hpp` | Move | Preserve existing responsibilities. |

### assets

See asset identity and repository split.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [asset_messages.hpp](D:/TheMorphicEngine/core/assets/asset_messages.hpp) | `D:/TheMorphicEngine/framework/assets/asset_messages.hpp` | Move | Shared asset/schema participation contract; CAssetAcquisitionId stays with the protocol. S1. |
| [asset_owner_payloads.def](D:/TheMorphicEngine/core/assets/asset_owner_payloads.def) | `D:/TheMorphicEngine/framework/assets/asset_owner_payloads.def` | Move | Shared asset/schema participation contract; CAssetAcquisitionId stays with the protocol. S1. |
| [asset_repository.hpp](D:/TheMorphicEngine/core/assets/asset_repository.hpp) | `D:/TheMorphicEngine/foundation/assets/asset_id.hpp`; `D:/TheMorphicEngine/host/assets/asset_repository.hpp` | Split | Extract neutral CAssetId and operators; retain records, leases and repository in Host. S1. |
| [asset_storage.hpp](D:/TheMorphicEngine/core/assets/asset_storage.hpp) | `D:/TheMorphicEngine/host/assets/asset_storage.hpp` | Move | ByteStorage is a private worker/repository payload; its buffer remains foundational. |
| [bundle.cpp](D:/TheMorphicEngine/core/assets/bundle.cpp) | `D:/TheMorphicEngine/foundation/assets/bundle.cpp` | Move | Reusable bundle format, synchronous construction and internal I/O seam; mounting/scheduling stay in Host. |
| [bundle.hpp](D:/TheMorphicEngine/core/assets/bundle.hpp) | `D:/TheMorphicEngine/foundation/assets/bundle.hpp` | Move | Reusable bundle format, synchronous construction and internal I/O seam; mounting/scheduling stay in Host. |
| [bundle_builder.cpp](D:/TheMorphicEngine/core/assets/bundle_builder.cpp) | `D:/TheMorphicEngine/foundation/assets/bundle_builder.cpp` | Move | Reusable bundle format, synchronous construction and internal I/O seam; mounting/scheduling stay in Host. |
| [bundle_builder.hpp](D:/TheMorphicEngine/core/assets/bundle_builder.hpp) | `D:/TheMorphicEngine/foundation/assets/bundle_builder.hpp` | Move | Reusable bundle format, synchronous construction and internal I/O seam; mounting/scheduling stay in Host. |
| [internal/bundle_builder_io.hpp](D:/TheMorphicEngine/core/assets/internal/bundle_builder_io.hpp) | `D:/TheMorphicEngine/foundation/assets/internal/bundle_builder_io.hpp` | Move | Reusable bundle format, synchronous construction and internal I/O seam; mounting/scheduling stay in Host. |
| [schema_messages.hpp](D:/TheMorphicEngine/core/assets/schema_messages.hpp) | `D:/TheMorphicEngine/framework/assets/schema_messages.hpp` | Move | Shared asset/schema participation contract; CAssetAcquisitionId stays with the protocol. S1. |

### bit_utils

Bit operations and representations remain reusable.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [TBitField.hpp](D:/TheMorphicEngine/core/bit_utils/TBitField.hpp) | `D:/TheMorphicEngine/foundation/bit_utils/TBitField.hpp` | Move | Preserve existing responsibilities. |
| [bit_ops.hpp](D:/TheMorphicEngine/core/bit_utils/bit_ops.hpp) | `D:/TheMorphicEngine/foundation/bit_utils/bit_ops.hpp` | Move | Preserve existing responsibilities. |
| [bit_utils.hpp](D:/TheMorphicEngine/core/bit_utils/bit_utils.hpp) | `D:/TheMorphicEngine/foundation/bit_utils/bit_utils.hpp` | Move | Preserve existing responsibilities. |
| [spatial_codes.hpp](D:/TheMorphicEngine/core/bit_utils/spatial_codes.hpp) | `D:/TheMorphicEngine/foundation/bit_utils/spatial_codes.hpp` | Move | Preserve existing responsibilities. |

### containers

Storage and container mechanisms remain reusable.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [BumpBuffer.hpp](D:/TheMorphicEngine/core/containers/BumpBuffer.hpp) | `D:/TheMorphicEngine/foundation/containers/BumpBuffer.hpp` | Move | Preserve existing responsibilities. |
| [ByteBuffers.hpp](D:/TheMorphicEngine/core/containers/ByteBuffers.hpp) | `D:/TheMorphicEngine/foundation/containers/ByteBuffers.hpp` | Move | Preserve existing responsibilities. |
| [StringBuffers.hpp](D:/TheMorphicEngine/core/containers/StringBuffers.hpp) | `D:/TheMorphicEngine/foundation/containers/StringBuffers.hpp` | Move | Preserve existing responsibilities. |
| [TInstance.hpp](D:/TheMorphicEngine/core/containers/TInstance.hpp) | `D:/TheMorphicEngine/foundation/containers/TInstance.hpp` | Move | Preserve existing responsibilities. |
| [TOrderedCollection.hpp](D:/TheMorphicEngine/core/containers/TOrderedCollection.hpp) | `D:/TheMorphicEngine/foundation/containers/TOrderedCollection.hpp` | Move | Preserve existing responsibilities. |
| [TOrderedSlots.hpp](D:/TheMorphicEngine/core/containers/TOrderedSlots.hpp) | `D:/TheMorphicEngine/foundation/containers/TOrderedSlots.hpp` | Move | Preserve existing responsibilities. |
| [TPodFifo.hpp](D:/TheMorphicEngine/core/containers/TPodFifo.hpp) | `D:/TheMorphicEngine/foundation/containers/TPodFifo.hpp` | Move | Preserve existing responsibilities. |
| [TPodOrderedSlots.hpp](D:/TheMorphicEngine/core/containers/TPodOrderedSlots.hpp) | `D:/TheMorphicEngine/foundation/containers/TPodOrderedSlots.hpp` | Move | Preserve existing responsibilities. |
| [TPodUnorderedSlots.hpp](D:/TheMorphicEngine/core/containers/TPodUnorderedSlots.hpp) | `D:/TheMorphicEngine/foundation/containers/TPodUnorderedSlots.hpp` | Move | Preserve existing responsibilities. |
| [TPodVector.hpp](D:/TheMorphicEngine/core/containers/TPodVector.hpp) | `D:/TheMorphicEngine/foundation/containers/TPodVector.hpp` | Move | Preserve existing responsibilities. |
| [TProcessLifetime.hpp](D:/TheMorphicEngine/core/containers/TProcessLifetime.hpp) | `D:/TheMorphicEngine/foundation/containers/TProcessLifetime.hpp` | Move | Preserve existing responsibilities. |
| [TUnorderedCollection.hpp](D:/TheMorphicEngine/core/containers/TUnorderedCollection.hpp) | `D:/TheMorphicEngine/foundation/containers/TUnorderedCollection.hpp` | Move | Preserve existing responsibilities. |
| [TUnorderedSlots.hpp](D:/TheMorphicEngine/core/containers/TUnorderedSlots.hpp) | `D:/TheMorphicEngine/foundation/containers/TUnorderedSlots.hpp` | Move | Preserve existing responsibilities. |
| [TVector.hpp](D:/TheMorphicEngine/core/containers/TVector.hpp) | `D:/TheMorphicEngine/foundation/containers/TVector.hpp` | Move | Preserve existing responsibilities. |
| [containers.hpp](D:/TheMorphicEngine/core/containers/containers.hpp) | `D:/TheMorphicEngine/foundation/containers/containers.hpp` | Move | Preserve existing responsibilities. |

### data_model

Document parsing, reading, writing, editing, copying, promotion, baking and translation remain foundational.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [baked_document.cpp](D:/TheMorphicEngine/core/data_model/baked_document.cpp) | `D:/TheMorphicEngine/foundation/data_model/baked_document.cpp` | Move | Preserve existing responsibilities. |
| [baked_document.hpp](D:/TheMorphicEngine/core/data_model/baked_document.hpp) | `D:/TheMorphicEngine/foundation/data_model/baked_document.hpp` | Move | Preserve existing responsibilities. |
| [baked_document_format.hpp](D:/TheMorphicEngine/core/data_model/baked_document_format.hpp) | `D:/TheMorphicEngine/foundation/data_model/baked_document_format.hpp` | Move | Preserve existing responsibilities. |
| [data_model_types.hpp](D:/TheMorphicEngine/core/data_model/data_model_types.hpp) | `D:/TheMorphicEngine/foundation/data_model/data_model_types.hpp` | Move | Preserve existing responsibilities. |
| [document_baking.cpp](D:/TheMorphicEngine/core/data_model/document_baking.cpp) | `D:/TheMorphicEngine/foundation/data_model/document_baking.cpp` | Move | Preserve existing responsibilities. |
| [document_copy.cpp](D:/TheMorphicEngine/core/data_model/document_copy.cpp) | `D:/TheMorphicEngine/foundation/data_model/document_copy.cpp` | Move | Preserve existing responsibilities. |
| [document_copy.hpp](D:/TheMorphicEngine/core/data_model/document_copy.hpp) | `D:/TheMorphicEngine/foundation/data_model/document_copy.hpp` | Move | Preserve existing responsibilities. |
| [document_findings.hpp](D:/TheMorphicEngine/core/data_model/document_findings.hpp) | `D:/TheMorphicEngine/foundation/data_model/document_findings.hpp` | Move | Preserve existing responsibilities. |
| [document_parser.cpp](D:/TheMorphicEngine/core/data_model/document_parser.cpp) | `D:/TheMorphicEngine/foundation/data_model/document_parser.cpp` | Move | Preserve existing responsibilities. |
| [document_parser.hpp](D:/TheMorphicEngine/core/data_model/document_parser.hpp) | `D:/TheMorphicEngine/foundation/data_model/document_parser.hpp` | Move | Preserve existing responsibilities. |
| [document_promotion.cpp](D:/TheMorphicEngine/core/data_model/document_promotion.cpp) | `D:/TheMorphicEngine/foundation/data_model/document_promotion.cpp` | Move | Preserve existing responsibilities. |
| [document_structure.cpp](D:/TheMorphicEngine/core/data_model/document_structure.cpp) | `D:/TheMorphicEngine/foundation/data_model/document_structure.cpp` | Move | Preserve existing responsibilities. |
| [document_structure.hpp](D:/TheMorphicEngine/core/data_model/document_structure.hpp) | `D:/TheMorphicEngine/foundation/data_model/document_structure.hpp` | Move | Preserve existing responsibilities. |
| [document_text_lex.cpp](D:/TheMorphicEngine/core/data_model/document_text_lex.cpp) | `D:/TheMorphicEngine/foundation/data_model/document_text_lex.cpp` | Move | Preserve existing responsibilities. |
| [document_text_lex.hpp](D:/TheMorphicEngine/core/data_model/document_text_lex.hpp) | `D:/TheMorphicEngine/foundation/data_model/document_text_lex.hpp` | Move | Preserve existing responsibilities. |
| [document_translation.hpp](D:/TheMorphicEngine/core/data_model/document_translation.hpp) | `D:/TheMorphicEngine/foundation/data_model/document_translation.hpp` | Move | Preserve existing responsibilities. |
| [document_writer.cpp](D:/TheMorphicEngine/core/data_model/document_writer.cpp) | `D:/TheMorphicEngine/foundation/data_model/document_writer.cpp` | Move | Preserve existing responsibilities. |
| [document_writer.hpp](D:/TheMorphicEngine/core/data_model/document_writer.hpp) | `D:/TheMorphicEngine/foundation/data_model/document_writer.hpp` | Move | Preserve existing responsibilities. |
| [live_document.cpp](D:/TheMorphicEngine/core/data_model/live_document.cpp) | `D:/TheMorphicEngine/foundation/data_model/live_document.cpp` | Move | Preserve existing responsibilities. |
| [live_document.hpp](D:/TheMorphicEngine/core/data_model/live_document.hpp) | `D:/TheMorphicEngine/foundation/data_model/live_document.hpp` | Move | Preserve existing responsibilities. |
| [live_document_node.hpp](D:/TheMorphicEngine/core/data_model/live_document_node.hpp) | `D:/TheMorphicEngine/foundation/data_model/live_document_node.hpp` | Move | Preserve existing responsibilities. |

### debug

Reporting and debug service mechanics remain foundational; Host provisions and controls their lifetime.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [event_arguments.cpp](D:/TheMorphicEngine/core/debug/event_arguments.cpp) | `D:/TheMorphicEngine/foundation/debug/event_arguments.cpp` | Move | Preserve existing responsibilities. |
| [event_arguments.hpp](D:/TheMorphicEngine/core/debug/event_arguments.hpp) | `D:/TheMorphicEngine/foundation/debug/event_arguments.hpp` | Move | Preserve existing responsibilities. |
| [log_path.cpp](D:/TheMorphicEngine/core/debug/log_path.cpp) | `D:/TheMorphicEngine/foundation/debug/log_path.cpp` | Move | Generic collision-safe path formatting stays here; Host selects stem, tag, directory and paths. S2. |
| [log_path.hpp](D:/TheMorphicEngine/core/debug/log_path.hpp) | `D:/TheMorphicEngine/foundation/debug/log_path.hpp` | Move | Generic collision-safe path formatting stays here; Host selects stem, tag, directory and paths. S2. |
| [macros.hpp](D:/TheMorphicEngine/core/debug/macros.hpp) | `D:/TheMorphicEngine/foundation/debug/macros.hpp` | Adapt | Depend on lightweight reporting declarations, not the complete service class. S2. |
| [service.cpp](D:/TheMorphicEngine/core/debug/service.cpp) | `D:/TheMorphicEngine/foundation/debug/service.cpp`; `D:/TheMorphicEngine/foundation/debug/reporting.cpp` | Split / adapt | Separate reporting from service administration; inject debug-thread identity. Host owns provisioning/lifetime. S2. |
| [service.hpp](D:/TheMorphicEngine/core/debug/service.hpp) | `D:/TheMorphicEngine/foundation/debug/service.hpp`; `D:/TheMorphicEngine/foundation/debug/reporting.hpp` | Split / adapt | Separate reporting from service administration; inject debug-thread identity. Host owns provisioning/lifetime. S2. |

### filesystem

Separate reusable path rules, public refresh protocol and Host filesystem configuration.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [filesystem_image.cpp](D:/TheMorphicEngine/core/filesystem/filesystem_image.cpp) | `D:/TheMorphicEngine/host/filesystem/filesystem_image.cpp` | Move / adapt | Configured roots, discovery, resolution, mount/cache associations and bundle integration. |
| [filesystem_image.hpp](D:/TheMorphicEngine/core/filesystem/filesystem_image.hpp) | `D:/TheMorphicEngine/host/filesystem/filesystem_image.hpp` | Move / adapt | Configured roots, discovery, resolution, mount/cache associations and bundle integration. |
| [filesystem_messages.hpp](D:/TheMorphicEngine/core/filesystem/filesystem_messages.hpp) | `D:/TheMorphicEngine/framework/filesystem/filesystem_messages.hpp` | Move | Public filesystem refresh protocol and shared owner eligibility. |
| [filesystem_owner_payloads.def](D:/TheMorphicEngine/core/filesystem/filesystem_owner_payloads.def) | `D:/TheMorphicEngine/framework/filesystem/filesystem_owner_payloads.def` | Move | Public filesystem refresh protocol and shared owner eligibility. |
| [logical_path.hpp](D:/TheMorphicEngine/core/filesystem/logical_path.hpp) | `D:/TheMorphicEngine/foundation/filesystem/logical_path.hpp`; `D:/TheMorphicEngine/host/filesystem/mount_rules.hpp` | Split | Keep generic path validation/comparison; extract reserved native: mount recognition. S3. |

### image

Image views and codecs remain foundational; Host conditioning messages remain private.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [codec/tga.cpp](D:/TheMorphicEngine/core/image/codec/tga.cpp) | `D:/TheMorphicEngine/foundation/image/codec/tga.cpp` | Move | Preserve existing responsibilities. |
| [codec/tga.hpp](D:/TheMorphicEngine/core/image/codec/tga.hpp) | `D:/TheMorphicEngine/foundation/image/codec/tga.hpp` | Move | Preserve existing responsibilities. |
| [image_messages.hpp](D:/TheMorphicEngine/core/image/image_messages.hpp) | `D:/TheMorphicEngine/host/image/image_messages.hpp` | Move | Host-local TGA request/completion payloads and local owner catalogue; no move into Rendering. |
| [image_owner_payloads.def](D:/TheMorphicEngine/core/image/image_owner_payloads.def) | `D:/TheMorphicEngine/host/image/image_owner_payloads.def` | Move | Host-local TGA request/completion payloads and local owner catalogue; no move into Rendering. |
| [image_view.cpp](D:/TheMorphicEngine/core/image/image_view.cpp) | `D:/TheMorphicEngine/foundation/image/image_view.cpp` | Move | Preserve existing responsibilities. |
| [image_view.hpp](D:/TheMorphicEngine/core/image/image_view.hpp) | `D:/TheMorphicEngine/foundation/image/image_view.hpp` | Move | Preserve existing responsibilities. |

### memory

Memory tokens, contexts, policies, views and abstract module/thread attribution remain foundational.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [memory_context.cpp](D:/TheMorphicEngine/core/memory/memory_context.cpp) | `D:/TheMorphicEngine/foundation/memory/memory_context.cpp` | Move | Preserve existing responsibilities. |
| [memory_context.hpp](D:/TheMorphicEngine/core/memory/memory_context.hpp) | `D:/TheMorphicEngine/foundation/memory/memory_context.hpp` | Move | Preserve existing responsibilities. |
| [memory_policies.hpp](D:/TheMorphicEngine/core/memory/memory_policies.hpp) | `D:/TheMorphicEngine/foundation/memory/memory_policies.hpp` | Move | Preserve existing responsibilities. |
| [memory_token.hpp](D:/TheMorphicEngine/core/memory/memory_token.hpp) | `D:/TheMorphicEngine/foundation/memory/memory_token.hpp` | Move | Preserve existing responsibilities. |
| [memory_view.hpp](D:/TheMorphicEngine/core/memory/memory_view.hpp) | `D:/TheMorphicEngine/foundation/memory/memory_view.hpp` | Move | Preserve existing responsibilities. |

### module

Common Morphic participation belongs in Framework; native bound-instance ownership belongs in Host.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [bound_module.cpp](D:/TheMorphicEngine/core/module/bound_module.cpp) | `D:/TheMorphicEngine/host/module/bound_module.cpp` | Move | CBoundModule owns native instance binding, installation and unloading. S4. |
| [bound_module.hpp](D:/TheMorphicEngine/core/module/bound_module.hpp) | `D:/TheMorphicEngine/host/module/bound_module.hpp` | Move | CBoundModule owns native instance binding, installation and unloading. S4. |
| [host_eligible_budget.cpp](D:/TheMorphicEngine/core/module/host_eligible_budget.cpp) | `D:/TheMorphicEngine/framework/module/host_eligible_budget.cpp` | Move | Shared restriction publication/reader or eligible-time calculation; Host manages restriction state. S7. |
| [host_eligible_budget.hpp](D:/TheMorphicEngine/core/module/host_eligible_budget.hpp) | `D:/TheMorphicEngine/framework/module/host_eligible_budget.hpp` | Move | Shared restriction publication/reader or eligible-time calculation; Host manages restriction state. S7. |
| [host_execution_context.hpp](D:/TheMorphicEngine/core/module/host_execution_context.hpp) | `D:/TheMorphicEngine/framework/module/host_execution_context.hpp` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |
| [host_pump.hpp](D:/TheMorphicEngine/core/module/host_pump.hpp) | `D:/TheMorphicEngine/framework/module/host_pump.hpp` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |
| [host_restriction.cpp](D:/TheMorphicEngine/core/module/host_restriction.cpp) | `D:/TheMorphicEngine/framework/module/host_restriction.cpp` | Move | Shared restriction publication/reader or eligible-time calculation; Host manages restriction state. S7. |
| [host_restriction.hpp](D:/TheMorphicEngine/core/module/host_restriction.hpp) | `D:/TheMorphicEngine/framework/module/host_restriction.hpp` | Move | Shared restriction publication/reader or eligible-time calculation; Host manages restriction state. S7. |
| [module_binding.hpp](D:/TheMorphicEngine/core/module/module_binding.hpp) | `D:/TheMorphicEngine/framework/module/module_binding.hpp` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |
| [module_binding_context.cpp](D:/TheMorphicEngine/core/module/module_binding_context.cpp) | `D:/TheMorphicEngine/framework/module/module_binding_context.cpp` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |
| [module_binding_context.hpp](D:/TheMorphicEngine/core/module/module_binding_context.hpp) | `D:/TheMorphicEngine/framework/module/module_binding_context.hpp` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |
| [module_identity.hpp](D:/TheMorphicEngine/core/module/module_identity.hpp) | `D:/TheMorphicEngine/framework/module/module_identity.hpp` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |
| [module_messages.hpp](D:/TheMorphicEngine/core/module/module_messages.hpp) | `D:/TheMorphicEngine/framework/module/module_messages.hpp` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |
| [module_owner_payloads.def](D:/TheMorphicEngine/core/module/module_owner_payloads.def) | `D:/TheMorphicEngine/framework/module/module_owner_payloads.def` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |
| [service_provider.hpp](D:/TheMorphicEngine/core/module/service_provider.hpp) | `D:/TheMorphicEngine/framework/module/service_provider.hpp` | Move | Shared Morphic module contract or common binding/participation code. S4/S7. |

### platform

Native OS mechanisms remain foundational; Host configuration substitution is extracted.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [filesystem/directory.cpp](D:/TheMorphicEngine/core/platform/filesystem/directory.cpp) | `D:/TheMorphicEngine/foundation/platform/filesystem/directory.cpp` | Move | Preserve existing responsibilities. |
| [filesystem/directory.hpp](D:/TheMorphicEngine/core/platform/filesystem/directory.hpp) | `D:/TheMorphicEngine/foundation/platform/filesystem/directory.hpp` | Move | Preserve existing responsibilities. |
| [filesystem/file.cpp](D:/TheMorphicEngine/core/platform/filesystem/file.cpp) | `D:/TheMorphicEngine/foundation/platform/filesystem/file.cpp` | Move | Preserve existing responsibilities. |
| [filesystem/file.hpp](D:/TheMorphicEngine/core/platform/filesystem/file.hpp) | `D:/TheMorphicEngine/foundation/platform/filesystem/file.hpp` | Move | Preserve existing responsibilities. |
| [filesystem/internal/file_utils.cpp](D:/TheMorphicEngine/core/platform/filesystem/internal/file_utils.cpp) | `D:/TheMorphicEngine/foundation/platform/filesystem/internal/file_utils.cpp` | Move | Preserve existing responsibilities. |
| [filesystem/internal/file_utils.hpp](D:/TheMorphicEngine/core/platform/filesystem/internal/file_utils.hpp) | `D:/TheMorphicEngine/foundation/platform/filesystem/internal/file_utils.hpp` | Move | Preserve existing responsibilities. |
| [filesystem/log.cpp](D:/TheMorphicEngine/core/platform/filesystem/log.cpp) | `D:/TheMorphicEngine/foundation/platform/filesystem/log.cpp` | Move | Preserve existing responsibilities. |
| [filesystem/log.hpp](D:/TheMorphicEngine/core/platform/filesystem/log.hpp) | `D:/TheMorphicEngine/foundation/platform/filesystem/log.hpp` | Move | Preserve existing responsibilities. |
| [module/binding.cpp](D:/TheMorphicEngine/core/platform/module/binding.cpp) | `D:/TheMorphicEngine/foundation/platform/module/binding.cpp`; `D:/TheMorphicEngine/host/config/module_extension.cpp` | Split | Keep native loader and filename helpers; extract configuration placeholder expansion. S3. |
| [module/binding.hpp](D:/TheMorphicEngine/core/platform/module/binding.hpp) | `D:/TheMorphicEngine/foundation/platform/module/binding.hpp`; `D:/TheMorphicEngine/host/config/module_extension.hpp` | Split | Keep native loader and filename helpers; extract configuration placeholder expansion. S3. |
| [path/native_path.cpp](D:/TheMorphicEngine/core/platform/path/native_path.cpp) | `D:/TheMorphicEngine/foundation/platform/path/native_path.cpp` | Move | Preserve existing responsibilities. |
| [path/native_path.hpp](D:/TheMorphicEngine/core/platform/path/native_path.hpp) | `D:/TheMorphicEngine/foundation/platform/path/native_path.hpp` | Move | Preserve existing responsibilities. |
| [platform_defines.hpp](D:/TheMorphicEngine/core/platform/platform_defines.hpp) | `D:/TheMorphicEngine/foundation/platform/platform_defines.hpp` | Move | Preserve existing responsibilities. |
| [system/debugger.cpp](D:/TheMorphicEngine/core/platform/system/debugger.cpp) | `D:/TheMorphicEngine/foundation/platform/system/debugger.cpp` | Move | Preserve existing responsibilities. |
| [system/debugger.hpp](D:/TheMorphicEngine/core/platform/system/debugger.hpp) | `D:/TheMorphicEngine/foundation/platform/system/debugger.hpp` | Move | Preserve existing responsibilities. |
| [system/performance_counter.cpp](D:/TheMorphicEngine/core/platform/system/performance_counter.cpp) | `D:/TheMorphicEngine/foundation/platform/system/performance_counter.cpp` | Move | Preserve existing responsibilities. |
| [system/performance_counter.hpp](D:/TheMorphicEngine/core/platform/system/performance_counter.hpp) | `D:/TheMorphicEngine/foundation/platform/system/performance_counter.hpp` | Move | Preserve existing responsibilities. |
| [system/process_id.cpp](D:/TheMorphicEngine/core/platform/system/process_id.cpp) | `D:/TheMorphicEngine/foundation/platform/system/process_id.cpp` | Move | Preserve existing responsibilities. |
| [system/process_id.hpp](D:/TheMorphicEngine/core/platform/system/process_id.hpp) | `D:/TheMorphicEngine/foundation/platform/system/process_id.hpp` | Move | Preserve existing responsibilities. |
| [system/process_priority.cpp](D:/TheMorphicEngine/core/platform/system/process_priority.cpp) | `D:/TheMorphicEngine/foundation/platform/system/process_priority.cpp` | Move | Preserve existing responsibilities. |
| [system/process_priority.hpp](D:/TheMorphicEngine/core/platform/system/process_priority.hpp) | `D:/TheMorphicEngine/foundation/platform/system/process_priority.hpp` | Move | Preserve existing responsibilities. |
| [threading/awake_clock.hpp](D:/TheMorphicEngine/core/platform/threading/awake_clock.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/awake_clock.hpp` | Move | Preserve existing responsibilities. |
| [threading/exclusive_lock.cpp](D:/TheMorphicEngine/core/platform/threading/exclusive_lock.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/exclusive_lock.cpp` | Move | Preserve existing responsibilities. |
| [threading/exclusive_lock.hpp](D:/TheMorphicEngine/core/platform/threading/exclusive_lock.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/exclusive_lock.hpp` | Move | Preserve existing responsibilities. |
| [threading/hw_thread_count.cpp](D:/TheMorphicEngine/core/platform/threading/hw_thread_count.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/hw_thread_count.cpp` | Move | Preserve existing responsibilities. |
| [threading/hw_thread_count.hpp](D:/TheMorphicEngine/core/platform/threading/hw_thread_count.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/hw_thread_count.hpp` | Move | Preserve existing responsibilities. |
| [threading/native_thread_id.cpp](D:/TheMorphicEngine/core/platform/threading/native_thread_id.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/native_thread_id.cpp` | Move | Preserve existing responsibilities. |
| [threading/native_thread_id.hpp](D:/TheMorphicEngine/core/platform/threading/native_thread_id.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/native_thread_id.hpp` | Move | Preserve existing responsibilities. |
| [threading/owner_wait.cpp](D:/TheMorphicEngine/core/platform/threading/owner_wait.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/owner_wait.cpp` | Move | Preserve existing responsibilities. |
| [threading/owner_wait.hpp](D:/TheMorphicEngine/core/platform/threading/owner_wait.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/owner_wait.hpp` | Move | Preserve existing responsibilities. |
| [threading/platform_threading.hpp](D:/TheMorphicEngine/core/platform/threading/platform_threading.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/platform_threading.hpp` | Move | Preserve existing responsibilities. |
| [threading/processor_relax.cpp](D:/TheMorphicEngine/core/platform/threading/processor_relax.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/processor_relax.cpp` | Move | Preserve existing responsibilities. |
| [threading/processor_relax.hpp](D:/TheMorphicEngine/core/platform/threading/processor_relax.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/processor_relax.hpp` | Move | Preserve existing responsibilities. |
| [threading/thread_lifetime.cpp](D:/TheMorphicEngine/core/platform/threading/thread_lifetime.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/thread_lifetime.cpp` | Move | Preserve existing responsibilities. |
| [threading/thread_lifetime.hpp](D:/TheMorphicEngine/core/platform/threading/thread_lifetime.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/thread_lifetime.hpp` | Move | Preserve existing responsibilities. |
| [threading/thread_naming.cpp](D:/TheMorphicEngine/core/platform/threading/thread_naming.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/thread_naming.cpp` | Move | Preserve existing responsibilities. |
| [threading/thread_naming.hpp](D:/TheMorphicEngine/core/platform/threading/thread_naming.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/thread_naming.hpp` | Move | Preserve existing responsibilities. |
| [threading/thread_priority.cpp](D:/TheMorphicEngine/core/platform/threading/thread_priority.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/thread_priority.cpp` | Move | Preserve existing responsibilities. |
| [threading/thread_priority.hpp](D:/TheMorphicEngine/core/platform/threading/thread_priority.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/thread_priority.hpp` | Move | Preserve existing responsibilities. |
| [threading/wait_deadline.hpp](D:/TheMorphicEngine/core/platform/threading/wait_deadline.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/wait_deadline.hpp` | Move | Preserve existing responsibilities. |
| [threading/wait_deadline_math.hpp](D:/TheMorphicEngine/core/platform/threading/wait_deadline_math.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/wait_deadline_math.hpp` | Move | Preserve existing responsibilities. |
| [threading/wait_word.cpp](D:/TheMorphicEngine/core/platform/threading/wait_word.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/wait_word.cpp` | Move | Preserve existing responsibilities. |
| [threading/wait_word.hpp](D:/TheMorphicEngine/core/platform/threading/wait_word.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/wait_word.hpp` | Move | Preserve existing responsibilities. |
| [threading/wait_word_internal.hpp](D:/TheMorphicEngine/core/platform/threading/wait_word_internal.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/wait_word_internal.hpp` | Move | Preserve existing responsibilities. |
| [threading/wait_word_semaphore.cpp](D:/TheMorphicEngine/core/platform/threading/wait_word_semaphore.cpp) | `D:/TheMorphicEngine/foundation/platform/threading/wait_word_semaphore.cpp` | Move | Preserve existing responsibilities. |
| [threading/wait_word_semaphore.hpp](D:/TheMorphicEngine/core/platform/threading/wait_word_semaphore.hpp) | `D:/TheMorphicEngine/foundation/platform/threading/wait_word_semaphore.hpp` | Move | Preserve existing responsibilities. |
| [windows_include.hpp](D:/TheMorphicEngine/core/platform/windows_include.hpp) | `D:/TheMorphicEngine/foundation/platform/windows_include.hpp` | Move | Preserve existing responsibilities. |

### schema

All schema and document transformation mechanisms remain foundational, including preparation and borrowed/owned views.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [baked_bulk_data.cpp](D:/TheMorphicEngine/core/schema/baked_bulk_data.cpp) | `D:/TheMorphicEngine/foundation/schema/baked_bulk_data.cpp` | Move | Preserve existing responsibilities. |
| [baked_bulk_data.hpp](D:/TheMorphicEngine/core/schema/baked_bulk_data.hpp) | `D:/TheMorphicEngine/foundation/schema/baked_bulk_data.hpp` | Move | Preserve existing responsibilities. |
| [baked_data_block.cpp](D:/TheMorphicEngine/core/schema/baked_data_block.cpp) | `D:/TheMorphicEngine/foundation/schema/baked_data_block.cpp` | Move | Preserve existing responsibilities. |
| [baked_data_block.hpp](D:/TheMorphicEngine/core/schema/baked_data_block.hpp) | `D:/TheMorphicEngine/foundation/schema/baked_data_block.hpp` | Adapt | Include neutral asset_id.hpp; retain CAssetId provenance and attach_asset(). S1. |
| [baked_instances.cpp](D:/TheMorphicEngine/core/schema/baked_instances.cpp) | `D:/TheMorphicEngine/foundation/schema/baked_instances.cpp` | Move | Preserve existing responsibilities. |
| [baked_instances.hpp](D:/TheMorphicEngine/core/schema/baked_instances.hpp) | `D:/TheMorphicEngine/foundation/schema/baked_instances.hpp` | Move | Preserve existing responsibilities. |
| [data_block.hpp](D:/TheMorphicEngine/core/schema/data_block.hpp) | `D:/TheMorphicEngine/foundation/schema/data_block.hpp` | Move | Preserve existing responsibilities. |
| [data_block_construction.cpp](D:/TheMorphicEngine/core/schema/data_block_construction.cpp) | `D:/TheMorphicEngine/foundation/schema/data_block_construction.cpp` | Move | Preserve existing responsibilities. |
| [data_block_construction.hpp](D:/TheMorphicEngine/core/schema/data_block_construction.hpp) | `D:/TheMorphicEngine/foundation/schema/data_block_construction.hpp` | Move | Preserve existing responsibilities. |
| [data_block_format.cpp](D:/TheMorphicEngine/core/schema/data_block_format.cpp) | `D:/TheMorphicEngine/foundation/schema/data_block_format.cpp` | Move | Preserve existing responsibilities. |
| [data_block_format.hpp](D:/TheMorphicEngine/core/schema/data_block_format.hpp) | `D:/TheMorphicEngine/foundation/schema/data_block_format.hpp` | Move | Preserve existing responsibilities. |
| [data_document_validation.cpp](D:/TheMorphicEngine/core/schema/data_document_validation.cpp) | `D:/TheMorphicEngine/foundation/schema/data_document_validation.cpp` | Move | Preserve existing responsibilities. |
| [data_document_validation.hpp](D:/TheMorphicEngine/core/schema/data_document_validation.hpp) | `D:/TheMorphicEngine/foundation/schema/data_document_validation.hpp` | Move | Preserve existing responsibilities. |
| [data_reconstruction.cpp](D:/TheMorphicEngine/core/schema/data_reconstruction.cpp) | `D:/TheMorphicEngine/foundation/schema/data_reconstruction.cpp` | Move | Preserve existing responsibilities. |
| [data_reconstruction.hpp](D:/TheMorphicEngine/core/schema/data_reconstruction.hpp) | `D:/TheMorphicEngine/foundation/schema/data_reconstruction.hpp` | Move | Preserve existing responsibilities. |
| [data_remap.cpp](D:/TheMorphicEngine/core/schema/data_remap.cpp) | `D:/TheMorphicEngine/foundation/schema/data_remap.cpp` | Move | Preserve existing responsibilities. |
| [data_remap.hpp](D:/TheMorphicEngine/core/schema/data_remap.hpp) | `D:/TheMorphicEngine/foundation/schema/data_remap.hpp` | Move | Preserve existing responsibilities. |
| [data_storage_validation.hpp](D:/TheMorphicEngine/core/schema/data_storage_validation.hpp) | `D:/TheMorphicEngine/foundation/schema/data_storage_validation.hpp` | Move | Preserve existing responsibilities. |
| [document_construction.cpp](D:/TheMorphicEngine/core/schema/document_construction.cpp) | `D:/TheMorphicEngine/foundation/schema/document_construction.cpp` | Move | Preserve existing responsibilities. |
| [document_construction.hpp](D:/TheMorphicEngine/core/schema/document_construction.hpp) | `D:/TheMorphicEngine/foundation/schema/document_construction.hpp` | Move | Preserve existing responsibilities. |
| [document_merge.cpp](D:/TheMorphicEngine/core/schema/document_merge.cpp) | `D:/TheMorphicEngine/foundation/schema/document_merge.cpp` | Move | Preserve existing responsibilities. |
| [document_merge.hpp](D:/TheMorphicEngine/core/schema/document_merge.hpp) | `D:/TheMorphicEngine/foundation/schema/document_merge.hpp` | Move | Preserve existing responsibilities. |
| [document_query.cpp](D:/TheMorphicEngine/core/schema/document_query.cpp) | `D:/TheMorphicEngine/foundation/schema/document_query.cpp` | Move | Preserve existing responsibilities. |
| [document_query.hpp](D:/TheMorphicEngine/core/schema/document_query.hpp) | `D:/TheMorphicEngine/foundation/schema/document_query.hpp` | Move | Preserve existing responsibilities. |
| [instance_selection.cpp](D:/TheMorphicEngine/core/schema/instance_selection.cpp) | `D:/TheMorphicEngine/foundation/schema/instance_selection.cpp` | Move | Preserve existing responsibilities. |
| [instance_selection.hpp](D:/TheMorphicEngine/core/schema/instance_selection.hpp) | `D:/TheMorphicEngine/foundation/schema/instance_selection.hpp` | Move | Preserve existing responsibilities. |
| [integer_notation.hpp](D:/TheMorphicEngine/core/schema/integer_notation.hpp) | `D:/TheMorphicEngine/foundation/schema/integer_notation.hpp` | Move | Preserve existing responsibilities. |
| [live_bulk_data.cpp](D:/TheMorphicEngine/core/schema/live_bulk_data.cpp) | `D:/TheMorphicEngine/foundation/schema/live_bulk_data.cpp` | Move | Preserve existing responsibilities. |
| [live_bulk_data.hpp](D:/TheMorphicEngine/core/schema/live_bulk_data.hpp) | `D:/TheMorphicEngine/foundation/schema/live_bulk_data.hpp` | Move | Preserve existing responsibilities. |
| [live_instances.cpp](D:/TheMorphicEngine/core/schema/live_instances.cpp) | `D:/TheMorphicEngine/foundation/schema/live_instances.cpp` | Move | Preserve existing responsibilities. |
| [live_instances.hpp](D:/TheMorphicEngine/core/schema/live_instances.hpp) | `D:/TheMorphicEngine/foundation/schema/live_instances.hpp` | Move | Preserve existing responsibilities. |
| [live_instances_edit.cpp](D:/TheMorphicEngine/core/schema/live_instances_edit.cpp) | `D:/TheMorphicEngine/foundation/schema/live_instances_edit.cpp` | Move | Preserve existing responsibilities. |
| [live_instances_translation.cpp](D:/TheMorphicEngine/core/schema/live_instances_translation.cpp) | `D:/TheMorphicEngine/foundation/schema/live_instances_translation.cpp` | Move | Preserve existing responsibilities. |
| [prepared_schema.cpp](D:/TheMorphicEngine/core/schema/prepared_schema.cpp) | `D:/TheMorphicEngine/foundation/schema/prepared_schema.cpp` | Move | Preserve existing responsibilities. |
| [prepared_schema.hpp](D:/TheMorphicEngine/core/schema/prepared_schema.hpp) | `D:/TheMorphicEngine/foundation/schema/prepared_schema.hpp` | Move | Preserve existing responsibilities. |
| [record_index.hpp](D:/TheMorphicEngine/core/schema/record_index.hpp) | `D:/TheMorphicEngine/foundation/schema/record_index.hpp` | Move | Preserve existing responsibilities. |
| [resolved_types.cpp](D:/TheMorphicEngine/core/schema/resolved_types.cpp) | `D:/TheMorphicEngine/foundation/schema/resolved_types.cpp` | Move | Preserve existing responsibilities. |
| [resolved_types.hpp](D:/TheMorphicEngine/core/schema/resolved_types.hpp) | `D:/TheMorphicEngine/foundation/schema/resolved_types.hpp` | Move | Preserve existing responsibilities. |
| [schema.cpp](D:/TheMorphicEngine/core/schema/schema.cpp) | `D:/TheMorphicEngine/foundation/schema/schema.cpp` | Move | Preserve existing responsibilities. |
| [schema.hpp](D:/TheMorphicEngine/core/schema/schema.hpp) | `D:/TheMorphicEngine/foundation/schema/schema.hpp` | Move | Preserve existing responsibilities. |
| [schema_runtime.cpp](D:/TheMorphicEngine/core/schema/schema_runtime.cpp) | `D:/TheMorphicEngine/foundation/schema/schema_runtime.cpp` | Move | Preserve existing responsibilities. |
| [type_compatibility.cpp](D:/TheMorphicEngine/core/schema/type_compatibility.cpp) | `D:/TheMorphicEngine/foundation/schema/type_compatibility.cpp` | Move | Preserve existing responsibilities. |
| [type_compatibility.hpp](D:/TheMorphicEngine/core/schema/type_compatibility.hpp) | `D:/TheMorphicEngine/foundation/schema/type_compatibility.hpp` | Move | Preserve existing responsibilities. |
| [types_admission.cpp](D:/TheMorphicEngine/core/schema/types_admission.cpp) | `D:/TheMorphicEngine/foundation/schema/types_admission.cpp` | Move | Preserve existing responsibilities. |
| [types_admission.hpp](D:/TheMorphicEngine/core/schema/types_admission.hpp) | `D:/TheMorphicEngine/foundation/schema/types_admission.hpp` | Move | Preserve existing responsibilities. |
| [types_declarations.cpp](D:/TheMorphicEngine/core/schema/types_declarations.cpp) | `D:/TheMorphicEngine/foundation/schema/types_declarations.cpp` | Move | Preserve existing responsibilities. |
| [types_output.cpp](D:/TheMorphicEngine/core/schema/types_output.cpp) | `D:/TheMorphicEngine/foundation/schema/types_output.cpp` | Move | Preserve existing responsibilities. |
| [types_wrappers.cpp](D:/TheMorphicEngine/core/schema/types_wrappers.cpp) | `D:/TheMorphicEngine/foundation/schema/types_wrappers.cpp` | Move | Preserve existing responsibilities. |
| [types_wrappers.hpp](D:/TheMorphicEngine/core/schema/types_wrappers.hpp) | `D:/TheMorphicEngine/foundation/schema/types_wrappers.hpp` | Move | Preserve existing responsibilities. |
| [unused_storage.cpp](D:/TheMorphicEngine/core/schema/unused_storage.cpp) | `D:/TheMorphicEngine/foundation/schema/unused_storage.cpp` | Move | Preserve existing responsibilities. |
| [unused_storage.hpp](D:/TheMorphicEngine/core/schema/unused_storage.hpp) | `D:/TheMorphicEngine/foundation/schema/unused_storage.hpp` | Move | Preserve existing responsibilities. |
| [value_codec.cpp](D:/TheMorphicEngine/core/schema/value_codec.cpp) | `D:/TheMorphicEngine/foundation/schema/value_codec.cpp` | Move | Preserve existing responsibilities. |
| [value_codec.hpp](D:/TheMorphicEngine/core/schema/value_codec.hpp) | `D:/TheMorphicEngine/foundation/schema/value_codec.hpp` | Move | Preserve existing responsibilities. |
| [value_conversion.hpp](D:/TheMorphicEngine/core/schema/value_conversion.hpp) | `D:/TheMorphicEngine/foundation/schema/value_conversion.hpp` | Move | Preserve existing responsibilities. |
| [value_work_budget.hpp](D:/TheMorphicEngine/core/schema/value_work_budget.hpp) | `D:/TheMorphicEngine/foundation/schema/value_work_budget.hpp` | Move | Preserve existing responsibilities. |

### services

Shared service protocols and reusable client participation belong in Framework.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [service_borrow.hpp](D:/TheMorphicEngine/core/services/service_borrow.hpp) | `D:/TheMorphicEngine/framework/services/service_borrow.hpp` | Move | Shared routing/borrow/notification contract or client helper; operational registries remain in Host. |
| [service_borrow_client.cpp](D:/TheMorphicEngine/core/services/service_borrow_client.cpp) | `D:/TheMorphicEngine/framework/services/service_borrow_client.cpp` | Move | Shared routing/borrow/notification contract or client helper; operational registries remain in Host. |
| [service_borrow_client.hpp](D:/TheMorphicEngine/core/services/service_borrow_client.hpp) | `D:/TheMorphicEngine/framework/services/service_borrow_client.hpp` | Move | Shared routing/borrow/notification contract or client helper; operational registries remain in Host. |
| [service_notifications.hpp](D:/TheMorphicEngine/core/services/service_notifications.hpp) | `D:/TheMorphicEngine/framework/services/service_notifications.hpp` | Move | Shared routing/borrow/notification contract or client helper; operational registries remain in Host. |
| [service_protocol.hpp](D:/TheMorphicEngine/core/services/service_protocol.hpp) | `D:/TheMorphicEngine/framework/services/service_protocol.hpp` | Move | Shared routing/borrow/notification contract or client helper; operational registries remain in Host. |

### slots

Slot management remains foundational.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [SlotsRankMap.hpp](D:/TheMorphicEngine/core/slots/SlotsRankMap.hpp) | `D:/TheMorphicEngine/foundation/slots/SlotsRankMap.hpp` | Move | Preserve existing responsibilities. |
| [TOrderedSlotManager.hpp](D:/TheMorphicEngine/core/slots/TOrderedSlotManager.hpp) | `D:/TheMorphicEngine/foundation/slots/TOrderedSlotManager.hpp` | Move | Preserve existing responsibilities. |
| [TUnorderedSlotManager.hpp](D:/TheMorphicEngine/core/slots/TUnorderedSlotManager.hpp) | `D:/TheMorphicEngine/foundation/slots/TUnorderedSlotManager.hpp` | Move | Preserve existing responsibilities. |

### system

Separate abstract identity, registration and erasure mechanisms from concrete Morphic catalogues.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [TStaticLookup.hpp](D:/TheMorphicEngine/core/system/TStaticLookup.hpp) | `D:/TheMorphicEngine/foundation/containers/TStaticLookup.hpp` | Relocate | Correct the historical system-directory placement. |
| [async_state.hpp](D:/TheMorphicEngine/core/system/async_state.hpp) | `D:/TheMorphicEngine/foundation/system/async_state.hpp` | Move | Erased POD slots; callers govern terminal release. No messaging/topology dependency. |
| [erased_owner.cpp](D:/TheMorphicEngine/core/system/erased_owner.cpp) | `D:/TheMorphicEngine/foundation/system/erased_owner.cpp` | Adapt | Keep owner and abstract mount dependencies; replace concrete catalogue-count coupling with capacity/registry checks. S6. |
| [erased_owner.hpp](D:/TheMorphicEngine/core/system/erased_owner.hpp) | `D:/TheMorphicEngine/foundation/system/erased_owner.hpp` | Adapt | Keep owner and abstract mount dependencies; replace concrete catalogue-count coupling with capacity/registry checks. S6. |
| [erased_owner_operations.cpp](D:/TheMorphicEngine/core/system/erased_owner_operations.cpp) | `D:/TheMorphicEngine/foundation/system/erased_owner_operations.cpp`; `D:/TheMorphicEngine/framework/system/system_erased_owner_operations.cpp` | Split | Separate generic operation registry/factories from concrete SYSTEM table generation and system_operations_view(). S6. |
| [erased_owner_operations.hpp](D:/TheMorphicEngine/core/system/erased_owner_operations.hpp) | `D:/TheMorphicEngine/foundation/system/erased_owner_operations.hpp`; `D:/TheMorphicEngine/framework/system/system_erased_owner_operations.hpp` | Split | Separate generic operation registry/factories from concrete SYSTEM table generation and system_operations_view(). S6. |
| [erased_owner_registration.hpp](D:/TheMorphicEngine/core/system/erased_owner_registration.hpp) | `D:/TheMorphicEngine/foundation/system/erased_owner_registration.hpp` | Move | Preserve existing responsibilities. |
| [erased_owner_transfer.hpp](D:/TheMorphicEngine/core/system/erased_owner_transfer.hpp) | `D:/TheMorphicEngine/foundation/system/erased_owner_transfer.hpp` | Move | Preserve existing responsibilities. |
| [erased_owner_transport.cpp](D:/TheMorphicEngine/core/system/erased_owner_transport.cpp) | `D:/TheMorphicEngine/foundation/system/erased_owner_transport.cpp` | Move | Preserve existing responsibilities. |
| [erased_owner_transport.hpp](D:/TheMorphicEngine/core/system/erased_owner_transport.hpp) | `D:/TheMorphicEngine/foundation/system/erased_owner_transport.hpp` | Move | Preserve existing responsibilities. |
| [erased_pod.hpp](D:/TheMorphicEngine/core/system/erased_pod.hpp) | `D:/TheMorphicEngine/foundation/system/erased_pod.hpp` | Move | Preserve existing responsibilities. |
| [erased_transport_admission.cpp](D:/TheMorphicEngine/core/system/erased_transport_admission.cpp) | `D:/TheMorphicEngine/foundation/system/erased_transport_admission.cpp` | Move | Preserve existing responsibilities. |
| [erased_transport_admission.hpp](D:/TheMorphicEngine/core/system/erased_transport_admission.hpp) | `D:/TheMorphicEngine/foundation/system/erased_transport_admission.hpp` | Move | Preserve existing responsibilities. |
| [local_type_ids.cpp.inl](D:/TheMorphicEngine/core/system/local_type_ids.cpp.inl) | `D:/TheMorphicEngine/foundation/system/local_type_ids.cpp.inl` | Move | Preserve existing responsibilities. |
| [local_type_ids.hpp.inl](D:/TheMorphicEngine/core/system/local_type_ids.hpp.inl) | `D:/TheMorphicEngine/foundation/system/local_type_ids.hpp.inl` | Move | Preserve existing responsibilities. |
| [local_type_registration.hpp](D:/TheMorphicEngine/core/system/local_type_registration.hpp) | `D:/TheMorphicEngine/foundation/system/local_type_registration.hpp` | Move | Preserve existing responsibilities. |
| [local_type_registry.cpp](D:/TheMorphicEngine/core/system/local_type_registry.cpp) | `D:/TheMorphicEngine/foundation/system/local_type_registry.cpp` | Move | Preserve existing responsibilities. |
| [local_type_registry.hpp](D:/TheMorphicEngine/core/system/local_type_registry.hpp) | `D:/TheMorphicEngine/foundation/system/local_type_registry.hpp` | Move | Preserve existing responsibilities. |
| [runtime_ids.def](D:/TheMorphicEngine/core/system/runtime_ids.def) | `D:/TheMorphicEngine/framework/system/runtime_ids.def` | Move | Concrete Morphic catalogue/binding/aggregate; no Foundation dependency on this file. S5/S6. |
| [storage_type_bindings.hpp](D:/TheMorphicEngine/core/system/storage_type_bindings.hpp) | `D:/TheMorphicEngine/framework/system/storage_type_bindings.hpp` | Move | Concrete Morphic catalogue/binding/aggregate; no Foundation dependency on this file. S5/S6. |
| [system_context.cpp](D:/TheMorphicEngine/core/system/system_context.cpp) | `D:/TheMorphicEngine/foundation/system/system_context.cpp` | Move | Preserve existing responsibilities. |
| [system_context.hpp](D:/TheMorphicEngine/core/system/system_context.hpp) | `D:/TheMorphicEngine/foundation/system/system_context.hpp` | Move | Preserve existing responsibilities. |
| [system_erased_owner_payloads.def](D:/TheMorphicEngine/core/system/system_erased_owner_payloads.def) | `D:/TheMorphicEngine/framework/system/system_erased_owner_payloads.def` | Move | Concrete Morphic catalogue/binding/aggregate; no Foundation dependency on this file. S5/S6. |
| [system_id_registry.hpp](D:/TheMorphicEngine/core/system/system_id_registry.hpp) | `D:/TheMorphicEngine/foundation/system/system_id_registry.hpp` | Move | Preserve existing responsibilities. |
| [system_ids.cpp](D:/TheMorphicEngine/core/system/system_ids.cpp) | `D:/TheMorphicEngine/foundation/system/system_ids.cpp` | Move / adapt | Generic registry validation, installation and lookup remain here; canonical table instances remain Host-owned. S5. |
| [system_ids.hpp](D:/TheMorphicEngine/core/system/system_ids.hpp) | `D:/TheMorphicEngine/foundation/system/system_ids.hpp`; `D:/TheMorphicEngine/framework/system/system_ids.hpp` | Split | Abstract encodings/types/helpers versus generated configured IDs and combined constants. S5. |
| [system_type_ids.def](D:/TheMorphicEngine/core/system/system_type_ids.def) | `D:/TheMorphicEngine/framework/system/system_type_ids.def` | Move | Concrete Morphic catalogue/binding/aggregate; no Foundation dependency on this file. S5/S6. |
| [system_type_registration.hpp](D:/TheMorphicEngine/core/system/system_type_registration.hpp) | `D:/TheMorphicEngine/foundation/system/system_type_registration.hpp` | Move | Preserve existing responsibilities. |
| [thread_ids.def](D:/TheMorphicEngine/core/system/thread_ids.def) | `D:/TheMorphicEngine/framework/system/thread_ids.def` | Move | Concrete Morphic catalogue/binding/aggregate; no Foundation dependency on this file. S5/S6. |
| [transported_types.hpp](D:/TheMorphicEngine/core/system/transported_types.hpp) | `D:/TheMorphicEngine/framework/system/transported_types.hpp` | Move | Concrete Morphic catalogue/binding/aggregate; no Foundation dependency on this file. S5/S6. |
| [type_id_binding_category.hpp](D:/TheMorphicEngine/core/system/type_id_binding_category.hpp) | `D:/TheMorphicEngine/foundation/system/type_id_binding_category.hpp` | Move | Preserve existing responsibilities. |
| [type_registration.hpp](D:/TheMorphicEngine/core/system/type_registration.hpp) | `D:/TheMorphicEngine/foundation/system/type_registration.hpp` | Move | Preserve existing responsibilities. |

### text

Text processing and diagnostics remain foundational.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [text_diagnostics.hpp](D:/TheMorphicEngine/core/text/text_diagnostics.hpp) | `D:/TheMorphicEngine/foundation/text/text_diagnostics.hpp` | Move | Preserve existing responsibilities. |
| [text_linter.cpp](D:/TheMorphicEngine/core/text/text_linter.cpp) | `D:/TheMorphicEngine/foundation/text/text_linter.cpp` | Move | Preserve existing responsibilities. |
| [text_linter.hpp](D:/TheMorphicEngine/core/text/text_linter.hpp) | `D:/TheMorphicEngine/foundation/text/text_linter.hpp` | Move | Preserve existing responsibilities. |
| [utf8_string.cpp](D:/TheMorphicEngine/core/text/utf8_string.cpp) | `D:/TheMorphicEngine/foundation/text/utf8_string.cpp` | Move | Preserve existing responsibilities. |
| [utf8_string.hpp](D:/TheMorphicEngine/core/text/utf8_string.hpp) | `D:/TheMorphicEngine/foundation/text/utf8_string.hpp` | Move | Preserve existing responsibilities. |

### threading

Separate generic synchronisation/transports, common participation and Host thread ownership.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [CBatchWork.hpp](D:/TheMorphicEngine/core/threading/CBatchWork.hpp) | `D:/TheMorphicEngine/framework/threading/CBatchWork.hpp` | Move | Shared work, channels and CBatchClient; Host owns and schedules the pool. S7. |
| [CCountingSemaphore.cpp](D:/TheMorphicEngine/core/threading/CCountingSemaphore.cpp) | `D:/TheMorphicEngine/foundation/threading/CCountingSemaphore.cpp` | Move | Preserve existing responsibilities. |
| [CCountingSemaphore.hpp](D:/TheMorphicEngine/core/threading/CCountingSemaphore.hpp) | `D:/TheMorphicEngine/foundation/threading/CCountingSemaphore.hpp` | Move | Preserve existing responsibilities. |
| [CNotification.hpp](D:/TheMorphicEngine/core/threading/CNotification.hpp) | `D:/TheMorphicEngine/foundation/threading/CNotification.hpp` | Move | Preserve existing responsibilities. |
| [CParkingGate.cpp](D:/TheMorphicEngine/core/threading/CParkingGate.cpp) | `D:/TheMorphicEngine/foundation/threading/CParkingGate.cpp` | Move | Preserve existing responsibilities. |
| [CParkingGate.hpp](D:/TheMorphicEngine/core/threading/CParkingGate.hpp) | `D:/TheMorphicEngine/foundation/threading/CParkingGate.hpp` | Move | Preserve existing responsibilities. |
| [CThreadControlState.hpp](D:/TheMorphicEngine/core/threading/CThreadControlState.hpp) | `D:/TheMorphicEngine/foundation/threading/CThreadControlState.hpp` | Move | Preserve existing responsibilities. |
| [CThreadPackage.cpp](D:/TheMorphicEngine/core/threading/CThreadPackage.cpp) | `D:/TheMorphicEngine/host/threading/CThreadPackage.cpp`; `D:/TheMorphicEngine/framework/threading/CThreadResources.hpp`; `D:/TheMorphicEngine/framework/threading/CThreadContext.cpp` | Split | Host package/launch/native lifetime versus shared resources and worker context. S4. |
| [CThreadPackage.hpp](D:/TheMorphicEngine/core/threading/CThreadPackage.hpp) | `D:/TheMorphicEngine/host/threading/CThreadPackage.hpp`; `D:/TheMorphicEngine/framework/threading/CThreadResources.hpp`; `D:/TheMorphicEngine/framework/threading/CThreadContext.hpp` | Split | Host package/launch/native lifetime versus shared resources and worker context. S4. |
| [CWaitPredicate.cpp](D:/TheMorphicEngine/core/threading/CWaitPredicate.cpp) | `D:/TheMorphicEngine/foundation/threading/CWaitPredicate.cpp` | Move | Preserve existing responsibilities. |
| [CWaitPredicate.hpp](D:/TheMorphicEngine/core/threading/CWaitPredicate.hpp) | `D:/TheMorphicEngine/foundation/threading/CWaitPredicate.hpp` | Move | Preserve existing responsibilities. |
| [messages/CErasedMessageTransports.hpp](D:/TheMorphicEngine/core/threading/messages/CErasedMessageTransports.hpp) | `D:/TheMorphicEngine/framework/threading/messages/CErasedMessageTransports.hpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/CErasedOwnerMsg.hpp](D:/TheMorphicEngine/core/threading/messages/CErasedOwnerMsg.hpp) | `D:/TheMorphicEngine/framework/threading/messages/CErasedOwnerMsg.hpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/CErasedOwnerMsgTransport.cpp](D:/TheMorphicEngine/core/threading/messages/CErasedOwnerMsgTransport.cpp) | `D:/TheMorphicEngine/framework/threading/messages/CErasedOwnerMsgTransport.cpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/CErasedPodMsg.hpp](D:/TheMorphicEngine/core/threading/messages/CErasedPodMsg.hpp) | `D:/TheMorphicEngine/framework/threading/messages/CErasedPodMsg.hpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/CErasedPodMsgTransport.cpp](D:/TheMorphicEngine/core/threading/messages/CErasedPodMsgTransport.cpp) | `D:/TheMorphicEngine/framework/threading/messages/CErasedPodMsgTransport.cpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/SErasedMsgHeader.hpp](D:/TheMorphicEngine/core/threading/messages/SErasedMsgHeader.hpp) | `D:/TheMorphicEngine/framework/threading/messages/SErasedMsgHeader.hpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/byte_view_payloads.hpp](D:/TheMorphicEngine/core/threading/messages/byte_view_payloads.hpp) | `D:/TheMorphicEngine/framework/threading/messages/byte_view_payloads.hpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/message_errors.hpp](D:/TheMorphicEngine/core/threading/messages/message_errors.hpp) | `D:/TheMorphicEngine/framework/threading/messages/message_errors.hpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/message_helpers.hpp](D:/TheMorphicEngine/core/threading/messages/message_helpers.hpp) | `D:/TheMorphicEngine/framework/threading/messages/message_helpers.hpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [messages/messages.hpp](D:/TheMorphicEngine/core/threading/messages/messages.hpp) | `D:/TheMorphicEngine/framework/threading/messages/messages.hpp` | Move | Morphic message envelope, payload convention, helper or message transport; underlying erasure/transports remain foundational. |
| [threading.hpp](D:/TheMorphicEngine/core/threading/threading.hpp) | `D:/TheMorphicEngine/foundation/threading/threading.hpp`; `D:/TheMorphicEngine/framework/threading/threading.hpp` | Split umbrella | Foundation aggregate exposes primitives only; participation aggregate excludes Host package. S4/S8. |
| [transports/TBulkTransport.hpp](D:/TheMorphicEngine/core/threading/transports/TBulkTransport.hpp) | `D:/TheMorphicEngine/foundation/threading/transports/TBulkTransport.hpp` | Move | Preserve existing responsibilities. |
| [transports/TMpmcTransport.hpp](D:/TheMorphicEngine/core/threading/transports/TMpmcTransport.hpp) | `D:/TheMorphicEngine/foundation/threading/transports/TMpmcTransport.hpp` | Move | Preserve existing responsibilities. |
| [transports/TOwningTransport.hpp](D:/TheMorphicEngine/core/threading/transports/TOwningTransport.hpp) | `D:/TheMorphicEngine/foundation/threading/transports/TOwningTransport.hpp` | Move | Preserve existing responsibilities. |
| [transports/TQueueTransport.hpp](D:/TheMorphicEngine/core/threading/transports/TQueueTransport.hpp) | `D:/TheMorphicEngine/foundation/threading/transports/TQueueTransport.hpp` | Move | Preserve existing responsibilities. |
| [transports/TRingTransport.hpp](D:/TheMorphicEngine/core/threading/transports/TRingTransport.hpp) | `D:/TheMorphicEngine/foundation/threading/transports/TRingTransport.hpp` | Move | Preserve existing responsibilities. |
| [transports/transports.hpp](D:/TheMorphicEngine/core/threading/transports/transports.hpp) | `D:/TheMorphicEngine/foundation/threading/transports/transports.hpp` | Move | Preserve existing responsibilities. |

### types

Primitive numeric and atomic representations remain foundational.

| Current file | Destination file or files | Action | Notes |
| --- | --- | --- | --- |
| [atomic_types.hpp](D:/TheMorphicEngine/core/types/atomic_types.hpp) | `D:/TheMorphicEngine/foundation/types/atomic_types.hpp` | Move | Preserve existing responsibilities. |
| [fp16data_t.cpp](D:/TheMorphicEngine/core/types/fp16data_t.cpp) | `D:/TheMorphicEngine/foundation/types/fp16data_t.cpp` | Move | Preserve existing responsibilities. |
| [fp16data_t.hpp](D:/TheMorphicEngine/core/types/fp16data_t.hpp) | `D:/TheMorphicEngine/foundation/types/fp16data_t.hpp` | Move | Preserve existing responsibilities. |

## Integration outside Core

The following existing files are concrete integration points. The Core inventory
above is exhaustive; this consumer list identifies semantic changes rather than
every mechanical include replacement. Search all source, test, project and
documentation references again during implementation so a newly added consumer
cannot be missed.

| Existing file | Planned change |
| --- | --- |
| [host/system/system_id_definitions.hpp](D:/TheMorphicEngine/host/system/system_id_definitions.hpp) | Retain canonical concrete identity table declarations in Host; consume Framework catalogues and Foundation registry contracts. |
| [host/system/system_id_definitions.cpp](D:/TheMorphicEngine/host/system/system_id_definitions.cpp) | Retain canonical table generation/instances and configured-name authority in Host. |
| [host/system/host_context.hpp](D:/TheMorphicEngine/host/system/host_context.hpp) | Update dependency ownership and include paths for moved contracts. |
| [host/system/host_context.cpp](D:/TheMorphicEngine/host/system/host_context.cpp) | Preserve Host installation of abstract ambient context and registry access. |
| [host/entry/bootstrap_config.cpp](D:/TheMorphicEngine/host/entry/bootstrap_config.cpp) | Use Host configuration substitution after extracting expand_module_extension(). |
| [host/runtime/host.hpp](D:/TheMorphicEngine/host/runtime/host.hpp) | Include Host package and repository, Framework contracts and Foundation diagnostics explicitly. |
| [host/runtime/host.cpp](D:/TheMorphicEngine/host/runtime/host.cpp) | Supply debug configuration and thread endpoints; retain startup/shutdown orchestration and service lifetime. |
| [host/runtime/host_worker_thread.cpp](D:/TheMorphicEngine/host/runtime/host_worker_thread.cpp) | Update Host-private image/storage includes and shared worker-resource access. |
| [host/runtime/client_record.hpp](D:/TheMorphicEngine/host/runtime/client_record.hpp) | Use Host package ownership and Framework module/service contracts. |
| [host/runtime/client_record.cpp](D:/TheMorphicEngine/host/runtime/client_record.cpp) | Adapt shared-resource and package access without changing client lifetime. |
| [host/runtime/module_service.hpp](D:/TheMorphicEngine/host/runtime/module_service.hpp) | Update Host package/bound-module dependencies. |
| [host/runtime/module_service.cpp](D:/TheMorphicEngine/host/runtime/module_service.cpp) | Retain operational unloading/retirement and adjust package/binding calls. |
| [host/runtime/asset_service.hpp](D:/TheMorphicEngine/host/runtime/asset_service.hpp) | Use Host repository and Framework asset protocol explicitly. |
| [host/runtime/asset_service.cpp](D:/TheMorphicEngine/host/runtime/asset_service.cpp) | Preserve orchestration, acquisition matching and backing lifetime; adjust moved dependencies. |
| [host/runtime/asset_schema.cpp](D:/TheMorphicEngine/host/runtime/asset_schema.cpp) | Keep scheduling/acquisition in Host and schema mechanics in Foundation. |
| [host/runtime/host_maintenance.hpp](D:/TheMorphicEngine/host/runtime/host_maintenance.hpp) | Update package and Framework execution-view dependencies. |
| [host/runtime/host_maintenance.cpp](D:/TheMorphicEngine/host/runtime/host_maintenance.cpp) | Preserve maintenance/pin lifetime across thread-context separation. |
| [host/runtime/service_borrows.hpp](D:/TheMorphicEngine/host/runtime/service_borrows.hpp) | Use Framework contracts while retaining Host registry and lifetime decisions. |
| [host/runtime/batch_pool.hpp](D:/TheMorphicEngine/host/runtime/batch_pool.hpp) | Own the pool while consuming Framework work/client/shared representations. |
| [host/runtime/batch_pool.cpp](D:/TheMorphicEngine/host/runtime/batch_pool.cpp) | Keep provisioning, scheduling and runner lifecycle in Host. |
| [host/runtime/host_restrictions.hpp](D:/TheMorphicEngine/host/runtime/host_restrictions.hpp) | Keep operational restriction ownership in Host. |
| [host/runtime/host_restrictions.cpp](D:/TheMorphicEngine/host/runtime/host_restrictions.cpp) | Continue publishing through Framework representation; preserve eligibility accounting. |
| [host/module/types/local_types.hpp](D:/TheMorphicEngine/host/module/types/local_types.hpp) | Use relocated Host-private image/storage payload definitions. |
| [host/module/types/local_type_ids.hpp](D:/TheMorphicEngine/host/module/types/local_type_ids.hpp) | Keep local catalogue generation per Host binary and include generic Foundation generator. |
| [host/module/types/local_type_ids.cpp](D:/TheMorphicEngine/host/module/types/local_type_ids.cpp) | Keep Host-local operations generation and installation separate from other binaries. |
| [host/module/types/local_type_ids.def](D:/TheMorphicEngine/host/module/types/local_type_ids.def) | Retain component-local type membership and ordering. |
| [host/module/types/erased_owner_payloads.def](D:/TheMorphicEngine/host/module/types/erased_owner_payloads.def) | Update include of relocated Host image payload fragment; preserve eligibility. |
| [framework/communications/direct_client.hpp](D:/TheMorphicEngine/framework/communications/direct_client.hpp) | Use Framework worker context/resources instead of Host package header. |
| [framework/platform/input.hpp](D:/TheMorphicEngine/framework/platform/input.hpp) | Use lightweight Foundation reporting declarations if full debug administration is unnecessary. |
| [executive/runtime/executive_thread.cpp](D:/TheMorphicEngine/executive/runtime/executive_thread.cpp) | Use Framework worker context/resources and revised provisioning access. |
| [audio/runtime/audio_thread.hpp](D:/TheMorphicEngine/audio/runtime/audio_thread.hpp) | Remove dependency on Host package declaration; use shared worker-facing types. |
| [audio/runtime/audio_thread.cpp](D:/TheMorphicEngine/audio/runtime/audio_thread.cpp) | Update worker context access while preserving module lifecycle. |
| [rendering/runtime/rendering_thread.cpp](D:/TheMorphicEngine/rendering/runtime/rendering_thread.cpp) | Use Framework context and state-preserving wait for exit/drain paths. |
| [rendering/runtime/shader_requests.hpp](D:/TheMorphicEngine/rendering/runtime/shader_requests.hpp) | Use shared worker participation declarations. |
| [rendering/runtime/texture_flow_tests.hpp](D:/TheMorphicEngine/rendering/runtime/texture_flow_tests.hpp) | Update worker-resource/context access. |
| [rendering/shader/shader_requests.hpp](D:/TheMorphicEngine/rendering/shader/shader_requests.hpp) | Narrow debug dependencies where appropriate. |
| [rendering/texture/texture_requests.hpp](D:/TheMorphicEngine/rendering/texture/texture_requests.hpp) | Narrow debug dependencies where appropriate. |
| [tests/environment/test_environment.cpp](D:/TheMorphicEngine/tests/environment/test_environment.cpp) | Install test registry, debug configuration and per-binary operations using the same boundaries. |
| [tests/environment/local_types.hpp](D:/TheMorphicEngine/tests/environment/local_types.hpp) | Update test-local payload dependencies; tests may deliberately cover Host-private implementations. |
| [tests/host_pump/fixture.cpp](D:/TheMorphicEngine/tests/host_pump/fixture.cpp) | Update common participation/provisioning includes without importing Host thread ownership into the provider DLL. |
| [tests/module_lifecycle/fixture.cpp](D:/TheMorphicEngine/tests/module_lifecycle/fixture.cpp) | Use Framework worker resources and per-component registration. |
| [tests/input_host/consumer.hpp](D:/TheMorphicEngine/tests/input_host/consumer.hpp) | Use worker-facing Framework declarations. |
| [tests/input_host/consumer.cpp](D:/TheMorphicEngine/tests/input_host/consumer.cpp) | Adapt context/provisioning access. |
| [tests/support/thread_helpers.cpp](D:/TheMorphicEngine/tests/support/thread_helpers.cpp) | Separate test-owned native execution from worker-facing resources. |
| [tests/support/host_client_fixture.hpp](D:/TheMorphicEngine/tests/support/host_client_fixture.hpp) | Use Host package explicitly where the fixture owns it. |
| [tests/support/direct_connection_fixture.hpp](D:/TheMorphicEngine/tests/support/direct_connection_fixture.hpp) | Update package/context split and preserve lifetime setup. |
| [tests/support/resource_service_fixture.hpp](D:/TheMorphicEngine/tests/support/resource_service_fixture.hpp) | Update Host ownership and Framework service-contract dependencies. |

Other component-local type catalogues remain with their current component.
Their generator and concrete shared-operation includes must be updated without
moving their local identities into a global catalogue.

### Project manifests and policy

| Existing file | Planned change |
| --- | --- |
| [visual_studio/MorphicCore.vcxitems](D:/TheMorphicEngine/visual_studio/MorphicCore.vcxitems) | Remove Framework/Host sources and headers; retain Foundation shared sources. Rename to MorphicFoundation.vcxitems only at the rename stage. |
| [visual_studio/MorphicCore.vcxitems.filters](D:/TheMorphicEngine/visual_studio/MorphicCore.vcxitems.filters) | Mirror retained Foundation files and eventual rename; preserve CRLF, encoding and BOM state. |
| [visual_studio/MorphicFramework.vcxitems](D:/TheMorphicEngine/visual_studio/MorphicFramework.vcxitems) | Add common participation implementations and concrete shared operation-table source. Update the present header-only description; do not import Foundation twice. |
| [visual_studio/MorphicFramework.vcxitems.filters](D:/TheMorphicEngine/visual_studio/MorphicFramework.vcxitems.filters) | Add moved and split Framework files. |
| [visual_studio/MorphicHost.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicHost.vcxproj) | Compile Host-private moved implementations, including package, binding and filesystem image, exactly once. |
| [visual_studio/MorphicHost.vcxproj.filters](D:/TheMorphicEngine/visual_studio/MorphicHost.vcxproj.filters) | Represent the resulting Host layout. |
| [visual_studio/MorphicTests.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicTests.vcxproj) | Include required Host-private test subjects explicitly; avoid relying on the old all-Core import. |
| [visual_studio/MorphicTests.vcxproj.filters](D:/TheMorphicEngine/visual_studio/MorphicTests.vcxproj.filters) | Reflect revised tested sources and include layout. |
| [visual_studio/MorphicExecutive.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicExecutive.vcxproj) | Retain separate Foundation/Framework compilation and update imports/include resolution. |
| [visual_studio/MorphicRendering.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicRendering.vcxproj) | Retain separate Foundation/Framework compilation and update imports/include resolution. |
| [visual_studio/MorphicAudio.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicAudio.vcxproj) | Retain separate Foundation/Framework compilation and update imports/include resolution. |
| [visual_studio/MorphicPlatform.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicPlatform.vcxproj) | Retain separate Foundation/Framework compilation and update imports/include resolution. |
| [visual_studio/MorphicLifecycleFixture.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicLifecycleFixture.vcxproj) | Update shared imports and per-component bootstrap dependencies. |
| [visual_studio/MorphicPumpFixture.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicPumpFixture.vcxproj) | Compile common participation sources in each fixture binary. |
| [visual_studio/MorphicInputPlatform.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicInputPlatform.vcxproj) | Update shared imports without breaking production-source parity. |
| [visual_studio/MorphicInputExecutive.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicInputExecutive.vcxproj) | Update shared imports and worker-facing dependencies. |
| [visual_studio/MorphicInputSecondary.vcxproj](D:/TheMorphicEngine/visual_studio/MorphicInputSecondary.vcxproj) | Update shared imports and worker-facing dependencies. |
| [MorphicEngine.sln](D:/TheMorphicEngine/MorphicEngine.sln) | Update shared-project identities/import references at final rename; preserve CRLF, encoding, BOM state and final newline. |
| [CMakeLists.txt](D:/TheMorphicEngine/CMakeLists.txt) | Update Core include root and header-only Framework assumptions; preserve one copy of shared implementation/state per executable/DLL and hidden symbol behaviour. |
| [tools/cmake_sources.py](D:/TheMorphicEngine/tools/cmake_sources.py) | Verify extraction follows the reorganised shared imports exactly once; modify only where needed. |
| [tools/tests/test_cmake_sources.py](D:/TheMorphicEngine/tools/tests/test_cmake_sources.py) | Verify compiled Framework source imports and deduplication across the new manifests. |
| [tools/policy_validator/policy_validator.cpp](D:/TheMorphicEngine/tools/policy_validator/policy_validator.cpp) | Update Core/Foundation path recognition, include resolution and windows_include exception. Enforce Foundation isolation and Framework separation from Host/module implementations. |
| [tools/tests/test_policy_includes.py](D:/TheMorphicEngine/tools/tests/test_policy_includes.py) | Update boundary cases for Foundation and Framework, including relative/absolute/alias include resolution and existing SuiteUTF allowance. |
| [tests/input_host/check_sources.py](D:/TheMorphicEngine/tests/input_host/check_sources.py) | Preserve production/fixture source checks when shared imports and paths change. |
| [tools/check_line_endings.ps1](D:/TheMorphicEngine/tools/check_line_endings.ps1) | Run the existing checker after implementation; change it only if a demonstrated rename dependency requires it. |

Also inspect companion filters for changed explicit entries, build scripts and
CI configuration for literal Core paths. Preserve shared-item GUIDs where only
a name/path changes. Do not convert Framework to a single shared runtime DLL:
its compiled participation and operation tables must retain per-binary state.
The Linux CMake build derives sources from Visual Studio manifests; it must
neither omit Framework implementations nor compile them a second time via an
additional library target.

Avoid include-root ambiguity after splitting similarly named headers such as
Foundation and Framework system_ids.hpp. Make layer selection explicit at
affected include sites; do not make correctness depend on include-directory
order or retain a Foundation umbrella that silently reimports Framework.

### Documentation alignment

Update these existing explanations when the corresponding implementation lands:

- [docs/system/type_identity.md](D:/TheMorphicEngine/docs/system/type_identity.md)
- [docs/system/module_bootstrap.md](D:/TheMorphicEngine/docs/system/module_bootstrap.md)
- [docs/system/erased_owner.md](D:/TheMorphicEngine/docs/system/erased_owner.md)
- [docs/debug/debug_system_interface.md](D:/TheMorphicEngine/docs/debug/debug_system_interface.md)
- [docs/debug/debug_service_substrate.md](D:/TheMorphicEngine/docs/debug/debug_service_substrate.md)
- [docs/assets/asynchronous_asset_services.md](D:/TheMorphicEngine/docs/assets/asynchronous_asset_services.md)
- [docs/assets/bundle_specification.md](D:/TheMorphicEngine/docs/assets/bundle_specification.md)
- [docs/schema/prepared-schema.md](D:/TheMorphicEngine/docs/schema/prepared-schema.md)
- [docs/modules/batch_runners.md](D:/TheMorphicEngine/docs/modules/batch_runners.md)
- [docs/modules/host_restriction_maintenance.md](D:/TheMorphicEngine/docs/modules/host_restriction_maintenance.md)
- [docs/modules/host_timed_wait.md](D:/TheMorphicEngine/docs/modules/host_timed_wait.md)
- [docs/project/policy_validator.md](D:/TheMorphicEngine/docs/project/policy_validator.md)
- [docs/project/building_and_testing.md](D:/TheMorphicEngine/docs/project/building_and_testing.md)
- [docs/project/linux_ci.md](D:/TheMorphicEngine/docs/project/linux_ci.md)
- [docs/project/windows_ci.md](D:/TheMorphicEngine/docs/project/windows_ci.md)
- [docs/README.md](D:/TheMorphicEngine/docs/README.md)

Preserve historical records as history; update current layout/API descriptions
and links rather than rewriting unrelated past design discussions.
No module protocol, ABI naming, catalogue ordering or persistent data-format
change is authorised merely by renaming Core.

## Proposed implementation order

1. Separate neutral asset identity, abstract system identity and concrete
   catalogues, and generic versus configured operation-table generation.
   Establish the dependency direction before moving higher-level consumers.
2. Separate debug reporting from administration and supply debug-thread
   configuration from Host. Preserve early/late reporting and per-binary
   installation.
3. Split thread ownership/resources/context and adopt the lifecycle-preserving
   wait. Update Host, modules, direct clients and fixtures together.
4. Move module/service/message/batch/restriction participation into Framework.
   Move repository, filesystem image, private image payloads and bound-module
   ownership into Host. Extract reserved mount and configuration substitution
   helpers.
5. Complete narrower includes, umbrellas, source manifests and enforceable
   layer boundaries. Confirm Foundation has no concrete topology dependency.
6. Once the separation is accepted, apply the proposed Core-to-Foundation
   directory/project rename and documentation path updates if selected.

This order describes dependencies, not a requirement for six commits or six
separate tasks. Keep intermediate builds coherent, coordinate any shared-checkout
changes, preserve unrelated edits, and commit only on explicit instruction.

## Validation when implementation is authorised

Use existing suites for relocation-only work. Add or adapt focused checks for
actual boundary/behaviour changes, especially:

- Foundation headers and sources build without Framework/Host/module headers;
  generic diagnostics, identity, erasure and schema use do not pull in topology.
- Foundation ID structure/registry checks and Framework catalogue capacity
  checks preserve rejection of invalid/unregistered identities and dependencies.
- Erased owner move/reset, transfer, attribution and destruction retain the
  correct mount-dependency mask and resolve operations in the executing binary.
  Type registration, owning eligibility and complete operations remain distinct.
- Debug reporting works before installation, during normal operation and through
  teardown; writer attribution uses the supplied ID and immediate/panic handling
  retains its guarantees.
- Ordinary context waiting retains Waiting/Running transitions; the new wait
  preserves Exiting or another existing lifecycle state. Wake/timeout behaviour,
  provisioning-pointer identity, join, notification pins and late batch
  publishers remain correct.
- Neutral CAssetId representation and comparisons are preserved; repository
  resolution, acquisition matching, payload-before-lease destruction and borrowed
  schema/view lifetimes retain existing semantics.
- Bundles, schema/data-model operations, paths, native-module filename
  construction and Host placeholder/mount handling retain existing results.

Relevant existing suites include SystemTypeIdentity, ErasedPod, ErasedOwner,
DebugService, AssetRepository, Bundle, BundleBuilder, BundleRequests,
BundleService, FilesystemImage, HostWorkers, HostRuntime, HostWait,
HostMaintenance, HostRestriction, Notification, DirectConnections, TimedWait,
SchemaService, PreparedSchema, the schema/data-model suites, and the batch and
module-lifecycle fixtures.

Validate the affected Windows configurations and Linux build/fixtures according
to the repository's current build instructions, including include-policy,
manifest/source-parity and line-ending checks. Keep x64/Win32 and Debug/Release
coverage appropriate to cross-binary identity/layout changes. Do not repeat an
already passing validation matrix without a new change or unresolved concern.

## Remaining choices

There is no unresolved destination in the current Core inventory.
The remaining choices are implementation names and grouping of extracted
headers/sources, exact include spelling, and whether/when to perform the final
Foundation rename. These choices must preserve the boundaries above; they do
not reopen the agreed ownership or lifecycle responsibilities.
