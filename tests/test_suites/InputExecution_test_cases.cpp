
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    InputExecution_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    24 Nov 26
//
//  Input acceptance-fixture source, borrow, shutdown and freeze custody.

#include "tests/test_suites/ExecutionFixture_test_cases.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/execution_facade_fixture.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include <cstring>

#include "tests/input_host/source_execution.hpp"
#include "tests/input_host/borrow_execution.hpp"
#include "tests/input_host/execution.hpp"

namespace input_host
{
struct SInputExecutionTestAccess
{
    static bool prepare_freeze(CInputExecution& execution, SShared& shared,
        const platform::input::SInputEndpointV4& view) noexcept
    {
        execution.native = true;
        execution.shared = &shared;
        execution.control_route = { 19u };
        execution.input_route = { 17u };
        execution.next_stage(CInputExecution::EStage::freeze);
        services::ServiceRequest request;
        request.route = { 17u };
        request.request = { 29u };
        request.opcode = 32u;
        if (!execution.borrows.begin(3u, request)) return false;
        bool runnable = false;
        std::uint64_t wake = UINT64_MAX;
        execution.borrows.advance(runnable, wake);
        services::ServiceReply reply;
        reply.route = request.route;
        reply.request = request.request;
        reply.opcode = request.opcode;
        reply.status = services::EServiceStatus::success;
        reply.notice = services::EServiceNotice::acknowledged;
        execution.borrows.receive(threading::make_pod_message(19, reply));
        reply.notice = services::EServiceNotice::prepared;
        const services::SPreparedServiceBorrow prepared{ { 31u }, { &view, sizeof(view), 1u } };
        reply.payload_size = sizeof(prepared);
        std::memcpy(reply.payload, &prepared, sizeof(prepared));
        execution.borrows.receive(threading::make_pod_message(19, reply));
        reply.notice = services::EServiceNotice::completed;
        reply.payload_size = 0u;
        execution.borrows.receive(threading::make_pod_message(19, reply));
        execution.pipes[0].view = &view;
        return !execution.borrows.failed() && execution.pipes[0].reader.emplace() &&
            execution.pipes[0].reader->install(view, execution.instance);
    }
    static bool expire_freeze(CInputExecution& execution, const bool stage) noexcept
    {
        return (stage ? execution.stage_deadline : execution.operation.deadline).set_after_milliseconds(0u).code ==
            platform::threading::EWaitError::none;
    }
    static bool reader_closed(CInputExecution& execution) noexcept
    {
        platform::input::SInputEvent event;
        return !execution.shared && !execution.pipes[0].view && execution.borrows.record(3u).local_closed &&
            (execution.pipes[0].reader->next(event) == platform::input::EInputRead::closing);
    }
    static services::ServiceRequest freeze_request(const CInputExecution& execution) noexcept { return execution.operation.request; }
    static bool retained_freeze(const CInputExecution& execution) noexcept { return execution.freeze_timeout_cleanup && execution.operation.sent; }
    static bool unsafe(const CInputExecution& execution) noexcept { return execution.unsafe; }
    static void refuse_freeze_exception(CInputExecution& execution, const unsigned mode) noexcept
    {
        if (mode == 1u) execution.native = false;
        if (mode == 2u) execution.operation.request.opcode = 3u;
        if (mode == 3u) execution.unsafe = true;
        if (mode == 4u) execution.operation.deadline = {}; // A failed read, not coherent expiry.
        if (mode == 5u) execution.stage_deadline = {};
    }
    static void secondary_cleanup(CInputExecution& execution) noexcept
    {
        execution.input_route = { 17u };
        execution.control_route = { 19u };
        execution.installations[1] = { 71u };
        execution.begin_cleanup();
        execution.cleanup = CInputExecution::ECleanup::source;
    }
    static bool secondary_notify(const CInputExecution& execution) noexcept
    {
        return execution.stage == CInputExecution::EStage::secondary_notify;
    }
    static void prepare_installation(CInputExecution& execution) noexcept
    {
        execution.input_route = { 17u };
        execution.setup = CInputExecution::ESetup::controller_install;
    }
    static void cleanup(CInputExecution& execution) noexcept { execution.begin_cleanup(); }
    static std::uint64_t installation(const CInputExecution& execution) noexcept { return execution.installations[1].value; }
    static bool begin_pipe(CInputExecution& execution) noexcept
    {
        execution.setup = CInputExecution::ESetup::control_acquire;
        execution.stage_started = true; // Controlled setup boundary, no unrelated request.
        return execution.begin_acquire(4u, { 17u }, 33u, nullptr, 0u);
    }
    static void await_pipe_ready(CInputExecution& execution) noexcept
    {
        execution.index = 1u;
        execution.setup = CInputExecution::ESetup::pipe_ready;
        execution.stage_started = false;
    }
};
}
namespace
{
struct SInputSourceFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    threading::CErasedPodMsg last;
    unsigned owners{}, pods{}, attempts{};
    bool valid{ true };
    bool consumer{};
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SInputSourceFacade& self = *static_cast<SInputSourceFacade*>(cookie);
        ++self.attempts;
        const RawAssetTransfer* const transfer = message.owner().payload<RawAssetTransfer>();
        self.valid &= (destination.kind == modules::SDestination::EKind::host) && (message.query_async_slot() == 2) &&
            transfer && (self.consumer ? (transfer->storage.value.size() != 0u) : (transfer->storage.value.size() == 16u));
        if (modules::accepted(self.result))
        {
            ++self.owners;
            message.take_owner().destroy();
        }
        return self.result;
    }
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SInputSourceFacade& self = *static_cast<SInputSourceFacade*>(cookie);
        ++self.attempts;
        self.valid &= (destination.kind == modules::SDestination::EKind::host) &&
            ((message.query_async_slot() == 2) || (self.consumer && (message.query_async_slot() == 1)));
        if (modules::accepted(self.result))
        {
            ++self.pods;
            self.last = message;
        }
        return self.result;
    }
};

