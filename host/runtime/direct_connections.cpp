
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_connections.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Connection establishment, FIFO publication boundaries and two-sided retirement.

#include "runtime/direct_connections.hpp"
#include "threading/messages/message_helpers.hpp"

namespace host
{

using connections::EStatus;

static std::atomic<std::uint64_t> s_next_connection{ 1u };

static connections::SToken issue_token() noexcept
{
    auto next = s_next_connection.load(std::memory_order_relaxed);
    while (next && !s_next_connection.compare_exchange_weak(next, ((next == UINT64_MAX) ? 0u : next + 1u),
        std::memory_order_relaxed)) {}
    return { next };
}

CDirectConnections::SRecord::SRecord(SClientRecord& first, SClientRecord& second,
    const std::uint32_t value) noexcept
    : version{ value }, forward{ second.module }, reverse{ first.module }
{
    sides[0].client = &first;
    sides[1].client = &second;
}

CDirectConnections::SRecord* CDirectConnections::find(const connections::SToken token) noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        auto* const record = m_records.get_object(slot);
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
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        const auto& record = *m_records.get_object(slot);
        if ((record.sides[0].client == &client) || (record.sides[1].client == &client))
        {
            ++result;
        }
    }
    return result;
}

bool CDirectConnections::client_idle(const SClientRecord& client) const noexcept
{
    return count(client) == 0u;
}

static bool live_participant(const SClientRecord& client) noexcept
{
    return client.thread && (client.direct_connections == EDirectConnectionState::available) && !client.stop_requested &&
        (client.phase == EClientPhase::ready) && !client.terminal();
}

void CDirectConnections::register_client(SClientRecord& client, const connections::Register& registration) noexcept
{
    if (!m_closed && (registration.version == 1u) && client.thread && !client.stop_requested &&
        (client.direct_connections != EDirectConnectionState::closing))
    {
        client.direct_connections = EDirectConnectionState::available;
    }
}

void CDirectConnections::refuse(SClientRecord& client, const SClientRecord* const peer,
    const connections::Request& request, const std::int32_t slot, const EStatus status,
    const connections::SToken token) noexcept
{
    const connections::Result result{ token, (peer ? peer->instance : modules::SModuleInstance{}), request.peer, status };
    if (!client.reply(threading::make_pod_message(slot, result)))
    {
        m_failed = true;
        if (client.thread && client.thread->pod_delivery_poisoned())
        {
            client.request_exit();
        }
    }
}

void CDirectConnections::request(SClientRecord& client, SClientRecord* const peer,
    const connections::Request& request, const std::int32_t slot,
    const modules::SHostRestrictionReader restriction) noexcept
{
    EStatus rejection = EStatus::success;
    if ((slot < 0) || !request.version || (request.peer == client.module) || !module_ids::ops::is_valid_id(request.peer))
    {
        rejection = EStatus::invalid_request;
    }
    else if (!peer) rejection = EStatus::unavailable;
    else if (m_closed || client.stop_requested || peer->stop_requested || client.terminal() || peer->terminal() ||
        (client.direct_connections == EDirectConnectionState::closing) || (peer->direct_connections == EDirectConnectionState::closing))
    {
        rejection = EStatus::closing;
    }
    else if (!live_participant(client) || !live_participant(*peer))
    {
        rejection = EStatus::unsupported;
    }
    if (rejection != EStatus::success)
    {
        refuse(client, peer, request, slot, rejection);
        return;
    }

    for (auto existing = m_records.first_live(); existing >= 0; existing = m_records.next_live(existing))
    {
        auto& record = *m_records.get_object(existing);
        const auto side = (record.sides[0].client == &client) ? 0u : 1u;
        if ((record.sides[side].client != &client) || (record.sides[1u - side].client != peer))
        {
            continue;
        }
        if (record.phase == EPhase::closing)
        {
            rejection = EStatus::closing;
        }
        else if (record.version != request.version)
        {
            rejection = EStatus::version_mismatch;
        }
        else if (record.phase == EPhase::active)
        {
            rejection = EStatus::already_connected;
        }
        else if (record.sides[side].initiated)
        {
            rejection = EStatus::busy;
        }
        else
        {
            record.sides[side].initiated = true;
            record.sides[side].slot = slot;
            return;
        }
        refuse(client, peer, request, slot, rejection,
            ((rejection == EStatus::already_connected) ? record.token : connections::SToken{}));
        return;
    }
    if ((m_count == k_capacity) || (count(client) == k_per_client) || (count(*peer) == k_per_client))
    {
        refuse(client, peer, request, slot, EStatus::busy);
        return;
    }
    const auto token = issue_token();
    if (!token.value)
    {
        refuse(client, peer, request, slot, EStatus::busy);
        return;
    }
    if (!m_records.is_ready() && !m_records.initialise())
    {
        refuse(client, peer, request, slot, EStatus::allocation_failed);
        return;
    }
    const auto record_slot = m_records.emplace(client, *peer, request.version);
    if (record_slot < 0)
    {
        refuse(client, peer, request, slot, EStatus::allocation_failed);
        return;
    }
    auto& record = *m_records.get_object(record_slot);
    record.token = token;
    record.sides[0].slot = slot;
    record.sides[0].initiated = true;
    ++m_count;
    const auto budget = record.budget.start(restriction, 5000000000u);
    if ((budget != modules::EBudgetStatus::running) && (budget != modules::EBudgetStatus::restricted))
    {
        begin_close(record, EStatus::transport_failed);
    }
}

