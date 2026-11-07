
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TUnorderedSlots.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    07 Nov 26
//
//  Unordered movable slot container with slot-based identity.
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - Payloads require nonthrowing default construction, move construction,
//    move assignment, and destruction.
//
//  Uses TUnorderedSlotManager for slot management.
//  Stores movable payloads directly in slot-indexed storage.
//  Unoccupied slots hold default values.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  slot_index is the public identity during mutation. pack() can change it.
//
//  pack() remaps slot metadata and payloads.
//
//  Traversal follows the occupied list. Packing uses slot-index order.
//
//  See docs/containers/MovableSlots.md.

#pragma once

#ifndef TUNORDERED_SLOTS_HPP_INCLUDED
#define TUNORDERED_SLOTS_HPP_INCLUDED

#include <algorithm>    //  std::max, std::min
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::int32_t, std::uint32_t
#include <type_traits>  //  std::is_nothrow_*
#include <utility>      //  std::forward, std::move, std::swap

#include "debug/macros.hpp"
#include "slots/SlotsRankMap.hpp"
#include "slots/TUnorderedSlotManager.hpp"
#include "TVector.hpp"

//==============================================================================
//  TUnorderedSlots<T>
//  Unordered movable slot container.
//==============================================================================

template<typename T>
class TUnorderedSlotsStorage
{
    static_assert(std::is_nothrow_default_constructible_v<T>, "TUnorderedSlots requires nonthrowing default construction.");
    static_assert(std::is_nothrow_move_assignable_v<T>, "TUnorderedSlots requires nonthrowing move assignment.");

public:
    void on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept;

    [[nodiscard]] std::uint32_t on_reserve_empty(
        const std::uint32_t minimum_capacity, const std::uint32_t recommended_capacity) noexcept;

protected:

    TVector<T> m_slots;
};

template<typename T>
class TUnorderedSlots : public slots::TUnorderedSlotManager<TUnorderedSlotsStorage<T>, std::int32_t>
{
    using slot_meta_class = slots::TUnorderedSlotManager<TUnorderedSlotsStorage<T>, std::int32_t>;

public:

    //  Default and deleted lifetime
    TUnorderedSlots() noexcept = default;
    TUnorderedSlots(const TUnorderedSlots&) = delete;
    TUnorderedSlots& operator=(const TUnorderedSlots&) = delete;
    TUnorderedSlots(TUnorderedSlots&&) noexcept = default;
    TUnorderedSlots& operator=(TUnorderedSlots&&) noexcept = default;

    //  Destructor
    ~TUnorderedSlots() noexcept = default;

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Occupied count
    [[nodiscard]] std::uint32_t occupied_count() const noexcept;

    //  Accessors
    [[nodiscard]] T* get_slot(const std::int32_t slot_index) noexcept;
    [[nodiscard]] const T* get_slot(const std::int32_t slot_index) const noexcept;

    //  Traversal
    [[nodiscard]] std::int32_t first_live() const noexcept;
    [[nodiscard]] std::int32_t last_live() const noexcept;
    [[nodiscard]] std::int32_t prev_live(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::int32_t next_live(const std::int32_t slot_index) const noexcept;

    //  Utility
    [[nodiscard]] slots::RankMap build_rank_map() const noexcept;
    [[nodiscard]] std::int32_t reverse_lookup_index_scan(const T* const slot) const noexcept;

    //  Content management
    //  The source must remain valid across reservation.
    [[nodiscard]] std::int32_t insert(T&& value) noexcept;
    [[nodiscard]] std::int32_t insert(const T& value) noexcept;
    [[nodiscard]] bool erase(const std::int32_t slot_index) noexcept;
    void pack() noexcept;

    //  Initialisation and deallocation
    [[nodiscard]] bool initialise(const std::size_t initial_slot_count = 0u) noexcept;
    void deallocate() noexcept;

    //  Integrity audit
    [[nodiscard]] bool check_integrity() const noexcept;

    //  Constants
    static constexpr std::size_t k_element_size = sizeof(T);

//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe the current attribution of metadata and payload backing.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace the contexts of all backing members, including unallocated ones.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

private:
    template<typename TSource>
    [[nodiscard]] std::int32_t insert_value(TSource&& value) noexcept;
};

//==============================================================================
//  TUnorderedSlots<T> out of class function bodies
//==============================================================================

template<typename T>
inline void TUnorderedSlotsStorage<T>::on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept
{
    std::swap(m_slots[static_cast<std::size_t>(source_index)], m_slots[static_cast<std::size_t>(target_index)]);
}

template<typename T>
inline std::uint32_t TUnorderedSlotsStorage<T>::on_reserve_empty(const std::uint32_t minimum_capacity, const std::uint32_t recommended_capacity) noexcept
{
    const std::size_t capacity = std::min(std::size_t{ recommended_capacity }, TVector<T>::k_max_elements);
    if ((capacity < minimum_capacity) ||
        ((capacity > m_slots.capacity()) && !m_slots.reallocate(capacity)))
    {
        return 0u;
    }
    if (!m_slots.resize(std::max(capacity, m_slots.size())))
    {
        return 0u;
    }
    return static_cast<std::uint32_t>(capacity);
}

template<typename T>
inline bool TUnorderedSlots<T>::is_valid() const noexcept
{
    //  Backing growth precedes metadata growth. A failed metadata allocation
    //  may retain excess backing. Every metadata slot must still be covered.
    return this->m_slots.is_valid() && (this->m_slots.size() >= slot_meta_class::capacity());
}

template<typename T>
inline bool TUnorderedSlots<T>::is_empty() const noexcept
{
    return slot_meta_class::is_empty();
}

template<typename T>
inline bool TUnorderedSlots<T>::is_ready() const noexcept
{
    return this->m_slots.is_ready();
}

template<typename T>
inline std::uint32_t TUnorderedSlots<T>::occupied_count() const noexcept
{
    return slot_meta_class::loose_count();
}

template<typename T>
inline T* TUnorderedSlots<T>::get_slot(const std::int32_t slot_index) noexcept
{
    return slot_meta_class::is_loose_slot(slot_index) ? this->m_slots.get(static_cast<std::size_t>(slot_index)) : nullptr;
}

template<typename T>
inline const T* TUnorderedSlots<T>::get_slot(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::is_loose_slot(slot_index) ? this->m_slots.get(static_cast<std::size_t>(slot_index)) : nullptr;
}

template<typename T>
inline std::int32_t TUnorderedSlots<T>::first_live() const noexcept
{
    return slot_meta_class::first_loose();
}

template<typename T>
inline std::int32_t TUnorderedSlots<T>::last_live() const noexcept
{
    return slot_meta_class::last_loose();
}

template<typename T>
inline std::int32_t TUnorderedSlots<T>::prev_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::prev_loose(slot_index);
}

