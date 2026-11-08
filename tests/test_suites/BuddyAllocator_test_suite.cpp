
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BuddyAllocator_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    07 Nov 26
//
//  Independent interval oracle, exact routing checks and ownership failure tests.

#include <cstring>
#include <limits>
#include <map>
#include <type_traits>
#include <utility>

#include "allocators/TBuddyAllocator.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/BuddyAllocator_test_suite.hpp"

namespace
{

using tests::TTestContext;

struct SGeometryAnchor
{
    std::uint32_t order;
    std::uint32_t node;
    std::uint32_t word;
    std::uint32_t bit;
};

template<typename TWord>
void expect_word_image(TTestContext& ctx, const buddy_detail::TPackedIndex<TWord>& index,
    const std::uint32_t expected_word, const TWord expected_mask)
{
    bool matches = true;
    std::uint32_t word = 0u;
    for (const TWord value : index.words)
    {
        const TWord expected = (word == expected_word) ? expected_mask : TWord{ 0u };
        matches = matches && (value == expected);
        ++word;
    }
    TEST_EXPECT(ctx, matches);
}

template<typename TWord, std::size_t N>
void check_geometry_anchors(TTestContext& ctx, const SGeometryAnchor (&anchors)[N], const std::uint32_t root_order)
{
    buddy_detail::TPackedIndex<TWord> index;
    for (const SGeometryAnchor& anchor : anchors)
    {
        index.reset(root_order);
        const TWord mask = TWord{ 1u } << anchor.bit;
        //  Check encoding against literal positions, including every other word
        //  staying clear. These isolated raw-node tests do not model allocations.
        index.set_reserved(anchor.order, anchor.node, true);
        expect_word_image(ctx, index, anchor.word, mask);
        index.reset(root_order);
        //  Decode a separately written literal image, avoiding a round-trip test
        //  in which a matching encoder/decoder error could cancel out.
        index.words[anchor.word] = mask;
        TEST_EXPECT(ctx, index.reserved(anchor.order, anchor.node));
        index.set_reserved(anchor.order, anchor.node, false);
        expect_word_image(ctx, index, anchor.word, TWord{ 0u });
        TEST_EXPECT(ctx, !index.reserved(anchor.order, anchor.node));
    }
}

template<typename TWord>
void test_literal_geometry(TTestContext& ctx)
{
    using TIndex = buddy_detail::TPackedIndex<TWord>;
    //  Deliberately literal: do not derive these expectations from the index's
    //  constants or mapping helpers. Include the far end of every tier as well
    //  as both child roots across each distinct-node word boundary.
    if constexpr (std::is_same_v<TWord, std::uint32_t>)
    {
        static_assert((TIndex::k_width == 32u) && (TIndex::k_levels == 5u));
        static_assert((TIndex::k_max_order == 14u) && (TIndex::k_slot_count == 16384u));
        static_assert((TIndex::k_no_slot == 16384u) && (TIndex::k_word_count == 1057u));
        static_assert((CBuddyAllocator32::k_max_slots == 16384u));
        const SGeometryAnchor anchors[]{
            { 14u, 0u, 0u, 30u }, { 13u, 1u, 0u, 29u }, { 11u, 7u, 0u, 23u },
            { 10u, 0u, 0u, 0u }, { 10u, 15u, 0u, 15u },
            { 9u, 0u, 1u, 30u }, { 9u, 1u, 2u, 30u }, { 9u, 31u, 32u, 30u },
            { 5u, 0u, 1u, 0u }, { 5u, 15u, 1u, 15u },
            { 5u, 16u, 2u, 0u }, { 5u, 511u, 32u, 15u },
            { 4u, 0u, 33u, 30u }, { 4u, 1u, 34u, 30u },
            { 4u, 31u, 64u, 30u }, { 4u, 32u, 65u, 30u }, { 4u, 1023u, 1056u, 30u },
            { 0u, 0u, 33u, 0u }, { 0u, 15u, 33u, 15u },
            { 0u, 16u, 34u, 0u }, { 0u, 16383u, 1056u, 15u }
        };
        check_geometry_anchors<TWord>(ctx, anchors, 14u);
    }
    else
    {
        static_assert((TIndex::k_width == 64u) && (TIndex::k_levels == 6u));
        static_assert((TIndex::k_max_order == 17u) && (TIndex::k_slot_count == 131072u));
        static_assert((TIndex::k_no_slot == 131072u) && (TIndex::k_word_count == 4161u));
        static_assert((CBuddyAllocator64::k_max_slots == 131072u));
        const SGeometryAnchor anchors[]{
            { 17u, 0u, 0u, 62u }, { 16u, 1u, 0u, 61u }, { 13u, 15u, 0u, 47u },
            { 12u, 0u, 0u, 0u }, { 12u, 31u, 0u, 31u },
            { 11u, 0u, 1u, 62u }, { 11u, 1u, 2u, 62u }, { 11u, 63u, 64u, 62u },
            { 6u, 0u, 1u, 0u }, { 6u, 31u, 1u, 31u },
            { 6u, 32u, 2u, 0u }, { 6u, 2047u, 64u, 31u },
            { 5u, 0u, 65u, 62u }, { 5u, 1u, 66u, 62u },
            { 5u, 63u, 128u, 62u }, { 5u, 64u, 129u, 62u }, { 5u, 4095u, 4160u, 62u },
            { 0u, 0u, 65u, 0u }, { 0u, 31u, 65u, 31u },
            { 0u, 32u, 66u, 0u }, { 0u, 131071u, 4160u, 31u }
        };
        check_geometry_anchors<TWord>(ctx, anchors, 17u);
    }
}

//  The oracle owns intervals, not packed nodes or routing information. Recursion
//  produces maximal empty intervals without consulting the implementation.
struct SReference
{
    std::uint32_t active_order;
    std::map<std::uint32_t, std::uint32_t> live;

