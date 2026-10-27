
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BumpBuffer_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Date:    27 Oct 26

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

#include "containers/BumpBuffer.hpp"
#include "tests/test_suites/BumpBuffer_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{

using TTestContext = tests::TTestContext;

constexpr std::size_t k_block_size = 4096u;

void expect_statistics(TTestContext& ctx, const CBumpBuffer& buffer,
    const std::size_t count, const std::size_t size, const std::size_t aligned_size, const std::size_t buffers)
{
    TEST_EXPECT(ctx, buffer.is_valid());
    TEST_EXPECT(ctx, buffer.allocation_count() == count);
    TEST_EXPECT(ctx, buffer.allocated_size() == size);
    TEST_EXPECT(ctx, buffer.allocated_size_with_alignment() == aligned_size);
    TEST_EXPECT(ctx, buffer.buffer_count() == buffers);
}

void test_configuration_and_rejection(TTestContext& ctx, tests::TAllocationFailureFixture& fixture)
{
    static_assert(!std::is_copy_constructible_v<CBumpBuffer>);
    static_assert(!std::is_copy_assignable_v<CBumpBuffer>);
    static_assert(std::is_nothrow_move_constructible_v<CBumpBuffer>);
    static_assert(std::is_nothrow_move_assignable_v<CBumpBuffer>);

    const auto attempts = fixture.attempts;
    CBumpBuffer buffer;
    TEST_EXPECT(ctx, buffer.is_empty() && !buffer.is_ready());
    TEST_EXPECT(ctx, (buffer.block_size() == 0u) && (buffer.align() == 0u));
    TEST_EXPECT(ctx, buffer.bump(1u) == nullptr);
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 0u);
    for (const std::size_t block_size : { memory::k_byte_size_ceiling + 1u, std::numeric_limits<std::size_t>::max() })
    {
        TEST_EXPECT(ctx, !buffer.allocate(block_size));
    }
    for (const std::size_t alignment : { std::size_t{ 0u }, std::size_t{ 3u }, std::numeric_limits<std::size_t>::max() })
    {
        TEST_EXPECT(ctx, !buffer.allocate(k_block_size, alignment));
    }
    TEST_EXPECT(ctx, fixture.attempts == attempts);
    TEST_EXPECT(ctx, buffer.allocate(k_block_size, 16u));
    TEST_EXPECT(ctx, (fixture.attempts == attempts + 1u) && buffer.is_ready() && buffer.is_empty());
    TEST_EXPECT(ctx, (buffer.block_size() == k_block_size) && (buffer.align() == 16u));
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 1u);

    auto* const first = static_cast<std::uint8_t*>(buffer.bump(k_block_size));
    TEST_EXPECT(ctx, first != nullptr);
    if (first == nullptr) return;
    first[k_block_size - 1u] = 0x5au;
    const auto allocated_attempts = fixture.attempts;
    TEST_EXPECT(ctx, buffer.bump(0u) == nullptr);
    TEST_EXPECT(ctx, buffer.bump(k_block_size + 1u) == nullptr);
    TEST_EXPECT(ctx, buffer.bump(std::numeric_limits<std::size_t>::max()) == nullptr);
    TEST_EXPECT(ctx, buffer.bump(1u, 3u) == nullptr);
    TEST_EXPECT(ctx, buffer.bump(1u, 32u) == nullptr);
    TEST_EXPECT(ctx, !buffer.allocate(memory::k_byte_size_ceiling + 1u, 16u));
    TEST_EXPECT(ctx, !buffer.allocate(k_block_size, 3u));
    TEST_EXPECT(ctx, !buffer.allocate(std::numeric_limits<std::size_t>::max(), 16u));
    expect_statistics(ctx, buffer, 1u, k_block_size, k_block_size, 1u);
    TEST_EXPECT(ctx, (first[k_block_size - 1u] == 0x5au) && (fixture.attempts == allocated_attempts));
}

