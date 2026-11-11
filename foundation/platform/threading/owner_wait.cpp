
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    owner_wait.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Windows event/message waiting and portable engine-only wait-word parking.

#include "platform/threading/owner_wait.hpp"
#include "platform/threading/wait_word.hpp"
#include "platform/threading/wait_deadline_math.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

namespace platform::threading
{

static SOwnerWaitResult failure_result(const WaitError error) noexcept
{
    if (error.code == EWaitError::unsupported)
    {
        return { EOwnerWaitStatus::unsupported, error.native_error };
    }
    return { (error.code == EWaitError::native_failure) ? EOwnerWaitStatus::native_failure :
        EOwnerWaitStatus::invalid_argument, error.native_error };
}

COwnerWait::~COwnerWait() noexcept
{
#if MV_PLATFORM_WINDOWS
    if (m_native) (void)CloseHandle(m_native);
#endif
}

WaitError COwnerWait::initialise() noexcept
{
    if (m_initialised)
    {
        return { EWaitError::invalid_argument, 0u };
    }
#if MV_PLATFORM_WINDOWS
    m_native = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_native)
    {
        return { EWaitError::native_failure, GetLastError() };
    }
#endif
    m_initialised = true;
    return {};
}

WaitError COwnerWait::error() const noexcept
{
    if (!m_initialised)
    {
        return { EWaitError::invalid_argument, 0u };
    }
    const std::uint32_t failure = m_failure.load(std::memory_order_acquire);
    return failure ? WaitError{ EWaitError::native_failure, failure } : WaitError{};
}

bool COwnerWait::wake_native() noexcept
{
#if MV_PLATFORM_WINDOWS
    if (!SetEvent(m_native))
    {
        const DWORD failure = GetLastError();
        m_failure.store(failure ? failure : ERROR_GEN_FAILURE, std::memory_order_release);
        return false;
    }
#else
    wake_one_waiter(m_state);
#endif
    return true;
}

bool COwnerWait::signal() noexcept
{
    if (!m_initialised) return false;
    //  Every publisher participates even when the native wake is coalesced.
    const std::uint32_t previous = m_state.fetch_or(k_pending, std::memory_order_acq_rel);
    if (m_failure.load(std::memory_order_acquire))
    {
        return false;
    }
#if defined(MV_HOST_RUNTIME_TESTS)
    if (!(previous & k_pending) && m_test_before_signal) m_test_before_signal(m_test_context);
#endif
    return (previous & k_pending) || wake_native();
}

void COwnerWait::release() noexcept
{
    if (!m_initialised) return;
    m_state.fetch_or(k_released, std::memory_order_release);
    (void)wake_native();
}

WaitError COwnerWait::begin_cycle() noexcept
{
    const WaitError failure = error();
    if (failure.code != EWaitError::none) return failure;
#if MV_PLATFORM_WINDOWS
    //  Reset BEFORE acknowledging publications. A concurrent signal either
    //  belongs to the following acquire/drain or leaves the event signalled.
    if (!ResetEvent(m_native))
    {
        const DWORD native_error = GetLastError();
        m_failure.store(native_error ? native_error : ERROR_GEN_FAILURE, std::memory_order_release);
        return error();
    }
#endif
#if defined(MV_HOST_RUNTIME_TESTS)
    if (m_test_before_ack) m_test_before_ack(m_test_context);
#endif
    m_state.fetch_and(~k_pending, std::memory_order_acq_rel);
    return {};
}

