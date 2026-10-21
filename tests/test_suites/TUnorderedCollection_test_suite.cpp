//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TUnorderedCollection_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    14 Jul 26

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

#include "containers/TUnorderedCollection.hpp"
#include "containers/TPodUnorderedSlots.hpp"
#include "tests/test_suites/TUnorderedCollection_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/collection_growth_helpers.hpp"

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

using TCollection = TUnorderedCollection<TTracked>;

std::size_t count_live_slots(const TCollection& collection) noexcept
{
    std::size_t count = 0u;
    for (std::int32_t slot = collection.first_live(); slot >= 0; slot = collection.next_live(slot))
    {
        ++count;
    }
    return count;
}

void test_default_state_and_bounds(TTestContext& ctx)
{
    static_assert(std::is_same_v<decltype(std::declval<TCollection&>().get_object(0)), TTracked*>);
    static_assert(std::is_same_v<decltype(std::declval<const TCollection&>().get_object(0)), const TTracked*>);

    TCollection collection;
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, !collection.is_ready());
    TEST_EXPECT(ctx, collection.first_live() == -1);
    TEST_EXPECT(ctx, collection.last_live() == -1);
    TEST_EXPECT(ctx, collection.prev_live(0) == -1);
    TEST_EXPECT(ctx, collection.next_live(0) == -1);
    TEST_EXPECT(ctx, collection.get_object(-1) == nullptr);
    TEST_EXPECT(ctx, collection.get_object(0) == nullptr);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(nullptr) == -1);
    TEST_EXPECT(ctx, collection.emplace(1, 1u) == -1);
    TEST_EXPECT(ctx, !collection.erase(-1));
    TEST_EXPECT(ctx, !collection.erase(0));

    TEST_EXPECT(ctx, collection.initialise(4u, 4u));
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_ready());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, count_live_slots(collection) == 0u);
    TEST_EXPECT(ctx, collection.check_integrity());
    TEST_EXPECT(ctx, collection.get_object(64) == nullptr);
    TEST_EXPECT(ctx, !collection.erase(64));

    collection.deallocate();
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, !collection.is_ready());

    TEST_EXPECT(ctx, collection.initialise(2u, 64u));
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_ready());
    TEST_EXPECT(ctx, collection.is_empty());
}

void test_emplace_erase_and_lookup(TTestContext& ctx)
{
    TTracked::reset_counts();

    TCollection collection;
    TEST_EXPECT(ctx, collection.initialise(0u, 4u));

    const std::int32_t first_slot = collection.emplace(11, 1u);
    const std::int32_t second_slot = collection.emplace(22, 2u);
    TEST_EXPECT(ctx, first_slot >= 0);
    TEST_EXPECT(ctx, second_slot >= 0);
    TEST_EXPECT(ctx, first_slot != second_slot);
    TEST_EXPECT(ctx, count_live_slots(collection) == 2u);

    TTracked* const first_object = collection.get_object(first_slot);
    TTracked* const second_object = collection.get_object(second_slot);
    TEST_EXPECT(ctx, first_object != nullptr);
    TEST_EXPECT(ctx, second_object != nullptr);
    TEST_EXPECT(ctx, first_object->value == 11);
    TEST_EXPECT(ctx, second_object->value == 22);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(first_object) == first_slot);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(second_object) == second_slot);
    TEST_EXPECT(ctx, TTracked::live_count == 2);
    TEST_EXPECT(ctx, TTracked::construction_count == 2);
    TEST_EXPECT(ctx, TTracked::destruction_count == 0);

    TEST_EXPECT(ctx, collection.erase(first_slot));
    TEST_EXPECT(ctx, collection.get_object(first_slot) == nullptr);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(first_object) == -1);
    TEST_EXPECT(ctx, count_live_slots(collection) == 1u);
    TEST_EXPECT(ctx, TTracked::live_count == 1);
    TEST_EXPECT(ctx, TTracked::destruction_count == 1);

    const std::int32_t third_slot = collection.emplace(33, 3u);
    TTracked* const third_object = collection.get_object(third_slot);
    TEST_EXPECT(ctx, third_slot >= 0);
    TEST_EXPECT(ctx, third_object != nullptr);
    TEST_EXPECT(ctx, third_object->value == 33);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(third_object) == third_slot);
    TEST_EXPECT(ctx, count_live_slots(collection) == 2u);
    TEST_EXPECT(ctx, collection.check_integrity());

    collection.deallocate();
    TEST_EXPECT(ctx, TTracked::live_count == 0);
    TEST_EXPECT(ctx, TTracked::construction_count == 3);
    TEST_EXPECT(ctx, TTracked::destruction_count == 3);
}

