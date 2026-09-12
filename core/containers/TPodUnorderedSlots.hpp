
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TPodUnorderedSlots.hpp
//  Author: Ritchie Brannan
//  Date:   13 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Unordered POD slot manager with slot-based identity.
//
//  Uses TUnorderedSlots for slot management.
//  Stores trivial payload data directly in slot-indexed POD storage.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  slot_index is the public identity during mutation and is not stable
//  across pack().
//
//  pack() remaps slot metadata and payload data.
//
//  Traversal order does not imply rank or ordering.

#pragma once

#ifndef TPOD_UNORDERED_SLOTS_HPP_INCLUDED
#define TPOD_UNORDERED_SLOTS_HPP_INCLUDED

#include <algorithm>    //  std::max
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::int32_t, std::uint32_t
#include <type_traits>  //  std::is_const_v, std::is_copy_assignable_v, std::is_trivially_copyable_v

#include "memory/memory_policies.hpp"
#include "slots/TUnorderedSlots.hpp"
#include "slots/SlotsRankMap.hpp"
#include "TPodVector.hpp"

#include "debug/macros.hpp"

//==============================================================================
//  TPodUnorderedSlots<T>
//  Unordered POD slot manager.
//==============================================================================

template<typename T>
class TPodUnorderedSlotsStorage
{
public:
    void on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept;
    [[nodiscard]] std::uint32_t on_reserve_empty(const std::uint32_t minimum_capacity, const std::uint32_t recommended_capacity) noexcept;

protected:
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool memory_source_context(memory::CMemoryContext*& source) const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

    TPodVector<T> m_slots;
};

template<typename T>
class TPodUnorderedSlots : public slots::TUnorderedSlots<TPodUnorderedSlotsStorage<T>, std::int32_t>
{
private:
    using slot_data_class = TPodUnorderedSlotsStorage<T>;
    using slot_meta_class = slots::TUnorderedSlots<slot_data_class, std::int32_t>;

    static_assert(!std::is_const_v<T>, "TPodUnorderedSlots<T> requires non-const T.");
    static_assert(std::is_copy_assignable_v<T>, "TPodUnorderedSlots<T> requires copy-assignable T.");
    static_assert(std::is_trivially_copyable_v<T>, "TPodUnorderedSlots<T> requires trivially copyable T.");

public:

    //  Default and deleted lifetime
    TPodUnorderedSlots() noexcept = default;
    TPodUnorderedSlots(const TPodUnorderedSlots&) noexcept = delete;
    TPodUnorderedSlots& operator=(const TPodUnorderedSlots&) noexcept = delete;
    TPodUnorderedSlots(TPodUnorderedSlots&&) noexcept = default;
    TPodUnorderedSlots& operator=(TPodUnorderedSlots&&) noexcept = default;

    //  Destructor
    ~TPodUnorderedSlots() noexcept { deallocate(); }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Occupied count
    [[nodiscard]] std::uint32_t occupied_count() const noexcept;

    //  Accessors
    T* get_slot(const std::int32_t slot_index) noexcept;
    const T* get_slot(const std::int32_t slot_index) const noexcept;

