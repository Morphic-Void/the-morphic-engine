
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CTrackedAllocator.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    07 Nov 26
//
//  Individually owned allocations indexed by their exact starting addresses.
//  Exclusive access is required. Index growth preserves payload addresses.

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "containers/TOrderedSlots.hpp"
#include "memory/memory_token.hpp"

class CTrackedAllocator
{
private:
    struct SAddressKey
    {
        std::uintptr_t address{ 0u };

        [[nodiscard]] std::int32_t relationship(const SAddressKey& other) const noexcept;
    };

    using TAllocations = TOrderedSlots<memory::CMemoryToken, SAddressKey>;

public:
    enum class EFreeResult
    {
        released,
        no_action,
        invalid_allocation
    };

    CTrackedAllocator() noexcept = default;
    CTrackedAllocator(const CTrackedAllocator&) = delete;
    CTrackedAllocator& operator=(const CTrackedAllocator&) = delete;
    CTrackedAllocator(CTrackedAllocator&&) noexcept = default;
    CTrackedAllocator& operator=(CTrackedAllocator&&) noexcept = default;
    ~CTrackedAllocator() noexcept = default;

    [[nodiscard]] bool initialise(memory::CMemoryContext& context, const std::size_t initial_slot_count = 0u) noexcept;
    //  Alignment must be a nonzero power of two. Bytes are retained exactly.
    [[nodiscard]] void* allocate(const std::size_t bytes, const std::size_t alignment = alignof(std::max_align_t)) noexcept;
    [[nodiscard]] EFreeResult free(void* const pointer) noexcept;
    [[nodiscard]] std::size_t allocation_size(const void* const pointer) const noexcept;
    [[nodiscard]] std::size_t allocation_alignment(const void* const pointer) const noexcept;

    //  Reset releases payloads and retains index storage; deallocate releases all.
    void reset() noexcept;
    void deallocate() noexcept { m_allocations.deallocate(); }

    [[nodiscard]] bool is_ready() const noexcept { return m_allocations.is_ready(); }
    [[nodiscard]] bool is_empty() const noexcept { return m_allocations.is_empty(); }
    [[nodiscard]] std::size_t allocation_count() const noexcept { return m_allocations.occupied_count(); }
    [[nodiscard]] std::uint64_t allocated_size() const noexcept;
    [[nodiscard]] std::uint64_t total_owned_size() const noexcept { return memory_attribution().allocation_size; }
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Increasing address order: visitor(const void*, requested bytes, alignment).
    //  The callback must not mutate this allocator. Tokens are never exposed.
    template<typename TVisitor> void visit_allocations(TVisitor&& visitor) const;

private:
    TAllocations m_allocations;
};

//==============================================================================
//  CTrackedAllocator out of class function bodies
//==============================================================================

inline std::int32_t CTrackedAllocator::SAddressKey::relationship(const SAddressKey& other) const noexcept
{
    return (address < other.address) ? -1 : ((address > other.address) ? 1 : 0);
}

inline bool CTrackedAllocator::initialise(memory::CMemoryContext& context, const std::size_t initial_slot_count) noexcept
{
    if (is_ready() || !context.is_usable())
    {
        return false;
    }
    TAllocations prepared{ context };
    if (!prepared.initialise(initial_slot_count))
    {
        return false;
    }
    m_allocations = std::move(prepared);
    return true;
}

inline void* CTrackedAllocator::allocate(const std::size_t bytes, const std::size_t alignment) noexcept
{
    if (!is_ready() || (bytes == 0u) || (bytes > memory::k_byte_size_ceiling) ||
        (alignment == 0u) || (alignment > memory::k_byte_size_ceiling) || ((alignment & (alignment - 1u)) != 0u))
    {
        return nullptr;
    }
    memory::CMemoryContext* const context = m_allocations.memory_attribution().source;
    memory::CMemoryToken candidate{ 1u, alignment, context };
    if (!candidate.allocate(bytes, false)) return nullptr;
    void* const pointer = candidate.data();
    const SAddressKey key{ reinterpret_cast<std::uintptr_t>(pointer) };
    if (m_allocations.insert(key, std::move(candidate)) < 0)
    {
        return nullptr;
    }
    return pointer;
}

inline CTrackedAllocator::EFreeResult CTrackedAllocator::free(void* const pointer) noexcept
{
    if (pointer == nullptr) return EFreeResult::no_action;
    const SAddressKey key{ reinterpret_cast<std::uintptr_t>(pointer) };
    return m_allocations.erase(key) ? EFreeResult::released : EFreeResult::invalid_allocation;
}

inline std::size_t CTrackedAllocator::allocation_size(const void* const pointer) const noexcept
{
    const SAddressKey key{ reinterpret_cast<std::uintptr_t>(pointer) };
    const memory::CMemoryToken* const token = m_allocations.get_slot(key);
    return (token != nullptr) ? token->bytes() : 0u;
}

inline std::size_t CTrackedAllocator::allocation_alignment(const void* const pointer) const noexcept
{
    const SAddressKey key{ reinterpret_cast<std::uintptr_t>(pointer) };
    const memory::CMemoryToken* const token = m_allocations.get_slot(key);
    return (token != nullptr) ? token->storage_alignment() : 0u;
}

inline void CTrackedAllocator::reset() noexcept
{
    std::int32_t slot = m_allocations.first_live();
    while (slot >= 0)
    {
        const std::int32_t next = m_allocations.next_live(slot);
        (void)m_allocations.erase(slot);
        slot = next;
    }
}

inline std::uint64_t CTrackedAllocator::allocated_size() const noexcept
{
    std::uint64_t bytes = 0u;
    for (std::int32_t slot = m_allocations.first_live(); slot >= 0; slot = m_allocations.next_live(slot))
    {
        bytes += m_allocations.get_slot(slot)->bytes();
    }
    return bytes;
}

inline memory::SMemoryAttribution CTrackedAllocator::memory_attribution() const noexcept
{
    memory::SMemoryAttribution result = m_allocations.memory_attribution();
    for (std::int32_t slot = m_allocations.first_live(); slot >= 0; slot = m_allocations.next_live(slot))
    {
        result = memory::combine_memory_attribution(result, memory::observe_memory_attribution(*m_allocations.get_slot(slot)));
    }
    return result;
}

template<typename TVisitor>
void CTrackedAllocator::visit_allocations(TVisitor&& visitor) const
{
    for (std::int32_t slot = m_allocations.first_live(); slot >= 0; slot = m_allocations.next_live(slot))
    {
        const memory::CMemoryToken& token = *m_allocations.get_slot(slot);
        visitor(token.data(), token.bytes(), token.storage_alignment());
    }
}
