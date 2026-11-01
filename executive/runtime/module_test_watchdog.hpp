
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_test_watchdog.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  One awake-time budget per generic module test; checks never renew it.

#pragma once

#include "platform/threading/wait_deadline.hpp"

namespace executive
{

//  The deadline type is a private test seam. Production uses the native
//  awake-time deadline; tests can control its clock and explicit failures.
template<typename TDeadline = platform::threading::CWaitDeadline>
class TModuleTestWatchdog
{
public:
    enum class EState { pending, expired, failed };

    [[nodiscard]] platform::threading::WaitError start() noexcept
    {
        return m_deadline.set_after_milliseconds(30000u);
    }

    [[nodiscard]] EState check() const noexcept
    {
        std::uint64_t remaining;
        const auto error = m_deadline.remaining_nanoseconds(remaining);
        if (error.code != platform::threading::EWaitError::none) return EState::failed;
        return remaining == 0u ? EState::expired : EState::pending;
    }

private:
    TDeadline m_deadline;
};

}   //  namespace executive
