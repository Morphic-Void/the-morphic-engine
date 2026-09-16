
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
#include <limits>
#include <type_traits>
#include <utility>

#include "containers/TOrderedCollection.hpp"
#include "containers/TPodOrderedSlots.hpp"
#include "containers/TPodUnorderedSlots.hpp"
#include "containers/TUnorderedCollection.hpp"
#include "containers/TInstance.hpp"
#include "containers/TPodFifo.hpp"
#include "tests/test_suites/TOrderedCollection_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

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

template<bool Pod, bool Ordered>
using TAttributionBase = std::conditional_t<Ordered,
    std::conditional_t<Pod, TPodOrderedSlots<int, TTrackedKey>, TOrderedCollection<int, TTrackedKey>>,
    std::conditional_t<Pod, TPodUnorderedSlots<int>, TUnorderedCollection<int>>>;

template<bool Pod, bool Ordered>
struct TAttributionContainer : TAttributionBase<Pod, Ordered>
{
    using Backing = std::conditional_t<Ordered,
        std::conditional_t<Pod, TPodOrderedSlotsStorage<int, TTrackedKey>, TOrderedCollectionStorage<int, TTrackedKey>>,
        std::conditional_t<Pod, TPodUnorderedSlotsStorage<int>, TUnorderedCollectionStorage<int>>>;
    using Metadata = std::conditional_t<Ordered, slots::TOrderedSlots<Backing>, slots::TUnorderedSlots<Backing>>;

    int insert_value(const int value) noexcept
    {
        if constexpr (Pod && Ordered) return this->insert(TTrackedKey{value}, value);
        else if constexpr (Pod) return this->insert(value);
        else if constexpr (Ordered) return this->emplace(TTrackedKey{value}, value);
        else return this->emplace(value);
    }

    const int* address(const int slot) const noexcept
    {
        if constexpr (Pod) return this->get_slot(slot);
        else return this->get_object(slot);
    }

    bool split_payload_context(memory::CMemoryContext* target) noexcept
    {
        return memory::reattribute(this->m_slots, target);
    }

    bool sources_match(memory::CMemoryContext* expected) const noexcept
    {
        const auto payload = Backing::memory_attribution();
        const auto metadata = Metadata::memory_attribution();
        return (payload.source_state == memory::EMemorySourceState::coherent) &&
            (metadata.source_state == memory::EMemorySourceState::coherent) &&
            (payload.source == expected) && (metadata.source == expected);
    }

    bool empty_storage_context_matches(memory::CMemoryContext* expected) const noexcept
    {
        if constexpr (Pod) return true;
        else return this->m_storage.context() == expected;
    }
};

