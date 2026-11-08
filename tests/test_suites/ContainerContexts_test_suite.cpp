
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ContainerContexts_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    08 Nov 26
//
//  Context isolation through construction, replacement, moves and reuse.

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

#include "containers/containers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "ContainerContexts_test_suite.hpp"

namespace
{

using tests::TTestContext;

struct SKey
{
    std::uint32_t value{ 0u };
    [[nodiscard]] std::int32_t relationship(const SKey& other) const noexcept
    {
        return (value < other.value) ? -1 : ((value > other.value) ? 1 : 0);
    }
};

using TPodOrdered = TPodOrderedSlots<std::uint32_t, SKey>;
using TPodUnordered = TPodUnorderedSlots<std::uint32_t>;
using TOrdered = TOrderedSlots<std::uint32_t, SKey>;
using TUnordered = TUnorderedSlots<std::uint32_t>;
using TStableOrdered = TOrderedCollection<std::uint32_t, SKey>;
using TStableUnordered = TUnorderedCollection<std::uint32_t>;

bool grow(CByteBuffer& owner, const std::size_t count)
{
    return owner.reallocate(count, count, 64u);
}

bool grow(CByteRectBuffer& owner, const std::size_t count)
{
    return owner.reallocate(count, 3u, 64u);
}

bool grow(TInstance<std::uint32_t>& owner, const std::size_t count)
{
    return owner.emplace(static_cast<std::uint32_t>(count));
}

bool grow(TPodVector<std::uint32_t>& owner, const std::size_t count)
{
    return owner.reallocate(count, count);
}

bool grow(TVector<std::uint32_t>& owner, const std::size_t count)
{
    return owner.resize(count);
}

bool grow(TPodFifo<std::uint32_t>& owner, const std::size_t count)
{
    if (!owner.reserve(count)) return false;
    while (owner.size() < count)
    {
        if (!owner.push_back(73u)) return false;
    }
    return true;
}

bool grow(CBumpBuffer& owner, const std::size_t count)
{
    if (!owner.is_ready() && !owner.allocate(4096u, 64u))
    {
        return false;
    }
    while (owner.allocation_count() < count)
    {
        if (owner.bump(128u) == nullptr) return false;
    }
    return true;
}

bool grow(CSimpleString& owner, const std::size_t count)
{
    std::array<std::uint8_t, 1024u> text{};
    text.fill('s');
    return owner.set(text.data(), count);
}

bool grow(CStringBuffer& owner, const std::size_t count)
{
    while (owner.size() < count)
    {
        if (owner.append("context") == CStringBuffer::k_invalid_offset)
        {
            return false;
        }
    }
    return true;
}

bool grow(CStableStrings& owner, const std::size_t count)
{
    for (std::size_t i = owner.string_count(); i < count; ++i)
    {
        char text[32]{};
        (void)std::snprintf(text, sizeof(text), "string-%zu", i);
        if (owner.append(text) == CStableStrings::k_invalid_id)
        {
            return false;
        }
    }
    return owner.sort() && owner.check_integrity();
}

template<typename T>
bool grow_slots(T& owner, const std::size_t count)
{
    if (!owner.is_ready() && !owner.initialise())
    {
        return false;
    }
    std::uint32_t live_count = 0u;
    for (std::int32_t slot = owner.first_live(); slot >= 0; slot = owner.next_live(slot)) ++live_count;
    for (std::uint32_t i = live_count; i < count; ++i)
    {
        std::int32_t slot = -1;
        if constexpr (std::is_same_v<T, TStableOrdered>)
        {
            slot = owner.emplace(SKey{ i }, i);
        }
        else if constexpr (std::is_same_v<T, TStableUnordered>)
        {
            slot = owner.emplace(i);
        }
        else if constexpr (std::is_same_v<T, TPodOrdered> || std::is_same_v<T, TOrdered>)
        {
            slot = owner.insert(SKey{ i }, std::uint32_t{ i });
        }
        else
        {
            slot = owner.insert(std::uint32_t{ i });
        }
        if (slot < 0) return false;
    }
    return owner.check_integrity();
}

bool grow(TPodOrdered& owner, const std::size_t count) { return grow_slots(owner, count); }
bool grow(TPodUnordered& owner, const std::size_t count) { return grow_slots(owner, count); }
bool grow(TOrdered& owner, const std::size_t count) { return grow_slots(owner, count); }
bool grow(TUnordered& owner, const std::size_t count) { return grow_slots(owner, count); }
bool grow(TStableOrdered& owner, const std::size_t count) { return grow_slots(owner, count); }
bool grow(TStableUnordered& owner, const std::size_t count) { return grow_slots(owner, count); }

template<typename T>
void release(T& owner) { owner.deallocate(); }

void release(TInstance<std::uint32_t>& owner) { owner.reset(); }

template<typename T>
void expect_context(TTestContext& ctx, const T& owner, const memory::CMemoryContext& context)
{
    const memory::SMemoryAttribution attribution = owner.memory_attribution();
    TEST_EXPECT(ctx, (attribution.source == &context));
    TEST_EXPECT(ctx, (attribution.source_state == memory::EMemorySourceState::coherent));
    TEST_EXPECT(ctx, (attribution.allocation_count == context.get_live_allocation_count()));
    TEST_EXPECT(ctx, (attribution.allocation_size == context.get_live_allocated_bytes()));
}

template<typename T>
void test_owner(TTestContext& ctx)
{
    static_assert(std::is_nothrow_constructible_v<T, memory::CMemoryContext&>);
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext original{ allocator };
    memory::CMemoryContext transferred{ allocator };
    {
        T owner{ original };
        TEST_EXPECT(ctx, (fixture.attempts == 0u));
        TEST_EXPECT(ctx, grow(owner, 40u));
        expect_context(ctx, owner, original);
        TEST_EXPECT(ctx, grow(owner, 160u));
        expect_context(ctx, owner, original);
        TEST_EXPECT(ctx, memory::reattribute(owner, &transferred));
        TEST_EXPECT(ctx, original.is_attribution_empty());
        TEST_EXPECT(ctx, grow(owner, 320u));
        expect_context(ctx, owner, transferred);
        const std::size_t before_move = fixture.attempts;
        T moved{ std::move(owner) };
        TEST_EXPECT(ctx, (fixture.attempts == before_move));
        TEST_EXPECT(ctx, (owner.memory_attribution().allocation_count == 0u));
        expect_context(ctx, moved, transferred);
        T destination{ original };
        TEST_EXPECT(ctx, grow(destination, 7u));
        const std::size_t before_assignment = fixture.attempts;
        destination = std::move(moved);
        T& self = destination;
        destination = std::move(self);
        TEST_EXPECT(ctx, (fixture.attempts == before_assignment));
        TEST_EXPECT(ctx, original.is_attribution_empty());
        expect_context(ctx, destination, transferred);
        release(destination);
        TEST_EXPECT(ctx, transferred.is_attribution_empty());
        TEST_EXPECT(ctx, grow(destination, 64u));
        expect_context(ctx, destination, transferred);
        release(destination);
        //  Empty owners must propagate reattribution into every unallocated member.
        TEST_EXPECT(ctx, memory::reattribute(destination, &original));
        TEST_EXPECT(ctx, grow(destination, 65u));
        expect_context(ctx, destination, original);
        release(destination);
        TEST_EXPECT(ctx, grow(owner, 8u));
        expect_context(ctx, owner, transferred);
    }
    TEST_EXPECT(ctx, (original.is_attribution_empty() && transferred.is_attribution_empty()));
    //  Default construction captures the then-current context, even for deferred storage.
    {
        const tests::TMemoryContextScope construction{ &original };
        T owner;
        {
            const tests::TMemoryContextScope later{ &transferred };
            TEST_EXPECT(ctx, grow(owner, 65u));
            expect_context(ctx, owner, original);
            TEST_EXPECT(ctx, transferred.is_attribution_empty());
        }
    }
    TEST_EXPECT(ctx, original.is_attribution_empty());
}

void run_owner_cases(TTestContext& ctx)
{
    test_owner<CByteBuffer>(ctx);
    test_owner<CByteRectBuffer>(ctx);
    test_owner<CBumpBuffer>(ctx);
    test_owner<CSimpleString>(ctx);
    test_owner<CStringBuffer>(ctx);
    test_owner<CStableStrings>(ctx);
    test_owner<TInstance<std::uint32_t>>(ctx);
    test_owner<TPodFifo<std::uint32_t>>(ctx);
    test_owner<TPodVector<std::uint32_t>>(ctx);
    test_owner<TVector<std::uint32_t>>(ctx);
    test_owner<TPodOrdered>(ctx);
    test_owner<TPodUnordered>(ctx);
    test_owner<TOrdered>(ctx);
    test_owner<TUnordered>(ctx);
    test_owner<TStableOrdered>(ctx);
    test_owner<TStableUnordered>(ctx);
}

void test_replacement_failure(TTestContext& ctx)
{
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    const std::uint8_t bytes[]{ 1u, 2u, 3u, 4u };
    {
        CByteBuffer owner{ context };
        TEST_EXPECT(ctx, owner.append(bytes, sizeof(bytes)));
        fixture.reject_all = true;
        TEST_EXPECT(ctx, !owner.reallocate(512u, 512u, 64u));
        TEST_EXPECT(ctx, ((owner.size() == sizeof(bytes)) && (std::memcmp(owner.data(), bytes, sizeof(bytes)) == 0)));
        expect_context(ctx, owner, context);
        fixture.reject_all = false;
        TEST_EXPECT(ctx, owner.construct_and_copy_from(CByteConstView{ bytes, sizeof(bytes), 1u }));
        expect_context(ctx, owner, context);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    {
        CByteRectBuffer owner{ context };
        TEST_EXPECT(ctx, owner.allocate(2u, 2u, 8u));
        fixture.reject_all = true;
        TEST_EXPECT(ctx, !owner.reallocate(128u, 4u, 64u));
        expect_context(ctx, owner, context);
        fixture.reject_all = false;
        TEST_EXPECT(ctx, owner.construct_and_copy_from(CByteRectConstView{ bytes, 2u, 2u, 2u, 1u }));
        expect_context(ctx, owner, context);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    //  Sorting replaces five backings; each failed candidate must retain the owner context.
    for (std::size_t failure = 0u; failure < 5u; ++failure)
    {
        CStableStrings owner{ context };
        const std::size_t b = owner.append("beta");
        const std::size_t a = owner.append("alpha");
        TEST_EXPECT(ctx, ((a != 0u) && (b != 0u)));
        const std::uint64_t before = context.get_live_allocated_bytes();
        fixture.fail_on = fixture.attempts + failure;
        fixture.failed = false;
        TEST_EXPECT(ctx, !owner.sort());
        TEST_EXPECT(ctx, fixture.failed);
        TEST_EXPECT(ctx, (context.get_live_allocated_bytes() == before));
        TEST_EXPECT(ctx, ((owner.find_id("alpha") == a) && (owner.find_id("beta") == b)));
        expect_context(ctx, owner, context);
        fixture.fail_on = std::numeric_limits<std::size_t>::max();
        TEST_EXPECT(ctx, owner.sort());
        expect_context(ctx, owner, context);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

}   //  namespace

int run_container_context_tests()
{
    TTestContext ctx;
    tests::TAllocationFailureFixture ambient_fixture;
    ambient_fixture.reject_all = true;
    memory::CMemoryAllocator allocator{ &ambient_fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext ambient{ allocator };
    {
        const tests::TMemoryContextScope scope{ &ambient };
        run_owner_cases(ctx);
        test_replacement_failure(ctx);
    }
    memory::CMemoryContext* const previous_module = memory::set_module_memory_context(nullptr);
    {
        const tests::TMemoryContextScope no_thread{ nullptr };
        run_owner_cases(ctx);
        test_replacement_failure(ctx);
        TEST_EXPECT(ctx, (memory::get_ambient_memory_context() == nullptr));
    }
    (void)memory::set_module_memory_context(previous_module);
    TEST_EXPECT(ctx, (ambient_fixture.attempts == 0u));
    TEST_EXPECT(ctx, ambient.is_attribution_empty());
    return ctx.report("ContainerContexts");
}
