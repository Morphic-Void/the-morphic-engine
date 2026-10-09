
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TMpmcTransport.hpp
//  Primary implementation: OpenAI tools
//  Reviewed and accepted by: Ritchie Brannan
//  Date:   21 Jul 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Single-header bounded MPMC transport family.
//
//  Operating overview:
//  - The raw building block is a fixed-capacity Rigtorp-style MPMC index ring.
//  - The first composed transport uses two such rings plus a same-sized typed
//    arena to implement a reserve/populate/publish and
//    acquire/process/recycle pipeline.
//  - A higher composition layer pairs two arena transports for work and
//    completion/status-return flow.
//
//  Ownership and moveability:
//  - The transport family performs no live allocation.
//  - When relocatable ownership is desired, wrap the whole instantiated
//    transport object in TInstance<...>.
//  - That permits outer-object relocation or reattribution without making the
//    live shared transport itself an internally allocative or independently
//    reattributable object.
//
//  The family consists of:
//  - a raw fixed-capacity Rigtorp-style MPMC index ring
//  - an arena-backed reserve/publish/acquire/recycle transport built from two
//    raw rings
//  - a paired higher-level composite holding two arena transports

#pragma once

#ifndef TMPMC_TRANSPORT_HPP_INCLUDED
#define TMPMC_TRANSPORT_HPP_INCLUDED

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "bit_utils/bit_ops.hpp"
#include "platform/threading/processor_relax.hpp"
#include "types/atomic_types.hpp"

namespace threading::transports
{

enum class EMpmcTransportStatus : std::uint32_t
{
    open = 0u,
    closing = 1u,
    closed = 2u,
    shutdown = 3u
};

template<std::uint32_t t_capacity_hint>
class TMpmcIndexRing
{
public:
    static constexpr std::uint32_t k_min_capacity = 16u;
    static constexpr std::uint32_t k_max_capacity = 1u << 20u; // closest power of two greater than 1,000,000

private:
    static constexpr std::uint32_t condition_capacity(const std::uint32_t hint) noexcept
    {
        const std::uint32_t rounded = bit_ops::round_up_to_pow2(hint);
        return (rounded < k_min_capacity) ? k_min_capacity : rounded;
    }

public:
    static_assert((t_capacity_hint <= k_max_capacity), "TMpmcIndexRing capacity hint exceeds the supported maximum.");

    static constexpr std::uint32_t k_capacity = condition_capacity(t_capacity_hint);
    static constexpr std::uint32_t k_mask = k_capacity - 1u;

private:
    static_assert(bit_ops::is_pow2(k_capacity), "TMpmcIndexRing effective capacity must be a power of two.");

    struct Slot
    {
        std::atomic<std::uint32_t> sequence;
        std::uint32_t payload;
    };

public:
    explicit TMpmcIndexRing(const bool start_full = false) noexcept;
    TMpmcIndexRing(const TMpmcIndexRing&) = delete;
    TMpmcIndexRing& operator=(const TMpmcIndexRing&) = delete;
    TMpmcIndexRing(TMpmcIndexRing&&) noexcept = delete;
    TMpmcIndexRing& operator=(TMpmcIndexRing&&) noexcept = delete;
    ~TMpmcIndexRing() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] std::uint32_t readable_count() const noexcept;
    [[nodiscard]] std::uint32_t writable_count() const noexcept;

    [[nodiscard]] bool push(const std::uint32_t payload, std::uint32_t& out_sequence) noexcept;
    [[nodiscard]] bool pop(std::uint32_t& out_payload, std::uint32_t& out_sequence) noexcept;

private:
    friend struct SMpmcTransportTestAccess;
    alignas(128) Slot m_slots[k_capacity];
    TCacheLineAtomic<std::uint32_t> m_enqueue_position{};
    TCacheLineAtomic<std::uint32_t> m_dequeue_position{};
};

template<typename T, std::uint32_t t_capacity_hint>
class TMpmcArenaTransport;

