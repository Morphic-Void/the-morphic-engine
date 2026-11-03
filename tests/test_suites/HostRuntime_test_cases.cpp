
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostRuntime_test_cases.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Faults at actual Host admission, native worker publication and clock boundaries.

#include "filesystem/filesystem_messages.hpp"
#include "module/module_messages.hpp"
#include <atomic>
#include "host/runtime/host.hpp"
#include "host/runtime/host_payloads.hpp"
#include "host/system/host_context.hpp"
#include "platform/threading/processor_relax.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/file_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"

namespace host
{

struct SHostRuntimeTestAccess
{
    struct SWorkers
    {
        std::atomic<bool> fail{false}, publish{false};
        CErasedOwner completion;
        SClientRecord client{module_ids::executive, 1u};
        bool interleaved{false}, completed_before_cancel{false};
    };

    template<typename Predicate> static bool wait(Predicate predicate)
    {
        platform::threading::CWaitDeadline deadline;
        if (deadline.set_after_milliseconds(5000u).code != platform::threading::EWaitError::none) return false;
        while (!predicate())
        {
            std::uint64_t remaining = 0u;
            if (deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none || remaining == 0u) return false;
            platform::threading::processor_relax();
        }
        return true;
    }

    static std::uint32_t MV_STD_ABI_CALL worker(void* data) noexcept
    {
        auto& resources = *static_cast<threading::CThreadResources*>(data);
        auto& state = *static_cast<SWorkers*>(resources.config.prepare_context);
        threading::CThreadContext context{resources};
        context.startup();
        context.mark_running();
        if (resources.config.thread_id == thread_ids::bg_conditioning)
        {
            (void)wait([&] { return state.fail.load(std::memory_order_acquire) || context.exit_requested(); });
            context.mark_failed(71u);
            return 71u;
        }
        const bool released = wait([&] { return state.publish.load(std::memory_order_acquire); });
        threading::CErasedOwnerMsg message;
        message.set_message_type<FilesystemScanResult>();
        message.set_async_slot(0);
        message.set_owner(std::move(state.completion));
        const bool posted = context.post(std::move(message));
        if (released && posted) { context.mark_exiting(); context.mark_exited(); }
        else context.mark_failed(72u);
        return released && posted ? 0u : 72u;
    }

    static void after_drain(CHost& runtime, threading::CThreadPackage& package)
    {
        auto& state = *static_cast<SWorkers*>(runtime.m_test_state);
        if (&package != runtime.worker_package(0u) || state.publish.load(std::memory_order_acquire)) return;
        //  Publish only after Host read both empty queues while the sibling is failed.
        state.publish.store(true, std::memory_order_release);
        state.interleaved = wait([&] { return package.query_state() == threading::EThreadRunState::Exited; });
    }

    static void after_failure(CHost& runtime)
    {
        auto& state = *static_cast<SWorkers*>(runtime.m_test_state);
        filesystem::FilesystemRefreshResult result;
        state.completed_before_cancel = state.client.message_count == 1u &&
            state.client.messages[0].copy_payload_to(result) && result.status == filesystem::EFilesystemStatus::scan_failed;
    }

    static void rejected_scan(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_worker_count = 1u;
        runtime.m_test_reject_initial_scan = true;
        TEST_EXPECT(ctx, !runtime.initialise_runtime("unused-test-executive"));
        TEST_EXPECT(ctx, runtime.worker_package(0u) != nullptr && runtime.filesystem_idle() && !runtime.m_scan_in_flight);
        TEST_EXPECT(ctx, runtime.shutdown());
        TEST_EXPECT(ctx, runtime.m_phase == CHost::EPhase::complete && runtime.m_worker_count == 0u);
    }

