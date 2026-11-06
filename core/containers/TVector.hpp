
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TVector.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Move-only contiguous ownership of explicitly constructed objects.
//  Requires C++17, nonthrowing move construction and destruction; no exceptions.
//  Spare capacity is raw storage. Reallocation moves objects, not their bytes.
//  Attribution covers backing storage only, excluding allocations owned by T.
//  See docs/containers/TVector.md for lifetime, aliasing and failure contracts.

#pragma once

#ifndef TVECTOR_HPP_INCLUDED
#define TVECTOR_HPP_INCLUDED

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"
#include "debug/macros.hpp"

template<typename T>
class TVector
{
private:
    static_assert((std::is_object_v<T> && !std::is_array_v<T>), "TVector<T> requires a non-array object type.");
    static_assert((!std::is_const_v<T> && !std::is_volatile_v<T>), "TVector<T> requires an unqualified element type.");
    static_assert(std::is_nothrow_move_constructible_v<T>, "TVector<T> requires nonthrowing move construction.");
    static_assert(std::is_nothrow_destructible_v<T>, "TVector<T> requires nonthrowing destruction.");

public:
    //  Default, deleted copy, and move lifetime
    TVector() noexcept = default;
    TVector(const TVector&) = delete;
    TVector& operator=(const TVector&) = delete;
    TVector(TVector&& src) noexcept;
    TVector& operator=(TVector&& src) noexcept;
    ~TVector() noexcept { deallocate(); }

    //  Status and storage. Only [0, size()) contains live objects.
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return m_size == 0u; }
    [[nodiscard]] bool is_ready() const noexcept { return is_valid() && (raw_data() != nullptr); }
    [[nodiscard]] T* data() noexcept { return is_ready() ? raw_data() : nullptr; }
    [[nodiscard]] const T* data() const noexcept { return is_ready() ? raw_data() : nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return is_ready() ? m_size : std::size_t{ 0u }; }
    [[nodiscard]] std::size_t capacity() const noexcept { return is_ready() ? m_capacity : std::size_t{ 0u }; }
    [[nodiscard]] std::size_t available() const noexcept { return is_ready() ? (m_capacity - m_size) : std::size_t{ 0u }; }

    //  Checked access returns null; reference access requires a live element.
    [[nodiscard]] T* get(const std::size_t index) noexcept;
    [[nodiscard]] const T* get(const std::size_t index) const noexcept;
    [[nodiscard]] T& operator[](const std::size_t index) noexcept;
    [[nodiscard]] const T& operator[](const std::size_t index) const noexcept;
    [[nodiscard]] T& first() noexcept { return (*this)[0u]; }
    [[nodiscard]] const T& first() const noexcept { return (*this)[0u]; }
    [[nodiscard]] T& last() noexcept { return (*this)[m_size - 1u]; }
    [[nodiscard]] const T& last() const noexcept { return (*this)[m_size - 1u]; }

    //  Single-element insertion supports arguments referring to live elements.
    template<typename... TArgs> [[nodiscard]] T* emplace_back(TArgs&&... args) noexcept;
    template<typename... TArgs> [[nodiscard]] T* emplace(const std::size_t index, TArgs&&... args) noexcept;
    [[nodiscard]] bool push_back(T&& item) noexcept { return emplace_back(std::move(item)) != nullptr; }
    [[nodiscard]] bool push_back(const T& item) noexcept { return emplace_back(item) != nullptr; }
    [[nodiscard]] bool insert(const std::size_t index, T&& item) noexcept { return emplace(index, std::move(item)) != nullptr; }
    [[nodiscard]] bool insert(const std::size_t index, const T& item) noexcept { return emplace(index, item) != nullptr; }

    //  Removal destroys objects and retains capacity.
    [[nodiscard]] bool erase(const std::size_t index, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool discard_back(const std::size_t count = 1u) noexcept;
    void clear() noexcept;

    //  Exact capacity: allocate clears on success; reallocate preserves size.
    [[nodiscard]] bool allocate(const std::size_t capacity) noexcept;
    [[nodiscard]] bool reallocate(const std::size_t capacity) noexcept;
    [[nodiscard]] bool resize(const std::size_t size) noexcept;
    [[nodiscard]] bool reserve(const std::size_t minimum_capacity) noexcept;
    [[nodiscard]] bool ensure_free(const std::size_t extra) noexcept;
    [[nodiscard]] bool shrink_to_fit() noexcept { return reallocate(m_size); }
    void deallocate() noexcept;

    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();
    static constexpr std::size_t k_max_elements = memory::t_max_elements<T>();

//  Interface for memory accounting and ownership-transfer infrastructure.
public:
    //  Observe direct backing storage only; contained owners are excluded.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace the backing context, including when no storage is allocated.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

private:
    [[nodiscard]] T* raw_data() noexcept { return static_cast<T*>(m_token.data()); }
    [[nodiscard]] const T* raw_data() const noexcept { return static_cast<const T*>(m_token.data()); }
    void move_from(TVector& src) noexcept;
    static void relocate(T* const destination, T* const source, const std::size_t count) noexcept;

    static_assert((k_element_size <= 0xffffu), "TVector<T> element size exceeds the memory token stride field.");

    memory::CMemoryToken m_token{ k_element_size, k_align };
    std::size_t m_size = 0u;
    std::size_t m_capacity = 0u;
};

