
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   async_state.hpp
//  Authors: Ritchie Brannan / OpenAI tools
//  Date:   6 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Shared storage for mutable POD state belonging to coordinated asynchronous
//  operations. Message completion governs terminal slot release.

#pragma once

#ifndef ASYNC_STATE_HPP_INCLUDED
#define ASYNC_STATE_HPP_INCLUDED

#include <cstddef>  //  std::size_t
#include <cstdint>  //  std::int32_t, std::uint32_t

#include "containers/TPodUnorderedSlots.hpp"
#include "system/erased_pod.hpp"

//==============================================================================
//  CASyncState
//  Fixed state carrier shared by asynchronous system operations.
//==============================================================================

using CASyncState = TErasedPod<48u>;

static_assert(sizeof(CASyncState) == 64u, "CASyncState must occupy 64 bytes.");
static_assert(alignof(CASyncState) == 16u, "CASyncState must have 16-byte alignment.");

//==============================================================================
//  CASyncStates
//  Restricted slot management for in-flight asynchronous state.
//==============================================================================

class CASyncStates
{
public:
    CASyncStates() noexcept = default;
    CASyncStates(const CASyncStates&) = delete;
    CASyncStates& operator=(const CASyncStates&) = delete;
    CASyncStates(CASyncStates&&) = delete;
    CASyncStates& operator=(CASyncStates&&) = delete;
    ~CASyncStates() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    [[nodiscard]] bool initialise(const std::size_t initial_slot_count = 0u) noexcept;

    void deallocate() noexcept;

    template<typename T>
    [[nodiscard]] std::int32_t acquire(const std::uint32_t tag = 0u) noexcept;

    //  Returned pointers are invalidated by capacity growth, reinitialisation,
    //  deallocation, or release of their slot. Retain the slot index instead.
    [[nodiscard]] CASyncState* resolve(const std::int32_t slot_index) noexcept;
    [[nodiscard]] const CASyncState* resolve(const std::int32_t slot_index) const noexcept;

    template<typename T>
    [[nodiscard]] T* payload(const std::int32_t slot_index) noexcept;

    template<typename T>
    [[nodiscard]] const T* payload(const std::int32_t slot_index) const noexcept;

    template<typename T>
    [[nodiscard]] T* redefine(const std::int32_t slot_index) noexcept;

    [[nodiscard]] bool release(const std::int32_t slot_index) noexcept;

    [[nodiscard]] bool check_integrity() const noexcept;

private:
    TPodUnorderedSlots<CASyncState> m_states;
};

//==============================================================================
//  CASyncStates out of class function bodies
//==============================================================================

inline bool CASyncStates::is_valid() const noexcept
{
    return m_states.is_valid();
}

inline bool CASyncStates::is_empty() const noexcept
{
    return m_states.is_empty();
}

inline bool CASyncStates::is_ready() const noexcept
{
    return m_states.is_ready();
}

inline bool CASyncStates::initialise(const std::size_t initial_slot_count) noexcept
{
    return m_states.initialise(initial_slot_count);
}

inline void CASyncStates::deallocate() noexcept
{
    m_states.deallocate();
}

template<typename T>
inline std::int32_t CASyncStates::acquire(const std::uint32_t tag) noexcept
{
    CASyncState empty_state;
    const std::int32_t slot_index = m_states.insert(empty_state);
    if (slot_index < 0)
    {
        return -1;
    }

    CASyncState* const state = m_states.get_slot(slot_index);
    if (state == nullptr)
    {
        (void)m_states.erase(slot_index);
        return -1;
    }

    state->set_tag(tag);
    (void)state->redefine<T>();
    return slot_index;
}

inline CASyncState* CASyncStates::resolve(const std::int32_t slot_index) noexcept
{
    return m_states.get_slot(slot_index);
}

inline const CASyncState* CASyncStates::resolve(const std::int32_t slot_index) const noexcept
{
    return m_states.get_slot(slot_index);
}

template<typename T>
inline T* CASyncStates::payload(const std::int32_t slot_index) noexcept
{
    CASyncState* const state = resolve(slot_index);
    return (state != nullptr) ? state->payload<T>() : nullptr;
}

template<typename T>
inline const T* CASyncStates::payload(const std::int32_t slot_index) const noexcept
{
    const CASyncState* const state = resolve(slot_index);
    return (state != nullptr) ? state->payload<T>() : nullptr;
}

template<typename T>
inline T* CASyncStates::redefine(const std::int32_t slot_index) noexcept
{
    CASyncState* const state = resolve(slot_index);
    return (state != nullptr) ? &state->redefine<T>() : nullptr;
}

inline bool CASyncStates::release(const std::int32_t slot_index) noexcept
{
    return m_states.erase(slot_index);
}

inline bool CASyncStates::check_integrity() const noexcept
{
    return m_states.check_integrity();
}

#endif  //  #ifndef ASYNC_STATE_HPP_INCLUDED
