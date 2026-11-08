
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HybridAllocator_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    08 Nov 26
//
//  Routing boundaries, lazy ownership, checked frees, failures and moves.

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "allocators/THybridAllocator.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "HybridAllocator_test_suite.hpp"

namespace
{

using tests::TTestContext;

struct SExpectedVisit
{
    const void* pointer;
    std::size_t extent, alignment;
    bool buddy;
};

template<typename TWord>
void check_visit_sequence(TTestContext& ctx, const THybridAllocator<TWord>& owner,
    const std::vector<SExpectedVisit>& expected)
{
    using TOwner = THybridAllocator<TWord>;
    std::size_t index = 0u;
    owner.visit_allocations([&](const void* const pointer, const std::size_t extent,
        const std::size_t alignment, const typename TOwner::EAllocationKind kind)
    {
        TEST_EXPECT(ctx, (index < expected.size()));
        if (index < expected.size())
        {
            const SExpectedVisit& entry = expected[index];
            TEST_EXPECT(ctx, (pointer == entry.pointer));
            TEST_EXPECT(ctx, (extent == entry.extent));
            TEST_EXPECT(ctx, (alignment == entry.alignment));
            TEST_EXPECT(ctx, (kind == (entry.buddy ? TOwner::EAllocationKind::buddy : TOwner::EAllocationKind::tracked)));
        }
        ++index;
    });
    TEST_EXPECT(ctx, (index == expected.size()));
}

template<typename TWord>
void check_accounting(TTestContext& ctx, const THybridAllocator<TWord>& owner, const memory::CMemoryContext& context)
{
    const memory::SMemoryAttribution attribution = owner.memory_attribution();
    TEST_EXPECT(ctx, (attribution.allocation_size == context.get_live_allocated_bytes()));
    TEST_EXPECT(ctx, (attribution.allocation_count == context.get_live_allocation_count()));
    TEST_EXPECT(ctx, (owner.total_owned_size() == attribution.allocation_size));
    TEST_EXPECT(ctx, (attribution.source == ((attribution.allocation_count != 0u) ? &context : nullptr)));
    std::uint64_t count = 0u, bytes = 0u;
    owner.visit_allocations([&](const void* const pointer, const std::size_t extent,
        const std::size_t alignment, const typename THybridAllocator<TWord>::EAllocationKind kind)
    {
        TEST_EXPECT(ctx, (pointer != nullptr));
        TEST_EXPECT(ctx, (extent == owner.allocation_size(pointer)));
        TEST_EXPECT(ctx, (alignment == owner.allocation_alignment(pointer)));
        TEST_EXPECT(ctx, (kind == owner.allocation_kind(pointer)));
        TEST_EXPECT(ctx, ((alignment != 0u) && ((reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0u)));
        ++count;
        bytes += extent;
    });
    TEST_EXPECT(ctx, (count == owner.allocation_count()));
    TEST_EXPECT(ctx, (bytes == owner.allocated_size()));
}

template<typename TWord>
void test_configuration(TTestContext& ctx, memory::CMemoryContext& context, tests::TAllocationFailureFixture& failure)
{
    using TOwner = THybridAllocator<TWord>;
    static_assert(!std::is_copy_constructible_v<TOwner>);
    static_assert(!std::is_copy_assignable_v<TOwner>);
    static_assert(std::is_nothrow_move_constructible_v<TOwner>);
    static_assert(std::is_nothrow_move_assignable_v<TOwner>);
    TOwner owner;
    std::uint8_t foreign = 0u;
    TEST_EXPECT(ctx, (!owner.is_ready() && owner.is_empty()));
    TEST_EXPECT(ctx, (owner.allocate(1u) == nullptr));
    TEST_EXPECT(ctx, (owner.free(nullptr) == TOwner::EFreeResult::no_action));
    TEST_EXPECT(ctx, (owner.free(&foreign) == TOwner::EFreeResult::invalid_allocation));
    TEST_EXPECT(ctx, (owner.allocation_alignment(&foreign) == 0u));
    TEST_EXPECT(ctx, (owner.allocation_kind(&foreign) == TOwner::EAllocationKind::none));
    owner.reset();
    owner.deallocate();
    check_accounting(ctx, owner, context);
    const std::size_t attempts = failure.attempts;
    const SHybridConfig invalid[]{
        { { 0u, 1024u }, 128u }, { { 8u, 0u }, 0u }, { { 17u, 31u }, 16u },
        { { 8u, 1000u }, 513u },
        { { 1u, std::size_t{ TBuddyAllocator<TWord>::k_max_slots } * 2u }, 128u },
        { { std::numeric_limits<std::size_t>::max(), 1024u }, 128u },
        { { 4096u, memory::k_byte_size_ceiling }, 128u }
    };
    for (const SHybridConfig& config : invalid)
    {
        TEST_EXPECT(ctx, !owner.initialise(config, context));
        TEST_EXPECT(ctx, !owner.is_ready());
    }
    memory::CMemoryAllocator unavailable{ nullptr, nullptr, nullptr };
    memory::CMemoryContext unusable{ unavailable };
    TEST_EXPECT(ctx, !owner.initialise({ { 8u, 1024u }, 128u }, unusable));
    //  Resolving configuration must neither allocate nor modify output on failure.
    SBuddyConfig resolved{ 123u, 456u };
    TEST_EXPECT(ctx, !TBuddyAllocator<TWord>::resolve_config({ 0u, 1024u }, context, resolved));
    TEST_EXPECT(ctx, ((resolved.minimum_slot_size == 123u) && (resolved.backing_size_limit == 456u)));
    failure.reject_all = true;
    TEST_EXPECT(ctx, owner.initialise({ { 17u, 1000u }, 128u }, context));
    TEST_EXPECT(ctx, (owner.is_ready() && owner.is_empty()));
    TEST_EXPECT(ctx, ((owner.config().buddy.minimum_slot_size == 32u) && (owner.config().buddy.backing_size_limit == 512u)));
    TEST_EXPECT(ctx, (owner.config().buddy_threshold == 128u));
    TEST_EXPECT(ctx, !owner.initialise({ { 8u, 256u }, 128u }, context));
    TEST_EXPECT(ctx, (failure.attempts == attempts));
    check_accounting(ctx, owner, context);
    const std::size_t bad[][2]{ { 0u, 8u }, { 1u, 0u }, { 1u, 3u },
        { std::numeric_limits<std::size_t>::max(), 8u }, { 1u, std::numeric_limits<std::size_t>::max() },
        { memory::k_byte_size_ceiling + 1u, 8u } };
    for (const auto& request : bad)
    {
        TEST_EXPECT(ctx, (owner.allocate(request[0], request[1]) == nullptr));
    }
    TEST_EXPECT(ctx, (failure.attempts == attempts));
    failure.reject_all = false;
    owner.deallocate();
    TEST_EXPECT(ctx, (!owner.is_ready() && (owner.config().buddy_threshold == 0u)));
}

template<typename TWord>
void test_routing(TTestContext& ctx, memory::CMemoryContext& context)
{
    using TOwner = THybridAllocator<TWord>;
    struct SCase { std::size_t bytes, alignment, extent; bool buddy; };
    const SCase cases[]{ { 1u, 1u, 8u, true }, { 1u, 16u, 16u, true },
        { 17u, 8u, 32u, true }, { 1u, 128u, 128u, true },
        { 128u, 8u, 128u, true }, { 129u, 8u, 129u, false },
        { 1u, 256u, 1u, false }, { 2048u, 8u, 2048u, false } };
    for (const SCase& request : cases)
    {
        TOwner owner;
        TEST_EXPECT(ctx, owner.initialise({ { 8u, 1024u }, 128u }, context));
        void* const pointer = owner.allocate(request.bytes, request.alignment);
        TEST_EXPECT(ctx, (pointer != nullptr));
        if (pointer == nullptr) continue;
        TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(pointer) % request.alignment) == 0u));
        TEST_EXPECT(ctx, (owner.allocation_size(pointer) == request.extent));
        TEST_EXPECT(ctx, (owner.allocation_kind(pointer) == (request.buddy ? TOwner::EAllocationKind::buddy : TOwner::EAllocationKind::tracked)));
        TEST_EXPECT(ctx, (owner.manager_count() == (request.buddy ? 1u : 0u)));
        TEST_EXPECT(ctx, (owner.tracked_allocation_count() == (request.buddy ? 0u : 1u)));
        std::memset(pointer, 0x5a, request.bytes);
        check_accounting(ctx, owner, context);
        TEST_EXPECT(ctx, (owner.free(pointer) == TOwner::EFreeResult::released));
        TEST_EXPECT(ctx, (owner.free(pointer) == TOwner::EFreeResult::invalid_allocation));
        TEST_EXPECT(ctx, (owner.allocation_size(pointer) == 0u));
        TEST_EXPECT(ctx, (owner.allocation_alignment(pointer) == 0u));
        TEST_EXPECT(ctx, (owner.allocation_kind(pointer) == TOwner::EAllocationKind::none));
    }
    for (const std::size_t threshold : { 0u, 7u, 100u })
    {
        TOwner owner;
        TEST_EXPECT(ctx, owner.initialise({ { 8u, 256u }, threshold }, context));
        void* const pointer = owner.allocate(65u, 1u);
        TEST_EXPECT(ctx, (pointer != nullptr));
        TEST_EXPECT(ctx, (owner.allocation_kind(pointer) == TOwner::EAllocationKind::tracked));
        TEST_EXPECT(ctx, (owner.manager_count() == 0u));
        void* const promoted = owner.allocate(1u, 128u);
        TEST_EXPECT(ctx, (promoted != nullptr));
        TEST_EXPECT(ctx, (owner.allocation_kind(promoted) == TOwner::EAllocationKind::tracked));
        TEST_EXPECT(ctx, (owner.manager_count() == 0u));
    }
    {
        //  Size fits the threshold, but alignment exceeds this payload guarantee.
        TOwner owner;
        TEST_EXPECT(ctx, owner.initialise({ { 8u, 1024u }, 512u }, context));
        void* const pointer = owner.allocate(1u, 256u);
        TEST_EXPECT(ctx, (pointer != nullptr));
        TEST_EXPECT(ctx, (owner.allocation_kind(pointer) == TOwner::EAllocationKind::tracked));
        TEST_EXPECT(ctx, (owner.manager_count() == 0u));
        TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(pointer) % 256u) == 0u));
    }
    //  A larger minimum slot raises the maximum supported buddy alignment.
    TOwner wide;
    TEST_EXPECT(ctx, wide.initialise({ { 256u, 1024u }, 512u }, context));
    void* const pointer = wide.allocate(1u, 256u);
    TEST_EXPECT(ctx, (pointer != nullptr));
    TEST_EXPECT(ctx, (wide.allocation_kind(pointer) == TOwner::EAllocationKind::buddy));
    TEST_EXPECT(ctx, (wide.allocation_alignment(pointer) == 256u));
}