void test_block_size_normalisation(TTestContext& ctx, tests::TAllocationFailureFixture& fixture)
{
    const std::size_t cases[][2]{
        { 0u, 4096u }, { 1u, 4096u }, { 63u, 4096u }, { 4095u, 4096u },
        { 4096u, 4096u }, { 4097u, 8192u }, { 5000u, 8192u },
        { 8191u, 8192u }, { 8192u, 8192u }, { 8193u, 16384u }
    };
    for (const auto& test_case : cases)
    {
        CBumpBuffer buffer;
        TEST_EXPECT(ctx, buffer.allocate(test_case[0], 16u));
        TEST_EXPECT(ctx, buffer.block_size() == test_case[1]);
        expect_statistics(ctx, buffer, 0u, 0u, 0u, 1u);
        TEST_EXPECT(ctx, buffer.bump(test_case[1]) != nullptr);
        TEST_EXPECT(ctx, buffer.bump(test_case[1] + 1u) == nullptr);
        expect_statistics(ctx, buffer, 1u, test_case[1], test_case[1], 1u);
    }

    CBumpBuffer buffer;
    TEST_EXPECT(ctx, buffer.allocate(5000u, 16u));
    void* const first = buffer.bump(8192u);
    void* const second = buffer.bump(16u);
    TEST_EXPECT(ctx, (first != nullptr) && (second != nullptr));
    const auto attempts = fixture.attempts;
    fixture.reject_all = true;
    for (const std::size_t request : { 4097u, 5001u, 8191u, 8192u })
    {
        TEST_EXPECT(ctx, buffer.allocate(request, 16u));
        expect_statistics(ctx, buffer, 0u, 0u, 0u, 2u);
        TEST_EXPECT(ctx, buffer.bump(8192u) == first);
        TEST_EXPECT(ctx, buffer.bump(16u) == second);
        TEST_EXPECT(ctx, fixture.attempts == attempts);
    }
    fixture.reject_all = false;
    TEST_EXPECT(ctx, buffer.allocate(0u, 16u));
    TEST_EXPECT(ctx, buffer.block_size() == k_block_size);
    TEST_EXPECT(ctx, fixture.attempts == attempts + 1u);
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 1u);
}

void test_alignment_and_block_tails(TTestContext& ctx)
{
    CBumpBuffer buffer;
    TEST_EXPECT(ctx, buffer.allocate(k_block_size, 32u));
    auto* const first = static_cast<std::uint8_t*>(buffer.bump(3u, 1u));
    TEST_EXPECT(ctx, first != nullptr);
    if (first == nullptr) return;
    TEST_EXPECT(ctx, buffer.bump(5u, 8u) == first + 8u);
    expect_statistics(ctx, buffer, 2u, 8u, 13u, 1u);
    TEST_EXPECT(ctx, buffer.bump(1u) == first + 32u);
    TEST_EXPECT(ctx, buffer.bump(k_block_size - 33u, 1u) == first + 33u);
    expect_statistics(ctx, buffer, 4u, k_block_size - 24u, k_block_size, 1u);
    void* const second = buffer.bump(k_block_size);
    TEST_EXPECT(ctx, (second != nullptr) && (second != first));
    TEST_EXPECT(ctx, (reinterpret_cast<std::uintptr_t>(second) % 32u) == 0u);
    expect_statistics(ctx, buffer, 5u, 2u * k_block_size - 24u, 2u * k_block_size, 2u);

    buffer.reset();
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 2u);
    TEST_EXPECT(ctx, buffer.bump(k_block_size - 4u, 1u) == first);
    TEST_EXPECT(ctx, buffer.bump(8u, 1u) == second);
    expect_statistics(ctx, buffer, 2u, k_block_size + 4u, k_block_size + 4u, 2u);
    buffer.reset();
    TEST_EXPECT(ctx, buffer.bump(k_block_size - 4u, 1u) == first);
    TEST_EXPECT(ctx, buffer.bump(1u, 8u) == second);
    expect_statistics(ctx, buffer, 2u, k_block_size - 3u, k_block_size - 3u, 2u);

    //  Alignment can exceed block size: every block origin is still aligned.
    CBumpBuffer over_aligned;
    TEST_EXPECT(ctx, over_aligned.allocate(k_block_size, 2u * k_block_size));
    void* const a = over_aligned.bump(1u);
    void* const b = over_aligned.bump(k_block_size);
    TEST_EXPECT(ctx, (a != nullptr) && (b != nullptr) && (a != b));
    TEST_EXPECT(ctx, (reinterpret_cast<std::uintptr_t>(a) % (2u * k_block_size)) == 0u);
    TEST_EXPECT(ctx, (reinterpret_cast<std::uintptr_t>(b) % (2u * k_block_size)) == 0u);
    expect_statistics(ctx, over_aligned, 2u, k_block_size + 1u, k_block_size + 1u, 2u);
}

