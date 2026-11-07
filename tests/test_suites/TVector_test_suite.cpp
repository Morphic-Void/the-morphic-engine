
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TVector_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    06 Nov 26
//
//  Exercise real object lifetimes, aliased insertion and transactional failure.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

#include "containers/ByteBuffers.hpp"
#include "containers/TVector.hpp"
#include "tests/test_suites/TVector_test_suite.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{

using tests::TTestContext;

struct STracked
{
    explicit STracked(const int input = 0) noexcept : value(input)
    {
        ++live;
        ++constructed;
    }

    STracked(const STracked& source) noexcept : STracked(source.value)
    {
        if (source.self != &source) ++errors;
        ++copies;
    }

    STracked(STracked&& source) noexcept : STracked(source.value)
    {
        if (source.self != &source) ++errors;
        source.value = -1;
        ++moves;
    }

    STracked& operator=(const STracked&) = delete;
    STracked& operator=(STracked&&) = delete;

    ~STracked() noexcept
    {
        if (self != this) ++errors;
        --live;
        ++destroyed;
    }

    static void reset() noexcept
    {
        live = 0;
        constructed = 0;
        destroyed = 0;
        moves = 0;
        copies = 0;
        errors = 0;
    }

    int value;
    const STracked* const self = this;
    inline static int live = 0;
    inline static int constructed = 0;
    inline static int destroyed = 0;
    inline static int moves = 0;
    inline static int copies = 0;
    inline static int errors = 0;
};

//  Neither default construction, copy construction nor assignment is available.
struct SMoveOnly
{
    explicit SMoveOnly(const int value) noexcept : tracked(value) {}
    SMoveOnly(SMoveOnly&& source) noexcept : tracked(std::move(source.tracked)) {}
    SMoveOnly(const SMoveOnly&) = delete;
    SMoveOnly& operator=(const SMoveOnly&) = delete;
    SMoveOnly& operator=(SMoveOnly&&) = delete;

    STracked tracked;
};

struct alignas(128) SAlignedValue : SMoveOnly
{
    explicit SAlignedValue(const int value) noexcept : SMoveOnly(value) {}
    SAlignedValue(SAlignedValue&&) noexcept = default;
};

//  Slot reuse must expose newly constructed const values and reference bindings.
struct SConstReferenceValue
{
    SConstReferenceValue(const int input, const int& referred) noexcept
        : value(input), reference(referred) {}
    SConstReferenceValue(SConstReferenceValue&&) noexcept = default;
    SConstReferenceValue(const SConstReferenceValue&) = delete;
    SConstReferenceValue& operator=(const SConstReferenceValue&) = delete;
    SConstReferenceValue& operator=(SConstReferenceValue&&) = delete;

    const int value;
    const int& reference;
};

void expect_balanced(TTestContext& ctx)
{
    TEST_EXPECT(ctx, (STracked::live == 0));
    TEST_EXPECT(ctx, (STracked::constructed == STracked::destroyed));
    TEST_EXPECT(ctx, (STracked::errors == 0));
}

