
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   ImageView_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Date:   19 Sep 26
//
//  Image-view access, drawing, clipping, copy and TGA regression tests.

#include "tests/test_suites/ImageView_test_suite.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

#include "image/image_view.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace image_view_tests
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
        for (std::uint32_t y = 0; y < 5u; ++y)
        {
            for (auto x = view.buffer_view().row_width(); x < 32u; ++x)
            {
                TEST_EXPECT(ctx, bytes[y * 32u + x] == 0xcdu);
            }
        }
    }
};

static void test_access_and_masks(TTestContext& ctx)
{
    tests::TAssertionTestScope assertions{ ctx };
    CImageView empty;
    TEST_EXPECT(ctx, !empty.is_ready());
    TEST_EXPECT(ctx, empty.set_read_only(true));
    TEST_EXPECT(ctx, !empty.set_read_only(false));
    TEST_EXPECT(ctx, empty.is_read_only());
    TEST_EXPECT(ctx, empty.texel(0, 0) == 0u);
    assertions.expect_assertion(ctx, [&] { empty.plot(0, 0, 1u); });
    assertions.expect_assertion(ctx, [&] { empty.fill(1u); });

    for (const bool gray : { false, true })
    {
        Fixture f{ gray };
        TEST_EXPECT(ctx, f.view.is_ready());
        TEST_EXPECT(ctx, f.view.set_read_only(false));
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
        TEST_EXPECT(ctx, f.view.set_read_only(true));
        TEST_EXPECT(ctx, f.view.set_read_only(true));
        CImageView writable_copy = f.view;
        TEST_EXPECT(ctx, writable_copy.set_read_only(false));
        TEST_EXPECT(ctx, !writable_copy.is_read_only() && f.view.is_read_only());
        writable_copy.plot(0, 0, 0x55u);
        TEST_EXPECT(ctx, f.view.texel(0, 0) == 0x55u);
        writable_copy.plot(0, 0, first);
        std::array<std::uint8_t, sizeof(f.bytes)> before;
        std::memcpy(before.data(), f.bytes, sizeof(f.bytes));
        assertions.expect_assertion(ctx, [&] { f.view.plot(0, 0, 0u); });
        assertions.expect_assertion(ctx, [&] { f.view.fill(0u); });
        assertions.expect_assertion(ctx, [&] { f.view.fill_rectangle(0, 0, 6, 5, 0u); });
        assertions.expect_assertion(ctx, [&] { f.view.draw_rectangle(0, 0, 6, 5, 0u); });
        assertions.expect_assertion(ctx, [&] { f.view.draw_line(0, 0, 5, 4, 0u); });
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !f.view.copy_rectangle(f.view, 0, 0, 3, 3, 1, 1)); });
        //  Degenerate/clipped requests and zero masks still constitute misuse.
        assertions.expect_assertion(ctx, [&] { f.view.plot(-1, -1, 0u, 0u); });
        assertions.expect_assertion(ctx, [&] { f.view.fill(0u, 0u); });
        assertions.expect_assertion(ctx, [&] { f.view.fill_rectangle(0, 0, 0, 0, 0u); });
        assertions.expect_assertion(ctx, [&] { f.view.draw_rectangle(0, 0, 0, 0, 0u); });
        assertions.expect_assertion(ctx, [&] { f.view.draw_line(-5, -5, -1, -1, 0u, 0u); });
        assertions.expect_assertion(ctx, [&] { TEST_EXPECT(ctx, !f.view.copy_rectangle(empty, 0, 0, 0, 0, 0, 0)); });
        TEST_EXPECT(ctx, std::memcmp(before.data(), f.bytes, sizeof(f.bytes)) == 0);
        TEST_EXPECT(ctx, f.view.set_read_only(false));
        f.view.plot(0, 0, 0x55u);
        TEST_EXPECT(ctx, f.view.texel(0, 0) == 0x55u);

        CImageView readonly{ f.view.buffer_view(), gray ? Desc::Gray : Desc::RGBA };
        TEST_EXPECT(ctx, readonly.set_read_only(true));
        TEST_EXPECT(ctx, !readonly.set_read_only(false));
        TEST_EXPECT(ctx, readonly.is_read_only());
        assertions.expect_assertion(ctx, [&] { readonly.fill(0u); });
        TEST_EXPECT(ctx, f.view.texel(0, 0) == 0x55u);
        f.view.set_vertical_flip(true);
        TEST_EXPECT(ctx, f.view.texel(0, 4) == 0x55u);
        f.view.plot(1, 0, 0x66u);
        f.view.set_vertical_flip(false);
        TEST_EXPECT(ctx, f.view.texel(1, 4) == 0x66u);
        f.check_padding(ctx);

        //  Rebinding a writable view to const storage must revoke write access.
        alignas(4) static const std::uint8_t constant_pixels[8]{ 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
        TEST_EXPECT(ctx, writable_copy.set(CByteRectConstView{ constant_pixels, 8u, gray ? 2u : 8u, 1u, 4u }, gray ? Desc::Gray : Desc::RGBA));
        TEST_EXPECT(ctx, writable_copy.set_read_only(true));
        TEST_EXPECT(ctx, !writable_copy.set_read_only(false));
        TEST_EXPECT(ctx, writable_copy.is_read_only());
        const auto constant_first = writable_copy.texel(0, 0);
        assertions.expect_assertion(ctx, [&] { writable_copy.fill(0u); });
        TEST_EXPECT(ctx, writable_copy.texel(0, 0) == constant_first);
        CImageView const_copy = writable_copy;
        TEST_EXPECT(ctx, !const_copy.set_read_only(false));
        TEST_EXPECT(ctx, const_copy.is_read_only());
        assertions.expect_assertion(ctx, [&] { const_copy.draw_line(0, 0, 1, 0, 0u); });

        TEST_EXPECT(ctx, writable_copy.set(CByteRectView{ f.bytes, 32u, gray ? 6u : 24u, 5u, 16u }, gray ? Desc::Gray : Desc::RGBA));
        TEST_EXPECT(ctx, !writable_copy.is_read_only());
        writable_copy.plot(0, 0, 0x77u);
        TEST_EXPECT(ctx, f.view.texel(0, 0) == 0x77u);
        writable_copy.reset();
        TEST_EXPECT(ctx, !writable_copy.set_read_only(false));
        TEST_EXPECT(ctx, !writable_copy.is_ready() && writable_copy.is_read_only());
    }
    alignas(8) std::uint8_t bad[64]{};
    TEST_EXPECT(ctx, (!CImageView{ CByteRectView{ bad, 8u, 7u, 2u, 8u }, Desc::RGBA }.is_ready()));
    TEST_EXPECT(ctx, (!CImageView{ CByteRectView{ bad, 9u, 8u, 2u, 8u }, Desc::RGBA }.is_ready()));
    TEST_EXPECT(ctx, (!CImageView{ CByteRectView{ bad + 1u, 8u, 8u, 2u }, Desc::RGBA }.is_ready()));
    TEST_EXPECT(ctx, (!CImageView{ CByteRectView{ bad, 8u, 8u, 2u, 8u }, static_cast<Desc>(3) }.is_ready()));
}

