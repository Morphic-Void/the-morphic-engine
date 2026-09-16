
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   memory_context.hpp
//  Author: Ritchie Brannan
//  Date:   12 Jul 26
//
//  Allocation routing, attribution accounting, and ambient memory context.

#pragma once

#ifndef MEMORY_CONTEXT_HPP_INCLUDED
#define MEMORY_CONTEXT_HPP_INCLUDED

#include <atomic>       //  std::atomic
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t, std::uint64_t, std::uintptr_t
#include <limits>       //  std::numeric_limits

#include "memory_policies.hpp"
#include "bit_utils/bit_ops.hpp"
#include "system/system_context.hpp"
#include "platform/platform_defines.hpp"
#include "debug/macros.hpp"

namespace memory
{

class CMemoryContext;

//==============================================================================
//  Ambient memory context
//==============================================================================

[[nodiscard]] CMemoryContext* get_ambient_memory_context() noexcept;
CMemoryContext* set_module_memory_context(CMemoryContext* const context = nullptr) noexcept;
CMemoryContext* set_thread_memory_context(CMemoryContext* const context = nullptr) noexcept;

//==============================================================================
//  Allocator callback interface
//==============================================================================

using FAllocate = void* (MV_STD_ABI_CALL*)(void* const context, const std::size_t align, const std::size_t bytes) noexcept;
using FDeallocate = bool (MV_STD_ABI_CALL*)(void* const context, const std::size_t align, void* const ptr) noexcept;

//==============================================================================
//  CMemoryAllocator
//==============================================================================

class CMemoryAllocator
{
public:
    CMemoryAllocator(void* const context, const FAllocate allocate, const FDeallocate deallocate,
        const system_ids::id_type system_id = system_context::get_ambient_system_id()) noexcept;

    CMemoryAllocator(const CMemoryAllocator&) = delete;
    CMemoryAllocator& operator=(const CMemoryAllocator&) = delete;
    CMemoryAllocator(CMemoryAllocator&&) = delete;
    CMemoryAllocator& operator=(CMemoryAllocator&&) = delete;
    ~CMemoryAllocator() noexcept = default;

    [[nodiscard]] bool is_usable() const noexcept { return (m_allocate != nullptr) && (m_deallocate != nullptr); }
    [[nodiscard]] system_ids::id_type get_system_id() const noexcept { return m_system_id; }

private:
    [[nodiscard]] void* allocate(const std::size_t conditioned_alignment, const std::size_t conditioned_bytes) noexcept;
    [[nodiscard]] bool deallocate(const std::size_t conditioned_alignment, void* const ptr) noexcept;

    friend class CMemoryContext;

    system_ids::id_type m_system_id{};
    void*       m_context{ nullptr };
    FAllocate   m_allocate{ nullptr };
    FDeallocate m_deallocate{ nullptr };
};

//==============================================================================
//  CMemoryContext
//==============================================================================

class CMemoryContext
{
public:
    explicit CMemoryContext(CMemoryAllocator& allocator,
        const system_ids::id_type system_id = system_context::get_ambient_system_id()) noexcept;

    CMemoryContext(const CMemoryContext&) = delete;
    CMemoryContext& operator=(const CMemoryContext&) = delete;
    CMemoryContext(CMemoryContext&&) = delete;
    CMemoryContext& operator=(CMemoryContext&&) = delete;
    ~CMemoryContext() noexcept;

    [[nodiscard]] bool is_usable() const noexcept { return m_allocator.is_usable(); }
    [[nodiscard]] bool is_compatible_with(const CMemoryContext& other) const noexcept { return &m_allocator == &other.m_allocator; }
    [[nodiscard]] bool belongs_to_module(const module_ids::id_type module_id) const noexcept;
    [[nodiscard]] bool is_attribution_empty() const noexcept;
    [[nodiscard]] const CMemoryAllocator& get_allocator() const noexcept { return m_allocator; }
    [[nodiscard]] system_ids::id_type get_system_id() const noexcept { return m_system_id; }

    [[nodiscard]] std::uint32_t get_live_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t get_live_allocated_bytes() const noexcept;
    [[nodiscard]] std::size_t condition_alignment(const std::size_t requested_alignment) const noexcept;
    [[nodiscard]] std::size_t condition_bytes(const std::size_t conditioned_alignment, const std::size_t requested_bytes) const noexcept;

