
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CThreadPackage.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Host-owned launch, native thread lifetime and shared-resource provisioning.
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.

#pragma once

#ifndef CTHREADPACKAGE_HPP_INCLUDED
#define CTHREADPACKAGE_HPP_INCLUDED

#include <cstdint>

#include "framework/system/system_ids.hpp"
#include "framework/threading/CThreadResources.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "platform/threading/thread_priority.hpp"
#include "containers/TInstance.hpp"
#include "threading/CNotification.hpp"

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

class CThreadPackage
{
public:
    CThreadPackage(const ThreadConfig& thread_config, const platform::system::CPerfCountConversion& perf_count_conversion) noexcept;
    ~CThreadPackage() noexcept;

    bool startup() noexcept;
    [[nodiscard]] bool install_owner_wake(const platform::threading::SWakeEndpoint wake) noexcept;
    bool shutdown() noexcept;
    [[nodiscard]] bool retains_execution() const noexcept { return m_created && m_resources.wait_predicate.has_control(); }
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
    [[nodiscard]] module_ids::id_type module_id() const noexcept { return m_resources.worker_module_id; }
    [[nodiscard]] CBatchClient* batch_client() noexcept { return m_resources.batch_client; }

private:
    static std::uint32_t MV_STD_ABI_CALL thread_entry_point(void* const user_data) noexcept;
    [[nodiscard]] bool join_execution() noexcept;
    void release_wait_control() noexcept;

    const ThreadConfig m_config;
    platform::threading::CThread m_thread;
    bool m_created{ false };
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

inline CThreadPackage::CThreadPackage(
    const ThreadConfig& thread_config,
    const platform::system::CPerfCountConversion& perf_count_conversion) noexcept
    : m_config{ thread_config }
    , m_resources{ module_ids::executable, thread_config.worker_module_id,
        thread_config.thread_id, perf_count_conversion }
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
