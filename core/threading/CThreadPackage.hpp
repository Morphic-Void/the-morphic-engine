
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CThreadPackage.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Common resources and owner/thread-facing interfaces for an engine thread.
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.

#pragma once

#ifndef CTHREADPACKAGE_HPP_INCLUDED
#define CTHREADPACKAGE_HPP_INCLUDED

#include <cstdint>      //  std::uint32_t
#include <utility>      //  std::move

#include "platform/threading/thread_lifetime.hpp"
#include "platform/threading/thread_priority.hpp"
#include "platform/system/performance_counter.hpp"
#include "containers/TInstance.hpp"
#include "system/system_ids.hpp"
#include "threading/CBatchWork.hpp"
#include "threading/CParkingGate.hpp"
#include "threading/CThreadControlState.hpp"
#include "threading/CWaitPredicate.hpp"
#include "threading/CNotification.hpp"
#include "module/host_execution_context.hpp"
#include "threading/messages/CErasedMessageTransports.hpp"

namespace threading
{

using FThreadPrepare = bool(MV_STD_ABI_CALL*)(void* const context, const thread_ids::id_type thread_id, void* const thread_resources) noexcept;

struct ThreadConfig
{
    thread_ids::id_type thread_id{};
    module_ids::id_type worker_module_id{};
    platform::threading::EThreadPriority priority{ platform::threading::EThreadPriority::Normal };
    platform::threading::FThreadEntry entry_point{ nullptr };
    FThreadPrepare prepare{ nullptr };
    void* prepare_context{ nullptr };
};

class CThreadResources
{
public:
    CThreadResources(const ThreadConfig& thread_config, const platform::system::CPerfCountConversion& perf_count_conversion) noexcept;
    ~CThreadResources() noexcept = default;

    //  Immutable configuration shared by the package and thread context.
    ThreadConfig config;
    const platform::system::CPerfCountConversion perf_count_conversion;

    //  Common resources available to the thread package and context.
    bool created{ false };
    platform::threading::CThread thread;
    CParkingTicket parking_ticket;
    CWaitPredicate wait_predicate;
    transports::CErasedPodMsgTransport host_to_worker_msgs;
    transports::CErasedPodMsgTransport worker_to_host_msgs;
    transports::CErasedOwnerMsgTransport worker_to_host_owned_msgs;
    CThreadControlState control_state;
    CBatchClient* batch_client{ nullptr };
    const modules::SHostExecutionContext* host_execution{ nullptr };
    platform::threading::SWakeEndpoint owner_wake;
};

class CThreadContext
{
public:
    explicit CThreadContext(CThreadResources& resources) noexcept : m_resources{ resources } {}
    ~CThreadContext() noexcept = default;

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

    bool read(CErasedPodMsg& msg) noexcept;
    bool post(const CErasedPodMsg& msg) noexcept;
    bool post(CErasedOwnerMsg&& msg) noexcept;
    [[nodiscard]] bool pod_submission_poisoned() const noexcept { return m_resources.worker_to_host_msgs.posting_poisoned(); }

private:
    CThreadResources& m_resources;
};

class CThreadPackage
{
public:
    CThreadPackage(const ThreadConfig& thread_config, const platform::system::CPerfCountConversion& perf_count_conversion) noexcept;
    ~CThreadPackage() noexcept;

    bool startup() noexcept;
    [[nodiscard]] bool install_owner_wake(const platform::threading::SWakeEndpoint wake) noexcept;
    bool shutdown() noexcept;
    [[nodiscard]] bool retains_execution() const noexcept { return m_resources.created && m_resources.wait_predicate.has_control(); }
    [[nodiscard]] bool install_host_execution(modules::SHostExecutionContext& view, CNotification& notification) noexcept;
    //  Host proves final drain, including deferred work, before retiring its pin.
    bool retire_host_execution() noexcept;
    //  Owner-thread proof only; retains transports, wait control and all pins.
    [[nodiscard]] bool join_terminated_execution() noexcept;
    //  Owner-thread only. Release after all readers AND signal calls are quiescent.
    [[nodiscard]] bool pin_notification(CNotification& notification) noexcept;
    void unpin_notification() noexcept;
    bool install_batch_client(SBatchShared& shared, const module_ids::id_type module_id, void* const binding) noexcept;
    void request_exit() noexcept;
    bool read(CErasedPodMsg& msg) noexcept;
    bool post(const CErasedPodMsg& msg) noexcept;
    bool read(CErasedOwnerMsg& msg) noexcept;
    [[nodiscard]] bool pod_delivery_poisoned() const noexcept { return m_resources.host_to_worker_msgs.posting_poisoned(); }
    [[nodiscard]] bool capture(CErasedOwnerMsg& msg) noexcept
    {
        const bool captured = m_resources.worker_to_host_owned_msgs.capture(msg);
        if (captured) (void)m_resources.wait_predicate.poke_epoch_and_wake_one();
        return captured;
    }
    [[nodiscard]] bool post_without_growth(const CErasedPodMsg& msg) noexcept
    {
        return !m_resources.host_to_worker_msgs.post_would_reallocate(1u) && post(msg);
    }