    [[nodiscard]] void* allocate(const std::size_t requested_alignment, const std::size_t requested_bytes) noexcept;
    void deallocate(const std::size_t requested_alignment, const std::size_t requested_bytes, void* const ptr) noexcept;

private:
    [[nodiscard]] static bool validate(const std::size_t align, const std::size_t bytes) noexcept;
    [[nodiscard]] static bool validate(const std::size_t align, const std::size_t bytes, const void* const ptr) noexcept;
    void add(const std::uint32_t allocation_count, const std::uint64_t bytes) noexcept;
    void sub(const std::uint32_t allocation_count, const std::uint64_t bytes) noexcept;

    friend struct SMemoryContextTestAccess;

    friend bool reattribute(CMemoryContext& from, CMemoryContext& to, const std::size_t allocation_count, const std::uint64_t bytes) noexcept;

    CMemoryAllocator&          m_allocator;
    system_ids::id_type        m_system_id{};
    std::atomic<std::uint32_t> m_live_allocations{ 0u };
    std::atomic<std::uint64_t> m_live_allocated_bytes{ 0u };
};

//  Diagnostic totals are modular, independently including zero. Only allocator
//  incompatibility rejects this adjustment; callers establish ownership validity.
[[nodiscard]] bool reattribute(CMemoryContext& from, CMemoryContext& to, const std::size_t allocation_count, const std::uint64_t bytes) noexcept;

//  Check every aggregate sum before a narrowed child total reaches its parent.
//  Overflow is diagnostic: retain the unsigned modulo result without rejection.
[[nodiscard]] inline std::uint32_t add_accounting_counts(const std::uint32_t left, const std::uint32_t right) noexcept
{
    const std::uint32_t total = left + right;
    if (right > (std::numeric_limits<std::uint32_t>::max() - left))
    {
        MV_ERROR("Memory accounting count sum overflow: left {} right {} total {}", left, right, total);
    }
    return total;
}

[[nodiscard]] inline std::uint64_t add_accounting_bytes(const std::uint64_t left, const std::uint64_t right) noexcept
{
    const std::uint64_t total = left + right;
    if (right > (std::numeric_limits<std::uint64_t>::max() - left))
    {
        MV_ERROR("Memory accounting bytes sum overflow: left {} right {} total {}", left, right, total);
    }
    return total;
}

//==============================================================================
//  Container attribution
//==============================================================================

//  Source state is structural and independent of the diagnostic modulo totals.
enum class EMemorySourceState
{
    empty = 0,
    coherent,
    mixed
};

struct SMemoryAttribution
{
    EMemorySourceState source_state{ EMemorySourceState::empty };
    CMemoryContext* source{ nullptr };
    std::uint32_t token_count{ 0u };
    std::uint32_t allocation_count{ 0u };
    std::uint64_t allocation_size{ 0u };
};

[[nodiscard]] inline SMemoryAttribution combine_memory_attribution(const SMemoryAttribution& left, const SMemoryAttribution& right) noexcept
{
    SMemoryAttribution result;
    result.token_count = left.token_count + right.token_count;
    result.allocation_count = add_accounting_counts(left.allocation_count, right.allocation_count);
    result.allocation_size = add_accounting_bytes(left.allocation_size, right.allocation_size);

    if ((left.source_state == EMemorySourceState::mixed) ||
        (right.source_state == EMemorySourceState::mixed) ||
        ((left.source_state == EMemorySourceState::coherent) &&
            (right.source_state == EMemorySourceState::coherent) && (left.source != right.source)))
    {
        result.source_state = EMemorySourceState::mixed;
    }
    else if (left.source_state == EMemorySourceState::coherent)
    {
        result.source_state = EMemorySourceState::coherent;
        result.source = left.source;
    }
    else if (right.source_state == EMemorySourceState::coherent)
    {
        result.source_state = EMemorySourceState::coherent;
        result.source = right.source;
    }
    return result;
}

[[nodiscard]] inline bool attribution_is_compatible(const SMemoryAttribution& attribution, const CMemoryContext& target) noexcept
{
    switch (attribution.source_state)
    {
        case EMemorySourceState::empty:
        {
            return attribution.source == nullptr;
        }
        case EMemorySourceState::coherent:
        {
            return (attribution.source != nullptr) && attribution.source->is_compatible_with(target);
        }
        default:
        {
            return false;
        }
    }
}

//  Queries are observations, not reservations. Keep exclusive access through
//  preflight and replacement; reattribute always performs its own fresh query.
template<typename TOwner>
[[nodiscard]] inline bool can_reattribute_to(const TOwner& owner, CMemoryContext* target = nullptr) noexcept
{
    target = (target != nullptr) ? target : get_ambient_memory_context();
    return (target != nullptr) && attribution_is_compatible(owner.memory_attribution(), *target);
}

template<typename TOwner>
[[nodiscard]] inline bool reattribute(TOwner& owner, CMemoryContext* target = nullptr) noexcept
{
    target = (target != nullptr) ? target : get_ambient_memory_context();
    if (target == nullptr)
    {
        return false;
    }
    const SMemoryAttribution attribution = owner.memory_attribution();
    if (!attribution_is_compatible(attribution, *target))
    {
        return false;
    }
    if ((attribution.source_state == EMemorySourceState::coherent) && (attribution.source != target) &&
        !memory::reattribute(*attribution.source, *target, attribution.allocation_count, attribution.allocation_size))
    {
        return false;
    }
    owner.unsafe_replace_memory_context_without_accounting(attribution.source, target);
    return true;
}

//==============================================================================
//  CMemoryAllocator implementation
//==============================================================================

inline CMemoryAllocator::CMemoryAllocator(
    void* const context,
    const FAllocate allocate,
    const FDeallocate deallocate,
    const system_ids::id_type system_id) noexcept
    : m_system_id(system_id)
    , m_context(context)
    , m_allocate(allocate)
    , m_deallocate(deallocate)
{
}

inline void* CMemoryAllocator::allocate(
    const std::size_t conditioned_alignment,
    const std::size_t conditioned_bytes) noexcept
{
    if ((m_allocate == nullptr) ||
        (memory::condition_alignment(conditioned_alignment) != conditioned_alignment) ||
        (memory::condition_bytes(conditioned_alignment, conditioned_bytes) != conditioned_bytes))
    {
        MV_ERROR("CMemoryAllocator::allocate received an invalid conditioned allocation request");
        return nullptr;
    }

    return m_allocate(m_context, conditioned_alignment, conditioned_bytes);
}

inline bool CMemoryAllocator::deallocate(const std::size_t conditioned_alignment, void* const ptr) noexcept
{
    if ((m_deallocate == nullptr) ||
        (memory::condition_alignment(conditioned_alignment) != conditioned_alignment) ||
        (ptr == nullptr))
    {
        MV_ERROR("CMemoryAllocator::deallocate received an invalid conditioned deallocation request");
        return false;
    }

    if (!m_deallocate(m_context, conditioned_alignment, ptr))
    {
        MV_ERROR("CMemoryAllocator::deallocate failed");
        return false;
    }
    return true;
}

//==============================================================================
//  CMemoryContext implementation
//==============================================================================

inline CMemoryContext::CMemoryContext(CMemoryAllocator& allocator, const system_ids::id_type system_id) noexcept
    : m_allocator(allocator)
    , m_system_id(system_id)
{
}

inline CMemoryContext::~CMemoryContext() noexcept
{
    if (!is_attribution_empty())
    {
        MV_ERROR("CMemoryContext was destroyed with live allocations still recorded");
    }
}

inline std::uint32_t CMemoryContext::get_live_allocation_count() const noexcept
{
    return m_live_allocations.load(std::memory_order_relaxed);
}

inline std::uint64_t CMemoryContext::get_live_allocated_bytes() const noexcept
{
    return m_live_allocated_bytes.load(std::memory_order_relaxed);
}

inline bool CMemoryContext::belongs_to_module(const module_ids::id_type module_id) const noexcept
{
    return module_ids::ops::is_valid_id(module_id) && system_ids::ops::is_valid_id(m_system_id) &&
        (system_ids::ops::get_module_id(m_system_id) == module_id);
}

inline bool CMemoryContext::is_attribution_empty() const noexcept
{
    return (get_live_allocation_count() == 0u) && (get_live_allocated_bytes() == 0u);
}

inline std::size_t CMemoryContext::condition_alignment(const std::size_t requested_alignment) const noexcept
{
    return memory::condition_alignment(requested_alignment);
}

inline std::size_t CMemoryContext::condition_bytes(
    const std::size_t conditioned_alignment,
    const std::size_t requested_bytes) const noexcept
{
    return memory::condition_bytes(conditioned_alignment, requested_bytes);
}

inline bool CMemoryContext::validate(const std::size_t align, const std::size_t bytes) noexcept
{
    return bit_ops::is_pow2(align) && memory::in_non_empty_range(bytes, memory::k_byte_size_ceiling);
}

inline bool CMemoryContext::validate(const std::size_t align, const std::size_t bytes, const void* const ptr) noexcept
{
    return validate(align, bytes) && (ptr != nullptr) && ((reinterpret_cast<std::uintptr_t>(ptr) & (align - 1u)) == 0u);
}

inline void CMemoryContext::add(const std::uint32_t allocation_count, const std::uint64_t bytes) noexcept
{
    const std::uint32_t count_before = m_live_allocations.fetch_add(allocation_count, std::memory_order_relaxed);
    const std::uint64_t bytes_before = m_live_allocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
    const std::uint32_t count_after = count_before + allocation_count;
    const std::uint64_t bytes_after = bytes_before + bytes;
    if (((count_before ^ count_after) & 0x80000000u) != 0u)
    {
        MV_ERROR("Memory accounting count add: context {} system {} before {} after {} adjustment {}",
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)), m_system_id, count_before, count_after, allocation_count);
    }
    if (((bytes_before ^ bytes_after) & 0x8000000000000000ull) != 0u)
    {
        MV_ERROR("Memory accounting bytes add: context {} system {} before {} after {} adjustment {}",
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)), m_system_id, bytes_before, bytes_after, bytes);
    }
}

