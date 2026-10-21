
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    collection_growth_helpers.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#pragma once

#ifndef MORPHIC_TEST_COLLECTION_GROWTH_HELPERS_HPP_INCLUDED
#define MORPHIC_TEST_COLLECTION_GROWTH_HELPERS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <limits>

#include "memory/memory_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace tests
{

//  Exercise growth under successive allocation allowances, including recovery and reattribution.
//  Suite wrappers retain their insertion/packing operations, lifetime assertion and failure threshold.
template<bool Pod, typename TContainer, typename TInsert, typename TGetValue, typename TPack, typename TCheckLifetime>
std::size_t exercise_failed_growth(TTestContext& ctx, const TInsert& insert, const TGetValue& get_value,
    const TPack& pack, const TCheckLifetime& check_lifetime)
{
    bool reached_success = false;
    std::size_t failures = 0u;
    for (std::size_t allowance = 0u; allowance < 8u; ++allowance)
    {
        tests::TAllocationAllowanceFixture fixture;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_allowance, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        memory::CMemoryContext destination{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            TContainer container;
            TEST_EXPECT(ctx, container.initialise(32u));
            for (std::int32_t value = 0; value < 32; ++value)
            {
                TEST_EXPECT(ctx, insert(container, value) == value);
            }
            const auto* const original = get_value(container, 0);
            const std::int32_t last_before = container.last_live();
            fixture.remaining = allowance;
            const std::int32_t added = insert(container, 32);
            fixture.remaining = std::numeric_limits<std::size_t>::max();
            TEST_EXPECT(ctx, container.is_valid());
            TEST_EXPECT(ctx, container.check_integrity());
            for (std::int32_t value = 0; value < 32; ++value)
            {
                const auto* const stored = get_value(container, value);
                TEST_EXPECT(ctx, (stored != nullptr) && (*stored == value));
            }
            if constexpr (!Pod)
            {
                TEST_EXPECT(ctx, get_value(container, 0) == original);
            }
            const auto retained_count = context.get_live_allocation_count();
            const auto retained_bytes = context.get_live_allocated_bytes();
            TEST_EXPECT(ctx, memory::reattribute(container, &destination));
            TEST_EXPECT(ctx, context.is_attribution_empty());
            TEST_EXPECT(ctx, destination.get_live_allocation_count() == retained_count);
            TEST_EXPECT(ctx, destination.get_live_allocated_bytes() == retained_bytes);
            //  Keep this after the container: restore the source context before its destruction.
            tests::TMemoryContextScope destination_scope{&destination};
            if (added < 0)
            {
                ++failures;
                TEST_EXPECT(ctx, container.last_live() == last_before);
                TEST_EXPECT(ctx, get_value(container, 32) == nullptr);
                //  Packing must also tolerate backing retained by failed metadata growth.
                pack(container);
                TEST_EXPECT(ctx, container.check_integrity());
                TEST_EXPECT(ctx, insert(container, 32) == 32);
            }
            else
            {
                reached_success = true;
                TEST_EXPECT(ctx, added == 32);
            }
            TEST_EXPECT(ctx, container.check_integrity());
            TEST_EXPECT(ctx, container.erase(0));
            pack(container);
            TEST_EXPECT(ctx, container.check_integrity());
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
        TEST_EXPECT(ctx, destination.is_attribution_empty());
        if constexpr (!Pod)
        {
            check_lifetime(ctx);
        }
        if (reached_success)
        {
            break;
        }
    }
    TEST_EXPECT(ctx, reached_success);
    return failures;
}

}   //  namespace tests

#endif  //  MORPHIC_TEST_COLLECTION_GROWTH_HELPERS_HPP_INCLUDED
