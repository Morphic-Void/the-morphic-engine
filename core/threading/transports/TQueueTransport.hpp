
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TQueueTransport.hpp
//  Author: Ritchie Brannan
//  Date:   15 Apr 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Triple-buffered SPSC sequential transport for trivially copyable T.
//
//  Defines threading::transports::TQueue<T>.
//  Also defines the concrete producer/consumer endpoint wrappers and the
//  simple one-transport bundle composition for TQueue.
// 
//  Does not provide MPMC-style queue semantics, blocking semantics,
//  or shared random access.
//
//  IMPORTANT SEMANTIC NOTE
//  -----------------------
//  post() and read() are all-or-nothing at the API boundary.
//
//  Consumer acceptance is phase-gated and only occurs through
//  refresh_readable_count() when no consumer buffer is currently owned.
//
//  Fixed-capacity mode may allow producer-side discard.
//  Growable mode may increase producer-side working capacity up to a
//  configured maximum.
//
//  Allocation failure during producer-side reallocation poisons the
//  producer side.
//
//  See docs/TQueueTransport.md for the full documentation.

#pragma once

#ifndef TQUEUE_TRANSPORT_HPP_INCLUDED
#define TQUEUE_TRANSPORT_HPP_INCLUDED

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
#include "debug/macros.hpp"

namespace threading::transports
{

//==============================================================================
//  TQueue<T>
//  Single Producer, Single Consumer (SPSC) transport
//==============================================================================

template<typename T>
class TQueue
{
private:
    static_assert(!std::is_const_v<T>, "TQueue<T> requires non-const T.");
    static_assert(std::is_trivially_copyable_v<T>, "TQueue<T> requires trivially copyable T.");

public:
    static constexpr std::uint32_t k_max_capacity = 0x00100000u;
    static constexpr std::uint32_t k_min_capacity = static_cast<std::uint32_t>(memory::k_vector_growth_policy_min_capacity);
    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();

public:
    TQueue() noexcept = default;
    explicit TQueue(memory::CMemoryContext* const context) noexcept;
    TQueue(const TQueue&) = delete;
    TQueue& operator=(const TQueue&) = delete;
    TQueue(TQueue&&) noexcept = delete;
    TQueue& operator=(TQueue&&) noexcept = delete;
    ~TQueue() noexcept { (void)deallocate(); }

    //  Producer status and operations
    //  - only safe to call from the producer thread or while quiescent
    //  - post_would_reallocate() returning false does not guarantee that no reallocation would occur
    [[nodiscard]] bool posting_is_valid() const noexcept;
    [[nodiscard]] bool posting_is_ready() const noexcept;
    [[nodiscard]] bool posting_poisoned() const noexcept;
    [[nodiscard]] bool post(const T& src) noexcept { return post(&src, 1u); }
    [[nodiscard]] bool post(const T* const src, const std::uint32_t count = 1u) noexcept;
    [[nodiscard]] bool post(const TPodConstView<T>& src) noexcept { return src.is_valid() && post(src.data(), static_cast<std::uint32_t>(src.size())); }
    [[nodiscard]] bool post_would_reallocate(const std::uint32_t count) const noexcept;

    //  Consumer status and operations
    //  - only safe to call from the consumer thread or while quiescent
    [[nodiscard]] bool reading_is_valid() const noexcept;
    [[nodiscard]] bool reading_is_ready() const noexcept;
    [[nodiscard]] bool read(T& dst) noexcept { return read(&dst, 1u); }
    [[nodiscard]] bool read(T* const dst, const std::uint32_t count = 1u) noexcept;
    [[nodiscard]] bool read(const TPodView<T>& dst) noexcept { return dst.is_valid() && read(dst.data(), static_cast<std::uint32_t>(dst.size())); }
    [[nodiscard]] std::uint32_t current_readable_count() const noexcept;
    [[nodiscard]] std::uint32_t refresh_readable_count() noexcept;

    //  Setup and teardown
    //  initialise_*() requires a deallocated / not-ready instance.
    //  deallocate() releases owned storage and must not race active role use.
    [[nodiscard]] bool initialise_fixed(const std::uint32_t capacity, const bool allow_discard = false) noexcept;
    [[nodiscard]] bool initialise_fixed(const std::uint32_t capacity, const bool allow_discard, memory::CMemoryContext* const context) noexcept;
    [[nodiscard]] bool initialise_growable(const std::uint32_t capacity, const std::uint32_t max_capacity = 0u) noexcept;
    [[nodiscard]] bool initialise_growable(const std::uint32_t capacity, const std::uint32_t max_capacity, memory::CMemoryContext* const context) noexcept;
    void deallocate() noexcept;