    static void final_worker_publication(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        SWorkers state;
        state.client.phase = EClientPhase::ready;
        state.completion = CErasedOwner::create<FilesystemScanResult>();
        TEST_EXPECT(ctx, state.completion.payload<FilesystemScanResult>() != nullptr);
        CHost runtime{ allocator };
        runtime.m_test_state = &state;
        runtime.m_test_after_worker_drain = &after_drain;
        runtime.m_test_after_worker_failure = &after_failure;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        TEST_EXPECT(ctx, runtime.m_thread_packages.initialise() && runtime.m_asset_service.initialise());
        runtime.m_worker_count = 2u;
        const thread_ids::id_type ids[]{thread_ids::bg_file_io, thread_ids::bg_conditioning};
        for (unsigned index = 0u; index < 2u; ++index)
        {
            const threading::ThreadConfig config{ids[index], module_ids::executable,
                platform::threading::EThreadPriority::Normal, &worker, nullptr, &state};
            runtime.m_worker_slots[index] = runtime.m_thread_packages.emplace(config, runtime.m_perf_count_conversion);
            TEST_EXPECT(ctx, runtime.worker_package(index) != nullptr && runtime.worker_package(index)->startup());
        }
        TEST_EXPECT(ctx, runtime.m_asset_service.configure_workers(*runtime.worker_package(0u), runtime.worker_package(1u)));
        runtime.m_refresh_queue[0].owner = CErasedOwner::create<filesystem::FilesystemRefreshRequest>();
        runtime.m_refresh_queue[0].client = &state.client;
        runtime.m_refresh_queue[0].slot = 51;
        runtime.m_refresh_count = 1u;
        runtime.m_scan_in_flight = true;
        TEST_EXPECT(ctx, state.client.reserve_replies(1u));
        state.fail.store(true, std::memory_order_release);
        TEST_EXPECT(ctx, wait([&] { return runtime.worker_package(1u)->query_state() == threading::EThreadRunState::Failed; }));
        runtime.run();
        TEST_EXPECT(ctx, state.interleaved && state.completed_before_cancel);
        TEST_EXPECT(ctx, runtime.m_worker_failure_drained && runtime.m_runtime_failed && !runtime.retains_state());
        TEST_EXPECT(ctx, state.client.replies_idle() && runtime.filesystem_idle());
        TEST_EXPECT(ctx, runtime.shutdown());
    }

    static void clock_failures(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        const auto path = test_environment::test_log_path("host_clock_failures");
        const auto direct = test_environment::test_log_path("host_clock_failures_direct");
        {
            auto owner = TInstance<debug_system::CDebugServiceState>::create();
            TEST_EXPECT(ctx, owner.is_ready());
            auto& service = *owner;
            TEST_EXPECT(ctx, service.configure_log_paths(path.c_str(), direct.c_str()) && service.open_logs());
            TEST_EXPECT(ctx, debug_system::install_service(&service) && service.start());
            for (unsigned failure = 1u; failure <= 2u; ++failure)
            {
                CHost runtime{ allocator };
                runtime.m_test_clock_failure = failure;
                runtime.check_shutdown_deadline();
                TEST_EXPECT(ctx, runtime.m_shutdown_reported && runtime.m_runtime_failed);
                TEST_EXPECT(ctx, runtime.m_phase != CHost::EPhase::complete); //  No forced completion or cleanup.
                runtime.check_shutdown_deadline(); //  One diagnostic for each failed clock.
            }
            TEST_EXPECT(ctx, service.stop() && debug_system::uninstall_service(&service));
        }
        TEST_EXPECT(ctx, tests::file_contains(path.c_str(), "shutdown clock failed (code 3, native 123)"));
        TEST_EXPECT(ctx, tests::file_contains(path.c_str(), "shutdown clock failed (code 3, native 456)"));
        TEST_EXPECT(ctx, tests::file_contains(path.c_str(), "continuing progress with live resources retained"));
    }

    static std::uint32_t MV_STD_ABI_CALL pinned_worker(void* data) noexcept
    {
        auto& resources = *static_cast<threading::CThreadResources*>(data);
        threading::CThreadContext context{ resources };
        context.startup();
        context.mark_running();
        (void)wait([&] { return context.exit_requested(); });
        context.mark_exited();
        return 0u;
    }