template<typename T>
inline std::int32_t TUnorderedSlots<T>::next_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::next_loose(slot_index);
}

template<typename T>
inline slots::RankMap TUnorderedSlots<T>::build_rank_map() const noexcept
{
    return slot_meta_class::build_rank_map();
}

template<typename T>
inline std::int32_t TUnorderedSlots<T>::reverse_lookup_index_scan(const T* const slot) const noexcept
{
    if (slot == nullptr) return -1;
    for (std::int32_t index = first_live(); index >= 0; index = next_live(index))
    {
        if (get_slot(index) == slot) return index;
    }
    return -1;
}

template<typename T>
inline std::int32_t TUnorderedSlots<T>::insert(T&& value) noexcept
{
    return insert_value(std::move(value));
}

template<typename T>
inline std::int32_t TUnorderedSlots<T>::insert(const T& value) noexcept
{
    return insert_value(value);
}

template<typename T>
inline bool TUnorderedSlots<T>::erase(const std::int32_t slot_index) noexcept
{
    if (!slot_meta_class::erase(slot_index)) return false;
    this->m_slots[static_cast<std::size_t>(slot_index)] = T{};
    return true;
}

template<typename T>
inline void TUnorderedSlots<T>::pack() noexcept
{
    slot_meta_class::pack();
}

template<typename T>
inline bool TUnorderedSlots<T>::initialise(const std::size_t initial_slot_count) noexcept
{
    const std::size_t capacity = std::max(initial_slot_count, std::size_t{ 32u });
    if ((capacity > slot_meta_class::capacity_limit()) || (capacity > TVector<T>::k_max_elements))
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

template<typename T>
inline void TUnorderedSlots<T>::deallocate() noexcept
{
    (void)slot_meta_class::shutdown();
    this->m_slots.deallocate();
}

template<typename T>
inline bool TUnorderedSlots<T>::check_integrity() const noexcept
{
    if (!is_valid())
    {
        MV_ASSERT_REPORT("TUnorderedSlots integrity check failed");
        return false;
    }
    return slot_meta_class::check_integrity();
}

template<typename T>
inline memory::SMemoryAttribution TUnorderedSlots<T>::memory_attribution() const noexcept
{
    return memory::combine_memory_attribution(this->m_slots.memory_attribution(), slot_meta_class::memory_attribution());
}

template<typename T>
inline void TUnorderedSlots<T>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
{
    this->m_slots.unsafe_replace_memory_context_without_accounting(expected_source, target);
    slot_meta_class::unsafe_replace_memory_context_without_accounting(expected_source, target);
}

template<typename T>
template<typename TSource>
inline std::int32_t TUnorderedSlots<T>::insert_value(TSource&& value) noexcept
{
    static_assert(std::is_nothrow_assignable_v<T&, TSource&&>, "TUnorderedSlots insertion requires nonthrowing assignment.");
    const std::int32_t slot_index = slot_meta_class::reserve_and_acquire();
    if (slot_index < 0) return -1;
    this->m_slots[static_cast<std::size_t>(slot_index)] = std::forward<TSource>(value);
    return slot_index;
}

#endif  //  TUNORDERED_SLOTS_HPP_INCLUDED
