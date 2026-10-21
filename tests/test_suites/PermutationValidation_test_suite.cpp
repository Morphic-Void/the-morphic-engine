//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    PermutationValidation_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    11 Oct 26
//
//  Include first to exercise the header's own dependencies.

#include "algo/validate_permutations.hpp"
#include "tests/test_suites/PermutationValidation_test_suite.hpp"
#include "tests/support/test_context.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace
{

using TTestContext = tests::TTestContext;

template<typename T>
void test_raw_type(TTestContext& ctx)
{
    const T values[]{ 2, 0, 1 };
    const T below[]{ 0 };
    const T high[]{ 128 };
    const T duplicate[]{ 0, 1, 1 };
    const T outside[]{ 0, 1, 3 };
    const T maximum = std::numeric_limits<T>::max();
    const T near_maximum[]{ static_cast<T>(maximum - 1), static_cast<T>(maximum - 3), static_cast<T>(maximum - 2) };
    std::uint8_t scratch[1u];

    TEST_EXPECT(ctx, algo::validate_raw_permutation(values, T{ 3 }));
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(values, T{ 3 }));
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(values, T{ 3 }, scratch, sizeof(scratch)));
    scratch[0] = 0xa5;
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(values, T{ 3 }, scratch, 0u));
    TEST_EXPECT(ctx, scratch[0] == 0xa5);
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(values, T{ 3 }, nullptr, 1000u));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation(below, T{ 1 }, T{ 1 }));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation_bitmap(below, T{ 1 }, T{ 1 }));
    TEST_EXPECT(ctx, algo::validate_raw_permutation(high, T{ 1 }, T{ 128 }));
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(high, T{ 1 }, T{ 128 }));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation(duplicate, T{ 3 }));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation_bitmap(duplicate, T{ 3 }));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation_bitmap(duplicate, T{ 3 }, scratch, 0u));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation(outside, T{ 3 }));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation_bitmap(outside, T{ 3 }));
    TEST_EXPECT(ctx, algo::validate_raw_permutation(near_maximum, T{ 3 }, static_cast<T>(maximum - 3)));
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(near_maximum, T{ 3 }, static_cast<T>(maximum - 3)));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation(&maximum, T{ 1 }, maximum));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation_bitmap(&maximum, T{ 1 }, maximum));
    TEST_EXPECT(ctx, algo::validate_raw_permutation<T>(nullptr, T{ 0 }, maximum));
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap<T>(nullptr, T{ 0 }, maximum));
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap<T>(nullptr, T{ 0 }, scratch, sizeof(scratch), maximum));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation<T>(nullptr, T{ 1 }));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation_bitmap<T>(nullptr, T{ 1 }));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation_bitmap<T>(nullptr, T{ 1 }, scratch, sizeof(scratch)));
}

