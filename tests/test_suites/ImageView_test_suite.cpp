//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//  Implementation: OpenAI Codex

#include "tests/test_suites/ImageView_test_suite.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>

#include "image/image_view.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using tests::TTestContext;
using image::CImageView;
using image::EImageCopyFlags;
using Desc = CImageView::description;
using Src = CImageView::encode_source;

struct Fixture
{
    alignas(16) std::uint8_t bytes[32u * 5u];
    CImageView view;

    explicit Fixture(const bool gray, const bool inverted = false)
        : view(CByteRectView{ bytes, 32u, gray ? 6u : 24u, 5u, 16u },
            gray ? Desc::Gray : Desc::RGBA, inverted)
    {
        std::memset(bytes, 0xcd, sizeof(bytes));
        view.fill(0u);
    }

    void check_padding(TTestContext& ctx) const
    {
        for (std::size_t y = 0; y < 5u; ++y)
        {
            for (auto x = view.buffer_view().row_width(); x < 32u; ++x)
            {
                TEST_EXPECT(ctx, bytes[y * 32u + x] == 0xcdu);
            }
        }
    }
};

void test_access_and_masks(TTestContext& ctx)
{
    CImageView empty;
    TEST_EXPECT(ctx, !empty.is_ready());
    TEST_EXPECT(ctx, empty.is_read_only());
    TEST_EXPECT(ctx, empty.texel(0, 0) == 0u);
    empty.plot(0, 0, 1u);
    empty.fill(1u);

    for (const bool gray : { false, true })
    {
        Fixture f{ gray };
        TEST_EXPECT(ctx, f.view.is_ready());
        TEST_EXPECT(ctx, f.view.width() == 6u && f.view.height() == 5u);
        f.view.fill(0x12345678u);
        f.view.plot(2, 3, 0xaabbccddu, 0x00ff00ffu);
        TEST_EXPECT(ctx, f.view.texel(2, 3) == (gray ? 0xddu : 0x12bb56ddu));
        f.view.plot(2, 3, 0x98765432u, 0u);
        TEST_EXPECT(ctx, f.view.texel(2, 3) == (gray ? 0x32u : 0x12bb56ddu));
        TEST_EXPECT(ctx, f.view.texel(-1, 0) == 0u);
        TEST_EXPECT(ctx, f.view.texel(6, 0) == 0u);
        TEST_EXPECT(ctx, f.view.texel(0, 5) == 0u);
        const auto first = f.view.texel(0, 0);
        f.view.plot(-1, 0, 0u);
        f.view.plot(0, -1, 0u);
        f.view.plot(6, 0, 0u);
        f.view.plot(0, 5, 0u);
        TEST_EXPECT(ctx, f.view.texel(0, 0) == first);
        f.view.set_read_only(true);
        std::array<std::uint8_t, sizeof(f.bytes)> before;
        std::memcpy(before.data(), f.bytes, sizeof(f.bytes));
        f.view.plot(0, 0, 0u);
        f.view.fill(0u);
        f.view.fill_rectangle(0, 0, 6, 5, 0u);
        f.view.draw_rectangle(0, 0, 6, 5, 0u);
        f.view.draw_line(0, 0, 5, 4, 0u);
        TEST_EXPECT(ctx, !f.view.copy_rectangle(f.view, 0, 0, 3, 3, 1, 1));
        TEST_EXPECT(ctx, std::memcmp(before.data(), f.bytes, sizeof(f.bytes)) == 0);
        f.view.set_read_only(false);
        f.view.plot(0, 0, 0x55u);
        TEST_EXPECT(ctx, f.view.texel(0, 0) == 0x55u);

        CImageView readonly{ f.view.buffer_view(), gray ? Desc::Gray : Desc::RGBA };
        readonly.set_read_only(false);
        TEST_EXPECT(ctx, readonly.is_read_only());
        readonly.fill(0u);
        TEST_EXPECT(ctx, f.view.texel(0, 0) == 0x55u);
        f.view.set_vertical_flip(true);
        TEST_EXPECT(ctx, f.view.texel(0, 4) == 0x55u);
        f.view.plot(1, 0, 0x66u);
        f.view.set_vertical_flip(false);
        TEST_EXPECT(ctx, f.view.texel(1, 4) == 0x66u);
        f.check_padding(ctx);
    }
    alignas(8) std::uint8_t bad[64]{};
    TEST_EXPECT(ctx, (!CImageView{ CByteRectView{ bad, 8u, 7u, 2u, 8u }, Desc::RGBA }.is_ready()));
    TEST_EXPECT(ctx, (!CImageView{ CByteRectView{ bad, 9u, 8u, 2u, 8u }, Desc::RGBA }.is_ready()));
    TEST_EXPECT(ctx, (!CImageView{ CByteRectView{ bad + 1u, 8u, 8u, 2u }, Desc::RGBA }.is_ready()));
    TEST_EXPECT(ctx, (!CImageView{ CByteRectView{ bad, 8u, 8u, 2u, 8u }, static_cast<Desc>(3) }.is_ready()));
}