void test_capacity_and_lifetime(TTestContext& ctx)
{
    static_assert(!std::is_copy_constructible_v<TVector<STracked>>);
    static_assert(!std::is_copy_assignable_v<TVector<STracked>>);
    static_assert(std::is_nothrow_move_constructible_v<TVector<SMoveOnly>>);
    static_assert(std::is_nothrow_move_assignable_v<TVector<SMoveOnly>>);
    static_assert(!std::is_default_constructible_v<SMoveOnly>);
    static_assert(!std::is_move_assignable_v<SMoveOnly>);
    static_assert(std::is_same_v<decltype(std::declval<const TVector<STracked>&>().get(0u)), const STracked*>);
    static_assert(std::is_same_v<decltype(std::declval<const TVector<STracked>&>().data()), const STracked*>);
    static_assert(std::is_same_v<decltype(std::declval<const TVector<STracked>&>()[0u]), const STracked&>);
    static_assert(TVector<STracked>::k_max_elements == memory::t_max_elements<STracked>());

    STracked::reset();
    {
        TVector<STracked> vector;
        TEST_EXPECT(ctx, (vector.is_valid() && vector.is_empty() && !vector.is_ready()));
        TEST_EXPECT(ctx, ((vector.data() == nullptr) && (vector.get(0u) == nullptr)));
        TEST_EXPECT(ctx, (vector.reserve(0u) && vector.ensure_free(0u) && vector.reallocate(0u)));
        TEST_EXPECT(ctx, vector.allocate(8u));
        TEST_EXPECT(ctx, (vector.is_ready() && vector.is_empty() && (vector.available() == 8u)));
        TEST_EXPECT(ctx, ((STracked::constructed == 0) && (vector.get(0u) == nullptr)));
        const STracked* const first = vector.emplace_back(11);
        TEST_EXPECT(ctx, (first == vector.data()));
        const STracked* const second = vector.emplace_back(22);
        TEST_EXPECT(ctx, (second == (vector.data() + 1u)));
        TEST_EXPECT(ctx, vector.resize(5u));
        TEST_EXPECT(ctx, ((STracked::live == 5) && (STracked::constructed == 5)));
        TEST_EXPECT(ctx, ((vector[2u].value == 0) && (vector.last().value == 0)));
        TEST_EXPECT(ctx, vector.reallocate(7u));
        TEST_EXPECT(ctx, ((vector.capacity() == 7u) && (vector.size() == 5u)));
        TEST_EXPECT(ctx, ((STracked::moves == 5) && (STracked::destroyed == 5) && (STracked::live == 5)));
        TEST_EXPECT(ctx, ((vector.first().value == 11) && (vector[1u].value == 22)));
        TEST_EXPECT(ctx, (vector.shrink_to_fit() && (vector.capacity() == 5u)));
        TEST_EXPECT(ctx, vector.resize(2u));
        TEST_EXPECT(ctx, ((STracked::live == 2) && (vector.capacity() == 5u)));
        TEST_EXPECT(ctx, (!vector.reallocate(1u) && !vector.reallocate(0u)));
        TEST_EXPECT(ctx, vector.allocate(5u));
        TEST_EXPECT(ctx, ((STracked::live == 0) && (vector.size() == 0u) && (vector.capacity() == 5u)));
        TEST_EXPECT(ctx, (vector.emplace_back(33) != nullptr));
        vector.clear();
        TEST_EXPECT(ctx, ((STracked::live == 0) && (vector.capacity() == 5u)));
        TEST_EXPECT(ctx, vector.shrink_to_fit());
        TEST_EXPECT(ctx, (vector.is_valid() && !vector.is_ready() && (vector.capacity() == 0u)));
        TEST_EXPECT(ctx, (vector.emplace_back(44) != nullptr));
        TEST_EXPECT(ctx, vector.allocate(3u));
        TEST_EXPECT(ctx, (vector.is_empty() && (vector.capacity() == 3u) && (STracked::live == 0)));
        TEST_EXPECT(ctx, (vector.emplace_back(55) != nullptr));
        TEST_EXPECT(ctx, vector.allocate(0u));
        TEST_EXPECT(ctx, (vector.is_valid() && !vector.is_ready() && (STracked::live == 0)));
        TEST_EXPECT(ctx, vector.resize(2u));
        TEST_EXPECT(ctx, (vector.discard_back(0u) && !vector.discard_back(3u)));
        TEST_EXPECT(ctx, (vector.discard_back() && (STracked::live == 1)));
        const TVector<STracked>& observed = vector;
        TEST_EXPECT(ctx, (observed.get(0u) == &observed.first()));
        TEST_EXPECT(ctx, ((&observed.last() == observed.data()) && (observed.get(1u) == nullptr)));
        TEST_EXPECT(ctx, (vector.available() == (vector.capacity() - vector.size())));
    }
    expect_balanced(ctx);

    TVector<int> integers;
    TEST_EXPECT(ctx, integers.resize(3u));
    TEST_EXPECT(ctx, ((integers[0u] == 0) && (integers[1u] == 0) && (integers[2u] == 0)));
}

