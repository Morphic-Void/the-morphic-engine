
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostMaintenance_test_cases.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Actual Host lifetime retention, bounded probes and installed subscriptions.

#include "host/runtime/host.hpp"
#include "module/module_messages.hpp"
#include "threading/messages/message_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/environment/test_paths.hpp"
#include "host/runtime/host_payloads.hpp"
#include "platform/filesystem/file.hpp"

namespace threading
{
struct SThreadPackageTestAccess
{
    static void fail_join(CThreadPackage& package, const bool fail = true) noexcept { package.m_test_fail_join = fail; }
    static void reject_create(CThreadPackage& package) noexcept { package.m_test_reject_create = true; }
    static bool retained(const CThreadPackage& package) noexcept
    {
        return package.m_resources.thread.is_valid() && package.m_resources.wait_predicate.has_control() &&
            package.m_host_pinned && package.m_notification_pins == 1u && package.m_resources.host_execution == package.m_host_execution;
    }
    static bool rolled_back(const CThreadPackage& package) noexcept
    {
        return !package.m_resources.thread.is_valid() && !package.m_resources.wait_predicate.has_control() &&
            !package.m_host_pinned && !package.m_notification_pins && !package.m_resources.host_execution;
    }
};
}

namespace modules
{
struct SBoundModuleTestAccess
{
    static EBindingResult MV_STD_ABI_CALL thread(const thread_ids::id_type) noexcept { return EBindingResult::success; }
    static EBindingResult MV_STD_ABI_CALL provision(void* const) noexcept { return EBindingResult::success; }
    static EBindingResult MV_STD_ABI_CALL memory(memory::CMemoryContext* const) noexcept { return EBindingResult::success; }
    static void prepare(CBoundModule& binding) noexcept
    {
        binding.m_installed = true;
        binding.m_core.install_ambient_thread_id = &thread;
        binding.m_core.install_thread_provisioning = &provision;
        binding.m_core.install_thread_memory_context = &memory;
    }
};
}

namespace host
{

struct SHostMaintenanceTestAccess
{
    struct SInvalidFrame
    {
        CHost* runtime{ nullptr };
        tests::TTestContext* tests{ nullptr };
        const modules::SHostPumpContext* context{ nullptr };
        std::atomic<bool> publish{ false };
        CErasedOwner completion;
        bool captured{ false }, deactivated{ false };
    };
    static inline SInvalidFrame* s_invalid_frame{ nullptr };

    static std::uint32_t MV_STD_ABI_CALL completion_worker(void* const data) noexcept
    {
        auto& resources = *static_cast<threading::CThreadResources*>(data);
        auto& frame = *static_cast<SInvalidFrame*>(resources.config.prepare_context);
        threading::CThreadContext context{ resources };
        context.startup();
        context.mark_running();
        while (!frame.publish.load()) platform::threading::processor_relax();
        threading::CErasedOwnerMsg result;
        result.set_message_type<FilesystemScanResult>();
        result.set_owner(std::move(frame.completion));
        const bool posted = context.post(std::move(result));
        if (posted) context.mark_exited();
        else context.mark_failed(1u);
        return posted ? 0u : 1u;
    }

    static modules::ECallStatus MV_STD_ABI_CALL invalid_activate(const modules::SHostPumpContext* const context,
        void** const state, modules::SProgress* const progress) noexcept
    {
        s_invalid_frame->context = context;
        *state = s_invalid_frame;
        progress->phase = modules::EPumpPhase::ready;
        return modules::ECallStatus::success;
    }

