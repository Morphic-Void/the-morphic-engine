
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_borrows.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Publication and retirement are separate from request completion.

#include <cstring>
#include "runtime/services/service_borrows.hpp"
#include "framework/threading/messages/message_helpers.hpp"

namespace host
{

using services::EServiceStatus;
using services::EServiceQuiescence;
using services::EServiceBorrowControl;

CServiceBorrows::SRecord* CServiceBorrows::find(const services::SServiceBorrow borrow) noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        auto* const record = m_records.get_object(slot);
        if (record->token.value == borrow.value) return record;
    }
    return nullptr;
}

const CServiceBorrows::SRecord* CServiceBorrows::find(const services::SServiceBorrow borrow) const noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        const auto* const record = m_records.get_object(slot);
        if (record->token.value == borrow.value) return record;
    }
    return nullptr;
}

EServiceStatus CServiceBorrows::prepare(SClientRecord& provider, SClientRecord& client,
    const services::SServiceBorrowOffer& offer, const std::int32_t reply_slot,
    const std::uint16_t opcode, const bool dispatched, services::SServiceBorrow& borrow) noexcept
{
    borrow = {};
    if ((offer.view.descriptor == nullptr) || (offer.view.size == 0u) || (offer.view.version == 0u))
    {
        return EServiceStatus::invalid_request;
    }
    if (client.terminal() || client.stop_requested || provider.stop_requested) return EServiceStatus::closing;
    if ((m_next == 0u) || (m_count == k_capacity)) return EServiceStatus::busy;
    if (!m_records.is_ready() && !m_records.initialise()) return EServiceStatus::allocation_failed;
    if (!client.reserve_replies(1u)) return EServiceStatus::busy;
    const auto slot = m_records.emplace();
    if (slot < 0)
    {
        client.cancel_reservation(1u);
        return EServiceStatus::allocation_failed;
    }
    auto& record = *m_records.get_object(slot);
    record.token.value = m_next++;
    record.offer = offer;
    record.provider = &provider;
    record.client = &client;
    record.slot = reply_slot;
    record.opcode = opcode;
    record.dispatched = dispatched;
    borrow = record.token;
    ++m_count;
    return EServiceStatus::success;
}

void CServiceBorrows::dispatched(const services::SServiceBorrow borrow) noexcept
{
    if (auto* const record = find(borrow)) record->dispatched = true;
}

bool CServiceBorrows::publication_settled(const services::SServiceBorrow borrow) const noexcept
{
    const auto* const record = find(borrow);
    return (record == nullptr) || record->settled;
}

void CServiceBorrows::close(SRecord& record) noexcept
{
    record.closing = true;
    if (!record.settled)
    {
        record.client->cancel_reservation(1u);
        record.settled = true;
        record.quiescence = EServiceQuiescence::never_published;
    }
}

EServiceStatus CServiceBorrows::begin_retirement(SClientRecord& provider, const services::SServiceBorrow borrow) noexcept
{
    auto* const record = find(borrow);
    if ((record == nullptr) || (record->provider != &provider)) return EServiceStatus::stale;
    close(*record);
    return EServiceStatus::success;
}

EServiceStatus CServiceBorrows::finish_retirement(SClientRecord& provider, const services::SServiceBorrow borrow) noexcept
{
    auto* const record = find(borrow);
    if ((record == nullptr) || (record->provider != &provider)) return EServiceStatus::stale;
    //  The provider has received permission to perform its own final cleanup.
    //  Finishing then needs no allocation, publication or additional reply credit.
    if (!record->closing || !record->quiescence_notified) return EServiceStatus::busy;
    record->finished = true;
    record->offer.view = {};
    return EServiceStatus::success;
}

void CServiceBorrows::release(SClientRecord& client, const services::ReleaseServiceBorrow& request) noexcept
{
    auto* const record = find(request.borrow);
    if ((record != nullptr) && (record->client == &client) && (record->offer.route.value == request.route.value)) close(*record);
}

services::ServiceNotificationRegistered CServiceBorrows::register_notification(SClientRecord& client,
    const services::RegisterServiceNotification& request) noexcept
{
    services::ServiceNotificationRegistered result{ request.route, request.borrow };
    auto* const record = find(request.borrow);
    if (!record || (record->client != &client) || (record->offer.route.value != request.route.value))
    {
        result.status = EServiceStatus::stale;
        return result;
    }
    if (record->closing || client.terminal() || client.stop_requested)
    {
        result.status = EServiceStatus::closing;
        return result;
    }
    if (!record->published) return result;
    if (record->registration.value == 0u)
    {
        if (m_next_registration == 0u) { result.status = EServiceStatus::busy; return result; }
        if (!client.endpoint->pin_notification(record->notification))
        {
            return result;
        }
        record->pinned_endpoint = client.endpoint;
        record->registration.value = m_next_registration++;
    }
    result.registration = record->registration;
    result.reader = record->notification.reader();
    result.status = EServiceStatus::success;
    return result;
}

EServiceStatus CServiceBorrows::bind_notification(SClientRecord& provider, const services::SServiceBorrow borrow,
    const services::SNotificationRegistration registration, threading::SNotificationEndpoint& endpoint) noexcept
{
    endpoint = {};
    auto* const record = find(borrow);
    if (!record || (record->provider != &provider) || !registration.value ||
        (record->registration.value != registration.value)) return EServiceStatus::stale;
    if (record->closing || record->client->terminal() || record->client->stop_requested) return EServiceStatus::closing;
    endpoint = record->notification.endpoint();
    return EServiceStatus::success;
}