void test_stable_addresses_across_growth(TTestContext& ctx)
{
    TTracked::reset_counts();

    TCollection collection;
    TEST_EXPECT(ctx, collection.initialise(1u, 4u));

    static constexpr std::size_t k_total = 40u;
    std::int32_t slots[k_total]{};
    TTracked* pointers[k_total]{};

    for (std::size_t i = 0u; i < k_total; ++i)
    {
        slots[i] = collection.emplace(static_cast<int>(100u + i), static_cast<std::uint32_t>(i + 1u));
        pointers[i] = collection.get_object(slots[i]);
        TEST_EXPECT(ctx, slots[i] >= 0);
        TEST_EXPECT(ctx, pointers[i] != nullptr);
        TEST_EXPECT(ctx, pointers[i]->value == static_cast<int>(100u + i));
    }

    TEST_EXPECT(ctx, count_live_slots(collection) == k_total);
    TEST_EXPECT(ctx, collection.check_integrity());
    TEST_EXPECT(ctx, TTracked::live_count == static_cast<int>(k_total));
    TEST_EXPECT(ctx, collection.memory_attribution().token_count == 3u);
    TEST_EXPECT(ctx, collection.memory_attribution().allocation_count != 0u);
    TEST_EXPECT(ctx, collection.memory_attribution().allocation_size != 0u);
    TEST_EXPECT(ctx, memory::can_reattribute_to(collection));
    TEST_EXPECT(ctx, memory::reattribute(collection));

    for (std::size_t i = 0u; i < k_total; ++i)
    {
        TEST_EXPECT(ctx, collection.get_object(slots[i]) == pointers[i]);
        TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(pointers[i]) == slots[i]);
        TEST_EXPECT(ctx, pointers[i]->generation == static_cast<std::uint32_t>(i + 1u));
    }

    collection.deallocate();
    TEST_EXPECT(ctx, TTracked::live_count == 0);
    TEST_EXPECT(ctx, TTracked::construction_count == static_cast<int>(k_total));
    TEST_EXPECT(ctx, TTracked::destruction_count == static_cast<int>(k_total));
}