    //  Memory context accessor
    [[nodiscard]] memory::CMemoryContext* memory_context() const noexcept;

    //  Validation
    //  - full structural validity check
    //  - only safe to call while quiescent
    [[nodiscard]] bool validate() const noexcept;

private:
    static constexpr std::uint32_t k_token_max_capacity = static_cast<std::uint32_t>(memory::t_max_elements<T>());
    static_assert((k_element_size <= 0xffffu), "TQueue<T> element size exceeds the memory token stride field.");
    static_assert((k_element_size <= memory::k_byte_size_ceiling), "TQueue<T> element size exceeds the shared byte ceiling.");

    [[nodiscard]] bool set_memory_context(memory::CMemoryContext* const context) noexcept;
    static std::uint32_t growth_policy(const std::uint32_t capacity) noexcept;
    bool growth_and_discard_policy(const std::uint32_t count, const std::uint32_t buffer_size, std::uint32_t& target_capacity, bool& discard) const noexcept;
    bool is_canonical_empty() const noexcept;

    //  Constants
    static constexpr std::uint32_t k_null_buffer_index{ 3u };
    static constexpr std::uint32_t k_default_staged_word{ 4u };

    //  Configuration
    bool m_allow_discard = false;
    std::uint32_t m_capacity = 0u;
    std::uint32_t m_max_capacity = 0u;

    //  Allocation failure poisoning state
    bool m_allocation_failed = false;

    //  Buffer state
    struct Buffer
    {
        [[nodiscard]] T* data() noexcept;
        [[nodiscard]] const T* data() const noexcept;
        [[nodiscard]] bool storage_is_valid() const noexcept;

        memory::CMemoryToken storage{ k_element_size, k_align };
        std::uint32_t capacity = 0u;
        std::uint32_t size = 0u;
    };

    //  Buffers
    Buffer m_buffers[3];

    //  Producer state - Consumer side must not access this
    bool m_posting_phase = false;
    std::uint32_t m_posting_output_buffer_index = 0u;  //  "owned" by the producer side
    std::uint32_t m_posting_staged_buffer_index = 1u;  //  "unowned" staged
    std::uint32_t m_posting_locked_buffer_index = 2u;  //  "owned" by the consumer side

    //  Consumer state - Producer side must not access this
    bool m_reading_phase = false;
    std::uint32_t m_reading_buffer_index = k_null_buffer_index;
    std::uint32_t m_reading_read_index = 0u;

    //  Publication state
    std::atomic<std::uint32_t> m_staged_word{ k_default_staged_word };
};

//==============================================================================
//  TQueueProducerEndpoint<T>
//  Single Producer, Single Consumer (SPSC) producer endpoint
//==============================================================================

template<typename T>
class TQueueProducerEndpoint
{
public:
    explicit TQueueProducerEndpoint(TQueue<T>& queue) noexcept : m_queue{ queue } {}
    ~TQueueProducerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_queue.posting_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_queue.posting_is_ready(); }
    [[nodiscard]] bool is_poisoned() const noexcept { return m_queue.posting_poisoned(); }
    [[nodiscard]] bool post(const T& src) noexcept { return m_queue.post(src); }
    [[nodiscard]] bool post(const T* const src, const std::uint32_t count = 1u) noexcept { return m_queue.post(src, count); }
    [[nodiscard]] bool post(const TPodConstView<T>& src) noexcept { return m_queue.post(src); }
    [[nodiscard]] bool post_would_reallocate(const std::uint32_t count) const noexcept { return m_queue.post_would_reallocate(count); }

private:
    TQueue<T>& m_queue;
};

//==============================================================================
//  TQueueConsumerEndpoint<T>
//  Single Producer, Single Consumer (SPSC) consumer endpoint
//==============================================================================

