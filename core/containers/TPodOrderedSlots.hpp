
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TPodOrderedSlots.hpp
//  Author: Ritchie Brannan
//  Date:   13 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Ordered POD slot manager with slot-based identity and key-based ordering.
//
//  Uses TOrderedSlots for ordering and slot management.
//  Stores trivial payload data directly in slot-indexed POD storage.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  slot_index is the public identity during mutation and is not stable
//  across sort_and_pack().
//
//  sort_and_pack() remaps slot metadata, payload data, and keys.
//
//  Ordered traversal is defined over live keyed slots.

#pragma once

#ifndef TPOD_ORDERED_SLOTS_HPP_INCLUDED
#define TPOD_ORDERED_SLOTS_HPP_INCLUDED

#include <algorithm>    //  std::max
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::int32_t, std::uint32_t
#include <type_traits>  //  std::is_const_v, std::is_copy_assignable_v, std::is_trivially_copyable_v

#include "memory/memory_policies.hpp"
#include "slots/TOrderedSlots.hpp"
#include "slots/SlotsRankMap.hpp"
#include "TPodVector.hpp"

#include "debug/macros.hpp"

//==============================================================================
//  TPodOrderedSlots<T, TKey>
//  Ordered keyed POD slot manager.
//==============================================================================

template<typename T, typename TKey>
class TPodOrderedSlotsStorage
{
public:
    void on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept;
    [[nodiscard]] std::uint32_t on_reserve_empty(const std::uint32_t minimum_capacity, const std::uint32_t recommended_capacity) noexcept;
    [[nodiscard]] std::int32_t on_compare_keys(const std::int32_t source_index, const std::int32_t target_index) const noexcept;

protected:
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool memory_source_context(memory::CMemoryContext*& source) const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

    TPodVector<T> m_slots;
    TPodVector<TKey> m_keys;

    T m_swap_slot;
    TKey m_swap_key;

    mutable TKey m_staged_key;
};

template<typename T, typename TKey>
class TPodOrderedSlots : public slots::TOrderedSlots<TPodOrderedSlotsStorage<T, TKey>, std::int32_t>
{
private:
    using slot_data_class = TPodOrderedSlotsStorage<T, TKey>;
    using slot_meta_class = slots::TOrderedSlots<slot_data_class, std::int32_t>;

    static_assert(!std::is_const_v<T>, "TPodOrderedSlots<T, TKey> requires non-const T.");
    static_assert(!std::is_const_v<TKey>, "TPodOrderedSlots<T, TKey> requires non-const TKey.");
    static_assert(std::is_copy_assignable_v<T>, "TPodOrderedSlots<T, TKey> requires copy-assignable T.");
    static_assert(std::is_copy_assignable_v<TKey>, "TPodOrderedSlots<T, TKey> requires copy-assignable TKey.");
    static_assert(std::is_trivially_copyable_v<T>, "TPodOrderedSlots<T, TKey> requires trivially copyable T.");
    static_assert(std::is_trivially_copyable_v<TKey>, "TPodOrderedSlots<T, TKey> requires trivially copyable TKey.");

public:

    //  Default and deleted lifetime
    TPodOrderedSlots() noexcept = default;
    TPodOrderedSlots(const TPodOrderedSlots&) noexcept = delete;
    TPodOrderedSlots& operator=(const TPodOrderedSlots&) noexcept = delete;
    TPodOrderedSlots(TPodOrderedSlots&&) noexcept = default;
    TPodOrderedSlots& operator=(TPodOrderedSlots&&) noexcept = default;

    //  Destructor
    ~TPodOrderedSlots() noexcept { deallocate(); }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Occupied count
    [[nodiscard]] std::uint32_t occupied_count() const noexcept;