    static void notification_shutdown_retention(tests::TTestContext& ctx)
    {
        using namespace services;
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete; //  Force final teardown to encounter a still-pinned package.
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init() && runtime.m_thread_packages.initialise());
        const threading::ThreadConfig config{ thread_ids::bg_file_io, module_ids::executable,
            platform::threading::EThreadPriority::Normal, &pinned_worker, nullptr, nullptr };
        runtime.m_worker_count = 1u;
        runtime.m_worker_slots[0] = runtime.m_thread_packages.emplace(config, runtime.m_perf_count_conversion);
        auto* const package = runtime.worker_package(0u);
        TEST_EXPECT(ctx, package && package->startup());
        if (!package) return;
        threading::CNotification pinned;
        TEST_EXPECT(ctx, package->pin_notification(pinned));

        const auto client_index = module_ids::executive_index.raw_value();
        const auto provider_index = module_ids::ops::decode_id(module_ids::rendering).raw_value();
        TEST_EXPECT(ctx, runtime.m_clients[client_index].emplace(module_ids::executive, 101u));
        TEST_EXPECT(ctx, runtime.m_clients[provider_index].emplace(module_ids::rendering, 102u));
        auto& client = *runtime.m_clients[client_index];
        auto& provider = *runtime.m_clients[provider_index];
        client.phase = provider.phase = EClientPhase::ready;
        client.execution = provider.execution = EClientExecution::pumped;
        const SServiceContract contract{ 1u, 1u };
        runtime.m_providers[provider_index] = { &provider, &contract, 1u };
        auto& router = runtime.m_service_router;
        TEST_EXPECT(ctx, client.reserve_replies(1u));
        router.open({ module_ids::rendering, 1u, 1u, { 1u } }, 1, client, &runtime.m_providers[provider_index]);
        OpenServiceResult opened;
        TEST_EXPECT(ctx, client.messages[0].copy_payload_to(opened) && opened.status == EServiceStatus::success);
        client.consume(1u);
        ServiceRequest request;
        request.route = opened.route;
        request.request.value = 1u;
        request.opcode = 1u;
        TEST_EXPECT(ctx, client.reserve_replies(2u));
        router.request(request, 2, client);
        client.consume(1u);
        SDispatchedCommand command;
        TEST_EXPECT(ctx, router.prepare_batch(provider, &command, 1u) == 1u);
        runtime.m_executive_request = CErasedOwner::create<modules::ModuleRequest>();
        auto* const dependency = runtime.m_executive_request.payload<modules::ModuleRequest>();
        TEST_EXPECT(ctx, dependency != nullptr);
        SServiceBorrow borrow;
        TEST_EXPECT(ctx, router.prepare_borrow(provider, { opened.route, { 1u }, { dependency, sizeof(*dependency), 1u } }, borrow) == EServiceStatus::success);
        TEST_EXPECT(ctx, router.finish_batch(provider, &command, 1u, 1u));
        router.progress();
        client.consume(client.message_count);
        TEST_EXPECT(ctx, client.reserve_replies(1u));
        router.register_notification(client, { opened.route, borrow }, 3);
        ServiceNotificationRegistered registered;
        TEST_EXPECT(ctx, client.messages[0].copy_payload_to(registered) && registered.status == EServiceStatus::success);
        client.consume(1u);
        threading::SNotificationEndpoint endpoint;
        TEST_EXPECT(ctx, router.bind_borrow_notification(provider, borrow, registered.registration, endpoint) == EServiceStatus::success);
        package->request_exit();
        TEST_EXPECT(ctx, wait([&] { return package->query_state() == threading::EThreadRunState::Exited; }));