//==============================================================================
//  TVector<T> out of class function bodies
//==============================================================================

template<typename T>
inline TVector<T>::TVector(TVector&& src) noexcept
{
    move_from(src);
}

template<typename T>
inline TVector<T>& TVector<T>::operator=(TVector&& src) noexcept
{
    if (this != &src)
    {
        deallocate();
        move_from(src);
    }
    return *this;
}

template<typename T>
inline void TVector<T>::move_from(TVector& src) noexcept
{
    m_token = std::move(src.m_token);
    m_size = src.m_size;
    m_capacity = src.m_capacity;
    src.m_size = 0u;
    src.m_capacity = 0u;
}

template<typename T>
inline bool TVector<T>::is_valid() const noexcept
{
    if (!m_token.is_relocatable() || (m_token.stride() != k_element_size) ||
        (m_token.storage_alignment() != k_align) || (m_token.count() != m_capacity))
    {
        return false;
    }
    return (raw_data() != nullptr) ?
        ((m_size <= m_capacity) && memory::in_non_empty_range(m_capacity, k_max_elements)) :
        ((m_size | m_capacity) == 0u);
}

template<typename T>
inline T* TVector<T>::get(const std::size_t index) noexcept
{
    return (is_ready() && (index < m_size)) ? (raw_data() + index) : nullptr;
}

template<typename T>
inline const T* TVector<T>::get(const std::size_t index) const noexcept
{
    return (is_ready() && (index < m_size)) ? (raw_data() + index) : nullptr;
}

template<typename T>
inline T& TVector<T>::operator[](const std::size_t index) noexcept
{
    T* const element = get(index);
    MV_ASSERT_MSG((element != nullptr), "TVector access requires a live element");
    return *element;
}

template<typename T>
inline const T& TVector<T>::operator[](const std::size_t index) const noexcept
{
    const T* const element = get(index);
    MV_ASSERT_MSG((element != nullptr), "TVector access requires a live element");
    return *element;
}

template<typename T>
template<typename... TArgs>
inline T* TVector<T>::emplace_back(TArgs&&... args) noexcept
{
    return emplace(m_size, std::forward<TArgs>(args)...);
}

template<typename T>
template<typename... TArgs>
inline T* TVector<T>::emplace(const std::size_t index, TArgs&&... args) noexcept
{
    static_assert(std::is_nothrow_constructible_v<T, TArgs&&...>,
        "TVector<T>::emplace(...) requires nonthrowing construction from its arguments.");

    if (!is_valid() || (index > m_size) || (m_size == k_max_elements))
    {
        return nullptr;
    }

    T* const elements = raw_data();
    if (m_size == m_capacity)
    {
        const std::size_t capacity = memory::vector_growth_policy((m_size + 1u), k_max_elements);
        memory::CMemoryToken replacement{ k_element_size, k_align, m_token.context() };
        if (!replacement.allocate(capacity, false))
        {
            return nullptr;
        }
        T* const destination = static_cast<T*>(replacement.data());
        //  Consume aliases before moving any source object. Allocation is the
        //  only recoverable failure and has already succeeded.
        T* const inserted = ::new (static_cast<void*>(destination + index)) T(std::forward<TArgs>(args)...);
        for (std::size_t i = 0u; i < m_size; ++i)
        {
            const std::size_t target = (i < index) ? i : (i + 1u);
            ::new (static_cast<void*>(destination + target)) T(std::move(elements[i]));
            elements[i].~T();
        }
        m_token = std::move(replacement);
        m_capacity = capacity;
        ++m_size;
        return inserted;
    }

    if (index == m_size)
    {
        T* const inserted = ::new (static_cast<void*>(elements + index)) T(std::forward<TArgs>(args)...);
        ++m_size;
        return inserted;
    }

    //  Stage aliased values and subobjects before shifting the live suffix.
    T staged(std::forward<TArgs>(args)...);
    for (std::size_t i = m_size; i > index; --i)
    {
        ::new (static_cast<void*>(elements + i)) T(std::move(elements[i - 1u]));
        elements[i - 1u].~T();
    }
    T* const inserted = ::new (static_cast<void*>(elements + index)) T(std::move(staged));
    ++m_size;
    return inserted;
}