template<typename TWord>
void test_lifetimes(TTestContext& ctx, memory::CMemoryContext& context)
{
    using TOwner = THybridAllocator<TWord>;
    TOwner owner;
    TEST_EXPECT(ctx, owner.initialise({ { 8u, 256u }, 128u }, context));
    std::vector<void*> pointers;
    std::uint64_t expected_size = 0u;
    for (std::size_t i = 0u; i < 90u; ++i)
    {
        const std::size_t size = ((i % 3u) == 0u) ? 65u : 129u;
        void* const pointer = owner.allocate(size, 8u);
        TEST_EXPECT(ctx, (pointer != nullptr));
        if (pointer == nullptr) return;
        std::memset(pointer, static_cast<int>(i + 1u), size);
        pointers.push_back(pointer);
        expected_size += (size == 65u) ? 128u : 129u;
    }
    TEST_EXPECT(ctx, (owner.manager_count() == 15u));
    TEST_EXPECT(ctx, (owner.tracked_allocation_count() == 60u));
    TEST_EXPECT(ctx, (owner.allocated_size() == expected_size));
    //  Build the oracle from issued requests and returned pointers, not owner
    //  queries. Equal buddy extents fill each manager in address order before
    //  creating the next manager. Tracked allocations follow numeric address order.
    const auto check_live_visits = [&](const bool skip_even)
    {
        std::vector<SExpectedVisit> expected, tracked;
        for (std::size_t i = 0u; i < pointers.size(); ++i)
        {
            if (skip_even && ((i % 2u) == 0u))
            {
                continue;
            }
            if ((i % 3u) == 0u) expected.push_back({ pointers[i], 128u, 128u, true });
            else tracked.push_back({ pointers[i], 129u, 8u, false });
        }
        std::sort(tracked.begin(), tracked.end(), [](const SExpectedVisit& left, const SExpectedVisit& right)
        {
            return reinterpret_cast<std::uintptr_t>(left.pointer) < reinterpret_cast<std::uintptr_t>(right.pointer);
        });
        expected.insert(expected.end(), tracked.begin(), tracked.end());
        check_visit_sequence(ctx, owner, expected);
    };
    check_live_visits(false);
    std::uint8_t foreign = 0u;
    const std::uint64_t retained = owner.total_owned_size();
    TEST_EXPECT(ctx, (owner.free(&foreign) == TOwner::EFreeResult::invalid_allocation));
    for (std::size_t i = 0u; i < pointers.size(); ++i)
    {
        std::uint8_t* const bytes = static_cast<std::uint8_t*>(pointers[i]);
        TEST_EXPECT(ctx, (bytes[0] == (i + 1u)));
        TEST_EXPECT(ctx, (owner.free(bytes + 1u) == TOwner::EFreeResult::invalid_allocation));
        TEST_EXPECT(ctx, (owner.allocation_kind(bytes + 1u) == TOwner::EAllocationKind::none));
    }
    TEST_EXPECT(ctx, (owner.total_owned_size() == retained));
    TEST_EXPECT(ctx, (owner.allocation_count() == 90u));
    for (std::size_t i = 0u; i < pointers.size(); i += 2u)
    {
        TEST_EXPECT(ctx, (owner.free(pointers[i]) == TOwner::EFreeResult::released));
    }
    check_live_visits(true);
    check_accounting(ctx, owner, context);
    owner.reset();
    TEST_EXPECT(ctx, (owner.is_ready() && owner.is_empty()));
    TEST_EXPECT(ctx, (owner.manager_count() == 15u));
    TEST_EXPECT(ctx, (owner.tracked_allocation_count() == 0u));
    TEST_EXPECT(ctx, (owner.total_owned_size() < retained));
    check_visit_sequence(ctx, owner, {});
    for (void* const pointer : pointers)
    {
        TEST_EXPECT(ctx, (owner.free(pointer) == TOwner::EFreeResult::invalid_allocation));
    }
    TEST_EXPECT(ctx, (owner.allocate(65u, 8u) == pointers[0]));
    void* const reused_tracked = owner.allocate(129u, 8u);
    TEST_EXPECT(ctx, (reused_tracked != nullptr));
    check_visit_sequence(ctx, owner, { { pointers[0], 128u, 128u, true }, { reused_tracked, 129u, 8u, false } });
    check_accounting(ctx, owner, context);
    owner.deallocate();
    TEST_EXPECT(ctx, !owner.is_ready());
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

template<typename TWord>
void test_failures(TTestContext& ctx)
{
    using TOwner = THybridAllocator<TWord>;
    for (const std::size_t request : { 65u, 129u })
    {
        std::size_t attempts = 0u;
        for (std::size_t fail_on = 0u; ; ++fail_on)
        {
            tests::TAllocationFailureFixture failure;
            memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            {
                TOwner owner;
                TEST_EXPECT(ctx, owner.initialise({ { 8u, 256u }, 128u }, context));
                //  First obtain the successful path's physical allocation count.
                failure.fail_on = (fail_on == 0u) ? std::numeric_limits<std::size_t>::max() : (fail_on - 1u);
                void* const pointer = owner.allocate(request, 8u);
                if (fail_on == 0u)
                {
                    attempts = failure.attempts;
                    TEST_EXPECT(ctx, (pointer != nullptr));
                    TEST_EXPECT(ctx, (attempts != 0u));
                }
                else
                {
                    TEST_EXPECT(ctx, (failure.failed && (pointer == nullptr)));
                    TEST_EXPECT(ctx, (owner.is_ready() && owner.is_empty()));
                    TEST_EXPECT(ctx, (owner.allocated_size() == 0u));
                    check_accounting(ctx, owner, context);
                    failure.fail_on = std::numeric_limits<std::size_t>::max();
                    TEST_EXPECT(ctx, (owner.allocate(request, 8u) != nullptr));
                }
                check_accounting(ctx, owner, context);
            }
            TEST_EXPECT(ctx, context.is_attribution_empty());
            if (fail_on == attempts) break;
        }
        //  Retry above exercises recovery. These independent instances instead
        //  release each failed state immediately, without allowing a retry to
        //  repair it. Cover explicit teardown and direct scope destruction.
        for (const bool explicit_teardown : { false, true })
        {
            for (std::size_t fail_on = 0u; fail_on < attempts; ++fail_on)
            {
                tests::TAllocationFailureFixture failure;
                memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
                memory::CMemoryContext context{ allocator };
                std::size_t failed_attempts = 0u;
                {
                    TOwner owner;
                    TEST_EXPECT(ctx, owner.initialise({ { 8u, 256u }, 128u }, context));
                    failure.fail_on = fail_on;
                    TEST_EXPECT(ctx, (owner.allocate(request, 8u) == nullptr));
                    TEST_EXPECT(ctx, failure.failed);
                    TEST_EXPECT(ctx, (owner.is_ready() && owner.is_empty()));
                    check_accounting(ctx, owner, context);
                    failed_attempts = failure.attempts;
                    if (explicit_teardown)
                    {
                        owner.deallocate();
                        TEST_EXPECT(ctx, (!owner.is_ready() && owner.is_empty()));
                        TEST_EXPECT(ctx, context.is_attribution_empty());
                        check_accounting(ctx, owner, context);
                    }
                }
                TEST_EXPECT(ctx, context.is_attribution_empty());
                TEST_EXPECT(ctx, (failure.attempts == failed_attempts));
            }
        }
    }
    tests::TAllocationFailureFixture failure;
    memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        TOwner owner;
        TEST_EXPECT(ctx, owner.initialise({ { 8u, 128u }, 128u }, context));
        void* const buddy = owner.allocate(128u);
        void* const tracked = owner.allocate(129u);
        TEST_EXPECT(ctx, ((buddy != nullptr) && (tracked != nullptr)));
        failure.reject_all = true;
        const std::size_t attempts = failure.attempts;
        const std::uint64_t owned = owner.total_owned_size();
        TEST_EXPECT(ctx, (owner.allocate(128u) == nullptr));
        TEST_EXPECT(ctx, (failure.attempts == (attempts + 1u)));
        TEST_EXPECT(ctx, ((owner.manager_count() == 1u) && (owner.tracked_allocation_count() == 1u)));
        TEST_EXPECT(ctx, (owner.total_owned_size() == owned));
        TEST_EXPECT(ctx, (owner.allocation_size(buddy) == 128u));
        TEST_EXPECT(ctx, (owner.allocation_size(tracked) == 129u));
        check_accounting(ctx, owner, context);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

template<typename TWord>
void test_moves(TTestContext& ctx)
{
    using TOwner = THybridAllocator<TWord>;
    memory::CMemoryAllocator allocator{ nullptr, tests::allocate_test_memory, tests::deallocate_test_memory };
    memory::CMemoryContext first{ allocator }, second{ allocator };
    {
        TOwner source;
        TEST_EXPECT(ctx, source.initialise({ { 8u, 256u }, 128u }, first));
        TOwner lazy{ std::move(source) };
        TEST_EXPECT(ctx, (!source.is_ready() && lazy.is_ready()));
        TEST_EXPECT(ctx, (lazy.total_owned_size() == 0u));
        void* const buddy = lazy.allocate(1u, 128u);
        void* const tracked = lazy.allocate(129u);
        TEST_EXPECT(ctx, ((buddy != nullptr) && (tracked != nullptr)));
        TOwner destination;
        TEST_EXPECT(ctx, destination.initialise({ { 16u, 512u }, 128u }, second));
        TEST_EXPECT(ctx, (destination.allocate(129u) != nullptr));
        destination = std::move(lazy);
        TEST_EXPECT(ctx, (!lazy.is_ready() && lazy.is_empty()));
        TEST_EXPECT(ctx, second.is_attribution_empty());
        TEST_EXPECT(ctx, (destination.allocation_size(buddy) == 128u));
        TEST_EXPECT(ctx, (destination.allocation_size(tracked) == 129u));
        TOwner& alias = destination;
        destination = std::move(alias);
        TEST_EXPECT(ctx, (destination.allocation_count() == 2u));
        check_accounting(ctx, destination, first);
        TEST_EXPECT(ctx, lazy.initialise({ { 16u, 512u }, 128u }, second));
        TEST_EXPECT(ctx, (lazy.allocate(1u) != nullptr));
    }
    TEST_EXPECT(ctx, (first.is_attribution_empty() && second.is_attribution_empty()));
}

template<typename TWord>
void run_word_tests(TTestContext& ctx)
{
    tests::TAllocationFailureFixture failure;
    memory::CMemoryAllocator allocator{ &failure, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    test_configuration<TWord>(ctx, context, failure);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_routing<TWord>(ctx, context);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_lifetimes<TWord>(ctx, context);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_failures<TWord>(ctx);
    test_moves<TWord>(ctx);
}

}   //  namespace

int run_hybrid_allocator_tests()
{
    TTestContext ctx;
    tests::TAllocationFailureFixture ambient_failure;
    ambient_failure.reject_all = true;
    memory::CMemoryAllocator allocator{ &ambient_failure, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext ambient{ allocator };
    const tests::TMemoryContextScope scope{ &ambient };
    run_word_tests<std::uint32_t>(ctx);
    run_word_tests<std::uint64_t>(ctx);
    TEST_EXPECT(ctx, (ambient_failure.attempts == 0u));
    TEST_EXPECT(ctx, ambient.is_attribution_empty());
    return ctx.report("HybridAllocator");
}
