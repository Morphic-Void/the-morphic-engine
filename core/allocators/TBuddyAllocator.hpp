
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TBuddyAllocator.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    07 Nov 26
//
//  Compact movable owner of separate packed buddy management and payload storage.
//  Exclusive access is required. See docs/memory/buddy_allocator_design.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "buddy_index.hpp"
#include "memory/memory_token.hpp"

struct SBuddyConfig
{
    std::size_t minimum_slot_size{ 0u };
    std::size_t backing_size_limit{ 0u };
};

enum class EFreeResult
{
    released,
    no_action,
    outside_backing,
    invalid_allocation
};

template<typename TWord>
class TBuddyAllocator
{
private:
    using TIndex = buddy_detail::TPackedIndex<TWord>;

    struct SManagement
    {
        std::size_t payload_offset{ 0u };
        std::uint32_t slot_log2{ 0u };
        TIndex index;
    };

public:
    static constexpr std::uint32_t k_max_order = TIndex::k_max_order;
    static constexpr std::uint32_t k_max_slots = TIndex::k_slot_count;
    static constexpr std::size_t k_management_size = sizeof(SManagement);
    static constexpr std::size_t k_minimum_backing_alignment = 128u;

    TBuddyAllocator() noexcept = default;
    TBuddyAllocator(const TBuddyAllocator&) = delete;
    TBuddyAllocator& operator=(const TBuddyAllocator&) = delete;
    TBuddyAllocator(TBuddyAllocator&&) noexcept = default;
    TBuddyAllocator& operator=(TBuddyAllocator&&) noexcept = default;
    ~TBuddyAllocator() noexcept = default;

    [[nodiscard]] bool initialise(const SBuddyConfig& config, memory::CMemoryContext& context) noexcept;
    [[nodiscard]] void* allocate(const std::size_t bytes) noexcept;
    [[nodiscard]] EFreeResult free(void* const pointer) noexcept;
    [[nodiscard]] std::size_t allocation_size(const void* const pointer) const noexcept;

    void reset() noexcept;
    void deallocate() noexcept { m_token.deallocate(); }

    [[nodiscard]] bool is_ready() const noexcept { return m_token.owns_storage(); }
    [[nodiscard]] bool is_empty() const noexcept { return allocation_count() == 0u; }
    [[nodiscard]] std::size_t slot_size() const noexcept;
    [[nodiscard]] std::size_t backing_size() const noexcept;
    [[nodiscard]] std::size_t allocation_count() const noexcept;
    [[nodiscard]] std::size_t allocated_size() const noexcept;
    [[nodiscard]] std::size_t largest_available_block() const noexcept;
    [[nodiscard]] std::uint64_t total_owned_size() const noexcept { return m_token.memory_allocation_size(); }
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::observe_memory_attribution(m_token);
    }

    //  Visits live starts and rounded extents in address order. The callback must
    //  not mutate this allocator. No requested-size history is retained.
    template<typename TVisitor> void visit_allocations(TVisitor&& visitor) const;

private:
    [[nodiscard]] SManagement* management() noexcept { return static_cast<SManagement*>(m_token.data()); }
    [[nodiscard]] const SManagement* management() const noexcept { return static_cast<const SManagement*>(m_token.data()); }
    [[nodiscard]] std::uint8_t* payload() noexcept;
    [[nodiscard]] const std::uint8_t* payload() const noexcept;
    [[nodiscard]] bool contains(const void* const pointer) const noexcept;
    [[nodiscard]] std::int32_t find_order(const void* const pointer) const noexcept;
    template<typename TVisitor> void visit_node(const std::uint32_t order, const std::uint32_t node, TVisitor& visitor) const;

    memory::CMemoryToken m_token;
};

using CBuddyAllocator32 = TBuddyAllocator<std::uint32_t>;
using CBuddyAllocator64 = TBuddyAllocator<std::uint64_t>;

static_assert((sizeof(CBuddyAllocator32) == sizeof(memory::CMemoryToken)));
static_assert((sizeof(CBuddyAllocator64) == sizeof(memory::CMemoryToken)));

template<typename TWord>
bool TBuddyAllocator<TWord>::initialise(const SBuddyConfig& config, memory::CMemoryContext& context) noexcept
{
    if (is_ready() || !context.is_usable() || (config.minimum_slot_size == 0u) || (config.backing_size_limit == 0u))
    {
        return false;
    }
    const std::size_t slot = bit_ops::round_up_to_pow2(config.minimum_slot_size);
    const std::size_t backing = bit_ops::hi_bit_mask(config.backing_size_limit);
    if ((slot == 0u) || (slot > backing) || ((backing / slot) > k_max_slots) ||
        (backing >= memory::k_byte_size_ceiling))
    {
        return false;
    }
    const std::size_t payload_alignment = std::max(slot, k_minimum_backing_alignment);
    //  Both operands are below the substrate ceiling, so addition is safe even
    //  on 32-bit hosts. Validate conditioning too, before calling the allocator.
    const std::size_t offset = (sizeof(SManagement) + payload_alignment - 1u) & ~(payload_alignment - 1u);
    if (offset > (memory::k_byte_size_ceiling - backing))
    {
        return false;
    }
    const std::size_t alignment = std::max(payload_alignment, alignof(SManagement));
    const std::size_t total = offset + backing;
    const std::size_t conditioned_alignment = context.condition_alignment(alignment);
    if ((conditioned_alignment < alignment) || (context.condition_bytes(conditioned_alignment, total) == 0u))
    {
        return false;
    }
    memory::CMemoryToken candidate{ 1u, alignment, &context };
    if (!candidate.allocate(total, false)) return false;
    SManagement* const state = ::new (candidate.data()) SManagement{};
    state->payload_offset = offset;
    state->slot_log2 = static_cast<std::uint32_t>(bit_ops::hi_bit_index(slot));
    state->index.reset(static_cast<std::uint32_t>(bit_ops::hi_bit_index(backing / slot)));
    m_token = std::move(candidate);
    return true;
}

