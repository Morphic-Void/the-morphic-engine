
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    provider_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Pumped provider fixture: retained commands and common execution callbacks.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/services/provider_client.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/filesystem/filesystem_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "platform/threading/native_thread_id.hpp"
#include "system/system_context.hpp"
#include "debug/macros.hpp"
#include "containers/TInstance.hpp"
#include "platform/threading/processor_relax.hpp"
#include "tests/host_pump/fixture_types.hpp"
#include <cstdlib>
#include <cstring>

namespace pump_tests
{

class CProviderExecution
{
public:
    CProviderExecution(const modules::SExecutionContext& context, const char* const selected) noexcept
        : m_communication{ *context.communication }, m_services{ m_communication },
          m_execution{ context.execution }, m_instance{ context.instance }, m_endpoint{ context.endpoint }
    {
        const std::size_t size = std::strlen(selected);
        if (size < sizeof(m_selected)) std::memcpy(m_selected, selected, (size + 1u));
        else m_failed = true;
    }
    bool context_valid() const noexcept
    {
        return m_execution && m_execution->restriction.is_valid() && m_execution->notification.is_valid() &&
            (m_execution->instance.value == m_instance.value) && (m_execution->client.value == m_endpoint.value) &&
            (system_context::get_ambient_thread_id() == thread_ids::host) &&
            (system_context::get_ambient_module_id() == module_ids::rendering) && memory::get_ambient_memory_context();
    }
    bool complete() const noexcept
    {
        return m_stop && m_admission_closed && !m_count && m_services.idle() && !m_borrow.value &&
            !m_test_asset && !m_test_acquisition && (m_released == m_created) && (m_disposed == m_created) &&
            (!m_credits || m_credit_creation_complete) && !m_test_unsafe && !m_credit_unsafe &&
            (!scenario("mixed") || ((m_extra_retired + m_extra_refused) == m_extra_posted)) && m_stop_delay_complete;
    }
    modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        if (!context_valid())
        {
            return modules::EExecutionStatus::failed;
        }
        switch (invocation.event)
        {
            case modules::EExecutionEvent::start:
            {
                const bool valid = foreign_context_rejected();
                m_failed |= !valid;
                if (valid) MV_LOG(info, "Pump provider: activation context and foreign-thread rejection passed");
                if (scenario("modal-activation")) m_failed |= !restricted_activation();
                m_failed |= scenario("activation-failed");
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
                m_stop = true;
                if (scenario("stop-timeout"))
                {
                    m_stop_delay_complete = false;
                    m_failed |= m_stop_deadline.set_after_milliseconds(11000u).code != platform::threading::EWaitError::none;
                }
                if (scenario("activation-timeout"))
                {
                    m_failed |= invocation.notification.stop_reason != modules::EExecutionStop::creation_failure;
                    MV_LOG(info, "Pump provider: activation deadline initiated cooperative stop");
                }
                MV_LOG(info, "Pump provider: begin stop");
                break;
            }
            case modules::EExecutionEvent::provider_admission_closed:
            {
                m_admission_closed = true;
                break;
            }
            case modules::EExecutionEvent::service_async:
            {
                break;
            }
            case modules::EExecutionEvent::child_created:
            case modules::EExecutionEvent::child_ready:
            case modules::EExecutionEvent::child_ended:
            case modules::EExecutionEvent::child_retired:
            case modules::EExecutionEvent::child_stop_result:
            {
                if (!child_notice(invocation)) m_failed = true;
                break;
            }
            default:
            {
                m_failed = true;
                break;
            }
        }
        m_runnable = false;
        m_next_wake = UINT64_MAX;
        if (!m_stop && !m_ready && !scenario("activation-timeout") && (++m_starting >= 3u))
        {
            m_ready = true;
        }
        if (scenario("mixed")) m_failed |= !advance_children();
        if (m_stop && !m_admission_requested)
        {
            modules::SExecutionIntent close;
            close.kind = modules::SExecutionIntent::EKind::close_provider_admission;
            const modules::ESubmission result = m_communication.submit(close);
            if (modules::accepted(result)) m_admission_requested = true;
            else if (result != modules::ESubmission::busy) m_failed = true;
        }
        if (m_count)
        {
            if (!m_stop && (scenario("pump-failed") || scenario("invalid-submission")) && !m_failure_triggered)
            {
                if (scenario("invalid-submission"))
                {
                    // Private lifecycle control is no longer a public POD. An
                    // empty/untyped POD must be rejected without a capacity wait.
                    m_failed |= m_communication.submit({}, threading::CErasedPodMsg{}) != modules::ESubmission::invalid;
                }
                m_failure_triggered = m_failed = true;
            }
            else if (!advance_command()) m_failed = true;
        }
        m_failed |= !retire_borrow();
        if (m_credits) m_failed |= !advance_credits();
        if (m_stop && !m_stop_delay_complete)
        {
            std::uint64_t remaining = 0u;
            m_failed |= m_stop_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none;
            if (remaining) limit_wait(remaining);
            else
            {
                m_stop_delay_complete = true;
                MV_LOG(info, "Pump provider: live state survived shutdown grace expiry");
            }
        }
        progress = {};
        progress.ready = m_ready;
        progress.work_pending = m_runnable || (!m_ready && !m_stop && !scenario("activation-timeout"));
        progress.work_complete = complete();
        progress.next_wake_ns = m_next_wake;
        progress.stop_requested = (!m_stop && scenario("unauthorised-native-stop")) ? 1u : 0u;
        progress.native_events_active = (!m_stop && scenario("unauthorised-native-events")) ? 1u : 0u;
        if (complete() && !m_stopped_logged)
        {
            m_stopped_logged = true;
            MV_LOG(info, "Pump provider: stopped after continued progress");
        }
        if (m_failed && !m_reported)
        {
            m_reported = true;
            MV_LOG(info, "Pump provider: domain failure reported once");
            return modules::EExecutionStatus::failed;
        }
        return modules::EExecutionStatus::success;
    }

private:
    struct SNotifier
    {
        SNotificationData data;
        threading::SNotificationEndpoint endpoint;
        platform::threading::CThread publisher;
        std::atomic<bool> finish{ false }, done{ false }, failed{ false };
    };
    struct SExtra
    {
        services::SClientToken endpoint;
        platform::threading::CPlatformThreadId created_on, destroyed_on;
        unsigned destroyed{ 0u };
        bool outcome{ false }, created{ false }, ready{ false }, ended{ false }, retired{ false }, stop_sent{ false }, stopping{ false };
    };
    struct SForeignCall
    {
        const modules::SHostExecutionContext* execution;
        modules::CCommunication communication;
        modules::ERestrictionStatus restriction{ modules::ERestrictionStatus::success };
        modules::EMaintenanceStatus maintenance{ modules::EMaintenanceStatus::success };
        modules::ESubmission submission{ modules::ESubmission::accepted };
    };
    modules::CCommunication m_communication;
    services::CProviderClient m_services;
    const modules::SHostExecutionContext* const m_execution;
    const modules::SModuleInstance m_instance;
    const services::SClientToken m_endpoint;
    services::SDispatchedCommand m_commands[256];
    services::ServiceReply m_reply;
    threading::CErasedOwnerMsg m_test_owner;
    CAssetId m_test_asset;
    CAssetAcquisitionId m_test_acquisition;
    TInstance<unsigned> m_borrowed;
    TInstance<SNotifier> m_notifier;
    TInstance<SModalData> m_modal;
    SModalDescriptor m_modal_view;
    services::SServiceBorrow m_borrow;
    services::SClientToken m_notification_client;
    services::ServiceReply m_borrow_reply;
    bool m_borrow_done{ false }, m_borrow_retiring{ false }, m_borrow_finished{ false }, m_borrower_quiescent{ false };
    bool m_notification_unsafe{ false };
    bool m_publisher_started{ false };
    unsigned m_retirement_progress{ 0u };
    SExtra m_extra[2];
    unsigned m_extra_posted{ 0u }, m_extra_retired{ 0u }, m_extra_refused{ 0u };
    CAssetId m_assets[512];
    CAssetAcquisitionId m_acquisitions[512];
    bool m_disposal_seen[512]{}, m_release_seen[512]{};
    threading::CErasedOwnerMsg m_credit_owner;
    unsigned m_created{ 0u }, m_credit_phase{ 0u }, m_disposals_posted{ 0u }, m_releases_posted{ 0u };
    unsigned m_disposed{ 0u }, m_released{ 0u }, m_control_reposts{ 0u };
    bool m_credits{ false }, m_credit_sent{ false }, m_credits_ready{ false }, m_credits_drained{ false };
    bool m_credit_creation_complete{ false };
    bool m_test_unsafe{ false }, m_credit_unsafe{ false };
    platform::threading::CWaitDeadline m_delay;
    platform::threading::CWaitDeadline m_stop_deadline;
    bool m_stop_delay_complete{ true };
    char m_selected[64]{};
    unsigned m_head{ 0u }, m_count{ 0u }, m_starting{ 0u }, m_command_phase{ 0u };
    unsigned m_test_phase{ 0u }, m_flooded{ 0u }, m_flood_received{ 0u };
    bool m_test_sent{ false }, m_module_acknowledged{ false };
    bool m_ready{ false }, m_stop{ false }, m_admission_requested{ false }, m_admission_closed{ false };
    bool m_failed{ false }, m_reported{ false }, m_failure_triggered{ false }, m_stopped_logged{ false }, m_runnable{ false };
    std::uint64_t m_next_wake{ UINT64_MAX };