template<typename T>
class TQueueConsumerEndpoint
{
public:
    explicit TQueueConsumerEndpoint(TQueue<T>& queue) noexcept : m_queue{ queue } {}
    ~TQueueConsumerEndpoint() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_queue.reading_is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_queue.reading_is_ready(); }
    [[nodiscard]] bool read(T& dst) noexcept { return m_queue.read(dst); }
    [[nodiscard]] bool read(T* const dst, const std::uint32_t count = 1u) noexcept { return m_queue.read(dst, count); }
    [[nodiscard]] bool read(const TPodView<T>& dst) noexcept { return m_queue.read(dst); }
    [[nodiscard]] std::uint32_t current_readable_count() const noexcept { return m_queue.current_readable_count(); }
    [[nodiscard]] std::uint32_t refresh_readable_count() noexcept { return m_queue.refresh_readable_count(); }

private:
    TQueue<T>& m_queue;
};

//==============================================================================
//  TQueueBundle<T>
//  Single Producer, Single Consumer (SPSC) transport bundle
//==============================================================================

template<typename T>
struct TQueueBundle
{
    TQueue<T> transport;
    TQueueProducerEndpoint<T> producer;
    TQueueConsumerEndpoint<T> consumer;

    TQueueBundle() noexcept : transport(), producer(transport), consumer(transport) {}
};

//==============================================================================
//  TQueue<T> out of class function bodies
//==============================================================================

template<typename T>
inline TQueue<T>::TQueue(memory::CMemoryContext* const context) noexcept
{
    (void)set_memory_context(context);
}

template<typename T>
inline T* TQueue<T>::Buffer::data() noexcept
{
    return static_cast<T*>(storage.data());
}

template<typename T>
inline const T* TQueue<T>::Buffer::data() const noexcept
{
    return static_cast<const T*>(storage.data());
}

template<typename T>
inline bool TQueue<T>::Buffer::storage_is_valid() const noexcept
{
    return storage.is_relocatable() &&
        (storage.stride() == k_element_size) &&
        (storage.storage_alignment() == k_align) &&
        (storage.count() == capacity);
}

template<typename T>
inline bool TQueue<T>::posting_is_valid() const noexcept
{
    if (m_max_capacity == 0u)
    {   //  uninitialised, safe to check canonical empty
        return is_canonical_empty();
    }
    if (m_allocation_failed || (m_capacity > m_max_capacity))
    {
        return false;
    }
    for (std::uint32_t buffer_index = 0u; buffer_index < 3; ++buffer_index)
    {
        const Buffer& buffer = m_buffers[buffer_index];
        if (!buffer.storage_is_valid() ||
            (buffer.data() == nullptr) ||
            (buffer.capacity < buffer.size) ||
            (buffer.capacity < k_min_capacity) ||
            (buffer.capacity > m_capacity))
        {
            return false;
        }
    }
    if ((m_posting_output_buffer_index > 2u) ||
        (m_posting_staged_buffer_index > 2u) ||
        (m_posting_locked_buffer_index > 2u))
    {
        return false;
    }
    const std::uint32_t permutation_check =
        (1u << m_posting_output_buffer_index) |
        (1u << m_posting_staged_buffer_index) |
        (1u << m_posting_locked_buffer_index);
    if (permutation_check != 7u)
    {
        return false;
    }
    return m_staged_word.load(std::memory_order_relaxed) < 8u;
}

template<typename T>
inline bool TQueue<T>::posting_is_ready() const noexcept
{
    return (!m_allocation_failed) && (m_max_capacity != 0u);
}

template<typename T>
inline bool TQueue<T>::posting_poisoned() const noexcept
{
    return m_allocation_failed;
}

