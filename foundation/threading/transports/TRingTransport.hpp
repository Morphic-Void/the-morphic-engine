
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TRingTransport.hpp
//  Author: Ritchie Brannan
//  Date:   15 Apr 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Fixed-capacity SPSC ring transport for trivially copyable T.
//
//  Defines threading::transports::TRing<T>.
//  Also defines the concrete producer/consumer endpoint wrappers and the
//  simple one-transport bundle composition for TRing.
// 
//  Does not grow, discard, overwrite unread data, or provide
//  blocking semantics.
//
//  IMPORTANT SEMANTIC NOTE
//  -----------------------
//  post() and read() are all-or-nothing.
//
//  writable_count() and readable_count() are snapshot observations,
//  not reservation mechanisms.
//
//  Capacity is conditioned at initialise() time to a power-of-two
//  internal capacity with a minimum floor.
//
//  See docs/threading/transports/TRingTransport.md for the full documentation.

#pragma once

#ifndef TRING_TRANSPORT_HPP_INCLUDED
#define TRING_TRANSPORT_HPP_INCLUDED

#include <algorithm>    //  std::min, std::max
#include <atomic>       //  std::atomic
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t
#include <cstring>      //  std::memcpy
#include <type_traits>  //  std::is_const_v, std::is_trivially_copyable_v

#include "containers/TPodVector.hpp"
#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"
#include "bit_utils/bit_ops.hpp"

namespace threading::transports
{

//==============================================================================
//  TRing<T>
//  Single Producer, Single Consumer (SPSC) transport
//==============================================================================

template<typename T>
class TRing
{
private:
    static_assert(!std::is_const_v<T>, "TRing<T> requires non-const T.");
    static_assert(std::is_trivially_copyable_v<T>, "TRing<T> requires trivially copyable T.");

public:
    static constexpr std::uint32_t k_max_capacity = 0x00100000u;    //  approximately 1 million elements
    static constexpr std::uint32_t k_min_capacity = 32u;
    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();

public:
    TRing() noexcept = default;
    explicit TRing(memory::CMemoryContext* const context) noexcept : m_ring{ k_element_size, k_align, context } {}
    TRing(const TRing&) = delete;
    TRing& operator=(const TRing&) = delete;
    TRing(TRing&&) noexcept = delete;
    TRing& operator=(TRing&&) noexcept = delete;
    ~TRing() noexcept { deallocate(); }

    //  Shared status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Producer status and operations
    //  - only safe to call from the producer thread or while quiescent
    [[nodiscard]] bool posting_is_valid() const noexcept;
    [[nodiscard]] bool post(const T& src) noexcept { return post(&src, 1u); }
    [[nodiscard]] bool post(const T* const src, const std::uint32_t count = 1u) noexcept;
    [[nodiscard]] bool post(const TPodConstView<T>& src) noexcept { return src.is_valid() && post(src.data(), static_cast<std::uint32_t>(src.size())); }
    [[nodiscard]] std::uint32_t writable_count() const noexcept;

    //  Consumer status and operations
    //  - only safe to call from the consumer thread or while quiescent
    [[nodiscard]] bool reading_is_valid() const noexcept;
    [[nodiscard]] bool read(T& dst) noexcept { return read(&dst, 1u); }
    [[nodiscard]] bool read(T* const dst, const std::uint32_t count = 1u) noexcept;
    [[nodiscard]] bool read(const TPodView<T>& dst) noexcept { return dst.is_valid() && read(dst.data(), static_cast<std::uint32_t>(dst.size())); }
    [[nodiscard]] std::uint32_t readable_count() const noexcept;

    //  Setup and teardown
    //  initialise() requires a deallocated / not-ready instance.
    //  deallocate() releases owned storage and must not race active role use.
    [[nodiscard]] bool initialise(const std::uint32_t capacity) noexcept;
    [[nodiscard]] bool initialise(const std::uint32_t capacity, memory::CMemoryContext* const context) noexcept;
    void deallocate() noexcept;

