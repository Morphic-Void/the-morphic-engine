
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_service.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Sep 26
//
//  Compose file I/O and conditioning without transferring assets to clients.

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>

#include "host/runtime/asset_service.hpp"
#include "debug/macros.hpp"
#include "filesystem/logical_path.hpp"

namespace host
{

static bool bundle_recipient_available(const threading::CThreadPackage& recipient) noexcept
{
    const auto state = recipient.query_state();
    return (state != threading::EThreadRunState::Exited) && (state != threading::EThreadRunState::Failed) &&
        (state != threading::EThreadRunState::Empty);
}

static EAssetStatus texture_asset_status(const ETextureStatus status) noexcept
{
    switch (status)
    {
        case ETextureStatus::success: return EAssetStatus::success;
        case ETextureStatus::invalid_input:
        case ETextureStatus::unsupported_format: return EAssetStatus::invalid_request;
        case ETextureStatus::allocation_failed: return EAssetStatus::allocation_failed;
        default: return EAssetStatus::conditioning_failed;
    }
}

//==============================================================================
//  Service lifetime and completion publication
//==============================================================================

bool CAssetService::initialise() noexcept
{
    return m_bundles.initialise() && m_assets.initialise() && m_operations.initialise();
}

EAssetStatus CAssetService::reserve_bundle(const CStringView root, std::uint64_t& binding) noexcept
{
    if ((m_filesystem == nullptr) || !m_filesystem->document().is_ready() || (root.length() < 2u) ||
        (root.cstring()[root.length() - 1u] != ':') ||
        !logical_path::valid_component(CStringView{ root.cstring(), root.length() - 1u })) return EAssetStatus::invalid_request;
    if (m_filesystem->root_state(root.cstring()) != filesystem_image::ERootState::missing) return EAssetStatus::busy;
    return m_filesystem->reserve_bundle(root.cstring(), binding) ? EAssetStatus::success : EAssetStatus::allocation_failed;
}

void CAssetService::request_reference(const BundleReferenceRequest& request, const std::int32_t slot, threading::CThreadPackage& client) noexcept
{
    reply(client, slot, acquire_bundle(request.source));
}

bool CAssetService::is_idle() const noexcept
{
    if (!m_operations.is_empty()) return false;
    for (std::int32_t slot = m_bundles.first_live(); slot >= 0; slot = m_bundles.next_live(slot))
    {
        if (m_bundles.get_object(slot)->references.operations != 0u) return false;
    }
    return true;
}

CAssetService::SBundleControl* CAssetService::find_bundle(const SAssetBackingState* const state) noexcept
{
    for (std::int32_t slot = m_bundles.first_live(); slot >= 0; slot = m_bundles.next_live(slot))
    {
        SBundleControl* const control = m_bundles.get_object(slot);
        if (&control->references == state) return control;
    }
    return nullptr;
}

AssetResult CAssetService::retain_bundle_view(SBundleControl& control, const EAssetBackingRole role,
    const AssetResult& view, const mount_point_ids::id_type dependency) noexcept
{
    AssetResult result;
    result.status = EAssetStatus::allocation_failed;
    CAssetBackingLease lease;
    if (!lease.acquire(control.references, role)) return result;
    CErasedOwner owner = CErasedOwner::create<BorrowedBundleAsset>();
    BorrowedBundleAsset* const carrier = owner.payload<BorrowedBundleAsset>();
    if (carrier == nullptr) return result;
    if (dependency.is_valid()) owner.add_hazard(dependency);
    carrier->view = view;
    const CAssetId asset = m_assets.insert(std::move(owner), std::move(lease));
    if (!asset) return result;
    result = view;
    result.asset = asset;
    result.status = EAssetStatus::success;
    return result;
}

AssetResult CAssetService::publish_bundle(CBakedDocumentBlock& block, const std::uint64_t binding, threading::CThreadPackage& owner) noexcept
{
    AssetResult result;
    if ((m_filesystem == nullptr) || (binding == 0u)) return result;
    bundle::CBundleView view;
    const auto validation = view.reset(block.buffer_bytes().data(), block.buffer_bytes().size());
    if (!validation.succeeded())
    {
        result.status = (validation.status == bundle::EValidationStatus::allocation_failed) ?
            EAssetStatus::allocation_failed : EAssetStatus::invalid_request;
        (void)m_filesystem->remove_bundle(binding);
        return result;
    }
    const std::int32_t slot = m_bundles.emplace();
    if (slot < 0)
    {
        result.status = EAssetStatus::allocation_failed;
        (void)m_filesystem->remove_bundle(binding);
        return result;
    }
    SBundleControl& control = *m_bundles.get_object(slot);
    control.binding = binding;
    control.owner = &owner;
    control.view = view;
    AssetResult description;
    description.set_bundle_view(view);
    result = retain_bundle_view(control, EAssetBackingRole::original, description);
    if (result.asset && m_filesystem->prepare_bundle(binding, result.asset.query_value(), view) &&
        m_filesystem->publish_bundle(binding))
    {
        control.original = result.asset;
        control.block = std::move(block);
        return result;
    }
    (void)m_assets.erase(result.asset);
    (void)m_filesystem->remove_bundle(binding);
    (void)m_bundles.erase(slot);
    result = {};
    result.status = EAssetStatus::allocation_failed;
    return result;
}

AssetResult CAssetService::acquire_bundle(const CAssetId source) noexcept
{
    AssetResult result;
    result.status = EAssetStatus::invalid_asset;
    const CAssetRecord* const record = disposal_pending(source) ? nullptr : m_assets.resolve(source);
    if ((record == nullptr) || (record->backing() == nullptr) || !record->backing()->open ||
        ((record->backing_role() != EAssetBackingRole::original) && (record->backing_role() != EAssetBackingRole::complete))) return result;
    SBundleControl* const control = find_bundle(record->backing());
    if (control == nullptr) return result;
    AssetResult view;
    view.set_bundle_view(control->view);
    return retain_bundle_view(*control, EAssetBackingRole::complete, view);
}

EAssetStatus CAssetService::begin_bundle_file(const filesystem_image::SResolvedSource& source,
    const std::size_t alignment, CBundleOperation& operation) noexcept
{
    if ((operation.m_backing.state() != nullptr) || (alignment > bundle::k_alignment) ||
        (alignment == 0u) || ((alignment & (alignment - 1u)) != 0u))
    {
        return EAssetStatus::invalid_request;
    }
    const CAssetId original = m_assets.find_identity(source.bundle);
    const CAssetRecord* const record = disposal_pending(original) ? nullptr : m_assets.resolve(original);
    if ((record == nullptr) || (record->backing() == nullptr) || !record->backing()->open ||
        (record->backing_role() != EAssetBackingRole::original))
    {
        return EAssetStatus::invalid_asset;
    }
    SBundleControl* const control = find_bundle(record->backing());
    if ((control == nullptr) || (source.binding == 0u) || (control->binding != source.binding) ||
        (control->original != original)) return EAssetStatus::invalid_asset;
    const CByteConstView bytes = control->view.bytes();

    //  The image supplies validated file extents. Check without offset+size
    //  arithmetic so an invalid descriptor cannot overflow or expose metadata.
    //  Strictly less leaves storage for the required zero terminator.
    if ((source.offset < control->view.document().byte_count()) || (source.offset % bundle::k_alignment != 0u) ||
        (source.offset >= bytes.size()) || (source.size == 0u) || (source.size >= bytes.size() - source.offset))
    {
        return EAssetStatus::invalid_request;
    }
    if (!operation.m_backing.acquire(control->references, EAssetBackingRole::operation))
    {
        return EAssetStatus::allocation_failed;
    }
    operation.m_bytes = CByteConstView{ bytes.data() + source.offset, source.size, bundle::k_alignment };
    return EAssetStatus::success;
}

AssetResult CAssetService::retain_bundle_file(const CBundleOperation& operation,
    const EAssetFileFormat format, const mount_point_ids::id_type dependency) noexcept
{
    AssetResult result;
    SBundleControl* const control = find_bundle(operation.m_backing.state());
    const CByteConstView bytes = operation.bytes();
    if ((control == nullptr) || !bytes.is_ready()) return result;
    const memory::CMemoryConstView memory{ bytes.data(), bytes.size(), 1u, bytes.align() };
    if (format == EAssetFileFormat::raw) result.set_raw_view(memory);
    else if (format == EAssetFileFormat::baked)
    {
        if (bytes.size() < sizeof(SBakedDocumentHeader)) return result;
        SBakedDocumentHeader header;
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.total_size > bytes.size()) return result;
        CBakedDocument document;
        if (!document.reset(bytes.data(), header.total_size)) return result;
        //  Match ordinary baked-file adoption: expose only the checked prefix.
        result.set_baked_view(BakedAssetView{
            memory::CMemoryConstView{ bytes.data(), document.byte_count(), 1u, bytes.align() }, document });
    }
    else return result;