void test_raw_domains(TTestContext& ctx)
{
    const std::uint64_t wide[]{ std::uint64_t{ 1 } << 32u };
    std::uint8_t scratch[3u];
    TEST_EXPECT(ctx, (!algo::validate_raw_permutation<std::uint64_t, std::uint32_t>(wide, 1u)));
    TEST_EXPECT(ctx, (!algo::validate_raw_permutation_bitmap<std::uint64_t, std::uint32_t>(wide, 1u)));
    TEST_EXPECT(ctx, (!algo::validate_raw_permutation_bitmap<std::uint64_t, std::uint32_t>(wide, 1u, scratch, sizeof(scratch))));
    TEST_EXPECT(ctx, (!algo::validate_raw_permutation_bitmap<std::uint64_t, std::uint32_t>(wide, 1u, nullptr, 0u)));

    const std::uint8_t byte[]{ 255u };
    TEST_EXPECT(ctx, (algo::validate_raw_permutation<std::uint8_t, std::uint32_t>(byte, 1u, 255u)));
    TEST_EXPECT(ctx, (algo::validate_raw_permutation_bitmap<std::uint8_t, std::uint32_t>(byte, 1u, 255u)));

    std::uint32_t values[17u];
    for (std::uint32_t i = 0u; i < 17u; ++i)
    {
        values[i] = 100u + i;
    }
    TEST_EXPECT(ctx, (algo::validate_raw_permutation<std::uint32_t, std::uint32_t, 4u>(values, 16u, 100u)));
    TEST_EXPECT(ctx, (algo::validate_raw_permutation_bitmap<std::uint32_t, std::uint32_t, 4u>(values, 16u, 100u)));
    TEST_EXPECT(ctx, (!algo::validate_raw_permutation<std::uint32_t, std::uint32_t, 4u>(values, 17u, 100u)));
    TEST_EXPECT(ctx, (!algo::validate_raw_permutation_bitmap<std::uint32_t, std::uint32_t, 4u>(values, 17u, scratch, sizeof(scratch), 100u)));
    TEST_EXPECT(ctx, (algo::validate_raw_permutation<std::uint32_t, std::uint32_t, 1u>(values, 2u, 100u)));
    TEST_EXPECT(ctx, (!algo::validate_raw_permutation<std::uint32_t, std::uint32_t, 1u>(values, 3u, 100u)));

    //  Exercise the full 8-bit offset domain and the narrow counter's maximum.
    std::uint8_t full_byte_range[256u];
    for (std::size_t i = 0u; i < 256u; ++i)
    {
        full_byte_range[i] = static_cast<std::uint8_t>(i);
    }
    TEST_EXPECT(ctx, algo::validate_raw_permutation(full_byte_range, std::uint8_t{ 255 }));
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(full_byte_range, std::uint8_t{ 255 }));
    TEST_EXPECT(ctx, (algo::validate_raw_permutation<std::uint8_t, std::uint32_t>(full_byte_range, 256u)));
    TEST_EXPECT(ctx, (algo::validate_raw_permutation_bitmap<std::uint8_t, std::uint32_t>(full_byte_range, 256u)));

    //  On Win32, the bitmap must compute offsets before narrowing to size_t.
    const std::uint64_t high_base = std::uint64_t{ 1 } << 40u;
    const std::uint64_t high_values[]{ high_base + 2u, high_base, high_base + 1u };
    TEST_EXPECT(ctx, algo::validate_raw_permutation(high_values, std::uint64_t{ 3 }, high_base));
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(high_values, std::uint64_t{ 3 }, high_base));
}

void test_exhaustive_small_inputs(TTestContext& ctx)
{
    //  Independent sorted-reference oracle, including duplicates, gaps, and
    //  values below/above the requested range. No expected result uses a validator.
    bool all_match = true;
    std::size_t cases = 0u;
    for (const std::uint8_t base : { std::uint8_t{ 0 }, std::uint8_t{ 1 }, std::uint8_t{ 100 } })
    {
        for (std::uint8_t count = 0u; count <= 5u; ++count)
        {
            std::size_t possibilities = 1u;
            for (std::uint8_t i = 0u; i < count; ++i)
            {
                possibilities *= count + 2u;
            }
            for (std::size_t code = 0u; code < possibilities; ++code)
            {
                std::array<std::uint8_t, 5u> values{};
                std::size_t remaining = code;
                for (std::uint8_t i = 0u; i < count; ++i)
                {
                    values[i] = static_cast<std::uint8_t>(base + (remaining % (count + 2u)) - (base != 0u));
                    remaining /= count + 2u;
                }
                auto sorted = values;
                std::sort(sorted.begin(), sorted.begin() + count);
                bool expected = true;
                for (std::uint8_t i = 0u; i < count; ++i)
                {
                    expected &= (sorted[i] == base + i);
                }

                std::uint8_t scratch[1u]{ 0xffu };
                const auto extract = [](const std::uint8_t value) noexcept { return value; };
                all_match &= (algo::validate_raw_permutation(values.data(), count, base) == expected);
                all_match &= (algo::validate_raw_permutation_bitmap(values.data(), count, base) == expected);
                all_match &= (algo::validate_raw_permutation_bitmap(values.data(), count, scratch, sizeof(scratch), base) == expected);
                all_match &= (algo::validate_extracted_permutation(values.data(), count, extract, base) == expected);
                all_match &= (algo::validate_extracted_permutation_bitmap(values.data(), count, extract, base) == expected);
                all_match &= (algo::validate_extracted_permutation_bitmap(values.data(), count, extract, scratch, sizeof(scratch), base) == expected);
                ++cases;
            }
        }
    }
    TEST_EXPECT(ctx, all_match);
    TEST_EXPECT(ctx, cases == 54744u);
}

