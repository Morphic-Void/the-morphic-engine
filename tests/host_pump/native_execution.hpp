
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    native_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Visible Windows acceptance with retained window/notification/report custody.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/platform/window.hpp"
#include "framework/services/service_borrow_client.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "tests/host_pump/report_execution.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "debug/macros.hpp"
#include <cstring>
#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

namespace pump_tests
{

class CNativeExecution
{
public:
    enum class EMode : unsigned { move, size, menu, report };
    CNativeExecution(const modules::CCommunication& communication, const modules::SHostExecutionContext* const execution) noexcept
        : m_communication{ communication }, m_execution{ execution }, m_report{ communication, false } {}
    bool active() const noexcept { return m_active; }
    bool failed() const noexcept { return m_failed; }
    bool done() const noexcept { return m_done; }
    bool domain_complete() const noexcept { return !m_active || (!m_unsafe && m_done && m_report.domain_complete()); }
    bool start(const services::OpenServiceResult& opened, const EMode mode) noexcept
    {
#if MV_PLATFORM_WINDOWS
        if (m_active || !m_execution || !opened.route.value || !opened.provider.value ||
            !m_consumer.begin(opened.route, { 1u }, create_opcode))
        {
            return false;
        }
        m_active = true;
        m_opened = opened;
        m_mode = mode;
        services::ServiceRequest request;
        request.route = opened.route;
        request.request = { 1u };
        request.opcode = create_opcode;
        prepare(EPost::create, 30, request);
        if (!deadline(10000u)) unsafe();
        return !m_failed;
#else
        (void)opened;
        (void)mode;
        return false;
#endif
    }
    bool owns(const threading::CErasedPodMsg& message) const noexcept
    {
        const std::int32_t slot = message.query_async_slot();
        return m_active && (((slot >= 30) && (slot <= 33)) ||
            message.is_payload_a<services::ServiceBorrowControl>() ||
            (m_report.active() && ((slot == 264) || (slot == 265))));
    }
    void stop() noexcept
    {
        if (!m_stopping && m_active && !deadline(5000u)) m_unsafe = m_failed = true;
        m_stopping = true;
        m_reader = {};
        m_report.stop();
        if (m_post == EPost::registration)
        {
            if (!m_consumer.abandon_unsubmitted_notification(m_borrow)) m_unsafe = m_failed = true;
            m_post = EPost::none;
        }
        if ((m_post == EPost::ready) || (m_post == EPost::deferred)) m_post = EPost::none;
    }
    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        using namespace services;
        if (m_unsafe || m_done)
        {
            return;
        }
        const std::int32_t slot = message.query_async_slot();
        if (m_report.active() && ((slot == 264) || (slot == 265)))
        {
            m_report.receive(message);
            return;
        }
        ServiceReply reply;
        ServiceNotificationRegistered registration;
        ServiceBorrowControl control;
        if (message.copy_payload_to(reply))
        {
            if (reply.route.value != m_opened.route.value)
            {
                unsafe();
                return;
            }
            if (reply.request.value == 1u)
            {
                if (!m_submitted || (slot != 30) || !m_consumer.observe(reply))
                {
                    unsafe();
                    return;
                }
                if (reply.notice == EServiceNotice::acknowledged) m_acknowledged = true;
                if (reply.notice == EServiceNotice::prepared)
                {
                    m_borrow = m_consumer.borrow(m_opened.route, { 1u });
                    if (!m_stopping)
                    {
                        const SServiceView view = m_consumer.view(m_borrow);
                        if (!view.descriptor || (view.size != sizeof(platform::windows::SWindowViewV1)) || (view.version != 1u))
                        {
                            fail();
                            return;
                        }
                        const platform::windows::SWindowViewV1& window = *static_cast<const platform::windows::SWindowViewV1*>(view.descriptor);
                        if (!window.is_valid() || (window.provider.value != m_opened.provider.value))
                        {
                            fail();
                            return;
                        }
                        m_window = window.window;
                        m_native_window = window.native_window;
                        m_have_view = true; // Only value identities survive this callback.
                    }
                }
                if (reply.notice == EServiceNotice::completed)
                {
                    m_terminal = true;
                    if ((reply.status != EServiceStatus::success) && !m_stopping) fail();
                }
                return;
            }
            const bool ready = reply.request.value == 2u;
            bool& acknowledged = ready ? m_ready_acknowledged : m_deferred_acknowledged;
            bool& complete = ready ? m_ready_complete : m_deferred_complete;
            if ((reply.request.value != (ready ? 2u : 3u)) || (slot != (ready ? 32 : 33)) ||
                (reply.opcode != (ready ? ready_opcode : UINT16_MAX)) ||
                !(ready ? m_ready_sent : m_deferred_sent) || complete)
            {
                unsafe();
                return;
            }
            if (!ready && !m_stop_requested && !m_stopping)
            {
                fail(); // Ordinary replies must stay deferred inside the native frame.
            }
            if (reply.notice == EServiceNotice::acknowledged)
            {
                if (acknowledged || (reply.status != EServiceStatus::success)) unsafe();
                else acknowledged = true;
            }
            else if ((reply.notice == EServiceNotice::completed) && acknowledged)
            {
                complete = true;
                const bool cancelled = (reply.status == EServiceStatus::closing) || (reply.status == EServiceStatus::cancelled);
                if (ready ? ((reply.status != EServiceStatus::success) && !(m_stopping && cancelled)) :
                    (reply.status != EServiceStatus::closing)) fail();
            }
            else unsafe();
        }
        else if (message.copy_payload_to(registration))
        {
            if ((slot != 31) || !m_registration_sent || m_registration_complete || !m_consumer.observe(registration))
            {
                unsafe();
                return;
            }
            m_registration_complete = true;
            if (registration.status != EServiceStatus::success)
            {
                if (!m_stopping) fail();
            }
            else if (!m_stopping) m_reader = m_consumer.notification(m_borrow);
        }
        else if (message.copy_payload_to(control))
        {
            if ((control.route.value != m_opened.route.value) || (control.request.value != 1u) ||
                ((control.control == EServiceBorrowControl::retired) && !m_quiescent_sent &&
                    (control.quiescence != EServiceQuiescence::never_published)) || !m_consumer.observe(control))
            {
                unsafe();
                return;
            }
            m_borrow = control.borrow;
            m_releasing = true;
            if (!m_stopping && !m_stop_requested) fail();
            else stop();
        }
        else unsafe();
    }
    void advance(bool& runnable, std::uint64_t& next_wake) noexcept
    {
        if (!m_active || m_done || m_unsafe)
        {
            return;
        }
        m_report.advance(runnable, next_wake);
        if (m_report.failed()) fail();
        if (m_stopping && !m_submitted)
        {
            m_done = m_consumer.abandon_unsubmitted(m_opened.route, { 1u });
            m_post = EPost::none;
            if (!m_done) unsafe();
            return;
        }
        std::uint64_t remaining = 0u;
        if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            unsafe();
            return;
        }
        if (remaining < next_wake) next_wake = remaining;
        if (!m_stopping && m_reader.is_valid())
        {
            if (m_reader.failed()) fail();
            else
            {
                m_notified |= m_reader.pending();
                m_reader.clear_pending();
            }
        }
        if (!m_stopping && (m_phase != EPhase::ready))
        {
            if (next_wake > 1000000u) next_wake = 1000000u;
        }
        for (unsigned turn = 0u; turn < 2u; ++turn)
        {
            if (m_post != EPost::none)
            {
                const modules::ESubmission result = m_communication.submit({}, m_pending);
                if (result == modules::ESubmission::busy)
                {
                    return;
                }
                if (!modules::accepted(result))
                {
                    const EPost refused = m_post;
                    if (refused == EPost::create)
                    {
                        m_done = m_consumer.abandon_unsubmitted(m_opened.route, { 1u });
                        if (!m_done) unsafe();
                    }
                    else if ((refused != EPost::registration) && (refused != EPost::ready) && (refused != EPost::deferred)) unsafe();
                    fail(); // stop() also abandons an unaccepted registration.
                    m_post = EPost::none;
                    runnable = !m_unsafe && !m_done;
                    return;
                }
                if (m_post == EPost::create) m_submitted = true;
                if (m_post == EPost::registration) m_registration_sent = true;
                if (m_post == EPost::ready) m_ready_sent = true;
                if (m_post == EPost::quiesce) m_quiescent_sent = true;
                if (m_post == EPost::deferred)
                {
                    m_deferred_sent = true;
                    if (m_due.set_after_milliseconds(150u).code != platform::threading::EWaitError::none) unsafe();
                }
                m_post = EPost::none;
            }
            if (m_stopping && m_terminal && (!m_registration_sent || m_registration_complete) &&
                (!m_ready_sent || m_ready_complete) && (!m_deferred_sent || m_deferred_complete) &&
                (!m_borrow.value || m_consumer.retired(m_borrow)) && m_report.domain_complete())
            {
#if MV_PLATFORM_WINDOWS
                if (m_native_window && IsWindow(reinterpret_cast<HWND>(m_native_window)))
                {
                    if (next_wake > 1000000u) next_wake = 1000000u;
                    return;
                }
#endif
                if (m_stop_requested)
                {
                    modules::SHostRestrictionSnapshot restriction;
                    const modules::ERestrictionRead read = m_execution->restriction.read(restriction);
                    if (read == modules::ERestrictionRead::retry)
                    {
                        if (next_wake > 1000000u) next_wake = 1000000u;
                        return;
                    }
                    if ((read != modules::ERestrictionRead::success) || restriction.depth)
                    {
                        unsafe();
                        return;
                    }
                }
                if (!m_consumer.forget(m_opened.route, { 1u })) unsafe();
                else
                {
                    m_done = true;
                    if (m_stop_requested) MV_LOG(info, "Pump fixture: Platform native shutdown retired borrow and destroyed window");
                }
                return;
            }
            if (!m_stopping && m_have_view && !m_registration_prepared)
            {
                services::RegisterServiceNotification registration;
                if (!m_consumer.request_notification(m_borrow, registration))
                {
                    unsafe();
                    return;
                }
                m_registration_prepared = true;
                prepare(EPost::registration, 31, registration);
            }
            else if (!m_stopping && m_registration_complete && !m_ready_sent)
            {
                services::ServiceRequest request;
                request.route = m_opened.route;
                request.request = { 2u };
                request.opcode = ready_opcode;
                const platform::windows::SWindowReady payload{ m_window, m_borrow, m_consumer.registration(m_borrow) };
                request.payload_size = sizeof(payload);
                std::memcpy(request.payload, &payload, sizeof(payload));
                prepare(EPost::ready, 32, request);
            }
            else if (m_stopping && !m_releasing && m_borrow.value)
            {
                services::ReleaseServiceBorrow release;
                if (!m_consumer.release(m_borrow, release))
                {
                    unsafe();
                    return;
                }
                m_releasing = true;
                prepare(EPost::release, -1, release);
            }
            else if (m_stopping && !m_releasing && m_acknowledged && !m_terminal)
            {
                services::CancelServiceRequest cancel;
                if (!m_consumer.cancel(m_opened.route, { 1u }, cancel))
                {
                    unsafe();
                    return;
                }
                m_releasing = true;
                prepare(EPost::cancel, -1, cancel);
            }
            else if (m_releasing && !m_quiescent_prepared)
            {
                services::ServiceBorrowQuiescent proof;
                if (!m_consumer.quiesce(m_borrow, proof))
                {
                    return;
                }
                m_reader = {};
                m_have_view = false;
                m_quiescent_prepared = true;
                prepare(EPost::quiesce, -1, proof);
            }
            else
            {
                if (!m_stopping) native_step(runnable, next_wake);
                return;
            }
        }
        runnable |= m_post != EPost::none;
    }

