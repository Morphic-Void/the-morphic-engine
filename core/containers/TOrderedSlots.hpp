
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TOrderedSlots.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    07 Nov 26
//
//  Ordered movable slot container with slot-based identity and key-based ordering.
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - Payloads require nonthrowing default construction, move construction,
//    move assignment, and destruction.
//  - Keys are trivially copyable and support nonthrowing copy assignment
//    and comparison through relationship().
//
//  Uses TOrderedSlotManager for ordering and slot management.
//  Stores movable payloads directly in slot-indexed storage.
//  Unoccupied slots hold default values.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  slot_index is the public identity during mutation. sort_and_pack() can
//  change it.
//
//  sort_and_pack() remaps slot metadata, payloads, and keys.
//
//  Ordered traversal is defined over live keyed slots.
//
//  See docs/containers/MovableSlots.md.

#pragma once

#ifndef TORDERED_SLOTS_HPP_INCLUDED
#define TORDERED_SLOTS_HPP_INCLUDED

#include <algorithm>    //  std::max, std::min
#include <cstddef>      //  std::byte, std::size_t
#include <cstdint>      //  std::int32_t, std::uint32_t
#include <cstring>      //  std::memcpy
#include <type_traits>  //  std::is_nothrow_*
#include <utility>      //  std::forward, std::move, std::swap

#include "debug/macros.hpp"
#include "slots/SlotsRankMap.hpp"
#include "slots/TOrderedSlotManager.hpp"
#include "TPodVector.hpp"
#include "TVector.hpp"

//==============================================================================
//  TOrderedSlots<T, TKey>
//  Ordered keyed movable slot container.
//==============================================================================

template<typename T, typename TKey>
class TOrderedSlotsStorage
{
    static_assert(std::is_nothrow_default_constructible_v<T>, "TOrderedSlots requires nonthrowing default construction.");
    static_assert(std::is_nothrow_move_assignable_v<T>, "TOrderedSlots requires nonthrowing move assignment.");
    static_assert(std::is_nothrow_copy_assignable_v<TKey>, "TOrderedSlots requires nonthrowing key assignment.");

public:
    using query_type = TKey;

    TOrderedSlotsStorage() noexcept = default;
    explicit TOrderedSlotsStorage(memory::CMemoryContext& context) noexcept;

    void on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept;
    [[nodiscard]] std::uint32_t on_reserve_empty(const std::uint32_t minimum_capacity, const std::uint32_t recommended_capacity) noexcept;
    [[nodiscard]] std::int32_t on_compare_query(const query_type& query, const std::int32_t candidate_index) const noexcept;
    [[nodiscard]] std::int32_t on_compare_keys(const std::int32_t source_index, const std::int32_t target_index) const noexcept;

protected:

    static constexpr std::size_t k_backing_limit = std::min(TVector<T>::k_max_elements, TPodVector<TKey>::k_max_elements);

    TVector<T> m_slots;
    TPodVector<TKey> m_keys;

    T m_swap_slot{};
    std::byte m_swap_key[sizeof(TKey)]{};
};

template<typename T, typename TKey>
class TOrderedSlots : public slots::TOrderedSlotManager<TOrderedSlotsStorage<T, TKey>, std::int32_t>
{
    using slot_data_class = TOrderedSlotsStorage<T, TKey>;
    using slot_meta_class = slots::TOrderedSlotManager<slot_data_class, std::int32_t>;

public:

    //  Default and deleted lifetime
    TOrderedSlots() noexcept = default;
    explicit TOrderedSlots(memory::CMemoryContext& context) noexcept;
    TOrderedSlots(const TOrderedSlots&) = delete;
    TOrderedSlots& operator=(const TOrderedSlots&) = delete;
    TOrderedSlots(TOrderedSlots&&) noexcept = default;
    TOrderedSlots& operator=(TOrderedSlots&&) noexcept = default;

