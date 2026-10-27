
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   BumpBuffer.hpp
//  Primary implementation: OpenAI Codex
//  Date:   27 Oct 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Owning bump allocator over fixed-size, address-stable byte blocks.
//  Each allocation is contiguous and never crosses a block boundary.
//  Models raw storage only; callers manage object construction/destruction.

#pragma once

#ifndef BUMP_BUFFER_HPP_INCLUDED
#define BUMP_BUFFER_HPP_INCLUDED

#include <cstddef>      //  std::size_t, std::max_align_t
#include <utility>      //  std::move, std::exchange

#include "memory/memory_token.hpp"
#include "bit_utils/bit_ops.hpp"

//==============================================================================
//  CBumpBuffer
//  allocate() prepares the first block; bump() grows storage as needed.
//  Existing pointers survive growth and moves.
//  reset() invalidates allocations but retains all blocks for reuse.
//  deallocate() releases all blocks; both operations preserve configuration.
//  Storage is not zero-filled. Individual allocations cannot be freed.
//==============================================================================

class CBumpBuffer
{
public:

    //  Default and deleted lifetime
    CBumpBuffer() noexcept = default;
    CBumpBuffer(const CBumpBuffer&) noexcept = delete;
    CBumpBuffer& operator=(const CBumpBuffer&) noexcept = delete;

    //  Move/lifetime
    CBumpBuffer(CBumpBuffer&&) noexcept;
    CBumpBuffer& operator=(CBumpBuffer&&) noexcept;
    ~CBumpBuffer() noexcept = default;

    //  Status: ready means backing storage exists, including after reset.
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return m_allocation_count == 0u; }
    [[nodiscard]] bool is_ready() const noexcept { return is_valid() && m_token.owns_storage(); }

    //  Configuration
    [[nodiscard]] std::size_t align() const noexcept { return m_token.storage_alignment(); }
    [[nodiscard]] std::size_t block_size() const noexcept { return m_token.per_buffer_capacity(); }

    //  Statistics for current bumps; reset clears all three allocation counters.
    //  Aligned size includes leading padding for successful bumps, but not
    //  skipped block tails. Buffer count includes all retained storage blocks,
    //  including unused blocks after reset, and excludes the token directory.
    [[nodiscard]] std::size_t allocation_count() const noexcept { return m_allocation_count; }
    [[nodiscard]] std::size_t allocated_size() const noexcept { return m_allocated_size; }
    [[nodiscard]] std::size_t allocated_size_with_alignment() const noexcept { return m_allocated_size_with_alignment; }
    [[nodiscard]] std::size_t buffer_count() const noexcept { return m_token.is_stable() ? (m_token.count() / block_size()) : std::size_t{ 0u }; }

    //  Buffer allocation: block_size and align must be non-zero powers of two.
    //  Matching settings reset bumps/statistics and retain all backing blocks.
    //  Changed settings release old storage before allocating a fresh block;
    //  allocation failure leaves an empty, configured buffer that is not ready.
    //  Invalid settings leave the existing state unchanged.
    [[nodiscard]] bool allocate(const std::size_t block_size, const std::size_t align = alignof(std::max_align_t)) noexcept;
    void deallocate() noexcept { m_token.deallocate(); reset(); }
    void reset() noexcept;

    //  Bump allocation: requires a successful allocate() first.
    //  align == 0 selects the configured block alignment.
    //  Explicit alignment must be a power of two no greater than align().
    //  Zero/oversized requests and allocation failures return nullptr,
    //  preserving the cursor, retained blocks, and previous allocations.
    [[nodiscard]] void* bump(const std::size_t size, const std::size_t align = 0u) noexcept;

//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe all owned backing storage without changing its attribution.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace all owned contexts, including unallocated members.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

private:

    memory::CMemoryToken m_token;
    std::size_t m_cursor = 0u;
    std::size_t m_allocation_count = 0u;
    std::size_t m_allocated_size = 0u;
    std::size_t m_allocated_size_with_alignment = 0u;
};