void input_source_custody(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SInputSourceFacade::pod, &SInputSourceFacade::owner };
    const std::uint8_t bytes[16]{};
    // Stop before publication and at each accepted-custody transition. Other
    // cases distinguish terminal refusal from contradictory/unsafe outcomes.
    for (unsigned scenario = 0u; scenario < 15u; ++scenario)
    {
        SInputSourceFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        input_host::CSourceExecution source{ communication };
        TEST_EXPECT(ctx, source.start(bytes, sizeof(bytes), { 17u }, { 9u }));
        const auto step = [&]
        {
            bool runnable = true;
            std::uint64_t next_wake = UINT64_MAX;
            source.advance(runnable, next_wake);
            TEST_EXPECT(ctx, (!runnable && facade.valid));
        };
        const auto accept = [&]
        {
            facade.result = modules::ESubmission::busy;
            const unsigned before = facade.owners + facade.pods;
            step();
            step();
            TEST_EXPECT(ctx, ((facade.owners + facade.pods) == before));
            facade.result = modules::ESubmission::published_wake_failed;
            step();
            TEST_EXPECT(ctx, ((facade.owners + facade.pods) == (before + 1u)));
            const unsigned attempts = facade.attempts;
            step();
            step();
            TEST_EXPECT(ctx, ((facade.attempts == attempts) && !source.complete()));
        };
        const auto reply = [&](const auto& result)
        {
            source.receive(threading::make_pod_message(2, result));
        };
        step();
        if (scenario == 1u)
        {
            source.stop();
            step();
            TEST_EXPECT(ctx, (source.complete() && !source.failed() && !facade.owners && !facade.pods));
            continue;
        }
        accept();
        if (scenario == 2u) source.stop();
        AssetResult transferred;
        transferred.status = ((scenario == 7u) || (scenario == 12u)) ? EAssetStatus::allocation_failed : EAssetStatus::success;
        if (scenario != 7u)
        {
            transferred.asset = CAssetId{ 31u };
            transferred.kind = EAssetKind::raw;
        }
        reply(transferred);
        if ((scenario == 7u) || (scenario == 12u))
        {
            const unsigned attempts = facade.attempts;
            step();
            TEST_EXPECT(ctx, (source.failed() && (source.complete() == (scenario == 7u)) && (facade.attempts == attempts)));
            continue;
        }
        if (scenario == 3u) source.stop();
        if ((scenario != 2u) && (scenario != 3u))
        {
            accept();
            AssetAcquireRequest acquire;
            TEST_EXPECT(ctx, (facade.last.copy_payload_to(acquire) && (acquire.source == CAssetId{ 31u })));
            if (scenario == 4u) source.stop();
            AssetAcquireResult acquired;
            acquired.status = ((scenario == 8u) || (scenario == 13u)) ? EAssetStatus::allocation_failed : EAssetStatus::success;
            if (scenario != 8u)
            {
                acquired.acquisition = CAssetAcquisitionId{ 41u };
                acquired.views.bytes = CByteConstView{ bytes, sizeof(bytes) };
            }
            reply(acquired);
            if (scenario == 13u)
            {
                const unsigned attempts = facade.attempts;
                source.stop();
                step();
                TEST_EXPECT(ctx, (source.failed() && !source.complete() && (facade.attempts == attempts)));
                continue;
            }
            if (scenario == 5u)
            {
                facade.result = modules::ESubmission::busy;
                step();
                source.stop();
            }
            if ((scenario != 4u) && (scenario != 5u) && (scenario != 8u))
            {
                accept();
                services::ServiceRequest install;
                TEST_EXPECT(ctx, (facade.last.copy_payload_to(install) && (install.route.value == 17u) &&
                    (install.request.value == 9u) && (install.opcode == static_cast<std::uint16_t>(platform::input::ECommand::install_configuration))));
                if (scenario == 6u) source.stop();
                services::ServiceReply result;
                result.route = install.route;
                result.request = install.request;
                result.opcode = install.opcode;
                result.notice = services::EServiceNotice::acknowledged;
                result.status = services::EServiceStatus::success;
                reply(result);
                const unsigned attempts = facade.attempts;
                step();
                TEST_EXPECT(ctx, ((facade.attempts == attempts) && !source.complete()));
                result.notice = services::EServiceNotice::completed;
                platform::input::SInstallationResult installed;
                if (scenario == 9u)
                {
                    result.status = services::EServiceStatus::invalid_request;
                    installed.error = platform::input::EConfigurationError::schema;
                }
                else installed.installation.value = 51u;
                result.payload_size = sizeof(installed);
                std::memcpy(result.payload, &installed, sizeof(installed));
                if (scenario == 14u) ++result.request.value;
                reply(result);
                if (scenario == 14u)
                {
                    step();
                    TEST_EXPECT(ctx, (source.failed() && !source.complete() && (facade.attempts == attempts)));
                    continue;
                }
                TEST_EXPECT(ctx, (source.installation().value == ((scenario == 9u) ? 0u : 51u)));
            }
            if (scenario != 8u)
            {
                accept();
                AssetReleaseRequest release;
                TEST_EXPECT(ctx, (facade.last.copy_payload_to(release) && (release.acquisition == CAssetAcquisitionId{ 41u })));
                reply(AssetReleaseResult{ release.acquisition, (scenario == 10u) ? EAssetStatus::invalid_request : EAssetStatus::success });
                if (scenario == 10u)
                {
                    const unsigned attempts = facade.attempts;
                    step();
                    TEST_EXPECT(ctx, (source.failed() && !source.complete() && (facade.attempts == attempts)));
                    continue;
                }
            }
        }
        accept();
        AssetDisposeRequest dispose;
        TEST_EXPECT(ctx, (facade.last.copy_payload_to(dispose) && (dispose.asset == CAssetId{ 31u })));
        reply(AssetDisposeResult{ dispose.asset, (scenario == 11u) ? EAssetStatus::invalid_request : EAssetStatus::success });
        const unsigned attempts = facade.attempts;
        step();
        TEST_EXPECT(ctx, ((facade.attempts == attempts) && (source.complete() == (scenario != 11u)) &&
            (source.failed() == ((scenario == 8u) || (scenario == 9u) || (scenario == 11u)))));
    }
}
}

