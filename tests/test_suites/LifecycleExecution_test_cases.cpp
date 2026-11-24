
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    LifecycleExecution_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    24 Nov 26
//
//  Lifecycle acceptance-fixture request retention, cancellation and resource custody.

#include "tests/test_suites/ExecutionFixture_test_cases.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/execution_facade_fixture.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include <cstring>

#include "tests/module_lifecycle/service_execution.hpp"
#include "tests/module_lifecycle/executive_execution.hpp"

using tests::SLifecycleExecutiveFacade;

namespace
{
struct SLifecycleServiceFacade
{
    modules::ESubmission owner_result{ modules::ESubmission::busy }, host_result{ modules::ESubmission::busy },
        direct_result{ modules::ESubmission::busy };
    unsigned transfers{ 0u }, acquires{ 0u }, disposals{ 0u }, releases{ 0u }, replies{ 0u }, quiescences{ 0u };
    bool valid{ true }, requests{ false };
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SLifecycleServiceFacade& self = *static_cast<SLifecycleServiceFacade*>(cookie);
        const RawAssetTransfer* const transfer = message.owner().payload<RawAssetTransfer>();
        self.valid &= (destination.kind == modules::SDestination::EKind::host) && transfer &&
            (message.query_async_slot() == 200) && (transfer->storage.value.size() == 16u);
        if (modules::accepted(self.owner_result))
        {
            ++self.transfers;
            message = {};
        }
        return self.owner_result;
    }
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SLifecycleServiceFacade& self = *static_cast<SLifecycleServiceFacade*>(cookie);
        if (destination.kind == modules::SDestination::EKind::connection)
        {
            if (self.requests)
            {
                modules::RunTestsRequest request;
                self.valid &= (destination.token.value == 77u) &&
                    (message.query_async_slot() == (self.replies ? 111 : INT32_MAX)) &&
                    message.copy_payload_to(request) && (request.module == module_ids::executive);
            }
            else
            {
                modules::RunTestsResult result;
                self.valid &= (destination.token.value == 77u) && (message.query_async_slot() == 9) &&
                    message.copy_payload_to(result) && (result.passed == 1u) && (result.failed == 0u);
            }
            if (modules::accepted(self.direct_result)) ++self.replies;
            return self.direct_result;
        }
        if (!modules::accepted(self.host_result)) return self.host_result;
        if (message.is_payload_a<AssetAcquireRequest>()) ++self.acquires;
        else if (message.is_payload_a<AssetDisposeRequest>()) ++self.disposals;
        else if (message.is_payload_a<AssetReleaseRequest>()) ++self.releases;
        else self.valid = false;
        return self.host_result;
    }
    static modules::ESubmission MV_STD_ABI_CALL intent(void* const cookie, const modules::SExecutionIntent& value) noexcept
    {
        SLifecycleServiceFacade& self = *static_cast<SLifecycleServiceFacade*>(cookie);
        if ((value.kind == modules::SExecutionIntent::EKind::participate) && (value.correlation != 0))
        {
            self.valid = false;
            return modules::ESubmission::invalid;
        }
        if (value.kind == modules::SExecutionIntent::EKind::connection_quiescent) ++self.quiescences;
        return modules::ESubmission::accepted;
    }
};

void lifecycle_rendering_conversation(tests::TTestContext& ctx)
{
    tests::TModuleIdScope module_scope{ module_ids::rendering };
    const thread_ids::id_type previous = system_context::get_ambient_thread_id();
    system_context::set_ambient_thread_id(thread_ids::rendering);
    static const modules::SCommunicationV1 functions{ &SLifecycleServiceFacade::pod,
        &SLifecycleServiceFacade::owner, &SLifecycleServiceFacade::intent };
    for (const bool cancelled : { false, true })
    {
        SLifecycleServiceFacade facade;
        facade.requests = true;
        facade.host_result = modules::ESubmission::accepted;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        module_lifecycle_tests::CServiceExecution execution{ context, false, false, cancelled ?
            module_lifecycle_tests::EAcquisitionScenario::rendering_tests_cancel :
            module_lifecycle_tests::EAcquisitionScenario::rendering_tests };
        modules::SInvocation invocation;
        invocation.notification.connection_status = connections::EStatus::success;
        invocation.notification.connection = { 77u };
        invocation.notification.correlation = 90;
        invocation.notification.first = { 11u };
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event)
        {
            invocation.event = event;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        };
        const auto receive = [&](const std::int32_t correlation, const auto& value, const bool direct = false)
        {
            const threading::CErasedPodMsg message = threading::make_pod_message(correlation, value);
            invocation.source = direct ? connections::SSource{ connections::SSource::EKind::direct,
                system_ids::executive, { 11u }, { 77u } } : connections::SSource{};
            invocation.message = &message;
            invoke(modules::EExecutionEvent::message);
            invocation.message = nullptr;
        };
        invoke(modules::EExecutionEvent::start);
        invoke(modules::EExecutionEvent::participation);
        invoke(modules::EExecutionEvent::connection_available);
        invoke(modules::EExecutionEvent::connection_result);
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, (!facade.replies && !progress.work_pending));
        facade.direct_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        receive(INT32_MAX, modules::RunTestsResult{ module_ids::executive, modules::ETestRunStatus::success, 7u, 0u, 2u }, true);
        TEST_EXPECT(ctx, (!facade.transfers && !progress.work_pending));
        facade.owner_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        AssetResult transferred;
        transferred.asset = CAssetId{ 51u };
        transferred.status = EAssetStatus::success;
        receive(200, transferred);
        std::uint8_t bytes[16];
        std::memset(bytes, 0x5a, sizeof(bytes));
        AssetAcquireResult acquired;
        acquired.acquisition = CAssetAcquisitionId{ 91u };
        acquired.status = EAssetStatus::success;
        acquired.views.bytes = CByteConstView{ bytes, sizeof(bytes), 1u };
        facade.host_result = modules::ESubmission::busy;
        receive(201, acquired);
        TEST_EXPECT(ctx, (!facade.disposals && (facade.replies == 1u) && !progress.work_pending));
        facade.direct_result = modules::ESubmission::busy;
        facade.host_result = modules::ESubmission::accepted;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.disposals == 1u) && (facade.replies == 1u) && !progress.work_pending));
        facade.direct_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, (facade.replies == 2u));
        if (!cancelled)
        {
            receive(111, modules::RunTestsResult{ module_ids::executive, modules::ETestRunStatus::failed, 3u, 1u, 2u }, true);
        }
        facade.host_result = modules::ESubmission::busy;
        if (cancelled) invoke(modules::EExecutionEvent::stop);
        invoke(modules::EExecutionEvent::connection_closing);
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, (!facade.quiescences && !facade.releases && !progress.work_pending && !progress.work_complete));
        facade.host_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.releases == 1u) && !facade.quiescences && !progress.work_pending));
        receive(203, AssetReleaseResult{ acquired.acquisition, EAssetStatus::success });
        TEST_EXPECT(ctx, (!facade.quiescences && !progress.work_complete));
        receive(202, AssetDisposeResult{ transferred.asset, EAssetStatus::success });
        TEST_EXPECT(ctx, ((facade.quiescences == 1u) && facade.valid));
        invoke(modules::EExecutionEvent::connection_retired);
        invoke(modules::EExecutionEvent::stop);
        TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && (facade.replies == 2u) &&
            (facade.transfers == 1u) && (facade.acquires == 1u) && (facade.disposals == 1u) && (facade.releases == 1u)));
    }
    {
        SLifecycleServiceFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        module_lifecycle_tests::CServiceExecution execution{ context, false, false,
            module_lifecycle_tests::EAcquisitionScenario::rendering_tests };
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        invocation.event = modules::EExecutionEvent::start;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        invocation.event = modules::EExecutionEvent::participation;
        invocation.notification.connection_status = connections::EStatus::success;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        invocation.event = modules::EExecutionEvent::connection_result;
        invocation.notification.correlation = 90;
        invocation.notification.connection_status = connections::EStatus::unavailable;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::failed));
        invocation.event = modules::EExecutionEvent::service_async;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && !progress.work_pending));
    }
    system_context::set_ambient_thread_id(previous);
}