void test_growth_and_reuse(TTestContext& ctx, tests::TAllocationFailureFixture& fixture, memory::CMemoryContext& context)
{
    CBumpBuffer buffer;
    TEST_EXPECT(ctx, buffer.allocate(k_block_size, 16u));
    std::uint8_t* blocks[12]{};
    for (std::size_t i = 0u; i < 12u; ++i)
    {
        blocks[i] = static_cast<std::uint8_t*>(buffer.bump(k_block_size));
        TEST_EXPECT(ctx, blocks[i] != nullptr);
        if (blocks[i] == nullptr) return;
        std::memset(blocks[i], static_cast<int>(i + 1u), k_block_size);
    }
    //  Cross both the direct-block and directory-growth transitions.
    for (std::size_t i = 0u; i < 12u; ++i)
    {
        bool preserved = true;
        for (std::size_t j = 0u; j < k_block_size; ++j) preserved = preserved && (blocks[i][j] == i + 1u);
        TEST_EXPECT(ctx, preserved);
    }
    expect_statistics(ctx, buffer, 12u, 12u * k_block_size, 12u * k_block_size, 12u);
    const auto attribution = buffer.memory_attribution();
    TEST_EXPECT(ctx, (attribution.source == &context) && (attribution.token_count == 1u));
    TEST_EXPECT(ctx, attribution.allocation_count == context.get_live_allocation_count());
    TEST_EXPECT(ctx, attribution.allocation_size == context.get_live_allocated_bytes());

    const auto attempts = fixture.attempts;
    fixture.reject_all = true;
    for (unsigned pass = 0u; pass < 2u; ++pass)
    {
        if (pass == 0u) buffer.reset();
        else TEST_EXPECT(ctx, buffer.allocate(k_block_size, 16u));
        TEST_EXPECT(ctx, buffer.is_empty() && buffer.is_ready());
        expect_statistics(ctx, buffer, 0u, 0u, 0u, 12u);
        TEST_EXPECT(ctx, context.get_live_allocation_count() == attribution.allocation_count);
        TEST_EXPECT(ctx, context.get_live_allocated_bytes() == attribution.allocation_size);
        for (std::size_t i = 0u; i < 12u; ++i)
        {
            TEST_EXPECT(ctx, buffer.bump(k_block_size) == blocks[i]);
            TEST_EXPECT(ctx, (blocks[i][0] == i + 1u) && (blocks[i][k_block_size - 1u] == i + 1u));
        }
        TEST_EXPECT(ctx, fixture.attempts == attempts);
    }
    TEST_EXPECT(ctx, buffer.bump(1u) == nullptr);
    expect_statistics(ctx, buffer, 12u, 12u * k_block_size, 12u * k_block_size, 12u);
    fixture.reject_all = false;
    buffer.deallocate();
    TEST_EXPECT(ctx, buffer.is_empty() && !buffer.is_ready() && context.is_attribution_empty());
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 0u);
    TEST_EXPECT(ctx, (buffer.block_size() == k_block_size) && (buffer.align() == 16u));
    TEST_EXPECT(ctx, buffer.bump(1u) == nullptr);
    TEST_EXPECT(ctx, buffer.allocate(k_block_size, 16u));
    TEST_EXPECT(ctx, buffer.bump(k_block_size) != nullptr);
}

void test_reconfiguration(TTestContext& ctx, tests::TAllocationFailureFixture& fixture, memory::CMemoryContext& context)
{
    CBumpBuffer buffer;
    fixture.reject_all = true;
    TEST_EXPECT(ctx, !buffer.allocate(k_block_size, 16u));
    TEST_EXPECT(ctx, !buffer.is_ready() && (buffer.bump(1u) == nullptr));
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 0u);
    fixture.reject_all = false;
    TEST_EXPECT(ctx, buffer.allocate(k_block_size, 16u));
    TEST_EXPECT(ctx, buffer.bump(k_block_size) != nullptr);
    TEST_EXPECT(ctx, buffer.bump(k_block_size) != nullptr);

    const auto attempts = fixture.attempts;
    TEST_EXPECT(ctx, buffer.allocate(2u * k_block_size, 16u));
    TEST_EXPECT(ctx, fixture.attempts == attempts + 1u);
    TEST_EXPECT(ctx, (buffer.block_size() == 2u * k_block_size) && (buffer.align() == 16u));
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 1u);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 1u);
    TEST_EXPECT(ctx, buffer.bump(2u * k_block_size) != nullptr);

    //  Alignment changes alone are destructive, even when alignment decreases.
    TEST_EXPECT(ctx, buffer.allocate(2u * k_block_size, 8u));
    TEST_EXPECT(ctx, fixture.attempts == attempts + 2u);
    TEST_EXPECT(ctx, buffer.align() == 8u);
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 1u);
    TEST_EXPECT(ctx, buffer.bump(2u * k_block_size) != nullptr);
    fixture.reject_all = true;
    TEST_EXPECT(ctx, !buffer.allocate(k_block_size, 32u));
    TEST_EXPECT(ctx, !buffer.is_ready() && context.is_attribution_empty());
    TEST_EXPECT(ctx, (buffer.block_size() == k_block_size) && (buffer.align() == 32u));
    expect_statistics(ctx, buffer, 0u, 0u, 0u, 0u);
    TEST_EXPECT(ctx, buffer.bump(1u) == nullptr);
    fixture.reject_all = false;
    TEST_EXPECT(ctx, buffer.allocate(k_block_size, 32u));
    TEST_EXPECT(ctx, buffer.bump(k_block_size) != nullptr);
}