namespace
{
struct SInputBorrowFacade : tests::TPodSubmissionCapture<8u>
{
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SInputBorrowFacade& self = *static_cast<SInputBorrowFacade*>(cookie);
        self.valid &= (message.query_async_slot() >= 16) && (message.query_async_slot() < 24);
        return self.capture(destination, message);
    }
};

void input_borrow_custody(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SInputBorrowFacade::pod };
    for (unsigned scenario = 0u; scenario < 9u; ++scenario)
    {
        SInputBorrowFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        input_host::CBorrowExecution borrows{ communication };
        services::ServiceRequest request;
        request.route = { 17u };
        request.request = { 19u };
        request.opcode = 32u;
        std::uint32_t descriptor = 23u;
        threading::CNotification notification;
        std::atomic<std::uint32_t> pending{};
        TEST_EXPECT(ctx, (notification.initialise(pending) && borrows.begin(0u, request)));
        const auto step = [&]
        {
            facade.count = 0u;
            bool runnable = false;
            std::uint64_t wake = UINT64_MAX;
            borrows.advance(runnable, wake);
            TEST_EXPECT(ctx, facade.valid);
            return runnable;
        };
        const auto reply = [&](const services::EServiceNotice notice, const services::EServiceStatus status = services::EServiceStatus::success)
        {
            services::ServiceReply result;
            result.route = request.route;
            result.request = request.request;
            result.opcode = request.opcode;
            result.notice = notice;
            result.status = status;
            if (notice == services::EServiceNotice::prepared)
            {
                const services::SPreparedServiceBorrow prepared{ { 29u }, { &descriptor, sizeof(descriptor), 1u } };
                result.payload_size = sizeof(prepared);
                std::memcpy(result.payload, &prepared, sizeof(prepared));
            }
            borrows.receive(threading::make_pod_message(16, result));
        };
        TEST_EXPECT(ctx, (!step() && !step() && !facade.count));
        if (scenario == 0u)
        {
            borrows.close_local(0u);
            TEST_EXPECT(ctx, (!step() && borrows.complete() && !borrows.failed() && !facade.count));
            continue;
        }
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!step() && (facade.count == 1u) && borrows.record(0u).submitted));
        const unsigned initial = facade.pod_attempts;
        TEST_EXPECT(ctx, (!step() && (facade.pod_attempts == initial)));
        if ((scenario == 1u) || (scenario == 2u))
        {
            borrows.close_local(0u);
            TEST_EXPECT(ctx, (!step() && (facade.pod_attempts == initial)));
        }
        reply(services::EServiceNotice::acknowledged);
        if (scenario == 1u)
        {
            facade.result = modules::ESubmission::busy;
            TEST_EXPECT(ctx, (!step() && !step() && !facade.count && !borrows.complete()));
            facade.result = modules::ESubmission::published_wake_failed;
            TEST_EXPECT(ctx, (step() && (facade.count == 1u) && facade.posts[0].is_payload_a<services::CancelServiceRequest>()));
            TEST_EXPECT(ctx, (!step() && !facade.count));
            reply(services::EServiceNotice::completed, services::EServiceStatus::cancelled);
            TEST_EXPECT(ctx, (!step() && borrows.complete() && !borrows.failed()));
            // Allowed late retirement of a never-exposed descriptor.
            borrows.receive(threading::make_pod_message(-1, services::ServiceBorrowControl{
                request.route, request.request, { 29u }, services::EServiceBorrowControl::retired, services::EServiceQuiescence::never_published }));
            TEST_EXPECT(ctx, (borrows.complete() && !borrows.failed()));
            continue;
        }
        if (scenario == 3u)
        {
            reply(services::EServiceNotice::completed, services::EServiceStatus::invalid_request);
            TEST_EXPECT(ctx, (borrows.failed() && borrows.record(0u).terminal && !borrows.record(0u).token.value));
            borrows.close_local(0u);
            TEST_EXPECT(ctx, (!step() && borrows.complete() && !facade.count));
            continue;
        }
        reply(services::EServiceNotice::prepared);
        if (scenario == 2u) TEST_EXPECT(ctx, !borrows.view(0u).descriptor);
        else TEST_EXPECT(ctx, (borrows.view(0u).descriptor == &descriptor));
        if ((scenario == 4u) || (scenario == 5u) || (scenario == 6u))
        {
            TEST_EXPECT(ctx, borrows.register_notification(0u));
            facade.result = modules::ESubmission::busy;
            TEST_EXPECT(ctx, (!step() && !step() && !facade.count));
            if (scenario != 4u)
            {
                facade.result = modules::ESubmission::published_wake_failed;
                TEST_EXPECT(ctx, (!step() && (facade.count == 1u) && facade.posts[0].is_payload_a<services::RegisterServiceNotification>()));
                TEST_EXPECT(ctx, (!step() && !facade.count));
            }
        }
        if (scenario == 7u)
        {
            borrows.receive(threading::make_pod_message(-1, services::ServiceBorrowControl{
                request.route, request.request, { 29u }, services::EServiceBorrowControl::closing, services::EServiceQuiescence::none }));
            TEST_EXPECT(ctx, (borrows.record(0u).closing && !borrows.view(0u).descriptor && !step() && !facade.count));
            // Host closure alone must not publish quiescence while a copied
            // descriptor/local input reader may still be in use.
            TEST_EXPECT(ctx, (!step() && !facade.count && !borrows.record(0u).quiescence_prepared));
        }
        borrows.close_local(0u);
        descriptor = 0u; // All local descriptor access has ceased.
        facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!step() && !step() && !facade.count && !borrows.complete()));
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (step() && (facade.count == 1u)));
        if (scenario != 7u)
        {
            TEST_EXPECT(ctx, facade.posts[0].is_payload_a<services::ReleaseServiceBorrow>());
            facade.result = modules::ESubmission::busy;
            TEST_EXPECT(ctx, (!step() && !step() && !facade.count));
            facade.result = modules::ESubmission::published_wake_failed;
            TEST_EXPECT(ctx, (step() && (facade.count == 1u)));
        }
        TEST_EXPECT(ctx, (facade.posts[0].is_payload_a<services::ServiceBorrowQuiescent>() && borrows.record(0u).quiescence_sent));
        TEST_EXPECT(ctx, (!step() && !facade.count && !borrows.complete() && !borrows.view(0u).descriptor));
        borrows.receive(threading::make_pod_message(-1, services::ServiceBorrowControl{
            request.route, request.request, { 29u }, services::EServiceBorrowControl::retired, services::EServiceQuiescence::relinquished }));
        TEST_EXPECT(ctx, (!step() && !borrows.complete())); // The terminal acquisition result is still owed.
        reply(services::EServiceNotice::completed, services::EServiceStatus::cancelled);
        if ((scenario == 5u) || (scenario == 6u))
        {
            TEST_EXPECT(ctx, (!step() && !borrows.complete()));
            services::ServiceNotificationRegistered registered;
            registered.route = request.route;
            registered.borrow = { 29u };
            registered.status = (scenario == 6u) ? services::EServiceStatus::unavailable : services::EServiceStatus::success;
            if (scenario == 5u)
            {
                registered.registration = { 31u };
                registered.reader = notification.reader();
            }
            borrows.receive(threading::make_pod_message(16, registered));
            TEST_EXPECT(ctx, !borrows.notification(0u).is_valid());
        }
        TEST_EXPECT(ctx, (!step() && borrows.complete() && !borrows.failed() && !facade.count));
        // A fully settled slot can be reused; the old exact request cannot
        // accidentally complete the successor.
        ++request.request.value;
        TEST_EXPECT(ctx, borrows.begin(0u, request));
        facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, !step());
        borrows.close_local(0u);
        TEST_EXPECT(ctx, (borrows.complete() && !borrows.failed()));
    }
    // The eight-slot limit remains explicit. A refused ordinary acquisition
    // blocks younger request IDs; accepted requests still get a bounded pass.
    SInputBorrowFacade facade;
    const modules::CCommunication communication{ functions, &facade };
    input_host::CBorrowExecution borrows{ communication };
    for (unsigned index = 0u; index < 8u; ++index)
    {
        services::ServiceRequest request;
        request.route = { 17u };
        request.request = { index + 1u };
        request.opcode = 32u;
        TEST_EXPECT(ctx, borrows.begin(index, request));
    }
    bool runnable = true;
    std::uint64_t wake = UINT64_MAX;
    borrows.advance(runnable, wake);
    TEST_EXPECT(ctx, (!runnable && !facade.count && (facade.pod_attempts == 1u) && borrows.publication_pending()));
    TEST_EXPECT(ctx, !borrows.begin(8u, services::ServiceRequest{}));
    for (unsigned index = 0u; index < 8u; ++index) borrows.close_local(index);
    TEST_EXPECT(ctx, (borrows.complete() && !borrows.failed()));
}
}

