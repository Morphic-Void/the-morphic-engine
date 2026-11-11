
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   CThreadControlState.hpp
//  Author: Ritchie Brannan
//  Date:   13 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Atomic thread progress and exit control class.

#pragma once

#ifndef CTHREAD_CONTROL_STATE_HPP_INCLUDED
#define CTHREAD_CONTROL_STATE_HPP_INCLUDED

#include <atomic>   //  std::atomic
#include <cstdint>  //  std::uint32_t
#include "platform/threading/owner_wait.hpp"

namespace threading
{

//==============================================================================
//  EThreadRunState
//==============================================================================

enum class EThreadRunState : std::uint32_t
{
    Empty = 0u,
    Pending,
    Startup,
    Running,
    Waiting,
    Exiting,
    Exited,
    Failed
};

//==============================================================================
//  CThreadControlState
//==============================================================================

class CThreadControlState
{
public:

    //  Default and deleted lifetime
    CThreadControlState() noexcept = default;
    CThreadControlState(const CThreadControlState&) = delete;
    CThreadControlState& operator=(const CThreadControlState&) = delete;
    CThreadControlState(CThreadControlState&&) = delete;
    CThreadControlState& operator=(CThreadControlState&&) = delete;
    ~CThreadControlState() noexcept = default;

    //  Controller side reset of the class
    void reset() noexcept;
    //  Owner only, before the controlled thread starts; retained through join.
    void install_owner_wake(const platform::threading::SWakeEndpoint wake) noexcept { m_owner_wake = wake; }

    //  Controller side worker exit request
    void request_exit() noexcept;

    //  Controller side queries
    bool is_starting() const noexcept;
    bool is_ready() const noexcept;
    bool is_done() const noexcept;
    EThreadRunState query_state() const noexcept;
    std::uint32_t query_heartbeat_epoch() const noexcept;
    std::uint32_t query_failure_code() const noexcept;

    //  Controller side thread state control
    void mark_empty() noexcept;
    void mark_pending() noexcept;

    //  Worker side state signalling
    void mark_startup() noexcept;
    void mark_running() noexcept;
    void mark_waiting() noexcept;
    void mark_exiting() noexcept;
    void mark_exited() noexcept;
    void mark_failed(const std::uint32_t code) noexcept;

    //  Worker side thread heartbeat
    void advance_heartbeat() noexcept;

    //  Worker and controller exit requested query
    bool exit_requested() const noexcept;

private:
    platform::threading::SWakeEndpoint m_owner_wake;
    std::atomic<std::uint32_t> m_exit_request{ 0u };
    std::atomic<std::uint32_t> m_state{ static_cast<std::uint32_t>(EThreadRunState::Empty) };
    std::atomic<std::uint32_t> m_heartbeat_epoch{ 0u };
    std::atomic<std::uint32_t> m_failure_code{ 0u };
};

//==============================================================================
//  CThreadControlState out of class function bodies
//==============================================================================

inline void CThreadControlState::reset() noexcept
{
    m_exit_request.store(0u, std::memory_order_release);
    m_state.store(static_cast<std::uint32_t>(EThreadRunState::Empty), std::memory_order_release);
    m_heartbeat_epoch.store(0u, std::memory_order_release);
    m_failure_code.store(0u, std::memory_order_release);
}

inline void CThreadControlState::request_exit() noexcept
{
    m_exit_request.store(1u, std::memory_order_release);
}

inline bool CThreadControlState::is_starting() const noexcept
{
    EThreadRunState state = query_state();
    return (state == EThreadRunState::Pending) || (state == EThreadRunState::Startup);
}

inline bool CThreadControlState::is_ready() const noexcept
{
    EThreadRunState state = query_state();
    return (state == EThreadRunState::Running) || (state == EThreadRunState::Waiting);
}

inline bool CThreadControlState::is_done() const noexcept
{
    EThreadRunState state = query_state();
    return (state == EThreadRunState::Exited) || (state == EThreadRunState::Failed);
}

inline EThreadRunState CThreadControlState::query_state() const noexcept
{
    return static_cast<EThreadRunState>(m_state.load(std::memory_order_acquire));
}

inline std::uint32_t CThreadControlState::query_heartbeat_epoch() const noexcept
{
    return m_heartbeat_epoch.load(std::memory_order_acquire);
}

inline std::uint32_t CThreadControlState::query_failure_code() const noexcept
{
    return m_failure_code.load(std::memory_order_acquire);
}

inline void CThreadControlState::mark_empty() noexcept
{
    m_state.store(static_cast<std::uint32_t>(EThreadRunState::Empty), std::memory_order_release);
}

inline void CThreadControlState::mark_pending() noexcept
{
    m_state.store(static_cast<std::uint32_t>(EThreadRunState::Pending), std::memory_order_release);
}

inline void CThreadControlState::mark_startup() noexcept
{
    m_state.store(static_cast<std::uint32_t>(EThreadRunState::Startup), std::memory_order_release);
}

inline void CThreadControlState::mark_running() noexcept
{
    const EThreadRunState previous = static_cast<EThreadRunState>(m_state.exchange(
        static_cast<std::uint32_t>(EThreadRunState::Running), std::memory_order_release));
    if ((previous == EThreadRunState::Startup) || (previous == EThreadRunState::Pending))
    {
        (void)m_owner_wake.signal();
    }
}

inline void CThreadControlState::mark_waiting() noexcept
{
    const EThreadRunState previous = static_cast<EThreadRunState>(m_state.exchange(
        static_cast<std::uint32_t>(EThreadRunState::Waiting), std::memory_order_release));
    if ((previous == EThreadRunState::Startup) || (previous == EThreadRunState::Pending))
    {
        (void)m_owner_wake.signal();
    }
}

inline void CThreadControlState::mark_exiting() noexcept
{
    m_state.store(static_cast<std::uint32_t>(EThreadRunState::Exiting), std::memory_order_release);
}

inline void CThreadControlState::mark_exited() noexcept
{
    m_state.store(static_cast<std::uint32_t>(EThreadRunState::Exited), std::memory_order_release);
    (void)m_owner_wake.signal();
}

inline void CThreadControlState::mark_failed(const std::uint32_t code) noexcept
{
    m_failure_code.store(code, std::memory_order_release);
    m_state.store(static_cast<std::uint32_t>(EThreadRunState::Failed), std::memory_order_release);
    (void)m_owner_wake.signal();
}

inline void CThreadControlState::advance_heartbeat() noexcept
{
    m_heartbeat_epoch.fetch_add(1u, std::memory_order_acq_rel);
}

inline bool CThreadControlState::exit_requested() const noexcept
{
    return m_exit_request.load(std::memory_order_acquire) != 0u;
}

}   //  namespace threading

#endif  //  #ifndef CTHREAD_CONTROL_STATE_HPP_INCLUDED