template<bool Pod, bool Ordered>
void test_complete_aggregate_reattribution(TTestContext& ctx)
{
    using Container = TAttributionContainer<Pod, Ordered>;
    memory::CMemoryAllocator allocator{nullptr, &tests::allocate_test_memory, &tests::deallocate_test_memory};
    memory::CMemoryAllocator other_allocator{nullptr, &tests::allocate_test_memory, &tests::deallocate_test_memory};
    memory::CMemoryContext source{allocator};
    memory::CMemoryContext target{allocator};
    memory::CMemoryContext other{other_allocator};
    tests::TMemoryContextScope source_scope{&source};
    Container container;

    TEST_EXPECT(ctx, container.memory_attribution().source_state == memory::EMemorySourceState::empty);
    TEST_EXPECT(ctx, container.memory_attribution().source == nullptr);
    TEST_EXPECT(ctx, memory::reattribute(container, &other));
    TEST_EXPECT(ctx, container.is_valid());
    TEST_EXPECT(ctx, container.empty_storage_context_matches(&other));
    TEST_EXPECT(ctx, memory::reattribute(container, &source));
    TEST_EXPECT(ctx, container.initialise(32u));
    const int slot = container.insert_value(7);
    const int* const address = container.address(slot);
    TEST_EXPECT(ctx, (address != nullptr) && (*address == 7));
    const std::uint32_t count = container.memory_attribution().allocation_count;
    const std::uint64_t bytes = container.memory_attribution().allocation_size;
    TEST_EXPECT(ctx, count == source.get_live_allocation_count());
    TEST_EXPECT(ctx, bytes == source.get_live_allocated_bytes());
    TEST_EXPECT(ctx, container.sources_match(&source));
    TEST_EXPECT(ctx, container.memory_attribution().source_state == memory::EMemorySourceState::coherent);
    TEST_EXPECT(ctx, container.memory_attribution().source == &source);
    TEST_EXPECT(ctx, !memory::can_reattribute_to(container, &other) && !memory::reattribute(container, &other));
    TEST_EXPECT(ctx, container.sources_match(&source));
    TEST_EXPECT(ctx, source.get_live_allocation_count() == count);
    TEST_EXPECT(ctx, source.get_live_allocated_bytes() == bytes);
    TEST_EXPECT(ctx, other.is_attribution_empty());

    //  Matching allocators do not permit mixed allocated source contexts.
    TEST_EXPECT(ctx, container.split_payload_context(&target));
    const auto source_count = source.get_live_allocation_count();
    const auto source_bytes = source.get_live_allocated_bytes();
    const auto target_count = target.get_live_allocation_count();
    const auto target_bytes = target.get_live_allocated_bytes();
    const auto mixed = container.memory_attribution();
    TEST_EXPECT(ctx, mixed.source_state == memory::EMemorySourceState::mixed && mixed.source == nullptr);
    TEST_EXPECT(ctx, mixed.allocation_count == count && mixed.allocation_size == bytes);
    TEST_EXPECT(ctx, !memory::can_reattribute_to(container, &target) && !memory::reattribute(container, &target));
    TEST_EXPECT(ctx, !memory::reattribute(container, &source));
    TEST_EXPECT(ctx, source.get_live_allocation_count() == source_count);
    TEST_EXPECT(ctx, source.get_live_allocated_bytes() == source_bytes);
    TEST_EXPECT(ctx, target.get_live_allocation_count() == target_count);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == target_bytes);
    TEST_EXPECT(ctx, container.split_payload_context(&source));
    TEST_EXPECT(ctx, container.sources_match(&source));

    //  A parent needs only the complete public interface, never the backing split.
    const auto attribution = container.memory_attribution();
    TEST_EXPECT(ctx, attribution.source_state == memory::EMemorySourceState::coherent);
    TEST_EXPECT(ctx, memory::can_reattribute_to(container, &target));
    TEST_EXPECT(ctx, memory::reattribute(*attribution.source, target, attribution.allocation_count, attribution.allocation_size));
    container.unsafe_replace_memory_context_without_accounting(attribution.source, &target);
    TEST_EXPECT(ctx, container.sources_match(&target));
    TEST_EXPECT(ctx, container.address(slot) == address);
    if constexpr (Ordered) TEST_EXPECT(ctx, container.key_at_slot(slot)->value == 7);
    TEST_EXPECT(ctx, source.is_attribution_empty());
    TEST_EXPECT(ctx, target.get_live_allocation_count() == count);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == bytes);
    TEST_EXPECT(ctx, memory::reattribute(container, &target));
    TEST_EXPECT(ctx, target.get_live_allocation_count() == count);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == bytes);
    TEST_EXPECT(ctx, memory::reattribute(container, &source));
    TEST_EXPECT(ctx, container.sources_match(&source));
    TEST_EXPECT(ctx, target.is_attribution_empty());

    {
        tests::TMemoryContextScope target_scope{&target};
        Container moved{std::move(container)};
        TEST_EXPECT(ctx, moved.sources_match(&source));
        TEST_EXPECT(ctx, moved.address(slot) == address);
        TEST_EXPECT(ctx, source.get_live_allocation_count() == count);
        TEST_EXPECT(ctx, target.is_attribution_empty());
        TEST_EXPECT(ctx, memory::reattribute(container, &other));
        TEST_EXPECT(ctx, container.is_valid());
        TEST_EXPECT(ctx, container.empty_storage_context_matches(&other));
        TEST_EXPECT(ctx, memory::reattribute(moved));
        for (int value = 8; value < 80; ++value) TEST_EXPECT(ctx, moved.insert_value(value) >= 0);
        TEST_EXPECT(ctx, moved.sources_match(&target));
        TEST_EXPECT(ctx, moved.memory_attribution().allocation_count == target.get_live_allocation_count());
        TEST_EXPECT(ctx, moved.memory_attribution().allocation_size == target.get_live_allocated_bytes());
        TEST_EXPECT(ctx, moved.check_integrity());
    }
    TEST_EXPECT(ctx, source.is_attribution_empty());
    TEST_EXPECT(ctx, target.is_attribution_empty());
    TEST_EXPECT(ctx, other.is_attribution_empty());
}

