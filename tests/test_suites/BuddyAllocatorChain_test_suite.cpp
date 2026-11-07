
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BuddyAllocatorChain_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    07 Nov 26
//
//  Chaining, fragmented growth, vector relocation and transactional ownership.

#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "allocators/TBuddyAllocatorChain.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/BuddyAllocatorChain_test_suite.hpp"

namespace
{

using tests::TTestContext;

template<typename TWord>
void expect_attribution(TTestContext& ctx, const TBuddyAllocatorChain<TWord>& chain, const memory::CMemoryContext& context)
{
    const memory::SMemoryAttribution attribution = chain.memory_attribution();
    TEST_EXPECT(ctx, (attribution.token_count == (chain.manager_count() + 1u)));
    TEST_EXPECT(ctx, (attribution.allocation_count == (chain.is_ready() ? (chain.manager_count() + 1u) : 0u)));
    TEST_EXPECT(ctx, (attribution.allocation_count == context.get_live_allocation_count()));
    TEST_EXPECT(ctx, (attribution.allocation_size == context.get_live_allocated_bytes()));
    TEST_EXPECT(ctx, (chain.total_owned_size() == attribution.allocation_size));
    TEST_EXPECT(ctx, (attribution.source == (chain.is_ready() ? &context : nullptr)));
    TEST_EXPECT(ctx, (attribution.source_state == (chain.is_ready() ? memory::EMemorySourceState::coherent : memory::EMemorySourceState::empty)));
}

template<typename TWord>
void test_initialisation(TTestContext& ctx, memory::CMemoryContext& context, tests::TAllocationFailureFixture& fixture)
{
    using TChain = TBuddyAllocatorChain<TWord>;
    static_assert(!std::is_copy_constructible_v<TChain>);
    static_assert(!std::is_copy_assignable_v<TChain>);
    static_assert(std::is_nothrow_move_constructible_v<TChain>);
    static_assert(std::is_nothrow_move_assignable_v<TChain>);
    TChain chain;
    std::uint8_t outside = 0u;
    TEST_EXPECT(ctx, (!chain.is_ready() && chain.is_empty()));
    TEST_EXPECT(ctx, ((chain.manager_count() == 0u) && (chain.manager_capacity() == 0u)));
    TEST_EXPECT(ctx, ((chain.slot_size() == 0u) && (chain.backing_size() == 0u)));
    TEST_EXPECT(ctx, ((chain.allocated_size() == 0u) && (chain.largest_available_block() == 0u)));
    TEST_EXPECT(ctx, (chain.allocate(1u) == nullptr));
    TEST_EXPECT(ctx, (chain.free(nullptr) == EFreeResult::no_action));
    TEST_EXPECT(ctx, (chain.free(&outside) == EFreeResult::outside_backing));
    TEST_EXPECT(ctx, (chain.allocation_size(&outside) == 0u));
    std::size_t visited = 0u;
    chain.visit_allocations([&](const void*, const std::size_t) { ++visited; });
    TEST_EXPECT(ctx, (visited == 0u));
    chain.reset();
    chain.deallocate();
    expect_attribution(ctx, chain, context);

    const std::size_t before_invalid = fixture.attempts;
    const SBuddyConfig invalid[]{
        { 0u, 64u }, { 16u, 0u }, { 17u, 31u },
        { 1u, std::size_t{ TBuddyAllocator<TWord>::k_max_slots } * 2u },
        { std::numeric_limits<std::size_t>::max(), 64u }
    };
    for (const SBuddyConfig& config : invalid)
    {
        TEST_EXPECT(ctx, !chain.initialise(config, context));
        TEST_EXPECT(ctx, !chain.is_ready());
    }
    TEST_EXPECT(ctx, (fixture.attempts == before_invalid));
    //  Initialization can fail obtaining either the manager or its vector slot.
    for (std::size_t failure = 0u; failure < 2u; ++failure)
    {
        const std::size_t attempts = fixture.attempts;
        fixture.fail_on = attempts + failure;
        fixture.failed = false;
        TEST_EXPECT(ctx, !chain.initialise({ 17u, 1000u }, context));
        TEST_EXPECT(ctx, (fixture.failed && (fixture.attempts == (attempts + failure + 1u))));
        TEST_EXPECT(ctx, (!chain.is_ready() && (chain.manager_capacity() == 0u)));
        expect_attribution(ctx, chain, context);
    }
    fixture.fail_on = std::numeric_limits<std::size_t>::max();
    TEST_EXPECT(ctx, chain.initialise({ 17u, 1000u }, context));
    if (!chain.is_ready()) return;
    TEST_EXPECT(ctx, (chain.is_empty() && (chain.manager_count() == 1u)));
    TEST_EXPECT(ctx, (chain.manager_capacity() >= 1u));
    TEST_EXPECT(ctx, ((chain.slot_size() == 32u) && (chain.backing_size() == 512u)));
    TEST_EXPECT(ctx, (chain.largest_available_block() == 512u));
    expect_attribution(ctx, chain, context);
    const std::uint64_t cost = chain.total_owned_size();
    const std::size_t attempts = fixture.attempts;
    TEST_EXPECT(ctx, !chain.initialise({ 16u, 64u }, context));
    TEST_EXPECT(ctx, (chain.allocate(0u) == nullptr));
    TEST_EXPECT(ctx, (chain.allocate(513u) == nullptr));
    TEST_EXPECT(ctx, (chain.allocate(std::numeric_limits<std::size_t>::max()) == nullptr));
    TEST_EXPECT(ctx, ((fixture.attempts == attempts) && (chain.total_owned_size() == cost)));
    TEST_EXPECT(ctx, (chain.allocate(512u) != nullptr));
    TEST_EXPECT(ctx, !chain.initialise({ 16u, 64u }, context));
    chain.deallocate();
    expect_attribution(ctx, chain, context);
    TEST_EXPECT(ctx, (!chain.is_ready() && (chain.manager_capacity() == 0u)));
    TEST_EXPECT(ctx, chain.initialise({ 16u, 64u }, context));
}

template<typename TWord>
void test_fragmentation_and_reuse(TTestContext& ctx, memory::CMemoryContext& context, tests::TAllocationFailureFixture& fixture)
{
    TBuddyAllocatorChain<TWord> chain;
    TEST_EXPECT(ctx, chain.initialise({ 16u, 64u }, context));
    std::uint8_t* slots[4]{};
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        slots[i] = static_cast<std::uint8_t*>(chain.allocate(16u));
        TEST_EXPECT(ctx, (slots[i] != nullptr));
        if (slots[i] == nullptr) return;
        TEST_EXPECT(ctx, (slots[i] == (slots[0] + (16u * i))));
    }
    TEST_EXPECT(ctx, (chain.manager_count() == 1u));
    TEST_EXPECT(ctx, (chain.free(slots[0]) == EFreeResult::released));
    TEST_EXPECT(ctx, (chain.free(slots[2]) == EFreeResult::released));
    TEST_EXPECT(ctx, ((chain.allocated_size() == 32u) && (chain.largest_available_block() == 16u)));
    //  Enough aggregate free bytes, but the 17-byte request needs a 32-byte buddy.
    std::uint8_t* const second = static_cast<std::uint8_t*>(chain.allocate(17u));
    TEST_EXPECT(ctx, (second != nullptr));
    if (second == nullptr) return;
    TEST_EXPECT(ctx, (chain.manager_count() == 2u));
    TEST_EXPECT(ctx, (chain.allocation_size(second) == 32u));
    const void* expected[]{ slots[1], slots[3], second };
    std::size_t visited = 0u;
    chain.visit_allocations([&](const void* const pointer, const std::size_t bytes)
    {
        TEST_EXPECT(ctx, (visited < 3u));
        if (visited >= 3u) return;
        TEST_EXPECT(ctx, (pointer == expected[visited]));
        TEST_EXPECT(ctx, (bytes == ((visited == 2u) ? 32u : 16u)));
        ++visited;
    });
    TEST_EXPECT(ctx, (visited == 3u));
    const std::uint64_t cost = chain.total_owned_size();
    const std::size_t attempts = fixture.attempts;
    std::uint8_t outside = 0u;
    TEST_EXPECT(ctx, (chain.free(slots[0]) == EFreeResult::invalid_allocation));
    TEST_EXPECT(ctx, (chain.free(slots[1] + 1u) == EFreeResult::invalid_allocation));
    TEST_EXPECT(ctx, (chain.free(second + 16u) == EFreeResult::invalid_allocation));
    TEST_EXPECT(ctx, (chain.free(slots[0] + 64u) == EFreeResult::outside_backing));
    TEST_EXPECT(ctx, (chain.free(&outside) == EFreeResult::outside_backing));
    TEST_EXPECT(ctx, (chain.free(nullptr) == EFreeResult::no_action));
    TEST_EXPECT(ctx, (chain.allocation_size(slots[1] + 1u) == 0u));
    TEST_EXPECT(ctx, (chain.allocation_size(second + 16u) == 0u));
    TEST_EXPECT(ctx, ((chain.allocation_count() == 3u) && (chain.allocated_size() == 64u)));
    TEST_EXPECT(ctx, ((chain.total_owned_size() == cost) && (fixture.attempts == attempts)));
    TEST_EXPECT(ctx, (chain.allocate(16u) == slots[0]));
    TEST_EXPECT(ctx, (chain.allocate(32u) == (second + 32u)));
    TEST_EXPECT(ctx, (chain.manager_count() == 2u));
    std::uint8_t* const third = static_cast<std::uint8_t*>(chain.allocate(64u));
    TEST_EXPECT(ctx, (third != nullptr));
    if (third == nullptr) return;
    TEST_EXPECT(ctx, (chain.manager_count() == 3u));
    const std::uint64_t retained = chain.total_owned_size();
    const std::size_t capacity = chain.manager_capacity();
    chain.reset();
    TEST_EXPECT(ctx, (chain.is_ready() && chain.is_empty()));
    TEST_EXPECT(ctx, ((chain.manager_count() == 3u) && (chain.manager_capacity() == capacity)));
    TEST_EXPECT(ctx, ((chain.total_owned_size() == retained) && (chain.largest_available_block() == 64u)));
    TEST_EXPECT(ctx, (chain.allocation_size(third) == 0u));
    fixture.reject_all = true;
    const std::size_t reset_attempts = fixture.attempts;
    TEST_EXPECT(ctx, (chain.allocate(64u) == slots[0]));
    TEST_EXPECT(ctx, (chain.allocate(64u) == second));
    TEST_EXPECT(ctx, (chain.allocate(64u) == third));
    TEST_EXPECT(ctx, (fixture.attempts == reset_attempts));
    TEST_EXPECT(ctx, (chain.allocate(1u) == nullptr));
    TEST_EXPECT(ctx, ((chain.manager_count() == 3u) && (chain.allocated_size() == 192u)));
    fixture.reject_all = false;
    expect_attribution(ctx, chain, context);
}

