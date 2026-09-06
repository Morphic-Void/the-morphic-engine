
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TOrderedCollection.hpp
//  Author: Ritchie Brannan
//  Date:   24 Mar 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Ordered collection over stable storage with slot-based identity
//  and key-based ordering.
//
//  Uses TOrderedSlots for ordering and slot management and
//  memory::CMemoryToken in stable mode for address-stable object backing.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  slot_index is the public identity during mutation and is not stable
//  across sort_and_pack().
//
//  sort_and_pack() remaps slot metadata, slot-side payload, and keys
//  but does not relocate constructed objects.
//
//  Ordered traversal is defined over constructed keyed slots.
//
//  See docs/containers/TOrderedCollection.md for the full documentation.

#pragma once

#ifndef TORDERED_COLLECTION_HPP_INCLUDED
#define TORDERED_COLLECTION_HPP_INCLUDED

#include <algorithm>    //  std::max, std::min
#include <cstddef>      //  std::size_t
#include <type_traits>  //  std::is_const_v, std::is_nothrow_destructible_v, std::is_trivially_copyable_v
#include <utility>      //  std::forward<TArgs>

#include "algo/validate_permutations.hpp"
#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"
#include "slots/TOrderedSlots.hpp"
#include "slots/SlotsRankMap.hpp"
#include "bit_utils/bit_ops.hpp"
#include "TPodVector.hpp"

#include "debug/macros.hpp"

//==============================================================================
//  TOrderedCollection<T>
//  Owning unique stable address slot manager for non-trivial types.
//==============================================================================

template<typename T, typename TKey>
class TOrderedCollectionStorage
{
public:
    enum class SlotState : std::size_t
    {
        Unmapped = 0u,
        Mapped = 1u,
        Constructed = 2u
    };

    struct SlotData
    {
        SlotState state;
        std::size_t storage_index;
    };

    void on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept;
    [[nodiscard]] std::uint32_t on_reserve_empty(const std::uint32_t minimum_capacity, const std::uint32_t recommended_capacity) noexcept;
    [[nodiscard]] std::int32_t on_compare_keys(const std::int32_t source_index, const std::int32_t target_index) const noexcept;

protected:
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool memory_source_context(memory::CMemoryContext*& source) const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* expected_source, memory::CMemoryContext* target) noexcept;

    memory::CMemoryToken m_storage;
    TPodVector<SlotData> m_slots;
    TPodVector<TKey> m_keys;

    SlotData m_swap_slot;
    TKey m_swap_key;

    mutable TKey m_staged_key;
};

template<typename T, typename TKey>
class TOrderedCollection : public slots::TOrderedSlots<TOrderedCollectionStorage<T, TKey>, std::int32_t>
{
private:
    using slot_data_class = TOrderedCollectionStorage<T, TKey>;
    using slot_meta_class = slots::TOrderedSlots<slot_data_class, std::int32_t>;

    static_assert(!std::is_const_v<T>, "TOrderedCollection<T, TKey> requires non-const T.");
    static_assert(!std::is_const_v<TKey>, "TOrderedCollection<T, TKey> requires non-const TKey.");
    static_assert(std::is_nothrow_destructible_v<T>, "TOrderedCollection<T, TKey> requires T to be nothrow destructible.");
    static_assert(std::is_trivially_copyable_v<TKey>, "TOrderedCollection<T, TKey> requires trivially copyable TKey.");

public:

    //  Default and deleted lifetime
    TOrderedCollection() noexcept = default;
    TOrderedCollection(const TOrderedCollection&) noexcept = delete;
    TOrderedCollection& operator=(const TOrderedCollection&) noexcept = delete;
    TOrderedCollection(TOrderedCollection&&) noexcept = default;
    TOrderedCollection& operator=(TOrderedCollection&&) noexcept = default;

