
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CThreadContext.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Worker-facing lifecycle, waits and messages over shared resources.

#pragma once

#ifndef CTHREADCONTEXT_HPP_INCLUDED
#define CTHREADCONTEXT_HPP_INCLUDED

#include <utility>

#include "framework/threading/CThreadResources.hpp"
#include "framework/threading/CEndpointContext.hpp"

namespace threading
{

class CThreadContext : public CEndpointContext
{
public:
    explicit CThreadContext(CThreadResources& resources) noexcept : CEndpointContext{ resources }, m_resources{ resources } {}
    ~CThreadContext() noexcept = default;


    void startup() noexcept;
    void mark_waiting() noexcept;
    void mark_running() noexcept;
    [[nodiscard]] bool publish_ready() noexcept { return post(make_pod_message(-1, EndpointReady{ identity() })); }
    void mark_exiting() noexcept;
    void mark_exited() noexcept;
    void mark_failed(const std::uint32_t code) noexcept;
    void advance_heartbeat() noexcept;

    [[nodiscard]] const platform::system::CPerfCountConversion& perf_count_conversion() const noexcept;
    [[nodiscard]] std::uint32_t observe_epoch() const noexcept;
    [[nodiscard]] WaitResult wait_for_new_epoch(const std::uint32_t epoch, const CWaitDeadline& deadline) noexcept;
    std::uint32_t wait_for_new_epoch(const std::uint32_t epoch) noexcept;
    //  Drain/exit waits must not republish Waiting or Running over the caller's state.
    [[nodiscard]] WaitResult wait_for_new_epoch_preserving_state(const std::uint32_t epoch, const CWaitDeadline& deadline) noexcept;
    [[nodiscard]] std::uint32_t wait_for_new_epoch_preserving_state(const std::uint32_t epoch) noexcept;


private:
    CThreadResources& m_resources;
};

inline void CThreadContext::mark_waiting() noexcept
{
    m_resources.control_state.mark_waiting();
}

inline void CThreadContext::mark_running() noexcept
{
    m_resources.control_state.mark_running();
}

inline void CThreadContext::mark_exiting() noexcept
{
    m_resources.control_state.mark_exiting();
}

inline void CThreadContext::mark_exited() noexcept
{
    m_resources.control_state.mark_exited();
}

inline void CThreadContext::mark_failed(const std::uint32_t code) noexcept
{
    m_resources.control_state.mark_failed(code);
}

inline void CThreadContext::advance_heartbeat() noexcept
{
    m_resources.control_state.advance_heartbeat();
}

inline const platform::system::CPerfCountConversion& CThreadContext::perf_count_conversion() const noexcept
{
    return m_resources.perf_count_conversion;
}

inline std::uint32_t CThreadContext::observe_epoch() const noexcept
{
    return m_resources.wait_predicate.get_word();
}

}   //  namespace threading

#endif  //  #ifndef CTHREADCONTEXT_HPP_INCLUDED
