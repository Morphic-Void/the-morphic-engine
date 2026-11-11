
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_borrow_client.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Consumer protocol state; never owns or destroys provider storage.

#pragma once

#include "foundation/containers/TUnorderedCollection.hpp"
#include "framework/services/service_borrow.hpp"
#include "framework/services/service_notifications.hpp"

namespace services
{

//  One designated consumer thread observes replies in transport order. begin()
//  precedes request submission. Closing prevents further view acquisition but
//  the caller must stop ALL existing readers/dependent objects before quiesce().
//  Retain this state and consumer-side notification storage until retired.
//  No destructor posts asynchronous cleanup or implies resource quiescence.
class CServiceBorrowClient
{
public:
    CServiceBorrowClient() noexcept = default;
    CServiceBorrowClient(const CServiceBorrowClient&) = delete;
    CServiceBorrowClient& operator=(const CServiceBorrowClient&) = delete;
    CServiceBorrowClient(CServiceBorrowClient&&) = delete;
    CServiceBorrowClient& operator=(CServiceBorrowClient&&) = delete;
    [[nodiscard]] bool begin(const SServiceRoute route, const SServiceRequestId request, const std::uint16_t opcode) noexcept;
    //  Caller must prove transport submission never succeeded.
    [[nodiscard]] bool abandon_unsubmitted(const SServiceRoute route, const SServiceRequestId request) noexcept;
    [[nodiscard]] bool observe(const ServiceReply& reply) noexcept;
    [[nodiscard]] bool observe(const ServiceBorrowControl& control) noexcept;
    [[nodiscard]] bool request_notification(const SServiceBorrow borrow, RegisterServiceNotification& message) noexcept;
    //  Caller must prove the registration request never entered the transport.
    [[nodiscard]] bool abandon_unsubmitted_notification(const SServiceBorrow borrow) noexcept;
    [[nodiscard]] bool observe(const ServiceNotificationRegistered& reply) noexcept;
    [[nodiscard]] SNotificationRegistration registration(const SServiceBorrow borrow) const noexcept;
    [[nodiscard]] threading::SNotificationReader notification(const SServiceBorrow borrow) const noexcept;
    [[nodiscard]] SServiceBorrow borrow(const SServiceRoute route, const SServiceRequestId request) const noexcept;
    [[nodiscard]] SServiceView view(const SServiceBorrow borrow) const noexcept;
    [[nodiscard]] bool release(const SServiceBorrow borrow, ReleaseServiceBorrow& message) noexcept;
    //  Wait for receipt acknowledgment so the cleanup lane cannot overtake admission.
    [[nodiscard]] bool cancel(const SServiceRoute route, const SServiceRequestId request, CancelServiceRequest& message) noexcept;
    [[nodiscard]] bool quiesce(const SServiceBorrow borrow, ServiceBorrowQuiescent& message) noexcept;
    [[nodiscard]] bool retired(const SServiceBorrow borrow) const noexcept;
    [[nodiscard]] bool forget(const SServiceRoute route, const SServiceRequestId request) noexcept;

private:
    struct SRecord
    {
        SServiceRoute route;
        SServiceRequestId request;
        SPreparedServiceBorrow prepared;
        std::uint16_t opcode{ 0u };
        bool acknowledged{ false };
        bool seen_prepared{ false };
        bool seen_terminal{ false };
        bool never_published{ false };
        bool closing{ false };
        bool quiescent{ false };
        bool retired{ false };
        bool notification_waiting{ false };
        SNotificationRegistration registration;
        threading::SNotificationReader notification;
    };

    [[nodiscard]] SRecord* find(const SServiceRoute route, const SServiceRequestId request) noexcept;
    [[nodiscard]] const SRecord* find(const SServiceRoute route, const SServiceRequestId request) const noexcept;
    [[nodiscard]] SRecord* find(const SServiceBorrow borrow) noexcept;
    [[nodiscard]] const SRecord* find(const SServiceBorrow borrow) const noexcept;

    TUnorderedCollection<SRecord> m_records;
    std::uint32_t m_count{ 0u };
};

}   //  namespace services