void lifecycle_rendering_acquisition(tests::TTestContext& ctx)
{
    tests::TModuleIdScope module_scope{ module_ids::rendering };
    const thread_ids::id_type previous = system_context::get_ambient_thread_id();
    system_context::set_ambient_thread_id(thread_ids::rendering);
    static const modules::SCommunicationV1 functions{ &SLifecycleServiceFacade::pod,
        &SLifecycleServiceFacade::owner, &SLifecycleServiceFacade::intent };
    for (const bool abandon : { false, true })
    {
        SLifecycleServiceFacade facade;
        facade.owner_result = facade.host_result = facade.direct_result = modules::ESubmission::accepted;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        module_lifecycle_tests::CServiceExecution execution{ context, false, false, abandon ?
            module_lifecycle_tests::EAcquisitionScenario::rendering_abandon :
            module_lifecycle_tests::EAcquisitionScenario::rendering_release };
        modules::SInvocation invocation;
        invocation.notification.connection_status = connections::EStatus::success;
        invocation.notification.connection = { 77u };
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event)
        {
            invocation.event = event;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        };
        const auto receive = [&](const std::int32_t correlation, const auto& value)
        {
            const threading::CErasedPodMsg message = threading::make_pod_message(correlation, value);
            invocation.message = &message;
            invoke(modules::EExecutionEvent::message);
            invocation.message = nullptr;
        };
        invoke(modules::EExecutionEvent::start);
        invoke(modules::EExecutionEvent::participation);
        invoke(modules::EExecutionEvent::connection_available);
        invocation.source = { connections::SSource::EKind::direct, system_ids::executive, { 11u }, { 77u } };
        receive(9, modules::RunTestsRequest{ module_ids::rendering });
        invocation.source = {};
        AssetResult transferred;
        transferred.asset = CAssetId{ 51u };
        transferred.status = EAssetStatus::success;
        receive(200, transferred);
        std::uint8_t bytes[16];
        std::memset(bytes, 0x5a, sizeof(bytes));
        AssetAcquireResult acquired;
        acquired.acquisition = CAssetAcquisitionId{ 91u };
        acquired.status = EAssetStatus::success;
        acquired.views.bytes = CByteConstView{ bytes, sizeof(bytes), 1u };
        receive(201, acquired);
        TEST_EXPECT(ctx, ((facade.replies == 1u) && !facade.disposals && !progress.work_pending));
        invoke(modules::EExecutionEvent::connection_closing);
        invoke(modules::EExecutionEvent::connection_retired);
        facade.host_result = modules::ESubmission::busy;
        invoke(modules::EExecutionEvent::stop);
        invoke(modules::EExecutionEvent::provider_admission_closed);
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_complete && !progress.work_pending && !facade.disposals && !facade.releases));
        }
        facade.host_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.disposals == 1u) && (facade.releases == (abandon ? 0u : 1u)) &&
            (execution.complete() == abandon)));
        // Once the abandoned view is relinquished, later callbacks must not read it.
        if (abandon) std::memset(bytes, 0, sizeof(bytes));
        invoke(modules::EExecutionEvent::service_async);
        if (!abandon)
        {
            receive(202, AssetDisposeResult{ transferred.asset, EAssetStatus::success });
            TEST_EXPECT(ctx, !execution.complete());
            receive(203, AssetReleaseResult{ acquired.acquisition, EAssetStatus::success });
        }
        TEST_EXPECT(ctx, (execution.complete() && facade.valid && (facade.disposals == 1u) &&
            (facade.releases == (abandon ? 0u : 1u))));
    }
    system_context::set_ambient_thread_id(previous);
}

