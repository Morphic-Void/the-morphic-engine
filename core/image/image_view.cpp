//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  Image utility design: Ritchie Brannan
//  Implementation: OpenAI Codex

#include "image/image_view.hpp"

#include <algorithm>
#include <cstring>

namespace image
{
namespace
{

struct Rectangle
{
    std::int64_t left;
    std::int64_t top;
    std::int64_t right;
    std::int64_t bottom;
};

Rectangle rectangle(const CImageView::coordinate x, const CImageView::coordinate y,
    const CImageView::coordinate width, const CImageView::coordinate height) noexcept
{
    const std::int64_t end_x = static_cast<std::int64_t>(x) + width;
    const std::int64_t end_y = static_cast<std::int64_t>(y) + height;
    return { std::min<std::int64_t>(x, end_x), std::min<std::int64_t>(y, end_y),
        std::max<std::int64_t>(x, end_x), std::max<std::int64_t>(y, end_y) };
}

//  Rect views may alias without sharing their starting address or row pitch.
//  Walk the actual active row intervals in address order, excluding row padding.
bool regions_overlap(const CByteRectConstView& a, const std::size_t ax, const std::size_t ay,
    const CByteRectConstView& b, const std::size_t bx, const std::size_t by,
    const std::size_t width_bytes, const std::size_t rows) noexcept
{
    std::size_t ar = 0u;
    std::size_t br = 0u;
    while ((ar < rows) && (br < rows))
    {
        const auto a_begin = reinterpret_cast<std::uintptr_t>(a.row_data(ay + ar) + ax);
        const auto b_begin = reinterpret_cast<std::uintptr_t>(b.row_data(by + br) + bx);
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

}   //  namespace

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
    if (!view.is_ready() ||
        ((desc != description::Gray) && (desc != description::RGBA) && (desc != description::RGBX)))
    {
        return false;
    }
    if ((desc != description::Gray) &&
        (((view.row_width() | view.row_pitch()) & 3u) != 0u || view.align() < 4u))
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
    m_write_data = view.data();
    m_read_only = false;
    return true;
}

bool CImageView::set_encode_source(const encode_source source) noexcept
{
    if (!is_ready() || (static_cast<unsigned>(source) > static_cast<unsigned>(encode_source::A)) ||
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

bool CImageView::contains(const coordinate x, const coordinate y) const noexcept
{
    return (x >= 0) && (y >= 0) &&
        (static_cast<std::size_t>(x) < width()) && (static_cast<std::size_t>(y) < height());
}

std::uint32_t CImageView::texel(const coordinate x, const coordinate y) const noexcept
{
    if (!contains(x, y))
    {
        return 0u;
    }
    const auto* const data = m_view.row_data(physical_row(static_cast<std::size_t>(y))) +
        static_cast<std::size_t>(x) * texel_bytes();
    if (is_greyscale())
    {
        return *data;
    }
    std::uint32_t result;
    std::memcpy(&result, data, sizeof(result));
    return result;
}

void CImageView::write_texel(const std::size_t x, const std::size_t y,
    const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    auto* const data = m_write_data + physical_row(y) * m_view.row_pitch() + x * texel_bytes();
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

void CImageView::plot(const coordinate x, const coordinate y, const std::uint32_t colour,
    const std::uint32_t write_mask) const noexcept
{
    if (!m_read_only && contains(x, y))
    {
        write_texel(static_cast<std::size_t>(x), static_cast<std::size_t>(y), colour, write_mask);
    }
}

void CImageView::fill_region(const std::int64_t left, const std::int64_t top,
    const std::int64_t right, const std::int64_t bottom,
    const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    if (m_read_only)
    {
        return;
    }
    const auto x_end = std::min<std::int64_t>(right, width());
    const auto y_end = std::min<std::int64_t>(bottom, height());
    for (auto y = std::max<std::int64_t>(top, 0); y < y_end; ++y)
    {
        for (auto x = std::max<std::int64_t>(left, 0); x < x_end; ++x)
        {
            write_texel(static_cast<std::size_t>(x), static_cast<std::size_t>(y), colour, write_mask);
        }
    }
}

void CImageView::fill(const std::uint32_t colour, const std::uint32_t write_mask) const noexcept
{
    fill_region(0, 0, width(), height(), colour, write_mask);
}

void CImageView::draw_line(const coordinate x0, const coordinate y0,
    const coordinate x1, const coordinate y1, const std::uint32_t colour,
    const std::uint32_t write_mask) const noexcept
{
    if (m_read_only || !is_ready())
    {
        return;
    }
    if (y0 == y1)
    {
        fill_region(std::min(x0, x1), y0, static_cast<std::int64_t>(std::max(x0, x1)) + 1,
            static_cast<std::int64_t>(y0) + 1, colour, write_mask);
        return;
    }
    if (x0 == x1)
    {
        fill_region(x0, std::min(y0, y1), static_cast<std::int64_t>(x0) + 1,
            static_cast<std::int64_t>(std::max(y0, y1)) + 1, colour, write_mask);
        return;
    }
    const std::int64_t dx = static_cast<std::int64_t>(x1) - x0;
    const std::int64_t dy = static_cast<std::int64_t>(y1) - y0;
    const auto abs_dx = static_cast<std::uint64_t>(dx < 0 ? -dx : dx);
    const auto abs_dy = static_cast<std::uint64_t>(dy < 0 ? -dy : dy);
    const bool x_major = abs_dx > abs_dy;
    std::int64_t major_start = x_major ? x0 : y0;
    std::int64_t major_finish = x_major ? x1 : y1;
    std::int64_t minor_start = x_major ? y0 : x0;
    std::int64_t minor_finish = x_major ? y1 : x1;
    if (y0 > y1)
    {
        std::swap(major_start, major_finish);
        std::swap(minor_start, minor_finish);
    }
    const auto major = x_major ? abs_dx : abs_dy;
    const auto minor = x_major ? abs_dy : abs_dx;
    const std::int64_t major_direction = major_finish > major_start ? 1 : -1;
    const std::int64_t direction = minor_finish > minor_start ? 1 : -1;
    const auto major_limit = static_cast<std::int64_t>(x_major ? width() : height());
    const auto minor_limit = static_cast<std::int64_t>(x_major ? height() : width());
    auto first = std::max<std::int64_t>(0,
        major_direction > 0 ? -major_start : major_start - (major_limit - 1));
    auto last = std::min<std::int64_t>(major,
        major_direction > 0 ? major_limit - 1 - major_start : major_start);
    const auto minor_first = std::max<std::int64_t>(0,
        direction > 0 ? -minor_start : minor_start - (minor_limit - 1));
    const auto minor_last = std::min<std::int64_t>(minor,
        direction > 0 ? minor_limit - 1 - minor_start : minor_start);
    if ((first > last) || (minor_first > minor_last))
    {
        return;
    }
    if (major == minor)
    {
        first = std::max(first, minor_first);
        last = std::min(last, minor_last);
        for (auto step = first; step <= last; ++step)
        {
            const auto a = static_cast<std::size_t>(major_start + major_direction * step);
            const auto b = static_cast<std::size_t>(minor_start + direction * step);
            write_texel(x_major ? a : b, x_major ? b : a, colour, write_mask);
        }
        return;
    }

    //  The reference pixel loop starts with error = major >> 1, subtracts minor
    //  after each pixel and advances the minor coordinate only on underflow.
    //  At major step k its minor advance count is (k * minor + bias) / major.
    //  Clip that original sequence, without replacing endpoints or restarting it.
    //  Int32 endpoints bound all products below UINT64_MAX, including at extremes.
    const auto half = major >> 1u;
    const auto bias = major - 1u - half;
    if (minor_first > 0)
    {
        const auto numerator = static_cast<std::uint64_t>(minor_first) * major - bias;
        first = std::max(first, static_cast<std::int64_t>((numerator + minor - 1u) / minor));
    }
    if (static_cast<std::uint64_t>(minor_last) < minor)
    {
        const auto numerator = (static_cast<std::uint64_t>(minor_last) + 1u) * major - bias - 1u;
        last = std::min(last, static_cast<std::int64_t>(numerator / minor));
    }
    if (first > last)
    {
        return;
    }
    auto step = static_cast<std::uint64_t>(first);
    const auto final_step = static_cast<std::uint64_t>(last);
    auto advance = (step * minor + bias) / major;
    const auto numerator = half + advance * major;
    auto run_end = numerator / minor;
    auto fraction = minor - 1u - (numerator % minor);
    const auto whole_steps = major / minor;
    const auto remainder = major % minor;
    while (step <= final_step)
    {
        const auto end = std::min(run_end, final_step);
        const auto a = major_start + major_direction * static_cast<std::int64_t>(step);
        const auto b = minor_start + direction * static_cast<std::int64_t>(advance);
        const auto z = major_start + major_direction * static_cast<std::int64_t>(end);
        const auto run_first = std::min(a, z);
        const auto run_after = std::max(a, z) + 1;
        if (x_major)
        {
            fill_region(run_first, b, run_after, b + 1, colour, write_mask);
        }
        else
        {
            fill_region(b, run_first, b + 1, run_after, colour, write_mask);
        }
        if (end == final_step)
        {
            break;
        }
        step = end + 1u;
        ++advance;
        run_end += whole_steps;
        if (fraction < remainder)
        {
            fraction += minor - remainder;
            ++run_end;
        }
        else
        {
            fraction -= remainder;
        }
    }
}

void CImageView::fill_rectangle(const coordinate x, const coordinate y,
    const coordinate width, const coordinate height, const std::uint32_t colour,
    const std::uint32_t write_mask) const noexcept
{
    const auto rect = rectangle(x, y, width, height);
    fill_region(rect.left, rect.top, rect.right, rect.bottom, colour, write_mask);
}

void CImageView::draw_rectangle(const coordinate x, const coordinate y,
    const coordinate width, const coordinate height, const std::uint32_t colour,
    const std::uint32_t write_mask) const noexcept
{
    const auto rect = rectangle(x, y, width, height);
    if ((rect.left == rect.right) || (rect.top == rect.bottom))
    {
        return;
    }
    fill_region(rect.left, rect.top, rect.right, rect.top + 1, colour, write_mask);
    if (rect.bottom - rect.top > 1)
    {
        fill_region(rect.left, rect.bottom - 1, rect.right, rect.bottom, colour, write_mask);
        fill_region(rect.left, rect.top + 1, rect.left + 1, rect.bottom - 1, colour, write_mask);
        if (rect.right - rect.left > 1)
        {
            fill_region(rect.right - 1, rect.top + 1, rect.right, rect.bottom - 1, colour, write_mask);
        }
    }
}

bool CImageView::copy_rectangle(const CImageView& source,
    const coordinate source_x, const coordinate source_y,
    const coordinate destination_x, const coordinate destination_y,
    const coordinate width, const coordinate height, const EImageCopyFlags flags,
    const std::uint32_t write_mask) const noexcept
{
    if (m_read_only || !is_ready() || !source.is_ready() ||
        (is_greyscale() != source.is_greyscale()) || (static_cast<unsigned>(flags) > 3u))
    {
        return false;
    }
    const auto src = rectangle(source_x, source_y, width, height);
    const auto dst = rectangle(destination_x, destination_y, width, height);
    const auto left = std::max({ std::int64_t{ 0 }, -src.left, -dst.left });
    const auto top = std::max({ std::int64_t{ 0 }, -src.top, -dst.top });
    const auto right = std::min({ src.right - src.left,
        static_cast<std::int64_t>(source.width()) - src.left, static_cast<std::int64_t>(this->width()) - dst.left });
    const auto bottom = std::min({ src.bottom - src.top,
        static_cast<std::int64_t>(source.height()) - src.top, static_cast<std::int64_t>(this->height()) - dst.top });
    if ((left >= right) || (top >= bottom))
    {
        return true;
    }
    const auto sx = static_cast<std::size_t>(src.left + left);
    const auto sy = static_cast<std::size_t>(src.top + top);
    const auto dx = static_cast<std::size_t>(dst.left + left);
    const auto dy = static_cast<std::size_t>(dst.top + top);
    const auto columns = static_cast<std::size_t>(right - left);
    const auto rows = static_cast<std::size_t>(bottom - top);
    const auto source_first_row = source.m_vertical_flip ? source.height() - sy - rows : sy;
    const auto destination_first_row = m_vertical_flip ? this->height() - dy - rows : dy;
    if (regions_overlap(source.m_view, sx * texel_bytes(), source_first_row,
        m_view, dx * texel_bytes(), destination_first_row, columns * texel_bytes(), rows))
    {
        return false;
    }
    const bool mirror = (static_cast<unsigned>(flags) & 1u) != 0u;
    const bool flip = (static_cast<unsigned>(flags) & 2u) != 0u;
    for (std::size_t y = 0u; y < rows; ++y)
    {
        for (std::size_t x = 0u; x < columns; ++x)
        {
            const auto tx = sx + (mirror ? columns - 1u - x : x);
            const auto ty = sy + (flip ? rows - 1u - y : y);
            write_texel(dx + x, dy + y,
                source.texel(static_cast<coordinate>(tx), static_cast<coordinate>(ty)), write_mask);
        }
    }
    return true;
}

}   //  namespace image