static void test_rectangles(TTestContext& ctx)
{
    //  An independent per-pixel boundary predicate checks normalisation, clipping,
    //  degenerate outlines and the absence of invented edges at clipping bounds.
    for (const bool gray : { false, true })
    {
        for (const bool inverted : { false, true })
        {
            Fixture f{ gray, inverted };
            for (const std::int32_t x : { -3, 0, 2, 6, 9 })
            for (const std::int32_t y : { -2, 0, 2, 5, 8 })
            for (const std::int32_t w : { -7, -1, 0, 1, 4, 9 })
            for (const std::int32_t h : { -6, -1, 0, 1, 3, 8 })
            for (const bool outline : { false, true })
            {
                f.view.fill(0x11223344u);
                if (outline) f.view.draw_rectangle(x, y, w, h, 0xaabbccddu, 0x0000ff00u);
                else f.view.fill_rectangle(x, y, w, h, 0xaabbccddu, 0x0000ff00u);
                const std::int32_t left = std::min(x, x + w), right = std::max(x, x + w);
                const std::int32_t top = std::min(y, y + h), bottom = std::max(y, y + h);
                bool matches = true;
                for (std::int32_t py = 0; py < 5; ++py)
                for (std::int32_t px = 0; px < 6; ++px)
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
            f.view.fill_rectangle(std::numeric_limits<std::int32_t>::max(), 0, std::numeric_limits<std::int32_t>::min(), 5, 7u);
            TEST_EXPECT(ctx, f.view.texel(0, 0) == 7u && f.view.texel(5, 4) == 7u);
            f.view.fill(0u);
            f.view.draw_rectangle(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min(), 7u);
            TEST_EXPECT(ctx, f.view.texel(0, 0) == 0u);
            f.check_padding(ctx);
        }
    }
}

static void reference_line(bool (&pixels)[5][6], std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1)
{
    //  Deliberately walk the entire unclipped line one pixel at a time. This
    //  reference has no run grouping, analytical clipping or phase skipping.
    if (y0 > y1 || (y0 == y1 && x0 > x1))
    {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    const std::int32_t dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const bool x_major = dx > dy;
    const std::int32_t major = std::max(dx, dy), minor = std::min(dx, dy);
    std::int32_t error = major >> 1;
    const std::int32_t x_step = x1 >= x0 ? 1 : -1;
    for (std::int32_t i = 0; i <= major; ++i)
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

static void test_lines(TTestContext& ctx)
{
    for (const bool gray : { false, true })
    for (const bool inverted : { false, true })
    {
        Fixture actual{ gray, inverted }, reversed{ gray, inverted }, mirrored{ gray, inverted }, translated{ gray, inverted };
        constexpr std::uint32_t background = 0x11223344u, colour = 0xaabbccddu, mask = 0x00ff00ffu;
        const auto drawn = gray ? 0xddu : 0x11bb33ddu;
        const auto untouched = gray ? 0x44u : background;
        for (std::int32_t y0 = -3; y0 <= 7; ++y0)
        for (std::int32_t x0 = -3; x0 <= 8; ++x0)
        for (std::int32_t y1 = -3; y1 <= 7; ++y1)
        for (std::int32_t x1 = -3; x1 <= 8; ++x1)
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
            for (std::int32_t y = 0; y < 5; ++y)
            for (std::int32_t x = 0; x < 6; ++x)
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

    constexpr auto low = std::numeric_limits<std::int32_t>::min();
    constexpr auto high = std::numeric_limits<std::int32_t>::max();
    f.view.fill(0u);
    f.view.draw_line(low, 0, high, 1, 9u);
    for (std::int32_t x = 0; x < 6; ++x)
        TEST_EXPECT(ctx, f.view.texel(x, 0) == 0u && f.view.texel(x, 1) == 9u);
    f.view.fill(0u);
    f.view.draw_line(high, 0, low, 1, 9u);
    for (std::int32_t x = 0; x < 6; ++x)
        TEST_EXPECT(ctx, f.view.texel(x, 0) == 9u && f.view.texel(x, 1) == 0u);
    f.view.fill(0u);
    f.view.draw_line(low, low, high, high, 9u);
    for (std::int32_t y = 0; y < 5; ++y)
    for (std::int32_t x = 0; x < 6; ++x)
        TEST_EXPECT(ctx, f.view.texel(x, y) == (x == y ? 9u : 0u));
    f.view.fill(0u);
    f.view.draw_line(low, low, high, high - 1, 9u);
    for (std::int32_t y = 0; y < 5; ++y)
    for (std::int32_t x = 0; x < 6; ++x)
        TEST_EXPECT(ctx, f.view.texel(x, y) == (x == y + 1 ? 9u : 0u));
    f.view.fill(0u);
    f.view.draw_line(low, high, high, low, 9u);
    TEST_EXPECT(ctx, f.view.texel(0, 0) == 0u);
    f.check_padding(ctx);
}

static void test_copies(TTestContext& ctx)
{
    struct Case { std::int32_t sx, sy, dx, dy, width, height; };
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
        for (std::int32_t y = 0; y < 5; ++y)
        for (std::int32_t x = 0; x < 6; ++x)
            source.view.plot(x, y, 0x80504000u + static_cast<std::uint32_t>(1 + x + 6 * y));
        for (const auto& c : cases)
        for (std::uint32_t flags = 0; flags < 4u; ++flags)
        {
            destination.view.fill(0x123456eeu);
            std::uint32_t expected[5][6];
            for (auto& row : expected) for (auto& value : row) value = gray ? 0xeeu : 0x123456eeu;
            const std::int32_t sx = c.sx + std::min(0, c.width), sy = c.sy + std::min(0, c.height);
            const std::int32_t dx = c.dx + std::min(0, c.width), dy = c.dy + std::min(0, c.height);
            const std::int32_t w = std::abs(c.width), h = std::abs(c.height);
            std::int32_t first_x = w, last_x = -1, first_y = h, last_y = -1;
            for (std::int32_t x = 0; x < w; ++x)
                if (sx + x >= 0 && sx + x < 6 && dx + x >= 0 && dx + x < 6)
                { first_x = std::min(first_x, x); last_x = x; }
            for (std::int32_t y = 0; y < h; ++y)
                if (sy + y >= 0 && sy + y < 5 && dy + y >= 0 && dy + y < 5)
                { first_y = std::min(first_y, y); last_y = y; }
            for (std::int32_t y = first_y; y <= last_y; ++y)
            for (std::int32_t x = first_x; x <= last_x; ++x)
            {
                const std::int32_t tx = sx + ((flags & 1u) ? first_x + last_x - x : x);
                const std::int32_t ty = sy + ((flags & 2u) ? first_y + last_y - y : y);
                const auto pixel = source.view.texel(tx, ty);
                expected[dy + y][dx + x] = gray ? pixel : (0x123456eeu & ~0x00ff00ffu) | (pixel & 0x00ff00ffu);
            }
            TEST_EXPECT(ctx, destination.view.copy_rectangle(source.view, c.sx, c.sy, c.dx, c.dy,
                c.width, c.height, static_cast<EImageCopyFlags>(flags), 0x00ff00ffu));
            for (std::int32_t y = 0; y < 5; ++y)
            for (std::int32_t x = 0; x < 6; ++x)
                TEST_EXPECT(ctx, destination.view.texel(x, y) == expected[y][x]);
        }
        source.check_padding(ctx);
        destination.check_padding(ctx);
    }
}

static void test_dimension_limits(TTestContext& ctx)
{
    constexpr std::int32_t limit = 65535;
    constexpr std::int32_t low = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t high = std::numeric_limits<std::int32_t>::max();
    for (const bool gray : { false, true })
    {
        const std::uint32_t bytes = gray ? 1u : 4u;
        const Desc desc = gray ? Desc::Gray : Desc::RGBA;
        CByteRectBuffer wide;
        TEST_EXPECT(ctx, wide.allocate((limit + 1u) * bytes, 1u, 4u));
        if (!wide.is_ready()) continue;
        CImageView rejected{ wide.view(), desc };
        TEST_EXPECT(ctx, !rejected.is_ready() && rejected.is_read_only());
        TEST_EXPECT(ctx, (!CImageView{ wide.const_view(), desc }.is_ready()));
        CImageView row{ CByteRectView{ wide.data(), wide.row_pitch(), limit * bytes, 1u, 4u }, desc };
        TEST_EXPECT(ctx, row.is_ready() && row.width() == limit);
        wide.data()[limit * bytes] = 0xcdu;
        row.draw_line(low, 0, high, 0, 0x44332211u);
        TEST_EXPECT(ctx, row.texel(0, 0) == (gray ? 0x11u : 0x44332211u));
        TEST_EXPECT(ctx, row.texel(limit - 1, 0) == row.texel(0, 0));
        TEST_EXPECT(ctx, wide.data()[limit * bytes] == 0xcdu);

        CByteRectBuffer tall;
        TEST_EXPECT(ctx, tall.allocate(bytes, limit + 1u, 4u));
        if (!tall.is_ready()) continue;
        TEST_EXPECT(ctx, !row.set(tall.view(), desc));
        TEST_EXPECT(ctx, !row.is_ready() && row.is_read_only());
        TEST_EXPECT(ctx, (!CImageView{ tall.const_view(), desc }.is_ready()));
        CImageView column{ CByteRectView{ tall.data(), tall.row_pitch(), bytes, limit, 4u }, desc, true };
        TEST_EXPECT(ctx, column.is_ready() && column.height() == limit);
        tall.row_data(limit)[0] = 0xcdu;
        column.draw_line(0, low, 0, high, 0x44332211u);
        TEST_EXPECT(ctx, column.texel(0, 0) == (gray ? 0x11u : 0x44332211u));
        TEST_EXPECT(ctx, column.texel(0, limit - 1) == column.texel(0, 0));
        TEST_EXPECT(ctx, tall.row_data(limit)[0] == 0xcdu);
    }
}

static void test_line_write_modes(TTestContext& ctx)
{
    constexpr std::int32_t endpoints[][4]{
        { 0, 0, 5, 4 }, { 5, 0, 0, 4 }, { 0, 0, 5, 1 }, { 5, 0, 0, 1 },
        { 0, 4, 1, 0 }, { 5, 4, 4, 0 }, { -4, -4, 8, 8 }, { -4, 6, 8, -6 },
        { -3, 1, 9, 1 }, { 3, -5, 3, 8 }, { 2, 2, 2, 2 }
    };
    for (const bool gray : { false, true })
    for (const bool inverted : { false, true })
    {
        Fixture f{ gray, inverted };
        for (const auto& line : endpoints)
        for (const std::uint32_t mask : { 0u, 0xffffffffu, 0x80000001u })
        {
            bool pixels[5][6]{};
            reference_line(pixels, line[0], line[1], line[2], line[3]);
            f.view.fill(0x11223344u);
            f.view.draw_line(line[0], line[1], line[2], line[3], 0xaabbccddu, mask);
            const std::uint32_t drawn = gray ? 0xddu : (0x11223344u & ~mask) | (0xaabbccddu & mask);
            for (std::int32_t y = 0; y < 5; ++y)
            for (std::int32_t x = 0; x < 6; ++x)
            {
                TEST_EXPECT(ctx, f.view.texel(x, y) == (pixels[y][x] ? drawn : (gray ? 0x44u : 0x11223344u)));
            }
        }
        f.check_padding(ctx);
    }
}

static void test_extreme_rectangles_and_copies(TTestContext& ctx)
{
    constexpr std::int32_t low = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t high = std::numeric_limits<std::int32_t>::max();
    struct SCase { std::int32_t sx, sy, dx, dy, width, height; };
    constexpr SCase cases[]{
        { high, high, high - 1, high - 2, low, low }, { 6, 5, 5, 4, low, low },
        { -4, -3, -2, -1, high, high }, { low, low, 0, 0, low, low },
        { low + 4, 0, 5, 0, high, 5 }, { 5, 0, low + 4, 0, high, 5 },
        { low + 1, 0, low + 1, 0, high, 5 }, { high, 0, high, 0, high, 5 },
        { 1, 1, 2, 2, high, high }, { 6, 5, 6, 5, -6, -5 },
        { 0, 0, 1, 1, low, low }, { high, high, high, high, 0, low }
    };
    for (const bool gray : { false, true })
    for (const bool inverted : { false, true })
    {
        Fixture src{ gray, inverted }, dst{ gray, !inverted };
        for (std::int32_t y = 0; y < 5; ++y)
        for (std::int32_t x = 0; x < 6; ++x)
            src.view.plot(x, y, static_cast<std::uint32_t>(1 + x + y * 6));
        for (const auto& c : cases)
        {
            //  Wide arithmetic is confined to this independent mathematical
            //  oracle, to verify the production code's 32-bit saturated bounds.
            const std::int64_t sx = std::min<std::int64_t>(c.sx, static_cast<std::int64_t>(c.sx) + c.width);
            const std::int64_t sy = std::min<std::int64_t>(c.sy, static_cast<std::int64_t>(c.sy) + c.height);
            const std::int64_t dx = std::min<std::int64_t>(c.dx, static_cast<std::int64_t>(c.dx) + c.width);
            const std::int64_t dy = std::min<std::int64_t>(c.dy, static_cast<std::int64_t>(c.dy) + c.height);
            const std::int64_t width = std::abs(static_cast<std::int64_t>(c.width));
            const std::int64_t height = std::abs(static_cast<std::int64_t>(c.height));
            for (const bool outline : { false, true })
            {
                dst.view.fill(0u);
                if (outline) dst.view.draw_rectangle(c.sx, c.sy, c.width, c.height, 7u);
                else dst.view.fill_rectangle(c.sx, c.sy, c.width, c.height, 7u);
                for (std::int32_t y = 0; y < 5; ++y)
                for (std::int32_t x = 0; x < 6; ++x)
                {
                    const bool inside = x >= sx && x < sx + width && y >= sy && y < sy + height;
                    const bool edge = x == sx || x == sx + width - 1 || y == sy || y == sy + height - 1;
                    TEST_EXPECT(ctx, dst.view.texel(x, y) == ((inside && (!outline || edge)) ? 7u : 0u));
                }
            }
            const std::int64_t first_x = std::max({ std::int64_t{ 0 }, -sx, -dx });
            const std::int64_t first_y = std::max({ std::int64_t{ 0 }, -sy, -dy });
            const std::int64_t after_x = std::min({ width, 6 - sx, 6 - dx });
            const std::int64_t after_y = std::min({ height, 5 - sy, 5 - dy });
            for (std::uint32_t flags = 0u; flags < 4u; ++flags)
            {
                dst.view.fill(0u);
                TEST_EXPECT(ctx, dst.view.copy_rectangle(src.view, c.sx, c.sy, c.dx, c.dy,
                    c.width, c.height, static_cast<EImageCopyFlags>(flags)));
                for (std::int32_t y = 0; y < 5; ++y)
                for (std::int32_t x = 0; x < 6; ++x)
                {
                    const std::int64_t u = x - dx, v = y - dy;
                    std::uint32_t expected = 0u;
                    if (u >= first_x && u < after_x && v >= first_y && v < after_y)
                    {
                        const std::int32_t tx = static_cast<std::int32_t>(sx + ((flags & 1u) ? first_x + after_x - 1 - u : u));
                        const std::int32_t ty = static_cast<std::int32_t>(sy + ((flags & 2u) ? first_y + after_y - 1 - v : v));
                        expected = src.view.texel(tx, ty);
                    }
                    TEST_EXPECT(ctx, dst.view.texel(x, y) == expected);
                }
            }
        }
        dst.check_padding(ctx);
    }
}

static void test_aliasing_and_copy_failure(TTestContext& ctx)
{
    Fixture f{ true }, colour{ false };
    f.view.fill(7u);
    TEST_EXPECT(ctx, !f.view.copy_rectangle(colour.view, 0, 0, 0, 0, 2, 2));
    TEST_EXPECT(ctx, !colour.view.copy_rectangle(f.view, 0, 0, 0, 0, 2, 2));
    TEST_EXPECT(ctx, !f.view.copy_rectangle(f.view, 0, 0, 1, 1, 3, 3));
    for (std::uint32_t flags = 0; flags < 4u; ++flags)
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

static void test_tga_metadata_and_orientation(TTestContext& ctx)
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
        for (std::int32_t y = 0; y < 5; ++y)
        for (std::int32_t x = 0; x < 6; ++x)
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

}   //  namespace image_view_tests

int run_image_view_tests()
{
    tests::TTestContext ctx;
    image_view_tests::test_access_and_masks(ctx);
    image_view_tests::test_rectangles(ctx);
    image_view_tests::test_lines(ctx);
    image_view_tests::test_line_write_modes(ctx);
    image_view_tests::test_dimension_limits(ctx);
    image_view_tests::test_extreme_rectangles_and_copies(ctx);
    image_view_tests::test_copies(ctx);
    image_view_tests::test_aliasing_and_copy_failure(ctx);
    image_view_tests::test_tga_metadata_and_orientation(ctx);
    std::cout << "ImageView: " << ctx.passed << " passed, " << ctx.failed << " failed; view size "
        << sizeof(image::CImageView) << ", encode options size " << sizeof(image::codec::tga::EncodeOptions) << '\n';
    return ctx.exit_code();
}