inline void CMemoryContext::sub(const std::uint32_t allocation_count, const std::uint64_t bytes) noexcept
{
    const std::uint32_t count_before = m_live_allocations.fetch_sub(allocation_count, std::memory_order_relaxed);
    const std::uint64_t bytes_before = m_live_allocated_bytes.fetch_sub(bytes, std::memory_order_relaxed);
    const std::uint32_t count_after = count_before - allocation_count;
    const std::uint64_t bytes_after = bytes_before - bytes;
    if (((count_before ^ count_after) & 0x80000000u) != 0u)
    {
        MV_ERROR("Memory accounting count sub: context {} system {} before {} after {} adjustment {}",
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)), m_system_id, count_before, count_after, allocation_count);
    }
    if (((bytes_before ^ bytes_after) & 0x8000000000000000ull) != 0u)
    {
        MV_ERROR("Memory accounting bytes sub: context {} system {} before {} after {} adjustment {}",
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)), m_system_id, bytes_before, bytes_after, bytes);
    }
}

inline void* CMemoryContext::allocate(
    const std::size_t requested_alignment,
    const std::size_t requested_bytes) noexcept
{
    const std::size_t conditioned_alignment = condition_alignment(requested_alignment);
    const std::size_t conditioned_bytes = condition_bytes(conditioned_alignment, requested_bytes);
    if (!validate(conditioned_alignment, conditioned_bytes))
    {
        MV_ERROR("CMemoryContext::allocate received an invalid allocation request");
        return nullptr;
    }

    add(1u, conditioned_bytes);

    void* const ptr = m_allocator.allocate(conditioned_alignment, conditioned_bytes);
    if (ptr == nullptr)
    {
        sub(1u, conditioned_bytes);
        return nullptr;
    }

    if ((reinterpret_cast<std::uintptr_t>(ptr) & (conditioned_alignment - 1u)) != 0u)
    {
        MV_ERROR("CMemoryContext::allocate returned a misaligned pointer");
        if (m_allocator.deallocate(conditioned_alignment, ptr))
        {
            sub(1u, conditioned_bytes);
        }
        return nullptr;
    }
    return ptr;
}