template<typename T>
inline bool TQueue<T>::post(const T* const src, const std::uint32_t count) noexcept
{
    if (m_allocation_failed || (m_max_capacity == 0u) || (count > m_max_capacity) || ((src == nullptr) && (count != 0u)))
    {
        return false;
    }
    if (count != 0u)
    {
        Buffer& output_buffer = m_buffers[m_posting_output_buffer_index];
        Buffer& staged_buffer = m_buffers[m_posting_staged_buffer_index];
        Buffer& locked_buffer = m_buffers[m_posting_locked_buffer_index];

        //  Growth and buffer discard policy
        bool discard = false;
        std::uint32_t target_capacity = m_capacity;
        if (!growth_and_discard_policy(count, output_buffer.size, target_capacity, discard))
        {   //  policy will not allow this posting
            return false;
        }
        m_capacity = target_capacity;

        //  Prepare the buffer for publication
        const std::size_t byte_count = (static_cast<std::size_t>(count) * sizeof(T));
        if ((output_buffer.capacity != m_capacity) && !output_buffer.storage.reallocate(m_capacity, output_buffer.size))
        {   //  reallocation failed, the buffer is unchanged but the post() operation cannot continue
            m_allocation_failed = true;
            MV_ERROR("TQueue::post failed to reallocate the output buffer");
            return false;
        }
        output_buffer.capacity = m_capacity;
        if (discard)
        {
            output_buffer.size = 0u;
        }
        std::memcpy((output_buffer.data() + output_buffer.size), src, byte_count);
        output_buffer.size += count;

        //  Publish and classify returned staged state
        const std::uint32_t received = m_staged_word.exchange((m_posting_output_buffer_index + (m_posting_phase ? 5u : 1u)), std::memory_order_acq_rel);
        if (received == 0u)
        {   //  Staged publication already consumed: rebase and republish
            m_posting_phase = !m_posting_phase;
            if ((locked_buffer.capacity != m_capacity) && !locked_buffer.storage.reallocate(m_capacity, locked_buffer.size))
            {   //  reallocation failed, the buffer is unchanged but the post() operation cannot continue
                m_allocation_failed = true;
                MV_ERROR("TQueue::post failed to reallocate the locked buffer");
                return false;
            }
            locked_buffer.capacity = m_capacity;
            std::memcpy(output_buffer.data(), src, byte_count);  //  safe to use because the publish of it will be ignored
            std::memcpy(locked_buffer.data(), src, byte_count);  //  safe to use because it has been consumed
            output_buffer.size = locked_buffer.size = count;
            m_staged_word.store((m_posting_locked_buffer_index + (m_posting_phase ? 5u : 1u)), std::memory_order_release);
            const std::uint32_t buffer_index_swap = m_posting_locked_buffer_index;
            m_posting_locked_buffer_index = m_posting_staged_buffer_index;
            m_posting_staged_buffer_index = buffer_index_swap;
        }
        else
        {   //  Staged publication still pending: extend staged backlog
            if ((staged_buffer.capacity != m_capacity) && !staged_buffer.storage.reallocate(m_capacity, staged_buffer.size))
            {   //  reallocation failed, the buffer is unchanged but the post() operation cannot continue
                m_allocation_failed = true;
                MV_ERROR("TQueue::post failed to reallocate the staged buffer");
                return false;
            }
            staged_buffer.capacity = m_capacity;
            if (discard)
            {
                staged_buffer.size = 0u;
            }
            std::memcpy((staged_buffer.data() + staged_buffer.size), src, byte_count);
            staged_buffer.size += count;
            const std::uint32_t buffer_index_swap = m_posting_output_buffer_index;
            m_posting_output_buffer_index = m_posting_staged_buffer_index;
            m_posting_staged_buffer_index = buffer_index_swap;
        }
    }
    return true;
}

template<typename T>
bool TQueue<T>::post_would_reallocate(const std::uint32_t count) const noexcept
{
    if (m_allocation_failed || (m_max_capacity == 0u) || (count > m_max_capacity) || (count == 0u))
    {
        return false;
    }

    const Buffer& output_buffer = m_buffers[m_posting_output_buffer_index];

    bool discard = false;
    std::uint32_t target_capacity = m_capacity;
    if (!growth_and_discard_policy(count, output_buffer.size, target_capacity, discard))
    {   //  policy will not allow this posting
        return false;
    }

    return output_buffer.capacity != target_capacity;
}

template<typename T>
inline bool TQueue<T>::reading_is_valid() const noexcept
{
    if (m_max_capacity == 0u)
    {   //  uninitialised, safe to check canonical empty
        return is_canonical_empty();
    }
    if (m_reading_buffer_index != k_null_buffer_index)
    {
        if (m_reading_buffer_index > 2u)
        {
            return false;
        }
        const Buffer& buffer = m_buffers[m_reading_buffer_index];
        if (!buffer.storage_is_valid() ||
            (buffer.data() == nullptr) ||
            (buffer.capacity < buffer.size) ||
            (buffer.capacity < k_min_capacity) ||
            (buffer.size < m_reading_read_index))
        {
            return false;
        }
    }
    return m_staged_word.load(std::memory_order_relaxed) < 8u;
}

template<typename T>
inline bool TQueue<T>::reading_is_ready() const noexcept
{
    return m_max_capacity != 0u;
}

