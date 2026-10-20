
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Public asset and bundle requests, borrowed views and completions.

#pragma once

#ifndef ASSET_MESSAGES_HPP_INCLUDED
#define ASSET_MESSAGES_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "assets/asset_repository.hpp"
#include "assets/bundle.hpp"
#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/live_document.hpp"
#include "data_model/document_findings.hpp"
#include "data_model/document_writer.hpp"
#include "image/codec/tga.hpp"
#include "image/image_view.hpp"
#include "rendering/interface/texture_data.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/system_type_registration.hpp"

//==============================================================================
//  Client-to-Host asset requests and shared settings
//  Asset operations keep ownership in the Host. Returned views do not extend
//  lifetime, and callers must not mutate an asset while an operation reads it.
//==============================================================================

enum class EAssetFileFormat : std::uint8_t { raw = 0, baked, json, tga };
enum class EAssetRetention : std::uint8_t { discard = 0, source, baked };

enum class EAssetStatus : std::uint8_t
{
    success = 0,
    invalid_request,
    invalid_asset,
    allocation_failed,
    read_failed,
    conditioning_failed,
    policy_rejected,
    write_failed,
    delivery_failed,
    busy
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

//  Transfers the complete logical buffer, including bytes after the document.
using BakedAssetTransfer = TAssetTransferRequest<CBakedDocumentBlock>;
using LiveAssetTransfer = TAssetTransferRequest<CLiveDocument>;

struct AssetLoadRequest
{
    //  Logical image path, or native: followed by an absolute UTF-8 native path.
    //  Native loads use normal conditioning/retention without image caching.
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

//  Owning path groups keep all nested allocations visible to erased transport.
struct BundleLoadPaths
{
    CSimpleString file;
    CSimpleString root;

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(file.memory_attribution(), root.memory_attribution());
    }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        file.unsafe_replace_memory_context_without_accounting(source, target);
        root.unsafe_replace_memory_context_without_accounting(source, target);
    }
};

struct BundleBakePaths
{
    CSimpleString source_root;
    CSimpleString subdirectory; //  Empty selects the entire source root.
    CSimpleString root;

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(source_root.memory_attribution(),
            memory::combine_memory_attribution(subdirectory.memory_attribution(), root.memory_attribution()));
    }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
    {
        source_root.unsafe_replace_memory_context_without_accounting(source, target);
        subdirectory.unsafe_replace_memory_context_without_accounting(source, target);
        root.unsafe_replace_memory_context_without_accounting(source, target);
    }
};

struct BundleLoadRequest { BundleLoadPaths paths; };
struct BundleBakeRequest { BundleBakePaths paths; };
struct BundleReferenceRequest { CAssetId source{}; };

//  Callers quiesce borrowed views before requesting disposal. Completion contains
//  no view; an accepted request prevents further saves using this identity.
struct AssetDisposeRequest { CAssetId asset{}; };

//==============================================================================
//  Host-to-client asset completions
//  Views borrow Host storage. Disposal returns only the identity and status.
//==============================================================================

struct AssetDisposeResult
{
    CAssetId asset{};
    EAssetStatus status{ EAssetStatus::invalid_asset };
};

struct BundleDisposedNotification { CAssetId asset{}; };

enum class EAssetKind : std::uint8_t { none = 0, raw, image, baked, live, encoded_texture, decoded_texture, bundle };

struct BakedAssetView
{
    memory::CMemoryConstView bytes; // Complete logical file or exact bundle-entry range.
    CBakedDocument document;        // Validated document prefix; appended bytes are opaque.
};

union AssetViews
{
    memory::CMemoryConstView bytes;
    BakedAssetView baked;
    bundle::CBundleView bundle;
    const image::CImageView* image;
    CLiveDocument* live;
    const image::texture::CEncodedTexture* encoded_texture;
    const image::texture::CDecodedTexture* decoded_texture;