    //  Memory context accessor
    [[nodiscard]] memory::CMemoryContext* memory_context() const noexcept { return m_ring.context(); }

private:
    [[nodiscard]] T* raw_data() noexcept { return static_cast<T*>(m_ring.data()); }
    [[nodiscard]] const T* raw_data() const noexcept { return static_cast<const T*>(m_ring.data()); }

    static_assert((k_element_size <= 0xffffu), "TRing<T> element size exceeds the memory token stride field.");

    memory::CMemoryToken m_ring{ k_element_size, k_align };
    std::uint32_t m_capacity = 0u;
    std::uint32_t m_read_index = 0u;
    std::uint32_t m_write_index = 0u;
    std::atomic<std::uint32_t> m_occupied_count{ 0u };
};

//==============================================================================
//  TRingProducerEndpoint<T>
//  Single Producer, Single Consumer (SPSC) producer endpoint
//==============================================================================

template<typename T>
class TRingProducerEndpoint
{
public:
    explicit TRingProducerEndpoint(TRing<T>& ring) noexcept : m_ring{ ring } {}
    ~TRingProducerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_ring.posting_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_ring.is_ready(); }
    [[nodiscard]] bool post(const T& src) noexcept { return m_ring.post(src); }
    [[nodiscard]] bool post(const T* const src, const std::uint32_t count = 1u) noexcept { return m_ring.post(src, count); }
    [[nodiscard]] bool post(const TPodConstView<T>& src) noexcept { return m_ring.post(src); }
    [[nodiscard]] std::uint32_t writable_count() const noexcept { return m_ring.writable_count(); }

private:
    TRing<T>& m_ring;
};

//==============================================================================
//  TRingConsumerEndpoint<T>
//  Single Producer, Single Consumer (SPSC) consumer endpoint
//==============================================================================

template<typename T>
class TRingConsumerEndpoint
{
public:
    explicit TRingConsumerEndpoint(TRing<T>& ring) noexcept : m_ring{ ring } {}
    ~TRingConsumerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_ring.reading_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_ring.is_ready(); }
    [[nodiscard]] bool read(T& dst) noexcept { return m_ring.read(dst); }
    [[nodiscard]] bool read(T* const dst, const std::uint32_t count = 1u) noexcept { return m_ring.read(dst, count); }
    [[nodiscard]] bool read(const TPodView<T>& dst) noexcept { return m_ring.read(dst); }
    [[nodiscard]] std::uint32_t readable_count() const noexcept { return m_ring.readable_count(); }

private:
    TRing<T>& m_ring;
};

//==============================================================================
//  TRingBundle<T>
//  Single Producer, Single Consumer (SPSC) transport bundle
//==============================================================================

template<typename T>
struct TRingBundle
{
    TRing<T> transport;
    TRingProducerEndpoint<T> producer;
    TRingConsumerEndpoint<T> consumer;

    TRingBundle() noexcept : transport(), producer(transport), consumer(transport) {}
};

//==============================================================================
//  TRing<T> out of class function bodies
//==============================================================================

template<typename T>
inline bool TRing<T>::is_valid() const noexcept
{
    if (!m_ring.is_relocatable() ||
        (m_ring.stride() != k_element_size) || (m_ring.storage_alignment() != k_align) ||
        (m_ring.count() != m_capacity))
    {
        return false;
    }
    if (m_capacity == 0u)
    {   //  uninitialised, safe to check both the read and the write indices
        if ((raw_data() != nullptr) || ((m_read_index | m_write_index) != 0u))
        {
            return false;
        }
    }
    else if (raw_data() == nullptr)
    {
        return false;
    }
    return m_occupied_count.load(std::memory_order_acquire) <= m_capacity;
}

template<typename T>
inline bool TRing<T>::is_ready() const noexcept
{
    return (m_capacity != 0u) && (raw_data() != nullptr);
}

template<typename T>
inline bool TRing<T>::posting_is_valid() const noexcept
{
    return is_valid() ? ((m_capacity != 0u) ? (m_write_index < m_capacity) : (m_write_index == 0u)) : false;
}