void test_rectangles(TTestContext& ctx)
{
    //  An independent per-pixel boundary predicate checks normalisation, clipping,
    //  degenerate outlines and the absence of invented edges at clipping bounds.
    for (const bool gray : { false, true })
    {
        for (const bool inverted : { false, true })
        {
            Fixture f{ gray, inverted };
            for (const int x : { -3, 0, 2, 6, 9 })
            for (const int y : { -2, 0, 2, 5, 8 })
            for (const int w : { -7, -1, 0, 1, 4, 9 })
            for (const int h : { -6, -1, 0, 1, 3, 8 })
            for (const bool outline : { false, true })
            {
                f.view.fill(0x11223344u);
                if (outline) f.view.draw_rectangle(x, y, w, h, 0xaabbccddu, 0x0000ff00u);
                else f.view.fill_rectangle(x, y, w, h, 0xaabbccddu, 0x0000ff00u);
                const int left = std::min(x, x + w), right = std::max(x, x + w);
                const int top = std::min(y, y + h), bottom = std::max(y, y + h);
                bool matches = true;
                for (int py = 0; py < 5; ++py)
                for (int px = 0; px < 6; ++px)
                {
                    const bool inside = px >= left && px < right && py >= top && py < bottom;
                    const bool edge = px == left || px == right - 1 || py == top || py == bottom - 1;
                    const bool written = inside && (!outline || edge);
                    const auto expected = gray ? (written ? 0xddu : 0x44u) : (written ? 0x1122cc44u : 0x11223344u);
                    matches = matches && f.view.texel(px, py) == expected;
                }
                TEST_EXPECT(ctx, matches);
            }
            f.view.fill(0u);
            f.view.fill_rectangle(std::numeric_limits<int>::max(), 0, std::numeric_limits<int>::min(), 5, 7u);
            TEST_EXPECT(ctx, f.view.texel(0, 0) == 7u && f.view.texel(5, 4) == 7u);
            f.view.fill(0u);
            f.view.draw_rectangle(std::numeric_limits<int>::min(), std::numeric_limits<int>::min(),
                std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), 7u);
            TEST_EXPECT(ctx, f.view.texel(0, 0) == 0u);
            f.check_padding(ctx);
        }
    }
}