void test_composed_instance_and_fifo(TTestContext& ctx)
{
    struct SOwner
    {
        TInstance<int> instance;
        TPodFifo<int> fifo;

        memory::SMemoryAttribution memory_attribution() const noexcept
        {
            return memory::combine_memory_attribution(instance.memory_attribution(), fifo.memory_attribution());
        }

        void unsafe_replace_memory_context_without_accounting(
            memory::CMemoryContext* const source, memory::CMemoryContext* const target) noexcept
        {
            instance.unsafe_replace_memory_context_without_accounting(source, target);
            fifo.unsafe_replace_memory_context_without_accounting(source, target);
        }
    };
    memory::CMemoryAllocator allocator{nullptr, &tests::allocate_test_memory, &tests::deallocate_test_memory};
    memory::CMemoryContext source{allocator};
    memory::CMemoryContext target{allocator};
    tests::TMemoryContextScope scope{&source};
    {
        SOwner owner;
        TEST_EXPECT(ctx, memory::reattribute(owner, &target));
        TEST_EXPECT(ctx, owner.memory_attribution().source_state == memory::EMemorySourceState::empty);
        TEST_EXPECT(ctx, memory::reattribute(owner, &source));
        owner.instance = TInstance<int>::create(42);
        TEST_EXPECT(ctx, owner.instance.is_ready());
        TEST_EXPECT(ctx, owner.fifo.allocate(8u) && owner.fifo.push_back(7));
        TEST_EXPECT(ctx, memory::reattribute(owner, &source));
        const auto* const instance = owner.instance.operator->();
        const auto* const fifo = owner.fifo.data();
        const auto attribution = owner.memory_attribution();
        TEST_EXPECT(ctx, attribution.token_count == 2u && attribution.allocation_count == 2u);
        TEST_EXPECT(ctx, attribution.allocation_size == source.get_live_allocated_bytes());
        TEST_EXPECT(ctx, memory::reattribute(owner.fifo, &target));
        TEST_EXPECT(ctx, owner.memory_attribution().source_state == memory::EMemorySourceState::mixed);
        TEST_EXPECT(ctx, !memory::reattribute(owner, &target));
        TEST_EXPECT(ctx, owner.instance.memory_attribution().source == &source);
        TEST_EXPECT(ctx, memory::reattribute(owner.fifo, &source));
        TEST_EXPECT(ctx, memory::reattribute(owner, &target));
        TEST_EXPECT(ctx, owner.instance.operator->() == instance && owner.fifo.data() == fifo);
        TEST_EXPECT(ctx, *owner.instance == 42 && owner.fifo.size() == 1u);
        int value = 0;
        TEST_EXPECT(ctx, owner.fifo.pop_front(value) && value == 7);
        TEST_EXPECT(ctx, source.is_attribution_empty());
        TEST_EXPECT(ctx, target.get_live_allocation_count() == attribution.allocation_count);
        TEST_EXPECT(ctx, target.get_live_allocated_bytes() == attribution.allocation_size);
    }
    TEST_EXPECT(ctx, source.is_attribution_empty() && target.is_attribution_empty());
}

void test_user_value_allocations_remain_separate(TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{nullptr, &tests::allocate_test_memory, &tests::deallocate_test_memory};
    memory::CMemoryContext source{allocator};
    memory::CMemoryContext target{allocator};
    tests::TMemoryContextScope scope{&source};
    {
        TOrderedCollection<TInstance<int>, TTrackedKey> collection;
        TEST_EXPECT(ctx, collection.initialise());
        auto value = TInstance<int>::create(42);
        const auto value_count = value.memory_attribution().allocation_count;
        const auto value_bytes = value.memory_attribution().allocation_size;
        const int slot = collection.emplace(TTrackedKey{1}, std::move(value));
        TEST_EXPECT(ctx, slot >= 0);
        TEST_EXPECT(ctx, memory::reattribute(collection, &target));
        TEST_EXPECT(ctx, source.get_live_allocation_count() == value_count);
        TEST_EXPECT(ctx, source.get_live_allocated_bytes() == value_bytes);
        TEST_EXPECT(ctx, target.get_live_allocation_count() == collection.memory_attribution().allocation_count);
        TEST_EXPECT(ctx, target.get_live_allocated_bytes() == collection.memory_attribution().allocation_size);
        TEST_EXPECT(ctx, **collection.get_object(slot) == 42);
    }
    TEST_EXPECT(ctx, source.is_attribution_empty() && target.is_attribution_empty());
}

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
    TEST_EXPECT(ctx, collection.memory_attribution().token_count == 4u);
    TEST_EXPECT(ctx, collection.memory_attribution().allocation_count != 0u);
    TEST_EXPECT(ctx, collection.memory_attribution().allocation_size != 0u);
    TEST_EXPECT(ctx, memory::can_reattribute_to(collection));
    TEST_EXPECT(ctx, memory::reattribute(collection));
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