template<typename T>
inline bool TVector<T>::erase(const std::size_t index, const std::size_t count) noexcept
{
    if (!is_valid() || (index > m_size) || (count > (m_size - index)))
    {
        return false;
    }
    if (count == 0u) return true;

    T* const elements = raw_data();
    for (std::size_t i = index; i < (index + count); ++i)
    {
        elements[i].~T();
    }
    for (std::size_t i = index + count; i < m_size; ++i)
    {
        ::new (static_cast<void*>(elements + (i - count))) T(std::move(elements[i]));
        elements[i].~T();
    }
    m_size -= count;
    return true;
}

template<typename T>
inline bool TVector<T>::discard_back(const std::size_t count) noexcept
{
    if (!is_valid() || (count > m_size))
    {
        return false;
    }
    const std::size_t size = m_size - count;
    while (m_size > size)
    {
        --m_size;
        raw_data()[m_size].~T();
    }
    return true;
}

template<typename T>
inline void TVector<T>::clear() noexcept
{
    while (m_size != 0u)
    {
        --m_size;
        raw_data()[m_size].~T();
    }
}

template<typename T>
inline void TVector<T>::relocate(T* const destination, T* const source, const std::size_t count) noexcept
{
    for (std::size_t i = 0u; i < count; ++i)
    {
        ::new (static_cast<void*>(destination + i)) T(std::move(source[i]));
        source[i].~T();
    }
}

template<typename T>
inline bool TVector<T>::allocate(const std::size_t capacity) noexcept
{
    if (!is_valid() || (capacity > k_max_elements))
    {
        return false;
    }
    if (capacity == m_capacity)
    {
        clear();
        return true;
    }
    if (capacity == 0u)
    {
        deallocate();
        return true;
    }
    memory::CMemoryToken replacement{ k_element_size, k_align, m_token.context() };
    if (!replacement.allocate(capacity, false))
    {
        return false;
    }
    clear();
    m_token = std::move(replacement);
    m_capacity = capacity;
    return true;
}

template<typename T>
inline bool TVector<T>::reallocate(const std::size_t capacity) noexcept
{
    if (!is_valid() || (capacity < m_size) || (capacity > k_max_elements))
    {
        return false;
    }
    if (capacity == m_capacity) return true;
    if (capacity == 0u)
    {
        deallocate();
        return true;
    }
    memory::CMemoryToken replacement{ k_element_size, k_align, m_token.context() };
    if (!replacement.allocate(capacity, false))
    {
        return false;
    }
    relocate(static_cast<T*>(replacement.data()), raw_data(), m_size);
    m_token = std::move(replacement);
    m_capacity = capacity;
    return true;
}

template<typename T>
inline bool TVector<T>::resize(const std::size_t size) noexcept
{
    static_assert(std::is_nothrow_default_constructible_v<T>,
        "TVector<T>::resize(...) requires nonthrowing default construction.");
    if (!reserve(size)) return false;
    if (size < m_size) return discard_back(m_size - size);

    while (m_size < size)
    {
        ::new (static_cast<void*>(raw_data() + m_size)) T();
        ++m_size;
    }
    return true;
}

template<typename T>
inline bool TVector<T>::reserve(const std::size_t minimum_capacity) noexcept
{
    if (!is_valid() || (minimum_capacity > k_max_elements))
    {
        return false;
    }
    return (minimum_capacity <= m_capacity) ? true :
        reallocate(memory::vector_growth_policy(minimum_capacity, k_max_elements));
}

template<typename T>
inline bool TVector<T>::ensure_free(const std::size_t extra) noexcept
{
    return (extra <= (k_max_elements - m_size)) ? reserve(m_size + extra) : false;
}

template<typename T>
inline void TVector<T>::deallocate() noexcept
{
    clear();
    m_token.deallocate();
    m_capacity = 0u;
}

template<typename T>
inline memory::SMemoryAttribution TVector<T>::memory_attribution() const noexcept
{
    return memory::observe_memory_attribution(m_token);
}

template<typename T>
inline void TVector<T>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_token.unsafe_replace_context_without_accounting(expected_source, target);
}

#endif  //  TVECTOR_HPP_INCLUDED