void CServiceBorrows::quiescent(SClientRecord& client, const services::ServiceBorrowQuiescent& request) noexcept
{
    auto* const record = find(request.borrow);
    if ((record == nullptr) || (record->client != &client) || (record->offer.route.value != request.route.value) ||
        !record->closing || !record->published || (record->quiescence != EServiceQuiescence::none)) return;
    record->quiescence = EServiceQuiescence::relinquished;
}

void CServiceBorrows::cancel(const services::SServiceBorrow borrow) noexcept
{
    if (auto* const record = find(borrow)) close(*record);
}

void CServiceBorrows::cancel_request(SClientRecord& client, const services::CancelServiceRequest& request) noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        auto& record = *m_records.get_object(slot);
        if ((record.client == &client) && (record.offer.route.value == request.route.value) &&
            (record.offer.request.value == request.request.value)) close(record);
    }
}

void CServiceBorrows::retire_client(SClientRecord& client, const bool execution_quiescent) noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        auto& record = *m_records.get_object(slot);
        if (record.client != &client) continue;
        close(record);
        if (execution_quiescent && (record.quiescence == EServiceQuiescence::none))
        {
            record.quiescence = EServiceQuiescence::execution_ended;
        }
    }
}

void CServiceBorrows::stop_provider(SClientRecord& provider) noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        auto& record = *m_records.get_object(slot);
        if (record.provider == &provider) close(record);
    }
}

bool CServiceBorrows::notify(const SRecord& record, SClientRecord& recipient, const EServiceBorrowControl control) noexcept
{
    const services::ServiceBorrowControl message{ record.offer.route, record.offer.request, record.token, control, record.quiescence };
    return recipient.post(threading::make_pod_message(-1, message));
}

void CServiceBorrows::publish_prepared(SRecord& record) noexcept
{
    services::ServiceReply reply;
    reply.route = record.offer.route;
    reply.request = record.offer.request;
    reply.opcode = record.opcode;
    reply.notice = services::EServiceNotice::prepared;
    reply.status = EServiceStatus::success;
    reply.payload_size = sizeof(services::SPreparedServiceBorrow);
    const services::SPreparedServiceBorrow payload{ record.token, record.offer.view };
    std::memcpy(reply.payload, &payload, sizeof(payload));
    const EReplyPublication result = record.client->publish_reply(threading::make_pod_message(record.slot, reply));
    if (result == EReplyPublication::queued)
    {
        record.settled = record.published = true;
    }
    else if (result == EReplyPublication::cancelled)
    {
        record.settled = true;
        record.quiescence = EServiceQuiescence::never_published;
        record.closing = true;
    }
}

void CServiceBorrows::erase(const std::int32_t slot) noexcept
{
    SRecord& record = *m_records.get_object(slot);
    //  Both proofs cover every reader/publisher, including final wake signals.
    if (record.pinned_endpoint) record.pinned_endpoint->unpin_notification();
    (void)m_records.erase(slot);
    --m_count;
}

CServiceBorrows::EOutput CServiceBorrows::output(const SRecord& record, const SClientRecord& client) noexcept
{
    if (record.client == &client)
    {
        if (!record.settled && record.dispatched)
        {
            return EOutput::prepared;
        }
        if (record.closing && !record.client_notified)
        {
            return EOutput::client_closing;
        }
        if (record.closing && record.finished && record.client_notified)
        {
            return EOutput::retired;
        }
    }
    if ((record.provider == &client) && record.closing && !client.terminal())
    {
        if (!record.provider_notified)
        {
            return EOutput::provider_closing;
        }
        if (!record.quiescence_notified && (record.quiescence != EServiceQuiescence::none))
        {
            return EOutput::quiescent;
        }
    }
    return EOutput::none;
}

bool CServiceBorrows::output_ready(const SClientRecord& client) const noexcept
{
    for (std::int32_t slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        if (output(*m_records.get_object(slot), client) != EOutput::none)
        {
            return true;
        }
    }
    return false;
}

void CServiceBorrows::retry_output(SClientRecord& client) noexcept
{
    std::int32_t& cursor = client.output_cursors[static_cast<std::uint32_t>(EOutputProducer::borrow)];
    std::int32_t first = -1, following = -1;
    for (std::int32_t slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        if (output(*m_records.get_object(slot), client) == EOutput::none)
        {
            continue;
        }
        if ((first < 0) || (slot < first)) first = slot;
        if ((slot > cursor) && ((following < 0) || (slot < following))) following = slot;
    }
    const std::int32_t slot = (following >= 0) ? following : first;
    if (slot < 0) return;
    cursor = slot;
    SRecord& record = *m_records.get_object(slot);
    switch (output(record, client))
    {
        case EOutput::prepared:
        {
            publish_prepared(record);
            break;
        }
        case EOutput::client_closing:
        {
            record.client_notified = client.terminal() || notify(record, client, EServiceBorrowControl::closing);
            break;
        }
        case EOutput::provider_closing:
        {
            record.provider_notified = notify(record, client, EServiceBorrowControl::closing);
            break;
        }
        case EOutput::quiescent:
        {
            record.quiescence_notified = notify(record, client, EServiceBorrowControl::consumer_quiescent);
            break;
        }
        case EOutput::retired:
        {
            if (client.terminal() || notify(record, client, EServiceBorrowControl::retired)) erase(slot);
            break;
        }
        case EOutput::none:
        {
            break;
        }
    }
}

bool CServiceBorrows::route_idle(const services::SServiceRoute route) const noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        if (m_records.get_object(slot)->offer.route.value == route.value) return false;
    }
    return true;
}

bool CServiceBorrows::provider_retired(const SClientRecord& provider) const noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        const auto& record = *m_records.get_object(slot);
        if ((record.provider == &provider) && !record.finished) return false;
    }
    return true;
}

}   //  namespace host
