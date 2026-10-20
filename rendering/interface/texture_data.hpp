
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_data.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 Oct 26
//
//  Engine-owned texture byte layouts and borrowed views. Graphics API
//  enumerations remain within their respective modules.

#pragma once

#ifndef TEXTURE_DATA_HPP_INCLUDED
#define TEXTURE_DATA_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <utility>

#include "containers/ByteBuffers.hpp"

namespace image::texture
{

enum class EInputFormat : std::uint8_t { gray8 = 0, rgba8, rgba16f, rgba32f };
enum class EContainer : std::uint8_t { ktx2 = 0 };
enum class EEncoding : std::uint8_t { uastc_ldr_4x4 = 0, uastc_hdr_4x4 };
enum class EStorageFormat : std::uint8_t { rgba8 = 0, rgba16f, bc7_unorm, bc7_srgb, bc6h_ufloat };
enum class ETransfer : std::uint8_t { linear = 0, srgb };
enum class EEncodeProfile : std::uint8_t { runtime_fast = 0, build_quality };

struct CInputView
{
    CByteRectConstView bytes;
    EInputFormat format{ EInputFormat::rgba8 };
    ETransfer transfer{ ETransfer::linear };

    [[nodiscard]] bool is_ready() const noexcept { return bytes.is_ready(); }
};

struct CEncodeOptions
{
    EEncoding encoding{ EEncoding::uastc_ldr_4x4 };
    EEncodeProfile profile{ EEncodeProfile::runtime_fast };
    bool generate_mips{ false };
    std::uint64_t max_pixels{ (16u * 1024u) * 1024u };
};

struct CEncodedDescription
{
    std::uint32_t width{ 0u };
    std::uint32_t height{ 0u };
    std::uint32_t levels{ 0u };
    EContainer container{ EContainer::ktx2 };
    EEncoding encoding{ EEncoding::uastc_ldr_4x4 };
    ETransfer transfer{ ETransfer::linear };
    float hdr_scale{ 1.0f };
};

struct CEncodedView
{
    CByteConstView bytes;
    CEncodedDescription description;

    [[nodiscard]] bool is_ready() const noexcept { return bytes.is_ready() && (description.levels != 0u); }
};

struct CDecodedDescription
{
    std::uint32_t width{ 0u };
    std::uint32_t height{ 0u };
    std::uint32_t level{ 0u };
    std::uint32_t row_pitch{ 0u }; // Bytes between rows of pixels or blocks.
    EStorageFormat format{ EStorageFormat::rgba8 };
    ETransfer transfer{ ETransfer::linear };
    float hdr_scale{ 1.0f };
};

struct CDecodedView
{
    CByteConstView bytes;
    CDecodedDescription description;

    [[nodiscard]] bool is_ready() const noexcept { return bytes.is_ready() && (description.width != 0u); }
};

struct CEncodedTexture
{
    CByteBuffer buffer;
    CEncodedDescription description;

    [[nodiscard]] CEncodedView view() const noexcept { return { buffer.const_view(), description }; }
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept { return buffer.memory_attribution(); }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
    {
        buffer.unsafe_replace_memory_context_without_accounting(expected_source, target);
    }
};

struct CDecodedTexture
{
    CByteBuffer buffer;
    CDecodedDescription description;

    [[nodiscard]] CDecodedView view() const noexcept { return { buffer.const_view(), description }; }
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept { return buffer.memory_attribution(); }
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
    {
        buffer.unsafe_replace_memory_context_without_accounting(expected_source, target);
    }
};

}   //  namespace image::texture

#endif  //  TEXTURE_DATA_HPP_INCLUDED