namespace
{
void input_secondary_cleanup(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SInputSourceFacade::pod, &SInputSourceFacade::owner };
    for (unsigned scenario = 0u; scenario < 3u; ++scenario)
    {
        SInputSourceFacade facade;
        facade.consumer = true;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        input_host::CInputExecution execution{ context, true };
        input_host::SInputExecutionTestAccess::secondary_cleanup(execution);
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event = modules::EExecutionEvent::service_async,
            const threading::CErasedPodMsg* const message = nullptr)
        {
            modules::SInvocation invocation;
            invocation.event = event;
            invocation.message = message;
            invocation.notification.stop_reason = modules::EExecutionStop::shutdown;
            const modules::EExecutionStatus result = execution.handle(invocation, progress);
            TEST_EXPECT(ctx, facade.valid);
            return result;
        };
        const auto deliver = [&](const auto& payload)
        {
            const threading::CErasedPodMsg message = threading::make_pod_message(1, payload);
            TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::message, &message) == modules::EExecutionStatus::success));
        };
        const auto command_reply = [&](const services::ServiceRequest& request, const services::EServiceNotice notice)
        {
            services::ServiceReply reply;
            reply.route = request.route;
            reply.request = request.request;
            reply.opcode = request.opcode;
            reply.notice = notice;
            reply.status = services::EServiceStatus::success;
            deliver(reply);
        };
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        }
        TEST_EXPECT(ctx, (!facade.pods && !progress.work_pending && !progress.work_complete));
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        services::ServiceRequest barrier;
        TEST_EXPECT(ctx, ((facade.pods == 1u) && facade.last.copy_payload_to(barrier) &&
            (barrier.opcode == input_host::k_secondary_cleanup_barrier)));
        const unsigned attempts = facade.attempts;
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, ((facade.attempts == attempts) && !progress.work_complete &&
                (input_host::SInputExecutionTestAccess::installation(execution) == 71u)));
        }
        if (scenario == 1u)
        {
            TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::stop) == modules::EExecutionStatus::failed));
            for (unsigned turn = 0u; (turn < 12u) && !progress.work_complete; ++turn)
            {
                TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
            }
            TEST_EXPECT(ctx, (execution.complete() && (facade.pods == 1u) &&
                (input_host::SInputExecutionTestAccess::installation(execution) == 71u)));
            continue;
        }
        facade.result = modules::ESubmission::busy;
        command_reply(barrier, services::EServiceNotice::acknowledged);
        command_reply(barrier, services::EServiceNotice::completed);
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        }
        TEST_EXPECT(ctx, ((facade.pods == 1u) && !progress.work_complete));
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        services::ServiceRequest retire;
        TEST_EXPECT(ctx, ((facade.pods == 2u) && facade.last.copy_payload_to(retire) &&
            (retire.opcode == static_cast<std::uint16_t>(platform::input::ECommand::retire_configuration))));
        TEST_EXPECT(ctx, (input_host::SInputExecutionTestAccess::installation(execution) == 71u));
        command_reply(retire, services::EServiceNotice::acknowledged);
        TEST_EXPECT(ctx, (input_host::SInputExecutionTestAccess::installation(execution) == 71u));
        command_reply(retire, services::EServiceNotice::completed);
        TEST_EXPECT(ctx, !input_host::SInputExecutionTestAccess::installation(execution));
        unsigned observed = facade.pods;
        for (unsigned turn = 0u; (turn < 32u) && !input_host::SInputExecutionTestAccess::secondary_notify(execution); ++turn)
        {
            TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
            if (facade.pods == observed) continue;
            TEST_EXPECT(ctx, (facade.pods == (observed + 1u)));
            observed = facade.pods;
            services::CloseServiceRequest close;
            TEST_EXPECT(ctx, facade.last.copy_payload_to(close));
            services::ServiceReply reply;
            reply.route = close.route;
            reply.request = close.request;
            reply.notice = services::EServiceNotice::completed;
            reply.status = services::EServiceStatus::success;
            deliver(reply);
        }
        TEST_EXPECT(ctx, (input_host::SInputExecutionTestAccess::secondary_notify(execution) && (facade.pods == 4u)));
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        services::OpenServiceRequest open;
        TEST_EXPECT(ctx, ((facade.pods == 5u) && facade.last.copy_payload_to(open)));
        services::OpenServiceResult opened;
        opened.request = open.request;
        opened.status = services::EServiceStatus::success;
        opened.route = { 27u };
        opened.provider = { 81u };
        facade.result = modules::ESubmission::busy;
        deliver(opened);
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, ((facade.pods == 5u) && !progress.work_complete && !progress.work_pending));
        facade.result = modules::ESubmission::published_wake_failed;
        if (scenario == 2u)
        {
            TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::stop) == modules::EExecutionStatus::failed));
            TEST_EXPECT(ctx, (execution.complete() && (facade.pods == 5u)));
        }
        else
        {
            TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
            services::ServiceRequest complete;
            TEST_EXPECT(ctx, ((facade.pods == 6u) && facade.last.copy_payload_to(complete) &&
                (complete.opcode == input_host::k_secondary_cleanup_complete)));
            // Stop may overtake the accepted value-only notification's ACK.
            TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::stop) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && (facade.pods == 6u)));
        }
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
    }
}