    //  An admitted operation may publish its result even after closure.
    return retain_bundle_view(*control, EAssetBackingRole::file, result, dependency);
}

bool CAssetService::protect_asset(const CAssetRecord& record, CAssetBackingLease& lease, const bool deriving) noexcept
{
    SAssetBackingState* const state = record.backing();
    if (state == nullptr) return true;
    if (!state->open && (deriving || (record.backing_role() != EAssetBackingRole::file))) return false;
    return lease.acquire(*state, EAssetBackingRole::operation);
}

void CAssetService::detach_client(threading::CThreadPackage& client) noexcept
{
    for (std::int32_t slot = m_bundles.first_live(); slot >= 0; slot = m_bundles.next_live(slot))
    {
        SBundleControl& control = *m_bundles.get_object(slot);
        if (control.owner == &client) control.owner = nullptr;
    }
}

void CAssetService::close_bundle(SBundleControl& control) noexcept
{
    if (!control.references.open) return;
    control.references.open = false;
    if (control.references.complete_views != 0u)
    {
        (void)debug_system::submit_event<debug_system::EEventLevel::assert, debug_system::EEventType::condition>(
            MV_INTERNAL_USAGE_POINT, "Host: bundle closure with {} outstanding complete-view records", control.references.complete_views);
    }
    if ((m_filesystem == nullptr) || !m_filesystem->close_bundle(control.binding)) m_failed = true;
    (void)m_assets.erase(control.original);
}

void CAssetService::finalise_bundles() noexcept
{
    for (std::int32_t slot = m_bundles.first_live(); slot >= 0;)
    {
        const std::int32_t next = m_bundles.next_live(slot);
        SBundleControl& control = *m_bundles.get_object(slot);
        if (!control.references.open && (control.references.records == 0u) && (control.references.operations == 0u))
        {
            if ((m_filesystem == nullptr) || !m_filesystem->remove_bundle(control.binding))
            {
                m_failed = true;
                return;
            }
            const BundleDisposedNotification result{ control.original };
            threading::CThreadPackage* const recipient = control.owner;
            (void)m_bundles.erase(slot); //  Storage and the root are gone before delivery.
            if ((recipient != nullptr) && bundle_recipient_available(*recipient))
            {
                threading::CErasedPodMsg message;
                message.assign_payload(result);
                message.set_async_slot(-1);

                //  The recipient may exit between the state check and posting.
                if (!recipient->post(message) && bundle_recipient_available(*recipient))
                {
                    m_failed = true;
                    MV_CRITICAL_EVENT("Host: Bundle final disposal notification delivery failed");
                }
            }
        }
        slot = next;
    }
}

bool CAssetService::configure_workers(threading::CThreadPackage& file_io, threading::CThreadPackage* const conditioning) noexcept
{
    if (!is_idle())
    {
        return false;
    }
    m_file_io = &file_io;
    m_conditioning = (conditioning != nullptr) ? conditioning : &file_io;
    return true;
}