    //  Traversal
    [[nodiscard]] std::int32_t first_live() const noexcept;
    [[nodiscard]] std::int32_t last_live() const noexcept;
    [[nodiscard]] std::int32_t prev_live(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::int32_t next_live(const std::int32_t slot_index) const noexcept;

    //  Utility
    [[nodiscard]] slots::RankMap build_rank_map() const noexcept;
    [[nodiscard]] std::int32_t reverse_lookup_index_scan(const T* const slot) const noexcept;

    //  Content management
    std::int32_t insert(const T& value) noexcept;
    bool erase(const std::int32_t slot_index) noexcept;
    void pack() noexcept;

    //  Initialisation and deallocation
    bool initialise(const std::size_t initial_slot_count = 0u) noexcept;
    void deallocate() noexcept;

    //  Integrity audit
    [[nodiscard]] bool check_integrity() const noexcept;

    //  Constants
    static constexpr std::size_t k_element_size = sizeof(T);

    //  Direct storage attribution
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(memory::CMemoryContext* context = nullptr) noexcept;

private:
    static [[nodiscard]] bool failed_integrity_check() noexcept;
};

//==============================================================================
//  TPodUnorderedSlots<T> out of class function bodies
//==============================================================================

template<typename T>
inline void TPodUnorderedSlotsStorage<T>::on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept
{
    T swap = m_slots[static_cast<std::size_t>(target_index)];
    m_slots[static_cast<std::size_t>(target_index)] = m_slots[static_cast<std::size_t>(source_index)];
    m_slots[static_cast<std::size_t>(source_index)] = swap;
}

template<typename T>
inline std::uint32_t TPodUnorderedSlotsStorage<T>::on_reserve_empty(
    const std::uint32_t minimum_capacity,
    const std::uint32_t recommended_capacity) noexcept
{
    (void)minimum_capacity;
    const std::size_t new_capacity = static_cast<std::size_t>(recommended_capacity);
    if (!m_slots.reallocate(new_capacity))
    {
        return 0u;
    }
    (void)m_slots.set_size(new_capacity);
    return recommended_capacity;
}

template<typename T>
inline std::uint32_t TPodUnorderedSlotsStorage<T>::memory_token_count() const noexcept
{
    return m_slots.memory_token_count();
}

template<typename T>
inline std::uint32_t TPodUnorderedSlotsStorage<T>::memory_allocation_count() const noexcept
{
    return m_slots.memory_allocation_count();
}

template<typename T>
inline std::uint64_t TPodUnorderedSlotsStorage<T>::memory_allocation_size() const noexcept
{
    return m_slots.memory_allocation_size();
}

template<typename T>
inline bool TPodUnorderedSlotsStorage<T>::memory_source_context(memory::CMemoryContext*& source) const noexcept
{
    memory::CMemoryContext* const context = m_slots.memory_source_context();
    if ((source != nullptr) && (context != nullptr) && (context != source))
    {
        return false;
    }
    if (source == nullptr)
    {
        source = context;
    }
    return true;
}

template<typename T>
inline void TPodUnorderedSlotsStorage<T>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_slots.unsafe_replace_memory_context_without_accounting(expected_source, target);
}

template<typename T>
inline bool TPodUnorderedSlots<T>::is_valid() const noexcept
{
    //  Backing growth precedes metadata growth. A failed metadata allocation
    //  may retain excess backing; every metadata slot must still be covered.
    return this->m_slots.is_valid() && (this->m_slots.size() >= slot_meta_class::capacity());
}

template<typename T>
inline bool TPodUnorderedSlots<T>::is_empty() const noexcept
{
    return slot_meta_class::is_empty();
}

template<typename T>
inline bool TPodUnorderedSlots<T>::is_ready() const noexcept
{
    return this->m_slots.is_ready();
}

template<typename T>
inline std::uint32_t TPodUnorderedSlots<T>::occupied_count() const noexcept
{
    return slot_meta_class::loose_count();
}

template<typename T>
inline T* TPodUnorderedSlots<T>::get_slot(const std::int32_t slot_index) noexcept
{
    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    if (element_index < this->m_slots.size())
    {
        if (slot_meta_class::is_loose_slot(slot_index))
        {
            return &this->m_slots[element_index];
        }
    }
    return nullptr;
}

template<typename T>
inline const T* TPodUnorderedSlots<T>::get_slot(const std::int32_t slot_index) const noexcept
{
    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    if (element_index < this->m_slots.size())
    {
        if (slot_meta_class::is_loose_slot(slot_index))
        {
            return &this->m_slots[element_index];
        }
    }
    return nullptr;
}

template<typename T>
inline std::int32_t TPodUnorderedSlots<T>::first_live() const noexcept
{
    return slot_meta_class::first_loose();
}

template<typename T>
inline std::int32_t TPodUnorderedSlots<T>::last_live() const noexcept
{
    return slot_meta_class::last_loose();
}

template<typename T>
inline std::int32_t TPodUnorderedSlots<T>::prev_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::prev_loose(slot_index);
}

template<typename T>
inline std::int32_t TPodUnorderedSlots<T>::next_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::next_loose(slot_index);
}

template<typename T>
inline slots::RankMap TPodUnorderedSlots<T>::build_rank_map() const noexcept
{
    return slot_meta_class::build_rank_map();
}

