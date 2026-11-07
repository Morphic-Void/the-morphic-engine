
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TBuddyAllocatorChain.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    07 Nov 26
//
//  TVector ownership of homogeneous buddy managers with transactional growth.
//  Exclusive access is required. Payload addresses survive vector relocation.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "TBuddyAllocator.hpp"
#include "containers/TVector.hpp"

template<typename TWord>
class TBuddyAllocatorChain
{
private:
    using TManager = TBuddyAllocator<TWord>;

public:
    TBuddyAllocatorChain() noexcept = default;
    TBuddyAllocatorChain(const TBuddyAllocatorChain&) = delete;
    TBuddyAllocatorChain& operator=(const TBuddyAllocatorChain&) = delete;
    TBuddyAllocatorChain(TBuddyAllocatorChain&&) noexcept = default;
    TBuddyAllocatorChain& operator=(TBuddyAllocatorChain&&) noexcept = default;
    ~TBuddyAllocatorChain() noexcept = default;

    //  Prepare one empty manager. Existing ownership must be released first.
    [[nodiscard]] bool initialise(const SBuddyConfig& config, memory::CMemoryContext& context) noexcept;
    [[nodiscard]] void* allocate(const std::size_t bytes) noexcept;
    [[nodiscard]] EFreeResult free(void* const pointer) noexcept;
    [[nodiscard]] std::size_t allocation_size(const void* const pointer) const noexcept;

    //  Reset retains managers and vector capacity; deallocate releases both.
    void reset() noexcept;
    void deallocate() noexcept { m_managers.deallocate(); }

    [[nodiscard]] bool is_ready() const noexcept { return manager_count() != 0u; }
    [[nodiscard]] bool is_empty() const noexcept { return allocation_count() == 0u; }
    [[nodiscard]] std::size_t manager_count() const noexcept { return m_managers.size(); }
    [[nodiscard]] std::size_t manager_capacity() const noexcept { return m_managers.capacity(); }
    [[nodiscard]] std::size_t slot_size() const noexcept;
    //  Payload budget per manager, not the total retained backing.
    [[nodiscard]] std::size_t backing_size() const noexcept;
    [[nodiscard]] std::uint64_t allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t allocated_size() const noexcept;
    [[nodiscard]] std::size_t largest_available_block() const noexcept;
    [[nodiscard]] std::uint64_t total_owned_size() const noexcept { return memory_attribution().allocation_size; }
    //  Deep observation: vector storage and each owned manager exactly once.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Manager creation order, then address order within each manager.
    //  Callbacks must not mutate the chain. Manager objects are never exposed.
    template<typename TVisitor> void visit_allocations(TVisitor&& visitor) const;

private:
    TVector<TManager> m_managers;
};

using CBuddyAllocatorChain32 = TBuddyAllocatorChain<std::uint32_t>;
using CBuddyAllocatorChain64 = TBuddyAllocatorChain<std::uint64_t>;

//==============================================================================
//  TBuddyAllocatorChain out of class function bodies
//==============================================================================

template<typename TWord>
bool TBuddyAllocatorChain<TWord>::initialise(const SBuddyConfig& config, memory::CMemoryContext& context) noexcept
{
    if (is_ready()) return false;
    TManager first;
    if (!first.initialise(config, context)) return false;
    TVector<TManager> prepared{ context };
    if (!prepared.push_back(std::move(first)))
    {
        return false;
    }
    m_managers = std::move(prepared);
    return true;
}

template<typename TWord>
void* TBuddyAllocatorChain<TWord>::allocate(const std::size_t bytes) noexcept
{
    if (!is_ready() || (bytes == 0u) || (bytes > backing_size()))
    {
        return nullptr;
    }
    for (std::size_t i = 0u; i < manager_count(); ++i)
    {
        TManager& manager = m_managers[i];
        if (manager.largest_available_block() >= bytes)
        {
            return manager.allocate(bytes);
        }
    }
    //  Keep the requested block private until the owning descriptor is stored.
    //  Failed vector growth leaves candidate ownership here for cleanup.
    if (manager_count() == TVector<TManager>::k_max_elements)
    {
        return nullptr;
    }
    TManager candidate;
    memory::CMemoryContext* const context = m_managers.first().memory_attribution().source;
    if (!candidate.initialise({ slot_size(), backing_size() }, *context))
    {
        return nullptr;
    }
    void* const pointer = candidate.allocate(bytes);
    if ((pointer == nullptr) || !m_managers.push_back(std::move(candidate)))
    {
        return nullptr;
    }
    return pointer;
}

template<typename TWord>
EFreeResult TBuddyAllocatorChain<TWord>::free(void* const pointer) noexcept
{
    if (pointer == nullptr) return EFreeResult::no_action;
    for (std::size_t i = 0u; i < manager_count(); ++i)
    {
        const EFreeResult result = m_managers[i].free(pointer);
        if (result != EFreeResult::outside_backing)
        {
            return result;
        }
    }
    return EFreeResult::outside_backing;
}

template<typename TWord>
std::size_t TBuddyAllocatorChain<TWord>::allocation_size(const void* const pointer) const noexcept
{
    for (std::size_t i = 0u; i < manager_count(); ++i)
    {
        const std::size_t size = m_managers[i].allocation_size(pointer);
        if (size != 0u) return size;
    }
    return 0u;
}

template<typename TWord>
void TBuddyAllocatorChain<TWord>::reset() noexcept
{
    for (std::size_t i = 0u; i < manager_count(); ++i) m_managers[i].reset();
}

template<typename TWord>
std::size_t TBuddyAllocatorChain<TWord>::slot_size() const noexcept
{
    return is_ready() ? m_managers.first().slot_size() : 0u;
}

template<typename TWord>
std::size_t TBuddyAllocatorChain<TWord>::backing_size() const noexcept
{
    return is_ready() ? m_managers.first().backing_size() : 0u;
}

template<typename TWord>
std::uint64_t TBuddyAllocatorChain<TWord>::allocation_count() const noexcept
{
    std::uint64_t count = 0u;
    for (std::size_t i = 0u; i < manager_count(); ++i) count += m_managers[i].allocation_count();
    return count;
}

template<typename TWord>
std::uint64_t TBuddyAllocatorChain<TWord>::allocated_size() const noexcept
{
    std::uint64_t bytes = 0u;
    for (std::size_t i = 0u; i < manager_count(); ++i) bytes += m_managers[i].allocated_size();
    return bytes;
}

template<typename TWord>
std::size_t TBuddyAllocatorChain<TWord>::largest_available_block() const noexcept
{
    std::size_t largest = 0u;
    for (std::size_t i = 0u; i < manager_count(); ++i)
    {
        largest = std::max(largest, m_managers[i].largest_available_block());
    }
    return largest;
}

template<typename TWord>
memory::SMemoryAttribution TBuddyAllocatorChain<TWord>::memory_attribution() const noexcept
{
    memory::SMemoryAttribution result = m_managers.memory_attribution();
    for (std::size_t i = 0u; i < manager_count(); ++i)
    {
        result = memory::combine_memory_attribution(result, m_managers[i].memory_attribution());
    }
    return result;
}

template<typename TWord>
template<typename TVisitor>
void TBuddyAllocatorChain<TWord>::visit_allocations(TVisitor&& visitor) const
{
    for (std::size_t i = 0u; i < manager_count(); ++i) m_managers[i].visit_allocations(visitor);
}