void lifecycle_service_obligations(tests::TTestContext& ctx)
{
    tests::TModuleIdScope module_scope{ module_ids::conditioning_general };
    const thread_ids::id_type previous = system_context::get_ambient_thread_id();
    system_context::set_ambient_thread_id(thread_ids::simulation);
    static const modules::SCommunicationV1 functions{ &SLifecycleServiceFacade::pod,
        &SLifecycleServiceFacade::owner, &SLifecycleServiceFacade::intent };
    // Stop at each custody boundary, plus a complete conversation with retained output.
    for (unsigned scenario = 0u; scenario < 7u; ++scenario)
    {
        const unsigned stop_at = (scenario < 5u) ? scenario : 4u;
        SLifecycleServiceFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        module_lifecycle_tests::CServiceExecution service{ context, true, false };
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event) noexcept
        {
            modules::SInvocation invocation;
            invocation.event = event;
            invocation.notification.connection = { 77u };
            invocation.notification.connection_status = connections::EStatus::success;
            progress = {};
            TEST_EXPECT(ctx, (service.handle(invocation, progress) == modules::EExecutionStatus::success));
        };
        const auto receive = [&](const std::int32_t correlation, const auto& value, const bool direct = false,
            const modules::EExecutionStatus expected = modules::EExecutionStatus::success) noexcept
        {
            const threading::CErasedPodMsg message = threading::make_pod_message(correlation, value);
            modules::SInvocation invocation;
            invocation.event = modules::EExecutionEvent::message;
            invocation.message = &message;
            if (direct) invocation.source = { connections::SSource::EKind::direct, system_ids::executive, { 11u }, { 77u } };
            progress = {};
            TEST_EXPECT(ctx, (service.handle(invocation, progress) == expected));
        };
        invoke(modules::EExecutionEvent::start);
        invoke(modules::EExecutionEvent::participation);
        invoke(modules::EExecutionEvent::connection_available);
        receive(9, modules::RunTestsRequest{ module_ids::conditioning_general }, true);
        for (unsigned attempt = 0u; attempt < 3u; ++attempt)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete && (facade.transfers == 0u)));
        }
        if (stop_at == 0u)
        {
            invoke(modules::EExecutionEvent::stop);
            TEST_EXPECT(ctx, (service.complete() && (facade.transfers == 0u)));
            continue;
        }
        facade.owner_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.transfers == 1u) && !progress.work_pending));
        if (stop_at == 1u) invoke(modules::EExecutionEvent::stop);
        const CAssetId asset{ 51u };
        AssetResult transferred;
        transferred.asset = asset;
        transferred.status = EAssetStatus::success;
        receive(200, transferred);
        TEST_EXPECT(ctx, (!progress.work_complete && !progress.work_pending && (facade.acquires == 0u)));
        if (stop_at == 2u) invoke(modules::EExecutionEvent::stop);
        facade.host_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        if (stop_at < 3u)
        {
            TEST_EXPECT(ctx, ((facade.acquires == 0u) && (facade.disposals == 1u) && !service.complete()));
            receive(202, AssetDisposeResult{ asset, EAssetStatus::success });
            TEST_EXPECT(ctx, (service.complete() && (facade.releases == 0u) && (facade.replies == 0u)));
            continue;
        }
        TEST_EXPECT(ctx, ((facade.acquires == 1u) && (facade.disposals == 0u)));
        if (stop_at == 3u) invoke(modules::EExecutionEvent::stop);
        std::uint8_t bytes[16];
        std::memset(bytes, 0x6b, sizeof(bytes));
        AssetAcquireResult acquired;
        acquired.acquisition = CAssetAcquisitionId{ 91u };
        acquired.status = EAssetStatus::success;
        acquired.views.bytes = CByteConstView{ bytes, sizeof(bytes), 1u };
        facade.host_result = modules::ESubmission::busy;
        receive(201, acquired);
        TEST_EXPECT(ctx, (!progress.work_pending && !service.complete() && (facade.disposals == 0u)));
        facade.host_result = modules::ESubmission::accepted;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.disposals == 1u) && (facade.replies == 0u)));
        if (stop_at == 4u)
        {
            TEST_EXPECT(ctx, !progress.work_pending);
            facade.direct_result = modules::ESubmission::published_wake_failed;
            for (unsigned turn = 0u; turn < 4u; ++turn) invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, ((facade.replies == 16u) && !progress.work_pending));
            invoke(modules::EExecutionEvent::connection_closing);
            TEST_EXPECT(ctx, (facade.quiescences == 1u));
            facade.host_result = modules::ESubmission::busy;
            invoke(modules::EExecutionEvent::stop);
            TEST_EXPECT(ctx, ((facade.releases == 0u) && !progress.work_pending && !service.complete()));
            invoke(modules::EExecutionEvent::provider_admission_closed);
            TEST_EXPECT(ctx, !service.complete());
            facade.host_result = modules::ESubmission::published_wake_failed;
            invoke(modules::EExecutionEvent::service_async);
        }
        TEST_EXPECT(ctx, ((facade.releases == 1u) && !service.complete()));
        // Disposal may be observed before release acknowledgement; both are required.
        receive(202, AssetDisposeResult{ asset, (scenario == 5u) ? EAssetStatus::invalid_request : EAssetStatus::success }, false,
            (scenario == 5u) ? modules::EExecutionStatus::failed : modules::EExecutionStatus::success);
        TEST_EXPECT(ctx, !service.complete());
        receive(203, AssetReleaseResult{ acquired.acquisition, (scenario == 6u) ? EAssetStatus::invalid_request : EAssetStatus::success }, false,
            (scenario == 6u) ? modules::EExecutionStatus::failed : modules::EExecutionStatus::success);
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((service.complete() == (scenario < 5u)) && !progress.work_pending &&
            facade.valid && (facade.disposals == 1u) && (facade.releases == 1u)));
    }
    for (const bool acquire_failure : { false, true })
    {
        SLifecycleServiceFacade facade;
        facade.owner_result = facade.host_result = modules::ESubmission::accepted;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        module_lifecycle_tests::CServiceExecution service{ context, false, false };
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        invocation.event = modules::EExecutionEvent::connection_available;
        invocation.notification.connection = { 77u };
        TEST_EXPECT(ctx, (service.handle(invocation, progress) == modules::EExecutionStatus::success));
        threading::CErasedPodMsg message = threading::make_pod_message(9, modules::RunTestsRequest{ module_ids::conditioning_general });
        invocation.event = modules::EExecutionEvent::message;
        invocation.source = { connections::SSource::EKind::direct, system_ids::executive, { 11u }, { 77u } };
        invocation.message = &message;
        TEST_EXPECT(ctx, (service.handle(invocation, progress) == modules::EExecutionStatus::success));
        const CAssetId asset{ 51u };
        AssetResult transferred;
        transferred.status = acquire_failure ? EAssetStatus::success : EAssetStatus::invalid_request;
        if (acquire_failure) transferred.asset = asset;
        message = threading::make_pod_message(200, transferred);
        invocation.source = {};
        if (acquire_failure)
        {
            TEST_EXPECT(ctx, (service.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (facade.acquires == 1u));
            message = threading::make_pod_message(201, AssetAcquireResult{});
        }
        TEST_EXPECT(ctx, (service.handle(invocation, progress) == modules::EExecutionStatus::failed));
        TEST_EXPECT(ctx, ((facade.disposals == (acquire_failure ? 1u : 0u)) && (facade.releases == 0u)));
        invocation.event = modules::EExecutionEvent::service_async;
        invocation.message = nullptr;
        progress = {};
        TEST_EXPECT(ctx, (service.handle(invocation, progress) == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, ((progress.work_complete != 0u) == !acquire_failure));
        if (acquire_failure)
        {
            message = threading::make_pod_message(202, AssetDisposeResult{ asset, EAssetStatus::success });
            invocation.event = modules::EExecutionEvent::message;
            invocation.message = &message;
            TEST_EXPECT(ctx, (service.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (progress.work_complete && (facade.disposals == 1u)));
        }
        TEST_EXPECT(ctx, (service.complete() && facade.valid));
    }
    system_context::set_ambient_thread_id(previous);
}
}

namespace
{
void lifecycle_executive_requests(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ nullptr, &SLifecycleExecutiveFacade::owner };
    for (const bool shutdown : { false, true })
    {
        SLifecycleExecutiveFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        module_lifecycle_tests::CExecutiveExecution execution{ context, shutdown ? "shutdown" : "ordinary" };
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        invocation.event = modules::EExecutionEvent::start;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        invocation.event = modules::EExecutionEvent::service_async;
        for (unsigned attempt = 0u; attempt < 4u; ++attempt)
        {
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete && (facade.accepted == 0u)));
        }
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        const unsigned attempts = facade.attempts;
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        }
        TEST_EXPECT(ctx, ((facade.accepted == 1u) && (facade.attempts == attempts)));
        if (!shutdown)
        {
            TEST_EXPECT(ctx, ((facade.module == module_ids::rendering) && (facade.action == modules::EModuleAction::load) &&
                (facade.correlation == 1)));
            modules::ModuleResult result;
            result.module = module_ids::rendering;
            result.status = modules::EModuleStatus::success;
            result.notice = modules::EModuleNotice::acknowledged;
            threading::CErasedPodMsg message = threading::make_pod_message(1, result);
            invocation.event = modules::EExecutionEvent::message;
            invocation.message = &message;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (facade.attempts == attempts));
            result.notice = modules::EModuleNotice::completed;
            result.available = true;
            message = threading::make_pod_message(1, result);
            facade.result = modules::ESubmission::busy;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, ((facade.attempts == (attempts + 1u)) && (facade.accepted == 1u) && !progress.work_pending));
        }
        else
        {
            TEST_EXPECT(ctx, ((facade.module == module_ids::executive) && (facade.action == modules::EModuleAction::unload) &&
                (facade.correlation == 100)));
        }
        invocation.event = modules::EExecutionEvent::stop;
        invocation.message = nullptr;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && !progress.work_pending && facade.valid));
    }
}