SOwnerWaitResult COwnerWait::wait(const CWaitDeadline& deadline, const bool native_events) noexcept
{
    const WaitError failure = error();
    if (failure.code != EWaitError::none)
    {
        return failure_result(failure);
    }
    if (!deadline.is_valid())
    {
        return failure_result(deadline.error());
    }
#if !MV_PLATFORM_WINDOWS
    //  Android/macOS engine-only waits use their existing wait-word backend.
    //  Native UI integration needs an owner-thread loop driver, not polling.
    if (native_events)
    {
        return { EOwnerWaitStatus::unsupported, 0u };
    }
#endif
    for (;;)
    {
        const std::uint32_t state = m_state.load(std::memory_order_acquire);
        if (state & k_released)
        {
            return { EOwnerWaitStatus::released, 0u };
        }
        if (state & k_pending)
        {
            return { EOwnerWaitStatus::activity, 0u };
        }
        const WaitError current_failure = error();
        if (current_failure.code != EWaitError::none)
        {
            return failure_result(current_failure);
        }
        std::uint64_t remaining = 0u;
        const WaitError clock_error = deadline.remaining_nanoseconds(remaining);
        if (clock_error.code != EWaitError::none)
        {
            return failure_result(clock_error);
        }
        if (!remaining)
        {
            //  A final acquired signal wins over timeout.
            if (m_state.load(std::memory_order_acquire)) continue;
            return { EOwnerWaitStatus::timed_out, 0u };
        }
#if defined(MV_HOST_RUNTIME_TESTS)
        if (m_test_before_park) m_test_before_park(m_test_context);
#endif
#if MV_PLATFORM_WINDOWS
        const std::uint32_t milliseconds = internal::finite_wait_milliseconds(remaining);
        const HANDLE handle = m_native;
        const DWORD result = native_events ?
            MsgWaitForMultipleObjectsEx(1u, &handle, milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE) :
            WaitForSingleObject(handle, milliseconds);
        if (result == WAIT_OBJECT_0)
        {
            //  A delayed SetEvent can outlive the publication's acknowledgement.
            //  Return a hint; do not spin on a still-signalled manual event.
            if (m_state.load(std::memory_order_acquire) & k_released)
            {
                return { EOwnerWaitStatus::released, 0u };
            }
            const WaitError wake_error = error();
            if (wake_error.code != EWaitError::none)
            {
                return failure_result(wake_error);
            }
            return { EOwnerWaitStatus::activity, 0u };
        }
        if (native_events && (result == (WAIT_OBJECT_0 + 1u)))
        {
            if (m_state.load(std::memory_order_acquire) & k_released)
            {
                return { EOwnerWaitStatus::released, 0u };
            }
            const WaitError wake_error = error();
            if (wake_error.code != EWaitError::none)
            {
                return failure_result(wake_error);
            }
            return { EOwnerWaitStatus::native_activity, 0u };
        }
        if (result != WAIT_TIMEOUT)
        {
            const DWORD native_error = (result == WAIT_FAILED) ? GetLastError() : ERROR_GEN_FAILURE;
            m_failure.store(native_error ? native_error : ERROR_GEN_FAILURE, std::memory_order_release);
            return failure_result(error());
        }
#else
        const WaitWordResult result = wait_while_equal(m_state, 0u, deadline);
        switch (result.status)
        {
            case EWaitWordStatus::awoken_or_changed:
            {
                if (m_state.load(std::memory_order_acquire) & k_released)
                {
                    return { EOwnerWaitStatus::released, 0u };
                }
                const WaitError wake_error = error();
                if (wake_error.code != EWaitError::none)
                {
                    return failure_result(wake_error);
                }
                return { EOwnerWaitStatus::activity, 0u };
            }
            case EWaitWordStatus::timed_out:
            {
                break;
            }
            case EWaitWordStatus::unsupported:
            {
                return { EOwnerWaitStatus::unsupported, result.native_error };
            }
            case EWaitWordStatus::native_failure:
            {
                m_failure.store(result.native_error ? result.native_error : 1u, std::memory_order_release);
                return { EOwnerWaitStatus::native_failure, result.native_error };
            }
            default:
            {
                return { EOwnerWaitStatus::invalid_argument, result.native_error };
            }
        }
#endif
    }
}

}   //  namespace platform::threading
