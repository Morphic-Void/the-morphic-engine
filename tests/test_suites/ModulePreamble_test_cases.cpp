
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ModulePreamble_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    22 Nov 26
//
//  Execution boundary, bounded pipe storage and persistent input fairness.

#include "host/runtime/execution/module_lifecycle.hpp"
#include "host/runtime/execution/module_preamble.hpp"
#include "host/runtime/execution/pressure_selection.hpp"
#include "host/runtime/execution/retained_replies.hpp"
#include "host/runtime/execution/output_retry_turn.hpp"
#include "host/threading/CThreadPackage.hpp"
#include "host/runtime/host.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/support/endpoint_fixture.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "framework/services/service_protocol.hpp"
#include "framework/services/provider_client.hpp"
#include "framework/communications/module_tests.hpp"
#include "foundation/platform/threading/thread_lifetime.hpp"
#include "foundation/debug/service.hpp"
#include "executive/runtime/execution_timing.hpp"
#include "tests/module_lifecycle/service_execution.hpp"
#include "tests/module_lifecycle/executive_execution.hpp"
#include "tests/host_pump/fixture_types.hpp"
#include "tests/host_pump/executive_execution.hpp"
#include "tests/input_host/source_execution.hpp"
#include "tests/input_host/borrow_execution.hpp"
#include "tests/input_host/execution.hpp"

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

namespace preamble_test_types
{
struct SLocalPod { std::uint32_t value; };
}

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
MV_REGISTER_LOCAL_TYPE(preamble_test_types::SLocalPod,
    local_type_ids::ops::encode_id(local_type_ids::ops::encode_index(1235u)));

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

namespace
{
void lifecycle_service_obligations(tests::TTestContext& ctx);
void lifecycle_rendering_acquisition(tests::TTestContext& ctx);
void lifecycle_rendering_conversation(tests::TTestContext& ctx);
void lifecycle_executive_requests(tests::TTestContext& ctx);
void lifecycle_save_requests(tests::TTestContext& ctx);
void lifecycle_bundle_requests(tests::TTestContext& ctx);
void lifecycle_audio_requests(tests::TTestContext& ctx);
void pump_credit_custody(tests::TTestContext& ctx);
void pump_borrow_custody(tests::TTestContext& ctx);
void pump_close_retry(tests::TTestContext& ctx);
void pump_notification_custody(tests::TTestContext& ctx);
void pump_modal_custody(tests::TTestContext& ctx);
void pump_schema_custody(tests::TTestContext& ctx);
void pump_platform_shell(tests::TTestContext& ctx);
void pump_report_custody(tests::TTestContext& ctx);
void pump_report_injected(tests::TTestContext& ctx);
void pump_native_custody(tests::TTestContext& ctx);
void input_source_custody(tests::TTestContext& ctx);
void input_borrow_custody(tests::TTestContext& ctx);
void input_installation_cleanup(tests::TTestContext& ctx);
void input_freeze_cleanup(tests::TTestContext& ctx);
void input_secondary_cleanup(tests::TTestContext& ctx);
void input_unexpected_close(tests::TTestContext& ctx);

struct SProviderFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    threading::CErasedPodMsg published;
    unsigned submissions{ 0u }, publications{ 0u }, failures{ 0u };
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SProviderFacade& self = *static_cast<SProviderFacade*>(cookie);
        if (destination.kind != modules::SDestination::EKind::host)
        {
            return modules::ESubmission::invalid;
        }
        ++self.submissions;
        if (modules::accepted(self.result))
        {
            self.published = message;
            ++self.publications;
        }
        return self.result;
    }
    static modules::ESubmission MV_STD_ABI_CALL intent(void* const cookie, const modules::SExecutionIntent& request) noexcept
    {
        SProviderFacade& self = *static_cast<SProviderFacade*>(cookie);
        if (request.kind == modules::SExecutionIntent::EKind::fail)
        {
            ++self.failures;
            return modules::ESubmission::accepted;
        }
        return modules::ESubmission::invalid;
    }
};

void provider_facade_requests(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SProviderFacade::pod, nullptr, &SProviderFacade::intent };
    for (const modules::ESubmission result : { modules::ESubmission::accepted, modules::ESubmission::published_wake_failed })
    {
        SProviderFacade state;
        modules::CCommunication communication{ functions, &state };
        services::CProviderClient provider{ communication };
        services::SServiceBorrowOffer offer;
        offer.route = { 1u };
        offer.request = { 2u };
        services::SServiceBorrow borrow;
        for (unsigned attempt = 0u; attempt < 3u; ++attempt)
        {
            TEST_EXPECT(ctx, (provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::busy));
            TEST_EXPECT(ctx, (provider.idle() && !provider.preparing(offer) && (state.publications == 0u)));
        }
        state.result = result;
        TEST_EXPECT(ctx, (provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::busy));
        TEST_EXPECT(ctx, (!provider.idle() && provider.preparing(offer) && (state.publications == 1u)));
        const unsigned submissions = state.submissions;
        TEST_EXPECT(ctx, (provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::busy));
        TEST_EXPECT(ctx, (state.submissions == submissions));
        services::ProviderOperationResult completed;
        completed.status = services::EServiceStatus::success;
        completed.borrow = { 3u };
        TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(state.published.query_async_slot(), completed)));
        TEST_EXPECT(ctx, ((provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::success) &&
            (borrow.value == 3u) && provider.idle() && (state.publications == 1u)));
        state.result = modules::ESubmission::closed;
        TEST_EXPECT(ctx, ((provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::closing) && provider.idle()));
        state.result = modules::ESubmission::invalid;
        TEST_EXPECT(ctx, ((provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::delivery_failed) && provider.idle()));
        TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(-1, completed)));
        TEST_EXPECT(ctx, (state.failures == 1u));
    }
}

void provider_pending_lifetimes(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SProviderFacade::pod, nullptr, &SProviderFacade::intent };
    SProviderFacade state;
    state.result = modules::ESubmission::accepted;
    const modules::CCommunication communication{ functions, &state };
    services::CProviderClient provider{ communication };
    threading::CErasedPodMsg published[256];
    services::ServiceReply completion;
    completion.route = { 1u };
    completion.status = services::EServiceStatus::success;
    for (std::uint64_t request = 1u; request <= 257u; ++request)
    {
        completion.request = { request };
        TEST_EXPECT(ctx, (provider.complete(&completion) == services::EServiceStatus::busy));
        if (request <= 256u) published[request - 1u] = state.published;
    }
    TEST_EXPECT(ctx, ((state.submissions == 256u) && (state.publications == 256u)));
    services::ProviderOperationResult result;
    result.status = services::EServiceStatus::success;
    for (std::uint64_t request = 1u; request <= 256u; ++request)
    {
        TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(published[request - 1u].query_async_slot(), result)));
        TEST_EXPECT(ctx, !provider.idle());
        completion.request = { request };
        TEST_EXPECT(ctx, (provider.complete(&completion) == services::EServiceStatus::success));
        TEST_EXPECT(ctx, (provider.idle() == (request == 256u)));
    }
    TEST_EXPECT(ctx, (state.submissions == 256u));
    threading::SNotificationEndpoint notification;
    TEST_EXPECT(ctx, (provider.bind_borrow_notification({ 3u }, { 4u }, &notification) == services::EServiceStatus::busy));
    TEST_EXPECT(ctx, (!provider.retire_bindings({ 3u }) && !provider.idle()));
    TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(state.published.query_async_slot(), result)));
    TEST_EXPECT(ctx, !provider.idle());
    TEST_EXPECT(ctx, (provider.retire_bindings({ 3u }) && provider.idle() && (state.submissions == 257u)));
}

void pressure_diagnostics(tests::TTestContext& ctx)
{
    using Diagnostic = host::CPressureDiagnostic;
    using Report = Diagnostic::EReport;
    Diagnostic pressure;
    constexpr std::uint64_t second = 1000000000u;
    TEST_EXPECT(ctx, (pressure.observe(0u, true, 0u).kind == Report::none));
    TEST_EXPECT(ctx, (pressure.observe((second - 1u), false, 1u).kind == Report::none));
    TEST_EXPECT(ctx, (pressure.observe(second, true, 1u).kind == Report::none));
    std::uint64_t wait = UINT64_MAX;
    pressure.limit_wait(second, wait);
    TEST_EXPECT(ctx, (wait == second));
    const Diagnostic::SReport first = pressure.observe((second + second), true, 2u);
    TEST_EXPECT(ctx, ((first.kind == Report::warning) && (first.duration_ns == second) && (first.progress == 1u)));
    for (std::uint64_t tick = 1u; tick < 5000u; ++tick)
    {
        TEST_EXPECT(ctx, (pressure.observe(((second + second) + (tick * 1000000u)), true, 2u).kind == Report::none));
    }
    TEST_EXPECT(ctx, (pressure.observe((7u * second), true, 3u).kind == Report::warning));
    const Diagnostic::SReport recovery = pressure.observe((8u * second), false, 4u);
    TEST_EXPECT(ctx, ((recovery.kind == Report::recovery) && (recovery.progress == 3u)));
    TEST_EXPECT(ctx, (pressure.observe((8u * second), true, 4u).kind == Report::none));
    TEST_EXPECT(ctx, (pressure.observe((10u * second), true, 4u).kind == Report::none));
    TEST_EXPECT(ctx, (pressure.observe((12u * second), true, 4u).kind == Report::warning));
    TEST_EXPECT(ctx, (pressure.observe((12u * second), false, 4u).kind == Report::recovery));
    wait = UINT64_MAX;
    pressure.limit_wait((12u * second), wait);
    TEST_EXPECT(ctx, (wait == UINT64_MAX));

    Diagnostic starvation;
    TEST_EXPECT(ctx, (starvation.observe(0u, true, 0u, true).kind == Report::none));
    TEST_EXPECT(ctx, (starvation.observe(second, true, 1u, true).kind == Report::none));
    TEST_EXPECT(ctx, (starvation.observe((second + second), true, 1u, true).kind == Report::warning));
    TEST_EXPECT(ctx, (starvation.observe((3u * second), true, 2u, true).kind == Report::recovery));
    TEST_EXPECT(ctx, (starvation.observe((4u * second), false, 2u, true).kind == Report::none));
    //  Blocked/paused sources are not eligible-source starvation, and a retired
    //  identity does not transfer its wait age into the replacement's record.
    starvation = {};
    TEST_EXPECT(ctx, (starvation.observe((20u * second), false, 0u, true).kind == Report::none));
    TEST_EXPECT(ctx, (starvation.observe((21u * second), true, 0u, true).kind == Report::none));
}

void executive_timing_policy(tests::TTestContext& ctx)
{
    struct SClock
    {
        std::uint64_t now{ 100u };
        bool failed{ false };
        static platform::threading::WaitError read(void* const cookie, std::uint64_t& time) noexcept
        {
            SClock& clock = *static_cast<SClock*>(cookie);
            time = clock.now;
            return clock.failed ? platform::threading::WaitError{ platform::threading::EWaitError::invalid_argument, 0u } :
                platform::threading::WaitError{};
        }
    } clock;
    using Status = modules::EBudgetStatus;
    using Event = executive::CTimingAvailability::EEvent;
    const platform::threading::SAwakeClock awake{ &clock, &SClock::read };
    modules::CHostRestrictionPublication publication;
    modules::SHostRestrictionSnapshot snapshot;
    snapshot.generation = 1u;
    TEST_EXPECT(ctx, publication.publish(snapshot));
    executive::TExecutionBudget<> budget{ awake };
    executive::CTimingAvailability availability{ awake };
    TEST_EXPECT(ctx, (budget.start(publication.reader()) == Status::running));
    const modules::SEligibleBudgetAnchor original = budget.anchor();
    TEST_EXPECT(ctx, (budget.publishable() && (original.awake_start_ns == clock.now)));
    snapshot.sampling = 1u;
    TEST_EXPECT(ctx, publication.publish(snapshot));
    TEST_EXPECT(ctx, (budget.observe().status == Status::retry));
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::none));
    clock.now += UINT64_C(1000000000);
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::warning));
    //  A domain completion and next-phase start are allowed during retry. The
    //  new publication is held, and neither transition erases the old anchor.
    TEST_EXPECT(ctx, (budget.start(publication.reader()) == Status::retry));
    TEST_EXPECT(ctx, (!budget.publishable() && budget.starting() && (budget.anchor().awake_start_ns == original.awake_start_ns)));
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::none));
    for (unsigned phase = 0u; phase < 29u; ++phase)
    {
        clock.now += UINT64_C(1000000000);
        TEST_EXPECT(ctx, (budget.start(publication.reader()) == Status::retry));
        TEST_EXPECT(ctx, (budget.anchor().awake_start_ns == original.awake_start_ns));
        const executive::CTimingAvailability::SObservation observed = availability.observe(Status::retry);
        TEST_EXPECT(ctx, (observed.event == ((phase == 28u) ? Event::failure :
            ((((phase + 2u) % 5u) == 1u) ? Event::warning : Event::none))));
    }
    snapshot.sampling = 0u;
    TEST_EXPECT(ctx, publication.publish(snapshot));
    TEST_EXPECT(ctx, (budget.observe().status == Status::running));
    TEST_EXPECT(ctx, (budget.publishable() && !budget.starting() && (budget.anchor().awake_start_ns == clock.now)));
    const executive::CTimingAvailability::SObservation recovery = availability.observe(Status::running);
    TEST_EXPECT(ctx, ((recovery.event == Event::recovery) && (recovery.duration_ns == executive::k_operation_stall_ns) &&
        !availability.retrying()));
    clock.now += 1u;
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::none));
    clock.now += 1u;
    TEST_EXPECT(ctx, ((availability.observe(Status::restricted).event == Event::none) && !availability.retrying()));
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::none));
    clock.failed = true;
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::failure));
    TEST_EXPECT(ctx, (availability.observe(Status::failed).event == Event::failure));
}

void pressure_and_fairness(tests::TTestContext& ctx)
{
    host::SPressureLane lanes[4];
    host::CPressureSelection selector{ lanes, 4u };
    lanes[0] = { 32u, 32u, 0u, 0u, true, false };
    lanes[1] = { 63u, 64u, 0u, 1u, true, false };
    lanes[2] = { 1u, 1024u, 0u, 0u, true, true };
    lanes[3] = { 0u, 32u, 0u, 1u, true, false }; //  Runnable retained head; its ring is empty.
    std::uint32_t last[4]{};
    std::uint32_t visits[4]{};
    for (std::uint32_t turn = 1u; turn <= 160u; ++turn)
    {
        const host::SPressureSelection selected = selector.select();
        TEST_EXPECT(ctx, (selected.lane < 4u));
        if (selected.lane >= 4u) return;
        if (turn == 1u) TEST_EXPECT(ctx, (selected.lane == 0u));
        ++visits[selected.lane];
        last[selected.lane] = turn;
        selector.served(selected.lane);
        //  Model an outer budget that permits only one message on every pump.
        selector.finish(selected, true);
        for (std::uint32_t lane = 0u; lane < 4u; ++lane)
        {
            TEST_EXPECT(ctx, ((turn - last[lane]) <= 36u));
        }
        if (turn == 80u)
        {
            lanes[0].occupied = 1u;
            lanes[1].occupied = 64u; //  Host pressure now leads.
        }
    }
    for (const std::uint32_t count : visits) TEST_EXPECT(ctx, (count != 0u));

    lanes[0].runnable = lanes[1].runnable = lanes[2].runnable = false;
    lanes[0].occupied = 32u; //  A full ring behind a blocked head is not work.
    const host::SPressureSelection retained = selector.select();
    TEST_EXPECT(ctx, (retained.lane == 3u));
    lanes[3].runnable = false;
    TEST_EXPECT(ctx, (selector.select().lane == UINT32_MAX));
    lanes[0].runnable = true; //  Storage/admission change restores eligibility.
    TEST_EXPECT(ctx, (selector.select().lane == 0u));

    host::SPressureLane equal[3];
    host::CPressureSelection rotating{ equal, 3u };
    equal[0] = equal[1] = equal[2] = { 32u, 32u, 0u, 0u, true, false };
    for (std::uint32_t turn = 0u; turn < 3u; ++turn)
    {
        const host::SPressureSelection selected = rotating.select();
        TEST_EXPECT(ctx, (selected.lane == turn));
        rotating.served(selected.lane);
        rotating.finish(selected, true);
    }

    //  Endpoint populations change inside a fixed address table. Remove a
    //  ranked slot before reuse; surviving identities keep their relative order.
    const std::uint32_t first_order = equal[0].order;
    const std::uint32_t last_order = equal[2].order;
    rotating.withdraw(1u);
    equal[1] = {};
    TEST_EXPECT(ctx, (equal[0].order < equal[2].order));
    TEST_EXPECT(ctx, (first_order < last_order));
    equal[1] = { 32u, 32u, 0u, 0u, true, false };
    (void)rotating.select();
    TEST_EXPECT(ctx, ((equal[1].order > equal[0].order) && (equal[1].order > equal[2].order)));
    rotating.withdraw(2u);
    rotating.withdraw(1u);
    TEST_EXPECT(ctx, (rotating.select().lane == 0u));
    rotating.withdraw(0u);
    TEST_EXPECT(ctx, (rotating.select().lane == UINT32_MAX));
    equal[2] = { 1u, 32u, 0u, 0u, true, false };
    TEST_EXPECT(ctx, (rotating.select().lane == 2u));
    TEST_EXPECT(ctx, (equal[2].order == 1u));
}

void bounded_storage(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator, system_ids::host };
    for (const std::uint32_t limit : { 3u, 32u, 1024u })
    {
        host::TModuleFifo<std::uint32_t> fifo{ context, limit };
        for (std::uint32_t index = 0u; index < limit; ++index)
        {
            TEST_EXPECT(ctx, fifo.ensure_free(1u));
            TEST_EXPECT(ctx, fifo.push(index));
            TEST_EXPECT(ctx, (fifo.capacity() <= limit));
        }
        TEST_EXPECT(ctx, !fifo.ensure_free(1u));
        TEST_EXPECT(ctx, !fifo.push(9999u));
        for (std::uint32_t index = 0u; index < limit; ++index)
        {
            std::uint32_t value = UINT32_MAX;
            TEST_EXPECT(ctx, (fifo.pop(value) && (value == index)));
        }
        TEST_EXPECT(ctx, fifo.ensure_free(limit));
    }
    host::TModuleFifo<std::uint32_t> refused{ context };
    allocation.reject_all = true;
    TEST_EXPECT(ctx, !refused.ensure_free(1u));
    TEST_EXPECT(ctx, ((refused.size() == 0u) && (refused.capacity() == 0u)));
    allocation.reject_all = false;

    host::CModulePodPipe pipe{ module_ids::executive };
    TEST_EXPECT(ctx, (pipe.post({}) == host::EModulePipePost::invalid));
    TEST_EXPECT(ctx, (pipe.post(threading::make_pod_message(0, services::OpenServiceResult{})) == host::EModulePipePost::not_ready));
    TEST_EXPECT(ctx, !pipe.initialise(context, 33u));
    TEST_EXPECT(ctx, pipe.initialise(context, 32u));
    TEST_EXPECT(ctx, (pipe.post(threading::make_pod_message(0, preamble_test_types::SLocalPod{})) == host::EModulePipePost::invalid));
    TEST_EXPECT(ctx, (pipe.writable_count() == 32u));
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, (pipe.post(threading::make_pod_message(index, services::OpenServiceResult{})) == host::EModulePipePost::published));
    }
    TEST_EXPECT(ctx, ((pipe.readable_count() == 32u) && (pipe.writable_count() == 0u)));
    TEST_EXPECT(ctx, (pipe.post(threading::make_pod_message(32, services::OpenServiceResult{})) == host::EModulePipePost::full));
    TEST_EXPECT(ctx, (pipe.post({}) == host::EModulePipePost::invalid));
    for (std::int32_t index = 0; index < 32; ++index)
    {
        threading::CErasedPodMsg message;
        TEST_EXPECT(ctx, (pipe.read(message) && (message.query_async_slot() == index)));
    }
    TEST_EXPECT(ctx, (pipe.writable_count() == 32u));

    host::SModuleLifecycle lifecycle;
    TEST_EXPECT(ctx, lifecycle.initialise(context));
    for (std::uint32_t index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        host::SLifecycleMessage control;
        control.frontier.pods = index;
        TEST_EXPECT(ctx, lifecycle.to_preamble.post(control));
        TEST_EXPECT(ctx, lifecycle.to_host.post(control));
    }
    TEST_EXPECT(ctx, !lifecycle.to_preamble.post({}));
    TEST_EXPECT(ctx, !lifecycle.to_host.post({}));
    for (std::uint32_t index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        host::SLifecycleMessage control;
        TEST_EXPECT(ctx, (lifecycle.to_host.read(control) && (control.frontier.pods == index)));
        TEST_EXPECT(ctx, (lifecycle.to_preamble.read(control) && (control.frontier.pods == index)));
    }
}

void finite_output_turns(tests::TTestContext& ctx)
{
    host::COutputRetryTurn turn;
    constexpr std::uint32_t producers = host::COutputRetryTurn::k_producers;
    constexpr std::uint32_t ready = (1u << producers) - 1u;
    std::uint32_t operation[producers]{};
    std::uint32_t visits[producers][3]{};
    std::uint32_t admissions = 0u;
    for (std::uint32_t tick = 0u; tick < 1000u; ++tick)
    {
        const host::EOutputProducer selected = turn.next(ready);
        if (selected == host::EOutputProducer::count)
        {
            TEST_EXPECT(ctx, (turn.admission_due() && turn.admission_allowed()));
            ++admissions; //  A continually arriving fallback request can enter here.
            turn.admitted_or_examined();
            continue;
        }
        TEST_EXPECT(ctx, !turn.admission_allowed());
        const std::uint32_t allowance = turn.remaining();
        TEST_EXPECT(ctx, ((allowance > 0u) && (allowance <= host::COutputRetryTurn::k_allowance)));
        //  A capacity wake / exhausted outer budget cannot reset this allowance.
        TEST_EXPECT(ctx, (turn.next(ready) == selected));
        TEST_EXPECT(ctx, (turn.remaining() == allowance));
        const std::uint32_t producer = static_cast<std::uint32_t>(selected);
        ++visits[producer][operation[producer]];
        if (++operation[producer] == 3u) operation[producer] = 0u;
        turn.attempted(selected); //  One output per outer pump.
    }
    TEST_EXPECT(ctx, (admissions == 111u));
    for (std::uint32_t producer = 0u; producer < producers; ++producer)
    {
        for (const std::uint32_t count : visits[producer]) TEST_EXPECT(ctx, (count > (500u / (3u * producers))));
    }
    TEST_EXPECT(ctx, (turn.next(0u) == host::EOutputProducer::count));
    TEST_EXPECT(ctx, turn.admission_allowed()); //  Waiting on a later event cannot hold a pause.
    turn.admitted_or_examined();
}

void retained_reply_storage(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator, system_ids::host };
    const threading::CErasedPodMsg value = threading::make_pod_message(123, services::OpenServiceResult{});
    for (const std::uint32_t limit : { 3u, 1024u })
    {
        host::CModulePodPipe pipe{ module_ids::executive };
        host::CRetainedReplies replies{ context, limit };
        TEST_EXPECT(ctx, (replies.publish(pipe, value) == host::EReplyWrite::unavailable));
        TEST_EXPECT(ctx, pipe.initialise(context, 32u));
        for (std::int32_t index = 0; index < 32; ++index)
        {
            TEST_EXPECT(ctx, (replies.publish(pipe, threading::make_pod_message(index, services::OpenServiceResult{})) == host::EReplyWrite::published));
        }
        TEST_EXPECT(ctx, (replies.publish(pipe, value) == host::EReplyWrite::busy));
        TEST_EXPECT(ctx, ((replies.issued() == 32u) && (replies.published() == 32u)));
        {
            host::CRetainedReplies::CDispatch dispatch{ replies, limit };
            TEST_EXPECT(ctx, static_cast<bool>(dispatch));
            TEST_EXPECT(ctx, (replies.reservations() == limit));
            host::CRetainedReplies::CDispatch nested{ replies, 0u };
            TEST_EXPECT(ctx, !nested);
            for (std::uint32_t index = 0u; index < limit; ++index)
            {
                const std::int32_t correlation = static_cast<std::int32_t>(32u + index);
                TEST_EXPECT(ctx, (replies.retain(pipe, threading::make_pod_message(correlation, services::OpenServiceResult{})) == host::EReplyWrite::retained));
            }
            TEST_EXPECT(ctx, ((replies.size() == limit) && (replies.reservations() == 0u)));
        }
        {
            host::CRetainedReplies::CDispatch full{ replies, 1u };
            TEST_EXPECT(ctx, !full);
        }
        {
            host::CRetainedReplies::CDispatch zero_reply{ replies, 0u, false };
            TEST_EXPECT(ctx, static_cast<bool>(zero_reply));
        }
        TEST_EXPECT(ctx, (replies.publish(pipe, {}) == host::EReplyWrite::invalid));
        TEST_EXPECT(ctx, (replies.publish(pipe, value) == host::EReplyWrite::busy));
        TEST_EXPECT(ctx, (replies.issued() == (32u + limit)));
        std::int32_t expected = 0;
        threading::CErasedPodMsg message;
        while (replies.size())
        {
            TEST_EXPECT(ctx, pipe.read(message));
            TEST_EXPECT(ctx, (message.query_async_slot() == expected));
            ++expected;
            std::uint32_t published = UINT32_MAX;
            (void)replies.flush(pipe, 1u, published);
            TEST_EXPECT(ctx, (published == 1u));
        }
        while (pipe.read(message))
        {
            TEST_EXPECT(ctx, (message.query_async_slot() == expected));
            ++expected;
        }
        TEST_EXPECT(ctx, (static_cast<std::uint32_t>(expected) == (32u + limit)));
        TEST_EXPECT(ctx, (replies.idle() && (replies.issued() == replies.published())));
        {
            host::CRetainedReplies::CDispatch unused{ replies, limit };
            TEST_EXPECT(ctx, static_cast<bool>(unused));
            TEST_EXPECT(ctx, (replies.retain(pipe, {}) == host::EReplyWrite::invalid));
            TEST_EXPECT(ctx, (replies.reservations() == limit));
        }
        TEST_EXPECT(ctx, (replies.idle() && (replies.reservations() == 0u)));
        TEST_EXPECT(ctx, (replies.retain(pipe, value) == host::EReplyWrite::lost));
    }
    host::CRetainedReplies refused{ context, 3u };
    allocation.reject_all = true;
    {
        host::CRetainedReplies::CDispatch failed{ refused, 3u };
        TEST_EXPECT(ctx, !failed);
    }
    TEST_EXPECT(ctx, (refused.idle() && (refused.issued() == 0u)));
    allocation.reject_all = false;
    {
        host::CRetainedReplies::CDispatch paused{ refused, 1u, false };
        TEST_EXPECT(ctx, !paused);
    }
    {
        host::CRetainedReplies::CDispatch recovered{ refused, 3u };
        TEST_EXPECT(ctx, static_cast<bool>(recovered));
    }
    TEST_EXPECT(ctx, refused.idle());
}

modules::EExecutionStatus MV_STD_ABI_CALL create_state(const modules::SExecutionContext* const, void** const) noexcept
{
    return modules::EExecutionStatus::success;
}

void execution_boundary(tests::TTestContext& ctx)
{
    modules::SExecutionV1 execution;
    TEST_EXPECT(ctx, execution.is_valid()); //  No-child Host coordinator.
    execution.version = 2u;
    TEST_EXPECT(ctx, !execution.is_valid());
    execution.version = 1u;
    --execution.size;
    TEST_EXPECT(ctx, !execution.is_valid());
    execution.size = sizeof(execution);
    execution.flags = modules::k_execution_native_events;
    TEST_EXPECT(ctx, !execution.is_valid()); //  A coordinator cannot own native events.
    execution.flags = 0u;
    execution.create = &create_state;
    TEST_EXPECT(ctx, !execution.is_valid()); //  A partial callback set is never a coordinator.
    execution.create = nullptr;
    execution.flags = 2u;
    TEST_EXPECT(ctx, !execution.is_valid());
    const modules::CCommunication unbound;
    TEST_EXPECT(ctx, (unbound.submit({}, threading::CErasedPodMsg{}) == modules::ESubmission::invalid));
    TEST_EXPECT(ctx, modules::accepted(modules::ESubmission::published_wake_failed));
    TEST_EXPECT(ctx, !modules::accepted(modules::ESubmission::busy));
    TEST_EXPECT(ctx, !connections::SSource{ connections::SSource::EKind::self }.is_host());
}

struct SExecutionFixture
{
    const modules::SExecutionContext* context{ nullptr };
    modules::CCommunication copied;
    std::uint32_t self_count{ 0u }, host_count{ 0u }, async_count{ 0u }, admission_count{ 0u }, destroy_count{ 0u };
    bool stopping{ false }, pending{ false }, valid_sources{ true }, foreign_rejected{ false };
    std::atomic<bool> self_complete{ false };
    platform::threading::CPlatformThreadId created_on, destroyed_on;

