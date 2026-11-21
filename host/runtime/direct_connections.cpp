
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_connections.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Connection establishment, FIFO publication boundaries and two-sided retirement.

#include "runtime/direct_connections.hpp"
#include "framework/threading/messages/message_helpers.hpp"

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
    : version{ value }, forward{ second.module }, reverse{ first.module }, bulk_configuration{ configuration }
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

void CDirectConnections::register_client(SClientRecord& client, const connections::Register& registration,
    const std::int32_t slot) noexcept
{
    EStatus status = EStatus::success;
    if (slot < 0) status = EStatus::invalid_request;
    else if (registration.version != 1u) status = EStatus::version_mismatch;
    else if (m_closed || client.stop_requested || client.terminal() || client.seal_pending || client.seal_acknowledged ||
        (client.direct_connections == EDirectConnectionState::closing) ||
        (client.endpoint->module_state && client.endpoint->module_state->stopping.load(std::memory_order_acquire)))
    {
        status = EStatus::closing;
    }
    //  Success must be in the ordinary FIFO before availability permits offers.
    if (!client.reply(threading::make_pod_message(slot, connections::Registered{ status })))
    {
        m_failed = true;
        return;
    }
    if (status == EStatus::success)
    {
        client.direct_connections = EDirectConnectionState::available;
    }
}

void CDirectConnections::refuse(SClientRecord& client, const SClientRecord* const first,
    const SClientRecord* const second, const std::int32_t slot, const EStatus status,
    const connections::SToken token) noexcept
{
    const connections::Result result{ token, (first ? first->identity : services::SClientToken{}),
        (second ? second->identity : services::SClientToken{}), status };
    if (!client.reply(threading::make_pod_message(slot, result)))
    {
        m_failed = true;
        if (client.endpoint->host_to_worker_msgs.posting_poisoned())
        {
            client.request_exit();
        }
    }
}

void CDirectConnections::request(SClientRecord& client, SClientRecord* const first, SClientRecord* const second,
    const connections::Request& request, const std::int32_t slot,
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
    else if (!first || !second) rejection = EStatus::unavailable;
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

bool CDirectConnections::publish(const SSide& side, const threading::CErasedPodMsg& message) noexcept
{
    if (side.client->post(message)) return true;
    if (side.client->endpoint->host_to_worker_msgs.posting_poisoned())
    {
        side.client->request_exit();
        m_failed = true;
    }
    return false;
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
            const SClientRecord* const requester = request.requester;
            request.ready = ((requester == record.sides[0].client) && record.sides[0].activated) ||
                ((requester == record.sides[1].client) && record.sides[1].activated) ||
                (record.sides[0].activated && record.sides[1].activated);
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
        const EReplyPublication delivery = client.publish_reply(threading::make_pod_message(request.slot, request.result));
        if (delivery != EReplyPublication::busy)
        {
            request = {};
            --m_request_count;
        }
        else if (client.endpoint->host_to_worker_msgs.posting_poisoned())
        {
            client.request_exit();
            m_failed = true;
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
    else if (!record.forward.initialise_growable(0u) || !record.reverse.initialise_growable(0u))
    {
        return false;
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
            address(*side.client), address(*peer.client), sizeof(connections::SEndpointDescriptor), record.version, &record.admission,
            (index ? &record.reverse : &record.forward), (index ? &record.forward : &record.reverse),
            side.notification.reader(), peer.notification.endpoint() };
        if (record.bulk_configuration.is_bulk())
        {
            side.descriptor.outgoing = nullptr;
            side.descriptor.incoming = nullptr;
            side.descriptor.bulk = record.bulk_configuration;
            if (side.client->module == record.bulk_configuration.producer)
            {
                side.descriptor.bulk_producer = &record.bulk_producer;
            }
            else
            {
                side.descriptor.bulk_consumer = &record.bulk_consumer;
            }
        }
    }
    return true;
}

void CDirectConnections::decision(const SClientRecord& client, const connections::Decision& decision) noexcept
{
    SRecord* const record = find(decision.token);
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
    if (decision.status != EStatus::success)
    {
        begin_close(*record, decision.status);
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

void CDirectConnections::ready(const SClientRecord& client, const connections::Ready& ready) noexcept
{
    SRecord* const record = find(ready.token);
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

void CDirectConnections::close(const SClientRecord& client, const connections::Close& close) noexcept
{
    SRecord* const record = find(close.token);
    if (record && ((record->sides[0].client == &client) || (record->sides[1].client == &client)))
    {
        begin_close(*record, close.reason);
    }
}

void CDirectConnections::quiescent(const SClientRecord& client, const connections::Quiescent& quiescent) noexcept
{
    SRecord* const record = find(quiescent.token);
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
        if ((record.phase != EPhase::offered) && (record.phase != EPhase::installing))
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
        if ((record.phase == EPhase::offered) || (record.phase == EPhase::installing))
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
                side.offered = publish(side, threading::make_pod_message(-1,
                    connections::Offer{ record.token, peer.identity, address(peer), record.version, record.bulk_configuration }));
                if (!side.offered && side.client->endpoint->host_to_worker_msgs.posting_poisoned())
                {
                    //  An uncertain offer also needs a final FIFO boundary.
                    side.offered = true;
                    begin_close(record, EStatus::transport_failed);
                    break;
                }
            }
        }
        if (record.phase == EPhase::installing)
        {
            for (SSide& side : record.sides)
            {
                if (side.exposed) continue;
                const bool queued = publish(side, threading::make_pod_message(-1, connections::Install{ record.token, &side.descriptor }));
                //  A poisoned queue can have published before reporting failure.
                side.exposed = queued || side.client->endpoint->host_to_worker_msgs.posting_poisoned();
                if (!queued && side.exposed)
                {
                    begin_close(record, EStatus::transport_failed);
                }
                if (record.phase == EPhase::closing) break;
            }
            if ((record.phase == EPhase::installing) && record.sides[0].ready && record.sides[1].ready)
            {
                record.phase = EPhase::active;
                record.admission.activate();
            }
        }
        if (record.phase == EPhase::active)
        {
            for (std::uint32_t index = 0u; index < 2u; ++index)
            {
                SSide& side = record.sides[index];
                if (!side.activated)
                {
                    side.activated = publish(side, threading::make_pod_message(-1, connections::Activate{ record.token }));
                }
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
                    side.closing_sent = (!side.offered && !side.exposed) ||
                        publish(side, threading::make_pod_message(-1, connections::Closing{ record.token, record.reason }));
                }
                if (!side.exposed) side.quiescent = true;
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
                        side.retired_sent = side.client->terminal() || (!side.offered && !side.exposed) || (side.closing_sent &&
                            publish(side, threading::make_pod_message(-1, connections::Retired{ record.token })));
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
