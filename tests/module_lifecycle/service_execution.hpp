
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Lifecycle fixture service: retained acquisition and affine common execution.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/module/module_execution.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "memory/memory_token.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "system/system_context.hpp"
#include "debug/macros.hpp"
#include <cstring>

namespace module_lifecycle_tests
{

enum class EAcquisitionScenario { service, rendering_release, rendering_abandon, rendering_tests, rendering_tests_cancel, rendering_malformed };

class CServiceExecution
{
public:
    CServiceExecution(const modules::SExecutionContext& context, const bool duplicate_results,
        const bool fail_start, const EAcquisitionScenario scenario = EAcquisitionScenario::service) noexcept
        : m_communication{ *context.communication }, m_scenario{ scenario },
        m_duplicate_results{ duplicate_results }, m_fail_start{ fail_start } {}

    modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        using modules::EExecutionEvent;
        switch (invocation.event)
        {
            case EExecutionEvent::start:
            {
                if (m_fail_start || (system_context::get_ambient_module_id() != local_module()) ||
                    (system_context::get_ambient_thread_id() != local_thread()))
                {
                    m_failed = true;
                }
                else m_participate = true;
                break;
            }
            case EExecutionEvent::participation:
            {
                m_ready = invocation.notification.connection_status == connections::EStatus::success;
                m_failed |= !m_ready;
                m_connect_pending = m_ready && routing();
                break;
            }
            case EExecutionEvent::connection_offered:
            {
                if (m_offer.value || m_connection.value) m_failed = true;
                else m_offer = invocation.notification.connection;
                break;
            }
            case EExecutionEvent::connection_available:
            {
                m_connection = invocation.notification.connection;
                m_peer_endpoint = invocation.notification.first;
                break;
            }
            case EExecutionEvent::connection_result:
            {
                if (!m_connect_sent || (invocation.notification.correlation != 90))
                {
                    m_failed = true;
                }
                else
                {
                    // A recognized terminal outcome consumes this request even
                    // when it fails. Cleanup must not await a duplicate result.
                    m_connect_sent = false;
                    if (!m_stopping)
                    {
                        if ((invocation.notification.connection_status != connections::EStatus::success) ||
                            (invocation.notification.connection.value != m_connection.value) || !m_connection.value)
                        {
                            m_failed = true;
                        }
                        else m_test = ETest::first_pending;
                    }
                }
                break;
            }
            case EExecutionEvent::connection_closing:
            {
                m_closing = invocation.notification.connection;
                m_replies = 0u; // Ordered closure cancels any unpublished test completion.
                if (routing()) cancel_tests();
                break;
            }
            case EExecutionEvent::connection_retired:
            {
                m_connection = {};
                break;
            }
            case EExecutionEvent::message:
            {
                if (!invocation.message || !receive(invocation.source, *invocation.message)) m_failed = true;
                break;
            }
            case EExecutionEvent::stop:
            {
                m_stopping = true;
                m_participate = false;
                m_connect_pending = false;
                m_replies = 0u;
                if (routing()) cancel_tests();
                break;
            }
            case EExecutionEvent::service_async:
            case EExecutionEvent::provider_admission_closed:
            {
                break;
            }
            default:
            {
                m_failed = true;
                break;
            }
        }
        if (m_failed) m_stopping = true;
        m_blocked = false;
        advance();
        progress.ready = m_ready;
        progress.direct_input = (routing() ? ((m_test == ETest::first_wait) || (m_test == ETest::second_wait)) : !m_conversation) && !m_stopping;
        progress.work_complete = complete();
        progress.work_pending = !m_blocked && pending();
        if (waiting())
        {
            std::uint64_t remaining = 0u;
            if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                m_failed = true;
            }
            else progress.next_wake_ns = remaining;
        }
        if (m_failed && !m_failure_reported)
        {
            m_failure_reported = true;
            MV_LOG(error, "Lifecycle service: execution failed");
            return modules::EExecutionStatus::failed;
        }
        return modules::EExecutionStatus::success;
    }

    bool complete() const noexcept
    {
        return m_stopping && !m_unsafe && !waiting() && (m_phase == EAsset::empty) &&
            (!m_asset || m_disposed || m_abandoned) && !m_offer.value && !m_closing.value;
    }