void CAssetService::deallocate() noexcept
{   //  Workers and clients must have stopped before their borrowed storage dies.
    if (m_filesystem != nullptr)
    {
        (void)m_filesystem->clear_cache();
    }
    for (std::int32_t slot = m_operations.first_live(); slot >= 0; slot = m_operations.next_live(slot))
    {
        const auto binding = m_operations.get_object(slot)->reserved_binding;
        if ((binding != 0u) && (m_filesystem != nullptr)) (void)m_filesystem->remove_bundle(binding);
    }
    m_operations.deallocate();
    m_assets.deallocate();
    for (std::int32_t slot = m_bundles.first_live(); slot >= 0; slot = m_bundles.next_live(slot))
    {
        SBundleControl& control = *m_bundles.get_object(slot);
        MV_ASSERT(control.references.operations == 0u);
        if (m_filesystem != nullptr)
        {
            if (control.references.open) (void)m_filesystem->close_bundle(control.binding);
            (void)m_filesystem->remove_bundle(control.binding);
        }
    }
    m_bundles.deallocate(); //  Full shutdown retires notifications after users stop.
    m_file_io = nullptr;
    m_conditioning = nullptr;
    m_rendering = nullptr;
}

void CAssetService::fail_pending() noexcept
{
    while (!m_operations.is_empty())
    {
        finish_operation(m_operations.first_live(), EAssetStatus::delivery_failed);
    }
}

template<typename TOwner>
void CAssetService::describe_views(TOwner& source, AssetResult& result) noexcept
{
    if (BorrowedBundleAsset* const borrowed = source.template payload<BorrowedBundleAsset>())
    {
        result = borrowed->view;
    }
    else if (LoadedFile* const raw = source.template payload<LoadedFile>())
    {
        result.set_raw_view(memory::CMemoryConstView{ raw->buffer.data(), raw->buffer.size(), 1u, raw->buffer.align() });
    }
    else if (DecodedTga* const image = source.template payload<DecodedTga>())
    {
        result.set_image_view(&image->view);
    }
    else if (BakedDocumentAsset* const baked = source.template payload<BakedDocumentAsset>())
    {
        const CByteConstView bytes = baked->block.bytes();
        result.set_baked_view(BakedAssetView{
            memory::CMemoryConstView{ bytes.data(), bytes.size(), 1u, bytes.align() }, baked->block.document() });
    }
    else if (LiveDocumentAsset* const live = source.template payload<LiveDocumentAsset>())
    {
        result.set_live_view(&live->document);
    }
    else if (TextureEncodeResult* const encoded = source.template payload<TextureEncodeResult>())
    {
        result.set_encoded_texture_view(&encoded->texture);
    }
    else if (TextureDecodeResult* const decoded = source.template payload<TextureDecodeResult>())
    {
        result.set_decoded_texture_view(&decoded->texture);
    }
}

template<typename TResult>
void CAssetService::reply(threading::CThreadPackage& client, const std::int32_t slot, const TResult& result) noexcept
{
    threading::CErasedPodMsg message;
    message.set_async_slot(slot);
    message.assign_payload(result);
    if (!client.post(message))
    {   //  A broken completion transport is terminal, never a silent success.
        m_failed = true;
        MV_CRITICAL_EVENT("Host: Asset completion delivery failed for slot {}", slot);
    }
}

void CAssetService::finish_operation(const std::int32_t slot, const EAssetStatus status) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    if (operation.reserved_binding != 0u)
    {
        if ((m_filesystem == nullptr) || !m_filesystem->remove_bundle(operation.reserved_binding)) m_failed = true;
        operation.reserved_binding = 0u;
    }
    if (operation.phase == EPhase::disposing)
    {
        MV_DETAIL("Host asset disposal completed at client slot {}", operation.client_slot);
        reply((*operation.client), operation.client_slot, AssetDisposeResult{ operation.retained_asset, status });
        (void)m_operations.erase(slot);
        return;
    }

    if ((status == EAssetStatus::success) && (m_filesystem != nullptr))
    {
        bool updated = true;
        if (operation.load_requested)
        {
            updated = m_filesystem->loaded(operation.logical_file, operation.retained_asset.query_value(),
                static_cast<std::uint64_t>(operation.load_format), operation.working_views.document_findings,
                operation.admission_serial, operation.source_binding);
        }
        else if (operation.save_requested && (operation.phase == EPhase::saving))
        {
            //  Complete bundles and live sources do not represent ordinary
            //  raw/baked loads of the saved file. Keep the association only
            //  where the retained representation can satisfy that load.
            const CAssetId cached = ((operation.working_views.kind != EAssetKind::live) &&
                (operation.working_views.kind != EAssetKind::bundle)) ? operation.retained_asset : CAssetId{};
            updated = m_filesystem->written(operation.logical_file, operation.file, cached.query_value(),
                static_cast<std::uint64_t>(operation.save_settings.format));
        }
        if (!updated)
        {
            m_failed = true;
            MV_CRITICAL_EVENT("Host: Filesystem cache update failed");
        }
    }
    AssetResult result = operation.retained_asset ? operation.working_views : AssetResult{};
    result.asset = operation.retained_asset;
    result.status = status;
    result.document_policy = operation.working_views.document_policy;
    result.document_findings = operation.working_views.document_findings;
    reply((*operation.client), operation.client_slot, result);
    (void)m_operations.erase(slot);
}

bool CAssetService::retain_candidate(SOperation& operation) noexcept
{
    const CAssetId asset = m_assets.insert(std::move(operation.candidate_owner));
    if (!asset)
    {
        return false;
    }

    operation.retained_asset = asset;
    return true;
}

//==============================================================================
//  Retained asset disposal
//  Pending disposal closes admission, then waits only for existing borrowers.
//==============================================================================

bool CAssetService::disposal_pending(const CAssetId asset) const noexcept
{
    for (std::int32_t slot = m_operations.first_live(); slot >= 0; slot = m_operations.next_live(slot))
    {
        const SOperation& operation = *m_operations.get_object(slot);
        if ((operation.phase == EPhase::disposing) && (operation.retained_asset == asset))
        {
            return true;
        }
    }
    return false;
}

bool CAssetService::asset_in_use(const CAssetId asset) const noexcept
{
    for (std::int32_t slot = m_operations.first_live(); slot >= 0; slot = m_operations.next_live(slot))
    {
        const SOperation& operation = *m_operations.get_object(slot);
        if ((operation.phase != EPhase::disposing) &&
            ((operation.retained_asset == asset) || (operation.texture_source == asset)))
        {
            return true;
        }
    }
    return false;
}

