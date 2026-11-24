
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    borrow_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Eight input fixture borrows with retained admission and cleanup publications.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/services/service_borrow_client.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include <cstring>

namespace input_host
{

class CBorrowExecution
{
public:
    static constexpr unsigned k_count = 8u;
    struct SRecord
    {
        services::ServiceRequest request;
        services::SServiceBorrow token;
        threading::CErasedPodMsg registration, cleanup;
        platform::threading::CWaitDeadline deadline;
        services::EServiceStatus expected{ services::EServiceStatus::success }, status{ services::EServiceStatus::success };
        bool live{}, submitted{}, acknowledged{}, prepared{}, terminal{}, closing{}, local_closed{};
        bool registration_pending{}, registration_sent{}, registration_done{};
        bool cancel_sent{}, release_sent{}, quiescence_prepared{}, quiescence_sent{}, cleanup_pending{};
    };
    explicit CBorrowExecution(const modules::CCommunication& communication) noexcept : m_communication{ communication } {}
    [[nodiscard]] const SRecord& record(const unsigned index) const noexcept { return m_records[index]; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] bool publication_pending() const noexcept
    {
        for (const SRecord& record : m_records)
        {
            if (record.live && (!record.submitted || record.registration_pending))
            {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool complete() const noexcept
    {
        if (m_unsafe) return false;
        for (const SRecord& record : m_records)
        {
            if (record.live) return false;
        }
        return true;
    }
    [[nodiscard]] bool begin(const unsigned index, const services::ServiceRequest& request,
        const services::EServiceStatus expected = services::EServiceStatus::success) noexcept
    {
        if ((index >= k_count) || m_records[index].live || m_failed ||
            (request.payload_size > sizeof(request.payload)) || !m_client.begin(request.route, request.request, request.opcode))
        {
            return false;
        }
        SRecord& record = m_records[index];
        record = {};
        record.live = true;
        record.request = request;
        record.expected = expected;
        return deadline(record);
    }
    [[nodiscard]] services::SServiceView view(const unsigned index) const noexcept
    {
        const SRecord& record = m_records[index];
        return (record.live && !record.local_closed && !record.closing) ? m_client.view(record.token) : services::SServiceView{};
    }
    [[nodiscard]] services::SNotificationRegistration registration(const unsigned index) const noexcept
    {
        return m_client.registration(m_records[index].token);
    }
    [[nodiscard]] threading::SNotificationReader notification(const unsigned index) const noexcept
    {
        const SRecord& record = m_records[index];
        return (record.live && !record.local_closed && !record.closing) ? m_client.notification(record.token) : threading::SNotificationReader{};
    }
    [[nodiscard]] bool register_notification(const unsigned index) noexcept
    {
        SRecord& record = m_records[index];
        services::RegisterServiceNotification request;
        if (!record.live || record.local_closed || record.closing || record.registration_pending ||
            record.registration_sent || !m_client.request_notification(record.token, request))
        {
            return false;
        }
        record.registration = threading::make_pod_message(slot(index), request);
        record.registration_pending = true;
        return deadline(record);
    }
    // Caller closes every reader, copied descriptor and dependent local user
    // BEFORE this call. A Host closing notice alone is not that local proof.
    void close_local(const unsigned index) noexcept
    {
        SRecord& record = m_records[index];
        if (!record.live || record.local_closed)
        {
            return;
        }
        record.local_closed = true;
        (void)deadline(record);
        if (record.registration_pending && !record.registration_sent)
        {
            if (!m_client.abandon_unsubmitted_notification(record.token)) unsafe();
            record.registration_pending = false;
            record.registration = {};
        }
        if (!record.submitted)
        {
            if (!m_client.abandon_unsubmitted(record.request.route, record.request.request)) unsafe();
            else record.live = false;
        }
    }
    [[nodiscard]] bool owns(const threading::CErasedPodMsg& message) const noexcept
    {
        const std::int32_t correlation = message.query_async_slot();
        if ((correlation >= k_first_slot) && (correlation < (k_first_slot + static_cast<std::int32_t>(k_count))))
        {
            return true;
        }
        services::ServiceBorrowControl control;
        return message.copy_payload_to(control);
    }
    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        if (m_unsafe) return;
        services::ServiceBorrowControl control;
        if (message.copy_payload_to(control))
        {
            SRecord* const record = find(control.route, control.request);
            if (!record)
            {
                // A terminal never-published cancellation may have already
                // settled. The underlying protocol explicitly permits this.
                if (!m_client.observe(control)) unsafe();
                return;
            }
            if (!record->submitted ||
                ((control.control == services::EServiceBorrowControl::retired) &&
                    (control.quiescence != services::EServiceQuiescence::never_published) && !record->quiescence_sent) ||
                !m_client.observe(control))
            {
                unsafe();
                return;
            }
            record->token = control.borrow;
            record->closing = true;
            return;
        }
        const std::int32_t correlation = message.query_async_slot();
        if ((correlation < k_first_slot) || (correlation >= (k_first_slot + static_cast<std::int32_t>(k_count))))
        {
            unsafe();
            return;
        }
        SRecord& record = m_records[correlation - k_first_slot];
        services::ServiceReply reply;
        services::ServiceNotificationRegistered registered;
        if (!record.live || !record.submitted)
        {
            unsafe();
            return;
        }
        if (message.copy_payload_to(reply))
        {
            if ((reply.route.value != record.request.route.value) || (reply.request.value != record.request.request.value) ||
                !m_client.observe(reply))
            {
                unsafe();
                return;
            }
            record.token = m_client.borrow(reply.route, reply.request);
            if (reply.notice == services::EServiceNotice::acknowledged) record.acknowledged = true;
            if (reply.notice == services::EServiceNotice::prepared) record.prepared = true;
            if (reply.notice == services::EServiceNotice::completed)
            {
                record.terminal = true;
                record.status = reply.status;
                if (reply.status != services::EServiceStatus::success) record.closing = true;
                if ((reply.status != record.expected) && !record.local_closed) m_failed = true;
            }
            return;
        }
        if (message.copy_payload_to(registered))
        {
            if (!record.registration_sent || record.registration_done ||
                (registered.borrow.value != record.token.value) || !m_client.observe(registered))
            {
                unsafe();
                return;
            }
            record.registration_done = true;
            if ((registered.status != services::EServiceStatus::success) && !record.local_closed) m_failed = true;
            return;
        }
        unsafe();
    }

    // One bounded pass, at most one publication per live record. Private
    // per-borrow storage retains each cleanup message after client mutation.
    void advance(bool& runnable, std::uint64_t& next_wake_ns) noexcept
    {
        runnable = false;
        if (m_unsafe) return;
        for (unsigned index = 0u; index < k_count; ++index)
        {
            SRecord& record = m_records[index];
            if (!record.live) continue;
            if (record.local_closed && record.terminal && (!record.token.value || m_client.retired(record.token)) &&
                (!record.registration_sent || record.registration_done) && !record.cleanup_pending)
            {
                if (!m_client.forget(record.request.route, record.request.request)) unsafe();
                else record.live = false;
                continue;
            }
            if (!record.terminal || record.local_closed || (record.registration_sent && !record.registration_done) || record.registration_pending)
            {
                std::uint64_t remaining = 0u;
                if ((record.deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
                {
                    unsafe();
                    return;
                }
                if (remaining < next_wake_ns) next_wake_ns = remaining;
            }
            if (!record.submitted)
            {
                const modules::ESubmission result = m_communication.submit({}, threading::make_pod_message(slot(index), record.request));
                if (modules::accepted(result)) record.submitted = true;
                else if (result == modules::ESubmission::busy)
                {
                    // Routed request IDs are monotonic. A later prepared
                    // acquisition must not overtake this refused publication.
                    return;
                }
                else if (result != modules::ESubmission::busy)
                {
                    m_failed = true;
                    close_local(index);
                }
                continue;
            }
            if (record.registration_pending)
            {
                const modules::ESubmission result = m_communication.submit({}, record.registration);
                if (modules::accepted(result))
                {
                    record.registration_sent = true;
                    record.registration_pending = false;
                    record.registration = {};
                }
                else if (result != modules::ESubmission::busy)
                {
                    m_failed = true;
                    // Do not close caller-owned readers from this helper.
                    record.closing = true;
                }
                continue;
            }
            if (!record.local_closed) continue;
            if (!record.cleanup_pending && !record.token.value && record.acknowledged && !record.terminal && !record.cancel_sent)
            {
                services::CancelServiceRequest cancellation;
                if (!m_client.cancel(record.request.route, record.request.request, cancellation))
                {
                    unsafe();
                    return;
                }
                record.cleanup = threading::make_pod_message(slot(index), cancellation);
                record.cleanup_pending = true;
            }
            if (!record.cleanup_pending && record.token.value && !m_client.retired(record.token))
            {
                if (!record.closing && !record.release_sent)
                {
                    services::ReleaseServiceBorrow release;
                    if (!m_client.release(record.token, release))
                    {
                        unsafe();
                        return;
                    }
                    record.cleanup = threading::make_pod_message(slot(index), release);
                    record.cleanup_pending = true;
                }
                else if (!record.quiescence_prepared)
                {
                    services::ServiceBorrowQuiescent quiescence;
                    if (m_client.quiesce(record.token, quiescence))
                    {
                        record.cleanup = threading::make_pod_message(slot(index), quiescence);
                        record.cleanup_pending = record.quiescence_prepared = true;
                    }
                }
            }
            if (!record.cleanup_pending) continue;
            const modules::ESubmission result = m_communication.submit({}, record.cleanup);
            if (modules::accepted(result))
            {
                services::CancelServiceRequest cancellation;
                services::ReleaseServiceBorrow release;
                if (record.cleanup.copy_payload_to(cancellation)) record.cancel_sent = true;
                else if (record.cleanup.copy_payload_to(release)) record.release_sent = record.closing = true;
                else record.quiescence_sent = true;
                record.cleanup = {};
                record.cleanup_pending = false;
                // The next invocation can prepare the following cleanup step.
                runnable = true;
            }
            else if (result != modules::ESubmission::busy)
            {
                unsafe();
                return;
            }
        }
    }

private:
    static constexpr std::int32_t k_first_slot = 16;
    static std::int32_t slot(const unsigned index) noexcept { return k_first_slot + static_cast<std::int32_t>(index); }
    SRecord* find(const services::SServiceRoute route, const services::SServiceRequestId request) noexcept
    {
        for (SRecord& record : m_records)
        {
            if (record.live && (record.request.route.value == route.value) && (record.request.request.value == request.value))
            {
                return &record;
            }
        }
        return nullptr;
    }
    bool deadline(SRecord& record) noexcept
    {
        if (record.deadline.set_after_milliseconds(10000u).code == platform::threading::EWaitError::none)
        {
            return true;
        }
        unsafe();
        return false;
    }
    void unsafe() noexcept { m_failed = m_unsafe = true; }
    const modules::CCommunication m_communication;
    services::CServiceBorrowClient m_client;
    SRecord m_records[k_count];
    bool m_failed{}, m_unsafe{};
};

}   //  namespace input_host