    //  Accessors
    T* get_slot(const TKey& key) noexcept;
    T* get_slot(const std::int32_t slot_index) noexcept;
    const T* get_slot(const TKey& key) const noexcept;
    const T* get_slot(const std::int32_t slot_index) const noexcept;

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
    std::int32_t insert(const TKey& key, const T& value) noexcept;
    bool erase(const TKey& key) noexcept;
    bool erase(const std::int32_t slot_index) noexcept;
    void sort_and_pack() noexcept;

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
//  TPodOrderedSlots<T, TKey> out of class function bodies
//==============================================================================

template<typename T, typename TKey>
inline void TPodOrderedSlotsStorage<T, TKey>::on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept
{
    T& source_slot = (source_index < 0) ? m_swap_slot : m_slots[static_cast<std::size_t>(source_index)];
    T& target_slot = (target_index < 0) ? m_swap_slot : m_slots[static_cast<std::size_t>(target_index)];
    target_slot = source_slot;

    TKey& source_key = (source_index < 0) ? m_swap_key : m_keys[static_cast<std::size_t>(source_index)];
    TKey& target_key = (target_index < 0) ? m_swap_key : m_keys[static_cast<std::size_t>(target_index)];
    target_key = source_key;
}

template<typename T, typename TKey>
inline std::uint32_t TPodOrderedSlotsStorage<T, TKey>::on_reserve_empty(
    const std::uint32_t minimum_capacity,
    const std::uint32_t recommended_capacity) noexcept
{
    (void)minimum_capacity;
    const std::size_t new_capacity = static_cast<std::size_t>(recommended_capacity);
    if (!m_slots.reallocate(new_capacity) || !m_keys.reallocate(new_capacity))
    {
        return 0u;
    }
    (void)m_slots.set_size(new_capacity);
    (void)m_keys.set_size(new_capacity);
    return recommended_capacity;
}

template<typename T, typename TKey>
inline std::int32_t TPodOrderedSlotsStorage<T, TKey>::on_compare_keys(const std::int32_t source_index, const std::int32_t target_index) const noexcept
{
    const TKey& source_key = (source_index < 0) ? m_staged_key : m_keys[static_cast<std::size_t>(source_index)];
    const TKey& target_key = (target_index < 0) ? m_staged_key : m_keys[static_cast<std::size_t>(target_index)];
    return static_cast<std::int32_t>(source_key.relationship(target_key));
}

template<typename T, typename TKey>
inline std::uint32_t TPodOrderedSlotsStorage<T, TKey>::memory_token_count() const noexcept
{
    return m_slots.memory_token_count() + m_keys.memory_token_count();
}

template<typename T, typename TKey>
inline std::uint32_t TPodOrderedSlotsStorage<T, TKey>::memory_allocation_count() const noexcept
{
    return m_slots.memory_allocation_count() + m_keys.memory_allocation_count();
}

template<typename T, typename TKey>
inline std::uint64_t TPodOrderedSlotsStorage<T, TKey>::memory_allocation_size() const noexcept
{
    return m_slots.memory_allocation_size() + m_keys.memory_allocation_size();
}

template<typename T, typename TKey>
inline bool TPodOrderedSlotsStorage<T, TKey>::memory_source_context(memory::CMemoryContext*& source) const noexcept
{
    memory::CMemoryContext* context = m_slots.memory_source_context();
    if ((source != nullptr) && (context != nullptr) && (context != source))
    {
        return false;
    }
    if (source == nullptr)
    {
        source = context;
    }

    context = m_keys.memory_source_context();
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

template<typename T, typename TKey>
inline void TPodOrderedSlotsStorage<T, TKey>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_slots.unsafe_replace_memory_context_without_accounting(expected_source, target);
    m_keys.unsafe_replace_memory_context_without_accounting(expected_source, target);
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::is_valid() const noexcept
{
    return
        this->m_slots.is_valid() && (this->m_slots.size() == slot_meta_class::capacity()) &&
        this->m_keys.is_valid() && (this->m_keys.size() == slot_meta_class::capacity());
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::is_empty() const noexcept
{
    return slot_meta_class::is_empty();
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::is_ready() const noexcept
{
    return this->m_slots.is_ready() && this->m_keys.is_ready();
}

template<typename T, typename TKey>
inline std::uint32_t TPodOrderedSlots<T, TKey>::occupied_count() const noexcept
{
    return slot_meta_class::occupied_count();
}

template<typename T, typename TKey>
inline T* TPodOrderedSlots<T, TKey>::get_slot(const TKey& key) noexcept
{
    this->m_staged_key = key;
    return get_slot(slot_meta_class::find_any_equal());
}

template<typename T, typename TKey>
inline T* TPodOrderedSlots<T, TKey>::get_slot(const std::int32_t slot_index) noexcept
{
    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    if (element_index < this->m_slots.size())
    {
        if (slot_meta_class::is_lexed_slot(slot_index))
        {
            return &this->m_slots[element_index];
        }
    }
    return nullptr;
}

template<typename T, typename TKey>
inline const T* TPodOrderedSlots<T, TKey>::get_slot(const TKey& key) const noexcept
{
    this->m_staged_key = key;
    return get_slot(slot_meta_class::find_any_equal());
}

template<typename T, typename TKey>
inline const T* TPodOrderedSlots<T, TKey>::get_slot(const std::int32_t slot_index) const noexcept
{
    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    if (element_index < this->m_slots.size())
    {
        if (slot_meta_class::is_lexed_slot(slot_index))
        {
            return &this->m_slots[element_index];
        }
    }
    return nullptr;
}

template<typename T, typename TKey>
[[nodiscard]] inline const TKey* TPodOrderedSlots<T, TKey>::key_at_slot(const std::int32_t slot_index) const noexcept
{
    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    if (element_index < this->m_keys.size())
    {
        if (slot_meta_class::is_lexed_slot(slot_index))
        {
            return &this->m_keys[element_index];
        }
    }
    return nullptr;
}

template<typename T, typename TKey>
inline std::int32_t TPodOrderedSlots<T, TKey>::find_index(const TKey& key) const noexcept
{
    this->m_staged_key = key;
    return slot_meta_class::find_any_equal();
}

template<typename T, typename TKey>
inline std::int32_t TPodOrderedSlots<T, TKey>::first_live() const noexcept
{
    return slot_meta_class::first_lexed();
}

template<typename T, typename TKey>
inline std::int32_t TPodOrderedSlots<T, TKey>::last_live() const noexcept
{
    return slot_meta_class::last_lexed();
}

template<typename T, typename TKey>
inline std::int32_t TPodOrderedSlots<T, TKey>::prev_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::prev_lexed(slot_index);
}

template<typename T, typename TKey>
inline std::int32_t TPodOrderedSlots<T, TKey>::next_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::next_lexed(slot_index);
}

template<typename T, typename TKey>
inline slots::RankMap TPodOrderedSlots<T, TKey>::build_rank_map() const noexcept
{
    return slot_meta_class::build_rank_map();
}

template<typename T, typename TKey>
inline std::int32_t TPodOrderedSlots<T, TKey>::reverse_lookup_index_scan(const T* const slot) const noexcept
{
    const std::size_t element_count = this->m_slots.size();
    for (std::size_t element_index = 0u; element_index < element_count; ++element_index)
    {
        const std::int32_t slot_index = static_cast<std::int32_t>(element_index);
        if (slot_meta_class::is_lexed_slot(slot_index))
        {
            if (slot == &this->m_slots[element_index])
            {
                return slot_index;
            }
        }
    }
    return -1;
}

template<typename T, typename TKey>
inline std::int32_t TPodOrderedSlots<T, TKey>::insert(const TKey& key, const T& value) noexcept
{
    this->m_staged_key = key;
    const std::int32_t slot_index = slot_meta_class::reserve_and_acquire(-1, /* lex */ true, /* require_unique */ true);
    if (slot_index < 0)
    {
        return -1;
    }

    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    this->m_slots[element_index] = value;
    this->m_keys[element_index] = key;
    return slot_index;
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::erase(const TKey& key) noexcept
{
    return erase(find_index(key));
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::erase(const std::int32_t slot_index) noexcept
{
    return slot_meta_class::erase(slot_index);
}

template<typename T, typename TKey>
inline void TPodOrderedSlots<T, TKey>::sort_and_pack() noexcept
{
    slot_meta_class::sort_and_pack(false);
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::initialise(const std::size_t initial_slot_count) noexcept
{
    deallocate();
    if (slot_meta_class::initialise(std::max(static_cast<std::uint32_t>(initial_slot_count), 32u)))
    {
        const std::size_t size = slot_meta_class::capacity();
        if (this->m_slots.allocate(size))
        {
            if (this->m_keys.allocate(size))
            {
                (void)this->m_slots.set_size(size);
                (void)this->m_keys.set_size(size);
                return true;
            }
            this->m_slots.deallocate();
        }
        (void)slot_meta_class::shutdown();
    }
    return false;
}

template<typename T, typename TKey>
inline void TPodOrderedSlots<T, TKey>::deallocate() noexcept
{
    (void)slot_meta_class::shutdown();
    this->m_slots.deallocate();
    this->m_keys.deallocate();
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::check_integrity() const noexcept
{
    if (!is_valid())
    {
        return failed_integrity_check();
    }

    if (!slot_meta_class::check_integrity())
    {
        return false;
    }

    return slot_meta_class::validate_tree(slot_meta_class::LexCheck::Unique);
}

template<typename T, typename TKey>
inline std::uint32_t TPodOrderedSlots<T, TKey>::memory_token_count() const noexcept
{
    return slot_data_class::memory_token_count() + slot_meta_class::memory_token_count();
}

template<typename T, typename TKey>
inline std::uint32_t TPodOrderedSlots<T, TKey>::memory_allocation_count() const noexcept
{
    return slot_data_class::memory_allocation_count() + slot_meta_class::memory_allocation_count();
}

template<typename T, typename TKey>
inline std::uint64_t TPodOrderedSlots<T, TKey>::memory_allocation_size() const noexcept
{
    return slot_data_class::memory_allocation_size() + slot_meta_class::memory_allocation_size();
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::can_reattribute_to(memory::CMemoryContext* target) const noexcept
{
    target = (target != nullptr) ? target : memory::get_ambient_memory_context();
    memory::CMemoryContext* source = nullptr;
    return (target != nullptr) &&
        slot_data_class::memory_source_context(source) &&
        slot_meta_class::memory_source_context(source) &&
        ((source == nullptr) || (source == target) || source->is_compatible_with(*target));
}

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::reattribute(memory::CMemoryContext* target) noexcept
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

template<typename T, typename TKey>
inline bool TPodOrderedSlots<T, TKey>::failed_integrity_check() noexcept
{
    MV_ASSERT(false);
    return false;
}

#endif  //  TPOD_ORDERED_SLOTS_HPP_INCLUDED
