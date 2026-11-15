
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CBulkBufferTransport.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    15 Nov 26
//
//  Fixed-capacity, 128-byte-aligned opaque buffers using TBulk slot exchange.
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Defines threading::transports::CBulkBufferTransport and its concrete
//  producer/consumer endpoint wrappers.
//
//  Triple-buffered SPSC latest-state transport for opaque byte buffers.
//
//  The transport owns three fixed-capacity buffers through the memory context
//  captured at construction. Each buffer begins on a 128-byte boundary; views
//  expose exactly the requested capacity, excluding allocation padding.
//
//  Initialise backing before exposing endpoints or using either role. The
//  owner, its memory context and allocator must outlive every endpoint and view.
//  Destroy the transport only after both roles and all dependent users quiesce.
//
//  Publication replaces any staged buffer that the consumer has not acquired.
//  Acquiring again relinquishes the previous consumer view even when no new
//  publication exists. A writable producer view expires upon publication.
//  Copied views neither retain backing nor grant another producer/consumer.
//
//  Allocation occurs only during initialisation. Exchange does not allocate,
//  grow, block, copy payload bytes or overwrite consumer-held data.

#pragma once

#ifndef CBULK_BUFFER_TRANSPORT_HPP_INCLUDED
#define CBULK_BUFFER_TRANSPORT_HPP_INCLUDED

#include <cstddef>  //  std::byte, std::size_t
#include <cstdint>  //  std::uint32_t, std::uint64_t
#include <limits>   //  std::numeric_limits

#include "TBulkTransport.hpp"
#include "memory/memory_token.hpp"

namespace threading::transports
{

//==============================================================================
//  CBulkBufferTransport
//  Single Producer, Single Consumer (SPSC) opaque byte-buffer transport
//==============================================================================

class CBulkBufferTransport
{
public:
    static constexpr std::size_t k_alignment_shift = 7u;
    static constexpr std::size_t k_alignment = std::size_t{ 1u } << k_alignment_shift;
    static constexpr std::size_t k_alignment_mask = k_alignment - 1u;

    //  Owner operations
    //  - construct in the owning memory context
    //  - initialise before either role becomes accessible
    //  - capacity is immutable after successful initialisation
    //  - a failed first initialisation leaves backing uninitialised
    [[nodiscard]] static bool valid_capacity(const std::uint64_t capacity) noexcept;
    [[nodiscard]] bool initialise(const std::uint64_t capacity) noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] memory::CMemoryContext* memory_context() const noexcept;

    //  Producer operations
    //  - only safe to call from the producer thread or while quiescent
    //  - producer_data() returns the currently writable buffer
    //  - publish() expires that writable view and stages the completed buffer
    //  - its return value describes acquisition of the previous publication,
    //    never publication failure
    [[nodiscard]] memory::CMemoryView producer_data() noexcept;
    bool publish() noexcept;

    //  Consumer operations
    //  - only safe to call from the consumer thread or while quiescent
    //  - has_staged_data() observes availability without acquiring or releasing
    //  - discard_and_acquire() always releases any current consumer view
    //  - consumer_data() returns a read-only view, empty if none is acquired
    [[nodiscard]] bool has_staged_data() const noexcept;
    bool discard_and_acquire() noexcept;
    [[nodiscard]] memory::CMemoryConstView consumer_data() noexcept;

private:
    [[nodiscard]] memory::CMemoryView view(const std::uint32_t slot) noexcept;

    //  Owning backing and the immutable identities exchanged by TBulk.
    memory::CMemoryToken m_storage{ k_alignment, k_alignment };
    TBulk<std::uint32_t> m_slots{ 0u, 1u, 2u };

    //  Read-only after initialisation; stride includes alignment padding.
    std::size_t m_capacity{ 0u }, m_stride{ 0u };
};

//==============================================================================
//  CBulkBufferProducerEndpoint
//  Single Producer, Single Consumer (SPSC) producer endpoint
//==============================================================================

class CBulkBufferProducerEndpoint
{
public:
    explicit CBulkBufferProducerEndpoint(CBulkBufferTransport& transport) noexcept;

    [[nodiscard]] memory::CMemoryView producer_data() noexcept;
    bool publish() noexcept;

private:
    CBulkBufferTransport& m_transport;
};