void CAssetService::request_disposal(const AssetDisposeRequest& request, const std::int32_t slot, threading::CThreadPackage& client) noexcept
{
    MV_DETAIL("Host asset disposal request at client slot {}", slot);
    if ((m_assets.resolve(request.asset) == nullptr) || disposal_pending(request.asset))
    {
        reply(client, slot, AssetDisposeResult{ request.asset, EAssetStatus::invalid_asset });
        return;
    }

    CAssetRecord& record = *m_assets.resolve(request.asset);
    if (record.backing_role() == EAssetBackingRole::original)
    {
        SBundleControl* const control = find_bundle(record.backing());
        if ((control == nullptr) || (control->owner != &client))
        {
            reply(client, slot, AssetDisposeResult{ request.asset, EAssetStatus::invalid_request });
            return;
        }
        close_bundle(*control);
        return; //  Only final disposal is notified; no correlated success.
    }

    const std::int32_t operation_slot = m_operations.emplace();
    if (operation_slot < 0)
    {
        reply(client, slot, AssetDisposeResult{ request.asset, EAssetStatus::allocation_failed });
        return;
    }

    SOperation& operation = *m_operations.get_object(operation_slot);
    operation.client = &client;
    operation.client_slot = slot;
    operation.phase = EPhase::disposing;
    operation.retained_asset = request.asset;
    if (asset_in_use(request.asset))
    {
        MV_DETAIL("Host asset disposal deferred for accepted borrowers at client slot {}", slot);
    }
}

void CAssetService::complete_disposals() noexcept
{
    //  Save the next slot before erasing this operation. Disposal replies never
    //  publish the views that ordinary load/save completions may carry.
    for (std::int32_t slot = m_operations.first_live(); slot >= 0;)
    {
        const std::int32_t next = m_operations.next_live(slot);
        const SOperation& operation = *m_operations.get_object(slot);
        if ((operation.phase == EPhase::disposing) && !asset_in_use(operation.retained_asset))
        {
            if ((m_filesystem != nullptr) && !m_filesystem->forget_asset(operation.retained_asset.query_value()))
            {
                m_failed = true;
            }
            const bool erased = m_assets.erase(operation.retained_asset);
            finish_operation(slot, (erased ? EAssetStatus::success : EAssetStatus::invalid_asset));
        }
        slot = next;
    }
    finalise_bundles();
}

void CAssetService::dispose_dependencies(const mount_point_ids::id_type mount) noexcept
{
    if (m_assets.has_dependency(mount))
    {
        //  A module should release its assets before stopping. Log the violated
        //  contract, then destroy remaining owners while their DLL is still bound.
        (void)debug_system::submit_event<debug_system::EEventLevel::assert, debug_system::EEventType::condition>(
            MV_INTERNAL_USAGE_POINT, "Host: disposing retained assets dependent on module mount {}", mount.raw_value());
        m_assets.erase_dependencies(mount);
        if ((m_filesystem != nullptr) && !m_filesystem->clear_cache())
        {
            m_failed = true;
        }
    }
    finalise_bundles();
}

//==============================================================================
//  Asset admission and worker submission
//==============================================================================

template<typename TRequest, typename TAsset>
bool CAssetService::handle_transfer_if_type_matches(SOperation& operation) noexcept
{
    TRequest* const request = operation.request_owner.payload<TRequest>();
    if (request == nullptr)
    {
        return false;
    }
    operation.candidate_owner = CErasedOwner::create<TAsset>();
    //  Moving backing storage into a Host carrier does not remove explicit code/data dependencies.
    for (std::uint32_t index = 0u; index < mount_point_ids::k_count; ++index)
    {
        const auto mount = mount_point_ids::ops::make_id(mount_point_ids::ops::make_index(index));
        if (operation.request_owner.has_hazard(mount))
        {
            operation.candidate_owner.add_hazard(mount);
        }
    }
    TAsset* const source = operation.candidate_owner.payload<TAsset>();
    if (source == nullptr)
    {
        operation.working_views.status = EAssetStatus::allocation_failed;
        return true;
    }
    if constexpr (std::is_same_v<TAsset, BakedDocumentAsset>)
    {
        source->block = std::move(request->storage.value);
    }
    else if constexpr (std::is_same_v<TAsset, LiveDocumentAsset>)
    {
        source->document = std::move(request->storage.value);
    }
    else
    {
        source->buffer = std::move(request->storage.value);
        if constexpr (std::is_same_v<TAsset, DecodedTga>)
        {
            source->desc = request->description;
            source->storage_bottom_up = request->storage_bottom_up;

            //  Prepare once, before retention or publication. Moving the erased owner
            //  never relocates this object, and later descriptions do not modify it.
            if (!source->view.set(source->buffer.view(), source->desc, source->storage_bottom_up))
            {
                operation.working_views.status = EAssetStatus::invalid_request;
                return true;
            }
        }
    }
    operation.file = request->storage.file.cstring();
    operation.save_settings = request->settings;
    operation.retention = request->retention;
    operation.save_requested = request->save;
    describe_views(operation.candidate_owner, operation.working_views);
    const bool live = (operation.working_views.live_document() != nullptr);
    const bool ready = live ? operation.working_views.live_document()->is_ready() :
        (operation.working_views.byte_view().is_ready() ||
            ((operation.working_views.kind == EAssetKind::image) && operation.working_views.views.image->is_ready()));
    const bool valid_retention = (operation.retention == EAssetRetention::source) ||
        (operation.retention == EAssetRetention::discard) ||
        (live && (operation.retention == EAssetRetention::baked));
    operation.working_views.status = (ready && valid_retention &&
        (operation.save_requested || (operation.retention != EAssetRetention::discard)) &&
        (!operation.save_requested || ((operation.file != nullptr) && (*operation.file != '\0')))) ?
        EAssetStatus::success : EAssetStatus::invalid_request;
    return true;
}

