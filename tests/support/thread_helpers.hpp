
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    thread_helpers.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#pragma once

#ifndef MORPHIC_TEST_THREAD_HELPERS_HPP_INCLUDED
#define MORPHIC_TEST_THREAD_HELPERS_HPP_INCLUDED

#include <cstdint>

#include "platform/platform_defines.hpp"
#include "platform/system/performance_counter.hpp"
#include "platform/threading/processor_relax.hpp"

namespace tests
{

//  Echo POD messages through the real worker transport, retaining its lifecycle and failure signals.
[[nodiscard]] std::uint32_t MV_STD_ABI_CALL echo_pod_messages(void* data) noexcept;

//  Stop at the first satisfied predicate. Receiving any reply ends the wait;
//  callers validate it separately, so a mismatched reply fails without retrying.
template<typename TPredicate>
bool wait_until(const platform::system::CPerfCountConversion& conversion, TPredicate&& predicate,
    const std::uint32_t timeout_seconds = 5u) noexcept(noexcept(predicate()))
{
    platform::system::CPerfCounter timer;
    (void)timer.update();
    while (timer.query_delta() < (conversion.query_ticks_per_second() * timeout_seconds))
    {
        if (predicate()) return true;
        platform::threading::processor_relax();
    }
    return false;
}

}   //  namespace tests

#endif  //  MORPHIC_TEST_THREAD_HELPERS_HPP_INCLUDED
