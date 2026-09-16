
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TPodFifo.hpp
//  Author: Ritchie Brannan
//  Date:   20 Apr 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Growable FIFO storage for trivially copyable T.
//
//  Does not provide transport semantics, blocking semantics, or
//  random-access array semantics.
//
//  IMPORTANT SEMANTIC NOTE
//  -----------------------
//  push_back() and pop_front() are all-or-nothing.
//
//  data() exposes backing storage, not necessarily a packed logical FIFO
//  sequence unless pack() has been performed.
//
//  reallocate() may normalise layout even when capacity is unchanged.
//
//  See docs/containers/TPodFifo.md for the full documentation.

#pragma once

#ifndef TPOD_FIFO_HPP_INCLUDED
#define TPOD_FIFO_HPP_INCLUDED

#include <algorithm>    //  std::min, std::max
#include <cstddef>      //  std::size_t
#include <cstring>      //  std::memcpy, std::memmove
#include <type_traits>  //  std::is_const_v, std::is_trivially_copyable_v
#include <utility>      //  std::move

#include "bit_utils/bit_ops.hpp"
#include "containers/TPodVector.hpp"
#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"

//==============================================================================
//  TPodFifo<T>
//  Owning unique FIFO container.
//==============================================================================

template<typename T>
class TPodFifo
{
private:
    static_assert(!std::is_const_v<T>, "TPodFifo<T> requires non-const T.");
    static_assert(std::is_trivially_copyable_v<T>, "TPodFifo<T> requires trivially copyable T.");

public:
    TPodFifo() noexcept = default;
    TPodFifo(const TPodFifo&) noexcept = delete;
    TPodFifo& operator=(const TPodFifo&) noexcept = delete;

    //  Move lifetime
    TPodFifo(TPodFifo&&) noexcept;
    TPodFifo& operator=(TPodFifo&&) noexcept;