void input_installation_cleanup(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SInputSourceFacade::pod, &SInputSourceFacade::owner };
    for (const bool shutdown : { false, true })
    {
        SInputSourceFacade facade;
        facade.consumer = true;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        context.instance = { 101u };
        context.endpoint = { 102u };
        input_host::CInputExecution execution{ context };
        input_host::SInputExecutionTestAccess::prepare_installation(execution);
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event = modules::EExecutionEvent::service_async,
            const threading::CErasedPodMsg* const message = nullptr)
        {
            modules::SInvocation invocation;
            invocation.event = event;
            invocation.message = message;
            invocation.notification.stop_reason = shutdown ? modules::EExecutionStop::shutdown : modules::EExecutionStop::requested;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, facade.valid);
        };
        const auto deliver = [&](const auto& result, const std::int32_t slot = 2)
        {
            const threading::CErasedPodMsg message = threading::make_pod_message(slot, result);
            invoke(modules::EExecutionEvent::message, &message);
        };
        invoke(modules::EExecutionEvent::start);
        TEST_EXPECT(ctx, (!facade.owners && !progress.work_complete));
        facade.result = modules::ESubmission::published_wake_failed;
        invoke();
        TEST_EXPECT(ctx, (facade.owners == 1u));
        facade.result = modules::ESubmission::busy;
        AssetResult transfer;
        transfer.asset = CAssetId{ 31u };
        transfer.kind = EAssetKind::raw;
        transfer.status = EAssetStatus::success;
        deliver(transfer);
        facade.result = modules::ESubmission::published_wake_failed;
        invoke();
        TEST_EXPECT(ctx, ((facade.pods == 1u) && facade.last.is_payload_a<AssetAcquireRequest>()));
        facade.result = modules::ESubmission::busy;
        const std::uint8_t bytes[16]{};
        AssetAcquireResult acquired;
        acquired.acquisition = CAssetAcquisitionId{ 41u };
        acquired.views.bytes = CByteConstView{ bytes, sizeof(bytes) };
        acquired.status = EAssetStatus::success;
        deliver(acquired);
        facade.result = modules::ESubmission::published_wake_failed;
        invoke();
        services::ServiceRequest install;
        TEST_EXPECT(ctx, ((facade.pods == 2u) && facade.last.copy_payload_to(install) &&
            (install.opcode == static_cast<std::uint16_t>(platform::input::ECommand::install_configuration))));
        facade.result = modules::ESubmission::busy;
        if (shutdown) invoke(modules::EExecutionEvent::stop);
        services::ServiceReply result;
        result.route = install.route;
        result.request = install.request;
        result.opcode = install.opcode;
        result.notice = services::EServiceNotice::acknowledged;
        result.status = services::EServiceStatus::success;
        deliver(result);
        TEST_EXPECT(ctx, (!progress.work_complete && (facade.pods == 2u)));
        result.notice = services::EServiceNotice::completed;
        platform::input::SInstallationResult installed;
        installed.installation.value = 71u;
        result.payload_size = sizeof(installed);
        std::memcpy(result.payload, &installed, sizeof(installed));
        deliver(result);
        TEST_EXPECT(ctx, (!progress.work_complete && (input_host::SInputExecutionTestAccess::installation(execution) == 71u)));
        if (!shutdown) input_host::SInputExecutionTestAccess::cleanup(execution);
        invoke();
        TEST_EXPECT(ctx, (!progress.work_complete && (facade.pods == 2u)));
        facade.result = modules::ESubmission::published_wake_failed;
        invoke();
        TEST_EXPECT(ctx, ((facade.pods == 3u) && facade.last.is_payload_a<AssetReleaseRequest>()));
        invoke();
        TEST_EXPECT(ctx, (!progress.work_complete && (facade.pods == 3u)));
        facade.result = modules::ESubmission::busy;
        deliver(AssetReleaseResult{ CAssetAcquisitionId{ 41u }, EAssetStatus::success });
        invoke();
        TEST_EXPECT(ctx, (!progress.work_complete && (facade.pods == 3u)));
        facade.result = modules::ESubmission::published_wake_failed;
        invoke();
        TEST_EXPECT(ctx, ((facade.pods == 4u) && facade.last.is_payload_a<AssetDisposeRequest>()));
        facade.result = modules::ESubmission::busy;
        deliver(AssetDisposeResult{ CAssetId{ 31u }, EAssetStatus::success });
        for (unsigned turn = 0u; turn < 8u; ++turn) invoke();
        if (shutdown)
        {
            // The Host shutdown event supplies the global lifecycle context.
            // Consumer completion retains the token; it does not pretend an
            // explicit configuration retirement succeeded on closed admission.
            TEST_EXPECT(ctx, (progress.work_complete && execution.complete() && (facade.pods == 4u) &&
                (input_host::SInputExecutionTestAccess::installation(execution) == 71u)));
        }
        else
        {
            TEST_EXPECT(ctx, (!progress.work_complete && (facade.pods == 4u) &&
                (input_host::SInputExecutionTestAccess::installation(execution) == 71u)));
            facade.result = modules::ESubmission::published_wake_failed;
            invoke();
            services::ServiceRequest retire;
            TEST_EXPECT(ctx, ((facade.pods == 5u) && facade.last.copy_payload_to(retire) &&
                (retire.opcode == static_cast<std::uint16_t>(platform::input::ECommand::retire_configuration))));
            result = {};
            result.route = retire.route;
            result.request = retire.request;
            result.opcode = retire.opcode;
            result.notice = services::EServiceNotice::acknowledged;
            result.status = services::EServiceStatus::success;
            facade.result = modules::ESubmission::busy;
            deliver(result, 1);
            TEST_EXPECT(ctx, (!progress.work_complete && (input_host::SInputExecutionTestAccess::installation(execution) == 71u)));
            result.notice = services::EServiceNotice::completed;
            deliver(result, 1);
            TEST_EXPECT(ctx, (input_host::SInputExecutionTestAccess::installation(execution) == 0u));
            invoke(modules::EExecutionEvent::stop);
            facade.result = modules::ESubmission::published_wake_failed;
            for (unsigned turn = 0u; (turn < 8u) && (facade.pods == 5u); ++turn) invoke();
            services::CloseServiceRequest close;
            TEST_EXPECT(ctx, ((facade.pods == 6u) && facade.last.copy_payload_to(close)));
            result = {};
            result.route = close.route;
            result.request = close.request;
            result.status = services::EServiceStatus::success;
            deliver(result, 1);
            for (unsigned turn = 0u; (turn < 8u) && !progress.work_complete; ++turn) invoke();
            TEST_EXPECT(ctx, (progress.work_complete && execution.complete() && (facade.pods == 6u)));
        }
    }
}
}

