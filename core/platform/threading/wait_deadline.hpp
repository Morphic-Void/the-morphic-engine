
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    wait_deadline.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Process-local deadlines in awake monotonic time. System suspend pauses the
//  budget; thread descheduling does not. These are not performance timestamps.

#pragma once

#ifndef WAIT_DEADLINE_HPP_INCLUDED
#define WAIT_DEADLINE_HPP_INCLUDED

#include <atomic>
#include <cstdint>

namespace platform::threading
{

enum class EWaitError
{
    none,
    invalid_argument,
    unsupported,
    native_failure
};

struct WaitError
{
    EWaitError code{ EWaitError::none };
    std::uint32_t native_error{ 0u };
};

struct WaitWordResult;

class CWaitDeadline
{
public:
    //  Default construction is invalid. Copies retain the same deadline.
    CWaitDeadline() noexcept = default;

    //  Zero means an immediate check, never infinity. Failed construction
    //  invalidates the previous deadline and retains the specific error.
    //  Durations must fit in INT64_MAX nanoseconds and in the native clock.
    [[nodiscard]] WaitError set_after_nanoseconds(std::uint64_t duration) noexcept;
    [[nodiscard]] WaitError set_after_milliseconds(std::uint64_t duration) noexcept;
    [[nodiscard]] WaitError remaining_nanoseconds(std::uint64_t& remaining) const noexcept;
    [[nodiscard]] bool is_valid() const noexcept { return m_error.code == EWaitError::none; }
    [[nodiscard]] WaitError error() const noexcept { return m_error; }

private:
    friend WaitWordResult wait_while_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t expected, const CWaitDeadline& deadline) noexcept;

    std::uint64_t m_deadline{ 0u };
    std::uint32_t m_nanoseconds_numerator{ 1u };
    std::uint32_t m_nanoseconds_denominator{ 1u };
    WaitError m_error{ EWaitError::invalid_argument, 0u };
};

}   //  namespace platform::threading

#endif  //  #ifndef WAIT_DEADLINE_HPP_INCLUDED