void test_erase_ranges(TTestContext& ctx)
{
    STracked::reset();
    for (std::size_t size = 0u; size <= 6u; ++size)
    {
        for (std::size_t index = 0u; index <= size; ++index)
        {
            for (std::size_t count = 0u; count <= (size - index); ++count)
            {
                TVector<SMoveOnly> vector;
                TEST_EXPECT(ctx, vector.allocate(size));
                for (std::size_t i = 0u; i < size; ++i)
                {
                    TEST_EXPECT(ctx, (vector.emplace_back(static_cast<int>(i)) != nullptr));
                }
                TEST_EXPECT(ctx, vector.erase(index, count));
                TEST_EXPECT(ctx, ((vector.size() == (size - count)) && (vector.capacity() == size)));
                TEST_EXPECT(ctx, (STracked::live == static_cast<int>(size - count)));
                for (std::size_t i = 0u; i < vector.size(); ++i)
                {
                    const int expected = static_cast<int>((i < index) ? i : (i + count));
                    TEST_EXPECT(ctx, (vector[i].tracked.value == expected));
                }
                TEST_EXPECT(ctx, !vector.erase(vector.size(), 1u));
                TEST_EXPECT(ctx, !vector.erase((vector.size() + 1u), 0u));
                TEST_EXPECT(ctx, !vector.erase(0u, std::numeric_limits<std::size_t>::max()));
            }
        }
    }
    expect_balanced(ctx);
}

void test_self_insertion(TTestContext& ctx)
{
    STracked::reset();
    //  Cover every source/insertion position, with and without spare capacity.
    for (const bool grow : { false, true })
    {
        for (std::size_t index = 0u; index <= 3u; ++index)
        {
            for (std::size_t source = 0u; source < 3u; ++source)
            {
                for (const bool moving : { false, true })
                {
                    TVector<STracked> vector;
                    TEST_EXPECT(ctx, vector.allocate(grow ? 3u : 8u));
                    for (int value = 10; value < 13; ++value)
                    {
                        TEST_EXPECT(ctx, (vector.emplace_back(value) != nullptr));
                    }
                    const int expected = vector[source].value;
                    const bool inserted = moving ? vector.insert(index, std::move(vector[source])) :
                        vector.insert(index, vector[source]);
                    TEST_EXPECT(ctx, (inserted && (vector.size() == 4u)));
                    TEST_EXPECT(ctx, (vector[index].value == expected));
                    for (std::size_t i = 0u; i < 3u; ++i)
                    {
                        const std::size_t target = (i < index) ? i : (i + 1u);
                        const int retained = (moving && (i == source)) ? -1 : (10 + static_cast<int>(i));
                        TEST_EXPECT(ctx, (vector[target].value == retained));
                    }
                    TEST_EXPECT(ctx, ((STracked::live == 4) && (STracked::errors == 0)));
                }
            }
        }
    }
    expect_balanced(ctx);

    //  Forwarded subobject references must be consumed before any shift or move.
    for (const bool grow : { false, true })
    {
        TVector<SMoveOnly> vector;
        TEST_EXPECT(ctx, vector.allocate(grow ? 1u : 4u));
        TEST_EXPECT(ctx, (vector.emplace_back(73) != nullptr));
        const SMoveOnly* const inserted = vector.emplace(0u, vector[0u].tracked.value);
        TEST_EXPECT(ctx, (inserted == vector.data()));
        TEST_EXPECT(ctx, ((vector[0u].tracked.value == 73) && (vector[1u].tracked.value == 73)));
    }
    {
        TVector<STracked> vector;
        TEST_EXPECT(ctx, vector.allocate(1u));
        TEST_EXPECT(ctx, (vector.emplace_back(91) != nullptr));
        TEST_EXPECT(ctx, vector.push_back(vector.first()));
        TEST_EXPECT(ctx, ((vector.first().value == 91) && (vector.last().value == 91)));
        TEST_EXPECT(ctx, vector.push_back(std::move(vector.first())));
        TEST_EXPECT(ctx, ((vector.first().value == -1) && (vector.last().value == 91)));
    }
    expect_balanced(ctx);
}

