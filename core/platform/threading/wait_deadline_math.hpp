
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    wait_deadline_math.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Internal integer conversion shared by deadline backends and boundary tests.

#pragma once

#ifndef WAIT_DEADLINE_MATH_HPP_INCLUDED
#define WAIT_DEADLINE_MATH_HPP_INCLUDED

#include <cstdint>
#include <limits>

namespace platform::threading::internal
{

//  floor(value * numerator / denominator), preserving output on failure.
static inline bool scale_awake_observation(const std::uint64_t value, const std::uint32_t numerator, const std::uint32_t denominator, std::uint64_t& result) noexcept
{
    if (!numerator || !denominator) return false;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto whole = value / denominator;
    const auto tail = ((value % denominator) * numerator) / denominator;
    if (whole > ((maximum - tail) / numerator)) return false;
    result = (whole * numerator) + tail;
    return true;
}

//  ceil(value * numerator / denominator), without an overflowing intermediate.
//  The remainder product fits because both ratio terms are 32-bit.
static inline bool scale_wait_duration(const std::uint64_t value, const std::uint32_t numerator, const std::uint32_t denominator, std::uint64_t& result) noexcept
{
    result = 0u;
    if ((numerator == 0u) || (denominator == 0u)) return false;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto whole = value / denominator;
    if (whole > (maximum / numerator)) return false;
    const auto remainder = (value % denominator) * numerator;
    const auto tail = (remainder / denominator) + (((remainder % denominator) != 0u) ? 1u : 0u);
    const auto head = whole * numerator;
    if (tail > (maximum - head)) return false;
    result = head + tail;
    return true;
}

//  Windows reserves UINT32_MAX for INFINITE. Large budgets use finite chunks.
static inline std::uint32_t finite_wait_milliseconds(const std::uint64_t nanoseconds) noexcept
{
    const auto rounded = (nanoseconds / 1000000u) + (((nanoseconds % 1000000u) != 0u) ? 1u : 0u);
    constexpr std::uint32_t maximum = 0xfffffffeu;
    return (rounded > maximum) ? maximum : static_cast<std::uint32_t>(rounded);
}

}   //  namespace platform::threading::internal

#endif  //  #ifndef WAIT_DEADLINE_MATH_HPP_INCLUDED
