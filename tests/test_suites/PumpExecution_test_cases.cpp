
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    PumpExecution_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    24 Nov 26
//
//  Pump acceptance-fixture protocols and controlled Provider DLL retirement.

#include "tests/test_suites/ExecutionFixture_test_cases.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/execution_facade_fixture.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include <cstring>

#include "tests/support/module_preamble_fixture.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/host_pump/executive_execution.hpp"
#include "foundation/debug/service.hpp"
#include <iostream>
#include <cstdlib>

using tests::SDllAllocation;
using tests::SLifecycleExecutiveFacade;
using SPodCapture = tests::TPodSubmissionCapture<4u>;

namespace pump_tests
{
struct SReportExecutionTestAccess
{
    static bool expire_poll(CReportExecution& execution) noexcept
    {
        return execution.m_poll_pending &&
            (execution.m_poll_due.set_after_milliseconds(0u).code == platform::threading::EWaitError::none);
    }
};
struct SModalExecutionTestAccess
{
    static bool expire_hold(CModalExecution& execution) noexcept
    {
        return execution.m_budget_started &&
            (execution.m_hold.set_after_milliseconds(0u).code == platform::threading::EWaitError::none);
    }
};
struct SExecutiveExecutionTestAccess
{
    static bool pending_shutdown(CExecutiveExecution& execution) noexcept
    {
        // Accepted delayed service and self-stop intents, before either stop or
        // the delayed terminal result has been delivered to this consumer.
        execution.m_stage = CExecutiveExecution::EStage::self_stop;
        execution.m_operation = CExecutiveExecution::EOperation::self;
        execution.m_primary.route = { 17u };
        execution.m_sent = execution.m_acknowledged = execution.m_delayed_pending = true;
        return execution.m_delayed_deadline.set_after_milliseconds(5000u).code == platform::threading::EWaitError::none;
    }
    static bool unproven_shutdown_close(CExecutiveExecution& execution, const unsigned mode) noexcept
    {
        execution.m_stage = CExecutiveExecution::EStage::closing_after_stop;
        execution.m_stopping = mode != 6u;
        execution.m_delayed_done = mode != 7u;
        return execution.begin_close({ 17u }, 7u, 50);
    }
    static bool expire_report_poll(CExecutiveExecution& execution) noexcept
    {
        return SReportExecutionTestAccess::expire_poll(execution.m_report);
    }
    static bool close(CExecutiveExecution& execution) noexcept
    {
        execution.m_stage = CExecutiveExecution::EStage::close;
        return execution.begin_close({ 17u }, 2u, 71) &&
            (execution.m_deadline.set_after_milliseconds(5000u).code == platform::threading::EWaitError::none);
    }
    static bool hold_retry(CExecutiveExecution& execution) noexcept
    {
        return execution.m_close_retry_pending &&
            (execution.m_close_retry.set_after_milliseconds(60000u).code == platform::threading::EWaitError::none);
    }
    static bool terminal_busy(CExecutiveExecution& execution, const threading::CErasedPodMsg& message) noexcept
    {
        std::uint64_t remaining = 0u;
        return execution.receive(message) && execution.m_close_retry_pending &&
            (execution.m_close_retry.remaining_nanoseconds(remaining).code == platform::threading::EWaitError::none) &&
            (remaining <= 1000000u);
    }
    static bool original_deadlines(const CExecutiveExecution& execution) noexcept
    {
        std::uint64_t retry = 0u, operation = 0u;
        return (execution.m_close_retry.remaining_nanoseconds(retry).code == platform::threading::EWaitError::none) &&
            (execution.m_deadline.remaining_nanoseconds(operation).code == platform::threading::EWaitError::none) &&
            (retry > 50000000000u) && (operation <= 5000000000u);
    }
    static bool retry_due(CExecutiveExecution& execution) noexcept
    {
        return execution.m_close_retry.set_after_milliseconds(0u).code == platform::threading::EWaitError::none;
    }
};
}

int run_provider_execution_focused(const bool refusals_only, const bool retirement_only, const bool notification_only)
{
    tests::TTestContext ctx;
    using Access = host::SModulePreambleTestAccess;
    TInstance<debug_system::CDebugServiceState> debug_service = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, debug_service.is_ready());
    if (!debug_service) return 1;
    const std::string log = test_environment::test_log_path("ProviderExecution");
    const std::string direct = test_environment::test_log_path("ProviderExecution_direct");
    TEST_EXPECT(ctx, (debug_service->configure_log_paths(log.c_str(), direct.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "provider_test" })));
#if MV_PLATFORM_WINDOWS
    char prior[64]{};
    std::size_t required = 0u;
    TEST_EXPECT(ctx, (getenv_s(&required, prior, sizeof(prior), "MORPHIC_PUMP_CASE") == 0));
    const std::string previous = prior;
#else
    const char* const prior = std::getenv("MORPHIC_PUMP_CASE");
    const std::string previous = prior ? prior : "";