template<typename T>
inline bool TQueue<T>::read(T* const dst, const std::uint32_t count) noexcept
{
    if ((m_max_capacity == 0u) || (count > m_max_capacity) || ((dst == nullptr) && (count != 0u)))
    {
        return false;
    }
    if (count != 0u)
    {
        const std::uint32_t available = refresh_readable_count();
        if (count > available)
        {
            return false;
        }
        Buffer& buffer = m_buffers[m_reading_buffer_index];
        std::memcpy(dst, (buffer.data() + m_reading_read_index), (static_cast<std::size_t>(count) * sizeof(T)));
        m_reading_read_index += count;
        if (m_reading_read_index == buffer.size)
        {   //  buffer exhausted
            m_reading_buffer_index = k_null_buffer_index;
            m_reading_read_index = 0u;
        }
    }
    return true;
}

template<typename T>
inline std::uint32_t TQueue<T>::current_readable_count() const noexcept
{
    if (m_reading_buffer_index != k_null_buffer_index)
    {
        const Buffer& buffer = m_buffers[m_reading_buffer_index];
        if (buffer.size >= m_reading_read_index)
        {
            return buffer.size - m_reading_read_index;
        }
    }
    return 0u;
}

template<typename T>
inline std::uint32_t TQueue<T>::refresh_readable_count() noexcept
{
    if (m_reading_buffer_index == k_null_buffer_index)
    {
        const std::uint32_t pre_check = m_staged_word.load(std::memory_order_acquire);
        if ((pre_check & 3u) == 0u)
        {   //  no published buffer id: either initial sentinel or already-consumed state
            return 0u;
        }
        const std::uint32_t received = m_staged_word.exchange(0u, std::memory_order_acq_rel);
        const std::uint32_t received_id = received & 3u;
        const bool received_phase = (received & 4u) != 0u;
        if ((received_id == 0u) || (received_phase != m_reading_phase))
        {   //  invalid buffer or phase
            return 0u;
        }
        m_reading_phase = !received_phase;
        m_reading_buffer_index = received_id - 1u;
        m_reading_read_index = 0u;
    }
    return current_readable_count();
}

template<typename T>
inline bool TQueue<T>::initialise_fixed(const std::uint32_t capacity, const bool allow_discard, memory::CMemoryContext* const context) noexcept
{
    if ((capacity > k_max_capacity) || (capacity > k_token_max_capacity))
    {   //  requested capacity not supported
        return false;
    }
    if (m_max_capacity != 0u)
    {   //  re-initialisation is not allowed without deallocation
        return false;
    }
    if (!set_memory_context(context))
    {
        return false;
    }
    const std::uint32_t conditioned_capacity = std::max(
        std::min((bit_ops::is_pow2(capacity) ? capacity : growth_policy(capacity)), std::min(k_max_capacity, k_token_max_capacity)),
        k_min_capacity);
    m_allow_discard = allow_discard;
    m_capacity = m_max_capacity = conditioned_capacity;
    for (std::uint32_t buffer_index = 0u; buffer_index < 3; ++buffer_index)
    {
        Buffer& buffer = m_buffers[buffer_index];
        if (!buffer.storage.allocate(conditioned_capacity))
        {
            deallocate();
            return false;
        }
        buffer.capacity = conditioned_capacity;
        buffer.size = 0u;
    }
    m_posting_phase = m_reading_phase = false;
    m_posting_output_buffer_index = 0u;
    m_posting_staged_buffer_index = 1u;
    m_posting_locked_buffer_index = 2u;
    m_reading_buffer_index = k_null_buffer_index;
    m_reading_read_index = 0u;
    m_staged_word.store(k_default_staged_word, std::memory_order_release);
    return true;
}

template<typename T>
inline bool TQueue<T>::initialise_fixed(const std::uint32_t capacity, const bool allow_discard) noexcept
{
    return initialise_fixed(capacity, allow_discard, memory_context());
}

template<typename T>
inline bool TQueue<T>::initialise_growable(const std::uint32_t capacity, const std::uint32_t max_capacity, memory::CMemoryContext* const context) noexcept
{
    if ((capacity > k_token_max_capacity) ||
        (max_capacity > k_max_capacity) ||
        (max_capacity > k_token_max_capacity) ||
        ((capacity > max_capacity) && (max_capacity != 0u)))
    {   //  requested capacity not supported
        return false;
    }
    if (!initialise_fixed(capacity, false, context))
    {
        return false;
    }
    if ((max_capacity == 0u) || (max_capacity != capacity))
    {   //  An omitted maximum requests growth, even from an initial hint of zero.
        m_max_capacity = (max_capacity == 0u)
            ? std::min(k_max_capacity, k_token_max_capacity)
            : std::max(max_capacity, m_capacity);
    }
    return true;
}