    //  Destructor
    ~TOrderedSlots() noexcept = default;

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Occupied count
    [[nodiscard]] std::uint32_t occupied_count() const noexcept;

    //  Accessors
    [[nodiscard]] T* get_slot(const TKey& key) noexcept;
    [[nodiscard]] const T* get_slot(const TKey& key) const noexcept;
    [[nodiscard]] T* get_slot(const std::int32_t slot_index) noexcept;
    [[nodiscard]] const T* get_slot(const std::int32_t slot_index) const noexcept;

    //  O(1) access to the key paired with a live keyed slot.
    [[nodiscard]] const TKey* key_at_slot(const std::int32_t slot_index) const noexcept;

    //  Traversal
    [[nodiscard]] std::int32_t first_live() const noexcept;
    [[nodiscard]] std::int32_t last_live() const noexcept;
    [[nodiscard]] std::int32_t prev_live(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::int32_t next_live(const std::int32_t slot_index) const noexcept;

    //  Utility
    [[nodiscard]] slots::RankMap build_rank_map() const noexcept;
    [[nodiscard]] std::int32_t reverse_lookup_index_scan(const T* const slot) const noexcept;
    [[nodiscard]] std::int32_t find_index(const TKey& key) const noexcept;

    //  Content management
    //  The key and source must remain valid across reservation.
    [[nodiscard]] std::int32_t insert(const TKey& key, T&& value) noexcept;
    [[nodiscard]] std::int32_t insert(const TKey& key, const T& value) noexcept;
    [[nodiscard]] bool erase(const TKey& key) noexcept;
    [[nodiscard]] bool erase(const std::int32_t slot_index) noexcept;
    void sort_and_pack() noexcept;

    //  Initialisation and deallocation
    [[nodiscard]] bool initialise(const std::size_t initial_slot_count = 0u) noexcept;
    void deallocate() noexcept;

    //  Integrity audit
    [[nodiscard]] bool check_integrity() const noexcept;

    //  Constants
    static constexpr std::size_t k_element_size = sizeof(T);

//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe the current attribution of metadata, key backing, and payload backing.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace the contexts of all backing members, including unallocated ones.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

private:
    template<typename TSource>
    [[nodiscard]] std::int32_t insert_value(const TKey& key, TSource&& value) noexcept;
};

//==============================================================================
//  TOrderedSlots<T, TKey> out of class function bodies
//==============================================================================

template<typename T, typename TKey>
inline TOrderedSlotsStorage<T, TKey>::TOrderedSlotsStorage(memory::CMemoryContext& context) noexcept : m_slots(context), m_keys(context)
{
}

template<typename T, typename TKey>
inline TOrderedSlots<T, TKey>::TOrderedSlots(memory::CMemoryContext& context) noexcept : slot_meta_class(context)
{
}

template<typename T, typename TKey>
inline void TOrderedSlotsStorage<T, TKey>::on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept
{
    T& source = (source_index < 0) ? m_swap_slot : m_slots[static_cast<std::size_t>(source_index)];
    T& target = (target_index < 0) ? m_swap_slot : m_slots[static_cast<std::size_t>(target_index)];

    //  Each cycle carries the default scratch value into the next source slot.
    std::swap(source, target);

    //  Empty slots have no key value to interpret during the full-domain remap.
    const void* const source_key = (source_index < 0) ? static_cast<const void*>(m_swap_key) : static_cast<const void*>(m_keys.data() + source_index);
    void* const target_key = (target_index < 0) ? static_cast<void*>(m_swap_key) : static_cast<void*>(m_keys.data() + target_index);
    std::memcpy(target_key, source_key, sizeof(TKey));
}

template<typename T, typename TKey>
inline std::uint32_t TOrderedSlotsStorage<T, TKey>::on_reserve_empty(const std::uint32_t minimum_capacity, const std::uint32_t recommended_capacity) noexcept
{
    const std::size_t capacity = std::min(std::size_t{ recommended_capacity }, k_backing_limit);
    if ((capacity < minimum_capacity) || ((capacity > m_slots.capacity()) && !m_slots.reallocate(capacity)))
    {
        return 0u;
    }
    if (!m_slots.resize(std::max(capacity, m_slots.size())))
    {
        return 0u;
    }
    if ((capacity > m_keys.size()) && !m_keys.reallocate(capacity, capacity))
    {
        return 0u;
    }
    return static_cast<std::uint32_t>(capacity);
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlotsStorage<T, TKey>::on_compare_query(const query_type& query, const std::int32_t candidate_index) const noexcept
{
    return static_cast<std::int32_t>(query.relationship(m_keys[static_cast<std::size_t>(candidate_index)]));
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlotsStorage<T, TKey>::on_compare_keys(const std::int32_t source_index, const std::int32_t target_index) const noexcept
{
    return on_compare_query(m_keys[static_cast<std::size_t>(source_index)], target_index);
}

template<typename T, typename TKey>
inline bool TOrderedSlots<T, TKey>::is_valid() const noexcept
{
    //  Backing growth precedes metadata growth. A failed metadata allocation
    //  may retain excess backing. Every metadata slot must still be covered.
    const std::uint32_t capacity = slot_meta_class::capacity();
    return this->m_slots.is_valid() && this->m_keys.is_valid() && (this->m_slots.size() >= capacity) && (this->m_keys.size() >= capacity);
}

template<typename T, typename TKey>
inline bool TOrderedSlots<T, TKey>::is_empty() const noexcept
{
    return slot_meta_class::is_empty();
}

template<typename T, typename TKey>
inline bool TOrderedSlots<T, TKey>::is_ready() const noexcept
{
    return this->m_slots.is_ready() && this->m_keys.is_ready();
}

template<typename T, typename TKey>
inline std::uint32_t TOrderedSlots<T, TKey>::occupied_count() const noexcept
{
    return slot_meta_class::occupied_count();
}

template<typename T, typename TKey>
inline T* TOrderedSlots<T, TKey>::get_slot(const TKey& key) noexcept
{
    return get_slot(find_index(key));
}

template<typename T, typename TKey>
inline const T* TOrderedSlots<T, TKey>::get_slot(const TKey& key) const noexcept
{
    return get_slot(find_index(key));
}

template<typename T, typename TKey>
inline T* TOrderedSlots<T, TKey>::get_slot(const std::int32_t slot_index) noexcept
{
    return slot_meta_class::is_lexed_slot(slot_index) ? this->m_slots.get(static_cast<std::size_t>(slot_index)) : nullptr;
}

template<typename T, typename TKey>
inline const T* TOrderedSlots<T, TKey>::get_slot(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::is_lexed_slot(slot_index) ? this->m_slots.get(static_cast<std::size_t>(slot_index)) : nullptr;
}

template<typename T, typename TKey>
inline const TKey* TOrderedSlots<T, TKey>::key_at_slot(const std::int32_t slot_index) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(slot_index);
    if ((index < this->m_keys.size()) && slot_meta_class::is_lexed_slot(slot_index))
    {
        return this->m_keys.data() + index;
    }
    return nullptr;
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlots<T, TKey>::find_index(const TKey& key) const noexcept
{
    return slot_meta_class::find_any_equal(key);
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlots<T, TKey>::first_live() const noexcept
{
    return slot_meta_class::first_lexed();
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlots<T, TKey>::last_live() const noexcept
{
    return slot_meta_class::last_lexed();
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlots<T, TKey>::prev_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::prev_lexed(slot_index);
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlots<T, TKey>::next_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::next_lexed(slot_index);
}

template<typename T, typename TKey>
inline slots::RankMap TOrderedSlots<T, TKey>::build_rank_map() const noexcept
{
    return slot_meta_class::build_rank_map();
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlots<T, TKey>::reverse_lookup_index_scan(const T* const slot) const noexcept
{
    if (slot == nullptr) return -1;
    for (std::int32_t index = first_live(); index >= 0; index = next_live(index))
    {
        if (get_slot(index) == slot) return index;
    }
    return -1;
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlots<T, TKey>::insert(const TKey& key, T&& value) noexcept
{
    return insert_value(key, std::move(value));
}

template<typename T, typename TKey>
inline std::int32_t TOrderedSlots<T, TKey>::insert(const TKey& key, const T& value) noexcept
{
    return insert_value(key, value);
}

template<typename T, typename TKey>
inline bool TOrderedSlots<T, TKey>::erase(const TKey& key) noexcept
{
    return erase(find_index(key));
}

template<typename T, typename TKey>
inline bool TOrderedSlots<T, TKey>::erase(const std::int32_t slot_index) noexcept
{
    if (!slot_meta_class::erase(slot_index)) return false;
    this->m_slots[static_cast<std::size_t>(slot_index)] = T{};
    return true;
}

template<typename T, typename TKey>
inline void TOrderedSlots<T, TKey>::sort_and_pack() noexcept
{
    slot_meta_class::sort_and_pack(false);
}

template<typename T, typename TKey>
inline bool TOrderedSlots<T, TKey>::initialise(const std::size_t initial_slot_count) noexcept
{
    const std::size_t capacity = std::max(initial_slot_count, std::size_t{ 32u });
    if ((capacity > slot_meta_class::capacity_limit()) || (capacity > slot_data_class::k_backing_limit))
    {
        return false;
    }
    deallocate();
    const std::uint32_t count = static_cast<std::uint32_t>(capacity);
    if (slot_meta_class::initialise(count) && (this->on_reserve_empty(count, count) == count))
    {
        return true;
    }
    deallocate();
    return false;
}

template<typename T, typename TKey>
inline void TOrderedSlots<T, TKey>::deallocate() noexcept
{
    (void)slot_meta_class::shutdown();
    this->m_slots.deallocate();
    this->m_keys.deallocate();
    this->m_swap_slot = T{};
}

template<typename T, typename TKey>
inline bool TOrderedSlots<T, TKey>::check_integrity() const noexcept
{
    if (!is_valid())
    {
        MV_ASSERT_REPORT("TOrderedSlots integrity check failed");
        return false;
    }
    return slot_meta_class::check_integrity() && slot_meta_class::validate_tree(slot_meta_class::LexCheck::Unique);
}

template<typename T, typename TKey>
inline memory::SMemoryAttribution TOrderedSlots<T, TKey>::memory_attribution() const noexcept
{
    const memory::SMemoryAttribution backing =
        memory::combine_memory_attribution(this->m_slots.memory_attribution(), this->m_keys.memory_attribution());
    return memory::combine_memory_attribution(backing, slot_meta_class::memory_attribution());
}

template<typename T, typename TKey>
inline void TOrderedSlots<T, TKey>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
{
    this->m_slots.unsafe_replace_memory_context_without_accounting(expected_source, target);
    this->m_keys.unsafe_replace_memory_context_without_accounting(expected_source, target);
    slot_meta_class::unsafe_replace_memory_context_without_accounting(expected_source, target);
}

template<typename T, typename TKey>
template<typename TSource>
inline std::int32_t TOrderedSlots<T, TKey>::insert_value(const TKey& key, TSource&& value) noexcept
{
    static_assert(std::is_nothrow_assignable_v<T&, TSource&&>, "TOrderedSlots insertion requires nonthrowing assignment.");
    const std::int32_t slot_index = slot_meta_class::reserve_and_acquire(key, -1, /* lex */ true, /* require_unique */ true);
    if (slot_index < 0) return -1;
    const std::size_t index = static_cast<std::size_t>(slot_index);
    //  A key may be a member of the incoming object.
    this->m_keys[index] = key;
    this->m_slots[index] = std::forward<TSource>(value);
    return slot_index;
}

#endif  //  TORDERED_SLOTS_HPP_INCLUDED