namespace
{
struct SInputFreezeFacade
{
    input_host::CInputExecution* execution{};
    modules::ESubmission result{ modules::ESubmission::accepted };
    unsigned freezes{}, releases{}, quiescent{}, freeze_attempts{};
    bool valid{ true };
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SInputFreezeFacade& self = *static_cast<SInputFreezeFacade*>(cookie);
        self.valid &= destination.kind == modules::SDestination::EKind::host;
        services::ServiceRequest request;
        const bool freeze = message.copy_payload_to(request) && (message.query_async_slot() == 1);
        const bool release = message.is_payload_a<services::ReleaseServiceBorrow>();
        const bool quiescent = message.is_payload_a<services::ServiceBorrowQuiescent>();
        if (freeze) ++self.freeze_attempts;
        if (release || quiescent)
        {
            self.valid &= self.execution && input_host::SInputExecutionTestAccess::reader_closed(*self.execution);
        }
        if (modules::accepted(self.result))
        {
            if (freeze) ++self.freezes;
            if (release) ++self.releases;
            if (quiescent) ++self.quiescent;
        }
        return self.result;
    }
};

void input_freeze_cleanup(tests::TTestContext& ctx)
{
    using Access = input_host::SInputExecutionTestAccess;
    static const modules::SCommunicationV1 functions{ &SInputFreezeFacade::pod };
    // 0: never accepted, 1: no ACK, 2: no completion, 3: completion without
    // frozen handshake. The latter is stage-only since its command has settled.
    for (unsigned scenario = 0u; scenario < 16u; ++scenario)
    {
        const unsigned phase = scenario < 7u ? (scenario >> 1u) : 1u;
        const bool stage_timeout = (scenario == 6u) || (scenario & 1u);
        const unsigned refusal = ((scenario >= 7u) && (scenario < 12u)) ? (scenario - 6u) : 0u;
        SInputFreezeFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        context.instance = { 101u };
        input_host::CInputExecution execution{ context };
        facade.execution = &execution;
        input_host::SShared shared;
        threading::transports::TRingBundle<platform::input::SActionInputV1> actions;
        platform::input::CInputPublication publication;
        platform::input::SInputSnapshot snapshot;
        snapshot.phase = platform::input::EPhase::active;
        platform::threading::COwnerWait wake;
        TEST_EXPECT(ctx, (actions.transport.initialise(platform::input::k_action_capacity) &&
            publication.publish(snapshot) && (wake.initialise().code == platform::threading::EWaitError::none)));
        platform::input::SInputEndpointV4 view;
        view.kind = platform::input::EConnection::controller;
        view.clock = platform::input::EClock::source_nanoseconds;
        view.connection = { 41u };
        view.provider = { 102u };
        view.consumer = context.instance;
        view.action_capacity = platform::input::k_action_capacity;
        view.actions = &actions.consumer;
        view.state = &publication;
        view.capacity_wake = wake.endpoint();
        TEST_EXPECT(ctx, Access::prepare_freeze(execution, shared, view));
        facade.result = phase ? modules::ESubmission::published_wake_failed : modules::ESubmission::busy;
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event = modules::EExecutionEvent::service_async,
            const threading::CErasedPodMsg* const message = nullptr)
        {
            modules::SInvocation invocation;
            invocation.event = event;
            invocation.message = message;
            invocation.notification.stop_reason = modules::EExecutionStop::shutdown;
            const modules::EExecutionStatus status = execution.handle(invocation, progress);
            TEST_EXPECT(ctx, facade.valid);
            return status;
        };
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        const services::ServiceRequest request = Access::freeze_request(execution);
        const auto reply = [&](const services::EServiceNotice notice, const services::EServiceStatus status = services::EServiceStatus::success)
        {
            services::ServiceReply result;
            result.route = request.route;
            result.request = request.request;
            result.opcode = request.opcode;
            result.notice = notice;
            result.status = status;
            const threading::CErasedPodMsg message = threading::make_pod_message(1, result);
            return invoke(modules::EExecutionEvent::message, &message);
        };
        if (phase >= 2u) TEST_EXPECT(ctx, (reply(services::EServiceNotice::acknowledged) == modules::EExecutionStatus::success));
        if (phase == 3u) TEST_EXPECT(ctx, (reply(services::EServiceNotice::completed) == modules::EExecutionStatus::success));
        facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, Access::expire_freeze(execution, stage_timeout));
        Access::refuse_freeze_exception(execution, refusal);
        if (refusal == 3u)
        {
            // Existing unsafe custody stays ineligible and cannot resume work.
            TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        }
        else TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::failed));
        const unsigned attempts = facade.freeze_attempts; // After the timeout is actually observed.
        TEST_EXPECT(ctx, !progress.work_complete);
        if (refusal)
        {
            TEST_EXPECT(ctx, (Access::unsafe(execution) && !Access::retained_freeze(execution)));
            TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::stop) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (!progress.work_complete && !facade.releases && !facade.quiescent));
            continue;
        }
        TEST_EXPECT(ctx, (Access::reader_closed(execution) && !Access::unsafe(execution) && !shared.frozen.load()));
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (!progress.work_pending && !facade.releases && !facade.quiescent &&
            (facade.freeze_attempts == attempts)));
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (facade.releases == 1u));
        facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (!facade.quiescent && !progress.work_complete));
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, ((facade.quiescent == 1u) && !progress.work_complete));
        TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, !progress.work_complete);
        const threading::CErasedPodMsg retired = threading::make_pod_message(-1, services::ServiceBorrowControl{
            { 17u }, { 29u }, { 31u }, services::EServiceBorrowControl::retired, services::EServiceQuiescence::relinquished });
        TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::message, &retired) == modules::EExecutionStatus::success));
        // Cross configurations/global-shutdown clear_operation BEFORE delivering
        // the late outcome. Still no domain completion without actual stop.
        for (unsigned turn = 0u; turn < 12u; ++turn) TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (!progress.work_complete && (Access::retained_freeze(execution) == (phase != 0u))));
        if (scenario >= 12u)
        {
            services::ServiceReply malformed;
            malformed.route = request.route;
            malformed.request = request.request;
            malformed.opcode = request.opcode;
            malformed.notice = services::EServiceNotice::acknowledged;
            malformed.status = services::EServiceStatus::success;
            if (scenario == 12u) ++malformed.route.value;
            if (scenario == 13u) ++malformed.request.value;
            if (scenario == 14u) ++malformed.opcode;
            if (scenario == 15u) malformed.notice = services::EServiceNotice::completed; // No ACK yet.
            const threading::CErasedPodMsg message = threading::make_pod_message(1, malformed);
            TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::message, &message) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (Access::unsafe(execution) && !execution.complete()));
            continue; // Earlier fatal callback stays latched; malformed custody is retained.
        }
        if (phase == 1u) TEST_EXPECT(ctx, (reply(services::EServiceNotice::acknowledged) == modules::EExecutionStatus::success));
        if ((phase == 1u) || ((phase == 2u) && stage_timeout))
        {
            TEST_EXPECT(ctx, (reply(services::EServiceNotice::completed, services::EServiceStatus::closing) == modules::EExecutionStatus::success));
        }
        // One ACK/no-completion case intentionally leaves the value-only result
        // unread: real stop/quiescence, not invented completion, cancels it.
        TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::stop) == modules::EExecutionStatus::success));
        for (unsigned turn = 0u; (turn < 12u) && !progress.work_complete; ++turn)
        {
            TEST_EXPECT(ctx, (invoke() == modules::EExecutionStatus::success));
        }
        TEST_EXPECT(ctx, (progress.work_complete && execution.complete() && (facade.releases == 1u) &&
            (facade.quiescent == 1u) && (facade.freezes == (phase ? 1u : 0u)) && (facade.freeze_attempts == attempts)));
    }
}

