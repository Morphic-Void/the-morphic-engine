
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TInstance_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    14 Jul 26

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

#include "containers/TInstance.hpp"
#include "tests/test_suites/TInstance_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using TTestContext = tests::TTestContext;

struct TTracked
{
    TTracked(const int in_value, const std::uint32_t in_generation) noexcept
        : value(in_value)
        , generation(in_generation)
    {
        ++live_count;
        ++construction_count;
    }

    ~TTracked() noexcept
    {
        --live_count;
        ++destruction_count;
    }

    static void reset_counts() noexcept
    {
        live_count = 0;
        construction_count = 0;
        destruction_count = 0;
    }

    int value{ 0 };
    std::uint32_t generation{ 0u };

    static int live_count;
    static int construction_count;
    static int destruction_count;
};

int TTracked::live_count = 0;
int TTracked::construction_count = 0;
int TTracked::destruction_count = 0;

void test_default_state_and_constness(TTestContext& ctx)
{
    using TOwner = TInstance<int>;

    static_assert(std::is_same_v<decltype(std::declval<TOwner&>().operator->()), int*>);
    static_assert(std::is_same_v<decltype(std::declval<const TOwner&>().operator->()), const int*>);
    static_assert(std::is_same_v<decltype(*std::declval<TOwner&>()), int&>);
    static_assert(std::is_same_v<decltype(*std::declval<const TOwner&>()), const int&>);

    TOwner owner;
    TEST_EXPECT(ctx, owner.is_valid());
    TEST_EXPECT(ctx, owner.is_empty());
    TEST_EXPECT(ctx, !owner.is_ready());
    TEST_EXPECT(ctx, !owner);
}

void test_first_and_repeated_emplace(TTestContext& ctx)
{
    TTracked::reset_counts();

    TInstance<TTracked> owner;
    TEST_EXPECT(ctx, owner.emplace(7, 1u));
    TEST_EXPECT(ctx, owner.is_valid());
    TEST_EXPECT(ctx, owner.is_ready());
    TEST_EXPECT(ctx, !owner.is_empty());
    TEST_EXPECT(ctx, owner->value == 7);
    TEST_EXPECT(ctx, owner->generation == 1u);
    TEST_EXPECT(ctx, TTracked::live_count == 1);
    TEST_EXPECT(ctx, TTracked::construction_count == 1);
    TEST_EXPECT(ctx, TTracked::destruction_count == 0);

    TTracked* const first_address = owner.operator->();
    TEST_EXPECT(ctx, owner.emplace(9, 2u));
    TEST_EXPECT(ctx, owner.operator->() == first_address);
    TEST_EXPECT(ctx, owner->value == 9);
    TEST_EXPECT(ctx, owner->generation == 2u);
    TEST_EXPECT(ctx, TTracked::live_count == 1);
    TEST_EXPECT(ctx, TTracked::construction_count == 2);
    TEST_EXPECT(ctx, TTracked::destruction_count == 1);
}

void test_reset_and_destruction(TTestContext& ctx)
{
    TTracked::reset_counts();

    {
        TInstance<TTracked> owner = TInstance<TTracked>::create(11, 3u);
        TEST_EXPECT(ctx, owner.is_valid());
        TEST_EXPECT(ctx, owner.is_ready());
        TEST_EXPECT(ctx, owner->value == 11);
        TEST_EXPECT(ctx, TTracked::live_count == 1);

        owner.reset();
        TEST_EXPECT(ctx, owner.is_valid());
        TEST_EXPECT(ctx, owner.is_empty());
        TEST_EXPECT(ctx, !owner.is_ready());
        TEST_EXPECT(ctx, TTracked::live_count == 0);
        TEST_EXPECT(ctx, TTracked::construction_count == 1);
        TEST_EXPECT(ctx, TTracked::destruction_count == 1);

        TEST_EXPECT(ctx, owner.emplace(12, 4u));
        TEST_EXPECT(ctx, owner->value == 12);
        TEST_EXPECT(ctx, TTracked::live_count == 1);
    }

    TEST_EXPECT(ctx, TTracked::live_count == 0);
    TEST_EXPECT(ctx, TTracked::construction_count == 2);
    TEST_EXPECT(ctx, TTracked::destruction_count == 2);
}

void test_move_construction_and_assignment(TTestContext& ctx)
{
    TTracked::reset_counts();

    TInstance<TTracked> source = TInstance<TTracked>::create(21, 5u);
    TTracked* const source_address = source.operator->();

    TInstance<TTracked> moved{ std::move(source) };
    TEST_EXPECT(ctx, moved.is_valid());
    TEST_EXPECT(ctx, moved.is_ready());
    TEST_EXPECT(ctx, moved.operator->() == source_address);
    TEST_EXPECT(ctx, moved->value == 21);
    TEST_EXPECT(ctx, source.is_valid());
    TEST_EXPECT(ctx, source.is_empty());
    TEST_EXPECT(ctx, !source.is_ready());
    TEST_EXPECT(ctx, TTracked::live_count == 1);
    TEST_EXPECT(ctx, TTracked::construction_count == 1);
    TEST_EXPECT(ctx, TTracked::destruction_count == 0);

    TInstance<TTracked> destination = TInstance<TTracked>::create(99, 6u);
    TEST_EXPECT(ctx, TTracked::live_count == 2);

    destination = std::move(moved);
    TEST_EXPECT(ctx, destination.is_valid());
    TEST_EXPECT(ctx, destination.is_ready());
    TEST_EXPECT(ctx, destination.operator->() == source_address);
    TEST_EXPECT(ctx, destination->value == 21);
    TEST_EXPECT(ctx, moved.is_valid());
    TEST_EXPECT(ctx, moved.is_empty());
    TEST_EXPECT(ctx, !moved.is_ready());
    TEST_EXPECT(ctx, TTracked::live_count == 1);
    TEST_EXPECT(ctx, TTracked::construction_count == 2);
    TEST_EXPECT(ctx, TTracked::destruction_count == 1);
}

void test_moved_from_reuse(TTestContext& ctx)
{
    TTracked::reset_counts();

    TInstance<TTracked> source = TInstance<TTracked>::create(31, 7u);
    TInstance<TTracked> destination{ std::move(source) };

    TEST_EXPECT(ctx, source.emplace(32, 8u));
    TEST_EXPECT(ctx, source.is_valid());
    TEST_EXPECT(ctx, source.is_ready());
    TEST_EXPECT(ctx, source->value == 32);
    TEST_EXPECT(ctx, destination.is_valid());
    TEST_EXPECT(ctx, destination.is_ready());
    TEST_EXPECT(ctx, destination->value == 31);
    TEST_EXPECT(ctx, TTracked::live_count == 2);

    TInstance<TTracked> assigned;
    assigned = std::move(destination);
    TEST_EXPECT(ctx, destination.is_valid());
    TEST_EXPECT(ctx, destination.is_empty());
    TEST_EXPECT(ctx, destination.emplace(33, 9u));
    TEST_EXPECT(ctx, destination->value == 33);
    TEST_EXPECT(ctx, assigned->value == 31);
    TEST_EXPECT(ctx, TTracked::live_count == 3);
}

}   //  namespace

int run_instance_tests()
{
    TTestContext ctx;
    test_default_state_and_constness(ctx);
    test_first_and_repeated_emplace(ctx);
    test_reset_and_destruction(ctx);
    test_move_construction_and_assignment(ctx);
    test_moved_from_reuse(ctx);

    return ctx.report("TInstance");
}

