
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TOrderedCollection_test_suite.cpp
//  Primary implementation: OpenAI tools
//  Used, occasionally adjusted, and accepted by: Ritchie Brannan
//  Date:   14 Jul 26

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

#include "containers/TOrderedCollection.hpp"
#include "tests/test_suites/TOrderedCollection_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using TTestContext = tests::TTestContext;

struct TTrackedKey
{
    std::int32_t value{ 0 };

    [[nodiscard]] std::int32_t relationship(const TTrackedKey& other) const noexcept
    {
        return (value < other.value) ? -1 : ((value > other.value) ? 1 : 0);
    }
};

static_assert(std::is_trivially_copyable_v<TTrackedKey>);

struct TTrackedValue
{
    TTrackedValue(const std::int32_t in_id, const std::int32_t in_payload, const std::uint32_t in_generation) noexcept
        : id(in_id)
        , payload(in_payload)
        , generation(in_generation)
    {
        ++live_count;
        ++construction_count;
    }

    ~TTrackedValue() noexcept
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

    std::int32_t id{ 0 };
    std::int32_t payload{ 0 };
    std::uint32_t generation{ 0u };

    static int live_count;
    static int construction_count;
    static int destruction_count;
};

int TTrackedValue::live_count = 0;
int TTrackedValue::construction_count = 0;
int TTrackedValue::destruction_count = 0;

using TCollection = TOrderedCollection<TTrackedValue, TTrackedKey>;

void test_default_state_and_initialise(TTestContext& ctx)
{
    TCollection collection;
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, !collection.is_ready());
    TEST_EXPECT(ctx, collection.get_object(std::int32_t{ 0 }) == nullptr);
    TEST_EXPECT(ctx, collection.key_at_slot(std::int32_t{ 0 }) == nullptr);
    TEST_EXPECT(ctx, collection.find_slot(TTrackedKey{ 5 }) == -1);
    TEST_EXPECT(ctx, !collection.erase(std::int32_t{ 0 }));

    TEST_EXPECT(ctx, collection.initialise(4u, 7u));
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_ready());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, collection.check_integrity());
    TEST_EXPECT(ctx, collection.first_live() == -1);
    TEST_EXPECT(ctx, collection.last_live() == -1);
    TEST_EXPECT(ctx, collection.key_at_slot(std::int32_t{ 0 }) == nullptr);
}

void test_ordered_insert_find_and_traversal(TTestContext& ctx)
{
    TTrackedValue::reset_counts();

    TCollection collection;
    TEST_EXPECT(ctx, collection.initialise(8u, 4u));

    const std::int32_t slot20 = collection.emplace(TTrackedKey{ 20 }, 200, 2000, 1u);
    const std::int32_t slot10 = collection.emplace(TTrackedKey{ 10 }, 100, 1000, 2u);
    const std::int32_t slot30 = collection.emplace(TTrackedKey{ 30 }, 300, 3000, 3u);
    TEST_EXPECT(ctx, slot20 >= 0);
    TEST_EXPECT(ctx, slot10 >= 0);
    TEST_EXPECT(ctx, slot30 >= 0);
    TEST_EXPECT(ctx, collection.emplace(TTrackedKey{ 20 }, 999, 9999, 4u) == -1);

    TTrackedValue* const value10 = collection.get_object(TTrackedKey{ 10 });
    TTrackedValue* const value20 = collection.get_object(slot20);
    TTrackedValue* const value30 = collection.get_object(TTrackedKey{ 30 });
    TEST_EXPECT(ctx, value10 != nullptr);
    TEST_EXPECT(ctx, value20 != nullptr);
    TEST_EXPECT(ctx, value30 != nullptr);
    TEST_EXPECT(ctx, value10->payload == 1000);
    TEST_EXPECT(ctx, value20->payload == 2000);
    TEST_EXPECT(ctx, value30->payload == 3000);
    TEST_EXPECT(ctx, collection.find_slot(TTrackedKey{ 20 }) == slot20);
    TEST_EXPECT(ctx, collection.find_slot(TTrackedKey{ 25 }) == -1);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(value20) == slot20);
    const TTrackedKey* const key10 = collection.key_at_slot(slot10);
    const TTrackedKey* const key20 = collection.key_at_slot(slot20);
    const TTrackedKey* const key30 = collection.key_at_slot(slot30);
    TEST_EXPECT(ctx, (key10 != nullptr) && (key10->value == 10));
    TEST_EXPECT(ctx, (key20 != nullptr) && (key20->value == 20));
    TEST_EXPECT(ctx, (key30 != nullptr) && (key30->value == 30));

    TEST_EXPECT(ctx, collection.first_live() == slot10);
    TEST_EXPECT(ctx, collection.next_live(slot10) == slot20);
    TEST_EXPECT(ctx, collection.next_live(slot20) == slot30);
    TEST_EXPECT(ctx, collection.prev_live(slot30) == slot20);
    TEST_EXPECT(ctx, collection.last_live() == slot30);
    TEST_EXPECT(ctx, collection.check_integrity());
    TEST_EXPECT(ctx, TTrackedValue::live_count == 3);
    TEST_EXPECT(ctx, TTrackedValue::construction_count == 3);
    TEST_EXPECT(ctx, TTrackedValue::destruction_count == 0);
}