template<typename TWord>
void test_relocation_and_rollback(TTestContext& ctx, memory::CMemoryContext& context, tests::TAllocationFailureFixture& fixture)
{
    TBuddyAllocatorChain<TWord> chain;
    TEST_EXPECT(ctx, chain.initialise({ 16u, 16u }, context));
    if (!chain.is_ready()) return;
    const std::size_t capacity = chain.manager_capacity();
    std::vector<std::uint8_t*> pointers;
    for (std::size_t i = 0u; i < capacity; ++i)
    {
        std::uint8_t* const pointer = static_cast<std::uint8_t*>(chain.allocate(16u));
        TEST_EXPECT(ctx, (pointer != nullptr));
        if (pointer == nullptr) return;
        std::memset(pointer, static_cast<int>((i % 251u) + 1u), 16u);
        pointers.push_back(pointer);
    }
    TEST_EXPECT(ctx, ((chain.manager_count() == capacity) && (chain.manager_capacity() == capacity)));
    expect_attribution(ctx, chain, context);
    const std::uint64_t cost = chain.total_owned_size();
    for (std::size_t failure = 0u; failure < 2u; ++failure)
    {
        const std::size_t attempts = fixture.attempts;
        fixture.fail_on = attempts + failure;
        fixture.failed = false;
        TEST_EXPECT(ctx, (chain.allocate(16u) == nullptr));
        TEST_EXPECT(ctx, (fixture.failed && (fixture.attempts == (attempts + failure + 1u))));
        TEST_EXPECT(ctx, ((chain.manager_count() == capacity) && (chain.manager_capacity() == capacity)));
        TEST_EXPECT(ctx, ((chain.allocation_count() == capacity) && (chain.allocated_size() == (capacity * 16u))));
        TEST_EXPECT(ctx, (chain.total_owned_size() == cost));
        expect_attribution(ctx, chain, context);
        for (std::size_t i = 0u; i < pointers.size(); ++i)
        {
            TEST_EXPECT(ctx, (chain.allocation_size(pointers[i]) == 16u));
            TEST_EXPECT(ctx, ((pointers[i][0] == ((i % 251u) + 1u)) && (pointers[i][15] == pointers[i][0])));
        }
    }
    fixture.fail_on = std::numeric_limits<std::size_t>::max();
    std::uint8_t* const added = static_cast<std::uint8_t*>(chain.allocate(16u));
    TEST_EXPECT(ctx, (added != nullptr));
    if (added == nullptr) return;
    std::memset(added, static_cast<int>((capacity % 251u) + 1u), 16u);
    pointers.push_back(added);
    TEST_EXPECT(ctx, ((chain.manager_count() == (capacity + 1u)) && (chain.manager_capacity() > capacity)));
    expect_attribution(ctx, chain, context);
    std::size_t visited = 0u;
    chain.visit_allocations([&](const void* const pointer, const std::size_t bytes)
    {
        TEST_EXPECT(ctx, (visited < pointers.size()));
        if (visited >= pointers.size()) return;
        TEST_EXPECT(ctx, ((pointer == pointers[visited]) && (bytes == 16u)));
        const std::uint8_t tag = static_cast<std::uint8_t>((visited % 251u) + 1u);
        TEST_EXPECT(ctx, std::all_of(pointers[visited], (pointers[visited] + 16u), [tag](const std::uint8_t value) { return value == tag; }));
        ++visited;
    });
    TEST_EXPECT(ctx, (visited == pointers.size()));
    for (std::size_t i = pointers.size(); i != 0u; --i)
    {
        TEST_EXPECT(ctx, (chain.free(pointers[i - 1u]) == EFreeResult::released));
    }
    TEST_EXPECT(ctx, (chain.is_empty() && (chain.manager_count() == pointers.size())));
    fixture.reject_all = true;
    const std::size_t attempts = fixture.attempts;
    for (std::uint8_t* const pointer : pointers) TEST_EXPECT(ctx, (chain.allocate(16u) == pointer));
    TEST_EXPECT(ctx, (fixture.attempts == attempts));
    fixture.reject_all = false;
    expect_attribution(ctx, chain, context);
}

