//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  Image utility design: Ritchie Brannan
//  Implementation: OpenAI Codex
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
    using coordinate = std::int32_t;
    using description = codec::tga::decoded_image_desc;
    using encode_source = codec::tga::image_encode_src;

    CImageView() noexcept = default;
    CImageView(const CByteRectView& view, description desc, bool vertical_flip = false) noexcept;
    CImageView(const CByteRectConstView& view, description desc, bool vertical_flip = false) noexcept;

    //  A failed attachment resets the view. Colour rows must be four-byte aligned.
    [[nodiscard]] bool set(const CByteRectView& view, description desc, bool vertical_flip = false) noexcept;
    [[nodiscard]] bool set(const CByteRectConstView& view, description desc, bool vertical_flip = false) noexcept;
    void reset() noexcept { *this = CImageView{}; }

    [[nodiscard]] bool is_ready() const noexcept { return m_view.is_ready(); }
    [[nodiscard]] bool is_read_only() const noexcept { return m_read_only; }
    //  Const storage can never be made writable through this flag.
    void set_read_only(bool read_only) noexcept { m_read_only = read_only || (m_write_data == nullptr); }
    [[nodiscard]] bool is_greyscale() const noexcept { return m_desc == description::Gray; }
    [[nodiscard]] std::size_t width() const noexcept { return m_view.row_width() / texel_bytes(); }
    [[nodiscard]] std::size_t height() const noexcept { return m_view.row_count(); }
    [[nodiscard]] description image_description() const noexcept { return m_desc; }
    [[nodiscard]] CByteRectConstView buffer_view() const noexcept { return m_view; }

    //  Logical coordinates start at top left. Normally y selects physical row y;
    //  vertical_flip reverses row addressing for bottom-up backing storage.
    [[nodiscard]] bool vertical_flip() const noexcept { return m_vertical_flip; }
    void set_vertical_flip(bool value) noexcept { m_vertical_flip = value; }

    //  Drawing does not update the decode description. Explicit RGB/RGBA encode
    //  selection changes RGBX/RGBA interpretation without changing stored pixels.
    [[nodiscard]] bool set_encode_source(encode_source source) noexcept;
    void set_encoding_compression(bool allow_clut, bool allow_rle) noexcept;
    [[nodiscard]] codec::tga::EncodeOptions encode_options() const noexcept;

    //  Invalid reads return zero. Invalid/read-only writes do nothing. Greyscale
    //  writes use the low eight colour bits and ignore the colour-channel mask.
    [[nodiscard]] std::uint32_t texel(coordinate x, coordinate y) const noexcept;
    void plot(coordinate x, coordinate y, std::uint32_t colour, std::uint32_t write_mask = 0xffffffffu) const noexcept;
    void fill(std::uint32_t colour, std::uint32_t write_mask = 0xffffffffu) const noexcept;

    //  Inclusive endpoints. Start at the endpoint with the lower numeric Y;
    //  use absolute deltas and signed coordinate steps. Half-delta initial error
    //  and strict subtraction-underflow stepping; clipping preserves phase.
    void draw_line(coordinate x0, coordinate y0, coordinate x1, coordinate y1,
        std::uint32_t colour, std::uint32_t write_mask = 0xffffffffu) const noexcept;

    //  Signed extents are normalised first. Right and bottom bounds are exclusive.
    //  Clipping never creates a new outline edge at the image boundary.
    void fill_rectangle(coordinate x, coordinate y, coordinate width, coordinate height,
        std::uint32_t colour, std::uint32_t write_mask = 0xffffffffu) const noexcept;
    void draw_rectangle(coordinate x, coordinate y, coordinate width, coordinate height,
        std::uint32_t colour, std::uint32_t write_mask = 0xffffffffu) const noexcept;

    //  Both rectangles are clipped by the same amounts, then the surviving source
    //  rectangle is optionally mirrored/flipped. No overlapping byte regions or
    //  greyscale/colour conversion. Failure never changes destination pixels.
    //  Empty clipped copies succeed; invalid/read-only/incompatible copies fail.
    [[nodiscard]] bool copy_rectangle(const CImageView& source,
        coordinate source_x, coordinate source_y, coordinate destination_x, coordinate destination_y,
        coordinate width, coordinate height, EImageCopyFlags flags = EImageCopyFlags::none,
        std::uint32_t write_mask = 0xffffffffu) const noexcept;

private:
    [[nodiscard]] std::size_t texel_bytes() const noexcept { return is_greyscale() ? 1u : 4u; }
    [[nodiscard]] std::size_t physical_row(std::size_t y) const noexcept { return m_vertical_flip ? height() - 1u - y : y; }
    [[nodiscard]] bool contains(coordinate x, coordinate y) const noexcept;
    void write_texel(std::size_t x, std::size_t y, std::uint32_t colour, std::uint32_t write_mask) const noexcept;
    void fill_region(std::int64_t left, std::int64_t top, std::int64_t right, std::int64_t bottom,
        std::uint32_t colour, std::uint32_t write_mask) const noexcept;

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
