
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   tga.hpp
//  Author: Ritchie Brannan
//  Date:   2 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  TGA encode and decode over byte-oriented image buffers.
//
//  Models codec-facing image layout and format selection only.
//  Does not make the buffer view types themselves format-bearing.
//
//  IMPORTANT SEMANTIC NOTE
//  -----------------------
//  Encode accepts two backing buffer layouts only:
//  - 1 byte per texel source
//  - 4 bytes per texel source
//
//  image_encode_src selects how the source buffer is interpreted.
//
//  Gray uses a 1-byte source texel layout.
//
//  All other encode sources use a 4-byte source texel layout that is
//  expected to be at least 4-byte aligned.
//
//  AutoTrue32 uses 32-bit true-colour only when decoded alpha data is
//  actually required by the source image. Otherwise it falls back to
//  24-bit true-colour.
//
//  Decode returns:
//  - Gray : 1 byte per texel
//  - RGBA : 4 bytes per texel with decoded alpha channel
//  - RGBX : 4 bytes per texel with opaque alpha expansion
//
//  vflip applies to image buffer orientation, not to the TGA file
//  format itself.
//
//  On encode it treats the source buffer as vertically inverted.
//
//  On decode it returns the destination buffer vertically inverted
//  relative to the engine-standard vertical direction.
//
//  Supported encoded output forms
//  ------------------------------
//  - 8-bit greyscale
//  - 24-bit true-colour
//  - 32-bit true-colour
//  - 8-bit indexed colour with 24-bit CLUT entries
//  - 8-bit indexed colour with 32-bit CLUT entries
//
//  Each supported output form may be written as raw or TGA RLE.
//
//  Supported decoded input forms
//  -----------------------------
//  - 8-bit greyscale
//  - 24-bit true-colour
//  - 32-bit true-colour
//  - 8-bit indexed colour with 24-bit CLUT entries
//  - 8-bit indexed colour with 32-bit CLUT entries
//
//  Interleaving is not specially handled and will not be linearised.
//
//  This interface does not expose:
//  - original raw vs RLE storage form
//  - original CLUT vs non-CLUT storage form
//  - image ID field content
//  - origin coordinates
//  - TGA extension, developer, or footer data
//
//  See tga.cpp for the current format notes and implementation details.

#pragma once

#ifndef TGA_HPP_INCLUDED
#define TGA_HPP_INCLUDED

#include <cstdint>      //  std::uint8_t

#include "containers/ByteBuffers.hpp"

namespace image::codec::tga
{

//==============================================================================
//  Encode source and decode result descriptors
//==============================================================================

//  Gray uses a 1-byte source texel layout.
//  All other encode sources use a 4-byte source texel layout that is expected to be at least 4-byte aligned.
//  AutoTrue32 will be encoded as RGBA if meaningful alpha is detected, otherwise it will be encoded as RGB.
enum class image_encode_src : std::uint8_t { Gray = 0u, AutoTrue32 = 1u, RGBA = 2u, RGB = 3u, R = 4u, G = 5u, B = 6u, A = 7u };

//  Decode output uses either:
//  - Gray : 1 byte per texel
//  - RGBA : 4 bytes per texel with decoded alpha channel
//  - RGBX : 4 bytes per texel with opaque alpha expansion
enum class decoded_image_desc : std::uint8_t { Gray = 0u, RGBA = 1u, RGBX = 2u };

//==============================================================================
//  Encode options
//==============================================================================

struct EncodeOptions
{
    image_encode_src src = image_encode_src::AutoTrue32;
    bool allow_clut = true;
    bool allow_rle = true;
    bool vflip = false; //  treat the source image buffer as vertically inverted
};

//==============================================================================
//  Codec interface
//==============================================================================

CByteBuffer encode(const CByteRectConstView& view, const EncodeOptions& options) noexcept;
CByteRectBuffer decode(const CByteConstView& view, decoded_image_desc& desc, const bool vflip = false) noexcept;

}   //  namespace image::codec::tga

#endif  //  TGA_HPP_INCLUDED