    static std::uint32_t MV_STD_ABI_CALL foreign(void* const runtime) noexcept
    {
        SExecutionFixture& self = *static_cast<SExecutionFixture*>(runtime);
        self.foreign_rejected = self.copied.submit({}, threading::make_pod_message(42, services::OpenServiceResult{})) == modules::ESubmission::invalid;
        return 0u;
    }

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context, void** const runtime) noexcept
    {
        SExecutionFixture& self = *static_cast<SExecutionFixture*>(context->creation_context);
        self.context = context;
        self.created_on = platform::threading::query_current_thread_id();
        self.copied = *context->communication;
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime,
        const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
    {
        SExecutionFixture& self = *static_cast<SExecutionFixture*>(runtime);
        const modules::CCommunication& communication = *self.context->communication;
        const threading::CErasedPodMsg message = threading::make_pod_message(42, services::OpenServiceResult{});
        switch (invocation->event)
        {
            case modules::EExecutionEvent::start:
            {
                (void)communication.submit({ modules::SDestination::EKind::self }, message);
                for (std::uint32_t index = 0u; index < 32u; ++index)
                {
                    self.valid_sources = self.valid_sources && modules::accepted(communication.submit({}, message));
                }
                self.pending = communication.submit({}, message) == modules::ESubmission::busy;
                platform::threading::CThread worker;
                if (worker.create(&foreign, &self))
                {
                    self.valid_sources = self.valid_sources && worker.join_and_close();
                }
                else self.valid_sources = false;
                break;
            }
            case modules::EExecutionEvent::message:
            {
                if (invocation->source.kind == connections::SSource::EKind::self)
                {
                    ++self.self_count;
                    self.valid_sources = self.valid_sources && (invocation->source.address == self.context->address) &&
                        (invocation->source.endpoint.value == self.context->endpoint.value);
                    if (self.self_count < 100u)
                    {
                        self.valid_sources = self.valid_sources && modules::accepted(
                            communication.submit({ modules::SDestination::EKind::self }, message));
                    }
                    else
                    {
                        self.self_complete.store(true, std::memory_order_release);
                        (void)self.context->owner_wake.signal();
                    }
                }
                else
                {
                    ++self.host_count;
                    self.valid_sources = self.valid_sources && invocation->source.is_host() &&
                        (invocation->source.address == system_ids::host);
                }
                break;
            }
            case modules::EExecutionEvent::service_async:
            {
                ++self.async_count;
                if (self.pending)
                {
                    const modules::ESubmission posted = communication.submit({}, message);
                    if (modules::accepted(posted)) self.pending = false;
                }
                break;
            }
            case modules::EExecutionEvent::stop:
            {
                self.stopping = true;
                break;
            }
            case modules::EExecutionEvent::provider_admission_closed:
            {
                ++self.admission_count;
                break;
            }
            default:
            {
                break;
            }
        }
        *progress = {};
        progress->ready = 1u;
        progress->work_complete = self.stopping ? 1u : 0u;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        SExecutionFixture& self = *static_cast<SExecutionFixture*>(runtime);
        ++self.destroy_count;
        self.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
    static platform::threading::WaitError clock(void* const context, std::uint64_t& now) noexcept
    {
        now = ++*static_cast<std::uint64_t*>(context);
        return {};
    }
};

void common_dispatch_and_frontiers(tests::TTestContext& ctx)
{
    platform::threading::COwnerWait wake;
    TEST_EXPECT(ctx, (wake.initialise().code == platform::threading::EWaitError::none));
    threading::CEndpointResources resources{ module_ids::executable, module_ids::executive, thread_ids::host };
    resources.identity = { 581u };
    resources.instance = { 991u };
    resources.owner_wake = wake.endpoint();
    host::CModuleChannel channel{ resources };
    TEST_EXPECT(ctx, channel.initialise(*memory::get_ambient_memory_context(), 32u, 32u));
    TEST_EXPECT(ctx, resources.worker_to_host_owned_msgs.initialise(32u));
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &SExecutionFixture::create;
    execution.handle = &SExecutionFixture::handle;
    execution.destroy = &SExecutionFixture::destroy;
    SExecutionFixture state;
    std::uint64_t ticks = 0u;
    {
        host::CModulePreamble preamble{ resources, channel, binding, execution, *memory::get_ambient_memory_context(),
            &state, { &ticks, &SExecutionFixture::clock } };
        TEST_EXPECT(ctx, preamble.activate());
        preamble.process();
        TEST_EXPECT(ctx, ((state.self_count == 64u) && (state.async_count == 3u)));
        TEST_EXPECT(ctx, (state.foreign_rejected && state.valid_sources && state.pending));
        TEST_EXPECT(ctx, (resources.published_pods == 32u));
        TEST_EXPECT(ctx, (state.copied.submit({}, threading::make_pod_message(42, services::OpenServiceResult{})) == modules::ESubmission::invalid));
        preamble.process();
        TEST_EXPECT(ctx, ((state.self_count == 100u) && !preamble.work_pending()));
        std::uint64_t diagnostic_wait = UINT64_MAX;
        preamble.limit_wait(diagnostic_wait);
        TEST_EXPECT(ctx, ((diagnostic_wait != 0u) && (diagnostic_wait <= host::CPressureDiagnostic::k_threshold_ns)));
        ticks += host::CPressureDiagnostic::k_threshold_ns;
        TEST_EXPECT(ctx, preamble.work_pending()); //  A parked full pipe gets a bounded diagnostic turn.
        preamble.process(0u);
        TEST_EXPECT(ctx, (!preamble.failed() && state.pending && !preamble.work_pending()));
        diagnostic_wait = UINT64_MAX;
        preamble.limit_wait(diagnostic_wait);
        TEST_EXPECT(ctx, ((diagnostic_wait > host::CPressureDiagnostic::k_threshold_ns) &&
            (diagnostic_wait <= host::CPressureDiagnostic::k_repeat_ns)));
        threading::CErasedPodMsg message;
        TEST_EXPECT(ctx, channel.read_host_pod(message));
        TEST_EXPECT(ctx, preamble.work_pending());
        preamble.process(0u);
        TEST_EXPECT(ctx, (!state.pending && (resources.published_pods == 33u)));
        TEST_EXPECT(ctx, !preamble.work_pending());
        diagnostic_wait = UINT64_MAX;
        preamble.limit_wait(diagnostic_wait);
        TEST_EXPECT(ctx, (diagnostic_wait == UINT64_MAX));

        host::SLifecycleMessage control;
        while (channel.read_host_control(control)) {}
        preamble.request_stop(modules::EExecutionStop::requested);
        preamble.process();
        TEST_EXPECT(ctx, state.stopping);
        bool closed = false;
        while (channel.read_host_control(control))
        {
            if (control.operation == host::ELifecycle::close_provider_admission)
            {
                closed = true;
                TEST_EXPECT(ctx, (control.frontier.pods == 33u));
            }
            TEST_EXPECT(ctx, (control.operation != host::ELifecycle::seal));
        }
        TEST_EXPECT(ctx, closed);
        control = {};
        control.operation = host::ELifecycle::provider_admission_closed;
        control.frontier.host_output = 2u;
        TEST_EXPECT(ctx, channel.host_control(control));
        preamble.process();
        TEST_EXPECT(ctx, ((state.admission_count == 0u) && !preamble.destroyed() && !preamble.work_pending()));
        TEST_EXPECT(ctx, (channel.to_module.post(threading::make_pod_message(3, services::OpenServiceResult{})) == host::EModulePipePost::published));
        preamble.process();
        TEST_EXPECT(ctx, ((state.host_count == 1u) && (state.admission_count == 0u)));
        TEST_EXPECT(ctx, (channel.to_module.post(threading::make_pod_message(4, services::OpenServiceResult{})) == host::EModulePipePost::published));
        preamble.process();
        TEST_EXPECT(ctx, ((state.host_count == 2u) && (state.admission_count == 1u)));
        bool sealed = false;
        while (channel.read_host_control(control))
        {
            if (control.operation == host::ELifecycle::seal)
            {
                sealed = true;
                TEST_EXPECT(ctx, ((control.frontier.pods == 33u) && (control.frontier.owners == 0u)));
            }
        }
        TEST_EXPECT(ctx, (sealed && !preamble.destroyed()));
        control = {};
        control.operation = host::ELifecycle::sealed;
        TEST_EXPECT(ctx, channel.host_control(control));
        preamble.process();
        TEST_EXPECT(ctx, (preamble.destroyed() && (state.destroy_count == 1u) && !preamble.failed()));
    }
    TEST_EXPECT(ctx, (state.copied.submit({}, threading::make_pod_message(42, services::OpenServiceResult{})) == modules::ESubmission::invalid));
}

void host_retained_ingress(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    host::SClientRecord* const other = host::SModulePreambleTestAccess::install(runtime, module_ids::audio, binding);
    TEST_EXPECT(ctx, (client && other));
    if (!client || !other)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(index, services::OpenServiceResult{})));
    }
    for (std::int32_t index = 0; index < 3; ++index)
    {
        TEST_EXPECT(ctx, (client->channel->to_host.post(threading::make_pod_message(index,
            services::OpenServiceRequest{ module_ids::rendering, 1u, 1u, { 1u } })) == host::EModulePipePost::published));
    }
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, ((client->pending_replies->size() == 3u) && (client->accounted_pods == 3u)));
    TEST_EXPECT(ctx, ((client->reserved_replies == 0u) && (client->pending_replies->reservations() == 0u)));
    TEST_EXPECT(ctx, (client->channel->to_host.post(threading::make_pod_message(99,
        services::OpenServiceRequest{ module_ids::rendering, 1u, 1u, { 2u } })) == host::EModulePipePost::published));
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime));
    TEST_EXPECT(ctx, (client->pod_head_live && !client->channel->to_host.readable_count()));
    const threading::CErasedPodMsg settlement = threading::make_pod_message(-1, services::ReleaseServiceBorrow{});
    for (std::uint32_t index = 0u; index < 32u; ++index)
    {
        TEST_EXPECT(ctx, (client->channel->to_host.post(settlement) == host::EModulePipePost::published));
    }
    TEST_EXPECT(ctx, (other->channel->to_host.post(settlement) == host::EModulePipePost::published));
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime));
    TEST_EXPECT(ctx, ((client->channel->to_host.readable_count() == 32u) && (client->accounted_pods == 3u)));
    TEST_EXPECT(ctx, (other->accounted_pods == 1u));
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, client->channel->read_preamble_pod(message));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, ((client->accounted_pods == 36u) && !client->pod_head_live && !client->channel->to_host.readable_count()));
    TEST_EXPECT(ctx, ((client->pending_replies->size() == 3u) && (client->reserved_replies == 0u) &&
        (client->pending_replies->reservations() == 0u)));
    TEST_EXPECT(ctx, (client->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::none));
    host::SModulePreambleTestAccess::clear(runtime);
}

void fallback_continuation_and_terminal(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding, 16u);
    TEST_EXPECT(ctx, client);
    if (!client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(index, services::OpenServiceResult{})));
    }
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 12u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(12u)));
        for (std::int32_t index = 32; index < 44; ++index)
        {
            TEST_EXPECT(ctx, client->reply(threading::make_pod_message(index, services::OpenServiceResult{})));
        }
    }
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime)); //  Full is not runnable.
    threading::CErasedPodMsg message;
    while (client->channel->read_preamble_pod(message)) {}
    TEST_EXPECT(ctx, host::SModulePreambleTestAccess::drain(runtime)); //  Eight published, four remain runnable.
    TEST_EXPECT(ctx, ((client->channel->to_module.readable_count() == 8u) && (client->pending_replies->size() == 4u)));
    const std::uint64_t published = client->pending_replies->published();
    client->phase = host::EClientPhase::stopped;
    client->terminal_before_drain = true;
    client->incoming_drained = false;
    client->flush_replies();
    TEST_EXPECT(ctx, ((client->pending_replies->published() == published) &&
        (client->channel->to_module.readable_count() == 8u) && (client->pending_replies->size() == 4u)));
    client->incoming_drained = true;
    client->flush_replies();
    TEST_EXPECT(ctx, (client->pending_replies->idle() && (client->pending_replies->published() == published)));
    TEST_EXPECT(ctx, (client->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::none));
    host::SModulePreambleTestAccess::clear(runtime);
}

void host_private_frontiers(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    TEST_EXPECT(ctx, client);
    if (!client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(index, services::OpenServiceResult{})));
    }
    host::SLifecycleMessage close;
    close.operation = host::ELifecycle::close_provider_admission;
    close.frontier = { 1u, 0u, 0u };
    TEST_EXPECT(ctx, client->channel->preamble_control(close));
    close.operation = host::ELifecycle::seal;
    TEST_EXPECT(ctx, client->channel->preamble_control(close));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, (client->seal_pending && !client->provider_closed && !client->seal_acknowledged));
    TEST_EXPECT(ctx, !client->channel->lifecycle.to_preamble.readable_count());
    TEST_EXPECT(ctx, (client->channel->to_host.post(threading::make_pod_message(40,
        services::OpenServiceRequest{ module_ids::audio, 1u, 1u, { 1u } })) == host::EModulePipePost::published));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, ((client->accounted_pods == 1u) && client->seal_acknowledged && client->provider_closed));
    TEST_EXPECT(ctx, ((client->pending_replies->size() == 1u) && (client->pending_replies->issued() == 33u)));
    host::SLifecycleMessage result;
    TEST_EXPECT(ctx, client->channel->read_preamble_control(result));
    TEST_EXPECT(ctx, ((result.operation == host::ELifecycle::provider_admission_closed) && (result.frontier.host_output == 33u)));
    TEST_EXPECT(ctx, client->channel->read_preamble_control(result));
    TEST_EXPECT(ctx, (result.operation == host::ELifecycle::sealed));
    TEST_EXPECT(ctx, (client->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::none));
    host::SModulePreambleTestAccess::clear(runtime);
}

void private_connection_activation(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const first = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    host::SClientRecord* const second = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding);
    host::SClientRecord* const requester = host::SModulePreambleTestAccess::install(runtime, module_ids::audio, binding);
    TEST_EXPECT(ctx, (first && second && requester));
    if (!first || !second || !requester)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    host::CDirectConnections& connections = host::SModulePreambleTestAccess::connections(runtime);
    const auto progress = [&]()
    {
        (void)host::SModulePreambleTestAccess::drain(runtime);
        connections.progress();
    };
    const auto post = [&](host::SClientRecord& client, const host::ELifecycle operation, const connections::SToken token)
    {
        host::SLifecycleMessage control;
        control.operation = operation;
        control.correlation = 1;
        control.notification.connection = token;
        control.notification.connection_status = connections::EStatus::success;
        TEST_EXPECT(ctx, client.channel->preamble_control(control));
    };
    const auto read = [&](host::SClientRecord& client, const host::ELifecycle expected)
    {
        host::SLifecycleMessage control;
        TEST_EXPECT(ctx, client.channel->read_preamble_control(control));
        TEST_EXPECT(ctx, (control.operation == expected));
        return control;
    };
    post(*first, host::ELifecycle::participate, {});
    post(*second, host::ELifecycle::participate, {});
    progress();
    (void)read(*first, host::ELifecycle::participated);
    (void)read(*second, host::ELifecycle::participated);
    for (std::uint32_t attempt = 0u; attempt < 2u; ++attempt)
    {
        host::SLifecycleMessage request;
        request.operation = host::ELifecycle::connect;
        request.correlation = 8;
        request.first_address = system_ids::ops::make_system_id(first->module, thread_ids::host);
        request.second_address = system_ids::ops::make_system_id(second->module, thread_ids::host);
        TEST_EXPECT(ctx, requester->channel->preamble_control(request));
        progress();
        const connections::SToken token = read(*first, host::ELifecycle::offer).notification.connection;
        TEST_EXPECT(ctx, (read(*second, host::ELifecycle::offer).notification.connection.value == token.value));
        post(*first, host::ELifecycle::decision, token);
        post(*second, host::ELifecycle::decision, token);
        progress();
        const host::SDirectEndpointDescriptor* const descriptor = read(*first, host::ELifecycle::install).descriptor;
        (void)read(*second, host::ELifecycle::install);
        TEST_EXPECT(ctx, (descriptor && (descriptor->incoming->capacity() == 1024u) && (descriptor->outgoing->capacity() == 1024u)));
        post(*first, host::ELifecycle::installed, token);
        post(*second, host::ELifecycle::installed, token);
        progress();
        (void)read(*first, host::ELifecycle::activate);
        (void)read(*second, host::ELifecycle::activate);
        post(*first, host::ELifecycle::activated, token);
        progress();
        TEST_EXPECT(ctx, !requester->channel->lifecycle.to_preamble.readable_count());
        TEST_EXPECT(ctx, (descriptor && descriptor->admission->available()));
        if (attempt == 1u)
        {
            host::SDirectConnectionTestAccess::expire(connections, token, requester->host_execution.restriction);
            std::uint64_t remaining = UINT64_MAX;
            connections.limit_wait(remaining);
            TEST_EXPECT(ctx, (remaining == 0u));
            connections.progress();
            post(*second, host::ELifecycle::activated, token); //  Late after timeout cannot restore success.
            progress();
        }
        else
        {
            post(*second, host::ELifecycle::activated, token);
            progress();
        }
        const host::SLifecycleMessage result = read(*requester, host::ELifecycle::connection_result);
        TEST_EXPECT(ctx, (result.notification.connection_status ==
            ((attempt == 0u) ? connections::EStatus::success : connections::EStatus::timed_out)));
        if (attempt == 0u)
        {
            post(*first, host::ELifecycle::close, token);
            progress();
        }
        (void)read(*first, host::ELifecycle::closing);
        (void)read(*second, host::ELifecycle::closing);
        TEST_EXPECT(ctx, (descriptor && !descriptor->admission->available()));
        post(*first, host::ELifecycle::quiescent, token);
        post(*second, host::ELifecycle::quiescent, token);
        progress();
        (void)read(*first, host::ELifecycle::retired);
        (void)read(*second, host::ELifecycle::retired);
        TEST_EXPECT(ctx, (connections.empty() && !connections.failed()));
        TEST_EXPECT(ctx, !requester->channel->lifecycle.to_preamble.readable_count());
    }
    host::SModulePreambleTestAccess::clear(runtime);
}

class CPeerExecution
{
public:
    modules::CCommunication communication;
    system_ids::id_type address{}, peer{};
    connections::SToken token;
    connections::EStatus result{ connections::EStatus::busy };
    bool participated{ false }, participation_sent{ false }, request{ false }, requested{ false };
    bool active{ false }, pause_input{ false }, close{ false }, closing{ false }, hold{ false }, quiescent{ false };
    bool stopping{ false }, valid{ true }, send_on_available{ false };
    bool allow_host{ false };
    module_ids::id_type test_module{};
    std::atomic<bool> test_start{ false }, test_done{ false };
    bool test_sent{ false };
    const threading::CErasedPodMsg* outgoing{ nullptr };
    threading::CErasedPodMsg* observations{ nullptr };
    std::uint32_t observation_capacity{ 0u };
    platform::threading::CPlatformThreadId created_on, destroyed_on;
    std::uint32_t received{ 0u }, retired{ 0u }, send_count{ 0u }, destroyed{ 0u };

    void handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        using EEvent = modules::EExecutionEvent;
        using EKind = modules::SExecutionIntent::EKind;
        modules::SExecutionIntent intent;
        intent.connection = invocation.notification.connection;
        switch (invocation.event)
        {
            case EEvent::start:
            case EEvent::service_async:
            {
                if (!participation_sent)
                {
                    intent.kind = EKind::participate;
                    intent.correlation = 0;
                    participation_sent = modules::accepted(communication.submit(intent));
                }
                if (test_start.load(std::memory_order_acquire)) request = true;
                if (request && participated && !requested)
                {
                    intent.kind = EKind::connect;
                    intent.correlation = 1;
                    intent.first = address;
                    intent.second = peer;
                    requested = modules::accepted(communication.submit(intent));
                }
                if (active && module_ids::ops::is_valid_id(test_module) && !test_sent)
                {
                    test_sent = modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, token },
                        threading::make_pod_message(77, modules::RunTestsRequest{ test_module })));
                }
                if (active && outgoing)
                {
                    const modules::ESubmission status = communication.submit({ modules::SDestination::EKind::connection, {}, token }, *outgoing);
                    if (modules::accepted(status)) outgoing = nullptr;
                    else valid = valid && (status == modules::ESubmission::busy);
                }
                for (std::uint32_t count = 0u; active && send_count && (count < 100u); ++count)
                {
                    const modules::ESubmission sent = communication.submit({ modules::SDestination::EKind::connection, {}, token },
                        threading::make_pod_message(7, services::OpenServiceResult{}));
                    if (sent == modules::ESubmission::busy)
                    {
                        break;
                    }
                    valid = valid && modules::accepted(sent);
                    --send_count;
                }
                if (close)
                {
                    intent.kind = EKind::close_connection;
                    intent.connection = token;
                    if (modules::accepted(communication.submit(intent))) close = false;
                }
                break;
            }
            case EEvent::participation:
            {
                participated = invocation.notification.connection_status == connections::EStatus::success;
                break;
            }
            case EEvent::connection_offered:
            {
                token = invocation.notification.connection;
                intent.kind = EKind::consent;
                intent.decision = connections::EStatus::success;
                valid = valid && modules::accepted(communication.submit(intent));
                break;
            }
            case EEvent::connection_available:
            {
                active = true;
                if (send_on_available)
                {
                    valid = valid && modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, token },
                        threading::make_pod_message(6, services::OpenServiceResult{})));
                }
                break;
            }
            case EEvent::message:
            {
                if (allow_host && invocation.source.is_host())
                {
                    break;
                }
                valid = valid && active && (invocation.source.kind == connections::SSource::EKind::direct) &&
                    (invocation.source.address == peer) && (invocation.source.connection.value == token.value);
                ++received;
                if (observations)
                {
                    if (received <= observation_capacity) observations[received - 1u] = *invocation.message;
                    else valid = false;
                }
                if (module_ids::ops::is_valid_id(test_module))
                {
                    modules::RunTestsResult tested;
                    valid = valid && invocation.message && invocation.message->copy_payload_to(tested) &&
                        (invocation.message->query_async_slot() == 77) && (tested.module == test_module) &&
                        (tested.status == modules::ETestRunStatus::success) && (tested.passed == 23u) &&
                        (tested.failed == 0u) && (tested.skipped == 0u);
                    test_done.store(true, std::memory_order_release);
                }
                break;
            }
            case EEvent::connection_result:
            {
                result = invocation.notification.connection_status;
                break;
            }
            case EEvent::connection_closing:
            {
                active = false;
                closing = true;
                send_count = 0u;
                break;
            }
            case EEvent::connection_retired:
            {
                ++retired;
                break;
            }
            case EEvent::stop:
            {
                stopping = true;
                break;
            }
            default:
            {
                break;
            }
        }
        if (closing && !hold && !quiescent)
        {
            intent.kind = EKind::connection_quiescent;
            intent.connection = token;
            quiescent = modules::accepted(communication.submit(intent));
        }
        progress = {};
        progress.ready = participated ? 1u : 0u;
        progress.direct_input = pause_input ? 0u : 1u;
        progress.work_complete = stopping ? 1u : 0u;
    }

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        CPeerExecution& self = *static_cast<CPeerExecution*>(context->creation_context);
        self.communication = *context->communication;
        self.address = context->address;
        self.created_on = platform::threading::query_current_thread_id();
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL dispatch(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        static_cast<CPeerExecution*>(runtime)->handle(*invocation, *progress);
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        CPeerExecution& self = *static_cast<CPeerExecution*>(runtime);
        ++self.destroyed;
        self.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
};

void preamble_connection_cancellation(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &CPeerExecution::create;
    execution.handle = &CPeerExecution::dispatch;
    execution.destroy = &CPeerExecution::destroy;
    CPeerExecution a, b;
    a.peer = system_ids::ops::make_system_id(module_ids::rendering, thread_ids::host);
    b.peer = system_ids::ops::make_system_id(module_ids::executive, thread_ids::host);
    host::SClientRecord* const first = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding, 3u, execution, &a);
    host::SClientRecord* const second = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding, 3u, execution, &b);
    TEST_EXPECT(ctx, (first && second));
    if (!first || !second)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    host::CDirectConnections& connections = host::SModulePreambleTestAccess::connections(runtime);
    TEST_EXPECT(ctx, (first->preamble->activate() && second->preamble->activate()));
    const auto turn = [&]()
    {
        first->preamble->process();
        second->preamble->process();
        (void)host::SModulePreambleTestAccess::drain(runtime);
        connections.progress();
    };
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, (a.participated && b.participated));
    a.request = a.send_on_available = true;
    for (std::uint32_t count = 0u; count < 12u; ++count) turn();
    TEST_EXPECT(ctx, (a.valid && b.valid && a.active && b.active && (b.received == 1u)));
    TEST_EXPECT(ctx, (a.result == connections::EStatus::success));
    //  A prior data/capacity notification must not suppress a later wake after
    //  the owner has drained its queue and parked. Exercise both directions.
    for (std::uint32_t count = 0u; count < 2u; ++count)
    {
        first->endpoint->work_pending.store(0u, std::memory_order_release);
        second->endpoint->work_pending.store(0u, std::memory_order_release);
        a.send_count = 1u;
        first->preamble->process();
        TEST_EXPECT(ctx, (second->endpoint->work_pending.load(std::memory_order_acquire) == 1u));
        second->preamble->process();
        TEST_EXPECT(ctx, (b.received == (count + 2u)));
        TEST_EXPECT(ctx, (first->endpoint->work_pending.load(std::memory_order_acquire) == 1u));
        first->preamble->process();
    }
    b.pause_input = b.hold = true;
    second->preamble->process();
    a.send_count = 100u;
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, ((a.send_count == 0u) && (b.received == 3u)));
    a.close = true;
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, (a.closing && b.closing && !connections.empty() && (b.retired == 0u)));
    b.hold = false;
    for (std::uint32_t count = 0u; count < 8u; ++count) turn();
    TEST_EXPECT(ctx, (connections.empty() && (a.retired == 1u) && (b.retired == 1u) && (b.received == 3u)));
    TEST_EXPECT(ctx, (a.valid && b.valid && !first->preamble->failed() && !second->preamble->failed()));
    first->preamble->request_stop(modules::EExecutionStop::requested);
    second->preamble->request_stop(modules::EExecutionStop::requested);
    for (std::uint32_t count = 0u; count < 8u; ++count) turn();
    TEST_EXPECT(ctx, ((a.destroyed == 1u) && (b.destroyed == 1u)));
    host::SModulePreambleTestAccess::clear(runtime);
}

// Each test action runs once inside the actual module callback. No test helper
// invokes a bound facade outside its authenticated execution context.
class CBulkPeerExecution
{
public:
    modules::CCommunication communication;
    system_ids::id_type address{};
    services::SClientToken endpoint;
    modules::SExecutionNotification results[8]{};
    std::uint32_t result_count{ 0u }, received{ 0u }, available{ 0u }, retired{ 0u }, destroyed{ 0u };
    bool participated{ false }, stopping{ false }, hold{ false }, valid{ true }, pause_input{ false }, ordered{ false };
    bool consent{ true }, result_before_available{ false };
    bool allow_host{ false }, track_sources{ false };
    services::SClientToken source_ids[2]{};
    std::int32_t source_counts[3]{};
    unsigned offers{ 0u };
    std::int32_t next_slot{ 0 };
    threading::CErasedPodMsg last_message;
    connections::SSource last_source;
    connections::SToken closing, offered;
    void* action_context{ nullptr };
    void (*action)(CBulkPeerExecution&, void* const) noexcept{ nullptr };
    platform::threading::CPlatformThreadId created_on, destroyed_on;