        TEST_EXPECT(ctx, !runtime.shutdown() && runtime.retains_state());
        TEST_EXPECT(ctx, runtime.worker_package(0u) == package && runtime.m_worker_count == 1u);
        TEST_EXPECT(ctx, &*runtime.m_clients[client_index] == &client && &*runtime.m_clients[provider_index] == &provider);
        TEST_EXPECT(ctx, runtime.m_executive_request.payload<modules::ModuleRequest>() == dependency);
        TEST_EXPECT(ctx, runtime.m_providers[provider_index].client == &provider);
        TEST_EXPECT(ctx, !router.client_idle(client) && router.provider_in_use(provider));
        TEST_EXPECT(ctx, endpoint.signal() && registered.reader.pending() && client.notifications_pending.load() == 1u);
        TEST_EXPECT(ctx, pinned.endpoint().signal()); //  Neither wait target was destroyed by refused teardown.

        router.release_borrow(client, { opened.route, borrow });
        router.quiesce_borrow(client, { opened.route, borrow });
        router.progress();
        TEST_EXPECT(ctx, router.finish_borrow_retirement(provider, borrow) == EServiceStatus::success);
        ServiceReply completed;
        completed.route = opened.route;
        completed.request.value = 1u;
        completed.opcode = 1u;
        completed.status = EServiceStatus::success;
        TEST_EXPECT(ctx, router.complete(provider, completed) == EServiceStatus::success);
        router.progress();
        TEST_EXPECT(ctx, client.reserve_replies(1u));
        router.close({ opened.route, { 2u } }, 4, client);
        router.progress();
        TEST_EXPECT(ctx, router.client_idle(client) && !router.provider_in_use(provider));
        package->unpin_notification();
        //  Only test cleanup revokes retention, after all synthetic lifetime proofs.
        runtime.m_retain_runtime = false;
        TEST_EXPECT(ctx, runtime.shutdown() && runtime.worker_package(0u) == nullptr);
        TEST_EXPECT(ctx, runtime.m_executive_request.is_empty());
    }

    static void context_installation_failure(tests::TTestContext& ctx)
    {
        auto* const previous_memory = memory::get_ambient_memory_context();
        const auto previous_module = system_context::get_ambient_module_id();
        const auto previous_thread = system_context::get_ambient_thread_id();
        {
            CHostContext context;
            auto* const host_memory = memory::get_ambient_memory_context();
            //  The test executable already installed its registries. A second
            //  installation must fail while leaving dependent storage usable
            //  until the rejected process state has been destroyed.
            TEST_EXPECT(ctx, !context.is_installed() && host_memory != previous_memory);
            TEST_EXPECT(ctx, &host_memory->get_allocator() == &context.allocator());
            TEST_EXPECT(ctx, system_context::get_ambient_module_id() == module_ids::executable);
            TEST_EXPECT(ctx, system_context::get_ambient_thread_id() == thread_ids::host);
            {
                CHost runtime{ context.allocator() };
                auto owner = TInstance<int>::create(7);
                TEST_EXPECT(ctx, owner.is_ready() && *owner == 7);
                TEST_EXPECT(ctx, !host_memory->is_attribution_empty());
            }
            TEST_EXPECT(ctx, host_memory->is_attribution_empty());
            TEST_EXPECT(ctx, debug_system::get_service() == nullptr);
        }
        TEST_EXPECT(ctx, memory::get_ambient_memory_context() == previous_memory);
        TEST_EXPECT(ctx, system_context::get_ambient_module_id() == previous_module);
        TEST_EXPECT(ctx, system_context::get_ambient_thread_id() == previous_thread);
    }
};

}   //  namespace host

void run_host_runtime_tests(tests::TTestContext& ctx)
{
    host::SHostRuntimeTestAccess::rejected_scan(ctx);
    host::SHostRuntimeTestAccess::final_worker_publication(ctx);
    host::SHostRuntimeTestAccess::clock_failures(ctx);
    host::SHostRuntimeTestAccess::notification_shutdown_retention(ctx);
    host::SHostRuntimeTestAccess::context_installation_failure(ctx);
}