void reference_line(bool (&pixels)[5][6], int x0, int y0, int x1, int y1)
{
    //  Deliberately walk the entire unclipped line one pixel at a time. This
    //  reference has no run grouping, analytical clipping or phase skipping.
    if (y0 > y1 || (y0 == y1 && x0 > x1))
    {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const bool x_major = dx > dy;
    const int major = std::max(dx, dy), minor = std::min(dx, dy);
    int error = major >> 1;
    const int x_step = x1 >= x0 ? 1 : -1;
    for (int i = 0; i <= major; ++i)
    {
        if (x0 >= 0 && x0 < 6 && y0 >= 0 && y0 < 5) pixels[y0][x0] = true;
        if (x_major) x0 += x_step;
        else ++y0;
        error -= minor;
        if (error < 0)
        {
            error += major;
            if (x_major) ++y0;
            else x0 += x_step;
        }
    }
}

void test_lines(TTestContext& ctx)
{
    for (const bool gray : { false, true })
    for (const bool inverted : { false, true })
    {
        Fixture actual{ gray, inverted }, reversed{ gray, inverted }, mirrored{ gray, inverted }, translated{ gray, inverted };
        constexpr std::uint32_t background = 0x11223344u, colour = 0xaabbccddu, mask = 0x00ff00ffu;
        const auto drawn = gray ? 0xddu : 0x11bb33ddu;
        const auto untouched = gray ? 0x44u : background;
        for (int y0 = -3; y0 <= 7; ++y0)
        for (int x0 = -3; x0 <= 8; ++x0)
        for (int y1 = -3; y1 <= 7; ++y1)
        for (int x1 = -3; x1 <= 8; ++x1)
        {
            bool expected[5][6]{};
            reference_line(expected, x0, y0, x1, y1);
            actual.view.fill(background);
            reversed.view.fill(background);
            mirrored.view.fill(background);
            translated.view.fill(background);
            actual.view.draw_line(x0, y0, x1, y1, colour, mask);
            reversed.view.draw_line(x1, y1, x0, y0, colour, mask);
            mirrored.view.draw_line(5 - x0, y0, 5 - x1, y1, colour, mask);
            translated.view.draw_line(x0 + 1, y0 + 1, x1 + 1, y1 + 1, colour, mask);
            bool raster_matches = true, reversal_matches = true, mirror_matches = true, translation_matches = true;
            for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 6; ++x)
            {
                const auto pixel = actual.view.texel(x, y);
                raster_matches = raster_matches && pixel == (expected[y][x] ? drawn : untouched);
                reversal_matches = reversal_matches && pixel == reversed.view.texel(x, y);
                mirror_matches = mirror_matches && pixel == mirrored.view.texel(5 - x, y);
                if (x < 5 && y < 4)
                    translation_matches = translation_matches && pixel == translated.view.texel(x + 1, y + 1);
            }
            TEST_EXPECT(ctx, raster_matches);
            TEST_EXPECT(ctx, reversal_matches);
            TEST_EXPECT(ctx, mirror_matches);
            TEST_EXPECT(ctx, translation_matches);
        }
        actual.check_padding(ctx);
        reversed.check_padding(ctx);
        mirrored.check_padding(ctx);
        translated.check_padding(ctx);
    }

    Fixture f{ true };
    //  Explicit half-pixel example, mirrored about x=2 to keep both lines visible.
    f.view.draw_line(2, 0, 4, 1, 1u);
    f.view.draw_line(2, 0, 0, 1, 2u);
    TEST_EXPECT(ctx, f.view.texel(3, 0) == 1u && f.view.texel(4, 1) == 1u);
    TEST_EXPECT(ctx, f.view.texel(1, 0) == 2u && f.view.texel(0, 1) == 2u);

    constexpr auto low = std::numeric_limits<CImageView::coordinate>::min();
    constexpr auto high = std::numeric_limits<CImageView::coordinate>::max();
    f.view.fill(0u);
    f.view.draw_line(low, 0, high, 1, 9u);
    for (int x = 0; x < 6; ++x)
        TEST_EXPECT(ctx, f.view.texel(x, 0) == 0u && f.view.texel(x, 1) == 9u);
    f.view.fill(0u);
    f.view.draw_line(high, 0, low, 1, 9u);
    for (int x = 0; x < 6; ++x)
        TEST_EXPECT(ctx, f.view.texel(x, 0) == 9u && f.view.texel(x, 1) == 0u);
    f.view.fill(0u);
    f.view.draw_line(low, low, high, high, 9u);
    for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 6; ++x)
        TEST_EXPECT(ctx, f.view.texel(x, y) == (x == y ? 9u : 0u));
    f.view.fill(0u);
    f.view.draw_line(low, low, high, high - 1, 9u);
    for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 6; ++x)
        TEST_EXPECT(ctx, f.view.texel(x, y) == (x == y + 1 ? 9u : 0u));
    f.view.fill(0u);
    f.view.draw_line(low, high, high, low, 9u);
    TEST_EXPECT(ctx, f.view.texel(0, 0) == 0u);
    f.check_padding(ctx);
}

