
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Incremental routed-service acceptance driven by the common execution context.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/services/service_protocol.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/host_pump/fixture_types.hpp"
#include "tests/host_pump/credit_execution.hpp"
#include "tests/host_pump/borrow_execution.hpp"
#include "tests/host_pump/notification_execution.hpp"
#include "tests/host_pump/modal_execution.hpp"
#include "tests/host_pump/schema_execution.hpp"
#include "tests/host_pump/report_execution.hpp"
#include "tests/host_pump/native_execution.hpp"
#include "system/system_context.hpp"
#include "debug/macros.hpp"
#include "debug/service.hpp"
#include <cstring>

namespace pump_tests
{

class CExecutiveExecution
{
public:
    CExecutiveExecution(const modules::SExecutionContext& context, const char* const selected, const bool replacement,
        const char* const schema_handoff_path = "declaration-id.bin", const char* const report_output_prefix = "") noexcept
        : m_communication{ *context.communication }, m_credits{ m_communication }, m_borrow{ m_communication },
          m_notification{ m_communication }, m_modal{ m_communication, context.execution },
          m_schema{ m_communication, replacement, schema_handoff_path },
          m_report{ m_communication, replacement, report_output_prefix }, m_native{ m_communication, context.execution }, m_replacement{ replacement }
    {
        const std::size_t length = std::strlen(selected);
        if (length < sizeof(m_selected)) std::memcpy(m_selected, selected, (length + 1u));
        else m_failed = true;
        // Additional domain protocols are migrated explicitly; an unsupported
        // fixture case must never report a generic route check as its pass.
        m_failed |= !supported();
        if (scenario("schema-process")) m_stage = EStage::schema_start;
        if (report_scenario()) m_stage = EStage::report_start;
    }
    bool complete() const noexcept
    {
        return m_stopping && (m_finished || m_failed) && m_credits.quiescent() && m_borrow.domain_complete() &&
            m_notification.domain_complete() && m_modal.domain_complete() && m_schema.domain_complete() && m_report.domain_complete() && m_native.domain_complete();
    }
    modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        switch (invocation.event)
        {
            case modules::EExecutionEvent::start:
            case modules::EExecutionEvent::service_async:
            case modules::EExecutionEvent::provider_admission_closed:
            {
                break;
            }
            case modules::EExecutionEvent::message:
            {
                if (!invocation.source.is_host() || !invocation.message || !receive(*invocation.message))
                {
                    m_failed = true;
                }
                break;
            }
            case modules::EExecutionEvent::stop:
            {
                if (!m_stopping && m_credits.active())
                {
                    MV_LOG(info, "Pump fixture: threaded stop received with {} results pending", m_credits.pending_results());
                }
                m_stopping = true;
                m_credits.stop();
                m_borrow.stop();
                m_notification.stop();
                m_modal.stop();
                m_schema.stop();
                m_report.stop();
                m_native.stop();
                break;
            }
            default:
            {
                m_failed = true;
                break;
            }
        }
        m_runnable = false;
        std::uint64_t remaining = UINT64_MAX;
        bool credit_runnable = false;
        m_credits.advance(credit_runnable, remaining);
        bool borrow_runnable = false;
        m_borrow.advance(borrow_runnable, remaining);
        bool notification_runnable = false;
        m_notification.advance(notification_runnable, remaining);
        bool modal_runnable = false;
        m_modal.advance(modal_runnable, remaining);
        bool schema_runnable = false;
        m_schema.advance(schema_runnable, remaining);
        bool report_runnable = false;
        m_report.advance(report_runnable, remaining);
        bool native_runnable = false;
        m_native.advance(native_runnable, remaining);
        if (m_credits.failed() || m_borrow.failed() || m_notification.failed() || m_modal.failed() || m_schema.failed() ||
            m_report.failed() || m_native.failed()) m_failed = true;
        if (!m_failed && !m_finished && !advance()) m_failed = true;
        if (!m_failed && !m_finished && m_timing)
        {
            std::uint64_t operation_remaining = 0u;
            m_failed |= (m_deadline.remaining_nanoseconds(operation_remaining).code != platform::threading::EWaitError::none) || !operation_remaining;
            if (operation_remaining < remaining) remaining = operation_remaining;
        }
        if (!m_failed && !m_finished && m_delayed_pending && !m_delayed_done)
        {
            std::uint64_t delayed = 0u;
            m_failed |= (m_delayed_deadline.remaining_nanoseconds(delayed).code != platform::threading::EWaitError::none) || !delayed;
            if (delayed < remaining) remaining = delayed;
        }
        if (!m_failed && m_close_retry_pending)
        {
            std::uint64_t retry = 0u;
            m_failed |= m_close_retry.remaining_nanoseconds(retry).code != platform::threading::EWaitError::none;
            if (retry < remaining) remaining = retry;
        }
        if (m_failed) m_owner = {};
        if (m_finished && !m_logged)
        {
            m_logged = true;
            if (m_credits.active())
            {
                MV_LOG(info, "Pump fixture: threaded 600 retained disposals, releases and self-exit passed");
            }
            MV_LOG_RICH(info, "Pump fixture: %s passed", m_selected);
        }
        progress = {};
        progress.ready = 1u;
        progress.work_pending = (!m_failed && !m_finished && m_runnable) || credit_runnable || borrow_runnable ||
            notification_runnable || modal_runnable || schema_runnable || report_runnable || native_runnable;
        progress.work_complete = complete();
        progress.next_wake_ns = ((m_failed || m_finished) && m_credits.quiescent() && m_borrow.domain_complete() &&
            m_notification.domain_complete() && m_modal.domain_complete() && m_schema.domain_complete() && m_report.domain_complete() &&
            m_native.domain_complete()) ? UINT64_MAX : remaining;
        if (m_failed && !m_reported)
        {
            m_reported = true;
            MV_LOG(error, "Pump fixture failed at stage {}", static_cast<unsigned>(m_stage));
            return modules::EExecutionStatus::failed;
        }
        return modules::EExecutionStatus::success;
    }

private:
    friend struct SExecutiveExecutionTestAccess;
    enum class EStage : unsigned
    {
        load, duplicate, open, busy_unload, echoes, second_open, second_echo, second_close,
        provider_probe, scenario_work, credit_control, close, unload, reload, fresh_open,
        stale_close, fresh_close, final_unload, self_stop, wait_stop,
        sequence_first, sequence_wrap, sequence_reload, delayed, closing_after_stop, credits, borrow_start, borrow,
        notification_start, notification, modal_start, modal, schema_start, schema, shell_first, shell_second, shell_final,
        report_start, report, native_start, native
    };
    enum class EOperation : unsigned { none, module, open, service, close, self };
    modules::CCommunication m_communication;
    CCreditExecution m_credits;
    CBorrowExecution m_borrow;
    CNotificationExecution m_notification;
    CModalExecution m_modal;
    CSchemaExecution m_schema;
    CReportExecution m_report;
    CNativeExecution m_native;
    const bool m_replacement;
    char m_selected[64]{};
    EStage m_stage{ EStage::load };
    EOperation m_operation{ EOperation::none };
    threading::CErasedOwnerMsg m_owner;
    threading::CErasedPodMsg m_pod;
    services::OpenServiceResult m_primary, m_second, m_fresh;
    services::OpenServiceResult* m_opened{ nullptr };
    services::SServiceRoute m_expected_route;
    services::SServiceRequestId m_expected_request;
    modules::EModuleStatus m_module_status{ modules::EModuleStatus::success };
    services::EServiceStatus m_service_status{ services::EServiceStatus::success };
    std::int32_t m_slot{ -1 };
    std::uint16_t m_opcode{ 0u };
    unsigned m_echo_posted{ 0u }, m_echo_complete{ 0u };
    unsigned m_shell_duplicates{ 0u };
    bool m_echo_acknowledged[3]{}, m_echo_completed[3]{};
    bool m_sent{ false }, m_acknowledged{ false }, m_done{ false }, m_timing{ false };
    bool m_runnable{ false }, m_stopping{ false }, m_finished{ false }, m_failed{ false }, m_reported{ false }, m_logged{ false };
    platform::threading::CWaitDeadline m_deadline;
    platform::threading::CWaitDeadline m_delayed_deadline;
    platform::threading::CWaitDeadline m_close_retry;
    bool m_close_retry_pending{ false }, m_close_retried{ false };
    bool m_delayed_pending{ false }, m_delayed_done{ false };
    std::uint64_t m_sequence_expected{ 0u };