//==============================================================================
//  CBulkBufferConsumerEndpoint
//  Single Producer, Single Consumer (SPSC) consumer endpoint
//==============================================================================

class CBulkBufferConsumerEndpoint
{
public:
    explicit CBulkBufferConsumerEndpoint(CBulkBufferTransport& transport) noexcept;

    [[nodiscard]] memory::CMemoryConstView consumer_data() noexcept;
    [[nodiscard]] bool has_staged_data() const noexcept;
    bool discard_and_acquire() noexcept;

private:
    CBulkBufferTransport& m_transport;
};

//==============================================================================
//  CBulkBufferTransport out of class function bodies
//==============================================================================

inline bool CBulkBufferTransport::valid_capacity(const std::uint64_t capacity) noexcept
{
    constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (!capacity || (capacity > UINT32_MAX) || (capacity > (maximum - k_alignment_mask)))
    {
        return false;
    }
    const std::size_t blocks = (static_cast<std::size_t>(capacity) + k_alignment_mask) >> k_alignment_shift;
    return blocks <= (memory::max_elements(k_alignment) / 3u);
}

inline bool CBulkBufferTransport::initialise(const std::uint64_t capacity) noexcept
{
    if (m_capacity || !valid_capacity(capacity))
    {
        return false;
    }
    const std::size_t blocks = (static_cast<std::size_t>(capacity) + k_alignment_mask) >> k_alignment_shift;
    if (!m_storage.allocate(blocks * 3u)) return false;
    m_stride = blocks << k_alignment_shift;
    m_capacity = static_cast<std::size_t>(capacity);
    return true;
}

inline std::size_t CBulkBufferTransport::capacity() const noexcept
{
    return m_capacity;
}

inline memory::CMemoryContext* CBulkBufferTransport::memory_context() const noexcept
{
    return m_storage.context();
}

inline memory::CMemoryView CBulkBufferTransport::producer_data() noexcept
{
    return view(*m_slots.producer_data());
}

inline bool CBulkBufferTransport::publish() noexcept
{
    return m_slots.publish();
}

inline bool CBulkBufferTransport::has_staged_data() const noexcept
{
    return m_slots.has_staged_data();
}

inline bool CBulkBufferTransport::discard_and_acquire() noexcept
{
    return m_slots.discard_and_acquire();
}

inline memory::CMemoryConstView CBulkBufferTransport::consumer_data() noexcept
{
    const std::uint32_t* const slot = m_slots.consumer_data();
    return slot ? view(*slot).const_view() : memory::CMemoryConstView{};
}

inline memory::CMemoryView CBulkBufferTransport::view(const std::uint32_t slot) noexcept
{
    if (!m_capacity) return {};
    std::byte* const bytes = static_cast<std::byte*>(m_storage.data());
    return { bytes + (slot * m_stride), m_capacity, 1u, k_alignment };
}

//==============================================================================
//  CBulkBufferProducerEndpoint out of class function bodies
//==============================================================================

inline CBulkBufferProducerEndpoint::CBulkBufferProducerEndpoint(CBulkBufferTransport& transport) noexcept
    : m_transport{ transport }
{
}

inline memory::CMemoryView CBulkBufferProducerEndpoint::producer_data() noexcept
{
    return m_transport.producer_data();
}

inline bool CBulkBufferProducerEndpoint::publish() noexcept
{
    return m_transport.publish();
}

//==============================================================================
//  CBulkBufferConsumerEndpoint out of class function bodies
//==============================================================================

inline CBulkBufferConsumerEndpoint::CBulkBufferConsumerEndpoint(CBulkBufferTransport& transport) noexcept
    : m_transport{ transport }
{
}

inline memory::CMemoryConstView CBulkBufferConsumerEndpoint::consumer_data() noexcept
{
    return m_transport.consumer_data();
}

inline bool CBulkBufferConsumerEndpoint::has_staged_data() const noexcept
{
    return m_transport.has_staged_data();
}

inline bool CBulkBufferConsumerEndpoint::discard_and_acquire() noexcept
{
    return m_transport.discard_and_acquire();
}

}   //  namespace threading::transports

#endif  //  #ifndef CBULK_BUFFER_TRANSPORT_HPP_INCLUDED