void test_copies(TTestContext& ctx)
{
    struct Case { int sx, sy, dx, dy, width, height; };
    constexpr Case cases[]{
        { 0, 0, 0, 0, 6, 5 }, { -2, -1, 1, 0, 7, 6 }, { 1, 0, -2, -1, 7, 6 },
        { 3, 2, 1, 0, 6, 5 }, { 1, 0, 3, 2, 6, 5 }, { 6, 5, 5, 4, -6, -5 },
        { 4, 0, 5, 1, -5, 5 }, { 0, 4, 1, 3, 6, -5 }, { -8, 0, 0, 0, 3, 3 },
        { 0, 0, 0, 0, 0, 4 }, { 2, 2, 2, 2, 1, 1 }
    };
    for (const bool gray : { false, true })
    for (const bool source_inverted : { false, true })
    for (const bool destination_inverted : { false, true })
    {
        Fixture source{ gray, source_inverted }, destination{ gray, destination_inverted };
        for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 6; ++x)
            source.view.plot(x, y, 0x80504000u + static_cast<unsigned>(1 + x + 6 * y));
        for (const auto& c : cases)
        for (unsigned flags = 0; flags < 4u; ++flags)
        {
            destination.view.fill(0x123456eeu);
            std::uint32_t expected[5][6];
            for (auto& row : expected) for (auto& value : row) value = gray ? 0xeeu : 0x123456eeu;
            const int sx = c.sx + std::min(0, c.width), sy = c.sy + std::min(0, c.height);
            const int dx = c.dx + std::min(0, c.width), dy = c.dy + std::min(0, c.height);
            const int w = std::abs(c.width), h = std::abs(c.height);
            int first_x = w, last_x = -1, first_y = h, last_y = -1;
            for (int x = 0; x < w; ++x)
                if (sx + x >= 0 && sx + x < 6 && dx + x >= 0 && dx + x < 6)
                { first_x = std::min(first_x, x); last_x = x; }
            for (int y = 0; y < h; ++y)
                if (sy + y >= 0 && sy + y < 5 && dy + y >= 0 && dy + y < 5)
                { first_y = std::min(first_y, y); last_y = y; }
            for (int y = first_y; y <= last_y; ++y)
            for (int x = first_x; x <= last_x; ++x)
            {
                const int tx = sx + ((flags & 1u) ? first_x + last_x - x : x);
                const int ty = sy + ((flags & 2u) ? first_y + last_y - y : y);
                const auto pixel = source.view.texel(tx, ty);
                expected[dy + y][dx + x] = gray ? pixel : (0x123456eeu & ~0x00ff00ffu) | (pixel & 0x00ff00ffu);
            }
            TEST_EXPECT(ctx, destination.view.copy_rectangle(source.view, c.sx, c.sy, c.dx, c.dy,
                c.width, c.height, static_cast<EImageCopyFlags>(flags), 0x00ff00ffu));
            for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 6; ++x)
                TEST_EXPECT(ctx, destination.view.texel(x, y) == expected[y][x]);
        }
        source.check_padding(ctx);
        destination.check_padding(ctx);
    }
}

void test_aliasing_and_copy_failure(TTestContext& ctx)
{
    Fixture f{ true }, colour{ false };
    f.view.fill(7u);
    TEST_EXPECT(ctx, !f.view.copy_rectangle(colour.view, 0, 0, 0, 0, 2, 2));
    TEST_EXPECT(ctx, !colour.view.copy_rectangle(f.view, 0, 0, 0, 0, 2, 2));
    TEST_EXPECT(ctx, !f.view.copy_rectangle(f.view, 0, 0, 1, 1, 3, 3));
    for (unsigned flags = 0; flags < 4u; ++flags)
        TEST_EXPECT(ctx, !f.view.copy_rectangle(f.view, 0, 0, 0, 0, 6, 5, static_cast<EImageCopyFlags>(flags)));
    TEST_EXPECT(ctx, f.view.texel(0, 0) == 7u && f.view.texel(3, 3) == 7u);
    TEST_EXPECT(ctx, f.view.copy_rectangle(f.view, 0, 0, 3, 0, 3, 5));
    TEST_EXPECT(ctx, f.view.copy_rectangle(f.view, -20, 0, 0, 0, 2, 2));
    TEST_EXPECT(ctx, !f.view.copy_rectangle(f.view, 0, 0, 3, 0, 1, 1, static_cast<EImageCopyFlags>(4)));

    //  Distinct starts and different pitches still overlap on a later row.
    CImageView alias{ CByteRectView{ f.bytes + 1u, 16u, 4u, 5u }, Desc::Gray };
    TEST_EXPECT(ctx, !f.view.copy_rectangle(alias, 0, 0, 0, 0, 4, 5));
    CImageView reversed{ f.view.buffer_view(), Desc::Gray, true };
    TEST_EXPECT(ctx, !f.view.copy_rectangle(reversed, 0, 0, 0, 4, 2, 1));
    TEST_EXPECT(ctx, f.view.copy_rectangle(reversed, 0, 0, 0, 0, 2, 1));
    f.check_padding(ctx);
}