void test_stable_pointer_preservation_across_growth(TTestContext& ctx)
{
    TTrackedValue::reset_counts();

    TCollection collection;
    TEST_EXPECT(ctx, collection.initialise(1u, 1u));

    TTrackedValue* first_address = nullptr;
    TTrackedValue* thirty_second_address = nullptr;
    TTrackedValue* sixty_fourth_address = nullptr;
    for (std::int32_t key = 0; key < 70; ++key)
    {
        const std::int32_t slot = collection.emplace(TTrackedKey{ key }, key, key * 10, static_cast<std::uint32_t>(key + 1));
        TEST_EXPECT(ctx, slot >= 0);
        TTrackedValue* const element = collection.get_object(TTrackedKey{ key });
        TEST_EXPECT(ctx, element != nullptr);
        TEST_EXPECT(ctx, element->id == key);
        TEST_EXPECT(ctx, element->payload == (key * 10));

        if (key == 0)
        {
            first_address = element;
        }
        else if (key == 31)
        {
            thirty_second_address = element;
        }
        else if (key == 63)
        {
            sixty_fourth_address = element;
        }
    }

    TEST_EXPECT(ctx, first_address != nullptr);
    TEST_EXPECT(ctx, thirty_second_address != nullptr);
    TEST_EXPECT(ctx, sixty_fourth_address != nullptr);
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 0 }) == first_address);
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 31 }) == thirty_second_address);
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 63 }) == sixty_fourth_address);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(first_address) == collection.find_slot(TTrackedKey{ 0 }));
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(sixty_fourth_address) == collection.find_slot(TTrackedKey{ 63 }));
    TEST_EXPECT(ctx, collection.check_integrity());
    TEST_EXPECT(ctx, TTrackedValue::live_count == 70);
    TEST_EXPECT(ctx, TTrackedValue::construction_count == 70);
    TEST_EXPECT(ctx, TTrackedValue::destruction_count == 0);
    TEST_EXPECT(ctx, collection.memory_token_count() == 4u);
    TEST_EXPECT(ctx, collection.memory_allocation_count() != 0u);
    TEST_EXPECT(ctx, collection.memory_allocation_size() != 0u);
    TEST_EXPECT(ctx, collection.can_reattribute_to());
    TEST_EXPECT(ctx, collection.reattribute());
}

void test_erase_sort_pack_and_stable_addresses(TTestContext& ctx)
{
    TTrackedValue::reset_counts();

    TCollection collection;
    TEST_EXPECT(ctx, collection.initialise(10u, 3u));

    const std::int32_t slot40 = collection.emplace(TTrackedKey{ 40 }, 40, 400, 1u);
    const std::int32_t slot10 = collection.emplace(TTrackedKey{ 10 }, 10, 100, 2u);
    const std::int32_t slot30 = collection.emplace(TTrackedKey{ 30 }, 30, 300, 3u);
    const std::int32_t slot20 = collection.emplace(TTrackedKey{ 20 }, 20, 200, 4u);
    TEST_EXPECT(ctx, slot40 >= 0);
    TEST_EXPECT(ctx, slot10 >= 0);
    TEST_EXPECT(ctx, slot30 >= 0);
    TEST_EXPECT(ctx, slot20 >= 0);

    TTrackedValue* const address10 = collection.get_object(TTrackedKey{ 10 });
    TTrackedValue* const address20 = collection.get_object(TTrackedKey{ 20 });
    TTrackedValue* const address30 = collection.get_object(TTrackedKey{ 30 });
    TTrackedValue* const address40 = collection.get_object(TTrackedKey{ 40 });

    TEST_EXPECT(ctx, collection.erase(slot30));
    TEST_EXPECT(ctx, !collection.erase(slot30));
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 30 }) == nullptr);
    TEST_EXPECT(ctx, collection.key_at_slot(slot30) == nullptr);
    TEST_EXPECT(ctx, TTrackedValue::live_count == 3);
    TEST_EXPECT(ctx, TTrackedValue::construction_count == 4);
    TEST_EXPECT(ctx, TTrackedValue::destruction_count == 1);

    collection.sort_and_pack();
    TEST_EXPECT(ctx, collection.check_integrity());
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 10 }) == address10);
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 20 }) == address20);
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 40 }) == address40);
    TEST_EXPECT(ctx, collection.find_slot(TTrackedKey{ 10 }) == 0);
    TEST_EXPECT(ctx, collection.find_slot(TTrackedKey{ 20 }) == 1);
    TEST_EXPECT(ctx, collection.find_slot(TTrackedKey{ 40 }) == 2);
    const TTrackedKey* const packed_key10 = collection.key_at_slot(0);
    const TTrackedKey* const packed_key20 = collection.key_at_slot(1);
    const TTrackedKey* const packed_key40 = collection.key_at_slot(2);
    TEST_EXPECT(ctx, (packed_key10 != nullptr) && (packed_key10->value == 10));
    TEST_EXPECT(ctx, (packed_key20 != nullptr) && (packed_key20->value == 20));
    TEST_EXPECT(ctx, (packed_key40 != nullptr) && (packed_key40->value == 40));
    TEST_EXPECT(ctx, collection.first_live() == 0);
    TEST_EXPECT(ctx, collection.next_live(0) == 1);
    TEST_EXPECT(ctx, collection.next_live(1) == 2);
    TEST_EXPECT(ctx, collection.last_live() == 2);
    TEST_EXPECT(ctx, collection.prev_live(2) == 1);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(address10) == 0);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(address20) == 1);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(address40) == 2);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(address30) == -1);
}

