
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CThreadPackage.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Host-owned launch, native thread lifetime and shared-resource provisioning.

#include <thread>       //  std::this_thread::yield

#include "host/threading/CThreadPackage.hpp"
#include "framework/threading/CThreadContext.hpp"

#include "debug/macros.hpp"
#include "platform/threading/thread_naming.hpp"
#include "system/system_context.hpp"
#include "system/system_id_registry.hpp"

namespace threading
{

std::uint32_t MV_STD_ABI_CALL CThreadPackage::thread_entry_point(void* const user_data) noexcept
{
    CThreadPackage* const package = static_cast<CThreadPackage*>(user_data);
    if (package == nullptr)
    {
        return ~0u;
    }

    const ThreadConfig& config = package->m_config;
    CThreadResources* const resources = &package->m_resources;
    (void)system_context::set_ambient_thread_id(config.thread_id);

    const char* const thread_name = system_id_registry::lookup_thread_name(config.thread_id);
    MV_ASSERT(thread_name != nullptr);
    if (thread_name != nullptr)
    {
        (void)platform::threading::set_current_thread_name(thread_name);
    }
    (void)platform::threading::set_current_thread_priority(config.priority);

    CThreadContext context{ *resources };
    if ((config.prepare != nullptr) && !config.prepare(config.prepare_context, config.thread_id, &context))
    {
        package->m_preparation.store(2u, std::memory_order_release);
        resources->control_state.mark_failed(~0u);
        package->m_returned.store(true, std::memory_order_release);
        (void)resources->owner_wake.signal();
        return ~0u;
    }

    package->m_preparation.store(1u, std::memory_order_release);
    resources->control_state.mark_running();
    (void)resources->owner_wake.signal();
    const std::uint32_t result = config.endpoint_entry ? config.endpoint_entry(context, config.module_context) : config.entry_point(resources);
    if ((result != 0u) || (resources->control_state.query_state() == EThreadRunState::Failed))
    {
        if (resources->control_state.query_state() != EThreadRunState::Failed) resources->control_state.mark_failed(result);
    }
    else resources->control_state.mark_exited();
    package->m_returned.store(true, std::memory_order_release);
    (void)resources->owner_wake.signal();
    return result;
}

bool CThreadPackage::install_owner_wake(const platform::threading::SWakeEndpoint wake) noexcept
{
    if (m_created) return false;
    m_resources.owner_wake = wake;
    m_resources.control_state.install_owner_wake(wake);
    return true;
}

bool CThreadPackage::startup() noexcept
{
    if ((m_config.entry_point == nullptr) && (m_config.endpoint_entry == nullptr))
    {
        return false;
    }

    if (m_resources.host_to_worker_msgs.initialise_growable(0u))
    {
        if (m_resources.worker_to_host_msgs.initialise_growable(0u))
        {
            if (m_resources.worker_to_host_owned_msgs.initialise(0u))
            {
                if (m_resources.wait_predicate.acquire_control())
                {
                    m_resources.wait_target = &m_resources.wait_predicate;
                    bool prepared = !m_host_notification || m_resources.pin_notification(*m_host_notification);
                    if (prepared && m_host_notification)
                    {
                        m_host_pinned = true;
                        m_host_execution->notification = m_host_notification->reader();
                        m_resources.host_execution = m_host_execution;
                        prepared = m_host_notification->endpoint().signal();
                    }
                    if (prepared)
                    {
                        m_resources.control_state.mark_pending();
#if defined(MV_HOST_RUNTIME_TESTS)
                        if (!m_test_reject_create)
#endif
                            m_created = m_thread.create(&CThreadPackage::thread_entry_point, this);
                    }
                    if (m_created)
                    {
                        return true;
                    }
                    m_resources.control_state.mark_empty();
                    retire_host_execution();
                    release_wait_control();
                }
                m_resources.worker_to_host_owned_msgs.deallocate();
            }
            m_resources.worker_to_host_msgs.deallocate();
        }
        m_resources.host_to_worker_msgs.deallocate();
    }
    return false;
}

bool CThreadPackage::install_batch_client(SBatchShared& shared, const module_ids::id_type module_id, void* const binding) noexcept
{
    if (m_created || m_batch_client || (binding == nullptr) || !module_ids::ops::is_valid_id(module_id))
    {
        return false;
    }
    if (!m_batch_client.emplace(shared, m_resources.control_state, m_resources.wait_predicate, module_id, binding))
    {
        return false;
    }
    m_resources.batch_client = &*m_batch_client;
    return true;
}

bool CThreadPackage::install_host_execution(modules::SHostExecutionContext& view, CNotification& notification) noexcept
{
    if (m_created || m_host_execution || !view.restriction.is_valid() || !view.subscription.value)
    {
        return false;
    }
    m_host_execution = &view;
    m_host_notification = &notification;
    return true;
}

bool CThreadPackage::join_execution() noexcept
{
    if (!m_thread.is_valid()) return true;
#if defined(MV_HOST_RUNTIME_TESTS)
    if (m_test_fail_join) return false;
#endif
    return m_thread.join_and_close();
}

bool CThreadPackage::retire_host_execution() noexcept
{
    if (!m_host_pinned) return true;
    MV_ASSERT(!m_created || m_resources.control_state.is_done());
    //  A terminal publication precedes the native return. Keep the context and
    //  predicate pin until join proves even that final execution has finished.
    if (m_created && (!execution_ended() || !join_execution()))
    {
        return false;
    }
    unpin_notification();
    m_host_pinned = false;
    m_resources.host_execution = nullptr;
    return true;
}

bool CThreadPackage::join_terminated_execution() noexcept
{
    if (!m_thread.is_valid()) return true;
    return execution_ended() && join_execution();
}

bool CThreadPackage::shutdown() noexcept
{
    //  The owner must continue provider retirement before attempting teardown.
    if (m_resources.notification_pins != 0u) return false;
    if (m_created)
    {
        m_resources.control_state.request_exit();
        if (m_resources.wait_predicate.has_control()) (void)m_resources.wait_predicate.poke_epoch_and_wake_one();
        if (!execution_ended()) return false;
        if (!join_execution()) return false;
        release_wait_control();
        if (m_created)
        {
            m_resources.worker_to_host_owned_msgs.deallocate();
            m_resources.worker_to_host_msgs.deallocate();
            m_resources.host_to_worker_msgs.deallocate();
            m_created = false;
        }
    }
    return !m_created;
}

void CThreadPackage::release_wait_control() noexcept
{
    MV_ASSERT(m_resources.notification_pins == 0u);
    //  A joined requester can have consumed its final response before the
    //  runner finishes waking it. Keep the predicate controlled until then.
    while ((m_resources.batch_client != nullptr) &&
        (m_resources.batch_client->return_publishers() != 0u))
    {
        std::this_thread::yield();
    }
    m_resources.wait_predicate.release_control();
}

CThreadPackage::~CThreadPackage() noexcept
{
    MV_ASSERT(m_resources.notification_pins == 0u);
}

bool CThreadPackage::pin_notification(CNotification& notification) noexcept
{
    return m_created && m_resources.pin_notification(notification);
}

void CThreadPackage::unpin_notification() noexcept
{
    m_resources.unpin_notification();
}

}   //  namespace threading