#endif
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    SDllAllocation attribution;
    const system_ids::id_type identity = system_ids::ops::make_system_id(module_ids::rendering, thread_ids::host);
    memory::CMemoryAllocator allocator{ &attribution, &SDllAllocation::allocate, &SDllAllocation::free, identity };
    memory::CMemoryContext module_memory{ allocator, identity };
    // Owning transport requires a shared allocator, as in production. Keep
    // Host attribution separate from the module's transferred allocations.
    memory::CMemoryContext shared_host_memory{ allocator, system_ids::host };
    memory::CMemoryAllocator host_allocator{ nullptr, &tests::allocate_scalar_test_memory,
        &tests::deallocate_scalar_test_memory, system_ids::host };
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string file = test_environment::binary_path(test_environment::module_filename("MorphicPumpProvider").c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(file.c_str());
    const bool installed = path.is_ready() && binding.bind(path, module_ids::rendering, host_identity) &&
        binding.install(test_environment::system_registry_view(), module_ids::rendering, &module_memory,
            system_context::sequence_counter, debug_service.operator->());
    TEST_EXPECT(ctx, installed);
    if (!installed)
    {
        TEST_EXPECT(ctx, binding.unbind());
        TEST_EXPECT(ctx, debug_service->stop());
        return 1;
    }
    // Bootstrap once per image: ELF may retain the DLL after dlclose. Each
    // scenario still creates and retires a fresh Host graph and execution.
    for (const char* const scenario : { "normal", "mixed", "borrow-cancel", "notify-cancel", "cleanup-credits",
        "credit-stop-transfer", "credit-stop-acquire", "credit-refuse-transfer", "credit-refuse-acquire",
        "test-refuse-transfer", "test-refuse-acquire", "mixed-refused", "borrow-stop-finishing", "borrow-stop-completing",
        "notify-bind-refused", "notify-stop-binding" })
    {
        const bool bind_refused = std::strcmp(scenario, "notify-bind-refused") == 0;
        const bool stop_binding = std::strcmp(scenario, "notify-stop-binding") == 0;
        if ((bind_refused || stop_binding) != notification_only) continue;
        const bool stop_finishing = std::strcmp(scenario, "borrow-stop-finishing") == 0;
        const bool stop_completing = std::strcmp(scenario, "borrow-stop-completing") == 0;
        const bool late_stop = stop_finishing || stop_completing;
        if (late_stop != retirement_only) continue;
        const bool child_refusal = std::strcmp(scenario, "mixed-refused") == 0;
        const bool test_refusal = std::strncmp(scenario, "test-refuse-", 12u) == 0;
        const bool asset_refusal = test_refusal || (std::strncmp(scenario, "credit-refuse-", 14u) == 0);
        const bool expected_failure = child_refusal || asset_refusal || bind_refused;
        if (!notification_only && (expected_failure != refusals_only)) continue;
        const char* const configured = child_refusal ? "mixed" : (late_stop ? "borrow-cancel" :
            (notification_only ? "notify-cancel" : scenario));
#if MV_PLATFORM_WINDOWS
        TEST_EXPECT(ctx, (_putenv_s("MORPHIC_PUMP_CASE", configured) == 0));
#else
        TEST_EXPECT(ctx, (setenv("MORPHIC_PUMP_CASE", configured, 1) == 0));
#endif
        const auto require = [&](const bool value, const unsigned line)
        {
            TEST_EXPECT(ctx, value);
            if (!value)
            {
                std::cerr << "Provider DLL fixture " << scenario << " failed at " << line << '\n';
                std::_Exit(1); // Never release live DLL/borrow/native state without proof.
            }
        };
        const auto caller = platform::threading::query_current_thread_id();
        const bool full_credits = std::strcmp(scenario, "cleanup-credits") == 0;
        const bool partial_credits = std::strncmp(scenario, "credit-stop-", 12u) == 0;
        const bool credits = full_credits || partial_credits || asset_refusal;
        require(module_memory.is_attribution_empty() && shared_host_memory.is_attribution_empty() &&
            (attribution.allocations == attribution.frees), __LINE__);
        attribution.allocations.store(0u);
        attribution.frees.store(0u);
        attribution.primary = nullptr;
        attribution.allocated_on = platform::threading::CPlatformThreadId{};
        attribution.freed_on = platform::threading::CPlatformThreadId{};
        attribution.track_primary = !credits;
        tests::TMemoryContextScope host_scope{ credits ? &shared_host_memory : memory::get_ambient_memory_context() };
        modules::SExecutionV1 execution;
        require((binding.query_execution(execution) == modules::EBindingResult::success) && execution.create && !execution.initial_child, __LINE__);
        host::CHost runtime{ host_allocator };
        require(Access::prepare_threads(runtime), __LINE__);
        if (child_refusal) Access::refuse_child_starts(runtime);
        host::SClientRecord* const client = Access::driver(runtime, module_ids::rendering, binding, execution);
        require(client && !client->preamble->failed(), __LINE__);
        const auto tick = [&]
        {
            Access::pump_one(runtime, module_ids::rendering);
            Access::progress_controls(runtime);
            require(expected_failure || !client->preamble->failed(), __LINE__);
        };
        const auto until = [&](const auto& predicate)
        {
            require(tests::wait_until(conversion, [&]
            {
                tick();
                return predicate();
            }), __LINE__);
        };
        if (!child_refusal) until([&] { return client->preamble->ready(); });
        const auto deliver = [&](const std::int32_t slot, const auto& payload)
        {
            require(client->channel->to_module.post(threading::make_pod_message(slot, payload)) == host::EModulePipePost::published, __LINE__);
            tick();
        };
        const auto read = [&]
        {
            threading::CErasedPodMsg message;
            until([&] { return client->read(message); });
            client->accounted_pod();
            return message;
        };
        const auto acknowledge = [&](const threading::CErasedPodMsg& message, const services::SServiceBorrow borrow = services::SServiceBorrow{},
            const threading::SNotificationEndpoint notification = threading::SNotificationEndpoint{})
        {
            services::ProviderOperationResult result;
            result.status = services::EServiceStatus::success;
            result.borrow = borrow;
            result.notification = notification;
            deliver(message.query_async_slot(), result);
        };
        services::SDispatchedCommand command;
        command.contract = { 1u, 1u };
        command.client = { 90u };
        command.command.route = { 91u };
        command.command.request = { 1u };
        command.command.opcode = 1u;
        command.command.payload_size = 1u;
        command.command.payload[0] = 29u;
        const auto submit = [&]
        {
            deliver(20, services::ServiceCommand{ command.command.request.value, &command });
        };
        const auto completion = [&](const services::EServiceStatus expected = services::EServiceStatus::success)
        {
            const threading::CErasedPodMsg message = read();
            services::CompleteServiceRequest complete;
            require(message.copy_payload_to(complete) && (complete.reply.route.value == command.command.route.value) &&
                (complete.reply.request.value == command.command.request.value) && (complete.reply.opcode == command.command.opcode) &&
                (complete.reply.status == expected), __LINE__);
            acknowledge(message);
            return complete.reply;
        };
        const bool notify = notification_only || (std::strcmp(scenario, "notify-cancel") == 0);
        const bool borrow = notify || (std::strcmp(scenario, "borrow-cancel") == 0) || late_stop;
        std::atomic<std::uint32_t> notified{ 0u };
        threading::CNotification notification;
        const unsigned* borrowed = nullptr;
        const pump_tests::SNotificationData* notification_data = nullptr;
        unsigned creation_count = 0u, disposal_count = 0u;
        if (child_refusal)
        {
            until([&] { return client->preamble->destroyed(); });
            require(client->preamble->failed() && Access::children_idle(runtime, module_ids::rendering), __LINE__);
        }
        else if (asset_refusal)
        {
            command.command.opcode = test_refusal ? 10u : 4u;
            submit();
            threading::CErasedOwnerMsg transfer;
            if (test_refusal)
            {
                until([&] { return client->capture(transfer); });
                client->accounted_owner();
                require((transfer.query_async_slot() == 101) && transfer.is_message_a<filesystem::FilesystemRefreshRequest>(), __LINE__);
                transfer.take_owner().destroy();
                filesystem::FilesystemRefreshResult refreshed;
                refreshed.status = filesystem::EFilesystemStatus::success;
                deliver(101, refreshed);
            }
            until([&] { return client->capture(transfer); });
            client->accounted_owner();
            const std::int32_t transfer_slot = test_refusal ? 102 : 210;
            require((transfer.query_async_slot() == transfer_slot) && transfer.is_message_a<RawAssetTransfer>(), __LINE__);
            transfer.take_owner().destroy();
            const bool acquisition_refusal = std::strstr(scenario, "acquire") != nullptr;
            AssetResult result;
            result.status = acquisition_refusal ? EAssetStatus::success : EAssetStatus::allocation_failed;
            if (acquisition_refusal) result.asset = CAssetId{ 100u };
            deliver(transfer_slot, result);
            if (acquisition_refusal)
            {
                const threading::CErasedPodMsg requested = read();
                AssetAcquireRequest acquire;
                require(requested.copy_payload_to(acquire) && (acquire.source == result.asset), __LINE__);
                AssetAcquireResult refusal;
                refusal.status = EAssetStatus::invalid_asset;
                deliver(requested.query_async_slot(), refusal);
                const threading::CErasedPodMsg disposal = read();
                AssetDisposeRequest dispose;
                require(disposal.copy_payload_to(dispose) && (dispose.asset == result.asset), __LINE__);
                require(!client->preamble->destroyed(), __LINE__);
                deliver(disposal.query_async_slot(), AssetDisposeResult{ result.asset, EAssetStatus::success });
            }
            (void)completion(services::EServiceStatus::cancelled);
            require(!client->channel->to_host.readable_count() && !client->endpoint->worker_to_host_owned_msgs.readable_count(), __LINE__);
        }
        else if (borrow)
        {
            command.command.opcode = notify ? 6u : 5u;
            submit();
            const threading::CErasedPodMsg message = read();
            services::PrepareServiceBorrow prepared;
            require(message.copy_payload_to(prepared) && prepared.offer.view.descriptor && (prepared.offer.view.version == 1u), __LINE__);
            if (notify)
            {
                notification_data = static_cast<const pump_tests::SNotificationData*>(prepared.offer.view.descriptor);
                require((prepared.offer.view.size == sizeof(*notification_data)) && notification.initialise(notified), __LINE__);
            }
            else
            {
                borrowed = static_cast<const unsigned*>(prepared.offer.view.descriptor);
                require((prepared.offer.view.size == sizeof(*borrowed)) && (*borrowed == 4242u), __LINE__);
            }
            acknowledge(message, { 92u });
            require(!client->channel->to_host.readable_count(), __LINE__);
            if (notify)
            {
                command.command.opcode = 7u;
                command.command.request = { 2u };
                const pump_tests::SNotificationReady ready{ { 92u }, { 93u } };
                command.command.payload_size = sizeof(ready);
                std::memcpy(command.command.payload, &ready, sizeof(ready));
                submit();
                const threading::CErasedPodMsg binding_message = read();
                services::BindServiceNotification bound;
                require(binding_message.copy_payload_to(bound) && (bound.borrow.value == 92u) && (bound.registration.value == 93u), __LINE__);
                if (notification_only)
                {
                    if (stop_binding) Access::replace(runtime, module_ids::rendering);
                    for (unsigned turn = 0u; turn < 4u; ++turn) tick();
                    require(!client->preamble->destroyed() && !client->preamble->work_pending() &&
                        !client->channel->to_host.readable_count() && !module_memory.is_attribution_empty(), __LINE__);
                    // Controlled padding on this pumped context's same native
                    // owner. It is removed directly, never counted as ingress.
                    const std::uint64_t published = client->endpoint->published_pods;
                    const std::uint32_t capacity = client->channel->to_host.capacity();
                    const threading::CErasedPodMsg padding = threading::make_pod_message(-1, AssetDisposeRequest{});
                    for (std::uint32_t count = 0u; count < capacity; ++count)
                    {
                        require(client->channel->to_host.post(padding) == host::EModulePipePost::published, __LINE__);
                    }
                    services::ProviderOperationResult refusal;
                    refusal.status = services::EServiceStatus::closing;
                    deliver(binding_message.query_async_slot(), refusal);
                    for (unsigned turn = 0u; turn < 4u; ++turn) tick();
                    require(!client->preamble->work_pending() && !client->preamble->destroyed() &&
                        (client->endpoint->published_pods == published) &&
                        (client->channel->to_host.readable_count() == capacity), __LINE__);
                    threading::CErasedPodMsg discarded;
                    for (std::uint32_t count = 0u; count < capacity; ++count)
                    {
                        require(client->channel->to_host.read(discarded) && discarded.is_payload_a<AssetDisposeRequest>(), __LINE__);
                    }
                    client->endpoint->signal_consumer();
                    const threading::CErasedPodMsg completed = read();
                    services::CompleteServiceRequest refused;
                    require(completed.copy_payload_to(refused) && (refused.reply.route.value == 91u) &&
                        (refused.reply.request.value == 2u) && (refused.reply.opcode == 7u) &&
                        (refused.reply.status == services::EServiceStatus::cancelled), __LINE__);
                    for (unsigned turn = 0u; turn < 4u; ++turn) tick();
                    require(!client->preamble->work_pending() && !client->preamble->destroyed() &&
                        !client->channel->to_host.readable_count() && !module_memory.is_attribution_empty(), __LINE__);
                    acknowledge(completed);
                    require(notification_data->value.load(std::memory_order_acquire) == 0u, __LINE__);
                    require(!notification.reader().pending(), __LINE__);
                }
                else
                {
                    acknowledge(binding_message, {}, notification.endpoint());
                    (void)completion();
                    until([&] { return notification_data->value.load(std::memory_order_acquire) == 1u; });
                    require(notification.reader().pending(), __LINE__);
                }
            }
        }
        else if (partial_credits)
        {
            command.command.opcode = 4u;
            submit();
            threading::CErasedOwnerMsg transfer;
            until([&] { return client->capture(transfer); });
            client->accounted_owner();
            require((transfer.query_async_slot() == 210) && transfer.is_message_a<RawAssetTransfer>(), __LINE__);
            transfer.take_owner().destroy();
            AssetResult result;
            result.asset = CAssetId{ 100u };
            result.status = EAssetStatus::success;
            const bool acquired = std::strcmp(scenario, "credit-stop-acquire") == 0;
            if (acquired)
            {
                deliver(210, result);
                require(read().is_payload_a<AssetAcquireRequest>(), __LINE__);
            }
            Access::replace(runtime, module_ids::rendering);
            for (unsigned turn = 0u; turn < 4u; ++turn) tick();
            require(!client->preamble->destroyed() && !client->preamble->work_pending(), __LINE__);
            if (acquired)
            {
                AssetAcquireResult acquisition;
                acquisition.acquisition = CAssetAcquisitionId{ 700u };
                acquisition.status = EAssetStatus::success;
                deliver(211, acquisition);
            }
            else deliver(210, result);
            const threading::CErasedPodMsg message = read();
            AssetDisposeRequest dispose;
            require(message.copy_payload_to(dispose) && (dispose.asset == result.asset) && (message.query_async_slot() == 1000), __LINE__);
            if (acquired)
            {
                const threading::CErasedPodMsg released = read();
                AssetReleaseRequest release;
                require(released.copy_payload_to(release) && (release.acquisition == CAssetAcquisitionId{ 700u }), __LINE__);
                deliver(released.query_async_slot(), AssetReleaseResult{ release.acquisition, EAssetStatus::success });
            }
            require(!client->preamble->destroyed(), __LINE__);
            deliver(message.query_async_slot(), AssetDisposeResult{ result.asset, EAssetStatus::success });
            (void)completion(services::EServiceStatus::cancelled);
            require(!client->channel->to_host.readable_count() && !client->endpoint->worker_to_host_owned_msgs.readable_count(), __LINE__);
        }
        else if (full_credits)
        {
            command.command.opcode = 4u;
            submit();
            while (creation_count != 512u)
            {
                threading::CErasedOwnerMsg message;
                until([&] { return client->capture(message); });
                client->accounted_owner();
                require((message.query_async_slot() == 210) && message.is_message_a<RawAssetTransfer>(), __LINE__);
                message.take_owner().destroy();
                AssetResult result;
                result.asset = CAssetId{ 100u + creation_count };
                result.status = EAssetStatus::success;
                deliver(210, result);
                const threading::CErasedPodMsg acquisition_message = read();
                AssetAcquireRequest acquisition;
                require(acquisition_message.copy_payload_to(acquisition) && (acquisition.source == result.asset), __LINE__);
                AssetAcquireResult acquired;
                acquired.acquisition = CAssetAcquisitionId{ 700u + creation_count };
                acquired.status = EAssetStatus::success;
                deliver(211, acquired);
                ++creation_count;
            }
            while (disposal_count != 512u)
            {
                const threading::CErasedPodMsg message = read();
                AssetDisposeRequest dispose;
                require(message.copy_payload_to(dispose) && (message.query_async_slot() == (1000 + static_cast<std::int32_t>(disposal_count))) &&
                    (dispose.asset == CAssetId{ 100u + disposal_count }), __LINE__);
                ++disposal_count; // Keep the synthetic disposal reply outstanding until release.
            }
            (void)completion();
            command.command.request = { 2u };
            command.command.opcode = 11u;
            submit();
            for (unsigned count = 0u; count < 1024u; ++count)
            {
                require(read().is_payload_a<services::CancelServiceRequest>(), __LINE__);
            }
            (void)completion();
        }
        else
        {
            submit();
            require(completion().payload[0] == 29u, __LINE__);
            command.contract = { 2u, 3u };
            command.command.request = { 2u };
            submit();
            require(completion().payload[0] == 33u, __LINE__);
            command.contract = { 1u, 1u };
            command.command.request = { 3u };
            command.command.opcode = 3u;
            submit();
            until([&] { return client->channel->to_host.writable_count() == 0u; });
            for (unsigned turn = 0u; turn < 4u; ++turn) tick();
            require(!client->preamble->work_pending() && (client->channel->to_host.readable_count() == 1024u), __LINE__);
            for (unsigned count = 0u; count < 1024u; ++count)
            {
                const threading::CErasedPodMsg message = read();
                AssetDisposeRequest dispose;
                require(message.copy_payload_to(dispose) && !dispose.asset && (message.query_async_slot() == 200), __LINE__);
                deliver(200, AssetDisposeResult{ {}, EAssetStatus::invalid_asset });
            }
            (void)completion();
        }
        if (!late_stop && !notification_only) Access::replace(runtime, module_ids::rendering);
        if (borrow)
        {
            if (!late_stop)
            {
                const threading::CErasedPodMsg message = read();
                services::BeginServiceBorrowRetirement retiring;
                require(message.copy_payload_to(retiring) && (retiring.borrow.value == 92u), __LINE__);
                acknowledge(message);
            }
            for (unsigned turn = 0u; turn < 4u; ++turn) tick();
            require(!client->preamble->destroyed() && !client->preamble->work_pending() && !module_memory.is_attribution_empty(), __LINE__);
            if (borrowed) require(*borrowed == 4242u, __LINE__);
            else require(notification_data->value.load(std::memory_order_acquire) == (notification_only ? 0u : 1u), __LINE__);
            services::ServiceBorrowControl control;
            control.borrow = { 92u };
            control.control = services::EServiceBorrowControl::consumer_quiescent;
            deliver(-1, control);
            const threading::CErasedPodMsg finished = read();
            services::FinishServiceBorrowRetirement retired;
            require(finished.copy_payload_to(retired) && (retired.borrow.value == 92u), __LINE__);
            if (notify) require(notification_data->value.load(std::memory_order_acquire) == (notification_only ? 0u : 2u), __LINE__);
            if (stop_finishing)
            {
                Access::replace(runtime, module_ids::rendering);
                for (unsigned turn = 0u; turn < 4u; ++turn) tick();
                require(!client->channel->to_host.readable_count() && !client->preamble->destroyed() &&
                    !module_memory.is_attribution_empty() && (*borrowed == 4242u), __LINE__);
            }
            acknowledge(finished);
            command.command.request = { 1u };
            command.command.opcode = notify ? 6u : 5u;
            if (late_stop)
            {
                const threading::CErasedPodMsg message = read();
                services::CompleteServiceRequest complete;
                require(message.copy_payload_to(complete) && (complete.reply.route.value == 91u) &&
                    (complete.reply.request.value == 1u) && (complete.reply.opcode == 5u), __LINE__);
                if (stop_completing) Access::replace(runtime, module_ids::rendering);
                for (unsigned turn = 0u; turn < 4u; ++turn) tick();
                require(!client->channel->to_host.readable_count() && !client->preamble->destroyed() &&
                    !module_memory.is_attribution_empty() && (*borrowed == 4242u), __LINE__);
                acknowledge(message);
            }
            else (void)completion(notification_only ? services::EServiceStatus::cancelled : services::EServiceStatus::success);
        }
        if (full_credits)
        {
            for (unsigned count = 0u; count < 512u; ++count)
            {
                const threading::CErasedPodMsg message = read();
                AssetReleaseRequest release;
                require(message.copy_payload_to(release) && (message.query_async_slot() == (1512 + static_cast<std::int32_t>(count))) &&
                    (release.acquisition == CAssetAcquisitionId{ 700u + count }), __LINE__);
                deliver(message.query_async_slot(), AssetReleaseResult{ release.acquisition, EAssetStatus::success });
                deliver((1000 + static_cast<std::int32_t>(count)), AssetDisposeResult{ CAssetId{ 100u + count }, EAssetStatus::success });
            }
        }
        until([&] { return client->preamble->destroyed(); });
        require(client->preamble->failed() == expected_failure, __LINE__);
        require(Access::children_idle(runtime, module_ids::rendering) && module_memory.is_attribution_empty(), __LINE__);
        if (!credits)
        {
            require((attribution.allocations == attribution.frees) && (attribution.allocated_on == caller) &&
                (attribution.freed_on == caller), __LINE__);
        }
        require(Access::retire(runtime, module_ids::rendering), __LINE__);
        Access::clear(runtime);
        require(module_memory.is_attribution_empty() && shared_host_memory.is_attribution_empty() &&
            (attribution.allocations == attribution.frees), __LINE__);
    }
#if MV_PLATFORM_WINDOWS
    TEST_EXPECT(ctx, (_putenv_s("MORPHIC_PUMP_CASE", previous.c_str()) == 0));
#else
    TEST_EXPECT(ctx, (previous.empty() ? (unsetenv("MORPHIC_PUMP_CASE") == 0) : (setenv("MORPHIC_PUMP_CASE", previous.c_str(), 1) == 0)));
#endif
    TEST_EXPECT(ctx, binding.unbind());
    TEST_EXPECT(ctx, debug_service->stop());
    return ctx.report(notification_only ? "ProviderNotification" :
        (retirement_only ? "ProviderRetirement" : (refusals_only ? "ProviderRefusals" : "ProviderExecution")));
}

namespace
{
struct SCreditFacade : tests::TPodSubmissionCapture<4u>
{
    unsigned owners{}, owner_attempts{};
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SCreditFacade& self = *static_cast<SCreditFacade*>(cookie);
        ++self.owner_attempts;
        const RawAssetTransfer* const transfer = message.owner().payload<RawAssetTransfer>();
        self.valid &= (destination.kind == modules::SDestination::EKind::host) && transfer && (message.query_async_slot() == 210);
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
        SCreditFacade& self = *static_cast<SCreditFacade*>(cookie);
        return self.capture(destination, message);
    }
};

