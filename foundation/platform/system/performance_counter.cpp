
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   performance_counter.cpp
//  Author: Ritchie Brannan
//  Date:   12 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.

#include "platform/system/performance_counter.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif // MV_PLATFORM_WINDOWS

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
#include <time.h>
#endif // MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

#if MV_PLATFORM_MAC_OS
#include <mach/kern_return.h>
#include <mach/mach_time.h>
#endif // MV_PLATFORM_MAC_OS

namespace platform::system
{

namespace internal
{

static constexpr std::uint64_t k_u64_max = ~static_cast<std::uint64_t>(0u);
static constexpr std::uint64_t k_counter_delta_high_bit = 1ull << 63u;
static constexpr std::uint64_t k_nanoseconds_per_second = 1000000000ull;
static constexpr std::uint64_t k_nanoseconds_per_millisecond = 1000000ull;
static constexpr std::uint64_t k_nanoseconds_per_microsecond = 1000ull;
static constexpr double k_nanoseconds_per_second_f64 = 1000000000.0;
static constexpr double k_nanoseconds_per_millisecond_f64 = 1000000.0;
static constexpr double k_max_reported_frequency_hz = 1000000.0;
static constexpr double k_min_seconds_for_frequency = 1.0 / k_max_reported_frequency_hz;

//==============================================================================
//  Ratio helpers
//==============================================================================

std::uint64_t calculate_gcd_u64(const std::uint64_t a, const std::uint64_t b) noexcept
{
    std::uint64_t current_a = a;
    std::uint64_t current_b = b;

    while (current_b != 0u)
    {
        const std::uint64_t remainder = current_a % current_b;
        current_a = current_b;
        current_b = remainder;
    }

    return current_a;
}

bool reduce_ratio_u64(std::uint64_t& numerator, std::uint64_t& denominator) noexcept
{
    if (denominator == 0u)
    {
        return false;
    }

    if (numerator == 0u)
    {
        denominator = 1u;
        return true;
    }

    const std::uint64_t gcd = calculate_gcd_u64(numerator, denominator);

    numerator /= gcd;
    denominator /= gcd;

    return true;
}

bool multiply_u64_checked(const std::uint64_t a, const std::uint64_t b, std::uint64_t& out_result) noexcept
{
    out_result = 0u;

    if ((a == 0u) || (b == 0u))
    {
        return true;
    }

    if (a > (k_u64_max / b))
    {
        return false;
    }

    out_result = a * b;
    return true;
}

bool multiply_divide_u64(const std::uint64_t value, const std::uint64_t numerator, const std::uint64_t denominator, std::uint64_t& out_result) noexcept
{
    out_result = 0u;

    if (denominator == 0u)
    {
        return false;
    }

    if ((value == 0u) || (numerator == 0u))
    {
        return true;
    }

    std::uint64_t reduced_value = value;
    std::uint64_t reduced_numerator = numerator;
    std::uint64_t reduced_denominator = denominator;

    std::uint64_t gcd = calculate_gcd_u64(reduced_value, reduced_denominator);

    reduced_value /= gcd;
    reduced_denominator /= gcd;

    gcd = calculate_gcd_u64(reduced_numerator, reduced_denominator);

    reduced_numerator /= gcd;
    reduced_denominator /= gcd;

    std::uint64_t product = 0u;

    if (!multiply_u64_checked(reduced_value, reduced_numerator, product))
    {
        return false;
    }

    out_result = product / reduced_denominator;
    return true;
}

//==============================================================================
//  Linux/Android utility
//==============================================================================

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

bool query_posix_perf_counter_value(const clockid_t clock_id, std::uint64_t& out_value) noexcept
{
    timespec timestamp = {};

    const int result = clock_gettime(clock_id, &timestamp);

    if (result != 0)
    {
        return false;
    }

    if (timestamp.tv_sec < 0)
    {
        return false;
    }

    const std::uint64_t seconds = static_cast<std::uint64_t>(timestamp.tv_sec);
    const std::uint64_t nanoseconds = static_cast<std::uint64_t>(timestamp.tv_nsec);

    if (seconds > (k_u64_max / k_nanoseconds_per_second))
    {
        return false;
    }

    const std::uint64_t second_nanoseconds = seconds * k_nanoseconds_per_second;

    if (nanoseconds > (k_u64_max - second_nanoseconds))
    {
        return false;
    }

    out_value = second_nanoseconds + nanoseconds;
    return true;
}

#endif

//==============================================================================
//  Platform capture
//==============================================================================

bool query_platform_perf_counter_value(std::uint64_t& out_value) noexcept
{
    out_value = 0u;

    //  Platform performance counter capture.

#if MV_PLATFORM_WINDOWS

    LARGE_INTEGER counter = {};
    const BOOL result = QueryPerformanceCounter(&counter);

    if (result == FALSE)
    {
        return false;
    }

    out_value = static_cast<std::uint64_t>(counter.QuadPart);
    return true;

#elif MV_PLATFORM_LINUX

    return query_posix_perf_counter_value(CLOCK_MONOTONIC_RAW, out_value);

#elif MV_PLATFORM_ANDROID

    return query_posix_perf_counter_value(CLOCK_MONOTONIC, out_value);

#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

    return query_posix_perf_counter_value

#elif MV_PLATFORM_MAC_OS

    out_value = static_cast<std::uint64_t>(mach_absolute_time());
    return true;

#else

#error "No platform performance counter implementation is available."

#endif  //  platform performance counter capture
}

//==============================================================================
//  Platform conversion
//==============================================================================

bool init_platform_perf_count_conversion(CPerfCountConversion& conversion) noexcept
{
    //  Platform performance counter conversion.

#if MV_PLATFORM_WINDOWS

    LARGE_INTEGER frequency = {};
    const BOOL result = QueryPerformanceFrequency(&frequency);

    if ((result == FALSE) || (frequency.QuadPart <= 0))
    {
        return false;
    }

    return conversion.set_nanosecond_ratio(
        k_nanoseconds_per_second,
        static_cast<std::uint64_t>(frequency.QuadPart),
        static_cast<std::uint64_t>(frequency.QuadPart));

#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

    return conversion.set_nanosecond_ratio(1u, 1u, k_nanoseconds_per_second);

#elif MV_PLATFORM_MAC_OS

    mach_timebase_info_data_t info = {};
    const kern_return_t result = mach_timebase_info(&info);

    if ((result != KERN_SUCCESS) || (info.numer == 0u) || (info.denom == 0u))
    {
        return false;
    }

    std::uint64_t numerator = static_cast<std::uint64_t>(info.numer);
    std::uint64_t denominator = static_cast<std::uint64_t>(info.denom);

    if (!reduce_ratio_u64(numerator, denominator))
    {
        return false;
    }

    std::uint64_t ticks_per_second = 0u;

    multiply_divide_u64(k_nanoseconds_per_second, denominator, numerator, ticks_per_second);

    return conversion.set_nanosecond_ratio(numerator, denominator, ticks_per_second);

#else

#error "No platform performance counter conversion implementation is available."

#endif  //  platform performance counter conversion
}

}   //  namespace internal

//==============================================================================
//  CPerfCounter
//==============================================================================

CPerfCounter::CPerfCounter() noexcept : m_value(0u) {}

bool CPerfCounter::update() noexcept
{
    std::uint64_t value = 0u;

    if (!internal::query_platform_perf_counter_value(value))
    {
        m_value = 0u;
        return false;
    }

    m_value = value;
    return true;
}

bool CPerfCounter::is_zero() const noexcept
{
    return m_value == 0u;
}

std::uint64_t CPerfCounter::query_value() const noexcept
{
    return m_value;
}

std::uint64_t CPerfCounter::query_delta() const noexcept
{
    std::uint64_t delta = 0u;
    query_delta(delta);
    return delta;
}

std::uint64_t CPerfCounter::update_delta() noexcept
{
    std::uint64_t delta = 0u;
    update_delta(delta);
    return delta;
}

bool CPerfCounter::query_delta(std::uint64_t& out_delta) const noexcept
{
    out_delta = 0u;

    CPerfCounter current;
    if (!current.update())
    {
        return false;
    }

    return calculate_perf_counter_delta(*this, current, out_delta);
}

bool CPerfCounter::update_delta(std::uint64_t& out_delta) noexcept
{
    out_delta = 0u;

    CPerfCounter current;
    if (!current.update())
    {
        m_value = 0u;
        return false;
    }

    const bool valid = calculate_perf_counter_delta(*this, current, out_delta);

    m_value = current.m_value;

    return valid;
}

//==============================================================================
//  CPerfCountConversion
//==============================================================================

CPerfCountConversion::CPerfCountConversion() noexcept :
    m_nanoseconds_numerator(0u),
    m_nanoseconds_denominator(0u),
    m_ticks_per_second(0u)
{
}

bool CPerfCountConversion::init() noexcept
{
    clear();

    if (!internal::init_platform_perf_count_conversion(*this))
    {
        clear();
        return false;
    }

    return is_valid();
}

bool CPerfCountConversion::set_nanosecond_ratio(
    const std::uint64_t numerator,
    const std::uint64_t denominator,
    const std::uint64_t ticks_per_second) noexcept
{
    clear();

    std::uint64_t reduced_numerator = numerator;
    std::uint64_t reduced_denominator = denominator;

    if (!internal::reduce_ratio_u64(reduced_numerator, reduced_denominator))
    {
        return false;
    }

    if ((reduced_numerator == 0u) || (reduced_denominator == 0u))
    {
        return false;
    }

    m_nanoseconds_numerator = reduced_numerator;
    m_nanoseconds_denominator = reduced_denominator;
    m_ticks_per_second = ticks_per_second;

    return true;
}

bool CPerfCountConversion::is_valid() const noexcept
{
    return (m_nanoseconds_numerator != 0u) && (m_nanoseconds_denominator != 0u);
}

std::uint64_t CPerfCountConversion::query_ticks_per_second() const noexcept
{
    return m_ticks_per_second;
}

bool CPerfCountConversion::to_seconds_f32(const std::uint64_t delta, float& out_seconds) const noexcept
{
    out_seconds = 0.0f;

    double seconds = 0.0;

    if (!to_seconds_f64(delta, seconds))
    {
        return false;
    }

    out_seconds = static_cast<float>(seconds);
    return true;
}

bool CPerfCountConversion::to_milliseconds_f32(const std::uint64_t delta, float& out_milliseconds) const noexcept
{
    out_milliseconds = 0.0f;

    double milliseconds = 0.0;

    if (!to_milliseconds_f64(delta, milliseconds))
    {
        return false;
    }

    out_milliseconds = static_cast<float>(milliseconds);
    return true;
}

bool CPerfCountConversion::to_frequency_f32(const std::uint64_t delta, float& out_hertz) const noexcept
{
    out_hertz = 0.0f;

    if (delta == 0u)
    {
        return false;
    }

    double seconds = 0.0;

    if (!to_seconds_f64(delta, seconds))
    {
        return false;
    }

    if (seconds < internal::k_min_seconds_for_frequency)
    {
        return false;
    }

    out_hertz = static_cast<float>(1.0 / seconds);
    return true;
}

bool CPerfCountConversion::to_nanoseconds_u64(const std::uint64_t delta, std::uint64_t& out_nanoseconds) const noexcept
{
    out_nanoseconds = 0u;

    if (!is_valid())
    {
        return false;
    }

    return internal::multiply_divide_u64(
        delta, m_nanoseconds_numerator, m_nanoseconds_denominator, out_nanoseconds);
}

bool CPerfCountConversion::to_microseconds_u64(const std::uint64_t delta, std::uint64_t& out_microseconds) const noexcept
{
    out_microseconds = 0u;

    std::uint64_t nanoseconds = 0u;

    if (!to_nanoseconds_u64(delta, nanoseconds))
    {
        return false;
    }

    out_microseconds = nanoseconds / internal::k_nanoseconds_per_microsecond;

    return true;
}

bool CPerfCountConversion::to_milliseconds_u64(const std::uint64_t delta, std::uint64_t& out_milliseconds) const noexcept
{
    out_milliseconds = 0u;

    std::uint64_t nanoseconds = 0u;

    if (!to_nanoseconds_u64(delta, nanoseconds))
    {
        return false;
    }

    out_milliseconds = nanoseconds / internal::k_nanoseconds_per_millisecond;

    return true;
}

void CPerfCountConversion::clear() noexcept
{
    m_nanoseconds_numerator = 0u;
    m_nanoseconds_denominator = 0u;
    m_ticks_per_second = 0u;
}

bool CPerfCountConversion::to_seconds_f64(const std::uint64_t delta, double& out_seconds) const noexcept
{
    out_seconds = 0.0;

    if (!is_valid())
    {
        return false;
    }

    const double numerator = static_cast<double>(m_nanoseconds_numerator);
    const double denominator = static_cast<double>(m_nanoseconds_denominator) * internal::k_nanoseconds_per_second_f64;

    out_seconds = (static_cast<double>(delta) * numerator) / denominator;

    return true;
}

bool CPerfCountConversion::to_milliseconds_f64(const std::uint64_t delta, double& out_milliseconds) const noexcept
{
    out_milliseconds = 0.0;

    if (!is_valid())
    {
        return false;
    }

    const double numerator = static_cast<double>(m_nanoseconds_numerator);
    const double denominator = static_cast<double>(m_nanoseconds_denominator) * internal::k_nanoseconds_per_millisecond_f64;

    out_milliseconds = (static_cast<double>(delta) * numerator) / denominator;

    return true;
}

//==============================================================================
//  Delta calculation
//==============================================================================

bool calculate_perf_counter_delta(const CPerfCounter begin, const CPerfCounter end, std::uint64_t& out_delta) noexcept
{
    const std::uint64_t delta = end.query_value() - begin.query_value();

    if ((delta & internal::k_counter_delta_high_bit) != 0u)
    {
        out_delta = 0u;
        return false;
    }

    out_delta = delta;
    return true;
}

}   //  namespace platform::system