    template<typename TFree, typename TReserved>
    void walk(const std::uint32_t start, const std::uint32_t order,
        TFree& on_free, TReserved& on_reserved) const
    {
        const auto found = live.lower_bound(start);
        if ((found == live.end()) || (found->first >= (start + (1u << order))))
        {
            on_free(start, order);
            return;
        }
        on_reserved(start, order);
        if ((found->first == start) && (found->second == order))
        {
            return;
        }
        walk(start, (order - 1u), on_free, on_reserved);
        walk((start + (1u << (order - 1u))), (order - 1u), on_free, on_reserved);
    }

    std::uint32_t allocate(const std::uint32_t requested)
    {
        std::uint32_t best_order = active_order + 1u;
        std::uint32_t best_start = std::numeric_limits<std::uint32_t>::max();
        const auto on_free = [&](const std::uint32_t start, const std::uint32_t order)
        {
            if ((order >= requested) && ((order < best_order) || ((order == best_order) && (start < best_start))))
            {
                best_order = order;
                best_start = start;
            }
        };
        const auto on_reserved = [](const std::uint32_t, const std::uint32_t) {};
        walk(0u, active_order, on_free, on_reserved);
        if (best_order <= active_order) live.emplace(best_start, requested);
        return best_start;
    }
};

template<typename TWord>
bool same_index(const buddy_detail::TPackedIndex<TWord>& left, const buddy_detail::TPackedIndex<TWord>& right)
{
    using TIndex = buddy_detail::TPackedIndex<TWord>;
    for (std::uint32_t word = 0u; word < TIndex::k_width; ++word)
    {
        if (!std::equal(left.middle_routes[word], (left.middle_routes[word] + TIndex::k_levels), right.middle_routes[word]))
        {
            return false;
        }
    }
    return std::equal(left.words, (left.words + TIndex::k_word_count), right.words) &&
        std::equal(left.top_routes, (left.top_routes + (2u * TIndex::k_levels)), right.top_routes) &&
        (left.available == right.available) &&
        (left.active_order == right.active_order) && (left.live_count == right.live_count) &&
        (left.live_slots == right.live_slots);
}

template<typename TWord>
void check_index(TTestContext& ctx, const buddy_detail::TPackedIndex<TWord>& index, const SReference& reference)
{
    using TIndex = buddy_detail::TPackedIndex<TWord>;
    TIndex expected;
    expected.active_order = reference.active_order;
    //  Decode geometry independently of index helpers, then reconstruct every
    //  word, including unused bits, inactive ancestors and unused routing slots.
    const auto on_reserved = [&](const std::uint32_t start, const std::uint32_t order)
    {
        const std::uint32_t depth = TIndex::k_max_order - order;
        const std::uint32_t layer = depth / TIndex::k_levels;
        const std::uint32_t local_level = depth % TIndex::k_levels;
        const std::uint32_t node = start >> order;
        const std::uint32_t word = node >> local_level;
        const std::uint32_t position = node % (1u << local_level);
        const std::uint32_t offset = (layer == 0u) ? 0u : ((layer == 1u) ? 1u : (1u + TIndex::k_width));
        const std::uint32_t bit = TIndex::k_width - (2u << local_level) + position;
        expected.words[offset + word] |= TWord{ 1u } << bit;
    };
    const auto on_free = [&](const std::uint32_t start, const std::uint32_t order)
    {
        expected.available |= 1u << order;
        std::uint32_t layer = (TIndex::k_max_order - order) / TIndex::k_levels;
        const std::uint32_t local_level = (TIndex::k_max_order - order) % TIndex::k_levels;
        std::uint32_t word = (start >> order) >> local_level;
        const std::uint32_t active_layer = (TIndex::k_max_order - reference.active_order) / TIndex::k_levels;
        while (layer > active_layer)
        {
            const TWord bit = TWord{ 1u } << (word % TIndex::k_width);
            word /= TIndex::k_width;
            --layer;
            if (layer == 0u) expected.top_routes[order] |= bit;
            else expected.middle_routes[word][order] |= bit;
        }
    };
    reference.walk(0u, reference.active_order, on_free, on_reserved);
    expected.live_count = static_cast<std::uint32_t>(reference.live.size());
    for (const auto& entry : reference.live)
    {
        expected.live_slots += 1u << entry.second;
        TEST_EXPECT(ctx, (index.allocation_order(entry.first) == static_cast<std::int32_t>(entry.second)));
    }
    TEST_EXPECT(ctx, same_index(index, expected));
}

template<typename TWord>
void test_index_orders(TTestContext& ctx)
{
    using TIndex = buddy_detail::TPackedIndex<TWord>;
    for (std::uint32_t active = 0u; active <= TIndex::k_max_order; ++active)
    {
        for (std::uint32_t order = 0u; order <= active; ++order)
        {
            TIndex index;
            index.reset(active);
            SReference reference{ active, {} };
            check_index(ctx, index, reference);
            const std::uint32_t first = index.allocate(order);
            TEST_EXPECT(ctx, (first == reference.allocate(order)));
            check_index(ctx, index, reference);
            //  A second large block checks the slot-unit address scaling and
            //  every cross-word boundary, rather than just allocation at zero.
            const std::uint32_t second = index.allocate(order);
            const std::uint32_t expected = reference.allocate(order);
            TEST_EXPECT(ctx, (second == ((expected == UINT32_MAX) ? TIndex::k_no_slot : expected)));
            check_index(ctx, index, reference);
            TEST_EXPECT(ctx, (index.release(first) == static_cast<std::int32_t>(order)));
            reference.live.erase(first);
            check_index(ctx, index, reference);
            const TIndex saved = index;
            TEST_EXPECT(ctx, (index.release(first) == -1));
            TEST_EXPECT(ctx, same_index(index, saved));
            if (second != TIndex::k_no_slot)
            {
                TEST_EXPECT(ctx, (index.release(second) == static_cast<std::int32_t>(order)));
                reference.live.erase(second);
            }
            check_index(ctx, index, reference);
            TEST_EXPECT(ctx, (index.allocate(active) == 0u));
            TEST_EXPECT(ctx, (index.allocate(0u) == TIndex::k_no_slot));
            index.reset(active);
            check_index(ctx, index, reference);
        }
    }
}

static std::uint32_t next_random(std::uint32_t& seed)
{
    seed ^= seed << 13u;
    seed ^= seed >> 17u;
    seed ^= seed << 5u;
    return seed;
}

template<typename TWord>
void test_index_mixed(TTestContext& ctx)
{
    using TIndex = buddy_detail::TPackedIndex<TWord>;
    std::uint32_t seed = 0x179aba31u;
    for (std::uint32_t active = 0u; active <= TIndex::k_max_order; ++active)
    {
        TIndex index;
        index.reset(active);
        SReference reference{ active, {} };
        for (std::uint32_t step = 0u; step < 600u; ++step)
        {
            const std::uint32_t action = next_random(seed) % 5u;
            if ((action < 3u) || reference.live.empty())
            {
                const std::uint32_t order = next_random(seed) % (active + 2u);
                const TIndex saved = index;
                const std::uint32_t expected = reference.allocate(order);
                const std::uint32_t actual = index.allocate(order);
                TEST_EXPECT(ctx, (actual == ((expected == UINT32_MAX) ? TIndex::k_no_slot : expected)));
                if (actual == TIndex::k_no_slot) TEST_EXPECT(ctx, same_index(index, saved));
            }
            else if (action == 3u)
            {
                auto found = reference.live.begin();
                const std::size_t offset = next_random(seed) % reference.live.size();
                for (std::size_t i = 0u; i < offset; ++i) ++found;
                TEST_EXPECT(ctx, (index.release(found->first) == static_cast<std::int32_t>(found->second)));
                reference.live.erase(found);
            }
            else
            {
                const std::uint32_t slot = next_random(seed) % ((1u << active) + 1u);
                const auto found = reference.live.find(slot);
                const TIndex saved = index;
                const std::int32_t expected = (found == reference.live.end()) ? -1 : static_cast<std::int32_t>(found->second);
                TEST_EXPECT(ctx, (index.allocation_order(slot) == expected));
                TEST_EXPECT(ctx, (index.release(slot) == expected));
                if (expected < 0) TEST_EXPECT(ctx, same_index(index, saved));
                else reference.live.erase(found);
            }
            check_index(ctx, index, reference);
        }
    }
}

//  Explore all sequences of allocate(any order)/free(any live start) for a tiny
//  backing. Copies let each branch exercise the same starting state independently.
template<typename TWord>
void exhaust_small(TTestContext& ctx, const buddy_detail::TPackedIndex<TWord>& index,
    const SReference& reference, const std::uint32_t remaining)
{
    check_index(ctx, index, reference);
    for (std::uint32_t slot = 0u; slot <= (1u << reference.active_order); ++slot)
    {
        const auto found = reference.live.find(slot);
        const std::int32_t expected = (found == reference.live.end()) ? -1 : static_cast<std::int32_t>(found->second);
        TEST_EXPECT(ctx, (index.allocation_order(slot) == expected));
    }
    if (remaining == 0u) return;
    for (std::uint32_t order = 0u; order <= reference.active_order; ++order)
    {
        auto child = index;
        SReference model = reference;
        const std::uint32_t expected = model.allocate(order);
        const std::uint32_t actual = child.allocate(order);
        if (expected == UINT32_MAX)
        {
            TEST_EXPECT(ctx, (actual == child.k_no_slot));
            TEST_EXPECT(ctx, same_index(child, index));
        }
        else
        {
            TEST_EXPECT(ctx, (actual == expected));
            exhaust_small(ctx, child, model, (remaining - 1u));
        }
    }
    for (const auto& entry : reference.live)
    {
        auto child = index;
        SReference model = reference;
        TEST_EXPECT(ctx, (child.release(entry.first) == static_cast<std::int32_t>(entry.second)));
        model.live.erase(entry.first);
        exhaust_small(ctx, child, model, (remaining - 1u));
    }
}

template<typename TWord>
void test_full_capacity(TTestContext& ctx)
{
    using TIndex = buddy_detail::TPackedIndex<TWord>;
    TIndex index;
    index.reset(TIndex::k_max_order);
    for (std::uint32_t slot = 0u; slot < TIndex::k_slot_count; ++slot)
    {
        TEST_EXPECT(ctx, (index.allocate(0u) == slot));
    }
    TEST_EXPECT(ctx, (index.live_slots == TIndex::k_slot_count));
    TEST_EXPECT(ctx, (index.available == 0u));
    TEST_EXPECT(ctx, (index.allocate(0u) == TIndex::k_no_slot));
    //  An odd multiplier permutes the whole power-of-two slot domain.
    for (std::uint32_t ordinal = 0u; ordinal < TIndex::k_slot_count; ++ordinal)
    {
        const std::uint32_t slot = (ordinal * 65521u) & (TIndex::k_slot_count - 1u);
        TEST_EXPECT(ctx, (index.release(slot) == 0));
    }
    check_index(ctx, index, SReference{ TIndex::k_max_order, {} });
}

template<typename TWord>
void test_owner(TTestContext& ctx, memory::CMemoryContext& context, tests::TAllocationFailureFixture& fixture)
{
    using TAllocator = TBuddyAllocator<TWord>;
    static_assert(!std::is_copy_constructible_v<TAllocator>);
    static_assert(!std::is_copy_assignable_v<TAllocator>);
    static_assert(std::is_nothrow_move_constructible_v<TAllocator>);
    static_assert(std::is_nothrow_move_assignable_v<TAllocator>);
    static_assert(std::is_nothrow_destructible_v<TAllocator>);
    static_assert((sizeof(TAllocator) == sizeof(memory::CMemoryToken)));

    TAllocator owner;
    std::uint8_t unrelated = 0u;
    TEST_EXPECT(ctx, (!owner.is_ready() && owner.is_empty()));
    TEST_EXPECT(ctx, (owner.allocate(1u) == nullptr));
    TEST_EXPECT(ctx, (owner.free(nullptr) == EFreeResult::no_action));
    TEST_EXPECT(ctx, (owner.free(&unrelated) == EFreeResult::outside_backing));
    TEST_EXPECT(ctx, (owner.allocation_size(&unrelated) == 0u));
    TEST_EXPECT(ctx, (owner.total_owned_size() == 0u));
    TEST_EXPECT(ctx, (owner.slot_size() == 0u));
    TEST_EXPECT(ctx, (owner.backing_size() == 0u));
    TEST_EXPECT(ctx, (owner.largest_available_block() == 0u));
    owner.reset();
    owner.deallocate();

    const std::size_t attempts = fixture.attempts;
    const SBuddyConfig invalid[]{
        { 0u, 1024u }, { 16u, 0u }, { 17u, 31u },
        { 1u, std::size_t{ TAllocator::k_max_slots } * 2u },
        { std::numeric_limits<std::size_t>::max(), 1024u },
        { 1u, std::numeric_limits<std::size_t>::max() },
        { memory::k_byte_size_ceiling, memory::k_byte_size_ceiling },
        { 65536u, memory::k_byte_size_ceiling },
        //  A payload at the ceiling leaves no room for management.
        { memory::k_byte_size_ceiling / 2u, memory::k_byte_size_ceiling }
    };
    for (const SBuddyConfig& config : invalid)
    {
        TEST_EXPECT(ctx, !owner.initialise(config, context));
        TEST_EXPECT(ctx, !owner.is_ready());
    }
    TEST_EXPECT(ctx, (fixture.attempts == attempts));
    fixture.reject_all = true;
    TEST_EXPECT(ctx, !owner.initialise({ 16u, 4096u }, context));
    TEST_EXPECT(ctx, (!owner.is_ready() && context.is_attribution_empty()));
    fixture.reject_all = false;
    TEST_EXPECT(ctx, owner.initialise({ 17u, 5000u }, context));
    if (!owner.is_ready()) return;
    TEST_EXPECT(ctx, (owner.slot_size() == 32u));
    TEST_EXPECT(ctx, (owner.backing_size() == 4096u));
    TEST_EXPECT(ctx, (owner.total_owned_size() >= (TAllocator::k_management_size + owner.backing_size())));
    TEST_EXPECT(ctx, (context.get_live_allocation_count() == 1u));
    TEST_EXPECT(ctx, (context.get_live_allocated_bytes() == owner.total_owned_size()));
    TEST_EXPECT(ctx, (owner.memory_attribution().source == &context));
    TEST_EXPECT(ctx, (owner.memory_attribution().allocation_count == 1u));
    TEST_EXPECT(ctx, !owner.initialise({ 16u, 1024u }, context));

    std::uint8_t* const base = static_cast<std::uint8_t*>(owner.allocate(4096u));
    TEST_EXPECT(ctx, (base != nullptr));
    if (base == nullptr) return;
    TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(base) % 32u) == 0u));
    TEST_EXPECT(ctx, (owner.allocation_size(base) == 4096u));
    const std::uint64_t cost = owner.total_owned_size();
    const std::size_t backed_attempts = fixture.attempts;
    fixture.reject_all = true;
    TEST_EXPECT(ctx, (owner.allocate(1u) == nullptr));
    TEST_EXPECT(ctx, (owner.allocate(0u) == nullptr));
    TEST_EXPECT(ctx, (owner.allocate(std::numeric_limits<std::size_t>::max()) == nullptr));
    const auto reject = [&](void* const pointer, const EFreeResult result)
    {
        TEST_EXPECT(ctx, (owner.free(pointer) == result));
        TEST_EXPECT(ctx, (owner.allocation_size(pointer) == 0u));
        TEST_EXPECT(ctx, (owner.allocation_count() == 1u));
        TEST_EXPECT(ctx, (owner.allocated_size() == 4096u));
        TEST_EXPECT(ctx, (owner.largest_available_block() == 0u));
        TEST_EXPECT(ctx, (owner.total_owned_size() == cost));
    };
    reject((base + 1u), EFreeResult::invalid_allocation);
    reject((base + 32u), EFreeResult::invalid_allocation);
    reject((base + 4095u), EFreeResult::invalid_allocation);
    reject((base + 4096u), EFreeResult::outside_backing);
    reject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(base) - 1u), EFreeResult::outside_backing);
    reject(&unrelated, EFreeResult::outside_backing);
    reject(reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()), EFreeResult::outside_backing);
    reject(nullptr, EFreeResult::no_action);
    TEST_EXPECT(ctx, !owner.initialise({ 16u, 1024u }, context));
    TEST_EXPECT(ctx, (owner.free(base) == EFreeResult::released));
    TEST_EXPECT(ctx, (owner.free(base) == EFreeResult::invalid_allocation));
    TEST_EXPECT(ctx, (owner.largest_available_block() == 4096u));
    TEST_EXPECT(ctx, (owner.allocate(1u) == base));
    TEST_EXPECT(ctx, (owner.allocate(33u) == (base + 64u)));
    TEST_EXPECT(ctx, (owner.allocate(32u) == (base + 32u)));
    TEST_EXPECT(ctx, (owner.allocated_size() == 128u));
    std::size_t visited = 0u;
    std::size_t visited_bytes = 0u;
    owner.visit_allocations([&](const void* const pointer, const std::size_t bytes)
    {
        TEST_EXPECT(ctx, (pointer == (base + (visited * 32u))));
        TEST_EXPECT(ctx, (owner.allocation_size(pointer) == bytes));
        ++visited;
        visited_bytes += bytes;
    });
    TEST_EXPECT(ctx, ((visited == 3u) && (visited_bytes == 128u)));
    TEST_EXPECT(ctx, (owner.free(base) == EFreeResult::released));
    TEST_EXPECT(ctx, (owner.free(base) == EFreeResult::invalid_allocation));
    TEST_EXPECT(ctx, (owner.allocation_size(base + 32u) == 32u));
    owner.reset();
    TEST_EXPECT(ctx, (owner.is_empty() && owner.is_ready()));
    TEST_EXPECT(ctx, (owner.allocation_size(base + 64u) == 0u));
    TEST_EXPECT(ctx, (owner.allocate(4096u) == base));
    TEST_EXPECT(ctx, (fixture.attempts == backed_attempts));
    fixture.reject_all = false;
    std::memset(base, 0x5au, 4096u);

    TAllocator moved{ std::move(owner) };
    TEST_EXPECT(ctx, (!owner.is_ready() && owner.is_empty()));
    TEST_EXPECT(ctx, (moved.allocation_size(base) == 4096u));
    TEST_EXPECT(ctx, (moved.memory_attribution().source == &context));
    TEST_EXPECT(ctx, owner.initialise({ 1u, 1u }, context));
    TEST_EXPECT(ctx, (owner.allocate(1u) != nullptr));
    owner = std::move(moved);
    TEST_EXPECT(ctx, (!moved.is_ready() && moved.is_empty()));
    TEST_EXPECT(ctx, (context.get_live_allocation_count() == 1u));
    TAllocator& self = owner;
    owner = std::move(self);
    TEST_EXPECT(ctx, (owner.allocation_size(base) == 4096u));
    TEST_EXPECT(ctx, ((base[0] == 0x5au) && (base[4095u] == 0x5au)));
    owner.deallocate();
    TEST_EXPECT(ctx, (!owner.is_ready() && owner.is_empty()));
    TEST_EXPECT(ctx, context.is_attribution_empty());
    TEST_EXPECT(ctx, owner.initialise({ 16u, 1024u }, context));
}

