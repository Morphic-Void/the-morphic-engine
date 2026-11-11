
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   performance_counter.hpp
//  Author: Ritchie Brannan
//  Date:   12 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Monotonic interval counter wrapper.
//  Not wall-clock or calendar time.

#pragma once

#ifndef PERFORMANCE_COUNTER_HPP_INCLUDED
#define PERFORMANCE_COUNTER_HPP_INCLUDED

#include <cstdint>

namespace platform::system
{

//==============================================================================
//  CPerfCounter
//==============================================================================

class CPerfCounter
{
public:

    CPerfCounter() noexcept;

    bool update() noexcept;

    bool is_zero() const noexcept;
    std::uint64_t query_value() const noexcept;

    std::uint64_t query_delta() const noexcept;
    std::uint64_t update_delta() noexcept;

    bool query_delta(std::uint64_t& out_delta) const noexcept;
    bool update_delta(std::uint64_t& out_delta) noexcept;

private:

    std::uint64_t m_value;
};

//==============================================================================
//  CPerfCountConversion
//==============================================================================

class CPerfCountConversion
{
public:

    CPerfCountConversion() noexcept;

    bool init() noexcept;

    //  Manually sets the raw-count to nanosecond conversion ratio.
    //  Normal platform code should use init().
    //  Intended for tests, injected platform data, or specialised callers
    //  that already own valid platform timing conversion data.
    //  The ratio is reduced before storage.
    bool set_nanosecond_ratio(const std::uint64_t numerator, const std::uint64_t denominator, const std::uint64_t ticks_per_second) noexcept;

    bool is_valid() const noexcept;
    std::uint64_t query_ticks_per_second() const noexcept;

    //  Floating-point conversions for profiling, frame timing, and display.

    bool to_seconds_f32(const std::uint64_t delta, float& out_seconds) const noexcept;

    bool to_milliseconds_f32(const std::uint64_t delta, float& out_milliseconds) const noexcept;

    //  Display-oriented frequency. Returns false for sub-microsecond intervals.
    bool to_frequency_f32( const std::uint64_t delta, float& out_hertz) const noexcept;

    //  Integer conversions for timeout, wait-budget, and diagnostic paths.
    bool to_nanoseconds_u64(const std::uint64_t delta, std::uint64_t& out_nanoseconds) const noexcept;
    bool to_microseconds_u64(const std::uint64_t delta, std::uint64_t& out_microseconds) const noexcept;
    bool to_milliseconds_u64(const std::uint64_t delta, std::uint64_t& out_milliseconds) const noexcept;

private:

    void clear() noexcept;

    bool to_seconds_f64(const std::uint64_t delta, double& out_seconds) const noexcept;
    bool to_milliseconds_f64(const std::uint64_t delta, double& out_milliseconds) const noexcept;

    std::uint64_t m_nanoseconds_numerator;
    std::uint64_t m_nanoseconds_denominator;
    std::uint64_t m_ticks_per_second;
};

//==============================================================================
//  Delta calculation
//==============================================================================

bool calculate_perf_counter_delta(const CPerfCounter begin, const CPerfCounter end, std::uint64_t& out_delta) noexcept;

}   //  namespace platform::system

#endif  //  #ifndef PERFORMANCE_COUNTER_HPP_INCLUDED