void test_failure_preservation(TTestContext& ctx)
{
    STracked::reset();
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        TVector<SMoveOnly> vector;
        SMoveOnly source{ 99 };
        fixture.reject_all = true;
        TEST_EXPECT(ctx, (vector.reserve(0u) && vector.ensure_free(0u) && vector.shrink_to_fit()));
        TEST_EXPECT(ctx, (fixture.attempts == 0u));
        TEST_EXPECT(ctx, (!vector.allocate(3u) && !vector.reallocate(3u)));
        TEST_EXPECT(ctx, (vector.emplace_back(std::move(source)) == nullptr));
        TEST_EXPECT(ctx, ((source.tracked.value == 99) && (STracked::moves == 0)));
        TEST_EXPECT(ctx, (vector.is_valid() && !vector.is_ready() && context.is_attribution_empty()));
        fixture.reject_all = false;
        TEST_EXPECT(ctx, vector.allocate(3u));
        for (int value = 1; value <= 3; ++value)
        {
            TEST_EXPECT(ctx, (vector.emplace_back(value) != nullptr));
        }
        SMoveOnly* const original = vector.data();
        const auto attribution = vector.memory_attribution();
        const auto bytes = context.get_live_allocated_bytes();
        const int constructed = STracked::constructed;
        const int destroyed = STracked::destroyed;
        const int moves = STracked::moves;
        fixture.reject_all = true;
        TEST_EXPECT(ctx, (!vector.allocate(5u) && !vector.allocate(1u)));
        TEST_EXPECT(ctx, (!vector.reallocate(5u) && !vector.reserve(4u) && !vector.ensure_free(1u)));
        TEST_EXPECT(ctx, !vector.push_back(std::move(source)));
        TEST_EXPECT(ctx, !vector.insert(0u, std::move(vector[2u])));
        TEST_EXPECT(ctx, (vector.emplace_back(vector[0u].tracked.value) == nullptr));
        TEST_EXPECT(ctx, ((source.tracked.value == 99) && (vector.data() == original)));
        TEST_EXPECT(ctx, ((vector.size() == 3u) && (vector.capacity() == 3u)));
        for (std::size_t i = 0u; i < 3u; ++i)
        {
            TEST_EXPECT(ctx, (vector[i].tracked.value == (1 + static_cast<int>(i))));
        }
        TEST_EXPECT(ctx, ((STracked::constructed == constructed) && (STracked::destroyed == destroyed)));
        TEST_EXPECT(ctx, ((STracked::moves == moves) && (STracked::errors == 0)));
        TEST_EXPECT(ctx, ((vector.memory_attribution().source == attribution.source) &&
            (vector.memory_attribution().allocation_size == attribution.allocation_size)));
        TEST_EXPECT(ctx, ((context.get_live_allocated_bytes() == bytes) && (context.get_live_allocation_count() == 1u)));

        const auto attempts = fixture.attempts;
        const std::size_t excessive = TVector<SMoveOnly>::k_max_elements + 1u;
        TEST_EXPECT(ctx, (!vector.allocate(excessive) && !vector.reallocate(excessive) && !vector.reserve(excessive)));
        TEST_EXPECT(ctx, !vector.ensure_free(std::numeric_limits<std::size_t>::max()));
        TEST_EXPECT(ctx, (!vector.reallocate(2u) && !vector.reallocate(0u)));
        TEST_EXPECT(ctx, (vector.emplace(4u, std::move(source)) == nullptr));
        TEST_EXPECT(ctx, (vector.reserve(3u) && vector.ensure_free(0u) && vector.reallocate(3u)));
        TEST_EXPECT(ctx, vector.shrink_to_fit());
        TEST_EXPECT(ctx, ((fixture.attempts == attempts) && (STracked::moves == moves)));
        TEST_EXPECT(ctx, vector.discard_back());
        TEST_EXPECT(ctx, (!vector.shrink_to_fit() && !vector.reallocate(2u)));
        TEST_EXPECT(ctx, ((vector.data() == original) && (vector.size() == 2u) && (vector.capacity() == 3u)));

        //  Same-capacity reset and in-place insertion work under exhaustion.
        TEST_EXPECT(ctx, vector.allocate(3u));
        TEST_EXPECT(ctx, ((vector.data() == original) && (STracked::live == 1)));
        TEST_EXPECT(ctx, (vector.emplace_back(17) != nullptr));
        TEST_EXPECT(ctx, vector.insert(0u, std::move(source)));
        TEST_EXPECT(ctx, ((vector.first().tracked.value == 99) && (vector.last().tracked.value == 17)));
        TEST_EXPECT(ctx, (source.tracked.value == -1));
        TEST_EXPECT(ctx, vector.allocate(0u));
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    expect_balanced(ctx);

    //  Default construction is also deferred until replacement allocation succeeds.
    {
        const tests::TMemoryContextScope scope{ &context };
        fixture.reject_all = false;
        TVector<STracked> vector;
        TEST_EXPECT(ctx, vector.allocate(1u));
        TEST_EXPECT(ctx, (vector.emplace_back(42) != nullptr));
        STracked* const original = vector.data();
        const int constructed = STracked::constructed;
        fixture.reject_all = true;
        TEST_EXPECT(ctx, !vector.resize(2u));
        TEST_EXPECT(ctx, ((vector.data() == original) && (vector.size() == 1u) && (vector[0u].value == 42)));
        TEST_EXPECT(ctx, (STracked::constructed == constructed));
        TEST_EXPECT(ctx, vector.resize(0u));
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    expect_balanced(ctx);
}

void test_const_and_reference_members(TTestContext& ctx)
{
    const int first = 101;
    const int second = 202;
    const int third = 303;
    TVector<SConstReferenceValue> vector;
    TEST_EXPECT(ctx, vector.allocate(4u));
    TEST_EXPECT(ctx, (vector.emplace_back(1, first) != nullptr));
    TEST_EXPECT(ctx, (vector.emplace_back(2, second) != nullptr));
    TEST_EXPECT(ctx, (vector.emplace_back(3, third) != nullptr));
    const SConstReferenceValue* const prefix = vector.get(0u);
    TEST_EXPECT(ctx, (vector.emplace(1u, 7, third) != nullptr));
    TEST_EXPECT(ctx, ((vector[1u].value == 7) && (&vector[1u].reference == &third)));
    TEST_EXPECT(ctx, ((vector[2u].value == 2) && (&vector[2u].reference == &second)));
    TEST_EXPECT(ctx, vector.erase(1u, 2u));
    TEST_EXPECT(ctx, ((vector.size() == 2u) && (vector[1u].value == 3)));
    TEST_EXPECT(ctx, (&vector[1u].reference == &third));
    TEST_EXPECT(ctx, ((vector.get(0u) == prefix) && (prefix->value == 1) && (&prefix->reference == &first)));
    vector.clear();
    TEST_EXPECT(ctx, (vector.emplace_back(9, second) != nullptr));
    TEST_EXPECT(ctx, ((vector[0u].value == 9) && (&vector[0u].reference == &second)));
    TEST_EXPECT(ctx, vector.reallocate(2u));
    TEST_EXPECT(ctx, ((vector[0u].value == 9) && (&vector[0u].reference == &second)));
    TEST_EXPECT(ctx, (vector.emplace_back(10, third) != nullptr));
    TEST_EXPECT(ctx, vector.discard_back());
    TEST_EXPECT(ctx, (vector.emplace_back(11, first) != nullptr));
    TEST_EXPECT(ctx, ((vector[1u].value == 11) && (&vector[1u].reference == &first)));
}

void test_copy_failure_and_retry(TTestContext& ctx)
{
    STracked::reset();
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        TVector<STracked> vector;
        const STracked source{ 99 };
        TEST_EXPECT(ctx, vector.allocate(1u));
        TEST_EXPECT(ctx, (vector.emplace_back(42) != nullptr));
        const STracked* const original = vector.data();
        const int constructed = STracked::constructed;
        const int destroyed = STracked::destroyed;
        fixture.reject_all = true;
        TEST_EXPECT(ctx, !vector.push_back(source));
        TEST_EXPECT(ctx, !vector.insert(0u, vector[0u]));
        TEST_EXPECT(ctx, ((STracked::copies == 0) && (STracked::moves == 0)));
        TEST_EXPECT(ctx, ((STracked::constructed == constructed) && (STracked::destroyed == destroyed)));
        TEST_EXPECT(ctx, ((vector.data() == original) && (vector.size() == 1u) && (vector.capacity() == 1u)));
        TEST_EXPECT(ctx, ((vector[0u].value == 42) && (source.value == 99)));
        fixture.reject_all = false;
        TEST_EXPECT(ctx, vector.insert(0u, vector[0u]));
        TEST_EXPECT(ctx, ((vector.size() == 2u) && (vector[0u].value == 42) && (vector[1u].value == 42)));
        TEST_EXPECT(ctx, vector.push_back(source));
        TEST_EXPECT(ctx, ((STracked::copies == 2) && (vector.last().value == 99) && (source.value == 99)));
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    expect_balanced(ctx);
}

void test_moves_and_alignment(TTestContext& ctx)
{
    STracked::reset();
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext source_context{ allocator };
    memory::CMemoryContext destination_context{ allocator };
    {
        const tests::TMemoryContextScope source_scope{ &source_context };
        TVector<SMoveOnly> source;
        TEST_EXPECT(ctx, (source.emplace_back(1) != nullptr));
        SMoveOnly* const address = source.data();
        TVector<SMoveOnly> destination;
        {
            const tests::TMemoryContextScope destination_scope{ &destination_context };
            TEST_EXPECT(ctx, memory::reattribute(destination, &destination_context));
            TEST_EXPECT(ctx, (destination.emplace_back(2) != nullptr));
        }
        const int moves = STracked::moves;
        const int destroyed = STracked::destroyed;
        fixture.reject_all = true;
        const auto attempts = fixture.attempts;
        destination = std::move(source);
        TEST_EXPECT(ctx, ((destination.data() == address) && (destination.first().tracked.value == 1)));
        TEST_EXPECT(ctx, (destination.memory_attribution().source == &source_context));
        TEST_EXPECT(ctx, destination_context.is_attribution_empty());
        TEST_EXPECT(ctx, ((STracked::moves == moves) && (STracked::destroyed == (destroyed + 1))));
        TEST_EXPECT(ctx, (source.is_valid() && source.is_empty() && (source.capacity() == 0u)));
        destination = std::move(destination);
        TVector<SMoveOnly> moved{ std::move(destination) };
        TEST_EXPECT(ctx, ((moved.data() == address) && (STracked::moves == moves)));
        TEST_EXPECT(ctx, (destination.is_valid() && destination.is_empty()));
        TEST_EXPECT(ctx, (fixture.attempts == attempts));
        fixture.reject_all = false;
        TEST_EXPECT(ctx, (source.emplace_back(3) != nullptr));
        TEST_EXPECT(ctx, (destination.emplace_back(4) != nullptr));
        moved = std::move(source);
        TEST_EXPECT(ctx, ((moved.first().tracked.value == 3) && (STracked::live == 2)));
        TVector<SMoveOnly> empty;
        moved = std::move(empty);
        TEST_EXPECT(ctx, (moved.is_valid() && moved.is_empty() && !moved.is_ready()));
    }
    TEST_EXPECT(ctx, (source_context.is_attribution_empty() && destination_context.is_attribution_empty()));
    expect_balanced(ctx);

    {
        TVector<SAlignedValue> vector;
        TEST_EXPECT(ctx, vector.allocate(1u));
        TEST_EXPECT(ctx, (vector.emplace_back(7) != nullptr));
        TEST_EXPECT(ctx, (vector.emplace(0u, 8) != nullptr));
        TEST_EXPECT(ctx, vector.reallocate(3u));
        for (std::size_t i = 0u; i < vector.size(); ++i)
        {
            TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(vector.get(i)) % alignof(SAlignedValue)) == 0u));
        }
        TEST_EXPECT(ctx, ((vector[0u].tracked.value == 8) && (vector[1u].tracked.value == 7)));
    }
    expect_balanced(ctx);
}