    static modules::ECallStatus MV_STD_ABI_CALL invalid_pump(void* const state, const modules::SPumpInput* const input,
        modules::SPumpResult* const result) noexcept
    {
        auto& frame = *static_cast<SInvalidFrame*>(state);
        auto& runtime = *frame.runtime;
        auto& ctx = *frame.tests;
        auto& maintenance = runtime.m_maintenance;
        auto& client = *runtime.module_client(module_ids::rendering);
        const auto& services = *frame.context->services;
        void* context = frame.context->service_context;
        const auto scope = services.enter_restriction(context, { 1u });
        TEST_EXPECT(ctx, scope.status == modules::ERestrictionStatus::success);
        threading::CErasedOwnerMsg request;
        request.set_message_type<modules::ModuleRequest>();
        request.set_owner(CErasedOwner::create<modules::ModuleRequest>());
        const auto* owner = request.owner().payload<modules::ModuleRequest>();
        TEST_EXPECT(ctx, services.post_owner(context, &request) == services::EServiceStatus::success);
        frame.publish.store(true);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        { return runtime.worker_package(0u)->query_state() == threading::EThreadRunState::Exited; }));
        const auto allocations = memory::get_ambient_memory_context()->get_live_allocation_count();
        for (unsigned count = 0u; count != 128u &&
            (!maintenance.captured(&client, nullptr) || !maintenance.references(*runtime.worker_package(0u))); ++count)
            TEST_EXPECT(ctx, services.maintain_host(context).status == modules::EMaintenanceStatus::success);
        frame.captured = maintenance.captured(&client, nullptr) && maintenance.references(*runtime.worker_package(0u));
        TEST_EXPECT(ctx, frame.captured && memory::get_ambient_memory_context()->get_live_allocation_count() == allocations);
        bool retained_owner = false;
        for (const auto& entry : maintenance.m_owners)
            if (entry.source.client == &client && entry.message.owner().payload<modules::ModuleRequest>() == owner) retained_owner = true;
        TEST_EXPECT(ctx, retained_owner && input->message_count == 1u && input->messages[0].query_async_slot() == 901);
        maintenance.replay();
        TEST_EXPECT(ctx, runtime.m_scan_in_flight && runtime.m_refresh_count == 1u && client.subscription_live);
        TEST_EXPECT(ctx, !client.retire_host_execution(false) && !runtime.shutdown() && runtime.retains_state());
        TEST_EXPECT(ctx, services.leave_restriction(context, scope.scope) == modules::ERestrictionStatus::success);
        result->consumed_messages = input->message_count + 1u;
        result->progress.phase = modules::EPumpPhase::ready;
        return modules::ECallStatus::success;
    }

    static modules::ECallStatus MV_STD_ABI_CALL invalid_stop(void* const, const modules::EStopReason, modules::SProgress* const progress) noexcept
    {
        progress->phase = modules::EPumpPhase::stopped;
        return modules::ECallStatus::success;
    }

    static modules::ECallStatus MV_STD_ABI_CALL invalid_deactivate(void* const state) noexcept
    {
        static_cast<SInvalidFrame*>(state)->deactivated = true;
        return modules::ECallStatus::success;
    }

    static void invalid_outer_prefix(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init() && runtime.m_thread_packages.initialise());
        SInvalidFrame frame;
        frame.runtime = &runtime;
        frame.tests = &ctx;
        frame.completion = CErasedOwner::create<FilesystemScanResult>();
        s_invalid_frame = &frame;
        const threading::ThreadConfig worker{ thread_ids::bg_file_io, module_ids::executable,
            platform::threading::EThreadPriority::Normal, &completion_worker, nullptr, &frame };
        runtime.m_worker_count = 1u;
        runtime.m_worker_slots[0] = runtime.m_thread_packages.emplace(worker, runtime.m_perf_count_conversion);
        TEST_EXPECT(ctx, runtime.worker_package(0u) && runtime.worker_package(0u)->startup());
        const auto index = module_ids::ops::decode_id(module_ids::rendering).raw_value();
        TEST_EXPECT(ctx, runtime.m_clients[index].emplace(module_ids::rendering, 81u));
        auto& client = *runtime.m_clients[index];
        client.execution = EClientExecution::pumped;
        modules::SHostPumpV1 capability;
        capability.activate = &invalid_activate;
        capability.pump = &invalid_pump;
        capability.begin_stop = &invalid_stop;
        capability.deactivate = &invalid_deactivate;
        TEST_EXPECT(ctx, runtime.m_pumps[index].emplace(client, binding, capability, runtime.m_service_router, runtime.m_maintenance));
        auto& pump = *runtime.m_pumps[index];
        TEST_EXPECT(ctx, pump.initialise());
        pump.activate();
        TEST_EXPECT(ctx, pump.ready());
        TEST_EXPECT(ctx, client.post_reply(threading::make_pod_message(901, modules::RunTestsResult{})) && client.reserve_replies(1u));
        runtime.m_refresh_queue[0].owner = CErasedOwner::create<filesystem::FilesystemRefreshRequest>();
        runtime.m_refresh_queue[0].client = &client;
        runtime.m_refresh_queue[0].slot = 902;
        runtime.m_refresh_count = 1u;
        runtime.m_scan_in_flight = true;
        pump.pump();
        TEST_EXPECT(ctx, frame.captured && pump.failed() && client.prefix_failed && !client.invocation_offered);
        TEST_EXPECT(ctx, !runtime.m_scan_in_flight && runtime.m_refresh_count == 0u && !runtime.m_maintenance.captured(&client, nullptr));
        TEST_EXPECT(ctx, client.message_count == 1u && client.messages[0].query_async_slot() == 901 &&
            client.staged_replies == 3u && client.reserved_replies == 3u && client.deferred_records == 3u && client.subscription_live);
        TEST_EXPECT(ctx, !client.retire_host_execution(false));
        pump.pump(); //  The stopped proof now permits cancelling the protected deliveries.
        TEST_EXPECT(ctx, pump.stopped() && runtime.m_maintenance.empty() && client.replies_idle());
        client.flush();
        client.terminal_before_drain = client.incoming_drained = true;
        TEST_EXPECT(ctx, pump.deactivate() && frame.deactivated && client.retire_host_execution(true));
        runtime.m_retain_runtime = false;
        TEST_EXPECT(ctx, runtime.shutdown());
        s_invalid_frame = nullptr;
    }

    static std::uint32_t MV_STD_ABI_CALL unexpected_entry(void* const) noexcept { return 1u; }
    static void fail_join(threading::CThreadPackage& package) noexcept
    { threading::SThreadPackageTestAccess::fail_join(package); }

    static void startup_rollback(tests::TTestContext& ctx)
    {
        platform::system::CPerfCountConversion conversion;
        TEST_EXPECT(ctx, conversion.init());
        CHostRestrictions restriction;
        for (unsigned failure = 0u; failure != 2u; ++failure)
        {
            const threading::ThreadConfig config{ thread_ids::executive, module_ids::executive,
                platform::threading::EThreadPriority::Normal, &unexpected_entry };
            threading::CThreadPackage package{ config, conversion };
            threading::CNotification notification;
            std::atomic<std::uint32_t> already_installed{ 0u };
            modules::SHostExecutionContext view;
            view.restriction = restriction.reader();
            view.subscription.value = failure + 1u;
            if (!failure) TEST_EXPECT(ctx, notification.initialise(already_installed));
            else threading::SThreadPackageTestAccess::reject_create(package);
            TEST_EXPECT(ctx, package.install_host_execution(view, notification) && !package.startup());
            TEST_EXPECT(ctx, threading::SThreadPackageTestAccess::rolled_back(package));
        }
    }

    static bool MV_STD_ABI_CALL reject_prepare(void* const, const thread_ids::id_type, void* const) noexcept
    { return false; }

    static void executive_startup_cleanup(tests::TTestContext& ctx)
    {
        for (const bool reject_create : { false, true })
        {
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            CHost runtime{ allocator };
            runtime.m_phase = CHost::EPhase::stopping_executive;
            runtime.m_runtime_failed = true;
            TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init() && runtime.m_thread_packages.initialise());
            const auto index = module_ids::executive_index.raw_value();
            const threading::ThreadConfig config{ thread_ids::executive, module_ids::executive,
                platform::threading::EThreadPriority::Normal, &unexpected_entry, &reject_prepare };
            const auto slot = runtime.m_thread_packages.emplace(config, runtime.m_perf_count_conversion);
            runtime.m_module_threads[index].slot = slot;
            auto* const package = runtime.module_thread_package(module_ids::executive);
            TEST_EXPECT(ctx, package && runtime.m_clients[index].emplace(module_ids::executive, 76u, package));
            if (!package || !runtime.m_clients[index]) continue;
            auto& client = *runtime.m_clients[index];
            TEST_EXPECT(ctx, client.install_host_execution(runtime.m_maintenance.restrictions().reader()));
            if (reject_create) threading::SThreadPackageTestAccess::reject_create(*package);
            TEST_EXPECT(ctx, !package->startup() && threading::SThreadPackageTestAccess::rolled_back(*package));
            TEST_EXPECT(ctx, package->query_state() == threading::EThreadRunState::Empty);
            //  Match start_executive after rollback; Host must still prove its
            //  final drain before retiring the client and package together.
            client.subscription_live = false;
            runtime.advance_lifecycle(package->query_state());
            TEST_EXPECT(ctx, (runtime.m_phase == CHost::EPhase::stopping_executive) &&
                (runtime.module_thread_package(module_ids::executive) == package));
            client.terminal_before_drain = client.incoming_drained = true;
            runtime.advance_lifecycle(package->query_state());
            TEST_EXPECT(ctx, !runtime.m_clients[index] && (runtime.m_module_threads[index].slot == -1));
            TEST_EXPECT(ctx, runtime.m_thread_packages.get_object(slot) == nullptr);
            TEST_EXPECT(ctx, (runtime.m_phase == CHost::EPhase::complete) && !runtime.retains_state());
            TEST_EXPECT(ctx, runtime.shutdown());
        }
    }

    static void failed_start_join(tests::TTestContext& ctx)
    {
        for (const bool executive : { false, true })
        {
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            CHost runtime{ allocator };
            runtime.m_phase = CHost::EPhase::complete;
            runtime.m_test_before_client_start = &fail_join;
            TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init() && runtime.m_thread_packages.initialise());
            const auto module = executive ? module_ids::executive : module_ids::rendering;
            const auto index = module_ids::ops::decode_id(module).raw_value();
            auto& service = runtime.m_module_service;
            TEST_EXPECT(ctx, service.m_records[index].emplace(allocator, module));
            auto& record = *service.m_records[index];
            const auto filename = test_environment::module_filename(executive ? "MorphicExecutive" : "MorphicRendering");
            const auto source = test_environment::binary_path(filename.c_str());
            const auto isolated = test_environment::test_output_path(filename.c_str());
            //  Some loaders retain a native image after dlclose. A private copy
            //  gives this failed-start test its own one-time bootstrap lifetime.
            CByteBuffer bytes = platform::filesystem::loadFile(source.c_str());
            TEST_EXPECT(ctx, platform::filesystem::saveFile(isolated.c_str(), bytes.const_view()));
            bytes.deallocate();
            const auto path = platform::path::makeNativePath(isolated.c_str());
            const modules::SAdvertisedIdentity peer{ module_ids::executable,
                { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
            TEST_EXPECT(ctx, path.is_ready() && record.binding.bind(path, module, peer));
            //  Deliberately leave the real DLL uninstalled: native preparation fails.
            record.function = reinterpret_cast<modules::FModuleFunction>(&unexpected_entry);
            if (executive)
            {
                TEST_EXPECT(ctx, runtime.m_clients[index].emplace(module, 75u));
                TEST_EXPECT(ctx, !runtime.start_executive());
            }
            else
            {
                service.m_work.next = &record.binding;
                service.m_work.service_thread = { thread_ids::rendering, &unexpected_entry };
                service.m_pending = service.m_start_pending = true;
                runtime.start_pending_module();
                TEST_EXPECT(ctx, service.is_idle() && !service.m_work.cleanup_only);
            }
            auto* const package = runtime.module_thread_package(module);
            TEST_EXPECT(ctx, package && runtime.retains_state() && runtime.m_runtime_failed && record.binding.is_bound());
            if (!package) { (void)record.binding.unbind(); continue; }
            auto& client = *runtime.m_clients[index];
            client.terminal_before_drain = client.incoming_drained = true;
            TEST_EXPECT(ctx, threading::SThreadPackageTestAccess::retained(*package));
            if (executive)
            {
                runtime.m_phase = CHost::EPhase::stopping_executive;
                runtime.advance_lifecycle(package->query_state());
                TEST_EXPECT(ctx, (runtime.m_phase == CHost::EPhase::stopping_executive) &&
                    (runtime.module_thread_package(module) == package) && client.subscription_live && record.binding.is_bound() &&
                    threading::SThreadPackageTestAccess::retained(*package));
            }
            TEST_EXPECT(ctx, !client.retire_host_execution(false) && !runtime.shutdown());
            TEST_EXPECT(ctx, client.subscription_live && threading::SThreadPackageTestAccess::retained(*package) &&
                service.m_records[index] && record.binding.is_bound() && runtime.module_thread_package(module) == package);
            threading::SThreadPackageTestAccess::fail_join(*package, false);
            TEST_EXPECT(ctx, client.retire_host_execution(false) && package->shutdown());
            TEST_EXPECT(ctx, record.binding.unbind());
            runtime.m_retain_runtime = false;
            TEST_EXPECT(ctx, runtime.shutdown());
        }
    }

    struct SClock
    {
        std::uint64_t time{ 1u }, step{ 0u };
        static platform::threading::WaitError read(void* const context, std::uint64_t& value) noexcept
        {
            auto& clock = *static_cast<SClock*>(context);
            value = clock.time;
            clock.time += clock.step;
            return {};
        }
    };

    static void bounds(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        SClock clock;
        CHostMaintenance maintenance{ runtime, { &clock, &SClock::read } };
        auto& restriction = maintenance.restrictions();
        TEST_EXPECT(ctx, restriction.begin_invocation({ 1u }, true));
        const auto scope = restriction.enter({ 0u });
        TEST_EXPECT(ctx, scope.status == modules::ERestrictionStatus::success);
        const auto count = maintenance.run();
        TEST_EXPECT(ctx, count.status == modules::EMaintenanceStatus::success && count.operations == 32u && count.continuation);
        const auto cursor = maintenance.m_lane;
        maintenance.m_running = true;
        const auto nested = maintenance.run();
        TEST_EXPECT(ctx, nested.status == modules::EMaintenanceStatus::reentrant && !nested.operations && maintenance.m_lane == cursor);
        maintenance.m_running = false;
        clock.step = 600000u;
        const auto duration = maintenance.run();
        TEST_EXPECT(ctx, duration.status == modules::EMaintenanceStatus::success && duration.operations == 1u);
        TEST_EXPECT(ctx, restriction.leave(scope.scope) == modules::ERestrictionStatus::success && restriction.end_invocation());
        TEST_EXPECT(ctx, maintenance.run().status == modules::EMaintenanceStatus::invalid_context);
    }

    static void deferred_retirement(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        const auto index = module_ids::executive_index.raw_value();
        TEST_EXPECT(ctx, runtime.m_clients[index].emplace(module_ids::executive, 11u));
        auto& client = *runtime.m_clients[index];
        client.execution = EClientExecution::pumped;
        client.phase = EClientPhase::stopped;
        auto& maintenance = runtime.m_maintenance;
        TEST_EXPECT(ctx, client.install_host_execution(maintenance.restrictions().reader()));
        TEST_EXPECT(ctx, maintenance.restrictions().begin_invocation(client.instance, true));
        const auto scope = maintenance.restrictions().enter({ 0u });
        TEST_EXPECT(ctx, scope.status == modules::ERestrictionStatus::success);
        modules::ModuleRequest* retained[32]{};
        for (unsigned position = 0u; position < 32u; ++position)
        {
            auto& entry = maintenance.m_owners[position];
            auto owner = CErasedOwner::create<modules::ModuleRequest>();
            retained[position] = owner.payload<modules::ModuleRequest>();
            TEST_EXPECT(ctx, retained[position] != nullptr && client.reserve_replies(2u));
            entry.message.set_message_type<modules::ModuleRequest>();
            entry.message.set_owner(std::move(owner));
            entry.source.client = &client;
            entry.source.credits = 2u;
            maintenance.accepted(entry.source);
        }
        //  Even a caller claiming empty source queues cannot bypass retained captures.
        client.terminal_before_drain = client.incoming_drained = true;
        client.invocation_offered = true;
        TEST_EXPECT(ctx, !client.execution_quiescent() && !client.retire_host_execution(true));
        const auto* ambient = memory::get_ambient_memory_context();
        const auto allocations = ambient->get_live_allocation_count();
        const auto full = maintenance.run();
        TEST_EXPECT(ctx, full.operations <= 32u && full.capacity_pressure && client.deferred_records == 32u);
        TEST_EXPECT(ctx, ambient->get_live_allocation_count() == allocations);
        TEST_EXPECT(ctx, client.host_execution.stop.request());
        const auto stopping = maintenance.run();
        TEST_EXPECT(ctx, stopping.shutdown_requested && stopping.cancel_native && runtime.m_work_admission_closed);
        TEST_EXPECT(ctx, runtime.m_shutdown_deadline.set_after_milliseconds(0u).code == platform::threading::EWaitError::none);
        (void)maintenance.run();
        TEST_EXPECT(ctx, runtime.m_runtime_failed && !runtime.shutdown() && runtime.retains_state());
        TEST_EXPECT(ctx, client.subscription_live && client.deferred_records == 32u && !maintenance.empty());
        for (unsigned position = 0u; position < 32u; ++position)
        {
            auto& entry = maintenance.m_owners[position];
            TEST_EXPECT(ctx, entry.message.owner().payload<modules::ModuleRequest>() == retained[position]);
        }
        TEST_EXPECT(ctx, maintenance.restrictions().leave(scope.scope) == modules::ERestrictionStatus::success);
        TEST_EXPECT(ctx, maintenance.restrictions().end_invocation());
        //  Stand in for safe rejection after an invalid prefix: discharge every
        //  accepted owner and credit explicitly, never merely clear a queue count.
        client.invocation_offered = false;
        for (unsigned position = 0u; position < 32u; ++position)
        {
            auto& entry = maintenance.m_owners[position];
            entry.message = {};
            client.cancel_reservation(entry.source.credits);
            maintenance.released(entry.source);
        }
        TEST_EXPECT(ctx, maintenance.empty() && client.execution_quiescent() && client.retire_host_execution(true));
        runtime.m_retain_runtime = false; //  Test proved and performed all deferred retirement.
        TEST_EXPECT(ctx, runtime.shutdown());
    }

    struct SSubscriber
    {
        std::atomic<unsigned> ready{ 0u }, restricted{ 0u }, resumed{ 0u }, failed{ 0u };
        std::atomic<unsigned> restricted_observations{ 0u };
        std::atomic<unsigned> fanout_complete{ 0u }, quiet_wait{ 0u };
    };

    static void prefix_staging(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        const auto index = module_ids::ops::decode_id(module_ids::rendering).raw_value();
        TEST_EXPECT(ctx, runtime.m_clients[index].emplace(module_ids::rendering, 41u));
        auto& client = *runtime.m_clients[index];
        auto& maintenance = runtime.m_maintenance;
        client.execution = EClientExecution::pumped;
        client.phase = EClientPhase::ready;
        client.delivery_owner = &maintenance;
        const auto message = [](const int slot) { return threading::make_pod_message(slot, modules::RunTestsResult{}); };
        TEST_EXPECT(ctx, client.post(message(10)) && client.post_reply(message(1)) && client.post_reply(message(2)));
        threading::CErasedPodMsg offered[3];
        std::uint32_t controls = 0u;
        TEST_EXPECT(ctx, client.copy_messages(offered, 3u, controls) == 3u && controls == 1u);
        TEST_EXPECT(ctx, client.reserve_control_replies(1u) && client.reserve_replies(63u));
        client.invocation_offered = true;
        TEST_EXPECT(ctx, client.reply_control(message(11)));
        for (int slot = 20; slot < 83; ++slot) TEST_EXPECT(ctx, client.retry_reply(message(slot)));
        TEST_EXPECT(ctx, client.deferred_records == 64u && client.message_count == 2u && client.control_count == 1u);
        TEST_EXPECT(ctx, client.reserved_replies == 63u && client.reserved_control_replies == 1u);
        TEST_EXPECT(ctx, client.reserve_replies(1u));
        TEST_EXPECT(ctx, !client.retry_reply(message(84)) && client.reserved_replies == 64u);
        client.cancel_reservation(1u);
        maintenance.replay();
        TEST_EXPECT(ctx, client.deferred_records == 64u && offered[0].query_async_slot() == 10 && offered[1].query_async_slot() == 1);
        client.consume_messages(2u, controls);
        client.invocation_offered = false;
        maintenance.replay();
        TEST_EXPECT(ctx, maintenance.empty() && client.replies_idle() && client.staged_replies == 0u && client.staged_controls == 0u);
        TEST_EXPECT(ctx, client.message_count == 64u && client.messages[0].query_async_slot() == 2 && client.control_messages[0].query_async_slot() == 11);
        for (unsigned position = 1u; position < 64u; ++position) TEST_EXPECT(ctx, client.messages[position].query_async_slot() == 19 + position);
        client.consume_messages(65u, 1u);

        client.invocation_offered = true;
        TEST_EXPECT(ctx, client.reserve_replies(1u) && client.retry_reply(message(99)));
        client.invocation_offered = false;
        client.prefix_failed = true;
        maintenance.replay();
        TEST_EXPECT(ctx, !maintenance.empty() && !client.message_count && client.reserved_replies == 1u);
        //  A blocked outbound stream must not prevent independent inbound replay.
        auto& capture = maintenance.m_owners[0];
        capture.message.set_message_type<modules::ModuleRequest>();
        capture.message.set_owner(CErasedOwner::create<modules::ModuleRequest>());
        capture.source.client = &client;
        capture.source.credits = 2u;
        TEST_EXPECT(ctx, client.reserve_replies(2u));
        maintenance.accepted(capture.source);
        maintenance.replay();
        TEST_EXPECT(ctx, !capture.source.sequence && !capture.message.has_owner() && client.deferred_records == 3u);
        client.phase = EClientPhase::stopped;
        maintenance.replay();
        TEST_EXPECT(ctx, maintenance.empty() && client.replies_idle() && !client.message_count && client.staged_replies == 0u);
        TEST_EXPECT(ctx, runtime.shutdown());
    }

    static std::uint32_t MV_STD_ABI_CALL subscriber(void* const data) noexcept
    {
        auto& resources = *static_cast<threading::CThreadResources*>(data);
        auto& state = *static_cast<SSubscriber*>(resources.config.prepare_context);
        threading::CThreadContext context{ resources };
        context.startup();
        context.mark_running();
        const auto* const execution = context.host_execution();
        if (!execution || !execution->notification.pending())
        {
            state.failed.store(1u, std::memory_order_release);
            context.mark_failed(1u);
            return 1u;
        }
        state.ready.store(1u, std::memory_order_release);
        while (!context.exit_requested())
        {
            //  A quiet iteration must capture its epoch after completed fanout.
            const bool fanout_complete = state.fanout_complete.load(std::memory_order_acquire) != 0u;
            const auto epoch = context.observe_epoch();
            execution->notification.clear_pending();
            modules::SHostRestrictionSnapshot snapshot;
            const auto read = execution->restriction.read(snapshot);
            if (read == modules::ERestrictionRead::failed) { state.failed.store(1u); break; }
            if (read == modules::ERestrictionRead::retry) continue;
            if (snapshot.depth)
            {
                state.restricted_observations.fetch_add(1u);
                state.restricted.store(1u, std::memory_order_release);
            }
            else if (state.restricted.load(std::memory_order_acquire)) state.resumed.store(1u, std::memory_order_release);
            if (context.exit_requested()) break;
            if (snapshot.depth)
            {
                if (fanout_complete)
                {
                    state.quiet_wait.store(1u, std::memory_order_release);
                }
                else
                {
                    //  Force publication before signalling: this wait uses the
                    //  old epoch and must return before the quiet iteration.
                    if (!tests::wait_until(context.perf_count_conversion(), [&]
                        { return state.fanout_complete.load(std::memory_order_acquire) != 0u; }))
                    {
                        state.failed.store(1u);
                        break;
                    }
                }
                (void)context.wait_for_new_epoch(epoch);
                continue;
            }
            threading::CWaitDeadline wait;
            if (wait.set_after_milliseconds(100u).code != platform::threading::EWaitError::none)
            { state.failed.store(1u); break; }
            (void)context.wait_for_new_epoch(epoch, wait);
        }
        context.mark_exiting();
        context.mark_exited();
        return 0u;
    }

    static void installed_subscription(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init() && runtime.m_thread_packages.initialise());
        SSubscriber state;
        const threading::ThreadConfig config{ thread_ids::executive, module_ids::executive,
            platform::threading::EThreadPriority::Normal, &subscriber, nullptr, &state };
        const auto index = module_ids::executive_index.raw_value();
        const auto slot = runtime.m_thread_packages.emplace(config, runtime.m_perf_count_conversion);
        auto* package = runtime.m_thread_packages.get_object(slot);
        TEST_EXPECT(ctx, package && runtime.m_clients[index].emplace(module_ids::executive, 29u, package));
        if (!package || !runtime.m_clients[index]) return;
        auto& client = *runtime.m_clients[index];
        runtime.m_module_threads[index].slot = slot;
        TEST_EXPECT(ctx, client.install_host_execution(runtime.m_maintenance.restrictions().reader()) && package->startup());
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.ready.load(std::memory_order_acquire) != 0u; }));
        auto& restriction = runtime.m_maintenance.restrictions();
        TEST_EXPECT(ctx, restriction.begin_invocation({ 30u }, true));
        const auto scope = restriction.enter({ 5u });
        TEST_EXPECT(ctx, scope.status == modules::ERestrictionStatus::success);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.restricted.load(std::memory_order_acquire) != 0u; }));
        runtime.m_maintenance.fanout();
        state.fanout_complete.store(1u, std::memory_order_release);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            return (state.quiet_wait.load(std::memory_order_acquire) != 0u) &&
                (package->query_state() == threading::EThreadRunState::Waiting);
        }));
        const auto observations = state.restricted_observations.load();
        TEST_EXPECT(ctx, observations == 2u);
        std::atomic<std::uint32_t> unchanged{ 0u };
        threading::CWaitDeadline pause;
        TEST_EXPECT(ctx, pause.set_after_milliseconds(20u).code == platform::threading::EWaitError::none);
        TEST_EXPECT(ctx, platform::threading::wait_until_not_equal(unchanged, 0u, pause).status == threading::EWaitStatus::timed_out);
        TEST_EXPECT(ctx, state.restricted_observations.load() == observations);
        TEST_EXPECT(ctx, restriction.leave(scope.scope) == modules::ERestrictionStatus::success && restriction.end_invocation());
        runtime.m_maintenance.fanout();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.resumed.load(std::memory_order_acquire) != 0u; }));
        client.request_exit();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return package->query_state() == threading::EThreadRunState::Exited; }));
        client.terminal_before_drain = client.incoming_drained = true;
        TEST_EXPECT(ctx, !package->shutdown());
        threading::SThreadPackageTestAccess::fail_join(*package);
        TEST_EXPECT(ctx, !client.retire_host_execution(false) && client.subscription_live &&
            threading::SThreadPackageTestAccess::retained(*package));
        threading::SThreadPackageTestAccess::fail_join(*package, false);
        TEST_EXPECT(ctx, client.retire_host_execution(false) && package->shutdown() && !state.failed.load());
        TEST_EXPECT(ctx, runtime.shutdown());
    }
};

}   //  namespace host

void run_host_maintenance_tests(tests::TTestContext& ctx)
{
    host::SHostMaintenanceTestAccess::bounds(ctx);
    host::SHostMaintenanceTestAccess::deferred_retirement(ctx);
    host::SHostMaintenanceTestAccess::prefix_staging(ctx);
    host::SHostMaintenanceTestAccess::installed_subscription(ctx);
    host::SHostMaintenanceTestAccess::startup_rollback(ctx);
    host::SHostMaintenanceTestAccess::executive_startup_cleanup(ctx);
    host::SHostMaintenanceTestAccess::failed_start_join(ctx);
    host::SHostMaintenanceTestAccess::invalid_outer_prefix(ctx);
}

void prepare_platform_test_binding(modules::CBoundModule& binding) noexcept
{
    modules::SBoundModuleTestAccess::prepare(binding);
}
