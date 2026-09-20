
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_service.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Sep 26
//
//  Compose file I/O and conditioning without transferring assets to clients.

#include <algorithm>
#include <type_traits>
#include <utility>

#include "host/runtime/asset_service.hpp"
#include "debug/macros.hpp"

namespace host
{

bool CAssetService::initialise() noexcept
{
    return m_assets.initialise() && m_operations.initialise();
}

void CAssetService::deallocate() noexcept
{   //  Workers and clients must have stopped before their borrowed storage dies.
    m_operations.deallocate();
    m_assets.deallocate();
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
    if (LoadedFile* const raw = source.template payload<LoadedFile>())
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
}

void CAssetService::reply(threading::CThreadPackage& client, const std::int32_t slot, const AssetResult& result) noexcept
{
    threading::CErasedPodMsg message;
    message.set_async_slot(slot);
    message.assign_payload(result);
    if (!client.post(message))
    {
        //  A broken completion transport is terminal, never a silent success.
        m_failed = true;
        MV_CRITICAL_EVENT("Host: Asset completion delivery failed for slot {}", slot);
    }
}

void CAssetService::finish_operation(const std::int32_t slot, const EAssetStatus status) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    AssetResult result = operation.retained_asset ? operation.working_views : AssetResult{};
    result.asset = operation.retained_asset;
    result.status = status;
    result.document_policy = operation.working_views.document_policy;
    result.document_findings = operation.working_views.document_findings;
    reply(*operation.client, operation.client_slot, result);
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

template<typename TRequest, typename TAsset>
bool CAssetService::handle_transfer_if_type_matches(SOperation& operation) noexcept
{
    TRequest* const request = operation.request_owner.payload<TRequest>();
    if (request == nullptr)
    {
        return false;
    }
    operation.candidate_owner = CErasedOwner::create<TAsset>();
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
    const bool live = operation.working_views.live_document() != nullptr;
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

void CAssetService::request(threading::CErasedOwnerMsg& message, threading::CThreadPackage& client,
    threading::CThreadPackage& file_io, threading::CThreadPackage& conditioning) noexcept
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
    operation.file_io = &file_io;
    operation.conditioning = &conditioning;
    operation.client_slot = message.query_async_slot();
    operation.request_owner = message.take_owner();
    if (operation.request_owner.query_type_id() != message.query_message_type_id())
    {
        finish_operation(slot, EAssetStatus::invalid_request);
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
        FileLoadRequest request{ operation.file,
            (load->format == EAssetFileFormat::baked) ? std::max(load->alignment, std::size_t{ 32u }) : load->alignment };
        threading::CErasedPodMsg outbound;
        outbound.set_async_slot(slot);
        outbound.assign_payload(request);
        if (!file_io.post(outbound))
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
        CAssetRecord* const source = m_assets.resolve(save->source);
        if ((source == nullptr) || (save->file.length() == 0u))
        {
            finish_operation(slot, (source == nullptr) ? EAssetStatus::invalid_asset : EAssetStatus::invalid_request);
            return;
        }
        operation.retained_asset = save->source;
        describe_views(*source, operation.working_views);
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
    const bool live = operation.working_views.live_document() != nullptr;
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
    else if ((operation.working_views.kind == EAssetKind::raw) && (operation.save_settings.format == EAssetFileFormat::raw))
    {
        begin_file_save(slot, operation.working_views.byte_view());
        return;
    }
    else
    {
        finish_operation(slot, EAssetStatus::invalid_request);
        return;
    }
    if (!operation.conditioning->post(outbound))
    {
        finish_operation(slot, EAssetStatus::delivery_failed);
    }
}

void CAssetService::begin_file_save(const std::int32_t slot, const CByteConstView& bytes) noexcept
{
    SOperation& operation = *m_operations.get_object(slot);
    operation.phase = EPhase::saving;
    threading::CErasedPodMsg outbound;
    outbound.set_async_slot(slot);
    outbound.assign_payload(FileSaveRequest{ operation.file, bytes });
    if (!operation.file_io->post(outbound))
    {
        finish_operation(slot, EAssetStatus::delivery_failed);
    }
}

void CAssetService::complete(const threading::CErasedPodMsg& message) noexcept
{
    const std::int32_t slot = message.query_async_slot();
    const SOperation* const operation = m_operations.get_object(slot);
    FileSaveResult result{};
    if ((operation != nullptr) && (operation->phase == EPhase::saving) && message.copy_payload_to(result))
    {
        finish_operation(slot, result.success ? EAssetStatus::success : EAssetStatus::write_failed);
    }
    else
    {
        m_failed = true;
        MV_CRITICAL_EVENT("Host: Unexpected asset worker completion at slot {}", slot);
    }
}

void CAssetService::complete(threading::CErasedOwnerMsg& message) noexcept
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
        case EPhase::loading:
        {
            if (identity == k_type_id_v<FileLoadResult>)
            {
                operation.worker_result_owner = message.take_owner();
                complete_file_load(slot);
                return;
            }
            break;
        }
        case EPhase::decoding:
        {
            if (identity == k_type_id_v<TgaDecodeResult>)
            {
                operation.worker_result_owner = message.take_owner();
                complete_image_decode(slot);
                return;
            }
            break;
        }
        case EPhase::encoding:
        {
            if (identity == k_type_id_v<TgaEncodeResult>)
            {
                operation.worker_result_owner = message.take_owner();
                complete_image_encode(slot);
                return;
            }
            break;
        }
        case EPhase::conditioning:
        {
            if (identity == k_type_id_v<DocumentConditionResult>)
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
            finish_operation(slot, (source == nullptr) ? EAssetStatus::allocation_failed : EAssetStatus::conditioning_failed);
            return;
        }
    }
    else
    {
        const AssetLoadRequest& load = *operation.request_owner.payload<AssetLoadRequest>();
        threading::CErasedPodMsg outbound;
        outbound.set_async_slot(slot);
        if (operation.load_format == EAssetFileFormat::tga)
        {
            operation.phase = EPhase::decoding;
            outbound.assign_payload(TgaDecodeRequest{ loaded->buffer.const_view(), load.decode_top_down });
        }
        else
        {
            operation.phase = EPhase::conditioning;
            DocumentConditionRequest request;
            request.set_text_source(loaded->buffer.const_view());
            request.policy = load.policy;
            outbound.assign_payload(request);
        }
        operation.conditioning_input_owner = std::move(operation.worker_result_owner);
        if (!operation.conditioning->post(outbound))
        {
            finish_operation(slot, EAssetStatus::delivery_failed);
        }
        return;
    }
    describe_views(operation.candidate_owner, operation.working_views);
    finish_operation(slot, retain_candidate(operation) ? EAssetStatus::success : EAssetStatus::allocation_failed);
    return;
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
    finish_operation(slot, retain_candidate(operation) ? EAssetStatus::success : EAssetStatus::allocation_failed);
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