void test_owned_payloads_and_attribution(TTestContext& ctx)
{
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext backing{ allocator };
    memory::CMemoryContext payload{ allocator };
    memory::CMemoryContext target{ allocator };
    {
        const tests::TMemoryContextScope scope{ &backing };
        TVector<CByteBuffer> vector;
        TEST_EXPECT(ctx, vector.allocate(1u));
        CByteBuffer owner;
        {
            const tests::TMemoryContextScope payload_scope{ &payload };
            TEST_EXPECT(ctx, memory::reattribute(owner, &payload));
            TEST_EXPECT(ctx, (owner.allocate(16u) && owner.set_size(3u)));
        }
        std::uint8_t* const address = owner.data();
        address[0u] = 0x12u;
        address[1u] = 0x34u;
        address[2u] = 0x56u;
        TEST_EXPECT(ctx, vector.push_back(std::move(owner)));
        TEST_EXPECT(ctx, (vector[0u].data() == address));
        TEST_EXPECT(ctx, vector.push_back(std::move(vector[0u])));
        TEST_EXPECT(ctx, (vector[0u].is_empty() && (vector[1u].data() == address)));
        TEST_EXPECT(ctx, vector.insert(0u, std::move(vector[1u])));
        TEST_EXPECT(ctx, ((vector[0u].data() == address) && vector[2u].is_empty()));
        TEST_EXPECT(ctx, (vector.reallocate(7u) && vector.shrink_to_fit()));
        TEST_EXPECT(ctx, (vector[0u].data() == address));
        TEST_EXPECT(ctx, ((address[0u] == 0x12u) && (address[1u] == 0x34u) && (address[2u] == 0x56u)));
        const auto attribution = vector.memory_attribution();
        TEST_EXPECT(ctx, ((attribution.token_count == 1u) && (attribution.allocation_count == 1u)));
        TEST_EXPECT(ctx, (attribution.source == &backing));
        TEST_EXPECT(ctx, ((payload.get_live_allocation_count() == 1u) && (backing.get_live_allocation_count() == 1u)));
        TEST_EXPECT(ctx, memory::reattribute(vector, &target));
        TEST_EXPECT(ctx, backing.is_attribution_empty());
        TEST_EXPECT(ctx, (target.get_live_allocated_bytes() == attribution.allocation_size));
        TEST_EXPECT(ctx, ((vector[0u].memory_attribution().source == &payload) && (vector[0u].data() == address)));
        //  Growth follows the backing token's context despite the ambient scope.
        TEST_EXPECT(ctx, vector.reallocate(9u));
        TEST_EXPECT(ctx, ((vector.memory_attribution().source == &target) && backing.is_attribution_empty()));
        TEST_EXPECT(ctx, (vector[0u].data() == address));
        vector.clear();
        TEST_EXPECT(ctx, payload.is_attribution_empty());
        TEST_EXPECT(ctx, (target.get_live_allocation_count() == 1u));
        vector.deallocate();
        TEST_EXPECT(ctx, target.is_attribution_empty());
        TEST_EXPECT(ctx, memory::reattribute(vector, &payload));
        TEST_EXPECT(ctx, vector.allocate(2u));
        TEST_EXPECT(ctx, (vector.memory_attribution().source == &payload));
    }
    TEST_EXPECT(ctx, (backing.is_attribution_empty() && payload.is_attribution_empty() && target.is_attribution_empty()));
}

