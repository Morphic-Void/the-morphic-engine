
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_requests.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Rendering-local texture requests, borrowed work and owning results.

#pragma once

#ifndef RENDERING_TEXTURE_REQUESTS_HPP_INCLUDED
#define RENDERING_TEXTURE_REQUESTS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "assets/asset_repository.hpp"
#include "containers/ByteBuffers.hpp"
#include "texture/texture_data.hpp"

//  Rendering retains requests while batch jobs borrow their inputs. Results own
//  aligned bytes and stay within Rendering until explicitly exported as raw data.
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

//  Encoding transfers pixels into Rendering. Decoding acquires generic source
//  bytes from Host and releases the acquisition after the batch callback drains.
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

#endif  //  RENDERING_TEXTURE_REQUESTS_HPP_INCLUDED