template<typename TWord>
std::uint8_t* TBuddyAllocator<TWord>::payload() noexcept
{
    return static_cast<std::uint8_t*>(m_token.data()) + management()->payload_offset;
}

template<typename TWord>
const std::uint8_t* TBuddyAllocator<TWord>::payload() const noexcept
{
    return static_cast<const std::uint8_t*>(m_token.data()) + management()->payload_offset;
}

template<typename TWord>
std::size_t TBuddyAllocator<TWord>::slot_size() const noexcept
{
    return is_ready() ? (std::size_t{ 1u } << management()->slot_log2) : 0u;
}

template<typename TWord>
std::size_t TBuddyAllocator<TWord>::backing_size() const noexcept
{
    return is_ready() ? (slot_size() << management()->index.active_order) : 0u;
}

template<typename TWord>
std::size_t TBuddyAllocator<TWord>::allocation_count() const noexcept
{
    return is_ready() ? management()->index.live_count : 0u;
}

template<typename TWord>
std::size_t TBuddyAllocator<TWord>::allocated_size() const noexcept
{
    return is_ready() ? (static_cast<std::size_t>(management()->index.live_slots) << management()->slot_log2) : 0u;
}

template<typename TWord>
std::size_t TBuddyAllocator<TWord>::largest_available_block() const noexcept
{
    if (!is_ready() || (management()->index.available == 0u))
    {
        return 0u;
    }
    return slot_size() << bit_ops::hi_bit_index(management()->index.available);
}

template<typename TWord>
void* TBuddyAllocator<TWord>::allocate(const std::size_t bytes) noexcept
{
    if (!is_ready() || (bytes == 0u) || (bytes > backing_size()))
    {
        return nullptr;
    }
    const std::uint32_t slots_minus_one = static_cast<std::uint32_t>((bytes - 1u) >> management()->slot_log2);
    const std::uint32_t order = bit_ops::bit_width(slots_minus_one);
    const std::uint32_t slot = management()->index.allocate(order);
    return (slot == TIndex::k_no_slot) ? nullptr : (payload() + (static_cast<std::size_t>(slot) << management()->slot_log2));
}

template<typename TWord>
bool TBuddyAllocator<TWord>::contains(const void* const pointer) const noexcept
{
    if (!is_ready()) return false;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(payload());
    return (address >= base) && ((address - base) < backing_size());
}

template<typename TWord>
std::int32_t TBuddyAllocator<TWord>::find_order(const void* const pointer) const noexcept
{
    if (!contains(pointer)) return -1;
    const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(pointer) - reinterpret_cast<std::uintptr_t>(payload());
    if ((offset & (slot_size() - 1u)) != 0u) return -1;
    return management()->index.allocation_order(static_cast<std::uint32_t>(offset >> management()->slot_log2));
}

template<typename TWord>
EFreeResult TBuddyAllocator<TWord>::free(void* const pointer) noexcept
{
    if (pointer == nullptr) return EFreeResult::no_action;
    if (!contains(pointer))
    {
        return EFreeResult::outside_backing;
    }
    const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(pointer) - reinterpret_cast<std::uintptr_t>(payload());
    if ((offset & (slot_size() - 1u)) != 0u)
    {
        return EFreeResult::invalid_allocation;
    }
    const std::int32_t order = management()->index.release(static_cast<std::uint32_t>(offset >> management()->slot_log2));
    return (order < 0) ? EFreeResult::invalid_allocation : EFreeResult::released;
}

template<typename TWord>
std::size_t TBuddyAllocator<TWord>::allocation_size(const void* const pointer) const noexcept
{
    const std::int32_t order = find_order(pointer);
    return (order < 0) ? 0u : (slot_size() << order);
}

template<typename TWord>
void TBuddyAllocator<TWord>::reset() noexcept
{
    if (!is_ready()) return;
    management()->index.reset(management()->index.active_order);
}

template<typename TWord>
template<typename TVisitor>
void TBuddyAllocator<TWord>::visit_allocations(TVisitor&& visitor) const
{
    if (!is_ready()) return;
    visit_node(management()->index.active_order, 0u, visitor);
}

template<typename TWord>
template<typename TVisitor>
void TBuddyAllocator<TWord>::visit_node(const std::uint32_t order, const std::uint32_t node, TVisitor& visitor) const
{
    const TIndex& index = management()->index;
    if (!index.reserved(order, node)) return;
    if ((order != 0u) && (index.reserved((order - 1u), (node * 2u)) || index.reserved((order - 1u), ((node * 2u) + 1u))))
    {
        visit_node((order - 1u), (node * 2u), visitor);
        visit_node((order - 1u), ((node * 2u) + 1u), visitor);
    }
    else
    {
        const std::size_t bytes = slot_size() << order;
        visitor((payload() + (static_cast<std::size_t>(node) * bytes)), bytes);
    }
}
