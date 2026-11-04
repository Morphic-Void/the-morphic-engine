
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CThreadPackage.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Common resources and owner/thread-facing interfaces for an engine thread.

#include <thread>       //  std::this_thread::yield

#include "threading/CThreadPackage.hpp"

#include "debug/macros.hpp"
#include "platform/threading/thread_naming.hpp"
#include "system/system_context.hpp"
#include "system/system_id_registry.hpp"

namespace threading
{

std::uint32_t MV_STD_ABI_CALL CThreadPackage::thread_entry_point(void* const user_data) noexcept
{
    CThreadResources* const resources = static_cast<CThreadResources*>(user_data);
    if ((resources == nullptr) || (resources->config.entry_point == nullptr))
    {
        return ~0u;
    }

    const ThreadConfig& config = resources->config;
    (void)system_context::set_ambient_thread_id(config.thread_id);

    const char* const thread_name = system_id_registry::lookup_thread_name(config.thread_id);
    MV_ASSERT(thread_name != nullptr);
    if (thread_name != nullptr)
    {
        (void)platform::threading::set_current_thread_name(thread_name);
    }
    (void)platform::threading::set_current_thread_priority(config.priority);

    if ((config.prepare != nullptr) && !config.prepare(config.prepare_context, config.thread_id, resources))
    {
        resources->control_state.mark_failed(~0u);
        return ~0u;
    }

    return config.entry_point(resources);
}

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

bool CThreadPackage::startup() noexcept
{
    if (m_resources.config.entry_point == nullptr)
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
                    bool prepared = !m_host_notification || m_host_notification->initialise(m_resources.wait_predicate);
                    if (prepared && m_host_notification)
                    {
                        ++m_notification_pins;
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
                            m_resources.created = m_resources.thread.create(&CThreadPackage::thread_entry_point, &m_resources);
                    }
                    if (m_resources.created)
                    {
                        while (m_resources.control_state.is_starting())
                        {
                            std::this_thread::yield();
                        }
                        if (m_resources.control_state.is_ready())
                        {
                            return true;
                        }
                        if (!join_execution()) return false;
                        m_resources.created = false;
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
    if (m_resources.created || m_batch_client || (binding == nullptr) || !module_ids::ops::is_valid_id(module_id))
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
    if (m_resources.created || m_host_execution || !view.restriction.is_valid() || !view.subscription.value) return false;
    m_host_execution = &view;
    m_host_notification = &notification;
    return true;
}

bool CThreadPackage::join_execution() noexcept
{
    if (!m_resources.thread.is_valid()) return true;
#if defined(MV_HOST_RUNTIME_TESTS)
    if (m_test_fail_join) return false;
#endif
    return m_resources.thread.join_and_close();
}

bool CThreadPackage::retire_host_execution() noexcept
{
    if (!m_host_pinned) return true;
    MV_ASSERT(!m_resources.created || m_resources.control_state.is_done());
    //  A terminal publication precedes the native return. Keep the context and
    //  predicate pin until join proves even that final execution has finished.
    if (m_resources.created && (!m_resources.control_state.is_done() || !join_execution())) return false;
    unpin_notification();
    m_host_pinned = false;
    m_resources.host_execution = nullptr;
    return true;
}

bool CThreadPackage::shutdown() noexcept
{
    //  The owner must continue provider retirement before attempting teardown.
    if (m_notification_pins != 0u) return false;
    if (m_resources.created && m_resources.wait_predicate.has_control())
    {
        m_resources.control_state.request_exit();
        (void)m_resources.wait_predicate.poke_epoch_and_wake_one();
        while (!m_resources.control_state.is_done())
        {
            std::this_thread::yield();
        }
        if (!join_execution()) return false;
        release_wait_control();
        if (!m_resources.created)
        {
            m_resources.worker_to_host_owned_msgs.deallocate();
            m_resources.worker_to_host_msgs.deallocate();
            m_resources.host_to_worker_msgs.deallocate();
        }
    }
    return m_resources.created;
}

void CThreadPackage::release_wait_control() noexcept
{
    MV_ASSERT(m_notification_pins == 0u);
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
    MV_ASSERT(m_notification_pins == 0u);
}

bool CThreadPackage::pin_notification(CNotification& notification) noexcept
{
    if (!m_resources.created || (m_notification_pins == ~0u) ||
        !notification.initialise(m_resources.wait_predicate)) return false;
    ++m_notification_pins;
    return true;
}

void CThreadPackage::unpin_notification() noexcept
{
    MV_ASSERT(m_notification_pins != 0u);
    if (m_notification_pins != 0u) --m_notification_pins;
}

}   //  namespace threading