bool CDirectConnections::publish(const SSide& side, const threading::CErasedPodMsg& message) noexcept
{
    if (side.client->post_reply(message)) return true;
    if (side.client->thread && side.client->thread->pod_delivery_poisoned())
    {
        side.client->request_exit();
        m_failed = true;
    }
    return false;
}

void CDirectConnections::result(SRecord& record, const std::uint32_t side, const EStatus status) noexcept
{
    auto& recipient = record.sides[side];
    if (recipient.slot < 0) return;
    const auto& peer = *record.sides[1u - side].client;
    const connections::Result result{ record.token, peer.instance, peer.module, status };
    const auto delivery = recipient.client->publish_reply(threading::make_pod_message(recipient.slot, result));
    if (delivery != EReplyPublication::busy)
    {
        recipient.slot = -1;
    }
    else if (recipient.client->thread && recipient.client->thread->pod_delivery_poisoned())
    {
        recipient.client->request_exit();
        m_failed = true;
    }
}

void CDirectConnections::begin_close(SRecord& record, const EStatus reason) noexcept
{
    if (record.phase == EPhase::closing) return;
    record.phase = EPhase::closing;
    record.reason = reason;
}

bool CDirectConnections::install(SRecord& record) noexcept
{
    if (!record.forward.initialise_growable(0u) || !record.reverse.initialise_growable(0u))
    {
        return false;
    }
    for (auto& side : record.sides)
    {
        if (!side.client->thread->pin_notification(side.notification))
        {
            return false;
        }
        side.pinned = true;
    }
    for (std::uint32_t index = 0u; index < 2u; ++index)
    {
        auto& side = record.sides[index];
        auto& peer = record.sides[1u - index];
        side.descriptor = { record.token, side.client->instance, peer.client->instance,
            side.client->module, peer.client->module, record.version,
            (index ? &record.reverse : &record.forward), (index ? &record.forward : &record.reverse),
            side.notification.reader(), peer.notification.endpoint() };
    }
    return true;
}

void CDirectConnections::decision(const SClientRecord& client, const connections::Decision& decision) noexcept
{
    auto* const record = find(decision.token);
    if (!record || (record->phase != EPhase::offered) || (record->sides[1].client != &client) || !record->sides[1].offered)
    {
        return;
    }
    if (decision.status != EStatus::success)
    {
        begin_close(*record, decision.status);
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
    auto* const record = find(ready.token);
    if (!record || (record->phase != EPhase::installing))
    {
        return;
    }
    for (auto& side : record->sides)
    {
        if ((side.client == &client) && side.exposed)
        {
            side.ready = true;
        }
    }
}

void CDirectConnections::close(const SClientRecord& client, const connections::Close& close) noexcept
{
    auto* const record = find(close.token);
    if (record && ((record->sides[0].client == &client) || (record->sides[1].client == &client)))
    {
        begin_close(*record, close.reason);
    }
}

void CDirectConnections::quiescent(const SClientRecord& client, const connections::Quiescent& quiescent) noexcept
{
    auto* const record = find(quiescent.token);
    if (!record || (record->phase != EPhase::closing))
    {
        return;
    }
    for (auto& side : record->sides)
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
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        auto& record = *m_records.get_object(slot);
        if ((record.sides[0].client == &client) || (record.sides[1].client == &client))
        {
            begin_close(record, EStatus::cancelled);
        }
    }
}