template<typename TWord>
void test_moves(TTestContext& ctx)
{
    using TChain = TBuddyAllocatorChain<TWord>;
    tests::TAllocationFailureFixture first_fixture;
    tests::TAllocationFailureFixture second_fixture;
    memory::CMemoryAllocator first_allocator{ &first_fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryAllocator second_allocator{ &second_fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext first_context{ first_allocator };
    memory::CMemoryContext second_context{ second_allocator };
    {
        TChain source;
        TEST_EXPECT(ctx, source.initialise({ 16u, 32u }, first_context));
        std::uint8_t* pointers[3]{};
        for (std::size_t i = 0u; i < 3u; ++i)
        {
            pointers[i] = static_cast<std::uint8_t*>(source.allocate(32u));
            TEST_EXPECT(ctx, (pointers[i] != nullptr));
            if (pointers[i] == nullptr) return;
            std::memset(pointers[i], static_cast<int>(i + 1u), 32u);
        }
        const std::uint64_t cost = source.total_owned_size();
        TChain moved{ std::move(source) };
        TEST_EXPECT(ctx, (!source.is_ready() && source.is_empty()));
        TEST_EXPECT(ctx, ((source.manager_count() == 0u) && (source.manager_capacity() == 0u)));
        TEST_EXPECT(ctx, ((source.slot_size() == 0u) && (source.backing_size() == 0u)));
        TEST_EXPECT(ctx, (source.memory_attribution().allocation_count == 0u));
        TEST_EXPECT(ctx, (source.allocation_size(pointers[0]) == 0u));
        TChain destination;
        TEST_EXPECT(ctx, destination.initialise({ 32u, 64u }, second_context));
        TEST_EXPECT(ctx, (destination.allocate(64u) != nullptr));
        first_fixture.reject_all = true;
        second_fixture.reject_all = true;
        destination = std::move(moved);
        TEST_EXPECT(ctx, (!moved.is_ready() && moved.is_empty()));
        TEST_EXPECT(ctx, second_context.is_attribution_empty());
        TEST_EXPECT(ctx, (destination.total_owned_size() == cost));
        TEST_EXPECT(ctx, ((destination.manager_count() == 3u) && (destination.allocated_size() == 96u)));
        TEST_EXPECT(ctx, ((destination.slot_size() == 16u) && (destination.backing_size() == 32u)));
        expect_attribution(ctx, destination, first_context);
        TChain& self = destination;
        destination = std::move(self);
        for (std::size_t i = 0u; i < 3u; ++i)
        {
            TEST_EXPECT(ctx, (destination.allocation_size(pointers[i]) == 32u));
            TEST_EXPECT(ctx, ((pointers[i][0] == (i + 1u)) && (pointers[i][31] == (i + 1u))));
        }
        second_fixture.reject_all = false;
        TEST_EXPECT(ctx, source.initialise({ 8u, 16u }, second_context));
        TEST_EXPECT(ctx, (source.allocate(1u) != nullptr));
        expect_attribution(ctx, source, second_context);
        destination.reset();
        for (std::uint8_t* const pointer : pointers) TEST_EXPECT(ctx, (destination.allocate(32u) == pointer));
        destination.deallocate();
        TEST_EXPECT(ctx, first_context.is_attribution_empty());
        TEST_EXPECT(ctx, (source.allocation_count() == 1u));
    }
    TEST_EXPECT(ctx, (first_context.is_attribution_empty() && second_context.is_attribution_empty()));
}

template<typename TWord>
void test_no_ambient(TTestContext& ctx, memory::CMemoryContext& context)
{
    memory::CMemoryContext* const previous_module = memory::set_module_memory_context(nullptr);
    {
        const tests::TMemoryContextScope no_thread_context{ nullptr };
        TEST_EXPECT(ctx, (memory::get_ambient_memory_context() == nullptr));
        TBuddyAllocatorChain<TWord> chain;
        TEST_EXPECT(ctx, chain.initialise({ 16u, 32u }, context));
        TEST_EXPECT(ctx, (chain.allocate(32u) != nullptr));
        TEST_EXPECT(ctx, (chain.allocate(32u) != nullptr));
        TEST_EXPECT(ctx, (chain.manager_count() == 2u));
        expect_attribution(ctx, chain, context);
        TEST_EXPECT(ctx, (memory::get_ambient_memory_context() == nullptr));
    }
    (void)memory::set_module_memory_context(previous_module);
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

template<typename TWord>
void run_word_tests(TTestContext& ctx, memory::CMemoryContext& context, tests::TAllocationFailureFixture& fixture)
{
    test_initialisation<TWord>(ctx, context, fixture);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_fragmentation_and_reuse<TWord>(ctx, context, fixture);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_relocation_and_rollback<TWord>(ctx, context, fixture);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_moves<TWord>(ctx);
    test_no_ambient<TWord>(ctx, context);
}

}   //  namespace

int run_buddy_allocator_chain_tests()
{
    TTestContext ctx;
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    tests::TAllocationFailureFixture ambient_fixture;
    ambient_fixture.reject_all = true;
    memory::CMemoryAllocator ambient_allocator{ &ambient_fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext ambient_context{ ambient_allocator };
    const tests::TMemoryContextScope ambient{ &ambient_context };
    run_word_tests<std::uint32_t>(ctx, context, fixture);
    run_word_tests<std::uint64_t>(ctx, context, fixture);
    TEST_EXPECT(ctx, (ambient_fixture.attempts == 0u));
    TEST_EXPECT(ctx, ambient_context.is_attribution_empty());
    return ctx.report("BuddyAllocatorChain");
}