void pump_credit_custody(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SCreditFacade::pod, &SCreditFacade::owner };
    SCreditFacade facade;
    const modules::CCommunication communication{ functions, &facade };
    pump_tests::CCreditExecution credit{ communication };
    credit.start();
    const auto advance = [&]
    {
        bool runnable = false;
        std::uint64_t next_wake = UINT64_MAX;
        facade.count = 0u;
        credit.advance(runnable, next_wake);
        TEST_EXPECT(ctx, (!credit.failed() && facade.valid && (facade.count <= 4u)));
        return runnable;
    };
    for (unsigned index = 0u; index < 600u; ++index)
    {
        facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!advance() && !advance() && (facade.owners == index)));
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, !advance());
        const unsigned attempts = facade.owner_attempts;
        TEST_EXPECT(ctx, (!advance() && (facade.owner_attempts == attempts) && (facade.owners == (index + 1u))));
        AssetResult transferred;
        transferred.asset = CAssetId{ index + 1u };
        transferred.kind = EAssetKind::raw;
        transferred.status = EAssetStatus::success;
        credit.receive(threading::make_pod_message(210, transferred));
        facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!advance() && !facade.count));
        facade.result = modules::ESubmission::accepted;
        TEST_EXPECT(ctx, (!advance() && (facade.count == 1u) && (facade.posts[0].query_async_slot() == 211)));
        AssetAcquireResult acquired;
        acquired.acquisition = CAssetAcquisitionId{ index + 1001u };
        acquired.status = EAssetStatus::success;
        credit.receive(threading::make_pod_message(211, acquired));
    }
    unsigned disposals = 0u;
    bool marker = false;
    for (unsigned turn = 0u; (turn < 160u) && !marker; ++turn)
    {
        facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!advance() && !facade.count));
        facade.result = modules::ESubmission::published_wake_failed;
        (void)advance();
        for (unsigned post = 0u; post < facade.count; ++post)
        {
            const threading::CErasedPodMsg& message = facade.posts[post];
            if (message.query_async_slot() == 214)
            {
                TEST_EXPECT(ctx, (!marker && (disposals == 600u)));
                marker = true;
            }
            else
            {
                AssetDisposeRequest request;
                TEST_EXPECT(ctx, ((message.query_async_slot() == (1000 + static_cast<std::int32_t>(disposals))) &&
                    message.copy_payload_to(request) && (request.asset == CAssetId{ disposals + 1u })));
                ++disposals;
            }
        }
    }
    TEST_EXPECT(ctx, (marker && !credit.ready_to_stop() && !credit.quiescent()));
    AssetAcquireResult invalid;
    invalid.status = EAssetStatus::invalid_asset;
    credit.receive(threading::make_pod_message(214, invalid));
    unsigned releases = 0u;
    for (unsigned turn = 0u; (turn < 160u) && !credit.ready_to_stop(); ++turn)
    {
        facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, !advance());
        facade.result = modules::ESubmission::published_wake_failed;
        (void)advance();
        for (unsigned post = 0u; post < facade.count; ++post)
        {
            AssetReleaseRequest request;
            TEST_EXPECT(ctx, ((facade.posts[post].query_async_slot() == (2000 + static_cast<std::int32_t>(releases))) &&
                facade.posts[post].copy_payload_to(request) && (request.acquisition == CAssetAcquisitionId{ releases + 1001u })));
            ++releases;
        }
    }
    credit.stop();
    TEST_EXPECT(ctx, ((releases == 600u) && credit.ready_to_stop() && !credit.quiescent() && !advance()));
    // Results deliberately arrive after stop and in reverse order. The final
    // mandatory result, rather than accepted publication, permits completion.
    for (unsigned index = 600u; index;)
    {
        --index;
        credit.receive(threading::make_pod_message((2000 + static_cast<std::int32_t>(index)),
            AssetReleaseResult{ CAssetAcquisitionId{ index + 1001u }, EAssetStatus::success }));
        TEST_EXPECT(ctx, !credit.quiescent());
        credit.receive(threading::make_pod_message((1000 + static_cast<std::int32_t>(index)),
            AssetDisposeResult{ CAssetId{ index + 1u }, EAssetStatus::success }));
    }
    TEST_EXPECT(ctx, (credit.quiescent() && !credit.failed() && facade.valid));

    // Stop before transfer acceptance, after accepted transfer, and after
    // accepted acquisition; then recognized transfer/acquire refusal and an
    // unproven cleanup outcome. No terminal result leaves an impossible wait.
    for (unsigned mode = 0u; mode < 6u; ++mode)
    {
        SCreditFacade local;
        const modules::CCommunication local_communication{ functions, &local };
        pump_tests::CCreditExecution state{ local_communication };
        state.start();
        const auto step = [&]
        {
            bool runnable = false;
            std::uint64_t next_wake = UINT64_MAX;
            local.count = 0u;
            state.advance(runnable, next_wake);
            return runnable;
        };
        local.result = (mode == 0u) ? modules::ESubmission::busy : modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, !step());
        if (mode <= 1u) state.stop();
        if (mode)
        {
            AssetResult result;
            result.status = (mode == 3u) ? EAssetStatus::allocation_failed : EAssetStatus::success;
            result.kind = EAssetKind::raw;
            if (mode != 3u) result.asset = CAssetId{ 1u };
            state.receive(threading::make_pod_message(210, result));
            if ((mode >= 2u) && (mode != 3u))
            {
                TEST_EXPECT(ctx, (!step() && (local.count == 1u) && (local.posts[0].query_async_slot() == 211)));
                state.stop();
                TEST_EXPECT(ctx, (!step() && !local.count && !state.quiescent()));
                AssetAcquireResult acquired;
                acquired.status = (mode == 4u) ? EAssetStatus::invalid_asset : EAssetStatus::success;
                if (mode != 4u) acquired.acquisition = CAssetAcquisitionId{ 1001u };
                state.receive(threading::make_pod_message(211, acquired));
            }
        }
        state.stop();
        local.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!step() && !local.count));
        local.result = modules::ESubmission::published_wake_failed;
        unsigned disposal_posts = 0u, release_posts = 0u;
        for (unsigned turn = 0u; turn < 4u; ++turn)
        {
            (void)step();
            for (unsigned post = 0u; post < local.count; ++post)
            {
                if (local.posts[post].query_async_slot() == 1000) ++disposal_posts;
                else if (local.posts[post].query_async_slot() == 2000) ++release_posts;
                else TEST_EXPECT(ctx, false);
            }
        }
        const bool has_asset = mode && (mode != 3u);
        const bool has_acquisition = (mode == 2u) || (mode == 5u);
        TEST_EXPECT(ctx, ((disposal_posts == (has_asset ? 1u : 0u)) && (release_posts == (has_acquisition ? 1u : 0u))));
        if (has_acquisition)
        {
            state.receive(threading::make_pod_message(2000,
                AssetReleaseResult{ CAssetAcquisitionId{ 1001u }, EAssetStatus::success }));
        }
        TEST_EXPECT(ctx, (state.quiescent() == !has_asset));
        if (has_asset)
        {
            state.receive(threading::make_pod_message(1000, AssetDisposeResult{ CAssetId{ 1u },
                (mode == 5u) ? EAssetStatus::invalid_asset : EAssetStatus::success }));
        }
        TEST_EXPECT(ctx, ((state.quiescent() == (mode != 5u)) && (state.failed() == (mode >= 3u)) && !step() && local.valid));
    }
}
}

namespace
{
void pump_borrow_custody(tests::TTestContext& ctx)
{
    using EMode = pump_tests::CBorrowExecution::EMode;
    static const modules::SCommunicationV1 functions{ &SPodCapture::pod };
    for (const EMode mode : { EMode::normal, EMode::cancel, EMode::abandon, EMode::shutdown, EMode::provider_failure })
    {
        SPodCapture facade;
        const modules::CCommunication communication{ functions, &facade };
        pump_tests::CBorrowExecution borrow{ communication };
        TEST_EXPECT(ctx, borrow.start({ 17u }, mode));
        unsigned requests = 0u, releases = 0u, cancellations = 0u, quiescences = 0u;
        const auto step = [&]
        {
            bool runnable = false;
            std::uint64_t next_wake = UINT64_MAX;
            facade.count = 0u;
            borrow.advance(runnable, next_wake);
            TEST_EXPECT(ctx, (!borrow.failed() && facade.valid));
            for (unsigned post = 0u; post < facade.count; ++post)
            {
                const threading::CErasedPodMsg& message = facade.posts[post];
                if (message.is_payload_a<services::ServiceRequest>()) ++requests;
                else if (message.is_payload_a<services::ReleaseServiceBorrow>()) ++releases;
                else if (message.is_payload_a<services::CancelServiceRequest>()) ++cancellations;
                else if (message.is_payload_a<services::ServiceBorrowQuiescent>()) ++quiescences;
                else TEST_EXPECT(ctx, false);
            }
            return runnable;
        };
        TEST_EXPECT(ctx, (!step() && !step() && !requests && !borrow.domain_complete()));
        facade.result = modules::ESubmission::published_wake_failed;
        (void)step();
        TEST_EXPECT(ctx, (!step() && (requests == 1u)));
        services::ServiceReply reply;
        reply.route = { 17u };
        reply.request = { 1u };
        reply.opcode = 5u;
        reply.status = services::EServiceStatus::success;
        reply.notice = services::EServiceNotice::acknowledged;
        borrow.receive(threading::make_pod_message(70, reply));
        unsigned value = 4242u;
        const services::SPreparedServiceBorrow prepared{ { 31u }, { &value, sizeof(value), 1u } };
        reply.notice = services::EServiceNotice::prepared;
        reply.payload_size = sizeof(prepared);
        std::memcpy(reply.payload, &prepared, sizeof(prepared));
        borrow.receive(threading::make_pod_message(70, reply));
        if (mode == EMode::cancel)
        {
            facade.result = modules::ESubmission::busy;
            TEST_EXPECT(ctx, (!step() && !step() && !cancellations && !quiescences));
            facade.result = modules::ESubmission::published_wake_failed;
            for (unsigned turn = 0u; turn < 3u; ++turn) (void)step();
            TEST_EXPECT(ctx, ((cancellations == 1u) && (quiescences == 1u) && !releases));
        }
        reply.notice = services::EServiceNotice::completed;
        reply.payload_size = 0u;
        reply.status = (mode == EMode::cancel) ? services::EServiceStatus::cancelled : services::EServiceStatus::success;
        borrow.receive(threading::make_pod_message(70, reply));
        if (mode == EMode::abandon)
        {
            TEST_EXPECT(ctx, (borrow.self_stop_due() && borrow.domain_complete() && !borrow.done()));
            value = 0u;
            borrow.stop();
            for (unsigned turn = 0u; turn < 3u; ++turn) TEST_EXPECT(ctx, !step());
            TEST_EXPECT(ctx, (!releases && !quiescences && !cancellations));
            continue;
        }
        if ((mode == EMode::shutdown) || (mode == EMode::provider_failure))
        {
            TEST_EXPECT(ctx, (!step() && !releases && !borrow.domain_complete()));
            TEST_EXPECT(ctx, (borrow.self_stop_due() == (mode == EMode::shutdown)));
            borrow.stop();
        }
        if (mode != EMode::cancel)
        {
            facade.result = modules::ESubmission::busy;
            TEST_EXPECT(ctx, (!step() && !step() && !releases && !quiescences && !borrow.domain_complete()));
            facade.result = modules::ESubmission::published_wake_failed;
            for (unsigned turn = 0u; turn < 3u; ++turn) (void)step();
            TEST_EXPECT(ctx, ((releases == 1u) && (quiescences == 1u) && !cancellations));
        }
        value = 0u; // No later callback may dereference the relinquished view.
        TEST_EXPECT(ctx, (!borrow.done() && !borrow.domain_complete() && !step()));
        services::ServiceBorrowControl control{ { 17u }, { 1u }, { 31u },
            services::EServiceBorrowControl::retired, services::EServiceQuiescence::relinquished };
        borrow.receive(threading::make_pod_message(-1, control));
        TEST_EXPECT(ctx, (!step() && borrow.done() && borrow.domain_complete()));
        const unsigned attempts = facade.pod_attempts;
        TEST_EXPECT(ctx, (!step() && (facade.pod_attempts == attempts)));
    }
    // Closing before prepared never makes the late descriptor readable again.
    SPodCapture facade;
    const modules::CCommunication communication{ functions, &facade };
    pump_tests::CBorrowExecution borrow{ communication };
    TEST_EXPECT(ctx, borrow.start({ 18u }, EMode::provider_failure));
    bool runnable = false;
    std::uint64_t wake = UINT64_MAX;
    facade.result = modules::ESubmission::accepted;
    borrow.advance(runnable, wake);
    services::ServiceReply reply;
    reply.route = { 18u };
    reply.request = { 1u };
    reply.opcode = 5u;
    reply.status = services::EServiceStatus::success;
    reply.notice = services::EServiceNotice::acknowledged;
    borrow.receive(threading::make_pod_message(70, reply));
    services::ServiceBorrowControl control{ { 18u }, { 1u }, { 32u }, services::EServiceBorrowControl::closing };
    borrow.receive(threading::make_pod_message(-1, control));
    unsigned late_value = 99u; // Would fail the validation if the closed view were used.
    const services::SPreparedServiceBorrow prepared{ { 32u }, { &late_value, sizeof(late_value), 1u } };
    reply.notice = services::EServiceNotice::prepared;
    reply.payload_size = sizeof(prepared);
    std::memcpy(reply.payload, &prepared, sizeof(prepared));
    borrow.receive(threading::make_pod_message(70, reply));
    facade.result = modules::ESubmission::busy;
    facade.count = 0u;
    borrow.advance(runnable, wake);
    TEST_EXPECT(ctx, (!borrow.failed() && !runnable && !facade.count && !borrow.domain_complete()));
    facade.result = modules::ESubmission::published_wake_failed;
    borrow.advance(runnable, wake);
    TEST_EXPECT(ctx, ((facade.count == 1u) && facade.posts[0].is_payload_a<services::ServiceBorrowQuiescent>()));
    reply.notice = services::EServiceNotice::completed;
    reply.payload_size = 0u;
    borrow.receive(threading::make_pod_message(70, reply));
    control.control = services::EServiceBorrowControl::retired;
    control.quiescence = services::EServiceQuiescence::relinquished;
    borrow.receive(threading::make_pod_message(-1, control));
    borrow.advance(runnable, wake);
    TEST_EXPECT(ctx, (borrow.done() && borrow.domain_complete() && !borrow.failed()));
    pump_tests::CBorrowExecution unsent{ communication };
    TEST_EXPECT(ctx, unsent.start({ 19u }, EMode::normal));
    facade.result = modules::ESubmission::busy;
    facade.count = 0u;
    unsent.advance(runnable, wake);
    unsent.stop();
    unsent.advance(runnable, wake);
    TEST_EXPECT(ctx, (unsent.done() && unsent.domain_complete() && !unsent.failed() && !facade.count));
}

void pump_shutdown_close(tests::TTestContext& ctx)
{
    using Access = pump_tests::SExecutiveExecutionTestAccess;
    static const modules::SCommunicationV1 functions{ &SPodCapture::pod };
    for (unsigned mode = 0u; mode < 9u; ++mode)
    {
        SPodCapture facade;
        facade.result = modules::ESubmission::busy;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        pump_tests::CExecutiveExecution execution{ context, (mode == 5u) ? "normal" : "shutdown-pending", false };
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        threading::CErasedPodMsg message;
        if ((mode < 5u) || (mode == 8u))
        {
            TEST_EXPECT(ctx, Access::pending_shutdown(execution));
            invocation.event = modules::EExecutionEvent::stop;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (!progress.work_complete && !facade.count));
            services::ServiceReply delayed;
            delayed.route = { 17u };
            delayed.request = { 5u };
            delayed.opcode = 2u;
            delayed.status = services::EServiceStatus::success;
            delayed.notice = services::EServiceNotice::completed;
            message = threading::make_pod_message(44, delayed);
            invocation.event = modules::EExecutionEvent::message;
            invocation.message = &message;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, (!progress.work_complete && !facade.count));
        }
        else TEST_EXPECT(ctx, Access::unproven_shutdown_close(execution, mode));
        invocation.event = modules::EExecutionEvent::service_async;
        invocation.message = nullptr;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (!progress.work_complete && !progress.work_pending && !facade.count));
        facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        services::CloseServiceRequest close;
        TEST_EXPECT(ctx, ((facade.count == 1u) && facade.posts[0].copy_payload_to(close) &&
            (close.route.value == 17u) && (close.request.value == 7u) && (facade.posts[0].query_async_slot() == 50)));
        const unsigned attempts = facade.pod_attempts;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (!progress.work_complete && (facade.pod_attempts == attempts)));
        services::ServiceReply reply;
        reply.route = { (mode == 2u) ? 18u : 17u };
        reply.request = { (mode == 3u) ? 8u : 7u };
        reply.notice = (mode == 4u) ? services::EServiceNotice::acknowledged : services::EServiceNotice::completed;
        reply.status = (mode == 0u) ? services::EServiceStatus::success : services::EServiceStatus::stale;
        message = threading::make_pod_message((mode == 8u) ? 51 : 50, reply);
        invocation.event = modules::EExecutionEvent::message;
        invocation.message = &message;
        const modules::EExecutionStatus expected = (mode < 2u) ? modules::EExecutionStatus::success : modules::EExecutionStatus::failed;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == expected));
        if (mode < 2u) TEST_EXPECT(ctx, progress.work_complete);
        // A diagnosed bad result reports failure once. This controlled fixture
        // owns no borrowed bytes; subsequent stop can deliver cleanup progress.
        invocation.event = modules::EExecutionEvent::stop;
        invocation.message = nullptr;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, (progress.work_complete && (facade.count == 1u) && (facade.pod_attempts == attempts)));
    }
}