void input_unexpected_close(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SInputBorrowFacade::pod };
    SInputBorrowFacade facade;
    facade.result = modules::ESubmission::accepted;
    const modules::CCommunication communication{ functions, &facade };
    modules::SExecutionContext context;
    context.communication = &communication;
    input_host::CInputExecution execution{ context };
    TEST_EXPECT(ctx, input_host::SInputExecutionTestAccess::begin_pipe(execution));
    modules::SExecutionProgress progress;
    const auto invoke = [&](const modules::EExecutionEvent event, const threading::CErasedPodMsg* const message = nullptr)
    {
        facade.count = 0u;
        modules::SInvocation invocation;
        invocation.event = event;
        invocation.message = message;
        invocation.notification.stop_reason = modules::EExecutionStop::shutdown;
        return execution.handle(invocation, progress);
    };
    TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::start) == modules::EExecutionStatus::success));
    services::ServiceRequest request;
    TEST_EXPECT(ctx, ((facade.count == 1u) && facade.posts[0].copy_payload_to(request)));
    const auto reply = [&](const services::EServiceNotice notice)
    {
        services::ServiceReply result;
        result.route = request.route;
        result.request = request.request;
        result.opcode = request.opcode;
        result.notice = notice;
        result.status = (notice == services::EServiceNotice::completed) ?
            services::EServiceStatus::cancelled : services::EServiceStatus::success;
        const threading::CErasedPodMsg message = threading::make_pod_message(20, result);
        return invoke(modules::EExecutionEvent::message, &message);
    };
    TEST_EXPECT(ctx, (reply(services::EServiceNotice::acknowledged) == modules::EExecutionStatus::success));
    input_host::SInputExecutionTestAccess::await_pipe_ready(execution);
    // A real Host closing control may precede preparation and the stop callback.
    // The scenario boundary would dereference its missing pipe without the guard.
    threading::CErasedPodMsg message = threading::make_pod_message(-1, services::ServiceBorrowControl{
        request.route, request.request, { 29u }, services::EServiceBorrowControl::closing, services::EServiceQuiescence::never_published });
    TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::message, &message) == modules::EExecutionStatus::failed));
    TEST_EXPECT(ctx, (facade.valid && (facade.count == 1u) && facade.posts[0].is_payload_a<services::ServiceBorrowQuiescent>()));
    TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::stop) == modules::EExecutionStatus::success));
    TEST_EXPECT(ctx, (!progress.work_complete && !facade.count));
    message = threading::make_pod_message(-1, services::ServiceBorrowControl{
        request.route, request.request, { 29u }, services::EServiceBorrowControl::retired, services::EServiceQuiescence::never_published });
    TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::message, &message) == modules::EExecutionStatus::success));
    TEST_EXPECT(ctx, !progress.work_complete);
    TEST_EXPECT(ctx, (reply(services::EServiceNotice::completed) == modules::EExecutionStatus::success));
    for (unsigned turn = 0u; (turn < 12u) && !progress.work_complete; ++turn)
    {
        TEST_EXPECT(ctx, (invoke(modules::EExecutionEvent::service_async) == modules::EExecutionStatus::success));
    }
    TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && facade.valid && !facade.count));
}
}

