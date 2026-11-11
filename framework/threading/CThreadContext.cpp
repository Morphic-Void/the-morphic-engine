
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CThreadContext.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Worker waits with ordinary or explicitly preserved lifecycle state.

#include "framework/threading/CThreadContext.hpp"

#include "debug/macros.hpp"

namespace threading
{

void CThreadContext::startup() noexcept
{
    m_resources.control_state.mark_startup();
}

WaitResult CThreadContext::wait_for_new_epoch(const std::uint32_t epoch, const CWaitDeadline& deadline) noexcept
{
    mark_waiting();
    const WaitResult result = m_resources.wait_predicate.wait_until_not_equal(m_resources.parking_ticket, epoch, deadline);
    mark_running();
    return result;
}

std::uint32_t CThreadContext::wait_for_new_epoch(const std::uint32_t epoch) noexcept
{
    MV_TRACE("Waiting epoch={}", epoch);

    mark_waiting();
    const std::uint32_t new_epoch = m_resources.wait_predicate.wait_until_not_equal(m_resources.parking_ticket, epoch);
    mark_running();

    MV_TRACE("Running epoch={}", new_epoch);

    return new_epoch;
}

WaitResult CThreadContext::wait_for_new_epoch_preserving_state(
    const std::uint32_t epoch, const CWaitDeadline& deadline) noexcept
{
    return m_resources.wait_predicate.wait_until_not_equal(m_resources.parking_ticket, epoch, deadline);
}

std::uint32_t CThreadContext::wait_for_new_epoch_preserving_state(const std::uint32_t epoch) noexcept
{
    return m_resources.wait_predicate.wait_until_not_equal(m_resources.parking_ticket, epoch);
}

}   //  namespace threading