void pump_close_retry(tests::TTestContext& ctx)
{
    using Access = pump_tests::SExecutiveExecutionTestAccess;
    static const modules::SCommunicationV1 functions{ &SPodCapture::pod };
    SPodCapture facade;
    facade.result = modules::ESubmission::published_wake_failed;
    const modules::CCommunication communication{ functions, &facade };
    modules::SExecutionContext context;
    context.communication = &communication;
    pump_tests::CExecutiveExecution execution{ context, "borrow-normal", false };
    TEST_EXPECT(ctx, Access::close(execution)); // Controlled post-borrow route-close boundary.
    modules::SInvocation invocation;
    modules::SExecutionProgress progress;
    const auto invoke = [&]
    {
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
    };
    invocation.event = modules::EExecutionEvent::service_async;
    invoke();
    TEST_EXPECT(ctx, (facade.count == 1u));
    services::ServiceReply reply;
    reply.route = { 17u };
    reply.request = { 2u };
    reply.status = services::EServiceStatus::busy;
    reply.notice = services::EServiceNotice::completed;
    threading::CErasedPodMsg message = threading::make_pod_message(71, reply);
    TEST_EXPECT(ctx, Access::terminal_busy(execution, message));
    // Pin a future retry due time for deterministic interleavings, then expire
    // only that timer. The overall five-second operation anchor must survive.
    TEST_EXPECT(ctx, Access::hold_retry(execution));
    const unsigned published = facade.count;
    invocation.message = nullptr;
    for (unsigned turn = 0u; turn < 4u; ++turn)
    {
        invocation.event = (turn & 1u) ? modules::EExecutionEvent::provider_admission_closed : modules::EExecutionEvent::service_async;
        invoke();
        TEST_EXPECT(ctx, ((facade.count == published) && !progress.work_pending && Access::original_deadlines(execution)));
    }
    TEST_EXPECT(ctx, Access::retry_due(execution));
    facade.result = modules::ESubmission::busy;
    invocation.event = modules::EExecutionEvent::service_async;
    invoke();
    TEST_EXPECT(ctx, ((facade.count == published) && !progress.work_pending));
    facade.result = modules::ESubmission::published_wake_failed;
    invoke();
    TEST_EXPECT(ctx, (facade.count == (published + 1u)));
    services::CloseServiceRequest retry;
    TEST_EXPECT(ctx, (facade.posts[published].copy_payload_to(retry) && (retry.route.value == 17u) && (retry.request.value == 2u) &&
        (facade.posts[published].query_async_slot() == 71)));
    const unsigned attempts = facade.pod_attempts;
    for (unsigned turn = 0u; turn < 4u; ++turn) invoke();
    TEST_EXPECT(ctx, ((facade.pod_attempts == attempts) && !progress.work_pending));
    reply.status = services::EServiceStatus::success;
    message = threading::make_pod_message(71, reply);
    invocation.event = modules::EExecutionEvent::message;
    invocation.message = &message;
    invoke();
    TEST_EXPECT(ctx, (progress.work_pending && facade.valid));
    pump_shutdown_close(ctx);
}
}

int run_pump_borrow_focused()
{
    tests::TTestContext ctx;
    pump_borrow_custody(ctx);
    return ctx.report("PumpBorrow");
}

int run_pump_close_focused()
{
    tests::TTestContext ctx;
    pump_close_retry(ctx);
    return ctx.report("PumpClose");
}

namespace
{
struct SNotificationConsumerFixture
{
    static inline const modules::SCommunicationV1 functions{ &SPodCapture::pod };
    SPodCapture facade;
    modules::CCommunication communication{ functions, &facade };
    pump_tests::CNotificationExecution consumer{ communication };
    pump_tests::SNotificationData data;
    std::atomic<std::uint32_t> pending{};
    threading::CNotification notification;
    unsigned requests{}, registrations{}, publishers{}, releases{}, cancellations{}, quiescences{};
    bool step() noexcept
    {
        bool runnable = false;
        std::uint64_t wake = UINT64_MAX;
        facade.count = 0u;
        consumer.advance(runnable, wake);
        for (unsigned i = 0u; i < facade.count; ++i)
        {
            const threading::CErasedPodMsg& message = facade.posts[i];
            if (message.is_payload_a<services::ServiceRequest>())
            {
                if (message.query_async_slot() == 70) ++requests;
                else if (message.query_async_slot() == 73) ++publishers;
                else facade.valid = false;
            }
            else if (message.is_payload_a<services::RegisterServiceNotification>()) ++registrations;
            else if (message.is_payload_a<services::ReleaseServiceBorrow>()) ++releases;
            else if (message.is_payload_a<services::CancelServiceRequest>()) ++cancellations;
            else if (message.is_payload_a<services::ServiceBorrowQuiescent>()) ++quiescences;
            else facade.valid = false;
        }
        return runnable;
    }
    void reply(const bool publisher, const services::EServiceNotice notice,
        const services::EServiceStatus status = services::EServiceStatus::success) noexcept
    {
        services::ServiceReply value;
        value.route = { 17u };
        value.request = { publisher ? 2u : 1u };
        value.opcode = publisher ? 7u : 6u;
        value.notice = notice;
        value.status = status;
        if (notice == services::EServiceNotice::prepared)
        {
            const services::SPreparedServiceBorrow prepared{ { 31u }, { &data, sizeof(data), 1u } };
            value.payload_size = sizeof(prepared);
            std::memcpy(value.payload, &prepared, sizeof(prepared));
        }
        if (publisher && (notice == services::EServiceNotice::completed) && (status == services::EServiceStatus::success))
        {
            const pump_tests::SNotificationReady echoed{ { 31u }, { 41u } };
            value.payload_size = sizeof(echoed);
            std::memcpy(value.payload, &echoed, sizeof(echoed));
        }
        consumer.receive(threading::make_pod_message(publisher ? 73 : 70, value));
    }
    void registration(const services::EServiceStatus status = services::EServiceStatus::success) noexcept
    {
        services::ServiceNotificationRegistered value{ { 17u }, { 31u }, { 41u }, notification.reader(), status };
        consumer.receive(threading::make_pod_message(72, value));
    }
    void retired() noexcept
    {
        consumer.receive(threading::make_pod_message(-1, services::ServiceBorrowControl{ { 17u }, { 1u }, { 31u },
            services::EServiceBorrowControl::retired, services::EServiceQuiescence::relinquished }));
    }
};

void pump_notification_custody(tests::TTestContext& ctx)
{
    using EMode = pump_tests::CNotificationExecution::EMode;
    using ENotice = services::EServiceNotice;
    for (const EMode mode : { EMode::normal, EMode::cancel, EMode::abandon, EMode::shutdown })
    {
        SNotificationConsumerFixture fixture;
        TEST_EXPECT(ctx, (fixture.notification.initialise(fixture.pending) && fixture.consumer.start({ 17u }, mode)));
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.step() && !fixture.requests));
        fixture.facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!fixture.step() && (fixture.requests == 1u)));
        fixture.reply(false, ENotice::acknowledged);
        fixture.reply(false, ENotice::prepared);
        fixture.facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.step() && !fixture.registrations));
        fixture.facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!fixture.step() && (fixture.registrations == 1u)));
        TEST_EXPECT(ctx, (!fixture.step() && (fixture.registrations == 1u)));
        fixture.registration();
        fixture.facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.step() && !fixture.publishers));
        fixture.facade.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!fixture.step() && (fixture.publishers == 1u)));
        fixture.reply(true, ENotice::acknowledged);
        fixture.reply(true, ENotice::completed);
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.consumer.self_stop_due() && !fixture.releases && !fixture.quiescences));
        fixture.data.value.store(1u, std::memory_order_release);
        TEST_EXPECT(ctx, fixture.notification.endpoint().signal());
        if (mode != EMode::cancel) fixture.reply(false, ENotice::completed);
        fixture.facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.consumer.failed()));
        TEST_EXPECT(ctx, (fixture.notification.reader().pending() == (mode == EMode::cancel)));
        if (mode == EMode::abandon)
        {
            TEST_EXPECT(ctx, (fixture.consumer.self_stop_due() && fixture.consumer.domain_complete() && !fixture.consumer.done()));
            fixture.data.value.store(99u);
            fixture.consumer.stop();
            TEST_EXPECT(ctx, (!fixture.step() && !fixture.releases && !fixture.quiescences));
            continue;
        }
        if (mode == EMode::shutdown)
        {
            TEST_EXPECT(ctx, (fixture.consumer.self_stop_due() && !fixture.consumer.domain_complete()));
            fixture.consumer.stop();
        }
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.quiescences && !fixture.releases && !fixture.cancellations));
        fixture.facade.result = modules::ESubmission::published_wake_failed;
        for (unsigned turn = 0u; turn < 3u; ++turn) (void)fixture.step();
        TEST_EXPECT(ctx, ((fixture.quiescences == 1u) && ((fixture.releases + fixture.cancellations) == 1u)));
        if (mode == EMode::cancel) fixture.reply(false, ENotice::completed, services::EServiceStatus::cancelled);
        TEST_EXPECT(ctx, (!fixture.consumer.domain_complete() && !fixture.step()));
        fixture.retired();
        TEST_EXPECT(ctx, (!fixture.step() && fixture.consumer.done() && fixture.consumer.domain_complete() &&
            !fixture.consumer.failed() && fixture.facade.valid));
    }
    // Stop at each distinct publication boundary: no submission, unsent
    // registration, accepted registration, unsent publisher, accepted publisher.
    for (unsigned boundary = 0u; boundary < 5u; ++boundary)
    {
        SNotificationConsumerFixture fixture;
        TEST_EXPECT(ctx, (fixture.notification.initialise(fixture.pending) && fixture.consumer.start({ 17u }, EMode::normal)));
        if (!boundary)
        {
            TEST_EXPECT(ctx, !fixture.step());
            fixture.consumer.stop();
            TEST_EXPECT(ctx, (!fixture.step() && fixture.consumer.domain_complete() && !fixture.requests));
            continue;
        }
        fixture.facade.result = modules::ESubmission::accepted;
        (void)fixture.step();
        fixture.reply(false, ENotice::acknowledged);
        fixture.reply(false, ENotice::prepared);
        fixture.facade.result = (boundary == 1u) ? modules::ESubmission::busy : modules::ESubmission::accepted;
        (void)fixture.step();
        if (boundary >= 3u)
        {
            fixture.registration();
            fixture.facade.result = (boundary == 3u) ? modules::ESubmission::busy : modules::ESubmission::accepted;
            (void)fixture.step();
        }
        fixture.consumer.stop();
        fixture.facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.step() && !fixture.consumer.domain_complete()));
        fixture.facade.result = modules::ESubmission::published_wake_failed;
        for (unsigned turn = 0u; turn < 3u; ++turn) (void)fixture.step();
        TEST_EXPECT(ctx, ((fixture.releases == 1u) && (fixture.quiescences == 1u) &&
            (fixture.publishers == ((boundary == 4u) ? 1u : 0u))));
        fixture.data.value.store(99u);
        fixture.reply(false, ENotice::completed, services::EServiceStatus::cancelled);
        fixture.retired();
        if ((boundary == 2u) || (boundary == 4u))
        {
            TEST_EXPECT(ctx, (!fixture.step() && !fixture.consumer.domain_complete()));
        }
        if (boundary == 2u) fixture.registration(); // Late reader is copied, never used.
        if (boundary == 4u)
        {
            fixture.reply(true, ENotice::acknowledged);
            fixture.reply(true, ENotice::completed, services::EServiceStatus::cancelled);
        }
        TEST_EXPECT(ctx, (!fixture.step() && fixture.consumer.done() && fixture.consumer.domain_complete() &&
            !fixture.consumer.failed() && fixture.facade.valid));
    }
    for (const bool publisher : { false, true })
    {
        SNotificationConsumerFixture fixture;
        TEST_EXPECT(ctx, (fixture.notification.initialise(fixture.pending) && fixture.consumer.start({ 17u }, EMode::normal)));
        fixture.facade.result = modules::ESubmission::accepted;
        (void)fixture.step();
        fixture.reply(false, ENotice::acknowledged);
        fixture.reply(false, ENotice::prepared);
        (void)fixture.step();
        if (publisher)
        {
            fixture.registration();
            (void)fixture.step();
            fixture.reply(true, ENotice::acknowledged);
            fixture.reply(true, ENotice::completed, services::EServiceStatus::cancelled);
        }
        else fixture.registration(services::EServiceStatus::closing);
        TEST_EXPECT(ctx, (fixture.consumer.failed() && !fixture.consumer.domain_complete()));
        fixture.facade.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.step() && !fixture.quiescences));
        fixture.facade.result = modules::ESubmission::published_wake_failed;
        for (unsigned turn = 0u; turn < 3u; ++turn) (void)fixture.step();
        TEST_EXPECT(ctx, ((fixture.quiescences == 1u) && (fixture.releases == 1u)));
        fixture.reply(false, ENotice::completed, services::EServiceStatus::cancelled);
        fixture.retired();
        TEST_EXPECT(ctx, (!fixture.step() && fixture.consumer.domain_complete() && fixture.consumer.failed() && fixture.facade.valid));
    }
}
}   // namespace