private:
    enum class EAsset { empty, transfer_pending, transfer_wait, acquire_pending, acquire_wait, held, release_wait };
    enum class ETest { none, first_pending, first_wait, assets, second_pending, second_wait, closing, done };
    modules::CCommunication m_communication;
    const EAcquisitionScenario m_scenario;
    threading::CErasedOwnerMsg m_transfer;
    CAssetId m_asset;
    AssetAcquireResult m_acquired;
    connections::SToken m_offer, m_connection, m_closing;
    services::SClientToken m_peer_endpoint;
    platform::threading::CWaitDeadline m_deadline;
    EAsset m_phase{ EAsset::empty };
    ETest m_test{ ETest::none };
    std::int32_t m_correlation{ -1 };
    unsigned m_replies{ 0u };
    bool m_duplicate_results, m_fail_start;
    bool m_participate{ false }, m_ready{ false }, m_conversation{ false }, m_dispose_sent{ false }, m_disposed{ false };
    bool m_stopping{ false }, m_failed{ false }, m_failure_reported{ false }, m_blocked{ false }, m_cleanup_logged{ false };
    bool m_unsafe{ false }; // A refused release/disposal does not prove safe resource retirement.
    bool m_abandoned{ false }, m_exit_view_checked{ false };
    bool m_connect_pending{ false }, m_connect_sent{ false }, m_close_sent{ false };

    bool routing() const noexcept
    {
        return (m_scenario == EAcquisitionScenario::rendering_tests) || (m_scenario == EAcquisitionScenario::rendering_tests_cancel);
    }
    bool cleanup_requested() const noexcept { return m_stopping || (routing() && m_closing.value); }
    void cancel_tests() noexcept
    {
        if ((m_test == ETest::second_wait) || (m_test == ETest::second_pending))
        {
            MV_LOG(info, "Lifecycle tests: pending conversation cancelled on stop or closing");
        }
        m_test = ETest::done;
    }

    module_ids::id_type local_module() const noexcept
    {
        return (m_scenario == EAcquisitionScenario::service) ? module_ids::conditioning_general : module_ids::rendering;
    }
    thread_ids::id_type local_thread() const noexcept
    {
        return (m_scenario == EAcquisitionScenario::service) ? thread_ids::simulation : thread_ids::rendering;
    }
    std::uint8_t pattern() const noexcept { return (m_scenario == EAcquisitionScenario::service) ? 0x6b : 0x5a; }
    bool disposal_pending() const noexcept
    {
        return m_asset && !m_dispose_sent && (m_phase != EAsset::acquire_wait) &&
            ((m_scenario == EAcquisitionScenario::service) || routing() || m_stopping);
    }

    bool waiting() const noexcept
    {
        return m_connect_sent || (m_test == ETest::first_wait) || (m_test == ETest::second_wait) ||
            (m_phase == EAsset::transfer_wait) || (m_phase == EAsset::acquire_wait) ||
            (m_phase == EAsset::release_wait) || (cleanup_requested() && m_dispose_sent && !m_disposed && !m_abandoned);
    }
    bool pending() const noexcept
    {
        const bool quiesce = m_closing.value && (!routing() || ((m_phase == EAsset::empty) && (!m_asset || m_disposed) && !m_unsafe));
        return m_participate || m_offer.value || quiesce || m_replies || m_connect_pending ||
            (m_test == ETest::first_pending) || (m_test == ETest::second_pending) || ((m_test == ETest::closing) && !m_close_sent) ||
            (m_phase == EAsset::transfer_pending) || (m_phase == EAsset::acquire_pending) ||
            disposal_pending() ||
            (cleanup_requested() && (m_phase == EAsset::held));
    }
    void arm() noexcept
    {
        if (m_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none) m_failed = true;
    }
    bool accepted(const modules::ESubmission result) noexcept
    {
        if (modules::accepted(result)) return true;
        m_blocked = result == modules::ESubmission::busy;
        m_failed |= !m_blocked;
        return false;
    }
    bool intent(const modules::SExecutionIntent::EKind kind, const connections::SToken token = {}) noexcept
    {
        modules::SExecutionIntent value;
        value.kind = kind;
        value.correlation = 0;
        value.connection = token;
        value.decision = m_stopping ? connections::EStatus::rejected : connections::EStatus::success;
        return accepted(m_communication.submit(value));
    }
    template<typename T> bool post(const std::int32_t correlation, const T& value) noexcept
    {
        return accepted(m_communication.submit({}, threading::make_pod_message(correlation, value)));
    }
    bool start_transfer() noexcept
    {
        m_transfer = threading::make_owner_message<RawAssetTransfer>(200);
        RawAssetTransfer* const transfer = m_transfer.owner().payload<RawAssetTransfer>();
        if (!transfer || !transfer->storage.value.resize(16u))
        {
            return false;
        }
        std::memset(transfer->storage.value.data(), pattern(), 16u);
        m_conversation = true;
        m_phase = EAsset::transfer_pending;
        return true;
    }
    bool receive_tests(const connections::SSource& source, const threading::CErasedPodMsg& message) noexcept
    {
        if ((source.address != system_ids::executive) || (source.connection.value != m_connection.value) ||
            (source.endpoint.value != m_peer_endpoint.value))
        {
            return false;
        }
        modules::RunTestsResult result;
        if (!message.copy_payload_to(result) || (result.module != module_ids::executive))
        {
            return false;
        }
        if (m_test == ETest::first_wait)
        {
            if ((message.query_async_slot() != INT32_MAX) || (result.status != modules::ETestRunStatus::success) ||
                (result.passed != 7u) || result.failed || (result.skipped != 2u))
            {
                return false;
            }
            MV_LOG(info, "Lifecycle tests: Executive destination and correlation checks passed");
            m_test = ETest::assets;
            return start_transfer();
        }
        if ((m_test != ETest::second_wait) || (m_scenario != EAcquisitionScenario::rendering_tests) ||
            (message.query_async_slot() != 111) || (result.status != modules::ETestRunStatus::failed) ||
            (result.passed != 3u) || (result.failed != 1u) || (result.skipped != 2u))
        {
            return false;
        }
        MV_LOG(info, "Lifecycle tests: Executive terminal completion passed");
        m_test = ETest::closing;
        return true;
    }
    bool receive(const connections::SSource& source, const threading::CErasedPodMsg& message) noexcept
    {
        if (!source.is_host())
        {
            modules::RunTestsRequest request;
            if (m_stopping) return true;
            if (routing()) return receive_tests(source, message);
            if (m_conversation || (source.address != system_ids::executive) ||
                (source.connection.value != m_connection.value) || !message.copy_payload_to(request) ||
                (request.module != local_module()))
            {
                return false;
            }
            m_correlation = message.query_async_slot();
            if (m_scenario == EAcquisitionScenario::rendering_malformed)
            {
                m_correlation = (m_correlation == INT32_MAX) ? INT32_MIN : m_correlation + 1;
                m_conversation = true;
                m_replies = 1u;
                return true;
            }
            return start_transfer();
        }
        const std::int32_t correlation = message.query_async_slot();
        if ((correlation == 200) && (m_phase == EAsset::transfer_wait))
        {
            AssetResult result;
            if (!message.copy_payload_to(result)) return false;
            if (result.status != EAssetStatus::success)
            {
                if (result.asset) return false; // Contradictory custody: retain the unresolved wait.
                m_phase = EAsset::empty;
                return false; // Terminal refusal settled; report failure without awaiting another result.
            }
            if (!result.asset)
            {
                return false;
            }
            m_asset = result.asset;
            m_phase = m_stopping ? EAsset::empty : EAsset::acquire_pending;
            return true;
        }
        if ((correlation == 201) && (m_phase == EAsset::acquire_wait))
        {
            AssetAcquireResult result;
            if (!message.copy_payload_to(result)) return false;
            if (result.status != EAssetStatus::success)
            {
                if (result.acquisition) return false;
                m_phase = EAsset::empty; // The original asset still requires disposal during stop.
                return false;
            }
            if (!result.acquisition)
            {
                return false;
            }
            m_acquired = result;
            m_phase = EAsset::held;
            if (!m_stopping && !routing() && (m_scenario != EAcquisitionScenario::service))
            {
                m_replies = 1u;
            }
            return true;
        }
        if ((correlation == 202) && m_dispose_sent && !m_disposed)
        {
            AssetDisposeResult result;
            if (!message.copy_payload_to(result) || (result.asset != m_asset))
            {
                return false;
            }
            m_disposed = true;
            if (result.status != EAssetStatus::success) m_unsafe = true;
            return !m_unsafe;
        }
        if ((correlation == 203) && (m_phase == EAsset::release_wait))
        {
            AssetReleaseResult result;
            if (!message.copy_payload_to(result) || (result.acquisition != m_acquired.acquisition))
            {
                return false;
            }
            m_phase = EAsset::empty;
            if (result.status != EAssetStatus::success) m_unsafe = true;
            else m_acquired = {};
            return !m_unsafe;
        }
        return false;
    }
    void advance() noexcept
    {
        if (m_participate)
        {
            if (!intent(modules::SExecutionIntent::EKind::participate))
            {
                return;
            }
            m_participate = false;
        }
        if (m_offer.value)
        {
            if (!intent(modules::SExecutionIntent::EKind::consent, m_offer))
            {
                return;
            }
            m_offer = {};
        }
        if (m_closing.value && (!routing() || ((m_phase == EAsset::empty) && (!m_asset || m_disposed) && !m_unsafe)))
        {
            if (routing())
            {
                MV_LOG(info, "Lifecycle tests: caller acquisition and disposal drained");
            }
            if (!intent(modules::SExecutionIntent::EKind::connection_quiescent, m_closing))
            {
                return;
            }
            m_closing = {};
        }
        if (cleanup_requested() && ((m_phase == EAsset::transfer_pending) || (m_phase == EAsset::acquire_pending)))
        {
            m_transfer = {};
            m_phase = EAsset::empty;
        }
        if (m_phase == EAsset::transfer_pending)
        {
            if (!accepted(m_communication.submit({}, std::move(m_transfer))))
            {
                return;
            }
            m_phase = EAsset::transfer_wait;
            arm();
        }
        if (m_phase == EAsset::acquire_pending)
        {
            if (!post(201, AssetAcquireRequest{ m_asset })) return;
            m_phase = EAsset::acquire_wait;
            arm();
        }
        if (disposal_pending())
        {
            if (!post(202, AssetDisposeRequest{ m_asset })) return;
            m_dispose_sent = true;
            arm();
            if (!cleanup_requested())
            {
                if (routing()) m_test = ETest::second_pending;
                else m_replies = m_duplicate_results ? 16u : 1u;
            }
        }
        if (m_connect_pending)
        {
            modules::SExecutionIntent request;
            request.kind = modules::SExecutionIntent::EKind::connect;
            request.correlation = 90;
            request.first = modules::test_connections::address(module_ids::rendering);
            request.second = system_ids::executive;
            if (!accepted(m_communication.submit(request)))
            {
                return;
            }
            m_connect_pending = false;
            m_connect_sent = true;
            arm();
        }
        if (!cleanup_requested() && ((m_test == ETest::first_pending) || (m_test == ETest::second_pending)))
        {
            const bool first = m_test == ETest::first_pending;
            const auto message = threading::make_pod_message(first ? INT32_MAX : 111, modules::RunTestsRequest{ module_ids::executive });
            if (!accepted(m_communication.submit({ modules::SDestination::EKind::connection, {}, m_connection }, message)))
            {
                return;
            }
            m_test = first ? ETest::first_wait : ETest::second_wait;
            arm();
        }
        if ((m_test == ETest::closing) && !m_close_sent)
        {
            if (!intent(modules::SExecutionIntent::EKind::close_connection, m_connection))
            {
                return;
            }
            m_close_sent = true;
        }
        // One operation owns the bounded completion count; no generic outbox.
        for (unsigned count = 0u; (count < 4u) && m_replies; ++count)
        {
            const modules::ESubmission result = m_communication.submit(
                { modules::SDestination::EKind::connection, {}, m_connection }, threading::make_pod_message(m_correlation,
                    modules::RunTestsResult{ local_module(), modules::ETestRunStatus::success, 1u, 0u, 0u }));
            if ((result == modules::ESubmission::closed) || (result == modules::ESubmission::no_pipe))
            {
                m_replies = 0u;
                break;
            }
            if (!accepted(result)) return;
            --m_replies;
            if (m_scenario == EAcquisitionScenario::rendering_malformed)
            {
                MV_LOG(info, "Lifecycle rendering: malformed test completion posted");
            }
        }
        if (cleanup_requested() && (m_phase == EAsset::held))
        {
            if (!m_exit_view_checked)
            {
                const CByteConstView view = m_acquired.byte_view();
                if ((view.size() != 16u) || (view.data()[15] != pattern())) m_failed = true;
                if (m_scenario != EAcquisitionScenario::service)
                {
                    MV_LOG(info, "Lifecycle rendering: acquired bytes survive exit request");
                }
                m_exit_view_checked = true;
            }
            if (m_scenario == EAcquisitionScenario::rendering_abandon)
            {
                // Disposal is accepted above, and this callback ceases all byte
                // access. Domain completion lets the preamble seal its ingress.
                // Host still retains the endpoint/module until native completion,
                // acquisition reclamation and the pending disposal have settled.
                m_acquired = {};
                m_phase = EAsset::empty;
                m_abandoned = true;
                MV_LOG(info, "Lifecycle rendering: abandoned acquisition view relinquished after disposal acceptance");
                return;
            }
            if (!post(203, AssetReleaseRequest{ m_acquired.acquisition }))
            {
                return;
            }
            m_phase = EAsset::release_wait;
            arm();
        }
        if (complete() && m_asset && !m_abandoned && !m_cleanup_logged)
        {
            m_cleanup_logged = true;
            MV_LOG(info, "Lifecycle service: acquisition and disposal drained");
        }
    }
};

}   //  namespace module_lifecycle_tests