void CDirectConnections::close_all() noexcept
{
    m_closed = true;
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        begin_close(*m_records.get_object(slot), EStatus::cancelled);
    }
}

void CDirectConnections::progress() noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; )
    {
        auto& record = *m_records.get_object(slot);
        const auto next = m_records.next_live(slot);
        for (const auto& side : record.sides)
        {
            if (side.client->stop_requested || side.client->terminal())
            {
                begin_close(record, EStatus::cancelled);
            }
        }
        if ((record.phase == EPhase::offered) || (record.phase == EPhase::installing))
        {
            const auto budget = record.budget.observe().status;
            if (budget == modules::EBudgetStatus::expired)
            {
                begin_close(record, EStatus::timed_out);
            }
            else if (budget == modules::EBudgetStatus::failed)
            {
                begin_close(record, EStatus::transport_failed);
            }
        }
        if ((record.phase == EPhase::offered) && !record.sides[1].offered)
        {
            auto& peer = *record.sides[0].client;
            record.sides[1].offered = publish(record.sides[1], threading::make_pod_message(-1,
                connections::Offer{ record.token, peer.instance, peer.module, record.version }));
        }
        if (record.phase == EPhase::installing)
        {
            for (auto& side : record.sides)
            {
                if (side.exposed) continue;
                const bool queued = publish(side, threading::make_pod_message(-1, connections::Install{ record.token, &side.descriptor }));
                //  A poisoned queue can have published before reporting failure.
                side.exposed = queued || side.client->thread->pod_delivery_poisoned();
                if (!queued && side.exposed)
                {
                    begin_close(record, EStatus::transport_failed);
                }
                if (record.phase == EPhase::closing) break;
            }
            if ((record.phase == EPhase::installing) && record.sides[0].ready && record.sides[1].ready)
            {
                record.phase = EPhase::active;
            }
        }
        if (record.phase == EPhase::active)
        {
            for (std::uint32_t index = 0u; index < 2u; ++index)
            {
                auto& side = record.sides[index];
                if (!side.activated)
                {
                    side.activated = publish(side, threading::make_pod_message(-1, connections::Activate{ record.token }));
                }
                if (side.activated)
                {
                    result(record, index, EStatus::success);
                }
            }
        }
        if (record.phase == EPhase::closing)
        {
            for (std::uint32_t index = 0u; index < 2u; ++index)
            {
                auto& side = record.sides[index];
                result(record, index, record.reason);
                if (!side.closing_sent && (side.slot < 0))
                {
                    side.closing_sent = publish(side, threading::make_pod_message(-1, connections::Closing{ record.token, record.reason }));
                }
                if (!side.exposed) side.quiescent = true;
                else if (!side.quiescent && side.client->execution_quiescent() && side.client->thread->join_terminated_execution())
                {
                    side.quiescent = true;
                }
            }
            if (record.sides[0].quiescent && record.sides[1].quiescent)
            {
                for (auto& side : record.sides)
                {
                    if (side.slot >= 0) continue;
                    if (!side.retired_sent)
                    {
                        side.retired_sent = side.client->terminal() || (side.closing_sent &&
                            publish(side, threading::make_pod_message(-1, connections::Retired{ record.token })));
                    }
                }
                if (record.sides[0].retired_sent && record.sides[1].retired_sent)
                {
                    for (auto& side : record.sides)
                    {
                        if (side.pinned)
                        {
                            side.client->thread->unpin_notification();
                        }
                    }
                    auto erased = slot;
                    (void)m_records.erase(erased);
                    --m_count;
                }
            }
        }
        slot = next;
    }
}

}   //  namespace host
