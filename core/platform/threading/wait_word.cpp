
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    wait_word.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Low-level atomic wait/wake primitive implementation.

#include <atomic>       //  std::atomic
#include <cstdint>      //  std::uint32_t
#include <cerrno>
#include <limits>

#include "platform/threading/wait_word.hpp"
#include "platform/threading/wait_deadline_math.hpp"
#include "platform/threading/wait_word_internal.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#endif

#if MV_PLATFORM_MAC_OS
#include <os/os_sync_wait_on_address.h>
#include <mach/mach_time.h>
#endif

#if MV_PLATFORM_NATIVE_WAIT_WORD_EXPERIMENTAL
#if defined(_MSC_VER)
#pragma message("warning: native wait-word support is experimental on this platform.")
#else
#warning "native wait-word support is experimental on this platform."
#endif
#endif

namespace platform::threading
{

//==============================================================================
//  Internals
//==============================================================================

namespace internal
{

#if MV_PLATFORM_WINDOWS
using FUnbiasedClock = VOID(WINAPI*)(PULONGLONG);

static FUnbiasedClock resolve_unbiased_clock() noexcept
{
    //  Modern Windows exports this through KernelBase/API sets; Kernel32 does
    //  not necessarily forward it. Both system modules remain loaded for the
    //  process lifetime. Older systems without the export remain unsupported.
    const wchar_t* const modules[]{ L"kernelbase.dll", L"kernel32.dll" };
    for (const auto name : modules)
    {
        const HMODULE module = GetModuleHandleW(name);
        const auto address = module == nullptr ? nullptr :
            GetProcAddress(module, "QueryUnbiasedInterruptTimePrecise");
        if (address != nullptr) return reinterpret_cast<FUnbiasedClock>(address);
    }
    return nullptr;
}
#endif

static WaitError query_wait_clock(std::uint64_t& value, std::uint32_t& numerator, std::uint32_t& denominator) noexcept
{
#if MV_PLATFORM_WINDOWS
    static const FUnbiasedClock clock = resolve_unbiased_clock();
    if (clock == nullptr) return { EWaitError::unsupported, 0u };
    ULONGLONG ticks = 0u;
    clock(&ticks);
    value = ticks;
    numerator = 100u;
    denominator = 1u;
#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return { EWaitError::native_failure, static_cast<std::uint32_t>(errno) };
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if ((now.tv_sec < 0) || (now.tv_nsec < 0) || (now.tv_nsec >= 1000000000L) ||
        (static_cast<std::uint64_t>(now.tv_sec) > (maximum - now.tv_nsec) / 1000000000u))
        return { EWaitError::native_failure, static_cast<std::uint32_t>(EOVERFLOW) };
    value = static_cast<std::uint64_t>(now.tv_sec) * 1000000000u + now.tv_nsec;
    numerator = denominator = 1u;
#elif MV_PLATFORM_MAC_OS
    mach_timebase_info_data_t ratio{};
    const auto result = mach_timebase_info(&ratio);
    if ((result != KERN_SUCCESS) || (ratio.numer == 0u) || (ratio.denom == 0u))
        return { EWaitError::native_failure, static_cast<std::uint32_t>(result) };
    value = mach_absolute_time();
    numerator = ratio.numer;
    denominator = ratio.denom;
#else
    (void)value;
    (void)numerator;
    (void)denominator;
    return { EWaitError::unsupported, 0u };
#endif
    return {};
}

static WaitWordResult clock_error_result(const WaitError error) noexcept
{
    switch (error.code)
    {
    case EWaitError::unsupported: return { EWaitWordStatus::unsupported, error.native_error };
    case EWaitError::native_failure: return { EWaitWordStatus::native_failure, error.native_error };
    default: return { EWaitWordStatus::invalid_argument, error.native_error };
    }
}

static_assert((sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t)),
    "std::atomic<std::uint32_t> must have the same storage size as std::uint32_t.");

static_assert((alignof(std::atomic<std::uint32_t>) >= alignof(std::uint32_t)),
    "std::atomic<std::uint32_t> must be naturally aligned for 32-bit waits.");

static_assert((std::atomic<std::uint32_t>::is_always_lock_free),
    "std::atomic<std::uint32_t> must be always lock-free.");

#if MV_PLATFORM_WINDOWS || MV_PLATFORM_MAC_OS

void* wait_address_from_word(const std::atomic<std::uint32_t>& word) noexcept
{
    const void* const address = static_cast<const void*>(&word);

    return const_cast<void*>(address);
}

#endif

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

const std::uint32_t* futex_address_from_word(const std::atomic<std::uint32_t>& word) noexcept
{
    return reinterpret_cast<const std::uint32_t*>(&word);
}

long futex_wait_private(const std::uint32_t* const address, const std::uint32_t value) noexcept
{
    return ::syscall(
        SYS_futex,
        const_cast<std::uint32_t*>(address),
        FUTEX_WAIT_PRIVATE, value,
        nullptr, nullptr, 0);
}

long futex_wake_private(const std::uint32_t* const address, const int waiter_count) noexcept
{
    return ::syscall(
        SYS_futex,
        const_cast<std::uint32_t*>(address),
        FUTEX_WAKE_PRIVATE, waiter_count,
        nullptr, nullptr, 0);
}

#endif

}   //  namespace internal

