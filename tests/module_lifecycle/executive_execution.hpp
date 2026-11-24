
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Explicit asynchronous lifecycle acceptance steps owned by the Executive DLL.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/module/module_execution.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "framework/threading/CBatchWork.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/module_lifecycle/save_execution.hpp"
#include "tests/module_lifecycle/bundle_execution.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "platform/threading/processor_relax.hpp"
#include "system/system_context.hpp"
#include "debug/macros.hpp"
#include <cstdlib>
#include <cstring>

namespace module_lifecycle_tests
{

class CExecutiveExecution
{
public:
    CExecutiveExecution(const modules::SExecutionContext& context, const char* const selected) noexcept
        : m_communication{ *context.communication }, m_batch{ context.batch }, m_saves{ *context.communication }, m_bundle{ context }
    {
        const std::size_t length = std::strlen(selected);
        if (length < sizeof(m_selected)) std::memcpy(m_selected, selected, (length + 1u));
        else m_failed = true;
    }

    modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        switch (invocation.event)
        {
            case modules::EExecutionEvent::start:
            {
                m_ready = true;
                m_direct = (std::strncmp(m_selected, "service-", 8u) == 0) ||
                    (std::strncmp(m_selected, "acquire-render-", 15u) == 0) || routing() || scenario("audio-lifecycle");
                if (routing()) m_peer = modules::test_connections::address(module_ids::rendering);
                m_participate_pending = m_direct;
                if (scenario("startup-failure")) m_failed = true;
                if (scenario("shutdown")) MV_LOG(info, "Lifecycle fixture: requesting shutdown");
                break;
            }
            case modules::EExecutionEvent::message:
            {
                if (!m_stopping && (!invocation.message ||
                    !(invocation.source.is_host() ? receive(*invocation.message) : receive_test(invocation.source, *invocation.message))))
                {
                    m_failed = true;
                }
                break;
            }
            case modules::EExecutionEvent::stop:
            {
                m_stopping = true;
                m_retained_results = 0u;
                m_route_reply_pending = false;
                break;
            }
            case modules::EExecutionEvent::participation:
            {
                m_participated = invocation.notification.connection_status == connections::EStatus::success;
                m_failed |= !m_participated;
                break;
            }
            case modules::EExecutionEvent::connection_offered:
            {
                modules::SExecutionIntent consent;
                consent.kind = modules::SExecutionIntent::EKind::consent;
                consent.connection = invocation.notification.connection;
                consent.decision = (!m_stopping && !invocation.notification.bulk.is_bulk() &&
                    (invocation.notification.peer == m_peer)) ? connections::EStatus::success : connections::EStatus::rejected;
                m_failed |= !modules::accepted(m_communication.submit(consent));
                break;
            }
            case modules::EExecutionEvent::connection_available:
            {
                m_connection = invocation.notification.connection;
                m_peer_endpoint = invocation.notification.first;
                break;
            }
            case modules::EExecutionEvent::connection_result:
            {
                const modules::SExecutionNotification& result = invocation.notification;
                const bool success = m_connect_status == connections::EStatus::success;
                if ((m_operation != EOperation::connect) || !m_accepted || (result.correlation != m_correlation) ||
                    (result.connection_status != m_connect_status) || (success &&
                        (!result.connection.value || (result.connection.value != m_connection.value) ||
                            (result.connection.value == m_previous.value))) || (!success && result.connection.value))
                {
                    m_failed = true;
                }
                else
                {
                    if (success) m_previous = result.connection;
                    m_operation = EOperation::none;
                    ++m_step;
                }
                break;
            }
            case modules::EExecutionEvent::connection_closing:
            {
                m_retained_results = 0u; // Cancel the fifteen deliberately unread copied completions.
                m_route_reply_pending = false;
                modules::SExecutionIntent quiescent;
                quiescent.kind = modules::SExecutionIntent::EKind::connection_quiescent;
                quiescent.connection = invocation.notification.connection;
                m_failed |= !modules::accepted(m_communication.submit(quiescent));
                if (!m_stopping && (m_operation == EOperation::test)) m_failed = true;
                if (routing() && (m_operation == EOperation::conversation))
                {
                    m_operation = EOperation::none;
                    ++m_step;
                }
                break;
            }
            case modules::EExecutionEvent::connection_retired:
            {
                if (invocation.notification.connection.value != m_connection.value) m_failed = true;
                m_connection = {};
                m_cancelled_test = false;
                if (m_operation == EOperation::retire)
                {
                    m_operation = EOperation::none;
                    ++m_step;
                }
                break;
            }
            case modules::EExecutionEvent::service_async:
            case modules::EExecutionEvent::provider_admission_closed:
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
        advance_batch();
        if (m_stopping)
        {
            // Unpublished requests still belong to this object. Host quiescence
            // explicitly cancels any unread results after unexpected shutdown.
            m_owner = {};
            m_saves.cancel();
            m_bundle.cancel();
        }
        else
        {
            if (m_participate_pending)
            {
                modules::SExecutionIntent participate;
                participate.kind = modules::SExecutionIntent::EKind::participate;
                participate.correlation = 0;
                const modules::ESubmission result = m_communication.submit(participate);
                if (modules::accepted(result)) m_participate_pending = false;
                else if (result != modules::ESubmission::busy) m_failed = true;
            }
            if ((m_operation == EOperation::none) && (!m_direct || m_participated)) next();
            publish();
            if (m_operation == EOperation::saves)
            {
                m_failed |= !m_saves.advance();
                if (m_saves.complete())
                {
                    MV_LOG(info, "Lifecycle fixture: 32 saves and ordered teardown completed");
                    m_operation = EOperation::none;
                    ++m_step;
                }
            }
            if (m_operation == EOperation::bundle)
            {
                m_failed |= !m_bundle.advance();
                if (m_bundle.complete())
                {
                    m_operation = EOperation::none;
                    ++m_step;
                }
                else if (m_bundle.stop_requested())
                {
                    stop();
                    publish();
                }
            }
            if (m_route_reply_pending && !m_failed)
            {
                const modules::ESubmission result = m_communication.submit(
                    { modules::SDestination::EKind::connection, {}, m_connection }, m_route_reply);
                if (modules::accepted(result)) m_route_reply_pending = false;
                else if (result != modules::ESubmission::busy) m_failed = true;
            }
        }
        m_failed |= !m_bundle.service_batch();
        progress = {};
        progress.ready = m_ready && (!m_direct || m_participated);
        progress.direct_input = !m_stopping && !m_retained_results && !m_cancelled_test && !m_route_reply_pending;
        progress.work_complete = complete();
        progress.work_pending = !m_stopping && (((m_operation == EOperation::none) && (!m_direct || m_participated)) ||
            ((m_operation == EOperation::saves) && m_saves.runnable()) ||
            ((m_operation == EOperation::bundle) && m_bundle.runnable()));
        if (!m_stopping && (m_operation != EOperation::none))
        {
            std::uint64_t remaining = 0u;
            if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                m_failed = true;
            }
            else progress.next_wake_ns = remaining;
        }
        if (m_batch_active && m_batch_exit && (m_submitted == 1u) && !m_stopping)
        {
            progress.next_wake_ns = 1000000u; // The held first item has no response wake yet.
        }
        if ((m_operation == EOperation::bundle) && m_bundle.timed_poll() && !m_stopping)
        {
            progress.next_wake_ns = 1000000u;
        }
        if (complete() && m_self_requested && !m_failed && !m_pass_logged)
        {
            m_pass_logged = true;
            MV_LOG(info, "Lifecycle fixture: Host requested Executive exit without notifications");
            MV_LOG_RICH(info, "Lifecycle fixture: %s passed", m_selected);
        }
        if (m_failed && !m_reported)
        {
            m_reported = true;
            if (scenario("startup-failure")) MV_LOG(info, "Lifecycle fixture: deliberate startup failure");
            else MV_LOG_RICH(error, "Lifecycle fixture failed: %s step %u event %u status %u correlation %d",
                m_selected, m_step, static_cast<unsigned>(invocation.event),
                static_cast<unsigned>(invocation.notification.connection_status), invocation.notification.correlation);
            return modules::EExecutionStatus::failed;
        }
        return modules::EExecutionStatus::success;
    }
    bool complete() const noexcept { return m_stopping && !m_batch_active && m_bundle.quiescent(); }

private:
    enum class EOperation { none, module, transfer, dispose, self, batch, connect, test, retire, conversation, saves, bundle };
    struct SBatchItem
    {
        std::uint32_t value{ 0u };
        std::atomic<bool>* started{ nullptr };
        const threading::CBatchClient* wait_for_exit{ nullptr };
        module_ids::id_type module{};
        thread_ids::id_type thread{};
        memory::CMemoryContext* memory{ nullptr };
    };
    modules::CCommunication m_communication;
    threading::CBatchClient* const m_batch;
    CSaveExecution m_saves;
    CBundleExecution m_bundle;
    SBatchItem m_items[16];
    bool m_seen[16]{};
    std::atomic<bool> m_first_started{ false };
    memory::CMemoryContext* m_expected_memory{ nullptr };
    unsigned m_submitted{ 0u }, m_received{ 0u }, m_queued{ 0u }, m_inline{ 0u };
    bool m_batch_active{ false }, m_batch_exit{ false };
    connections::SToken m_connection, m_previous;
    services::SClientToken m_peer_endpoint;
    system_ids::id_type m_peer{};
    module_ids::id_type m_peer_module{ module_ids::conditioning_general };
    connections::EStatus m_connect_status{ connections::EStatus::success };
    modules::RunTestsResult m_test_results[16];
    unsigned m_test_count{ 0u }, m_expected_results{ 0u }, m_retained_results{ 0u };
    bool m_direct{ false }, m_participated{ false }, m_participate_pending{ false };
    bool m_cancelled_test{ false };
    threading::CErasedPodMsg m_route_reply;
    unsigned m_route_requests{ 0u };
    bool m_route_reply_pending{ false };
    threading::CErasedOwnerMsg m_owner;
    platform::threading::CWaitDeadline m_deadline;
    char m_selected[40]{};
    CAssetId m_assets[2], m_disposal;
    EOperation m_operation{ EOperation::none };
    modules::EModuleStatus m_module_status{ modules::EModuleStatus::success };
    EAssetStatus m_disposal_status{ EAssetStatus::success };
    module_ids::id_type m_module{ module_ids::rendering };
    std::int32_t m_correlation{ -1 };
    unsigned m_step{ 0u }, m_asset_slot{ 0u };
    bool m_accepted{ false }, m_acknowledged{ false }, m_available{ false }, m_self_requested{ false };
    bool m_ready{ false }, m_stopping{ false }, m_failed{ false }, m_reported{ false }, m_pass_logged{ false };