    bool scenario(const char* const name) const noexcept { return std::strcmp(m_selected, name) == 0; }
    void limit_wait(const std::uint64_t delay) noexcept
    {
        if (delay < m_next_wake) m_next_wake = delay;
    }
    static std::uint32_t MV_STD_ABI_CALL foreign_call(void* const object) noexcept
    {
        SForeignCall& call = *static_cast<SForeignCall*>(object);
        const modules::SHostExecutionContext& execution = *call.execution;
        call.restriction = execution.enter_restriction(execution.native_context, { 0u }).status;
        call.maintenance = execution.maintain_host(execution.native_context).status;
        call.submission = call.communication.submit({}, threading::make_pod_message(90, AssetDisposeRequest{}));
        return 0u;
    }
    bool foreign_context_rejected() noexcept
    {
        SForeignCall call{ m_execution, m_communication };
        platform::threading::CThread thread;
        if (!thread.create(&foreign_call, &call))
        {
            return false;
        }
        if (!thread.join_and_close())
        {
            MV_LOG(error, "Pump fixture: foreign callback lifetime could not be joined");
            std::abort();
        }
        return (call.restriction == modules::ERestrictionStatus::invalid_context) &&
            (call.maintenance == modules::EMaintenanceStatus::invalid_context) &&
            (call.submission == modules::ESubmission::invalid);
    }
    bool receive(const threading::CErasedPodMsg& message) noexcept
    {
        services::ProviderOperationResult operation;
        services::ServiceCommand command;
        if (message.copy_payload_to(operation))
        {
            return m_services.receive(message);
        }
        if (message.copy_payload_to(command))
        {
            if (!command.operation || !command.descriptor || (m_count == 256u))
            {
                return false;
            }
            m_commands[(m_head + m_count) & 255u] = *command.descriptor;
            ++m_count;
            return true;
        }
        if (message.is_payload_a<services::ServiceRequestCancelled>())
        {
            return true;
        }
        services::ServiceBorrowControl control;
        if (message.copy_payload_to(control))
        {
            if (!m_borrow.value || (control.borrow.value != m_borrow.value))
            {
                return false;
            }
            if (control.control == services::EServiceBorrowControl::consumer_quiescent)
            {
                m_borrower_quiescent = true;
                MV_LOG(info, "Pump provider: consumer quiescence received; draining publisher activity");
            }
            return true;
        }
        if (message.query_async_slot() == 8801)
        {
            AssetDisposeResult result;
            if (!m_modal || !message.copy_payload_to(result) || (result.status != EAssetStatus::invalid_asset))
            {
                return false;
            }
            m_modal->ready.store(1u, std::memory_order_release);
            return true;
        }
        if (m_credits && ((message.query_async_slot() == 210) || (message.query_async_slot() == 211) ||
            ((message.query_async_slot() >= 1000) && (message.query_async_slot() < 2024))))
        {
            const bool valid = credit_message(message);
            if (!valid)
            {
                // Malformed identity/custody or unproven release/disposal is
                // fatal retention, not an obligation awaiting a second result.
                m_credit_sent = false;
                m_credit_unsafe = true;
            }
            return valid;
        }
        if (message.query_async_slot() == 200)
        {
            AssetDisposeResult result;
            if (!message.copy_payload_to(result) || result.asset || (result.status != EAssetStatus::invalid_asset) ||
                (m_flood_received == m_flooded))
            {
                return false;
            }
            ++m_flood_received;
            return true;
        }
        if (!m_test_sent || (message.query_async_slot() != (100 + static_cast<std::int32_t>(m_test_phase))))
        {
            return false;
        }
        switch (m_test_phase)
        {
            case 1u:
            {
                filesystem::FilesystemRefreshResult result;
                if (!message.copy_payload_to(result) || (result.status != filesystem::EFilesystemStatus::success))
                {
                    return false;
                }
                break;
            }
            case 2u:
            {
                AssetResult result;
                if (!message.copy_payload_to(result) || ((result.status == EAssetStatus::success) != static_cast<bool>(result.asset)))
                {
                    return unsafe_test();
                }
                if (result.status != EAssetStatus::success)
                {
                    m_test_sent = false;
                    m_test_phase = 8u;
                    m_reply.status = services::EServiceStatus::cancelled;
                    m_failed = true;
                    return true;
                }
                m_test_asset = result.asset;
                break;
            }
            case 3u:
            {
                AssetAcquireResult result;
                if (!message.copy_payload_to(result) || ((result.status == EAssetStatus::success) != static_cast<bool>(result.acquisition)))
                {
                    return unsafe_test();
                }
                if (result.status != EAssetStatus::success)
                {
                    m_test_sent = false;
                    m_test_phase = 5u;
                    m_reply.status = services::EServiceStatus::cancelled;
                    m_failed = true;
                    return true;
                }
                m_test_acquisition = result.acquisition;
                break;
            }
            case 4u:
            {
                AssetReleaseResult result;
                if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success) ||
                    (result.acquisition != m_test_acquisition))
                {
                    return unsafe_test();
                }
                m_test_acquisition = {};
                break;
            }
            case 5u:
            {
                AssetDisposeResult result;
                if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success) || (result.asset != m_test_asset))
                {
                    return unsafe_test();
                }
                m_test_asset = {};
                break;
            }
            case 6u:
            case 7u:
            {
                modules::ModuleResult result;
                const module_ids::id_type target = (m_test_phase == 6u) ? module_ids::rendering : module_ids::executive;
                if (!message.copy_payload_to(result) || (result.module != target))
                {
                    return false;
                }
                if (!m_module_acknowledged)
                {
                    m_module_acknowledged = (result.notice == modules::EModuleNotice::acknowledged) &&
                        (result.status == modules::EModuleStatus::success);
                    return m_module_acknowledged;
                }
                if ((result.notice != modules::EModuleNotice::completed) || (result.status != modules::EModuleStatus::invalid_request))
                {
                    return false;
                }
                m_module_acknowledged = false;
                break;
            }
            default:
            {
                return false;
            }
        }
        m_test_sent = false;
        ++m_test_phase;
        return true;
    }
    bool advance_test() noexcept
    {
        if (m_test_unsafe) return true;
        if (m_test_sent) return true;
        if (m_stop)
        {
            // Stop cancels only unsubmitted work. A transferred asset still
            // needs disposal; an accepted acquisition still needs its release.
            if ((m_test_phase <= 2u) || (m_test_phase >= 6u))
            {
                m_test_owner = {};
                m_test_phase = 8u;
            }
            else if (m_test_phase == 3u) m_test_phase = 5u;
            m_reply.status = services::EServiceStatus::cancelled;
        }
        if (m_test_phase == 8u)
        {
            m_reply.payload_size = 1u;
            m_reply.payload[0] = 5u;
            if (m_reply.status == services::EServiceStatus::success)
            {
                MV_LOG(info, "Pump provider: refresh, assets, acquisitions and requester restrictions passed");
            }
            m_command_phase = 2u;
            return true;
        }
        const std::int32_t slot = 100 + static_cast<std::int32_t>(m_test_phase);
        threading::CErasedPodMsg pod;
        switch (m_test_phase)
        {
            case 1u:
            {
                if (!m_test_owner.has_owner())
                {
                    m_test_owner = threading::make_owner_message<filesystem::FilesystemRefreshRequest>(slot);
                    filesystem::FilesystemRefreshRequest* const request = m_test_owner.owner().payload<filesystem::FilesystemRefreshRequest>();
                    if (!request || !request->root.set("dev-source:"))
                    {
                        return false;
                    }
                }
                break;
            }
            case 2u:
            {
                if (!m_test_owner.has_owner())
                {
                    m_test_owner = threading::make_owner_message<RawAssetTransfer>(slot);
                    RawAssetTransfer* const request = m_test_owner.owner().payload<RawAssetTransfer>();
                    if (!request || !request->storage.value.resize(32u, 32u))
                    {
                        return false;
                    }
                }
                break;
            }
            case 3u:
            {
                pod = threading::make_pod_message(slot, AssetAcquireRequest{ m_test_asset });
                break;
            }
            case 4u:
            {
                pod = threading::make_pod_message(slot, AssetReleaseRequest{ m_test_acquisition });
                break;
            }
            case 5u:
            {
                pod = threading::make_pod_message(slot, AssetDisposeRequest{ m_test_asset });
                break;
            }
            case 6u:
            case 7u:
            {
                if (!m_test_owner.has_owner())
                {
                    m_test_owner = threading::make_owner_message<modules::ModuleRequest>(slot);
                    modules::ModuleRequest* const request = m_test_owner.owner().payload<modules::ModuleRequest>();
                    if (!request) return false;
                    request->module = (m_test_phase == 6u) ? module_ids::rendering : module_ids::executive;
                    request->action = modules::EModuleAction::unload;
                }
                break;
            }
            default:
            {
                return false;
            }
        }
        const modules::ESubmission result = m_test_owner.has_owner() ?
            m_communication.submit({}, std::move(m_test_owner)) : m_communication.submit({}, pod);
        if (modules::accepted(result)) m_test_sent = true;
        else if (result != modules::ESubmission::busy) return false;
        return true;
    }
    bool unsafe_test() noexcept
    {
        m_test_sent = false;
        m_test_unsafe = true;
        return false;
    }
    bool advance_command() noexcept
    {
        const services::SDispatchedCommand& dispatched = m_commands[m_head];
        const services::ServiceRequest& command = dispatched.command;
        if (!m_command_phase)
        {
            m_reply = {};
            m_reply.route = command.route;
            m_reply.request = command.request;
            m_reply.opcode = command.opcode;
            m_reply.status = m_failure_triggered ? services::EServiceStatus::cancelled : services::EServiceStatus::success;
            m_reply.payload_size = command.payload_size;
            std::memcpy(m_reply.payload, command.payload, sizeof(command.payload));
            if (dispatched.contract.number == 2u) m_reply.payload[0] = 33u;
            if (command.opcode == 12u)
            {
                const SSequenceProbe sequence{ system_context::sequence_counter, system_context::sequence_counter->fetch_add(1u) };
                m_reply.payload_size = sizeof(sequence);
                std::memcpy(m_reply.payload, &sequence, sizeof(sequence));
            }
            if ((command.opcode == 2u) && (m_delay.set_after_milliseconds(100u).code != platform::threading::EWaitError::none))
            {
                return false;
            }
            if (command.opcode == 10u) m_test_phase = 1u;
            m_command_phase = 1u;
        }
        if ((command.opcode == 10u) && (m_command_phase == 1u))
        {
            if (!advance_test()) return false;
            if (m_command_phase == 1u) return true;
        }
        else if (command.opcode == 2u)
        {
            std::uint64_t remaining = 0u;
            if (m_delay.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
            {
                return false;
            }
            if (remaining)
            {
                limit_wait(remaining);
                return true;
            }
        }
        else if (command.opcode == 3u)
        {
            for (unsigned count = 0u; (count < 64u) && (m_flooded < 1024u); ++count)
            {
                const modules::ESubmission result = m_communication.submit({}, threading::make_pod_message(200, AssetDisposeRequest{}));
                if (result == modules::ESubmission::busy)
                {
                    return true;
                }
                if (!modules::accepted(result))
                {
                    return false;
                }
                ++m_flooded;
            }
            if (m_flooded != 1024u)
            {
                m_runnable = true;
                return true;
            }
        }
        else if ((command.opcode == 5u) || (command.opcode == 6u) || (command.opcode == 8u))
        {
            return advance_borrow(dispatched);
        }
        else if (command.opcode == 7u) return advance_notification(dispatched);
        else if (command.opcode == 9u)
        {
            if (m_command_phase == 1u)
            {
                if (!m_modal) return false;
                if (!m_modal->ready.load(std::memory_order_acquire))
                {
                    return true;
                }
                if (!native_modal()) return false;
                m_command_phase = 2u;
            }
        }
        else if (command.opcode == 4u)
        {
            m_credits = true;
            if (!m_credits_ready) return true;
        }
        else if (command.opcode == 11u)
        {
            if (!m_credits_ready || (m_disposals_posted != 512u))
            {
                return false;
            }
            for (unsigned count = 0u; (count < 64u) && (m_control_reposts < 1024u); ++count)
            {
                const modules::ESubmission result = m_communication.submit({},
                    threading::make_pod_message(-1, services::CancelServiceRequest{}));
                if (result == modules::ESubmission::busy)
                {
                    return true;
                }
                if (!modules::accepted(result))
                {
                    return false;
                }
                ++m_control_reposts;
            }
            if (m_control_reposts != 1024u)
            {
                m_runnable = true;
                return true;
            }
        }
        else if ((command.opcode != 1u) && (command.opcode != 10u) && (command.opcode != 12u))
        {
            return false;
        }
        const services::EServiceStatus status = m_services.complete(&m_reply);
        if (status == services::EServiceStatus::busy)
        {
            return true;
        }
        if (status != services::EServiceStatus::success)
        {
            return false;
        }
        if (command.opcode == 2u) MV_LOG(info, "Pump provider: delayed operation completed");
        if (command.opcode == 3u)
        {
            if (m_flood_received != 1024u) return false;
            MV_LOG(info, "Pump provider: bounded inbox and outbound traffic exercised");
        }
        pop_command();
        return true;
    }
    void pop_command() noexcept
    {
        m_commands[m_head] = {};
        m_head = (m_head + 1u) & 255u;
        --m_count;
        m_command_phase = 0u;
        m_runnable = m_count != 0u;
    }
    static std::uint32_t MV_STD_ABI_CALL publish_notifications(void* const object) noexcept
    {
        SNotifier& notifier = *static_cast<SNotifier*>(object);
        notifier.data.value.store(1u, std::memory_order_release);
        if (!notifier.endpoint.signal()) notifier.failed.store(true, std::memory_order_release);
        while (!notifier.finish.load(std::memory_order_acquire)) platform::threading::processor_relax();
        notifier.data.value.store(2u, std::memory_order_release);
        if (!notifier.endpoint.signal()) notifier.failed.store(true, std::memory_order_release);
        notifier.done.store(true, std::memory_order_release);
        return 0u;
    }
    bool advance_borrow(const services::SDispatchedCommand& dispatched) noexcept
    {
        const services::ServiceRequest& command = dispatched.command;
        if (m_command_phase == 1u)
        {
            services::SServiceView view;
            if (command.opcode == 5u)
            {
                if (!m_borrowed && !m_borrowed.emplace(4242u))
                {
                    return false;
                }
                view = { m_borrowed.operator->(), sizeof(unsigned), 1u };
            }
            else if (command.opcode == 6u)
            {
                if (!m_notifier && !m_notifier.emplace())
                {
                    return false;
                }
                view = { &m_notifier->data, sizeof(SNotificationData), 1u };
            }
            else
            {
                if (!m_modal && !m_modal.emplace())
                {
                    return false;
                }
                m_modal_view.state = &*m_modal;
                view = { &m_modal_view, sizeof(SModalDescriptor), 1u };
            }
            const services::SServiceBorrowOffer offer{ command.route, command.request, view };
            const services::EServiceStatus status = m_services.prepare_borrow(&offer, &m_borrow);
            if (status == services::EServiceStatus::busy)
            {
                return true;
            }
            if (status != services::EServiceStatus::success)
            {
                return false;
            }
            m_notification_client = dispatched.client;
            m_borrow_reply = m_reply;
            m_borrow_done = m_borrower_quiescent = m_borrow_retiring = m_borrow_finished = false;
            m_retirement_progress = 0u;
            m_command_phase = 2u;
        }
        if (m_command_phase == 2u)
        {
            if (command.opcode == 8u)
            {
                const modules::ESubmission result = m_communication.submit({}, threading::make_pod_message(8801, AssetDisposeRequest{}));
                if (result == modules::ESubmission::busy)
                {
                    return true;
                }
                if (!modules::accepted(result))
                {
                    return false;
                }
            }
            m_command_phase = 3u;
        }
        if ((command.opcode != 6u) && !scenario("borrow-cancel"))
        {
            const services::EServiceStatus status = m_services.complete(&m_borrow_reply);
            if (status == services::EServiceStatus::busy)
            {
                return true;
            }
            if (status != services::EServiceStatus::success)
            {
                return false;
            }
            m_borrow_done = true;
        }
        pop_command();
        if (scenario("borrow-provider-failed")) m_failed = true;
        return true;
    }
    bool advance_notification(const services::SDispatchedCommand& dispatched) noexcept
    {
        if (m_notification_unsafe) return true;
        SNotificationReady ready;
        if (!m_notifier || (dispatched.command.payload_size != sizeof(ready)) ||
            (dispatched.client.value != m_notification_client.value))
        {
            return false;
        }
        std::memcpy(&ready, dispatched.command.payload, sizeof(ready));
        if (m_command_phase == 1u)
        {
            const services::EServiceStatus status = m_services.bind_borrow_notification(ready.borrow, ready.registration, &m_notifier->endpoint);
            if (status == services::EServiceStatus::busy)
            {
                return true;
            }
            if ((status > services::EServiceStatus::delivery_failed) || m_notifier->publisher.is_valid() ||
                ((status == services::EServiceStatus::success) && !m_notifier->endpoint.is_valid()))
            {
                // Unknown binding/native state cannot establish safe cleanup.
                m_notification_unsafe = m_failed = true;
                return true;
            }
            if (status != services::EServiceStatus::success)
            {
                // A terminal binding refusal settles this accepted command;
                // retaining phase 1 would block the borrow's own retirement.
                m_reply.status = m_borrow_reply.status = services::EServiceStatus::cancelled;
                if (!m_stop && !m_borrower_quiescent) m_failed = true;
            }
            else if (!m_notifier->publisher.create(&publish_notifications, &*m_notifier))
            {
                // CThread was invalid before create; failure leaves it invalid
                // and starts no native thread on either supported backend.
                m_reply.status = m_borrow_reply.status = services::EServiceStatus::cancelled;
                m_failed = true;
            }
            else m_publisher_started = true;
            m_command_phase = 2u;
        }
        if (m_command_phase == 2u)
        {
            const services::EServiceStatus status = m_services.complete(&m_reply);
            if (status == services::EServiceStatus::busy)
            {
                return true;
            }
            if (status != services::EServiceStatus::success)
            {
                return false;
            }
            m_command_phase = 3u;
        }
        if (!scenario("notify-cancel"))
        {
            const services::EServiceStatus status = m_services.complete(&m_borrow_reply);
            if (status == services::EServiceStatus::busy)
            {
                return true;
            }
            if (status != services::EServiceStatus::success)
            {
                return false;
            }
            m_borrow_done = true;
        }
        pop_command();
        return true;
    }
    bool retire_borrow() noexcept
    {
        if (!m_borrow.value) return true;
        // A command can own an accepted completion or notification binding. Let
        // its named state settle before retiring the same borrow operation.
        if (m_count)
        {
            const unsigned opcode = m_commands[m_head].command.opcode;
            if ((opcode >= 5u) && (opcode <= 9u))
            {
                return true;
            }
        }
        if (m_stop && !m_borrow_retiring)
        {
            const services::EServiceStatus status = m_services.begin_borrow_retirement(m_borrow);
            if (status == services::EServiceStatus::success) m_borrow_retiring = true;
            else if (status != services::EServiceStatus::busy) return false;
        }
        if (!m_borrower_quiescent || (m_stop && !m_borrow_retiring))
        {
            return true;
        }
        if (m_notifier && m_publisher_started)
        {
            m_notifier->finish.store(true, std::memory_order_release);
            if (!m_notifier->done.load(std::memory_order_acquire))
            {
                limit_wait(1000000u);
                return true;
            }
            if (!m_notifier->publisher.join_and_close() || m_notifier->failed.load())
            {
                return false; // Retain the object if native lifetime is unproven.
            }
            m_publisher_started = false;
            MV_LOG(info, "Pump provider: final notification signal drained after consumer quiescence");
        }
        if (m_retirement_progress < 3u)
        {
            ++m_retirement_progress;
            m_runnable = true;
            return true;
        }
        if (!m_borrow_finished)
        {
            // Consumer quiescence already proves closure. Stay in the finish
            // phase if stop arrives while its acknowledgement or the later
            // completion is pending; beginning again can target a retired ID.
            m_borrow_retiring = true;
            const services::EServiceStatus status = m_services.finish_borrow_retirement(m_borrow);
            if (status == services::EServiceStatus::busy)
            {
                return true;
            }
            if (status != services::EServiceStatus::success)
            {
                return false;
            }
            m_borrow_finished = true;
        }
        if (!m_borrow_done)
        {
            const services::EServiceStatus status = m_services.complete(&m_borrow_reply);
            if (status == services::EServiceStatus::busy)
            {
                return true;
            }
            if (status != services::EServiceStatus::success)
            {
                return false;
            }
            m_borrow_done = true;
        }
        m_borrow = {};
        m_borrowed.reset();
        m_notifier.reset();
        if (m_modal) MV_LOG(info, "Pump provider: modal borrow retired after invocation and quiescence");
        m_modal_view = {};
        m_modal.reset();
        MV_LOG(info, "Pump provider: provider-owned storage destroyed after publisher drain");
        return true;
    }
    bool restricted_activation() noexcept
    {
        const modules::SHostExecutionContext& execution = *m_execution;
        const auto scope = execution.enter_restriction(execution.native_context, { 3u });
        platform::threading::CWaitDeadline hold;
        if ((scope.status != modules::ERestrictionStatus::success) ||
            (hold.set_after_milliseconds(5500u).code != platform::threading::EWaitError::none))
        {
            return false;
        }
        for (;;)
        {
            std::uint64_t remaining = 0u;
            if (hold.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none)
            {
                return false;
            }
            if (!remaining) break;
            const auto pass = execution.maintain_host(execution.native_context);
            if ((pass.status != modules::EMaintenanceStatus::success) || (pass.operations > 32u) || pass.cancel_native)
            {
                return false;
            }
            platform::threading::processor_relax();
        }
        if (execution.leave_restriction(execution.native_context, scope.scope) != modules::ERestrictionStatus::success)
        {
            return false;
        }
        MV_LOG(info, "Pump provider: activation excluded simulated restriction beyond five seconds");
        return true;
    }
    bool native_modal() noexcept
    {
        const modules::SHostExecutionContext& execution = *m_execution;
        void* const native = execution.native_context;
        const auto a = execution.enter_restriction(native, { 0u });
        const auto b = execution.enter_restriction(native, { 0u });
        const auto c = execution.enter_restriction(native, { 1u });
        if ((a.status != modules::ERestrictionStatus::success) || (b.status != modules::ERestrictionStatus::success) ||
            (c.status != modules::ERestrictionStatus::success))
        {
            return false;
        }
        m_modal->entered.store(1u, std::memory_order_release);
        platform::threading::CWaitDeadline hold;
        if (hold.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
        {
            return false;
        }
        const services::ServiceReply retained = m_reply;
        for (;;)
        {
            const auto pass = execution.maintain_host(native);
            if ((pass.status != modules::EMaintenanceStatus::success) || (pass.operations > 32u) || !context_valid())
            {
                return false;
            }
            m_modal->passes.fetch_add(1u, std::memory_order_release);
            if (pass.capacity_pressure) m_modal->pressure.store(1u, std::memory_order_release);
            if ((retained.route.value != m_reply.route.value) || (retained.request.value != m_reply.request.value))
            {
                return false;
            }
            if (m_modal->leave.load(std::memory_order_acquire) || pass.cancel_native)
            {
                break;
            }
            std::uint64_t remaining = 0u;
            if ((hold.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                return false;
            }
            platform::threading::processor_relax();
        }
        for (unsigned count = 0u; count < 64u; ++count) (void)execution.maintain_host(native);
        if ((execution.leave_restriction(native, a.scope) != modules::ERestrictionStatus::success) ||
            (execution.leave_restriction(native, a.scope) != modules::ERestrictionStatus::invalid_scope) ||
            (execution.leave_restriction(native, c.scope) != modules::ERestrictionStatus::success) ||
            (execution.leave_restriction(native, b.scope) != modules::ERestrictionStatus::success))
        {
            return false;
        }
        MV_LOG(info, "Pump provider: nested modal maintenance preserved accepted command state");
        return true;
    }
    bool credit_message(const threading::CErasedPodMsg& message) noexcept
    {
        const std::int32_t slot = message.query_async_slot();
        if ((slot == 210) || (slot == 211))
        {
            if (!m_credit_sent || (m_created >= 512u))
            {
                return false;
            }
            if ((slot == 210) && (m_credit_phase == 0u))
            {
                AssetResult result;
                if (!message.copy_payload_to(result) || ((result.status == EAssetStatus::success) != static_cast<bool>(result.asset)))
                {
                    return false;
                }
                if (result.status != EAssetStatus::success)
                {
                    m_credit_sent = false;
                    m_credit_creation_complete = true;
                    m_reply.status = services::EServiceStatus::cancelled;
                    m_failed = true;
                    return true;
                }
                m_assets[m_created] = result.asset;
                m_credit_phase = 1u;
            }
            else if ((slot == 211) && (m_credit_phase == 1u))
            {
                AssetAcquireResult result;
                if (!message.copy_payload_to(result) || ((result.status == EAssetStatus::success) != static_cast<bool>(result.acquisition)))
                {
                    return false;
                }
                m_acquisitions[m_created++] = result.acquisition;
                m_credit_phase = 0u;
                if (result.status != EAssetStatus::success)
                {
                    m_credit_creation_complete = true;
                    m_reply.status = services::EServiceStatus::cancelled;
                    m_failed = true;
                }
            }
            else return false;
            m_credit_sent = false;
            return true;
        }
        if ((slot >= 1000) && (slot < 1512))
        {
            const unsigned index = static_cast<unsigned>(slot - 1000);
            AssetDisposeResult result;
            if ((index >= m_disposals_posted) || m_disposal_seen[index] || !message.copy_payload_to(result) ||
                (result.status != EAssetStatus::success) || (result.asset != m_assets[index]))
            {
                return false;
            }
            m_disposal_seen[index] = true;
            ++m_disposed;
            return true;
        }
        if ((slot >= 1512) && (slot < 2024))
        {
            const unsigned index = static_cast<unsigned>(slot - 1512);
            AssetReleaseResult result;
            if ((index >= m_releases_posted) || m_release_seen[index] || !message.copy_payload_to(result) ||
                (result.status != EAssetStatus::success) || (result.acquisition != m_acquisitions[index]))
            {
                return false;
            }
            m_release_seen[index] = true;
            ++m_released;
            return true;
        }
        return false;
    }
    bool advance_credits() noexcept
    {
        if (m_credit_unsafe) return true;
        if (m_created == 512u) m_credit_creation_complete = true;
        if (!m_credit_creation_complete && m_stop && !m_credit_sent)
        {
            m_credit_owner = {};
            // A transfer result may have arrived before its acquire could be
            // posted. Retain that asset for disposal without inventing a borrow.
            if (m_credit_phase == 1u) ++m_created;
            m_credit_phase = 0u;
            m_credit_creation_complete = true;
            m_reply.status = services::EServiceStatus::cancelled;
        }
        if (!m_credit_creation_complete)
        {
            if (m_credit_sent) return true;
            modules::ESubmission result;
            if (!m_credit_phase)
            {
                if (!m_credit_owner.has_owner())
                {
                    m_credit_owner = threading::make_owner_message<RawAssetTransfer>(210);
                    RawAssetTransfer* const request = m_credit_owner.owner().payload<RawAssetTransfer>();
                    if (!request || !request->storage.value.resize(32u, 32u))
                    {
                        return false;
                    }
                }
                result = m_communication.submit({}, std::move(m_credit_owner));
            }
            else result = m_communication.submit({}, threading::make_pod_message(211, AssetAcquireRequest{ m_assets[m_created] }));
            if (modules::accepted(result)) m_credit_sent = true;
            else if (result != modules::ESubmission::busy) return false;
            return true;
        }
        for (unsigned count = 0u; (count < 64u) && (m_disposals_posted < m_created); ++count)
        {
            const modules::ESubmission result = m_communication.submit({}, threading::make_pod_message(
                (1000 + static_cast<std::int32_t>(m_disposals_posted)), AssetDisposeRequest{ m_assets[m_disposals_posted] }));
            if (result == modules::ESubmission::busy)
            {
                return true;
            }
            if (!modules::accepted(result)) return false;
            ++m_disposals_posted;
        }
        if (m_disposals_posted != m_created)
        {
            m_runnable = true;
            return true;
        }
        if (!m_credits_ready)
        {
            m_credits_ready = true;
            m_runnable = true;
            if (m_created == 512u) MV_LOG(info, "Pump provider: 512 disposal credits retained by live acquisitions");
        }
        if (!m_stop) return true;
        for (unsigned count = 0u; (count < 64u) && (m_releases_posted < m_created); ++count)
        {
            if (!m_acquisitions[m_releases_posted])
            {
                ++m_releases_posted;
                ++m_released;
                continue;
            }
            const modules::ESubmission result = m_communication.submit({}, threading::make_pod_message(
                (1512 + static_cast<std::int32_t>(m_releases_posted)), AssetReleaseRequest{ m_acquisitions[m_releases_posted] }));
            if (result == modules::ESubmission::busy)
            {
                return true;
            }
            if (!modules::accepted(result)) return false;
            ++m_releases_posted;
        }
        if (m_releases_posted != m_created) m_runnable = true;
        if ((m_released == m_created) && (m_disposed == m_created) && !m_credits_drained)
        {
            m_credits_drained = true;
            if (m_created == 512u) MV_LOG(info, "Pump provider: 512 retained disposals completed through common FIFO settlement");
        }
        return true;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL child_create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        if (!context || !runtime || !context->creation_context)
        {
            return modules::EExecutionStatus::invalid_argument;
        }
        *runtime = context->creation_context;
        static_cast<SExtra*>(*runtime)->created_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL child_handle(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        SExtra& child = *static_cast<SExtra*>(runtime);
        if (invocation->event == modules::EExecutionEvent::stop) child.stopping = true;
        *progress = {};
        progress->ready = true;
        progress->work_complete = child.stopping;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL child_destroy(void* const runtime) noexcept
    {
        SExtra& child = *static_cast<SExtra*>(runtime);
        child.destroyed_on = platform::threading::query_current_thread_id();
        ++child.destroyed;
        return (child.stopping && (child.created_on == child.destroyed_on) && (child.destroyed == 1u)) ?
            modules::EExecutionStatus::success : modules::EExecutionStatus::failed;
    }
    bool child_notice(const modules::SInvocation& invocation) noexcept
    {
        const modules::SExecutionNotification& notice = invocation.notification;
        if (invocation.event == modules::EExecutionEvent::child_created)
        {
            if ((notice.correlation < 40) || (notice.correlation >= 42))
            {
                return false;
            }
            SExtra& child = m_extra[notice.correlation - 40];
            if (child.outcome || ((notice.status == services::EServiceStatus::success) != (notice.first.value != 0u)))
            {
                return false;
            }
            child.outcome = true;
            if (notice.status != services::EServiceStatus::success)
            {
                ++m_extra_refused;
                m_failed = true;
                return true;
            }
            child.created = true;
            child.endpoint = notice.first;
            return true;
        }
        for (SExtra& child : m_extra)
        {
            if (!child.created || (child.endpoint.value != notice.first.value))
            {
                continue;
            }
            if (notice.status != services::EServiceStatus::success)
            {
                return false;
            }
            switch (invocation.event)
            {
                case modules::EExecutionEvent::child_ready:
                {
                    if (child.ready || child.ended)
                    {
                        return false;
                    }
                    child.ready = true;
                    return true;
                }
                case modules::EExecutionEvent::child_ended:
                {
                    if (child.ended) return false;
                    child.ended = true;
                    return true;
                }
                case modules::EExecutionEvent::child_retired:
                {
                    if (!child.ended || child.retired || (child.destroyed != 1u) || (child.created_on != child.destroyed_on) ||
                        (child.created_on == platform::threading::query_current_thread_id()))
                    {
                        return false;
                    }
                    child.retired = true;
                    ++m_extra_retired;
                    return true;
                }
                case modules::EExecutionEvent::child_stop_result:
                {
                    return child.stop_sent;
                }
                default:
                {
                    return false;
                }
            }
        }
        return false;
    }
    bool advance_children() noexcept
    {
        static const modules::SExecutionV1 descriptor{ sizeof(modules::SExecutionV1), 1u, 0u,
            &child_create, &child_handle, &child_destroy };
        static constexpr thread_ids::id_type threads[]{ thread_ids::rendering, thread_ids::audio };
        while (!m_stop && (m_extra_posted != 2u))
        {
            modules::SExecutionIntent create;
            create.kind = modules::SExecutionIntent::EKind::create_child;
            create.correlation = 40 + static_cast<std::int32_t>(m_extra_posted);
            create.child = { &descriptor, &m_extra[m_extra_posted], threads[m_extra_posted] };
            const modules::ESubmission result = m_communication.submit(create);
            if (result == modules::ESubmission::busy)
            {
                return true;
            }
            if (!modules::accepted(result)) return false;
            ++m_extra_posted;
        }
        if (!m_stop) return true;
        for (SExtra& child : m_extra)
        {
            if (!child.created || child.ended || child.stop_sent)
            {
                continue;
            }
            modules::SExecutionIntent stop;
            stop.kind = modules::SExecutionIntent::EKind::stop_child;
            stop.correlation = 42 + static_cast<std::int32_t>(&child - m_extra);
            stop.endpoint = child.endpoint;
            const modules::ESubmission result = m_communication.submit(stop);
            if (modules::accepted(result)) child.stop_sent = true;
            else if (result != modules::ESubmission::busy) return false;
        }
        return true;
    }
};

}   // namespace pump_tests
