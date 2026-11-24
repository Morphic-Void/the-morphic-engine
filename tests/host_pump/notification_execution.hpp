
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    notification_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Notification consumer with retained registration and publisher obligations.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/services/service_borrow_client.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "tests/host_pump/fixture_types.hpp"
#include "debug/macros.hpp"
#include <cstring>

namespace pump_tests
{

class CNotificationExecution
{
public:
    enum class EMode : unsigned { normal, cancel, abandon, shutdown };
    explicit CNotificationExecution(const modules::CCommunication& communication) noexcept : m_communication{ communication } {}
    bool start(const services::SServiceRoute route, const EMode mode) noexcept
    {
        if (m_active || !m_consumer.begin(route, { 1u }, 6u))
        {
            return false;
        }
        m_route = route;
        m_mode = mode;
        m_active = true;
        services::ServiceRequest request;
        request.route = route;
        request.request = { 1u };
        request.opcode = 6u;
        prepare(EPost::request, 70, request);
        if (m_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none) unsafe();
        return !m_failed;
    }
    void stop() noexcept { m_stopping = true; close_local(); }
    bool failed() const noexcept { return m_failed; }
    bool done() const noexcept { return m_done; }
    // Deliberate departure closes local access only. Host/native retirement
    // still protects the provider's publisher, backing storage and registration.
    bool domain_complete() const noexcept { return !m_active || (!m_unsafe && (m_done || m_abandoned)); }
    bool self_stop_due() const noexcept
    {
        return m_abandoned || (m_observed && m_terminal && m_publisher_done &&
            (m_mode == EMode::shutdown) && !m_stopping);
    }
    bool owns(const threading::CErasedPodMsg& message) const noexcept
    {
        const std::int32_t slot = message.query_async_slot();
        return m_active && ((slot == 70) || (slot == 72) || (slot == 73) ||
            message.is_payload_a<services::ServiceBorrowControl>());
    }
    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        if (m_unsafe || m_abandoned) return;
        services::ServiceReply reply;
        services::ServiceNotificationRegistered registration;
        services::ServiceBorrowControl control;
        if (message.copy_payload_to(reply))
        {
            if (message.query_async_slot() == 73)
            {
                receive_publisher(reply);
                return;
            }
            if (!m_submitted || (message.query_async_slot() != 70) || !m_consumer.observe(reply))
            {
                unsafe();
                return;
            }
            if (reply.notice == services::EServiceNotice::prepared)
            {
                m_borrow = m_consumer.borrow(m_route, { 1u });
                const services::SServiceView view = m_consumer.view(m_borrow);
                if (!m_stopping && !m_releasing && view.descriptor)
                {
                    if ((view.size != sizeof(SNotificationData)) || (view.version != 1u)) fail();
                    else m_data = static_cast<const SNotificationData*>(view.descriptor);
                }
            }
            if (reply.notice == services::EServiceNotice::completed)
            {
                m_terminal = true;
                const services::EServiceStatus expected = (m_mode == EMode::cancel) ?
                    services::EServiceStatus::cancelled : services::EServiceStatus::success;
                if ((reply.status != expected) && !(m_stopping && (reply.status == services::EServiceStatus::cancelled))) fail();
                if (reply.status != services::EServiceStatus::success)
                {
                    m_releasing = true;
                    close_local();
                }
            }
        }
        else if (message.copy_payload_to(registration))
        {
            if ((message.query_async_slot() != 72) || !m_registration_sent || m_registration_done ||
                !m_consumer.observe(registration))
            {
                unsafe();
                return;
            }
            m_registration_done = true;
            if (registration.status != services::EServiceStatus::success)
            {
                if (!m_stopping && !m_releasing) fail();
                return;
            }
            // observe() permits an already-expired reader after closure. Never
            // dereference that handle, including to clear its pending state.
            if (!m_stopping && !m_releasing)
            {
                m_registration = registration.registration;
                m_reader = m_consumer.notification(m_borrow);
                if (!m_reader.is_valid()) fail();
            }
        }
        else if (message.copy_payload_to(control))
        {
            if ((control.route.value != m_route.value) || (control.request.value != 1u) ||
                ((control.control == services::EServiceBorrowControl::retired) && !m_quiescent_sent &&
                    (control.quiescence != services::EServiceQuiescence::never_published)) || !m_consumer.observe(control))
            {
                unsafe();
                return;
            }
            m_borrow = control.borrow;
            m_releasing = true;
            close_local();
        }
        else unsafe();
    }
    void advance(bool& runnable, std::uint64_t& next_wake) noexcept
    {
        if (!m_active || m_done || m_abandoned || m_unsafe)
        {
            return;
        }
        if (m_stopping && !m_submitted)
        {
            m_done = m_consumer.abandon_unsubmitted(m_route, { 1u });
            if (!m_done) unsafe();
            m_post = EPost::none;
            return;
        }
        std::uint64_t remaining = 0u;
        if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            unsafe();
            return;
        }
        if (remaining < next_wake) next_wake = remaining;
        if ((m_stopping || m_releasing) && (m_post == EPost::registration))
        {
            if (!m_consumer.abandon_unsubmitted_notification(m_borrow)) unsafe();
            m_post = EPost::none;
        }
        if ((m_stopping || m_releasing) && (m_post == EPost::publisher)) m_post = EPost::none;
        if (m_unsafe) return;
        for (unsigned turn = 0u; turn < 2u; ++turn)
        {
            if (m_post != EPost::none)
            {
                const modules::ESubmission result = m_communication.submit({}, m_pending);
                if (result == modules::ESubmission::busy) return;
                if (!modules::accepted(result))
                {
                    if (m_post == EPost::request)
                    {
                        m_done = m_consumer.abandon_unsubmitted(m_route, { 1u });
                        if (!m_done) unsafe();
                        fail();
                    }
                    else if (m_post == EPost::registration)
                    {
                        if (!m_consumer.abandon_unsubmitted_notification(m_borrow)) unsafe();
                        fail();
                    }
                    else if (m_post == EPost::publisher) fail();
                    else unsafe();
                    m_post = EPost::none;
                    runnable = !m_unsafe && !m_done;
                    return;
                }
                if (m_post == EPost::request) m_submitted = true;
                if (m_post == EPost::registration) m_registration_sent = true;
                if (m_post == EPost::publisher) m_publisher_sent = true;
                if (m_post == EPost::quiesce) m_quiescent_sent = true;
                m_post = EPost::none;
            }
            if (m_terminal && (!m_registration_sent || m_registration_done) &&
                (!m_publisher_sent || m_publisher_done) && (!m_borrow.value || m_consumer.retired(m_borrow)))
            {
                if (!m_consumer.forget(m_route, { 1u })) unsafe();
                else m_done = true;
                if (m_done && m_borrow.value) MV_LOG(info, "Pump fixture: notification retired after final publisher signal");
                return;
            }
            if (!m_releasing && !m_stopping && m_reader.is_valid() && m_publisher_done && !m_observed)
            {
                if (m_reader.failed() || !m_data) fail();
                else
                {
                    bool available = m_data->value.load(std::memory_order_acquire) != 0u;
                    if (!available)
                    {
                        // Clear/recheck durable data before common preamble
                        // parks using its sampled notification epoch.
                        m_reader.clear_pending();
                        available = m_data->value.load(std::memory_order_acquire) != 0u;
                    }
                    if (available)
                    {
                        m_observed = true;
                        // Cancellation deliberately tests a coalesced final
                        // signal; other modes require a fresh final signal.
                        if (m_mode != EMode::cancel) m_reader.clear_pending();
                        MV_LOG(info, "Pump fixture: registered notification published durable data before wake");
                    }
                }
            }
            if (!m_stopping && !m_releasing && m_observed && m_terminal && (m_mode == EMode::abandon))
            {
                close_local();
                m_abandoned = true;
                MV_LOG(info, "Pump fixture: notification consumer relinquished local access before departure");
                return;
            }
            if (!m_stopping && !m_releasing && m_data && !m_registration_prepared)
            {
                services::RegisterServiceNotification registration;
                if (!m_consumer.request_notification(m_borrow, registration))
                {
                    unsafe();
                    return;
                }
                m_registration_prepared = true;
                prepare(EPost::registration, 72, registration);
            }
            else if (!m_stopping && !m_releasing && m_reader.is_valid() && !m_publisher_sent)
            {
                services::ServiceRequest request;
                request.route = m_route;
                request.request = { 2u };
                request.opcode = 7u;
                const SNotificationReady payload{ m_borrow, m_consumer.registration(m_borrow) };
                request.payload_size = sizeof(payload);
                std::memcpy(request.payload, &payload, sizeof(payload));
                prepare(EPost::publisher, 73, request);
            }
            else if (!m_releasing && m_observed && (m_mode == EMode::cancel) && !m_terminal && !m_stopping)
            {
                services::CancelServiceRequest cancel;
                if (!m_consumer.cancel(m_route, { 1u }, cancel))
                {
                    unsafe();
                    return;
                }
                prepare(EPost::cancel, -1, cancel);
                m_releasing = true;
                close_local();
            }
            else if (!m_releasing && m_borrow.value && (m_stopping ||
                (m_observed && m_terminal && (m_mode == EMode::normal))))
            {
                services::ReleaseServiceBorrow release;
                if (!m_consumer.release(m_borrow, release))
                {
                    unsafe();
                    return;
                }
                prepare(EPost::release, -1, release);
                m_releasing = true;
                close_local();
            }
            else if (m_releasing && !m_quiescent_prepared)
            {
                services::ServiceBorrowQuiescent quiescent;
                if (!m_consumer.quiesce(m_borrow, quiescent)) return;
                prepare(EPost::quiesce, -1, quiescent);
                m_quiescent_prepared = true;
            }
            else return;
        }
        runnable = m_post != EPost::none;
    }