void lifecycle_audio_requests(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SLifecycleExecutiveFacade::pod,
        &SLifecycleExecutiveFacade::owner, &SLifecycleExecutiveFacade::intent };
    for (const bool stop_before_acceptance : { false, true })
    {
        SLifecycleExecutiveFacade facade;
        facade.result = modules::ESubmission::accepted;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        module_lifecycle_tests::CExecutiveExecution execution{ context, "audio-lifecycle" };
        modules::SInvocation invocation;
        invocation.notification.connection_status = connections::EStatus::success;
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event)
        {
            invocation.event = event;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        };
        const auto module_result = [&](const std::int32_t correlation, const modules::EModuleNotice notice, const bool available)
        {
            modules::ModuleResult result;
            result.module = module_ids::audio;
            result.status = modules::EModuleStatus::success;
            result.notice = notice;
            result.available = available;
            threading::CErasedPodMsg message = threading::make_pod_message(correlation, result);
            invocation.source = {};
            invocation.message = &message;
            invoke(modules::EExecutionEvent::message);
            invocation.message = nullptr;
        };
        const auto test_result = [&](const std::int32_t correlation, const bool unavailable)
        {
            const modules::RunTestsResult result{ module_ids::audio,
                unavailable ? modules::ETestRunStatus::unavailable : modules::ETestRunStatus::success,
                unavailable ? 0u : 1u, 0u, unavailable ? 1u : 0u };
            threading::CErasedPodMsg message = threading::make_pod_message(correlation, result);
            invocation.source = { connections::SSource::EKind::direct,
                modules::test_connections::address(module_ids::audio), { 27u }, { 17u } };
            invocation.message = &message;
            invoke(modules::EExecutionEvent::message);
            invocation.message = nullptr;
        };
        invoke(modules::EExecutionEvent::start);
        invoke(modules::EExecutionEvent::participation);
        module_result(1, modules::EModuleNotice::acknowledged, false);
        module_result(1, modules::EModuleNotice::completed, true);
        TEST_EXPECT(ctx, ((facade.accepted == 1u) && (facade.connections == 1u)));
        invocation.notification.connection = { 17u };
        invocation.notification.first = { 27u };
        invoke(modules::EExecutionEvent::connection_available);
        invocation.notification.correlation = 50;
        invoke(modules::EExecutionEvent::connection_result);
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!facade.pods && !progress.work_pending && progress.direct_input));
        }
        facade.pod_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.pods == 1u) && (facade.pod_slots[0] == 2)));
        test_result(2, false);
        TEST_EXPECT(ctx, ((facade.pods == 2u) && (facade.pod_slots[1] == 3)));
        facade.pod_result = modules::ESubmission::busy;
        test_result(3, true);
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.pods == 2u) && (facade.accepted == 1u) && !progress.work_pending));
        if (!stop_before_acceptance)
        {
            facade.pod_result = modules::ESubmission::published_wake_failed;
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, ((facade.pods == 3u) && (facade.pod_slots[2] == 5) && !progress.direct_input));
            facade.result = modules::ESubmission::busy;
            invoke(modules::EExecutionEvent::service_async);
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, ((facade.accepted == 1u) && !progress.work_pending && !progress.direct_input));
            invoke(modules::EExecutionEvent::connection_closing);
            invoke(modules::EExecutionEvent::connection_retired);
            facade.result = modules::ESubmission::accepted;
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, ((facade.accepted == 2u) && (facade.action == modules::EModuleAction::unload) &&
                (facade.module == module_ids::audio) && (facade.correlation == 4) && (facade.quiescences == 1u)));
            module_result(4, modules::EModuleNotice::acknowledged, true);
            module_result(4, modules::EModuleNotice::completed, false);
            TEST_EXPECT(ctx, ((facade.accepted == 3u) && (facade.module == module_ids::executive)));
        }
        invoke(modules::EExecutionEvent::stop);
        TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && facade.valid &&
            (facade.pods == (stop_before_acceptance ? 2u : 3u))));
    }
}
}

