
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_borrow_client.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Closing controls may precede prepared replies; a late view never reopens use.

#include <cstring>
#include "services/service_borrow_client.hpp"

namespace services
{

CServiceBorrowClient::SRecord* CServiceBorrowClient::find(const SServiceRoute route, const SServiceRequestId request) noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        auto* const record = m_records.get_object(slot);
        if ((record->route.value == route.value) && (record->request.value == request.value)) return record;
    }
    return nullptr;
}

const CServiceBorrowClient::SRecord* CServiceBorrowClient::find(const SServiceRoute route, const SServiceRequestId request) const noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        const auto* const record = m_records.get_object(slot);
        if ((record->route.value == route.value) && (record->request.value == request.value)) return record;
    }
    return nullptr;
}

CServiceBorrowClient::SRecord* CServiceBorrowClient::find(const SServiceBorrow borrow) noexcept
{
    if (borrow.value == 0u) return nullptr;
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        auto* const record = m_records.get_object(slot);
        if (record->prepared.borrow.value == borrow.value) return record;
    }
    return nullptr;
}

const CServiceBorrowClient::SRecord* CServiceBorrowClient::find(const SServiceBorrow borrow) const noexcept
{
    if (borrow.value == 0u) return nullptr;
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        const auto* const record = m_records.get_object(slot);
        if (record->prepared.borrow.value == borrow.value) return record;
    }
    return nullptr;
}

bool CServiceBorrowClient::begin(const SServiceRoute route, const SServiceRequestId request, const std::uint16_t opcode) noexcept
{
    if ((route.value == 0u) || (request.value == 0u) || (opcode == 0u) ||
        (m_count == 512u) || (find(route, request) != nullptr)) return false;
    if (!m_records.is_ready() && !m_records.initialise()) return false;
    const auto slot = m_records.emplace();
    if (slot < 0) return false;
    auto& record = *m_records.get_object(slot);
    record.route = route;
    record.request = request;
    record.opcode = opcode;
    ++m_count;
    return true;
}

bool CServiceBorrowClient::observe(const ServiceReply& reply) noexcept
{
    auto* const record = find(reply.route, reply.request);
    if ((record == nullptr) || (record->opcode != reply.opcode) || (reply.payload_size > sizeof(reply.payload)) ||
        (reply.status > EServiceStatus::delivery_failed)) return false;
    if (reply.notice == EServiceNotice::acknowledged)
    {
        if (record->acknowledged || record->seen_terminal || (reply.status != EServiceStatus::success)) return false;
        record->acknowledged = true;
        return true;
    }
    if (reply.notice == EServiceNotice::prepared)
    {
        if (record->seen_prepared || record->seen_terminal || record->never_published ||
            (reply.status != EServiceStatus::success) || (reply.payload_size != sizeof(SPreparedServiceBorrow))) return false;
        SPreparedServiceBorrow prepared;
        std::memcpy(&prepared, reply.payload, sizeof(prepared));
        if ((prepared.borrow.value == 0u) || (prepared.view.descriptor == nullptr) ||
            (prepared.view.size == 0u) || (prepared.view.version == 0u) ||
            ((record->prepared.borrow.value != 0u) && (record->prepared.borrow.value != prepared.borrow.value))) return false;
        const auto* const existing = find(prepared.borrow);
        if ((existing != nullptr) && (existing != record)) return false;
        record->seen_prepared = true;
        record->prepared = prepared;
        if (record->closing) record->prepared.view = {};
        return true;
    }
    if ((reply.notice != EServiceNotice::completed) || record->seen_terminal) return false;
    record->seen_terminal = true;
    if (reply.status != EServiceStatus::success)
    {
        record->closing = true;
        record->prepared.view = {};
    }
    return true;
}