void test_scratch_contract(TTestContext& ctx)
{
    struct Element
    {
        std::size_t index;
        std::uint32_t payload;
    };
    const Element values[]{ { 107u, 0xabcd }, { 100u, 0xabcd }, { 108u, 0xabcd },
        { 101u, 0xabcd }, { 102u, 0xabcd }, { 103u, 0xabcd }, { 104u, 0xabcd },
        { 105u, 0xabcd }, { 106u, 0xabcd } };
    std::size_t calls = 0u;
    const auto extract = [&calls](const Element& value) noexcept { ++calls; return value.index; };
    std::uint8_t storage[]{ 0xa5, 0xff, 0xff, 0xa5 };
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap(values, 9u, extract, storage + 1u, 2u, 100u));
    TEST_EXPECT(ctx, calls == 9u);
    TEST_EXPECT(ctx, storage[0] == 0xa5 && storage[3] == 0xa5);
    //  Reuse dirty scratch; the unused suffix must be preserved even when offered.
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap(values, 9u, extract, storage + 1u, 3u, 100u));
    TEST_EXPECT(ctx, storage[3] == 0xa5);
    TEST_EXPECT(ctx, values[0].index == 107u && values[0].payload == 0xabcd);

    storage[1] = 0xa5;
    calls = 0u;
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap(values, 9u, extract, storage + 1u, 1u, 100u));
    TEST_EXPECT(ctx, calls == 81u && storage[1] == 0xa5);
    calls = 0u;
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap(values, 9u, extract, nullptr, 1000u, 100u));
    TEST_EXPECT(ctx, calls == 81u);

    calls = 0u;
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap<Element>(nullptr, 0u, extract, storage, sizeof(storage)));
    TEST_EXPECT(ctx, !algo::validate_extracted_permutation_bitmap<Element>(nullptr, 9u, extract, storage, sizeof(storage)));
    TEST_EXPECT(ctx, !algo::validate_extracted_permutation_bitmap(values, 9u, extract, storage, sizeof(storage), SIZE_MAX - 8u));
    TEST_EXPECT(ctx, calls == 0u && storage[0] == 0xa5);

    const std::size_t near_maximum[]{ SIZE_MAX - 1u, SIZE_MAX - 3u, SIZE_MAX - 2u };
    const auto identity = [](const std::size_t value) noexcept { return value; };
    TEST_EXPECT(ctx, algo::validate_extracted_permutation(near_maximum, 3u, identity, SIZE_MAX - 3u));
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap(near_maximum, 3u, identity, SIZE_MAX - 3u));
    const std::size_t invalid[]{ SIZE_MAX };
    TEST_EXPECT(ctx, !algo::validate_extracted_permutation_bitmap(invalid, 1u, identity));
}

void test_bitmap_threshold(TTestContext& ctx)
{
    std::vector<std::size_t> values(8193u);
    for (std::size_t i = 0u; i < values.size(); ++i)
    {
        values[i] = i;
    }
    std::reverse(values.begin(), values.begin() + 8192u);
    const auto original = values;
    std::size_t calls = 0u;
    const auto extract = [&calls](const std::size_t value) noexcept { ++calls; return value; };
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap(values.data(), 8192u, extract));
    TEST_EXPECT(ctx, calls == 8192u);
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(values.data(), std::size_t{ 8192 }));

    calls = 0u;
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap(values.data(), values.size(), extract));
    TEST_EXPECT(ctx, calls == 8193u * 8193u);
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(values.data(), values.size()));

    std::uint8_t scratch[1027u];
    std::fill(std::begin(scratch), std::end(scratch), std::uint8_t{ 0xa5 });
    calls = 0u;
    TEST_EXPECT(ctx, algo::validate_extracted_permutation_bitmap(values.data(), values.size(), extract, scratch + 1u, 1025u));
    TEST_EXPECT(ctx, calls == values.size());
    TEST_EXPECT(ctx, algo::validate_raw_permutation_bitmap(values.data(), values.size(), scratch + 1u, 1025u));
    TEST_EXPECT(ctx, scratch[0] == 0xa5 && scratch[1026] == 0xa5);
    TEST_EXPECT(ctx, values == original);

    //  Duplicate crosses the final bitmap-byte boundary, leaving 8192 missing.
    values.back() = 8191u;
    TEST_EXPECT(ctx, !algo::validate_extracted_permutation_bitmap(values.data(), values.size(), extract, scratch + 1u, 1025u));
    TEST_EXPECT(ctx, !algo::validate_raw_permutation_bitmap(values.data(), values.size(), scratch + 1u, 1025u));
}

}   //  namespace

int run_permutation_validation_tests()
{
    TTestContext ctx;
    test_raw_type<std::uint8_t>(ctx);
    test_raw_type<std::uint16_t>(ctx);
    test_raw_type<std::uint32_t>(ctx);
    test_raw_type<std::uint64_t>(ctx);
    test_raw_domains(ctx);
    test_exhaustive_small_inputs(ctx);
    test_scratch_contract(ctx);
    test_bitmap_threshold(ctx);
    return ctx.report("PermutationValidation");
}
