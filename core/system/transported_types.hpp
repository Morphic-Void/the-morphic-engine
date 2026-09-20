
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    transported_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Jul 26
//
//  Built-in system payload declarations and C++ type registrations.

#pragma once

#ifndef TRANSPORTED_TYPES_HPP_INCLUDED
#define TRANSPORTED_TYPES_HPP_INCLUDED

#include <cstddef>      //  std::size_t

#include "assets/asset_repository.hpp"
#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/live_document.hpp"
#include "data_model/document_findings.hpp"
#include "data_model/document_writer.hpp"
#include "image/codec/tga.hpp"
#include "image/image_view.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/system_type_registration.hpp"

struct UnrecognisedMsg { system_type_id msg_id; };

struct FileLoadRequest { const char* file; std::size_t alignment{ 16u }; };
struct FileSaveRequest { const char* file; CByteConstView view; };

//  Active Host-to-conditioning-worker codec requests.
struct TgaEncodeRequest
{
    CByteRectConstView view;
    image::codec::tga::EncodeOptions options;
};

struct TgaDecodeRequest { CByteConstView view; bool decode_top_down; };

struct FileLoadResult {};
struct FileSaveResult { bool success; };

//  Active conditioning-worker completion identities.
struct TgaEncodeResult {};
struct TgaDecodeResult {};

struct LoadedFile { CByteBuffer buffer; };
struct EncodedTga { CByteBuffer buffer; };
struct DecodedTga
{
    CByteRectBuffer buffer;
    image::codec::tga::decoded_image_desc desc;
    bool storage_bottom_up{ false };

    //  Initialise after the buffer arrives at the Host; never change after publication.
    image::CImageView view;
};
struct BakedDocumentAsset { CBakedDocumentBlock block; };
struct LiveDocumentAsset { CLiveDocument document; };

//  Asset operations keep ownership in the Host. Returned views do not extend
//  lifetime, and callers must not mutate an asset while an operation reads it.
enum class EAssetFileFormat : std::uint8_t { raw, baked, json, tga };
enum class EAssetRetention : std::uint8_t { discard, source, baked };
enum class EAssetStatus : std::uint8_t
{
    success, invalid_request, invalid_asset, allocation_failed, read_failed,
    conditioning_failed, policy_rejected, write_failed, delivery_failed
};

struct AssetSaveSettings
{
    EAssetFileFormat format{ EAssetFileFormat::baked };
    image::codec::tga::EncodeOptions image{};
    CDocumentWriteOptions document{ EDocumentWriteMode::strict_json };
};

template<typename T>
struct TAssetTransferStorage
{
    T value;
    CSimpleString file;

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(value.memory_attribution(), file.memory_attribution());
    }

    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
    {
        value.unsafe_replace_memory_context_without_accounting(expected_source, target);
        file.unsafe_replace_memory_context_without_accounting(expected_source, target);
    }
};

template<typename T>
struct TAssetTransferRequest
{
    TAssetTransferStorage<T> storage;
    AssetSaveSettings settings;

    //  Retention::baked requests conditioning even when save is false.
    EAssetRetention retention{ EAssetRetention::source };
    bool save{ false };

    //  Source storage metadata, used only by ImageAssetTransfer. Encoding uses settings.image.
    image::codec::tga::decoded_image_desc description{ image::codec::tga::decoded_image_desc::RGBA };
    bool storage_bottom_up{ false };
};

using RawAssetTransfer = TAssetTransferRequest<CByteBuffer>;
using ImageAssetTransfer = TAssetTransferRequest<CByteRectBuffer>;
using BakedAssetTransfer = TAssetTransferRequest<CBakedDocumentBlock>;
using LiveAssetTransfer = TAssetTransferRequest<CLiveDocument>;

struct AssetLoadRequest
{
    CSimpleString file;
    EAssetFileFormat format{ EAssetFileFormat::raw };
    std::size_t alignment{ 16u };
    CDocumentParseOptions policy{};
    bool decode_top_down{ true };
};

