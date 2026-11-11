
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

namespace threading
{

class CThreadContext
{
public:
    explicit CThreadContext(CThreadResources& resources) noexcept : m_resources{ resources } {}
    ~CThreadContext() noexcept = default;

    [[nodiscard]] module_ids::id_type module_id() const noexcept { return m_resources.worker_module_id; }
    [[nodiscard]] thread_ids::id_type thread_id() const noexcept { return m_resources.thread_id; }
    [[nodiscard]] CBatchClient* batch_client() noexcept { return m_resources.batch_client; }

    void startup() noexcept;
    void mark_waiting() noexcept;
    void mark_running() noexcept;
    void mark_exiting() noexcept;
    void mark_exited() noexcept;
    void mark_failed(const std::uint32_t code) noexcept;
    void advance_heartbeat() noexcept;

    [[nodiscard]] bool exit_requested() const noexcept;
    [[nodiscard]] const platform::system::CPerfCountConversion& perf_count_conversion() const noexcept;
    [[nodiscard]] std::uint32_t observe_epoch() const noexcept;
    [[nodiscard]] const modules::SHostExecutionContext* host_execution() const noexcept { return m_resources.host_execution; }
    [[nodiscard]] WaitResult wait_for_new_epoch(const std::uint32_t epoch, const CWaitDeadline& deadline) noexcept;
    std::uint32_t wait_for_new_epoch(const std::uint32_t epoch) noexcept;
    //  Drain/exit waits must not republish Waiting or Running over the caller's state.
    [[nodiscard]] WaitResult wait_for_new_epoch_preserving_state(const std::uint32_t epoch, const CWaitDeadline& deadline) noexcept;
    [[nodiscard]] std::uint32_t wait_for_new_epoch_preserving_state(const std::uint32_t epoch) noexcept;

    bool read(CErasedPodMsg& msg) noexcept;
    bool post(const CErasedPodMsg& msg) noexcept;
    bool post(CErasedOwnerMsg&& msg) noexcept;
    [[nodiscard]] bool pod_submission_poisoned() const noexcept { return m_resources.worker_to_host_msgs.posting_poisoned(); }

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

inline bool CThreadContext::exit_requested() const noexcept
{
    return m_resources.control_state.exit_requested();
}

inline const platform::system::CPerfCountConversion& CThreadContext::perf_count_conversion() const noexcept
{
    return m_resources.perf_count_conversion;
}

inline bool CThreadContext::read(CErasedPodMsg& msg) noexcept
{
    const bool consumed = m_resources.host_to_worker_msgs.read(msg);
    if (consumed) (void)m_resources.owner_wake.signal();
    return consumed;
}

inline std::uint32_t CThreadContext::observe_epoch() const noexcept
{
    return m_resources.wait_predicate.get_word();
}

inline bool CThreadContext::post(const CErasedPodMsg& msg) noexcept
{
    const bool posted = m_resources.worker_to_host_msgs.post(msg);
    if (posted || m_resources.worker_to_host_msgs.posting_poisoned())
    {
        (void)m_resources.owner_wake.signal();
    }
    return posted;
}

inline bool CThreadContext::post(CErasedOwnerMsg&& msg) noexcept
{
    const bool posted = m_resources.worker_to_host_owned_msgs.post(std::move(msg));
    if (posted) (void)m_resources.owner_wake.signal();
    return posted;
}

}   //  namespace threading

#endif  //  #ifndef CTHREADCONTEXT_HPP_INCLUDED
