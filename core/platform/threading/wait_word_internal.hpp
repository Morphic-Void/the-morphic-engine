
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    wait_word_internal.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Internal predicate loop. A supplied wait attempt allows deterministic tests
//  of publication at the native-return boundary without production test hooks.

#pragma once

#ifndef WAIT_WORD_INTERNAL_HPP_INCLUDED
#define WAIT_WORD_INTERNAL_HPP_INCLUDED

#include "platform/threading/wait_word.hpp"

namespace platform::threading::internal
{

static inline EWaitStatus wait_error_status(const EWaitError error) noexcept
{
    switch (error)
    {
    case EWaitError::unsupported: return EWaitStatus::unsupported;
    case EWaitError::native_failure: return EWaitStatus::native_failure;
    default: return EWaitStatus::invalid_argument;
    }
}

//  Validate arguments before entering. Every terminal native result is followed
//  by an acquire observation; release/satisfaction wins over timeout or error.
template<typename FWait>
static WaitResult wait_predicate_loop(const std::atomic<std::uint32_t>& word, const std::uint32_t value, const bool equal, const bool controlled, FWait wait) noexcept
{
    WaitWordResult result{ EWaitWordStatus::awoken_or_changed, 0u };
    for (;;)
    {
        const auto seen = word.load(std::memory_order_acquire);
        if (controlled && (seen == ~std::uint32_t{ 0u }))
            return { EWaitStatus::control_released, seen, 0u };
        if ((seen == value) == equal)
            return { EWaitStatus::satisfied, seen, 0u };
        switch (result.status)
        {
        case EWaitWordStatus::timed_out: return { EWaitStatus::timed_out, seen, 0u };
        case EWaitWordStatus::invalid_argument: return { EWaitStatus::invalid_argument, seen, result.native_error };
        case EWaitWordStatus::unsupported: return { EWaitStatus::unsupported, seen, result.native_error };
        case EWaitWordStatus::native_failure: return { EWaitStatus::native_failure, seen, result.native_error };
        default: break;
        }
        result = wait(seen);
    }
}

}   //  namespace platform::threading::internal

#endif  //  #ifndef WAIT_WORD_INTERNAL_HPP_INCLUDED
