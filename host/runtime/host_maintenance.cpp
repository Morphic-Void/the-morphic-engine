
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_maintenance.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//


#include "runtime/host.hpp"
#include "runtime/host_payloads.hpp"
#include "module/module_messages.hpp"
#include "assets/asset_messages.hpp"

namespace host
{

static std::uint32_t reply_count(const threading::CErasedPodMsg& message) noexcept
{
    if (connections::zero_reply_control(message) || message.is_payload_a<services::ReleaseServiceBorrow>() ||
        message.is_payload_a<services::ServiceBorrowQuiescent>() || message.is_payload_a<services::CancelServiceRequest>())
    {
        return 0u;
    }
    return message.is_payload_a<services::ServiceRequest>() ? 2u : 1u;
}

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
    for (const auto& entry : m_deliveries)
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

void CHostMaintenance::poll_stop() noexcept
{
    auto* const executive = m_host.module_client(module_ids::executive);
    if ((executive && executive->host_stop_requested.load(std::memory_order_acquire)) ||
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
    if (auto* const executive = module_thread_package(module_ids::executive))
    {
        executive->request_exit();
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

bool CHostMaintenance::stage_delivery(SClientRecord& client, const threading::CErasedPodMsg& message,
    const bool control, const bool reserved) noexcept
{
    if (!client.delivery_protected() || !m_next_sequence)
    {
        return false;
    }
    SDelivery* vacant = nullptr;
    for (auto& entry : m_deliveries)
    {
        if (!entry.source.sequence)
        {
            vacant = &entry;
            break;
        }
    }
    if (!vacant)
    {
        m_pressure = true;
        return false;
    }
    auto& staged = control ? client.staged_controls : client.staged_replies;
    const auto credits = control ? client.reserved_control_replies : client.reserved_replies;
    if ((!reserved || (credits <= staged)) && !(control ? client.reserve_control_replies(1u) : client.reserve_replies(1u)))
    {
        m_pressure = true;
        return false;
    }
    vacant->message = message;
    vacant->source.client = &client;
    vacant->source.package = client.thread;
    vacant->source.control = control;
    vacant->source.credits = 1u;
    ++staged;
    accepted(vacant->source);
    return true;
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
    index = m_cursors[lane]++ % module_ids::k_count;
    if (!m_host.m_clients[index]) return false;
    source.client = &*m_host.m_clients[index];
    source.package = source.client->thread;
    return source.package || m_host.m_pumps[index];
}

bool CHostMaintenance::capture_pod(const bool worker, const bool control) noexcept
{
    const auto lane = control ? 4u : (worker ? 1u : 0u);
    const auto begin = control ? 256u : (worker ? 128u : 0u);
    const auto end = control ? 288u : begin + 128u;
    SPod* vacant = nullptr;
    for (auto index = begin; index < end; ++index)
    {
        if (!m_pods[index].source.sequence)
        {
            vacant = &m_pods[index];
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
    if (!next_source(worker, lane, source, index) || (control && source.package))
    {
        return false;
    }
    const auto* offered = source.package ? nullptr : m_host.m_pumps[index]->peek_pod(control);
    if (!source.package && !offered)
    {
        return false;
    }
    source.control = control;
    source.credits = source.client ? (control ? reply_count(*offered) : 2u) : 0u;
    if (source.client && !(control ? source.client->reserve_control_replies(source.credits) :
        source.client->reserve_replies(source.credits)))
    {
        m_pressure = true;
        return false;
    }
    const bool copied = source.package ? source.package->read(vacant->message) : true;
    if (!copied)
    {
        if (source.client)
        {
            source.client->cancel_reservation(source.credits);
        }
        return false;
    }
    if (offered)
    {
        vacant->message = *offered;
        m_host.m_pumps[index]->consume_pod(control);
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
    if (source.client && !source.client->reserve_replies(source.credits))
    {
        m_pressure = true;
        return false;
    }
    const bool captured = source.package ? source.package->capture(vacant->message) : m_host.m_pumps[index]->capture(vacant->message);
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

bool CHostMaintenance::publish_reply() noexcept
{
    const auto index = m_cursors[5]++ % module_ids::k_count;
    if (!m_host.m_clients[index]) return false;
    auto& client = *m_host.m_clients[index];
    if (!client.thread || client.invocation_offered || client.terminal() || !client.message_count)
    {
        return false;
    }
    if (!client.thread->post_without_growth(client.messages[0]))
    {
        m_pressure = true;
        return false;
    }
    client.consume(1u);
    return true;
}

bool CHostMaintenance::signal_one() noexcept
{
    const auto index = m_cursors[6]++ % module_ids::k_count;
    if (!m_host.m_clients[index]) return false;
    auto& client = *m_host.m_clients[index];
    if (!client.subscription_live) return false;
    if (!client.signal_restriction(m_restrictions.revision()))
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
        switch (m_lane++ % 7u)
        {
        case 0u:
        {
            (void)capture_pod(false, false);
            break;
        }
        case 1u:
        {
            (void)capture_pod(true, false);
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
            (void)capture_pod(false, true);
            break;
        }
        case 5u:
        {
            (void)publish_reply();
            break;
        }
        case 6u:
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
        SDelivery* delivery = nullptr;
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
        for (auto& entry : m_deliveries)
        {
            if ((entry.source.sequence > attempted) && (entry.source.sequence <= sequence))
            {
                sequence = entry.source.sequence;
                delivery = &entry;
                owner = nullptr;
                pod = nullptr;
            }
        }
        if (!pod && !owner && !delivery)
        {
            break;
        }
        attempted = sequence;
        if (pod)
        {
            auto& source = pod->source;
            if (source.client)
            {
                const auto surplus = source.credits - reply_count(pod->message);
                if (source.control)
                {
                    source.client->cancel_control_reservation(surplus);
                }
                else
                {
                    source.client->cancel_reservation(surplus);
                }
                m_host.receive_message(pod->message, *source.client);
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
            if (source.client)
            {
                if (!owner->message.is_message_a<modules::ModuleRequest>())
                {
                    source.client->cancel_reservation(1u);
                }
                m_host.receive_request(owner->message, *source.client);
            }
            else if ((source.package == m_host.worker_package(0u)) && owner->message.is_message_a<FilesystemScanResult>())
            {
                m_host.complete_scan(owner->message);
            }
            else
            {
                m_host.m_asset_service.complete(owner->message, *source.package);
            }
            owner->message = {}; //  Destruction is allowed only after outer-prefix validation.
            released(source);
        }
        else if (delivery)
        {
            auto& source = delivery->source;
            auto& client = *source.client;
            if (client.delivery_protected()) continue;
            bool earlier = false;
            for (const auto& entry : m_deliveries)
            {
                if (entry.source.sequence && (entry.source.sequence < sequence) && (entry.source.client == &client) &&
                    (entry.source.control == source.control))
                {
                    earlier = true;
                }
            }
            if (earlier) continue;
            if (source.control)
            {
                client.cancel_control_reservation(1u);
                if (!client.terminal() && !client.post(delivery->message))
                {
                    ++client.reserved_control_replies;
                    continue;
                }
                --client.staged_controls;
            }
            else
            {
                if (!client.retry_reply(delivery->message))
                {
                    continue;
                }
                --client.staged_replies;
            }
            delivery->message = {};
            released(source);
        }
    }
}

}   //  namespace host