template<typename TWord>
void test_cross_context_move(TTestContext& ctx)
{
    using TAllocator = TBuddyAllocator<TWord>;
    tests::TAllocationFailureFixture source_fixture;
    tests::TAllocationFailureFixture destination_fixture;
    memory::CMemoryAllocator source_allocator{ &source_fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryAllocator destination_allocator{ &destination_fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext source_context{ source_allocator };
    memory::CMemoryContext destination_context{ destination_allocator };
    {
        //  Unrelated live storage establishes nonzero baselines in each context.
        memory::CMemoryToken source_sentinel{ 1u, 8u, &source_context };
        memory::CMemoryToken destination_sentinel{ 1u, 8u, &destination_context };
        TEST_EXPECT(ctx, source_sentinel.allocate(64u));
        TEST_EXPECT(ctx, destination_sentinel.allocate(128u));
        const std::uint32_t source_count = source_context.get_live_allocation_count();
        const std::uint32_t destination_count = destination_context.get_live_allocation_count();
        const std::uint64_t source_bytes = source_context.get_live_allocated_bytes();
        const std::uint64_t destination_bytes = destination_context.get_live_allocated_bytes();
        {
            TAllocator destination;
            TEST_EXPECT(ctx, destination.initialise({ 16u, 1024u }, destination_context));
            TEST_EXPECT(ctx, (destination.allocate(17u) != nullptr));
            TEST_EXPECT(ctx, (destination_context.get_live_allocation_count() == (destination_count + 1u)));
            TEST_EXPECT(ctx, (destination_context.get_live_allocated_bytes() == (destination_bytes + destination.total_owned_size())));
            std::uint8_t* transferred = nullptr;
            std::uint64_t transferred_cost = 0u;
            {
                TAllocator source;
                TEST_EXPECT(ctx, source.initialise({ 32u, 4096u }, source_context));
                transferred = static_cast<std::uint8_t*>(source.allocate(65u));
                TEST_EXPECT(ctx, (transferred != nullptr));
                if (transferred == nullptr) return;
                std::memset(transferred, 0xa7, 128u);
                transferred_cost = source.total_owned_size();
                const std::size_t source_attempts = source_fixture.attempts;
                const std::size_t destination_attempts = destination_fixture.attempts;
                source_fixture.reject_all = true;
                destination_fixture.reject_all = true;

                destination = std::move(source);

                TEST_EXPECT(ctx, (!source.is_ready() && source.is_empty()));
                TEST_EXPECT(ctx, (source.memory_attribution().source_state == memory::EMemorySourceState::empty));
                TEST_EXPECT(ctx, (source.memory_attribution().allocation_size == 0u));
                TEST_EXPECT(ctx, (source.allocation_size(transferred) == 0u));
                TEST_EXPECT(ctx, (destination.memory_attribution().source == &source_context));
                TEST_EXPECT(ctx, (destination.memory_attribution().allocation_count == 1u));
                TEST_EXPECT(ctx, (destination.total_owned_size() == transferred_cost));
                TEST_EXPECT(ctx, ((destination.slot_size() == 32u) && (destination.backing_size() == 4096u)));
                TEST_EXPECT(ctx, ((destination.allocation_count() == 1u) && (destination.allocated_size() == 128u)));
                TEST_EXPECT(ctx, (destination.allocation_size(transferred) == 128u));
                TEST_EXPECT(ctx, (source_context.get_live_allocation_count() == (source_count + 1u)));
                TEST_EXPECT(ctx, (source_context.get_live_allocated_bytes() == (source_bytes + transferred_cost)));
                TEST_EXPECT(ctx, (destination_context.get_live_allocation_count() == destination_count));
                TEST_EXPECT(ctx, (destination_context.get_live_allocated_bytes() == destination_bytes));
                TEST_EXPECT(ctx, (source_fixture.attempts == source_attempts));
                TEST_EXPECT(ctx, (destination_fixture.attempts == destination_attempts));

                //  Reuse the moved-from object with the other explicit context.
                destination_fixture.reject_all = false;
                TEST_EXPECT(ctx, source.initialise({ 8u, 256u }, destination_context));
                TEST_EXPECT(ctx, (source.allocate(1u) != nullptr));
                TEST_EXPECT(ctx, (source.memory_attribution().source == &destination_context));
                TEST_EXPECT(ctx, (destination_context.get_live_allocation_count() == (destination_count + 1u)));
                TEST_EXPECT(ctx, (destination_context.get_live_allocated_bytes() == (destination_bytes + source.total_owned_size())));
            }
            //  Destruction of the reused source must not release the transferred
            //  payload. A logical free also retains its physical backing charge.
            TEST_EXPECT(ctx, (destination_context.get_live_allocation_count() == destination_count));
            TEST_EXPECT(ctx, (destination_context.get_live_allocated_bytes() == destination_bytes));
            TEST_EXPECT(ctx, (destination.allocation_size(transferred) == 128u));
            TEST_EXPECT(ctx, std::all_of(transferred, (transferred + 128u), [](const std::uint8_t value) { return value == 0xa7u; }));
            TEST_EXPECT(ctx, (destination.free(transferred) == EFreeResult::released));
            TEST_EXPECT(ctx, (source_context.get_live_allocation_count() == (source_count + 1u)));
            TEST_EXPECT(ctx, (source_context.get_live_allocated_bytes() == (source_bytes + transferred_cost)));
            TEST_EXPECT(ctx, (destination.allocate(65u) == transferred));
        }
        //  The transferred backing is released on destination destruction,
        //  leaving only the unrelated sentinel in its original source context.
        TEST_EXPECT(ctx, (source_context.get_live_allocation_count() == source_count));
        TEST_EXPECT(ctx, (source_context.get_live_allocated_bytes() == source_bytes));
        TEST_EXPECT(ctx, (destination_context.get_live_allocation_count() == destination_count));
        TEST_EXPECT(ctx, (destination_context.get_live_allocated_bytes() == destination_bytes));
    }
    TEST_EXPECT(ctx, source_context.is_attribution_empty());
    TEST_EXPECT(ctx, destination_context.is_attribution_empty());
}

template<typename TWord>
void test_owner_configurations(TTestContext& ctx, memory::CMemoryContext& context)
{
    using TAllocator = TBuddyAllocator<TWord>;
    const std::size_t requested_slots[]{ 1u, 2u, 3u, 15u, 16u, 17u, 31u, 32u, 33u, 127u, 128u, 129u, 4095u, 4096u, 4097u };
    for (const std::size_t requested_slot : requested_slots)
    {
        const std::size_t normalized = bit_ops::round_up_to_pow2(requested_slot);
        for (const std::size_t budget : { normalized, (normalized * 2u) - 1u, normalized * 2u, (normalized * 2u) + 1u })
        {
            TAllocator owner;
            TEST_EXPECT(ctx, owner.initialise({ requested_slot, budget }, context));
            TEST_EXPECT(ctx, (owner.slot_size() == normalized));
            TEST_EXPECT(ctx, (owner.backing_size() == bit_ops::hi_bit_mask(budget)));
            void* const pointer = owner.allocate(requested_slot);
            TEST_EXPECT(ctx, (pointer != nullptr));
            TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(pointer) % normalized) == 0u));
            TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(pointer) % 128u) == 0u));
            TEST_EXPECT(ctx, (owner.allocation_size(pointer) == normalized));
        }
    }
    for (std::uint32_t order = 0u; order <= TAllocator::k_max_order; ++order)
    {
        TAllocator owner;
        const std::size_t backing = std::size_t{ 16u } << order;
        TEST_EXPECT(ctx, owner.initialise({ 16u, backing }, context));
        std::uint8_t* const base = static_cast<std::uint8_t*>(owner.allocate(backing));
        TEST_EXPECT(ctx, (base != nullptr));
        TEST_EXPECT(ctx, (owner.allocation_size(base) == backing));
        if (base == nullptr) continue;
        TEST_EXPECT(ctx, (owner.free(base + backing) == EFreeResult::outside_backing));
        owner.reset();
        TEST_EXPECT(ctx, (owner.allocate(1u) == base));
        TEST_EXPECT(ctx, (owner.free(base) == EFreeResult::released));
        TEST_EXPECT(ctx, (owner.largest_available_block() == backing));
    }
}