//==============================================================================
//  Awake monotonic deadlines
//==============================================================================

WaitError CWaitDeadline::set_after_nanoseconds(const std::uint64_t duration) noexcept
{
    m_error = { EWaitError::invalid_argument, 0u };
    m_deadline = 0u;
    if (duration > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return m_error;
    std::uint64_t now = 0u;
    m_error = internal::query_wait_clock(now, m_nanoseconds_numerator, m_nanoseconds_denominator);
    if (m_error.code != EWaitError::none) return m_error;
    std::uint64_t ticks = 0u;
    if (!internal::scale_wait_duration(duration, m_nanoseconds_denominator,
            m_nanoseconds_numerator, ticks) ||
        (ticks > std::numeric_limits<std::uint64_t>::max() - now))
    {
        m_error = { EWaitError::invalid_argument, 0u };
        return m_error;
    }
    m_deadline = now + ticks;
#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
    //  Futex deadlines are converted to signed 64-bit nanoseconds by the
    //  kernel. Reject a timestamp it would otherwise silently saturate.
    if (m_deadline > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        m_deadline = 0u;
        m_error = { EWaitError::invalid_argument, 0u };
        return m_error;
    }
#endif
    return {};
}

WaitError CWaitDeadline::set_after_milliseconds(const std::uint64_t duration) noexcept
{
    if (duration > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / 1000000u)
    {
        m_deadline = 0u;
        m_error = { EWaitError::invalid_argument, 0u };
        return m_error;
    }
    return set_after_nanoseconds(duration * 1000000u);
}

WaitError CWaitDeadline::remaining_nanoseconds(std::uint64_t& remaining) const noexcept
{
    remaining = 0u;
    if (!is_valid()) return m_error;
    std::uint64_t now = 0u;
    std::uint32_t numerator = 0u, denominator = 0u;
    const WaitError error = internal::query_wait_clock(now, numerator, denominator);
    if (error.code != EWaitError::none) return error;
    if (now >= m_deadline) return {};
    if (!internal::scale_wait_duration(m_deadline - now, m_nanoseconds_numerator,
        m_nanoseconds_denominator, remaining))
        return { EWaitError::invalid_argument, 0u };
    return {};
}

//==============================================================================
//  Finite atomic wait word primitive
//==============================================================================

WaitWordResult wait_while_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t expected, const CWaitDeadline& deadline) noexcept
{
    if (!deadline.is_valid()) return internal::clock_error_result(deadline.error());
    //  Always check time, even if the word keeps changing. Predicate helpers
    //  already check satisfaction; the native call supplies compare-and-park.
    std::uint64_t remaining = 0u;
    const WaitError error = deadline.remaining_nanoseconds(remaining);
    if (error.code != EWaitError::none) return internal::clock_error_result(error);
    if (remaining == 0u) return { EWaitWordStatus::timed_out, 0u };

#if MV_PLATFORM_WINDOWS
    const BOOL result = WaitOnAddress(internal::wait_address_from_word(word),
        const_cast<std::uint32_t*>(&expected), sizeof(expected),
        internal::finite_wait_milliseconds(remaining));
    if (result != FALSE) return { EWaitWordStatus::awoken_or_changed, 0u };
    const auto native_error = GetLastError();
    if (native_error != ERROR_TIMEOUT)
        return { EWaitWordStatus::native_failure, native_error };
#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
    const std::uint64_t seconds = deadline.m_deadline / 1000000000u;
    const std::uint64_t nanoseconds = deadline.m_deadline % 1000000000u;
    long result = 0;
    //  SYS_futex uses the kernel's long-sized timespec, NOT a libc timespec
    //  that may have been enlarged by _TIME_BITS=64 on a 32-bit ABI.
#if defined(SYS_futex_time64)
    struct KernelTimespec64 { std::int64_t seconds; std::int64_t nanoseconds; };
    const KernelTimespec64 timeout{ static_cast<std::int64_t>(seconds), static_cast<std::int64_t>(nanoseconds) };
    result = ::syscall(SYS_futex_time64, internal::futex_address_from_word(word),
        FUTEX_WAIT_BITSET_PRIVATE, expected, &timeout, nullptr, FUTEX_BITSET_MATCH_ANY);
    if ((result < 0) && (errno == ENOSYS))
#endif
    {
        if (seconds > static_cast<std::uint64_t>(std::numeric_limits<long>::max()))
            return { EWaitWordStatus::unsupported, 0u };
        struct KernelTimespec { long seconds; long nanoseconds; };
        const KernelTimespec timeout{ static_cast<long>(seconds), static_cast<long>(nanoseconds) };
        result = ::syscall(SYS_futex, internal::futex_address_from_word(word),
            FUTEX_WAIT_BITSET_PRIVATE, expected, &timeout, nullptr, FUTEX_BITSET_MATCH_ANY);
    }
    if (result == 0) return { EWaitWordStatus::awoken_or_changed, 0u };
    const int native_error = errno;
    if ((native_error == EAGAIN) || (native_error == EINTR))
        return { EWaitWordStatus::awoken_or_changed, 0u };
    if (native_error == ENOSYS) return { EWaitWordStatus::unsupported, static_cast<std::uint32_t>(native_error) };
    if (native_error != ETIMEDOUT)
        return { EWaitWordStatus::native_failure, static_cast<std::uint32_t>(native_error) };
#elif MV_PLATFORM_MAC_OS
    const int result = os_sync_wait_on_address_with_deadline(
        internal::wait_address_from_word(word), expected, sizeof(expected),
        OS_SYNC_WAIT_ON_ADDRESS_NONE, OS_CLOCK_MACH_ABSOLUTE_TIME, deadline.m_deadline);
    if (result >= 0) return { EWaitWordStatus::awoken_or_changed, 0u };
    const int native_error = errno;
    if (native_error == EINTR) return { EWaitWordStatus::awoken_or_changed, 0u };
    if (native_error != ETIMEDOUT)
        return { EWaitWordStatus::native_failure, static_cast<std::uint32_t>(native_error) };
#else
    return { EWaitWordStatus::unsupported, 0u };
#endif
    //  A finite chunk or an early native timeout is not the overall deadline.
    const WaitError final_error = deadline.remaining_nanoseconds(remaining);
    if (final_error.code != EWaitError::none) return internal::clock_error_result(final_error);
    return { remaining == 0u ? EWaitWordStatus::timed_out : EWaitWordStatus::awoken_or_changed, 0u };
}