    //  Destructor
    ~TPodFifo() noexcept { deallocate(); }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Accessors
    [[nodiscard]] T* data() noexcept { return is_ready() ? raw_data() : nullptr; }
    [[nodiscard]] const T* data() const noexcept { return is_ready() ? raw_data() : nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return is_ready() ? m_size : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t capacity() const noexcept { return is_ready() ? m_capacity : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t available() const noexcept { return is_ready() ? (m_capacity - m_size) : std::size_t{ 0 }; }

    //  Operations
    [[nodiscard]] bool push_back(const T& src) noexcept { return push_back(&src, 1u); }
    [[nodiscard]] bool push_back(const T* const src, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool push_back(const TPodConstView<T>& src) noexcept { return src.is_valid() && push_back(src.data(), src.size()); }
    [[nodiscard]] bool pop_front(T& dst) noexcept { return pop_front(&dst, 1u); }
    [[nodiscard]] bool pop_front(T* const dst, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool pop_front(const TPodView<T>& dst) noexcept { return dst.is_valid() && pop_front(dst.data(), dst.size()); }
    [[nodiscard]] bool discard_front(const std::size_t count = 1u) noexcept;

    //  Allocation and capacity management
    [[nodiscard]] bool allocate(const std::size_t capacity) noexcept;
    [[nodiscard]] bool reallocate(const std::size_t capacity) noexcept;
    [[nodiscard]] bool reserve(const std::size_t minimum_capacity) noexcept;
    [[nodiscard]] bool ensure_free(const std::size_t extra) noexcept;
    [[nodiscard]] bool shrink_to_fit() noexcept;
    void deallocate() noexcept;

    //  Mutator
    void pack() noexcept;

    //  Constants
    static constexpr std::size_t k_max_elements = memory::t_max_elements<T>();
    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();

//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe all owned backing storage without changing its attribution.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace all owned contexts, including unallocated members.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

private:
    [[nodiscard]] T* raw_data() noexcept { return static_cast<T*>(m_token.data()); }
    [[nodiscard]] const T* raw_data() const noexcept { return static_cast<const T*>(m_token.data()); }
    [[nodiscard]] bool is_internal_ptr(const T* const ptr) const noexcept;
    void pack(T* const dst, const T* const src) noexcept;
    void move_from(TPodFifo& src) noexcept;

    static_assert((k_element_size <= 0xffffu), "TPodFifo<T> element size exceeds the memory token stride field.");

    memory::CMemoryToken m_token{ k_element_size, k_align };
    std::size_t m_size = 0u;
    std::size_t m_capacity = 0u;
    std::size_t m_read_index = 0u;
};

//==============================================================================
//  TPodFifo<T> out of class function bodies
//==============================================================================

template<typename T>
inline TPodFifo<T>::TPodFifo(TPodFifo&& src) noexcept
{
    move_from(src);
}

template<typename T>
inline TPodFifo<T>& TPodFifo<T>::operator=(TPodFifo&& src) noexcept
{
    if (this != &src)
    {
        move_from(src);
    }
    return *this;
}

template<typename T>
inline bool TPodFifo<T>::is_valid() const noexcept
{
    if (!m_token.is_relocatable() ||
        (m_token.stride() != k_element_size) || (m_token.storage_alignment() != k_align) ||
        (m_token.count() != m_capacity))
    {
        return false;
    }

    return (raw_data() != nullptr) ?
        ((m_size <= m_capacity) && memory::in_non_empty_range(m_capacity, k_max_elements) && (m_read_index < m_capacity) && ((m_size != 0u) || (m_read_index == 0u))) :
        ((m_size | m_capacity | m_read_index) == 0u);
}

template<typename T>
inline bool TPodFifo<T>::is_empty() const noexcept
{
    return (raw_data() == nullptr) || (m_size == 0u) || (m_capacity == 0u) || (m_read_index >= m_capacity);
}

template<typename T>
inline bool TPodFifo<T>::is_ready() const noexcept
{
    return is_valid() && (raw_data() != nullptr);
}

template<typename T>
inline bool TPodFifo<T>::push_back(const T* const src, const std::size_t count) noexcept
{
    if (count == 0u)
    {
        return true;
    }
    if (!is_ready() || (src == nullptr) || is_internal_ptr(src) || (count > available()))
    {
        return false;
    }
    const std::size_t index_check = m_read_index + m_size;
    const std::size_t write_index = (index_check >= m_capacity) ? (index_check - m_capacity) : index_check;
    const std::size_t tail_size = m_capacity - write_index;
    if (count <= tail_size)
    {
        std::memcpy((data() + write_index), src, (count * sizeof(T)));
    }
    else
    {
        std::memcpy((data() + write_index), src, (tail_size * sizeof(T)));
        std::memcpy(data(), (src + tail_size), ((count - tail_size) * sizeof(T)));
    }
    m_size += count;
    return true;
}

template<typename T>
inline bool TPodFifo<T>::pop_front(T* const dst, const std::size_t count) noexcept
{
    if (count == 0u)
    {
        return true;
    }
    if (!is_ready() || (dst == nullptr) || is_internal_ptr(dst) || (count > size()))
    {
        return false;
    }
    const std::size_t tail_size = m_capacity - m_read_index;
    if (count <= tail_size)
    {
        std::memcpy(dst, (data() + m_read_index), (count * sizeof(T)));
        m_read_index = (count != tail_size) ? (m_read_index + count) : 0u;
    }
    else
    {
        std::memcpy(dst, (data() + m_read_index), (tail_size * sizeof(T)));
        m_read_index = count - tail_size;
        std::memcpy((dst + tail_size), data(), (m_read_index * sizeof(T)));
    }
    m_size -= count;
    if (m_size == 0u)
    {
        m_read_index = 0u;
    }
    return true;
}

template<typename T>
inline bool TPodFifo<T>::discard_front(const std::size_t count) noexcept
{
    if (count == 0u)
    {
        return true;
    }
    if (!is_ready() || (count > size()))
    {
        return false;
    }
    m_read_index += count;
    if (m_read_index >= m_capacity)
    {
        m_read_index -= m_capacity;
    }
    m_size -= count;
    if (m_size == 0u)
    {
        m_read_index = 0u;
    }
    return true;
}

template<typename T>
inline bool TPodFifo<T>::allocate(const std::size_t capacity) noexcept
{
    if ((capacity <= k_max_elements) && ((capacity == m_capacity) || m_token.allocate(capacity, false)))
    {
        m_size = 0u;
        m_capacity = capacity;
        m_read_index = 0u;
        return true;
    }
    return false;
}

template<typename T>
inline bool TPodFifo<T>::reallocate(const std::size_t capacity) noexcept
{
    if ((capacity >= m_size) && (capacity <= k_max_elements))
    {
        if (capacity == m_capacity)
        {
            T* const packed = data();
            pack(packed, packed);
            return true;
        }

        memory::CMemoryToken token{ k_element_size, k_align };
        if (token.allocate(capacity, false))
        {
            pack(static_cast<T*>(token.data()), data());
            m_token = std::move(token);
            m_capacity = capacity;
            return true;
        }
    }
    return false;
}

template<typename T>
inline bool TPodFifo<T>::reserve(const std::size_t minimum_capacity) noexcept
{
    return (minimum_capacity <= k_max_elements) ?
        ((minimum_capacity > m_capacity) ?
            reallocate(std::max(memory::vector_growth_policy(minimum_capacity, k_max_elements), m_capacity)) :
            true) :
        false;
}

template<typename T>
inline bool TPodFifo<T>::ensure_free(const std::size_t extra) noexcept
{
    return (extra <= (k_max_elements - m_size)) ? reserve(m_size + extra) : false;
}

template<typename T>
inline bool TPodFifo<T>::shrink_to_fit() noexcept
{
    if (m_size == 0u)
    {
        deallocate();
        return true;
    }
    return reallocate(m_size);
}

template<typename T>
inline void TPodFifo<T>::deallocate() noexcept
{
    m_token.deallocate();
    m_size = 0u;
    m_capacity = 0u;
    m_read_index = 0u;
}

template<typename T>
inline bool TPodFifo<T>::is_internal_ptr(const T* const ptr) const noexcept
{
    return pod_storage_contains_ptr(raw_data(), m_capacity, ptr);
}

template<typename T>
inline void TPodFifo<T>::pack() noexcept
{
    if (is_ready())
    {
        T* const packed = data();
        pack(packed, packed);
    }
}

template<typename T>
inline memory::SMemoryAttribution TPodFifo<T>::memory_attribution() const noexcept
{
    return memory::observe_memory_attribution(m_token);
}

template<typename T>
inline void TPodFifo<T>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
{
    m_token.unsafe_replace_context_without_accounting(expected_source, target);
}

template<typename T>
inline void TPodFifo<T>::pack(T* const dst, const T* const src) noexcept
{
    if (m_size != 0u)
    {
        const std::size_t tail_size = m_capacity - m_read_index;
        if (m_size <= tail_size)
        {
            const std::size_t body_bytes = m_size * sizeof(T);
            if ((m_read_index >= m_size) || (src != dst))
            {
                std::memcpy(dst, (src + m_read_index), body_bytes);
            }
            else
            {
                std::memmove(dst, (src + m_read_index), body_bytes);
            }
        }
        else
        {
            const std::size_t head_bytes = (m_read_index + m_size - m_capacity) * sizeof(T);
            const std::size_t tail_bytes = tail_size * sizeof(T);
            if (src != dst)
            {
                std::memcpy((dst + tail_size), src, head_bytes);
                std::memcpy(dst, (src + m_read_index), tail_bytes);
            }
            else if (m_size <= m_read_index)
            {
                // Save the wrapped head in its final range before moving the
                // unread tail; this condition keeps those ranges disjoint.
                std::memmove((dst + tail_size), src, head_bytes);
                std::memmove(dst, (src + m_read_index), tail_bytes);
            }
            else
            {
                // Rotate the complete backing range so wrapped logical elements
                // become the packed prefix without requiring T assignment.
                const std::size_t cycle_count = bit_ops::highest_common_factor(m_capacity, m_read_index);
                for (std::size_t cycle = 0u; cycle < cycle_count; ++cycle)
                {
                    std::byte temporary[sizeof(T)];
                    std::memcpy(temporary, (dst + cycle), sizeof(T));

                    std::size_t current = cycle;
                    while (true)
                    {
                        std::size_t next = current + m_read_index;
                        if (next >= m_capacity)
                        {
                            next -= m_capacity;
                        }
                        if (next == cycle)
                        {
                            break;
                        }
                        std::memcpy((dst + current), (dst + next), sizeof(T));
                        current = next;
                    }
                    std::memcpy((dst + current), temporary, sizeof(T));
                }
            }
        }
    }
    m_read_index = 0u;
}

template<typename T>
inline void TPodFifo<T>::move_from(TPodFifo& src) noexcept
{
    m_token = std::move(src.m_token);
    m_size = src.m_size;
    m_capacity = src.m_capacity;
    m_read_index = src.m_read_index;
    src.m_size = 0u;
    src.m_capacity = 0u;
    src.m_read_index = 0u;
}

#endif  //  #ifndef TPOD_FIFO_HPP_INCLUDED
