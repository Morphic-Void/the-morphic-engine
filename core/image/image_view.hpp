
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   image_view.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:   19 Sep 26
//
//  Borrowed image storage for development tools, experiments and texture markup.
//  No allocation, ownership transfer, blending or colour conversion.

#pragma once

#ifndef IMAGE_VIEW_HPP_INCLUDED
#define IMAGE_VIEW_HPP_INCLUDED

#include <cstdint>

#include "containers/ByteBuffers.hpp"
#include "image/codec/tga.hpp"

namespace image
{

enum class EImageCopyFlags : std::uint8_t
{
    none = 0u,
    mirror = 1u,
    flip = 2u,
    mirror_and_flip = 3u
};

class CImageView
{
public:
    using description = codec::tga::decoded_image_desc;
    using encode_source = codec::tga::image_encode_src;

    CImageView() noexcept = default;
    CImageView(const CByteRectView& view, const description desc, const bool vertical_flip = false) noexcept;
    CImageView(const CByteRectConstView& view, const description desc, const bool vertical_flip = false) noexcept;

    //  A failed attachment resets the view. Dimensions must fit TGA (1..65535).
    //  Colour rows must be four-byte aligned.
    [[nodiscard]] bool set(const CByteRectView& view, const description desc, const bool vertical_flip = false) noexcept;
    [[nodiscard]] bool set(const CByteRectConstView& view, const description desc, const bool vertical_flip = false) noexcept;
    void reset() noexcept { *this = CImageView{}; }

    [[nodiscard]] bool is_ready() const noexcept { return m_view.is_ready(); }
    [[nodiscard]] bool is_read_only() const noexcept { return m_read_only; }
    //  Const storage can never be made writable through this flag.
    void set_read_only(const bool read_only) noexcept { m_read_only = read_only || (m_write_data == nullptr); }
    [[nodiscard]] bool is_greyscale() const noexcept { return m_desc == description::Gray; }
    [[nodiscard]] std::int32_t width() const noexcept { return static_cast<std::int32_t>(m_view.row_width() / texel_bytes()); }
    [[nodiscard]] std::int32_t height() const noexcept { return static_cast<std::int32_t>(m_view.row_count()); }
    [[nodiscard]] description image_description() const noexcept { return m_desc; }
    [[nodiscard]] CByteRectConstView buffer_view() const noexcept { return m_view; }

    //  Logical coordinates start at top left. Normally y selects physical row y;
    //  vertical_flip reverses row addressing for bottom-up backing storage.
    [[nodiscard]] bool vertical_flip() const noexcept { return m_vertical_flip; }
    void set_vertical_flip(const bool value) noexcept { m_vertical_flip = value; }

    //  Drawing does not update the decode description. Explicit RGB/RGBA encode
    //  selection changes RGBX/RGBA interpretation without changing stored pixels.
    [[nodiscard]] bool set_encode_source(const encode_source source) noexcept;
    void set_encoding_compression(const bool allow_clut, const bool allow_rle) noexcept;
    [[nodiscard]] codec::tga::EncodeOptions encode_options() const noexcept;

    //  Invalid reads return zero. Invalid/read-only writes do nothing. Greyscale
    //  writes use the low eight colour bits and ignore the colour-channel mask.
    [[nodiscard]] std::uint32_t texel(const std::int32_t x, const std::int32_t y) const noexcept;
    void plot(const std::int32_t x, const std::int32_t y, const std::uint32_t colour, const std::uint32_t write_mask = 0xffffffffu) const noexcept;
    void fill(const std::uint32_t colour, const std::uint32_t write_mask = 0xffffffffu) const noexcept;

    //  Inclusive endpoints. Start at the endpoint with the lower numeric Y;
    //  use absolute deltas and signed coordinate steps. Half-delta initial error
    //  and strict subtraction-underflow stepping; clipping preserves phase.
    void draw_line(const std::int32_t x0, const std::int32_t y0, const std::int32_t x1, const std::int32_t y1,
        const std::uint32_t colour, const std::uint32_t write_mask = 0xffffffffu) const noexcept;

    //  Signed extents are normalised first. Right and bottom bounds are exclusive.
    //  Clipping never creates a new outline edge at the image boundary.
    void fill_rectangle(const std::int32_t x, const std::int32_t y, const std::int32_t width, const std::int32_t height,
        const std::uint32_t colour, const std::uint32_t write_mask = 0xffffffffu) const noexcept;
    void draw_rectangle(const std::int32_t x, const std::int32_t y, const std::int32_t width, const std::int32_t height,
        const std::uint32_t colour, const std::uint32_t write_mask = 0xffffffffu) const noexcept;

    //  Both rectangles are clipped by the same amounts, then the surviving source
    //  rectangle is optionally mirrored/flipped. No overlapping byte regions or
    //  greyscale/colour conversion. Failure never changes destination pixels.
    //  Empty clipped copies succeed; invalid/read-only/incompatible copies fail.
    [[nodiscard]] bool copy_rectangle(const CImageView& source,
        const std::int32_t source_x, const std::int32_t source_y,
        const std::int32_t destination_x, const std::int32_t destination_y,
        const std::int32_t width, const std::int32_t height,
        const EImageCopyFlags flags = EImageCopyFlags::none,
        const std::uint32_t write_mask = 0xffffffffu) const noexcept;

private:
    [[nodiscard]] std::uint32_t texel_bytes() const noexcept { return is_greyscale() ? 1u : 4u; }
    [[nodiscard]] std::int32_t physical_row(const std::int32_t y) const noexcept { return m_vertical_flip ? (height() - 1 - y) : y; }
    [[nodiscard]] std::uintptr_t buffer_offset(const std::int32_t x, const std::int32_t y) const noexcept;
    [[nodiscard]] bool contains(const std::int32_t x, const std::int32_t y) const noexcept;
    void write_texel(const std::int32_t x, const std::int32_t y, const std::uint32_t colour, const std::uint32_t write_mask) const noexcept;
    void fill_region(const std::int32_t left, const std::int32_t top, const std::int32_t right, const std::int32_t bottom,
        const std::uint32_t colour, const std::uint32_t write_mask) const noexcept;

    CByteRectConstView m_view;
    std::uint8_t* m_write_data = nullptr;
    description m_desc = description::Gray;
    encode_source m_encode_source = encode_source::Gray;
    bool m_read_only = true;
    bool m_vertical_flip = false;
    bool m_allow_clut = true;
    bool m_allow_rle = true;
};

}   //  namespace image

#endif  //  IMAGE_VIEW_HPP_INCLUDED