template<typename T, std::uint32_t t_capacity_hint>
class TReservedArenaSlot
{
public:
    explicit TReservedArenaSlot(TMpmcArenaTransport<T, t_capacity_hint>& transport) noexcept;
    TReservedArenaSlot(const TReservedArenaSlot&) = delete;
    TReservedArenaSlot& operator=(const TReservedArenaSlot&) = delete;
    TReservedArenaSlot(TReservedArenaSlot&& other) noexcept;
    TReservedArenaSlot& operator=(TReservedArenaSlot&&) noexcept = delete;
    ~TReservedArenaSlot() noexcept;

    [[nodiscard]] bool is_ready() const noexcept { return m_slot != nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return is_ready(); }
    [[nodiscard]] T* get() noexcept { return m_slot; }
    [[nodiscard]] const T* get() const noexcept { return m_slot; }
    [[nodiscard]] T& operator*() noexcept { return *m_slot; }
    [[nodiscard]] const T& operator*() const noexcept { return *m_slot; }
    [[nodiscard]] T* operator->() noexcept { return m_slot; }
    [[nodiscard]] const T* operator->() const noexcept { return m_slot; }
    [[nodiscard]] std::uint32_t index() const noexcept { return m_index; }
    [[nodiscard]] std::uint32_t reserve_sequence() const noexcept { return m_reserve_sequence; }
    [[nodiscard]] bool publish(std::uint32_t& out_sequence) noexcept;
    [[nodiscard]] bool publish() noexcept;

private:
    TMpmcArenaTransport<T, t_capacity_hint>* m_transport = nullptr;
    T* m_slot = nullptr;
    std::uint32_t m_index = 0u;
    std::uint32_t m_reserve_sequence = 0u;
};

template<typename T, std::uint32_t t_capacity_hint>
class TAcquiredArenaSlot
{
public:
    explicit TAcquiredArenaSlot(TMpmcArenaTransport<T, t_capacity_hint>& transport) noexcept;
    TAcquiredArenaSlot(const TAcquiredArenaSlot&) = delete;
    TAcquiredArenaSlot& operator=(const TAcquiredArenaSlot&) = delete;
    TAcquiredArenaSlot(TAcquiredArenaSlot&& other) noexcept;
    TAcquiredArenaSlot& operator=(TAcquiredArenaSlot&&) noexcept = delete;
    ~TAcquiredArenaSlot() noexcept;

    [[nodiscard]] bool is_ready() const noexcept { return m_slot != nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return is_ready(); }
    [[nodiscard]] T* get() noexcept { return m_slot; }
    [[nodiscard]] const T* get() const noexcept { return m_slot; }
    [[nodiscard]] T& operator*() noexcept { return *m_slot; }
    [[nodiscard]] const T& operator*() const noexcept { return *m_slot; }
    [[nodiscard]] T* operator->() noexcept { return m_slot; }
    [[nodiscard]] const T* operator->() const noexcept { return m_slot; }
    [[nodiscard]] std::uint32_t index() const noexcept { return m_index; }
    [[nodiscard]] std::uint32_t acquire_sequence() const noexcept { return m_acquire_sequence; }
    [[nodiscard]] bool recycle(std::uint32_t& out_sequence) noexcept;
    [[nodiscard]] bool recycle() noexcept;

private:
    TMpmcArenaTransport<T, t_capacity_hint>* m_transport = nullptr;
    T* m_slot = nullptr;
    std::uint32_t m_index = 0u;
    std::uint32_t m_acquire_sequence = 0u;
};

template<typename T, std::uint32_t t_capacity_hint>
class TMpmcArenaTransport
{
    static_assert(std::is_nothrow_default_constructible_v<T>,
        "TMpmcArenaTransport<T> requires nothrow default constructible T.");
    static_assert(std::is_nothrow_destructible_v<T>,
        "TMpmcArenaTransport<T> requires nothrow destructible T.");

public:
    static constexpr std::uint32_t k_capacity = TMpmcIndexRing<t_capacity_hint>::k_capacity;