    //  Destructor
    ~TOrderedCollection() noexcept { deallocate(); };

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Accessors
    T* get_object(const TKey& key) noexcept;
    T* get_object(const std::int32_t slot_index) noexcept;
    const T* get_object(const TKey& key) const noexcept;
    const T* get_object(const std::int32_t slot_index) const noexcept;

    //  O(1) access to the key paired with a constructed slot.
    [[nodiscard]] const TKey* key_at_slot(const std::int32_t slot_index) const noexcept;

    //  Traversal
    [[nodiscard]] std::int32_t first_live() const noexcept;
    [[nodiscard]] std::int32_t last_live() const noexcept;
    [[nodiscard]] std::int32_t prev_live(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::int32_t next_live(const std::int32_t slot_index) const noexcept;

    //  Utility
    [[nodiscard]] slots::RankMap build_rank_map() const noexcept;
    [[nodiscard]] std::int32_t reverse_lookup_slot_index_scan(const T* const object) const noexcept;
    [[nodiscard]] std::int32_t find_slot(const TKey& key) const noexcept;

    //  Content management
    template<typename... TArgs> std::int32_t emplace(const TKey& key, TArgs&&... args) noexcept;
    bool erase(const TKey& key) noexcept;
    bool erase(const std::int32_t slot_index) noexcept;
    void sort_and_pack() noexcept;

    //  Initialisation and deallocation
    bool initialise(const std::size_t initial_slot_count = 0u, const std::size_t slots_per_buffer = 0u) noexcept;
    void deallocate() noexcept;

    //  Integrity audit
    [[nodiscard]] bool check_integrity() const noexcept;

    //  Constants
    static constexpr std::size_t k_max_elements = memory::t_max_elements<T>();
    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_element_align = memory::t_default_align<T>();

    //  Direct storage attribution. Allocations owned by contained T objects are excluded.
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(memory::CMemoryContext* context = nullptr) noexcept;

private:
    [[nodiscard]] bool storage_is_valid() const noexcept;
    [[nodiscard]] bool storage_is_ready() const noexcept;
    [[nodiscard]] T* storage_map_index(const std::size_t storage_index) noexcept;
    [[nodiscard]] T* storage_index_ptr(const std::size_t storage_index) noexcept;
    [[nodiscard]] const T* storage_index_ptr(const std::size_t storage_index) const noexcept;

    void deconstruct_payload() noexcept;
    static [[nodiscard]] bool failed_integrity_check() noexcept;

    using SlotState = typename slot_data_class::SlotState;
    using SlotData = typename slot_data_class::SlotData;
};

//==============================================================================
//  TOrderedCollection<T> out of class function bodies
//==============================================================================

template<typename T, typename TKey>
inline void TOrderedCollectionStorage<T, TKey>::on_move_payload(const std::int32_t source_index, const std::int32_t target_index) noexcept
{
    SlotData& source_slot = (source_index < 0) ? m_swap_slot : m_slots[source_index];
    SlotData& target_slot = (target_index < 0) ? m_swap_slot : m_slots[target_index];
    target_slot = source_slot;
    TKey& source_key = (source_index < 0) ? m_swap_key : m_keys[source_index];
    TKey& target_key = (target_index < 0) ? m_swap_key : m_keys[target_index];
    target_key = source_key;
}

template<typename T, typename TKey>
inline std::uint32_t TOrderedCollectionStorage<T, TKey>::on_reserve_empty(const std::uint32_t minimum_capacity, const std::uint32_t recommended_capacity) noexcept
{
    (void)minimum_capacity;
    const std::size_t new_capacity = static_cast<std::size_t>(recommended_capacity);
    if (!m_slots.reallocate(new_capacity) || !m_keys.reallocate(new_capacity))
    {   //  decline the reserve attempt and force the base class slot acquisition to fail
        return 0u;
    }
    for (std::size_t i = m_slots.size(); i < new_capacity; ++i)
    {
        (void)m_slots.push_back({ SlotState::Unmapped, i });
    }
    (void)m_keys.set_size(new_capacity);
    return recommended_capacity;
}

template<typename T, typename TKey>
inline std::int32_t TOrderedCollectionStorage<T, TKey>::on_compare_keys(const std::int32_t source_index, const std::int32_t target_index) const noexcept
{
    const TKey& source_key = (source_index < 0) ? m_staged_key : m_keys[source_index];
    const TKey& target_key = (target_index < 0) ? m_staged_key : m_keys[target_index];
    return static_cast<std::int32_t>(source_key.relationship(target_key));
}

template<typename T, typename TKey>
inline std::uint32_t TOrderedCollectionStorage<T, TKey>::memory_token_count() const noexcept
{
    return
        m_storage.memory_token_count() +
        m_slots.memory_token_count() +
        m_keys.memory_token_count();
}

template<typename T, typename TKey>
inline std::uint32_t TOrderedCollectionStorage<T, TKey>::memory_allocation_count() const noexcept
{
    return
        m_storage.memory_allocation_count() +
        m_slots.memory_allocation_count() +
        m_keys.memory_allocation_count();
}

template<typename T, typename TKey>
inline std::uint64_t TOrderedCollectionStorage<T, TKey>::memory_allocation_size() const noexcept
{
    return
        m_storage.memory_allocation_size() +
        m_slots.memory_allocation_size() +
        m_keys.memory_allocation_size();
}

template<typename T, typename TKey>
inline bool TOrderedCollectionStorage<T, TKey>::memory_source_context(memory::CMemoryContext*& source) const noexcept
{
    if (m_storage.owns_storage())
    {
        if ((source != nullptr) && (source != m_storage.context()))
        {
            return false;
        }
        source = m_storage.context();
    }

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
inline void TOrderedCollectionStorage<T, TKey>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_storage.unsafe_replace_context_without_accounting(expected_source, target);
    m_slots.unsafe_replace_memory_context_without_accounting(expected_source, target);
    m_keys.unsafe_replace_memory_context_without_accounting(expected_source, target);
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::is_valid() const noexcept
{
    return
        storage_is_valid() &&
        this->m_slots.is_valid() && (this->m_slots.size() == slot_meta_class::capacity()) &&
        this->m_keys.is_valid() &&  (this->m_keys.size() == slot_meta_class::capacity()) &&
        (this->m_storage.count() <= this->m_slots.size());
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::is_empty() const noexcept
{
    return slot_meta_class::is_empty();
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::is_ready() const noexcept
{
    return this->m_slots.is_ready() && storage_is_ready();
}

template<typename T, typename TKey>
inline T* TOrderedCollection<T, TKey>::get_object(const TKey& key) noexcept
{
    this->m_staged_key = key;
    return get_object(slot_meta_class::find_any_equal());
}

template<typename T, typename TKey>
inline T* TOrderedCollection<T, TKey>::get_object(const std::int32_t slot_index) noexcept
{
    const std::size_t internal_slot_index = static_cast<std::size_t>(slot_index);
    if (internal_slot_index < this->m_slots.size())
    {
        const SlotData& slot = this->m_slots[internal_slot_index];
        if (slot.state == SlotState::Constructed)
        {
            T* const element = storage_index_ptr(slot.storage_index);
            MV_ASSERT(element != nullptr);
            return element;
        }
    }
    return nullptr;
}

template<typename T, typename TKey>
inline const T* TOrderedCollection<T, TKey>::get_object(const TKey& key) const noexcept
{
    this->m_staged_key = key;
    return get_object(slot_meta_class::find_any_equal());
}

template<typename T, typename TKey>
inline const T* TOrderedCollection<T, TKey>::get_object(const std::int32_t slot_index) const noexcept
{
    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    if (element_index < this->m_slots.size())
    {
        const SlotData& slot = this->m_slots[element_index];
        if (slot.state == SlotState::Constructed)
        {
            const T* const element = storage_index_ptr(slot.storage_index);
            MV_ASSERT(element != nullptr);
            return element;
        }
    }
    return nullptr;
}

template<typename T, typename TKey>
[[nodiscard]] inline const TKey* TOrderedCollection<T, TKey>::key_at_slot(const std::int32_t slot_index) const noexcept
{
    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    if (element_index < this->m_keys.size())
    {
        const SlotData& slot = this->m_slots[element_index];
        if (slot.state == SlotState::Constructed)
        {
            return &this->m_keys[element_index];
        }
    }
    return nullptr;
}

template<typename T, typename TKey>
inline std::int32_t TOrderedCollection<T, TKey>::find_slot(const TKey& key) const noexcept
{
    this->m_staged_key = key;
    return slot_meta_class::find_any_equal();
}

template<typename T, typename TKey>
inline std::int32_t TOrderedCollection<T, TKey>::first_live() const noexcept
{
    return slot_meta_class::first_lexed();
}

template<typename T, typename TKey>
inline std::int32_t TOrderedCollection<T, TKey>::last_live() const noexcept
{
    return slot_meta_class::last_lexed();
}

template<typename T, typename TKey>
inline std::int32_t TOrderedCollection<T, TKey>::prev_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::prev_lexed(slot_index);
}

template<typename T, typename TKey>
inline std::int32_t TOrderedCollection<T, TKey>::next_live(const std::int32_t slot_index) const noexcept
{
    return slot_meta_class::next_lexed(slot_index);
}

template<typename T, typename TKey>
inline slots::RankMap TOrderedCollection<T, TKey>::build_rank_map() const noexcept
{
    return slot_meta_class::build_rank_map();
}

template<typename T, typename TKey>
inline std::int32_t TOrderedCollection<T, TKey>::reverse_lookup_slot_index_scan(const T* const object) const noexcept
{
    const std::size_t element_count = this->m_slots.size();
    for (std::size_t element_index = 0u; element_index < element_count; ++element_index)
    {
        const SlotData& slot = this->m_slots[element_index];
        if (slot.state == SlotState::Constructed)
        {
            if (object == storage_index_ptr(slot.storage_index))
            {
                return static_cast<std::int32_t>(element_index);
            }
        }
    }
    return -1;
}

template<typename T, typename TKey>
template<typename... TArgs>
inline std::int32_t TOrderedCollection<T, TKey>::emplace(const TKey& key, TArgs&&... args) noexcept
{
    static_assert(std::is_nothrow_constructible_v<T, TArgs&&...>,
        "TOrderedCollection<T, TKey>::emplace(...) requires T to be nothrow constructible.");

    //  Acquire a slot index
    this->m_staged_key = key;
    const std::int32_t slot_index = slot_meta_class::reserve_and_acquire(-1, /* lex */ true, /* require_unique */ true);
    if (slot_index < 0)
    {
        return -1;
    }

    //  Fetch or map backing storage
    T* element = nullptr;
    SlotData& slot = this->m_slots[static_cast<std::size_t>(slot_index)];
    MV_ASSERT(slot.state != SlotState::Constructed);
    if (slot.state == SlotState::Unmapped)
    {
        element = storage_map_index(slot.storage_index);
        if (element != nullptr)
        {
            slot.state = SlotState::Mapped;
        }
    }
    else if(slot.state == SlotState::Mapped)
    {
        element = storage_index_ptr(slot.storage_index);
    }
    if (element == nullptr)
    {
        (void)slot_meta_class::erase(slot_index);
        MV_ASSERT(false);
        return -1;
    }

    //  Construct the object
    new (element) T(std::forward<TArgs>(args)...);
    slot.state = SlotState::Constructed;
    this->m_keys[static_cast<std::size_t>(slot_index)] = key;
    return slot_index;
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::erase(const TKey& key) noexcept
{
    this->m_staged_key = key;
    return erase(slot_meta_class::find_any_equal());
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::erase(const std::int32_t slot_index) noexcept
{
    const std::size_t element_index = static_cast<std::size_t>(slot_index);
    if (element_index < this->m_slots.size())
    {
        SlotData& slot = this->m_slots[element_index];
        if (slot.state == SlotState::Constructed)
        {
            T* const element = storage_index_ptr(slot.storage_index);
            MV_ASSERT(element != nullptr);
            if (element != nullptr)
            {
                element->~T();
                slot.state = SlotState::Mapped;
                return slot_meta_class::erase(slot_index);
            }
        }
    }
    return false;
}

template<typename T, typename TKey>
inline void TOrderedCollection<T, TKey>::sort_and_pack() noexcept
{
    slot_meta_class::sort_and_pack(false);
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::initialise(const std::size_t initial_slot_count, const std::size_t slots_per_buffer) noexcept
{
    deallocate();
    if (slot_meta_class::initialise(std::max(static_cast<std::uint32_t>(initial_slot_count), 32u)))
    {
        memory::CMemoryToken storage;
        if (storage.configure_stable(k_element_size, k_element_align, std::max(slots_per_buffer, std::size_t{ 32u })))
        {
            const std::size_t size = slot_meta_class::capacity();
            if (this->m_slots.allocate(size))
            {
                if (this->m_keys.allocate(size))
                {
                    for (std::size_t i = 0u; i < size; ++i)
                    {
                        (void)this->m_slots.push_back({ SlotState::Unmapped, i });
                    }
                    (void)this->m_keys.set_size(size);
                    this->m_storage = std::move(storage);
                    return true;
                }
                this->m_slots.deallocate();
            }
        }
        (void)slot_meta_class::shutdown();
    }
    return false;
}

template<typename T, typename TKey>
inline void TOrderedCollection<T, TKey>::deallocate() noexcept
{
    deconstruct_payload();
    (void)slot_meta_class::shutdown();
    this->m_storage.deallocate();
    this->m_storage = memory::CMemoryToken{};
    this->m_slots.deallocate();
    this->m_keys.deallocate();
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::check_integrity() const noexcept
{
    //  basic structural integrity check
    if (!is_valid())
    {
        return failed_integrity_check();
    }

    //  base class integrity check
    if (!slot_meta_class::check_integrity())
    {   //  no need to catch the error here as the base class will have already caught it
        return false;
    }

    //  metadata coherence check
    const std::size_t element_count = this->m_slots.size();
    for (std::size_t element_index = 0u; element_index < element_count; ++element_index)
    {
        const std::int32_t slot_index = static_cast<std::int32_t>(element_index);
        const SlotData& slot = this->m_slots[element_index];
        if (slot.storage_index >= element_count)
        {
            return failed_integrity_check();
        }
        switch (slot.state)
        {
            case(SlotState::Unmapped):
            {
                if (!slot_meta_class::is_empty_slot(slot_index))
                {
                    return failed_integrity_check();
                }
                break;
            }
            case(SlotState::Mapped):
            {
                if (!slot_meta_class::is_empty_slot(slot_index))
                {
                    return failed_integrity_check();
                }
                if (storage_index_ptr(slot.storage_index) == nullptr)
                {
                    return failed_integrity_check();
                }
                break;
            }
            case(SlotState::Constructed):
            {
                if (!slot_meta_class::is_lexed_slot(slot_index))
                {
                    return failed_integrity_check();
                }
                if (storage_index_ptr(slot.storage_index) == nullptr)
                {
                    return failed_integrity_check();
                }
                break;
            }
            default:
            {
                return failed_integrity_check();
            }
        }
    }

    //  storage mapping permutation check
    if (!algo::validate_extracted_permutation(this->m_slots.data(), this->m_slots.size(),
        [](const SlotData& slot) noexcept { return slot.storage_index; }))
    {
        return failed_integrity_check();
    }

    //  base class tree structure and ordering check
    return slot_meta_class::validate_tree(slot_meta_class::LexCheck::Unique);
}

template<typename T, typename TKey>
inline std::uint32_t TOrderedCollection<T, TKey>::memory_token_count() const noexcept
{
    return slot_data_class::memory_token_count() + slot_meta_class::memory_token_count();
}

template<typename T, typename TKey>
inline std::uint32_t TOrderedCollection<T, TKey>::memory_allocation_count() const noexcept
{
    return slot_data_class::memory_allocation_count() + slot_meta_class::memory_allocation_count();
}

template<typename T, typename TKey>
inline std::uint64_t TOrderedCollection<T, TKey>::memory_allocation_size() const noexcept
{
    return slot_data_class::memory_allocation_size() + slot_meta_class::memory_allocation_size();
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::can_reattribute_to(memory::CMemoryContext* target) const noexcept
{
    target = (target != nullptr) ? target : memory::get_ambient_memory_context();
    memory::CMemoryContext* source = nullptr;
    return (target != nullptr) &&
        slot_data_class::memory_source_context(source) &&
        slot_meta_class::memory_source_context(source) &&
        ((source == nullptr) || (source == target) || source->is_compatible_with(*target));
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::reattribute(memory::CMemoryContext* target) noexcept
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
inline bool TOrderedCollection<T, TKey>::storage_is_valid() const noexcept
{
    if (!this->m_storage.is_configured())
    {
        return !this->m_storage.owns_storage() &&
            (this->m_storage.context() == nullptr) &&
            (this->m_storage.count() == 0u) &&
            (this->m_storage.stride() == 0u) &&
            (this->m_storage.storage_alignment() == 0u) &&
            (this->m_storage.per_buffer_capacity() == 0u);
    }

    return this->m_storage.is_stable() &&
        (this->m_storage.stride() == k_element_size) &&
        (this->m_storage.storage_alignment() == k_element_align) &&
        (this->m_storage.per_buffer_capacity() >= 32u) &&
        bit_ops::is_pow2(this->m_storage.per_buffer_capacity()) &&
        (this->m_storage.count() <= k_max_elements);
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::storage_is_ready() const noexcept
{
    return this->m_storage.is_stable();
}

template<typename T, typename TKey>
inline T* TOrderedCollection<T, TKey>::storage_index_ptr(const std::size_t storage_index) noexcept
{
    return static_cast<T*>(this->m_storage.index_ptr(storage_index));
}

template<typename T, typename TKey>
inline const T* TOrderedCollection<T, TKey>::storage_index_ptr(const std::size_t storage_index) const noexcept
{
    return static_cast<const T*>(this->m_storage.index_ptr(storage_index));
}

template<typename T, typename TKey>
inline T* TOrderedCollection<T, TKey>::storage_map_index(const std::size_t storage_index) noexcept
{
    return static_cast<T*>(this->m_storage.map_index(storage_index));
}

template<typename T, typename TKey>
inline void TOrderedCollection<T, TKey>::deconstruct_payload() noexcept
{
    const std::size_t element_count = this->m_slots.size();
    for (std::size_t element_index = 0u; element_index < element_count; ++element_index)
    {
        SlotData& slot = this->m_slots[element_index];
        if (slot.state == SlotState::Constructed)
        {
            T* element = storage_index_ptr(slot.storage_index);
            MV_ASSERT(element != nullptr);
            if (element != nullptr)
            {
                element->~T();
                slot.state = SlotState::Mapped;
                (void)slot_meta_class::erase(static_cast<std::int32_t>(element_index));
            }
        }
    }
}

template<typename T, typename TKey>
inline bool TOrderedCollection<T, TKey>::failed_integrity_check() noexcept
{
    MV_ASSERT(false);
    return false;
}

#endif  //  TORDERED_COLLECTION_HPP_INCLUDED
