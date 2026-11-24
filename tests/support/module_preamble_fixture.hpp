
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_preamble_fixture.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    24 Nov 26
//
//  Controlled Host setup and allocation observations shared by execution tests.

#pragma once

#include "host/runtime/host.hpp"
#include "host/runtime/execution/module_preamble.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/support/test_allocator.hpp"
#include "platform/threading/native_thread_id.hpp"
#include <atomic>

namespace modules
{
struct SBoundModuleTestAccess
{
    static EBindingResult MV_STD_ABI_CALL thread(const thread_ids::id_type) noexcept { return EBindingResult::success; }
    static EBindingResult MV_STD_ABI_CALL provision(void* const) noexcept { return EBindingResult::success; }
    static EBindingResult MV_STD_ABI_CALL memory(memory::CMemoryContext* const) noexcept { return EBindingResult::success; }
    static void install(CBoundModule& binding) noexcept
    {
        binding.m_installed = true;
        binding.m_core.install_ambient_thread_id = &thread;
        binding.m_core.install_thread_provisioning = &provision;
        binding.m_core.install_thread_memory_context = &memory;
    }
    static void identity(CBoundModule& binding, const module_ids::id_type id) noexcept
    {
        binding.m_advertised_module_identity.advertised_module_id = id;
    }
};
}