    TMpmcArenaTransport() noexcept = default;
    TMpmcArenaTransport(const TMpmcArenaTransport&) = delete;
    TMpmcArenaTransport& operator=(const TMpmcArenaTransport&) = delete;
    TMpmcArenaTransport(TMpmcArenaTransport&&) noexcept = delete;
    TMpmcArenaTransport& operator=(TMpmcArenaTransport&&) noexcept = delete;
    ~TMpmcArenaTransport() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] EMpmcTransportStatus status() const noexcept;
    [[nodiscard]] bool is_open() const noexcept { return status() == EMpmcTransportStatus::open; }
    [[nodiscard]] bool is_closing() const noexcept { return status() == EMpmcTransportStatus::closing; }
    [[nodiscard]] bool is_closed() const noexcept { return status() == EMpmcTransportStatus::closed; }
    [[nodiscard]] bool is_shutdown() const noexcept { return status() == EMpmcTransportStatus::shutdown; }

    [[nodiscard]] bool begin_closing() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] T* reserve(std::uint32_t& out_index, std::uint32_t& out_sequence) noexcept;
    //  An owned slot guarantees ring capacity, but an earlier pop may still be
    //  releasing its ring cell. Completion waits for that transient contention;
    //  shutdown may abandon it. Reserve/acquire remain nonblocking attempts.
    [[nodiscard]] bool publish(const T* const slot, std::uint32_t& out_sequence) noexcept;
    [[nodiscard]] T* acquire(std::uint32_t& out_index, std::uint32_t& out_sequence) noexcept;
    [[nodiscard]] bool recycle(const T* const slot, std::uint32_t& out_sequence) noexcept;

    [[nodiscard]] std::uint32_t outstanding_count() const noexcept { return m_outstanding_count.value.load(std::memory_order_acquire); }

private:
    friend struct SMpmcTransportTestAccess;
    friend class TReservedArenaSlot<T, t_capacity_hint>;
    friend class TAcquiredArenaSlot<T, t_capacity_hint>;

    [[nodiscard]] bool state_allows_reserve() const noexcept;
    [[nodiscard]] bool state_allows_acquire_or_complete() const noexcept;
    [[nodiscard]] std::uint32_t slot_index(const T* const slot, bool& ok) const noexcept;

    TMpmcIndexRing<t_capacity_hint> m_supplier_ring{ true };
    TMpmcIndexRing<t_capacity_hint> m_populated_ring{ false };
    TCacheLineAtomic<std::uint32_t> m_status_word{ static_cast<std::uint32_t>(EMpmcTransportStatus::open) };
    TCacheLineAtomic<std::uint32_t> m_outstanding_count{};
    alignas(128) T m_arena[k_capacity];
};

template<
    typename TWork, std::uint32_t t_work_capacity_hint,
    typename TReturn, std::uint32_t t_return_capacity_hint>
struct TMpmcJobTransport
{
    TMpmcArenaTransport<TWork, t_work_capacity_hint> work;
    TMpmcArenaTransport<TReturn, t_return_capacity_hint> feedback;

    [[nodiscard]] bool is_valid() const noexcept { return work.is_valid() && feedback.is_valid(); }
};

//==============================================================================
//  TMpmcIndexRing<t_capacity_hint> out of class function bodies
//==============================================================================

template<std::uint32_t t_capacity_hint>
inline TMpmcIndexRing<t_capacity_hint>::TMpmcIndexRing(const bool start_full) noexcept
{
    for (std::uint32_t index = 0u; index < k_capacity; ++index)
    {
        m_slots[index].payload = index;
        m_slots[index].sequence.store(index + (start_full ? 1u : 0u), std::memory_order_relaxed);
    }

    m_enqueue_position.value.store(start_full ? k_capacity : 0u, std::memory_order_relaxed);
    m_dequeue_position.value.store(0u, std::memory_order_relaxed);
}

template<std::uint32_t t_capacity_hint>
inline bool TMpmcIndexRing<t_capacity_hint>::is_valid() const noexcept
{
    const std::uint32_t enqueue = m_enqueue_position.value.load(std::memory_order_acquire);
    const std::uint32_t dequeue = m_dequeue_position.value.load(std::memory_order_acquire);
    return (enqueue - dequeue) <= k_capacity;
}