void test_bump_failure(TTestContext& ctx, tests::TAllocationFailureFixture& fixture, memory::CMemoryContext& context)
{
    //  Fail the directory allocation, then the new block allocation.
    for (std::size_t fail_offset = 0u; fail_offset < 2u; ++fail_offset)
    {
        CBumpBuffer buffer;
        TEST_EXPECT(ctx, buffer.allocate(k_block_size));
        auto* const first = static_cast<std::uint8_t*>(buffer.bump(k_block_size - 4u, 1u));
        TEST_EXPECT(ctx, first != nullptr);
        if (first == nullptr) return;
        std::memset(first, 0x5a, k_block_size - 4u);
        const auto allocations = context.get_live_allocation_count();
        const auto bytes = context.get_live_allocated_bytes();
        fixture.failed = false;
        fixture.fail_on = fixture.attempts + fail_offset;
        TEST_EXPECT(ctx, buffer.bump(8u, 1u) == nullptr);
        TEST_EXPECT(ctx, fixture.failed);
        expect_statistics(ctx, buffer, 1u, k_block_size - 4u, k_block_size - 4u, 1u);
        TEST_EXPECT(ctx, context.get_live_allocation_count() == allocations);
        TEST_EXPECT(ctx, context.get_live_allocated_bytes() == bytes);
        TEST_EXPECT(ctx, (first[0] == 0x5au) && (first[k_block_size - 5u] == 0x5au));
        fixture.fail_on = std::numeric_limits<std::size_t>::max();
        TEST_EXPECT(ctx, buffer.bump(4u, 1u) == first + k_block_size - 4u);
        TEST_EXPECT(ctx, buffer.bump(8u) != nullptr);
        expect_statistics(ctx, buffer, 3u, k_block_size + 8u, k_block_size + 8u, 2u);
    }
}

void test_moves(TTestContext& ctx)
{
    CBumpBuffer source;
    TEST_EXPECT(ctx, source.allocate(k_block_size, 8u));
    auto* const first = static_cast<std::uint8_t*>(source.bump(3u, 1u));
    void* const second = source.bump(5u);
    void* const third = source.bump(k_block_size - 12u);
    TEST_EXPECT(ctx, (first != nullptr) && (second != nullptr) && (third != nullptr));
    if ((first == nullptr) || (second == nullptr) || (third == nullptr)) return;
    first[0] = 0x6bu;
    CBumpBuffer moved{ std::move(source) };
    expect_statistics(ctx, source, 0u, 0u, 0u, 0u);
    expect_statistics(ctx, moved, 3u, k_block_size - 4u, k_block_size + 1u, 2u);
    TEST_EXPECT(ctx, source.allocate(k_block_size, 8u));
    TEST_EXPECT(ctx, source.bump(1u) != nullptr);

    CBumpBuffer target;
    TEST_EXPECT(ctx, target.allocate(2u * k_block_size));
    TEST_EXPECT(ctx, target.bump(2u * k_block_size) != nullptr);
    target = std::move(moved);
    expect_statistics(ctx, moved, 0u, 0u, 0u, 0u);
    expect_statistics(ctx, target, 3u, k_block_size - 4u, k_block_size + 1u, 2u);
    TEST_EXPECT(ctx, (target.block_size() == k_block_size) && (target.align() == 8u));
    CBumpBuffer& self = target;
    target = std::move(self);
    expect_statistics(ctx, target, 3u, k_block_size - 4u, k_block_size + 1u, 2u);
    TEST_EXPECT(ctx, first[0] == 0x6bu);
    target.reset();
    TEST_EXPECT(ctx, target.bump(3u, 1u) == first);
    TEST_EXPECT(ctx, target.bump(5u) == second);
    TEST_EXPECT(ctx, target.bump(k_block_size - 12u) == third);
}

}   //  namespace

int run_bump_buffer_tests()
{
    TTestContext ctx;
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        test_configuration_and_rejection(ctx, fixture);
        test_block_size_normalisation(ctx, fixture);
        test_alignment_and_block_tails(ctx);
        test_growth_and_reuse(ctx, fixture, context);
        test_reconfiguration(ctx, fixture, context);
        test_bump_failure(ctx, fixture, context);
        test_moves(ctx);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    return ctx.report("BumpBuffer");
}