namespace
{
struct SLifecycleSaveFacade
{
    bool busy_owner{ true }, valid{ true }, dispose{ false };
    unsigned saves{ 0u }, controls{ 0u }, rejected{ 0u }, pods{ 0u }, pod_allowance{ 0u };
    std::int32_t pod_base{ 30 };
    CAssetId asset{ 81u }, second{ 82u };

    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SLifecycleSaveFacade& self = *static_cast<SLifecycleSaveFacade*>(cookie);
        self.valid &= (destination.kind == modules::SDestination::EKind::host) && message.has_owner();
        if (!self.valid) return modules::ESubmission::invalid;
        if (self.busy_owner) return modules::ESubmission::busy;
        if (const AssetSaveRequest* const request = message.owner().payload<AssetSaveRequest>())
        {
            self.valid &= (request->source == self.asset) && (request->settings.format == EAssetFileFormat::raw);
            if (message.query_async_slot() == 300) ++self.rejected;
            else
            {
                self.valid &= message.query_async_slot() == (200 + static_cast<std::int32_t>(self.saves));
                ++self.saves;
            }
        }
        else if (const modules::ModuleRequest* const request = message.owner().payload<modules::ModuleRequest>())
        {
            self.valid &= (self.saves == 32u) && (message.query_async_slot() == 40) &&
                (request->module == module_ids::rendering) &&
                (request->action == (self.dispose ? modules::EModuleAction::load : modules::EModuleAction::unload));
            ++self.controls;
        }
        else self.valid = false;
        message.take_owner().destroy();
        return modules::ESubmission::published_wake_failed;
    }
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SLifecycleSaveFacade& self = *static_cast<SLifecycleSaveFacade*>(cookie);
        AssetDisposeRequest request;
        self.valid &= (destination.kind == modules::SDestination::EKind::host) &&
            message.copy_payload_to(request) && (message.query_async_slot() == (self.pod_base + static_cast<std::int32_t>(self.pods))) &&
            (request.asset == (((self.pod_base == 600) && self.pods) ? self.second : self.asset));
        if (!self.valid) return modules::ESubmission::invalid;
        if (!self.pod_allowance) return modules::ESubmission::busy;
        --self.pod_allowance;
        ++self.pods;
        return modules::ESubmission::published_wake_failed;
    }
};