int run_input_freeze_focused()
{
    tests::TTestContext ctx;
    input_freeze_cleanup(ctx);
    return ctx.report("InputFreezeCleanup");
}

int run_input_unexpected_close_focused()
{
    tests::TTestContext ctx;
    input_unexpected_close(ctx);
    return ctx.report("InputUnexpectedClose");
}

int run_input_installation_focused()
{
    tests::TTestContext ctx;
    input_installation_cleanup(ctx);
    return ctx.report("InputInstallationCleanup");
}

int run_input_secondary_focused()
{
    tests::TTestContext ctx;
    input_secondary_cleanup(ctx);
    return ctx.report("InputSecondaryCleanup");
}

int run_input_borrow_focused()
{
    tests::TTestContext ctx;
    input_borrow_custody(ctx);
    return ctx.report("InputBorrowCustody");
}

int run_input_source_focused()
{
    tests::TTestContext ctx;
    input_source_custody(ctx);
    return ctx.report("InputSourceCustody");
}


void run_input_fixture_tests(tests::TTestContext& ctx)
{
    input_source_custody(ctx);
    input_borrow_custody(ctx);
    input_freeze_cleanup(ctx);
    input_installation_cleanup(ctx);
    input_secondary_cleanup(ctx);
    input_unexpected_close(ctx);
}