template<typename T>
inline bool TQueue<T>::initialise_growable(const std::uint32_t capacity, const std::uint32_t max_capacity) noexcept
{
    return initialise_growable(capacity, max_capacity, memory_context());
}

template<typename T>
inline void TQueue<T>::deallocate() noexcept
{
    m_allow_discard = false;
    m_capacity = m_max_capacity = 0u;
    m_allocation_failed = false;
    for (std::uint32_t buffer_index = 0u; buffer_index < 3; ++buffer_index)
    {
        Buffer& buffer = m_buffers[buffer_index];
        buffer.storage.deallocate();
        buffer.capacity = buffer.size = 0u;
    }
    m_posting_phase = m_reading_phase = false;
    m_posting_output_buffer_index = 0u;
    m_posting_staged_buffer_index = 1u;
    m_posting_locked_buffer_index = 2u;
    m_reading_buffer_index = k_null_buffer_index;
    m_reading_read_index = 0u;
    m_staged_word.store(k_default_staged_word, std::memory_order_relaxed);
}

template<typename T>
inline memory::CMemoryContext* TQueue<T>::memory_context() const noexcept
{
    memory::CMemoryContext* const context = m_buffers[0].storage.context();
    if ((context == nullptr) ||
        (m_buffers[1].storage.context() != context) ||
        (m_buffers[2].storage.context() != context))
    {
        return nullptr;
    }
    return context;
}

template<typename T>
inline bool TQueue<T>::set_memory_context(memory::CMemoryContext* const context) noexcept
{
    return
        m_buffers[0].storage.set_context(context) &&
        m_buffers[1].storage.set_context(context) &&
        m_buffers[2].storage.set_context(context);
}

template<typename T>
inline bool TQueue<T>::validate() const noexcept
{
    return posting_is_valid() && reading_is_valid() && ((m_reading_buffer_index == m_posting_locked_buffer_index) || (m_reading_buffer_index == k_null_buffer_index));
}

template<typename T>
inline std::uint32_t TQueue<T>::growth_policy(const std::uint32_t capacity) noexcept
{
    return static_cast<std::uint32_t>(memory::vector_growth_policy(
        static_cast<std::size_t>(capacity),
        static_cast<std::size_t>(std::min(k_max_capacity, k_token_max_capacity))));
}

template<typename T>
inline bool TQueue<T>::growth_and_discard_policy(const std::uint32_t count, const std::uint32_t buffer_size, std::uint32_t& target_capacity, bool& discard) const noexcept
{
    discard = false;
    target_capacity = m_capacity;
    if (count <= (m_capacity - buffer_size))
    {
        return true;
    }
    const std::uint32_t capacity = std::min(std::max(growth_policy(buffer_size + count), m_capacity), m_max_capacity);
    if (count <= (capacity - buffer_size))
    {   //  buffer growth can accomodate the new posting
        target_capacity = capacity;
        return true;
    }
    if (m_allow_discard)
    {   //  buffer discard and growth can accomodate the new posting
        if (count > m_capacity)
        {
            target_capacity = std::min(std::max(growth_policy(count), m_capacity), m_max_capacity);
        }
        discard = true;
        return true;
    }
    return false;
}

template<typename T>
inline bool TQueue<T>::is_canonical_empty() const noexcept
{
    if (m_allocation_failed || (m_max_capacity != 0u))
    {
        return false;
    }
    if ((m_capacity != 0u) || m_allow_discard || m_posting_phase || m_reading_phase ||
        (m_posting_output_buffer_index != 0u) ||
        (m_posting_staged_buffer_index != 1u) ||
        (m_posting_locked_buffer_index != 2u) ||
        (m_reading_buffer_index != k_null_buffer_index) ||
        (m_reading_read_index != 0u))
    {
        return false;
    }
    for (std::uint32_t buffer_index = 0u; buffer_index < 3; ++buffer_index)
    {
        const Buffer& buffer = m_buffers[buffer_index];
        if ((buffer.data() != nullptr) || ((buffer.capacity | buffer.size) != 0u))
        {
            return false;
        }
    }
    return m_staged_word.load(std::memory_order_relaxed) == k_default_staged_word;
}

}   //  namespace threading::transports

#endif  //  #ifndef TQUEUE_TRANSPORT_HPP_INCLUDED
