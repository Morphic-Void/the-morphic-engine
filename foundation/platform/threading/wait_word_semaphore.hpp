
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   wait_word_semaphore.hpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Process-local counting semaphore.
//
//  Provides a counted permit primitive over an atomic wait word. Permits are
//  remembered until consumed. acquire() consumes one permit, blocking while no
//  permit is available. try_acquire() consumes one permit only if immediately
//  available. release() adds permits and wakes waiters.
//
//  signal_shutdown() places the semaphore into a terminal shutdown state and
//  wakes all waiters. acquire() returns false after observing this state.
//  release() fails while shutdown is signalled.
//
//  restart() returns the semaphore to normal zero-count state. It is only
//  valid when caller synchronization guarantees that no operations or waiters
//  are active.
//
//  The destructor signals shutdown as a fail-safe wake. This does not replace
//  caller responsibility for object lifetime synchronization.
//
//  Associated queues, pools, work lists, and shutdown policy remain the
//  responsibility of the caller.

#pragma once

#ifndef WAIT_WORD_SEMAPHORE_HPP_INCLUDED
#define WAIT_WORD_SEMAPHORE_HPP_INCLUDED

#include <atomic>       //  std::atomic
#include <cstdint>      //  std::uint32_t

namespace threading
{

//==============================================================================
//  Wait word based counting semaphore
//==============================================================================

class CWaitWordSemaphore
{
public:

    //  Deleted lifetime
    CWaitWordSemaphore(const CWaitWordSemaphore&) = delete;
    CWaitWordSemaphore& operator=(const CWaitWordSemaphore&) = delete;
    CWaitWordSemaphore(CWaitWordSemaphore&&) = delete;
    CWaitWordSemaphore& operator=(CWaitWordSemaphore&&) = delete;

    //  Construction and destruction
    CWaitWordSemaphore() noexcept;
    ~CWaitWordSemaphore() noexcept;

    //  State queries
    std::uint32_t query_count() const noexcept;
    bool is_shutdown_signalled() const noexcept;

    //  Consumer operations
    bool acquire() noexcept;
    bool try_acquire() noexcept;

    //  Producer operations
    bool release(const std::uint32_t release_count = 1u) noexcept;

    //  Producer state management
    void signal_shutdown() noexcept;
    void restart() noexcept;

private:

    std::atomic<std::uint32_t> m_count{ 0u };
};

}   //  namespace threading

#endif  //  #ifndef WAIT_WORD_SEMAPHORE_HPP_INCLUDED