void lifecycle_save_requests(tests::TTestContext& ctx)
{
    tests::TModuleIdScope scope{ module_ids::executive };
    static const modules::SCommunicationV1 functions{ &SLifecycleSaveFacade::pod, &SLifecycleSaveFacade::owner };
    for (const bool dispose : { false, true })
    {
        SLifecycleSaveFacade facade;
        facade.dispose = dispose;
        const modules::CCommunication communication{ functions, &facade };
        module_lifecycle_tests::CSaveExecution saves{ communication };
        saves.start(facade.asset, dispose);
        TEST_EXPECT(ctx, saves.advance());
        TEST_EXPECT(ctx, (!facade.saves && !saves.runnable() && !saves.complete()));
        facade.busy_owner = false;
        for (unsigned turn = 0u; turn < 8u; ++turn)
        {
            TEST_EXPECT(ctx, saves.advance());
            TEST_EXPECT(ctx, ((facade.saves == ((turn + 1u) << 2u)) && !facade.controls && saves.runnable()));
        }
        facade.busy_owner = true;
        TEST_EXPECT(ctx, saves.advance());
        TEST_EXPECT(ctx, (!saves.runnable() && !facade.controls));
        facade.busy_owner = false;
        TEST_EXPECT(ctx, saves.advance());
        TEST_EXPECT(ctx, ((facade.controls == 1u) && !facade.pods && !saves.runnable()));
        modules::ModuleResult module;
        module.module = module_ids::rendering;
        module.notice = modules::EModuleNotice::acknowledged;
        module.status = modules::EModuleStatus::success;
        TEST_EXPECT(ctx, saves.receive(threading::make_pod_message(40, module)));
        if (dispose)
        {
            TEST_EXPECT(ctx, saves.advance());
            TEST_EXPECT(ctx, (!facade.pods && !saves.runnable()));
            facade.pod_allowance = 1u;
            TEST_EXPECT(ctx, saves.advance());
            TEST_EXPECT(ctx, ((facade.pods == 1u) && !saves.runnable()));
            TEST_EXPECT(ctx, !saves.receive(threading::make_pod_message(30,
                AssetDisposeResult{ facade.asset, EAssetStatus::success })));
            facade.pod_allowance = 1u;
            TEST_EXPECT(ctx, saves.advance());
            TEST_EXPECT(ctx, ((facade.pods == 2u) && !saves.runnable()));
            TEST_EXPECT(ctx, saves.receive(threading::make_pod_message(31,
                AssetDisposeResult{ facade.asset, EAssetStatus::invalid_asset })));
            facade.busy_owner = true;
            TEST_EXPECT(ctx, saves.advance());
            TEST_EXPECT(ctx, (!facade.rejected && !saves.runnable()));
            facade.busy_owner = false;
            TEST_EXPECT(ctx, saves.advance());
            AssetResult rejected;
            rejected.status = EAssetStatus::invalid_asset;
            TEST_EXPECT(ctx, saves.receive(threading::make_pod_message(300, rejected)));
            TEST_EXPECT(ctx, (facade.rejected == 1u));
        }
        else
        {
            module.notice = modules::EModuleNotice::completed;
            TEST_EXPECT(ctx, !saves.receive(threading::make_pod_message(40, module)));
        }
        AssetResult saved;
        saved.asset = facade.asset;
        saved.status = EAssetStatus::success;
        for (std::int32_t slot = 231; slot >= 200; --slot)
        {
            TEST_EXPECT(ctx, saves.receive(threading::make_pod_message(slot, saved)));
        }
        TEST_EXPECT(ctx, !saves.receive(threading::make_pod_message(200, saved)));
        module.notice = modules::EModuleNotice::completed;
        module.status = dispose ? modules::EModuleStatus::already_loaded : modules::EModuleStatus::success;
        module.available = dispose;
        TEST_EXPECT(ctx, saves.receive(threading::make_pod_message(40, module)));
        if (dispose)
        {
            TEST_EXPECT(ctx, !saves.complete());
            TEST_EXPECT(ctx, saves.receive(threading::make_pod_message(30,
                AssetDisposeResult{ facade.asset, EAssetStatus::success })));
        }
        TEST_EXPECT(ctx, (saves.complete() && facade.valid && !saves.runnable()));
    }
    for (const bool reply_before_second : { false, true })
    {
        SLifecycleSaveFacade facade;
        facade.pod_base = 600;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        module_lifecycle_tests::CFinalDisposalExecution execution{ context };
        execution.configure(facade.asset, facade.second);
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event)
        {
            invocation.event = event;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        };
        invoke(modules::EExecutionEvent::start);
        TEST_EXPECT(ctx, (progress.ready && !progress.work_pending && !facade.pods));
        invoke(modules::EExecutionEvent::stop);
        TEST_EXPECT(ctx, (!progress.work_complete && !progress.work_pending && !facade.pods));
        facade.pod_allowance = 1u;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.pods == 1u) && !progress.work_complete && !progress.work_pending));
        if (reply_before_second)
        {
            const threading::CErasedPodMsg result = threading::make_pod_message(600,
                AssetDisposeResult{ facade.asset, EAssetStatus::success });
            invocation.message = &result;
            invoke(modules::EExecutionEvent::message);
            invocation.message = nullptr;
        }
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.pods == 1u) && !execution.complete()));
        facade.pod_allowance = 1u;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.pods == 2u) && progress.work_complete && !progress.work_pending));
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.pods == 2u) && execution.complete() && facade.valid));
    }
}
}

namespace module_lifecycle_tests
{
struct SBundleExecutionTestAccess
{
    // Controlled boundary after the two named JSON loads were accepted. Real
    // worker/cache publication and bundle bytes are covered by the DLL cases.
    static void await_parses(CBundleExecution& execution) noexcept
    {
        execution.m_scenario = CBundleExecution::EScenario::workflow;
        execution.m_stage = 14u;
        execution.m_parse_slots[0] = 2001;
        execution.m_parse_slots[1] = 2002;
    }
};
}

namespace
{
struct SLifecycleBundleFacade
{
    bool busy{ true }, valid{ true };
    modules::ESubmission result{ modules::ESubmission::published_wake_failed };
    unsigned owners{ 0u }, pods{ 0u };
    std::int32_t last_slot{ -1 };
    threading::CErasedPodMsg last_pod;
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SLifecycleBundleFacade& self = *static_cast<SLifecycleBundleFacade*>(cookie);
        self.valid &= (destination.kind == modules::SDestination::EKind::host) && message.has_owner();
        if (self.busy) return modules::ESubmission::busy;
        if (!modules::accepted(self.result)) return self.result;
        self.last_slot = message.query_async_slot();
        ++self.owners;
        message = {};
        return self.result;
    }
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SLifecycleBundleFacade& self = *static_cast<SLifecycleBundleFacade*>(cookie);
        self.valid &= destination.kind == modules::SDestination::EKind::host;
        if (self.busy) return modules::ESubmission::busy;
        if (!modules::accepted(self.result)) return self.result;
        self.last_slot = message.query_async_slot();
        self.last_pod = message;
        ++self.pods;
        return self.result;
    }
};

