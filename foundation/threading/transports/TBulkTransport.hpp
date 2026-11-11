
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TBulkTransport.hpp
//  Author: Ritchie Brannan
//  Date:   25 Apr 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Defines threading::transports::TBulk<T>.
//  Also defines the concrete producer/consumer endpoint wrappers and the
//  simple one-transport bundle composition for TBulk.
//
//  Triple-buffered SPSC latest-state transport for bulk payloads.
//
//  TBulk owns three live T instances for the lifetime of the transport.
//  The producer mutates the producer-owned instance returned by
//  producer_data() and publishes it with publish().
//
//  This is a discard-based transport, not a FIFO.  A newly published
//  payload replaces any previously staged payload that the consumer has
//  not acquired.  The consumer acquires at most one staged payload with
//  discard_and_acquire(), which first releases any currently acquired
//  consumer payload.
//
//  There is no allocation, growth, blocking, overwrite of consumer-held
//  data, or sequential consume guarantee.
//
//  T must be non-const, nothrow default constructible, and nothrow
//  destructible.  TBulk does not construct, destroy, copy, or move T
//  during publication or acquisition.

#pragma once

#ifndef TBULK_TRANSPORT_HPP_INCLUDED
#define TBULK_TRANSPORT_HPP_INCLUDED

#include <atomic>       //  std::atomic
#include <cstdint>      //  std::uint32_t
#include <type_traits>  //  std::is_const_v, std::is_trivially_copyable_v

#include "debug/macros.hpp"

namespace threading::transports
{

//==============================================================================
//  TBulk<T>
//  Single Producer, Single Consumer (SPSC) transport
//==============================================================================

template<typename T>
class TBulk
{
private:
    static_assert(!std::is_const_v<T>, "TBulk<T> requires non-const T.");
    static_assert(std::is_nothrow_default_constructible_v<T>, "TBulk<T> requires nothrow default constructible T.");
    static_assert(std::is_nothrow_destructible_v<T>, "TBulk<T> requires nothrow destructible T.");

public:
    TBulk() noexcept = default;
    TBulk(const TBulk&) = delete;
    TBulk& operator=(const TBulk&) = delete;
    TBulk(TBulk&&) noexcept = delete;
    TBulk& operator=(TBulk&&) noexcept = delete;
    ~TBulk() noexcept = default;

    //  Producer operations
    //  - only safe to call from the producer thread or while quiescent
    //  - producer_data() returns the producer-owned payload currently safe to modify
    //  - publish() always publishes the current producer payload
    //  - returns true if the previously staged payload had been acquired by the consumer
    //  - returns false if the previously staged payload was replaced before acquisition
    [[nodiscard]] T* producer_data() noexcept;
    bool publish() noexcept;

    //  Consumer operations
    //  - only safe to call from the consumer thread or while quiescent
    //  - consumer_data() returns the currently acquired consumer payload,
    //    or nullptr if none is active; it does not acquire new data
    //  - discard_and_acquire() always discards any active consumer data and
    //    returns true if new data was acquired.
    [[nodiscard]] T* consumer_data() noexcept;
    bool discard_and_acquire() noexcept;

    //  Validation
    //  - full structural validity check
    //  - only safe to call while quiescent
    [[nodiscard]] bool validate() const noexcept;

private:
    [[nodiscard]] std::uint32_t exchange(const std::uint32_t stage) noexcept;

    //  Atomic publication word encoding:
    //  - 0 means the consumer acquired the staged payload.
    //  - 1..3 mean payload index + 1.
    //  - 4 is the initial/null staged word; it prevents the first publish()
    //    from reporting that a previous staged payload had been acquired.
    //  The consumer tests (word & 3u) to accept only payload words 1..3.
    static constexpr std::uint32_t k_null_payload_index{ 3u };
    static constexpr std::uint32_t k_null_staged_word{ k_null_payload_index + 1u };

    //  Payload data
    T m_data[3];

    //  Producer state - Consumer side must not access this
    std::uint32_t m_producer_output_payload_index = 0u;  //  "owned" by the producer side
    std::uint32_t m_producer_staged_payload_index = 1u;  //  "unowned" staged
    std::uint32_t m_producer_locked_payload_index = 2u;  //  "owned" by the consumer side

    //  Consumer state - Producer side must not access this
    std::uint32_t m_consumer_payload_index = k_null_payload_index;

    //  Publication state
    std::atomic<std::uint32_t> m_staged_word{ k_null_staged_word };
};

//==============================================================================
//  TBulkProducerEndpoint<T>
//  Single Producer, Single Consumer (SPSC) producer endpoint
//==============================================================================

template<typename T>
class TBulkProducerEndpoint
{
public:
    explicit TBulkProducerEndpoint(TBulk<T>& bulk) noexcept : m_bulk{ bulk } {}
    ~TBulkProducerEndpoint() noexcept = default;