//  This envelope owns the filename, but only borrows the retained source asset.
struct AssetSaveRequest
{
    CSimpleString file;
    CAssetId source{};
    AssetSaveSettings settings;
};

enum class EAssetKind : std::uint8_t { none, raw, image, baked, live };

struct BakedAssetView
{
    memory::CMemoryConstView bytes;
    CBakedDocument document;
};

union AssetViews
{
    memory::CMemoryConstView bytes;
    BakedAssetView baked;
    const image::CImageView* image;
    CLiveDocument* live;

    AssetViews() noexcept : bytes{} {}
    explicit AssetViews(const memory::CMemoryConstView& value) noexcept : bytes{ value } {}
    explicit AssetViews(const BakedAssetView& value) noexcept : baked{ value } {}
    explicit AssetViews(const image::CImageView* const value) noexcept : image{ value } {}
    explicit AssetViews(CLiveDocument* const value) noexcept : live{ value } {}
};

struct AssetResult
{
    CAssetId asset{};
    AssetViews views;
    EAssetKind kind{ EAssetKind::none };
    EAssetStatus status{ EAssetStatus::invalid_request };
    EDocumentPolicyStatus document_policy{ EDocumentPolicyStatus::unexamined };
    std::uint32_t document_findings{ 0u };

    void set_raw_view(const memory::CMemoryConstView& value) noexcept
    {
        views = AssetViews{ value };
        kind = EAssetKind::raw;
    }

    void set_baked_view(const BakedAssetView& value) noexcept
    {
        views = AssetViews{ value };
        kind = EAssetKind::baked;
    }

    void set_image_view(const image::CImageView* const value) noexcept
    {
        views = AssetViews{ value };
        kind = EAssetKind::image;
    }

    void set_live_view(CLiveDocument* const value) noexcept
    {
        views = AssetViews{ value };
        kind = EAssetKind::live;
    }

    [[nodiscard]] CByteConstView byte_view() const noexcept
    {
        const memory::CMemoryConstView bytes = (kind == EAssetKind::raw) ? views.bytes :
            ((kind == EAssetKind::baked) ? views.baked.bytes : memory::CMemoryConstView{});
        return CByteConstView{ static_cast<const std::uint8_t*>(bytes.data()), bytes.count(), bytes.storage_alignment() };
    }

    [[nodiscard]] CBakedDocument document_view() const noexcept
    {
        return (kind == EAssetKind::baked) ? views.baked.document : CBakedDocument{};
    }

    //  Copy the published metadata; texel storage remains owned by the Host.
    [[nodiscard]] image::CImageView image_view() const noexcept
    {
        return ((kind == EAssetKind::image) && (views.image != nullptr)) ? *views.image : image::CImageView{};
    }

    [[nodiscard]] CLiveDocument* live_document() const noexcept
    {
        return (kind == EAssetKind::live) ? views.live : nullptr;
    }
};

enum class EDocumentSource : std::uint8_t { text, baked, live };

union DocumentConditionSource
{
    CByteConstView text;
    CBakedDocument baked;
    const CLiveDocument* live;

    DocumentConditionSource() noexcept : text{} {}
    explicit DocumentConditionSource(const CByteConstView& value) noexcept : text{ value } {}
    explicit DocumentConditionSource(const CBakedDocument& value) noexcept : baked{ value } {}
    explicit DocumentConditionSource(const CLiveDocument* const value) noexcept : live{ value } {}
};

//  The Host owns every object referenced here until the worker completes.
struct DocumentConditionRequest
{
    DocumentConditionSource source;
    const CDocumentWriteOptions* options{ nullptr };
    CDocumentParseOptions policy{};
    EDocumentSource kind{ EDocumentSource::text };
    bool write_json{ false };

    void set_text_source(const CByteConstView& value) noexcept
    {
        source = DocumentConditionSource{ value };
        kind = EDocumentSource::text;
    }

    void set_baked_source(const CBakedDocument& value) noexcept
    {
        source = DocumentConditionSource{ value };
        kind = EDocumentSource::baked;
    }