template<std::uint32_t t_capacity_hint>
inline std::uint32_t TMpmcIndexRing<t_capacity_hint>::readable_count() const noexcept
{
    const std::uint32_t enqueue = m_enqueue_position.value.load(std::memory_order_acquire);
    const std::uint32_t dequeue = m_dequeue_position.value.load(std::memory_order_acquire);
    const std::uint32_t count = enqueue - dequeue;
    return (count <= k_capacity) ? count : 0u;
}

template<std::uint32_t t_capacity_hint>
inline std::uint32_t TMpmcIndexRing<t_capacity_hint>::writable_count() const noexcept
{
    return k_capacity - readable_count();
}

template<std::uint32_t t_capacity_hint>
inline bool TMpmcIndexRing<t_capacity_hint>::push(const std::uint32_t payload, std::uint32_t& out_sequence) noexcept
{
    std::uint32_t position = m_enqueue_position.value.load(std::memory_order_relaxed);

    for (;;)
    {
        Slot& slot = m_slots[position & k_mask];
        const std::uint32_t sequence = slot.sequence.load(std::memory_order_acquire);
        const std::int32_t difference = static_cast<std::int32_t>(sequence - position);

        if (difference == 0)
        {
            if (m_enqueue_position.value.compare_exchange_weak(
                position, position + 1u, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                slot.payload = payload;
                slot.sequence.store(position + 1u, std::memory_order_release);
                out_sequence = position;
                return true;
            }
        }
        else if (difference < 0)
        {
            return false;
        }
        else
        {
            position = m_enqueue_position.value.load(std::memory_order_relaxed);
        }
    }
}

template<std::uint32_t t_capacity_hint>
inline bool TMpmcIndexRing<t_capacity_hint>::pop(std::uint32_t& out_payload, std::uint32_t& out_sequence) noexcept
{
    std::uint32_t position = m_dequeue_position.value.load(std::memory_order_relaxed);

    for (;;)
    {
        Slot& slot = m_slots[position & k_mask];
        const std::uint32_t sequence = slot.sequence.load(std::memory_order_acquire);
        const std::int32_t difference = static_cast<std::int32_t>(sequence - (position + 1u));

        if (difference == 0)
        {
            if (m_dequeue_position.value.compare_exchange_weak(
                position, position + 1u, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                out_payload = slot.payload;
                slot.sequence.store(position + k_capacity, std::memory_order_release);
                out_sequence = position;
                return true;
            }
        }
        else if (difference < 0)
        {
            return false;
        }
        else
        {
            position = m_dequeue_position.value.load(std::memory_order_relaxed);
        }
    }
}

//==============================================================================
//  TReservedArenaSlot<T, t_capacity_hint> out of class function bodies
//==============================================================================

template<typename T, std::uint32_t t_capacity_hint>
inline TReservedArenaSlot<T, t_capacity_hint>::TReservedArenaSlot(
    TMpmcArenaTransport<T, t_capacity_hint>& transport) noexcept :
    m_transport{ &transport }
{
    m_slot = m_transport->reserve(m_index, m_reserve_sequence);
}

template<typename T, std::uint32_t t_capacity_hint>
inline TReservedArenaSlot<T, t_capacity_hint>::TReservedArenaSlot(TReservedArenaSlot&& other) noexcept :
    m_transport{ other.m_transport },
    m_slot{ other.m_slot },
    m_index{ other.m_index },
    m_reserve_sequence{ other.m_reserve_sequence }
{
    other.m_transport = nullptr;
    other.m_slot = nullptr;
    other.m_index = 0u;
    other.m_reserve_sequence = 0u;
}

template<typename T, std::uint32_t t_capacity_hint>
inline TReservedArenaSlot<T, t_capacity_hint>::~TReservedArenaSlot() noexcept
{
    (void)publish();
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TReservedArenaSlot<T, t_capacity_hint>::publish(std::uint32_t& out_sequence) noexcept
{
    if ((m_transport == nullptr) || (m_slot == nullptr))
    {
        return false;
    }

    T* const slot = m_slot;
    TMpmcArenaTransport<T, t_capacity_hint>* const transport = m_transport;
    m_slot = nullptr;
    m_transport = nullptr;
    return transport->publish(slot, out_sequence);
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TReservedArenaSlot<T, t_capacity_hint>::publish() noexcept
{
    std::uint32_t ignored_sequence = 0u;
    return publish(ignored_sequence);
}

//==============================================================================
//  TAcquiredArenaSlot<T, t_capacity_hint> out of class function bodies
//==============================================================================

template<typename T, std::uint32_t t_capacity_hint>
inline TAcquiredArenaSlot<T, t_capacity_hint>::TAcquiredArenaSlot(
    TMpmcArenaTransport<T, t_capacity_hint>& transport) noexcept :
    m_transport{ &transport }
{
    m_slot = m_transport->acquire(m_index, m_acquire_sequence);
}

template<typename T, std::uint32_t t_capacity_hint>
inline TAcquiredArenaSlot<T, t_capacity_hint>::TAcquiredArenaSlot(TAcquiredArenaSlot&& other) noexcept :
    m_transport{ other.m_transport },
    m_slot{ other.m_slot },
    m_index{ other.m_index },
    m_acquire_sequence{ other.m_acquire_sequence }
{
    other.m_transport = nullptr;
    other.m_slot = nullptr;
    other.m_index = 0u;
    other.m_acquire_sequence = 0u;
}

template<typename T, std::uint32_t t_capacity_hint>
inline TAcquiredArenaSlot<T, t_capacity_hint>::~TAcquiredArenaSlot() noexcept
{
    (void)recycle();
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TAcquiredArenaSlot<T, t_capacity_hint>::recycle(std::uint32_t& out_sequence) noexcept
{
    if ((m_transport == nullptr) || (m_slot == nullptr))
    {
        return false;
    }

    T* const slot = m_slot;
    TMpmcArenaTransport<T, t_capacity_hint>* const transport = m_transport;
    m_slot = nullptr;
    m_transport = nullptr;
    return transport->recycle(slot, out_sequence);
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TAcquiredArenaSlot<T, t_capacity_hint>::recycle() noexcept
{
    std::uint32_t ignored_sequence = 0u;
    return recycle(ignored_sequence);
}

//==============================================================================
//  TMpmcArenaTransport<T, t_capacity_hint> out of class function bodies
//==============================================================================

template<typename T, std::uint32_t t_capacity_hint>
inline bool TMpmcArenaTransport<T, t_capacity_hint>::is_valid() const noexcept
{
    const std::uint32_t raw_status = m_status_word.value.load(std::memory_order_acquire);
    return
        m_supplier_ring.is_valid() &&
        m_populated_ring.is_valid() &&
        (outstanding_count() <= k_capacity) &&
        (raw_status <= static_cast<std::uint32_t>(EMpmcTransportStatus::shutdown));
}

template<typename T, std::uint32_t t_capacity_hint>
inline EMpmcTransportStatus TMpmcArenaTransport<T, t_capacity_hint>::status() const noexcept
{
    return static_cast<EMpmcTransportStatus>(m_status_word.value.load(std::memory_order_acquire));
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TMpmcArenaTransport<T, t_capacity_hint>::begin_closing() noexcept
{
    std::uint32_t expected = static_cast<std::uint32_t>(EMpmcTransportStatus::open);
    if (!m_status_word.value.compare_exchange_strong(
        expected,
        static_cast<std::uint32_t>(EMpmcTransportStatus::closing),
        std::memory_order_acq_rel,
        std::memory_order_acquire))
    {
        return false;
    }

    if (outstanding_count() == 0u)
    {
        expected = static_cast<std::uint32_t>(EMpmcTransportStatus::closing);
        (void)m_status_word.value.compare_exchange_strong(
            expected,
            static_cast<std::uint32_t>(EMpmcTransportStatus::closed),
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    return true;
}

template<typename T, std::uint32_t t_capacity_hint>
inline void TMpmcArenaTransport<T, t_capacity_hint>::shutdown() noexcept
{
    m_status_word.value.store(static_cast<std::uint32_t>(EMpmcTransportStatus::shutdown), std::memory_order_release);
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TMpmcArenaTransport<T, t_capacity_hint>::state_allows_reserve() const noexcept
{
    return status() == EMpmcTransportStatus::open;
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TMpmcArenaTransport<T, t_capacity_hint>::state_allows_acquire_or_complete() const noexcept
{
    const EMpmcTransportStatus current = status();
    return (current == EMpmcTransportStatus::open) || (current == EMpmcTransportStatus::closing);
}

template<typename T, std::uint32_t t_capacity_hint>
inline std::uint32_t TMpmcArenaTransport<T, t_capacity_hint>::slot_index(const T* const slot, bool& ok) const noexcept
{
    if ((slot == nullptr) || (slot < m_arena) || (slot >= (m_arena + k_capacity)))
    {
        ok = false;
        return 0u;
    }

    ok = true;
    return static_cast<std::uint32_t>(slot - m_arena);
}

template<typename T, std::uint32_t t_capacity_hint>
inline T* TMpmcArenaTransport<T, t_capacity_hint>::reserve(std::uint32_t& out_index, std::uint32_t& out_sequence) noexcept
{
    if (!state_allows_reserve())
    {
        return nullptr;
    }

    std::uint32_t index = 0u;
    if (!m_supplier_ring.pop(index, out_sequence))
    {
        return nullptr;
    }

    m_outstanding_count.value.fetch_add(1u, std::memory_order_acq_rel);
    out_index = index;
    return &m_arena[index];
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TMpmcArenaTransport<T, t_capacity_hint>::publish(const T* const slot, std::uint32_t& out_sequence) noexcept
{
    if (!state_allows_acquire_or_complete())
    {
        return false;
    }

    bool ok = false;
    const std::uint32_t index = slot_index(slot, ok);
    if (!ok)
    {
        return false;
    }
    while (!m_populated_ring.push(index, out_sequence))
    {
        if (!state_allows_acquire_or_complete())
        {
            return false;
        }
        platform::threading::processor_relax();
    }
    return true;
}

template<typename T, std::uint32_t t_capacity_hint>
inline T* TMpmcArenaTransport<T, t_capacity_hint>::acquire(std::uint32_t& out_index, std::uint32_t& out_sequence) noexcept
{
    if (!state_allows_acquire_or_complete())
    {
        return nullptr;
    }

    std::uint32_t index = 0u;
    if (!m_populated_ring.pop(index, out_sequence))
    {
        return nullptr;
    }

    out_index = index;
    return &m_arena[index];
}

template<typename T, std::uint32_t t_capacity_hint>
inline bool TMpmcArenaTransport<T, t_capacity_hint>::recycle(const T* const slot, std::uint32_t& out_sequence) noexcept
{
    if (!state_allows_acquire_or_complete())
    {
        return false;
    }

    bool ok = false;
    const std::uint32_t index = slot_index(slot, ok);
    if (!ok)
    {
        return false;
    }
    while (!m_supplier_ring.push(index, out_sequence))
    {
        if (!state_allows_acquire_or_complete())
        {
            return false;
        }
        platform::threading::processor_relax();
    }

    const std::uint32_t previous = m_outstanding_count.value.fetch_sub(1u, std::memory_order_acq_rel);
    if (previous == 1u)
    {
        std::uint32_t expected = static_cast<std::uint32_t>(EMpmcTransportStatus::closing);
        (void)m_status_word.value.compare_exchange_strong(
            expected,
            static_cast<std::uint32_t>(EMpmcTransportStatus::closed),
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }
    return true;
}

}   // namespace threading::transports

#endif  // TMPMC_TRANSPORT_HPP_INCLUDED