template<typename T>
inline bool TRing<T>::post(const T* const src, const std::uint32_t count) noexcept
{
    if (!is_ready() || (count > writable_count()) || ((src == nullptr) && (count != 0u)))
    {
        return false;
    }
    if (count != 0u)
    {
        const std::uint32_t tail_size = m_capacity - m_write_index;
        if (count <= tail_size)
        {
            std::memcpy((raw_data() + m_write_index), src, (static_cast<std::size_t>(count) * sizeof(T)));
            m_write_index = (m_write_index + count) & (m_capacity - 1u);
        }
        else
        {
            std::memcpy((raw_data() + m_write_index), src, (static_cast<std::size_t>(tail_size) * sizeof(T)));
            m_write_index = count - tail_size;
            std::memcpy(raw_data(), (src + tail_size), (static_cast<std::size_t>(m_write_index) * sizeof(T)));
        }
        m_occupied_count.fetch_add(count, std::memory_order_release);
    }
    return true;
}

template<typename T>
inline std::uint32_t TRing<T>::writable_count() const noexcept
{
    const std::uint32_t count = m_occupied_count.load(std::memory_order_acquire);
    return (count <= m_capacity) ? (m_capacity - count) : 0u;
}

template<typename T>
inline bool TRing<T>::reading_is_valid() const noexcept
{
    return is_valid() ? ((m_capacity != 0u) ? (m_read_index < m_capacity) : (m_read_index == 0u)) : false;
}

template<typename T>
inline bool TRing<T>::read(T* const dst, const std::uint32_t count) noexcept
{
    if (!is_ready() || (count > readable_count()) || ((dst == nullptr) && (count != 0u)))
    {
        return false;
    }
    if (count != 0u)
    {
        const std::uint32_t tail_size = m_capacity - m_read_index;
        if (count <= tail_size)
        {
            std::memcpy(dst, (raw_data() + m_read_index), (static_cast<std::size_t>(count) * sizeof(T)));
            m_read_index = (m_read_index + count) & (m_capacity - 1u);
        }
        else
        {
            std::memcpy(dst, (raw_data() + m_read_index), (static_cast<std::size_t>(tail_size) * sizeof(T)));
            m_read_index = count - tail_size;
            std::memcpy((dst + tail_size), raw_data(), (static_cast<std::size_t>(m_read_index) * sizeof(T)));
        }
        m_occupied_count.fetch_sub(count, std::memory_order_release);
    }
    return true;
}

template<typename T>
inline std::uint32_t TRing<T>::readable_count() const noexcept
{
    const std::uint32_t count = m_occupied_count.load(std::memory_order_acquire);
    return (count <= m_capacity) ? count : 0u;
}

template<typename T>
inline bool TRing<T>::initialise(const std::uint32_t capacity, memory::CMemoryContext* const context) noexcept
{
    if (capacity > k_max_capacity)
    {   //  requested capacity not supported
        return false;
    }
    if (m_capacity != 0u)
    {   //  re-initialisation is not allowed without deallocation
        return false;
    }
    if (!m_ring.set_context(context))
    {
        return false;
    }
    const std::uint32_t conditioned_capacity = std::max(std::min(bit_ops::round_up_to_pow2(capacity), k_max_capacity), k_min_capacity);
    if (!m_ring.allocate(conditioned_capacity))
    {   //  allocation failed
        return false;
    }
    m_capacity = conditioned_capacity;
    m_read_index = 0u;
    m_write_index = 0u;
    m_occupied_count.store(0u, std::memory_order_release);
    return true;
}

template<typename T>
inline bool TRing<T>::initialise(const std::uint32_t capacity) noexcept
{
    return initialise(capacity, m_ring.context());
}

template<typename T>
inline void TRing<T>::deallocate() noexcept
{
    m_ring.deallocate();
    m_capacity = 0u;
    m_read_index = 0u;
    m_write_index = 0u;
    m_occupied_count.store(0u, std::memory_order_release);
}

}   //  namespace threading::transports

#endif  //  #ifndef TRING_TRANSPORT_HPP_INCLUDED