    void handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        using EEvent = modules::EExecutionEvent;
        using EKind = modules::SExecutionIntent::EKind;
        modules::SExecutionIntent intent;
        switch (invocation.event)
        {
            case EEvent::start:
            {
                intent.correlation = 0;
                valid = modules::accepted(communication.submit(intent)) && valid;
                break;
            }
            case EEvent::participation:
            {
                participated = invocation.notification.connection_status == connections::EStatus::success;
                valid = participated && valid;
                break;
            }
            case EEvent::connection_offered:
            {
                offered = invocation.notification.connection;
                ++offers;
                intent.kind = EKind::consent;
                intent.connection = invocation.notification.connection;
                intent.decision = connections::EStatus::success;
                if (consent) valid = modules::accepted(communication.submit(intent)) && valid;
                break;
            }
            case EEvent::connection_available:
            {
                ++available;
                break;
            }
            case EEvent::connection_result:
            {
                if ((invocation.notification.connection_status == connections::EStatus::success) &&
                    ((invocation.notification.first.value == endpoint.value) ||
                        (invocation.notification.second.value == endpoint.value)) && !available)
                {
                    result_before_available = true;
                }
                if (result_count < std::size(results)) results[result_count++] = invocation.notification;
                else valid = false;
                break;
            }
            case EEvent::connection_closing:
            {
                closing = invocation.notification.connection;
                break;
            }
            case EEvent::connection_retired:
            {
                ++retired;
                break;
            }
            case EEvent::message:
            {
                valid = valid && ((invocation.source.kind == connections::SSource::EKind::direct) ||
                    (allow_host && invocation.source.is_host()));
                last_message = *invocation.message;
                last_source = invocation.source;
                if (ordered) valid = (last_message.query_async_slot() == next_slot++) && valid;
                if (track_sources)
                {
                    const unsigned source = last_source.is_host() ? 0u :
                        ((last_source.endpoint.value == source_ids[0].value) ? 1u : 2u);
                    valid = (last_source.is_host() || (last_source.endpoint.value == source_ids[source - 1u].value)) && valid;
                    valid = (last_message.query_async_slot() == source_counts[source]++) && valid;
                }
                ++received;
                break;
            }
            case EEvent::stop:
            {
                stopping = true;
                break;
            }
            case EEvent::service_async:
            {
                if (action)
                {
                    const auto invoke = action;
                    action = nullptr;
                    invoke(*this, action_context);
                }
                break;
            }
            default:
            {
                break;
            }
        }
        if (closing.value && !hold)
        {
            intent.kind = EKind::connection_quiescent;
            intent.connection = closing;
            const bool accepted = modules::accepted(communication.submit(intent));
            valid = accepted && valid;
            if (accepted) closing = {};
        }
        progress = {};
        progress.ready = participated ? 1u : 0u;
        progress.direct_input = pause_input ? 0u : 1u;
        progress.work_complete = stopping ? 1u : 0u;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        CBulkPeerExecution& self = *static_cast<CBulkPeerExecution*>(context->creation_context);
        self.communication = *context->communication;
        self.address = context->address;
        self.endpoint = context->endpoint;
        self.created_on = platform::threading::query_current_thread_id();
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL dispatch(void* const runtime,
        const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
    {
        static_cast<CBulkPeerExecution*>(runtime)->handle(*invocation, *progress);
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        CBulkPeerExecution& self = *static_cast<CBulkPeerExecution*>(runtime);
        ++self.destroyed;
        self.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
};

struct SCommonBulkFixture
{
    tests::TTestContext& test;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    CBulkPeerExecution a, b;
    host::SClientRecord* first{ nullptr };
    host::SClientRecord* second{ nullptr };

    explicit SCommonBulkFixture(tests::TTestContext& ctx) noexcept : test{ ctx }
    {
        modules::SBoundModuleTestAccess::install(binding);
        modules::SExecutionV1 execution;
        execution.create = &CBulkPeerExecution::create;
        execution.handle = &CBulkPeerExecution::dispatch;
        execution.destroy = &CBulkPeerExecution::destroy;
        first = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding, 3u, execution, &a);
        second = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding, 3u, execution, &b);
        TEST_EXPECT(test, (first && second));
        if (!first || !second)
        {
            return;
        }
        TEST_EXPECT(test, (first->preamble->activate() && second->preamble->activate()));
        step(4u);
        TEST_EXPECT(test, (a.participated && b.participated));
    }
    host::CDirectConnections& connections() noexcept { return host::SModulePreambleTestAccess::connections(runtime); }
    void step(const unsigned count = 1u) noexcept
    {
        for (unsigned index = 0u; index < count; ++index)
        {
            first->preamble->process();
            second->preamble->process();
            (void)host::SModulePreambleTestAccess::drain(runtime);
            connections().progress();
        }
    }
    template<typename T> void invoke(CBulkPeerExecution& peer, T&& operation) noexcept
    {
        peer.action_context = &operation;
        peer.action = [](CBulkPeerExecution& state, void* const context) noexcept
        {
            (*static_cast<std::remove_reference_t<T>*>(context))(state.communication);
        };
        ((&peer == &a) ? first : second)->preamble->process();
        TEST_EXPECT(test, !peer.action);
        peer.action_context = nullptr;
    }
    void request(CBulkPeerExecution& peer, const connections::SBulkConfiguration bulk,
        const std::int32_t correlation, const std::uint32_t version = 1u) noexcept
    {
        invoke(peer, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::connect;
            intent.correlation = correlation;
            intent.first = peer.address;
            intent.second = (&peer == &a) ? b.address : a.address;
            intent.bulk = bulk;
            intent.connection_version = version;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
    }
    connections::SToken establish(const bool consumer_first, const bool reciprocal,
        const connections::SBulkConfiguration bulk = { 129u, module_ids::executive }) noexcept
    {
        a.result_count = b.result_count = 0u;
        request(consumer_first ? b : a, bulk, consumer_first ? 9 : 7);
        if (reciprocal) request(consumer_first ? a : b, bulk, consumer_first ? 7 : 9);
        step(12u);
        const modules::SExecutionNotification& result = consumer_first ? b.results[0] : a.results[0];
        TEST_EXPECT(test, ((result.connection_status == connections::EStatus::success) && result.connection.value));
        TEST_EXPECT(test, (a.result_count == ((!consumer_first || reciprocal) ? 1u : 0u)));
        TEST_EXPECT(test, (b.result_count == ((consumer_first || reciprocal) ? 1u : 0u)));
        if (reciprocal)
        {
            TEST_EXPECT(test, ((a.results[0].connection.value == b.results[0].connection.value) &&
                (a.results[0].connection_status == connections::EStatus::success) &&
                (b.results[0].connection_status == connections::EStatus::success)));
        }
        return result.connection;
    }
    void close(const connections::SToken token) noexcept
    {
        invoke(a, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::close_connection;
            intent.connection = token;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
        step(8u);
    }
    ~SCommonBulkFixture() noexcept
    {
        if (first && second)
        {
            a.hold = b.hold = false;
            first->preamble->request_stop(modules::EExecutionStop::requested);
            second->preamble->request_stop(modules::EExecutionStop::requested);
            step(16u);
            TEST_EXPECT(test, (connections().empty() && a.valid && b.valid));
            TEST_EXPECT(test, ((a.destroyed == 1u) && (b.destroyed == 1u) &&
                (a.created_on == a.destroyed_on) && (b.created_on == b.destroyed_on)));
            TEST_EXPECT(test, ((first->endpoint->notification_pins == 0u) && (second->endpoint->notification_pins == 0u)));
        }
        host::SModulePreambleTestAccess::clear(runtime);
    }
};

void common_bulk_buffers(tests::TTestContext& ctx, const bool consumer_first, const bool reciprocal)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(consumer_first, reciprocal);
    const modules::SDestination destination{ modules::SDestination::EKind::connection, {}, token };
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query(destination).active);
        TEST_EXPECT(ctx, !communication.discard_and_acquire(token));
        TEST_EXPECT(ctx, !communication.consumer_data(token));
        TEST_EXPECT(ctx, !communication.producer_data(token));
        TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::inactive));
    });
    void* slots[3]{};
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.discard_and_acquire(token));
        TEST_EXPECT(ctx, (communication.submit(destination, threading::make_pod_message(0,
            modules::RunTestsRequest{ module_ids::rendering })) == modules::ESubmission::no_pipe));
        for (unsigned index = 0u; index < 2u; ++index)
        {
            const memory::CMemoryView output = communication.producer_data(token);
            TEST_EXPECT(ctx, ((output.bytes() == 129u) && (output.storage_alignment() == 128u)));
            if (!output) return;
            slots[index] = output.data();
            std::memset(output.data(), static_cast<int>(index + 1u), output.bytes());
            TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::published));
        }
    });
    memory::CMemoryConstView held;
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        // process() has cleared the notification hint before this callback.
        TEST_EXPECT(ctx, communication.discard_and_acquire(token));
        held = communication.consumer_data(token);
        TEST_EXPECT(ctx, ((held.bytes() == 129u) && (held.data() == slots[1])));
    });
    if (!held) return;
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        for (unsigned index = 0u; index < 32u; ++index)
        {
            const memory::CMemoryView output = communication.producer_data(token);
            TEST_EXPECT(ctx, (output && (output.data() != held.data()) &&
                ((reinterpret_cast<std::uintptr_t>(output.data()) & 127u) == 0u)));
            if (!output) return;
            if (index == 1u) slots[2] = output.data();
            std::memset(output.data(), static_cast<int>(index + 3u), output.bytes());
            TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::published));
            for (std::size_t byte = 0u; byte < held.bytes(); ++byte)
            {
                TEST_EXPECT(ctx, (static_cast<const unsigned char*>(held.data())[byte] == 2u));
            }
        }
    });
    TEST_EXPECT(ctx, ((slots[0] != slots[1]) && (slots[0] != slots[2]) && (slots[1] != slots[2])));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.discard_and_acquire(token));
        const memory::CMemoryConstView latest = communication.consumer_data(token);
        TEST_EXPECT(ctx, (latest && (*static_cast<const unsigned char*>(latest.data()) == 34u)));
        TEST_EXPECT(ctx, !communication.discard_and_acquire(token));
        TEST_EXPECT(ctx, !communication.consumer_data(token));
    });
    fixture.close(token);
    TEST_EXPECT(ctx, fixture.connections().empty());
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.producer_data(token));
        TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::inactive));
    });
    const connections::SToken successor = fixture.establish(!consumer_first, reciprocal);
    TEST_EXPECT(ctx, (successor.value != token.value));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.discard_and_acquire(successor));
        TEST_EXPECT(ctx, !communication.consumer_data(successor));
    });
    fixture.close(successor);
}

void common_bulk_configuration(tests::TTestContext& ctx, const bool wrong_producer)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken pod = fixture.establish(false, false, {});
    fixture.a.result_count = fixture.b.result_count = 0u;
    fixture.request(fixture.a, { 129u, module_ids::executive }, 7);
    fixture.request(fixture.b, { wrong_producer ? 129u : 257u,
        wrong_producer ? module_ids::rendering : module_ids::executive }, 9);
    fixture.step(12u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 1u)));
    TEST_EXPECT(ctx, (fixture.a.results[0].connection_status == connections::EStatus::success));
    TEST_EXPECT(ctx, (fixture.b.results[0].connection_status == connections::EStatus::configuration_mismatch));
    const connections::SToken bulk = fixture.a.results[0].connection;
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, pod }).active);
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, bulk }).active);
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, pod },
            threading::make_pod_message(31, modules::RunTestsRequest{ module_ids::rendering }))));
    });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.b.received == 1u));
    fixture.close(bulk);
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, pod }).active);
    });
    fixture.close(pod);
}

void common_bulk_validation(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SBulkConfiguration invalid[]{
        { 0u, module_ids::executive }, { UINT64_MAX, module_ids::executive },
        { memory::k_byte_size_ceiling, module_ids::executive }, { 129u, module_ids::audio }
    };
    for (const connections::SBulkConfiguration configuration : invalid)
    {
        fixture.a.result_count = 0u;
        fixture.request(fixture.a, configuration, 7);
        fixture.step(4u);
        TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::invalid_request) &&
            !fixture.a.results[0].connection.value && fixture.connections().empty()));
    }
}

void common_bulk_allocation(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext memory{ allocator };
    {
        tests::TMemoryContextScope scope{ &memory };
        SCommonBulkFixture fixture{ ctx };
        fixture.request(fixture.a, { 129u, module_ids::executive }, 7);
        fixture.step(); // Host creates the offered operation before allocation failure.
        allocation.fail_on = allocation.attempts;
        fixture.step(12u);
        allocation.fail_on = SIZE_MAX;
        TEST_EXPECT(ctx, allocation.failed);
        TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::allocation_failed)));
        TEST_EXPECT(ctx, (fixture.connections().empty() && !fixture.connections().failed()));
    }
    TEST_EXPECT(ctx, memory.is_attribution_empty());
}

void common_bulk_retained_closure(tests::TTestContext& ctx, const bool wake_failed)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(false, false);
    fixture.b.hold = true;
    if (wake_failed)
    {
        // Inject an unavailable notification target after installation. The
        // bulk publication itself must still commit exactly once.
        host::SDirectEndpointDescriptor* const descriptor =
            host::SDirectConnectionTestAccess::private_descriptor(fixture.connections(), token, 0u);
        TEST_EXPECT(ctx, descriptor);
        if (!descriptor) return;
        descriptor->peer_notification = {};
    }
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        const memory::CMemoryView output = communication.producer_data(token);
        TEST_EXPECT(ctx, output);
        if (!output) return;
        std::memset(output.data(), 42, output.bytes());
        TEST_EXPECT(ctx, (communication.publish_bulk(token) == (wake_failed ?
            connections::EBulkPublish::published_wake_failed : connections::EBulkPublish::published)));
        if (wake_failed)
        {
            TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::inactive));
        }
    });
    memory::CMemoryConstView held;
    // The peer runs before Host consumes the closure. Even failed notification
    // does not roll back the already-published buffer.
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.discard_and_acquire(token));
        held = communication.consumer_data(token);
        TEST_EXPECT(ctx, (held && (*static_cast<const unsigned char*>(held.data()) == 42u)));
    });
    if (!held) return;
    if (wake_failed) fixture.step(8u);
    else fixture.close(token);
    TEST_EXPECT(ctx, (fixture.b.closing.value == token.value));
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::bulk_capacity(fixture.connections(), token) == 129u));
    TEST_EXPECT(ctx, ((fixture.first->endpoint->notification_pins == 1u) &&
        (fixture.second->endpoint->notification_pins == 1u)));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.discard_and_acquire(token));
        TEST_EXPECT(ctx, !communication.consumer_data(token));
        for (std::size_t byte = 0u; byte < held.bytes(); ++byte)
        {
            TEST_EXPECT(ctx, (static_cast<const unsigned char*>(held.data())[byte] == 42u));
        }
    });
    TEST_EXPECT(ctx, (fixture.first->preamble->failed() == wake_failed));
    if (wake_failed)
    {
        TEST_EXPECT(ctx, (fixture.first->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::wake_failed));
    }
    held = memory::CMemoryConstView{};
    fixture.b.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, (fixture.connections().empty() && (fixture.a.retired == 1u) && (fixture.b.retired == 1u)));
    TEST_EXPECT(ctx, ((fixture.first->endpoint->notification_pins == 0u) &&
        (fixture.second->endpoint->notification_pins == 0u)));
}

void common_partial_installation(tests::TTestContext& ctx, const bool bulk)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.b.hold = true;
    fixture.request(fixture.a, bulk ? connections::SBulkConfiguration{ 129u, module_ids::executive } :
        connections::SBulkConfiguration{}, 7);
    fixture.step(2u); // Both offers accepted; neither installation consumed yet.
    const connections::SToken token = fixture.a.offered;
    TEST_EXPECT(ctx, (token.value && (fixture.b.offered.value == token.value)));
    fixture.second->preamble->process(); // Only B acknowledges installation.
    TEST_EXPECT(ctx, ((fixture.a.available == 0u) && (fixture.b.available == 0u)));
    fixture.first->preamble->request_stop(modules::EExecutionStop::requested);
    fixture.step(8u); // A's late installation cannot override stop.
    TEST_EXPECT(ctx, ((fixture.a.available == 0u) && (fixture.b.available == 0u)));
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::cancelled)));
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::contains(fixture.connections(), token) &&
        (fixture.first->endpoint->notification_pins == 1u) && (fixture.a.destroyed == 0u)));
    fixture.b.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, (fixture.connections().empty() && (fixture.a.destroyed == 1u) &&
        (fixture.a.retired == 1u) && (fixture.b.retired == 1u)));
}

void common_activation_and_timeout(tests::TTestContext& ctx, const bool expire, const bool bulk)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.b.hold = true;
    fixture.request(fixture.a, bulk ? connections::SBulkConfiguration{ 129u, module_ids::executive } :
        connections::SBulkConfiguration{}, 7);
    fixture.step(2u);
    fixture.first->preamble->process();
    fixture.second->preamble->process();
    // Delay B's actual installed acknowledgement without converting it to
    // ordinary traffic or counting capture as dispatch.
    host::SLifecycleMessage installed;
    TEST_EXPECT(ctx, fixture.second->channel->read_host_control(installed));
    TEST_EXPECT(ctx, (installed.operation == host::ELifecycle::installed));
    const connections::SToken token = installed.notification.connection;
    (void)host::SModulePreambleTestAccess::drain(fixture.runtime);
    fixture.connections().progress();
    const modules::SDestination destination{ modules::SDestination::EKind::connection, {}, token };
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.query(destination).active);
        TEST_EXPECT(ctx, !communication.producer_data(token));
        TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::inactive));
    });
    TEST_EXPECT(ctx, ((fixture.a.result_count == 0u) && (fixture.a.available == 0u) && (fixture.b.available == 0u)));
    if (expire)
    {
        host::SDirectConnectionTestAccess::expire(fixture.connections(), token, fixture.first->host_execution.restriction);
        fixture.connections().progress();
        TEST_EXPECT(ctx, fixture.second->channel->preamble_control(installed));
        fixture.step(8u);
        TEST_EXPECT(ctx, ((fixture.a.available == 0u) && (fixture.b.available == 0u) &&
            (fixture.a.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::timed_out)));
        TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
        fixture.b.hold = false;
        fixture.step(8u);
        TEST_EXPECT(ctx, fixture.connections().empty());
        return;
    }
    TEST_EXPECT(ctx, fixture.second->channel->preamble_control(installed));
    (void)host::SModulePreambleTestAccess::drain(fixture.runtime);
    fixture.connections().progress();
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query(destination).active);
        if (bulk)
        {
            const memory::CMemoryView output = communication.producer_data(token);
            TEST_EXPECT(ctx, output);
            if (!output) return;
            std::memset(output.data(), 42, output.bytes());
            TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::published));
        }
        else
        {
            TEST_EXPECT(ctx, modules::accepted(communication.submit(destination,
                threading::make_pod_message(4, modules::RunTestsRequest{ module_ids::rendering }))));
        }
    });
    (void)host::SModulePreambleTestAccess::drain(fixture.runtime);
    fixture.connections().progress();
    TEST_EXPECT(ctx, ((fixture.a.available == 1u) && (fixture.b.available == 0u) &&
        (fixture.b.received == 0u) && (fixture.a.result_count == 0u)));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query(destination).active);
        TEST_EXPECT(ctx, (fixture.b.available == 1u));
        if (bulk)
        {
            TEST_EXPECT(ctx, communication.discard_and_acquire(token));
            const memory::CMemoryConstView input = communication.consumer_data(token);
            TEST_EXPECT(ctx, (input && (*static_cast<const unsigned char*>(input.data()) == 42u)));
        }
    });
    fixture.step(4u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::success)));
    TEST_EXPECT(ctx, (fixture.b.received == (bulk ? 0u : 1u)));
    fixture.b.hold = false;
    fixture.close(token);
}

void common_request_conflicts(tests::TTestContext& ctx)
{
    {
        SCommonBulkFixture fixture{ ctx };
        fixture.request(fixture.a, { 129u, module_ids::executive }, 7);
        fixture.request(fixture.b, { 129u, module_ids::executive }, 9, 2u);
        fixture.step(12u);
        TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::success) &&
            (fixture.b.results[0].connection_status == connections::EStatus::version_mismatch)));
        fixture.close(fixture.a.results[0].connection);
    }
    {
        SCommonBulkFixture fixture{ ctx };
        fixture.second->direct_connections = host::EDirectConnectionState::unregistered;
        fixture.request(fixture.a, {}, 7);
        fixture.step(4u);
        TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::unavailable) && fixture.connections().empty()));
        fixture.second->direct_connections = host::EDirectConnectionState::available;
        fixture.a.result_count = 0u;
        fixture.request(fixture.a, {}, 7);
        fixture.request(fixture.a, {}, 99);
        fixture.step(12u);
        TEST_EXPECT(ctx, (fixture.a.result_count == 2u));
        connections::SToken token;
        unsigned successes = 0u, refused = 0u;
        for (unsigned index = 0u; index < fixture.a.result_count; ++index)
        {
            const modules::SExecutionNotification& result = fixture.a.results[index];
            if (result.correlation == 7)
            {
                TEST_EXPECT(ctx, (result.connection_status == connections::EStatus::success));
                token = result.connection;
                ++successes;
            }
            else
            {
                TEST_EXPECT(ctx, ((result.correlation == 99) &&
                    (result.connection_status == connections::EStatus::busy) && !result.connection.value));
                ++refused;
            }
        }
        TEST_EXPECT(ctx, ((successes == 1u) && (refused == 1u)));
        fixture.close(token);
    }
}

void common_delayed_request(tests::TTestContext& ctx, const bool after_retirement, const bool successor)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.request(fixture.a, {}, 7);
    fixture.request(fixture.b, {}, 9);
    host::SLifecycleMessage held;
    TEST_EXPECT(ctx, fixture.second->channel->read_host_control(held));
    TEST_EXPECT(ctx, (held.operation == host::ELifecycle::connect));
    fixture.step(12u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 0u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::success)));
    const connections::SToken old = fixture.a.results[0].connection;
    fixture.b.hold = true;
    fixture.close(old);
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), old));
    if (!after_retirement)
    {
        TEST_EXPECT(ctx, fixture.second->channel->preamble_control(held));
        fixture.step(4u);
        TEST_EXPECT(ctx, ((fixture.b.result_count == 1u) &&
            (fixture.b.results[0].connection_status == connections::EStatus::closing) &&
            !fixture.b.results[0].connection.value));
    }
    fixture.b.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, fixture.connections().empty());
    if (after_retirement)
    {
        if (!successor) fixture.connections().close_admission();
        TEST_EXPECT(ctx, fixture.second->channel->preamble_control(held));
        fixture.step(12u);
        TEST_EXPECT(ctx, ((fixture.b.result_count == 1u) && (fixture.b.results[0].correlation == 9) &&
            (fixture.b.results[0].connection_status ==
                (successor ? connections::EStatus::success : connections::EStatus::closing))));
        if (successor)
        {
            const connections::SToken next = fixture.b.results[0].connection;
            TEST_EXPECT(ctx, (next.value && (next.value != old.value)));
            fixture.close(next);
        }
    }
    TEST_EXPECT(ctx, fixture.connections().empty());
}

void common_activation_pressure(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.request(fixture.a, {}, 7);
    fixture.step(2u);
    fixture.first->preamble->process();
    fixture.second->preamble->process();
    const connections::SToken token = fixture.a.offered;
    // Controlled padding occupies the private lane only. The test removes it
    // before invoking the module, so no synthetic stop is delivered.
    host::SLifecycleMessage padding;
    padding.operation = host::ELifecycle::stop;
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        TEST_EXPECT(ctx, fixture.second->channel->host_control(padding));
    }
    (void)host::SModulePreambleTestAccess::drain(fixture.runtime);
    fixture.connections().progress();
    fixture.first->preamble->process();
    TEST_EXPECT(ctx, ((fixture.a.available == 1u) && (fixture.b.available == 0u) &&
        (fixture.a.result_count == 0u) && (fixture.b.result_count == 0u)));
    host::SLifecycleMessage discarded;
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        TEST_EXPECT(ctx, (fixture.second->channel->read_preamble_control(discarded) &&
            (discarded.operation == host::ELifecycle::stop)));
    }
    fixture.request(fixture.b, {}, 9);
    TEST_EXPECT(ctx, ((fixture.b.available == 0u) && (fixture.b.result_count == 0u)));
    fixture.step(8u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 1u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::success) &&
        (fixture.b.results[0].connection_status == connections::EStatus::success) &&
        (fixture.b.results[0].connection.value == token.value) && (fixture.b.available == 1u)));
    fixture.request(fixture.b, {}, 10);
    fixture.step(4u);
    TEST_EXPECT(ctx, ((fixture.b.result_count == 2u) && (fixture.b.available == 1u) &&
        (fixture.b.results[1].connection_status == connections::EStatus::already_connected) &&
        (fixture.b.results[1].connection.value == token.value)));
    fixture.close(token);
}

void common_pod_exchange(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(false, true, {});
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, token },
            threading::make_pod_message(31, modules::RunTestsRequest{ module_ids::rendering }))));
    });
    fixture.step();
    modules::RunTestsRequest request;
    TEST_EXPECT(ctx, ((fixture.b.received == 1u) && fixture.b.last_message.copy_payload_to(request) &&
        (request.module == module_ids::rendering) && (fixture.b.last_message.query_async_slot() == 31) &&
        (fixture.b.last_source.address == fixture.a.address) &&
        (fixture.b.last_source.endpoint.value == fixture.first->identity.value) &&
        (fixture.b.last_source.connection.value == token.value)));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::peer, fixture.a.address, token },
            threading::make_pod_message(31, modules::RunTestsResult{ module_ids::rendering, modules::ETestRunStatus::success }))));
    });
    fixture.step();
    modules::RunTestsResult result;
    TEST_EXPECT(ctx, ((fixture.a.received == 1u) && fixture.a.last_message.copy_payload_to(result) &&
        (result.status == modules::ETestRunStatus::success) && (fixture.a.last_message.query_async_slot() == 31) &&
        (fixture.a.last_source.address == fixture.b.address) &&
        (fixture.a.last_source.endpoint.value == fixture.second->identity.value)));
    fixture.close(token);
    const connections::SToken successor = fixture.establish(false, false, {});
    TEST_EXPECT(ctx, (successor.value != token.value));
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        modules::SExecutionIntent close;
        close.kind = modules::SExecutionIntent::EKind::close_connection;
        close.connection = token;
        TEST_EXPECT(ctx, (communication.submit(close) == modules::ESubmission::no_pipe));
        close.kind = modules::SExecutionIntent::EKind::connection_quiescent;
        TEST_EXPECT(ctx, (communication.submit(close) == modules::ESubmission::invalid));
        TEST_EXPECT(ctx, (communication.submit({ modules::SDestination::EKind::peer, fixture.b.address, token },
            threading::make_pod_message(32, modules::RunTestsRequest{ module_ids::rendering })) == modules::ESubmission::no_pipe));
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, successor }).active);
    });
    fixture.close(successor);
}

void common_pod_pressure(tests::TTestContext& ctx, const bool cancel)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext memory{ allocator };
    {
        tests::TMemoryContextScope scope{ &memory };
        SCommonBulkFixture fixture{ ctx };
        const connections::SToken token = fixture.establish(false, false, {});
        const modules::SDestination destination{ modules::SDestination::EKind::connection, {}, token };
        fixture.b.pause_input = fixture.b.ordered = true;
        fixture.second->preamble->process();
        const std::size_t attempts = allocation.attempts;
        allocation.fail_on = attempts;
        fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
        {
            for (std::uint32_t index = 0u; index < host::k_module_pod_capacity; ++index)
            {
                TEST_EXPECT(ctx, (communication.submit(destination, threading::make_pod_message(static_cast<std::int32_t>(index),
                    modules::RunTestsRequest{ module_ids::rendering })) == modules::ESubmission::accepted));
            }
            TEST_EXPECT(ctx, (communication.submit(destination, threading::make_pod_message(
                static_cast<std::int32_t>(host::k_module_pod_capacity), modules::RunTestsRequest{ module_ids::rendering })) ==
                modules::ESubmission::busy));
        });
        TEST_EXPECT(ctx, ((allocation.attempts == attempts) && !allocation.failed && !fixture.first->preamble->failed()));
        allocation.fail_on = SIZE_MAX;
        TEST_EXPECT(ctx, (!fixture.first->preamble->work_pending() && !fixture.second->preamble->work_pending() &&
            (fixture.b.received == 0u)));
        if (cancel)
        {
            fixture.close(token);
            // Unread cancellation is bounded per turn just like dispatch.
            // Complete the remaining prefix without reopening direct input.
            fixture.step(32u);
            TEST_EXPECT(ctx, ((fixture.b.received == 0u) && fixture.connections().empty()));
        }
        else
        {
            fixture.b.pause_input = false;
            fixture.second->preamble->process();
            TEST_EXPECT(ctx, ((fixture.b.received > 0u) && fixture.first->preamble->work_pending()));
            fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
            {
                TEST_EXPECT(ctx, (communication.submit(destination, threading::make_pod_message(
                    static_cast<std::int32_t>(host::k_module_pod_capacity), modules::RunTestsRequest{ module_ids::rendering })) ==
                    modules::ESubmission::accepted));
            });
            fixture.step(32u);
            TEST_EXPECT(ctx, (fixture.b.valid && (fixture.b.received == (host::k_module_pod_capacity + 1u))));
            fixture.close(token);
        }
    }
    TEST_EXPECT(ctx, memory.is_attribution_empty());
}

void common_pod_wake_failure(tests::TTestContext& ctx, const bool after_read)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(false, false, {});
    host::SDirectEndpointDescriptor* const descriptor = host::SDirectConnectionTestAccess::private_descriptor(
        fixture.connections(), token, after_read ? 1u : 0u);
    TEST_EXPECT(ctx, descriptor);
    if (!descriptor) return;
    descriptor->peer_notification = {};
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, (communication.submit({ modules::SDestination::EKind::connection, {}, token },
            threading::make_pod_message(4, modules::RunTestsRequest{ module_ids::rendering })) ==
            (after_read ? modules::ESubmission::accepted : modules::ESubmission::published_wake_failed)));
    });
    fixture.second->preamble->process(); // Accepted data may dispatch before Host closure.
    TEST_EXPECT(ctx, ((fixture.b.received == 1u) && (fixture.b.last_message.query_async_slot() == 4)));
    const host::SClientRecord& reporter = after_read ? *fixture.second : *fixture.first;
    TEST_EXPECT(ctx, (reporter.preamble->failed() &&
        (reporter.endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::wake_failed)));
    fixture.step(12u);
    TEST_EXPECT(ctx, ((fixture.b.received == 1u) && fixture.connections().empty()));
}

void common_retirement_admission(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.connections().close_client(*fixture.second);
    fixture.request(fixture.a, {}, 7);
    fixture.request(fixture.b, {}, 9);
    fixture.step(4u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 1u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::unavailable) &&
        (fixture.b.results[0].connection_status == connections::EStatus::closing) && fixture.connections().empty()));
    // Registration belongs to an exact endpoint, so an independently controlled
    // replacement at the same logical address can participate after old closure.
    tests::SCommonEndpointFixture replacement{ module_ids::rendering, fixture.second->identity.value + 1000u };
    fixture.connections().register_client(replacement, 0);
    replacement.flush_controls();
    host::SLifecycleMessage registered;
    TEST_EXPECT(ctx, replacement.channel->read_preamble_control(registered));
    TEST_EXPECT(ctx, ((registered.operation == host::ELifecycle::participated) &&
        (registered.notification.connection_status == connections::EStatus::success) &&
        (replacement.direct_connections == host::EDirectConnectionState::available) &&
        (fixture.second->direct_connections == host::EDirectConnectionState::closing)));
}

struct SCommonNativeJoin
{
    platform::system::CPerfCountConversion conversion;
    threading::CEndpointResources* resources{ nullptr };
    threading::SNotificationEndpoint notification;
    std::atomic<bool> entered{ false }, terminal{ false }, published{ false }, signal{ false }, signalled{ false }, finish{ false };

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        SCommonNativeJoin& self = *static_cast<SCommonNativeJoin*>(context->creation_context);
        *runtime = nullptr; // Deliberately fail creation without any module-owned state.
        self.resources->control_state.mark_running();
        self.entered.store(true, std::memory_order_release);
        if (!tests::wait_until(self.conversion, [&]() noexcept { return self.terminal.load(std::memory_order_acquire); }))
        {
            std::fputs("Native join fixture did not receive terminal command.\n", stderr);
            std::_Exit(1);
        }
        self.resources->control_state.mark_exited();
        self.published.store(true, std::memory_order_release);
        const bool finished = tests::wait_until(self.conversion, [&]() noexcept
        {
            if (self.signal.load(std::memory_order_acquire) && !self.signalled.load(std::memory_order_relaxed))
            {
                self.signalled.store(self.notification.signal(), std::memory_order_release);
            }
            return self.finish.load(std::memory_order_acquire);
        });
        if (!finished)
        {
            std::fputs("Native join fixture retained live callback at timeout.\n", stderr);
            std::_Exit(1);
        }
        return modules::EExecutionStatus::failed;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const,
        const modules::SInvocation* const, modules::SExecutionProgress* const) noexcept
    {
        std::fputs("Native join fixture invoked a handler after failed create.\n", stderr);
        std::_Exit(1);
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const) noexcept
    {
        std::fputs("Native join fixture destroyed absent module state.\n", stderr);
        std::_Exit(1);
    }
};

