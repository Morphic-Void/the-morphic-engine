
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   wait_word_semaphore.cpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Process-local counting semaphore implementation.

#include <cstdint>      //  std::uint32_t

#include "platform/threading/wait_word_semaphore.hpp"
#include "platform/threading/wait_word.hpp"
#include "platform/platform_defines.hpp"

namespace threading
{

static constexpr std::uint32_t k_shutdown_signalled_count = ~std::uint32_t{ 0u };
static constexpr std::uint32_t k_max_permit_count = k_shutdown_signalled_count - 1u;

//==============================================================================
//  Construction and destruction
//==============================================================================

CWaitWordSemaphore::CWaitWordSemaphore() noexcept : m_count(0u)
{
}

CWaitWordSemaphore::~CWaitWordSemaphore() noexcept
{
    signal_shutdown();
}

//==============================================================================
//  State queries
//==============================================================================

std::uint32_t CWaitWordSemaphore::query_count() const noexcept
{
    const std::uint32_t count = m_count.load(std::memory_order_relaxed);

    return (count == k_shutdown_signalled_count) ? 0u : count;
}

bool CWaitWordSemaphore::is_shutdown_signalled() const noexcept
{
    return m_count.load(std::memory_order_acquire) == k_shutdown_signalled_count;
}

//==============================================================================
//  Consumer operations
//==============================================================================

bool CWaitWordSemaphore::acquire() noexcept
{
    std::uint32_t seen = m_count.load(std::memory_order_acquire);

    for (;;)
    {
        while (seen == 0u)
        {
            platform::threading::wait_while_equal(m_count, 0u);

            seen = m_count.load(std::memory_order_acquire);
        }

        if (seen == k_shutdown_signalled_count)
        {
            return false;
        }

        const std::uint32_t wanted = seen - 1u;

        if (m_count.compare_exchange_weak(
            seen, wanted, std::memory_order_acquire, std::memory_order_relaxed))
        {
            return true;
        }

        //  On failure, seen has been updated with the current m_count.
    }
}

bool CWaitWordSemaphore::try_acquire() noexcept
{
    std::uint32_t seen = m_count.load(std::memory_order_acquire);

    while ((seen != 0u) && (seen != k_shutdown_signalled_count))
    {
        const std::uint32_t wanted = seen - 1u;

        if (m_count.compare_exchange_weak(
            seen, wanted, std::memory_order_acquire, std::memory_order_relaxed))
        {
            return true;
        }

        //  On failure, seen has been updated with the current m_count.
    }

    return false;
}

//==============================================================================
//  Producer operations
//==============================================================================

bool CWaitWordSemaphore::release(const std::uint32_t release_count) noexcept
{
    if (release_count == 0u)
    {
        return true;
    }

    std::uint32_t seen = m_count.load(std::memory_order_relaxed);

    for (;;)
    {
        if (seen == k_shutdown_signalled_count)
        {
            return false;
        }

        if (release_count > (k_max_permit_count - seen))
        {
            return false;
        }

        const std::uint32_t wanted = seen + release_count;

        if (m_count.compare_exchange_weak(
            seen, wanted, std::memory_order_release, std::memory_order_relaxed))
        {
            if (release_count == 1u)
            {
                platform::threading::wake_one_waiter(m_count);
            }
            else
            {
                platform::threading::wake_all_waiters(m_count);
            }

            return true;
        }

        //  On failure, seen has been updated with the current m_count.
    }
}

//==============================================================================
//  Producer state management
//==============================================================================

void CWaitWordSemaphore::signal_shutdown() noexcept
{
    const std::uint32_t previous = m_count.exchange(k_shutdown_signalled_count, std::memory_order_acq_rel);

    if (previous != k_shutdown_signalled_count)
    {
        platform::threading::wake_all_waiters(m_count);
    }
}

void CWaitWordSemaphore::restart() noexcept
{
    m_count.store(0u, std::memory_order_relaxed);
}

}   //  namespace threading