bool CServiceBorrowClient::observe(const ServiceBorrowControl& control) noexcept
{
    if ((control.borrow.value == 0u) || (control.control == EServiceBorrowControl::consumer_quiescent) ||
        (control.control > EServiceBorrowControl::retired) || (control.quiescence > EServiceQuiescence::execution_ended)) return false;
    auto* const record = find(control.route, control.request);
    //  A never-exposed cancellation may already have completed and been forgotten.
    if (record == nullptr) return control.quiescence == EServiceQuiescence::never_published;
    if ((record->prepared.borrow.value != 0u) && (record->prepared.borrow.value != control.borrow.value)) return false;
    const auto* const existing = find(control.borrow);
    if ((existing != nullptr) && (existing != record)) return false;
    if ((control.quiescence == EServiceQuiescence::never_published) && record->seen_prepared) return false;
    if ((control.control == EServiceBorrowControl::retired) && !record->quiescent &&
        (control.quiescence != EServiceQuiescence::never_published)) return false;
    record->prepared.borrow = control.borrow;
    record->prepared.view = {};
    record->closing = true;
    record->never_published = control.quiescence == EServiceQuiescence::never_published;
    if (control.control == EServiceBorrowControl::retired) record->retired = true;
    return true;
}

SServiceBorrow CServiceBorrowClient::borrow(const SServiceRoute route, const SServiceRequestId request) const noexcept
{
    const auto* const record = find(route, request);
    return (record == nullptr) ? SServiceBorrow{} : record->prepared.borrow;
}

SServiceView CServiceBorrowClient::view(const SServiceBorrow borrow) const noexcept
{
    const auto* const record = find(borrow);
    return ((record == nullptr) || record->closing) ? SServiceView{} : record->prepared.view;
}

bool CServiceBorrowClient::release(const SServiceBorrow borrow, ReleaseServiceBorrow& message) noexcept
{
    message = {};
    auto* const record = find(borrow);
    if ((record == nullptr) || record->retired) return false;
    record->closing = true;
    record->prepared.view = {};
    message = { record->route, borrow };
    return true;
}

bool CServiceBorrowClient::cancel(const SServiceRoute route, const SServiceRequestId request, CancelServiceRequest& message) noexcept
{
    message = {};
    auto* const record = find(route, request);
    if ((record == nullptr) || !record->acknowledged || record->seen_terminal) return false;
    record->closing = true;
    record->prepared.view = {};
    message = { route, request };
    return true;
}

bool CServiceBorrowClient::quiesce(const SServiceBorrow borrow, ServiceBorrowQuiescent& message) noexcept
{
    message = {};
    auto* const record = find(borrow);
    if ((record == nullptr) || !record->closing || record->retired ||
        (!record->seen_prepared && !record->never_published)) return false;
    record->quiescent = true;
    record->prepared.view = {};
    message = { record->route, borrow };
    return true;
}

bool CServiceBorrowClient::retired(const SServiceBorrow borrow) const noexcept
{
    const auto* const record = find(borrow);
    return (record != nullptr) && record->retired;
}

bool CServiceBorrowClient::forget(const SServiceRoute route, const SServiceRequestId request) noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        const auto& record = *m_records.get_object(slot);
        if ((record.route.value != route.value) || (record.request.value != request.value)) continue;
        if (!record.seen_terminal || ((record.prepared.borrow.value != 0u) && !record.retired)) return false;
        (void)m_records.erase(slot);
        --m_count;
        return true;
    }
    return false;
}

bool CServiceBorrowClient::abandon_unsubmitted(const SServiceRoute route, const SServiceRequestId request) noexcept
{
    for (auto slot = m_records.first_live(); slot >= 0; slot = m_records.next_live(slot))
    {
        const auto& record = *m_records.get_object(slot);
        if ((record.route.value != route.value) || (record.request.value != request.value)) continue;
        if (record.acknowledged || record.seen_terminal || (record.prepared.borrow.value != 0u)) return false;
        (void)m_records.erase(slot);
        --m_count;
        return true;
    }
    return false;
}

}   //  namespace services