void common_native_join_retention(tests::TTestContext& ctx, const bool bulk)
{
    SCommonNativeJoin state;
    TEST_EXPECT(ctx, state.conversion.init());
    platform::threading::COwnerWait owner;
    TEST_EXPECT(ctx, (owner.initialise().code == platform::threading::EWaitError::none));
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &SCommonNativeJoin::create;
    execution.handle = &SCommonNativeJoin::handle;
    execution.destroy = &SCommonNativeJoin::destroy;
    threading::CThreadPackage package{ { thread_ids::executive, module_ids::executive }, state.conversion };
    host::SClientRecord first{ module_ids::executive, 731u, &package, { 1731u } };
    tests::SCommonEndpointFixture second{ module_ids::rendering, 732u };
    host::CHostRestrictions restrictions;
    host::CDirectConnections connections;
    first.owner_wake = owner.endpoint();
    first.host_execution.instance = first.instance;
    first.host_execution.restriction = restrictions.reader();
    first.endpoint->host_execution = &first.host_execution;
    state.resources = first.endpoint;
    TEST_EXPECT(ctx, first.initialise_execution(binding, execution, &state));
    TEST_EXPECT(ctx, package.startup());
    const auto require = [&](const bool success) noexcept
    {
        TEST_EXPECT(ctx, success);
        if (!success)
        {
            std::fputs("Native join fixture cannot prove safe teardown.\n", stderr);
            std::_Exit(1);
        }
    };
    require(tests::wait_until(state.conversion, [&]() noexcept { return state.entered.load(std::memory_order_acquire); }));
    first.phase = second.phase = host::EClientPhase::ready;
    first.direct_connections = second.direct_connections = host::EDirectConnectionState::available;
    // Controlled Host participation/consent exposes descriptors while actual
    // native create is held. The native side never consumes its private lane.
    host::SDirectRequest request;
    request.first = system_ids::ops::make_system_id(first.module, first.endpoint->thread_id);
    request.second = system_ids::ops::make_system_id(second.module, second.endpoint->thread_id);
    if (bulk)
    {
        request.bulk_peer = second.module;
        request.bulk = { 129u, first.module };
    }
    connections.request(first, &first, &second, request, 7, restrictions.reader());
    connections.progress();
    host::SLifecycleMessage message;
    require(second.channel->read_preamble_control(message));
    TEST_EXPECT(ctx, (message.operation == host::ELifecycle::offer));
    const connections::SToken token = message.notification.connection;
    connections.decision(first, token, connections::EStatus::success);
    connections.decision(second, token, connections::EStatus::success);
    connections.progress();
    require(second.channel->read_preamble_control(message));
    TEST_EXPECT(ctx, ((message.operation == host::ELifecycle::install) && message.descriptor));
    state.notification = host::SDirectConnectionTestAccess::notification(connections, token);
    threading::SDirectConnectionTestAccess::fail_join(package, true);
    state.terminal.store(true, std::memory_order_release);
    require(tests::wait_until(state.conversion, [&]() noexcept { return state.published.load(std::memory_order_acquire); }));
    connections.close_client(first);
    first.terminal_before_drain = first.incoming_drained = true; // No ordinary ingress was submitted.
    connections.progress();
    require(second.channel->read_preamble_control(message));
    TEST_EXPECT(ctx, (message.operation == host::ELifecycle::closing));
    connections.quiescent(second, token);
    connections.progress();
    TEST_EXPECT(ctx, (!connections.empty() && !package.execution_ended() &&
        (first.endpoint->notification_pins == 1u) && (second.endpoint->notification_pins == 1u)));
    TEST_EXPECT(ctx, (!package.join_terminated_execution() && !package.shutdown()));
    if (bulk)
    {
        TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::bulk_capacity(connections, token) == 129u));
    }
    state.signal.store(true, std::memory_order_release);
    require(tests::wait_until(state.conversion, [&]() noexcept { return state.signalled.load(std::memory_order_acquire); }));
    state.finish.store(true, std::memory_order_release);
    require(tests::wait_until(state.conversion, [&]() noexcept { return package.execution_ended(); }));
    connections.progress();
    TEST_EXPECT(ctx, (!package.join_terminated_execution() && !connections.empty() &&
        (first.endpoint->notification_pins == 1u) && (second.endpoint->notification_pins == 1u)));
    threading::SDirectConnectionTestAccess::fail_join(package, false);
    require(package.join_terminated_execution());
    TEST_EXPECT(ctx, (first.endpoint->notification_pins == 1u));
    connections.progress();
    TEST_EXPECT(ctx, (connections.empty() && (first.endpoint->notification_pins == 0u) &&
        (second.endpoint->notification_pins == 0u)));
    require(second.channel->read_preamble_control(message));
    TEST_EXPECT(ctx, (message.operation == host::ELifecycle::retired));
    TEST_EXPECT(ctx, (first.preamble->failed() && first.preamble->destroyed()));
    require(package.shutdown());
}

struct SCommonMultiFixture
{
    struct SPeer
    {
        CBulkPeerExecution state;
        host::SClientRecord* client{ nullptr };
        bool deliver{ true }, activated{ false };
        TInstance<host::SClientRecord> retained;
    };
    tests::TTestContext& test;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    SPeer peers[12];
    unsigned count{ 0u };

    explicit SCommonMultiFixture(tests::TTestContext& ctx) noexcept : test{ ctx }
    {
        modules::SBoundModuleTestAccess::install(binding);
    }
    SPeer& add(const module_ids::id_type module, const thread_ids::id_type thread, const bool activate = true) noexcept
    {
        if (count == std::size(peers)) std::_Exit(1);
        SPeer& peer = peers[count++];
        modules::SExecutionV1 execution;
        execution.create = &CBulkPeerExecution::create;
        execution.handle = &CBulkPeerExecution::dispatch;
        execution.destroy = &CBulkPeerExecution::destroy;
        peer.client = host::SModulePreambleTestAccess::controlled_endpoint(runtime, module, thread, binding, execution, &peer.state);
        TEST_EXPECT(test, peer.client);
        if (!peer.client) std::_Exit(1);
        peer.activated = peer.deliver = activate;
        if (activate) TEST_EXPECT(test, peer.client->preamble->activate());
        return peer;
    }
    host::CDirectConnections& connections() noexcept { return host::SModulePreambleTestAccess::connections(runtime); }
    void step(const unsigned rounds = 1u) noexcept
    {
        for (unsigned round = 0u; round < rounds; ++round)
        {
            for (unsigned index = 0u; index < count; ++index)
            {
                if (peers[index].deliver && peers[index].activated) peers[index].client->preamble->process();
            }
            (void)host::SModulePreambleTestAccess::drain(runtime);
            connections().progress();
        }
    }
    template<typename T> void invoke(SPeer& peer, T&& operation) noexcept
    {
        peer.state.action_context = &operation;
        peer.state.action = [](CBulkPeerExecution& state, void* const context) noexcept
        {
            (*static_cast<std::remove_reference_t<T>*>(context))(state.communication);
        };
        peer.client->preamble->process();
        TEST_EXPECT(test, !peer.state.action);
        peer.state.action_context = nullptr;
    }
    void request(SPeer& requester, const SPeer& first, const SPeer& second, const std::int32_t slot) noexcept
    {
        invoke(requester, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::connect;
            intent.first = first.state.address;
            intent.second = second.state.address;
            intent.correlation = slot;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
    }
    connections::SToken establish(SPeer& requester, SPeer& first, SPeer& second, const std::int32_t slot = 7) noexcept
    {
        step(4u);
        requester.state.result_count = 0u;
        request(requester, first, second, slot);
        step(12u);
        const modules::SExecutionNotification& result = requester.state.results[0];
        TEST_EXPECT(test, ((requester.state.result_count == 1u) && (result.correlation == slot) &&
            (result.connection_status == connections::EStatus::success)));
        return result.connection;
    }
    void consent(SPeer& peer, const connections::SToken token, const connections::EStatus decision = connections::EStatus::success) noexcept
    {
        invoke(peer, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::consent;
            intent.connection = token;
            intent.decision = decision;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
    }
    void close(SPeer& peer, const connections::SToken token) noexcept
    {
        invoke(peer, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::close_connection;
            intent.connection = token;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
        step(8u);
        TEST_EXPECT(test, !host::SDirectConnectionTestAccess::contains(connections(), token));
    }
    ~SCommonMultiFixture() noexcept
    {
        for (unsigned index = 0u; index < count; ++index)
        {
            peers[index].deliver = true;
            peers[index].state.hold = false;
            peers[index].client->preamble->request_stop(modules::EExecutionStop::requested);
        }
        step(32u);
        TEST_EXPECT(test, connections().empty());
        for (unsigned index = 0u; index < count; ++index)
        {
            const SPeer& peer = peers[index];
            TEST_EXPECT(test, (peer.state.valid && (peer.state.destroyed == (peer.activated ? 1u : 0u)) &&
                (peer.state.created_on == peer.state.destroyed_on) && (peer.client->endpoint->notification_pins == 0u)));
        }
        host::SModulePreambleTestAccess::clear(runtime);
    }
};

void common_endpoint_pairings(tests::TTestContext& ctx)
{
    for (unsigned combination = 0u; combination < 8u; ++combination)
    {
        const bool same_module = (combination & 4u) != 0u;
        const bool first_pump = (combination & 1u) != 0u;
        const bool second_pump = (combination & 2u) != 0u;
        if (same_module && first_pump && second_pump)
        {
            continue;
        }
        SCommonMultiFixture fixture{ ctx };
        SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, first_pump ? thread_ids::host : thread_ids::executive);
        SCommonMultiFixture::SPeer& b = fixture.add(same_module ? module_ids::executive : module_ids::rendering,
            second_pump ? thread_ids::host : thread_ids::rendering);
        SCommonMultiFixture::SPeer& requester = fixture.add(module_ids::audio, thread_ids::host);
        const connections::SToken token = fixture.establish(requester, a, b);
        TEST_EXPECT(ctx, ((a.state.offers == 1u) && (b.state.offers == 1u) && !a.state.result_count &&
            !b.state.result_count && !requester.state.offers));
        const threading::CErasedPodMsg message = threading::make_pod_message(23, modules::RunTestsRequest{ module_ids::audio });
        fixture.invoke(a, [&](const modules::CCommunication& communication) noexcept
        {
            TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::peer, b.state.address, token }, message)));
        });
        fixture.step();
        TEST_EXPECT(ctx, ((b.state.received == 1u) && (b.state.last_source.address == a.state.address) &&
            (b.state.last_source.endpoint.value == a.client->identity.value) &&
            (b.state.last_source.connection.value == token.value) && (b.state.last_message.query_async_slot() == 23)));
        fixture.invoke(b, [&](const modules::CCommunication& communication) noexcept
        {
            TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::peer, a.state.address }, message)));
        });
        fixture.step();
        TEST_EXPECT(ctx, ((a.state.received == 1u) && (a.state.last_source.endpoint.value == b.client->identity.value)));
        fixture.invoke(requester, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent close;
            close.kind = modules::SExecutionIntent::EKind::close_connection;
            close.connection = token;
            TEST_EXPECT(ctx, (communication.submit(close) == modules::ESubmission::no_pipe));
        });
        // Also exercise Host authentication of a forged private transition.
        host::SLifecycleMessage close;
        close.operation = host::ELifecycle::close;
        close.notification.connection = token;
        TEST_EXPECT(ctx, requester.client->channel->preamble_control(close));
        fixture.step();
        TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
        fixture.close(a, token);
        const connections::SToken successor = fixture.establish(requester, a, b);
        TEST_EXPECT(ctx, (successor.value != token.value));
        fixture.invoke(a, [&](const modules::CCommunication& communication) noexcept
        {
            TEST_EXPECT(ctx, (communication.submit({ modules::SDestination::EKind::peer, b.state.address, token }, message) ==
                modules::ESubmission::no_pipe));
        });
        fixture.close(b, successor);
    }
}

void common_explicit_consent(tests::TTestContext& ctx, const bool reject)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::executive, thread_ids::executive);
    SCommonMultiFixture::SPeer& requester = fixture.add(module_ids::rendering, thread_ids::host);
    fixture.step(4u);
    a.state.consent = b.state.consent = false;
    fixture.request(a, a, b, 1);
    fixture.request(b, b, a, 2);
    fixture.request(requester, a, b, 3);
    fixture.request(requester, b, a, 4);
    fixture.step(3u);
    const connections::SToken token = a.state.offered;
    TEST_EXPECT(ctx, ((a.state.offers == 1u) && (b.state.offers == 1u) && (token.value == b.state.offered.value)));
    TEST_EXPECT(ctx, ((requester.state.result_count == 1u) && (requester.state.results[0].correlation == 4) &&
        (requester.state.results[0].connection_status == connections::EStatus::busy)));
    fixture.consent(a, token);
    fixture.step(2u);
    TEST_EXPECT(ctx, ((a.state.available == 0u) && (b.state.available == 0u)));
    fixture.consent(b, token, reject ? connections::EStatus::rejected : connections::EStatus::success);
    fixture.step(12u);
    const connections::EStatus expected = reject ? connections::EStatus::rejected : connections::EStatus::success;
    TEST_EXPECT(ctx, ((a.state.result_count == 1u) && (b.state.result_count == 1u) && (requester.state.result_count == 2u) &&
        (a.state.results[0].connection_status == expected) && (b.state.results[0].connection_status == expected) &&
        (requester.state.results[1].connection_status == expected) && (requester.state.results[1].correlation == 3)));
    TEST_EXPECT(ctx, (!a.state.result_before_available && !b.state.result_before_available));
    if (reject) TEST_EXPECT(ctx, fixture.connections().empty());
    else fixture.close(a, token);
}

void common_requester_limit(tests::TTestContext& ctx, const bool departing)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    const thread_ids::id_type threads[]{ thread_ids::host, thread_ids::executive, thread_ids::rendering, thread_ids::audio,
        thread_ids::simulation, thread_ids::debug_service, thread_ids::bg_file_io, thread_ids::bg_conditioning, thread_ids::batch_runner_00 };
    for (unsigned index = 0u; index < std::size(threads); ++index)
    {
        (void)fixture.add(module_ids::audio, threads[index], !departing || (index != 0u));
    }
    fixture.step(4u);
    a.state.consent = b.state.consent = false;
    for (unsigned index = 2u; index < fixture.count; ++index)
    {
        if (departing && (index == 2u))
        {
            // A controlled requester with no activated module state can become
            // terminal independently. Live preambles instead await their result.
            host::SLifecycleMessage request;
            request.operation = host::ELifecycle::connect;
            request.first_address = a.state.address;
            request.second_address = b.state.address;
            request.correlation = static_cast<std::int32_t>(index);
            TEST_EXPECT(ctx, fixture.peers[index].client->channel->preamble_control(request));
        }
        else fixture.request(fixture.peers[index], a, b, static_cast<std::int32_t>(index));
    }
    fixture.step(3u);
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::requests(fixture.connections()) == 8u));
    // Host traverses stable unordered endpoint slots, not the requester's test
    // insertion order. Exactly one of the nine requests is refused.
    unsigned refused = 0u;
    unsigned refused_index = 0u;
    for (unsigned index = 2u; index < fixture.count; ++index)
    {
        const CBulkPeerExecution& requester = fixture.peers[index].state;
        if (requester.result_count)
        {
            TEST_EXPECT(ctx, ((requester.result_count == 1u) &&
                (requester.results[0].connection_status == connections::EStatus::busy) &&
                (requester.results[0].correlation == static_cast<std::int32_t>(index))));
            ++refused;
            refused_index = index;
        }
    }
    TEST_EXPECT(ctx, (refused == 1u));
    if (departing)
    {
        host::SClientRecord& requester = *fixture.peers[2].client;
        fixture.connections().close_client(requester);
        requester.phase = host::EClientPhase::stopped;
        requester.terminal_before_drain = requester.incoming_drained = true;
        fixture.connections().progress();
        TEST_EXPECT(ctx, (fixture.connections().client_idle(requester) &&
            (host::SDirectConnectionTestAccess::requests(fixture.connections()) == 7u)));
    }
    const connections::SToken token = a.state.offered;
    fixture.consent(a, token);
    fixture.consent(b, token);
    fixture.step(12u);
    TEST_EXPECT(ctx, ((a.state.offers == 1u) && (b.state.offers == 1u) &&
        (a.state.available == 1u) && (b.state.available == 1u)));
    for (unsigned index = departing ? 3u : 2u; index < fixture.count; ++index)
    {
        if (index == refused_index) continue;
        const CBulkPeerExecution& requester = fixture.peers[index].state;
        TEST_EXPECT(ctx, ((requester.result_count == 1u) && (requester.results[0].connection_status == connections::EStatus::success) &&
            (requester.results[0].connection.value == token.value) &&
            (requester.results[0].correlation == static_cast<std::int32_t>(index))));
    }
    fixture.close(a, token);
}

void common_connection_limits(tests::TTestContext& ctx, const bool overall)
{
    SCommonMultiFixture fixture{ ctx };
    const thread_ids::id_type threads[]{ thread_ids::host, thread_ids::executive, thread_ids::rendering, thread_ids::audio,
        thread_ids::simulation, thread_ids::debug_service, thread_ids::bg_file_io, thread_ids::bg_conditioning,
        thread_ids::batch_runner_00, thread_ids::batch_runner_01 };
    for (const thread_ids::id_type thread : threads) (void)fixture.add(module_ids::audio, thread);
    if (overall)
    {
        unsigned established = 0u;
        for (unsigned first = 0u; (first < 9u) && (established < 32u); ++first)
        {
            for (unsigned second = first + 1u; (second < 9u) && (established < 32u); ++second)
            {
                (void)fixture.establish(fixture.peers[first], fixture.peers[first], fixture.peers[second]);
                ++established;
            }
        }
        fixture.peers[5].state.result_count = 0u;
        fixture.request(fixture.peers[5], fixture.peers[5], fixture.peers[8], 99);
    }
    else
    {
        for (unsigned second = 1u; second <= 8u; ++second)
        {
            (void)fixture.establish(fixture.peers[0], fixture.peers[0], fixture.peers[second]);
        }
        fixture.peers[0].state.result_count = 0u;
        fixture.request(fixture.peers[0], fixture.peers[0], fixture.peers[9], 99);
    }
    fixture.step(4u);
    const CBulkPeerExecution& requester = fixture.peers[overall ? 5u : 0u].state;
    TEST_EXPECT(ctx, ((requester.result_count == 1u) && (requester.results[0].correlation == 99) &&
        (requester.results[0].connection_status == connections::EStatus::busy)));
    fixture.connections().close_all();
    fixture.step(16u);
    TEST_EXPECT(ctx, fixture.connections().empty());
}

void common_result_after_retirement(tests::TTestContext& ctx)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    SCommonMultiFixture::SPeer& requester = fixture.add(module_ids::audio, thread_ids::host);
    fixture.step(4u);
    fixture.request(requester, a, b, 23);
    requester.deliver = false;
    host::SLifecycleMessage padding;
    padding.operation = host::ELifecycle::stop;
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        TEST_EXPECT(ctx, requester.client->channel->host_control(padding));
    }
    fixture.step(12u);
    const connections::SToken token = a.state.offered;
    TEST_EXPECT(ctx, ((a.state.available == 1u) && (b.state.available == 1u) &&
        (requester.state.result_count == 0u) && (host::SDirectConnectionTestAccess::requests(fixture.connections()) == 1u)));
    fixture.close(a, token);
    TEST_EXPECT(ctx, (!fixture.connections().empty() && fixture.connections().client_idle(*a.client) &&
        fixture.connections().client_idle(*b.client) && !fixture.connections().client_idle(*requester.client)));
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        host::SLifecycleMessage discarded;
        TEST_EXPECT(ctx, (requester.client->channel->read_preamble_control(discarded) &&
            (discarded.operation == host::ELifecycle::stop)));
    }
    requester.deliver = true;
    fixture.step(4u);
    const modules::SExecutionNotification& result = requester.state.results[0];
    TEST_EXPECT(ctx, ((requester.state.result_count == 1u) && (result.correlation == 23) &&
        (result.connection_status == connections::EStatus::success) && (result.connection.value == token.value) &&
        (result.first.value == a.client->identity.value) && (result.second.value == b.client->identity.value) &&
        fixture.connections().empty()));
}

void common_reused_correlation(tests::TTestContext& ctx)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& peer = fixture.add(module_ids::audio, thread_ids::host);
    fixture.step(4u);
    for (unsigned index = 0u; index < 48u; ++index)
    {
        peer.state.result_count = 0u;
        fixture.request(peer, peer, peer, 7);
        fixture.step(4u);
        TEST_EXPECT(ctx, ((peer.state.result_count == 1u) && (peer.state.results[0].correlation == 7) &&
            (peer.state.results[0].connection_status == connections::EStatus::invalid_request) && fixture.connections().empty()));
    }
}

void common_authenticated_selection(tests::TTestContext& ctx, const bool pump)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, pump ? thread_ids::host : thread_ids::executive);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    SCommonMultiFixture::SPeer& c = fixture.add(module_ids::audio, thread_ids::host);
    const connections::SToken ab = fixture.establish(a, a, b);
    const connections::SToken ac = fixture.establish(a, a, c, 8);
    fixture.invoke(b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, ab },
            threading::make_pod_message(99, services::OpenServiceResult{}))));
    });
    fixture.step();
    TEST_EXPECT(ctx, (!a.state.last_source.is_host() && (a.state.last_source.connection.value == ab.value) &&
        a.state.last_message.is_payload_a<services::OpenServiceResult>()));
    a.state.allow_host = a.state.track_sources = a.state.pause_input = true;
    a.state.source_ids[0] = b.client->identity;
    a.state.source_ids[1] = c.client->identity;
    a.client->preamble->process();
    for (SCommonMultiFixture::SPeer* const peer : { &b, &c })
    {
        const connections::SToken token = (peer == &b) ? ab : ac;
        fixture.invoke(*peer, [&](const modules::CCommunication& communication) noexcept
        {
            for (std::int32_t index = 0; index < 6; ++index)
            {
                TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, token },
                    threading::make_pod_message(index, modules::RunTestsRequest{}))));
            }
        });
    }
    for (std::int32_t index = 0; index < 6; ++index)
    {
        TEST_EXPECT(ctx, a.client->post(threading::make_pod_message(index, modules::RunTestsRequest{})));
    }
    a.client->preamble->process();
    TEST_EXPECT(ctx, (a.state.valid && (a.state.source_counts[0] == 6) &&
        (a.state.source_counts[1] == 0) && (a.state.source_counts[2] == 0) && !a.client->preamble->work_pending()));
    for (std::int32_t index = 6; index < 12; ++index)
    {
        TEST_EXPECT(ctx, a.client->post(threading::make_pod_message(index, modules::RunTestsRequest{})));
    }
    a.state.pause_input = false;
    fixture.step(4u);
    // Pressure selection replaces the historical round-robin order. Every
    // source still preserves its own FIFO and receives a dispatch opportunity.
    TEST_EXPECT(ctx, (a.state.valid && (a.state.source_counts[0] == 12) &&
        (a.state.source_counts[1] == 6) && (a.state.source_counts[2] == 6)));
    a.state.track_sources = false;
    a.state.pause_input = a.state.hold = true;
    a.client->preamble->process();
    fixture.invoke(b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, ab },
            threading::make_pod_message(77, modules::RunTestsRequest{}))));
        modules::SExecutionIntent close;
        close.kind = modules::SExecutionIntent::EKind::close_connection;
        close.connection = ab;
        TEST_EXPECT(ctx, modules::accepted(communication.submit(close)));
    });
    fixture.step(4u);
    TEST_EXPECT(ctx, (a.state.closing.value == ab.value));
    fixture.invoke(a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.query({ modules::SDestination::EKind::connection, {}, ab }).active);
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, ac }).active);
    });
    a.state.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, !host::SDirectConnectionTestAccess::contains(fixture.connections(), ab));
    TEST_EXPECT(ctx, (a.state.received == 25u)); // Forged Host-shaped data + 24 ordinary records.
    fixture.close(c, ac);
}

struct SPrivateAdmittedOperation
{
    const host::SDirectEndpointDescriptor* endpoint{ nullptr };
    platform::system::CPerfCountConversion conversion;
    std::atomic<bool> entered{ false }, finish{ false }, posted{ false }, signalled{ false };
    static std::uint32_t MV_STD_ABI_CALL run(void* const context) noexcept
    {
        SPrivateAdmittedOperation& self = *static_cast<SPrivateAdmittedOperation*>(context);
        const host::CDirectOperation operation{ self.endpoint->admission };
        if (!operation) return 1u;
        self.posted.store(self.endpoint->outgoing->post(threading::make_pod_message(91, modules::RunTestsRequest{})) ==
            host::EModulePipePost::published, std::memory_order_release);
        self.entered.store(true, std::memory_order_release);
        if (!tests::wait_until(self.conversion, [&]() noexcept { return self.finish.load(std::memory_order_acquire); }))
        {
            std::fputs("Admitted signal fixture retained native operation at timeout.\n", stderr);
            std::_Exit(1);
        }
        self.signalled.store(self.endpoint->peer_notification.signal(), std::memory_order_release);
        return 0u;
    }
};

void common_close_during_signal(tests::TTestContext& ctx, const bool pump)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::executive);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, pump ? thread_ids::host : thread_ids::rendering);
    const connections::SToken token = fixture.establish(a, a, b);
    a.state.hold = true;
    SPrivateAdmittedOperation operation;
    TEST_EXPECT(ctx, operation.conversion.init());
    operation.endpoint = host::SDirectConnectionTestAccess::private_descriptor(fixture.connections(), token, 0u);
    TEST_EXPECT(ctx, operation.endpoint);
    if (!operation.endpoint) return;
    // Only this controlled native operation produces on A's pipe until join.
    // It exercises Host admission lifetime, not foreign-thread facade access.
    platform::threading::CThread worker;
    const bool created = worker.create(&SPrivateAdmittedOperation::run, &operation);
    TEST_EXPECT(ctx, created);
    if (!created) return;
    const bool entered = tests::wait_until(operation.conversion, [&]() noexcept { return operation.entered.load(std::memory_order_acquire); });
    TEST_EXPECT(ctx, entered);
    if (!entered) std::_Exit(1);
    fixture.connections().close(*b.client, token, connections::EStatus::cancelled);
    fixture.step(3u);
    TEST_EXPECT(ctx, (!operation.endpoint->admission->available() && !operation.endpoint->admission->drained()));
    {
        const host::CDirectOperation refused{ operation.endpoint->admission };
        TEST_EXPECT(ctx, !refused);
    }
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::contains(fixture.connections(), token) &&
        (a.client->endpoint->notification_pins == 1u) && (b.client->endpoint->notification_pins == 1u)));
    operation.finish.store(true, std::memory_order_release);
    const bool joined = worker.join_and_close();
    TEST_EXPECT(ctx, joined);
    if (!joined) std::_Exit(1);
    TEST_EXPECT(ctx, (operation.posted.load(std::memory_order_acquire) && operation.signalled.load(std::memory_order_acquire) &&
        operation.endpoint->admission->drained()));
    fixture.connections().progress();
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
    a.state.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, !host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
}

void common_deferred_quiescence(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(false, false, {});
    const host::SDirectEndpointDescriptor* const descriptor =
        host::SDirectConnectionTestAccess::private_descriptor(fixture.connections(), token, 0u);
    fixture.a.hold = true;
    fixture.connections().close(*fixture.second, token, connections::EStatus::cancelled);
    fixture.step(4u);
    host::SLifecycleMessage padding;
    padding.operation = host::ELifecycle::activated;
    padding.notification.connection = token;
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        TEST_EXPECT(ctx, fixture.first->channel->preamble_control(padding));
    }
    fixture.a.hold = false;
    fixture.first->preamble->process();
    TEST_EXPECT(ctx, (!fixture.first->preamble->work_pending() &&
        host::SDirectConnectionTestAccess::contains(fixture.connections(), token)));
    host::SLifecycleMessage stale;
    stale.operation = host::ELifecycle::activate;
    stale.notification.connection = token;
    TEST_EXPECT(ctx, fixture.first->channel->host_control(stale));
    stale.operation = host::ELifecycle::install;
    stale.descriptor = descriptor;
    TEST_EXPECT(ctx, fixture.first->channel->host_control(stale));
    fixture.first->preamble->process();
    TEST_EXPECT(ctx, ((fixture.a.available == 1u) && !fixture.first->preamble->failed()));
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        host::SLifecycleMessage discarded;
        TEST_EXPECT(ctx, (fixture.first->channel->read_host_control(discarded) &&
            (discarded.operation == host::ELifecycle::activated)));
    }
    fixture.step(8u);
    TEST_EXPECT(ctx, (fixture.connections().empty() && (fixture.a.retired == 1u)));
    // Exact-token lookup must reject this now-obsolete descriptor before read.
    TEST_EXPECT(ctx, fixture.first->channel->host_control(stale));
    fixture.first->preamble->process();
    TEST_EXPECT(ctx, (!fixture.first->preamble->failed() && (fixture.a.available == 1u) && !fixture.first->preamble->work_pending()));
}

void common_address_replacement(tests::TTestContext& ctx)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    fixture.step(4u);
    TEST_EXPECT(ctx, ((a.client->phase == host::EClientPhase::starting) &&
        (b.client->phase == host::EClientPhase::starting) && a.state.participated && b.state.participated));
    const connections::SToken previous = fixture.establish(a, a, b);
    fixture.close(a, previous);
    b.client->preamble->request_stop(modules::EExecutionStop::requested);
    fixture.step(16u);
    const bool detached = host::SModulePreambleTestAccess::detach_coordinator(fixture.runtime, *b.client, b.retained);
    TEST_EXPECT(ctx, detached);
    if (!detached) return;
    b.deliver = false;
    SCommonMultiFixture::SPeer& replacement = fixture.add(module_ids::rendering, thread_ids::host);
    const connections::SToken current = fixture.establish(a, a, replacement);
    TEST_EXPECT(ctx, ((current.value != previous.value) && (b.client->identity.value != replacement.client->identity.value)));
    fixture.connections().close(*b.client, current, connections::EStatus::cancelled);
    fixture.connections().close(*a.client, previous, connections::EStatus::cancelled);
    fixture.connections().quiescent(*b.client, current);
    fixture.invoke(a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, current }).active);
        TEST_EXPECT(ctx, (communication.submit({ modules::SDestination::EKind::peer, replacement.state.address, previous },
            threading::make_pod_message(7, modules::RunTestsRequest{})) == modules::ESubmission::no_pipe));
    });
    fixture.invoke(replacement, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::peer, a.state.address, current },
            threading::make_pod_message(7, modules::RunTestsRequest{}))));
    });
    fixture.step();
    TEST_EXPECT(ctx, (a.state.last_source.endpoint.value == replacement.client->identity.value));
    fixture.close(replacement, current);
}