template<typename TWord>
void test_owner_alignment(TTestContext& ctx, memory::CMemoryContext& context)
{
    //  Occupy the first small slot so larger blocks must come from nonzero
    //  offsets. Check every remaining block, including rounded byte requests.
    for (const std::size_t slot : { 1u, 8u, 16u, 64u, 128u, 256u })
    {
        TBuddyAllocator<TWord> owner;
        TEST_EXPECT(ctx, owner.initialise({ slot, 4096u }, context));
        for (std::size_t extent = slot; extent <= 1024u; extent *= 2u)
        {
            owner.reset();
            void* const first = owner.allocate(1u);
            TEST_EXPECT(ctx, (first != nullptr));
            if (first == nullptr) return;
            TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(first) % 128u) == 0u));
            const std::size_t alignment = std::min(extent, std::max(slot, std::size_t{ 128u }));
            const std::size_t requested = (extent == slot) ? 1u : ((extent / 2u) + 1u);
            std::size_t count = 0u;
            while (void* const pointer = owner.allocate(requested))
            {
                TEST_EXPECT(ctx, (pointer != first));
                TEST_EXPECT(ctx, (owner.allocation_size(pointer) == extent));
                TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0u));
                ++count;
            }
            TEST_EXPECT(ctx, (count == ((4096u / extent) - 1u)));
        }
    }
}