    [[nodiscard]] EThreadRunState query_state() const noexcept;
    [[nodiscard]] module_ids::id_type module_id() const noexcept { return m_resources.config.worker_module_id; }
    [[nodiscard]] CBatchClient* batch_client() noexcept { return m_resources.batch_client; }

private:
    static std::uint32_t MV_STD_ABI_CALL thread_entry_point(void* const user_data) noexcept;
    [[nodiscard]] bool join_execution() noexcept;
    void release_wait_control() noexcept;

    CThreadResources m_resources;
    TInstance<CBatchClient> m_batch_client;
    std::uint32_t m_notification_pins{ 0u };
    modules::SHostExecutionContext* m_host_execution{ nullptr };
    CNotification* m_host_notification{ nullptr };
    bool m_host_pinned{ false };
#if defined(MV_HOST_RUNTIME_TESTS)
    friend struct SThreadPackageTestAccess;
    friend struct SDirectConnectionTestAccess;
    bool m_test_fail_join{ false };
    bool m_test_reject_create{ false };
#endif
};

//==============================================================================
//  CThreadResources inline out of class function bodies
//==============================================================================

inline CThreadResources::CThreadResources(const ThreadConfig& thread_config, const platform::system::CPerfCountConversion& perf_count_conversion) noexcept
    : config{ thread_config }
    , perf_count_conversion{ perf_count_conversion }
    , host_to_worker_msgs{ thread_config.worker_module_id }
    , worker_to_host_msgs{ module_ids::executable }
    , worker_to_host_owned_msgs{ module_ids::executable, nullptr }
{
}

//==============================================================================
//  CThreadContext inline out of class function bodies
//==============================================================================

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

//==============================================================================
//  CThreadPackage inline out of class function bodies
//==============================================================================

inline CThreadPackage::CThreadPackage(
    const ThreadConfig& thread_config,
    const platform::system::CPerfCountConversion& perf_count_conversion) noexcept
    : m_resources{ thread_config, perf_count_conversion }
{
}

inline bool CThreadPackage::read(CErasedPodMsg& msg) noexcept
{
    const bool consumed = m_resources.worker_to_host_msgs.read(msg);
    if (consumed) (void)m_resources.wait_predicate.poke_epoch_and_wake_one();
    return consumed;
}

inline bool CThreadPackage::post(const CErasedPodMsg& msg) noexcept
{
    const bool success = m_resources.host_to_worker_msgs.post(msg);
    if (success)
    {
        m_resources.wait_predicate.poke_epoch_and_wake_one();
    }
    return success;
}

inline bool CThreadPackage::read(CErasedOwnerMsg& msg) noexcept
{
    const bool consumed = m_resources.worker_to_host_owned_msgs.read(msg);
    if (consumed) (void)m_resources.wait_predicate.poke_epoch_and_wake_one();
    return consumed;
}

inline EThreadRunState CThreadPackage::query_state() const noexcept
{
    return m_resources.control_state.query_state();
}

inline void CThreadPackage::request_exit() noexcept
{
    m_resources.control_state.request_exit();
    if (m_resources.wait_predicate.has_control())
    {
        //  Failed startup has already released the wait predicate and joined the thread.
        (void)m_resources.wait_predicate.poke_epoch_and_wake_one();
    }
}

}   //  namespace threading

#endif  //  #ifndef CTHREADPACKAGE_HPP_INCLUDED