    [[nodiscard]] T* producer_data() noexcept { return m_bulk.producer_data(); }
    bool publish() noexcept { return m_bulk.publish(); }

private:
    TBulk<T>& m_bulk;
};

//==============================================================================
//  TBulkConsumerEndpoint<T>
//  Single Producer, Single Consumer (SPSC) consumer endpoint
//==============================================================================

template<typename T>
class TBulkConsumerEndpoint
{
public:
    explicit TBulkConsumerEndpoint(TBulk<T>& bulk) noexcept : m_bulk{ bulk } {}
    ~TBulkConsumerEndpoint() noexcept = default;

    [[nodiscard]] T* consumer_data() noexcept { return m_bulk.consumer_data(); }
    bool discard_and_acquire() noexcept { return m_bulk.discard_and_acquire(); }

private:
    TBulk<T>& m_bulk;
};

//==============================================================================
//  TBulkBundle<T>
//  Single Producer, Single Consumer (SPSC) transport bundle
//==============================================================================

template<typename T>
struct TBulkBundle
{
    TBulk<T> transport;
    TBulkProducerEndpoint<T> producer;
    TBulkConsumerEndpoint<T> consumer;

    TBulkBundle() noexcept : transport(), producer(transport), consumer(transport) {}
};

//==============================================================================
//  TBulk<T> out of class function bodies
//==============================================================================

template<typename T>
inline bool TBulk<T>::publish() noexcept
{
    bool previous_staged_was_acquired = false;
    const std::uint32_t payload_index_swap = m_producer_output_payload_index;
    const std::uint32_t received = exchange(m_producer_output_payload_index + 1u);
    if (received == 0u)
    {   //  Previous staged publication was acquired by the consumer
        previous_staged_was_acquired = true;
        m_producer_locked_payload_index = m_producer_staged_payload_index;
        m_producer_output_payload_index = m_producer_locked_payload_index;
    }
    else
    {   //  Previous staged publication was replaced before acquisition
        m_producer_output_payload_index = m_producer_staged_payload_index;
    }
    m_producer_staged_payload_index = payload_index_swap;
    return previous_staged_was_acquired;
}

template<typename T>
inline T* TBulk<T>::producer_data() noexcept
{
    MV_ASSERT(m_producer_output_payload_index < k_null_payload_index);
    return &m_data[(m_producer_output_payload_index < k_null_payload_index) ? m_producer_output_payload_index : 0u];
}

template<typename T>
inline bool TBulk<T>::discard_and_acquire() noexcept
{
    bool staged_was_acquired = false;
    m_consumer_payload_index = k_null_payload_index;
    const std::uint32_t received = exchange(0u);
    if ((received & 3u) != 0u)
    {   //  Staged publication was acquired
        staged_was_acquired = true;
        m_consumer_payload_index = received - 1u;
    }
    return staged_was_acquired;
}

template<typename T>
inline T* TBulk<T>::consumer_data() noexcept
{
    if (m_consumer_payload_index > k_null_payload_index)
    {
        MV_ERROR("TBulk::consumer_data observed an invalid consumer payload index");
    }
    return (m_consumer_payload_index < k_null_payload_index) ? &m_data[m_consumer_payload_index] : nullptr;
}

template<typename T>
inline bool TBulk<T>::validate() const noexcept
{
    if ((m_producer_output_payload_index >= k_null_payload_index) ||
        (m_producer_staged_payload_index >= k_null_payload_index) ||
        (m_producer_locked_payload_index >= k_null_payload_index) ||
        (m_consumer_payload_index > k_null_payload_index))
    {
        return false;
    }

    if ((m_consumer_payload_index != k_null_payload_index) &&
        (m_consumer_payload_index != m_producer_locked_payload_index))
    {
        return false;
    }

    const std::uint32_t permutation_check =
        (1u << m_producer_output_payload_index) |
        (1u << m_producer_staged_payload_index) |
        (1u << m_producer_locked_payload_index);

    if (permutation_check != 7u)
    {
        return false;
    }

    return m_staged_word.load(std::memory_order_relaxed) <= k_null_staged_word;
}

template<typename T>
inline std::uint32_t TBulk<T>::exchange(const std::uint32_t stage) noexcept
{
    return m_staged_word.exchange(stage, std::memory_order_acq_rel);
}

}   //  namespace threading::transports

#endif  //  #ifndef TBULK_TRANSPORT_HPP_INCLUDED