void test_repeated_initialise_and_deallocate(TTestContext& ctx)
{
    TTrackedValue::reset_counts();

    TCollection collection;
    TEST_EXPECT(ctx, collection.initialise(6u, 2u));
    TEST_EXPECT(ctx, collection.emplace(TTrackedKey{ 1 }, 1, 10, 1u) >= 0);
    TEST_EXPECT(ctx, collection.emplace(TTrackedKey{ 2 }, 2, 20, 2u) >= 0);
    TEST_EXPECT(ctx, TTrackedValue::live_count == 2);

    TEST_EXPECT(ctx, collection.initialise(3u, 64u));
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_ready());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, collection.first_live() == -1);
    TEST_EXPECT(ctx, TTrackedValue::live_count == 0);
    TEST_EXPECT(ctx, TTrackedValue::construction_count == 2);
    TEST_EXPECT(ctx, TTrackedValue::destruction_count == 2);

    TEST_EXPECT(ctx, collection.emplace(TTrackedKey{ 7 }, 7, 70, 3u) >= 0);
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 7 }) != nullptr);
    collection.deallocate();
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, !collection.is_ready());
    TEST_EXPECT(ctx, collection.get_object(TTrackedKey{ 7 }) == nullptr);
    TEST_EXPECT(ctx, TTrackedValue::live_count == 0);
    TEST_EXPECT(ctx, TTrackedValue::construction_count == 3);
    TEST_EXPECT(ctx, TTrackedValue::destruction_count == 3);

    collection.deallocate();
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, collection.is_empty());
    TEST_EXPECT(ctx, !collection.is_ready());
}

void test_failure_and_bounds_behaviour(TTestContext& ctx)
{
    TTrackedValue::reset_counts();

    TCollection collection;
    TEST_EXPECT(ctx, !collection.erase(TTrackedKey{ 9 }));
    TEST_EXPECT(ctx, !collection.initialise(0u, TCollection::k_max_elements + 1u));
    TEST_EXPECT(ctx, collection.is_valid());
    TEST_EXPECT(ctx, !collection.is_ready());

    TEST_EXPECT(ctx, collection.initialise(2u, 2u));
    TEST_EXPECT(ctx, collection.get_object(-1) == nullptr);
    TEST_EXPECT(ctx, collection.get_object(99) == nullptr);
    TEST_EXPECT(ctx, collection.key_at_slot(-1) == nullptr);
    TEST_EXPECT(ctx, collection.key_at_slot(99) == nullptr);
    TEST_EXPECT(ctx, !collection.erase(-1));
    TEST_EXPECT(ctx, !collection.erase(99));
    TEST_EXPECT(ctx, collection.find_slot(TTrackedKey{ 123 }) == -1);
    TEST_EXPECT(ctx, collection.reverse_lookup_slot_index_scan(nullptr) == -1);
    TEST_EXPECT(ctx, collection.emplace(TTrackedKey{ 3 }, 3, 30, 1u) >= 0);
    TEST_EXPECT(ctx, collection.emplace(TTrackedKey{ 3 }, 33, 330, 2u) == -1);
    TEST_EXPECT(ctx, TTrackedValue::live_count == 1);
    TEST_EXPECT(ctx, TTrackedValue::construction_count == 1);
    TEST_EXPECT(ctx, TTrackedValue::destruction_count == 0);
}

}   //  namespace

int run_ordered_collection_tests()
{
    TTestContext ctx;
    test_default_state_and_initialise(ctx);
    test_ordered_insert_find_and_traversal(ctx);
    test_stable_pointer_preservation_across_growth(ctx);
    test_erase_sort_pack_and_stable_addresses(ctx);
    test_repeated_initialise_and_deallocate(ctx);
    test_failure_and_bounds_behaviour(ctx);

    TEST_EXPECT(ctx, TTrackedValue::live_count == 0);
    std::cout << "TOrderedCollection: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