void CAssetService::request(threading::CErasedOwnerMsg& message, threading::CThreadPackage& client) noexcept
{
    const std::int32_t slot = m_operations.emplace();
    if (slot < 0)
    {
        AssetResult result;
        result.status = EAssetStatus::allocation_failed;
        reply(client, message.query_async_slot(), result);
        return;
    }
    SOperation& operation = *m_operations.get_object(slot);
    operation.client = &client;
    operation.client_slot = message.query_async_slot();
    operation.request_owner = message.take_owner();
    if (operation.request_owner.query_type_id() != message.query_message_type_id())
    {
        finish_operation(slot, EAssetStatus::invalid_request);
        return;
    }

    const BundleBakeRequest* const bake = operation.request_owner.payload<BundleBakeRequest>();
    const BundleLoadRequest* const bundle_load = operation.request_owner.payload<BundleLoadRequest>();
    if ((bake != nullptr) || (bundle_load != nullptr))
    {
        if (m_file_io == nullptr)
        {
            finish_operation(slot, EAssetStatus::busy);
            return;
        }
        const auto status = reserve_bundle(((bake != nullptr) ? bake->paths.root.view() : bundle_load->paths.root.view()),
            operation.reserved_binding);
        if (status != EAssetStatus::success)
        {
            finish_operation(slot, status);
            return;
        }
        threading::CErasedPodMsg outbound;
        outbound.set_async_slot(slot);
        if (bake != nullptr)
        {
            const char* const directory = (bake->paths.subdirectory.length() == 0u) ? "" : bake->paths.subdirectory.cstring();
            if (!m_filesystem->capture_bake(bake->paths.source_root.cstring(), directory, operation.bake_input))
            {
                finish_operation(slot, EAssetStatus::invalid_request);
                return;
            }
            operation.phase = EPhase::bundle_building;
            outbound.assign_payload(BundleBuildWork{ &operation.bake_input });
        }
        else
        {
            //  A load-and-bind source is a physical file; nested bundles are outside this format's scope.
            if (!m_filesystem->resolve(bundle_load->paths.file.cstring(), false, operation.physical_file))
            {
                finish_operation(slot, EAssetStatus::read_failed);
                return;
            }
            operation.phase = EPhase::bundle_loading;
            outbound.assign_payload(FileLoadRequest{ operation.physical_file.cstring(), bundle::k_alignment });
        }
        if (!m_file_io->post(outbound)) finish_operation(slot, EAssetStatus::delivery_failed);
        return;
    }

    if (TextureEncodeRequest* const encode = operation.request_owner.payload<TextureEncodeRequest>())
    {
        if (m_rendering == nullptr)
        {
            finish_operation(slot, EAssetStatus::busy);
            return;
        }
        operation.phase = EPhase::texture_encoding;
        threading::CErasedPodMsg outbound;
        outbound.set_async_slot(slot);
        outbound.assign_payload(TextureEncodeWork{ encode });
        if (!m_rendering->post(outbound))
        {
            finish_operation(slot, EAssetStatus::delivery_failed);
        }
        return;
    }
    if (const TextureDecodeRequest* const decode = operation.request_owner.payload<TextureDecodeRequest>())
    {
        if (m_rendering == nullptr)
        {
            finish_operation(slot, EAssetStatus::busy);
            return;
        }
        CAssetRecord* const source = disposal_pending(decode->source) ? nullptr : m_assets.resolve(decode->source);
        if ((source == nullptr) || !protect_asset(*source, operation.backing, true))
        {
            finish_operation(slot, EAssetStatus::invalid_asset);
            return;
        }
        AssetResult source_view;
        describe_views((*source), source_view);
        const CByteConstView bytes = source_view.byte_view();
        if (!bytes.is_ready())
        {
            finish_operation(slot, EAssetStatus::invalid_request);
            return;
        }
        operation.texture_source = decode->source;
        operation.texture_source_view = bytes;
        operation.phase = EPhase::texture_decoding;
        threading::CErasedPodMsg outbound;
        outbound.set_async_slot(slot);
        outbound.assign_payload(TextureDecodeWork{ decode, (&operation.texture_source_view) });
        if (!m_rendering->post(outbound))
        {
            finish_operation(slot, EAssetStatus::delivery_failed);
        }
        return;
    }

    if (const AssetLoadRequest* const load = operation.request_owner.payload<AssetLoadRequest>())
    {
        operation.load_requested = true;
        operation.file = load->file.cstring();
        operation.load_format = load->format;
        if ((load->file.length() == 0u) || (load->format > EAssetFileFormat::tga))
        {
            finish_operation(slot, EAssetStatus::invalid_request);
            return;
        }
        operation.logical_file = operation.file;
        filesystem_image::SResolvedSource resolved;
        if ((m_filesystem == nullptr) || !m_filesystem->resolve(operation.logical_file, false, resolved))
        {
            finish_operation(slot, EAssetStatus::read_failed);
            return;
        }
        operation.source_binding = resolved.binding;
        operation.physical_file = std::move(resolved.physical_path);
        if ((resolved.binding != 0u) && (load->alignment > bundle::k_alignment))
        {
            finish_operation(slot, EAssetStatus::invalid_request);
            return;
        }
        if (resolved.binding == 0u) operation.file = operation.physical_file.cstring();
        operation.admission_serial = m_filesystem->write_serial();
        const CAssetId cached = m_assets.find_identity(m_filesystem->cached_asset(operation.logical_file, static_cast<std::uint64_t>(load->format)));
        if (CAssetRecord* const record = disposal_pending(cached) ? nullptr : m_assets.resolve(cached))
        {
            AssetResult views;
            describe_views((*record), views);
            const std::uint32_t findings = m_filesystem->cached_findings(operation.logical_file);
            const bool json = (load->format == EAssetFileFormat::json);
            const bool adequate_alignment = !views.byte_view().is_ready() || (views.byte_view().align() >= std::max(load->alignment, std::size_t{ 16u }));
            if (adequate_alignment && (!json || (findings != UINT32_MAX)))
            {
                if (json)
                {
                    const CDocumentPolicyResult policy = document_policy::evaluate(findings, load->policy);
                    operation.working_views.document_policy = policy.status;
                    operation.working_views.document_findings = findings;
                    if (!policy.accepted())
                    {
                        finish_operation(slot, EAssetStatus::policy_rejected);
                        return;
                    }
                    views.document_policy = policy.status;
                    views.document_findings = findings;
                }
                operation.retained_asset = cached;
                operation.working_views = views;
                finish_operation(slot, EAssetStatus::success);
                return;
            }
        }
        if (resolved.binding != 0u)
        {
            const auto status = begin_bundle_file(resolved, memory::condition_alignment(std::max(load->alignment, std::size_t{ 16u })), operation.bundle_input);
            if (status != EAssetStatus::success)
            {
                finish_operation(slot, status);
                return;
            }
            if ((load->format == EAssetFileFormat::raw) || (load->format == EAssetFileFormat::baked))
            {
                operation.working_views = retain_bundle_file(operation.bundle_input, load->format);
                operation.retained_asset = operation.working_views.asset;
                const auto result = operation.working_views.status;
                finish_operation(slot, (result == EAssetStatus::invalid_request) ? EAssetStatus::conditioning_failed : result);
            }
            else begin_conditioning_load(slot, operation.bundle_input.bytes());
            return;
        }
        FileLoadRequest request{ operation.file, ((load->format == EAssetFileFormat::baked) ? std::max(load->alignment, std::size_t{ 32u }) : load->alignment) };
        threading::CErasedPodMsg outbound;
        outbound.set_async_slot(slot);
        outbound.assign_payload(request);
        if ((m_file_io == nullptr) || !m_file_io->post(outbound))
        {
            finish_operation(slot, EAssetStatus::delivery_failed);
        }
        return;
    }
    if (const AssetSaveRequest* const save = operation.request_owner.payload<AssetSaveRequest>())
    {
        operation.file = save->file.cstring();
        operation.save_settings = save->settings;
        operation.save_requested = true;
        CAssetRecord* const source = disposal_pending(save->source) ? nullptr : m_assets.resolve(save->source);
        if ((source == nullptr) || (save->file.length() == 0u))
        {
            finish_operation(slot, ((source == nullptr) ? EAssetStatus::invalid_asset : EAssetStatus::invalid_request));
            return;
        }
        if (!protect_asset(*source, operation.backing, false))
        {
            finish_operation(slot, EAssetStatus::invalid_asset);
            return;
        }
        operation.retained_asset = save->source;
        describe_views((*source), operation.working_views);
    }
    else
    {
        const bool recognised =
            handle_transfer_if_type_matches<RawAssetTransfer, LoadedFile>(operation) ||
            handle_transfer_if_type_matches<ImageAssetTransfer, DecodedTga>(operation) ||
            handle_transfer_if_type_matches<BakedAssetTransfer, BakedDocumentAsset>(operation) ||
            handle_transfer_if_type_matches<LiveAssetTransfer, LiveDocumentAsset>(operation);
        if (!recognised || (operation.working_views.status != EAssetStatus::success))
        {
            finish_operation(slot, operation.working_views.status);
            return;
        }
        if ((operation.retention == EAssetRetention::source) && !retain_candidate(operation))
        {
            finish_operation(slot, EAssetStatus::allocation_failed);
            return;
        }
    }
    begin_save_or_bake(slot);
}

