
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TOwningTransport.hpp
//  Author: Ritchie Brannan
//  Date:   21 Apr 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Fixed-capacity SPSC ring transport for owned T.
//
//  Defines threading::transports::TOwning<T>.
//  Also defines the concrete producer/consumer endpoint wrappers and the
//  simple one-transport bundle composition for TOwning.
// 
//  Does not grow, discard, overwrite unread data, or provide
//  blocking semantics.
//
//  IMPORTANT SEMANTIC NOTE
//  -----------------------
//  Slots are live T objects after initialise() and remain live until deallocate().
//
//  Occupancy controls readability and writability, not object lifetime.
//
//  writable_count() and readable_count() are snapshot observations,
//  not reservation mechanisms.
//
//  See docs/threading/transports/TOwningTransport.md for the full documentation.

#pragma once

#ifndef TOWNING_TRANSPORT_HPP_INCLUDED
#define TOWNING_TRANSPORT_HPP_INCLUDED

#include <algorithm>    //  std::min, std::max
#include <atomic>       //  std::atomic
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t
#include <new>          //  placement new
#include <type_traits>  //  std::is_const_v, std::is_trivially_copyable_v

#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"
#include "bit_utils/bit_ops.hpp"

namespace threading::transports
{

//==============================================================================
//  TOwning<T>
//  Single Producer, Single Consumer (SPSC) transport
//==============================================================================

template<typename T>
class TOwning
{
private:
    static_assert(!std::is_const_v<T>, "TOwning<T> requires non-const T.");
    static_assert(!std::is_copy_constructible_v<T>, "TOwning<T> requires non-copy constructible T.");
    static_assert(!std::is_copy_assignable_v<T>, "TOwning<T> requires non-copy assignable T.");
    static_assert(std::is_nothrow_default_constructible_v<T>, "TOwning<T> requires nothrow default constructible T.");
    static_assert(std::is_nothrow_move_constructible_v<T>, "TOwning<T> requires nothrow move constructible T.");
    static_assert(std::is_nothrow_move_assignable_v<T>, "TOwning<T> requires nothrow move assignable T.");
    static_assert(std::is_nothrow_destructible_v<T>, "TOwning<T> requires nothrow destructible T.");

public:
    static constexpr std::uint32_t k_max_capacity = 0x00100000u;    //  approximately 1 million elements
    static constexpr std::uint32_t k_min_capacity = 32u;
    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();

private:
    static constexpr std::uint32_t k_token_max_capacity = static_cast<std::uint32_t>(memory::t_max_elements<T>());

    [[nodiscard]] T* raw_data() noexcept { return static_cast<T*>(m_ring.data()); }
    [[nodiscard]] const T* raw_data() const noexcept { return static_cast<const T*>(m_ring.data()); }

public:
    TOwning() noexcept = default;
    explicit TOwning(memory::CMemoryContext* const context) noexcept : m_ring{ k_element_size, k_align, context } {}
    TOwning(const TOwning&) = delete;
    TOwning& operator=(const TOwning&) = delete;
    TOwning(TOwning&&) noexcept = delete;
    TOwning& operator=(TOwning&&) noexcept = delete;
    ~TOwning() noexcept { deallocate(); }

    //  Shared status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Producer status and operations
    //  - only safe to call from the producer thread or while quiescent
    [[nodiscard]] bool posting_is_valid() const noexcept;
    [[nodiscard]] bool post(T&& src) noexcept;
    [[nodiscard]] std::uint32_t writable_count() const noexcept;

    //  Consumer status and operations
    //  - only safe to call from the consumer thread or while quiescent
    [[nodiscard]] bool reading_is_valid() const noexcept;
    [[nodiscard]] bool read(T& dst) noexcept;
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
    static_assert((k_element_size <= 0xffffu), "TOwning<T> element size exceeds the memory token stride field.");
    static_assert((k_element_size <= memory::k_byte_size_ceiling), "TOwning<T> element size exceeds the shared byte ceiling.");

    memory::CMemoryToken m_ring{ k_element_size, k_align };
    std::uint32_t m_capacity = 0u;
    std::uint32_t m_read_index = 0u;
    std::uint32_t m_write_index = 0u;
    std::atomic<std::uint32_t> m_occupied_count{ 0u };
};

//==============================================================================
//  TOwningProducerEndpoint<T>
//  Single Producer, Single Consumer (SPSC) producer endpoint
//==============================================================================

template<typename T>
class TOwningProducerEndpoint
{
public:
    explicit TOwningProducerEndpoint(TOwning<T>& ring) noexcept : m_ring{ ring } {}
    ~TOwningProducerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_ring.posting_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_ring.is_ready(); }
    [[nodiscard]] bool post(T&& src) noexcept { return m_ring.post(std::move(src)); }
    [[nodiscard]] std::uint32_t writable_count() const noexcept { return m_ring.writable_count(); }

private:
    TOwning<T>& m_ring;
};

//==============================================================================
//  TOwningConsumerEndpoint<T>
//  Single Producer, Single Consumer (SPSC) consumer endpoint
//==============================================================================