struct TFailingGrowthAllocator
{
    std::size_t remaining{ std::numeric_limits<std::size_t>::max() };
};

void* MV_STD_ABI_CALL allocate_growth_memory(
    void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    auto& fixture = *static_cast<TFailingGrowthAllocator*>(state);
    if (fixture.remaining == 0u)
    {
        return nullptr;
    }
    --fixture.remaining;
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

template<bool Pod>
void test_failed_growth(TTestContext& ctx)
{
    using TContainer = std::conditional_t<Pod, TPodOrderedSlots<std::int32_t, TTrackedKey>, TCollection>;
    const auto insert = [](TContainer& container, const std::int32_t value) noexcept
    {
        if constexpr (Pod)
        {
            return container.insert(TTrackedKey{ value }, value);
        }
        else
        {
            return container.emplace(TTrackedKey{ value }, value, value, 1u);
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
            return (object != nullptr) ? &object->payload : nullptr;
        }
    };

    bool reached_success = false;
    std::size_t failures = 0u;
    for (std::size_t allowance = 0u; allowance < 8u; ++allowance)
    {
        TFailingGrowthAllocator fixture;
        memory::CMemoryAllocator allocator{ &fixture, &allocate_growth_memory, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        memory::CMemoryContext destination{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            TContainer container;
            TEST_EXPECT(ctx, container.initialise(32u));
            for (std::int32_t value = 0; value < 32; ++value)
            {
                TEST_EXPECT(ctx, insert(container, value) == value);
            }
            const auto* const original = get_value(container, 0);
            const std::int32_t last_before = container.last_live();
            fixture.remaining = allowance;
            const std::int32_t added = insert(container, 32);
            fixture.remaining = std::numeric_limits<std::size_t>::max();
            TEST_EXPECT(ctx, container.is_valid());
            TEST_EXPECT(ctx, container.check_integrity());
            for (std::int32_t value = 0; value < 32; ++value)
            {
                const auto* const stored = get_value(container, value);
                TEST_EXPECT(ctx, (stored != nullptr) && (*stored == value));
            }
            if constexpr (!Pod)
            {
                TEST_EXPECT(ctx, get_value(container, 0) == original);
            }
            const auto retained_count = context.get_live_allocation_count();
            const auto retained_bytes = context.get_live_allocated_bytes();
            TEST_EXPECT(ctx, memory::reattribute(container, &destination));
            TEST_EXPECT(ctx, context.is_attribution_empty());
            TEST_EXPECT(ctx, destination.get_live_allocation_count() == retained_count);
            TEST_EXPECT(ctx, destination.get_live_allocated_bytes() == retained_bytes);
            tests::TMemoryContextScope destination_scope{&destination};
            if (added < 0)
            {
                ++failures;
                TEST_EXPECT(ctx, container.last_live() == last_before);
                TEST_EXPECT(ctx, get_value(container, 32) == nullptr);
                //  Packing must also tolerate backing retained by failed metadata growth.
                container.sort_and_pack();
                TEST_EXPECT(ctx, container.check_integrity());
                TEST_EXPECT(ctx, insert(container, 32) == 32);
            }
            else
            {
                reached_success = true;
                TEST_EXPECT(ctx, added == 32);
            }
            TEST_EXPECT(ctx, container.check_integrity());
            TEST_EXPECT(ctx, container.erase(0));
            container.sort_and_pack();
            TEST_EXPECT(ctx, container.check_integrity());
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
        TEST_EXPECT(ctx, destination.is_attribution_empty());
        if constexpr (!Pod)
        {
            TEST_EXPECT(ctx, TTrackedValue::live_count == 0);
        }
        if (reached_success)
        {
            break;
        }
    }
    TEST_EXPECT(ctx, reached_success);
    TEST_EXPECT(ctx, failures >= 3u);
}

}   //  namespace

int run_ordered_collection_tests()
{
    TTestContext ctx;
    test_complete_aggregate_reattribution<true, true>(ctx);
    test_complete_aggregate_reattribution<true, false>(ctx);
    test_complete_aggregate_reattribution<false, true>(ctx);
    test_complete_aggregate_reattribution<false, false>(ctx);
    test_user_value_allocations_remain_separate(ctx);
    test_composed_instance_and_fifo(ctx);
    test_failed_growth<true>(ctx);
    test_failed_growth<false>(ctx);
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