template<typename T>
inline std::int32_t TPodUnorderedSlots<T>::reverse_lookup_index_scan(const T* const slot) const noexcept
{
    const std::size_t element_count = this->m_slots.size();
    for (std::size_t element_index = 0u; element_index < element_count; ++element_index)
    {
        const std::int32_t slot_index = static_cast<std::int32_t>(element_index);
        if (slot_meta_class::is_loose_slot(slot_index))
        {
            if (slot == &this->m_slots[element_index])
            {
                return slot_index;
            }
        }
    }
    return -1;
}

template<typename T>
inline std::int32_t TPodUnorderedSlots<T>::insert(const T& value) noexcept
{
    const std::int32_t slot_index = slot_meta_class::reserve_and_acquire(-1);
    if (slot_index < 0)
    {
        return -1;
    }

    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    this->m_slots[element_index] = value;
    return slot_index;
}

template<typename T>
inline bool TPodUnorderedSlots<T>::erase(const std::int32_t slot_index) noexcept
{
    return slot_meta_class::erase(slot_index);
}

template<typename T>
inline void TPodUnorderedSlots<T>::pack() noexcept
{
    slot_meta_class::pack();
}

template<typename T>
inline bool TPodUnorderedSlots<T>::initialise(const std::size_t initial_slot_count) noexcept
{
    deallocate();
    if (slot_meta_class::initialise(std::max(static_cast<std::uint32_t>(initial_slot_count), 32u)))
    {
        const std::size_t size = slot_meta_class::capacity();
        if (this->m_slots.allocate(size))
        {
            (void)this->m_slots.set_size(size);
            return true;
        }
        (void)slot_meta_class::shutdown();
    }
    return false;
}

template<typename T>
inline void TPodUnorderedSlots<T>::deallocate() noexcept
{
    (void)slot_meta_class::shutdown();
    this->m_slots.deallocate();
}

template<typename T>
inline bool TPodUnorderedSlots<T>::check_integrity() const noexcept
{
    if (!is_valid())
    {
        return failed_integrity_check();
    }

    if (!slot_meta_class::check_integrity())
    {
        return false;
    }

    return true;
}

template<typename T>
inline std::uint32_t TPodUnorderedSlots<T>::memory_token_count() const noexcept
{
    return slot_data_class::memory_token_count() + slot_meta_class::memory_token_count();
}

template<typename T>
inline std::uint32_t TPodUnorderedSlots<T>::memory_allocation_count() const noexcept
{
    return slot_data_class::memory_allocation_count() + slot_meta_class::memory_allocation_count();
}

template<typename T>
inline std::uint64_t TPodUnorderedSlots<T>::memory_allocation_size() const noexcept
{
    return slot_data_class::memory_allocation_size() + slot_meta_class::memory_allocation_size();
}

template<typename T>
inline bool TPodUnorderedSlots<T>::can_reattribute_to(memory::CMemoryContext* target) const noexcept
{
    target = (target != nullptr) ? target : memory::get_ambient_memory_context();
    memory::CMemoryContext* source = nullptr;
    return (target != nullptr) &&
        slot_data_class::memory_source_context(source) &&
        slot_meta_class::memory_source_context(source) &&
        ((source == nullptr) || (source == target) || source->is_compatible_with(*target));
}

template<typename T>
inline bool TPodUnorderedSlots<T>::reattribute(memory::CMemoryContext* target) noexcept
{
    target = (target != nullptr) ? target : memory::get_ambient_memory_context();
    memory::CMemoryContext* source = nullptr;
    if ((target == nullptr) ||
        !slot_data_class::memory_source_context(source) ||
        !slot_meta_class::memory_source_context(source) ||
        ((source != nullptr) && (source != target) && !source->is_compatible_with(*target)))
    {
        return false;
    }

    if ((source != nullptr) && (source != target) &&
        !memory::reattribute(*source, *target, memory_allocation_count(), memory_allocation_size()))
    {
        return false;
    }

    slot_data_class::unsafe_replace_memory_context_without_accounting(source, target);
    slot_meta_class::unsafe_replace_memory_context_without_accounting(source, target);
    return true;
}

template<typename T>
inline bool TPodUnorderedSlots<T>::failed_integrity_check() noexcept
{
    MV_ASSERT(false);
    return false;
}

#endif  //  TPOD_UNORDERED_SLOTS_HPP_INCLUDED