void provider_borrow_destruction_gate(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &CPeerExecution::create;
    execution.handle = &CPeerExecution::dispatch;
    execution.destroy = &CPeerExecution::destroy;
    CPeerExecution state;
    state.allow_host = true;
    host::SClientRecord* const provider = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding, 3u, execution, &state);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    TEST_EXPECT(ctx, (provider && client));
    if (!provider || !client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    TEST_EXPECT(ctx, provider->preamble->activate());
    const auto turn = [&]()
    {
        provider->preamble->process();
        (void)host::SModulePreambleTestAccess::drain(runtime);
    };
    turn();
    turn();
    host::CServiceRouter& router = host::SModulePreambleTestAccess::router(runtime);
    const services::SServiceContract contract{ 1u, 1u };
    const host::SServiceProvider advertisement{ provider, &contract, 1u };
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(1u)));
        router.open({ module_ids::rendering, 1u, 1u, { 1u } }, 0, *client, &advertisement);
    }
    threading::CErasedPodMsg message;
    services::OpenServiceResult opened;
    TEST_EXPECT(ctx, (client->channel->read_preamble_pod(message) && message.copy_payload_to(opened)));
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 2u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(2u)));
        services::ServiceRequest request;
        request.route = opened.route;
        request.request = { 1u };
        request.opcode = 1u;
        router.request(request, 1, *client);
    }
    turn();
    turn();
    services::SServiceBorrow borrow;
    TEST_EXPECT(ctx, (router.prepare_borrow(*provider, { opened.route, { 1u }, { &state, sizeof(state), 1u } }, borrow) ==
        services::EServiceStatus::success));
    services::ServiceReply completed;
    completed.route = opened.route;
    completed.request = { 1u };
    completed.opcode = 1u;
    TEST_EXPECT(ctx, (router.complete(*provider, completed) == services::EServiceStatus::success));
    for (std::uint32_t count = 0u; count < 3u; ++count) turn();
    while (client->channel->read_preamble_pod(message)) {}
    provider->preamble->request_stop(modules::EExecutionStop::requested);
    for (std::uint32_t count = 0u; count < 6u; ++count) turn();
    TEST_EXPECT(ctx, (state.stopping && provider->seal_pending && !provider->seal_acknowledged && (state.destroyed == 0u)));
    TEST_EXPECT(ctx, !router.provider_borrows_retired(*provider));
    TEST_EXPECT(ctx, (router.begin_borrow_retirement(*provider, borrow) == services::EServiceStatus::success));
    turn();
    router.release_borrow(*client, { opened.route, borrow });
    router.quiesce_borrow(*client, { opened.route, borrow });
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, (state.destroyed == 0u));
    TEST_EXPECT(ctx, (router.finish_borrow_retirement(*provider, borrow) == services::EServiceStatus::success));
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, (router.provider_borrows_retired(*provider) && provider->seal_acknowledged && (state.destroyed == 1u)));
    TEST_EXPECT(ctx, ((state.created_on == state.destroyed_on) &&
        (state.destroyed_on == platform::threading::query_current_thread_id()) && !provider->preamble->failed()));
    host::SModulePreambleTestAccess::clear(runtime);
}

void standard_coordinator_controls(tests::TTestContext& ctx)
{
    // Host is the only producer of these private records. The fixture controls
    // that boundary; actual child launch/join is covered by the following case.
    for (unsigned scenario = 0u; scenario < 12u; ++scenario)
    {
        platform::threading::COwnerWait wake;
        TEST_EXPECT(ctx, (wake.initialise().code == platform::threading::EWaitError::none));
        threading::SModuleEndpointState module;
        threading::CEndpointResources resources{ module_ids::executable, module_ids::audio, thread_ids::host };
        resources.identity = { 751u };
        resources.instance = { 851u };
        resources.owner_wake = wake.endpoint();
        resources.module_state = &module;
        host::CModuleChannel channel{ resources };
        TEST_EXPECT(ctx, channel.initialise(*memory::get_ambient_memory_context(), 32u, 32u));
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::install(binding);
        modules::SExecutionV1 child_execution;
        child_execution.create = &CPeerExecution::create;
        child_execution.handle = &CPeerExecution::dispatch;
        child_execution.destroy = &CPeerExecution::destroy;
        const modules::SChildExecution child{ &child_execution, nullptr, thread_ids::audio };
        modules::SExecutionV1 execution;
        execution.initial_child = &child;
        host::CModulePreamble preamble{ resources, channel, binding, execution, *memory::get_ambient_memory_context() };
        TEST_EXPECT(ctx, preamble.activate());
        preamble.process();
        host::SLifecycleMessage request;
        TEST_EXPECT(ctx, (channel.read_host_control(request) && (request.operation == host::ELifecycle::create_child) &&
            (request.correlation == 0) && (request.child.descriptor == &child_execution)));
        TEST_EXPECT(ctx, (!channel.read_host_control(request) && !preamble.ready()));
        unsigned stops = 0u, readiness = 0u, seals = 0u, closures = 0u;
        const auto post = [&](const host::ELifecycle operation, const std::uint64_t endpoint = 951u,
            const services::EServiceStatus status = services::EServiceStatus::success)
        {
            host::SLifecycleMessage message;
            message.operation = operation;
            message.correlation = 0;
            message.notification.first = { endpoint };
            message.notification.status = status;
            TEST_EXPECT(ctx, channel.host_control(message));
        };
        const auto controls = [&]()
        {
            host::SLifecycleMessage message;
            while (channel.read_host_control(message))
            {
                host::SLifecycleMessage response;
                switch (message.operation)
                {
                    case host::ELifecycle::stop_child:
                    {
                        ++stops;
                        TEST_EXPECT(ctx, (message.notification.first.value == 951u));
                        response.operation = host::ELifecycle::child_stop_result;
                        response.correlation = message.correlation;
                        response.notification = message.notification;
                        TEST_EXPECT(ctx, channel.host_control(response));
                        break;
                    }
                    case host::ELifecycle::close_provider_admission:
                    {
                        ++closures;
                        response.operation = host::ELifecycle::provider_admission_closed;
                        TEST_EXPECT(ctx, channel.host_control(response));
                        break;
                    }
                    case host::ELifecycle::seal:
                    {
                        ++seals;
                        response.operation = host::ELifecycle::sealed;
                        TEST_EXPECT(ctx, channel.host_control(response));
                        break;
                    }
                    case host::ELifecycle::ready:
                    {
                        ++readiness;
                        break;
                    }
                    case host::ELifecycle::stop_module:
                    {
                        break;
                    }
                    default:
                    {
                        TEST_EXPECT(ctx, false);
                        break;
                    }
                }
            }
        };
        const bool creation_failed = (scenario == 3u) || (scenario == 7u);
        const bool invalid = (scenario == 2u) || (scenario >= 8u);
        if ((scenario == 6u) || (scenario == 7u))
        {
            preamble.request_stop(modules::EExecutionStop::requested);
            preamble.process();
            controls();
            TEST_EXPECT(ctx, (!stops && !seals && !preamble.destroyed()));
        }
        if (scenario == 8u)
        {
            post(host::ELifecycle::child_ready);
            preamble.process();
            TEST_EXPECT(ctx, (preamble.failed() && !preamble.ready()));
        }
        post(host::ELifecycle::child_created, creation_failed ? 0u : 951u,
            creation_failed ? services::EServiceStatus::native_failure : services::EServiceStatus::success);
        if (scenario != 1u) preamble.process();
        TEST_EXPECT(ctx, !preamble.ready());
        if (scenario == 4u) post(host::ELifecycle::child_ended);
        if (scenario == 5u) module.stopping.store(1u, std::memory_order_release);
        if ((scenario <= 5u) && !creation_failed)
        {
            post(host::ELifecycle::child_ready, (scenario == 2u) ? 952u : 951u);
        }
        if (scenario == 9u) post(host::ELifecycle::child_created);
        if (scenario == 10u) post(host::ELifecycle::child_ended, 952u);
        if (scenario == 11u) post(host::ELifecycle::child_retired, 952u);
        preamble.process();
        controls();
        TEST_EXPECT(ctx, (preamble.ready() == (scenario <= 1u)));
        TEST_EXPECT(ctx, (preamble.failed() == (invalid || (scenario == 3u))));
        preamble.request_stop(modules::EExecutionStop::requested);
        preamble.process();
        controls();
        if (!creation_failed)
        {
            TEST_EXPECT(ctx, (!seals && !preamble.destroyed()));
            if (scenario != 4u) post(host::ELifecycle::child_ended);
            post(host::ELifecycle::child_retired);
        }
        for (unsigned turn = 0u; turn < 8u; ++turn)
        {
            preamble.process();
            controls();
        }
        TEST_EXPECT(ctx, (preamble.destroyed() && (seals == 1u) && (closures == 1u)));
        TEST_EXPECT(ctx, (readiness == ((scenario <= 1u) ? 1u : 0u)));
        TEST_EXPECT(ctx, (stops == ((creation_failed || (scenario == 4u)) ? 0u : 1u)));
    }
}

void standard_coordinator_child(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    TEST_EXPECT(ctx, host::SModulePreambleTestAccess::prepare_threads(runtime));
    modules::SExecutionV1 child_execution;
    child_execution.create = &CPeerExecution::create;
    child_execution.handle = &CPeerExecution::dispatch;
    child_execution.destroy = &CPeerExecution::destroy;
    CPeerExecution child_state;
    const modules::SChildExecution child{ &child_execution, &child_state, thread_ids::audio };
    modules::SExecutionV1 coordinator_execution;
    coordinator_execution.initial_child = &child;
    host::SClientRecord* const coordinator = host::SModulePreambleTestAccess::install(runtime,
        module_ids::audio, binding, 3u, coordinator_execution);
    TEST_EXPECT(ctx, (coordinator && coordinator->preamble->activate()));
    if (!coordinator)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    threading::CWaitDeadline deadline;
    TEST_EXPECT(ctx, (deadline.set_after_milliseconds(5000u).code == platform::threading::EWaitError::none));
    bool ready = false;
    while (!coordinator->preamble->destroyed())
    {
        coordinator->preamble->process();
        host::SModulePreambleTestAccess::progress_threads(runtime);
        if (!ready && coordinator->preamble->ready())
        {
            ready = true;
            TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::children_idle(runtime, module_ids::audio));
            coordinator->preamble->request_stop(modules::EExecutionStop::requested);
        }
        std::uint64_t remaining = 0u;
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            break;
        }
        threading::CWaitDeadline pause;
        (void)pause.set_after_milliseconds(1u);
        std::atomic<std::uint32_t> unchanged{ 0u };
        (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
    }
    TEST_EXPECT(ctx, (ready && coordinator->preamble->destroyed() && !coordinator->preamble->failed()));
    TEST_EXPECT(ctx, host::SModulePreambleTestAccess::children_idle(runtime, module_ids::audio));
    //  Successful retirement includes native join before these callback writes
    //  are inspected on Host. The standard coordinator waits for that outcome.
    TEST_EXPECT(ctx, ((child_state.destroyed == 1u) && (child_state.created_on == child_state.destroyed_on) &&
        (child_state.created_on != platform::threading::query_current_thread_id())));
    host::SModulePreambleTestAccess::clear(runtime);
}

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

void module_dll_execution(tests::TTestContext& ctx, const module_ids::id_type module, const char* const stem,
    const thread_ids::id_type service_thread)
{
    TInstance<debug_system::CDebugServiceState> debug_service = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, debug_service.is_ready());
    if (!debug_service) return;
    const std::string log = test_environment::test_log_path(stem);
    const std::string direct_log = test_environment::test_log_path((std::string{ stem } + "_direct").c_str());
    TEST_EXPECT(ctx, (debug_service->configure_log_paths(log.c_str(), direct_log.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "preamble_test" })));
    const system_ids::id_type caller_identity = system_context::get_ambient_system_id();
    memory::CMemoryContext* const caller_memory = memory::get_ambient_memory_context();
    const platform::threading::CPlatformThreadId caller_thread = platform::threading::query_current_thread_id();
    memory::CMemoryAllocator host_allocator{ nullptr, &tests::allocate_scalar_test_memory,
        &tests::deallocate_scalar_test_memory, system_ids::host };
    SDllAllocation attribution;
    const system_ids::id_type module_identity = system_ids::ops::make_system_id(module, service_thread);
    memory::CMemoryAllocator module_allocator{ &attribution, &SDllAllocation::allocate, &SDllAllocation::free, module_identity };
    memory::CMemoryContext module_memory{ module_allocator, module_identity };
    //  Owning transfers require the same allocator on both sides, as in
    //  production Host. Keep distinct attribution contexts for each image.
    memory::CMemoryContext shared_host_memory{ module_allocator, system_ids::host };
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string filename = test_environment::binary_path(test_environment::module_filename(stem).c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
    const bool bound = path.is_ready() && binding.bind(path, module, host_identity);
    const bool installed = bound && binding.install(test_environment::system_registry_view(), module, &module_memory,
        system_context::sequence_counter, debug_service.operator->());
    TEST_EXPECT(ctx, installed);
    if (!installed)
    {
        std::cerr << "DLL fixture installation failed: " << stem << ", bound " << bound << '\n';
        TEST_EXPECT(ctx, binding.unbind());
        TEST_EXPECT(ctx, debug_service->stop());
        return;
    }
    modules::SExecutionV1 advertised;
    TEST_EXPECT(ctx, (binding.query_execution(advertised) == modules::EBindingResult::success));
    //  Bootstrap is once per binding, including when the ELF loader retains an
    //  image after dlclose. Exercise fresh contexts under that single binding.
    const unsigned cycles = ((module == module_ids::audio) || (module == module_ids::rendering)) ? 3u : 1u;
    for (unsigned cycle = 0u; cycle < cycles; ++cycle)
    {
        const bool serviced = (cycle == 1u) || (module == module_ids::conditioning_general);
        const bool failure = cycle == 2u;
        const bool exercise = (module == module_ids::rendering) && !failure;
        const bool empty = module_memory.is_attribution_empty() && shared_host_memory.is_attribution_empty();
        TEST_EXPECT(ctx, empty);
        if (!empty)
        {
            std::cerr << "DLL fixture retained allocations before next execution: " << stem << '\n';
            std::_Exit(1);
        }
        attribution.allocations.store(0u);
        attribution.frees.store(0u);
        attribution.primary = nullptr;
        attribution.allocated_on = platform::threading::CPlatformThreadId{};
        attribution.freed_on = platform::threading::CPlatformThreadId{};
        attribution.track_primary = !exercise;
        tests::TMemoryContextScope host_scope{ exercise ? &shared_host_memory : caller_memory };
        modules::SExecutionV1 execution = advertised;
        if (module == module_ids::platform)
        {
            TEST_EXPECT(ctx, (execution.create && (execution.flags == modules::k_execution_native_events)));
        }
        else
        {
            TEST_EXPECT(ctx, (execution.initial_child && execution.initial_child->descriptor));
            if (!execution.initial_child || !execution.initial_child->descriptor)
            {
                continue;
            }
            if (!serviced) execution = *execution.initial_child->descriptor;
        }
        host::CHost runtime{ host_allocator };
        TEST_EXPECT(ctx, host::SModulePreambleTestAccess::prepare_threads(runtime));
        host::SClientRecord* const coordinator = host::SModulePreambleTestAccess::install(runtime,
            module, binding, 3u, execution);
        TEST_EXPECT(ctx, coordinator);
        const bool batch_ready = coordinator && host::SModulePreambleTestAccess::prepare_batch(runtime, *coordinator, binding);
        TEST_EXPECT(ctx, batch_ready);
        const bool activated = batch_ready && coordinator->preamble->activate();
        TEST_EXPECT(ctx, activated);
        if (!activated)
        {
            std::cerr << "DLL execution activation failed: " << stem << ", batch " << batch_ready
                << ", allocations " << attribution.allocations << ", frees " << attribution.frees << '\n';
        }
        if (!coordinator)
        {
            host::SModulePreambleTestAccess::clear(runtime);
            continue;
        }
        modules::CBoundModule requester_binding;
        modules::SBoundModuleTestAccess::install(requester_binding);
        CPeerExecution requester_state;
        requester_state.test_module = module;
        requester_state.peer = system_ids::ops::make_system_id(module, serviced ? service_thread : thread_ids::host);
        modules::SExecutionV1 requester_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
            &CPeerExecution::create, &CPeerExecution::dispatch, &CPeerExecution::destroy };
        const modules::SChildExecution requester_child{ &requester_execution, &requester_state, thread_ids::executive };
        modules::SExecutionV1 requester_coordinator;
        requester_coordinator.initial_child = &requester_child;
        host::SClientRecord* requester = nullptr;
        if (exercise)
        {
            TEST_EXPECT(ctx, host::SModulePreambleTestAccess::start_batch(runtime));
            requester = host::SModulePreambleTestAccess::install(runtime, module_ids::executive,
                requester_binding, 3u, requester_coordinator);
            TEST_EXPECT(ctx, (requester && requester->preamble->activate()));
        }
        threading::CWaitDeadline deadline;
        TEST_EXPECT(ctx, (deadline.set_after_milliseconds(exercise ? 60000u : 5000u).code == platform::threading::EWaitError::none));
        bool ready = false, stopping = false, timed_out = false;
        while (!coordinator->preamble->destroyed() || (requester && !requester->preamble->destroyed()))
        {
            coordinator->preamble->process();
            if (requester) requester->preamble->process();
            host::SModulePreambleTestAccess::progress_threads(runtime);
            if (exercise)
            {
                host::SModulePreambleTestAccess::connections(runtime).progress();
                host::SModulePreambleTestAccess::assets(runtime).complete_disposals();
            }
            TEST_EXPECT(ctx, ((system_context::get_ambient_system_id() == caller_identity) &&
                (memory::get_ambient_memory_context() == (exercise ? &shared_host_memory : caller_memory))));
            if (!ready && coordinator->preamble->ready())
            {
                ready = true;
                TEST_EXPECT(ctx, ((module_memory.get_live_allocation_count() != 0u) && (attribution.allocations != 0u)));
                if (module == module_ids::audio)
                {
                    //  This is the class allocation only: idle Audio never opens a device.
                    TEST_EXPECT(ctx, ((module_memory.get_live_allocation_count() == 1u) && (attribution.allocations == 1u)));
                }
                if (exercise)
                {
                    requester_state.test_start.store(true, std::memory_order_release);
                    host::SModulePreambleTestAccess::wake(runtime, system_ids::executive);
                }
                if (failure)
                {
                    TEST_EXPECT(ctx, (coordinator->channel->to_module.post(threading::make_pod_message(-1,
                        modules::RunTestsRequest{ module })) == host::EModulePipePost::published));
                }
            }
            if (ready && !stopping && !failure && (!exercise || requester_state.test_done.load(std::memory_order_acquire)))
            {
                stopping = true;
                coordinator->preamble->request_stop(modules::EExecutionStop::requested);
                if (requester) requester->preamble->request_stop(modules::EExecutionStop::requested);
            }
            std::uint64_t remaining = 0u;
            if ((!failure && !timed_out && (coordinator->endpoint->failure.load(std::memory_order_acquire) != threading::EEndpointFailure::none)) ||
                (deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                TEST_EXPECT(ctx, false);
                if (timed_out)
                {
                    //  Do not destroy affine live DLL state on the test owner.
                    std::cerr << "DLL fixture could not retire after timeout: " << stem << '\n';
                    std::_Exit(1);
                }
                timed_out = stopping = true;
                coordinator->preamble->request_stop(modules::EExecutionStop::requested);
                if (requester) requester->preamble->request_stop(modules::EExecutionStop::requested);
                (void)deadline.set_after_milliseconds(5000u);
            }
            threading::CWaitDeadline pause;
            (void)pause.set_after_milliseconds(1u);
            std::atomic<std::uint32_t> unchanged{ 0u };
            (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
        }
        TEST_EXPECT(ctx, (ready && coordinator->preamble->destroyed() && (coordinator->preamble->failed() == failure)));
        TEST_EXPECT(ctx, host::SModulePreambleTestAccess::children_idle(runtime, module));
        if (exercise)
        {
            TEST_EXPECT(ctx, (requester_state.test_done.load(std::memory_order_acquire) &&
                host::SModulePreambleTestAccess::children_idle(runtime, module_ids::executive) && requester_state.valid));
            TEST_EXPECT(ctx, host::SModulePreambleTestAccess::stop_batch(runtime));
            TEST_EXPECT(ctx, host::SModulePreambleTestAccess::assets(runtime).is_idle());
        }
        TEST_EXPECT(ctx, module_memory.is_attribution_empty());
        if (!exercise)
        {
            TEST_EXPECT(ctx, (attribution.frees == attribution.allocations));
            TEST_EXPECT(ctx, ((attribution.allocated_on == attribution.freed_on) &&
                ((attribution.allocated_on == caller_thread) != serviced)));
        }
        host::SModulePreambleTestAccess::clear(runtime);
    }
    TEST_EXPECT(ctx, (module_memory.is_attribution_empty() && shared_host_memory.is_attribution_empty()));
    TEST_EXPECT(ctx, binding.unbind());
    TEST_EXPECT(ctx, debug_service->stop());
}

struct SExecutiveFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    unsigned owner_attempts{ 0u }, owners{ 0u }, requests{ 0u }, connects{ 0u }, closes{ 0u }, quiescences{ 0u };
    std::int32_t load_slot{ -1 }, connect_slot{ -1 }, request_slot{ -1 };
    bool valid{ true };
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SExecutiveFacade& self = *static_cast<SExecutiveFacade*>(cookie);
        ++self.owner_attempts;
        self.valid = self.valid && (destination.kind == modules::SDestination::EKind::host) &&
            (message.owner().payload<modules::ModuleRequest>() != nullptr);
        if (modules::accepted(self.result))
        {
            self.load_slot = message.query_async_slot();
            ++self.owners;
            message = {};
        }
        return self.result;
    }
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SExecutiveFacade& self = *static_cast<SExecutiveFacade*>(cookie);
        self.valid = self.valid && (destination.kind == modules::SDestination::EKind::connection) &&
            (destination.token.value == 77u) && message.is_payload_a<modules::RunTestsRequest>();
        if (modules::accepted(self.result))
        {
            self.request_slot = message.query_async_slot();
            ++self.requests;
        }
        return self.result;
    }
    static modules::ESubmission MV_STD_ABI_CALL intent(void* const cookie, const modules::SExecutionIntent& intent) noexcept
    {
        SExecutiveFacade& self = *static_cast<SExecutiveFacade*>(cookie);
        switch (intent.kind)
        {
            case modules::SExecutionIntent::EKind::connect:
            {
                ++self.connects;
                self.connect_slot = intent.correlation;
                break;
            }
            case modules::SExecutionIntent::EKind::close_connection:
            {
                ++self.closes;
                break;
            }
            case modules::SExecutionIntent::EKind::connection_quiescent:
            {
                ++self.quiescences;
                break;
            }
            default:
            {
                break;
            }
        }
        return modules::ESubmission::accepted;
    }
    static modules::SConnectionQuery MV_STD_ABI_CALL query(void*, const modules::SDestination& destination) noexcept
    {
        return { destination.token, modules::test_connections::address(module_ids::rendering), 1u, 0u };
    }
};

void executive_dll_retries(tests::TTestContext& ctx)
{
    TInstance<debug_system::CDebugServiceState> debug_service = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, debug_service.is_ready());
    if (!debug_service) return;
    const std::string log = test_environment::test_log_path("ExecutiveRetries");
    const std::string direct = test_environment::test_log_path("ExecutiveRetries_direct");
    TEST_EXPECT(ctx, (debug_service->configure_log_paths(log.c_str(), direct.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "executive_retry" })));
    const system_ids::id_type caller_identity = system_context::get_ambient_system_id();
    memory::CMemoryContext* const caller_memory = memory::get_ambient_memory_context();
    SDllAllocation attribution;
    memory::CMemoryAllocator allocator{ &attribution, &SDllAllocation::allocate, &SDllAllocation::free, system_ids::executive };
    memory::CMemoryContext module_memory{ allocator, system_ids::executive };
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string filename = test_environment::binary_path(test_environment::module_filename("MorphicExecutive").c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
    const bool installed = path.is_ready() && binding.bind(path, module_ids::executive, host_identity) &&
        binding.install(test_environment::system_registry_view(), module_ids::executive, &module_memory,
            system_context::sequence_counter, debug_service.operator->());
    TEST_EXPECT(ctx, installed);
    if (installed)
    {
        modules::SExecutionV1 coordinator;
        TEST_EXPECT(ctx, (binding.query_execution(coordinator) == modules::EBindingResult::success));
        TEST_EXPECT(ctx, (coordinator.initial_child && coordinator.initial_child->descriptor));
        if (coordinator.initial_child && coordinator.initial_child->descriptor)
        {
            const modules::SExecutionV1 execution = *coordinator.initial_child->descriptor;
            static const modules::SCommunicationV1 functions{ &SExecutiveFacade::pod, &SExecutiveFacade::owner,
                &SExecutiveFacade::intent, &SExecutiveFacade::query };
            for (const bool early_stop : { true, false })
            {
                SExecutiveFacade state;
                modules::CCommunication communication{ functions, &state };
                modules::CHostRestrictionPublication restrictions;
                modules::SHostRestrictionSnapshot snapshot;
                snapshot.generation = 1u;
                snapshot.sampling = 1u;
                TEST_EXPECT(ctx, restrictions.publish(snapshot));
                std::atomic<std::uint32_t> work{ 0u };
                threading::CNotification notification;
                TEST_EXPECT(ctx, notification.initialise(work));
                modules::SHostExecutionContext host_execution;
                host_execution.restriction = restrictions.reader();
                host_execution.notification = notification.reader();
                modules::SExecutionContext context;
                context.address = system_ids::executive;
                context.communication = &communication;
                context.execution = &host_execution;
                void* runtime = nullptr;
                TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
                TEST_EXPECT(ctx, (execution.create(&context, &runtime) == modules::EExecutionStatus::success));
                TEST_EXPECT(ctx, runtime);
                if (!runtime) continue;
                modules::SExecutionProgress progress;
                const auto invoke = [&](const modules::EExecutionEvent event, const modules::SExecutionNotification detail = {},
                    const threading::CErasedPodMsg* const message = nullptr, const connections::SSource source = {})
                {
                    modules::SInvocation invocation;
                    invocation.event = event;
                    invocation.notification = detail;
                    invocation.message = message;
                    invocation.source = source;
                    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
                    TEST_EXPECT(ctx, (execution.handle(runtime, &invocation, &progress) == modules::EExecutionStatus::success));
                    TEST_EXPECT(ctx, ((system_context::get_ambient_system_id() == caller_identity) &&
                        (memory::get_ambient_memory_context() == caller_memory)));
                };
                invoke(modules::EExecutionEvent::start);
                TEST_EXPECT(ctx, ((state.owner_attempts == 0u) && !progress.work_pending &&
                    (progress.next_wake_ns == executive::k_timing_retry_ns)));
                if (!early_stop)
                {
                    snapshot.sampling = 0u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.owner_attempts == 1u) && (state.owners == 0u) && !progress.work_pending));
                    state.result = modules::ESubmission::accepted;
                    invoke(modules::EExecutionEvent::service_async);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.owners == 1u) && (state.owner_attempts == 2u)));
                    snapshot.sampling = 1u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    modules::ModuleResult loaded;
                    loaded.module = module_ids::rendering;
                    loaded.status = modules::EModuleStatus::success;
                    loaded.notice = modules::EModuleNotice::acknowledged;
                    threading::CErasedPodMsg message = threading::make_pod_message(state.load_slot, loaded);
                    connections::SSource source;
                    source.kind = connections::SSource::EKind::host;
                    source.address = system_ids::host;
                    invoke(modules::EExecutionEvent::message, {}, &message, source);
                    loaded.notice = modules::EModuleNotice::completed;
                    loaded.available = true;
                    message = threading::make_pod_message(state.load_slot, loaded);
                    invoke(modules::EExecutionEvent::message, {}, &message, source);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.connects == 0u) && !progress.work_pending &&
                        (progress.next_wake_ns == executive::k_timing_retry_ns)));
                    snapshot.sampling = 0u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, (state.connects == 1u));
                    snapshot.sampling = 1u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    modules::SExecutionNotification connected;
                    connected.correlation = state.connect_slot;
                    connected.connection = { 77u };
                    connected.connection_status = connections::EStatus::success;
                    invoke(modules::EExecutionEvent::connection_result, connected);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.requests == 0u) && !progress.work_pending));
                    snapshot.sampling = 0u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    state.result = modules::ESubmission::published_wake_failed;
                    invoke(modules::EExecutionEvent::service_async);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, (state.requests == 1u));
                    snapshot.sampling = 1u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    message = threading::make_pod_message(state.request_slot,
                        modules::RunTestsResult{ module_ids::rendering, modules::ETestRunStatus::success, 23u, 0u, 0u });
                    source.kind = connections::SSource::EKind::direct;
                    source.address = modules::test_connections::address(module_ids::rendering);
                    source.connection = connected.connection;
                    invoke(modules::EExecutionEvent::message, {}, &message, source);
                    TEST_EXPECT(ctx, (state.closes == 1u));
                    invoke(modules::EExecutionEvent::connection_closing, connected);
                    invoke(modules::EExecutionEvent::connection_retired, connected);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.quiescences == 1u) && (state.owner_attempts == 2u) &&
                        !progress.work_pending && (progress.next_wake_ns == executive::k_timing_retry_ns)));
                }
                invoke(modules::EExecutionEvent::stop);
                TEST_EXPECT(ctx, (progress.work_complete && !progress.work_pending && (progress.next_wake_ns == UINT64_MAX)));
                TEST_EXPECT(ctx, (execution.destroy(runtime) == modules::EExecutionStatus::success));
                TEST_EXPECT(ctx, (state.valid && module_memory.is_attribution_empty()));
            }
        }
        TEST_EXPECT(ctx, binding.unbind());
    }
    TEST_EXPECT(ctx, (attribution.frees == attribution.allocations));
    TEST_EXPECT(ctx, debug_service->stop());
}