void lifecycle_bundle_requests(tests::TTestContext& ctx)
{
    tests::TModuleIdScope scope{ module_ids::executive };
    static const modules::SCommunicationV1 functions{ &SLifecycleBundleFacade::pod, &SLifecycleBundleFacade::owner };
    SLifecycleBundleFacade facade;
    const modules::CCommunication communication{ functions, &facade };
    modules::SExecutionContext context;
    context.communication = &communication;
    {
        module_lifecycle_tests::CBundleExecution missing{ context };
        TEST_EXPECT(ctx, !missing.begin("bundle-batch-stop"));
        threading::SBatchShared shared;
        threading::CThreadControlState control;
        std::atomic<std::uint32_t> pending{ 0u };
        threading::CBatchClient batch{ shared, control, pending, module_ids::executive, nullptr };
        modules::SExecutionContext batch_context = context;
        batch_context.batch = &batch;
        module_lifecycle_tests::CBundleExecution skipped{ batch_context };
        TEST_EXPECT(ctx, skipped.begin("bundle-batch-stop"));
        TEST_EXPECT(ctx, (skipped.complete() && skipped.quiescent() && !skipped.runnable() && !skipped.poll_batch_start()));
        TEST_EXPECT(ctx, (skipped.advance() && skipped.service_batch() && !batch.outstanding()));
        skipped.cancel();
        TEST_EXPECT(ctx, (skipped.complete() && skipped.quiescent() && !facade.owners && !facade.pods));
        shared.runner_count = 1u;
        module_lifecycle_tests::CBundleExecution queued{ batch_context };
        TEST_EXPECT(ctx, (queued.begin("bundle-batch-stop") && !queued.complete()));
    }
    module_lifecycle_tests::CBundleExecution execution{ context };
    TEST_EXPECT(ctx, execution.begin("bundle-resume"));
    const auto publish = [&]
    {
        facade.busy = true;
        const unsigned owners = facade.owners;
        const unsigned pods = facade.pods;
        TEST_EXPECT(ctx, execution.advance());
        TEST_EXPECT(ctx, execution.advance());
        TEST_EXPECT(ctx, ((facade.owners == owners) && (facade.pods == pods) && !execution.runnable() && !execution.complete()));
        facade.busy = false;
        TEST_EXPECT(ctx, execution.advance());
        TEST_EXPECT(ctx, (((facade.owners + facade.pods) == (owners + pods + 1u)) && !execution.runnable()));
        TEST_EXPECT(ctx, execution.advance());
        TEST_EXPECT(ctx, ((facade.owners + facade.pods) == (owners + pods + 1u)));
    };
    const auto receive = [&](const auto& value)
    {
        TEST_EXPECT(ctx, execution.receive(threading::make_pod_message(facade.last_slot, value)));
    };
    publish(); // Load replacement handoff.
    CAssetId handoff[]{ CAssetId{ 81u }, CAssetId{ 82u } };
    AssetResult result;
    result.asset = CAssetId{ 101u };
    result.status = EAssetStatus::success;
    result.set_raw_view({ handoff, sizeof(handoff), 1u });
    receive(result);
    publish(); // Dispose loaded handoff only after copying the value identities.
    AssetDisposeRequest disposal;
    TEST_EXPECT(ctx, (facade.last_pod.copy_payload_to(disposal) && (disposal.asset == result.asset)));
    handoff[0] = handoff[1] = {};
    receive(AssetDisposeResult{ result.asset, EAssetStatus::success });
    publish();
    filesystem::FilesystemRefreshResult refresh;
    refresh.status = filesystem::EFilesystemStatus::invalid_root;
    receive(refresh);
    publish();
    BundleReferenceRequest reference;
    TEST_EXPECT(ctx, (facade.last_pod.copy_payload_to(reference) && (reference.source == CAssetId{ 81u })));
    result = {};
    result.status = EAssetStatus::invalid_asset;
    receive(result);
    publish();
    result.status = EAssetStatus::busy;
    receive(result);
    publish();
    result.status = EAssetStatus::success;
    result.asset = CAssetId{ 82u };
    receive(result);
    publish();
    TEST_EXPECT(ctx, (facade.last_pod.copy_payload_to(disposal) && (disposal.asset == CAssetId{ 82u })));
    receive(AssetDisposeResult{ CAssetId{ 82u }, EAssetStatus::success });
    publish();
    result.asset = CAssetId{ 83u };
    receive(result);
    publish(); // No original-byte access after accepted closure.
    TEST_EXPECT(ctx, (facade.last_pod.copy_payload_to(disposal) && (disposal.asset == CAssetId{ 83u })));
    TEST_EXPECT(ctx, !execution.complete());
    TEST_EXPECT(ctx, execution.receive(threading::make_pod_message(-1, BundleDisposedNotification{ CAssetId{ 83u } })));
    TEST_EXPECT(ctx, execution.advance());
    TEST_EXPECT(ctx, (execution.complete() && !execution.runnable() && facade.valid && (facade.owners == 5u) && (facade.pods == 4u)));

    // Stop cancels an unsent owner; acceptance instead leaves its unread outcome
    // to common quiescence. Neither cancellation path may publish it again.
    for (const bool accepted : { false, true })
    {
        SLifecycleBundleFacade stopped;
        const modules::CCommunication stopped_communication{ functions, &stopped };
        context.communication = &stopped_communication;
        module_lifecycle_tests::CBundleExecution cancelled{ context };
        TEST_EXPECT(ctx, cancelled.begin("bundle-resume"));
        stopped.busy = !accepted;
        TEST_EXPECT(ctx, cancelled.advance());
        cancelled.cancel();
        stopped.busy = false;
        TEST_EXPECT(ctx, cancelled.advance());
        TEST_EXPECT(ctx, (cancelled.complete() && !cancelled.runnable() && (stopped.owners == (accepted ? 1u : 0u))));
    }
    CLiveDocument document;
    static constexpr char text[]{ "{\"value\":7}" };
    TEST_EXPECT(ctx, document_parser::parse(CByteConstView{
        reinterpret_cast<const std::uint8_t*>(text), (sizeof(text) - 1u) }, document).accepted());
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, document_translation::bake(document, baked));
    context.communication = &communication;
    module_lifecycle_tests::CBundleExecution parsed{ context };
    module_lifecycle_tests::SBundleExecutionTestAccess::await_parses(parsed);
    TEST_EXPECT(ctx, parsed.advance());
    TEST_EXPECT(ctx, !parsed.runnable());
    result = {};
    result.status = EAssetStatus::success;
    result.asset = CAssetId{ 90u };
    result.set_baked_view({ { baked.bytes().data(), baked.bytes().size(), 1u }, baked.document() });
    TEST_EXPECT(ctx, parsed.receive(threading::make_pod_message(2002, result)));
    TEST_EXPECT(ctx, !parsed.runnable());
    TEST_EXPECT(ctx, !parsed.receive(threading::make_pod_message(2002, result)));
    TEST_EXPECT(ctx, !parsed.receive(threading::make_pod_message(2003, result)));
    result.asset = CAssetId{ 91u };
    TEST_EXPECT(ctx, parsed.receive(threading::make_pod_message(2001, result)));
    TEST_EXPECT(ctx, parsed.runnable());
    parsed.cancel();

    tests::TModuleIdScope rendering_scope{ module_ids::rendering };
    for (const unsigned scenario : { 0u, 1u, 2u })
    {
        SLifecycleBundleFacade rendering;
        const modules::CCommunication rendering_communication{ functions, &rendering };
        context.communication = &rendering_communication;
        module_lifecycle_tests::CBundleExecution owner{ context };
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event)
        {
            invocation.event = event;
            TEST_EXPECT(ctx, (owner.handle_rendering(invocation, progress) == modules::EExecutionStatus::success));
        };
        const auto unblock = [&]
        {
            TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete));
            rendering.busy = false;
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete));
        };
        const auto receive_owner = [&](const std::int32_t slot, const auto& value)
        {
            rendering.busy = true;
            const threading::CErasedPodMsg message = threading::make_pod_message(slot, value);
            invocation.message = &message;
            invoke(modules::EExecutionEvent::message);
            invocation.message = nullptr;
        };
        invoke(modules::EExecutionEvent::start);
        TEST_EXPECT(ctx, progress.ready);
        unblock();
        AssetResult asset;
        asset.status = EAssetStatus::success;
        asset.asset = CAssetId{ 81u };
        receive_owner(rendering.last_slot, asset); // Baked original -> save.
        unblock();
        receive_owner(rendering.last_slot, asset); // Save -> original closure.
        unblock();
        receive_owner(-1, BundleDisposedNotification{ asset.asset });
        unblock();
        asset.asset = CAssetId{ 82u };
        receive_owner(rendering.last_slot, asset); // Loaded replacement -> sub-file.
        unblock();
        asset.asset = CAssetId{ 83u };
        receive_owner(rendering.last_slot, asset); // Sub-file -> saved handoff.
        unblock();
        if (scenario == 0u)
        {
            asset.asset = {};
            receive_owner(rendering.last_slot, asset);
            TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete));
        }
        // Other cases stop before consuming the accepted handoff's result.
        rendering.busy = true;
        invoke(modules::EExecutionEvent::stop);
        TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete && (rendering.pods == 1u)));
        rendering.busy = false;
        if (scenario == 2u)
        {
            rendering.result = modules::ESubmission::invalid;
            invocation.event = modules::EExecutionEvent::service_async;
            TEST_EXPECT(ctx, (owner.handle_rendering(invocation, progress) == modules::EExecutionStatus::failed));
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (progress.work_complete && (rendering.pods == 1u)));
        }
        else
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_complete && progress.work_pending && (rendering.pods == 2u)));
            AssetDisposeRequest closed;
            TEST_EXPECT(ctx, (rendering.last_pod.copy_payload_to(closed) && (closed.asset == CAssetId{ 82u })));
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (progress.work_complete && !progress.work_pending));
        }
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, (owner.complete() && rendering.valid && (rendering.owners == 5u)));
    }
}
}


void run_lifecycle_fixture_tests(tests::TTestContext& ctx)
{
    lifecycle_service_obligations(ctx);
    lifecycle_rendering_acquisition(ctx);
    lifecycle_rendering_conversation(ctx);
    lifecycle_executive_requests(ctx);
    lifecycle_save_requests(ctx);
    lifecycle_bundle_requests(ctx);
    lifecycle_audio_requests(ctx);
}

void run_lifecycle_executive_fixture_tests(tests::TTestContext& ctx)
{
    lifecycle_executive_requests(ctx);
    lifecycle_audio_requests(ctx);
    lifecycle_save_requests(ctx);
    lifecycle_bundle_requests(ctx);
}

void run_lifecycle_service_fixture_tests(tests::TTestContext& ctx)
{
    lifecycle_service_obligations(ctx);
    lifecycle_rendering_acquisition(ctx);
    lifecycle_rendering_conversation(ctx);
}
