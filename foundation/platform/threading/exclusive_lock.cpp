
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   exclusive_lock.cpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  In-process exclusive native lock implementation.

#include <atomic>       //  std::memory_order_*
#include <cstdint>      //  std::uint32_t
#include <cstring>      //  std::memset

#include "platform/threading/exclusive_lock.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

#if MV_PLATFORM_HAS_PTHREADS
#include <pthread.h>
#endif

namespace platform::threading
{

//==============================================================================
//  Construction and destruction
//==============================================================================

CExclusiveLock::CExclusiveLock() noexcept
{
    m_valid = initialise_native_lock();
}

CExclusiveLock::~CExclusiveLock() noexcept
{
    if (!m_valid)
    {
        clear();
        return;
    }

    const std::uint32_t waiter_count = m_waiter_count.load(std::memory_order_acquire);

    if (waiter_count == 0u)
    {
        if (try_acquire_owner_thread_id_gate())
        {
            const bool is_owned = m_owner_thread_id.is_valid();

            release_owner_thread_id_gate();

            if (!is_owned)
            {
                if (destroy_native_lock())
                {
                    m_valid = false;
                    m_owner_thread_id = CPlatformThreadId();
                    m_waiter_count.store(0u, std::memory_order_release);
                    clear();
                }
            }
        }
    }
}

//==============================================================================
//  Status
//==============================================================================

bool CExclusiveLock::is_valid() const noexcept
{
    return m_valid;
}

//==============================================================================
//  Operations
//==============================================================================

void CExclusiveLock::acquire() noexcept
{
    if (!m_valid)
    {
        return;
    }

    const CPlatformThreadId current_thread_id = query_current_thread_id();

    if (!current_thread_id.is_valid())
    {
        return;
    }

    m_waiter_count.fetch_add(1u, std::memory_order_acq_rel);

    const bool acquired = acquire_native_lock();

    if (!acquired)
    {
        m_waiter_count.fetch_sub(1u, std::memory_order_acq_rel);
        return;
    }

    if (!try_acquire_owner_thread_id_gate())
    {
        (void)release_native_lock();
        m_waiter_count.fetch_sub(1u, std::memory_order_acq_rel);
        return;
    }

    m_owner_thread_id = current_thread_id;

    release_owner_thread_id_gate();

    m_waiter_count.fetch_sub(1u, std::memory_order_acq_rel);
}

bool CExclusiveLock::try_acquire() noexcept
{
    if (!m_valid)
    {
        return false;
    }

    const CPlatformThreadId current_thread_id = query_current_thread_id();

    if (!current_thread_id.is_valid())
    {
        return false;
    }

    if (!try_acquire_native_lock())
    {
        return false;
    }

    if (!try_acquire_owner_thread_id_gate())
    {
        (void)release_native_lock();
        return false;
    }

    const bool is_owned = m_owner_thread_id.is_valid();
    if (is_owned)
    {
        release_owner_thread_id_gate();
        (void)release_native_lock();
        return false;
    }

    m_owner_thread_id = current_thread_id;
    release_owner_thread_id_gate();
    return true;
}

void CExclusiveLock::release() noexcept
{
    if (!m_valid)
    {
        return;
    }

    const CPlatformThreadId current_thread_id = query_current_thread_id();

    if (!current_thread_id.is_valid())
    {
        return;
    }

    if (!try_acquire_owner_thread_id_gate())
    {
        return;
    }

    const bool is_owner = (m_owner_thread_id == current_thread_id);

    if (!is_owner)
    {
        release_owner_thread_id_gate();
        return;
    }

    m_owner_thread_id = CPlatformThreadId();

    release_owner_thread_id_gate();

    (void)release_native_lock();
}

//==============================================================================
//  Owner thread id gate
//==============================================================================

bool CExclusiveLock::try_acquire_owner_thread_id_gate() noexcept
{
    std::uint32_t expected = 0u;

    return m_owner_thread_id_gate.compare_exchange_strong(
        expected, 1u, std::memory_order_acquire, std::memory_order_relaxed);
}

void CExclusiveLock::release_owner_thread_id_gate() noexcept
{
    m_owner_thread_id_gate.store(0u, std::memory_order_release);
}

//==============================================================================
//  Native lock implementation
//==============================================================================

bool CExclusiveLock::initialise_native_lock() noexcept
{
#if MV_PLATFORM_WINDOWS

    static_assert((sizeof(SRWLOCK) <= k_opaque_size), "CExclusiveLock storage is too small for SRWLOCK.");
    static_assert((alignof(SRWLOCK) <= k_opaque_alignment), "CExclusiveLock alignment is too small for SRWLOCK.");

    clear();

    InitializeSRWLock(reinterpret_cast<SRWLOCK*>(m_opaque));

    return true;

#elif MV_PLATFORM_HAS_PTHREADS

    static_assert((sizeof(pthread_mutex_t) <= k_opaque_size), "CExclusiveLock storage is too small for pthread_mutex_t.");
    static_assert((alignof(pthread_mutex_t) <= k_opaque_alignment), "CExclusiveLock alignment is too small for pthread_mutex_t.");

    clear();

    const int result = ::pthread_mutex_init(reinterpret_cast<pthread_mutex_t*>(m_opaque), nullptr);

    if (result != 0)
    {
        clear();
        return false;
    }

    return true;

#else

    clear();
    return false;

#endif
}

bool CExclusiveLock::destroy_native_lock() noexcept
{
#if MV_PLATFORM_WINDOWS

    return true;

#elif MV_PLATFORM_HAS_PTHREADS

    const int result = ::pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t*>(m_opaque));

    return (result == 0);

#else

    return false;

#endif
}

bool CExclusiveLock::acquire_native_lock() noexcept
{
#if MV_PLATFORM_WINDOWS

    AcquireSRWLockExclusive(reinterpret_cast<SRWLOCK*>(m_opaque));

    return true;

#elif MV_PLATFORM_HAS_PTHREADS

    const int result = ::pthread_mutex_lock(reinterpret_cast<pthread_mutex_t*>(m_opaque));

    return (result == 0);

#else

    return false;

#endif
}

bool CExclusiveLock::try_acquire_native_lock() noexcept
{
#if MV_PLATFORM_WINDOWS

    return TryAcquireSRWLockExclusive(
        reinterpret_cast<SRWLOCK*>(m_opaque)) != FALSE;

#elif MV_PLATFORM_HAS_PTHREADS

    const int result =
        ::pthread_mutex_trylock(reinterpret_cast<pthread_mutex_t*>(m_opaque));

    return (result == 0);

#else

    return false;

#endif
}

bool CExclusiveLock::release_native_lock() noexcept
{
#if MV_PLATFORM_WINDOWS

    ReleaseSRWLockExclusive(reinterpret_cast<SRWLOCK*>(m_opaque));

    return true;

#elif MV_PLATFORM_HAS_PTHREADS

    const int result = ::pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t*>(m_opaque));

    return (result == 0);

#else

    return false;

#endif
}

//==============================================================================
//  Storage
//==============================================================================

void CExclusiveLock::clear() noexcept
{
    std::memset(m_opaque, 0, sizeof(m_opaque));
}

#if !MV_PLATFORM_WINDOWS && !MV_PLATFORM_HAS_PTHREADS

#error "platform::threading::CExclusiveLock is not implemented for this platform."

#endif

}   //  namespace platform::threading