struct SFailureExecution
{
    static inline SFailureExecution* next{ nullptr };
    modules::CCommunication communication;
    bool refuse_create{}, fail_start{}, fail_message{}, fill_output{}, stopping{}, allow_completion{}, participate{};
    bool refuse_destroy{}, fail_create_with_state{};
    unsigned created{}, destroyed{}, published{}, handled{};
    platform::threading::CPlatformThreadId created_on, destroyed_on;
    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        *runtime = nullptr;
        if (next->refuse_create) return modules::EExecutionStatus::failed;
        next->communication = *context->communication;
        next->created_on = platform::threading::query_current_thread_id();
        ++next->created;
        *runtime = next;
        return next->fail_create_with_state ? modules::EExecutionStatus::failed : modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        SFailureExecution& self = *static_cast<SFailureExecution*>(runtime);
        ++self.handled;
        *progress = {};
        if (invocation->event == modules::EExecutionEvent::stop) self.stopping = true;
        if (self.participate && (invocation->event == modules::EExecutionEvent::start))
        {
            modules::SExecutionIntent request;
            request.kind = modules::SExecutionIntent::EKind::participate;
            request.correlation = 0;
            if (!modules::accepted(self.communication.submit(request)))
            {
                return modules::EExecutionStatus::failed;
            }
        }
        if (self.fail_start && (invocation->event == modules::EExecutionEvent::start))
        {
            self.fail_start = false;
            return modules::EExecutionStatus::failed;
        }
        if (self.fill_output && !self.stopping)
        {
            const auto filler = threading::make_pod_message(-1, platform::reports::PlatformReportCaptureFailed{});
            for (unsigned index = 0u; index <= host::k_module_pod_capacity; ++index)
            {
                const modules::ESubmission status = self.communication.submit({}, filler);
                if (status == modules::ESubmission::busy) break;
                if (status != modules::ESubmission::accepted)
                {
                    return modules::EExecutionStatus::failed;
                }
                ++self.published;
            }
            self.fill_output = false;
        }
        if (self.fail_message && (invocation->event == modules::EExecutionEvent::message))
        {
            self.fail_message = false;
            return modules::EExecutionStatus::failed;
        }
        progress->ready = self.stopping ? 0u : 1u;
        progress->work_complete = (self.stopping && self.allow_completion) ? 1u : 0u;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        SFailureExecution& self = *static_cast<SFailureExecution*>(runtime);
        ++self.destroyed;
        self.destroyed_on = platform::threading::query_current_thread_id();
        return self.refuse_destroy ? modules::EExecutionStatus::failed : modules::EExecutionStatus::success;
    }
    static modules::SExecutionV1 descriptor() noexcept
    {
        return { sizeof(modules::SExecutionV1), 1u, 0u, &create, &handle, &destroy };
    }
};

void pumped_destroy_refusal(tests::TTestContext& ctx)
{
    using Access = host::SModulePreambleTestAccess;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    SFailureExecution retained, other;
    retained.allow_completion = retained.refuse_destroy = true;
    SFailureExecution::next = &retained;
    host::SClientRecord* const client = Access::driver(runtime, module_ids::rendering, binding, SFailureExecution::descriptor());
    SFailureExecution::next = &other;
    host::SClientRecord* const unrelated = Access::driver(runtime, module_ids::audio, binding, SFailureExecution::descriptor());
    tests::SCommonEndpointFixture requester{ module_ids::executive, 700u, 32u };
    TEST_EXPECT(ctx, (client && unrelated && Access::assets(runtime).initialise() && Access::prepare_turns(runtime)));
    if (!client || !unrelated)
    {
        Access::clear(runtime);
        return;
    }
    TEST_EXPECT(ctx, requester.reserve_replies(1u));
    Access::pending_unload(runtime, requester, binding, module_ids::rendering);
    host::CModuleService& service = Access::module_service(runtime);
    for (unsigned turn = 0u; turn < 20u; ++turn) Access::turn(runtime);
    TEST_EXPECT(ctx, (Access::emergency(runtime) && Access::retaining(runtime) && !Access::complete(runtime)));
    TEST_EXPECT(ctx, (client->preamble->destroy_refused() && client->preamble->retained() &&
        client->preamble->failed() && !client->preamble->destroyed() && !client->endpoint->sealed &&
        !client->execution_quiescent() && client->process_retention_eligible()));
    TEST_EXPECT(ctx, ((retained.destroyed == 1u) && (retained.created_on == retained.destroyed_on) &&
        client->subscription_live && service.output_ready(requester) && (requester.reserved_replies == 1u)));
    const unsigned handled = retained.handled;
    for (unsigned turn = 0u; turn < 8u; ++turn) Access::turn(runtime);
    service.retry_output();
    service.retry_output();
    modules::ModuleResult result;
    TEST_EXPECT(ctx, ((requester.pending() == 1u) && (requester.received(0).query_async_slot() == 94) &&
        requester.received(0).copy_payload_to(result) && (result.status == modules::EModuleStatus::unload_failed) &&
        (result.module == module_ids::rendering) && !requester.reserved_replies && service.is_idle()));
    requester.discard(requester.pending());
    // An unrelated live execution still blocks final process retention.
    Access::lifecycle(runtime);
    TEST_EXPECT(ctx, (!Access::complete(runtime) && !other.destroyed));
    other.allow_completion = true;
    for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
    TEST_EXPECT(ctx, (other.destroyed == 1u));
    TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
    // Controlled outer-frame hold: even a returned destroy callback does not
    // permit process completion while its caller can still access the graph.
    client->invocation_active = true;
    TEST_EXPECT(ctx, !client->process_retention_eligible());
    Access::lifecycle(runtime);
    TEST_EXPECT(ctx, !Access::complete(runtime));
    client->invocation_active = false;
    client->destroy_refusal_unwound = false;
    TEST_EXPECT(ctx, !client->process_retention_eligible());
    client->destroy_refusal_unwound = true;
    ++client->endpoint->published_pods;
    TEST_EXPECT(ctx, !client->process_retention_eligible());
    --client->endpoint->published_pods;
    client->incoming_drained = false;
    TEST_EXPECT(ctx, !client->process_retention_eligible());
    client->incoming_drained = true;
    Access::lifecycle(runtime);
    TEST_EXPECT(ctx, Access::complete(runtime));
    TEST_EXPECT(ctx, ((Access::endpoint(runtime, system_ids::ops::make_system_id(module_ids::rendering, thread_ids::host)) == client) &&
        (retained.destroyed == 1u) && (retained.handled == handled) && !requester.pending() &&
        client->subscription_live && !client->retire_host_execution(true) && !client->execution_quiescent()));
    // This fixture owns static callback state and a fake binding. Dismantling
    // its controlled graph is not the production retained-process teardown.
    Access::clear(runtime);

    host::CHost unsafe_host{ allocator };
    SFailureExecution unsafe;
    unsafe.fail_create_with_state = true;
    SFailureExecution::next = &unsafe;
    host::SClientRecord* const unsafe_client = Access::driver(unsafe_host, module_ids::rendering, binding, SFailureExecution::descriptor());
    TEST_EXPECT(ctx, unsafe_client);
    if (unsafe_client)
    {
        Access::pump(unsafe_host);
        TEST_EXPECT(ctx, (unsafe_client->preamble->retained() && !unsafe_client->preamble->destroy_refused() &&
            !unsafe_client->process_retention_eligible() && !unsafe_client->execution_quiescent()));
        TEST_EXPECT(ctx, (!Access::retire(unsafe_host, module_ids::rendering) && !Access::retaining(unsafe_host) && !unsafe.destroyed));
    }
    Access::clear(unsafe_host);
}

void global_failure_shutdown(tests::TTestContext& ctx)
{
    using Access = host::SModulePreambleTestAccess;
    const modules::SExecutionV1 execution = SFailureExecution::descriptor();
    for (const module_ids::id_type target : { module_ids::executive, module_ids::rendering })
    {
        //  Both initial binding and replacement can fail before any ready event.
        for (unsigned pending_case = 0u; pending_case < 4u; ++pending_case)
        {
            const bool replacement = (pending_case & 1u) != 0u;
            const bool refuse_create = pending_case < 2u;
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            host::CHost runtime{ allocator };
            modules::CBoundModule binding;
            modules::SBoundModuleTestAccess::install(binding);
            modules::SBoundModuleTestAccess::identity(binding, target);
            SFailureExecution unaffected, failed;
            SFailureExecution::next = &unaffected;
            host::SClientRecord* const other = Access::driver(runtime, module_ids::audio, binding, execution);
            TEST_EXPECT(ctx, other);
            Access::pump(runtime);
            failed.refuse_create = refuse_create;
            failed.fail_start = !refuse_create;
            SFailureExecution::next = &failed;
            Access::pending(runtime, binding, execution, target, replacement);
            if (!refuse_create) Access::pump(runtime);
            //  A failed create escalates immediately; a failed startup callback
            //  escalates on that pump, without a later worker result or readiness.
            TEST_EXPECT(ctx, (Access::emergency(runtime) && other->stop_requested));
            for (unsigned turn = 0u; turn < 8u; ++turn) Access::pump(runtime);
            TEST_EXPECT(ctx, (unaffected.stopping && !unaffected.destroyed &&
                (failed.created == (refuse_create ? 0u : 1u)) && !failed.destroyed));
            failed.allow_completion = unaffected.allow_completion = true;
            for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
            TEST_EXPECT(ctx, ((unaffected.destroyed == 1u) && (unaffected.created_on == unaffected.destroyed_on)));
            TEST_EXPECT(ctx, (failed.destroyed == (refuse_create ? 0u : 1u)));
            TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
            TEST_EXPECT(ctx, Access::retire(runtime, target));
            Access::finish_pending(runtime);
            Access::clear(runtime);
        }
        for (const bool lost_reply : { false, true })
        {
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            host::CHost runtime{ allocator };
            modules::CBoundModule binding;
            modules::SBoundModuleTestAccess::install(binding);
            SFailureExecution unaffected, failed;
            SFailureExecution::next = &unaffected;
            host::SClientRecord* const other = Access::driver(runtime, module_ids::audio, binding, execution);
            SFailureExecution::next = &failed;
            host::SClientRecord* const client = Access::driver(runtime, target, binding, execution);
            TEST_EXPECT(ctx, (other && client));
            Access::pump(runtime);
            TEST_EXPECT(ctx, (client->preamble->ready() && !Access::emergency(runtime)));
            const auto filler = threading::make_pod_message(-1, modules::RunTestsResult{});
            if (lost_reply)
            {
                failed.fill_output = true;
                Access::pump_one(runtime, target);
                for (unsigned index = 0u; index < host::k_module_pod_capacity; ++index)
                {
                    TEST_EXPECT(ctx, (client->channel->to_module.post(filler) == host::EModulePipePost::published));
                }
                TEST_EXPECT(ctx, ((failed.published == host::k_module_pod_capacity) &&
                    !client->channel->to_host.writable_count() && !client->channel->to_module.writable_count()));
                //  Deliberately violate the accepted-reply storage contract. A
                //  missing reservation is loss, unlike the ordinary busy above.
                TEST_EXPECT(ctx, !client->reply(filler));
                TEST_EXPECT(ctx, (client->endpoint->failure.load() == threading::EEndpointFailure::accounting_failure));
            }
            else
            {
                failed.fail_message = true;
                TEST_EXPECT(ctx, (client->channel->to_module.post(filler) == host::EModulePipePost::published));
            }
            Access::pump(runtime);
            TEST_EXPECT(ctx, (Access::emergency(runtime) && client->stop_requested && other->stop_requested));
            for (unsigned turn = 0u; turn < 8u; ++turn) Access::pump(runtime);
            TEST_EXPECT(ctx, (failed.stopping && unaffected.stopping && !failed.destroyed && !unaffected.destroyed));
            failed.allow_completion = unaffected.allow_completion = true;
            for (unsigned turn = 0u; turn < 48u; ++turn) Access::pump(runtime);
            TEST_EXPECT(ctx, ((failed.destroyed == 1u) && (unaffected.destroyed == 1u) && client->preamble->failed()));
            TEST_EXPECT(ctx, ((failed.created_on == failed.destroyed_on) && (unaffected.created_on == unaffected.destroyed_on)));
            TEST_EXPECT(ctx, Access::retire(runtime, target));
            TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
            Access::clear(runtime);
        }
    }
    //  Ordinary replacement closes only the outgoing Executive and preserves
    //  other modules. A new instance can then run without fatal state leaking.
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    SFailureExecution unaffected, old, replacement;
    SFailureExecution::next = &unaffected;
    host::SClientRecord* const other = Access::driver(runtime, module_ids::audio, binding, execution);
    SFailureExecution::next = &old;
    host::SClientRecord* const outgoing = Access::driver(runtime, module_ids::executive, binding, execution);
    TEST_EXPECT(ctx, (other && outgoing));
    Access::pump(runtime);
    const modules::SModuleInstance previous = outgoing->instance;
    Access::replace(runtime, module_ids::executive);
    for (unsigned turn = 0u; turn < 8u; ++turn) Access::pump(runtime);
    TEST_EXPECT(ctx, (old.stopping && !old.destroyed && !other->stop_requested && !Access::emergency(runtime)));
    old.allow_completion = true;
    for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
    TEST_EXPECT(ctx, Access::retire(runtime, module_ids::executive));
    SFailureExecution::next = &replacement;
    host::SClientRecord* const incoming = Access::driver(runtime, module_ids::executive, binding, execution);
    TEST_EXPECT(ctx, (incoming && (incoming->instance.value != previous.value)));
    Access::pump(runtime);
    TEST_EXPECT(ctx, (incoming->preamble->ready() && !other->stop_requested && !Access::emergency(runtime)));
    replacement.allow_completion = unaffected.allow_completion = true;
    Access::replace(runtime, module_ids::executive);
    Access::replace(runtime, module_ids::audio);
    for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
    TEST_EXPECT(ctx, ((replacement.destroyed == 1u) && (old.destroyed == 1u) && (unaffected.destroyed == 1u)));
    TEST_EXPECT(ctx, Access::retire(runtime, module_ids::executive));
    TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
    Access::clear(runtime);
}

void replacement_result_retention(tests::TTestContext& ctx)
{
    using Access = host::SModulePreambleTestAccess;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    SFailureExecution successor;
    successor.allow_completion = successor.participate = true;
    SFailureExecution::next = &successor;
    host::SClientRecord* const client = Access::driver(runtime, module_ids::audio, binding, SFailureExecution::descriptor());
    tests::SCommonEndpointFixture requester{ module_ids::executive, 700u, 32u };
    host::CAssetService& assets = Access::assets(runtime);
    TEST_EXPECT(ctx, (client && assets.initialise() && Access::prepare_turns(runtime)));
    if (!client)
    {
        Access::clear(runtime);
        return;
    }
    Access::pump(runtime);
    Access::pump(runtime);
    TEST_EXPECT(ctx, (client->preamble->ready() && (client->direct_connections == host::EDirectConnectionState::available)));
    threading::CErasedOwnerMsg transfer = threading::make_owner_message<RawAssetTransfer>(91);
    TEST_EXPECT(ctx, transfer.owner().payload<RawAssetTransfer>()->storage.value.resize(16u));
    transfer.owner().add_hazard(mount_point_ids::audio);
    {
        host::CRetainedReplies::CDispatch dispatch{ *requester.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && requester.reserve_replies(1u)));
        assets.request(transfer, requester);
    }
    assets.retry_output(requester);
    AssetResult transferred;
    TEST_EXPECT(ctx, ((requester.pending() == 1u) && requester.received(0).copy_payload_to(transferred) &&
        (transferred.status == EAssetStatus::success) && transferred.asset.is_valid()));
    requester.discard(requester.pending());
    const auto filler = threading::make_pod_message(-1, services::OpenServiceResult{});
    for (unsigned index = 0u; index < 32u; ++index)
    {
        TEST_EXPECT(ctx, (requester.channel->to_module.post(filler) == host::EModulePipePost::published));
    }
    TEST_EXPECT(ctx, requester.reserve_replies(1u));
    // Inject the completed replacement boundary after its successor is active.
    // The actual Host turn must not interpret this retained request as new work.
    Access::completed_replacement(runtime, requester, binding, module_ids::audio);
    host::CModuleService& service = Access::module_service(runtime);
    for (unsigned turn = 0u; turn < 12u; ++turn)
    {
        Access::turn(runtime);
        service.retry_output(); // Controlled recipient is outside the module registry.
        host::SClientRecord* const current = Access::endpoint(runtime, system_ids::ops::make_system_id(module_ids::audio, thread_ids::host));
        TEST_EXPECT(ctx, ((current == client) && current && !current->stop_requested &&
            !current->endpoint->module_state->stopping.load() &&
            (current->direct_connections == host::EDirectConnectionState::available)));
        TEST_EXPECT(ctx, (service.output_ready(requester) && !service.is_in_flight() &&
            (requester.reserved_replies == 1u) && !successor.stopping && !successor.destroyed && !Access::emergency(runtime)));
    }
    TEST_EXPECT(ctx, (requester.pending() == 32u));
    requester.discard(32u);
    service.retry_output();
    service.retry_output();
    modules::ModuleResult completed;
    TEST_EXPECT(ctx, ((requester.pending() == 1u) && (requester.received(0).query_async_slot() == 93) &&
        requester.received(0).copy_payload_to(completed) && (completed.status == modules::EModuleStatus::success) &&
        (completed.module == module_ids::audio) && service.is_idle() && !requester.reserved_replies));
    requester.discard(requester.pending());
    // A second dependency-disposal pass would have invalidated this asset.
    {
        host::CRetainedReplies::CDispatch dispatch{ *requester.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && requester.reserve_replies(1u)));
        assets.request_disposal({ transferred.asset }, 94, requester);
    }
    assets.complete_disposals();
    assets.retry_output(requester);
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, ((requester.pending() == 1u) && requester.received(0).copy_payload_to(disposed) &&
        (disposed.status == EAssetStatus::success)));
    requester.discard(requester.pending());
    if (Access::endpoint(runtime, system_ids::ops::make_system_id(module_ids::audio, thread_ids::host)))
    {
        Access::replace(runtime, module_ids::audio);
        for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
        TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
    }
    TEST_EXPECT(ctx, ((successor.destroyed == 1u) && (successor.created_on == successor.destroyed_on)));
    assets.detach_client(requester);
    Access::clear(runtime);
}

struct SReportFixtureFacade
{
    modules::ESubmission read_status{ modules::ESubmission::busy }, release_status{ modules::ESubmission::busy };
    unsigned reads{}, releases{}, failures{}, read_attempts{}, release_attempts{};
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SReportFixtureFacade& self = *static_cast<SReportFixtureFacade*>(cookie);
        if (destination.kind != modules::SDestination::EKind::host)
        {
            return modules::ESubmission::invalid;
        }
        if (message.is_payload_a<platform::reports::PlatformReportCaptureFailed>())
        {
            ++self.failures;
            return modules::ESubmission::accepted;
        }
        if (message.is_payload_a<platform::reports::PlatformReportRequest>())
        {
            ++self.read_attempts;
            if (modules::accepted(self.read_status)) ++self.reads;
            return self.read_status;
        }
        AssetReleaseRequest release;
        if (message.copy_payload_to(release) && (release.acquisition.query_value() == 91u))
        {
            ++self.release_attempts;
            if (modules::accepted(self.release_status)) ++self.releases;
            return self.release_status;
        }
        return modules::ESubmission::invalid;
    }
};

//  Exercise the rebuilt acceptance DLL with controlled Host replies. Its native
//  window remains idle; this isolates fixture report ownership from native input.
void platform_fixture_report_stop(tests::TTestContext& ctx)
{
#if MV_PLATFORM_WINDOWS
    char previous[512]{};
    std::size_t previous_size{};
    TEST_EXPECT(ctx, (getenv_s(&previous_size, previous, sizeof(previous), "MORPHIC_PUMP_CASE") == 0));
    TEST_EXPECT(ctx, (_putenv_s("MORPHIC_PUMP_CASE", "platform-report-injected") == 0));
    TInstance<debug_system::CDebugServiceState> debug_service = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, debug_service.is_ready());
    if (!debug_service)
    {
        (void)_putenv_s("MORPHIC_PUMP_CASE", previous);
        return;
    }
    const std::string log = test_environment::test_log_path("PlatformFixtureStop");
    const std::string direct = test_environment::test_log_path("PlatformFixtureStop_direct");
    TEST_EXPECT(ctx, (debug_service->configure_log_paths(log.c_str(), direct.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "fixture_stop" })));
    SDllAllocation attribution;
    memory::CMemoryAllocator allocator{ &attribution, &SDllAllocation::allocate, &SDllAllocation::free, system_ids::ops::make_system_id(module_ids::platform, thread_ids::host) };
    memory::CMemoryContext module_memory{ allocator, system_ids::ops::make_system_id(module_ids::platform, thread_ids::host) };
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string filename = test_environment::binary_path(test_environment::module_filename("MorphicInputPlatform").c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
    const bool installed = path.is_ready() && binding.bind(path, module_ids::platform, host_identity) &&
        binding.install(test_environment::system_registry_view(), module_ids::platform, &module_memory,
            system_context::sequence_counter, debug_service.operator->());
    TEST_EXPECT(ctx, installed);
    if (installed)
    {
        modules::SExecutionV1 execution;
        TEST_EXPECT(ctx, (binding.query_execution(execution) == modules::EBindingResult::success));
        static const modules::SCommunicationV1 functions{ &SReportFixtureFacade::pod };
        for (const modules::ESubmission accepted : { modules::ESubmission::busy,
            modules::ESubmission::accepted, modules::ESubmission::published_wake_failed })
        {
            SReportFixtureFacade state;
            modules::CCommunication communication{ functions, &state };
            modules::CHostRestrictionPublication restrictions;
            modules::SHostExecutionContext host_execution;
            host_execution.restriction = restrictions.reader();
            modules::SExecutionContext context;
            context.instance = { 1u };
            context.communication = &communication;
            context.execution = &host_execution;
            void* runtime = nullptr;
            TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::host, &context));
            TEST_EXPECT(ctx, (execution.create(&context, &runtime) == modules::EExecutionStatus::success));
            if (!runtime) continue;
            modules::SExecutionProgress progress;
            const auto invoke = [&](const modules::EExecutionEvent event, const threading::CErasedPodMsg* const message = nullptr)
            {
                modules::SInvocation invocation;
                invocation.event = event;
                invocation.message = message;
                invocation.time_budget_ns = 2000000u;
                TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::host, &context));
                TEST_EXPECT(ctx, (execution.handle(runtime, &invocation, &progress) == modules::EExecutionStatus::success));
            };
            invoke(modules::EExecutionEvent::start);
            //  An invalid capture deadline yields a deterministic failure outcome.
            //  The supplied published receipt drives the fixture's independent read.
            const platform::reports::SCaptureIdentity identity{ context.instance, 1u };
            const auto capture = threading::make_pod_message(-1, platform::reports::PlatformReportCaptureRequest{ identity, {} });
            invoke(modules::EExecutionEvent::message, &capture);
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (state.failures == 1u));
            const auto receipt = threading::make_pod_message(-1,
                platform::reports::PlatformReportCaptureReceipt{ identity, platform::reports::EDisposition::published });
            invoke(modules::EExecutionEvent::message, &receipt);
            for (unsigned attempt = 0u; attempt < 3u; ++attempt) invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, ((state.read_attempts == 3u) && !state.reads && !progress.work_pending));
            state.read_status = accepted;
            invoke(modules::EExecutionEvent::service_async);
            invoke(modules::EExecutionEvent::stop);
            const unsigned stopped_attempts = state.read_attempts;
            for (unsigned attempt = 0u; attempt < 3u; ++attempt) invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (state.read_attempts == stopped_attempts));
            if (accepted == modules::ESubmission::busy)
            {
                TEST_EXPECT(ctx, (progress.work_complete && !state.reads && !state.releases));
            }
            else
            {
                TEST_EXPECT(ctx, ((state.reads == 1u) && !progress.work_complete && !progress.work_pending));
                const std::uint8_t byte = 1u;
                platform::reports::PlatformReportResult result;
                result.status = platform::reports::EStatus::success;
                result.acquisition = CAssetAcquisitionId{ 91u };
                result.bytes = { &byte, 1u };
                const auto acquired = threading::make_pod_message(710, result);
                invoke(modules::EExecutionEvent::message, &acquired);
                for (unsigned attempt = 0u; attempt < 3u; ++attempt) invoke(modules::EExecutionEvent::service_async);
                TEST_EXPECT(ctx, ((state.release_attempts == 3u) && !state.releases && !progress.work_complete && !progress.work_pending));
                state.release_status = accepted;
                for (unsigned attempt = 0u; attempt < 3u; ++attempt) invoke(modules::EExecutionEvent::service_async);
                TEST_EXPECT(ctx, ((state.releases == 1u) && !progress.work_complete && !progress.work_pending));
                const auto released = threading::make_pod_message(711, AssetReleaseResult{ result.acquisition, EAssetStatus::success });
                invoke(modules::EExecutionEvent::message, &released);
                invoke(modules::EExecutionEvent::service_async);
                TEST_EXPECT(ctx, (progress.work_complete && (state.reads == 1u) && (state.releases == 1u)));
            }
            TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::host, &context));
            TEST_EXPECT(ctx, (execution.destroy(runtime) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, module_memory.is_attribution_empty());
        }
        TEST_EXPECT(ctx, binding.unbind());
    }
    TEST_EXPECT(ctx, (attribution.frees == attribution.allocations));
    TEST_EXPECT(ctx, debug_service->stop());
    TEST_EXPECT(ctx, (_putenv_s("MORPHIC_PUMP_CASE", previous) == 0));
#else
    (void)ctx;
#endif
}

void rejected_execution_roles(tests::TTestContext& ctx)
{
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    for (std::uint32_t mode = 0u; mode < 2u; ++mode)
    {
        threading::CEndpointResources resources{ module_ids::executable, module_ids::audio, thread_ids::audio };
        host::CModuleChannel channel{ resources };
        modules::SExecutionV1 execution;
        if (mode == 1u)
        {
            execution.create = &CPeerExecution::create;
            execution.handle = &CPeerExecution::dispatch;
            execution.destroy = &CPeerExecution::destroy;
            execution.flags = modules::k_execution_native_events;
        }
        host::CModulePreamble preamble{ resources, channel, binding, execution, *memory::get_ambient_memory_context() };
        TEST_EXPECT(ctx, !preamble.activate());
        TEST_EXPECT(ctx, (preamble.failed() && preamble.destroyed() && !preamble.retained() && !preamble.work_pending()));
    }
}

void setup_allocation_rollback(tests::TTestContext& ctx)
{
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    std::size_t rejected = 0u;
    bool reached_success = false;
    for (std::size_t failure = 0u; failure < 24u; ++failure)
    {
        tests::TAllocationFailureFixture allocation;
        memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator, system_ids::host };
        {
            tests::TMemoryContextScope scope{ &memory };
            host::SClientRecord client{ module_ids::audio, 98u, nullptr, { 97u } };
            allocation.fail_on = failure;
            const bool ready = client.initialise_execution(binding, {}, nullptr, 32u, 3u);
            if (ready)
            {
                reached_success = true;
                TEST_EXPECT(ctx, (client.channel && client.pending_replies && client.preamble && !allocation.failed));
            }
            else
            {
                ++rejected;
                TEST_EXPECT(ctx, (allocation.failed && !client.channel && !client.pending_replies && !client.preamble));
                TEST_EXPECT(ctx, (memory.get_live_allocation_count() == 0u));
                client.phase = host::EClientPhase::stopped;
                client.incoming_drained = client.terminal_before_drain = true;
                TEST_EXPECT(ctx, (client.execution_quiescent() && client.replies_idle()));
                client.flush();
                TEST_EXPECT(ctx, (client.endpoint->notification_pins == 0u));
            }
        }
        TEST_EXPECT(ctx, memory.is_attribution_empty());
        if (reached_success) break;
    }
    TEST_EXPECT(ctx, (reached_success && (rejected >= 7u))); //  Channel, all four rings, replies and preamble.

    rejected = 0u;
    bool reached_native_create = false;
    for (std::size_t failure = 0u; failure < 32u; ++failure)
    {
        tests::TAllocationFailureFixture allocation;
        memory::CMemoryAllocator fault_allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ fault_allocator, system_ids::host };
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        host::CHost runtime{ allocator };
        host::SClientRecord* const requester = host::SModulePreambleTestAccess::install(runtime, module_ids::audio, binding);
        TEST_EXPECT(ctx, requester);
        if (!requester)
        {
            host::SModulePreambleTestAccess::clear(runtime);
            return;
        }
        CPeerExecution state;
        modules::SExecutionV1 execution;
        execution.create = &CPeerExecution::create;
        execution.handle = &CPeerExecution::dispatch;
        execution.destroy = &CPeerExecution::destroy;
        const modules::SChildExecution request{ &execution, &state, thread_ids::audio };
        {
            tests::TMemoryContextScope scope{ &memory };
            allocation.fail_on = failure;
            host::SModulePreambleTestAccess::failed_child(runtime, *requester, request);
        }
        for (std::uint32_t turn = 0u; turn < 4u; ++turn) host::SModulePreambleTestAccess::progress_threads(runtime);
        host::SLifecycleMessage result;
        TEST_EXPECT(ctx, requester->channel->read_preamble_control(result));
        TEST_EXPECT(ctx, ((result.operation == host::ELifecycle::child_created) && (result.correlation == 81) &&
            (result.notification.status != services::EServiceStatus::success) && !result.notification.first.value));
        TEST_EXPECT(ctx, !requester->channel->read_preamble_control(result));
        TEST_EXPECT(ctx, host::SModulePreambleTestAccess::children_idle(runtime, module_ids::audio));
        TEST_EXPECT(ctx, (!state.created_on.is_valid() && (state.destroyed == 0u)));
        if (allocation.failed) ++rejected;
        else reached_native_create = true; //  The seam refused native launch after complete setup.
        host::SModulePreambleTestAccess::clear(runtime);
        TEST_EXPECT(ctx, memory.is_attribution_empty());
        if (reached_native_create) break;
    }
    TEST_EXPECT(ctx, (reached_native_create && (rejected >= 9u)));
}