//==============================================================================
//  CBumpBuffer out of class function bodies
//==============================================================================

inline CBumpBuffer::CBumpBuffer(CBumpBuffer&& other) noexcept
    : m_token(std::move(other.m_token))
    , m_cursor(std::exchange(other.m_cursor, 0u))
    , m_allocation_count(std::exchange(other.m_allocation_count, 0u))
    , m_allocated_size(std::exchange(other.m_allocated_size, 0u))
    , m_allocated_size_with_alignment(std::exchange(other.m_allocated_size_with_alignment, 0u))
{
}

inline CBumpBuffer& CBumpBuffer::operator=(CBumpBuffer&& other) noexcept
{
    if (this != &other)
    {
        m_token = std::move(other.m_token);
        m_cursor = std::exchange(other.m_cursor, 0u);
        m_allocation_count = std::exchange(other.m_allocation_count, 0u);
        m_allocated_size = std::exchange(other.m_allocated_size, 0u);
        m_allocated_size_with_alignment = std::exchange(other.m_allocated_size_with_alignment, 0u);
    }
    return *this;
}

inline bool CBumpBuffer::is_valid() const noexcept
{
    return (m_token.is_stable() || (!m_token.is_configured() && m_token.is_empty())) &&
        (m_allocation_count <= m_allocated_size) &&
        (m_allocated_size <= m_allocated_size_with_alignment) &&
        (m_allocated_size_with_alignment <= m_cursor) && (m_cursor <= m_token.count());
}

inline bool CBumpBuffer::allocate(const std::size_t block_size, const std::size_t align) noexcept
{
    if (!bit_ops::is_pow2(block_size) || !bit_ops::is_pow2(align))
    {
        return false;
    }
    if ((block_size != this->block_size()) || (align != this->align()))
    {
        memory::CMemoryToken token{ 1u, align, block_size, m_token.context() };
        if (!token.is_stable())
        {
            return false;
        }
        m_token = std::move(token);
    }
    reset();
    return m_token.owns_storage() || m_token.allocate(block_size, false);
}

inline void CBumpBuffer::reset() noexcept
{
    m_cursor = m_allocation_count = m_allocated_size = m_allocated_size_with_alignment = 0u;
}

inline void* CBumpBuffer::bump(const std::size_t size, const std::size_t align) noexcept
{
    const std::size_t block_bytes = block_size();
    const std::size_t alignment = (align != 0u) ? align : this->align();
    if (!is_ready() || (size == 0u) || (size > block_bytes) ||
        !bit_ops::is_pow2(alignment) || (alignment > this->align()))
    {
        return nullptr;
    }

    std::size_t offset = m_cursor & (block_bytes - 1u);
    std::size_t block_start = m_cursor - offset;
    std::size_t padding = (alignment - (offset & (alignment - 1u))) & (alignment - 1u);
    const std::size_t remaining = block_bytes - offset;
    if ((padding > remaining) || (size > (remaining - padding)))
    {
        block_start += block_bytes;
        offset = 0u;
        padding = 0u;
    }
    else
    {
        offset += padding;
    }

    if (block_start > (m_token.max_count() - block_bytes))
    {
        return nullptr;
    }
    const std::size_t required_capacity = block_start + block_bytes;
    if ((required_capacity > m_token.count()) && !m_token.grow_to(required_capacity, false))
    {
        return nullptr;
    }
    void* const result = m_token.index_ptr(block_start + offset);
    m_cursor = block_start + offset + size;
    ++m_allocation_count;
    m_allocated_size += size;
    m_allocated_size_with_alignment += size + padding;
    return result;
}

inline memory::SMemoryAttribution CBumpBuffer::memory_attribution() const noexcept
{
    return memory::observe_memory_attribution(m_token);
}

inline void CBumpBuffer::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_token.unsafe_replace_context_without_accounting(expected_source, target);
}

#endif  //  #ifndef BUMP_BUFFER_HPP_INCLUDED
