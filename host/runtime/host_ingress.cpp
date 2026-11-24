
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_ingress.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Retained Host ingress heads, pressure selection and finite output retry turns.

#include "host/runtime/host.hpp"
#include "framework/assets/schema_messages.hpp"
#include "framework/platform/report_messages.hpp"

namespace host
{

static std::uint32_t owner_reply_count(const threading::CErasedOwnerMsg& message) noexcept
{
    if (message.is_message_a<platform::reports::PlatformReportCaptured>())
    {
        return 0u;
    }
    return (message.is_message_a<modules::ModuleRequest>() || message.is_message_a<SchemaProcessRequest>()) ? 2u : 1u;
}

bool CHost::module_pod_runnable(const SClientRecord& client) const noexcept
{
    if (!client.pod_head_live || client.pod_admission_failed || client.invocation_active)
    {
        return false;
    }
    const std::uint32_t replies = client_reply_count(client.pod_head);
    return !replies || client.terminal() ||
        (client.output_turn.admission_allowed() && client.pending_replies->available(replies));
}

bool CHost::module_owner_runnable(const SClientRecord& client) const noexcept
{
    if (!client.owner_head_live || client.owner_admission_failed || client.invocation_active ||
        defer_startup_module_request(client.owner_head, client) ||
        (client.owner_head.is_message_a<SchemaProcessRequest>() && !m_asset_service.schema_ingress_available()))
    {
        return false;
    }
    const std::uint32_t replies = owner_reply_count(client.owner_head);
    return !replies || client.terminal() ||
        (client.output_turn.admission_allowed() && client.pending_replies->available(replies));
}

void CHost::progress_module_output(SClientRecord& client) noexcept
{
    client.flush_replies();
    for (std::uint32_t count = 0u; count < COutputRetryTurn::k_allowance; ++count)
    {
        std::uint32_t ready = 0u;
        if (m_service_router.output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::service);
        if (m_service_router.borrow_output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::borrow);
        if (m_asset_service.output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::asset);
        if (m_asset_service.schema_output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::schema);
        if (m_asset_service.process_output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::process);
        if (m_asset_service.report_output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::report);
        if (m_asset_service.bundle_output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::bundle);
        if (m_module_service.output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::module);
        if (refresh_output_ready(client)) ready |= COutputRetryTurn::mask(EOutputProducer::filesystem);
        const EOutputProducer selected = client.output_turn.next(ready);
        if ((selected == EOutputProducer::count) || !client.output_ready())
        {
            break;
        }
        switch (selected)
        {
            case EOutputProducer::service:
            {
                m_service_router.retry_output(client);
                break;
            }
            case EOutputProducer::asset:
            {
                m_asset_service.retry_output(client);
                break;
            }
            case EOutputProducer::borrow:
            {
                m_service_router.retry_borrow_output(client);
                break;
            }
            case EOutputProducer::module:
            {
                m_module_service.retry_output();
                break;
            }
            case EOutputProducer::schema:
            {
                m_asset_service.retry_schema_output(client);
                break;
            }
            case EOutputProducer::process:
            {
                m_asset_service.retry_process_output(client);
                break;
            }
            case EOutputProducer::filesystem:
            {
                retry_refresh_output();
                break;
            }
            case EOutputProducer::report:
            {
                m_asset_service.retry_report_output(client);
                break;
            }
            case EOutputProducer::bundle:
            {
                m_asset_service.retry_bundle_output(client);
                break;
            }
            default:
            {
                break;
            }
        }
        client.output_turn.attempted(selected);
    }
}

void CHost::refresh_module_inputs() noexcept
{
    //  The extent is a fixed catalogue address table, never a compacted list.
    //  Token changes withdraw the old identity before the slot is assigned again.
    SClientRecord* current[k_module_input_slots]{};
    for (SClientRecord* client = first_client(); client; client = next_client(*client))
    {
        if (!client->channel) continue;
        const std::uint64_t module = module_ids::ops::decode_id(client->module).raw_value();
        const std::uint64_t thread = thread_ids::ops::decode_id(client->endpoint->thread_id).raw_value();
        if ((module >= module_ids::k_count) || (thread >= thread_ids::k_count))
        {
            client->endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
            continue;
        }
        const std::uint32_t slot = static_cast<std::uint32_t>((module * thread_ids::k_count) + thread);
        current[slot] = client;
        (void)client->capture_pod_head();
        (void)client->capture_owner_head();
    }
    for (std::uint32_t slot = 0u; slot < k_module_input_slots; ++slot)
    {
        SClientRecord* const client = current[slot];
        const std::uint64_t identity = client ? client->identity.value : 0u;
        if (m_input_identities[slot] != identity)
        {
            m_input_selection.withdraw(slot);
            m_input_lanes[slot] = {};
            m_input_identities[slot] = identity;
        }
        m_input_clients[slot] = client;
        SPressureLane& lane = m_input_lanes[slot];
        lane.runnable = client && module_pod_runnable(*client);
        lane.occupied = client ? client->channel->to_host.readable_count() : 0u;
        lane.capacity = client ? client->channel->to_host.capacity() : 0u;
    }
}

void CHost::drain_module_inputs() noexcept
{
    std::uint32_t controls = 0u;
    const bool controls_drained = drain_module_controls(controls);
    for (SClientRecord* client = first_client(); client; client = next_client(*client))
    {
        if (!client->channel) continue;
        client->pod_admission_failed = client->owner_admission_failed = client->admission_served = false;
        progress_module_output(*client);
    }
    std::uint32_t dispatched = 0u;
    while (controls_drained && (dispatched < 64u))
    {
        if (!drain_module_controls(controls)) break;
        refresh_module_inputs();
        const SPressureSelection selected = m_input_selection.select();
        if (selected.lane == UINT32_MAX) break;
        SClientRecord& client = *m_input_clients[selected.lane];
        bool served = false;
        for (std::uint32_t count = 0u; (count < selected.allowance) && (dispatched < 64u); ++count)
        {
            if (!drain_module_controls(controls)) break;
            if (!module_pod_runnable(client)) break;
            const std::uint32_t replies = client_reply_count(client.pod_head);
            CRetainedReplies::CDispatch dispatch{ *client.pending_replies, (client.terminal() ? 0u : replies),
                client.output_turn.admission_allowed() };
            if (!dispatch || !client.reserve_replies(replies))
            {
                client.pod_admission_failed = true;
                break;
            }
            receive_message(client.pod_head, client);
            client.pod_head = {};
            client.pod_head_live = false;
            client.accounted_pod();
            client.admission_served = served = true;
            ++dispatched;
            m_input_selection.served(selected.lane);
            (void)client.capture_pod_head();
        }
        m_input_selection.finish(selected, served);
    }
    //  Owning submissions keep their existing transport and a separate bounded
    //  round-robin pass. A retained head owns its payload until actual dispatch.
    std::uint32_t owners = 0u;
    for (std::uint32_t visited = 0u; controls_drained && (visited < k_module_input_slots) && (owners < 32u); ++visited)
    {
        if (!drain_module_controls(controls)) break;
        const std::uint32_t slot = m_owner_input_cursor;
        if (++m_owner_input_cursor == k_module_input_slots) m_owner_input_cursor = 0u;
        SClientRecord* const client = m_input_clients[slot];
        if (!client || !module_owner_runnable(*client))
        {
            continue;
        }
        const std::uint32_t replies = owner_reply_count(client->owner_head);
        CRetainedReplies::CDispatch dispatch{ *client->pending_replies, (client->terminal() ? 0u : replies),
            client->output_turn.admission_allowed() };
        if (!dispatch || !client->reserve_replies(replies))
        {
            client->owner_admission_failed = true;
            continue;
        }
        receive_request(client->owner_head, *client);
        client->owner_head = {};
        client->owner_head_live = false;
        client->accounted_owner();
        client->admission_served = true;
        (void)client->capture_owner_head();
        ++owners;
    }
    refresh_module_inputs();
    for (SClientRecord* const client : m_input_clients)
    {
        if (!client) continue;
        progress_module_frontiers(*client);
        const bool runnable_pod = module_pod_runnable(*client), runnable_owner = module_owner_runnable(*client);
        if (client->output_turn.admission_due() &&
            (client->admission_served || (!runnable_pod && !runnable_owner)))
        {
            client->output_turn.admitted_or_examined();
        }
    }
    schedule_module_work();
}

void CHost::schedule_module_work() noexcept
{
    refresh_module_inputs();
    std::uint64_t now = 0u;
    const bool clock_ready = platform::threading::SAwakeClock{}.read(now).code == platform::threading::EWaitError::none;
    for (SClientRecord* const client : m_input_clients)
    {
        if (!client) continue;
        const bool runnable_pod = module_pod_runnable(*client), runnable_owner = module_owner_runnable(*client);
        if (clock_ready)
        {
            //  Source bits identify pressure: output ring, fallback FIFO,
            //  retained POD head, retained owning head, private lifecycle ring.
            const std::uint32_t reasons = (client->channel->to_module.writable_count() ? 0u : 1u) |
                (client->pending_replies->size() ? 2u : 0u) |
                ((client->pod_head_live && !runnable_pod) ? 4u : 0u) |
                ((client->owner_head_live && !runnable_owner) ? 8u : 0u) |
                (client->channel->lifecycle.to_preamble.writable_count() ? 0u : 16u);
            const bool blocked = !client->terminal() && (reasons != 0u);
            const std::uint64_t address = system_ids::ops::make_system_id(client->module, client->endpoint->thread_id).raw_value();
            CPressureDiagnostic::log(client->pressure.observe(now, blocked, client->pending_replies->published()),
                "endpoint pressure", address, client->identity.value, reasons, client->channel->to_module.readable_count(),
                client->channel->to_module.capacity(),
                (client->pending_replies->size() + (client->pod_head_live ? 1u : 0u) + (client->owner_head_live ? 1u : 0u)));
            CPressureDiagnostic::log(client->pod_starvation.observe(now, runnable_pod, client->accounted_pods, true),
                "Host POD ingress starvation", address, client->identity.value, 0u, client->channel->to_host.readable_count(),
                client->channel->to_host.capacity(), (client->pod_head_live ? 1u : 0u));
            CPressureDiagnostic::log(client->owner_starvation.observe(now, runnable_owner, client->accounted_owners, true),
                "Host owning ingress starvation", address, client->identity.value, 0u, 0u, 0u,
                (client->owner_head_live ? 1u : 0u));
        }
        bool controls = client->channel->lifecycle.to_host.readable_count() != 0u;
        if (!client->terminal() && client->channel->lifecycle.to_preamble.writable_count())
        {
            for (const SClientRecord::SControlOutcome& outcome : client->control_outcomes)
            {
                controls = controls || outcome.pending;
            }
        }
        if (controls || runnable_pod || runnable_owner || (!client->terminal() && client->pending_replies->size() &&
                client->channel->to_module.writable_count()) || (client->output_ready() &&
            (m_service_router.output_ready(*client) || m_service_router.borrow_output_ready(*client) ||
                m_asset_service.output_ready(*client) || m_asset_service.schema_output_ready(*client) ||
                m_asset_service.process_output_ready(*client) || m_asset_service.report_output_ready(*client) ||
                m_asset_service.bundle_output_ready(*client) || m_module_service.output_ready(*client) || refresh_output_ready(*client))))
        {
            m_turn_pending = true;
        }
    }
}

}   //  namespace host
