
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_router.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Correlated operations and independent provider-owned resource borrows.

#include "services/service_protocol.hpp"
#include "runtime/service_router.hpp"
#include "threading/messages/message_helpers.hpp"

namespace host
{

using services::EServiceStatus;
using services::EServiceNotice;

CServiceRouter::SRoute* CServiceRouter::find_route(const services::SServiceRoute token) noexcept
{
    if (token.value != 0u)
        for (auto& route : m_routes) if (route.token.value == token.value) return &route;
    return nullptr;
}

CServiceRouter::SOperation* CServiceRouter::find_operation(SRoute& route, const services::SServiceRequestId request) noexcept
{
    for (auto& operation : m_operations)
        if ((operation.route == &route) && (operation.request.request.value == request.value)) return &operation;
    return nullptr;
}

bool CServiceRouter::route_idle(const SRoute& route) const noexcept
{
    for (const auto& operation : m_operations) if (operation.route == &route) return false;
    return m_borrows.route_idle(route.token);
}

void CServiceRouter::open(const services::OpenServiceRequest& request, const std::int32_t slot,
    SClientRecord& client, const SServiceProvider* provider) noexcept
{
    services::OpenServiceResult result;
    result.request = request.request;
    result.status = EServiceStatus::unavailable;
    if ((slot < 0) || (request.request.value == 0u) || (request.contract == 0u) || (request.version == 0u))
        result.status = EServiceStatus::invalid_request;
    else if ((provider != nullptr) && provider->available() && (provider->client != &client) &&
        (provider->client->module_id() == request.module) && !client.terminal())
    {
        result.status = EServiceStatus::unsupported;
        for (std::uint32_t index = 0u; index < provider->contract_count; ++index)
        {
            const auto& contract = provider->contracts[index];
            if ((contract.number != request.contract) || (contract.version != request.version)) continue;
            result.status = EServiceStatus::busy;
            if (m_next_route != 0u)
            {
                for (auto& route : m_routes)
                {
                    if (route.client != nullptr) continue;
                    route.token.value = m_next_route++;
                    route.client = &client;
                    route.provider = provider->client;
                    route.contract = contract;
                    result.route = route.token;
                    result.provider = provider->client->instance;
                    result.status = EServiceStatus::success;
                    break;
                }
            }
            break;
        }
    }
    if (!client.reply(threading::make_pod_message(slot, result)))
    {
        if (auto* const route = find_route(result.route)) *route = {};
        m_failed = true;
    }
}

void CServiceRouter::close(const services::CloseServiceRequest& request, const std::int32_t slot, SClientRecord& client) noexcept
{
    services::ServiceReply result;
    result.route = request.route;
    result.request = request.request;
    auto* const route = find_route(request.route);
    result.status = EServiceStatus::stale;
    if ((slot < 0) || (request.request.value == 0u)) result.status = EServiceStatus::invalid_request;
    else if ((route != nullptr) && (route->client == &client))
    {
        result.status = route_idle(*route) ? EServiceStatus::success : EServiceStatus::busy;
        if (result.status == EServiceStatus::success) *route = {};
    }
    if (!client.reply(threading::make_pod_message(slot, result))) m_failed = true;
}

void CServiceRouter::request(const services::ServiceRequest& request, const std::int32_t slot, SClientRecord& client) noexcept
{
    services::ServiceReply result;
    result.route = request.route;
    result.request = request.request;
    result.opcode = request.opcode;
    result.notice = EServiceNotice::acknowledged;
    result.status = EServiceStatus::success;
    if (!client.reply(threading::make_pod_message(slot, result)))
    {
        client.cancel_reservation(1u);
        m_failed = true;
        return;
    }
    result.notice = EServiceNotice::completed;
    result.status = EServiceStatus::invalid_request;
    auto* const route = find_route(request.route);
    bool padding_valid = request.payload_size <= sizeof(request.payload);
    if (padding_valid)
        for (std::uint32_t index = request.payload_size; index < sizeof(request.payload); ++index)
            padding_valid = padding_valid && (request.payload[index] == 0u);
    if ((slot >= 0) && (request.request.value != 0u) && (request.opcode != 0u) &&
        (request.flags == 0u) && (request.reserved == 0u) && padding_valid)
    {
        result.status = EServiceStatus::stale;
        if ((route != nullptr) && (route->client == &client) && (request.request.value > route->last_request))
        {
            result.status = EServiceStatus::closing;
            if (!route->closing && (route->provider->phase == EClientPhase::ready) && !client.terminal())
            {
                result.status = EServiceStatus::busy;
                if (m_next_order != 0u)
                {
                    for (auto& operation : m_operations)
                    {
                        if (operation.route != nullptr) continue;
                        operation.route = route;
                        operation.request = request;
                        operation.slot = slot;
                        operation.order = m_next_order++;
                        route->last_request = request.request.value;
                        return;
                    }
                }
            }
        }
    }
    if (!client.reply(threading::make_pod_message(slot, result))) m_failed = true;
}

services::EServiceStatus CServiceRouter::complete(SClientRecord& provider, const services::ServiceReply& reply) noexcept
{
    auto* const route = find_route(reply.route);
    if ((route == nullptr) || (route->provider != &provider)) return EServiceStatus::stale;
    auto* const operation = find_operation(*route, reply.request);
    if ((operation == nullptr) || operation->completed || (!operation->offered && !operation->delivered)) return EServiceStatus::stale;
    if ((reply.notice != EServiceNotice::completed) || (reply.opcode != operation->request.opcode) ||
        (reply.status > EServiceStatus::delivery_failed) || (reply.payload_size > sizeof(reply.payload))) return EServiceStatus::invalid_request;
    for (std::uint32_t index = reply.payload_size; index < sizeof(reply.payload); ++index)
        if (reply.payload[index] != 0u) return EServiceStatus::invalid_request;
    operation->reply = reply;
    operation->completed = true;
    if (reply.status != EServiceStatus::success) m_borrows.cancel(operation->borrow);
    return EServiceStatus::success;
}

EServiceStatus CServiceRouter::prepare_borrow(SClientRecord& provider,
    const services::SServiceBorrowOffer& offer, services::SServiceBorrow& borrow) noexcept
{
    borrow = {};
    auto* const route = find_route(offer.route);
    if ((route == nullptr) || (route->provider != &provider)) return EServiceStatus::stale;
    auto* const operation = find_operation(*route, offer.request);
    if ((operation == nullptr) || (!operation->offered && !operation->delivered)) return EServiceStatus::stale;
    if (operation->cancelled || route->closing || route->client->terminal()) return EServiceStatus::closing;
    if (operation->completed || (operation->borrow.value != 0u)) return EServiceStatus::invalid_request;
    const auto status = m_borrows.prepare(provider, *route->client, offer, operation->slot,
        operation->request.opcode, operation->delivered, borrow);
    if (status == EServiceStatus::success) operation->borrow = borrow;
    return status;
}

EServiceStatus CServiceRouter::begin_borrow_retirement(SClientRecord& provider, const services::SServiceBorrow borrow) noexcept
{
    return m_borrows.begin_retirement(provider, borrow);
}

EServiceStatus CServiceRouter::finish_borrow_retirement(SClientRecord& provider, const services::SServiceBorrow borrow) noexcept
{
    return m_borrows.finish_retirement(provider, borrow);
}

void CServiceRouter::release_borrow(SClientRecord& client, const services::ReleaseServiceBorrow& request) noexcept
{
    m_borrows.release(client, request);
}

void CServiceRouter::register_notification(SClientRecord& client,
    const services::RegisterServiceNotification& request, const std::int32_t slot) noexcept
{
    const auto result = m_borrows.register_notification(client, request);
    if (!client.reply(threading::make_pod_message(slot, result))) m_failed = true;
}

EServiceStatus CServiceRouter::bind_borrow_notification(SClientRecord& provider, const services::SServiceBorrow borrow,
    const services::SNotificationRegistration registration, threading::SNotificationEndpoint& endpoint) noexcept
{
    return m_borrows.bind_notification(provider, borrow, registration, endpoint);
}

void CServiceRouter::quiesce_borrow(SClientRecord& client, const services::ServiceBorrowQuiescent& request) noexcept
{
    m_borrows.quiescent(client, request);
}

void CServiceRouter::cancel_request(SClientRecord& client, const services::CancelServiceRequest& request) noexcept
{
    auto* const route = find_route(request.route);
    if ((route == nullptr) || (route->client != &client)) return;
    //  A successful result may already be queued, and its operation gone, while
    //  the consumer is still cancelling. Preserve that terminal result but retire
    //  its independent borrow so the consumer's closed state cannot strand it.
    m_borrows.cancel_request(client, request);
    if (auto* const operation = find_operation(*route, request.request)) cancel(*operation);
}

bool CServiceRouter::provider_borrows_retired(const SClientRecord& provider) const noexcept
{
    return m_borrows.provider_retired(provider);
}

std::uint32_t CServiceRouter::prepare_batch(SClientRecord& provider,
    services::SDispatchedCommand* commands, const std::uint32_t capacity) noexcept
{
    std::uint32_t count = 0u;
    std::uint64_t after = 0u;
    while (count < capacity)
    {
        SOperation* next = nullptr;
        for (auto& operation : m_operations)
            if ((operation.route != nullptr) && (operation.route->provider == &provider) &&
                !operation.delivered && !operation.completed && !operation.cancelled && (operation.order > after) &&
                ((next == nullptr) || (operation.order < next->order))) next = &operation;
        if (next == nullptr) break;
        next->offered = true;
        commands[count++] = { next->request, next->route->client->instance, next->route->client->identity, next->route->contract };
        after = next->order;
    }
    return count;
}

bool CServiceRouter::finish_batch(SClientRecord& provider, const services::SDispatchedCommand* commands,
    const std::uint32_t offered, const std::uint32_t consumed) noexcept
{
    if (consumed > offered) return false;
    bool valid = true;
    for (std::uint32_t index = 0u; index < offered; ++index)
    {
        auto* const route = find_route(commands[index].command.route);
        auto* const operation = (route != nullptr) ? find_operation(*route, commands[index].command.request) : nullptr;
        if ((operation == nullptr) || (route->provider != &provider)) return false;
        operation->offered = false;
        if (index < consumed)
        {
            operation->delivered = true;
            m_borrows.dispatched(operation->borrow);
        }
        else if (operation->completed || (operation->borrow.value != 0u)) valid = false;
    }
    return valid;
}

void CServiceRouter::fail(SOperation& operation, const EServiceStatus status) noexcept
{
    operation.reply = {};
    operation.reply.route = operation.request.route;
    operation.reply.request = operation.request.request;
    operation.reply.opcode = operation.request.opcode;
    operation.reply.status = status;
    operation.completed = true;
    operation.delivered = true;
}

void CServiceRouter::progress() noexcept
{
    m_borrows.progress();
    for (auto& operation : m_operations)
    {
        if (operation.route == nullptr) continue;
        if (operation.cancelled && operation.delivered && !operation.completed && !operation.cancellation_notified)
        {
            const services::ServiceRequestCancelled message{ operation.request.route, operation.request.request };
            operation.cancellation_notified = operation.route->provider->post(threading::make_pod_message(-1, message));
        }
        if (!operation.delivered || (!operation.completed && !operation.cancelled) ||
            !m_borrows.publication_settled(operation.borrow)) continue;
        if (!operation.terminal_sent)
        {
            auto reply = operation.reply;
            if (operation.cancelled)
            {
                reply = {};
                reply.route = operation.request.route;
                reply.request = operation.request.request;
                reply.opcode = operation.request.opcode;
                reply.status = EServiceStatus::cancelled;
            }
            operation.terminal_sent = operation.route->client->retry_reply(threading::make_pod_message(operation.slot, reply));
        }
        //  A cancelled wait can finish before its accepted provider work drains.
        if (operation.terminal_sent && operation.completed) operation = {};
    }
    for (auto& route : m_routes)
        if ((route.client != nullptr) && route.closing && route_idle(route)) route = {};
}

void CServiceRouter::cancel(SOperation& operation) noexcept
{
    if (operation.completed && operation.delivered) return;
    operation.cancelled = true;
    m_borrows.cancel(operation.borrow);
    if (!operation.delivered && !operation.offered) fail(operation, EServiceStatus::cancelled);
}

void CServiceRouter::stop_provider(SClientRecord& provider, const bool quiescent) noexcept
{
    m_borrows.stop_provider(provider);
    if (quiescent && !m_borrows.provider_retired(provider)) m_failed = true;
    for (auto& route : m_routes)
        if (route.provider == &provider) route.closing = true;
    for (auto& operation : m_operations)
        if ((operation.route != nullptr) && (operation.route->provider == &provider) &&
            !(operation.completed && operation.delivered) &&
            (quiescent || (!operation.delivered && !operation.offered))) fail(operation, EServiceStatus::cancelled);
}

void CServiceRouter::retire_client(SClientRecord& client) noexcept
{
    m_borrows.retire_client(client, client.execution_quiescent());
    for (auto& route : m_routes)
        if (route.client == &client) route.closing = true;
    for (auto& operation : m_operations)
        if ((operation.route != nullptr) && (operation.route->client == &client) &&
            !(operation.completed && operation.delivered)) cancel(operation);
}

bool CServiceRouter::client_idle(const SClientRecord& client) const noexcept
{
    for (const auto& route : m_routes)
        if ((route.client == &client) || (route.provider == &client)) return false;
    return true;
}

bool CServiceRouter::provider_in_use(const SClientRecord& provider) const noexcept
{
    for (const auto& route : m_routes) if (route.provider == &provider) return true;
    return false;
}

}   //  namespace host