void test_tga_metadata_and_orientation(TTestContext& ctx)
{
    for (const bool gray : { false, true })
    for (const bool inverted : { false, true })
    {
        Fixture f{ gray, inverted };
        f.view.plot(0, 0, gray ? 0x12u : 0x12345678u);
        f.view.plot(5, 4, gray ? 0x34u : 0x87654321u);
        f.view.set_encoding_compression(false, false);
        auto encoded = image::codec::tga::encode(f.view.buffer_view(), f.view.encode_options());
        TEST_EXPECT(ctx, encoded.is_ready());
        if (!encoded.is_ready()) continue;
        TEST_EXPECT(ctx, encoded.data()[17] == (gray ? 0u : 8u));
        //  A TGA with a bottom-left origin emits the bottom logical row first.
        TEST_EXPECT(ctx, encoded.data()[18u] == 0u);
        Desc desc;
        auto decoded = image::codec::tga::decode(encoded.const_view(), desc, true);
        CImageView roundtrip{ decoded.view(), desc };
        TEST_EXPECT(ctx, roundtrip.is_ready());
        for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 6; ++x)
            TEST_EXPECT(ctx, roundtrip.texel(x, y) == f.view.texel(x, y));
        auto bottom_up = image::codec::tga::decode(encoded.const_view(), desc);
        CImageView bottom_up_view{ bottom_up.view(), desc, true };
        TEST_EXPECT(ctx, bottom_up_view.texel(0, 0) == f.view.texel(0, 0));
        TEST_EXPECT(ctx, bottom_up_view.texel(5, 4) == f.view.texel(5, 4));
    }

    Fixture f{ false };
    TEST_EXPECT(ctx, f.view.set_encode_source(Src::RGB));
    TEST_EXPECT(ctx, f.view.image_description() == Desc::RGBX);
    f.view.fill(0x12345678u);
    TEST_EXPECT(ctx, f.view.image_description() == Desc::RGBX);
    TEST_EXPECT(ctx, f.view.encode_options().src == Src::RGB);
    f.view.set_encoding_compression(false, false);
    auto rgb = image::codec::tga::encode(f.view.buffer_view(), f.view.encode_options());
    TEST_EXPECT(ctx, rgb.is_ready() && rgb.data()[16] == 24u);
    TEST_EXPECT(ctx, f.view.set_encode_source(Src::RGBA));
    TEST_EXPECT(ctx, f.view.image_description() == Desc::RGBA);
    auto rgba = image::codec::tga::encode(f.view.buffer_view(), f.view.encode_options());
    TEST_EXPECT(ctx, rgba.is_ready() && rgba.data()[16] == 32u);
    TEST_EXPECT(ctx, f.view.set_encode_source(Src::A));
    auto alpha = image::codec::tga::encode(f.view.buffer_view(), f.view.encode_options());
    Desc desc;
    auto decoded = image::codec::tga::decode(alpha.const_view(), desc, true);
    CImageView alpha_view{ decoded.view(), desc };
    TEST_EXPECT(ctx, alpha_view.is_greyscale() && alpha_view.texel(0, 0) == 0x12u);
    TEST_EXPECT(ctx, !f.view.set_encode_source(Src::Gray));
    Fixture gray{ true };
    TEST_EXPECT(ctx, !gray.view.set_encode_source(Src::RGBA));
    TEST_EXPECT(ctx, !gray.view.set_encode_source(static_cast<Src>(255)));
}

}   //  namespace

int run_image_view_tests()
{
    TTestContext ctx;
    test_access_and_masks(ctx);
    test_rectangles(ctx);
    test_lines(ctx);
    test_copies(ctx);
    test_aliasing_and_copy_failure(ctx);
    test_tga_metadata_and_orientation(ctx);
    std::cout << "ImageView: " << ctx.passed << " passed, " << ctx.failed << " failed; view size "
        << sizeof(CImageView) << ", encode options size " << sizeof(image::codec::tga::EncodeOptions) << '\n';
    return ctx.exit_code();
}
