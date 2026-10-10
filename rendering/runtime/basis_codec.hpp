
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    basis_codec.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 Oct 26
//
//  Rendering-owned Basis Universal encoding and transcoding.

#pragma once

#ifndef RENDERING_BASIS_CODEC_HPP_INCLUDED
#define RENDERING_BASIS_CODEC_HPP_INCLUDED

#include <cstdint>

#include "image/texture_data.hpp"

namespace rendering::basis_codec
{

enum class EStatus : std::uint8_t
{
    success = 0,
    invalid_input,
    unsupported_format,
    allocation_failed,
    codec_failed
};

//  Initialize on the rendering thread before accepting codec work; shut down
//  only after all accepted codec operations have completed.
[[nodiscard]] bool initialise() noexcept;
void shutdown() noexcept;

[[nodiscard]] EStatus encode_ktx2(
    const image::texture::CInputView& input,
    const image::texture::CEncodeOptions& options,
    image::texture::CEncodedTexture& output) noexcept;

//  Transcodes one mip level of a 2D KTX2 texture into one aligned buffer.
[[nodiscard]] EStatus transcode_ktx2(
    const CByteConstView& input,
    const image::texture::EStorageFormat target,
    const std::uint32_t level,
    image::texture::CDecodedTexture& output) noexcept;

}   //  namespace rendering::basis_codec

#endif  //  RENDERING_BASIS_CODEC_HPP_INCLUDED
