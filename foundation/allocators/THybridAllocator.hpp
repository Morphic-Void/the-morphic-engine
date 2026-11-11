
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    THybridAllocator.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Lazy buddy-chain and tracked ownership with size/alignment routing.
//  Exclusive access is required. No vendor policy or requested-size history.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "TBuddyAllocatorChain.hpp"
#include "CTrackedAllocator.hpp"

struct SHybridConfig
{
    SBuddyConfig buddy;
    //  Maximum rounded buddy extent. Zero routes every request to tracked storage.
    std::size_t buddy_threshold{ 0u };
};

template<typename TWord>
class THybridAllocator
{
public:
    enum class EFreeResult { released, no_action, invalid_allocation };
    enum class EAllocationKind { none, buddy, tracked };

    THybridAllocator() noexcept = default;
    THybridAllocator(const THybridAllocator&) = delete;
    THybridAllocator& operator=(const THybridAllocator&) = delete;
    THybridAllocator(THybridAllocator&& other) noexcept;
    THybridAllocator& operator=(THybridAllocator&& other) noexcept;
    ~THybridAllocator() noexcept = default;

    [[nodiscard]] bool initialise(const SHybridConfig& config, memory::CMemoryContext& context) noexcept;
    [[nodiscard]] void* allocate(const std::size_t bytes, const std::size_t alignment = alignof(std::max_align_t)) noexcept;
    [[nodiscard]] EFreeResult free(void* const pointer) noexcept;
    //  Buddy: reserved extent. Tracked: requested bytes. Both are usable extents.
    [[nodiscard]] std::size_t allocation_size(const void* const pointer) const noexcept;
    [[nodiscard]] std::size_t allocation_alignment(const void* const pointer) const noexcept;
    [[nodiscard]] EAllocationKind allocation_kind(const void* const pointer) const noexcept;

    void reset() noexcept;
    void deallocate() noexcept;
    [[nodiscard]] bool is_ready() const noexcept { return m_context != nullptr; }
    [[nodiscard]] bool is_empty() const noexcept { return allocation_count() == 0u; }
    [[nodiscard]] const SHybridConfig& config() const noexcept { return m_config; }
    [[nodiscard]] std::size_t manager_count() const noexcept { return m_buddy.manager_count(); }
    [[nodiscard]] std::size_t tracked_allocation_count() const noexcept { return m_tracked.allocation_count(); }
    [[nodiscard]] std::uint64_t allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t allocated_size() const noexcept;
    [[nodiscard]] std::uint64_t total_owned_size() const noexcept { return memory_attribution().allocation_size; }
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Buddy manager/address order, then tracked address order; not global order.
    //  visitor(pointer, usable extent, guaranteed alignment, kind) must not mutate.
    template<typename TVisitor> void visit_allocations(TVisitor&& visitor) const;

private:
    [[nodiscard]] std::size_t buddy_alignment(const std::size_t extent) const noexcept;

    SHybridConfig m_config{};
    memory::CMemoryContext* m_context{ nullptr };
    TBuddyAllocatorChain<TWord> m_buddy;
    CTrackedAllocator m_tracked;
};

using CHybridAllocator32 = THybridAllocator<std::uint32_t>;
using CHybridAllocator64 = THybridAllocator<std::uint64_t>;

//==============================================================================
//  THybridAllocator out of class function bodies
//==============================================================================

template<typename TWord>
THybridAllocator<TWord>::THybridAllocator(THybridAllocator&& other) noexcept
    : m_config(std::exchange(other.m_config, SHybridConfig{}))
    , m_context(std::exchange(other.m_context, nullptr))
    , m_buddy(std::move(other.m_buddy))
    , m_tracked(std::move(other.m_tracked))
{
}

template<typename TWord>
THybridAllocator<TWord>& THybridAllocator<TWord>::operator=(THybridAllocator&& other) noexcept
{
    if (this == &other) return *this;
    deallocate();
    m_config = std::exchange(other.m_config, SHybridConfig{});
    m_context = std::exchange(other.m_context, nullptr);
    m_buddy = std::move(other.m_buddy);
    m_tracked = std::move(other.m_tracked);
    return *this;
}

template<typename TWord>
bool THybridAllocator<TWord>::initialise(const SHybridConfig& config, memory::CMemoryContext& context) noexcept
{
    SBuddyConfig normalized;
    if (is_ready() || !TBuddyAllocator<TWord>::resolve_config(config.buddy, context, normalized) ||
        (config.buddy_threshold > normalized.backing_size_limit))
    {
        return false;
    }
    m_config = { normalized, config.buddy_threshold };
    m_context = &context;
    return true;
}

