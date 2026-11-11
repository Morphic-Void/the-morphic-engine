
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CWaitPredicate.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Engine-facing process-local 32-bit wait predicate.
//
//  CWaitPredicate owns a controlled atomic 32-bit predicate word. Waiters can
//  block until the word becomes equal to a value, or until it becomes different
//  from a value. Native backends use platform wait-word support where available;
//  the fallback backend parks through CParkingGate.
//
//  Construction starts in the control-released state. acquire_control()
//  publishes an initial controlled word value. release_control() publishes the
//  control-released sentinel and wakes or releases waiters.
//
//  One 32-bit value is reserved as the control-released sentinel. Predicate
//  waits stop waiting after observing this state. Ordinary word modification
//  cannot store the sentinel. increment() and decrement() wrap through the
//  controlled value domain and skip the sentinel.
//
//  The combined modification-and-wake functions are the expected producer-side
//  operations. Standalone word modification and standalone wake functions exist
//  for protocols that intentionally split state transition from notification.
//
//  poke_epoch() is a semantic alias for increment(), intended for call sites where
//  the operation is an epoch poke rather than caller-meaningful arithmetic.

#pragma once

#ifndef CWAIT_PREDICATE_HPP_INCLUDED
#define CWAIT_PREDICATE_HPP_INCLUDED

#include <atomic>       //  std::atomic
#include <cstdint>      //  std::uint32_t

#include "threading/CParkingGate.hpp"
#include "platform/threading/wait_word.hpp"

namespace threading
{

using platform::threading::CWaitDeadline;
using platform::threading::EWaitStatus;
using platform::threading::WaitResult;

//==============================================================================
//  CWaitPredicate
//==============================================================================

class CWaitPredicate
{
public:

    //  Constructor and destructor
    CWaitPredicate() noexcept;
    ~CWaitPredicate() noexcept;

    //  Deleted lifetime
    CWaitPredicate(const CWaitPredicate&) = delete;
    CWaitPredicate& operator=(const CWaitPredicate&) = delete;
    CWaitPredicate(CWaitPredicate&&) = delete;
    CWaitPredicate& operator=(CWaitPredicate&&) = delete;

    //  Status
    bool is_valid() const noexcept;
    bool has_control() const noexcept;
    bool is_control_released() const noexcept;

    //  Control
    bool acquire_control(const std::uint32_t initial_value = 0u) noexcept;
    void release_control() noexcept;

    //  Word observation
    //
    //  Returns the raw predicate word, including the control-released sentinel.
    std::uint32_t get_word() const noexcept;

    //  Predicate waits
    //
    //  wait_until_equal() returns true only after seeing the requested controlled value.
    //  It returns false on invalid use, sentinel request, or control release.
    //
    //  wait_until_not_equal() returns the first observed word different from value.
    //  Control release also satisfies this wait; callers typically treat the returned
    //  word as the next local epoch.
    bool wait_until_equal(CParkingTicket& ticket, const std::uint32_t value) noexcept;
    std::uint32_t wait_until_not_equal(CParkingTicket& ticket, const std::uint32_t value) noexcept;

    //  Explicit timed outcomes. Control release is distinct from inequality.
    //  A final acquire observation of release or satisfaction wins over timeout.
    //  Keep the predicate alive until all waiters AND producers have finished;
    //  release_control() is a notification, not a join. Quiesce users before
    //  acquiring a new control lifetime. Tickets remain per-waiter resources.
    [[nodiscard]] WaitResult wait_until_equal(CParkingTicket& ticket, const std::uint32_t value, const CWaitDeadline& deadline) noexcept;
    [[nodiscard]] WaitResult wait_until_not_equal(CParkingTicket& ticket, const std::uint32_t value, const CWaitDeadline& deadline) noexcept;

    //  Producer-side operations.
    bool set_and_wake_one(const std::uint32_t value) noexcept;
    bool set_and_wake_all(const std::uint32_t value) noexcept;
    bool increment_and_wake_one() noexcept;
    bool increment_and_wake_all() noexcept;
    bool decrement_and_wake_one() noexcept;
    bool decrement_and_wake_all() noexcept;
    bool poke_epoch_and_wake_one() noexcept { return increment_and_wake_one(); }
    bool poke_epoch_and_wake_all() noexcept { return increment_and_wake_all(); }

    //  Word modification without waiter notification. Use with caution.
    //
    //  These functions do not wake, release, or otherwise activate waiting threads.
    //  See the top-of-file contract for sentinel and wrapping semantics.
    bool set(const std::uint32_t value) noexcept;
    bool increment() noexcept;
    bool decrement() noexcept;
    bool poke_epoch() noexcept { return increment(); }

    //  Waiter notification without word modification. Use with caution.
    //
    //  These functions do not modify the predicate word.
    bool wake_one_waiter() noexcept;
    bool wake_all_waiters() noexcept;

private:

    WaitResult wait_timed(std::uint32_t value, const CWaitDeadline& deadline, bool equal) noexcept;

    static constexpr std::uint32_t k_control_released_word = ~std::uint32_t{ 0u };
    static constexpr std::uint32_t k_max_predicate_word = k_control_released_word - 1u;

    static std::uint32_t next_predicate_word(const std::uint32_t value) noexcept;
    static std::uint32_t previous_predicate_word(const std::uint32_t value) noexcept;

    void wake_one_waiter_unchecked() noexcept;
    void wake_all_waiters_unchecked() noexcept;

    std::atomic<std::uint32_t> m_word{ k_control_released_word };

    CParkingGate m_fallback_gate;

    bool m_valid = false;
    bool m_has_control = false;
};

}   //  namespace threading

#endif  //  #ifndef CWAIT_PREDICATE_HPP_INCLUDED