void host_output_admission_turn(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    host::SClientRecord* const provider = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding);
    TEST_EXPECT(ctx, (client && provider));
    if (!client || !provider)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    host::CServiceRouter& router = host::SModulePreambleTestAccess::router(runtime);
    const services::SServiceContract contract{ 1u, 1u };
    const host::SServiceProvider advertisement{ provider, &contract, 1u };
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(1u)));
        router.open({ module_ids::rendering, 1u, 1u, { 1u } }, 0, *client, &advertisement);
    }
    threading::CErasedPodMsg message;
    services::OpenServiceResult opened;
    TEST_EXPECT(ctx, (client->channel->read_preamble_pod(message) && message.copy_payload_to(opened)));
    TEST_EXPECT(ctx, (opened.status == services::EServiceStatus::success));
    for (std::uint64_t index = 1u; index <= 12u; ++index)
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 2u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(2u)));
        services::ServiceRequest request;
        request.route = opened.route;
        request.request = { index };
        request.opcode = 1u;
        router.request(request, static_cast<std::int32_t>(index), *client);
    }
    TEST_EXPECT(ctx, ((client->reserved_replies == 12u) && (client->pending_replies->reservations() == 0u)));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    (void)host::SModulePreambleTestAccess::drain(runtime);
    for (std::uint64_t index = 1u; index <= 12u; ++index)
    {
        services::ServiceCommand command;
        TEST_EXPECT(ctx, (provider->channel->read_preamble_pod(message) && message.copy_payload_to(command)));
        TEST_EXPECT(ctx, (command.descriptor && (command.descriptor->command.request.value == index)));
        services::ServiceReply reply;
        reply.route = opened.route;
        reply.request = { index };
        reply.opcode = 1u;
        TEST_EXPECT(ctx, (router.complete(*provider, reply) == services::EServiceStatus::success));
    }
    for (std::int32_t index = 12; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(index, services::OpenServiceResult{})));
    }
    TEST_EXPECT(ctx, (client->channel->to_host.post(threading::make_pod_message(99,
        services::OpenServiceRequest{ module_ids::audio, 1u, 1u, { 1u } })) == host::EModulePipePost::published));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, (client->pod_head_live && (client->accounted_pods == 0u)));
    for (std::uint32_t turn = 0u; turn < 8u; ++turn)
    {
        TEST_EXPECT(ctx, client->channel->read_preamble_pod(message));
        (void)host::SModulePreambleTestAccess::drain(runtime);
        TEST_EXPECT(ctx, (client->accounted_pods == ((turn == 7u) ? 1u : 0u)));
    }
    TEST_EXPECT(ctx, (router.output_ready(*client) && (client->reserved_replies == 4u)));
    TEST_EXPECT(ctx, ((client->pending_replies->size() == 1u) && (client->pending_replies->reservations() == 0u)));
    while (client->channel->read_preamble_pod(message)) {}
    (void)host::SModulePreambleTestAccess::drain(runtime);
    std::uint32_t results = 0u;
    while (client->channel->read_preamble_pod(message))
    {
        services::ServiceReply result;
        if (message.copy_payload_to(result))
        {
            ++results;
            TEST_EXPECT(ctx, (result.request.value == (8u + results)));
        }
        else TEST_EXPECT(ctx, (message.query_async_slot() == 99));
    }
    TEST_EXPECT(ctx, ((results == 4u) && (client->reserved_replies == 0u) && client->pending_replies->idle()));
    TEST_EXPECT(ctx, !router.failed());
    host::SModulePreambleTestAccess::clear(runtime);
}

void operation_owned_outputs(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    host::CAssetService& assets = host::SModulePreambleTestAccess::assets(runtime);
    TEST_EXPECT(ctx, (client && assets.initialise()));
    if (!client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(-1, services::OpenServiceResult{})));
    }
    const auto request = [&](const std::int32_t correlation)
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(1u)));
        threading::CErasedOwnerMsg message = threading::make_owner_message<AssetLoadRequest>(correlation);
        TEST_EXPECT(ctx, message.has_owner());
        assets.request(message, *client); //  Empty filename fixes one terminal result in its operation.
    };
    request(1);
    request(2);
    request(3);
    TEST_EXPECT(ctx, (assets.output_ready(*client) && !assets.work_idle()));
    TEST_EXPECT(ctx, (client->pending_replies->idle() && (client->reserved_replies == 3u)));
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime));
    threading::CErasedPodMsg message;
    for (std::uint32_t turn = 0u; turn < 4u; ++turn)
    {
        TEST_EXPECT(ctx, client->channel->read_preamble_pod(message));
        (void)host::SModulePreambleTestAccess::drain(runtime);
        if (turn == 0u) request(4); //  Reuses a low slot; surviving older operations still rotate.
    }
    std::int32_t result_count = 0;
    while (client->channel->read_preamble_pod(message))
    {
        AssetResult result;
        if (!message.copy_payload_to(result)) continue;
        ++result_count;
        TEST_EXPECT(ctx, (message.query_async_slot() == result_count));
        TEST_EXPECT(ctx, (result.status == EAssetStatus::invalid_request));
    }
    TEST_EXPECT(ctx, ((result_count == 4) && assets.work_idle() && !assets.output_ready(*client)));
    TEST_EXPECT(ctx, (client->pending_replies->idle() && (client->reserved_replies == 0u) && !assets.failed()));
    TEST_EXPECT(ctx, client->reserve_replies(1u));
    TEST_EXPECT(ctx, !client->reply(threading::make_pod_message(99, services::OpenServiceResult{})));
    TEST_EXPECT(ctx, ((client->reserved_replies == 1u) &&
        (client->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::accounting_failure)));
    host::SModulePreambleTestAccess::clear(runtime);
}

void report_owned_outputs(tests::TTestContext& ctx)
{
    using namespace platform::reports;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::platform, binding);
    host::CAssetService& assets = host::SModulePreambleTestAccess::assets(runtime);
    TEST_EXPECT(ctx, (client && assets.initialise()));
    if (!client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(-1, services::OpenServiceResult{})));
    }
    assets.install_report_provider(*client);
    assets.progress_reports(false);
    TEST_EXPECT(ctx, (assets.report_output_ready(*client) && (client->reserved_replies == 3u)));
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime));
    TEST_EXPECT(ctx, (client->pending_replies->idle() && !assets.failed()));
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, client->channel->read_preamble_pod(message));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    PlatformReportCaptureRequest capture;
    std::uint32_t requests = 0u;
    while (client->channel->read_preamble_pod(message))
    {
        if (message.copy_payload_to(capture)) ++requests;
    }
    TEST_EXPECT(ctx, ((requests == 1u) && capture.capture.capture_id && (client->reserved_replies == 2u)));
    assets.report_failed(*client, { capture.capture, { EStage::capture, EReason::native_failure } });
    assets.progress_reports(false);
    TEST_EXPECT(ctx, assets.report_output_ready(*client));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    PlatformReportCaptureReceipt receipt;
    TEST_EXPECT(ctx, (client->channel->read_preamble_pod(message) && message.copy_payload_to(receipt)));
    TEST_EXPECT(ctx, ((receipt.capture.capture_id == capture.capture.capture_id) && (receipt.disposition == EDisposition::rejected)));
    assets.progress_reports(false);
    TEST_EXPECT(ctx, ((client->reserved_replies == 0u) && !assets.report_output_ready(*client) && !assets.failed()));

    //  A later capture cancelled before physical publication never reaches the
    //  module and needs neither a module cancellation nor a transfer receipt.
    assets.install_report_provider(*client);
    assets.progress_reports(false);
    TEST_EXPECT(ctx, (client->reserved_replies == 3u));
    assets.close_report_provider(*client);
    assets.progress_reports(true);
    TEST_EXPECT(ctx, ((client->reserved_replies == 0u) && !client->channel->to_module.readable_count() && !assets.failed()));
    host::SModulePreambleTestAccess::clear(runtime);
}

void serviced_driver(tests::TTestContext& ctx)
{
    platform::threading::COwnerWait wake;
    TEST_EXPECT(ctx, (wake.initialise().code == platform::threading::EWaitError::none));
    threading::ThreadConfig config;
    config.thread_id = thread_ids::executive;
    config.worker_module_id = module_ids::executive;
    threading::CThreadPackage package{ config, {} };
    TEST_EXPECT(ctx, package.install_owner_wake(wake.endpoint()));
    threading::CThreadResources& resources = package.resources();
    resources.identity = { 582u };
    resources.instance = { 992u };
    host::CModuleChannel channel{ resources };
    TEST_EXPECT(ctx, channel.initialise(*memory::get_ambient_memory_context(), 32u, 32u));
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &SExecutionFixture::create;
    execution.handle = &SExecutionFixture::handle;
    execution.destroy = &SExecutionFixture::destroy;
    SExecutionFixture state;
    host::CModulePreamble preamble{ resources, channel, binding, execution, *memory::get_ambient_memory_context(), &state };
    TEST_EXPECT(ctx, package.install_preamble(preamble));
    const bool started = package.startup();
    TEST_EXPECT(ctx, started);
    if (!started) return;
    threading::CWaitDeadline timeout;
    TEST_EXPECT(ctx, (timeout.set_after_milliseconds(5000u).code == platform::threading::EWaitError::none));
    std::uint32_t received = 0u;
    bool stopped = false, closed = false, sealed = false, valid = true;
    while (!package.execution_ended())
    {
        host::SLifecycleMessage control;
        while (channel.read_host_control(control))
        {
            if (control.operation == host::ELifecycle::close_provider_admission)
            {
                closed = true;
                valid = valid && (control.frontier.pods == 33u);
                control = {};
                control.operation = host::ELifecycle::provider_admission_closed;
                control.frontier.host_output = 2u;
                valid = valid && channel.host_control(control);
                for (std::int32_t index = 0; index < 2; ++index)
                {
                    valid = valid && (channel.to_module.post(threading::make_pod_message(index, services::OpenServiceResult{})) == host::EModulePipePost::published);
                }
                resources.signal_consumer();
            }
            else if (control.operation == host::ELifecycle::seal)
            {
                sealed = true;
                valid = valid && (control.frontier.pods == 33u);
                control = {};
                control.operation = host::ELifecycle::sealed;
                valid = valid && channel.host_control(control);
            }
            else valid = valid && (control.operation == host::ELifecycle::ready);
        }
        threading::CErasedPodMsg message;
        while (channel.read_host_pod(message)) ++received;
        if (!stopped && (received == 33u) && state.self_complete.load(std::memory_order_acquire))
        {
            stopped = true;
            package.request_exit();
        }
        std::uint64_t remaining = 0u;
        if ((timeout.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            valid = false;
            package.request_exit();
            break;
        }
        threading::CWaitDeadline next;
        (void)next.set_after_milliseconds(1u);
        (void)wake.wait(next);
    }
    TEST_EXPECT(ctx, (valid && stopped && closed && sealed && package.execution_ended()));
    //  Join publishes the fixture's non-atomic callback state to this owner.
    TEST_EXPECT(ctx, package.shutdown());
    TEST_EXPECT(ctx, ((state.self_count == 100u) && (state.host_count == 2u) && (state.admission_count == 1u)));
    TEST_EXPECT(ctx, (state.foreign_rejected && state.valid_sources && (state.destroy_count == 1u)));
    TEST_EXPECT(ctx, ((state.created_on == state.destroyed_on) &&
        (state.created_on != platform::threading::query_current_thread_id())));
    TEST_EXPECT(ctx, (preamble.destroyed() && !preamble.failed()));
}

}   //  namespace

void run_module_preamble_tests(tests::TTestContext& ctx)
{
    lifecycle_service_obligations(ctx);
    lifecycle_rendering_acquisition(ctx);
    lifecycle_rendering_conversation(ctx);
    lifecycle_executive_requests(ctx);
    lifecycle_save_requests(ctx);
    lifecycle_bundle_requests(ctx);
    lifecycle_audio_requests(ctx);
    execution_boundary(ctx);
    provider_facade_requests(ctx);
    provider_pending_lifetimes(ctx);
    pressure_diagnostics(ctx);
    executive_timing_policy(ctx);
    pressure_and_fairness(ctx);
    bounded_storage(ctx);
    retained_reply_storage(ctx);
    finite_output_turns(ctx);
    common_dispatch_and_frontiers(ctx);
    host_retained_ingress(ctx);
    fallback_continuation_and_terminal(ctx);
    host_private_frontiers(ctx);
    private_connection_activation(ctx);
    preamble_connection_cancellation(ctx);
    provider_borrow_destruction_gate(ctx);
    standard_coordinator_child(ctx);
    standard_coordinator_controls(ctx);
    global_failure_shutdown(ctx);
    pumped_destroy_refusal(ctx);
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
    input_source_custody(ctx);
    input_borrow_custody(ctx);
    input_freeze_cleanup(ctx);
    input_installation_cleanup(ctx);
    input_secondary_cleanup(ctx);
    input_unexpected_close(ctx);
    replacement_result_retention(ctx);
    module_dll_execution(ctx, module_ids::audio, "MorphicAudio", thread_ids::audio);
    module_dll_execution(ctx, module_ids::rendering, "MorphicRendering", thread_ids::rendering);
    module_dll_execution(ctx, module_ids::platform, "MorphicPlatform", thread_ids::host);
    executive_dll_retries(ctx);
    rejected_execution_roles(ctx);
    setup_allocation_rollback(ctx);
    host_output_admission_turn(ctx);
    operation_owned_outputs(ctx);
    report_owned_outputs(ctx);
    serviced_driver(ctx);
}

int run_module_preamble_focused()
{
    tests::TTestContext ctx;
    run_module_preamble_tests(ctx);
    return ctx.report("ModulePreamble");
}

void run_common_bulk_connection_tests(tests::TTestContext& ctx)
{
    common_bulk_buffers(ctx, false, false);
    common_bulk_buffers(ctx, true, false);
    common_bulk_buffers(ctx, false, true);
    common_bulk_buffers(ctx, true, true);
    common_bulk_configuration(ctx, false);
    common_bulk_configuration(ctx, true);
    common_bulk_validation(ctx);
    common_bulk_allocation(ctx);
    common_bulk_retained_closure(ctx, false);
    common_bulk_retained_closure(ctx, true);
}

int run_common_bulk_focused()
{
    tests::TTestContext ctx;
    run_common_bulk_connection_tests(ctx);
    return ctx.report("CommonBulk");
}

void run_common_establishment_tests(tests::TTestContext& ctx)
{
    for (const bool bulk : { false, true })
    {
        common_partial_installation(ctx, bulk);
        common_activation_and_timeout(ctx, false, bulk);
        common_activation_and_timeout(ctx, true, bulk);
    }
    common_request_conflicts(ctx);
    common_delayed_request(ctx, false, false);
    common_delayed_request(ctx, true, false);
    common_delayed_request(ctx, true, true);
    common_activation_pressure(ctx);
}

int run_common_establishment_focused()
{
    tests::TTestContext ctx;
    run_common_establishment_tests(ctx);
    return ctx.report("CommonEstablishment");
}

void run_common_pod_connection_tests(tests::TTestContext& ctx)
{
    common_pod_exchange(ctx);
    common_pod_pressure(ctx, false);
    common_pod_pressure(ctx, true);
    common_pod_wake_failure(ctx, false);
    common_pod_wake_failure(ctx, true);
    common_retirement_admission(ctx);
    preamble_connection_cancellation(ctx); // Repeated data/capacity clear-and-wake regression.
}

int run_common_pod_focused()
{
    tests::TTestContext ctx;
    run_common_pod_connection_tests(ctx);
    return ctx.report("CommonPod");
}

void run_common_native_join_tests(tests::TTestContext& ctx)
{
    common_native_join_retention(ctx, false);
    common_native_join_retention(ctx, true);
}

int run_common_native_join_focused()
{
    tests::TTestContext ctx;
    run_common_native_join_tests(ctx);
    return ctx.report("CommonNativeJoin");
}

void run_common_multi_connection_tests(tests::TTestContext& ctx)
{
    common_endpoint_pairings(ctx);
    common_explicit_consent(ctx, false);
    common_explicit_consent(ctx, true);
    common_requester_limit(ctx, false);
    common_requester_limit(ctx, true);
    common_connection_limits(ctx, false);
    common_connection_limits(ctx, true);
    common_result_after_retirement(ctx);
    common_reused_correlation(ctx);
    common_authenticated_selection(ctx, false);
    common_authenticated_selection(ctx, true);
    common_close_during_signal(ctx, false);
    common_close_during_signal(ctx, true);
    common_deferred_quiescence(ctx);
    common_address_replacement(ctx);
}

int run_common_multi_focused()
{
    tests::TTestContext ctx;
    run_common_multi_connection_tests(ctx);
    return ctx.report("CommonMulti");
}

void run_provider_facade_tests(tests::TTestContext& ctx)
{
    provider_facade_requests(ctx);
    provider_pending_lifetimes(ctx);
}

void run_standard_coordinator_tests(tests::TTestContext& ctx)
{
    standard_coordinator_controls(ctx);
    standard_coordinator_child(ctx);
}

int run_platform_execution_focused()
{
    tests::TTestContext ctx;
    module_dll_execution(ctx, module_ids::platform, "MorphicPlatform", thread_ids::host);
    platform_fixture_report_stop(ctx);
    return ctx.report("PlatformExecution");
}

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
struct SLifecycleExecutiveFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    modules::ESubmission pod_result{ modules::ESubmission::busy };
    unsigned attempts{ 0u }, accepted{ 0u };
    unsigned pods{ 0u }, connections{ 0u }, quiescences{ 0u };
    std::int32_t pod_slots[3]{};
    modules::EModuleAction action{};
    module_ids::id_type module{};
    std::int32_t correlation{ -1 };
    bool valid{ true };
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SLifecycleExecutiveFacade& self = *static_cast<SLifecycleExecutiveFacade*>(cookie);
        modules::RunTestsRequest request;
        self.valid &= (destination.kind == modules::SDestination::EKind::connection) &&
            (destination.token.value == 17u) && message.copy_payload_to(request) && (request.module == module_ids::audio);
        if (modules::accepted(self.pod_result))
        {
            if (self.pods < 3u) self.pod_slots[self.pods] = message.query_async_slot();
            else self.valid = false;
            ++self.pods;
        }
        return self.pod_result;
    }
    static modules::ESubmission MV_STD_ABI_CALL intent(void* const cookie, const modules::SExecutionIntent& request) noexcept
    {
        SLifecycleExecutiveFacade& self = *static_cast<SLifecycleExecutiveFacade*>(cookie);
        if (request.kind == modules::SExecutionIntent::EKind::connect)
        {
            ++self.connections;
            self.valid &= (request.correlation == 50) && (request.first == system_ids::executive) &&
                (request.second == modules::test_connections::address(module_ids::audio));
        }
        else if (request.kind == modules::SExecutionIntent::EKind::connection_quiescent)
        {
            ++self.quiescences;
            self.valid &= request.connection.value == 17u;
        }
        else self.valid &= request.kind == modules::SExecutionIntent::EKind::participate;
        return modules::ESubmission::accepted;
    }
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SLifecycleExecutiveFacade& self = *static_cast<SLifecycleExecutiveFacade*>(cookie);
        ++self.attempts;
        const modules::ModuleRequest* const request = message.owner().payload<modules::ModuleRequest>();
        self.valid &= (destination.kind == modules::SDestination::EKind::host) && request;
        if (!request) return modules::ESubmission::invalid;
        if (modules::accepted(self.result))
        {
            ++self.accepted;
            self.module = request->module;
            self.action = request->action;
            self.correlation = message.query_async_slot();
            message = {};
        }
        return self.result;
    }
};
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

int run_lifecycle_executive_focused()
{
    tests::TTestContext ctx;
    lifecycle_executive_requests(ctx);
    lifecycle_audio_requests(ctx);
    lifecycle_save_requests(ctx);
    lifecycle_bundle_requests(ctx);
    replacement_result_retention(ctx);
    return ctx.report("LifecycleExecutive");
}

int run_lifecycle_service_focused()
{
    tests::TTestContext ctx;
    lifecycle_service_obligations(ctx);
    lifecycle_rendering_acquisition(ctx);
    lifecycle_rendering_conversation(ctx);
    module_dll_execution(ctx, module_ids::conditioning_general, "MorphicLifecycleService", thread_ids::simulation);
    return ctx.report("LifecycleService");
}

int run_audio_execution_focused()
{
    tests::TTestContext ctx;
    module_dll_execution(ctx, module_ids::audio, "MorphicAudio", thread_ids::audio);
    return ctx.report("AudioExecution");
}

int run_rendering_execution_focused()
{
    tests::TTestContext ctx;
    module_dll_execution(ctx, module_ids::rendering, "MorphicRendering", thread_ids::rendering);
    return ctx.report("RenderingExecution");
}

int run_execution_failure_focused()
{
    tests::TTestContext ctx;
    global_failure_shutdown(ctx);
    return ctx.report("ExecutionFailure");
}

int run_destroy_refusal_focused()
{
    tests::TTestContext ctx;
    pumped_destroy_refusal(ctx);
    return ctx.report("DestroyRefusal");
}

namespace
{
//  The test's Host thread exchanges commands and observations with the real
//  Executive service thread. Only that service thread accesses the bound facade.
struct SControlledServicePeer
{
    CPeerExecution peer;
    system_ids::id_type target{ system_ids::ops::make_system_id(module_ids::audio, thread_ids::audio) };
    threading::CErasedPodMsg outgoing;
    threading::CErasedPodMsg observations[host::k_module_pod_capacity + 16u];
    std::atomic<bool> outbound_pending{ false }, connect_requested{ false }, close_requested{ false };
    std::atomic<bool> pause_input{ false }, pause_observed{ false }, active{ false }, participated{ false };
    std::atomic<connections::EStatus> result{ connections::EStatus::busy };
    std::atomic<unsigned> received{ 0u }, retired{ 0u };

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        auto& self = *static_cast<SControlledServicePeer*>(context->creation_context);
        self.peer.communication = *context->communication;
        self.peer.address = context->address;
        self.peer.peer = self.target;
        self.peer.created_on = platform::threading::query_current_thread_id();
        self.peer.observations = self.observations;
        self.peer.observation_capacity = host::k_module_pod_capacity + 16u;
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        auto& self = *static_cast<SControlledServicePeer*>(runtime);
        if (self.connect_requested.exchange(false, std::memory_order_acq_rel))
        {
            self.peer.requested = self.peer.closing = self.peer.quiescent = false;
            self.peer.request = true;
            self.peer.result = connections::EStatus::busy;
        }
        if (self.close_requested.exchange(false, std::memory_order_acq_rel)) self.peer.close = true;
        const bool outbound_pending = self.outbound_pending.load(std::memory_order_acquire);
        if (outbound_pending) self.peer.outgoing = &self.outgoing;
        self.peer.pause_input = self.pause_input.load(std::memory_order_acquire);
        self.peer.handle(*invocation, *progress);
        if (outbound_pending && !self.peer.outgoing)
        {
            self.outbound_pending.store(false, std::memory_order_release);
        }
        self.pause_observed.store(self.peer.pause_input, std::memory_order_release);
        self.active.store(self.peer.active, std::memory_order_release);
        self.participated.store(self.peer.participated, std::memory_order_release);
        self.result.store(self.peer.result, std::memory_order_release);
        self.received.store(self.peer.received, std::memory_order_release);
        self.retired.store(self.peer.retired, std::memory_order_release);
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        auto& self = *static_cast<SControlledServicePeer*>(runtime);
        return CPeerExecution::destroy(&self.peer);
    }
};
}

void run_audio_preamble_service(tests::TTestContext& ctx, const modules::SExecutionV1& execution, void* const context,
    std::atomic<unsigned>& opens, std::atomic<unsigned>& starts, std::atomic<unsigned>& closes,
    std::atomic<std::uint32_t>& complete, std::atomic<bool>& output_wait)
{
    using Access = host::SModulePreambleTestAccess;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    TEST_EXPECT(ctx, Access::prepare_threads(runtime));
    const modules::SChildExecution child{ &execution, context, thread_ids::audio };
    modules::SExecutionV1 coordinator_execution;
    coordinator_execution.initial_child = &child;
    host::SClientRecord* const audio = Access::install(runtime, module_ids::audio, binding, 3u, coordinator_execution);
    SControlledServicePeer peer;
    const modules::SExecutionV1 peer_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &SControlledServicePeer::create, &SControlledServicePeer::handle, &SControlledServicePeer::destroy };
    const modules::SChildExecution peer_child{ &peer_execution, &peer, thread_ids::executive };
    modules::SExecutionV1 peer_coordinator;
    peer_coordinator.initial_child = &peer_child;
    host::SClientRecord* const executive = Access::install(runtime, module_ids::executive, binding, 3u, peer_coordinator);
    TEST_EXPECT(ctx, (audio && executive && audio->preamble->activate() && executive->preamble->activate()));
    host::CDirectConnections& connections = Access::connections(runtime);
    const auto turn = [&]()
    {
        audio->preamble->process();
        executive->preamble->process();
        Access::progress_threads(runtime);
        connections.progress();
    };
    const auto await = [&](const auto& predicate)
    {
        threading::CWaitDeadline deadline;
        if (deadline.set_after_milliseconds(5000u).code != platform::threading::EWaitError::none) return false;
        while (!predicate())
        {
            turn();
            if (predicate()) return true;
            std::uint64_t remaining{};
            if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                return false;
            }
            threading::CWaitDeadline pause;
            (void)pause.set_after_milliseconds(1u);
            std::atomic<std::uint32_t> unchanged{ 0u };
            (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
        }
        return true;
    };
    TEST_EXPECT(ctx, await([&] { return audio->preamble->ready() && peer.participated.load(std::memory_order_acquire); }));
    const auto connect = [&]()
    {
        peer.result.store(connections::EStatus::busy, std::memory_order_release);
        peer.connect_requested.store(true, std::memory_order_release);
        Access::wake(runtime, system_ids::executive);
        TEST_EXPECT(ctx, await([&] { return peer.active.load(std::memory_order_acquire) &&
            (peer.result.load(std::memory_order_acquire) == connections::EStatus::success); }));
    };
    connect();
    const system_ids::id_type audio_address = system_ids::ops::make_system_id(module_ids::audio, thread_ids::audio);
    host::SClientRecord* const service = Access::endpoint(runtime, audio_address);
    TEST_EXPECT(ctx, (service && service->thread));
    TEST_EXPECT(ctx, await([&] { return service->thread->query_state() == threading::EThreadRunState::Waiting; }));
    TEST_EXPECT(ctx, !opens.load());
    const auto post = [&](const int slot, const module_ids::id_type module = module_ids::audio)
    {
        peer.outgoing = threading::make_pod_message(slot, modules::RunTestsRequest{ module });
        peer.outbound_pending.store(true, std::memory_order_release);
        Access::wake(runtime, system_ids::executive);
        TEST_EXPECT(ctx, await([&] { return !peer.outbound_pending.load(std::memory_order_acquire); }));
    };
    unsigned read = 0u;
    const auto receive = [&](const int slot, const modules::ETestRunStatus status,
        const unsigned passed, const unsigned failed, const unsigned skipped)
    {
        TEST_EXPECT(ctx, await([&] { return peer.received.load(std::memory_order_acquire) > read; }));
        modules::RunTestsResult result;
        TEST_EXPECT(ctx, (peer.observations[read].copy_payload_to(result) && (peer.observations[read].query_async_slot() == slot) &&
            (result.module == module_ids::audio) && (result.status == status) &&
            (result.passed == passed) && (result.failed == failed) && (result.skipped == skipped)));
        ++read;
    };
    post(7, module_ids::rendering); receive(7, modules::ETestRunStatus::invalid_request, 0u, 0u, 0u);
    post(threading::k_uncorrelated_slot); receive(threading::k_uncorrelated_slot, modules::ETestRunStatus::invalid_request, 0u, 0u, 0u);
    post(-2); receive(-2, modules::ETestRunStatus::invalid_request, 0u, 0u, 0u);
    peer.pause_input.store(true, std::memory_order_release);
    Access::wake(runtime, system_ids::executive);
    TEST_EXPECT(ctx, await([&] { return peer.pause_observed.load(std::memory_order_acquire); }));
    for (unsigned index = 0u; index <= host::k_module_pod_capacity; ++index)
    {
        post(static_cast<int>(1000u + index), module_ids::rendering);
    }
    TEST_EXPECT(ctx, await([&]
    {
        return output_wait.load(std::memory_order_acquire) &&
            (service->thread->query_state() == threading::EThreadRunState::Waiting);
    }));
    peer.pause_input.store(false, std::memory_order_release);
    Access::wake(runtime, system_ids::executive);
    for (unsigned index = 0u; index <= host::k_module_pod_capacity; ++index)
    {
        receive(static_cast<int>(1000u + index), modules::ETestRunStatus::invalid_request, 0u, 0u, 0u);
    }
    post(8);
    TEST_EXPECT(ctx, await([&] { return starts.load() == 1u; }));
    post(9); receive(9, modules::ETestRunStatus::busy, 0u, 0u, 0u);
    complete.store(1u);
    Access::wake(runtime, audio_address);
    receive(8, modules::ETestRunStatus::success, 1u, 0u, 0u);
    TEST_EXPECT(ctx, (closes.load() == 1u));
    post(10);
    TEST_EXPECT(ctx, await([&] { return starts.load() == 2u; }));
    peer.close_requested.store(true, std::memory_order_release);
    Access::wake(runtime, system_ids::executive);
    bool cleanup_before_retirement = true;
    TEST_EXPECT(ctx, await([&]
    {
        if (connections.empty() && (closes.load() != 2u)) cleanup_before_retirement = false;
        return connections.empty() && (peer.retired.load(std::memory_order_acquire) == 1u);
    }));
    TEST_EXPECT(ctx, (cleanup_before_retirement && (closes.load() == 2u)));
    connect();
    post(11);
    TEST_EXPECT(ctx, await([&] { return starts.load() == 3u; }));
    audio->preamble->request_stop(modules::EExecutionStop::requested);
    executive->preamble->request_stop(modules::EExecutionStop::requested);
    TEST_EXPECT(ctx, await([&]
    {
        return audio->preamble->destroyed() && executive->preamble->destroyed() &&
            Access::children_idle(runtime, module_ids::audio) &&
            Access::children_idle(runtime, module_ids::executive) && connections.empty();
    }));
    TEST_EXPECT(ctx, ((closes.load() == 3u) && peer.peer.valid && !audio->preamble->failed() && !connections.failed()));
    TEST_EXPECT(ctx, ((peer.peer.destroyed == 1u) && (peer.peer.created_on == peer.peer.destroyed_on) &&
        (peer.peer.created_on != platform::threading::query_current_thread_id())));
    Access::clear(runtime);
}

