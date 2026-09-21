
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   image_view.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:   19 Sep 26
//
//  Clipped image drawing and copying over borrowed rectangular storage.

#include "image/image_view.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include "debug/macros.hpp"

namespace image
{

namespace rasterization
{

struct SRectangle
{
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

static std::int32_t saturated_add(const std::int32_t value, const std::int32_t delta) noexcept
{
    constexpr std::int32_t low = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t high = std::numeric_limits<std::int32_t>::max();
    if ((delta > 0) && (value > (high - delta)))
    {
        return high;
    }
    if ((delta < 0) && (value < (low - delta)))
    {
        return low;
    }
    return value + delta;
}

static SRectangle rectangle(const std::int32_t x, const std::int32_t y, const std::int32_t width, const std::int32_t height) noexcept
{
    //  Saturated endpoints remain far outside the 16-bit image bounds. This
    //  preserves the visible rectangle without widening coordinate arithmetic.
    const std::int32_t end_x = saturated_add(x, width);
    const std::int32_t end_y = saturated_add(y, height);
    return { std::min(x, end_x), std::min(y, end_y), std::max(x, end_x), std::max(y, end_y) };
}

static bool intersects_image(const SRectangle& rect, const std::int32_t width, const std::int32_t height) noexcept
{
    return (rect.left < rect.right) && (rect.top < rect.bottom) &&
        (rect.left < width) && (rect.right > 0) && (rect.top < height) && (rect.bottom > 0);
}

static std::uint32_t axis_distance(const std::int32_t a, const std::int32_t b) noexcept
{
    //  The distance between INT32_MIN and INT32_MAX needs all 32 unsigned bits.
    //  Unsigned subtraction gives that magnitude without signed overflow.
    return (a >= b) ?
        (static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)) :
        (static_cast<std::uint32_t>(b) - static_cast<std::uint32_t>(a));
}

static bool clip_axis(const std::int32_t start, const std::int32_t finish, const std::int32_t limit, std::uint32_t& first, std::uint32_t& last) noexcept
{
    if ((std::max(start, finish) < 0) || (std::min(start, finish) >= limit))
    {
        return false;
    }
    first = axis_distance(start, std::clamp(start, 0, (limit - 1)));
    last = axis_distance(start, std::clamp(finish, 0, (limit - 1)));
    return true;
}

static std::int32_t clipped_coordinate(const std::int32_t start, const bool increasing, const std::uint32_t steps) noexcept
{
    //  Called only after clipping: the final unsigned value is in 0..65534,
    //  even if the skipped distance is greater than INT32_MAX.
    const std::uint32_t result = increasing ?
        (static_cast<std::uint32_t>(start) + steps) :
        (static_cast<std::uint32_t>(start) - steps);
    return static_cast<std::int32_t>(result);
}

struct SLineRaster
{
    std::uintptr_t offset = 0u;
    std::uintptr_t major_step = 0u;
    std::uintptr_t minor_step = 0u;
    std::uint32_t pixel_count = 0u;
    std::uint32_t first_run = 0u;
    std::uint32_t whole_steps = 0u;
    std::uint32_t remainder = 0u;
    std::uint32_t fraction = 0u;
    std::uint32_t minor_delta = 0u;
};

template<bool Gray, bool Masked>
static void draw_line_pixels(std::uint8_t* const data, const SLineRaster& line, const std::uint32_t colour, const std::uint32_t write_mask) noexcept
{
    std::uintptr_t address = reinterpret_cast<std::uintptr_t>(data) + line.offset;
    std::uint32_t remaining = line.pixel_count;
    std::uint32_t run = line.first_run;
    std::uint32_t fraction = line.fraction;
    const std::uintptr_t major_step = line.major_step;
    const std::uintptr_t minor_step = line.minor_step;
    const std::uint32_t whole_steps = line.whole_steps;
    const std::uint32_t remainder = line.remainder;
    const std::uint32_t minor_delta = line.minor_delta;
    const std::uint32_t masked_colour = colour & write_mask;
    const std::uint32_t preserve_mask = ~write_mask;
    while (remaining != 0u)
    {
        const std::uint32_t count = std::min(run, remaining);
        for (std::uint32_t pixel = 0u; pixel < count; ++pixel)
        {
            if constexpr (Gray)
            {
                *reinterpret_cast<std::uint8_t*>(address) = static_cast<std::uint8_t>(colour);
            }
            else if constexpr (Masked)
            {
                std::uint32_t previous;
                std::memcpy(&previous, reinterpret_cast<const void*>(address), sizeof(previous));
                const std::uint32_t result = (previous & preserve_mask) | masked_colour;
                std::memcpy(reinterpret_cast<void*>(address), &result, sizeof(result));
            }
            else
            {
                std::memcpy(reinterpret_cast<void*>(address), &colour, sizeof(colour));
            }
            //  Unsigned address deltas also represent backward traversal. Only
            //  clipped pixel addresses are dereferenced; no coordinate products
            //  or pixel-format/mask branches are needed inside this loop.
            address += major_step;
        }
        remaining -= count;
        if (remaining == 0u)
        {
            break;
        }
        address += minor_step;
        run = whole_steps;
        if (fraction < remainder)
        {
            fraction += minor_delta - remainder;
            ++run;
        }
        else
        {
            fraction -= remainder;
        }
    }
}

//  Rect views may alias without sharing their starting address or row pitch.
//  Walk the actual active row intervals in address order, excluding row padding.
static bool regions_overlap(
    const CByteRectConstView& a, const std::uintptr_t ax, const std::int32_t ay,
    const CByteRectConstView& b, const std::uintptr_t bx, const std::int32_t by,
    const std::uintptr_t width_bytes, const std::int32_t rows) noexcept
{
    std::int32_t ar = 0;
    std::int32_t br = 0;
    while ((ar < rows) && (br < rows))
    {
        const auto a_begin = reinterpret_cast<std::uintptr_t>(a.row_data((ay + ar)) + ax);
        const auto b_begin = reinterpret_cast<std::uintptr_t>(b.row_data((by + br)) + bx);
        if ((a_begin < b_begin + width_bytes) && (b_begin < a_begin + width_bytes))
        {
            return true;
        }
        if (a_begin < b_begin)
        {
            ++ar;
        }
        else
        {
            ++br;
        }
    }
    return false;
}

}   //  namespace rasterization

CImageView::CImageView(const CByteRectView& view, const description desc, const bool vertical_flip) noexcept
{
    (void)set(view, desc, vertical_flip);
}

CImageView::CImageView(const CByteRectConstView& view, const description desc, const bool vertical_flip) noexcept
{
    (void)set(view, desc, vertical_flip);
}

bool CImageView::set(const CByteRectConstView& view, const description desc, const bool vertical_flip) noexcept
{
    reset();
    if (!view.is_ready() || ((desc != description::Gray) && (desc != description::RGBA) && (desc != description::RGBX)))
    {
        return false;
    }
    if ((desc != description::Gray) && (((view.row_width() | view.row_pitch()) & 3u) != 0u || view.align() < 4u))
    {
        return false;
    }
    const std::uint32_t bytes = (desc == description::Gray) ? 1u : 4u;
    if ((view.row_width() / bytes > 0xffffu) || (view.row_count() > 0xffffu))
    {
        return false;
    }
    m_view = view;
    m_desc = desc;
    m_vertical_flip = vertical_flip;
    m_encode_source = (desc == description::Gray) ? encode_source::Gray :
        ((desc == description::RGBA) ? encode_source::RGBA : encode_source::RGB);
    return true;
}

bool CImageView::set(const CByteRectView& view, const description desc, const bool vertical_flip) noexcept
{
    if (!set(view.const_view(), desc, vertical_flip))
    {
        return false;
    }
    m_access = EAccess::writable;
    return true;
}

bool CImageView::set_read_only(const bool read_only) noexcept
{
    if (m_access == EAccess::const_storage)
    {
        return read_only;
    }
    m_access = read_only ? EAccess::read_only : EAccess::writable;
    return true;
}

bool CImageView::set_encode_source(const encode_source source) noexcept
{
    if (!is_ready() || (static_cast<std::uint8_t>(source) > static_cast<std::uint8_t>(encode_source::A)) ||
        (is_greyscale() != (source == encode_source::Gray)))
    {
        return false;
    }
    m_encode_source = source;
    if (source == encode_source::RGBA)
    {
        m_desc = description::RGBA;
    }
    else if (source == encode_source::RGB)
    {
        m_desc = description::RGBX;
    }
    return true;
}

void CImageView::set_encoding_compression(const bool allow_clut, const bool allow_rle) noexcept
{
    m_allow_clut = allow_clut;
    m_allow_rle = allow_rle;
}

codec::tga::EncodeOptions CImageView::encode_options() const noexcept
{
    codec::tga::EncodeOptions result;
    result.src = m_encode_source;
    result.allow_clut = m_allow_clut;
    result.allow_rle = m_allow_rle;
    //  The existing codec's canonical row zero is the bottom row. The image
    //  view's default row zero is the top row, so these flags have opposite sense.
    result.vflip = !m_vertical_flip;
    return result;
}

std::uintptr_t CImageView::buffer_offset(const std::int32_t x, const std::int32_t y) const noexcept
{
    return static_cast<std::uintptr_t>(physical_row(y)) * m_view.row_pitch() +
        static_cast<std::uintptr_t>(x) * texel_bytes();
}

bool CImageView::contains(const std::int32_t x, const std::int32_t y) const noexcept
{
    return (x >= 0) && (y >= 0) && (x < width()) && (y < height());
}

std::uint32_t CImageView::texel(const std::int32_t x, const std::int32_t y) const noexcept
{
    if (!contains(x, y))
    {
        return 0u;
    }
    const std::uint8_t* const data = m_view.data() + buffer_offset(x, y);
    if (is_greyscale())
    {
        return *data;
    }
    std::uint32_t result;
    std::memcpy(&result, data, sizeof(result));
    return result;
}

void CImageView::write_texel(const std::int32_t x, const std::int32_t y, const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    //  All callers have rejected read-only access before reaching this helper.
    std::uint8_t* const data = const_cast<std::uint8_t*>(m_view.data()) + buffer_offset(x, y);
    if (is_greyscale())
    {
        *data = static_cast<std::uint8_t>(colour);
    }
    else if (write_mask != 0u)
    {
        std::uint32_t result = colour;
        if (write_mask != 0xffffffffu)
        {
            std::uint32_t previous;
            std::memcpy(&previous, data, sizeof(previous));
            result = (previous & ~write_mask) | (colour & write_mask);
        }
        std::memcpy(data, &result, sizeof(result));
    }
}

void CImageView::plot(const std::int32_t x, const std::int32_t y, const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    if (is_read_only())
    {
        MV_ASSERT_MSG(false, "Cannot draw to a read-only image view.");
        return;
    }
    if (contains(x, y))
    {
        write_texel(x, y, colour, write_mask);
    }
}

void CImageView::fill_region(
    const std::int32_t left, const std::int32_t top,
    const std::int32_t right, const std::int32_t bottom,
    const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    if (is_read_only())
    {
        MV_ASSERT_MSG(false, "Cannot draw to a read-only image view.");
        return;
    }
    const std::int32_t x_begin = std::max(left, 0);
    const std::int32_t x_end = std::min(right, width());
    const std::int32_t y_end = std::min(bottom, height());
    for (std::int32_t y = std::max(top, 0); y < y_end; ++y)
    {
        for (std::int32_t x = x_begin; x < x_end; ++x)
        {
            write_texel(x, y, colour, write_mask);
        }
    }
}

void CImageView::fill(const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    fill_region(0, 0, width(), height(), colour, write_mask);
}

void CImageView::draw_line(
    const std::int32_t x0, const std::int32_t y0,
    const std::int32_t x1, const std::int32_t y1,
    const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    const bool gray = is_greyscale();
    if (is_read_only())
    {
        MV_ASSERT_MSG(false, "Cannot draw to a read-only image view.");
        return;
    }
    if (!is_ready() || (!gray && (write_mask == 0u)))
    {
        return;
    }
    const std::uintptr_t x_step = texel_bytes();
    const std::uintptr_t y_step = m_vertical_flip ? (std::uintptr_t{ 0u } - m_view.row_pitch()) : m_view.row_pitch();
    rasterization::SLineRaster line;
    if (y0 == y1)
    {
        const std::int32_t first = std::max(std::min(x0, x1), 0);
        const std::int32_t last = std::min(std::max(x0, x1), (width() - 1));
        if ((y0 < 0) || (y0 >= height()) || (first > last))
        {
            return;
        }
        line.offset = buffer_offset(first, y0);
        line.major_step = x_step;
        line.pixel_count = static_cast<std::uint32_t>(last - first + 1);
        line.first_run = line.pixel_count;
    }
    else if (x0 == x1)
    {
        const std::int32_t first = std::max(std::min(y0, y1), 0);
        const std::int32_t last = std::min(std::max(y0, y1), (height() - 1));
        if ((x0 < 0) || (x0 >= width()) || (first > last))
        {
            return;
        }
        line.offset = buffer_offset(x0, first);
        line.major_step = y_step;
        line.pixel_count = static_cast<std::uint32_t>(last - first + 1);
        line.first_run = line.pixel_count;
    }
    else
    {
        const std::int32_t start_x = (y0 < y1) ? x0 : x1;
        const std::int32_t finish_x = (y0 < y1) ? x1 : x0;
        const std::int32_t start_y = std::min(y0, y1);
        const std::int32_t finish_y = std::max(y0, y1);
        const std::uint32_t dx = rasterization::axis_distance(start_x, finish_x);
        const std::uint32_t dy = rasterization::axis_distance(start_y, finish_y);
        const bool x_major = dx > dy;
        const bool x_increasing = start_x < finish_x;
        const std::uint32_t major = std::max(dx, dy);
        const std::uint32_t minor = std::min(dx, dy);
        const std::int32_t major_start = x_major ? start_x : start_y;
        const std::int32_t major_finish = x_major ? finish_x : finish_y;
        const std::int32_t minor_start = x_major ? start_y : start_x;
        const std::int32_t minor_finish = x_major ? finish_y : finish_x;
        std::uint32_t first, last, minor_first, minor_last;
        if (!rasterization::clip_axis(major_start, major_finish, (x_major ? width() : height()), first, last) ||
            !rasterization::clip_axis(minor_start, minor_finish, (x_major ? height() : width()), minor_first, minor_last))
        {
            return;
        }
        const std::uintptr_t horizontal_step = x_increasing ? x_step : (std::uintptr_t{ 0u } - x_step);
        std::uint32_t advance;
        if (major == minor)
        {
            first = std::max(first, minor_first);
            last = std::min(last, minor_last);
            if (first > last)
            {
                return;
            }
            advance = first;
            line.major_step = horizontal_step + y_step;
            line.first_run = last - first + 1u;
        }
        else
        {
            //  At major step k the scalar underflow algorithm has advanced the
            //  minor axis (k * minor + bias) / major times. Products alone need
            //  uint64: an extreme off-image line can have 32-bit unsigned deltas.
            //  All coordinates, clipped counts and the raster loop remain 32-bit.
            const std::uint32_t half = major >> 1u;
            const std::uint32_t bias = major - 1u - half;
            if (minor_first != 0u)
            {
                const std::uint64_t numerator = (static_cast<std::uint64_t>(minor_first) * major) - bias;
                first = std::max(first, static_cast<std::uint32_t>((numerator + minor - 1u) / minor));
            }
            if (minor_last < minor)
            {
                const std::uint64_t numerator = ((static_cast<std::uint64_t>(minor_last) + 1u) * major) - bias - 1u;
                last = std::min(last, static_cast<std::uint32_t>(numerator / minor));
            }
            if (first > last)
            {
                return;
            }
            advance = static_cast<std::uint32_t>(((static_cast<std::uint64_t>(first) * minor) + bias) / major);
            const std::uint64_t numerator = half + (static_cast<std::uint64_t>(advance) * major);
            line.first_run = static_cast<std::uint32_t>(std::min<std::uint64_t>((numerator / minor), last)) - first + 1u;
            line.fraction = minor - 1u - static_cast<std::uint32_t>(numerator % minor);
            line.whole_steps = major / minor;
            line.remainder = major % minor;
            line.minor_delta = minor;
            line.major_step = x_major ? horizontal_step : y_step;
            line.minor_step = x_major ? y_step : horizontal_step;
        }
        const std::int32_t x = rasterization::clipped_coordinate(start_x, x_increasing, (x_major ? first : advance));
        const std::int32_t y = rasterization::clipped_coordinate(start_y, true, (x_major ? advance : first));
        line.offset = buffer_offset(x, y);
        line.pixel_count = last - first + 1u;
    }
    std::uint8_t* const data = const_cast<std::uint8_t*>(m_view.data());
    if (gray)
    {
        rasterization::draw_line_pixels<true, false>(data, line, colour, write_mask);
    }
    else if (write_mask == 0xffffffffu)
    {
        rasterization::draw_line_pixels<false, false>(data, line, colour, write_mask);
    }
    else
    {
        rasterization::draw_line_pixels<false, true>(data, line, colour, write_mask);
    }
}

void CImageView::fill_rectangle(
    const std::int32_t x, const std::int32_t y,
    const std::int32_t width, const std::int32_t height,
    const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    const rasterization::SRectangle rect = rasterization::rectangle(x, y, width, height);
    fill_region(rect.left, rect.top, rect.right, rect.bottom, colour, write_mask);
}

void CImageView::draw_rectangle(
    const std::int32_t x, const std::int32_t y,
    const std::int32_t width, const std::int32_t height,
    const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    if (is_read_only())
    {
        MV_ASSERT_MSG(false, "Cannot draw to a read-only image view.");
        return;
    }
    const rasterization::SRectangle rect = rasterization::rectangle(x, y, width, height);
    if ((rect.left == rect.right) || (rect.top == rect.bottom))
    {
        return;
    }
    fill_region(rect.left, rect.top, rect.right, (rect.top + 1), colour, write_mask);
    if (rect.bottom > (rect.top + 1))
    {
        fill_region(rect.left, (rect.bottom - 1), rect.right, rect.bottom, colour, write_mask);
        fill_region(rect.left, (rect.top + 1), (rect.left + 1), (rect.bottom - 1), colour, write_mask);
        if (rect.right > (rect.left + 1))
        {
            fill_region((rect.right - 1), (rect.top + 1), rect.right, (rect.bottom - 1), colour, write_mask);
        }
    }
}

bool CImageView::copy_rectangle(const CImageView& source,
    const std::int32_t source_x, const std::int32_t source_y,
    const std::int32_t destination_x, const std::int32_t destination_y,
    const std::int32_t width, const std::int32_t height,
    const EImageCopyFlags flags, const std::uint32_t write_mask) const noexcept
{
    if (is_read_only())
    {
        MV_ASSERT_MSG(false, "Cannot draw to a read-only image view.");
        return false;
    }
    if (!is_ready() || !source.is_ready() ||
        (is_greyscale() != source.is_greyscale()) || (static_cast<std::uint8_t>(flags) > 3u))
    {
        return false;
    }
    const rasterization::SRectangle src = rasterization::rectangle(source_x, source_y, width, height);
    const rasterization::SRectangle dst = rasterization::rectangle(destination_x, destination_y, width, height);
    if (!rasterization::intersects_image(src, source.width(), source.height()) ||
        !rasterization::intersects_image(dst, this->width(), this->height()))
    {
        return true;
    }
    //  Any rectangle starting at INT32_MIN cannot reach a positive image texel
    //  with an int32 extent, so the intersection check above makes negation safe.
    const std::int32_t left = std::max({ 0, -src.left, -dst.left });
    const std::int32_t top = std::max({ 0, -src.top, -dst.top });
    const std::int32_t sx = rasterization::saturated_add(src.left, left);
    const std::int32_t sy = rasterization::saturated_add(src.top, top);
    const std::int32_t dx = rasterization::saturated_add(dst.left, left);
    const std::int32_t dy = rasterization::saturated_add(dst.top, top);
    const std::int32_t columns = std::min((std::min(src.right, source.width()) - sx), (std::min(dst.right, this->width()) - dx));
    const std::int32_t rows = std::min((std::min(src.bottom, source.height()) - sy), (std::min(dst.bottom, this->height()) - dy));
    if ((columns <= 0) || (rows <= 0))
    {
        return true;
    }
    const std::int32_t source_first_row = source.m_vertical_flip ? (source.height() - sy - rows) : sy;
    const std::int32_t destination_first_row = m_vertical_flip ? (this->height() - dy - rows) : dy;
    if (rasterization::regions_overlap(
        source.m_view, (static_cast<std::uintptr_t>(sx) * texel_bytes()), source_first_row,
        m_view, (static_cast<std::uintptr_t>(dx) * texel_bytes()), destination_first_row,
        (static_cast<std::uintptr_t>(columns) * texel_bytes()), rows))
    {
        return false;
    }
    const bool mirror = (static_cast<std::uint8_t>(flags) & 1u) != 0u;
    const bool flip = (static_cast<std::uint8_t>(flags) & 2u) != 0u;
    for (std::int32_t y = 0; y < rows; ++y)
    {
        for (std::int32_t x = 0; x < columns; ++x)
        {
            const std::int32_t tx = sx + (mirror ? (columns - 1 - x) : x);
            const std::int32_t ty = sy + (flip ? (rows - 1 - y) : y);
            write_texel((dx + x), (dy + y), source.texel(tx, ty), write_mask);
        }
    }
    return true;
}

}   //  namespace image