    bool scenario(const char* const name) const noexcept { return std::strcmp(m_selected, name) == 0; }
    bool routing() const noexcept
    {
        return scenario("tests-routing") || scenario("tests-self-stop") || scenario("tests-client-unload");
    }
    static void MV_STD_ABI_CALL execute_batch(void* const object) noexcept
    {
        SBatchItem& item = *static_cast<SBatchItem*>(object);
        if (item.started)
        {
            item.started->store(true, std::memory_order_release);
            while (!item.wait_for_exit->exit_requested()) platform::threading::processor_relax();
        }
        ++item.value;
        item.module = system_context::get_ambient_module_id();
        item.thread = system_context::get_ambient_thread_id();
        item.memory = memory::get_ambient_memory_context();
        MV_LOG(info, "Lifecycle fixture: batch item executed");
    }
    void submit_batch_item() noexcept
    {
        const unsigned index = m_submitted;
        const threading::SBatchSubmission result = m_batch->submit(&execute_batch, &m_items[index],
            ((m_batch_exit ? 200u : 100u) + index));
        if (result.status == threading::EBatchSubmission::queued) ++m_queued;
        else if (result.status == threading::EBatchSubmission::completed_inline)
        {
            ++m_inline;
            m_seen[index] = true;
        }
        else
        {
            m_failed = true;
            return;
        }
        ++m_submitted;
    }
    void start_batch() noexcept
    {
        if (!m_batch)
        {
            m_failed = true;
            return;
        }
        m_batch_exit = scenario("batch-exit");
        if (m_batch_exit && !m_batch->has_runners())
        {
            MV_LOG(info, "Lifecycle fixture: batch exit skipped without runners");
            ++m_step;
            return;
        }
        m_batch_active = true;
        m_expected_memory = memory::get_ambient_memory_context();
        arm(EOperation::batch, -1);
        if (m_batch_exit)
        {
            m_items[0].started = &m_first_started;
            m_items[0].wait_for_exit = m_batch;
            submit_batch_item();
        }
        else
        {
            for (unsigned count = 0u; (count < 16u) && !m_failed; ++count) submit_batch_item();
            if (scenario("batch-inline") && m_queued) m_failed = true;
        }
        advance_batch();
    }
    void advance_batch() noexcept
    {
        if (!m_batch_active) return;
        if (m_batch_exit && (m_submitted == 1u) && !m_stopping && !m_failed)
        {
            if (!m_first_started.load(std::memory_order_acquire)) return;
            for (unsigned count = 1u; (count < 8u) && !m_failed; ++count) submit_batch_item();
            if (m_inline) m_failed = true;
            m_operation = EOperation::none;
            ++m_step; // Request Host shutdown while the first accepted item remains held.
        }
        for (unsigned count = 0u; count < 16u; ++count)
        {
            threading::SBatchResponse response;
            if (!m_batch->receive(response)) break;
            const std::uint32_t base = m_batch_exit ? 200u : 100u;
            if ((response.correlation < base) || (response.correlation >= (base + m_submitted)))
            {
                m_failed = true;
                continue;
            }
            const unsigned index = response.correlation - base;
            const threading::EBatchCompletion expected = (m_batch_exit && index) ?
                threading::EBatchCompletion::discarded_requester_exiting : threading::EBatchCompletion::executed;
            m_failed |= m_seen[index] || (response.completion != expected);
            m_seen[index] = true;
            ++m_received;
        }
        if (m_batch->outstanding()) return;
        m_batch_active = false;
        m_failed |= m_received != m_queued;
        for (unsigned index = 0u; index < m_submitted; ++index)
        {
            const SBatchItem& item = m_items[index];
            const bool discarded = m_batch_exit && index;
            m_failed |= !m_seen[index] || (item.value != (discarded ? 0u : 1u));
            if (!discarded)
            {
                m_failed |= (item.module != module_ids::executive) || (item.memory != m_expected_memory) ||
                    (item.thread != (m_queued ? thread_ids::batch_runner_00 : thread_ids::executive));
            }
        }
        if (m_failed) return;
        if (m_batch_exit) MV_LOG(info, "Lifecycle fixture: batch exit drained one executed and seven discarded");
        else
        {
            MV_LOG(info, "Lifecycle fixture: batch queued {} inline {}", m_queued, m_inline);
            m_operation = EOperation::none;
            ++m_step;
        }
    }
    void arm(const EOperation operation, const std::int32_t correlation) noexcept
    {
        m_operation = operation;
        m_correlation = correlation;
        m_accepted = m_acknowledged = false;
        if (m_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none) m_failed = true;
    }
    void module(const modules::EModuleAction action, const char* const stem, const std::int32_t correlation,
        const modules::EModuleStatus expected, const bool available,
        const system_type_id function = system_type_ids::undefined,
        const module_ids::id_type target = module_ids::rendering) noexcept
    {
        m_owner = threading::make_owner_message<modules::ModuleRequest>(correlation);
        modules::ModuleRequest* const request = m_owner.owner().payload<modules::ModuleRequest>();
        if (!request || (stem && !request->file.set(test_environment::module_filename(stem).c_str())))
        {
            m_failed = true;
            return;
        }
        request->module = target;
        request->action = action;
        request->required_function = function;
        m_module = request->module;
        m_module_status = expected;
        m_available = available;
        arm(EOperation::module, correlation);
    }
    void stop(const char* const replacement = nullptr) noexcept
    {
        module(replacement ? modules::EModuleAction::replace : modules::EModuleAction::unload,
            replacement, 100, modules::EModuleStatus::success, false);
        modules::ModuleRequest* const request = m_owner.owner().payload<modules::ModuleRequest>();
        if (!request) return;
        request->module = module_ids::executive;
        m_operation = EOperation::self;
    }
    void transfer(const unsigned slot, const mount_point_ids::id_type dependency = {}, const std::size_t size = 16u) noexcept
    {
        m_owner = threading::make_owner_message<RawAssetTransfer>(20);
        RawAssetTransfer* const request = m_owner.owner().payload<RawAssetTransfer>();
        if (!request || !request->storage.value.resize(size))
        {
            m_failed = true;
            return;
        }
        std::memset(request->storage.value.data(), 0x5a, size);
        if (dependency != mount_point_ids::id_type{}) m_owner.owner().add_hazard(dependency);
        m_asset_slot = slot;
        arm(EOperation::transfer, 20);
    }
    void dispose(const CAssetId asset, const EAssetStatus expected) noexcept
    {
        m_disposal = asset;
        m_disposal_status = expected;
        arm(EOperation::dispose, 30);
    }
    void connect(const std::int32_t correlation, const connections::EStatus expected,
        const module_ids::id_type peer = module_ids::conditioning_general) noexcept
    {
        m_peer_module = peer;
        m_peer = (peer == module_ids::conditioning_general) ?
            system_ids::ops::make_system_id(peer, thread_ids::simulation) : modules::test_connections::address(peer);
        m_connect_status = expected;
        arm(EOperation::connect, correlation);
    }
    void test(const std::int32_t correlation) noexcept
    {
        m_test_count = 0u;
        m_expected_results = scenario("service-lifecycle") ? 16u : 1u;
        arm(EOperation::test, correlation);
    }
    void retire() noexcept
    {
        if (!m_connection.value)
        {
            m_failed = true;
            return;
        }
        arm(EOperation::retire, -1);
    }
    bool receive_test(const connections::SSource& source, const threading::CErasedPodMsg& message) noexcept
    {
        if (routing())
        {
            modules::RunTestsRequest request;
            const bool first = m_route_requests == 0u;
            if ((m_route_requests >= 2u) || m_route_reply_pending || (source.address != m_peer) ||
                (source.endpoint.value != m_peer_endpoint.value) || (source.connection.value != m_connection.value) ||
                !message.copy_payload_to(request) || (request.module != module_ids::executive) ||
                (message.query_async_slot() != (first ? INT32_MAX : 111)))
            {
                return false;
            }
            ++m_route_requests;
            if (first || scenario("tests-routing"))
            {
                m_route_reply = threading::make_pod_message(message.query_async_slot(), modules::RunTestsResult{
                    module_ids::executive, first ? modules::ETestRunStatus::success : modules::ETestRunStatus::failed,
                    first ? 7u : 3u, first ? 0u : 1u, 2u });
                m_route_reply_pending = true;
            }
            if (!first && (m_operation == EOperation::conversation)) m_operation = EOperation::none;
            return true;
        }
        if ((m_operation != EOperation::test) || !m_accepted || (source.address != m_peer) ||
            (source.endpoint.value != m_peer_endpoint.value) || (source.connection.value != m_connection.value) ||
            (message.query_async_slot() != m_correlation) || (m_test_count >= m_expected_results))
        {
            return false;
        }
        modules::RunTestsResult& result = m_test_results[m_test_count];
        if (!message.copy_payload_to(result) || (result.module != m_peer_module))
        {
            return false;
        }
        const bool passed = (result.status == modules::ETestRunStatus::success) &&
            (result.passed == 1u) && !result.failed && !result.skipped;
        const bool unavailable = (m_peer_module == module_ids::audio) &&
            (result.status == modules::ETestRunStatus::unavailable) && !result.passed && !result.failed && (result.skipped == 1u);
        if (!passed && !unavailable)
        {
            return false;
        }
        if (++m_test_count == m_expected_results)
        {
            m_retained_results = m_test_count - 1u;
            m_operation = EOperation::none;
            ++m_step;
        }
        return true;
    }
    void publish() noexcept
    {
        if (m_failed || m_accepted || (m_operation == EOperation::none) ||
            (m_operation == EOperation::batch) || (m_operation == EOperation::conversation) ||
            (m_operation == EOperation::saves) || (m_operation == EOperation::bundle))
        {
            return;
        }
        modules::ESubmission result;
        if ((m_operation == EOperation::connect) || (m_operation == EOperation::retire))
        {
            modules::SExecutionIntent intent;
            intent.kind = (m_operation == EOperation::connect) ?
                modules::SExecutionIntent::EKind::connect : modules::SExecutionIntent::EKind::close_connection;
            intent.correlation = m_correlation;
            intent.first = system_ids::executive;
            intent.second = m_peer;
            intent.connection = m_connection;
            result = m_communication.submit(intent);
        }
        else if (m_operation == EOperation::test)
        {
            result = m_communication.submit({ modules::SDestination::EKind::connection, {}, m_connection },
                threading::make_pod_message(m_correlation, modules::RunTestsRequest{ m_peer_module }));
        }
        else result = (m_operation == EOperation::dispose) ?
            m_communication.submit({}, threading::make_pod_message(m_correlation, AssetDisposeRequest{ m_disposal })) :
            m_communication.submit({}, std::move(m_owner));
        if (modules::accepted(result))
        {
            m_accepted = true;
            m_self_requested = (m_operation == EOperation::self) || scenario("service-start-failure");
            if (scenario("audio-lifecycle") && (m_operation == EOperation::test) && (m_correlation == 5))
            {
                // Transport acceptance precedes unload. Pause ordinary direct
                // handling and let ordered closure cancel even a queued reply.
                m_cancelled_test = true;
                m_operation = EOperation::none;
                ++m_step;
            }
        }
        else if (result != modules::ESubmission::busy) m_failed = true;
    }
    bool receive(const threading::CErasedPodMsg& message) noexcept
    {
        if (m_operation == EOperation::saves) return m_saves.receive(message);
        if (m_operation == EOperation::bundle) return m_bundle.receive(message);
        if (!m_accepted || (message.query_async_slot() != m_correlation))
        {
            return false;
        }
        switch (m_operation)
        {
            case EOperation::module:
            {
                modules::ModuleResult result;
                if (!message.copy_payload_to(result) || (result.module != m_module))
                {
                    return false;
                }
                if (!m_acknowledged)
                {
                    m_acknowledged = (result.notice == modules::EModuleNotice::acknowledged) &&
                        (result.status == modules::EModuleStatus::success);
                    return m_acknowledged;
                }
                if ((result.notice != modules::EModuleNotice::completed) || (result.status != m_module_status) ||
                    (result.available != m_available))
                {
                    return false;
                }
                MV_LOG(info, "Lifecycle fixture: operation {} passed", m_correlation);
                // A failed service startup initiates global shutdown. Its result
                // may be cancelled by quiescence; if delivered, await stop.
                if (scenario("service-start-failure")) return true;
                break;
            }
            case EOperation::transfer:
            {
                AssetResult result;
                if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success) || !result.asset)
                {
                    return false;
                }
                m_assets[m_asset_slot] = result.asset;
                break;
            }
            case EOperation::dispose:
            {
                AssetDisposeResult result;
                if (!message.copy_payload_to(result) || (result.status != m_disposal_status) || (result.asset != m_disposal))
                {
                    return false;
                }
                break;
            }
            default:
            {
                return false;
            }
        }
        m_operation = EOperation::none;
        ++m_step;
        return true;
    }
    void next() noexcept
    {
        using modules::EModuleAction;
        using modules::EModuleStatus;
        static constexpr char rendering[]{ "package:/bin/MorphicRendering" };
        static constexpr char missing[]{ "package:/bin/MorphicMissingRendering" };
        static constexpr char executive[]{ "package:/bin/MorphicExecutive" };
        if (std::strncmp(m_selected, "bundle", 6u) == 0)
        {
            if (!m_step)
            {
                m_failed |= !m_bundle.begin(m_selected);
                arm(EOperation::bundle, -1);
            }
            else stop(scenario("bundle-replace") ? "package:/bin/MorphicLifecycleExecutive" : nullptr);
            return;
        }
        if (scenario("disposal-during-save") || scenario("render-drain"))
        {
            switch (m_step)
            {
                case 0u:
                {
                    module(EModuleAction::load, rendering, 1, EModuleStatus::success, true);
                    break;
                }
                case 1u:
                {
                    transfer(0u, mount_point_ids::render);
                    break;
                }
                case 2u:
                {
                    m_saves.start(m_assets[0], scenario("disposal-during-save"));
                    arm(EOperation::saves, -1);
                    break;
                }
                case 3u:
                {
                    dispose(m_assets[0], EAssetStatus::invalid_asset);
                    break;
                }
                case 4u:
                {
                    if (scenario("disposal-during-save")) module(EModuleAction::unload, nullptr, 2, EModuleStatus::success, false);
                    else stop();
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        if (scenario("render-exit-disposal"))
        {
            switch (m_step)
            {
                case 0u:
                {
                    transfer(0u, mount_point_ids::render, (std::size_t{ 1u } << 20u));
                    break;
                }
                case 1u:
                {
                    transfer(1u, mount_point_ids::render);
                    break;
                }
                case 2u:
                {
                    // Test configuration contains value identities, no pointers.
                    std::FILE* const file = open_disposal_handoff("wb");
                    if (!file)
                    {
                        m_failed = true;
                        break;
                    }
                    const bool written = std::fwrite(m_assets, sizeof(m_assets), 1u, file) == 1u;
                    const bool closed = std::fclose(file) == 0;
                    if (!written || !closed)
                    {
                        m_failed = true;
                        break;
                    }
                    module(EModuleAction::load, "package:/bin/MorphicLifecycleRenderingDisposal", 1, EModuleStatus::success, true);
                    break;
                }
                case 3u:
                {
                    m_saves.start(m_assets[0], false);
                    arm(EOperation::saves, -1);
                    break;
                }
                case 4u:
                case 5u:
                {
                    dispose(m_assets[m_step - 4u], EAssetStatus::invalid_asset);
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        if (routing())
        {
            switch (m_step)
            {
                case 0u:
                {
                    module(EModuleAction::load, "package:/bin/MorphicLifecycleRenderingDisposal", 1, EModuleStatus::success, true);
                    break;
                }
                case 1u:
                {
                    if ((m_route_requests < 2u) || scenario("tests-routing"))
                    {
                        arm(EOperation::conversation, -1);
                    }
                    else if (scenario("tests-self-stop")) stop();
                    else module(EModuleAction::unload, nullptr, 120, EModuleStatus::success, false);
                    break;
                }
                case 2u:
                {
                    if (scenario("tests-client-unload"))
                    {
                        MV_LOG(info, "Lifecycle tests: caller unload waited for cancellation and cleanup");
                    }
                    if (m_connection.value)
                    {
                        arm(EOperation::retire, -1);
                        m_accepted = true; // Peer closure or unload already owns the transition.
                    }
                    else stop();
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        if (std::strncmp(m_selected, "acquire-render-", 15u) == 0)
        {
            switch (m_step)
            {
                case 0u:
                {
                    module(EModuleAction::load, "package:/bin/MorphicLifecycleRenderingDisposal", 1, EModuleStatus::success, true);
                    break;
                }
                case 1u:
                {
                    connect(49, connections::EStatus::success, module_ids::rendering);
                    break;
                }
                case 2u:
                {
                    test(50);
                    break;
                }
                case 3u:
                {
                    retire();
                    break;
                }
                case 4u:
                {
                    if (scenario("acquire-render-replace"))
                    {
                        if (!select_case("shutdown")) m_failed = true;
                        stop("package:/bin/MorphicLifecycleExecutive");
                    }
                    else module(EModuleAction::unload, nullptr, 2, EModuleStatus::success, false);
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        if (scenario("audio-lifecycle"))
        {
            switch (m_step)
            {
                case 0u:
                {
                    module(EModuleAction::load, "package:/bin/MorphicAudio", 1,
                        EModuleStatus::success, true, system_type_ids::undefined, module_ids::audio);
                    break;
                }
                case 1u:
                {
                    connect(50, connections::EStatus::success, module_ids::audio);
                    break;
                }
                case 2u:
                {
                    test(2);
                    break;
                }
                case 3u:
                {
                    test(3);
                    break;
                }
                case 4u:
                {
                    MV_LOG(info, "Audio lifecycle: repeated test completed before unload");
                    test(5);
                    break;
                }
                case 5u:
                {
                    module(EModuleAction::unload, nullptr, 4,
                        EModuleStatus::success, false, system_type_ids::undefined, module_ids::audio);
                    break;
                }
                case 6u:
                {
                    MV_LOG(info, "Audio lifecycle: cancellation and unload completed");
                    if (m_connection.value)
                    {
                        arm(EOperation::retire, -1);
                        m_accepted = true; // Unload already requested closure; await its final notification.
                    }
                    else stop();
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        if (scenario("service-start-failure"))
        {
            module(EModuleAction::load, "package:/bin/MorphicLifecycleService", 1,
                EModuleStatus::installation_failed, false, system_type_ids::undefined, module_ids::conditioning_general);
            return;
        }
        if (scenario("service-lifecycle") || scenario("service-shutdown"))
        {
            const auto change = [&](const EModuleAction action, const char* const file, const std::int32_t correlation,
                const EModuleStatus status, const bool available) noexcept
            {
                module(action, file, correlation, status, available, system_type_ids::undefined, module_ids::conditioning_general);
            };
            static constexpr char passive[]{ "package:/bin/MorphicLifecyclePassive" };
            static constexpr char service[]{ "package:/bin/MorphicLifecycleService" };
            if (scenario("service-shutdown") && (m_step == 9u))
            {
                stop();
                return;
            }
            switch (m_step)
            {
                case 0u:
                {
                    module(EModuleAction::load, rendering, 1, EModuleStatus::success, true);
                    break;
                }
                case 1u:
                {
                    change(EModuleAction::load, passive, 2, EModuleStatus::success, true);
                    break;
                }
                case 2u:
                {
                    change(EModuleAction::load, passive, 3, EModuleStatus::already_loaded, true);
                    break;
                }
                case 3u:
                {
                    connect(4, connections::EStatus::unavailable);
                    break;
                }
                case 4u:
                {
                    change(EModuleAction::load, passive, 5, EModuleStatus::already_loaded, true);
                    break;
                }
                case 5u:
                {
                    change(EModuleAction::replace, service, 6, EModuleStatus::success, true);
                    break;
                }
                case 6u:
                {
                    connect(7, connections::EStatus::success);
                    break;
                }
                case 7u:
                {
                    test(7);
                    break;
                }
                case 8u:
                {
                    retire();
                    break;
                }
                case 9u:
                {
                    transfer(0u, mount_point_ids::conditioning);
                    break;
                }
                case 10u:
                {
                    change(EModuleAction::replace, service, 8, EModuleStatus::success, true);
                    break;
                }
                case 11u:
                {
                    dispose(m_assets[0], EAssetStatus::invalid_asset);
                    break;
                }
                case 12u:
                {
                    connect(9, connections::EStatus::success);
                    break;
                }
                case 13u:
                {
                    test(9);
                    break;
                }
                case 14u:
                {
                    retire();
                    break;
                }
                case 15u:
                {
                    change(EModuleAction::unload, nullptr, 10, EModuleStatus::success, false);
                    break;
                }
                case 16u:
                {
                    connect(11, connections::EStatus::unavailable);
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        if (scenario("batch") || scenario("batch-inline") || scenario("batch-exit"))
        {
            if (!m_step) start_batch();
            else stop();
            return;
        }
        if (scenario("ordinary"))
        {
            switch (m_step)
            {
                case 0u:
                {
                    module(EModuleAction::load, rendering, 1, EModuleStatus::success, true);
                    break;
                }
                case 1u:
                {
                    module(EModuleAction::load, rendering, 2, EModuleStatus::already_loaded, true);
                    break;
                }
                case 2u:
                {
                    module(EModuleAction::replace, rendering, 3, EModuleStatus::success, true);
                    break;
                }
                case 3u:
                {
                    module(EModuleAction::unload, nullptr, 4, EModuleStatus::success, false);
                    break;
                }
                case 4u:
                {
                    module(EModuleAction::unload, nullptr, 5, EModuleStatus::not_loaded, false);
                    break;
                }
                case 5u:
                {
                    module(EModuleAction::load, missing, 6, EModuleStatus::binding_failed, false);
                    break;
                }
                case 6u:
                {
                    module(EModuleAction::load, rendering, 7, EModuleStatus::function_unavailable, false, system_type_ids::asset_load_request);
                    break;
                }
                case 7u:
                {
                    module(EModuleAction::load, rendering, 8, EModuleStatus::success, true);
                    break;
                }
                case 8u:
                {
                    module(EModuleAction::replace, missing, 9, EModuleStatus::binding_failed, false);
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        if (scenario("shutdown"))
        {
            stop();
            return;
        }
        if (scenario("execution-required"))
        {
            if (!m_step) module(EModuleAction::load, "package:/bin/MorphicLifecycleMissingExecution", 1, EModuleStatus::function_unavailable, false);
            else stop();
            return;
        }
        if (scenario("replace-missing"))
        {
            stop("package:/bin/MorphicMissingExecutive");
            return;
        }
        if (scenario("replace-startup-failure"))
        {
            if (!select_case("startup-failure")) m_failed = true;
            else stop("package:/bin/MorphicLifecycleExecutive");
            return;
        }
        if (scenario("replace") || scenario("replace-dependency") || scenario("shutdown-dependency"))
        {
            if (!m_step) transfer(0u, scenario("replace") ? mount_point_ids::id_type{} : mount_point_ids::executive);
            else stop(scenario("shutdown-dependency") ? nullptr : executive);
            return;
        }
        if (scenario("render-shutdown") || scenario("render-shutdown-dependency") || scenario("render-executive-replace"))
        {
            if (!m_step) module(EModuleAction::load, rendering, 1, EModuleStatus::success, true);
            else if ((m_step == 1u) && scenario("render-shutdown-dependency")) transfer(0u, mount_point_ids::render);
            else stop(scenario("render-executive-replace") ? executive : nullptr);
            return;
        }
        if (scenario("dependency") || scenario("render-replace-dependency"))
        {
            switch (m_step)
            {
                case 0u:
                {
                    module(EModuleAction::load, rendering, 1, EModuleStatus::success, true);
                    break;
                }
                case 1u:
                {
                    transfer(0u, mount_point_ids::render);
                    break;
                }
                case 2u:
                {
                    transfer(1u);
                    break;
                }
                case 3u:
                {
                    const bool replace = scenario("render-replace-dependency");
                    module(replace ? EModuleAction::replace : EModuleAction::unload, replace ? rendering : nullptr,
                        2, EModuleStatus::success, replace);
                    break;
                }
                case 4u:
                {
                    dispose(m_assets[0], EAssetStatus::invalid_asset);
                    break;
                }
                case 5u:
                {
                    dispose(m_assets[1], EAssetStatus::success);
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        if (scenario("disposal"))
        {
            switch (m_step)
            {
                case 0u:
                {
                    module(EModuleAction::load, rendering, 1, EModuleStatus::success, true);
                    break;
                }
                case 1u:
                {
                    transfer(0u, mount_point_ids::render);
                    break;
                }
                case 2u:
                {
                    dispose(m_assets[0], EAssetStatus::success);
                    break;
                }
                case 3u:
                {
                    dispose(m_assets[0], EAssetStatus::invalid_asset);
                    break;
                }
                case 4u:
                {
                    dispose({}, EAssetStatus::invalid_asset);
                    break;
                }
                case 5u:
                {
                    module(EModuleAction::unload, nullptr, 2, EModuleStatus::success, false);
                    break;
                }
                default:
                {
                    stop();
                    break;
                }
            }
            return;
        }
        m_failed = true; // Reject a scenario outside this fixture's maintained cases.
    }
};

}   //  namespace module_lifecycle_tests