namespace host
{
struct SModulePreambleTestAccess
{
    static SClientRecord* driver(CHost& host, const module_ids::id_type id, modules::CBoundModule& binding,
        const modules::SExecutionV1& execution) noexcept
    {
        TInstance<SModuleEndpoints>& owned = host.m_modules[module_ids::ops::decode_id(id).raw_value()];
        if (!owned.emplace()) return nullptr;
        SModuleEndpoints& module = *owned;
        module.binding = &binding;
        module.state.instance = { host.allocate_identity() };
        if (!module.threads.initialise() || !module.coordinator.emplace(id, host.allocate_identity(), nullptr, module.state.instance))
        {
            return nullptr;
        }
        SClientRecord& client = *module.coordinator;
        client.owner_wake = host.m_owner_wait.endpoint();
        client.endpoint->module_state = &module.state;
        if (!module.pump.emplace(client, binding, execution, host.m_maintenance) || !module.pump->initialise())
        {
            return nullptr;
        }
        module.pump->activate();
        return &client;
    }
    //  Inject the worker's completed binding boundary, then use the actual
    //  pending activation/retirement path. No native loader is exercised here.
    static void pending(CHost& host, modules::CBoundModule& binding, const modules::SExecutionV1& execution,
        const module_ids::id_type id, const bool replacement) noexcept
    {
        CModuleService& service = host.m_module_service;
        service.m_pending = service.m_start_pending = true;
        service.m_purpose = (id == module_ids::executive) ?
            (replacement ? CModuleService::EPurpose::executive_change : CModuleService::EPurpose::bootstrap) : CModuleService::EPurpose::client;
        service.m_work.next = &binding;
        service.m_work.execution = execution;
        service.m_request_owner = CErasedOwner::create<modules::ModuleRequest>();
        modules::ModuleRequest& request = *service.m_request_owner.payload<modules::ModuleRequest>();
        request.module = id;
        request.action = replacement ? modules::EModuleAction::replace : modules::EModuleAction::load;
        service.m_work.request = &request;
        host.m_phase = replacement ? CHost::EPhase::replacing_executive : CHost::EPhase::starting;
        host.start_pending_module();
    }
    static void pump(CHost& host) noexcept { host.pump_clients(); }
    static void pump_one(CHost& host, const module_ids::id_type id) noexcept { host.module_endpoints(id)->pump->pump(); }
    static bool emergency(const CHost& host) noexcept
    {
        return host.m_emergency_shutdown && host.m_runtime_failed && host.m_work_admission_closed;
    }
    static void replace(CHost& host, const module_ids::id_type id) noexcept
    {
        host.stop_module(*host.module_endpoints(id), modules::EExecutionStop::replacement);
    }
    static bool retire(CHost& host, const module_ids::id_type id) noexcept
    {
        return host.stop_module_thread(module_ids::ops::get_mount_point_id(id));
    }
    static void finish_pending(CHost& host) noexcept
    {
        host.m_module_service.cancel_pending();
        modules::ModuleResult result;
        CModuleService::EPurpose purpose;
        (void)host.m_module_service.take_internal_result(result, purpose);
    }
    static bool prepare_turns(CHost& host) noexcept
    {
        if (!host.m_perf_count_conversion.init() || !host.m_thread_packages.initialise())
        {
            return false;
        }
        const threading::ThreadConfig config{ thread_ids::bg_file_io, module_ids::executable,
            platform::threading::EThreadPriority::Normal, nullptr };
        host.m_worker_count = 1u;
        host.m_worker_slots[0] = host.m_thread_packages.emplace(config, host.m_perf_count_conversion);
        host.m_phase = CHost::EPhase::running;
        // No native worker/job is launched. A retained result must never reach it.
        return host.worker_package(0u) != nullptr;
    }
    static void completed_replacement(CHost& host, SClientRecord& requester, modules::CBoundModule& binding,
        const module_ids::id_type target) noexcept
    {
        CModuleService& service = host.m_module_service;
        service.m_request_owner = CErasedOwner::create<modules::ModuleRequest>();
        modules::ModuleRequest& request = *service.m_request_owner.payload<modules::ModuleRequest>();
        request.module = target;
        request.action = modules::EModuleAction::replace;
        service.m_work.request = &request;
        service.m_work.previous = service.m_work.next = &binding;
        service.m_client = &requester;
        service.m_client_slot = 93;
        service.m_pending = true;
        service.finish(modules::EModuleStatus::success);
    }
    static void turn(CHost& host) noexcept { host.run_turn(); }
    static bool complete(const CHost& host) noexcept { return host.m_phase == CHost::EPhase::complete; }
    static bool retaining(const CHost& host) noexcept { return host.m_retain_runtime; }
    static void lifecycle(CHost& host) noexcept { host.advance_lifecycle(); }
    static void pending_unload(CHost& host, SClientRecord& requester, modules::CBoundModule& binding,
        const module_ids::id_type target) noexcept
    {
        CModuleService& service = host.m_module_service;
        service.m_request_owner = CErasedOwner::create<modules::ModuleRequest>();
        modules::ModuleRequest& request = *service.m_request_owner.payload<modules::ModuleRequest>();
        request.module = target;
        request.action = modules::EModuleAction::unload;
        service.m_work.request = &request;
        service.m_work.previous = &binding;
        service.m_client = &requester;
        service.m_client_slot = 94;
        service.m_pending = true;
    }
    static CModuleService& module_service(CHost& host) noexcept { return host.m_module_service; }
    static SClientRecord* install(CHost& host, const module_ids::id_type id, modules::CBoundModule& binding,
        const std::uint32_t fallback_limit = 3u, const modules::SExecutionV1 execution = {},
        void* const creation_context = nullptr) noexcept
    {
        TInstance<SModuleEndpoints>& owned = host.m_modules[module_ids::ops::decode_id(id).raw_value()];
        if (!owned.emplace()) return nullptr;
        SModuleEndpoints& module = *owned;
        module.binding = &binding;
        module.state.instance = { host.allocate_identity() };
        if (!module.coordinator.emplace(id, host.allocate_identity(), nullptr, module.state.instance))
        {
            return nullptr;
        }
        SClientRecord& client = *module.coordinator;
        client.owner_wake = host.m_owner_wait.endpoint();
        client.endpoint->module_state = &module.state;
        if (!client.initialise_execution(binding, execution, creation_context, 32u, fallback_limit))
        {
            return nullptr;
        }
        client.phase = EClientPhase::ready;
        client.host_execution.restriction = host.m_maintenance.restrictions().reader();
        client.endpoint->host_execution = &client.host_execution;
        return &client;
    }
    // Controlled role/address interleavings use real common preambles and Host
    // routing, but deliberately drive every callback on the test owner thread.
    static SClientRecord* controlled_endpoint(CHost& host, const module_ids::id_type id, const thread_ids::id_type thread,
        modules::CBoundModule& binding, const modules::SExecutionV1& execution, void* const state) noexcept
    {
        TInstance<SModuleEndpoints>& owned = host.m_modules[module_ids::ops::decode_id(id).raw_value()];
        if (!owned)
        {
            if (!install(host, id, binding)) return nullptr;
        }
        SModuleEndpoints& module = *owned;
        SClientRecord* client = nullptr;
        if (thread == thread_ids::host)
        {
            if (module.coordinator && module.coordinator->preamble->context().creation_context)
            {
                return nullptr;
            }
            if (!module.coordinator.emplace(id, host.allocate_identity(), nullptr, module.state.instance))
            {
                return nullptr;
            }
            client = &*module.coordinator;
        }
        else
        {
            if (!module.threads.is_ready() && !module.threads.initialise())
            {
                return nullptr;
            }
            const std::int32_t slot = module.threads.emplace();
            if (slot < 0) return nullptr;
            SRequestedThread& attempt = *module.threads.get_object(slot);
            attempt.thread = thread;
            attempt.identity = { host.allocate_identity() };
            if (!attempt.package.emplace(threading::ThreadConfig{ thread, id }, host.m_perf_count_conversion) ||
                !attempt.client.emplace(id, attempt.identity.value, &*attempt.package, module.state.instance))
            {
                return nullptr;
            }
            threading::SDirectConnectionTestAccess::prepare_controlled(*attempt.package);
            client = &*attempt.client;
        }
        client->owner_wake = host.m_owner_wait.endpoint();
        client->endpoint->module_state = &module.state;
        client->host_execution.restriction = host.m_maintenance.restrictions().reader();
        client->endpoint->host_execution = &client->host_execution;
        if (!client->initialise_execution(binding, execution, state, 32u, 3u))
        {
            return nullptr;
        }
        return client;
    }
    static bool drain(CHost& host) noexcept
    {
        host.m_turn_pending = false;
        host.drain_module_inputs();
        return host.m_turn_pending;
    }
    static bool detach_coordinator(CHost& host, SClientRecord& client, TInstance<SClientRecord>& retained) noexcept
    {
        SModuleEndpoints* const module = host.module_endpoints(client.module);
        if (!module || (&*module->coordinator != &client) || !client.preamble->destroyed() ||
            !host.m_direct_connections.client_idle(client) || !client.replies_idle())
        {
            return false;
        }
        client.phase = EClientPhase::stopped;
        client.terminal_before_drain = client.incoming_drained = true;
        retained = std::move(module->coordinator);
        return true;
    }
    static CServiceRouter& router(CHost& host) noexcept { return host.m_service_router; }
    static CAssetService& assets(CHost& host) noexcept { return host.m_asset_service; }
    static CDirectConnections& connections(CHost& host) noexcept { return host.m_direct_connections; }
    static bool prepare_threads(CHost& host) noexcept { return host.m_perf_count_conversion.init(); }
    static bool start_batch(CHost& host) noexcept { return host.m_batch_pool.start(2u) && host.m_asset_service.initialise(); }
    static bool stop_batch(CHost& host) noexcept { return host.m_batch_pool.stop(); }
    static void wake(CHost& host, const system_ids::id_type address) noexcept
    {
        SClientRecord* const client = host.endpoint_client(address);
        if (client) client->endpoint->signal_consumer();
    }
    static SClientRecord* endpoint(CHost& host, const system_ids::id_type address) noexcept { return host.endpoint_client(address); }
    static bool prepare_batch(CHost& host, SClientRecord& client, modules::CBoundModule& binding) noexcept
    {
        if (!client.endpoint->batch_owner.emplace(host.m_batch_pool.shared(), client.endpoint->control_state,
            client.endpoint->work_pending, client.module, &binding))
        {
            return false;
        }
        client.endpoint->batch_client = &*client.endpoint->batch_owner;
        return true;
    }
    static void progress_threads(CHost& host) noexcept
    {
        host.drain_module_inputs();
        for (SClientRecord* client = host.first_client(); client; client = host.next_client(*client)) host.drain_client(*client);
        for (TInstance<SModuleEndpoints>& module : host.m_modules)
        {
            if (module) host.progress_threads(*module);
        }
    }
    //  Controlled domain-completion fixtures own ordinary ingress explicitly;
    //  private lifecycle, native join and retirement still use Host's paths.
    static void progress_controls(CHost& host) noexcept
    {
        std::uint32_t count = 0u;
        (void)host.drain_module_controls(count);
        for (SClientRecord* client = host.first_client(); client; client = host.next_client(*client)) host.drain_client(*client);
        for (TInstance<SModuleEndpoints>& module : host.m_modules)
        {
            if (module) host.progress_threads(*module);
        }
        host.m_direct_connections.progress();
    }
    static bool children_idle(CHost& host, const module_ids::id_type id) noexcept
    {
        return host.module_endpoints(id)->threads.is_empty();
    }
    static void failed_child(CHost& host, SClientRecord& requester, const modules::SChildExecution& request) noexcept
    {
        host.m_test_before_client_start = [](threading::CThreadPackage& package)
        {
            threading::SDirectConnectionTestAccess::reject_create(package);
        };
        host.create_child(requester, request, 81);
    }
    static void refuse_child_starts(CHost& host) noexcept
    {
        host.m_test_before_client_start = [](threading::CThreadPackage& package)
        {
            threading::SDirectConnectionTestAccess::reject_create(package);
        };
    }
    static void clear(CHost& host) noexcept
    {
        for (TInstance<SModuleEndpoints>& module : host.m_modules)
        {
            if (module && module->coordinator) host.m_asset_service.close_report_provider(*module->coordinator);
            module.reset();
        }
        host.m_phase = CHost::EPhase::complete;
    }
};
}

namespace tests
{

struct SDllAllocation
{
    platform::threading::CPlatformThreadId allocated_on, freed_on;
    void* primary{ nullptr };
    bool track_primary{ true };
    std::atomic<unsigned> allocations{ 0u }, frees{ 0u };
    static void* MV_STD_ABI_CALL allocate(void* const cookie, const std::size_t alignment, const std::size_t bytes) noexcept
    {
        SDllAllocation& self = *static_cast<SDllAllocation*>(cookie);
        void* const pointer = tests::allocate_scalar_test_memory(nullptr, alignment, bytes);
        const unsigned index = self.allocations.fetch_add(1u);
        if (self.track_primary && (index == 0u))
        {
            self.allocated_on = platform::threading::query_current_thread_id();
            self.primary = pointer;
        }
        return pointer;
    }
    static bool MV_STD_ABI_CALL free(void* const cookie, const std::size_t alignment, void* const pointer) noexcept
    {
        SDllAllocation& self = *static_cast<SDllAllocation*>(cookie);
        if (self.track_primary && (pointer == self.primary)) self.freed_on = platform::threading::query_current_thread_id();
        ++self.frees;
        return tests::deallocate_scalar_test_memory(nullptr, alignment, pointer);
    }
};

}   // namespace tests
