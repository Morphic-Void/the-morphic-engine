
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    awake_clock.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Checked observation of the same awake clock used by CWaitDeadline.

#pragma once

#include "platform/threading/wait_deadline.hpp"

namespace platform::threading
{

[[nodiscard]] WaitError query_awake_nanoseconds(std::uint64_t& value) noexcept;

//  Local clock injection for deterministic accounting tests. The default always
//  uses the native awake clock; failure never selects another time source.
struct SAwakeClock
{
    using FQuery = WaitError (*)(void* const, std::uint64_t&) noexcept;
    void* context{ nullptr };
    FQuery query{ nullptr };

    [[nodiscard]] WaitError read(std::uint64_t& value) const noexcept
    {
        return query ? query(context, value) : query_awake_nanoseconds(value);
    }
};

}   //  namespace platform::threading