void CAssetService::begin_save_or_bake(const std::int32_t slot) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    const bool live = (operation.working_views.live_document() != nullptr);
    if (!operation.save_requested && (!live || (operation.retention != EAssetRetention::baked)))
    {
        finish_operation(slot, EAssetStatus::success);
        return;
    }
    threading::CErasedPodMsg outbound;
    outbound.set_async_slot(slot);
    if ((operation.working_views.kind == EAssetKind::image) && (operation.save_settings.format == EAssetFileFormat::tga))
    {
        operation.phase = EPhase::encoding;
        outbound.assign_payload(TgaEncodeRequest{ operation.working_views.views.image->buffer_view(), operation.save_settings.image });
    }
    else if ((live || operation.working_views.document_view().is_ready()) &&
        (!operation.save_requested || (operation.save_settings.format == EAssetFileFormat::baked) ||
            (operation.save_settings.format == EAssetFileFormat::json)))
    {
        if (!live && (operation.save_settings.format == EAssetFileFormat::baked))
        {
            begin_file_save(slot, operation.working_views.byte_view());
            return;
        }
        operation.phase = EPhase::conditioning;
        DocumentConditionRequest request;
        if (live)
        {
            request.set_live_source(operation.working_views.live_document());
        }
        else
        {
            request.set_baked_source(operation.working_views.document_view());
        }
        request.write_json = operation.save_requested && (operation.save_settings.format == EAssetFileFormat::json);
        request.options = &operation.save_settings.document;
        outbound.assign_payload(request);
    }
    else if (((operation.working_views.kind == EAssetKind::raw) || (operation.working_views.kind == EAssetKind::bundle) ||
        (operation.working_views.kind == EAssetKind::encoded_texture)) &&
        (operation.save_settings.format == EAssetFileFormat::raw))
    {
        begin_file_save(slot, operation.working_views.byte_view());
        return;
    }
    else
    {
        finish_operation(slot, EAssetStatus::invalid_request);
        return;
    }
    if (!post_conditioning(outbound))
    {
        finish_operation(slot, EAssetStatus::delivery_failed);
    }
}

void CAssetService::begin_file_save(const std::int32_t slot, const CByteConstView& bytes) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    operation.logical_file = operation.file;
    if ((m_filesystem == nullptr) || !m_filesystem->resolve(operation.logical_file, true, operation.physical_file))
    {
        finish_operation(slot, EAssetStatus::write_failed);
        return;
    }
    operation.file = operation.physical_file.cstring();
    operation.phase = EPhase::saving;
    threading::CErasedPodMsg outbound;
    outbound.set_async_slot(slot);
    outbound.assign_payload(FileSaveRequest{ operation.file, bytes });
    if ((m_file_io == nullptr) || !m_file_io->post(outbound))
    {
        finish_operation(slot, EAssetStatus::delivery_failed);
    }
}

//==============================================================================
//  Worker completion dispatch and operation continuation
//==============================================================================

bool CAssetService::post_conditioning(const threading::CErasedPodMsg& message) noexcept
{
    return (m_conditioning != nullptr) && m_conditioning->post(message);
}

