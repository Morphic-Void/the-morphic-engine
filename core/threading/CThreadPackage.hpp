
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
#include "system/system_ids.hpp"
#include "threading/CParkingGate.hpp"
#include "threading/CThreadControlState.hpp"
#include "threading/CWaitPredicate.hpp"
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
    CThreadResources(
        const ThreadConfig& thread_config,
        const platform::system::CPerfCountConversion& perf_count_conversion) noexcept;
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
    std::uint32_t wait_for_new_epoch(const std::uint32_t epoch) noexcept;

    bool read(CErasedPodMsg& msg) noexcept;
    bool post(const CErasedPodMsg& msg) noexcept;
    bool post(CErasedOwnerMsg&& msg) noexcept;

private:
    CThreadResources& m_resources;
};

class CThreadPackage
{
public:
    CThreadPackage(
        const ThreadConfig& thread_config,
        const platform::system::CPerfCountConversion& perf_count_conversion) noexcept;
    ~CThreadPackage() noexcept = default;

    bool startup() noexcept;
    bool shutdown() noexcept;
    void request_exit() noexcept;
    bool read(CErasedPodMsg& msg) noexcept;
    bool post(const CErasedPodMsg& msg) noexcept;
    bool read(CErasedOwnerMsg& msg) noexcept;

    [[nodiscard]] EThreadRunState query_state() const noexcept;

private:
    static std::uint32_t MV_STD_ABI_CALL thread_entry_point(void* const user_data) noexcept;

    CThreadResources m_resources;
};

//==============================================================================
//  CThreadResources inline out of class function bodies
//==============================================================================

inline CThreadResources::CThreadResources(
    const ThreadConfig& thread_config,
    const platform::system::CPerfCountConversion& perf_count_conversion) noexcept
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

inline const platform::system::CPerfCountConversion&
CThreadContext::perf_count_conversion() const noexcept
{
    return m_resources.perf_count_conversion;
}

inline bool CThreadContext::read(CErasedPodMsg& msg) noexcept
{
    return m_resources.host_to_worker_msgs.read(msg);
}

inline bool CThreadContext::post(const CErasedPodMsg& msg) noexcept
{
    return m_resources.worker_to_host_msgs.post(msg);
}

inline bool CThreadContext::post(CErasedOwnerMsg&& msg) noexcept
{
    return m_resources.worker_to_host_owned_msgs.post(std::move(msg));
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
    return m_resources.worker_to_host_msgs.read(msg);
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
    return m_resources.worker_to_host_owned_msgs.read(msg);
}

inline EThreadRunState CThreadPackage::query_state() const noexcept
{
    return m_resources.control_state.query_state();
}

inline void CThreadPackage::request_exit() noexcept
{
    m_resources.control_state.request_exit();
    if (m_resources.wait_predicate.has_control())
    {   //  Failed startup has already released the wait predicate and joined the thread.
        (void)m_resources.wait_predicate.poke_epoch_and_wake_one();
    }
}

}   //  namespace threading

#endif  //  #ifndef CTHREADPACKAGE_HPP_INCLUDED