void test_pack_and_repeated_initialise(TTestContext& ctx)
{
    TTracked::reset_counts();

    TCollection collection;
    TEST_EXPECT(ctx, collection.initialise(8u, 4u));

    const std::int32_t slot0 = collection.emplace(10, 1u);
    const std::int32_t slot1 = collection.emplace(20, 2u);
    const std::int32_t slot2 = collection.emplace(30, 3u);
    const std::int32_t slot3 = collection.emplace(40, 4u);
    const std::int32_t slot4 = collection.emplace(50, 5u);
    const std::int32_t slot5 = collection.emplace(60, 6u);

    TTracked* const object0 = collection.get_object(slot0);
    TTracked* const object2 = collection.get_object(slot2);
    TTracked* const object3 = collection.get_object(slot3);
    TTracked* const object5 = collection.get_object(slot5);

    TEST_EXPECT(ctx, collection.erase(slot1));
    TEST_EXPECT(ctx, collection.erase(slot4));
    TEST_EXPECT(ctx, TTracked::live_count == 4);
    TEST_EXPECT(ctx, TTracked::destruction_count == 2);

    collection.pack();
    TEST_EXPECT(ctx, collection.check_integrity());
    TEST_EXPECT(ctx, count_live_slots(collection) == 4u);

    const std::int32_t remapped_slot0 = collection.reverse_lookup_slot_index_scan(object0);
    const std::int32_t remapped_slot2 = collection.reverse_lookup_slot_index_scan(object2);
    const std::int32_t remapped_slot3 = collection.reverse_lookup_slot_index_scan(object3);
    const std::int32_t remapped_slot5 = collection.reverse_lookup_slot_index_scan(object5);

    TEST_EXPECT(ctx, remapped_slot0 == 0);
    TEST_EXPECT(ctx, remapped_slot2 == 1);
    TEST_EXPECT(ctx, remapped_slot3 == 2);
    TEST_EXPECT(ctx, remapped_slot5 == 3);

    TEST_EXPECT(ctx, collection.get_object(remapped_slot0) == object0);
    TEST_EXPECT(ctx, collection.get_object(remapped_slot2) == object2);
    TEST_EXPECT(ctx, collection.get_object(remapped_slot3) == object3);
    TEST_EXPECT(ctx, collection.get_object(remapped_slot5) == object5);
    TEST_EXPECT(ctx, object0->value == 10);
    TEST_EXPECT(ctx, object2->value == 30);
    TEST_EXPECT(ctx, object3->value == 40);
    TEST_EXPECT(ctx, object5->value == 60);

    TEST_EXPECT(ctx, collection.initialise(3u, 64u));
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_ready());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, count_live_slots(collection) == 0u);
    TEST_EXPECT(ctx, TTracked::live_count == 0);
    TEST_EXPECT(ctx, TTracked::construction_count == 6);
    TEST_EXPECT(ctx, TTracked::destruction_count == 6);

    const std::int32_t restarted_slot = collection.emplace(70, 7u);
    TEST_EXPECT(ctx, restarted_slot >= 0);
    TEST_EXPECT(ctx, collection.get_object(restarted_slot) != nullptr);
    TEST_EXPECT(ctx, collection.get_object(restarted_slot)->value == 70);

    collection.deallocate();
    TEST_EXPECT(ctx, TTracked::live_count == 0);
    TEST_EXPECT(ctx, TTracked::construction_count == 7);
    TEST_EXPECT(ctx, TTracked::destruction_count == 7);
}


template<bool Pod>
void test_failed_growth(TTestContext& ctx)
{
    using TContainer = std::conditional_t<Pod, TPodUnorderedSlots<std::int32_t>, TCollection>;
    const auto insert = [](TContainer& container, const std::int32_t value) noexcept
    {
        if constexpr (Pod)
        {
            return container.insert(value);
        }
        else
        {
            return container.emplace(value, 1u);
        }
    };
    const auto get_value = [](const TContainer& container, const std::int32_t slot) noexcept
        -> const std::int32_t*
    {
        if constexpr (Pod)
        {
            return container.get_slot(slot);
        }
        else
        {
            const auto* const object = container.get_object(slot);
            return (object != nullptr) ? &object->value : nullptr;
        }
    };

    const auto pack = [](TContainer& container)
    {
        container.pack();
    };
    const auto check_lifetime = [](TTestContext& ctx)
    {
        TEST_EXPECT(ctx, TTracked::live_count == 0);
    };
    const std::size_t failures = tests::exercise_failed_growth<Pod, TContainer>(
        ctx, insert, get_value, pack, check_lifetime);
    TEST_EXPECT(ctx, failures >= 2u);
}

}   //  namespace

int run_unordered_collection_tests()
{
    TTestContext ctx;
    test_failed_growth<true>(ctx);
    test_failed_growth<false>(ctx);
    test_default_state_and_bounds(ctx);
    test_emplace_erase_and_lookup(ctx);
    test_stable_addresses_across_growth(ctx);
    test_pack_and_repeated_initialise(ctx);

    return ctx.report("TUnorderedCollection");
}
