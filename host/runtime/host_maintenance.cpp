
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_maintenance.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//


#include "framework/system/system_ids.hpp"
#include "runtime/host.hpp"
#include "runtime/host_payloads.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/assets/asset_messages.hpp"

namespace host
{

bool CHostMaintenance::references(const threading::CThreadPackage& package) const noexcept
{
    for (const auto& entry : m_pods)
    {
        if (entry.source.sequence && (entry.source.package == &package))
        {
            return true;
        }
    }
    for (const auto& entry : m_owners)
    {
        if (entry.source.sequence && (entry.source.package == &package))
        {
            return true;
        }
    }
    return false;
}

bool CHostMaintenance::captured(const SClientRecord* const client, const threading::CThreadPackage* const package) const noexcept
{
    for (const auto& entry : m_pods)
    {
        if (entry.source.sequence && ((client && (entry.source.client == client)) || (package && (entry.source.package == package))))
        {
            return true;
        }
    }
    for (const auto& entry : m_owners)
    {
        if (entry.source.sequence && ((client && (entry.source.client == client)) || (package && (entry.source.package == package))))
        {
            return true;
        }
    }
    return false;
}

bool CHostMaintenance::captured_pod(const SClientRecord& client) const noexcept
{
    for (const SPod& entry : m_pods)
    {
        if (entry.source.sequence && (entry.source.client == &client))
        {
            return true;
        }
    }
    return false;
}

bool CHostMaintenance::captured_owner(const SClientRecord& client) const noexcept
{
    for (const SOwner& entry : m_owners)
    {
        if (entry.source.sequence && (entry.source.client == &client))
        {
            return true;
        }
    }
    return false;
}

bool CHostMaintenance::defer_owner(SClientRecord& client, threading::CErasedOwnerMsg& message) noexcept
{
    if (!m_next_sequence) return false;
    for (std::uint32_t index = 0u; index < 32u; ++index)
    {
        SOwner& entry = m_owners[index];
        if (entry.source.sequence) continue;
        entry.message = std::move(message);
        entry.source = { &client, client.thread, 0u, 2u };
        accepted(entry.source);
        return true;
    }
    return false;
}

void CHostMaintenance::poll_stop() noexcept
{
    bool executive_stop = false;
    for (SClientRecord* client = m_host.first_client(); client; client = m_host.next_client(*client))
    {
        if ((client->module == module_ids::executive) && client->host_stop_requested.load(std::memory_order_acquire))
        {
            executive_stop = true;
            break;
        }
    }
    if (executive_stop ||
        (m_host.m_debug_service && (m_host.m_debug_service->read_shutdown_request() != debug_system::EShutdownReason::none)) ||
        m_restrictions.failed() || m_host.m_emergency_shutdown || m_host.m_native_stop_requested ||
        (m_host.m_phase == CHost::EPhase::shutting_down))
    {
        m_stop_requested = true;
    }
    if (m_host.m_phase == CHost::EPhase::stopping_executive)
    {
        const auto* const transition = m_host.m_executive_request.payload<modules::ModuleRequest>();
        if (!transition || (transition->action != modules::EModuleAction::replace))
        {
            m_stop_requested = true;
        }
    }
    for (std::uint32_t index = 0u; index < m_host.m_worker_count; ++index)
    {
        if (const auto* const worker = m_host.worker_package(index))
        {
            if (worker->query_state() == threading::EThreadRunState::Failed)
            {
                m_stop_requested = m_host.m_runtime_failed = true;
            }
        }
    }
    if (!m_stop_requested) return;
    m_host.m_work_admission_closed = true;
    m_host.m_direct_connections.close_admission();
    m_host.m_service_router.close_admission();
    if (!m_host.m_shutdown_timed)
    {
        m_host.m_shutdown_timed = true;
        if (m_host.m_shutdown_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
        {
            m_host.m_runtime_failed = true;
        }
    }
    std::uint64_t remaining = 0u;
    if ((m_host.m_shutdown_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
    {
        m_host.m_runtime_failed = true;
    }
}

void CHost::apply_host_stop() noexcept
{
    if ((!m_maintenance.stop_requested() && !m_native_stop_requested) || m_host_stop_applied)
    {
        return;
    }
    m_host_stop_applied = true;
    m_direct_connections.close_all();
    m_executive_request.destroy();
    if (SModuleEndpoints* const executive = module_endpoints(module_ids::executive))
    {
        stop_module(*executive, modules::EStopReason::shutdown);
        if (m_phase != EPhase::shutting_down)
        {
            m_phase = EPhase::stopping_executive;
        }
    }
    else if (m_module_service.is_idle())
    {
        m_phase = EPhase::shutting_down;
    }
}

void CHostMaintenance::accepted(SSource& source) noexcept
{
    source.sequence = m_next_sequence;
    m_next_sequence = (m_next_sequence == UINT64_MAX) ? 0u : m_next_sequence + 1u;
    ++m_count;
    if (source.client)
    {
        ++source.client->deferred_records;
        source.client->incoming_drained = false;
    }
}

void CHostMaintenance::released(SSource& source) noexcept
{
    if (source.client) --source.client->deferred_records;
    source = {};
    --m_count;
}

bool CHostMaintenance::next_source(const bool worker, const std::uint32_t lane, SSource& source, std::uint32_t& index) noexcept
{
    if (!m_next_sequence)
    {
        m_restrictions.fail({ platform::threading::EWaitError::overflow, 0u });
        return false;
    }
    if (worker)
    {
        if (!m_host.m_worker_count) return false;
        index = m_cursors[lane]++ % m_host.m_worker_count;
        source.package = m_host.worker_package(index);
        return source.package != nullptr;
    }
    index = m_cursors[lane]++;
    source.client = m_host.first_client();
    for (std::uint32_t skip = 0u; source.client && (skip < index); ++skip)
    {
        source.client = m_host.next_client(*source.client);
    }
    if (!source.client)
    {
        m_cursors[lane] = 0u;
        return false;
    }
    source.package = source.client->thread;
    return true;
}

bool CHostMaintenance::capture_pod(const bool worker) noexcept
{
    const std::uint32_t begin = worker ? 128u : 0u;
    SPod* vacant = nullptr;
    for (std::uint32_t i = begin; i < (begin + 128u); ++i)
    {
        if (!m_pods[i].source.sequence)
        {
            vacant = &m_pods[i];
            break;
        }
    }
    if (!vacant)
    {
        m_pressure = true;
        return false;
    }
    SSource source;
    std::uint32_t index = 0u;
    if (!next_source(worker, (worker ? 1u : 0u), source, index))
    {
        return false;
    }
    source.credits = source.client ? 2u : 0u;
    if (source.client && !source.client->reserve_replies(source.credits))
    {
        return false;
    }
    const bool copied = source.client ? source.client->read(vacant->message) : source.package->read(vacant->message);
    if (!copied)
    {
        if (source.client) source.client->cancel_reservation(source.credits);
        return false;
    }
    vacant->source = source;
    accepted(vacant->source);
    return true;
}

bool CHostMaintenance::capture_owner(const bool worker) noexcept
{
    const auto begin = worker ? 32u : 0u;
    SOwner* vacant = nullptr;
    for (auto index = begin; index < (begin + 32u); ++index)
    {
        if (!m_owners[index].source.sequence)
        {
            vacant = &m_owners[index];
            break;
        }
    }
    if (!vacant)
    {
        m_pressure = true;
        return false;
    }
    SSource source;
    std::uint32_t index = 0u;
    if (!next_source(worker, (worker ? 3u : 2u), source, index))
    {
        return false;
    }
    source.credits = source.client ? 2u : 0u;
    if (source.credits && !source.client->reserve_replies(source.credits))
    {
        m_pressure = true;
        return false;
    }
    const bool captured = source.client ? source.client->capture(vacant->message) : source.package->capture(vacant->message);
    if (!captured)
    {
        if (source.client)
        {
            source.client->cancel_reservation(source.credits);
        }
        return false;
    }
    vacant->source = source;
    accepted(vacant->source);
    return true;
}

bool CHostMaintenance::signal_one() noexcept
{
    SSource source;
    std::uint32_t index = 0u;
    if (!next_source(false, 4u, source, index) || !source.client->subscription_live)
    {
        return false;
    }
    if (!source.client->signal_restriction(m_restrictions.revision()))
    {
        m_restrictions.fail({ platform::threading::EWaitError::native_failure, 0u });
        return false;
    }
    return true;
}

void CHostMaintenance::fanout() noexcept
{
    for (std::uint32_t count = 0u; count < module_ids::k_count; ++count)
    {
        (void)signal_one();
    }
}

modules::SHostMaintenanceResult CHostMaintenance::run() noexcept
{
    modules::SHostMaintenanceResult result;
    result.shutdown_requested = m_stop_requested ? 1u : 0u;
    result.cancel_native = (m_stop_requested || m_restrictions.failed()) ? 1u : 0u;
    if (m_running)
    {
        result.status = modules::EMaintenanceStatus::reentrant;
        return result;
    }
    if (!m_restrictions.active()) return result;
    m_running = true;
    m_pressure = false;
    poll_stop();
    std::uint64_t start = 0u;
    auto error = m_restrictions.now(start);
    std::uint64_t previous = start;
    while (!m_restrictions.failed() && (error.code == platform::threading::EWaitError::none) && (result.operations < 32u))
    {
        std::uint64_t now = 0u;
        error = m_restrictions.now(now);
        if (error.code != platform::threading::EWaitError::none)
        {
            break;
        }
        if (now < previous)
        {
            error = { platform::threading::EWaitError::invalid_argument, 0u };
            break;
        }
        previous = now;
        if ((now - start) >= 1000000u) break;
        switch (m_lane++ % 5u)
        {
        case 0u:
        {
            (void)capture_pod(false);
            break;
        }
        case 1u:
        {
            (void)capture_pod(true);
            break;
        }
        case 2u:
        {
            (void)capture_owner(false);
            break;
        }
        case 3u:
        {
            (void)capture_owner(true);
            break;
        }
        case 4u:
        {
            (void)signal_one();
            break;
        }
        }
        ++result.operations; //  Empty/full probes consume the same finite allowance.
    }
    if (error.code != platform::threading::EWaitError::none)
    {
        m_restrictions.fail(error);
    }
    poll_stop();
    result.status = m_restrictions.failed() ? modules::EMaintenanceStatus::failed : modules::EMaintenanceStatus::success;
    result.continuation = 1u;
    result.capacity_pressure = m_pressure ? 1u : 0u;
    result.shutdown_requested = m_stop_requested ? 1u : 0u;
    result.cancel_native = (m_stop_requested || m_restrictions.failed()) ? 1u : 0u;
    m_running = false;
    return result;
}

void CHostMaintenance::replay() noexcept
{
    if (m_running || m_restrictions.active())
    {
        return;
    }
    m_host.apply_host_stop();
    std::uint64_t attempted = 0u;
    while (m_count)
    {
        SPod* pod = nullptr;
        SOwner* owner = nullptr;
        std::uint64_t sequence = UINT64_MAX;
        for (auto& entry : m_pods)
        {
            if ((entry.source.sequence > attempted) && (entry.source.sequence <= sequence))
            {
                sequence = entry.source.sequence;
                pod = &entry;
                owner = nullptr;
            }
        }
        for (auto& entry : m_owners)
        {
            if ((entry.source.sequence > attempted) && (entry.source.sequence <= sequence))
            {
                sequence = entry.source.sequence;
                owner = &entry;
                pod = nullptr;
            }
        }
        if (!pod && !owner)
        {
            break;
        }
        attempted = sequence;
        if (pod)
        {
            auto& source = pod->source;
            if (source.client)
            {
                const std::uint32_t surplus = source.credits - client_reply_count(pod->message);
                source.client->cancel_reservation(surplus);
                m_host.receive_message(pod->message, *source.client);
                source.client->accounted_pod();
            }
            else if ((source.package == m_host.worker_package(0u)) && pod->message.is_payload_a<ModuleWorkResult>())
            {
                m_host.m_module_service.complete(pod->message);
            }
            else
            {
                m_host.m_asset_service.complete(pod->message, *source.package);
            }
            pod->message = {};
            released(source);
        }
        else if (owner)
        {
            auto& source = owner->source;
            bool preceding = false;
            for (const SOwner& entry : m_owners)
            {
                if (entry.source.sequence && (entry.source.sequence < sequence) &&
                    (entry.source.client == source.client) && (entry.source.package == source.package))
                {
                    preceding = true;
                }
            }
            if (preceding) continue;
            if (source.client)
            {
                if (m_host.defer_startup_module_request(owner->message, *source.client))
                {
                    continue;
                }
                if (owner->message.is_message_a<SchemaProcessRequest>() && !m_host.m_asset_service.schema_ingress_available())
                {
                    continue;
                }
                if (!owner->message.is_message_a<modules::ModuleRequest>() && !owner->message.is_message_a<SchemaProcessRequest>())
                {
                    const std::uint32_t required = owner->message.is_message_a<platform::reports::PlatformReportCaptured>() ? 0u : 1u;
                    source.client->cancel_reservation(source.credits - required);
                }
                m_host.receive_request(owner->message, *source.client);
                source.client->accounted_owner();
            }
            else if ((source.package == m_host.worker_package(0u)) && owner->message.is_message_a<FilesystemScanResult>())
            {
                m_host.complete_scan(owner->message);
            }
            else
            {
                m_host.m_asset_service.complete(owner->message, *source.package);
            }
            owner->message = {}; //  Destruction is allowed only after the native invocation returns.
            released(source);
        }

    }
}

}   //  namespace host