//==============================================================================
//  Atomic wait word primitive implementations
//==============================================================================

#if MV_PLATFORM_WINDOWS

void wait_while_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t expected) noexcept
{
    //  WaitOnAddress performs the compare-and-park operation. Failure,
    //  mismatch, and spurious return are normal at this abstraction level.
    (void)WaitOnAddress(
        internal::wait_address_from_word(word),
        static_cast<void*>(const_cast<std::uint32_t*>(&expected)),
        sizeof(expected), INFINITE);
}

void wake_one_waiter(const std::atomic<std::uint32_t>& word) noexcept
{
    WakeByAddressSingle(internal::wait_address_from_word(word));
}

void wake_all_waiters(const std::atomic<std::uint32_t>& word) noexcept
{
    WakeByAddressAll(internal::wait_address_from_word(word));
}

#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

void wait_while_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t expected) noexcept
{
    const std::uint32_t* const address = internal::futex_address_from_word(word);

    (void)internal::futex_wait_private(address, expected);
}

void wake_one_waiter(const std::atomic<std::uint32_t>& word) noexcept
{
    const std::uint32_t* const address = internal::futex_address_from_word(word);

    (void)internal::futex_wake_private(address, 1);
}

void wake_all_waiters(const std::atomic<std::uint32_t>& word) noexcept
{
    const std::uint32_t* const address = internal::futex_address_from_word(word);

    (void)internal::futex_wake_private(address, INT_MAX);
}