void run_rendering_preamble_protocol(tests::TTestContext& ctx)
{
    using Access = host::SModulePreambleTestAccess;
    const auto require = [&](const unsigned line, const bool satisfied)
    {
        TEST_EXPECT(ctx, satisfied);
        //  Never unwind live affine state if a test cannot establish retirement.
        if (!satisfied)
        {
            std::cerr << "Rendering protocol requirement at line " << line << '\n';
            std::_Exit(1);
        }
    };
    TInstance<debug_system::CDebugServiceState> debug_service;
    require(__LINE__, debug_service.emplace());
    const std::string log = test_environment::test_log_path("rendering_protocol");
    const std::string direct_log = test_environment::test_log_path("rendering_protocol_direct");
    require(__LINE__, debug_service->configure_log_paths(log.c_str(), direct_log.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "render_protocol" }));
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string filename = test_environment::binary_path(test_environment::module_filename("MorphicRendering").c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
    require(__LINE__, path.is_ready() && binding.bind(path, module_ids::rendering, host_identity) &&
        binding.install(test_environment::system_registry_view(), module_ids::rendering,
            test_environment::rendering_memory_context(), system_context::sequence_counter, debug_service.operator->()));
    modules::SExecutionV1 execution;
    require(__LINE__, binding.query_execution(execution) == modules::EBindingResult::success);
    require(__LINE__, execution.initial_child && execution.initial_child->descriptor);
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    require(__LINE__, Access::prepare_threads(runtime));
    host::SClientRecord* const rendering = Access::install(runtime, module_ids::rendering, binding, 3u, execution);
    modules::CBoundModule peer_binding;
    modules::SBoundModuleTestAccess::install(peer_binding);
    SControlledServicePeer peer;
    peer.target = modules::test_connections::address(module_ids::rendering);
    const modules::SExecutionV1 peer_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &SControlledServicePeer::create, &SControlledServicePeer::handle, &SControlledServicePeer::destroy };
    const modules::SChildExecution peer_child{ &peer_execution, &peer, thread_ids::executive };
    modules::SExecutionV1 peer_coordinator;
    peer_coordinator.initial_child = &peer_child;
    host::SClientRecord* const executive = Access::install(runtime, module_ids::executive, peer_binding, 3u, peer_coordinator);
    require(__LINE__, rendering && executive && rendering->preamble->activate() && executive->preamble->activate());
    host::CDirectConnections& connections = Access::connections(runtime);
    const auto turn = [&]()
    {
        rendering->preamble->process();
        executive->preamble->process();
        Access::progress_controls(runtime);
    };
    const auto await = [&](const auto& predicate)
    {
        threading::CWaitDeadline deadline;
        require(__LINE__, deadline.set_after_milliseconds(5000u).code == platform::threading::EWaitError::none);
        while (!predicate())
        {
            turn();
            if (predicate()) return true;
            std::uint64_t remaining{};
            if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                return false;
            }
            threading::CWaitDeadline pause;
            (void)pause.set_after_milliseconds(1u);
            std::atomic<std::uint32_t> unchanged{ 0u };
            (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
        }
        return true;
    };
    require(__LINE__, await([&] { return rendering->preamble->ready() && peer.participated.load(std::memory_order_acquire); }));
    peer.connect_requested.store(true, std::memory_order_release);
    Access::wake(runtime, system_ids::executive);
    require(__LINE__, await([&] { return peer.active.load(std::memory_order_acquire) &&
        (peer.result.load(std::memory_order_acquire) == connections::EStatus::success); }));
    host::SClientRecord* const service = Access::endpoint(runtime, peer.target);
    require(__LINE__, service && service->thread);
    const auto send = [&](const std::int32_t slot, const module_ids::id_type module)
    {
        peer.outgoing = threading::make_pod_message(slot, modules::RunTestsRequest{ module });
        peer.outbound_pending.store(true, std::memory_order_release);
        Access::wake(runtime, system_ids::executive);
        require(__LINE__, await([&] { return !peer.outbound_pending.load(std::memory_order_acquire); }));
    };
    unsigned received = 0u;
    const auto receive = [&](const std::int32_t slot, const modules::ETestRunStatus status,
        const std::uint32_t skipped = 0u)
    {
        require(__LINE__, await([&] { return peer.received.load(std::memory_order_acquire) > received; }));
        const threading::CErasedPodMsg& message = peer.observations[received++];
        modules::RunTestsResult result;
        TEST_EXPECT(ctx, (message.copy_payload_to(result) && (message.query_async_slot() == slot) &&
            (result.module == module_ids::rendering) && (result.status == status) &&
            !result.passed && !result.failed && (result.skipped == skipped)));
    };
    const auto capture = [&](threading::CErasedOwnerMsg& message)
    {
        require(__LINE__, await([&] { return service->capture(message); }));
        require(__LINE__, service->reserve_replies(1u));
        service->accounted_owner();
        TEST_EXPECT(ctx, message.is_message_a<RawAssetTransfer>());
    };
    send(80, module_ids::executive);
    receive(80, modules::ETestRunStatus::invalid_request);
    constexpr std::int32_t run_slot = INT32_MAX;
    constexpr std::int32_t busy_slot = INT32_MAX - 1;
    send(run_slot, module_ids::rendering);
    threading::CErasedOwnerMsg input;
    capture(input);
    send(busy_slot, module_ids::rendering);
    receive(busy_slot, modules::ETestRunStatus::busy);
    AssetResult failed;
    failed.status = EAssetStatus::allocation_failed;
    {
        host::CRetainedReplies::CDispatch dispatch{ *service->pending_replies, 1u };
        require(__LINE__, static_cast<bool>(dispatch));
        require(__LINE__, service->reply(threading::make_pod_message(input.query_async_slot(), failed)));
    }
    input = {};
    receive(run_slot, modules::ETestRunStatus::failed, 23u);

    //  Stop after an accepted owning transfer, before its Host result. The late
    //  retained asset must be disposed before connection quiescence and destroy.
    send(83, module_ids::rendering);
    capture(input);
    rendering->preamble->request_stop(modules::EExecutionStop::requested);
    require(__LINE__, await([&] { return service->thread->query_state() == threading::EThreadRunState::Exiting; }));
    host::CAssetService assets;
    require(__LINE__, assets.initialise());
    {
        host::CRetainedReplies::CDispatch dispatch{ *service->pending_replies, 1u };
        require(__LINE__, static_cast<bool>(dispatch));
        assets.request(input, *service);
    }
    TEST_EXPECT(ctx, (assets.output_ready(*service) && (service->reserved_replies == 1u) &&
        (service->pending_replies->reservations() == 0u)));
    assets.retry_output(*service);
    threading::CErasedPodMsg disposal_message;
    require(__LINE__, await([&] { return service->read(disposal_message); }));
    service->accounted_pod();
    require(__LINE__, service->reserve_replies(1u));
    AssetDisposeRequest disposal;
    require(__LINE__, disposal_message.copy_payload_to(disposal) && disposal.asset);
    threading::CErasedPodMsg premature;
    TEST_EXPECT(ctx, (!service->read(premature) && !connections.empty() &&
        (service->thread->query_state() == threading::EThreadRunState::Exiting) &&
        !rendering->preamble->destroyed()));
    {
        host::CRetainedReplies::CDispatch dispatch{ *service->pending_replies, 1u };
        require(__LINE__, static_cast<bool>(dispatch));
        assets.request_disposal(disposal, disposal_message.query_async_slot(), *service);
    }
    assets.complete_disposals();
    TEST_EXPECT(ctx, assets.output_ready(*service));
    assets.retry_output(*service);
    TEST_EXPECT(ctx, (assets.is_idle() && !assets.failed()));
    assets.deallocate();
    executive->preamble->request_stop(modules::EExecutionStop::requested);
    require(__LINE__, await([&]
    {
        return rendering->preamble->destroyed() && executive->preamble->destroyed() &&
            Access::children_idle(runtime, module_ids::rendering) &&
            Access::children_idle(runtime, module_ids::executive) && connections.empty();
    }));
    TEST_EXPECT(ctx, ((peer.received.load(std::memory_order_acquire) == received) && peer.peer.valid &&
        !rendering->preamble->failed() && !executive->preamble->failed() && !connections.failed()));
    TEST_EXPECT(ctx, ((peer.peer.destroyed == 1u) && (peer.peer.created_on == peer.peer.destroyed_on)));
    Access::clear(runtime);
    TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
    TEST_EXPECT(ctx, binding.unbind());
    TEST_EXPECT(ctx, debug_service->stop());
}

void run_executive_submission_failure(tests::TTestContext& ctx, modules::CBoundModule& binding)
{
    modules::SExecutionV1 coordinator;
    TEST_EXPECT(ctx, (binding.query_execution(coordinator) == modules::EBindingResult::success));
    if (!coordinator.initial_child || !coordinator.initial_child->descriptor)
    {
        TEST_EXPECT(ctx, false);
        return;
    }
    const modules::SExecutionV1 execution = *coordinator.initial_child->descriptor;
    static const modules::SCommunicationV1 functions{ &SExecutiveFacade::pod, &SExecutiveFacade::owner,
        &SExecutiveFacade::intent, &SExecutiveFacade::query };
    SExecutiveFacade state;
    state.result = modules::ESubmission::invalid;
    modules::CCommunication communication{ functions, &state };
    modules::CHostRestrictionPublication publication;
    modules::SHostRestrictionSnapshot snapshot;
    snapshot.generation = 1u;
    TEST_EXPECT(ctx, publication.publish(snapshot));
    threading::CNotification notification;
    std::atomic<std::uint32_t> pending{ 0u };
    TEST_EXPECT(ctx, notification.initialise(pending));
    modules::SHostExecutionContext view;
    view.restriction = publication.reader();
    view.notification = notification.reader();
    modules::SExecutionContext context;
    context.address = system_ids::executive;
    context.communication = &communication;
    context.execution = &view;
    void* runtime = nullptr;
    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
    TEST_EXPECT(ctx, (execution.create(&context, &runtime) == modules::EExecutionStatus::success));
    if (!runtime) return;
    modules::SInvocation invocation;
    invocation.event = modules::EExecutionEvent::start;
    modules::SExecutionProgress progress;
    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
    TEST_EXPECT(ctx, (execution.handle(runtime, &invocation, &progress) == modules::EExecutionStatus::failed));
    TEST_EXPECT(ctx, (state.valid && (state.owner_attempts == 1u) && !state.owners));
    invocation.event = modules::EExecutionEvent::stop;
    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
    TEST_EXPECT(ctx, (execution.handle(runtime, &invocation, &progress) == modules::EExecutionStatus::success));
    TEST_EXPECT(ctx, (progress.work_complete && !progress.work_pending));
    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
    TEST_EXPECT(ctx, (execution.destroy(runtime) == modules::EExecutionStatus::success));
}

namespace
{
struct SExecutiveStartupHarness
{
    using Access = host::SModulePreambleTestAccess;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule peer_binding;
    SControlledServicePeer rendering, audio;
    const modules::SExecutionV1 peer_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &SControlledServicePeer::create, &SControlledServicePeer::handle, &SControlledServicePeer::destroy };
    const modules::SChildExecution rendering_child{ &peer_execution, &rendering, thread_ids::rendering };
    const modules::SChildExecution audio_child{ &peer_execution, &audio, thread_ids::audio };
    host::SClientRecord* coordinators[3]{};
    unsigned rendering_received{ 0u }, audio_received{ 0u };

    bool start(modules::CBoundModule& binding) noexcept
    {
        if (!Access::prepare_threads(runtime)) return false;
        modules::SExecutionV1 execution;
        if (binding.query_execution(execution) != modules::EBindingResult::success)
        {
            return false;
        }
        modules::SBoundModuleTestAccess::install(peer_binding);
        rendering.target = audio.target = system_ids::executive;
        coordinators[0] = Access::install(runtime, module_ids::executive, binding, 3u, execution);
        execution = {};
        execution.initial_child = &rendering_child;
        coordinators[1] = Access::install(runtime, module_ids::rendering, peer_binding, 3u, execution);
        execution.initial_child = &audio_child;
        coordinators[2] = Access::install(runtime, module_ids::audio, peer_binding, 3u, execution);
        for (host::SClientRecord* const coordinator : coordinators)
        {
            if (!coordinator || !coordinator->preamble->activate())
            {
                return false;
            }
        }
        return true;
    }
    void tick() noexcept
    {
        for (host::SClientRecord* const coordinator : coordinators) coordinator->preamble->process();
        Access::progress_controls(runtime);
    }
    template<typename TPredicate> bool until(const TPredicate& predicate) noexcept
    {
        threading::CWaitDeadline deadline;
        if (deadline.set_after_milliseconds(5000u).code != platform::threading::EWaitError::none) return false;
        while (!predicate())
        {
            tick();
            if (predicate()) return true;
            std::uint64_t remaining{};
            if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                return false;
            }
            threading::CWaitDeadline pause;
            (void)pause.set_after_milliseconds(1u);
            std::atomic<std::uint32_t> unchanged{ 0u };
            (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
        }
        return true;
    }
    bool capture(threading::CErasedOwnerMsg& message) noexcept
    {
        host::SClientRecord* const client = Access::endpoint(runtime, system_ids::executive);
        if (!client || !client->capture(message)) return false;
        client->accounted_owner();
        return client->reserve_replies(message.is_message_a<modules::ModuleRequest>() ? 2u : 1u);
    }
    bool read_owned(threading::CErasedOwnerMsg& message) noexcept
    {
        return until([&] { return capture(message); });
    }
    bool publish_host(const threading::CErasedPodMsg& message) noexcept
    {
        host::SClientRecord* const client = Access::endpoint(runtime, system_ids::executive);
        if (!client) return false;
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        return dispatch && client->reply(message);
    }
    bool read_peer(SControlledServicePeer& peer, threading::CErasedPodMsg& message) noexcept
    {
        unsigned& consumed = (&peer == &rendering) ? rendering_received : audio_received;
        if (!until([&] { return peer.received.load(std::memory_order_acquire) > consumed; }))
        {
            return false;
        }
        message = peer.observations[consumed++];
        return true;
    }
    bool publish_peer(SControlledServicePeer& peer, const threading::CErasedPodMsg& message) noexcept
    {
        peer.outgoing = message;
        peer.outbound_pending.store(true, std::memory_order_release);
        Access::wake(runtime, modules::test_connections::address((&peer == &rendering) ? module_ids::rendering : module_ids::audio));
        return until([&] { return !peer.outbound_pending.load(std::memory_order_acquire); });
    }
    bool failed() noexcept { return Access::emergency(runtime); }
    bool stop() noexcept
    {
        for (host::SClientRecord* const coordinator : coordinators) coordinator->preamble->request_stop(modules::EExecutionStop::requested);
        const bool stopped = until([&]
        {
            threading::CErasedOwnerMsg cancelled;
            while (capture(cancelled)) cancelled = {};
            host::SClientRecord* const client = Access::endpoint(runtime, system_ids::executive);
            //  This test owns the synthetic service results. Cancel their
            //  remaining credits only after the recipient's actual quiescence.
            if (client && client->execution_quiescent() && client->reserved_replies)
            {
                client->cancel_reservation(client->reserved_replies);
            }
            for (host::SClientRecord* const coordinator : coordinators)
            {
                if (!coordinator->preamble->destroyed() || !Access::children_idle(runtime, coordinator->module))
                {
                    return false;
                }
            }
            return Access::connections(runtime).empty();
        });
        if (stopped) Access::clear(runtime);
        return stopped;
    }
};
}

void run_executive_preamble_protocol(tests::TTestContext& ctx, modules::CBoundModule& binding)
{
    const auto require = [&](const unsigned line, const bool satisfied)
    {
        TEST_EXPECT(ctx, satisfied);
        if (!satisfied)
        {
            std::cerr << "Executive protocol requirement at line " << line << '\n';
            std::_Exit(1);
        }
    };
    enum class ECase : std::uint8_t
    {
        loaded = 0, already_loaded, unavailable, wrong_implementation, load_failure,
        missing_acknowledgement, wrong_correlation, duplicate_acknowledgement,
        audio_unavailable, audio_failed, audio_cancelled, audio_busy, audio_bad_counts, audio_wrong_correlation, audio_load_failure
    };
    for (const ECase scenario : { ECase::loaded, ECase::already_loaded, ECase::unavailable,
        ECase::wrong_implementation, ECase::load_failure, ECase::missing_acknowledgement,
        ECase::wrong_correlation, ECase::duplicate_acknowledgement,
        ECase::audio_unavailable, ECase::audio_failed, ECase::audio_cancelled, ECase::audio_busy,
        ECase::audio_bad_counts, ECase::audio_wrong_correlation, ECase::audio_load_failure })
    {
        SExecutiveStartupHarness fixture;
        require(__LINE__, fixture.start(binding));
        require(__LINE__, fixture.until([&] { return fixture.rendering.participated.load(std::memory_order_acquire) &&
            fixture.audio.participated.load(std::memory_order_acquire); }));
        threading::CErasedOwnerMsg request;
        const bool received = fixture.read_owned(request);
        require(__LINE__, received);
        const modules::ModuleRequest* const load = request.owner().payload<modules::ModuleRequest>();
        TEST_EXPECT(ctx, ((load != nullptr) && (load->action == modules::EModuleAction::load) &&
            (load->module == module_ids::rendering) && (load->file.length() != 0u) &&
            (std::strcmp(load->file.cstring(), test_environment::module_filename("package:/bin/MorphicRendering").c_str()) == 0)));
        const std::int32_t slot = request.query_async_slot();
        request.take_owner().destroy();

        modules::ModuleResult result;
        result.module = module_ids::rendering;
        result.notice = modules::EModuleNotice::acknowledged;
        result.status = modules::EModuleStatus::success;
        threading::CErasedPodMsg reply;
        reply.set_async_slot(slot);
        if (scenario != ECase::missing_acknowledgement)
        {
            reply.assign_payload(result);
            TEST_EXPECT(ctx, fixture.publish_host(reply));
        }
        result.notice = (scenario == ECase::duplicate_acknowledgement) ?
            modules::EModuleNotice::acknowledged : modules::EModuleNotice::completed;
        result.status = (scenario == ECase::load_failure) ? modules::EModuleStatus::binding_failed :
            (((scenario == ECase::loaded) || (scenario == ECase::duplicate_acknowledgement)) ?
                modules::EModuleStatus::success : modules::EModuleStatus::already_loaded);
        result.available = (scenario != ECase::unavailable) && (scenario != ECase::load_failure);
        if (scenario == ECase::wrong_implementation)
        {
            //  A different implementation at the same mount needs no catalogue entry.
            result.module = module_ids::ops::make_id(mount_point_ids::render,
                module_ids::ops::make_index(module_ids::k_count));
        }
        if (scenario == ECase::wrong_correlation)
        {
            reply.set_async_slot(slot + 1);
        }
        reply.assign_payload(result);
        TEST_EXPECT(ctx, fixture.publish_host(reply));
        if ((scenario == ECase::loaded) || (scenario == ECase::already_loaded) || (scenario >= ECase::audio_unavailable))
        {
            threading::CErasedPodMsg tests_request;
            require(__LINE__, fixture.read_peer(fixture.rendering, tests_request));
            modules::RunTestsRequest tests;
            TEST_EXPECT(ctx, (tests_request.copy_payload_to(tests) && (tests.module == module_ids::rendering)));
            TEST_EXPECT(ctx, (tests_request.query_async_slot() == (slot + 1)));
            TEST_EXPECT(ctx, !fixture.capture(request));
            //  Executive only observes generic completion; it does not know
            //  the destination's test cases or require a particular case count.
            modules::RunTestsResult completed;
            completed.module = module_ids::rendering;
            completed.status = modules::ETestRunStatus::success;
            completed.passed = 7u;
            reply.set_async_slot(slot + 1);
            reply.assign_payload(completed);
            TEST_EXPECT(ctx, fixture.publish_peer(fixture.rendering, reply));
            require(__LINE__, fixture.read_owned(request));
            const modules::ModuleRequest* const audio_load = request.owner().payload<modules::ModuleRequest>();
            TEST_EXPECT(ctx, (audio_load && (audio_load->module == module_ids::audio) && (audio_load->action == modules::EModuleAction::load) &&
                (std::strcmp(audio_load->file.cstring(), test_environment::module_filename("package:/bin/MorphicAudio").c_str()) == 0)));
            TEST_EXPECT(ctx, (request.query_async_slot() == (slot + 2)));
            request.take_owner().destroy();
            result.module = module_ids::audio;
            result.notice = modules::EModuleNotice::acknowledged;
            result.status = modules::EModuleStatus::success;
            result.available = false;
            reply.set_async_slot(slot + 2);
            reply.assign_payload(result);
            TEST_EXPECT(ctx, fixture.publish_host(reply));
            result.notice = modules::EModuleNotice::completed;
            result.status = (scenario == ECase::audio_load_failure) ? modules::EModuleStatus::binding_failed : modules::EModuleStatus::success;
            result.available = scenario != ECase::audio_load_failure;
            reply.assign_payload(result);
            TEST_EXPECT(ctx, fixture.publish_host(reply));
            if (scenario != ECase::audio_load_failure)
            {
                require(__LINE__, fixture.read_peer(fixture.audio, tests_request));
                TEST_EXPECT(ctx, (tests_request.copy_payload_to(tests) && (tests.module == module_ids::audio) && (tests_request.query_async_slot() == (slot + 3))));
                TEST_EXPECT(ctx, !fixture.capture(request));
                completed = { module_ids::audio, modules::ETestRunStatus::success, 1u, 0u, 0u };
                if (scenario == ECase::audio_unavailable)
                {
                    completed = { module_ids::audio, modules::ETestRunStatus::unavailable, 0u, 0u, 1u };
                }
                if (scenario == ECase::audio_failed)
                {
                    completed = { module_ids::audio, modules::ETestRunStatus::failed, 0u, 1u, 0u };
                }
                if (scenario == ECase::audio_cancelled)
                {
                    completed = { module_ids::audio, modules::ETestRunStatus::cancelled, 0u, 0u, 1u };
                }
                if (scenario == ECase::audio_busy)
                {
                    completed = { module_ids::audio, modules::ETestRunStatus::busy, 0u, 1u, 0u };
                }
                if (scenario == ECase::audio_bad_counts)
                {
                    completed.passed = 2u;
                }
                reply.set_async_slot(slot + ((scenario == ECase::audio_wrong_correlation) ? 4 : 3));
                reply.assign_payload(completed);
                TEST_EXPECT(ctx, fixture.publish_peer(fixture.audio, reply));
            }
            if ((scenario == ECase::loaded) || (scenario == ECase::already_loaded) || (scenario == ECase::audio_unavailable))
            {
                require(__LINE__, fixture.read_owned(request));
                TEST_EXPECT(ctx, (request.is_message_a<RawAssetTransfer>() && (request.query_async_slot() == (slot + 4))));
            }
            else
            {
                TEST_EXPECT(ctx, fixture.until([&] { return fixture.failed(); }));
                TEST_EXPECT(ctx, !fixture.capture(request));
            }
        }
        else
        {
            TEST_EXPECT(ctx, fixture.until([&] { return fixture.failed(); }));
            TEST_EXPECT(ctx, !fixture.capture(request));
        }
        request.take_owner().destroy();
        const bool expected_failure = (scenario != ECase::loaded) && (scenario != ECase::already_loaded) &&
            (scenario != ECase::audio_unavailable);
        TEST_EXPECT(ctx, (fixture.failed() == expected_failure));
        require(__LINE__, fixture.stop());
        TEST_EXPECT(ctx, (fixture.rendering.peer.valid && fixture.audio.peer.valid &&
            (fixture.rendering.peer.destroyed == 1u) && (fixture.audio.peer.destroyed == 1u) &&
            (fixture.rendering.peer.created_on == fixture.rendering.peer.destroyed_on) &&
            (fixture.audio.peer.created_on == fixture.audio.peer.destroyed_on)));
        TEST_EXPECT(ctx, test_environment::executive_memory_context()->is_attribution_empty());
    }
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
        SDllAllocation attribution;
        attribution.track_primary = !credits;
        const system_ids::id_type identity = system_ids::ops::make_system_id(module_ids::rendering, thread_ids::host);
        memory::CMemoryAllocator allocator{ &attribution, &SDllAllocation::allocate, &SDllAllocation::free, identity };
        memory::CMemoryContext module_memory{ allocator, identity };
        // Owning transport requires a shared allocator, as in production. Keep
        // Host attribution separate from the module's transferred allocations.
        memory::CMemoryContext shared_host_memory{ allocator, system_ids::host };
        tests::TMemoryContextScope host_scope{ credits ? &shared_host_memory : memory::get_ambient_memory_context() };
        memory::CMemoryAllocator host_allocator{ nullptr, &tests::allocate_scalar_test_memory,
            &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
            { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
        const std::string file = test_environment::binary_path(test_environment::module_filename("MorphicPumpProvider").c_str());
        const platform::path::NativePath path = platform::path::makeNativePath(file.c_str());
        require(path.is_ready() && binding.bind(path, module_ids::rendering, host_identity) &&
            binding.install(test_environment::system_registry_view(), module_ids::rendering, &module_memory,
                system_context::sequence_counter, debug_service.operator->()), __LINE__);
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
        require(binding.unbind(), __LINE__);
    }
#if MV_PLATFORM_WINDOWS
    TEST_EXPECT(ctx, (_putenv_s("MORPHIC_PUMP_CASE", previous.c_str()) == 0));
#else
    TEST_EXPECT(ctx, (previous.empty() ? (unsetenv("MORPHIC_PUMP_CASE") == 0) : (setenv("MORPHIC_PUMP_CASE", previous.c_str(), 1) == 0)));
#endif
    TEST_EXPECT(ctx, debug_service->stop());
    return ctx.report(notification_only ? "ProviderNotification" :
        (retirement_only ? "ProviderRetirement" : (refusals_only ? "ProviderRefusals" : "ProviderExecution")));
}

namespace
{
struct SCreditFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    threading::CErasedPodMsg posts[4];
    unsigned count{}, owners{}, owner_attempts{}, pod_attempts{};
    bool valid{ true };
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
        ++self.pod_attempts;
        self.valid &= destination.kind == modules::SDestination::EKind::host;
        if (modules::accepted(self.result))
        {
            if (self.count == 4u) self.valid = false;
            else self.posts[self.count++] = message;
        }
        return self.result;
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
    static const modules::SCommunicationV1 functions{ &SCreditFacade::pod };
    for (const EMode mode : { EMode::normal, EMode::cancel, EMode::abandon, EMode::shutdown, EMode::provider_failure })
    {
        SCreditFacade facade;
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
    SCreditFacade facade;
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
    static const modules::SCommunicationV1 functions{ &SCreditFacade::pod };
    for (unsigned mode = 0u; mode < 9u; ++mode)
    {
        SCreditFacade facade;
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
    static const modules::SCommunicationV1 functions{ &SCreditFacade::pod };
    SCreditFacade facade;
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
    static inline const modules::SCommunicationV1 functions{ &SCreditFacade::pod };
    SCreditFacade facade;
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
    SCreditFacade output;
    modules::ESubmission owner_result{ modules::ESubmission::busy };
    unsigned owners{};
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        return SCreditFacade::pod(&static_cast<SModalFacade*>(cookie)->output, destination, message);
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
    SCreditFacade output;
    unsigned requests{}, attempts{};
    unsigned module_attempts{}, module_submissions{};
    modules::ESubmission module_result{ modules::ESubmission::busy };
    bool replacement{};
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        return SCreditFacade::pod(&static_cast<SSchemaFacade*>(cookie)->output, destination, message);
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
    SCreditFacade output;
    static inline const modules::SCommunicationV1 functions{ &SCreditFacade::pod };
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
struct SInputBorrowFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    threading::CErasedPodMsg posts[8];
    unsigned count{}, attempts{};
    bool valid{ true };
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SInputBorrowFacade& self = *static_cast<SInputBorrowFacade*>(cookie);
        ++self.attempts;
        self.valid &= (destination.kind == modules::SDestination::EKind::host) &&
            (message.query_async_slot() >= 16) && (message.query_async_slot() < 24);
        if (modules::accepted(self.result))
        {
            if (self.count == 8u) self.valid = false;
            else self.posts[self.count++] = message;
        }
        return self.result;
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
        const unsigned initial = facade.attempts;
        TEST_EXPECT(ctx, (!step() && (facade.attempts == initial)));
        if ((scenario == 1u) || (scenario == 2u))
        {
            borrows.close_local(0u);
            TEST_EXPECT(ctx, (!step() && (facade.attempts == initial)));
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
    TEST_EXPECT(ctx, (!runnable && !facade.count && (facade.attempts == 1u) && borrows.publication_pending()));
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
