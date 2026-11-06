
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_cadence.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Anchored 120 Hz opportunities in awake time, skipping overdue periods.

#pragma once

#include <limits>
#include "platform/threading/awake_clock.hpp"

namespace host
{

class CHostCadence final
{
public:
    explicit CHostCadence(const platform::threading::SAwakeClock clock = {}) noexcept : m_clock{ clock } {}

    [[nodiscard]] platform::threading::WaitError remaining(std::uint64_t& duration) noexcept
    {
        duration = 0u;
        std::uint64_t now = 0u;
        const platform::threading::WaitError error = m_clock.read(now);
        if (error.code != platform::threading::EWaitError::none)
        {
            return error;
        }
        if (!m_started)
        {
            m_origin = m_last = now;
            m_started = true;
        }
        if (now < m_last)
        {
            return { platform::threading::EWaitError::native_failure, 0u };
        }
        m_last = now;
        //  Round each absolute tick upward; never accumulate rounded periods.
        const std::uint64_t elapsed = now - m_origin;
        const std::uint64_t seconds = elapsed / 1000000000u;
        const std::uint64_t slot = (((elapsed % 1000000000u) * 120u) / 1000000000u) + 1u;
        const std::uint64_t fraction = ((slot * 1000000000u) + 119u) / 120u;
        const std::uint64_t whole = seconds * 1000000000u;
        const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        if ((fraction > (maximum - whole)) || ((whole + fraction) > (maximum - m_origin)))
        {
            return { platform::threading::EWaitError::overflow, 0u };
        }
        duration = (whole + fraction) - elapsed;
        return {};
    }

private:
    platform::threading::SAwakeClock m_clock;
    std::uint64_t m_origin{ 0u }, m_last{ 0u };
    bool m_started{ false };
};

}   //  namespace host