int run_pump_notification_focused()
{
    tests::TTestContext ctx;
    pump_notification_custody(ctx);
    return ctx.report("PumpNotification");
}

namespace
{
struct SModalFacade
{
    SPodCapture output;
    modules::ESubmission owner_result{ modules::ESubmission::busy };
    unsigned owners{};
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        return SPodCapture::pod(&static_cast<SModalFacade*>(cookie)->output, destination, message);
    }
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SModalFacade& self = *static_cast<SModalFacade*>(cookie);
        const filesystem::FilesystemRefreshRequest* const request = message.owner().payload<filesystem::FilesystemRefreshRequest>();
        self.output.valid &= request && (std::strcmp(request->root.cstring(), "unknown:") == 0) &&
            (destination.kind == modules::SDestination::EKind::host) && (message.query_async_slot() == (10000 + static_cast<std::int32_t>(self.owners)));
        if (self.owners == 32u) return modules::ESubmission::busy;
        if (modules::accepted(self.owner_result))
        {
            ++self.owners;
            message.take_owner().destroy();
        }
        return self.owner_result;
    }
};

void pump_modal_custody(tests::TTestContext& ctx)
{
    using EMode = pump_tests::CModalExecution::EMode;
    static const modules::SCommunicationV1 functions{ &SModalFacade::pod, &SModalFacade::owner };
    for (unsigned scenario = 0u; scenario < 5u; ++scenario)
    {
        SModalFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        modules::CHostRestrictionPublication publication;
        modules::SHostRestrictionSnapshot snapshot;
        snapshot.generation = 1u;
        TEST_EXPECT(ctx, (platform::threading::query_awake_nanoseconds(snapshot.active_start_ns).code == platform::threading::EWaitError::none));
        snapshot.depth = 3u;
        snapshot.counts[0] = 2u;
        snapshot.counts[1] = 1u;
        TEST_EXPECT(ctx, publication.publish(snapshot));
        modules::SHostExecutionContext execution;
        execution.restriction = publication.reader();
        std::atomic<std::uint32_t> stop_requested{};
        execution.stop.requested = &stop_requested;
        pump_tests::CModalExecution modal{ communication, &execution };
        const EMode mode = (scenario == 1u) ? EMode::full : ((scenario == 2u) ? EMode::shutdown : EMode::normal);
        TEST_EXPECT(ctx, modal.start({ 17u }, mode));
        unsigned requests = 0u, commands = 0u, pods = 0u, releases = 0u, quiescences = 0u;
        std::uint64_t next_wake = UINT64_MAX;
        const auto step = [&]
        {
            bool runnable = false;
            next_wake = UINT64_MAX;
            facade.output.count = 0u;
            modal.advance(runnable, next_wake);
            TEST_EXPECT(ctx, (!modal.failed() && facade.output.valid && (facade.output.count <= 4u)));
            for (unsigned count = 0u; count < facade.output.count; ++count)
            {
                const threading::CErasedPodMsg& message = facade.output.posts[count];
                if (message.query_async_slot() == 80) ++requests;
                else if (message.query_async_slot() == 81) ++commands;
                else if (message.is_payload_a<AssetReleaseRequest>())
                {
                    TEST_EXPECT(ctx, (message.query_async_slot() == (9000 + static_cast<std::int32_t>(pods))));
                    ++pods;
                }
                else if (message.is_payload_a<services::ReleaseServiceBorrow>()) ++releases;
                else if (message.is_payload_a<services::ServiceBorrowQuiescent>()) ++quiescences;
                else TEST_EXPECT(ctx, false);
            }
            return runnable;
        };
        TEST_EXPECT(ctx, (!step() && !step() && !requests));
        if (scenario == 3u)
        {
            modal.stop();
            TEST_EXPECT(ctx, (!step() && modal.domain_complete() && !requests));
            continue;
        }
        facade.output.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!step() && (requests == 1u)));
        services::ServiceReply reply;
        reply.route = { 17u };
        reply.request = { 1u };
        reply.opcode = 8u;
        reply.status = services::EServiceStatus::success;
        reply.notice = services::EServiceNotice::acknowledged;
        modal.receive(threading::make_pod_message(80, reply));
        pump_tests::SModalData data;
        pump_tests::SModalDescriptor descriptor{ &data };
        const services::SPreparedServiceBorrow prepared{ { 31u }, { &descriptor, sizeof(descriptor), 1u } };
        reply.notice = services::EServiceNotice::prepared;
        reply.payload_size = sizeof(prepared);
        std::memcpy(reply.payload, &prepared, sizeof(prepared));
        modal.receive(threading::make_pod_message(80, reply));
        reply.notice = services::EServiceNotice::completed;
        reply.payload_size = 0u;
        modal.receive(threading::make_pod_message(80, reply));
        TEST_EXPECT(ctx, (!step() && (next_wake <= 1000000u) && !commands));
        data.ready.store(1u, std::memory_order_release);
        facade.output.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!step() && !step() && (next_wake <= 1000000u) && !commands));
        facade.output.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!step() && (commands == 1u)));
        reply.request = { 2u };
        reply.opcode = 9u;
        reply.notice = services::EServiceNotice::acknowledged;
        modal.receive(threading::make_pod_message(81, reply));
        data.entered.store(1u, std::memory_order_release);
        data.passes.store(8u, std::memory_order_release);
        if (scenario == 4u) modal.stop();
        else
        {
            (void)step();
            if (mode == EMode::full)
            {
                for (unsigned turn = 0u; (turn < 150u) && (pods < 512u); ++turn) (void)step();
                TEST_EXPECT(ctx, ((pods == 512u) && !step() && (next_wake <= 1000000u) && !facade.owners));
                facade.owner_result = modules::ESubmission::published_wake_failed;
                for (unsigned turn = 0u; (turn < 20u) && (facade.owners < 32u); ++turn) (void)step();
                TEST_EXPECT(ctx, ((facade.owners == 32u) && !step() && (next_wake <= 1000000u) && !data.leave.load()));
                data.pressure.store(1u, std::memory_order_release);
                TEST_EXPECT(ctx, !step());
            }
            TEST_EXPECT(ctx, pump_tests::SModalExecutionTestAccess::expire_hold(modal));
        }
        facade.output.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!step() && !step() && !releases && !quiescences && (data.leave.load() == 1u)));
        TEST_EXPECT(ctx, (stop_requested.load() == ((mode == EMode::shutdown) ? 1u : 0u)));
        facade.output.result = modules::ESubmission::published_wake_failed;
        for (unsigned turn = 0u; turn < 3u; ++turn) (void)step();
        TEST_EXPECT(ctx, ((releases == 1u) && (quiescences == 1u) && !modal.domain_complete()));
        modal.receive(threading::make_pod_message(-1, services::ServiceBorrowControl{ { 17u }, { 1u }, { 31u },
            services::EServiceBorrowControl::retired, services::EServiceQuiescence::relinquished }));
        TEST_EXPECT(ctx, (!step() && !modal.domain_complete()));
        reply.notice = services::EServiceNotice::completed;
        modal.receive(threading::make_pod_message(81, reply));
        if (mode == EMode::full)
        {
            modal.stop();
            TEST_EXPECT(ctx, (!step() && !modal.domain_complete()));
            for (unsigned count = pods; count != 0u;)
            {
                --count;
                modal.receive(threading::make_pod_message((9000 + static_cast<std::int32_t>(count)), AssetReleaseResult{}));
            }
            for (unsigned count = facade.owners; count != 0u;)
            {
                --count;
                modal.receive(threading::make_pod_message((10000 + static_cast<std::int32_t>(count)),
                    filesystem::FilesystemRefreshResult{ filesystem::EFilesystemStatus::invalid_root }));
            }
        }
        TEST_EXPECT(ctx, (!step() && modal.domain_complete() && !modal.failed()));
    }
}
}   // namespace

int run_pump_modal_focused()
{
    tests::TTestContext ctx;
    pump_modal_custody(ctx);
    return ctx.report("PumpModal");
}

namespace
{
struct SSchemaFacade
{
    SPodCapture output;
    unsigned requests{}, attempts{};
    unsigned module_attempts{}, module_submissions{};
    modules::ESubmission module_result{ modules::ESubmission::busy };
    bool replacement{};
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        return SPodCapture::pod(&static_cast<SSchemaFacade*>(cookie)->output, destination, message);
    }
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SSchemaFacade& self = *static_cast<SSchemaFacade*>(cookie);
        if (const modules::ModuleRequest* const module = message.owner().payload<modules::ModuleRequest>())
        {
            ++self.module_attempts;
            self.output.valid &= (module->module == module_ids::executive) && (module->action == modules::EModuleAction::replace);
            if (modules::accepted(self.module_result))
            {
                ++self.module_submissions;
                message.take_owner().destroy();
            }
            return self.module_result;
        }
        ++self.attempts;
        const SchemaProcessRequest* const request = message.owner().payload<SchemaProcessRequest>();
        self.output.valid &= (destination.kind == modules::SDestination::EKind::host) && (message.query_async_slot() == 290) &&
            request && (request->request_id.value == 1u) && (request->contract_version == 1u) &&
            (request->input_count == (self.replacement ? 2u : 1u));
        if (modules::accepted(self.output.result))
        {
            ++self.requests;
            message.take_owner().destroy();
        }
        return self.output.result;
    }
};

