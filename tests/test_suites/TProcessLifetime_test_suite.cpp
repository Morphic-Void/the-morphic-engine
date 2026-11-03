
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TProcessLifetime_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Inline lifetime, dependency teardown order and terminal retention at exit.

#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

#include "containers/TProcessLifetime.hpp"
#include "tests/test_suites/TProcessLifetime_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

struct SObservations
{
    unsigned constructed{ 0u };
    unsigned destroyed{ 0u };
    bool context_live{ false };
    bool valid_order{ true };
};

struct SContext
{
    explicit SContext(SObservations& state) noexcept : observations{ state }
    {
        observations.context_live = true;
        ++observations.constructed;
    }
    ~SContext() noexcept
    {
        observations.valid_order = observations.valid_order && (observations.destroyed == 1u);
        observations.context_live = false;
        ++observations.destroyed;
    }
    SObservations& observations;
};

struct alignas(128) SRuntime
{
    SRuntime(SObservations& state, const int initial_value) noexcept : observations{ state }, value{ initial_value }
    {
        observations.valid_order = observations.valid_order && observations.context_live;
        ++observations.constructed;
    }
    ~SRuntime() noexcept
    {
        observations.valid_order = observations.valid_order && observations.context_live;
        ++observations.destroyed;
    }
    SObservations& observations;
    int value;
};

struct SProcess
{
    SProcess(SObservations& state, const int value) noexcept : context{ state }, runtime{ state, value } {}
    SProcess(const SProcess&) = delete;
    SProcess& operator=(const SProcess&) = delete;
    SProcess(SProcess&&) = delete;
    SProcess& operator=(SProcess&&) = delete;

    SContext context;
    SRuntime runtime;
};

using TOwner = TProcessLifetime<SProcess>;
static_assert(!std::is_copy_constructible_v<TOwner> && !std::is_copy_assignable_v<TOwner>);
static_assert(!std::is_move_constructible_v<TOwner> && !std::is_move_assignable_v<TOwner>);
static_assert(std::is_same_v<decltype(*std::declval<const TOwner&>()), const SProcess&>);
static_assert(std::is_same_v<decltype(std::declval<const TOwner&>().operator->()), const SProcess*>);
static_assert(alignof(TOwner) >= alignof(SProcess));

void normal_destruction(tests::TTestContext& ctx)
{
    SObservations state;
    {
        TOwner owner{ state, 7 };
        TEST_EXPECT(ctx, owner.is_ready() && !owner.is_retained());
        TEST_EXPECT(ctx, state.constructed == 2u && state.destroyed == 0u && state.valid_order);
        const TOwner& constant = owner;
        TEST_EXPECT(ctx, constant->runtime.value == 7 && &*constant == owner.operator->());
        const auto begin = reinterpret_cast<std::uintptr_t>(&owner);
        const auto object = reinterpret_cast<std::uintptr_t>(owner.operator->());
        TEST_EXPECT(ctx, object >= begin && object + sizeof(SProcess) <= begin + sizeof(owner));
        TEST_EXPECT(ctx, (object % alignof(SProcess)) == 0u);
    }
    TEST_EXPECT(ctx, state.destroyed == 2u && !state.context_live && state.valid_order);
}

void explicit_destruction(tests::TTestContext& ctx)
{
    SObservations state;
    {
        TOwner owner{ state, 11 };
        TEST_EXPECT(ctx, owner.destroy());
        TEST_EXPECT(ctx, !owner.is_ready() && !owner.is_retained());
        TEST_EXPECT(ctx, state.destroyed == 2u && state.valid_order);
        TEST_EXPECT(ctx, owner.destroy());
        TEST_EXPECT(ctx, !owner.retain_until_process_exit());
    }
    TEST_EXPECT(ctx, state.destroyed == 2u && !state.context_live && state.valid_order);
}

struct SExitCheck
{
    explicit SExitCheck(const SObservations& state) noexcept : observations{ state } {}
    ~SExitCheck() noexcept
    {
        //  Registered before the retained owner, so this observes its destructor
        //  after the test runner has returned. Failure must fail the process.
        if (observations.destroyed != 0u || !observations.context_live || !observations.valid_order)
        {
            std::abort();
        }
    }
    const SObservations& observations;
};

void terminal_retention(tests::TTestContext& ctx)
{
    static SObservations state;
    static SExitCheck exit_check{ state };
    static TOwner owner{ state, 19 };
    SProcess* const object = owner.operator->();
    TEST_EXPECT(ctx, owner.retain_until_process_exit());
    TEST_EXPECT(ctx, owner.is_ready() && owner.is_retained());
    TEST_EXPECT(ctx, owner.retain_until_process_exit());
    TEST_EXPECT(ctx, !owner.destroy());
    TEST_EXPECT(ctx, owner.operator->() == object && owner->runtime.value == 19);
    TEST_EXPECT(ctx, state.constructed == 2u && state.destroyed == 0u && state.context_live && state.valid_order);
}

}   //  namespace

int run_process_lifetime_tests()
{
    tests::TTestContext ctx;
    normal_destruction(ctx);
    explicit_destruction(ctx);
    terminal_retention(ctx);
    return ctx.report("TProcessLifetime");
}
