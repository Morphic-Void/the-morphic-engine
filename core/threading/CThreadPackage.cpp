
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

    if ((config.prepare != nullptr) &&
        !config.prepare(config.prepare_context, config.thread_id, resources))
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
                    m_resources.control_state.mark_pending();
                    m_resources.created = m_resources.thread.create(&CThreadPackage::thread_entry_point, &m_resources);
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
                        (void)m_resources.thread.join_and_close();
                        m_resources.created = false;
                    }
                    m_resources.control_state.mark_empty();
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

bool CThreadPackage::shutdown() noexcept
{
    if (m_resources.created && m_resources.wait_predicate.has_control())
    {
        m_resources.control_state.request_exit();
        (void)m_resources.wait_predicate.poke_epoch_and_wake_one();
        while (!m_resources.control_state.is_done())
        {
            std::this_thread::yield();
        }
        m_resources.created = m_resources.thread.join_and_close();
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
    //  A joined requester can have consumed its final response before the
    //  runner finishes waking it. Keep the predicate controlled until then.
    while ((m_resources.batch_client != nullptr) &&
        (m_resources.batch_client->return_publishers() != 0u))
    {
        std::this_thread::yield();
    }
    m_resources.wait_predicate.release_control();
}

}   //  namespace threading