void CAssetService::complete(const threading::CErasedPodMsg& message, threading::CThreadPackage& worker) noexcept
{
    const std::int32_t slot = message.query_async_slot();
    const SOperation* const operation = m_operations.get_object(slot);
    FileSaveResult result{};
    if ((operation != nullptr) && (operation->phase == EPhase::saving) &&
        (&worker == m_file_io) && message.copy_payload_to(result))
    {
        finish_operation(slot, (result.success ? EAssetStatus::success : EAssetStatus::write_failed));
    }
    else
    {
        m_failed = true;
        MV_CRITICAL_EVENT("Host: Unexpected asset worker completion at slot {}", slot);
    }
}

void CAssetService::complete(threading::CErasedOwnerMsg& message, threading::CThreadPackage& worker) noexcept
{
    const std::int32_t slot = message.query_async_slot();
    SOperation* const pending = m_operations.get_object(slot);
    if (pending == nullptr)
    {
        m_failed = true;
        MV_CRITICAL_EVENT("Host: Unknown asset worker slot {}", slot);
        return;
    }
    SOperation& operation = *pending;
    const type_id identity = message.query_message_type_id();
    switch (operation.phase)
    {
        case EPhase::bundle_loading:
        case EPhase::bundle_building:
        {
            const type_id expected = (operation.phase == EPhase::bundle_loading) ?
                k_type_id_v<FileLoadResult> : k_type_id_v<BundleBuildResult>;
            if ((identity == expected) && (&worker == m_file_io))
            {
                operation.worker_result_owner = message.take_owner();
                complete_bundle(slot);
                return;
            }
            break;
        }
        case EPhase::loading:
        {
            if ((identity == k_type_id_v<FileLoadResult>) && (&worker == m_file_io))
            {
                operation.worker_result_owner = message.take_owner();
                complete_file_load(slot);
                return;
            }
            break;
        }
        case EPhase::decoding:
        {
            if ((identity == k_type_id_v<TgaDecodeResult>) && (&worker == m_conditioning))
            {
                operation.worker_result_owner = message.take_owner();
                complete_image_decode(slot);
                return;
            }
            break;
        }
        case EPhase::encoding:
        {
            if ((identity == k_type_id_v<TgaEncodeResult>) && (&worker == m_conditioning))
            {
                operation.worker_result_owner = message.take_owner();
                complete_image_encode(slot);
                return;
            }
            break;
        }
        case EPhase::texture_encoding:
        {
            if ((identity == k_type_id_v<TextureEncodeResult>) && (&worker == m_rendering))
            {
                operation.worker_result_owner = message.take_owner();
                complete_texture_encode(slot);
                return;
            }
            break;
        }
        case EPhase::texture_decoding:
        {
            if ((identity == k_type_id_v<TextureDecodeResult>) && (&worker == m_rendering))
            {
                operation.worker_result_owner = message.take_owner();
                complete_texture_decode(slot);
                return;
            }
            break;
        }
        case EPhase::conditioning:
        {
            if ((identity == k_type_id_v<DocumentConditionResult>) && (&worker == m_conditioning))
            {
                operation.worker_result_owner = message.take_owner();
                complete_document_conditioning(slot);
                return;
            }
            break;
        }
        default:
        {
            break;
        }
    }
    m_failed = true;
    MV_CRITICAL_EVENT("Host: Asset completion does not match the active phase at slot {}", slot);
}

void CAssetService::complete_file_load(const std::int32_t slot) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    LoadedFile* const loaded = operation.worker_result_owner.payload<LoadedFile>();
    if ((loaded == nullptr) || !loaded->buffer.is_ready())
    {
        finish_operation(slot, EAssetStatus::read_failed);
        return;
    }
    if (operation.load_format == EAssetFileFormat::raw)
    {
        operation.candidate_owner = std::move(operation.worker_result_owner);
    }
    else if (operation.load_format == EAssetFileFormat::baked)
    {
        operation.candidate_owner = CErasedOwner::create<BakedDocumentAsset>();
        BakedDocumentAsset* const source = operation.candidate_owner.payload<BakedDocumentAsset>();
        if ((source == nullptr) || !source->block.adopt(std::move(loaded->buffer)))
        {
            finish_operation(slot, ((source == nullptr) ? EAssetStatus::allocation_failed : EAssetStatus::conditioning_failed));
            return;
        }
    }
    else
    {
        const CByteConstView bytes = loaded->buffer.const_view();
        operation.conditioning_input_owner = std::move(operation.worker_result_owner);
        begin_conditioning_load(slot, bytes);
        return;
    }
    describe_views(operation.candidate_owner, operation.working_views);
    finish_operation(slot, (retain_candidate(operation) ? EAssetStatus::success : EAssetStatus::allocation_failed));
    return;
}

void CAssetService::begin_conditioning_load(const std::int32_t slot, const CByteConstView bytes) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    const AssetLoadRequest& load = *operation.request_owner.payload<AssetLoadRequest>();
    threading::CErasedPodMsg outbound;
    outbound.set_async_slot(slot);
    if (operation.load_format == EAssetFileFormat::tga)
    {
        operation.phase = EPhase::decoding;
        outbound.assign_payload(TgaDecodeRequest{ bytes, load.decode_top_down });
    }
    else
    {
        operation.phase = EPhase::conditioning;
        DocumentConditionRequest request;
        request.set_text_source(bytes);
        request.policy = load.policy;
        outbound.assign_payload(request);
    }
    if (!post_conditioning(outbound)) finish_operation(slot, EAssetStatus::delivery_failed);
}