template<typename TWord>
void test_owner_mixed(TTestContext& ctx, memory::CMemoryContext& context)
{
    using TAllocator = TBuddyAllocator<TWord>;
    TAllocator owner;
    const std::size_t backing = std::size_t{ 16u } << TAllocator::k_max_order;
    TEST_EXPECT(ctx, owner.initialise({ 16u, backing }, context));
    std::uint8_t* const base = static_cast<std::uint8_t*>(owner.allocate(backing));
    TEST_EXPECT(ctx, (base != nullptr));
    if (base == nullptr) return;
    owner.reset();
    SReference model{ TAllocator::k_max_order, {} };
    std::uint32_t seed = 0xc001b33fu;
    for (std::uint32_t step = 0u; step < 600u; ++step)
    {
        if (model.live.empty() || ((next_random(seed) % 3u) != 0u))
        {
            const std::uint32_t order = next_random(seed) % (TAllocator::k_max_order + 1u);
            const std::size_t rounded = std::size_t{ 16u } << order;
            //  Exercise the upward rounding on byte requests as well as the
            //  index oracle's exact orders. Successful blocks receive a tag.
            const std::size_t requested = (rounded / 2u) + 1u;
            const std::uint32_t slot = model.allocate(order);
            void* const pointer = owner.allocate(requested);
            if (slot == UINT32_MAX)
            {
                TEST_EXPECT(ctx, (pointer == nullptr));
            }
            else
            {
                TEST_EXPECT(ctx, (pointer == (base + (std::size_t{ slot } * 16u))));
                TEST_EXPECT(ctx, (owner.allocation_size(pointer) == rounded));
                if (pointer == nullptr) return;
                std::memset(pointer, static_cast<int>((slot % 251u) + 1u), rounded);
            }
        }
        else
        {
            auto found = model.live.begin();
            const std::size_t ordinal = next_random(seed) % model.live.size();
            for (std::size_t i = 0u; i < ordinal; ++i) ++found;
            TEST_EXPECT(ctx, (owner.free(base + (std::size_t{ found->first } * 16u)) == EFreeResult::released));
            model.live.erase(found);
        }
        std::size_t bytes = 0u;
        std::size_t count = 0u;
        auto expected = model.live.begin();
        owner.visit_allocations([&](const void* const pointer, const std::size_t size)
        {
            TEST_EXPECT(ctx, (expected != model.live.end()));
            if (expected == model.live.end()) return;
            TEST_EXPECT(ctx, (pointer == (base + (std::size_t{ expected->first } * 16u))));
            TEST_EXPECT(ctx, (size == (std::size_t{ 16u } << expected->second)));
            const std::uint8_t* const data = static_cast<const std::uint8_t*>(pointer);
            const std::uint8_t tag = static_cast<std::uint8_t>((expected->first % 251u) + 1u);
            TEST_EXPECT(ctx, ((data[0] == tag) && (data[size - 1u] == tag)));
            bytes += size;
            ++count;
            ++expected;
        });
        TEST_EXPECT(ctx, (expected == model.live.end()));
        TEST_EXPECT(ctx, (owner.allocation_count() == count));
        TEST_EXPECT(ctx, (owner.allocated_size() == bytes));
        TEST_EXPECT(ctx, (context.get_live_allocation_count() == 1u));
        TEST_EXPECT(ctx, (context.get_live_allocated_bytes() == owner.total_owned_size()));
    }
    owner.reset();
    TEST_EXPECT(ctx, (owner.allocate(backing) == base));
}