    bool scenario(const char* const value) const noexcept { return std::strcmp(m_selected, value) == 0; }
    bool borrow_scenario() const noexcept
    {
        return scenario("borrow-normal") || scenario("borrow-cancel") || scenario("borrow-abandoned") ||
            scenario("borrow-shutdown") || scenario("borrow-provider-failed");
    }
    bool rejected() const noexcept { return std::strncmp(m_selected, "bad-", 4u) == 0; }
    bool notification_scenario() const noexcept
    {
        return scenario("notify-normal") || scenario("notify-cancel") || scenario("notify-abandoned") ||
            scenario("notify-replacement") || scenario("notify-shutdown");
    }
    bool supported() const noexcept
    {
        return rejected() || activation_failure() || pump_failure() || borrow_scenario() || notification_scenario() || modal_scenario() || scenario("normal") || scenario("mixed") ||
            scenario("empty-provider") || scenario("cleanup-credits") || scenario("inbox-full") ||
            scenario("stop-timeout") || scenario("deactivate-failed") || scenario("sequence") ||
            scenario("shutdown-pending") || scenario("replacement") || scenario("emergency") || scenario("threaded-credits") ||
            scenario("schema-process") || scenario("platform-shell") || report_scenario() || native_scenario();
    }
    bool activation_failure() const noexcept
    {
        return scenario("activation-failed") || scenario("activation-null") || scenario("activation-timeout") ||
            scenario("unauthorised-native-stop") || scenario("unauthorised-native-events");
    }
    bool modal_scenario() const noexcept
    {
        return scenario("modal-normal") || scenario("modal-full") || scenario("modal-shutdown") || scenario("modal-activation");
    }
    bool pump_failure() const noexcept { return scenario("invalid-submission") || scenario("pump-failed"); }
    bool report_scenario() const noexcept
    {
        return scenario("platform-report") || scenario("platform-report-cancel") || scenario("platform-report-shutdown") ||
            scenario("platform-report-departure") || scenario("platform-report-injected");
    }
    bool native_scenario() const noexcept
    {
        return scenario("platform-native-move") || scenario("platform-native-size") || scenario("platform-native-menu") ||
            scenario("platform-native-report");
    }
    module_ids::id_type provider_module() const noexcept { return (scenario("platform-shell") || native_scenario()) ? module_ids::platform : module_ids::rendering; }
    bool start_deadline() noexcept
    {
        m_timing = true;
        return m_deadline.set_after_milliseconds(10000u).code == platform::threading::EWaitError::none;
    }
    void next(const EStage stage) noexcept
    {
        m_stage = stage;
        m_operation = EOperation::none;
        m_sent = m_acknowledged = m_done = m_timing = false;
        m_close_retry_pending = false;
        m_runnable = true;
    }
    bool begin_module(const modules::EModuleAction action, const modules::EModuleStatus expected,
        const bool self = false) noexcept
    {
        m_owner = threading::make_owner_message<modules::ModuleRequest>(10);
        modules::ModuleRequest* const request = m_owner.owner().payload<modules::ModuleRequest>();
        if (!request) return false;
        request->module = self ? module_ids::executive : provider_module();
        request->action = action;
        if (action != modules::EModuleAction::unload)
        {
            const std::string file = test_environment::module_filename(self ? "package:/bin/MorphicPumpReplacement" :
                ((scenario("platform-shell") || native_scenario()) ? "package:/bin/MorphicPlatform" : "package:/bin/MorphicPumpProvider"));
            if (!request->file.set(file.c_str()))
            {
                return false;
            }
        }
        m_module_status = expected;
        m_operation = self ? EOperation::self : EOperation::module;
        m_slot = 10;
        return start_deadline();
    }
    bool begin_open(services::OpenServiceResult& target, const std::int32_t slot, const std::uint32_t contract,
        const std::uint32_t version, const std::uint64_t request) noexcept
    {
        m_opened = &target;
        m_expected_request = { request };
        m_slot = slot;
        m_pod = threading::make_pod_message(slot, services::OpenServiceRequest{ provider_module(), contract, version, { request } });
        m_operation = EOperation::open;
        return start_deadline();
    }
    bool begin_close(const services::SServiceRoute route, const std::uint64_t request, const std::int32_t slot,
        const services::EServiceStatus expected = services::EServiceStatus::success) noexcept
    {
        m_expected_route = route;
        m_expected_request = { request };
        m_service_status = expected;
        m_slot = slot;
        m_pod = threading::make_pod_message(slot, services::CloseServiceRequest{ route, { request } });
        m_operation = EOperation::close;
        return start_deadline();
    }
    bool begin_service(const services::SServiceRoute route, const std::uint64_t request, const std::uint16_t opcode,
        const std::int32_t slot, const services::EServiceStatus expected = services::EServiceStatus::success) noexcept
    {
        services::ServiceRequest command;
        command.route = route;
        command.request = { request };
        command.opcode = opcode;
        if (opcode == 1u) command.payload_size = 1u;
        m_expected_route = route;
        m_expected_request = command.request;
        m_opcode = opcode;
        m_service_status = expected;
        m_slot = slot;
        m_pod = threading::make_pod_message(slot, command);
        m_operation = EOperation::service;
        return start_deadline();
    }
    bool publish() noexcept
    {
        if (m_sent) return true;
        if (m_close_retry_pending)
        {
            std::uint64_t remaining = 0u;
            if (m_close_retry.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
            {
                return false;
            }
            if (remaining) return true;
            m_close_retry_pending = false;
        }
        const modules::ESubmission result = m_owner.has_owner() ?
            m_communication.submit({}, std::move(m_owner)) : m_communication.submit({}, m_pod);
        if (modules::accepted(result))
        {
            m_sent = true;
            if ((m_operation == EOperation::self) && m_credits.active())
            {
                MV_LOG(info, "Pump fixture: threaded self-stop published with {} results pending", m_credits.pending_results());
            }
            if ((m_operation == EOperation::self) && scenario("schema-process") && !m_replacement) m_schema.replacement_accepted();
        }
        else if (result != modules::ESubmission::busy) return false;
        return true;
    }
    bool begin_sequence(const services::SServiceRoute route, const std::uint64_t request, const std::uint64_t expected) noexcept
    {
        std::atomic<std::uint64_t>* const counter = system_context::sequence_counter;
        if (!counter || (counter->fetch_add(1u, std::memory_order_relaxed) != expected))
        {
            return false;
        }
        m_sequence_expected = expected;
        return begin_service(route, request, 12u, 90);
    }
    bool receive(const threading::CErasedPodMsg& message) noexcept
    {
        if (m_native.owns(message))
        {
            m_native.receive(message);
            return true;
        }
        if (m_report.active())
        {
            m_report.receive(message);
            return true;
        }
        if (m_schema.owns(message.query_async_slot()))
        {
            m_schema.receive(message);
            return true;
        }
        if (m_modal.owns(message))
        {
            m_modal.receive(message);
            return true;
        }
        if (m_notification.owns(message))
        {
            m_notification.receive(message);
            return true;
        }
        if (m_borrow.owns(message))
        {
            m_borrow.receive(message);
            return true;
        }
        if (m_credits.owns(message.query_async_slot()))
        {
            m_credits.receive(message);
            return true;
        }
        if (m_delayed_pending && (message.query_async_slot() == 44))
        {
            services::ServiceReply reply;
            if (m_delayed_done || !message.copy_payload_to(reply) || (reply.route.value != m_primary.route.value) ||
                (reply.request.value != 5u) || (reply.opcode != 2u) || (reply.notice != services::EServiceNotice::completed) ||
                (reply.status != services::EServiceStatus::success))
            {
                return false;
            }
            m_delayed_done = true;
            return true;
        }
        if (m_stage == EStage::echoes)
        {
            services::ServiceReply reply;
            if (!message.copy_payload_to(reply) || (reply.route.value != m_primary.route.value) || !reply.request.value ||
                (reply.request.value > m_echo_posted) || (reply.opcode != 1u))
            {
                return false;
            }
            const unsigned index = static_cast<unsigned>(reply.request.value - 1u);
            if (message.query_async_slot() != (30 + static_cast<std::int32_t>(index)))
            {
                return false;
            }
            if (reply.notice == services::EServiceNotice::acknowledged)
            {
                if (m_echo_acknowledged[index] || (reply.status != services::EServiceStatus::success))
                {
                    return false;
                }
                m_echo_acknowledged[index] = true;
                return true;
            }
            if ((reply.notice != services::EServiceNotice::completed) || !m_echo_acknowledged[index] || m_echo_completed[index])
            {
                return false;
            }
            if (pump_failure())
            {
                if (reply.status != services::EServiceStatus::cancelled)
                {
                    return false;
                }
            }
            else if ((reply.status != services::EServiceStatus::success) || (reply.payload_size != 1u) || (reply.payload[0] != index))
            {
                return false;
            }
            m_echo_completed[index] = true;
            ++m_echo_complete;
            return true;
        }
        if (!m_sent || m_done || (message.query_async_slot() != m_slot))
        {
            return false;
        }
        if ((m_operation == EOperation::module) || (m_operation == EOperation::self))
        {
            modules::ModuleResult result;
            const module_ids::id_type expected_module = (m_operation == EOperation::self) ? module_ids::executive : provider_module();
            if (!message.copy_payload_to(result) || (result.module != expected_module))
            {
                return false;
            }
            if (!m_acknowledged)
            {
                m_acknowledged = (result.notice == modules::EModuleNotice::acknowledged) &&
                    (result.status == modules::EModuleStatus::success);
                return m_acknowledged;
            }
            if ((result.notice != modules::EModuleNotice::completed) || (result.status != m_module_status))
            {
                return false;
            }
        }
        else if (m_operation == EOperation::open)
        {
            services::OpenServiceResult result;
            const services::EServiceStatus expected = scenario("empty-provider") ?
                services::EServiceStatus::unsupported : services::EServiceStatus::success;
            if (!message.copy_payload_to(result) || (result.request.value != m_expected_request.value) || (result.status != expected))
            {
                return false;
            }
            if ((expected == services::EServiceStatus::success) && (!result.provider.value || !result.route.value))
            {
                return false;
            }
            *m_opened = result;
        }
        else if ((m_operation == EOperation::service) || (m_operation == EOperation::close))
        {
            services::ServiceReply reply;
            if (!message.copy_payload_to(reply) || (reply.route.value != m_expected_route.value) ||
                (reply.request.value != m_expected_request.value)) return false;
            if (m_operation == EOperation::service)
            {
                const services::EServiceStatus expected = m_acknowledged ? m_service_status : services::EServiceStatus::success;
                if ((reply.opcode != m_opcode) || (reply.status != expected))
                {
                    return false;
                }
                if (!m_acknowledged)
                {
                    m_acknowledged = reply.notice == services::EServiceNotice::acknowledged;
                    if (m_acknowledged && (m_stage == EStage::delayed))
                    {
                        m_delayed_pending = true;
                        m_delayed_deadline = m_deadline;
                        next(EStage::self_stop);
                        return true;
                    }
                    return m_acknowledged;
                }
                if (reply.notice != services::EServiceNotice::completed)
                {
                    return false;
                }
                if ((m_stage == EStage::second_echo) && ((reply.payload_size != 1u) || (reply.payload[0] != 33u)))
                {
                    return false;
                }
                if ((m_stage == EStage::provider_probe) && ((reply.payload_size != 1u) || (reply.payload[0] != 5u)))
                {
                    return false;
                }
                if (m_opcode == 12u)
                {
                    SSequenceProbe sequence;
                    if (reply.payload_size != sizeof(sequence)) return false;
                    std::memcpy(&sequence, reply.payload, sizeof(sequence));
                    if ((sequence.counter != system_context::sequence_counter) || (sequence.sequence != (m_sequence_expected + 1u)) ||
                        (system_context::sequence_counter->fetch_add(1u, std::memory_order_relaxed) != (m_sequence_expected + 2u)))
                    {
                        return false;
                    }
                }
            }
            else
            {
                if ((m_operation == EOperation::close) && (borrow_scenario() || notification_scenario() || modal_scenario()) &&
                    (reply.notice == services::EServiceNotice::completed) && (reply.status == services::EServiceStatus::busy))
                {
                    // Consumer retirement can precede a cancelled provider's
                    // late completion. Retry this settled close request while
                    // preserving the original operation deadline.
                    m_sent = false;
                    m_close_retry_pending = true;
                    if (!m_close_retried)
                    {
                        m_close_retried = true;
                        MV_LOG(info, "Pump fixture: route close waiting for late provider completion");
                    }
                    return m_close_retry.set_after_milliseconds(1u).code == platform::threading::EWaitError::none;
                }
                // Shutdown may retire the now-idle closing route before this
                // close arrives. Only this settled delayed-operation boundary
                // permits stale; ordinary route closure still requires success.
                const bool retired_after_stop = scenario("shutdown-pending") &&
                    (m_stage == EStage::closing_after_stop) && m_stopping && m_delayed_done &&
                    (reply.status == services::EServiceStatus::stale);
                if ((reply.notice != services::EServiceNotice::completed) ||
                    ((reply.status != m_service_status) && !retired_after_stop))
                {
                    return false;
                }
            }
        }
        else return false;
        m_done = true;
        return true;
    }
    bool advance() noexcept
    {
        if (m_stage == EStage::native)
        {
            m_finished = m_native.done();
            return true;
        }
        if (m_stage == EStage::report)
        {
            m_finished = m_report.done();
            return true;
        }
        if (scenario("schema-process") && (m_stage == EStage::self_stop) && m_stopping && !m_sent)
        {
            // This covers both next(self_stop)'s empty operation and a prepared
            // but transport-busy request. Stop retains the uncommitted asset's
            // disposal obligation; it must not publish a replacement afterward.
            m_owner = {};
            m_finished = m_schema.domain_complete();
            return true;
        }
        if (m_stage == EStage::schema)
        {
            if (m_stopping)
            {
                if (m_schema.domain_complete()) next(EStage::wait_stop);
            }
            else if (m_schema.ready()) next(EStage::self_stop);
            return true;
        }
        if (m_stage == EStage::modal)
        {
            if (m_modal.done()) next((m_stopping || scenario("modal-shutdown")) ? EStage::wait_stop : EStage::close);
            return true;
        }
        if (m_stage == EStage::borrow)
        {
            if (m_borrow.self_stop_due()) next(EStage::self_stop);
            else if (m_borrow.done()) next(m_stopping ? EStage::wait_stop : EStage::close);
            return true;
        }
        if (m_stage == EStage::notification)
        {
            if (m_notification.self_stop_due()) next(EStage::self_stop);
            else if (m_notification.done()) next(m_stopping ? EStage::wait_stop : EStage::close);
            return true;
        }
        if (m_stage == EStage::credits)
        {
            if (m_credits.ready_to_stop()) next(EStage::self_stop);
            return true;
        }
        if (m_stage == EStage::wait_stop)
        {
            if (m_stopping) m_finished = true;
            return true;
        }
        if (m_stage == EStage::echoes)
        {
            if (!m_timing && !start_deadline())
            {
                return false;
            }
            while (m_echo_posted != 3u)
            {
                services::ServiceRequest request;
                request.route = m_primary.route;
                request.request = { m_echo_posted + 1u };
                request.opcode = 1u;
                request.payload_size = 1u;
                request.payload[0] = static_cast<unsigned char>(m_echo_posted);
                const modules::ESubmission result = m_communication.submit({}, threading::make_pod_message(
                    (30 + static_cast<std::int32_t>(m_echo_posted)), request));
                if (result == modules::ESubmission::busy)
                {
                    return true;
                }
                if (!modules::accepted(result))
                {
                    return false;
                }
                ++m_echo_posted;
            }
            if (m_echo_complete == 3u) next(pump_failure() ? EStage::wait_stop : EStage::second_open);
            return true;
        }
        if (m_operation != EOperation::none)
        {
            if (m_operation == EOperation::self)
            {
                if (m_stopping && m_sent)
                {
                    if (!m_credits.quiescent() || !m_borrow.domain_complete() || !m_notification.domain_complete() ||
                        !m_modal.domain_complete() || !m_schema.domain_complete())
                    {
                        return true;
                    }
                    if (scenario("shutdown-pending"))
                    {
                        if (!m_delayed_done) return true;
                        MV_LOG(info, "Pump fixture: cleanup reply received after Executive exit request");
                        next(EStage::closing_after_stop);
                        return true;
                    }
                    m_finished = true;
                }
                return m_finished || publish();
            }
            if (!m_done) return publish();
            switch (m_stage)
            {
                case EStage::load:
                {
                    next(activation_failure() ? EStage::wait_stop : (rejected() ? EStage::self_stop :
                        ((m_replacement || native_scenario()) ? EStage::open : EStage::duplicate)));
                    if (m_replacement) MV_LOG(info, "Pump fixture: independent provider survived Executive replacement");
                    break;
                }
                case EStage::duplicate:
                {
                    ++m_shell_duplicates;
                    next((scenario("platform-shell") && (m_shell_duplicates < 2u)) ? EStage::duplicate : EStage::open);
                    break;
                }
                case EStage::open:
                {
                    next(scenario("empty-provider") ? EStage::unload : (borrow_scenario() ? EStage::borrow_start :
                        (notification_scenario() ? EStage::notification_start : (modal_scenario() ? EStage::modal_start :
                            (scenario("platform-shell") ? EStage::shell_first :
                                (native_scenario() ? EStage::native_start : EStage::busy_unload))))));
                    break;
                }
                case EStage::busy_unload:
                {
                    next(scenario("platform-shell") ? EStage::shell_final : EStage::echoes);
                    break;
                }
                case EStage::shell_first:
                {
                    next(EStage::shell_second);
                    break;
                }
                case EStage::shell_second:
                {
                    MV_LOG(info, "Pump fixture: Platform unsupported service commands completed and route remained usable");
                    next(EStage::busy_unload);
                    break;
                }
                case EStage::shell_final:
                {
                    next(EStage::close);
                    break;
                }
                case EStage::second_open:
                {
                    if (m_second.route.value == m_primary.route.value)
                    {
                        return false;
                    }
                    next(EStage::second_echo);
                    break;
                }
                case EStage::second_echo:
                {
                    next(EStage::second_close);
                    break;
                }
                case EStage::second_close:
                {
                    MV_LOG(info, "Pump fixture: simultaneous contracts preserved number and version");
                    next(EStage::provider_probe);
                    break;
                }
                case EStage::provider_probe:
                {
                    next(scenario("sequence") ? EStage::sequence_first : EStage::scenario_work);
                    break;
                }
                case EStage::sequence_first:
                {
                    system_context::sequence_counter->store((UINT64_MAX - 1u), std::memory_order_relaxed);
                    next(EStage::sequence_wrap);
                    break;
                }
                case EStage::sequence_wrap:
                {
                    next(EStage::scenario_work);
                    break;
                }
                case EStage::sequence_reload:
                {
                    MV_LOG(info, "Pump fixture: shared sequence pointer, wrap and reload passed");
                    next(EStage::stale_close);
                    break;
                }
                case EStage::scenario_work:
                {
                    if (scenario("emergency"))
                    {
                        debug_system::get_service()->request_shutdown(debug_system::EShutdownReason::critical_incident);
                        next(EStage::wait_stop);
                    }
                    else next(scenario("cleanup-credits") ? EStage::credit_control : EStage::self_stop);
                    break;
                }
                case EStage::credit_control:
                {
                    MV_LOG(info, "Pump fixture: zero-reply cancellation admitted with 512 reserved replies");
                    next(EStage::self_stop);
                    break;
                }
                case EStage::close:
                {
                    next(scenario("stop-timeout") ? EStage::self_stop : EStage::unload);
                    break;
                }
                case EStage::unload:
                {
                    if (scenario("platform-shell")) MV_LOG(info, "Pump fixture: Platform shell loaded on demand, routed and retired without a service thread");
                    if (scenario("deactivate-failed"))
                    {
                        MV_LOG(info, "Pump fixture: destroy refusal unload_failed received");
                        next(EStage::wait_stop);
                    }
                    else next((scenario("normal") || scenario("sequence")) ? EStage::reload : EStage::self_stop);
                    break;
                }
                case EStage::reload:
                {
                    next(EStage::fresh_open);
                    break;
                }
                case EStage::fresh_open:
                {
                    if ((m_fresh.provider.value == m_primary.provider.value) || (m_fresh.route.value == m_primary.route.value))
                    {
                        return false;
                    }
                    next(scenario("sequence") ? EStage::sequence_reload : EStage::stale_close);
                    break;
                }
                case EStage::closing_after_stop:
                {
                    m_finished = true;
                    break;
                }
                case EStage::stale_close:
                {
                    next(EStage::fresh_close);
                    break;
                }
                case EStage::fresh_close:
                {
                    next(EStage::final_unload);
                    break;
                }
                case EStage::final_unload:
                {
                    MV_LOG(info, "Pump fixture: stale route rejected after provider reload");
                    next(EStage::self_stop);
                    break;
                }
                default:
                {
                    return false;
                }
            }
            return true;
        }
        bool prepared = false;
        switch (m_stage)
        {
            case EStage::native_start:
            {
                if (m_stopping)
                {
                    m_finished = true;
                    return true;
                }
                CNativeExecution::EMode mode = CNativeExecution::EMode::move;
                if (scenario("platform-native-size")) mode = CNativeExecution::EMode::size;
                else if (scenario("platform-native-menu")) mode = CNativeExecution::EMode::menu;
                else if (scenario("platform-native-report")) mode = CNativeExecution::EMode::report;
                if (!m_native.start(m_primary, mode)) return false;
                next(EStage::native);
                return true;
            }
            case EStage::report_start:
            {
                if (m_stopping)
                {
                    m_finished = true;
                    return true;
                }
                CReportExecution::EMode mode = CReportExecution::EMode::snapshot;
                if (scenario("platform-report-cancel")) mode = CReportExecution::EMode::cancel;
                else if (scenario("platform-report-shutdown")) mode = CReportExecution::EMode::shutdown;
                else if (scenario("platform-report-departure")) mode = CReportExecution::EMode::departure;
                else if (scenario("platform-report-injected")) mode = CReportExecution::EMode::injected;
                if (!m_report.start(mode)) return false;
                next(EStage::report);
                return true;
            }
            case EStage::shell_first:
            case EStage::shell_second:
            case EStage::shell_final:
            {
                const bool final = m_stage == EStage::shell_final;
                const std::uint64_t request = final ? 20u : ((m_stage == EStage::shell_first) ? 18u : 19u);
                const std::int32_t slot = final ? 30 : static_cast<std::int32_t>(request);
                std::uint16_t opcode = 65535u;
#if !MV_PLATFORM_WINDOWS
                if (final) opcode = 16u;
#endif
                prepared = begin_service(m_primary.route, request, opcode, slot, services::EServiceStatus::unsupported);
                break;
            }
            case EStage::schema_start:
            {
                if (!m_schema.start()) return false;
                next(EStage::schema);
                return true;
            }
            case EStage::modal_start:
            {
                const CModalExecution::EMode mode = scenario("modal-full") ? CModalExecution::EMode::full :
                    (scenario("modal-shutdown") ? CModalExecution::EMode::shutdown : CModalExecution::EMode::normal);
                if (!m_modal.start(m_primary.route, mode))
                {
                    return false;
                }
                next(EStage::modal);
                return true;
            }
            case EStage::notification_start:
            {
                CNotificationExecution::EMode mode = CNotificationExecution::EMode::normal;
                if (scenario("notify-cancel")) mode = CNotificationExecution::EMode::cancel;
                else if (scenario("notify-abandoned") || (scenario("notify-replacement") && !m_replacement)) mode = CNotificationExecution::EMode::abandon;
                else if (scenario("notify-shutdown")) mode = CNotificationExecution::EMode::shutdown;
                if (!m_notification.start(m_primary.route, mode))
                {
                    return false;
                }
                next(EStage::notification);
                return true;
            }
            case EStage::borrow_start:
            {
                CBorrowExecution::EMode mode = CBorrowExecution::EMode::normal;
                if (scenario("borrow-cancel")) mode = CBorrowExecution::EMode::cancel;
                else if (scenario("borrow-abandoned")) mode = CBorrowExecution::EMode::abandon;
                else if (scenario("borrow-shutdown")) mode = CBorrowExecution::EMode::shutdown;
                else if (scenario("borrow-provider-failed")) mode = CBorrowExecution::EMode::provider_failure;
                if (!m_borrow.start(m_primary.route, mode))
                {
                    return false;
                }
                next(EStage::borrow);
                return true;
            }
            case EStage::load:
            {
                const modules::EModuleStatus expected = rejected() ? modules::EModuleStatus::function_unavailable :
                    (activation_failure() ? modules::EModuleStatus::installation_failed :
                        (m_replacement ? modules::EModuleStatus::already_loaded : modules::EModuleStatus::success));
                prepared = begin_module(modules::EModuleAction::load, expected);
                break;
            }
            case EStage::duplicate:
            {
                prepared = begin_module(modules::EModuleAction::load, modules::EModuleStatus::already_loaded);
                break;
            }
            case EStage::open:
            {
                prepared = begin_open(m_primary, 20, 1u, 1u, 1u);
                break;
            }
            case EStage::busy_unload:
            {
                prepared = begin_module(modules::EModuleAction::unload, modules::EModuleStatus::busy);
                break;
            }
            case EStage::second_open:
            {
                prepared = begin_open(m_second, 21, 2u, 3u, 2u);
                break;
            }
            case EStage::second_echo:
            {
                prepared = begin_service(m_second.route, 1u, 1u, 31);
                break;
            }
            case EStage::second_close:
            {
                prepared = begin_close(m_second.route, 2u, 22);
                break;
            }
            case EStage::provider_probe:
            {
                prepared = begin_service(m_primary.route, 4u, 10u, 40);
                break;
            }
            case EStage::scenario_work:
            {
                if (scenario("threaded-credits"))
                {
                    m_credits.start();
                    next(EStage::credits);
                    return true;
                }
                if (scenario("cleanup-credits")) prepared = begin_service(m_primary.route, 5u, 4u, 44);
                else if (scenario("inbox-full") || scenario("emergency")) prepared = begin_service(m_primary.route, 5u, 3u, 44);
                else if (scenario("shutdown-pending") || (scenario("replacement") && !m_replacement))
                {
                    next(EStage::delayed);
                    return true;
                }
                else
                {
                    next(EStage::close);
                    return true;
                }
                break;
            }
            case EStage::credit_control:
            {
                prepared = begin_service(m_primary.route, 6u, 11u, 41);
                break;
            }
            case EStage::close:
            {
                prepared = scenario("platform-shell") ? begin_close(m_primary.route, 21u, 31) : begin_close(m_primary.route, 7u, 50);
                break;
            }
            case EStage::unload:
            {
                prepared = begin_module(modules::EModuleAction::unload, scenario("deactivate-failed") ?
                    modules::EModuleStatus::unload_failed : modules::EModuleStatus::success);
                break;
            }
            case EStage::reload:
            {
                prepared = begin_module(modules::EModuleAction::load, modules::EModuleStatus::success);
                break;
            }
            case EStage::fresh_open:
            {
                prepared = begin_open(m_fresh, 60, 1u, 1u, 1u);
                break;
            }
            case EStage::stale_close:
            {
                prepared = begin_close(m_primary.route, 6u, 61, services::EServiceStatus::stale);
                break;
            }
            case EStage::fresh_close:
            {
                prepared = begin_close(m_fresh.route, 2u, 62);
                break;
            }
            case EStage::final_unload:
            {
                prepared = begin_module(modules::EModuleAction::unload, modules::EModuleStatus::success);
                break;
            }
            case EStage::self_stop:
            {
                prepared = begin_module(((scenario("replacement") || scenario("notify-replacement") || scenario("schema-process")) && !m_replacement) ? modules::EModuleAction::replace :
                    modules::EModuleAction::unload, modules::EModuleStatus::success, true);
                break;
            }
            case EStage::delayed:
            {
                prepared = begin_service(m_primary.route, 5u, 2u, 44);
                break;
            }
            case EStage::closing_after_stop:
            {
                prepared = begin_close(m_primary.route, 7u, 50);
                break;
            }
            case EStage::sequence_first:
            {
                prepared = begin_sequence(m_primary.route, 5u, 0u);
                break;
            }
            case EStage::sequence_wrap:
            {
                prepared = begin_sequence(m_primary.route, 6u, (UINT64_MAX - 1u));
                break;
            }
            case EStage::sequence_reload:
            {
                prepared = begin_sequence(m_fresh.route, 1u, 1u);
                break;
            }
            default:
            {
                return false;
            }
        }
        return prepared && publish();
    }
};

}   // namespace pump_tests