void pump_schema_custody(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SSchemaFacade::pod, &SSchemaFacade::owner };
    const std::string handoff = test_environment::test_output_path("pump-schema-handoff.bin");
    for (unsigned scenario = 0u; scenario < 7u; ++scenario)
    {
        SSchemaFacade facade;
        facade.replacement = scenario == 6u;
        const CAssetId declarations{ 80u };
        if (facade.replacement)
        {
            TEST_EXPECT(ctx, platform::filesystem::saveFile(handoff.c_str(),
                CByteConstView{ reinterpret_cast<const std::uint8_t*>(&declarations), sizeof(declarations) }));
        }
        const modules::CCommunication communication{ functions, &facade };
        pump_tests::CSchemaExecution schema{ communication, facade.replacement, handoff.c_str() };
        TEST_EXPECT(ctx, schema.start());
        const auto step = [&]
        {
            bool runnable = false;
            std::uint64_t wake = UINT64_MAX;
            facade.output.count = 0u;
            schema.advance(runnable, wake);
            TEST_EXPECT(ctx, (facade.output.valid && (facade.output.count <= 1u)));
            return runnable;
        };
        TEST_EXPECT(ctx, (!step() && !step() && !facade.requests && !schema.domain_complete()));
        if (scenario == 1u)
        {
            schema.stop();
            TEST_EXPECT(ctx, (!step() && schema.domain_complete() && !schema.failed() && !facade.requests));
            continue;
        }
        facade.output.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!step() && (facade.requests == 1u)));
        const unsigned attempts = facade.attempts;
        TEST_EXPECT(ctx, (!step() && (facade.attempts == attempts)));
        if ((scenario == 2u) || facade.replacement)
        {
            schema.stop();
            TEST_EXPECT(ctx, (!step() && !facade.output.count && !schema.domain_complete()));
        }
        if (scenario == 4u)
        {
            SchemaProcessResult refusal;
            refusal.request_id = { 1u };
            refusal.status = ESchemaProcessStatus::busy;
            schema.receive(threading::make_pod_message(290, refusal));
            TEST_EXPECT(ctx, (!step() && schema.failed() && schema.domain_complete()));
            continue;
        }
        schema.receive(threading::make_pod_message(290, SchemaProcessAccepted{ { 1u } }));
        if ((scenario == 2u) || facade.replacement)
        {
            facade.output.result = modules::ESubmission::busy;
            TEST_EXPECT(ctx, (!step() && !step() && !facade.output.count));
            facade.output.result = modules::ESubmission::published_wake_failed;
            TEST_EXPECT(ctx, (!step() && (facade.output.count == 1u) && facade.output.posts[0].is_payload_a<SchemaProcessCancel>()));
            TEST_EXPECT(ctx, (!step() && !facade.output.count));
        }
        std::uint8_t bytes[16]{};
        SSchemaOutputV1 descriptor;
        descriptor.kind = schema::ESchemaOutput::schema_document;
        descriptor.bytes = CByteConstView{ bytes, sizeof(bytes) };
        const CAssetId output{ 81u };
        const CAssetAcquisitionId acquisition{ 82u };
        const SchemaProcessResult result{ { 1u }, descriptor.kind, { output, acquisition, &descriptor } };
        schema.receive(threading::make_pod_message(290, result));
        descriptor = {}; // No retained descriptor access during later retries.
        facade.output.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!step() && !step() && !schema.domain_complete() && !schema.ready() && !schema.failed()));
        facade.output.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!step() && (facade.output.count == 1u) && (facade.output.posts[0].query_async_slot() == 292)));
        TEST_EXPECT(ctx, (!step() && !facade.output.count && !schema.domain_complete()));
        schema.receive(threading::make_pod_message(292, AssetReleaseResult{ acquisition,
            (scenario == 5u) ? EAssetStatus::invalid_request : EAssetStatus::success }));
        if (scenario == 5u)
        {
            schema.stop();
            TEST_EXPECT(ctx, (!step() && schema.failed() && !schema.domain_complete() && !facade.output.count));
            continue; // Synthetic tokens; deliberately unsafe retention, no backing storage.
        }
        if ((scenario == 0u) || (scenario == 3u))
        {
            TEST_EXPECT(ctx, (!step() && schema.ready() && !schema.domain_complete() && !facade.output.count));
            const CByteBuffer copied = platform::filesystem::loadFile(handoff.c_str());
            TEST_EXPECT(ctx, ((copied.size() == sizeof(output)) && (std::memcmp(copied.data(), &output, sizeof(output)) == 0)));
            if (scenario == 0u)
            {
                schema.replacement_accepted();
                schema.stop();
                TEST_EXPECT(ctx, (!step() && schema.domain_complete() && !schema.failed() && !facade.output.count));
                continue;
            }
            schema.stop(); // Stop before accepted replacement cancels the handoff.
        }
        for (const CAssetId asset : { declarations, output })
        {
            if ((asset == declarations) && !facade.replacement) continue;
            facade.output.result = modules::ESubmission::busy;
            TEST_EXPECT(ctx, (!step() && !step() && !facade.output.count && !schema.domain_complete()));
            facade.output.result = modules::ESubmission::published_wake_failed;
            TEST_EXPECT(ctx, (!step() && (facade.output.count == 1u)));
            AssetDisposeRequest dispose;
            TEST_EXPECT(ctx, (facade.output.posts[0].copy_payload_to(dispose) && (dispose.asset == asset)));
            const std::int32_t slot = facade.output.posts[0].query_async_slot();
            TEST_EXPECT(ctx, (!step() && !facade.output.count && !schema.domain_complete()));
            schema.receive(threading::make_pod_message(slot, AssetDisposeResult{ asset, EAssetStatus::success }));
        }
        TEST_EXPECT(ctx, (!step() && schema.domain_complete() && !schema.failed()));
    }
    // Exercise the actual Executive gap between schema.ready()/next(self_stop)
    // and construction, as well as an already-constructed transport-busy owner.
    for (const bool prepared : { false, true })
    {
        SSchemaFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        pump_tests::CExecutiveExecution execution{ context, "schema-process", false, handoff.c_str() };
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event, const threading::CErasedPodMsg* const message = nullptr)
        {
            facade.output.count = 0u;
            invocation.event = event;
            invocation.message = message;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, facade.output.valid);
        };
        invoke(modules::EExecutionEvent::start);
        facade.output.result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, (facade.requests == 1u));
        threading::CErasedPodMsg message = threading::make_pod_message(290, SchemaProcessAccepted{ { 1u } });
        invoke(modules::EExecutionEvent::message, &message);
        std::uint8_t bytes[16]{};
        SSchemaOutputV1 descriptor;
        descriptor.kind = schema::ESchemaOutput::schema_document;
        descriptor.bytes = CByteConstView{ bytes, sizeof(bytes) };
        const CAssetId output{ 81u };
        const CAssetAcquisitionId acquisition{ 82u };
        message = threading::make_pod_message(290, SchemaProcessResult{ { 1u }, descriptor.kind, { output, acquisition, &descriptor } });
        invoke(modules::EExecutionEvent::message, &message);
        TEST_EXPECT(ctx, ((facade.output.count == 1u) && (facade.output.posts[0].query_async_slot() == 292)));
        descriptor = {};
        message = threading::make_pod_message(292, AssetReleaseResult{ acquisition, EAssetStatus::success });
        invoke(modules::EExecutionEvent::message, &message);
        TEST_EXPECT(ctx, (!facade.module_attempts && !progress.work_complete));
        if (prepared) invoke(modules::EExecutionEvent::service_async);
        const unsigned attempts = prepared ? 1u : 0u;
        TEST_EXPECT(ctx, ((facade.module_attempts == attempts) && !facade.module_submissions));
        facade.output.result = modules::ESubmission::busy;
        // If the bad path still publishes, acceptance would erase the live
        // disposal obligation. This catches more than an ordinary busy return.
        facade.module_result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::stop);
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_complete && !progress.work_pending && !facade.output.count &&
                (facade.module_attempts == attempts) && !facade.module_submissions));
        }
        facade.output.result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        AssetDisposeRequest dispose;
        TEST_EXPECT(ctx, ((facade.output.count == 1u) && facade.output.posts[0].copy_payload_to(dispose) &&
            (dispose.asset == output) && (facade.output.posts[0].query_async_slot() == 293) && !progress.work_complete));
        const unsigned posts = facade.output.pod_attempts;
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_complete && !facade.output.count && (facade.output.pod_attempts == posts)));
        }
        message = threading::make_pod_message(293, AssetDisposeResult{ output, EAssetStatus::success });
        invoke(modules::EExecutionEvent::message, &message);
        TEST_EXPECT(ctx, (progress.work_complete && execution.complete() && !progress.work_pending &&
            (facade.module_attempts == attempts) && !facade.module_submissions && (facade.output.pod_attempts == posts)));
    }
}
}   // namespace

namespace
{
struct SPlatformShellFacade
{
    SLifecycleExecutiveFacade modules;
    threading::CErasedPodMsg last;
    unsigned pod_attempts{}, pods{};
    unsigned pod_allowance{ UINT32_MAX };
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SPlatformShellFacade& self = *static_cast<SPlatformShellFacade*>(cookie);
        return SLifecycleExecutiveFacade::owner(&self.modules, destination, std::move(message));
    }
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SPlatformShellFacade& self = *static_cast<SPlatformShellFacade*>(cookie);
        self.modules.valid &= destination.kind == modules::SDestination::EKind::host;
        ++self.pod_attempts;
        if (!self.pod_allowance) return modules::ESubmission::busy;
        if (modules::accepted(self.modules.result))
        {
            --self.pod_allowance;
            ++self.pods;
            self.last = message;
        }
        return self.modules.result;
    }
};

void pump_platform_shell(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SPlatformShellFacade::pod, &SPlatformShellFacade::owner };
    SPlatformShellFacade facade;
    const modules::CCommunication communication{ functions, &facade };
    modules::SExecutionContext context;
    context.communication = &communication;
    pump_tests::CExecutiveExecution execution{ context, "platform-shell", false };
    modules::SInvocation invocation;
    modules::SExecutionProgress progress;
    const auto invoke = [&](const modules::EExecutionEvent event, const threading::CErasedPodMsg* const message = nullptr)
    {
        invocation.event = event;
        invocation.message = message;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        TEST_EXPECT(ctx, facade.modules.valid);
    };
    invoke(modules::EExecutionEvent::start);
    // Three loads, open, two unsupported commands, busy unload, another
    // unsupported command, close, successful unload, then Executive self-stop.
    for (unsigned step = 0u; step < 11u; ++step)
    {
        facade.modules.result = modules::ESubmission::busy;
        const unsigned accepted = facade.modules.accepted + facade.pods;
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete &&
                ((facade.modules.accepted + facade.pods) == accepted)));
        }
        facade.modules.result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.modules.accepted + facade.pods) == (accepted + 1u)));
        const unsigned attempts = facade.modules.attempts + facade.pod_attempts;
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (((facade.modules.attempts + facade.pod_attempts) == attempts) && !progress.work_complete));
        }
        threading::CErasedPodMsg reply;
        if ((step < 3u) || (step == 6u) || (step >= 9u))
        {
            const module_ids::id_type module = (step == 10u) ? module_ids::executive : module_ids::platform;
            const modules::EModuleAction action = (step < 3u) ? modules::EModuleAction::load : modules::EModuleAction::unload;
            TEST_EXPECT(ctx, ((facade.modules.module == module) && (facade.modules.action == action) &&
                (facade.modules.correlation == 10)));
            if (step == 10u)
            {
                invoke(modules::EExecutionEvent::stop);
                TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && !progress.work_pending));
                break;
            }
            modules::ModuleResult result;
            result.module = module;
            result.notice = modules::EModuleNotice::acknowledged;
            result.status = modules::EModuleStatus::success;
            reply = threading::make_pod_message(10, result);
            invoke(modules::EExecutionEvent::message, &reply);
            result.notice = modules::EModuleNotice::completed;
            result.status = ((step == 1u) || (step == 2u)) ? modules::EModuleStatus::already_loaded :
                ((step == 6u) ? modules::EModuleStatus::busy : modules::EModuleStatus::success);
            reply = threading::make_pod_message(10, result);
        }
        else if (step == 3u)
        {
            services::OpenServiceRequest request;
            TEST_EXPECT(ctx, (facade.last.copy_payload_to(request) && (request.module == module_ids::platform) &&
                (request.contract == 1u) && (request.version == 1u) && (request.request.value == 1u)));
            reply = threading::make_pod_message(facade.last.query_async_slot(),
                services::OpenServiceResult{ request.request, { 17u }, { 18u }, services::EServiceStatus::success });
        }
        else
        {
            services::ServiceReply result;
            result.route = { 17u };
            if (step == 8u)
            {
                services::CloseServiceRequest request;
                TEST_EXPECT(ctx, (facade.last.copy_payload_to(request) && (request.route.value == 17u) &&
                    (request.request.value == 21u)));
                result.request = request.request;
                result.status = services::EServiceStatus::success;
            }
            else
            {
                services::ServiceRequest request;
                TEST_EXPECT(ctx, facade.last.copy_payload_to(request));
                const std::uint64_t expected = (step == 4u) ? 18u : ((step == 5u) ? 19u : 20u);
                std::uint16_t opcode = 65535u;
#if !MV_PLATFORM_WINDOWS
                if (step == 7u) opcode = 16u;
#endif
                TEST_EXPECT(ctx, ((request.route.value == 17u) && (request.request.value == expected) &&
                    (request.opcode == opcode)));
                result.request = request.request;
                result.opcode = request.opcode;
                result.notice = services::EServiceNotice::acknowledged;
                result.status = services::EServiceStatus::success;
                reply = threading::make_pod_message(facade.last.query_async_slot(), result);
                invoke(modules::EExecutionEvent::message, &reply);
                TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete &&
                    ((facade.modules.attempts + facade.pod_attempts) == attempts)));
                result.status = services::EServiceStatus::unsupported;
            }
            result.notice = services::EServiceNotice::completed;
            reply = threading::make_pod_message(facade.last.query_async_slot(), result);
        }
        invoke(modules::EExecutionEvent::message, &reply);
        TEST_EXPECT(ctx, (progress.work_pending && !progress.work_complete));
    }
    TEST_EXPECT(ctx, ((facade.modules.accepted == 6u) && (facade.pods == 5u)));
}
}