inline void CMemoryContext::deallocate(
    const std::size_t requested_alignment,
    const std::size_t requested_bytes,
    void* const ptr) noexcept
{
    const std::size_t conditioned_alignment = condition_alignment(requested_alignment);
    const std::size_t conditioned_bytes = condition_bytes(conditioned_alignment, requested_bytes);
    if (!validate(conditioned_alignment, conditioned_bytes, ptr))
    {
        MV_ERROR("CMemoryContext::deallocate received an invalid deallocation request");
        return;
    }

    sub(1u, conditioned_bytes);
    if (!m_allocator.deallocate(conditioned_alignment, ptr))
    {
        add(1u, conditioned_bytes);
    }
}

inline bool reattribute(
    CMemoryContext& from,
    CMemoryContext& to,
    const std::size_t allocation_count,
    const std::uint64_t bytes) noexcept
{
    if (&from == &to)
    {
        return true;
    }
    if (!from.is_compatible_with(to))
    {
        MV_ERROR("memory::reattribute received an invalid attribution transfer request");
        return false;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(allocation_count);
    if (allocation_count > std::numeric_limits<std::uint32_t>::max())
    {
        MV_ERROR("Memory accounting count narrowed: source {} target {} original {} adjustment {}",
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&from)),
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&to)),
            static_cast<std::uint64_t>(allocation_count), count);
    }
    to.add(count, bytes);
    from.sub(count, bytes);
    return true;
}

}   //  namespace memory

#endif  //  #ifndef MEMORY_CONTEXT_HPP_INCLUDED
