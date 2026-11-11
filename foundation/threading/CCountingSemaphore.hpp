
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   CCountingSemaphore.hpp
//  Author: Ritchie Brannan
//  Date:   10 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Engine-facing process-local counting semaphore.
//
//  CCountingSemaphore owns a counted permit state. acquire() consumes one
//  permit, blocking while no permit is available. try_acquire() consumes
//  one permit only if immediately available. release() adds permits and
//  notifies waiters.
//
//  Construction starts in the control-released state. acquire_control() resets
//  the count to zero and enables controlled operation. release_control() moves
//  the count to the control-released sentinel and wakes or releases waiters.
//
//  The control-released sentinel lets waiters distinguish "no permit yet" from
//  "this semaphore is no longer controlled". acquire() returns false after
//  observing this state.
//
//  Associated queues, pools, work lists, and shutdown policy remain the
//  responsibility of the caller.

#pragma once

#ifndef CCOUNTING_SEMAPHORE_HPP_INCLUDED
#define CCOUNTING_SEMAPHORE_HPP_INCLUDED

#include <atomic>       //  std::atomic
#include <cstdint>      //  std::uint32_t

#include "threading/CParkingGate.hpp"

namespace threading
{

//==============================================================================
//  CCountingSemaphore
//==============================================================================

class CCountingSemaphore
{
public:

    //  Constructor and destructor
    CCountingSemaphore() noexcept;
    ~CCountingSemaphore() noexcept;

    //  Deleted lifetime
    CCountingSemaphore(const CCountingSemaphore&) = delete;
    CCountingSemaphore& operator=(const CCountingSemaphore&) = delete;
    CCountingSemaphore(CCountingSemaphore&&) = delete;
    CCountingSemaphore& operator=(CCountingSemaphore&&) = delete;

    //  Status
    bool is_valid() const noexcept;
    bool has_control() const noexcept;
    bool is_control_released() const noexcept;

    //  Control
    bool acquire_control() noexcept;
    void release_control() noexcept;

    //  Count observation
    std::uint32_t query_count() const noexcept;

    //  Consumer operations
    bool acquire(CParkingTicket& ticket) noexcept;
    bool try_acquire() noexcept;

    //  Producer operations
    bool release(const std::uint32_t release_count = 1u) noexcept;

private:

    static constexpr std::uint32_t k_control_released_count = ~std::uint32_t{ 0u };
    static constexpr std::uint32_t k_max_permit_count = k_control_released_count - 1u;

    std::atomic<std::uint32_t> m_count{ k_control_released_count };

    CParkingGate m_fallback_gate;

    bool m_valid = false;
    bool m_has_control = false;
};

}   //  namespace threading

#endif  //  #ifndef CCOUNTING_SEMAPHORE_HPP_INCLUDED