namespace
{
void pump_report_snapshots(tests::TTestContext& ctx)
{
#if MV_PLATFORM_WINDOWS
    using namespace platform::reports;
    static const modules::SCommunicationV1 functions{ &SPlatformShellFacade::pod, &SPlatformShellFacade::owner };
    const std::string prefix = test_environment::test_output_path("pump-report-snapshot-");
    for (const bool stopped : { false, true })
    {
        SPlatformShellFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        pump_tests::CReportExecution execution{ communication, false, prefix.c_str() };
        TEST_EXPECT(ctx, execution.start(pump_tests::CReportExecution::EMode::snapshot));
        const auto step = [&]
        {
            bool runnable = false;
            std::uint64_t wake = UINT64_MAX;
            const unsigned attempts = facade.modules.attempts + facade.pod_attempts;
            execution.advance(runnable, wake);
            TEST_EXPECT(ctx, (!execution.failed() && facade.modules.valid &&
                ((facade.modules.attempts + facade.pod_attempts) <= (attempts + 1u))));
            return runnable;
        };
        const auto post = [&]
        {
            facade.modules.result = modules::ESubmission::busy;
            const unsigned count = facade.modules.accepted + facade.pods;
            for (unsigned turn = 0u; turn < 3u; ++turn) (void)step();
            TEST_EXPECT(ctx, ((facade.modules.accepted + facade.pods) == count));
            facade.modules.result = modules::ESubmission::published_wake_failed;
            (void)step();
            TEST_EXPECT(ctx, ((facade.modules.accepted + facade.pods) == (count + 1u)));
            facade.modules.result = modules::ESubmission::busy;
        };
        const auto deliver = [&](const std::int32_t slot, const auto& payload)
        {
            execution.receive(threading::make_pod_message(slot, payload));
            (void)step();
        };
        const auto module_result = [&]
        {
            TEST_EXPECT(ctx, (facade.modules.module == module_ids::platform));
            modules::ModuleResult result;
            result.module = module_ids::platform;
            result.notice = modules::EModuleNotice::acknowledged;
            result.status = modules::EModuleStatus::success;
            deliver(10, result);
            result.notice = modules::EModuleNotice::completed;
            deliver(10, result);
        };
        post();
        deliver(264, PlatformReportResult{});
        post();
        module_result();
        std::uint8_t bytes[3][8]{ { 1u }, { 2u }, { 3u } };
        for (unsigned index = 0u; index < 3u; ++index)
        {
            post();
            TEST_EXPECT(ctx, ((index == 1u) ? facade.last.is_payload_a<PlatformReportUpdateRequest>() :
                facade.last.is_payload_a<PlatformReportRequest>()));
            PlatformReportResult result;
            result.status = EStatus::success;
            result.availability = EAvailability::available;
            result.last_update = ELastUpdate::succeeded;
            result.acquisition = CAssetAcquisitionId{ 100u + index };
            result.bytes = CByteConstView{ bytes[index], sizeof(bytes[index]) };
            deliver(264, result);
            if (index == 1u)
            {
                post();
                TEST_EXPECT(ctx, (facade.modules.action == modules::EModuleAction::unload));
                module_result();
                post();
                deliver(264, PlatformReportResult{});
                post();
                TEST_EXPECT(ctx, (facade.modules.action == modules::EModuleAction::load));
                module_result();
            }
        }
        for (unsigned index = 0u; index < 2u; ++index)
        {
            const std::string path = prefix + (index ? "historical-refresh.bsd" : "historical.bsd");
            const CByteBuffer saved = platform::filesystem::loadFile(path.c_str());
            TEST_EXPECT(ctx, ((saved.size() == sizeof(bytes[index])) &&
                (std::memcmp(saved.data(), bytes[index], sizeof(bytes[index])) == 0)));
        }
        if (stopped) execution.stop();
        const unsigned modules_before = facade.modules.accepted;
        for (unsigned index = 0u; index < 3u; ++index)
        {
            post();
            AssetReleaseRequest release;
            TEST_EXPECT(ctx, (facade.last.copy_payload_to(release) && (release.acquisition == CAssetAcquisitionId{ 100u + index })));
            const unsigned attempts = facade.pod_attempts;
            for (unsigned turn = 0u; turn < 3u; ++turn)
            {
                TEST_EXPECT(ctx, (!step() && !execution.domain_complete() && (facade.pod_attempts == attempts)));
            }
            bytes[index][0] = 255u; // A released view may no longer be inspected.
            deliver(265, AssetReleaseResult{ release.acquisition, EAssetStatus::success });
            TEST_EXPECT(ctx, (execution.domain_complete() == (stopped && (index == 2u))));
        }
        if (!stopped)
        {
            post();
            module_result();
            post();
            TEST_EXPECT(ctx, ((facade.modules.module == module_ids::executive) && !execution.domain_complete()));
            execution.stop();
        }
        else TEST_EXPECT(ctx, (facade.modules.accepted == modules_before));
        TEST_EXPECT(ctx, (execution.domain_complete() && !step()));
    }
#else
    (void)ctx; // Actual unsupported-backend behavior is qualified on its platform.
#endif
}

void pump_report_custody(tests::TTestContext& ctx)
{
    using namespace platform::reports;
    static const modules::SCommunicationV1 functions{ &SPlatformShellFacade::pod, &SPlatformShellFacade::owner };
    const std::string prefix = test_environment::test_output_path("pump-report-");
    // Actual Executive integration: stop before report submission, unread result
    // cancellation, known acquisition release, and unaccepted replacement stop.
    for (unsigned scenario = 0u; scenario < 7u; ++scenario)
    {
        SPlatformShellFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        const bool departure = scenario >= 3u;
        const char* const selected = departure ? "platform-report-departure" : "platform-report-shutdown";
        pump_tests::CExecutiveExecution execution{ context, selected, false, "unused", prefix.c_str() };
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        bool failed = false;
        const auto invoke = [&](const modules::EExecutionEvent event, const threading::CErasedPodMsg* const message = nullptr)
        {
            invocation.event = event;
            invocation.message = message;
            const modules::EExecutionStatus status = execution.handle(invocation, progress);
            if (status != modules::EExecutionStatus::success)
            {
                TEST_EXPECT(ctx, ((scenario == 6u) && !failed));
                failed = true;
            }
            TEST_EXPECT(ctx, facade.modules.valid);
        };
        const auto post = [&]
        {
            facade.modules.result = modules::ESubmission::busy;
            const unsigned count = facade.modules.accepted + facade.pods;
            for (unsigned turn = 0u; turn < 3u; ++turn) invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (((facade.modules.accepted + facade.pods) == count) && !progress.work_pending));
            facade.modules.result = modules::ESubmission::published_wake_failed;
            for (unsigned turn = 0u; (turn < 4u) && ((facade.modules.accepted + facade.pods) == count); ++turn)
            {
                invoke(modules::EExecutionEvent::service_async);
            }
            TEST_EXPECT(ctx, ((facade.modules.accepted + facade.pods) == (count + 1u)));
            facade.modules.result = modules::ESubmission::busy;
        };
        const auto deliver = [&](const std::int32_t slot, const auto& payload)
        {
            facade.modules.result = modules::ESubmission::busy;
            const threading::CErasedPodMsg message = threading::make_pod_message(slot, payload);
            invoke(modules::EExecutionEvent::message, &message);
        };
        invoke(modules::EExecutionEvent::start);
        if (scenario == 0u)
        {
            invoke(modules::EExecutionEvent::stop);
            TEST_EXPECT(ctx, (progress.work_complete && !facade.modules.accepted && !facade.pods));
            continue;
        }
        post();
        TEST_EXPECT(ctx, ((facade.modules.module == module_ids::platform) && (facade.modules.action == modules::EModuleAction::load)));
        modules::ModuleResult loaded;
        loaded.module = module_ids::platform;
        loaded.notice = modules::EModuleNotice::acknowledged;
        loaded.status = modules::EModuleStatus::success;
        deliver(10, loaded);
        loaded.notice = modules::EModuleNotice::completed;
        deliver(10, loaded);
        post();
        TEST_EXPECT(ctx, (facade.last.is_payload_a<PlatformReportRequest>() && (facade.last.query_async_slot() == 264)));
        if (scenario == 1u)
        {
            invoke(modules::EExecutionEvent::stop);
            TEST_EXPECT(ctx, progress.work_complete);
            std::uint8_t bytes[8]{};
            PlatformReportResult late;
            late.status = EStatus::success;
            late.acquisition = CAssetAcquisitionId{ 91u };
            late.bytes = CByteConstView{ bytes, sizeof(bytes) };
            deliver(264, late); // Unread acquisition remains Host-owned until quiescence.
            TEST_EXPECT(ctx, (progress.work_complete && !progress.work_pending && (facade.pods == 1u)));
            continue;
        }
        if (scenario == 2u)
        {
            PlatformReportResult pending;
            pending.status = EStatus::not_ready;
            pending.availability = EAvailability::not_ready;
            pending.update_in_progress = true;
            deliver(264, pending);
            post();
            TEST_EXPECT(ctx, ((facade.modules.module == module_ids::executive) &&
                (facade.modules.action == modules::EModuleAction::unload) && !progress.work_complete));
            invoke(modules::EExecutionEvent::stop);
            TEST_EXPECT(ctx, (progress.work_complete && !progress.work_pending));
            continue;
        }
        std::uint8_t bytes[8]{ 17u };
        PlatformReportResult current;
        current.status = EStatus::success;
        current.availability = EAvailability::available;
        current.last_update = ELastUpdate::succeeded;
        current.acquisition = CAssetAcquisitionId{ 92u };
        current.bytes = CByteConstView{ bytes, sizeof(bytes) };
        current.update_in_progress = scenario == 5u;
        deliver(264, current);
        post();
        AssetReleaseRequest release;
        TEST_EXPECT(ctx, (facade.last.copy_payload_to(release) && (release.acquisition == current.acquisition)));
        if ((scenario == 4u) || (scenario == 6u)) invoke(modules::EExecutionEvent::stop);
        const unsigned attempts = facade.pod_attempts;
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_complete && !progress.work_pending && (facade.pod_attempts == attempts)));
        }
        deliver(265, AssetReleaseResult{ release.acquisition,
            (scenario == 6u) ? EAssetStatus::invalid_request : EAssetStatus::success });
        if ((scenario == 4u) || (scenario == 6u))
        {
            TEST_EXPECT(ctx, (progress.work_complete == (scenario == 4u)));
            for (unsigned turn = 0u; turn < 3u; ++turn) invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, ((facade.pod_attempts == attempts) && !progress.work_pending && (failed == (scenario == 6u))));
            continue;
        }
        if (scenario == 5u)
        {
            TEST_EXPECT(ctx, pump_tests::SExecutiveExecutionTestAccess::expire_report_poll(execution));
            post();
            TEST_EXPECT(ctx, facade.last.is_payload_a<PlatformReportRequest>());
            current.update_in_progress = false;
            current.acquisition = CAssetAcquisitionId{ 93u };
            deliver(264, current);
            post();
            TEST_EXPECT(ctx, (facade.last.copy_payload_to(release) && (release.acquisition == current.acquisition)));
            deliver(265, AssetReleaseResult{ release.acquisition, EAssetStatus::success });
        }
        post();
        TEST_EXPECT(ctx, (facade.last.is_payload_a<PlatformReportUpdateRequest>() && (facade.last.query_async_slot() == 267)));
        // Stop in the empty self-stop stage or after its busy owner exists.
        if (scenario == 5u) invoke(modules::EExecutionEvent::service_async);
        const unsigned module_attempts = facade.modules.attempts;
        facade.modules.result = modules::ESubmission::published_wake_failed;
        invoke(modules::EExecutionEvent::stop);
        TEST_EXPECT(ctx, (progress.work_complete && !progress.work_pending && (facade.modules.accepted == 1u)));
        current.acquisition = CAssetAcquisitionId{ 94u };
        deliver(267, current);
        for (unsigned turn = 0u; turn < 3u; ++turn) invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, (progress.work_complete && (facade.modules.attempts == module_attempts) && !failed));
    }
    pump_report_snapshots(ctx);
}
}

namespace
{
void pump_report_injected(tests::TTestContext& ctx)
{
    using namespace platform::reports;
    static const modules::SCommunicationV1 functions{ &SPlatformShellFacade::pod, &SPlatformShellFacade::owner };
    for (unsigned scenario = 0u; scenario < 4u; ++scenario)
    {
        SPlatformShellFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        pump_tests::CReportExecution execution{ communication, false };
        TEST_EXPECT(ctx, execution.start(pump_tests::CReportExecution::EMode::injected));
        const auto step = [&]
        {
            bool runnable = false;
            std::uint64_t wake = UINT64_MAX;
            const unsigned attempts = facade.modules.attempts + facade.pod_attempts;
            execution.advance(runnable, wake);
            TEST_EXPECT(ctx, (facade.modules.valid && ((facade.modules.attempts + facade.pod_attempts) <= (attempts + 3u))));
            return runnable;
        };
        const auto post = [&]
        {
            facade.modules.result = modules::ESubmission::busy;
            const unsigned count = facade.modules.accepted + facade.pods;
            for (unsigned turn = 0u; turn < 3u; ++turn) (void)step();
            TEST_EXPECT(ctx, ((facade.modules.accepted + facade.pods) == count));
            facade.modules.result = modules::ESubmission::published_wake_failed;
            (void)step();
            TEST_EXPECT(ctx, ((facade.modules.accepted + facade.pods) == (count + 1u)));
            facade.modules.result = modules::ESubmission::busy;
        };
        const auto deliver = [&](const std::int32_t slot, const auto& payload)
        {
            execution.receive(threading::make_pod_message(slot, payload));
            (void)step();
        };
        const auto module_result = [&]
        {
            modules::ModuleResult result;
            result.module = module_ids::platform;
            result.notice = modules::EModuleNotice::acknowledged;
            result.status = modules::EModuleStatus::success;
            deliver(10, result);
            result.notice = modules::EModuleNotice::completed;
            deliver(10, result);
        };
        const auto release_result = [&](const std::uint64_t token)
        {
            post();
            AssetReleaseRequest request;
            TEST_EXPECT(ctx, (facade.last.copy_payload_to(request) && (request.acquisition == CAssetAcquisitionId{ token })));
            const unsigned count = facade.pod_attempts;
            for (unsigned turn = 0u; turn < 3u; ++turn)
            {
                TEST_EXPECT(ctx, (!step() && !execution.domain_complete() && (facade.pod_attempts == count)));
            }
            deliver(265, AssetReleaseResult{ request.acquisition, EAssetStatus::success });
        };
        std::uint8_t initial[8]{ 17u }, updated[8]{ 18u }, reloaded[8]{ 19u };
        PlatformReportResult current;
        current.status = EStatus::success;
        current.availability = EAvailability::available;
        current.last_update = ELastUpdate::succeeded;
        current.acquisition = CAssetAcquisitionId{ 100u };
        current.bytes = CByteConstView{ initial, sizeof(initial) };
        post();
        module_result();
        post();
        deliver(264, current);
        for (unsigned failure = 0u; failure < 2u; ++failure)
        {
            post();
            TEST_EXPECT(ctx, (facade.last.is_payload_a<PlatformReportCaptureFailed>() && (facade.last.query_async_slot() == -1)));
            post();
            TEST_EXPECT(ctx, facade.last.is_payload_a<PlatformReportUpdateRequest>());
            PlatformReportResult refused;
            refused.status = EStatus::failed;
            refused.availability = EAvailability::available;
            refused.last_update = ELastUpdate::failed;
            refused.failure.stage = failure ? EStage::validation : EStage::capture;
            deliver(264, refused);
            post();
            TEST_EXPECT(ctx, facade.last.is_payload_a<PlatformReportRequest>());
            current.acquisition = CAssetAcquisitionId{ 110u + failure };
            deliver(264, current);
            release_result(110u + failure);
        }
        facade.modules.result = modules::ESubmission::published_wake_failed;
        for (unsigned index = 0u; index < 3u; ++index)
        {
            facade.pod_allowance = 1u;
            const unsigned count = facade.pods;
            TEST_EXPECT(ctx, !step());
            TEST_EXPECT(ctx, ((facade.pods == (count + 1u)) && (facade.last.query_async_slot() == (267 + static_cast<std::int32_t>(index)))));
            TEST_EXPECT(ctx, ((index == 2u) ? facade.last.is_payload_a<PlatformReportRequest>() :
                facade.last.is_payload_a<PlatformReportUpdateRequest>()));
            for (unsigned turn = 0u; turn < 3u; ++turn) TEST_EXPECT(ctx, (!step() && (facade.pods == (count + 1u))));
        }
        facade.pod_allowance = UINT32_MAX;
        facade.modules.result = modules::ESubmission::busy;
        PlatformReportResult update = current;
        update.acquisition = CAssetAcquisitionId{ 120u };
        update.bytes = CByteConstView{ updated, sizeof(updated) };
        current.acquisition = CAssetAcquisitionId{ 112u };
        current.update_in_progress = true;
        PlatformReportResult pending;
        pending.status = EStatus::update_in_progress;
        pending.availability = EAvailability::available;
        pending.update_in_progress = true;
        if (scenario >= 2u) deliver(267, update);
        if (scenario == 3u)
        {
            deliver(267, update); // Duplicate successful custody is never re-adopted.
            execution.stop();
            TEST_EXPECT(ctx, (execution.failed() && !execution.domain_complete() && !step()));
            continue; // Synthetic identities: deliberately unsafe retention.
        }
        if (scenario) execution.stop();
        deliver(269, current);
        deliver(268, pending);
        if (scenario < 2u) deliver(267, update);
        if (!scenario)
        {
            release_result(112u);
            post();
            module_result();
            post();
            deliver(264, PlatformReportResult{});
            post();
            module_result();
            post();
            current.acquisition = CAssetAcquisitionId{ 130u };
            current.bytes = CByteConstView{ reloaded, sizeof(reloaded) };
            current.update_in_progress = false;
            deliver(264, current);
        }
        release_result(100u);
        if (scenario != 1u) release_result(120u);
        if (!scenario)
        {
            release_result(130u);
            post();
            module_result();
            post();
            execution.stop();
        }
        TEST_EXPECT(ctx, (execution.domain_complete() && !execution.failed() && !step()));
    }
}
}

