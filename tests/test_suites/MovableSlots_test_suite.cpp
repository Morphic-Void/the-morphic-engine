
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    MovableSlots_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    07 Nov 26
//
//  Exercise slot remapping, owner lifetimes and partial backing growth.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "containers/ByteBuffers.hpp"
#include "containers/TOrderedSlots.hpp"
#include "containers/TUnorderedSlots.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "MovableSlots_test_suite.hpp"

namespace
{

using tests::TTestContext;

struct SKey
{
    std::int32_t value = 0;

    [[nodiscard]] std::int32_t relationship(const SKey& other) const noexcept
    {
        return (value < other.value) ? -1 : ((value > other.value) ? 1 : 0);
    }
};

using TUnorderedBuffers = TUnorderedSlots<CByteBuffer>;
using TOrderedBuffers = TOrderedSlots<CByteBuffer, SKey>;
using TOrderedTokens = TOrderedSlots<memory::CMemoryToken, SKey>;

static_assert(!std::is_copy_constructible_v<TUnorderedBuffers>);
static_assert(!std::is_copy_constructible_v<TOrderedTokens>);
static_assert(std::is_nothrow_move_assignable_v<TUnorderedBuffers>);
static_assert(std::is_nothrow_move_assignable_v<TOrderedTokens>);

[[nodiscard]] bool make_buffer(CByteBuffer& buffer, const std::uint8_t value) noexcept
{
    return buffer.append(&value, 1u);
}

void test_unordered_pack_and_move(TTestContext& ctx)
{
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    const tests::TMemoryContextScope scope{ &context };
    TUnorderedBuffers source;
    TEST_EXPECT(ctx, (source.is_valid() && source.is_empty() && !source.is_ready()));
    const bool initialised = source.initialise();
    TEST_EXPECT(ctx, initialised);
    if (!initialised) return;

    const std::uint8_t* payloads[4]{};
    for (std::int32_t index = 0; index < 4; ++index)
    {
        CByteBuffer buffer;
        const bool prepared = make_buffer(buffer, static_cast<std::uint8_t>(index));
        TEST_EXPECT(ctx, prepared);
        if (!prepared) return;
        payloads[index] = buffer.data();
        const std::int32_t inserted = source.insert(std::move(buffer));
        TEST_EXPECT(ctx, ((inserted == index) && (buffer.data() == nullptr)));
        if (inserted != index) return;
    }
    TEST_EXPECT(ctx, (context.get_live_allocation_count() == 6u));
    TEST_EXPECT(ctx, source.erase(1));
    TEST_EXPECT(ctx, ((source.get_slot(1) == nullptr) && (context.get_live_allocation_count() == 5u)));
    TEST_EXPECT(ctx, !source.erase(1));
    TEST_EXPECT(ctx, (source.insert(CByteBuffer{}) == 1));
    const CByteBuffer* const reused = source.get_slot(1);
    TEST_EXPECT(ctx, ((reused != nullptr) && reused->is_empty()));
    TEST_EXPECT(ctx, source.erase(1));
    {
        const slots::RankMap ranks = source.build_rank_map();
        TEST_EXPECT(ctx, ((ranks.size() > 3u) && (ranks[3u].slot_to_rank == 2)));
    }
    source.pack();
    TEST_EXPECT(ctx, source.check_integrity());
    TEST_EXPECT(ctx, ((source.occupied_count() == 3u) && (source.get_slot(3) == nullptr)));
    const CByteBuffer* const packed = source.get_slot(1);
    TEST_EXPECT(ctx, ((packed != nullptr) && (packed->data() == payloads[2])));
    TEST_EXPECT(ctx, (source.reverse_lookup_index_scan(packed) == 1));

    TUnorderedBuffers destination;
    const bool destination_ready = destination.initialise();
    TEST_EXPECT(ctx, destination_ready);
    if (!destination_ready) return;
    CByteBuffer replaced;
    const bool prepared = make_buffer(replaced, 9u);
    TEST_EXPECT(ctx, prepared);
    if (!prepared) return;
    TEST_EXPECT(ctx, (destination.insert(std::move(replaced)) == 0));
    destination = std::move(source);
    TEST_EXPECT(ctx, (source.is_valid() && source.is_empty() && !source.is_ready()));
    TEST_EXPECT(ctx, ((destination.get_slot(1) == packed) && (context.get_live_allocation_count() == 5u)));
    TEST_EXPECT(ctx, destination.check_integrity());
    TEST_EXPECT(ctx, source.initialise());
    source.deallocate();
    destination.deallocate();
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void test_ordered_tokens_and_pack(TTestContext& ctx)
{
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    const tests::TMemoryContextScope scope{ &context };
    TOrderedTokens slots;
    const bool initialised = slots.initialise();
    TEST_EXPECT(ctx, initialised);
    if (!initialised) return;

    const SKey keys[] = { { 40 }, { 10 }, { 30 }, { 20 } };
    const void* payloads[4]{};
    for (std::int32_t index = 0; index < 4; ++index)
    {
        memory::CMemoryToken token{ 1u, 1u };
        const bool prepared = token.allocate(8u);
        TEST_EXPECT(ctx, prepared);
        if (!prepared) return;
        payloads[index] = token.data();
        const std::int32_t inserted = slots.insert(keys[index], std::move(token));
        TEST_EXPECT(ctx, ((inserted == index) && (token.data() == nullptr)));
        if (inserted != index) return;
    }
    TEST_EXPECT(ctx, ((slots.first_live() == 1) && (slots.next_live(1) == 3) && (slots.last_live() == 0)));
    TEST_EXPECT(ctx, (slots.prev_live(0) == 2));
    TEST_EXPECT(ctx, (slots.memory_attribution().allocation_count == 3u));
    TEST_EXPECT(ctx, (context.get_live_allocation_count() == 7u));

    memory::CMemoryToken duplicate{ 1u, 1u };
    const bool prepared = duplicate.allocate(8u);
    TEST_EXPECT(ctx, prepared);
    if (!prepared) return;
    const void* const duplicate_address = duplicate.data();
    TEST_EXPECT(ctx, (slots.insert(keys[0], std::move(duplicate)) == -1));
    TEST_EXPECT(ctx, (duplicate.data() == duplicate_address));
    duplicate.deallocate();

    TEST_EXPECT(ctx, slots.erase(keys[1]));
    TEST_EXPECT(ctx, ((slots.key_at_slot(1) == nullptr) && (context.get_live_allocation_count() == 6u)));
    {
        const slots::RankMap ranks = slots.build_rank_map();
        TEST_EXPECT(ctx, ((ranks.size() > 3u) && (ranks[3u].slot_to_rank == 0)));
    }
    slots.sort_and_pack();
    TEST_EXPECT(ctx, slots.check_integrity());
    TEST_EXPECT(ctx, ((slots.occupied_count() == 3u) && (slots.get_slot(3) == nullptr)));
    const std::int32_t source_indices[] = { 3, 2, 0 };
    for (std::int32_t index = 0; index < 3; ++index)
    {
        const std::int32_t original = source_indices[index];
        const SKey* const key = slots.key_at_slot(index);
        const memory::CMemoryToken* const stored = slots.get_slot(keys[original]);
        TEST_EXPECT(ctx, (slots.find_index(keys[original]) == index));
        TEST_EXPECT(ctx, ((key != nullptr) && (key->value == keys[original].value)));
        TEST_EXPECT(ctx, ((stored != nullptr) && (stored->data() == payloads[original])));
    }
    slots.sort_and_pack();
    TOrderedTokens moved{ std::move(slots) };
    TEST_EXPECT(ctx, (slots.is_valid() && slots.is_empty()));
    TEST_EXPECT(ctx, slots.initialise());
    TEST_EXPECT(ctx, moved.check_integrity());
    slots.deallocate();
    moved.deallocate();
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

template<bool Ordered>
using TBufferSlots = std::conditional_t<Ordered, TOrderedBuffers, TUnorderedBuffers>;

template<bool Ordered>
[[nodiscard]] std::int32_t insert_buffer(
    TBufferSlots<Ordered>& slots, const std::int32_t key, CByteBuffer& buffer) noexcept
{
    if constexpr (Ordered)
    {
        return slots.insert(SKey{ key }, std::move(buffer));
    }
    else
    {
        return slots.insert(std::move(buffer));
    }
}

template<bool Ordered>
void test_failed_initialise(TTestContext& ctx)
{
    //  Initialisation allocates metadata, payload backing, and ordered key backing.
    constexpr std::size_t allocation_count = Ordered ? 3u : 2u;
    for (std::size_t failure = 0u; failure < allocation_count; ++failure)
    {
        tests::TAllocationFailureFixture fixture;
        fixture.fail_on = failure;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        const tests::TMemoryContextScope scope{ &context };
        {
            TBufferSlots<Ordered> slots;
            TEST_EXPECT(ctx, !slots.initialise());
            TEST_EXPECT(ctx, fixture.failed);
            TEST_EXPECT(ctx, (slots.is_valid() && slots.is_empty() && !slots.is_ready()));
            TEST_EXPECT(ctx, context.is_attribution_empty());

            const bool initialised = slots.initialise();
            TEST_EXPECT(ctx, initialised);
            if (!initialised) return;
            TEST_EXPECT(ctx, slots.check_integrity());
            CByteBuffer buffer;
            const bool prepared = make_buffer(buffer, 1u);
            TEST_EXPECT(ctx, prepared);
            if (!prepared) return;
            TEST_EXPECT(ctx, (insert_buffer<Ordered>(slots, 1, buffer) == 0));
            TEST_EXPECT(ctx, (buffer.data() == nullptr));
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
}

template<bool Ordered>
void test_failed_growth(TTestContext& ctx)
{
    //  Fail each of the payload, optional key, and metadata allocations.
    constexpr std::size_t growth_allocations = Ordered ? 3u : 2u;
    for (std::size_t failure = 0u; failure < growth_allocations; ++failure)
    {
        tests::TAllocationFailureFixture fixture;
        memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        memory::CMemoryContext destination{ allocator };
        const tests::TMemoryContextScope scope{ &context };
        TBufferSlots<Ordered> slots;
        const bool initialised = slots.initialise(32u);
        TEST_EXPECT(ctx, initialised);
        if (!initialised) return;
        const std::uint8_t* payloads[32]{};
        for (std::int32_t index = 0; index < 32; ++index)
        {
            CByteBuffer buffer;
            const bool prepared = make_buffer(buffer, static_cast<std::uint8_t>(index));
            TEST_EXPECT(ctx, prepared);
            if (!prepared) return;
            payloads[index] = buffer.data();
            const std::int32_t inserted = insert_buffer<Ordered>(slots, (31 - index), buffer);
            TEST_EXPECT(ctx, (inserted == index));
            if (inserted != index) return;
        }

        CByteBuffer incoming;
        const bool prepared = make_buffer(incoming, 32u);
        TEST_EXPECT(ctx, prepared);
        if (!prepared) return;
        const std::uint8_t* const incoming_address = incoming.data();
        fixture.fail_on = fixture.attempts + failure;
        TEST_EXPECT(ctx, (insert_buffer<Ordered>(slots, 32, incoming) == -1));
        TEST_EXPECT(ctx, (fixture.failed && (incoming.data() == incoming_address)));
        TEST_EXPECT(ctx, (slots.is_valid() && slots.check_integrity()));
        TEST_EXPECT(ctx, ((slots.occupied_count() == 32u) && (slots.get_slot(32) == nullptr)));
        for (std::int32_t index = 0; index < 32; ++index)
        {
            const CByteBuffer* const stored = slots.get_slot(index);
            TEST_EXPECT(ctx, ((stored != nullptr) && (stored->data() == payloads[index])));
        }

        fixture.fail_on = std::numeric_limits<std::size_t>::max();
        if constexpr (Ordered)
        {
            //  Pack real permutation cycles before a retry can extend metadata.
            const std::size_t attempts = fixture.attempts;
            slots.sort_and_pack();
            TEST_EXPECT(ctx, (fixture.attempts == attempts));
            TEST_EXPECT(ctx, slots.check_integrity());
            for (std::int32_t index = 0; index < 32; ++index)
            {
                const std::int32_t original = 31 - index;
                const CByteBuffer* const stored = slots.get_slot(SKey{ index });
                TEST_EXPECT(ctx, (slots.find_index(SKey{ index }) == index));
                TEST_EXPECT(ctx, ((stored != nullptr) && (stored->data() == payloads[original]) &&
                    (stored->size() == 1u) && (stored->data()[0] == static_cast<std::uint8_t>(original))));
            }
        }
        const std::int32_t inserted = insert_buffer<Ordered>(slots, 32, incoming);
        TEST_EXPECT(ctx, (inserted == 32));
        const CByteBuffer* const retained = slots.get_slot(inserted);
        TEST_EXPECT(ctx, ((retained != nullptr) && (retained->data() == incoming_address)));
        TEST_EXPECT(ctx, slots.check_integrity());
        TEST_EXPECT(ctx, memory::reattribute(slots, &destination));
        TEST_EXPECT(ctx, (destination.get_live_allocation_count() == growth_allocations));
        TEST_EXPECT(ctx, (context.get_live_allocation_count() == 33u));
        slots.deallocate();
        TEST_EXPECT(ctx, (context.is_attribution_empty() && destination.is_attribution_empty()));
    }
}

struct SKeyedValue
{
    SKeyedValue() noexcept = default;
    explicit SKeyedValue(const std::int32_t value) noexcept : key{ value } {}
    SKeyedValue(SKeyedValue&& source) noexcept : key(std::exchange(source.key, SKey{})) {}
    SKeyedValue& operator=(SKeyedValue&& source) noexcept
    {
        key = std::exchange(source.key, SKey{});
        return *this;
    }
    SKeyedValue& operator=(const SKeyedValue&) noexcept = default;

    SKey key;
};

void test_key_from_incoming_value(TTestContext& ctx)
{
    TOrderedSlots<SKeyedValue, SKey> slots;
    const bool initialised = slots.initialise();
    TEST_EXPECT(ctx, initialised);
    if (!initialised) return;
    SKeyedValue incoming{ 10 };
    TEST_EXPECT(ctx, (slots.insert(incoming.key, std::move(incoming)) == 0));
    TEST_EXPECT(ctx, ((incoming.key.value == 0) && (slots.find_index(SKey{ 10 }) == 0)));
    const SKeyedValue copied{ 20 };
    TEST_EXPECT(ctx, (slots.insert(copied.key, copied) == 1));
    TEST_EXPECT(ctx, ((copied.key.value == 20) && (slots.find_index(SKey{ 20 }) == 1)));
    TEST_EXPECT(ctx, !slots.initialise(std::numeric_limits<std::size_t>::max()));
    TEST_EXPECT(ctx, ((slots.occupied_count() == 2u) && slots.check_integrity()));
}

template<bool Ordered>
void test_explicit_context(TTestContext& ctx)
{
    using TSlots = std::conditional_t<Ordered, TOrderedTokens, TUnorderedSlots<memory::CMemoryToken>>;
    static_assert(std::is_nothrow_constructible_v<TSlots, memory::CMemoryContext&>);
    static_assert(std::is_nothrow_constructible_v<TPodVector<SKey>, memory::CMemoryContext&>);
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    memory::CMemoryContext* const previous_module = memory::set_module_memory_context(nullptr);
    {
        const tests::TMemoryContextScope no_thread{ nullptr };
        TSlots slots{ context };
        TEST_EXPECT(ctx, (slots.is_valid() && slots.is_empty() && !slots.is_ready()));
        TEST_EXPECT(ctx, (fixture.attempts == 0u));
        TEST_EXPECT(ctx, slots.initialise());
        TEST_EXPECT(ctx, (slots.memory_attribution().source == &context));
        for (std::int32_t i = 0; i < 65; ++i)
        {
            memory::CMemoryToken token{ 1u, 16u, &context };
            TEST_EXPECT(ctx, token.allocate(13u, false));
            if constexpr (Ordered)
            {
                TEST_EXPECT(ctx, (slots.insert(SKey{ i }, std::move(token)) >= 0));
            }
            else
            {
                TEST_EXPECT(ctx, (slots.insert(std::move(token)) >= 0));
            }
        }
        TEST_EXPECT(ctx, slots.check_integrity());
        TEST_EXPECT(ctx, (context.get_live_allocation_count() == (65u + (Ordered ? 3u : 2u))));
        if constexpr (Ordered)
        {
            slots.sort_and_pack();
        }
        else
        {
            slots.pack();
        }
        TEST_EXPECT(ctx, slots.check_integrity());
        slots.deallocate();
        TEST_EXPECT(ctx, context.is_attribution_empty());
        TEST_EXPECT(ctx, slots.initialise());
        TEST_EXPECT(ctx, (slots.memory_attribution().source == &context));
        TEST_EXPECT(ctx, (memory::get_ambient_memory_context() == nullptr));
    }
    (void)memory::set_module_memory_context(previous_module);
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

}   //  namespace

int run_movable_slots_tests()
{
    TTestContext ctx;
    test_unordered_pack_and_move(ctx);
    test_ordered_tokens_and_pack(ctx);
    test_failed_initialise<false>(ctx);
    test_failed_initialise<true>(ctx);
    test_failed_growth<false>(ctx);
    test_failed_growth<true>(ctx);
    test_key_from_incoming_value(ctx);
    test_explicit_context<false>(ctx);
    test_explicit_context<true>(ctx);
    return ctx.report("MovableSlots");
}