private:
    friend struct SNativeExecutionTestAccess;
    static constexpr std::uint16_t create_opcode = static_cast<std::uint16_t>(platform::windows::EWindowCommand::create_window);
    static constexpr std::uint16_t ready_opcode = static_cast<std::uint16_t>(platform::windows::EWindowCommand::window_ready);
    enum class EPost : unsigned { none, create, registration, ready, deferred, cancel, release, quiesce };
    enum class EPhase : unsigned { ready, report, modal_start, modal_observe, hold, moving, drain };
    const modules::CCommunication& m_communication;
    const modules::SHostExecutionContext* const m_execution;
    CReportExecution m_report;
    services::CServiceBorrowClient m_consumer;
    services::OpenServiceResult m_opened;
    services::SServiceBorrow m_borrow;
    platform::windows::SWindowToken m_window;
    threading::SNotificationReader m_reader;
    std::uintptr_t m_native_window{};
    EMode m_mode{ EMode::move };
    EPhase m_phase{ EPhase::ready };
    EPost m_post{ EPost::none };
    threading::CErasedPodMsg m_pending;
    platform::threading::CWaitDeadline m_deadline, m_due;
    bool m_active{}, m_failed{}, m_unsafe{}, m_done{}, m_stopping{}, m_stop_requested{}, m_have_view{}, m_notified{};
    bool m_submitted{}, m_acknowledged{}, m_terminal{}, m_registration_prepared{}, m_registration_sent{}, m_registration_complete{};
    bool m_ready_sent{}, m_ready_acknowledged{}, m_ready_complete{}, m_deferred_sent{}, m_deferred_acknowledged{}, m_deferred_complete{};
    bool m_releasing{}, m_quiescent_prepared{}, m_quiescent_sent{};
    bool m_restriction_retry{};
    unsigned m_moves{}, m_samples{};
    std::uint64_t m_worst{};
    void fail() noexcept { m_failed = true; stop(); }
    void unsafe() noexcept { m_unsafe = true; fail(); }
    bool deadline(const std::uint32_t milliseconds) noexcept
    {
        return m_deadline.set_after_milliseconds(milliseconds).code == platform::threading::EWaitError::none;
    }
    template<typename T> void prepare(const EPost post, const std::int32_t slot, const T& value) noexcept
    {
        m_pending = threading::make_pod_message(slot, value);
        m_post = post;
    }
    bool due(std::uint64_t& next_wake) noexcept
    {
        std::uint64_t remaining = 0u;
        if (m_due.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
        {
            fail();
            return false;
        }
        if (remaining && (remaining < next_wake)) next_wake = remaining;
        return !remaining;
    }
    bool restricted() noexcept
    {
        modules::SHostRestrictionSnapshot restriction;
        const modules::ERestrictionRead result = m_execution->restriction.read(restriction);
        m_restriction_retry = result == modules::ERestrictionRead::retry;
        if (result == modules::ERestrictionRead::failed) fail();
        return (result == modules::ERestrictionRead::success) &&
            (restriction.authority.value == m_opened.provider.value) &&
            (restriction.reasons & (1u << ((m_mode == EMode::menu) ? 4u : 3u)));
    }
    void native_step(bool& runnable, std::uint64_t& next_wake) noexcept
    {
#if MV_PLATFORM_WINDOWS
        const HWND window = reinterpret_cast<HWND>(m_native_window);
        switch (m_phase)
        {
            case EPhase::ready:
            {
                if (!m_terminal || !m_ready_complete || !m_notified)
                {
                    return;
                }
                if (!IsWindow(window))
                {
                    fail();
                    return;
                }
                if (m_mode == EMode::report)
                {
                    if (!m_report.start(CReportExecution::EMode::live) || !deadline(12000u))
                    {
                        fail();
                        return;
                    }
                    m_phase = EPhase::report;
                }
                else m_phase = EPhase::modal_start;
                runnable = true;
                return;
            }
            case EPhase::report:
            {
                if (m_report.live_ready())
                {
                    if (!m_samples)
                    {
                        fail();
                        return;
                    }
                    MV_LOG(info, "Live Platform report refresh, window replies {}, maximum window response ns {}", m_samples, m_worst);
                    m_phase = EPhase::modal_start;
                    runnable = true;
                    return;
                }
                if (m_report.live_update_pending())
                {
                    DWORD_PTR ignored{};
                    std::uint64_t before = 0u, after = 0u;
                    if (!PostMessageW(window, WM_MOUSEMOVE, 0u, MAKELPARAM((m_samples & 63u), 8u)) ||
                        (platform::threading::query_awake_nanoseconds(before).code != platform::threading::EWaitError::none) ||
                        !SendMessageTimeoutW(window, WM_NULL, 0u, 0u, SMTO_BLOCK, 7000u, &ignored) ||
                        (platform::threading::query_awake_nanoseconds(after).code != platform::threading::EWaitError::none) || (after < before))
                    {
                        fail();
                        return;
                    }
                    if ((after - before) > m_worst) m_worst = after - before;
                    ++m_samples;
                }
                return;
            }
            case EPhase::modal_start:
            {
                const WPARAM command = (m_mode == EMode::menu) ? SC_KEYMENU :
                    ((m_mode == EMode::size) ? (SC_SIZE + WMSZ_BOTTOMRIGHT) : SC_MOVE);
                if (!PostMessageW(window, WM_SYSCOMMAND, command, (m_mode == EMode::menu) ? VK_SPACE : 0) || !deadline(3000u))
                {
                    fail();
                    return;
                }
                m_phase = EPhase::modal_observe;
                if (next_wake > 1000000u) next_wake = 1000000u;
                return;
            }
            case EPhase::modal_observe:
            {
                if (!restricted()) return;
                MV_LOG(info, "Pump fixture: Platform native modal scope authenticated by Host");
                services::ServiceRequest request;
                request.route = m_opened.route;
                request.request = { 3u };
                request.opcode = UINT16_MAX;
                prepare(EPost::deferred, 33, request);
                m_phase = EPhase::hold;
                runnable = true;
                return;
            }
            case EPhase::hold:
            case EPhase::moving:
            {
                if (!m_deferred_sent || !due(next_wake))
                {
                    return;
                }
                if ((m_mode == EMode::size) && (m_moves < 20u))
                {
                    INPUT input{};
                    input.type = INPUT_MOUSE;
                    input.mi.dx = input.mi.dy = (m_moves & 1u) ? -2 : 2;
                    input.mi.dwFlags = MOUSEEVENTF_MOVE;
                    if ((SendInput(1u, &input, sizeof(input)) != 1u) ||
                        (m_due.set_after_milliseconds(10u).code != platform::threading::EWaitError::none))
                    {
                        fail();
                        return;
                    }
                    ++m_moves;
                    m_phase = EPhase::moving;
                    return;
                }
                if (!restricted())
                {
                    if (!m_failed && !m_restriction_retry) fail();
                    return;
                }
                m_stop_requested = true;
                if (!m_execution->stop.request())
                {
                    fail();
                    return;
                }
                m_phase = EPhase::drain;
                return;
            }
            case EPhase::drain:
            {
                break;
            }
        }
#else
        (void)runnable;
        (void)next_wake;
        fail();
#endif
    }
};

} // namespace pump_tests
