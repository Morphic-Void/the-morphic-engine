
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Shared Rendering texture requests, borrowed work and owning results.

#pragma once

#ifndef RENDERING_TEXTURE_MESSAGES_HPP_INCLUDED
#define RENDERING_TEXTURE_MESSAGES_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "assets/asset_repository.hpp"
#include "containers/ByteBuffers.hpp"
#include "rendering/interface/texture_data.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/system_type_registration.hpp"

//  Texture work is submitted to the rendering thread. Requests borrow Host
//  storage; results own aligned bytes that the Host can retain for client views.
struct TextureEncodeRequest;
struct TextureDecodeRequest;

struct TextureEncodeWork
{
    const TextureEncodeRequest* request{ nullptr };
};

struct TextureDecodeWork
{
    const TextureDecodeRequest* request{ nullptr };
    const CByteConstView* input{ nullptr };
};

enum class ETextureStatus : std::uint8_t
{
    success = 0, invalid_input, unsupported_format, allocation_failed, codec_failed
};

struct TextureEncodeResult
{
    image::texture::CEncodedTexture texture;
    ETextureStatus status{ ETextureStatus::allocation_failed };

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept { return texture.memory_attribution(); }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
    {
        texture.unsafe_replace_memory_context_without_accounting(expected_source, target);
    }
};

struct TextureDecodeResult
{
    image::texture::CDecodedTexture texture;
    ETextureStatus status{ ETextureStatus::allocation_failed };

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept { return texture.memory_attribution(); }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
    {
        texture.unsafe_replace_memory_context_without_accounting(expected_source, target);
    }
};

//  Encoding transfers source pixels into the Host. Decoding borrows a retained
//  encoded texture; its identity remains live until the rendering reply arrives.
struct TextureEncodeRequest
{
    CByteRectBuffer input;
    image::texture::EInputFormat format{ image::texture::EInputFormat::rgba8 };
    image::texture::ETransfer transfer{ image::texture::ETransfer::linear };
    image::texture::CEncodeOptions options;

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept { return input.memory_attribution(); }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
    {
        input.unsafe_replace_memory_context_without_accounting(expected_source, target);
    }
};

struct TextureDecodeRequest
{
    CAssetId source{};
    image::texture::EStorageFormat target{ image::texture::EStorageFormat::rgba8 };
    std::uint32_t level{ 0u };
};

//  Type bindings precede owning-payload eligibility.
MV_REGISTER_SYSTEM_TYPE(TextureEncodeWork, system_type_ids::texture_encode_work);
MV_REGISTER_SYSTEM_TYPE(TextureDecodeWork, system_type_ids::texture_decode_work);
MV_REGISTER_SYSTEM_TYPE(TextureEncodeRequest, system_type_ids::texture_encode_request);
MV_REGISTER_SYSTEM_TYPE(TextureDecodeRequest, system_type_ids::texture_decode_request);
MV_REGISTER_SYSTEM_TYPE(TextureEncodeResult, system_type_ids::texture_encode_result);
MV_REGISTER_SYSTEM_TYPE(TextureDecodeResult, system_type_ids::texture_decode_result);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "rendering/interface/texture_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

#endif  //  RENDERING_TEXTURE_MESSAGES_HPP_INCLUDED