void CAssetService::complete_bundle(const std::int32_t slot) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    CBakedDocumentBlock loaded_block;
    CBakedDocumentBlock* block = &loaded_block;
    if (operation.phase == EPhase::bundle_loading)
    {
        LoadedFile* const loaded = operation.worker_result_owner.payload<LoadedFile>();
        if ((loaded == nullptr) || !loaded->buffer.is_ready())
        {
            finish_operation(slot, EAssetStatus::read_failed);
            return;
        }
        if (!loaded_block.adopt_buffer(std::move(loaded->buffer)))
        {
            finish_operation(slot, EAssetStatus::conditioning_failed);
            return;
        }
    }
    else
    {
        BundleBuildResult* const built = operation.worker_result_owner.payload<BundleBuildResult>();
        if (built == nullptr)
        {
            finish_operation(slot, EAssetStatus::allocation_failed);
            return;
        }
        if (!built->result.succeeded())
        {
            EAssetStatus status = EAssetStatus::conditioning_failed;
            switch (built->result.status)
            {
                case bundle::EBuildStatus::allocation_failed: status = EAssetStatus::allocation_failed; break;
                case bundle::EBuildStatus::size_query_failed:
                case bundle::EBuildStatus::read_failed: status = EAssetStatus::read_failed; break;
                case bundle::EBuildStatus::invalid_input:
                case bundle::EBuildStatus::no_content:
                case bundle::EBuildStatus::too_large: status = EAssetStatus::invalid_request; break;
                default: break;
            }
            finish_operation(slot, status);
            return;
        }
        block = &built->block;
    }

    //  Publication takes responsibility for cancelling this reservation on failure.
    const auto binding = std::exchange(operation.reserved_binding, 0u);
    operation.working_views = publish_bundle(*block, binding, *operation.client);
    if (operation.working_views.status == EAssetStatus::invalid_request)
        operation.working_views.status = EAssetStatus::conditioning_failed;
    operation.retained_asset = operation.working_views.asset;
    finish_operation(slot, operation.working_views.status);
}

void CAssetService::complete_image_decode(const std::int32_t slot) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    DecodedTga* const decoded = operation.worker_result_owner.payload<DecodedTga>();
    if ((decoded == nullptr) || !decoded->buffer.is_ready())
    {
        finish_operation(slot, EAssetStatus::conditioning_failed);
        return;
    }

    //  Decoder output has reached its stable Host allocation. Prepare the
    //  published metadata here, before clients can borrow its address.
    if (!decoded->view.set(decoded->buffer.view(), decoded->desc, decoded->storage_bottom_up))
    {
        finish_operation(slot, EAssetStatus::conditioning_failed);
        return;
    }
    operation.candidate_owner = std::move(operation.worker_result_owner);
    describe_views(operation.candidate_owner, operation.working_views);
    finish_operation(slot, (retain_candidate(operation) ? EAssetStatus::success : EAssetStatus::allocation_failed));
    return;
}

void CAssetService::complete_image_encode(const std::int32_t slot) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    const EncodedTga* const encoded = operation.worker_result_owner.payload<EncodedTga>();
    if ((encoded == nullptr) || !encoded->buffer.is_ready())
    {
        finish_operation(slot, EAssetStatus::conditioning_failed);
        return;
    }
    begin_file_save(slot, encoded->buffer.const_view());
    return;
}

void CAssetService::complete_texture_encode(const std::int32_t slot) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    TextureEncodeResult* const result = operation.worker_result_owner.payload<TextureEncodeResult>();
    if (result == nullptr)
    {
        finish_operation(slot, EAssetStatus::allocation_failed);
        return;
    }
    if (result->status != ETextureStatus::success)
    {
        finish_operation(slot, texture_asset_status(result->status));
        return;
    }
    if (!result->texture.view().is_ready())
    {
        finish_operation(slot, EAssetStatus::conditioning_failed);
        return;
    }
    operation.candidate_owner = std::move(operation.worker_result_owner);
    describe_views(operation.candidate_owner, operation.working_views);
    finish_operation(slot, (retain_candidate(operation) ? EAssetStatus::success : EAssetStatus::allocation_failed));
}

void CAssetService::complete_texture_decode(const std::int32_t slot) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    TextureDecodeResult* const result = operation.worker_result_owner.payload<TextureDecodeResult>();
    if (result == nullptr)
    {
        finish_operation(slot, EAssetStatus::allocation_failed);
        return;
    }
    if (result->status != ETextureStatus::success)
    {
        finish_operation(slot, texture_asset_status(result->status));
        return;
    }
    if (!result->texture.view().is_ready())
    {
        finish_operation(slot, EAssetStatus::conditioning_failed);
        return;
    }
    operation.candidate_owner = std::move(operation.worker_result_owner);
    describe_views(operation.candidate_owner, operation.working_views);
    finish_operation(slot, (retain_candidate(operation) ? EAssetStatus::success : EAssetStatus::allocation_failed));
}

void CAssetService::complete_document_conditioning(const std::int32_t slot) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    DocumentConditionResult* const conditioned = operation.worker_result_owner.payload<DocumentConditionResult>();
    if (conditioned == nullptr)
    {
        finish_operation(slot, EAssetStatus::allocation_failed);
        return;
    }
    operation.working_views.document_policy = conditioned->policy;
    operation.working_views.document_findings = conditioned->findings;

    //  Preserve a requested baked snapshot even when later JSON writing fails.
    if ((operation.load_requested || (operation.retention == EAssetRetention::baked)) && conditioned->storage.baked.is_ready())
    {
        CErasedOwner owner = CErasedOwner::create<BakedDocumentAsset>();
        BakedDocumentAsset* const baked = owner.payload<BakedDocumentAsset>();
        if (baked == nullptr)
        {
            finish_operation(slot, EAssetStatus::allocation_failed);
            return;
        }
        baked->block = std::move(conditioned->storage.baked);
        operation.candidate_owner = std::move(owner);
        describe_views(operation.candidate_owner, operation.working_views);
        if (!retain_candidate(operation))
        {
            finish_operation(slot, EAssetStatus::allocation_failed);
            return;
        }
    }
    if ((conditioned->status != EAssetStatus::success) || !operation.save_requested)
    {
        finish_operation(slot, conditioned->status);
        return;
    }
    if (operation.save_settings.format == EAssetFileFormat::json)
    {
        begin_file_save(slot, conditioned->storage.text.const_view());
    }
    else
    {   //  Baked retention moved the block into the repository. Otherwise the
        //  worker reply still owns it. Both owners outlive the file save.
        const CByteConstView binary = conditioned->storage.baked.is_ready() ?
            conditioned->storage.baked.bytes() : operation.working_views.byte_view();
        begin_file_save(slot, binary);
    }
    return;
}

}   //  namespace host