private:
    enum class EPost : unsigned { none, request, registration, publisher, cancel, release, quiesce };
    const modules::CCommunication& m_communication;
    services::CServiceBorrowClient m_consumer;
    services::SServiceRoute m_route;
    services::SServiceBorrow m_borrow;
    services::SNotificationRegistration m_registration;
    threading::SNotificationReader m_reader;
    const SNotificationData* m_data{ nullptr };
    threading::CErasedPodMsg m_pending;
    platform::threading::CWaitDeadline m_deadline;
    EMode m_mode{ EMode::normal };
    EPost m_post{ EPost::none };
    bool m_active{}, m_submitted{}, m_terminal{}, m_stopping{}, m_releasing{};
    bool m_registration_prepared{}, m_registration_sent{}, m_registration_done{};
    bool m_publisher_sent{}, m_publisher_acknowledged{}, m_publisher_done{}, m_observed{};
    bool m_quiescent_prepared{}, m_quiescent_sent{}, m_done{}, m_abandoned{}, m_failed{}, m_unsafe{};

    template<typename T> void prepare(const EPost post, const std::int32_t slot, const T& value) noexcept
    {
        m_pending = threading::make_pod_message(slot, value);
        m_post = post;
    }
    void close_local() noexcept { m_reader = {}; m_data = nullptr; }
    void fail() noexcept { m_failed = m_stopping = true; close_local(); }
    void unsafe() noexcept { fail(); m_unsafe = true; }
    void receive_publisher(const services::ServiceReply& reply) noexcept
    {
        if (!m_publisher_sent || m_publisher_done || (reply.route.value != m_route.value) ||
            (reply.request.value != 2u) || (reply.opcode != 7u) || (reply.payload_size > sizeof(reply.payload)) ||
            (reply.status > services::EServiceStatus::delivery_failed))
        {
            unsafe();
            return;
        }
        if (reply.notice == services::EServiceNotice::acknowledged)
        {
            if (m_publisher_acknowledged || (reply.status != services::EServiceStatus::success)) unsafe();
            else m_publisher_acknowledged = true;
            return;
        }
        if ((reply.notice != services::EServiceNotice::completed) ||
            ((reply.status == services::EServiceStatus::success) && !m_publisher_acknowledged))
        {
            unsafe();
            return;
        }
        if (reply.status == services::EServiceStatus::success)
        {
            SNotificationReady echoed;
            if (reply.payload_size != sizeof(echoed))
            {
                unsafe();
                return;
            }
            std::memcpy(&echoed, reply.payload, sizeof(echoed));
            if ((echoed.borrow.value != m_borrow.value) || (echoed.registration.value != m_registration.value))
            {
                unsafe();
                return;
            }
        }
        m_publisher_done = true;
        if ((reply.status != services::EServiceStatus::success) &&
            !(m_stopping && (reply.status == services::EServiceStatus::cancelled))) fail();
    }
};

}   // namespace pump_tests