template<typename TWord>
void* THybridAllocator<TWord>::allocate(const std::size_t bytes, const std::size_t alignment) noexcept
{
    if (!is_ready() || (bytes == 0u) || (bytes > memory::k_byte_size_ceiling) ||
        (alignment == 0u) || (alignment > memory::k_byte_size_ceiling) || ((alignment & (alignment - 1u)) != 0u))
    {
        return nullptr;
    }
    const std::size_t conditioned_alignment = m_context->condition_alignment(alignment);
    if ((conditioned_alignment < alignment) || (m_context->condition_bytes(conditioned_alignment, bytes) == 0u))
    {
        return nullptr;
    }
    const std::size_t promoted = std::max({ bytes, alignment, m_config.buddy.minimum_slot_size });
    const std::size_t extent = bit_ops::round_up_to_pow2(promoted);
    if ((extent != 0u) && (extent <= m_config.buddy_threshold) &&
        (alignment <= buddy_alignment(extent)))
    {
        if (!m_buddy.is_ready() && !m_buddy.initialise(m_config.buddy, *m_context))
        {
            return nullptr;
        }
        return m_buddy.allocate(extent);
    }
    if (!m_tracked.is_ready() && !m_tracked.initialise(*m_context))
    {
        return nullptr;
    }
    return m_tracked.allocate(bytes, alignment);
}

template<typename TWord>
typename THybridAllocator<TWord>::EFreeResult THybridAllocator<TWord>::free(void* const pointer) noexcept
{
    if (pointer == nullptr) return EFreeResult::no_action;
    switch (m_buddy.free(pointer))
    {
        case ::EFreeResult::released:
        {
            return EFreeResult::released;
        }
        case ::EFreeResult::outside_backing:
        {
            return (m_tracked.free(pointer) == CTrackedAllocator::EFreeResult::released) ?
                EFreeResult::released : EFreeResult::invalid_allocation;
        }
        default:
        {
            return EFreeResult::invalid_allocation;
        }
    }
}

template<typename TWord>
std::size_t THybridAllocator<TWord>::allocation_size(const void* const pointer) const noexcept
{
    const std::size_t extent = m_buddy.allocation_size(pointer);
    return (extent != 0u) ? extent : m_tracked.allocation_size(pointer);
}

template<typename TWord>
std::size_t THybridAllocator<TWord>::buddy_alignment(const std::size_t extent) const noexcept
{
    return std::min(extent, std::max(m_config.buddy.minimum_slot_size,
        TBuddyAllocator<TWord>::k_minimum_backing_alignment));
}

template<typename TWord>
std::size_t THybridAllocator<TWord>::allocation_alignment(const void* const pointer) const noexcept
{
    const std::size_t extent = m_buddy.allocation_size(pointer);
    return (extent != 0u) ? buddy_alignment(extent) : m_tracked.allocation_alignment(pointer);
}

template<typename TWord>
typename THybridAllocator<TWord>::EAllocationKind THybridAllocator<TWord>::allocation_kind(const void* const pointer) const noexcept
{
    if (m_buddy.allocation_size(pointer) != 0u)
    {
        return EAllocationKind::buddy;
    }
    return (m_tracked.allocation_size(pointer) != 0u) ? EAllocationKind::tracked : EAllocationKind::none;
}

template<typename TWord>
void THybridAllocator<TWord>::reset() noexcept
{
    m_buddy.reset();
    m_tracked.reset();
}

template<typename TWord>
void THybridAllocator<TWord>::deallocate() noexcept
{
    m_buddy.deallocate();
    m_tracked.deallocate();
    m_context = nullptr;
    m_config = {};
}

template<typename TWord>
std::uint64_t THybridAllocator<TWord>::allocation_count() const noexcept
{
    return m_buddy.allocation_count() + m_tracked.allocation_count();
}

template<typename TWord>
std::uint64_t THybridAllocator<TWord>::allocated_size() const noexcept
{
    return m_buddy.allocated_size() + m_tracked.allocated_size();
}

template<typename TWord>
memory::SMemoryAttribution THybridAllocator<TWord>::memory_attribution() const noexcept
{
    return memory::combine_memory_attribution(m_buddy.memory_attribution(), m_tracked.memory_attribution());
}

template<typename TWord>
template<typename TVisitor>
void THybridAllocator<TWord>::visit_allocations(TVisitor&& visitor) const
{
    m_buddy.visit_allocations([&](const void* const pointer, const std::size_t extent)
    {
        visitor(pointer, extent, buddy_alignment(extent), EAllocationKind::buddy);
    });
    m_tracked.visit_allocations([&](const void* const pointer, const std::size_t bytes, const std::size_t alignment)
    {
        visitor(pointer, bytes, alignment, EAllocationKind::tracked);
    });
}
