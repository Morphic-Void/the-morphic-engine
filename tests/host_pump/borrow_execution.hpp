
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    borrow_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Borrow fixture protocol with retained cleanup submissions and local access closure.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/services/service_borrow_client.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "debug/macros.hpp"

namespace pump_tests
{

class CBorrowExecution
{
public:
    enum class EMode : unsigned { normal, cancel, abandon, shutdown, provider_failure };
    explicit CBorrowExecution(const modules::CCommunication& communication) noexcept : m_communication{ communication } {}
    bool start(const services::SServiceRoute route, const EMode mode) noexcept
    {
        if (m_active || !m_consumer.begin(route, { 1u }, 5u))
        {
            return false;
        }
        m_route = route;
        m_mode = mode;
        m_active = true;
        services::ServiceRequest request;
        request.route = route;
        request.request = { 1u };
        request.opcode = 5u;
        m_pending = threading::make_pod_message(70, request);
        m_post = EPost::request;
        if (m_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none) unsafe();
        return !m_failed;
    }
    void stop() noexcept { m_stopping = true; }
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] bool done() const noexcept { return m_done; }
    // Local domain access only. Abandonment still needs Host/native retirement
    // before provider-owned bytes can be reclaimed.
    [[nodiscard]] bool domain_complete() const noexcept { return !m_active || (!m_unsafe && (m_done || m_abandoned)); }
    [[nodiscard]] bool self_stop_due() const noexcept
    {
        return m_terminal && ((m_mode == EMode::abandon) || ((m_mode == EMode::shutdown) && !m_stopping));
    }
    [[nodiscard]] bool owns(const threading::CErasedPodMsg& message) const noexcept
    {
        return m_active && ((message.query_async_slot() == 70) || message.is_payload_a<services::ServiceBorrowControl>());
    }
    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        // Abandonment relinquished every local reader before domain completion;
        // Host owns unread outcome cancellation and native retirement proof.
        if (m_unsafe || m_abandoned)
        {
            return;
        }
        services::ServiceReply reply;
        services::ServiceBorrowControl control;
        if (message.copy_payload_to(reply))
        {
            if (!m_submitted || (message.query_async_slot() != 70) || !m_consumer.observe(reply))
            {
                unsafe();
                return;
            }
            if (reply.notice == services::EServiceNotice::prepared)
            {
                m_prepared = true;
                m_borrow = m_consumer.borrow(m_route, { 1u });
                const services::SServiceView view = m_consumer.view(m_borrow);
                if (view.descriptor && ((view.size != sizeof(unsigned)) || (view.version != 1u) ||
                    (*static_cast<const unsigned*>(view.descriptor) != 4242u)))
                {
                    m_failed = true;
                    m_stopping = true;
                }
                MV_LOG(info, "Pump fixture: borrowed descriptor observed before terminal result");
            }
            if (reply.notice == services::EServiceNotice::completed)
            {
                m_terminal = true;
                const services::EServiceStatus expected = (m_mode == EMode::cancel) ?
                    services::EServiceStatus::cancelled : services::EServiceStatus::success;
                if (reply.status != expected)
                {
                    m_failed = true;
                    m_stopping = true;
                }
                if ((m_mode == EMode::abandon) && !m_failed)
                {
                    // No descriptor, dependent object or asynchronous reader is
                    // retained by this case. Suppress any subsequent local use.
                    m_abandoned = true;
                    MV_LOG(info, "Pump fixture: consumer departed with a live borrow");
                }
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
            if (!m_consumer.abandon_unsubmitted(m_route, { 1u })) unsafe();
            else m_done = true;
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
                        m_failed = true;
                        m_done = m_consumer.abandon_unsubmitted(m_route, { 1u });
                        if (!m_done) unsafe();
                    }
                    else unsafe();
                    m_post = EPost::none;
                    return;
                }
                if (m_post == EPost::request) m_submitted = true;
                if (m_post == EPost::quiesce) m_quiescent_sent = true;
                m_post = EPost::none;
            }
            if (m_terminal && (!m_borrow.value || m_consumer.retired(m_borrow)))
            {
                if (!m_consumer.forget(m_route, { 1u })) unsafe();
                else m_done = true;
                if (m_done && m_borrow.value) MV_LOG(info, "Pump fixture: borrowed resource retired after consumer quiescence");
                return;
            }
            if (!m_releasing && (m_mode == EMode::cancel) && m_prepared && !m_terminal)
            {
                services::CancelServiceRequest cancel;
                if (!m_consumer.cancel(m_route, { 1u }, cancel))
                {
                    unsafe();
                    return;
                }
                m_pending = threading::make_pod_message(-1, cancel);
                m_post = EPost::cancel;
                m_releasing = true;
            }
            else if (!m_releasing && m_borrow.value && (m_stopping ||
                (m_terminal && (m_mode == EMode::normal))))
            {
                services::ReleaseServiceBorrow release;
                if (!m_consumer.release(m_borrow, release))
                {
                    unsafe();
                    return;
                }
                m_pending = threading::make_pod_message(-1, release);
                m_post = EPost::release;
                m_releasing = true;
            }
            else if (m_releasing && !m_quiescent_prepared)
            {
                services::ServiceBorrowQuiescent quiescent;
                if (!m_consumer.quiesce(m_borrow, quiescent))
                {
                    return;
                }
                m_pending = threading::make_pod_message(-1, quiescent);
                m_post = EPost::quiesce;
                m_quiescent_prepared = true;
            }
            else return;
        }
        runnable = m_post != EPost::none;
    }

private:
    enum class EPost : unsigned { none, request, cancel, release, quiesce };
    const modules::CCommunication& m_communication;
    services::CServiceBorrowClient m_consumer;
    services::SServiceRoute m_route;
    services::SServiceBorrow m_borrow;
    threading::CErasedPodMsg m_pending;
    platform::threading::CWaitDeadline m_deadline;
    EMode m_mode{ EMode::normal };
    EPost m_post{ EPost::none };
    bool m_active{}, m_submitted{}, m_prepared{}, m_terminal{}, m_stopping{}, m_releasing{};
    bool m_quiescent_prepared{}, m_quiescent_sent{}, m_done{}, m_abandoned{}, m_failed{}, m_unsafe{};
    void unsafe() noexcept { m_failed = m_unsafe = true; }
};

}   // namespace pump_tests
