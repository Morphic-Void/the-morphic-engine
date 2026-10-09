
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    batch_pool.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 Oct 26
//
//  Host-owned runners for module-defined batch work.

#include "host/runtime/batch_pool.hpp"

#include "debug/macros.hpp"
#include "module/bound_module.hpp"
#include "platform/threading/thread_naming.hpp"
#include "platform/threading/thread_priority.hpp"
#include "platform/threading/processor_relax.hpp"
#include "system/system_context.hpp"
#include "system/system_id_registry.hpp"

namespace host
{

CBatchPool::~CBatchPool() noexcept
{
    (void)stop();
}

bool CBatchPool::start(const std::uint32_t count) noexcept
{
    if ((m_count != 0u) || (count > k_max_batch_runner_count))
    {
        return false;
    }
    if (count == 0u)
    {
        return true;
    }
#if !MV_PLATFORM_HAS_NATIVE_WAIT_WORD
    return false;
#else
    if (!m_shared.wake.acquire_control())
    {
        return false;
    }
    m_shared.runner_count = count;
    m_count = count;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        SRunner& runner = m_runners[index];
        runner.pool = this;
        runner.index = index;
        runner.control.mark_pending();
        runner.created = runner.thread.create(&CBatchPool::runner_entry, &runner);
        if (!runner.created)
        {
            (void)stop();
            return false;
        }
        while (runner.control.is_starting())
        {
            platform::threading::processor_relax();
        }
        if (!runner.control.is_ready())
        {
            (void)stop();
            return false;
        }
    }
    return true;
#endif
}

bool CBatchPool::stop() noexcept
{
    if (m_count == 0u)
    {
        return true;
    }
    for (std::uint32_t index = 0u; index < m_count; ++index)
    {
        m_runners[index].control.request_exit();
    }
    m_shared.wake.release_control();
    bool joined = true;
    for (std::uint32_t index = 0u; index < m_count; ++index)
    {
        SRunner& runner = m_runners[index];
        if (runner.created)
        {
            joined = runner.thread.join_and_close() && joined;
            runner.created = false;
        }
    }
    m_shared.runner_count = 0u;
    m_count = 0u;
    return joined && (m_shared.work.outstanding_count() == 0u);
}

bool CBatchPool::failed() const noexcept
{
    for (std::uint32_t index = 0u; index < m_count; ++index)
    {
        if (m_runners[index].control.query_state() == threading::EThreadRunState::Failed)
        {
            return true;
        }
    }
    return false;
}

std::uint32_t MV_STD_ABI_CALL CBatchPool::runner_entry(void* const user_data) noexcept
{
    SRunner* const runner = static_cast<SRunner*>(user_data);
    if ((runner == nullptr) || (runner->pool == nullptr))
    {
        return ~0u;
    }
    return runner->pool->run(*runner) ? 0u : ~0u;
}

bool CBatchPool::run(SRunner& runner) noexcept
{
    const auto first = thread_ids::batch_runner_00_index.raw_value();
    const auto index = thread_ids::ops::make_index(first + runner.index);
    const thread_ids::id_type identity = thread_ids::ops::make_id(index);
    (void)system_context::set_ambient_thread_id(identity);
    const char* const name = system_id_registry::lookup_thread_name(identity);
    if (name != nullptr)
    {
        (void)platform::threading::set_current_thread_name(name);
    }
    (void)platform::threading::set_current_thread_priority(platform::threading::EThreadPriority::Normal);
    runner.control.mark_startup();
    runner.control.mark_running();

    bool success = true;
    while (!runner.control.exit_requested())
    {
        const std::uint32_t epoch = m_shared.wake.get_word();
        bool found = false;
        for (;;)
        {
            threading::transports::TAcquiredArenaSlot<threading::SBatchWork,
                threading::k_batch_channel_capacity> acquired(m_shared.work);
            if (!acquired)
            {
                break;
            }
            found = true;
            const threading::SBatchWork work = *acquired;
            (void)acquired.recycle();
            threading::EBatchCompletion result = threading::EBatchCompletion::discarded_requester_exiting;
            if (!work.requester->exit_requested())
            {
                modules::CBoundModule* const binding =
                    static_cast<modules::CBoundModule*>(work.requester->binding());
                if (binding->prepare_batch_thread(identity, work.memory_context))
                {
                    work.function(work.object);
                    result = threading::EBatchCompletion::executed;
                }
                else
                {
                    result = threading::EBatchCompletion::discarded_context_install_failed;
                }
            }
            if (!work.requester->complete({ work.correlation, result }))
            {
                MV_CRITICAL_EVENT("Batch runner could not return a terminal response");
                success = false;
                break;
            }
            runner.control.advance_heartbeat();
        }
        if (!success)
        {
            break;
        }
        if (!found && !runner.control.exit_requested())
        {
            runner.control.mark_waiting();
            (void)m_shared.wake.wait_until_not_equal(runner.ticket, epoch);
            runner.control.mark_running();
        }
    }
    if (success)
    {
        runner.control.mark_exiting();
        runner.control.mark_exited();
    }
    else
    {
        runner.control.mark_failed(~0u);
    }
    return success;
}

}   //  namespace host
