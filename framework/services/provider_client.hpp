
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    provider_client.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Incremental provider operations. Callers retain inputs until a terminal result.

#pragma once

#include "framework/services/provider_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "framework/module/module_execution.hpp"

namespace services
{

[[nodiscard]] inline EServiceStatus submission_status(const modules::ESubmission result) noexcept
{
    switch (result)
    {
        case modules::ESubmission::accepted:
        case modules::ESubmission::published_wake_failed: return EServiceStatus::success;
        case modules::ESubmission::busy: return EServiceStatus::busy;
        case modules::ESubmission::closed: return EServiceStatus::closing;
        case modules::ESubmission::invalid: return EServiceStatus::invalid_request;
        case modules::ESubmission::no_pipe:
        case modules::ESubmission::transport_failed: return EServiceStatus::delivery_failed;
    }
    return EServiceStatus::delivery_failed;
}

class CProviderClient
{
public:
    explicit CProviderClient(const modules::CCommunication& communication) noexcept : m_communication{ communication } {}

    [[nodiscard]] EServiceStatus post_pod(const threading::CErasedPodMsg* const message) noexcept
    {
        return submission_status(submit(*message));
    }
    [[nodiscard]] EServiceStatus post_owner(threading::CErasedOwnerMsg* const message) noexcept
    {
        return submission_status(m_communication.submit({}, std::move(*message)));
    }

    [[nodiscard]] bool receive(const threading::CErasedPodMsg& message) noexcept
    {
        ProviderOperationResult result;
        if (!message.copy_payload_to(result)) return false;
        for (SPending& pending : m_pending)
        {
            if (pending.live && (pending.slot == message.query_async_slot()) && !pending.ready)
            {
                pending.result = result;
                pending.ready = true;
                return true;
            }
        }
        fail();
        return true;
    }

    [[nodiscard]] bool idle() const noexcept { return m_pending_count == 0u; }

    [[nodiscard]] bool preparing(const SServiceBorrowOffer& offer) const noexcept
    {
        return find(k_type_id_v<PrepareServiceBorrow>, offer.route.value, offer.request.value) != nullptr;
    }

    [[nodiscard]] bool committing(const ServiceReply& reply) const noexcept
    {
        return find(k_type_id_v<CommitServiceResource>, reply.route.value, reply.request.value) != nullptr;
    }

    [[nodiscard]] bool retire_bindings(const SServiceBorrow borrow) noexcept
    {
        bool settled = true;
        for (SPending& pending : m_pending)
        {
            if (pending.live && (pending.type == k_type_id_v<BindServiceNotification>) && (pending.first == borrow.value))
            {
                if (pending.ready)
                {
                    pending = {};
                    --m_pending_count;
                }
                else settled = false;
            }
        }
        return settled;
    }

    [[nodiscard]] EServiceStatus complete(const ServiceReply* const reply) noexcept
    {
        ProviderOperationResult result;
        return exchange(CompleteServiceRequest{ *reply }, reply->route.value, reply->request.value, result);
    }
    [[nodiscard]] EServiceStatus commit_resource(const ServiceReply* const reply) noexcept
    {
        ProviderOperationResult result;
        return exchange(CommitServiceResource{ *reply }, reply->route.value, reply->request.value, result);
    }
    [[nodiscard]] EServiceStatus prepare_borrow(const SServiceBorrowOffer* const offer, SServiceBorrow* const borrow) noexcept
    {
        ProviderOperationResult result;
        const EServiceStatus status = exchange(PrepareServiceBorrow{ *offer }, offer->route.value, offer->request.value, result);
        if (status == EServiceStatus::success) *borrow = result.borrow;
        return status;
    }
    [[nodiscard]] EServiceStatus begin_borrow_retirement(const SServiceBorrow borrow) noexcept
    {
        ProviderOperationResult result;
        return exchange(BeginServiceBorrowRetirement{ borrow }, borrow.value, 0u, result);
    }
    [[nodiscard]] EServiceStatus finish_borrow_retirement(const SServiceBorrow borrow) noexcept
    {
        ProviderOperationResult result;
        return exchange(FinishServiceBorrowRetirement{ borrow }, borrow.value, 0u, result);
    }
    [[nodiscard]] EServiceStatus bind_borrow_notification(const SServiceBorrow borrow,
        const SNotificationRegistration registration, threading::SNotificationEndpoint* const notification) noexcept
    {
        ProviderOperationResult result;
        const EServiceStatus status = exchange(BindServiceNotification{ borrow, registration }, borrow.value, registration.value, result);
        if (status == EServiceStatus::success) *notification = result.notification;
        return status;
    }

private:
    void fail() noexcept
    {
        modules::SExecutionIntent intent;
        intent.kind = modules::SExecutionIntent::EKind::fail;
        (void)m_communication.submit(intent);
    }
    [[nodiscard]] modules::ESubmission submit(const threading::CErasedPodMsg& message) noexcept
    {
        return m_communication.submit({}, message);
    }
    struct SPending
    {
        type_id type;
        std::uint64_t first{ 0u }, second{ 0u };
        std::int32_t slot{ -1 };
        ProviderOperationResult result;
        bool live{ false }, ready{ false };
    };
    [[nodiscard]] const SPending* find(const type_id type, const std::uint64_t first, const std::uint64_t second) const noexcept
    {
        if (!m_pending_count) return nullptr;
        for (const SPending& pending : m_pending)
        {
            if (pending.live && (pending.type == type) && (pending.first == first) && (pending.second == second))
            {
                return &pending;
            }
        }
        return nullptr;
    }
    template<typename T>
    [[nodiscard]] EServiceStatus exchange(const T& request, const std::uint64_t first,
        const std::uint64_t second, ProviderOperationResult& result) noexcept
    {
        SPending* vacant = nullptr;
        for (SPending& pending : m_pending)
        {
            if (!pending.live) vacant = &pending;
            else if ((pending.type == k_type_id_v<T>) && (pending.first == first) && (pending.second == second))
            {
                if (!pending.ready)
                {
                    return EServiceStatus::busy;
                }
                result = pending.result;
                pending = {};
                --m_pending_count;
                return result.status;
            }
        }
        if (!vacant) return EServiceStatus::busy;
        if (m_next_slot == INT32_MAX)
        {
            fail();
            return EServiceStatus::delivery_failed;
        }
        const modules::ESubmission posted = submit(threading::make_pod_message(m_next_slot, request));
        if (!modules::accepted(posted))
        {
            if (posted == modules::ESubmission::busy)
            {
                return EServiceStatus::busy;
            }
            return (posted == modules::ESubmission::closed) ? EServiceStatus::closing : EServiceStatus::delivery_failed;
        }
        //  Accepted publication may fail to notify the peer. Keep its inputs
        //  live until a result settles it or fatal shutdown terminates the process.
        vacant->type = k_type_id_v<T>;
        vacant->first = first;
        vacant->second = second;
        vacant->slot = m_next_slot++;
        vacant->live = true;
        ++m_pending_count;
        return EServiceStatus::busy;
    }

    const modules::CCommunication m_communication;
    SPending m_pending[256];
    std::uint32_t m_pending_count{ 0u };
    std::int32_t m_next_slot{ 0x40000000 };
};

}   //  namespace services