    AssetViews() noexcept : bytes{} {}
    explicit AssetViews(const memory::CMemoryConstView& value) noexcept : bytes{ value } {}
    explicit AssetViews(const BakedAssetView& value) noexcept : baked{ value } {}
    explicit AssetViews(const bundle::CBundleView& value) noexcept : bundle{ value } {}
    explicit AssetViews(const image::CImageView* const value) noexcept : image{ value } {}
    explicit AssetViews(CLiveDocument* const value) noexcept : live{ value } {}
    explicit AssetViews(const image::texture::CEncodedTexture* const value) noexcept : encoded_texture{ value } {}
    explicit AssetViews(const image::texture::CDecodedTexture* const value) noexcept : decoded_texture{ value } {}
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

    void set_bundle_view(const bundle::CBundleView& value) noexcept
    {
        views = AssetViews{ value };
        kind = EAssetKind::bundle;
    }

    [[nodiscard]] bundle::CBundleView bundle_view() const noexcept
    {
        return (kind == EAssetKind::bundle) ? views.bundle : bundle::CBundleView{};
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

    void set_encoded_texture_view(const image::texture::CEncodedTexture* const value) noexcept
    {
        views = AssetViews{ value };
        kind = EAssetKind::encoded_texture;
    }

    void set_decoded_texture_view(const image::texture::CDecodedTexture* const value) noexcept
    {
        views = AssetViews{ value };
        kind = EAssetKind::decoded_texture;
    }

    [[nodiscard]] CByteConstView byte_view() const noexcept
    {
        if (kind == EAssetKind::bundle) return views.bundle.bytes();
        if ((kind == EAssetKind::encoded_texture) && (views.encoded_texture != nullptr))
        {
            return views.encoded_texture->buffer.const_view();
        }
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

    [[nodiscard]] image::texture::CEncodedView encoded_texture_view() const noexcept
    {
        return ((kind == EAssetKind::encoded_texture) && (views.encoded_texture != nullptr)) ?
            views.encoded_texture->view() : image::texture::CEncodedView{};
    }

    [[nodiscard]] image::texture::CDecodedView decoded_texture_view() const noexcept
    {
        return ((kind == EAssetKind::decoded_texture) && (views.decoded_texture != nullptr)) ?
            views.decoded_texture->view() : image::texture::CDecodedView{};
    }
};

//  Type bindings precede owning-payload eligibility.
MV_REGISTER_SYSTEM_TYPE(RawAssetTransfer, system_type_ids::raw_asset_transfer);
MV_REGISTER_SYSTEM_TYPE(ImageAssetTransfer, system_type_ids::image_asset_transfer);
MV_REGISTER_SYSTEM_TYPE(BakedAssetTransfer, system_type_ids::baked_asset_transfer);
MV_REGISTER_SYSTEM_TYPE(LiveAssetTransfer, system_type_ids::live_asset_transfer);
MV_REGISTER_SYSTEM_TYPE(AssetLoadRequest, system_type_ids::asset_load_request);
MV_REGISTER_SYSTEM_TYPE(AssetSaveRequest, system_type_ids::asset_save_request);
MV_REGISTER_SYSTEM_TYPE(AssetDisposeRequest, system_type_ids::asset_dispose_request);
MV_REGISTER_SYSTEM_TYPE(AssetDisposeResult, system_type_ids::asset_dispose_result);
MV_REGISTER_SYSTEM_TYPE(AssetResult, system_type_ids::asset_result);
MV_REGISTER_SYSTEM_TYPE(BundleDisposedNotification, system_type_ids::bundle_disposed_notification);
MV_REGISTER_SYSTEM_TYPE(BundleLoadRequest, system_type_ids::bundle_load_request);
MV_REGISTER_SYSTEM_TYPE(BundleBakeRequest, system_type_ids::bundle_bake_request);
MV_REGISTER_SYSTEM_TYPE(BundleReferenceRequest, system_type_ids::bundle_reference_request);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "assets/asset_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

#endif  //  ASSET_MESSAGES_HPP_INCLUDED
