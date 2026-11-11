
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   CCountingSemaphore.cpp
//  Author: Ritchie Brannan
//  Date:   10 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.

#include "threading/CCountingSemaphore.hpp"
#include "platform/platform_defines.hpp"
#include "debug/macros.hpp"

#if MV_PLATFORM_HAS_NATIVE_WAIT_WORD
#include "platform/threading/wait_word.hpp"
#endif

namespace threading
{

//==============================================================================
//  Constructor and destructor
//==============================================================================

CCountingSemaphore::CCountingSemaphore() noexcept : m_count(k_control_released_count)
{
#if MV_PLATFORM_HAS_NATIVE_WAIT_WORD
    m_valid = true;
#else
    m_valid = m_fallback_gate.is_valid();
#endif
    m_has_control = false;
}

CCountingSemaphore::~CCountingSemaphore() noexcept
{
    MV_ASSERT(!m_has_control);
    MV_ASSERT(is_control_released());

#if !MV_PLATFORM_HAS_NATIVE_WAIT_WORD
    MV_ASSERT(!m_fallback_gate.has_control());
#endif
}

//==============================================================================
//  Status
//==============================================================================

bool CCountingSemaphore::is_valid() const noexcept
{
    return m_valid;
}

bool CCountingSemaphore::has_control() const noexcept
{
    return m_has_control;
}

bool CCountingSemaphore::is_control_released() const noexcept
{
    return m_count.load(std::memory_order_acquire) == k_control_released_count;
}

//==============================================================================
//  Control
//==============================================================================

bool CCountingSemaphore::acquire_control() noexcept
{
    if (!m_valid || m_has_control)
    {
        MV_ASSERT_REPORT("CCountingSemaphore::acquire_control received an invalid control request");
        return false;
    }

    if (!is_control_released())
    {
        MV_ASSERT_REPORT("CCountingSemaphore::acquire_control was called before the semaphore was released");
        return false;
    }

#if !MV_PLATFORM_HAS_NATIVE_WAIT_WORD
    if (m_fallback_gate.has_control())
    {
        MV_ASSERT_REPORT("CCountingSemaphore::acquire_control found the fallback gate already controlled");
        return false;
    }
    if (!m_fallback_gate.acquire_control())
    {
        return false;
    }
#endif

    m_count.store(0u, std::memory_order_release);
    m_has_control = true;
    return true;
}

void CCountingSemaphore::release_control() noexcept
{
    if (!m_valid || !m_has_control)
    {
        MV_ASSERT_REPORT("CCountingSemaphore::release_control was called without valid control");
        return;
    }

    const std::uint32_t previous = m_count.exchange(k_control_released_count, std::memory_order_acq_rel);
    if (previous == k_control_released_count)
    {
        MV_ERROR("CCountingSemaphore::release_control found the count already released");
    }

#if MV_PLATFORM_HAS_NATIVE_WAIT_WORD
    platform::threading::wake_all_waiters(m_count);
#else
    if (!m_fallback_gate.has_control())
    {
        MV_ERROR("CCountingSemaphore::release_control expected the fallback gate to be controlled");
    }
    else
    {
        m_fallback_gate.release_control();
    }
#endif

    m_has_control = false;
}

//==============================================================================
//  Count observation
//==============================================================================

std::uint32_t CCountingSemaphore::query_count() const noexcept
{
    const std::uint32_t count = m_count.load(std::memory_order_relaxed);

    return (count == k_control_released_count) ? 0u : count;
}

//==============================================================================
//  Consumer operations
//==============================================================================

bool CCountingSemaphore::acquire(CParkingTicket& ticket) noexcept
{
#if MV_PLATFORM_HAS_NATIVE_WAIT_WORD
    (void)ticket;
#endif
    if (!m_valid)
    {
        MV_ASSERT_REPORT("CCountingSemaphore::acquire was called on an invalid semaphore");
        return false;
    }

    std::uint32_t seen = m_count.load(std::memory_order_acquire);

    for (;;)
    {
        while (seen == 0u)
        {
#if MV_PLATFORM_HAS_NATIVE_WAIT_WORD
            platform::threading::wait_while_equal(m_count, 0u);
#else
            m_fallback_gate.park(ticket);
#endif
            seen = m_count.load(std::memory_order_acquire);
        }

        if (seen == k_control_released_count)
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

bool CCountingSemaphore::try_acquire() noexcept
{
    if (!m_valid)
    {
        MV_ASSERT_REPORT("CCountingSemaphore::try_acquire was called on an invalid semaphore");
        return false;
    }

    std::uint32_t seen = m_count.load(std::memory_order_acquire);

    while ((seen != 0u) && (seen != k_control_released_count))
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

bool CCountingSemaphore::release(const std::uint32_t release_count) noexcept
{
    if (!m_valid || !m_has_control)
    {
        MV_ASSERT_REPORT("CCountingSemaphore::release was called without valid control");
        return false;
    }

    if (release_count == 0u)
    {
        return true;
    }

    std::uint32_t seen = m_count.load(std::memory_order_relaxed);

    for (;;)
    {
        if (seen == k_control_released_count)
        {
            break;
        }

        if (release_count > (k_max_permit_count - seen))
        {
            break;
        }

        const std::uint32_t wanted = seen + release_count;

        if (m_count.compare_exchange_weak(
            seen, wanted, std::memory_order_release, std::memory_order_relaxed))
        {
#if MV_PLATFORM_HAS_NATIVE_WAIT_WORD
            if (release_count == 1u)
            {
                platform::threading::wake_one_waiter(m_count);
            }
            else
            {
                platform::threading::wake_all_waiters(m_count);
            }
#else
            if (!m_fallback_gate.has_control())
            {
                MV_ERROR("CCountingSemaphore::release expected the fallback gate to be controlled");
            }
            else
            {
                m_fallback_gate.cycle_phase();
            }
#endif
            return true;
        }

        //  On failure, seen has been updated with the current m_count.
    }

    return false;
}

}   //  namespace threading
