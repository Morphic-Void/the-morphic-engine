
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_connections.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Connection establishment, FIFO publication boundaries and two-sided retirement.

#include "runtime/execution/direct_connections.hpp"

namespace host
{

using connections::EStatus;

static std::atomic<std::uint64_t> s_next_connection{ 1u };

static connections::SToken issue_token() noexcept
{
    std::uint64_t next = s_next_connection.load(std::memory_order_relaxed);
    while (next && !s_next_connection.compare_exchange_weak(next, ((next == UINT64_MAX) ? 0u : next + 1u),
        std::memory_order_relaxed)) {}
    return { next };
}

CDirectConnections::SRecord::SRecord(SClientRecord& first, SClientRecord& second,
    const std::uint32_t value, const connections::SBulkConfiguration configuration) noexcept
    : version{ value }, forward{ second.module }, reverse{ first.module }
    , bulk_configuration{ configuration }
{
    sides[0].client = &first;
    sides[1].client = &second;
}

CDirectConnections::SRecord* CDirectConnections::find(const connections::SToken token) noexcept
{
    for (std::int32_t slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        SRecord* const record = m_records.get_object(slot);
        if (record->token.value == token.value)
        {
            return record;
        }
    }
    return nullptr;
}

std::uint32_t CDirectConnections::count(const SClientRecord& client) const noexcept
{
    std::uint32_t result = 0u;
    for (std::int32_t slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        const SRecord& record = *m_records.get_object(slot);
        if ((record.sides[0].client == &client) || (record.sides[1].client == &client))
        {
            ++result;
        }
    }
    return result;
}

bool CDirectConnections::client_idle(const SClientRecord& client) const noexcept
{
    if (count(client) != 0u) return false;
    for (const SRequest& request : m_requests)
    {
        if (request.requester == &client) return false;
    }
    return true;
}

static bool live_participant(const SClientRecord& client) noexcept
{
    return (client.direct_connections == EDirectConnectionState::available) && !client.stop_requested &&
        !client.seal_pending && !client.seal_acknowledged && !client.terminal() &&
        (!client.endpoint->module_state || !client.endpoint->module_state->stopping.load(std::memory_order_acquire));
}

static system_ids::id_type address(const SClientRecord& client) noexcept
{
    return system_ids::ops::make_system_id(client.module, client.endpoint->thread_id);
}

void CDirectConnections::register_client(SClientRecord& client, const std::int32_t slot) noexcept
{
    EStatus status = EStatus::success;
    if (slot < 0) status = EStatus::invalid_request;
    else if (m_closed || client.stop_requested || client.terminal() || client.seal_pending || client.seal_acknowledged ||
        (client.direct_connections == EDirectConnectionState::closing) ||
        (client.endpoint->module_state && client.endpoint->module_state->stopping.load(std::memory_order_acquire)))
    {
        status = EStatus::closing;
    }
    SLifecycleMessage result;
    result.operation = ELifecycle::participated;
    result.correlation = slot;
    result.notification.connection_status = status;
    client.retain_control(result);
    if (status == EStatus::success) client.direct_connections = EDirectConnectionState::available;
}

void CDirectConnections::refuse(SClientRecord& client, const SClientRecord* const first,
    const SClientRecord* const second, const std::int32_t slot, const EStatus status,
    const connections::SToken token) noexcept
{
    SLifecycleMessage reply;
    reply.operation = ELifecycle::connection_result;
    reply.correlation = slot;
    reply.notification.connection = token;
    reply.notification.first = first ? first->identity : services::SClientToken{};
    reply.notification.second = second ? second->identity : services::SClientToken{};
    reply.notification.connection_status = status;
    client.retain_control(reply);
}

void CDirectConnections::request(SClientRecord& client, SClientRecord* const first, SClientRecord* const second,
    const SDirectRequest& request, const std::int32_t slot,
    const modules::SHostRestrictionReader restriction) noexcept
{
    EStatus rejection = EStatus::success;
    const bool valid_bulk = request.bulk.is_bulk() ?
        (threading::transports::CBulkBufferTransport::valid_capacity(request.bulk.capacity) &&
            ((request.bulk.producer == client.module) || (request.bulk.producer == request.bulk_peer)) &&
            (request.bulk_peer != client.module) && module_ids::ops::is_valid_id(request.bulk_peer)) :
        ((request.bulk.producer == module_ids::id_type{}) && (request.bulk_peer == module_ids::id_type{}) &&
            system_ids::ops::is_valid_id(request.first) && system_ids::ops::is_valid_id(request.second) &&
            (request.first != request.second));
    if ((slot < 0) || !request.version || !valid_bulk || (first && (first == second)))
    {
        rejection = EStatus::invalid_request;
    }
    else if (!request.bulk.is_bulk() && (request.version != 1u)) rejection = EStatus::version_mismatch;
    else if (!first || !second || !first->channel || !second->channel) rejection = EStatus::unavailable;
    else if (m_closed || client.stop_requested || client.terminal() || client.seal_pending || client.seal_acknowledged ||
        (client.direct_connections == EDirectConnectionState::closing))
    {
        rejection = EStatus::closing;
    }
    if (rejection != EStatus::success)
    {
        refuse(client, first, second, slot, rejection);
        return;
    }

    SRecord* selected = nullptr;
    for (std::int32_t existing = m_records.first_live(); existing >= 0; existing = m_records.next_live(existing))
    {
        SRecord& record = *m_records.get_object(existing);
        const std::uint32_t side = (record.sides[0].client == first) ? 0u : 1u;
        if ((record.sides[side].client != first) || (record.sides[1u - side].client != second))
        {
            continue;
        }
        if (record.bulk_configuration.is_bulk() != request.bulk.is_bulk())
        {
            continue;
        }
        if (!live_participant(*first) || !live_participant(*second)) begin_close(record, EStatus::cancelled);
        if (record.phase == EPhase::closing)
        {
            rejection = EStatus::closing;
        }
        else if (record.version != request.version)
        {
            rejection = EStatus::version_mismatch;
        }
        else if (!record.bulk_configuration.matches(request.bulk))
        {
            rejection = EStatus::configuration_mismatch;
        }
        else
        {
            selected = &record;
            break;
        }
        refuse(client, first, second, slot, rejection);
        return;
    }
    if (!live_participant(*first) || !live_participant(*second))
    {
        refuse(client, first, second, slot, EStatus::unavailable);
        return;
    }
    SRequest* pending = nullptr;
    std::uint32_t attached = 0u;
    for (SRequest& candidate : m_requests)
    {
        if (!candidate.requester)
        {
            if (!pending) pending = &candidate;
        }
        else if (selected && (candidate.result.token.value == selected->token.value))
        {
            ++attached;
            if (candidate.requester == &client) rejection = EStatus::busy;
        }
    }
    if (!pending || (attached >= k_requesters) || (rejection != EStatus::success) ||
        (!selected && ((m_count == k_capacity) || (count(*first) == k_per_client) || (count(*second) == k_per_client))))
    {
        refuse(client, first, second, slot, EStatus::busy);
        return;
    }
    if (!selected)
    {
        const connections::SToken token = issue_token();
        if (!token.value)
        {
            refuse(client, first, second, slot, EStatus::busy);
            return;
        }
        if (!m_records.is_ready() && !m_records.initialise())
        {
            refuse(client, first, second, slot, EStatus::allocation_failed);
            return;
        }
        const std::int32_t record_slot = m_records.emplace(*first, *second, request.version, request.bulk);
        if (record_slot < 0)
        {
            refuse(client, first, second, slot, EStatus::allocation_failed);
            return;
        }
        selected = m_records.get_object(record_slot);
        selected->token = token;
        ++m_count;
        const modules::EBudgetStatus budget = selected->budget.start(restriction, UINT64_C(5000000000));
        if ((budget != modules::EBudgetStatus::running) && (budget != modules::EBudgetStatus::restricted))
        {
            begin_close(*selected, EStatus::transport_failed);
        }
    }
    //  Even an existing connection must publish the participant's Activate
    //  before its successful result. Keep this result under the same ordering.
    const EStatus status = (selected->phase == EPhase::active) ? EStatus::already_connected : EStatus::success;
    *pending = { &client, { selected->token, first->identity, second->identity, status }, slot, false };
    ++m_request_count;
}

void CDirectConnections::results(SRecord& record) noexcept
{
    for (SRequest& request : m_requests)
    {
        if (!request.requester || request.ready || (request.result.token.value != record.token.value))
        {
            continue;
        }
        if (record.phase == EPhase::closing)
        {
            request.result.status = record.reason;
            request.ready = true;
        }
        else if (record.phase == EPhase::active)
        {
            request.ready = record.sides[0].activated && record.sides[1].activated;
        }
    }
}

void CDirectConnections::progress_results() noexcept
{
    for (SRequest& request : m_requests)
    {
        if (!request.requester) continue;
        SClientRecord& client = *request.requester;
        if (!request.ready && !client.terminal())
        {
            continue;
        }
        SLifecycleMessage result;
        result.operation = ELifecycle::connection_result;
        result.correlation = request.slot;
        result.notification.connection = request.result.token;
        result.notification.first = request.result.first;
        result.notification.second = request.result.second;
        result.notification.connection_status = request.result.status;
        if (client.terminal() || client.channel->host_control(result))
        {
            request = {};
            --m_request_count;
        }
    }
}

void CDirectConnections::begin_close(SRecord& record, const EStatus reason) noexcept
{
    if (record.phase == EPhase::closing) return;
    record.admission.close();
    record.phase = EPhase::closing;
    record.reason = reason;
}

bool CDirectConnections::install(SRecord& record) noexcept
{
    if (record.bulk_configuration.is_bulk())
    {
        if (!record.bulk.initialise(record.bulk_configuration.capacity))
        {
            return false;
        }
    }
    else
    {
        memory::CMemoryContext& memory = *memory::get_ambient_memory_context();
        if (!record.forward.initialise(memory) || !record.reverse.initialise(memory))
        {
            return false;
        }
    }
    for (SSide& side : record.sides)
    {
        if (!side.client->endpoint->pin_notification(side.notification))
        {
            return false;
        }
        side.pinned = true;
    }
    for (std::uint32_t index = 0u; index < 2u; ++index)
    {
        SSide& side = record.sides[index];
        SSide& peer = record.sides[1u - index];
        side.descriptor = { record.token, side.client->identity, peer.client->identity,
            address(*side.client), address(*peer.client), sizeof(SDirectEndpointDescriptor), 1u, &record.admission,
            (index ? &record.reverse : &record.forward), (index ? &record.forward : &record.reverse),
            side.notification.reader(), peer.notification.endpoint(), record.bulk_configuration };
        if (record.bulk_configuration.is_bulk())
        {
            side.descriptor.outgoing = side.descriptor.incoming = nullptr;
            if (side.client->module == record.bulk_configuration.producer) side.descriptor.bulk_producer = &record.bulk_producer;
            else side.descriptor.bulk_consumer = &record.bulk_consumer;
        }
    }
    return true;
}

void CDirectConnections::decision(const SClientRecord& client, const connections::SToken token, const connections::EStatus status) noexcept
{
    SRecord* const record = find(token);
    if (!record || (record->phase != EPhase::offered))
    {
        return;
    }
    SSide* participant = nullptr;
    for (SSide& side : record->sides)
    {
        if ((side.client == &client) && side.offered && !side.accepted) participant = &side;
    }
    if (!participant) return;
    if (status != EStatus::success)
    {
        begin_close(*record, status);
        return;
    }
    participant->accepted = true;
    if (!record->sides[0].accepted || !record->sides[1].accepted)
    {
        return;
    }
    if (!live_participant(*record->sides[0].client) || !live_participant(*record->sides[1].client))
    {
        begin_close(*record, EStatus::cancelled);
        return;
    }
    if (!install(*record))
    {
        begin_close(*record, EStatus::allocation_failed);
        return;
    }
    record->phase = EPhase::installing;
}

void CDirectConnections::ready(const SClientRecord& client, const connections::SToken token) noexcept
{
    SRecord* const record = find(token);
    if (!record || (record->phase != EPhase::installing))
    {
        return;
    }
    for (SSide& side : record->sides)
    {
        if ((side.client == &client) && side.exposed)
        {
            side.ready = true;
        }
    }
}

void CDirectConnections::activated(const SClientRecord& client, const connections::SToken token) noexcept
{
    SRecord* const record = find(token);
    if (!record || (record->phase != EPhase::activating))
    {
        return;
    }
    //  A late acknowledgement cannot revive a closed or timed-out attempt.
    for (SSide& side : record->sides)
    {
        if ((side.client == &client) && side.activation_sent) side.activated = true;
    }
}

void CDirectConnections::close(const SClientRecord& client, const connections::SToken token, const connections::EStatus reason) noexcept
{
    SRecord* const record = find(token);
    if (record && ((record->sides[0].client == &client) || (record->sides[1].client == &client)))
    {
        begin_close(*record, reason);
    }
}

void CDirectConnections::quiescent(const SClientRecord& client, const connections::SToken token) noexcept
{
    SRecord* const record = find(token);
    if (!record || (record->phase != EPhase::closing))
    {
        return;
    }
    for (SSide& side : record->sides)
    {
        if ((side.client == &client) && side.closing_sent)
        {
            side.quiescent = true;
        }
    }
}

void CDirectConnections::close_client(SClientRecord& client) noexcept
{
    client.direct_connections = EDirectConnectionState::closing;
    for (std::int32_t slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        SRecord& record = *m_records.get_object(slot);
        if ((record.sides[0].client == &client) || (record.sides[1].client == &client))
        {
            begin_close(record, EStatus::cancelled);
        }
    }
}

void CDirectConnections::close_all() noexcept
{
    m_closed = true;
    for (std::int32_t slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        begin_close(*m_records.get_object(slot), EStatus::cancelled);
    }
}

void CDirectConnections::limit_wait(std::uint64_t& remaining) noexcept
{
    for (std::int32_t slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        SRecord& record = *m_records.get_object(slot);
        if ((record.phase != EPhase::offered) && (record.phase != EPhase::installing) && (record.phase != EPhase::activating))
        {
            continue;
        }
        const modules::SEligibleBudgetObservation budget = record.budget.observe();
        if (budget.status == modules::EBudgetStatus::running)
        {
            if (budget.remaining_ns < remaining) remaining = budget.remaining_ns;
        }
        else if (budget.status != modules::EBudgetStatus::restricted)
        {
            remaining = 0u;
        }
    }
}

void CDirectConnections::progress() noexcept
{
    for (std::int32_t slot = m_records.first_live(); slot >= 0; )
    {
        SRecord& record = *m_records.get_object(slot);
        const std::int32_t next = m_records.next_live(slot);
        for (const SSide& side : record.sides)
        {
            if (!live_participant(*side.client))
            {
                begin_close(record, EStatus::cancelled);
            }
        }
        if ((record.phase == EPhase::offered) || (record.phase == EPhase::installing) || (record.phase == EPhase::activating))
        {
            const modules::EBudgetStatus budget = record.budget.observe().status;
            if (budget == modules::EBudgetStatus::expired)
            {
                begin_close(record, EStatus::timed_out);
            }
            else if (budget == modules::EBudgetStatus::failed)
            {
                begin_close(record, EStatus::transport_failed);
            }
        }
        if (record.phase == EPhase::offered)
        {
            for (std::uint32_t index = 0u; index < 2u; ++index)
            {
                SSide& side = record.sides[index];
                if (side.offered || side.accepted)
                {
                    continue;
                }
                const SClientRecord& peer = *record.sides[1u - index].client;
                //  Participation must be published before any offer.
                side.client->flush_controls();
                bool pending = false;
                for (const SClientRecord::SControlOutcome& outcome : side.client->control_outcomes)
                {
                    pending = pending || (outcome.pending && (outcome.message.operation == ELifecycle::participated));
                }
                if (pending) continue;
                SLifecycleMessage offer;
                offer.operation = ELifecycle::offer;
                offer.notification.connection = record.token;
                offer.notification.first = peer.identity;
                offer.notification.peer = address(peer);
                offer.notification.connection_version = record.version;
                offer.notification.bulk = record.bulk_configuration;
                side.offered = side.client->channel->host_control(offer);
            }
        }
        if (record.phase == EPhase::installing)
        {
            for (SSide& side : record.sides)
            {
                if (side.exposed) continue;
                SLifecycleMessage install;
                install.operation = ELifecycle::install;
                install.notification.connection = record.token;
                install.descriptor = &side.descriptor;
                side.exposed = side.client->channel->host_control(install);
            }
            if ((record.phase == EPhase::installing) && record.sides[0].ready && record.sides[1].ready)
            {
                record.phase = EPhase::activating;
                record.admission.activate();
            }
        }
        if ((record.phase == EPhase::activating) || (record.phase == EPhase::active))
        {
            for (std::uint32_t index = 0u; index < 2u; ++index)
            {
                SSide& side = record.sides[index];
                if (!side.activation_sent)
                {
                    SLifecycleMessage activate;
                    activate.operation = ELifecycle::activate;
                    activate.notification.connection = record.token;
                    side.activation_sent = side.client->channel->host_control(activate);
                }
            }
            if ((record.phase == EPhase::activating) && record.sides[0].activated && record.sides[1].activated)
            {
                record.phase = EPhase::active;
            }
        }
        results(record);
        if (record.phase == EPhase::closing)
        {
            for (std::uint32_t index = 0u; index < 2u; ++index)
            {
                SSide& side = record.sides[index];
                if (!side.closing_sent)
                {
                    SLifecycleMessage closing;
                    closing.operation = ELifecycle::closing;
                    closing.notification.connection = record.token;
                    closing.notification.connection_status = record.reason;
                    side.closing_sent = (!side.offered && !side.exposed) || side.client->terminal() ||
                        side.client->channel->host_control(closing);
                }
                if (!side.exposed && !side.offered)
                {
                    side.quiescent = true;
                }
                else if (!side.quiescent && side.client->execution_quiescent() &&
                    (!side.client->thread || side.client->thread->join_terminated_execution()))
                {
                    side.quiescent = true;
                }
            }
            if (record.sides[0].quiescent && record.sides[1].quiescent && record.admission.drained())
            {
                for (SSide& side : record.sides)
                {
                    if (!side.retired_sent)
                    {
                        SLifecycleMessage retired;
                        retired.operation = ELifecycle::retired;
                        retired.notification.connection = record.token;
                        side.retired_sent = side.client->terminal() || (!side.offered && !side.exposed) ||
                            (side.closing_sent && side.client->channel->host_control(retired));
                    }
                }
                if (record.sides[0].retired_sent && record.sides[1].retired_sent)
                {
                    for (SSide& side : record.sides)
                    {
                        if (side.pinned)
                        {
                            side.client->endpoint->unpin_notification();
                        }
                    }
                    std::int32_t erased = slot;
                    (void)m_records.erase(erased);
                    --m_count;
                }
            }
        }
        slot = next;
    }
    progress_results();
}

}   //  namespace host