template<typename T>
class TOwningConsumerEndpoint
{
public:
    explicit TOwningConsumerEndpoint(TOwning<T>& ring) noexcept : m_ring{ ring } {}
    ~TOwningConsumerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_ring.reading_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_ring.is_ready(); }
    [[nodiscard]] bool read(T& dst) noexcept { return m_ring.read(dst); }
    [[nodiscard]] std::uint32_t readable_count() const noexcept { return m_ring.readable_count(); }

private:
    TOwning<T>& m_ring;
};

//==============================================================================
//  TOwningBundle<T>
//  Single Producer, Single Consumer (SPSC) transport bundle
//==============================================================================

template<typename T>
struct TOwningBundle
{
    TOwning<T> transport;
    TOwningProducerEndpoint<T> producer;
    TOwningConsumerEndpoint<T> consumer;

    TOwningBundle() noexcept : transport(), producer(transport), consumer(transport) {}
};

//==============================================================================
//  TOwning<T> out of class function bodies
//==============================================================================

template<typename T>
inline bool TOwning<T>::is_valid() const noexcept
{
    if (!m_ring.is_relocatable() ||
        (m_ring.stride() != k_element_size) || (m_ring.storage_alignment() != k_align) ||
        (m_ring.count() != m_capacity))
    {
        return false;
    }

    const T* const ring = raw_data();
    if (m_capacity == 0u)
    {
        return (ring == nullptr) && ((m_read_index | m_write_index) == 0u);
    }
    if (ring == nullptr)
    {
        return false;
    }
    return (m_occupied_count.load(std::memory_order_acquire) <= m_capacity) &&
        (m_capacity <= k_max_capacity) && (m_capacity <= k_token_max_capacity);
}

template<typename T>
inline bool TOwning<T>::is_ready() const noexcept
{
    return (m_capacity != 0u) && (raw_data() != nullptr);
}

template<typename T>
inline bool TOwning<T>::posting_is_valid() const noexcept
{
    return is_valid() ? ((m_capacity != 0u) ? (m_write_index < m_capacity) : (m_write_index == 0u)) : false;
}

template<typename T>
inline bool TOwning<T>::post(T&& src) noexcept
{
    if (!is_ready() || (writable_count() == 0u))
    {
        return false;
    }
    raw_data()[m_write_index] = std::move(src);
    m_write_index = (m_write_index + 1u) & (m_capacity - 1u);
    m_occupied_count.fetch_add(1u, std::memory_order_release);
    return true;
}

template<typename T>
inline std::uint32_t TOwning<T>::writable_count() const noexcept
{
    const std::uint32_t count = m_occupied_count.load(std::memory_order_acquire);
    return (count <= m_capacity) ? (m_capacity - count) : 0u;
}

template<typename T>
inline bool TOwning<T>::reading_is_valid() const noexcept
{
    return is_valid() ? ((m_capacity != 0u) ? (m_read_index < m_capacity) : (m_read_index == 0u)) : false;
}

template<typename T>
inline bool TOwning<T>::read(T& dst) noexcept
{
    if (!is_ready() || (readable_count() == 0u))
    {
        return false;
    }
    dst = std::move(raw_data()[m_read_index]);
    m_read_index = (m_read_index + 1u) & (m_capacity - 1u);
    m_occupied_count.fetch_sub(1u, std::memory_order_release);
    return true;
}

template<typename T>
inline std::uint32_t TOwning<T>::readable_count() const noexcept
{
    const std::uint32_t count = m_occupied_count.load(std::memory_order_acquire);
    return (count <= m_capacity) ? count : 0u;
}

template<typename T>
inline bool TOwning<T>::initialise(const std::uint32_t capacity, memory::CMemoryContext* const context) noexcept
{
    if ((capacity > k_max_capacity) || (capacity > k_token_max_capacity))
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
    const std::uint32_t conditioned_capacity = std::max(
        std::min(bit_ops::round_up_to_pow2(capacity), std::min(k_max_capacity, k_token_max_capacity)),
        k_min_capacity);
    if (!m_ring.allocate(conditioned_capacity))
    {   //  allocation failed
        return false;
    }
    m_capacity = conditioned_capacity;
    m_read_index = 0u;
    m_write_index = 0u;
    m_occupied_count.store(0u, std::memory_order_release);
    for (std::uint32_t index = 0u; index < m_capacity; ++index)
    {
        ::new (static_cast<void*>(&raw_data()[index])) T();
    }
    return true;
}

template<typename T>
inline bool TOwning<T>::initialise(const std::uint32_t capacity) noexcept
{
    return initialise(capacity, m_ring.context());
}

template<typename T>
inline void TOwning<T>::deallocate() noexcept
{
    if (raw_data() != nullptr)
    {
        for (std::uint32_t index = 0u; index < m_capacity; ++index)
        {
            raw_data()[index].~T();
        }
    }
    m_ring.deallocate();
    m_capacity = 0u;
    m_read_index = 0u;
    m_write_index = 0u;
    m_occupied_count.store(0u, std::memory_order_release);
}

}   //  namespace threading::transports

#endif  //  #ifndef TOWNING_TRANSPORT_HPP_INCLUDED
