
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TProcessLifetime.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Direct, inline ownership with explicit terminal process-lifetime retention.
//
//  Constructs one T directly; no allocation, placement new or reconstruction.
//  destroy() permits early teardown; the owner's destructor does not repeat it.
//  Retention is irreversible and suppresses T's destruction at process teardown.
//  An owner that may be retained must have static storage duration. Retention
//  does not extend the storage duration of a local owner or any borrowed state.
//  The caller must establish quiescence and include every lifetime dependency.
//  Single-threaded lifecycle control; no copy, move, swap or ownership release.

#pragma once

#ifndef TPROCESSLIFETIME_HPP_INCLUDED
#define TPROCESSLIFETIME_HPP_INCLUDED

#include <cstdint>
#include <type_traits>
#include <utility>

#include "debug/macros.hpp"

template<typename T>
class TProcessLifetime final
{
    static_assert(!std::is_array_v<T> && !std::is_reference_v<T>, "TProcessLifetime<T> owns one object.");
    static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>, "TProcessLifetime<T> requires an unqualified object type.");
    static_assert(std::is_nothrow_destructible_v<T>, "TProcessLifetime<T> requires nothrow destruction.");

public:
    template<typename... TArgs> explicit TProcessLifetime(TArgs&&... args) noexcept;
    TProcessLifetime(const TProcessLifetime&) = delete;
    TProcessLifetime& operator=(const TProcessLifetime&) = delete;
    TProcessLifetime(TProcessLifetime&&) = delete;
    TProcessLifetime& operator=(TProcessLifetime&&) = delete;
    ~TProcessLifetime() noexcept;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_retained() const noexcept;
    [[nodiscard]] T& operator*() noexcept;
    [[nodiscard]] const T& operator*() const noexcept;
    [[nodiscard]] T* operator->() noexcept;
    [[nodiscard]] const T* operator->() const noexcept;

    //  Repeating the same terminal decision is harmless; reversing it fails.
    [[nodiscard]] bool destroy() noexcept;
    [[nodiscard]] bool retain_until_process_exit() noexcept;

private:
    enum class EState : std::uint8_t { live, destroyed, retained };

    union { T m_value; };
    EState m_state{ EState::live };
};

//==============================================================================
//  TProcessLifetime<T> out of class function bodies
//==============================================================================

template<typename T>
template<typename... TArgs>
inline TProcessLifetime<T>::TProcessLifetime(TArgs&&... args) noexcept
    : m_value(std::forward<TArgs>(args)...)
{
    static_assert(std::is_nothrow_constructible_v<T, TArgs&&...>, "TProcessLifetime<T> requires nothrow construction.");
}

template<typename T>
inline TProcessLifetime<T>::~TProcessLifetime() noexcept
{
    (void)destroy();
}

template<typename T>
inline bool TProcessLifetime<T>::is_ready() const noexcept
{
    return m_state != EState::destroyed;
}

template<typename T>
inline bool TProcessLifetime<T>::is_retained() const noexcept
{
    return m_state == EState::retained;
}

template<typename T>
inline T& TProcessLifetime<T>::operator*() noexcept
{
    MV_ASSERT(is_ready());
    return m_value;
}

template<typename T>
inline const T& TProcessLifetime<T>::operator*() const noexcept
{
    MV_ASSERT(is_ready());
    return m_value;
}

template<typename T>
inline T* TProcessLifetime<T>::operator->() noexcept
{
    MV_ASSERT(is_ready());
    return &m_value;
}

template<typename T>
inline const T* TProcessLifetime<T>::operator->() const noexcept
{
    MV_ASSERT(is_ready());
    return &m_value;
}

template<typename T>
inline bool TProcessLifetime<T>::destroy() noexcept
{
    if (is_retained())
    {
        return false;
    }
    if (m_state == EState::live)
    {
        m_state = EState::destroyed;
        m_value.~T();
    }
    return true;
}

template<typename T>
inline bool TProcessLifetime<T>::retain_until_process_exit() noexcept
{
    if (!is_ready())
    {
        return false;
    }
    m_state = EState::retained;
    return true;
}

#endif  //  TPROCESSLIFETIME_HPP_INCLUDED