namespace
{
struct SNativeConsumerFixture
{
    SPodCapture output;
    static inline const modules::SCommunicationV1 functions{ &SPodCapture::pod };
    const modules::CCommunication communication{ functions, &output };
    modules::SHostExecutionContext host_execution;
    pump_tests::CNativeExecution execution{ communication, &host_execution };
    platform::windows::CWindowPublication publication;
    platform::windows::SWindowViewV1 view;
    threading::CNotification notification;
    std::atomic<std::uint32_t> pending{};
    unsigned requests{}, registrations{}, readiness{}, releases{}, quiescences{};
    bool valid{ true };
    SNativeConsumerFixture()
    {
        view.provider = { 18u };
        view.window = { 19u };
        view.native_window = view.native_instance = 1u; // Controlled invalid OS identities; no native window is created.
        view.state = &publication;
    }
    bool step()
    {
        bool runnable = false;
        std::uint64_t wake = UINT64_MAX;
        output.count = 0u;
        execution.advance(runnable, wake);
        valid &= output.count <= 2u;
        for (unsigned index = 0u; index < output.count; ++index)
        {
            const threading::CErasedPodMsg& message = output.posts[index];
            services::ServiceRequest request;
            if (message.copy_payload_to(request))
            {
                valid &= request.route.value == 17u;
                if (request.request.value == 1u)
                {
                    valid &= (message.query_async_slot() == 30) && (request.opcode == 16u);
                    ++requests;
                }
                else
                {
                    valid &= (request.request.value == 2u) && (message.query_async_slot() == 32) && (request.opcode == 22u);
                    ++readiness;
                }
            }
            else if (message.is_payload_a<services::RegisterServiceNotification>()) ++registrations;
            else if (message.is_payload_a<services::ReleaseServiceBorrow>()) ++releases;
            else if (message.is_payload_a<services::ServiceBorrowQuiescent>()) ++quiescences;
            else valid = false;
        }
        return runnable;
    }
    void reply(const bool ready, const services::EServiceNotice notice,
        const services::EServiceStatus status = services::EServiceStatus::success)
    {
        services::ServiceReply result;
        result.route = { 17u };
        result.request = { ready ? 2u : 1u };
        result.opcode = ready ? 22u : 16u;
        result.notice = notice;
        result.status = status;
        if (notice == services::EServiceNotice::prepared)
        {
            const services::SPreparedServiceBorrow prepared{ { 20u }, { &view, sizeof(view), 1u } };
            result.payload_size = sizeof(prepared);
            std::memcpy(result.payload, &prepared, sizeof(prepared));
        }
        execution.receive(threading::make_pod_message((ready ? 32 : 30), result));
    }
    void registration(const services::EServiceStatus status = services::EServiceStatus::success)
    {
        services::ServiceNotificationRegistered result;
        result.route = { 17u };
        result.borrow = { 20u };
        result.registration = { 21u };
        result.reader = notification.reader();
        result.status = status;
        execution.receive(threading::make_pod_message(31, result));
    }
    void retired()
    {
        execution.receive(threading::make_pod_message(-1, services::ServiceBorrowControl{
            { 17u }, { 1u }, { 20u }, services::EServiceBorrowControl::retired, services::EServiceQuiescence::relinquished }));
    }
};

void pump_native_custody(tests::TTestContext& ctx)
{
#if MV_PLATFORM_WINDOWS
    for (unsigned boundary = 0u; boundary < 6u; ++boundary)
    {
        SNativeConsumerFixture fixture;
        TEST_EXPECT(ctx, (fixture.notification.initialise(fixture.pending) &&
            fixture.execution.start(services::OpenServiceResult{ { 1u }, { 17u }, { 18u }, services::EServiceStatus::success },
                pump_tests::CNativeExecution::EMode::move)));
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.step() && !fixture.requests));
        if (!boundary)
        {
            fixture.execution.stop();
            TEST_EXPECT(ctx, (!fixture.step() && fixture.execution.domain_complete() && !fixture.requests));
            continue;
        }
        fixture.output.result = modules::ESubmission::published_wake_failed;
        TEST_EXPECT(ctx, (!fixture.step() && (fixture.requests == 1u)));
        fixture.reply(false, services::EServiceNotice::acknowledged);
        fixture.reply(false, services::EServiceNotice::prepared);
        fixture.output.result = (boundary == 1u) ? modules::ESubmission::busy : modules::ESubmission::published_wake_failed;
        (void)fixture.step();
        if (boundary >= 3u)
        {
            fixture.registration((boundary == 5u) ? services::EServiceStatus::closing : services::EServiceStatus::success);
            fixture.output.result = (boundary == 3u) ? modules::ESubmission::busy : modules::ESubmission::published_wake_failed;
            if (boundary != 5u) (void)fixture.step();
        }
        fixture.execution.stop();
        fixture.output.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!fixture.step() && !fixture.step() && !fixture.execution.domain_complete()));
        fixture.output.result = modules::ESubmission::published_wake_failed;
        for (unsigned turn = 0u; turn < 3u; ++turn) (void)fixture.step();
        TEST_EXPECT(ctx, ((fixture.releases == 1u) && (fixture.quiescences == 1u) &&
            (fixture.readiness == ((boundary == 4u) ? 1u : 0u))));
        fixture.view = {}; // No descriptor read after local closure/quiescence.
        fixture.reply(false, services::EServiceNotice::completed, services::EServiceStatus::cancelled);
        fixture.retired();
        if ((boundary == 2u) || (boundary == 4u))
        {
            TEST_EXPECT(ctx, (!fixture.step() && !fixture.execution.domain_complete()));
        }
        if (boundary == 2u) fixture.registration();
        if (boundary == 4u)
        {
            fixture.reply(true, services::EServiceNotice::acknowledged);
            fixture.reply(true, services::EServiceNotice::completed, services::EServiceStatus::closing);
        }
        TEST_EXPECT(ctx, (!fixture.step() && fixture.execution.done() && fixture.execution.domain_complete() &&
            (fixture.execution.failed() == (boundary == 5u)) && fixture.valid && fixture.output.valid));
    }
    // The live-report helper owns two independent acquisitions while the native
    // context supplies OS sampling. No native window is involved in this proof.
    static const modules::SCommunicationV1 functions{ &SPlatformShellFacade::pod, &SPlatformShellFacade::owner };
    const std::string prefix = test_environment::test_output_path("pump-live-");
    SPlatformShellFacade facade;
    const modules::CCommunication communication{ functions, &facade };
    pump_tests::CReportExecution report{ communication, false, prefix.c_str() };
    TEST_EXPECT(ctx, report.start(pump_tests::CReportExecution::EMode::live));
    const auto step = [&]
    {
        bool runnable = false;
        std::uint64_t wake = UINT64_MAX;
        report.advance(runnable, wake);
        TEST_EXPECT(ctx, (!report.failed() && facade.modules.valid));
        return runnable;
    };
    std::uint8_t bytes[8]{ 17u };
    for (unsigned index = 0u; index < 2u; ++index)
    {
        facade.modules.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!step() && !step() && !report.live_ready()));
        facade.modules.result = modules::ESubmission::published_wake_failed;
        (void)step();
        TEST_EXPECT(ctx, (report.live_update_pending() == (index == 1u)));
        platform::reports::PlatformReportResult result;
        result.status = platform::reports::EStatus::success;
        result.acquisition = CAssetAcquisitionId{ 31u + index };
        result.bytes = CByteConstView{ bytes, sizeof(bytes) };
        report.receive(threading::make_pod_message(264, result));
        facade.modules.result = modules::ESubmission::busy;
        (void)step();
    }
    for (unsigned index = 0u; index < 2u; ++index)
    {
        facade.modules.result = modules::ESubmission::busy;
        TEST_EXPECT(ctx, (!step() && !step() && !report.domain_complete()));
        facade.modules.result = modules::ESubmission::published_wake_failed;
        (void)step();
        AssetReleaseRequest release;
        TEST_EXPECT(ctx, (facade.last.copy_payload_to(release) && (release.acquisition == CAssetAcquisitionId{ 31u + index })));
        TEST_EXPECT(ctx, (!step() && !report.live_ready()));
        report.receive(threading::make_pod_message(265, AssetReleaseResult{ release.acquisition, EAssetStatus::success }));
        facade.modules.result = modules::ESubmission::busy;
    }
    (void)step();
    TEST_EXPECT(ctx, (report.live_ready() && report.domain_complete() && !facade.modules.accepted && (facade.pods == 4u)));
#else
    (void)ctx;
#endif
}
}

int run_pump_native_focused()
{
    tests::TTestContext ctx;
    pump_native_custody(ctx);
    return ctx.report("PumpNativeCustody");
}

int run_pump_report_injected_focused()
{
    tests::TTestContext ctx;
    pump_report_injected(ctx);
    return ctx.report("PumpReportInjected");
}

int run_pump_report_focused()
{
    tests::TTestContext ctx;
    pump_report_custody(ctx);
    return ctx.report("PumpReport");
}

int run_pump_platform_shell_focused()
{
    tests::TTestContext ctx;
    pump_platform_shell(ctx);
    return ctx.report("PumpPlatformShell");
}

int run_pump_schema_focused()
{
    tests::TTestContext ctx;
    pump_schema_custody(ctx);
    return ctx.report("PumpSchema");
}

int run_pump_credits_focused()
{
    tests::TTestContext ctx;
    pump_credit_custody(ctx);
    return ctx.report("PumpCredits");
}

int run_pump_executive_focused()
{
    tests::TTestContext ctx;
    static const modules::SCommunicationV1 functions{ nullptr, &SLifecycleExecutiveFacade::owner };
    for (const modules::ESubmission accepted : { modules::ESubmission::accepted, modules::ESubmission::published_wake_failed })
    {
        SLifecycleExecutiveFacade facade;
        const modules::CCommunication communication{ functions, &facade };
        modules::SExecutionContext context;
        context.communication = &communication;
        pump_tests::CExecutiveExecution execution{ context, "bad-callback", false };
        modules::SInvocation invocation;
        modules::SExecutionProgress progress;
        const auto invoke = [&](const modules::EExecutionEvent event)
        {
            invocation.event = event;
            TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
        };
        invoke(modules::EExecutionEvent::start);
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete && (facade.accepted == 0u)));
        }
        facade.result = accepted;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.accepted == 1u) && (facade.correlation == 10) &&
            (facade.module == module_ids::rendering) && (facade.action == modules::EModuleAction::load)));
        const unsigned initial_attempts = facade.attempts;
        for (unsigned turn = 0u; turn < 3u; ++turn) invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, (facade.attempts == initial_attempts));
        modules::ModuleResult result;
        result.module = module_ids::rendering;
        result.notice = modules::EModuleNotice::acknowledged;
        result.status = modules::EModuleStatus::success;
        threading::CErasedPodMsg message = threading::make_pod_message(10, result);
        invocation.message = &message;
        invoke(modules::EExecutionEvent::message);
        result.notice = modules::EModuleNotice::completed;
        result.status = modules::EModuleStatus::function_unavailable;
        message = threading::make_pod_message(10, result);
        facade.result = modules::ESubmission::busy;
        invoke(modules::EExecutionEvent::message);
        invocation.message = nullptr;
        for (unsigned turn = 0u; turn < 3u; ++turn)
        {
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (!progress.work_pending && !progress.work_complete && (facade.accepted == 1u)));
        }
        facade.result = accepted;
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.accepted == 2u) && (facade.module == module_ids::executive) &&
            (facade.action == modules::EModuleAction::unload)));
        const unsigned final_attempts = facade.attempts;
        for (unsigned turn = 0u; turn < 3u; ++turn) invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, ((facade.attempts == final_attempts) && !progress.work_complete));
        invoke(modules::EExecutionEvent::stop);
        TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && !progress.work_pending));
        // The common preamble reports admission closure even for a context
        // which provides no routed service. Cleanup progress remains valid.
        invoke(modules::EExecutionEvent::provider_admission_closed);
        invoke(modules::EExecutionEvent::service_async);
        TEST_EXPECT(ctx, (execution.complete() && progress.work_complete && !progress.work_pending &&
            (facade.attempts == final_attempts) && facade.valid));
    }
    return ctx.report("PumpExecutive");
}

void run_pump_fixture_tests(tests::TTestContext& ctx)
{
    pump_credit_custody(ctx);
    pump_borrow_custody(ctx);
    pump_close_retry(ctx);
    pump_notification_custody(ctx);
    pump_modal_custody(ctx);
    pump_schema_custody(ctx);
    pump_platform_shell(ctx);
    pump_report_custody(ctx);
    pump_report_injected(ctx);
    pump_native_custody(ctx);
}
