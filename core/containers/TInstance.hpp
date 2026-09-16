
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TInstance.hpp
//  Author: Ritchie Brannan
//  Date:   01 Apr 26
//
//  Move-only owning single-object wrapper over a relocatable memory token.
//
//  TInstance<T> is the unique owning wrapper for a single constructed T.
//  Non-empty state implies exactly one live object.
//
//  Overview:
//  - TInstance<T> owns storage for exactly one T.
//  - Non-empty state always implies that exactly one live T object is present.
//  - Construction is fused with acquisition.
//  - Destruction destroys the object and then releases the storage.
//  - Owner identity may remain stable across emplace(...).
//  - Object identity is not stable across emplace(...).
//
//  Scope:
//  - Single-object ownership only.
//  - No arrays.
//  - No custom deleters.
//  - No raw-pointer ownership release.
//  - No exposed allocated-but-unconstructed state.
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - T must be nothrow destructible.
//  - create(...) and emplace(...) require T to be nothrow constructible
//    from the supplied arguments.

#pragma once

#ifndef TINSTANCE_HPP_INCLUDED
#define TINSTANCE_HPP_INCLUDED

#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"

#include "debug/macros.hpp"

template<typename T>
class TInstance final
{
    static_assert(!std::is_array_v<T>, "TInstance<T> does not support array types.");
    static_assert(!std::is_reference_v<T>, "TInstance<T> does not support reference types.");
    static_assert(!std::is_const_v<T>, "TInstance<T> should not own const-qualified types.");
    static_assert(!std::is_volatile_v<T>, "TInstance<T> should not own volatile-qualified types.");
    static_assert(std::is_nothrow_destructible_v<T>, "TInstance<T> requires T to be nothrow destructible.");
    static_assert(sizeof(T) <= memory::k_byte_size_ceiling, "TInstance<T> element size exceeds the shared byte ceiling.");

public:

    //  Default and deleted lifetime
    TInstance() noexcept = default;
    TInstance(const TInstance&) = delete;
    TInstance& operator=(const TInstance&) = delete;

    //  Move lifetime
    TInstance(TInstance&& src) noexcept : m_token(std::move(src.m_token)) {}
    TInstance& operator=(TInstance&&) noexcept;

    //  Destructor
    ~TInstance() noexcept { destroy_and_deallocate(); }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return m_token.is_empty(); }
    [[nodiscard]] bool is_ready() const noexcept { return is_valid() && (object_ptr() != nullptr); }
    [[nodiscard]] explicit operator bool() const noexcept { return is_ready(); }

    //  Accessors
    [[nodiscard]] T& operator*() noexcept;
    [[nodiscard]] const T& operator*() const noexcept;
    [[nodiscard]] T* operator->() noexcept;
    [[nodiscard]] const T* operator->() const noexcept;

    //  Content management
    template<typename... TArgs> [[nodiscard]] static TInstance create(TArgs&&... args) noexcept;
    template<typename... TArgs> bool emplace(TArgs&&... args) noexcept;
    void reset() noexcept;
    void swap(TInstance& other) noexcept;

//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe all owned backing storage without changing its attribution.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace all owned contexts, including unallocated members.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

private:
    void destroy_and_deallocate() noexcept;
    [[nodiscard]] T* object_ptr() noexcept { return static_cast<T*>(m_token.data()); }
    [[nodiscard]] const T* object_ptr() const noexcept { return static_cast<const T*>(m_token.data()); }

    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();

    memory::CMemoryToken m_token{ 1u, k_align };
};

//==============================================================================
//  TInstance<T> non-member helper functions
//==============================================================================

template<typename T>
inline void swap(TInstance<T>& lhs, TInstance<T>& rhs) noexcept
{   //  exchange payloads
    lhs.swap(rhs);
}

template<typename T, typename... TArgs>
inline TInstance<T> make_object_owner(TArgs&&... args) noexcept
{   //  factory
    return TInstance<T>::create(std::forward<TArgs>(args)...);
}

//==============================================================================
//  TInstance<T> out of class function bodies
//==============================================================================

template<typename T>
inline TInstance<T>& TInstance<T>::operator=(TInstance<T>&& other) noexcept
{
    if (this != &other)
    {
        destroy_and_deallocate();
        m_token = std::move(other.m_token);
    }
    return *this;
}

template<typename T>
inline bool TInstance<T>::is_valid() const noexcept
{
    if (!m_token.is_relocatable() ||
        (m_token.stride() != 1u) ||
        (m_token.storage_alignment() != k_align))
    {
        return false;
    }
    return (object_ptr() != nullptr) ? (m_token.count() == k_element_size) : (m_token.count() == 0u);
}

template<typename T>
inline T& TInstance<T>::operator*() noexcept
{
    MV_ASSERT(object_ptr() != nullptr);
    return *object_ptr();
}

template<typename T>
inline const T& TInstance<T>::operator*() const noexcept
{
    MV_ASSERT(object_ptr() != nullptr);
    return *object_ptr();
}

template<typename T>
inline T* TInstance<T>::operator->() noexcept
{
    MV_ASSERT(object_ptr() != nullptr);
    return object_ptr();
}

template<typename T>
inline const T* TInstance<T>::operator->() const noexcept
{
    MV_ASSERT(object_ptr() != nullptr);
    return object_ptr();
}

template<typename T>
template<typename... TArgs>
inline TInstance<T> TInstance<T>::create(TArgs&&... args) noexcept
{
    static_assert(std::is_nothrow_constructible_v<T, TArgs&&...>, "TInstance<T>::create(...) requires T to be nothrow constructible.");

    TInstance owner;
    (void)owner.emplace(std::forward<TArgs>(args)...);
    return owner;
}

template<typename T>
template<typename... TArgs>
inline bool TInstance<T>::emplace(TArgs&&... args) noexcept
{
    static_assert(std::is_nothrow_constructible_v<T, TArgs&&...>, "TInstance<T>::emplace(...) requires T to be nothrow constructible.");

    T* ptr = object_ptr();
    if (ptr != nullptr)
    {   //  existing storage - preserve owner identity, deconstruct and reconstruct in-place
        ptr->~T();
    }
    else if (m_token.allocate(k_element_size))
    {   //  storage allocated
        ptr = object_ptr();
    }
    else
    {
        return false;
    }
    ::new (static_cast<void*>(ptr)) T(std::forward<TArgs>(args)...);
    return true;
}

template<typename T>
inline void TInstance<T>::destroy_and_deallocate() noexcept
{
    T* const ptr = object_ptr();
    if (ptr != nullptr)
    {
        ptr->~T();
    }

    m_token.deallocate();
}

template<typename T>
inline void TInstance<T>::reset() noexcept
{
    destroy_and_deallocate();
}

template<typename T>
inline void TInstance<T>::swap(TInstance& other) noexcept
{
    using std::swap;
    swap(m_token, other.m_token);
}

template<typename T>
inline memory::SMemoryAttribution TInstance<T>::memory_attribution() const noexcept
{
    return memory::observe_memory_attribution(m_token);
}

template<typename T>
inline void TInstance<T>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
{
    m_token.unsafe_replace_context_without_accounting(expected_source, target);
}

#endif  //  TINSTANCE_HPP_INCLUDED
