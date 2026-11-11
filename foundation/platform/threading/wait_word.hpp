
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    wait_word.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Low-level atomic wait/wake primitives for 32-bit wait words.
//
//  The primitive functions expose platform wait-on-address style blocking over
//  an externally owned atomic word. Wakeups are not remembered. A single wait
//  may return spuriously or after the observed word has changed.
//
//  The predicate helpers build simple acquire-load wait loops over those
//  primitives. wait_until_equal() waits until a specific value is observed.
//  wait_until_not_equal() waits until a different value is observed and returns
//  that value.
//
//  The wait and wake primitives do not modify the atomic word or provide
//  synchronization by themselves. Ordering is provided by the caller's atomic
//  operations and by the acquire loads used by the predicate helpers.
//
//  The wait word must remain alive and stable for the duration of any wait.

#pragma once

#ifndef WAIT_WORD_HPP_INCLUDED
#define WAIT_WORD_HPP_INCLUDED

#include <atomic>       //  std::atomic
#include <cstdint>      //  std::uint32_t

#include "platform/threading/wait_deadline.hpp"

namespace platform::threading
{

enum class EWaitStatus
{
    satisfied,
    timed_out,
    control_released,
    invalid_argument,
    unsupported,
    native_failure
};

struct WaitResult
{
    EWaitStatus status{ EWaitStatus::invalid_argument };
    std::uint32_t observed{ 0u };
    std::uint32_t native_error{ 0u };
};

enum class EWaitWordStatus
{
    awoken_or_changed,
    timed_out,
    invalid_argument,
    unsupported,
    native_failure
};

struct WaitWordResult
{
    EWaitWordStatus status{ EWaitWordStatus::invalid_argument };
    std::uint32_t native_error{ 0u };
};

//==============================================================================
//  Atomic wait word primitives
//==============================================================================

//  Blocks the calling thread while word equals expected.
void wait_while_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t expected) noexcept;

//  One finite compare-and-park attempt. Success includes interruption and
//  spurious return: recheck the predicate and reuse the same deadline.
[[nodiscard]] WaitWordResult wait_while_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t expected, const CWaitDeadline& deadline) noexcept;

//  Wakes one current waiter blocked on word, if one exists.
void wake_one_waiter(const std::atomic<std::uint32_t>& word) noexcept;

//  Wakes all current waiters blocked on word, if any exist.
void wake_all_waiters(const std::atomic<std::uint32_t>& word) noexcept;

//==============================================================================
//  Atomic wait word predicates
//==============================================================================

//  Wait until word is seen to equal value.
//  This waits for a specific visible value, not just for progress.
void wait_until_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t value) noexcept;

//  Wait until word is seen to differ from value.
//  Returns the seen non-equal value.
std::uint32_t wait_until_not_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t value) noexcept;

//  Timed predicates perform a final acquire load before reporting timeout.
//  The word must survive all wait AND wake calls; a return does not join peers.
[[nodiscard]] WaitResult wait_until_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t value, const CWaitDeadline& deadline) noexcept;
[[nodiscard]] WaitResult wait_until_not_equal(const std::atomic<std::uint32_t>& word, const std::uint32_t value, const CWaitDeadline& deadline) noexcept;

}   //  namespace platform::threading

#endif  //  #ifndef WAIT_WORD_HPP_INCLUDED