void test_owned_payload_removal(TTestContext& ctx)
{
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        TVector<CByteBuffer> vector;
        TEST_EXPECT(ctx, vector.allocate(4u));
        for (std::size_t i = 0u; i < 3u; ++i)
        {
            CByteBuffer* const element = vector.emplace_back();
            TEST_EXPECT(ctx, (element != nullptr));
            if (element == nullptr) return;
            TEST_EXPECT(ctx, element->allocate(16u));
        }
        TEST_EXPECT(ctx, (context.get_live_allocation_count() == 4u));
        const std::uint8_t* const retained = vector[2u].data();
        TEST_EXPECT(ctx, vector.erase(0u, 2u));
        TEST_EXPECT(ctx, ((context.get_live_allocation_count() == 2u) && (vector[0u].data() == retained)));
        TEST_EXPECT(ctx, vector.resize(0u));
        TEST_EXPECT(ctx, (context.get_live_allocation_count() == 1u));
        TEST_EXPECT(ctx, vector.resize(2u));
        TEST_EXPECT(ctx, (context.get_live_allocation_count() == 1u));
        TEST_EXPECT(ctx, vector[0u].allocate(16u));
        TEST_EXPECT(ctx, vector.discard_back(2u));
        TEST_EXPECT(ctx, (context.get_live_allocation_count() == 1u));
        TEST_EXPECT(ctx, vector.resize(1u));
        TEST_EXPECT(ctx, vector[0u].allocate(16u));
        TEST_EXPECT(ctx, vector.allocate(4u));
        TEST_EXPECT(ctx, (vector.is_empty() && (context.get_live_allocation_count() == 1u)));
        TEST_EXPECT(ctx, vector.reallocate(0u));
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void test_explicit_context_without_ambient(TTestContext& ctx)
{
    static_assert(std::is_nothrow_constructible_v<TVector<std::uint32_t>, memory::CMemoryContext&>);
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    memory::CMemoryContext* const previous_module = memory::set_module_memory_context(nullptr);
    {
        const tests::TMemoryContextScope no_thread_context{ nullptr };
        TEST_EXPECT(ctx, (memory::get_ambient_memory_context() == nullptr));
        fixture.reject_all = true;
        TVector<std::uint32_t> vector{ context };
        TEST_EXPECT(ctx, (vector.is_valid() && vector.is_empty() && !vector.is_ready()));
        TEST_EXPECT(ctx, (fixture.attempts == 0u));
        fixture.reject_all = false;
        TEST_EXPECT(ctx, (vector.emplace_back(73u) != nullptr));
        TEST_EXPECT(ctx, vector.reallocate(vector.capacity() + 1u));
        const std::uint32_t* const value = vector.get(0u);
        TEST_EXPECT(ctx, ((value != nullptr) && (*value == 73u)));
        TEST_EXPECT(ctx, (vector.memory_attribution().source == &context));
        TEST_EXPECT(ctx, (context.get_live_allocation_count() == 1u));
        TEST_EXPECT(ctx, (context.get_live_allocated_bytes() == vector.memory_attribution().allocation_size));
        vector.deallocate();
        TEST_EXPECT(ctx, context.is_attribution_empty());
        TEST_EXPECT(ctx, vector.reserve(2u));
        TEST_EXPECT(ctx, (vector.memory_attribution().source == &context));
        TEST_EXPECT(ctx, (memory::get_ambient_memory_context() == nullptr));
    }
    (void)memory::set_module_memory_context(previous_module);
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

}   //  namespace

int run_vector_tests()
{
    TTestContext ctx;
    test_capacity_and_lifetime(ctx);
    test_erase_ranges(ctx);
    test_self_insertion(ctx);
    test_failure_preservation(ctx);
    test_const_and_reference_members(ctx);
    test_copy_failure_and_retry(ctx);
    test_moves_and_alignment(ctx);
    test_owned_payloads_and_attribution(ctx);
    test_owned_payload_removal(ctx);
    test_explicit_context_without_ambient(ctx);
    return ctx.report("TVector");
}