#elif MV_PLATFORM_MAC_OS

void wait_while_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t expected) noexcept
{
    //  The wait may return for wake, mismatch, interruption, or error.
    //  Callers must re-check their controlling predicate.
    (void)os_sync_wait_on_address(
        internal::wait_address_from_word(word),
        static_cast<std::uint64_t>(expected),
        sizeof(std::uint32_t),
        OS_SYNC_WAIT_ON_ADDRESS_NONE);
}

void wake_one_waiter(const std::atomic<std::uint32_t>& word) noexcept
{
    (void)os_sync_wake_by_address_any(
        internal::wait_address_from_word(word),
        sizeof(std::uint32_t),
        OS_SYNC_WAKE_BY_ADDRESS_NONE);
}

void wake_all_waiters(const std::atomic<std::uint32_t>& word) noexcept
{
    (void)os_sync_wake_by_address_all(
        internal::wait_address_from_word(word),
        sizeof(std::uint32_t),
        OS_SYNC_WAKE_BY_ADDRESS_NONE);
}

#else

void wait_while_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t expected) noexcept
{
    (void)word;
    (void)expected;
}

void wake_one_waiter(const std::atomic<std::uint32_t>& word) noexcept
{
    (void)word;
}

void wake_all_waiters(const std::atomic<std::uint32_t>& word) noexcept
{
    (void)word;
}

#error "platform::threading wait-word is not implemented for this platform."

#endif

//==============================================================================
//  Atomic wait word predicate implementations
//==============================================================================

void wait_until_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t value) noexcept
{
    for (;;)
    {
        const std::uint32_t seen = word.load(std::memory_order_acquire);

        if (seen == value)
        {
            break;
        }

        platform::threading::wait_while_equal(word, seen);
    }
}

std::uint32_t wait_until_not_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t value) noexcept
{
    for (;;)
    {
        const std::uint32_t seen = word.load(std::memory_order_acquire);

        if (seen != value)
        {
            return seen;
        }

        platform::threading::wait_while_equal(word, value);
    }
}

static WaitResult wait_word_predicate(const std::atomic<std::uint32_t>& word, const std::uint32_t value, const CWaitDeadline& deadline, const bool equal) noexcept
{
    if (!deadline.is_valid())
    {
        const auto error = deadline.error();
        return { internal::wait_error_status(error.code), word.load(std::memory_order_acquire), error.native_error };
    }
    return internal::wait_predicate_loop(word, value, equal, false,
        [&](const std::uint32_t seen) noexcept { return wait_while_equal(word, seen, deadline); });
}

WaitResult wait_until_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t value, const CWaitDeadline& deadline) noexcept
{
    return wait_word_predicate(word, value, deadline, true);
}

WaitResult wait_until_not_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t value, const CWaitDeadline& deadline) noexcept
{
    return wait_word_predicate(word, value, deadline, false);
}

}   //  namespace platform::threading