template<typename TWord>
void run_word_tests(TTestContext& ctx, memory::CMemoryContext& context, tests::TAllocationFailureFixture& fixture)
{
    test_literal_geometry<TWord>(ctx);
    test_index_orders<TWord>(ctx);
    test_index_mixed<TWord>(ctx);
    buddy_detail::TPackedIndex<TWord> small;
    small.reset(2u);
    exhaust_small(ctx, small, SReference{ 2u, {} }, 7u);
    test_full_capacity<TWord>(ctx);
    test_owner<TWord>(ctx, context, fixture);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_cross_context_move<TWord>(ctx);
    test_owner_configurations<TWord>(ctx, context);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_owner_alignment<TWord>(ctx, context);
    TEST_EXPECT(ctx, context.is_attribution_empty());
    test_owner_mixed<TWord>(ctx, context);
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

}   //  namespace

int run_buddy_allocator_tests()
{
    TTestContext ctx;
    tests::TAllocationFailureFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    //  A rejecting ambient context ensures all storage uses the explicit owner.
    tests::TAllocationFailureFixture ambient_fixture;
    ambient_fixture.reject_all = true;
    memory::CMemoryAllocator ambient_allocator{ &ambient_fixture, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext ambient{ ambient_allocator };
    tests::TMemoryContextScope scope{ &ambient };
    run_word_tests<std::uint32_t>(ctx, context, fixture);
    run_word_tests<std::uint64_t>(ctx, context, fixture);
    TEST_EXPECT(ctx, (ambient_fixture.attempts == 0u));
    TEST_EXPECT(ctx, ambient.is_attribution_empty());
    return ctx.report("BuddyAllocator");
}