    void set_live_source(const CLiveDocument* const value) noexcept
    {
        source = DocumentConditionSource{ value };
        kind = EDocumentSource::live;
    }
};

struct DocumentConditionStorage
{
    CBakedDocumentBlock baked;
    CByteBuffer text;

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(baked.memory_attribution(), text.memory_attribution());
    }

    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
    {
        baked.unsafe_replace_memory_context_without_accounting(expected_source, target);
        text.unsafe_replace_memory_context_without_accounting(expected_source, target);
    }
};

struct DocumentConditionResult
{
    DocumentConditionStorage storage;
    EAssetStatus status{ EAssetStatus::conditioning_failed };
    EDocumentPolicyStatus policy{ EDocumentPolicyStatus::unexamined };
    std::uint32_t findings{ 0u };
};

MV_REGISTER_SYSTEM_TYPE(CByteBuffer, system_type_ids::byte_buffer);
MV_REGISTER_SYSTEM_TYPE(CByteRectBuffer, system_type_ids::byte_rect_buffer);
MV_REGISTER_SYSTEM_TYPE(CSimpleString, system_type_ids::simple_string);
MV_REGISTER_SYSTEM_TYPE(CStringBuffer, system_type_ids::string_buffer);
MV_REGISTER_SYSTEM_TYPE(CStableStrings, system_type_ids::stable_strings);

MV_REGISTER_SYSTEM_TYPE(UnrecognisedMsg, system_type_ids::unrecognised_msg);

MV_REGISTER_SYSTEM_TYPE(FileLoadRequest, system_type_ids::file_load_request);
MV_REGISTER_SYSTEM_TYPE(FileSaveRequest, system_type_ids::file_save_request);
MV_REGISTER_SYSTEM_TYPE(TgaEncodeRequest, system_type_ids::tga_encode_request);
MV_REGISTER_SYSTEM_TYPE(TgaDecodeRequest, system_type_ids::tga_decode_request);

MV_REGISTER_SYSTEM_TYPE(FileLoadResult, system_type_ids::file_load_result);
MV_REGISTER_SYSTEM_TYPE(FileSaveResult, system_type_ids::file_save_result);
MV_REGISTER_SYSTEM_TYPE(TgaEncodeResult, system_type_ids::tga_encode_result);
MV_REGISTER_SYSTEM_TYPE(TgaDecodeResult, system_type_ids::tga_decode_result);

MV_REGISTER_SYSTEM_TYPE(LoadedFile, system_type_ids::loaded_file);
MV_REGISTER_SYSTEM_TYPE(EncodedTga, system_type_ids::encoded_tga);
MV_REGISTER_SYSTEM_TYPE(DecodedTga, system_type_ids::decoded_tga);
MV_REGISTER_SYSTEM_TYPE(BakedDocumentAsset, system_type_ids::baked_document_asset);
MV_REGISTER_SYSTEM_TYPE(LiveDocumentAsset, system_type_ids::live_document_asset);

MV_REGISTER_SYSTEM_TYPE(RawAssetTransfer, system_type_ids::raw_asset_transfer);
MV_REGISTER_SYSTEM_TYPE(ImageAssetTransfer, system_type_ids::image_asset_transfer);
MV_REGISTER_SYSTEM_TYPE(BakedAssetTransfer, system_type_ids::baked_asset_transfer);
MV_REGISTER_SYSTEM_TYPE(LiveAssetTransfer, system_type_ids::live_asset_transfer);
MV_REGISTER_SYSTEM_TYPE(AssetLoadRequest, system_type_ids::asset_load_request);
MV_REGISTER_SYSTEM_TYPE(AssetSaveRequest, system_type_ids::asset_save_request);
MV_REGISTER_SYSTEM_TYPE(AssetResult, system_type_ids::asset_result);
MV_REGISTER_SYSTEM_TYPE(DocumentConditionRequest, system_type_ids::document_condition_request);
MV_REGISTER_SYSTEM_TYPE(DocumentConditionResult, system_type_ids::document_condition_result);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "system/system_erased_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

#endif  //  #ifndef TRANSPORTED_TYPES_HPP_INCLUDED
