
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostMaintenance_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    03 Nov 26
//
//  Actual Host lifetime retention, bounded probes and installed subscriptions.

#include "framework/threading/CThreadContext.hpp"
#include "framework/system/system_ids.hpp"
#include "host/runtime/host.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/endpoint_fixture.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/environment/test_paths.hpp"
#include "host/runtime/host_payloads.hpp"
#include "platform/filesystem/file.hpp"

namespace threading
{
struct SThreadPackageTestAccess
{
    static void fail_join(CThreadPackage& package, const bool fail = true) noexcept { package.m_test_fail_join = fail; }
    static void reject_create(CThreadPackage& package) noexcept { package.m_test_reject_create = true; }
    static void preparation(CThreadPackage& package, const std::uint32_t value) noexcept { package.m_preparation.store(value, std::memory_order_release); }
    static bool retained(const CThreadPackage& package) noexcept
    {
        return package.m_thread.is_valid() && package.m_resources.wait_predicate.has_control() &&
            package.m_host_pinned && (package.m_resources.notification_pins == 1u) && (package.m_resources.host_execution == package.m_host_execution);
    }
    static bool rolled_back(const CThreadPackage& package) noexcept
    {
        return !package.m_thread.is_valid() && !package.m_resources.wait_predicate.has_control() &&
            !package.m_host_pinned && !package.m_resources.notification_pins && !package.m_resources.host_execution;
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
    static EBindingResult MV_STD_ABI_CALL rejected_provision(void* const) noexcept { return EBindingResult::invalid_argument; }
    static void prepare(CBoundModule& binding, const bool reject = false) noexcept
    {
        binding.m_installed = true;
        binding.m_core.install_ambient_thread_id = &thread;
        binding.m_core.install_thread_provisioning = reject ? &rejected_provision : &provision;
        binding.m_core.install_thread_memory_context = &memory;
    }
};
}

void prepare_platform_test_binding(modules::CBoundModule& binding) noexcept
{
    modules::SBoundModuleTestAccess::prepare(binding);
}

namespace host
{

struct SHostMaintenanceTestAccess
{
    static SModuleEndpoints& module(tests::TTestContext& ctx, CHost& host, const module_ids::id_type id,
        const std::uint64_t identity, modules::CBoundModule* const binding = nullptr, const bool initialise_coordinator = true)
    {
        const auto index = module_ids::ops::decode_id(id).raw_value();
        TEST_EXPECT(ctx, host.m_modules[index].emplace());
        SModuleEndpoints& result = *host.m_modules[index];
        result.state.instance.value = identity;
        result.binding = binding;
        TEST_EXPECT(ctx, result.threads.initialise());
        TEST_EXPECT(ctx, result.coordinator.emplace(id, (identity + 1u), nullptr, result.state.instance));
        result.coordinator->endpoint->module_state = &result.state;
        if (initialise_coordinator)
        {
            TEST_EXPECT(ctx, result.coordinator->initialise());
            result.coordinator->phase = EClientPhase::ready;
        }
        return result;
    }

    static threading::CErasedOwnerMsg module_request(const module_ids::id_type id,
        const modules::EModuleAction action, const std::int32_t slot)
    {
        auto message = threading::make_owner_message<modules::ModuleRequest>(slot);
        modules::ModuleRequest* const request = message.owner().payload<modules::ModuleRequest>();
        request->module = id;
        request->action = action;
        (void)request->file.set("package:/bin/startup-fixture");
        return message;
    }

    static void startup_module_requests(tests::TTestContext& ctx)
    {
        for (unsigned scenario = 0u; scenario < 5u; ++scenario)
        {
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            CHost runtime{ allocator };
            runtime.m_phase = CHost::EPhase::starting;
            SModuleEndpoints& owned = module(ctx, runtime, module_ids::executive, (1200u + scenario));
            SClientRecord& client = *owned.coordinator;
            threading::CEndpointContext endpoint{ *client.endpoint };
            auto first = module_request(module_ids::rendering, modules::EModuleAction::load, 10);
            auto second = module_request(module_ids::audio, modules::EModuleAction::load, 11);
            TEST_EXPECT(ctx, (endpoint.post(std::move(first)) && endpoint.post(std::move(second))));
            runtime.drain_client(client);
            TEST_EXPECT(ctx, ((client.consumed_owners == 1u) && !client.accounted_owners &&
                (client.deferred_records == 1u) && (client.reserved_replies == 2u)));
            // A held owner must not prevent the readiness/control POD lane moving.
            TEST_EXPECT(ctx, endpoint.post(threading::make_pod_message(12, threading::CloseProviderAdmission{})));
            runtime.drain_client(client);
            TEST_EXPECT(ctx, ((client.accounted_pods == 1u) && (client.consumed_owners == 1u)));
            threading::CErasedPodMsg reply;
            TEST_EXPECT(ctx, (endpoint.read(reply) && reply.is_payload_a<threading::ProviderAdmissionClosed>()));
            runtime.m_maintenance.replay();
            TEST_EXPECT(ctx, ((client.deferred_records == 1u) && !client.accounted_owners));
            if (scenario == 0u) runtime.m_phase = CHost::EPhase::running;
            else if (scenario == 1u) client.request_exit();
            else if (scenario == 2u) client.phase = EClientPhase::stopped;
            else if (scenario == 3u) runtime.begin_emergency_shutdown();
            else owned.state.stopping.store(1u);
            runtime.m_maintenance.replay();
            TEST_EXPECT(ctx, (!client.deferred_records && (client.accounted_owners == 1u)));
            if (scenario == 0u)
            {
                TEST_EXPECT(ctx, !runtime.m_module_service.is_idle());
                runtime.m_module_service.cancel_pending();
            }
            while (endpoint.read(reply)) {}
            runtime.drain_client(client);
            TEST_EXPECT(ctx, ((client.accounted_owners == 2u) && !client.deferred_records));
            runtime.m_module_service.cancel_pending();
            while (endpoint.read(reply)) {}
            TEST_EXPECT(ctx, client.replies_idle());
            runtime.m_phase = CHost::EPhase::complete;
        }
    }

    static void early_self_change(tests::TTestContext& ctx)
    {
        for (const bool replacing : { false, true })
        {
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            CHost runtime{ allocator };
            runtime.m_phase = replacing ? CHost::EPhase::replacing_executive : CHost::EPhase::starting;
            SModuleEndpoints& owned = module(ctx, runtime, module_ids::executive, 1300u);
            SClientRecord& client = *owned.coordinator;
            threading::CEndpointContext endpoint{ *client.endpoint };
            tests::SEndpointFixture stale{ module_ids::executive, 1300u };
            auto invalid = module_request(module_ids::executive, modules::EModuleAction::replace, 20);
            TEST_EXPECT(ctx, stale.reserve_replies(2u));
            runtime.receive_request(invalid, stale);
            TEST_EXPECT(ctx, (runtime.m_executive_request.is_empty() && stale.replies_idle()));
            auto first = module_request(module_ids::executive, modules::EModuleAction::replace, 21);
            const modules::ModuleRequest* const retained = first.owner().payload<modules::ModuleRequest>();
            auto second = module_request(module_ids::executive, modules::EModuleAction::unload, 22);
            TEST_EXPECT(ctx, (endpoint.post(std::move(first)) && endpoint.post(std::move(second))));
            runtime.drain_client(client);
            TEST_EXPECT(ctx, ((runtime.m_executive_request.payload<modules::ModuleRequest>() == retained) &&
                !client.stop_requested && client.replies_idle() && !owned.state.stopping.load()));
            // Fatal startup wins over the pending successor request.
            runtime.executive_failure(modules::EModuleStatus::installation_failed);
            TEST_EXPECT(ctx, (runtime.m_executive_request.is_empty() && owned.state.stopping.load()));
            runtime.m_phase = CHost::EPhase::complete;
        }
    }

    static std::uint32_t MV_STD_ABI_CALL returned_entry(threading::CThreadContext&, void* const) noexcept
    {
        return 0u; //  The wrapper must record execution end without progress calls.
    }

    static std::uint32_t MV_STD_ABI_CALL requesting_entry(threading::CThreadContext& context, void* const) noexcept
    {
        const threading::CreateThreadRequest request{ &returned_entry, nullptr, thread_ids::audio };
        return context.post(threading::make_pod_message(19, request)) ? 0u : 1u;
    }

    static void thread_outcomes(tests::TTestContext& ctx, const bool requester_exits)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::rendering, 100u, &binding);
        threading::CEndpointContext endpoint{ *owned.coordinator->endpoint };
        const threading::CreateThreadRequest first{ requester_exits ? &requesting_entry : &returned_entry, nullptr, thread_ids::rendering };
        TEST_EXPECT(ctx, endpoint.post(threading::make_pod_message(11, first)));
        if (!requester_exits)
        {
            TEST_EXPECT(ctx, endpoint.post(threading::make_pod_message(12, threading::CreateThreadRequest{ &returned_entry, nullptr, thread_ids::audio })));
            TEST_EXPECT(ctx, endpoint.post(threading::make_pod_message(13, first)));
        }
        runtime.drain_client(*owned.coordinator);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            for (std::int32_t slot = owned.threads.first_live(); slot >= 0; slot = owned.threads.next_live(slot))
            {
                SRequestedThread& attempt = *owned.threads.get_object(slot);
                if (attempt.client) runtime.drain_client(*attempt.client);
            }
            runtime.progress_threads(owned);
            return owned.threads.is_empty();
        }));
        unsigned created = 0u, ended = 0u, retired = 0u, rejected = 0u;
        services::SClientToken identities[2];
        unsigned phases[2]{};
        threading::CErasedPodMsg message;
        while (endpoint.read(message))
        {
            threading::EndpointLifecycle lifecycle;
            if (!message.copy_payload_to(lifecycle))
            {
                continue;
            }
            if (lifecycle.status != services::EServiceStatus::success)
            {
                TEST_EXPECT(ctx, ((lifecycle.event == threading::EEndpointLifecycle::created) &&
                    (lifecycle.status == services::EServiceStatus::occupied) && (lifecycle.correlation == 13)));
                ++rejected;
                continue;
            }
            unsigned index = 0u;
            if (lifecycle.event == threading::EEndpointLifecycle::created)
            {
                index = created++;
                TEST_EXPECT(ctx, (index < 2u));
                if (index >= 2u) continue;
                identities[index] = lifecycle.endpoint;
                TEST_EXPECT(ctx, (lifecycle.endpoint.value && (lifecycle.endpoint.value != owned.state.instance.value)));
                if (requester_exits && (lifecycle.correlation == 19))
                {
                    TEST_EXPECT(ctx, (lifecycle.requester.value != owned.coordinator->identity.value));
                }
            }
            else
            {
                index = (identities[0].value == lifecycle.endpoint.value) ? 0u : 1u;
                TEST_EXPECT(ctx, (identities[index].value == lifecycle.endpoint.value));
                if (lifecycle.event == threading::EEndpointLifecycle::execution_ended) ++ended;
                else ++retired;
            }
            TEST_EXPECT(ctx, (phases[index]++ == static_cast<unsigned>(lifecycle.event)));
        }
        TEST_EXPECT(ctx, ((created == 2u) && (ended == 2u) && (retired == 2u) && (rejected == (requester_exits ? 0u : 1u))));
        TEST_EXPECT(ctx, (identities[0].value != identities[1].value));
        TEST_EXPECT(ctx, (owned.coordinator->replies_idle() && !runtime.m_emergency_shutdown));
    }

    struct SDirectPump
    {
        threading::CEndpointContext* endpoint{ nullptr };
        TInstance<connections::CClient> client;
        connections::SToken token;
        unsigned calls{ 0u }, received{ 0u }, markers{ 0u };
        bool direct_data{ true }, pending_response{ false }, stopping{ false }, bad_order{ false };
    };
    static inline SDirectPump* direct_pumps[2]{};
    static modules::ECallStatus MV_STD_ABI_CALL direct_activate(const modules::SHostPumpContext* const context,
        void** const value, modules::SProgress* const progress) noexcept
    {
        SDirectPump& state = *direct_pumps[(context->endpoint->module_id() == module_ids::rendering) ? 0u : 1u];
        state.endpoint = context->endpoint;
        if (!state.client.emplace(*context->endpoint) || !state.client->enable(1))
        {
            return modules::ECallStatus::failed;
        }
        *value = &state;
        *progress = {};
        progress->phase = modules::EPumpPhase::ready;
        return modules::ECallStatus::success;
    }
    static modules::ECallStatus MV_STD_ABI_CALL direct_pump(void* const value, const modules::SPumpInput* const,
        modules::SPumpResult* const result) noexcept
    {
        SDirectPump& state = *static_cast<SDirectPump*>(value);
        ++state.calls;
        connections::CClient& client = *state.client;
        client.clear_notifications();
        if (state.pending_response)
        {
            const connections::EPost posted = client.post(state.token, threading::make_pod_message(32, threading::EndpointReady{}));
            if (posted != connections::EPost::full) state.pending_response = false;
        }
        connections::SReceived received;
        for (unsigned index = 0u; (index < 4u) && client.receive(received, (state.direct_data && !state.pending_response)); ++index)
        {
            connections::Offer offer;
            connections::Result connected;
            if (!received.source.is_host())
            {
                if (received.message.query_async_slot() != static_cast<int>(state.received)) state.bad_order = true;
                ++state.received;
            }
            else if (received.message.copy_payload_to(offer)) (void)client.decide(offer.token, connections::EStatus::success);
            else if (received.message.copy_payload_to(connected)) state.token = connected.token;
            else if (received.message.is_payload_a<threading::EndpointReady>()) ++state.markers;
        }
        for (connections::SToken token = client.next_closing(); token.value; token = client.next_closing())
        {
            state.pending_response = false;
            if (!client.quiesce(token)) break;
        }
        client.progress();
        if (state.stopping && client.idle()) (void)state.endpoint->seal();
        *result = {};
        result->progress.phase = state.endpoint->sealed() ? modules::EPumpPhase::stopped :
            (state.stopping ? modules::EPumpPhase::stopping : modules::EPumpPhase::ready);
        result->progress.work_pending = (client.needs_progress() || client.has_data(state.direct_data && !state.pending_response) ||
            (state.stopping && !state.endpoint->sealed())) ? 1u : 0u;
        return (client.failed() && !state.stopping) ? modules::ECallStatus::failed : modules::ECallStatus::success;
    }
    static modules::ECallStatus MV_STD_ABI_CALL direct_stop(void* const value, const modules::EStopReason,
        modules::SProgress* const progress) noexcept
    {
        SDirectPump& state = *static_cast<SDirectPump*>(value);
        state.stopping = true;
        state.client->close_all();
        *progress = {};
        progress->phase = modules::EPumpPhase::stopping;
        progress->work_pending = 1u;
        return modules::ECallStatus::success;
    }
    static modules::ECallStatus MV_STD_ABI_CALL direct_deactivate(void* const value) noexcept
    {
        SDirectPump& state = *static_cast<SDirectPump*>(value);
        if (!state.endpoint->sealed())
        {
            return modules::ECallStatus::failed;
        }
        state.client.reset();
        return modules::ECallStatus::success;
    }
    static void direct_pump_pressure(tests::TTestContext& ctx, const bool close_under_pressure, const bool poison = false)
    {
        tests::TAllocationFailureFixture allocation;
        memory::CMemoryAllocator failing_allocator{ &allocation, tests::allocate_with_failure,
            tests::deallocate_test_memory, system_ids::host };
        memory::CMemoryContext memory{ failing_allocator, system_ids::host };
        tests::TMemoryContextScope scope{ &memory };
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& a = module(ctx, runtime, module_ids::rendering, 1400u, &binding, false);
        SModuleEndpoints& b = module(ctx, runtime, module_ids::audio, 1500u, &binding, false);
        const auto step = [&]
        {
            runtime.pump_clients();
            runtime.m_direct_connections.progress();
        };
        SDirectPump first, second;
        direct_pumps[0] = &first;
        direct_pumps[1] = &second;
        const modules::SHostPumpV1 capability{ sizeof(modules::SHostPumpV1), 1u, 0u,
            &direct_activate, &direct_pump, &direct_stop, &direct_deactivate };
        for (SModuleEndpoints* const owned : { &a, &b })
        {
            TEST_EXPECT(ctx, owned->pump.emplace(*owned->coordinator, binding, capability, runtime.m_service_router, runtime.m_maintenance));
            TEST_EXPECT(ctx, owned->pump->initialise());
            owned->pump->activate();
        }
        for (unsigned round = 0u; round < 3u; ++round) step();
        TEST_EXPECT(ctx, (first.client->enabled() && second.client->enabled()));
        TEST_EXPECT(ctx, first.client->open(second.client->address(), 2));
        for (unsigned round = 0u; round < 8u; ++round) step();
        const connections::SToken token = first.token;
        second.token = token;
        TEST_EXPECT(ctx, (first.client->active(token) && second.client->active(token)));
        const connections::SEndpointDescriptor* const descriptor = SDirectConnectionTestAccess::descriptor(runtime.m_direct_connections, token, 0u);
        TEST_EXPECT(ctx, descriptor);
        if (!descriptor) return;
        if (poison)
        {
            descriptor->outgoing->deallocate();
            TEST_EXPECT(ctx, descriptor->outgoing->initialise_growable(32u, 128u));
            const threading::CErasedPodMsg message = threading::make_pod_message(0, threading::EndpointReady{});
            for (unsigned index = 0u; index < 32u; ++index)
            {
                TEST_EXPECT(ctx, (first.client->post(token, message) == connections::EPost::queued));
            }
            allocation.fail_on = allocation.attempts + 1u;
            TEST_EXPECT(ctx, (first.client->post(token, message) == connections::EPost::transport_failed));
            TEST_EXPECT(ctx, (allocation.failed && descriptor->outgoing->posting_poisoned() && first.client->failed() &&
                (a.coordinator->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::direct_transport_poisoned)));
            TEST_EXPECT(ctx, (first.client->post(token, message) == connections::EPost::no_pipe));
            runtime.drain_client(*a.coordinator);
            TEST_EXPECT(ctx, (runtime.m_emergency_shutdown && !runtime.m_direct_connections.empty()));
        }
        else
        {
            descriptor->outgoing->deallocate();
            TEST_EXPECT(ctx, descriptor->outgoing->initialise_fixed(32u));
            second.direct_data = false;
            for (int index = 0; index < 32; ++index)
            {
                TEST_EXPECT(ctx, (first.client->post(token, threading::make_pod_message(index, threading::EndpointReady{})) == connections::EPost::queued));
            }
            first.pending_response = true;
            for (int index = 0; index < 4; ++index)
            {
                TEST_EXPECT(ctx, (second.client->post(token, threading::make_pod_message(index, threading::EndpointReady{})) == connections::EPost::queued));
            }
            TEST_EXPECT(ctx, a.coordinator->post(threading::make_pod_message(42, threading::EndpointReady{})));
            for (unsigned round = 0u; round < 3u; ++round) step();
            TEST_EXPECT(ctx, (first.pending_response && !first.received && (first.markers == 1u) && !second.received));
            runtime.m_turn_pending = false;
            step();
            TEST_EXPECT(ctx, (!a.pump->work_pending() && !b.pump->work_pending() && !runtime.m_turn_pending));
            if (close_under_pressure)
            {
                second.client->close(token);
                second.client->progress();
                for (unsigned round = 0u; round < 6u; ++round) step();
                TEST_EXPECT(ctx, (runtime.m_direct_connections.empty() && !first.pending_response && !first.received));
            }
            else
            {
                second.direct_data = true;
                b.coordinator->endpoint->signal_consumer();
                for (unsigned round = 0u; round < 16u; ++round) step();
                TEST_EXPECT(ctx, (!first.pending_response && (first.received == 4u) && (second.received == 33u) &&
                    !first.bad_order && !second.bad_order));
            }
        }
        runtime.stop_module(a, modules::EStopReason::requested);
        runtime.stop_module(b, modules::EStopReason::requested);
        for (unsigned round = 0u; round < 20u; ++round) step();
        TEST_EXPECT(ctx, (a.pump->stopped() && b.pump->stopped() && runtime.m_direct_connections.empty() && (runtime.m_emergency_shutdown == poison)));
        for (SModuleEndpoints* const owned : { &a, &b })
        {
            TEST_EXPECT(ctx, owned->pump->deactivate());
            runtime.drain_client(*owned->coordinator);
            TEST_EXPECT(ctx, owned->coordinator->retire_host_execution(true));
            owned->pump.reset();
            owned->coordinator.reset();
        }
        direct_pumps[0] = direct_pumps[1] = nullptr;
    }
    struct SReadyProbe
    {
        std::atomic<bool> published{ false }, finish{ false };
        bool valid_registration{ true }, ignore_stop{ false }, host_stop{ false };
        module_ids::id_type request_peer{};
    };
    static std::uint32_t MV_STD_ABI_CALL ready_entry(threading::CThreadContext& context, void* const value) noexcept
    {
        SReadyProbe& probe = *static_cast<SReadyProbe*>(value);
        connections::Register registration;
        if (!probe.valid_registration) registration.version = UINT32_MAX;
        bool posted = context.post(threading::make_pod_message(0, registration)) && context.publish_ready();
        if (posted && (probe.request_peer != module_ids::id_type{}))
        {
            posted = context.post(threading::make_pod_message(33, connections::Request{ system_ids::ops::make_system_id(context.module_id(), context.thread_id()), system_ids::ops::make_system_id(probe.request_peer, thread_ids::audio) }));
        }
        if (probe.host_stop)
        {
            for (std::int32_t slot = 0; posted && (slot < 64); ++slot)
            {
                posted = context.post(threading::make_pod_message(slot, threading::StopThreadRequest{ { UINT64_MAX } }));
            }
            posted = posted && context.host_execution() && context.host_execution()->stop.request();
        }
        probe.published.store(true, std::memory_order_release);
        while (!probe.finish.load(std::memory_order_acquire) && (probe.ignore_stop || !context.exit_requested()))
        {
            platform::threading::processor_relax();
        }
        return posted ? 0u : 1u;
    }
    static void readiness(tests::TTestContext& ctx, const unsigned scenario)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::rendering, 300u, &binding);
        SReadyProbe probe;
        probe.valid_registration = scenario != 1u;
        probe.ignore_stop = scenario == 9u;
        threading::CEndpointContext coordinator{ *owned.coordinator->endpoint };
        const threading::CreateThreadRequest request{ &ready_entry, &probe, thread_ids::rendering,
            platform::threading::EThreadPriority::Normal, 1u };
        TEST_EXPECT(ctx, coordinator.post(threading::make_pod_message(5, request)));
        runtime.drain_client(*owned.coordinator);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return probe.published.load(std::memory_order_acquire); }));
        SRequestedThread& child = *owned.threads.get_object(owned.threads.first_live());
        if (scenario == 2u) owned.state.stopping.store(1u);
        if (scenario == 4u) child.client->request_exit();
        if (scenario == 5u) child.client->seal_pending = true;
        if (scenario == 6u) child.client->provider_closed = true;
        if (scenario == 7u) threading::SThreadPackageTestAccess::preparation(*child.package, 2u);
        if (scenario == 8u) child.client->direct_connections = EDirectConnectionState::closing;
        if (scenario == 3u)
        {
            probe.finish.store(true, std::memory_order_release);
            TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return child.package->execution_ended(); }));
        }
        // Register and Ready are both buffered before the first Host drain.
        runtime.drain_client(*child.client);
        TEST_EXPECT(ctx, ((child.client->phase == EClientPhase::ready) == ((scenario == 0u) || (scenario == 1u) || (scenario == 9u))));
        if (scenario == 7u) threading::SThreadPackageTestAccess::preparation(*child.package, 1u);
        if (scenario == 9u) child.client->request_exit();
        runtime.progress_threads(owned);
        if ((scenario == 4u) && !owned.threads.is_empty())
        {
            const SRequestedThread& pending = *owned.threads.get_object(owned.threads.first_live());
            TEST_EXPECT(ctx, (pending.client->phase != EClientPhase::ready));
        }
        unsigned readiness_count = 0u;
        threading::CErasedPodMsg message;
        while (coordinator.read(message))
        {
            threading::EndpointReady ready;
            if (message.copy_payload_to(ready))
            {
                ++readiness_count;
                TEST_EXPECT(ctx, (ready.endpoint.value == owned.designated_service.value));
            }
        }
        TEST_EXPECT(ctx, (readiness_count == (((scenario == 0u) || (scenario == 1u)) ? 1u : 0u)));
        TEST_EXPECT(ctx, (runtime.m_emergency_shutdown == ((scenario == 5u) || (scenario == 7u))));
        probe.finish.store(true, std::memory_order_release);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            for (std::int32_t slot = owned.threads.first_live(); slot >= 0; slot = owned.threads.next_live(slot))
            {
                SRequestedThread& attempt = *owned.threads.get_object(slot);
                if (attempt.client) runtime.drain_client(*attempt.client);
            }
            runtime.progress_threads(owned);
            return owned.threads.is_empty();
        }));
    }
    static void MV_STD_ABI_CALL batch_work(void* const value) noexcept
    {
        ++*static_cast<unsigned*>(value);
    }
    static void terminal_batch(tests::TTestContext& ctx, const bool completed)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        memory::CMemoryContext memory{ allocator, system_ids::host };
        tests::TMemoryContextScope scope{ &memory };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::executable, 850u);
        SClientRecord& client = *owned.coordinator;
        TEST_EXPECT(ctx, client.install_host_execution(runtime.m_maintenance.restrictions().reader()));
        threading::SBatchShared shared;
        shared.runner_count = 1u;
        threading::CBatchClient batch{ shared, client.endpoint->control_state, client.endpoint->work_pending,
            module_ids::executable, &runtime };
        client.endpoint->batch_client = &batch;
        unsigned executed = 0u;
        TEST_EXPECT(ctx, (batch.submit(&batch_work, &executed, 7u).status == threading::EBatchSubmission::queued));
        const auto finish = [&]
        {
            threading::transports::TAcquiredArenaSlot<threading::SBatchWork,
                threading::k_batch_channel_capacity> acquired(shared.work);
            TEST_EXPECT(ctx, static_cast<bool>(acquired));
            if (!acquired) return;
            const threading::SBatchWork work = *acquired;
            TEST_EXPECT(ctx, acquired.recycle());
            work.function(work.object);
            TEST_EXPECT(ctx, work.requester->complete({ work.correlation, threading::EBatchCompletion::executed }));
        };
        if (completed) finish();
        client.phase = EClientPhase::stopped;
        runtime.drain_client(client);
        TEST_EXPECT(ctx, (runtime.m_emergency_shutdown && runtime.m_fatal_timed &&
            (client.endpoint->failure.load() == threading::EEndpointFailure::lifecycle_failure)));
        TEST_EXPECT(ctx, ((batch.outstanding() == 1u) && !client.execution_quiescent() &&
            !client.retire_host_execution(true) && client.subscription_live && (client.endpoint->notification_pins == 1u)));
        // Test-only cleanup after verifying retention; production never consumes
        // another endpoint's batch returns as an abandonment protocol.
        if (!completed) finish();
        threading::SBatchResponse response;
        TEST_EXPECT(ctx, (batch.receive(response) && (response.correlation == 7u) && (executed == 1u)));
        TEST_EXPECT(ctx, client.retire_host_execution(true));
        client.endpoint->batch_client = nullptr;
        runtime.m_phase = CHost::EPhase::complete;
    }

    static void child_host_stop(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::executive, 900u, &binding);
        threading::CEndpointContext coordinator{ *owned.coordinator->endpoint };
        SReadyProbe probe;
        probe.ignore_stop = probe.host_stop = true;
        const threading::CreateThreadRequest request{ &ready_entry, &probe, thread_ids::executive,
            platform::threading::EThreadPriority::Normal, 1u };
        TEST_EXPECT(ctx, coordinator.post(threading::make_pod_message(5, request)));
        runtime.drain_client(*owned.coordinator);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return probe.published.load(std::memory_order_acquire); }));
        SRequestedThread& child = *owned.threads.get_object(owned.threads.first_live());
        TEST_EXPECT(ctx, (child.client->host_stop_requested.load() && !owned.coordinator->host_stop_requested.load()));
        TEST_EXPECT(ctx, (child.client->endpoint->worker_to_host_msgs.refresh_readable_count() == 66u));
        CHostRestrictions& restrictions = runtime.m_maintenance.restrictions();
        TEST_EXPECT(ctx, restrictions.begin_invocation(owned.state.instance, true));
        const modules::SRestrictionEnterResult scope = restrictions.enter({ 1u });
        TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
        runtime.m_maintenance.poll_stop();
        TEST_EXPECT(ctx, (runtime.m_maintenance.stop_requested() && runtime.m_work_admission_closed));
        TEST_EXPECT(ctx, (child.client->endpoint->worker_to_host_msgs.refresh_readable_count() == 66u));
        runtime.apply_host_stop();
        TEST_EXPECT(ctx, (owned.state.stopping.load() && !child.client->stop_requested));
        TEST_EXPECT(ctx, ((restrictions.leave(scope.scope) == modules::ERestrictionStatus::success) && restrictions.end_invocation()));
        probe.finish.store(true, std::memory_order_release);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return child.package->execution_ended(); }));
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            for (std::int32_t slot = owned.threads.first_live(); slot >= 0; slot = owned.threads.next_live(slot))
            {
                SRequestedThread& attempt = *owned.threads.get_object(slot);
                if (attempt.client) runtime.drain_client(*attempt.client);
            }
            runtime.progress_threads(owned);
            return owned.threads.is_empty();
        }));
        TEST_EXPECT(ctx, !runtime.m_emergency_shutdown);
        runtime.m_phase = CHost::EPhase::complete;
    }

    struct SOrderedStop
    {
        threading::CEndpointContext* endpoint{ nullptr };
        services::SClientToken children[2];
        bool requested{ false }, stopping{ false }, stop_sent[2]{}, ended[2]{}, retired[2]{};
        bool closing{ false }, closed{ false };
        std::atomic<bool> a_started{ false }, b_started{ false }, a_cleaned{ false }, b_stopped{ false }, bad_order{ false };
    };
    static inline SOrderedStop* ordered{ nullptr };
    static std::uint32_t MV_STD_ABI_CALL ordered_a(threading::CThreadContext& context, void* const value) noexcept
    {
        SOrderedStop& state = *static_cast<SOrderedStop*>(value);
        state.a_started.store(true, std::memory_order_release);
        while (!context.exit_requested()) platform::threading::processor_relax();
        if (!state.b_started.load(std::memory_order_acquire) || state.b_stopped.load(std::memory_order_acquire))
        {
            state.bad_order.store(true, std::memory_order_release);
        }
        state.a_cleaned.store(true, std::memory_order_release);
        return 0u;
    }
    static std::uint32_t MV_STD_ABI_CALL ordered_b(threading::CThreadContext& context, void* const value) noexcept
    {
        SOrderedStop& state = *static_cast<SOrderedStop*>(value);
        state.b_started.store(true, std::memory_order_release);
        while (!context.exit_requested()) platform::threading::processor_relax();
        if (!state.a_cleaned.load(std::memory_order_acquire)) state.bad_order.store(true, std::memory_order_release);
        state.b_stopped.store(true, std::memory_order_release);
        return 0u;
    }
    static modules::ECallStatus MV_STD_ABI_CALL ordered_activate(const modules::SHostPumpContext* const context,
        void** const value, modules::SProgress* const progress) noexcept
    {
        ordered->endpoint = context->endpoint;
        *value = ordered;
        *progress = {};
        progress->work_pending = 1u;
        return modules::ECallStatus::success;
    }
    static modules::ECallStatus MV_STD_ABI_CALL ordered_pump(void* const value, const modules::SPumpInput* const,
        modules::SPumpResult* const result) noexcept
    {
        SOrderedStop& state = *static_cast<SOrderedStop*>(value);
        if (!state.requested)
        {
            const threading::CreateThreadRequest a{ &ordered_a, &state, thread_ids::rendering };
            const threading::CreateThreadRequest b{ &ordered_b, &state, thread_ids::audio };
            if (!state.endpoint->post(threading::make_pod_message(0, a)) ||
                !state.endpoint->post(threading::make_pod_message(1, b)))
            {
                return modules::ECallStatus::failed;
            }
            state.requested = true;
        }
        threading::CErasedPodMsg message;
        while (state.endpoint->read(message))
        {
            threading::CreateThreadResult created;
            threading::EndpointLifecycle lifecycle;
            if (message.copy_payload_to(created))
            {
                if ((created.status != services::EServiceStatus::success) || (message.query_async_slot() < 0) ||
                    (message.query_async_slot() > 1))
                {
                    return modules::ECallStatus::failed;
                }
                state.children[message.query_async_slot()] = created.endpoint;
            }
            else if (message.copy_payload_to(lifecycle) && (lifecycle.correlation >= 0) && (lifecycle.correlation <= 1))
            {
                if (lifecycle.event == threading::EEndpointLifecycle::execution_ended) state.ended[lifecycle.correlation] = true;
                if (lifecycle.event == threading::EEndpointLifecycle::retired) state.retired[lifecycle.correlation] = true;
            }
            else if (message.is_payload_a<threading::ProviderAdmissionClosed>()) state.closed = true;
        }
        if (state.stopping)
        {
            for (unsigned index = 0u; index < 2u; ++index)
            {
                if (state.children[index].value && !state.stop_sent[index] && ((index == 0u) || state.ended[0]))
                {
                    state.stop_sent[index] = state.endpoint->post(threading::make_pod_message((10 + index),
                        threading::StopThreadRequest{ state.children[index] }));
                }
            }
            if (state.retired[0] && state.retired[1])
            {
                if (!state.closing) state.closing = state.endpoint->post(threading::make_pod_message(20, threading::CloseProviderAdmission{}));
                if (state.closed) (void)state.endpoint->seal();
            }
        }
        *result = {};
        result->progress.work_pending = 1u;
        result->progress.phase = state.endpoint->sealed() ? modules::EPumpPhase::stopped :
            (state.stopping ? modules::EPumpPhase::stopping : ((state.children[0].value && state.children[1].value) ?
                modules::EPumpPhase::ready : modules::EPumpPhase::starting));
        return modules::ECallStatus::success;
    }
    static modules::ECallStatus MV_STD_ABI_CALL ordered_stop(void* const value, const modules::EStopReason,
        modules::SProgress* const progress) noexcept
    {
        static_cast<SOrderedStop*>(value)->stopping = true;
        *progress = {};
        progress->phase = modules::EPumpPhase::stopping;
        progress->work_pending = 1u;
        return modules::ECallStatus::success;
    }
    static modules::ECallStatus MV_STD_ABI_CALL ordered_deactivate(void* const value) noexcept
    {
        return static_cast<SOrderedStop*>(value)->endpoint->sealed() ? modules::ECallStatus::success : modules::ECallStatus::failed;
    }
    static void ordered_shutdown(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::rendering, 1000u, &binding, false);
        SOrderedStop state;
        ordered = &state;
        const modules::SHostPumpV1 capability{ sizeof(modules::SHostPumpV1), 1u, 0u,
            &ordered_activate, &ordered_pump, &ordered_stop, &ordered_deactivate };
        TEST_EXPECT(ctx, owned.pump.emplace(*owned.coordinator, binding, capability, runtime.m_service_router, runtime.m_maintenance));
        TEST_EXPECT(ctx, owned.pump->initialise());
        owned.pump->activate();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            runtime.pump_clients();
            return owned.pump->ready() && state.a_started.load() && state.b_started.load();
        }));
        runtime.stop_module(owned, modules::EStopReason::requested);
        for (std::int32_t slot = owned.threads.first_live(); slot >= 0; slot = owned.threads.next_live(slot))
        {
            TEST_EXPECT(ctx, !owned.threads.get_object(slot)->client->stop_requested);
        }
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            runtime.pump_clients();
            return owned.pump->stopped();
        }));
        TEST_EXPECT(ctx, (state.a_cleaned.load() && state.b_stopped.load() && !state.bad_order.load() &&
            !runtime.m_emergency_shutdown && owned.threads.is_empty()));
        TEST_EXPECT(ctx, owned.pump->deactivate());
        runtime.drain_client(*owned.coordinator);
        TEST_EXPECT(ctx, owned.coordinator->retire_host_execution(true));
        owned.pump.reset();
        owned.coordinator.reset();
        ordered = nullptr;
    }

    static void ready_fifo_request(tests::TTestContext& ctx, const bool preparation_first)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& primary = module(ctx, runtime, module_ids::rendering, 700u, &binding);
        SModuleEndpoints& peer = module(ctx, runtime, module_ids::audio, 800u, &binding);
        SReadyProbe peer_probe, primary_probe;
        primary_probe.request_peer = module_ids::audio;
        threading::CEndpointContext peer_coordinator{ *peer.coordinator->endpoint };
        threading::CEndpointContext primary_coordinator{ *primary.coordinator->endpoint };
        const threading::CreateThreadRequest peer_request{ &ready_entry, &peer_probe, thread_ids::audio,
            platform::threading::EThreadPriority::Normal, 1u };
        TEST_EXPECT(ctx, peer_coordinator.post(threading::make_pod_message(5, peer_request)));
        runtime.drain_client(*peer.coordinator);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return peer_probe.published.load(std::memory_order_acquire); }));
        SRequestedThread& peer_child = *peer.threads.get_object(peer.threads.first_live());
        runtime.drain_client(*peer_child.client);
        runtime.progress_threads(peer);
        const threading::CreateThreadRequest primary_request{ &ready_entry, &primary_probe, thread_ids::rendering,
            platform::threading::EThreadPriority::Normal, 1u };
        TEST_EXPECT(ctx, primary_coordinator.post(threading::make_pod_message(6, primary_request)));
        runtime.drain_client(*primary.coordinator);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return primary_probe.published.load(std::memory_order_acquire); }));
        SRequestedThread& child = *primary.threads.get_object(primary.threads.first_live());
        if (preparation_first) runtime.progress_threads(primary);
        TEST_EXPECT(ctx, ((child.package->preparation() == 1u) && (child.client->phase == EClientPhase::starting)));
        // Register, Ready and Request are processed in one Host drain. The direct
        // request must enter the existing handshake, not return unsupported.
        runtime.drain_client(*child.client);
        TEST_EXPECT(ctx, ((child.client->accounted_pods == 3u) && (child.client->phase == EClientPhase::ready) &&
            !runtime.m_direct_connections.client_idle(*child.client)));
        primary_probe.finish.store(true, std::memory_order_release);
        peer_probe.finish.store(true, std::memory_order_release);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            for (SModuleEndpoints* const owned : { &primary, &peer })
            {
                for (std::int32_t slot = owned->threads.first_live(); slot >= 0; slot = owned->threads.next_live(slot))
                {
                    SRequestedThread& attempt = *owned->threads.get_object(slot);
                    if (attempt.client) runtime.drain_client(*attempt.client);
                }
            }
            runtime.m_direct_connections.progress();
            runtime.progress_threads(primary);
            runtime.progress_threads(peer);
            return primary.threads.is_empty() && peer.threads.is_empty();
        }));
        TEST_EXPECT(ctx, (runtime.m_direct_connections.empty() && !runtime.m_emergency_shutdown));
    }

    static void stop_identity(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::rendering, 500u, &binding);
        SModuleEndpoints& other = module(ctx, runtime, module_ids::audio, 600u);
        threading::CEndpointContext coordinator{ *owned.coordinator->endpoint };
        threading::CEndpointContext outsider{ *other.coordinator->endpoint };
        services::SClientToken previous;
        for (unsigned generation = 0u; generation < 2u; ++generation)
        {
            SReadyProbe probe;
            const threading::CreateThreadRequest request{ &ready_entry, &probe, thread_ids::rendering,
                platform::threading::EThreadPriority::Normal, 1u };
            TEST_EXPECT(ctx, coordinator.post(threading::make_pod_message(5, request)));
            runtime.drain_client(*owned.coordinator);
            TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return probe.published.load(std::memory_order_acquire); }));
            SRequestedThread& child = *owned.threads.get_object(owned.threads.first_live());
            runtime.drain_client(*child.client);
            runtime.progress_threads(owned);
            const services::SClientToken identity = child.identity;
            threading::CErasedPodMsg message;
            while (coordinator.read(message)) {}
            if (generation)
            {
                TEST_EXPECT(ctx, (identity.value != previous.value));
                TEST_EXPECT(ctx, coordinator.post(threading::make_pod_message(10, threading::StopThreadRequest{ previous })));
                runtime.drain_client(*owned.coordinator);
                threading::StopThreadResult result;
                TEST_EXPECT(ctx, (coordinator.read(message) && message.copy_payload_to(result) &&
                    (result.status == services::EServiceStatus::stale) && !child.client->stop_requested));
                TEST_EXPECT(ctx, outsider.post(threading::make_pod_message(11, threading::StopThreadRequest{ identity })));
                runtime.drain_client(*other.coordinator);
                TEST_EXPECT(ctx, (outsider.read(message) && message.copy_payload_to(result) &&
                    (result.status == services::EServiceStatus::invalid_request) && !child.client->stop_requested));
                TEST_EXPECT(ctx, coordinator.post(threading::make_pod_message(12, threading::StopThreadRequest{ identity })));
                runtime.drain_client(*owned.coordinator);
                TEST_EXPECT(ctx, (coordinator.read(message) && message.copy_payload_to(result) &&
                    (result.status == services::EServiceStatus::success) && child.client->stop_requested));
            }
            previous = identity;
            probe.finish.store(true, std::memory_order_release);
            TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
            {
                for (std::int32_t slot = owned.threads.first_live(); slot >= 0; slot = owned.threads.next_live(slot))
                {
                    SRequestedThread& attempt = *owned.threads.get_object(slot);
                    if (attempt.client) runtime.drain_client(*attempt.client);
                }
                runtime.progress_threads(owned);
                return owned.threads.is_empty();
            }));
            while (coordinator.read(message)) {}
        }
        TEST_EXPECT(ctx, !runtime.m_emergency_shutdown);
    }

    static void rejected_creation(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding, true);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::rendering, 400u, &binding);
        threading::CEndpointContext endpoint{ *owned.coordinator->endpoint };
        TEST_EXPECT(ctx, endpoint.post(threading::make_pod_message(1, threading::CreateThreadRequest{})));
        TEST_EXPECT(ctx, endpoint.post(threading::make_pod_message(2, threading::CreateThreadRequest{ &returned_entry, nullptr, thread_ids::rendering })));
        runtime.drain_client(*owned.coordinator);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            for (std::int32_t slot = owned.threads.first_live(); slot >= 0; slot = owned.threads.next_live(slot))
            {
                SRequestedThread& attempt = *owned.threads.get_object(slot);
                if (attempt.client) runtime.drain_client(*attempt.client);
            }
            runtime.progress_threads(owned);
            return owned.threads.is_empty();
        }));
        unsigned outcomes = 0u, notices = 0u;
        threading::CErasedPodMsg message;
        while (endpoint.read(message))
        {
            threading::CreateThreadResult outcome;
            threading::EndpointLifecycle notice;
            if (message.copy_payload_to(outcome))
            {
                ++outcomes;
                TEST_EXPECT(ctx, (!outcome.endpoint.value && (outcome.status == ((message.query_async_slot() == 1) ?
                    services::EServiceStatus::invalid_request : services::EServiceStatus::native_failure))));
            }
            else if (message.copy_payload_to(notice))
            {
                ++notices;
                TEST_EXPECT(ctx, ((notice.event == threading::EEndpointLifecycle::created) && !notice.endpoint.value));
            }
        }
        TEST_EXPECT(ctx, ((outcomes == 2u) && (notices == 2u) && owned.coordinator->replies_idle() && !runtime.m_emergency_shutdown));
    }

    static void seal_lanes(tests::TTestContext& ctx, const bool owner_first)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::rendering, 200u);
        SClientRecord& client = *owned.coordinator;
        threading::CEndpointContext endpoint{ *client.endpoint };
        auto request = threading::make_owner_message<modules::ModuleRequest>(3);
        TEST_EXPECT(ctx, endpoint.post(std::move(request)));
        TEST_EXPECT(ctx, endpoint.seal());
        auto settlement = module_request(module_ids::audio, modules::EModuleAction::unload, 5);
        TEST_EXPECT(ctx, endpoint.post(std::move(settlement)));
        TEST_EXPECT(ctx, (endpoint.post(threading::make_pod_message(4, threading::CreateThreadRequest{})).status == threading::EEndpointPost::closed));
        auto& maintenance = runtime.m_maintenance;
        maintenance.m_lane = owner_first ? 2u : 0u;
        TEST_EXPECT(ctx, maintenance.restrictions().begin_invocation(client.instance, true));
        const auto scope = maintenance.restrictions().enter({ 0u });
        for (unsigned count = 0u; count < 16u; ++count) (void)maintenance.run();
        TEST_EXPECT(ctx, ((client.consumed_pods == 1u) && (client.consumed_owners == 2u) && !client.accounted_pods && !client.accounted_owners));
        TEST_EXPECT(ctx, !client.seal_acknowledged);
        TEST_EXPECT(ctx, (maintenance.restrictions().leave(scope.scope) == modules::ERestrictionStatus::success));
        TEST_EXPECT(ctx, maintenance.restrictions().end_invocation());
        maintenance.replay();
        runtime.drain_client(client);
        TEST_EXPECT(ctx, (client.seal_pending && client.seal_acknowledged && (client.accounted_owners == 2u) && (client.seal_boundary.owners == 1u)));
        threading::CErasedPodMsg message;
        while (endpoint.read(message)) {}
        TEST_EXPECT(ctx, endpoint.sealed());
        TEST_EXPECT(ctx, (client.endpoint->failure.load() == threading::EEndpointFailure::none));
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
            if (!failure)
            {
                TEST_EXPECT(ctx, notification.initialise(already_installed));
            }
            else
            {
                threading::SThreadPackageTestAccess::reject_create(package);
            }
            TEST_EXPECT(ctx, (package.install_host_execution(view, notification) && !package.startup()));
            TEST_EXPECT(ctx, threading::SThreadPackageTestAccess::rolled_back(package));
        }
    }

    static bool MV_STD_ABI_CALL reject_prepare(void* const, const thread_ids::id_type, void* const) noexcept
    { return false; }

    static void failed_preparation_join(tests::TTestContext& ctx)
    {
        platform::system::CPerfCountConversion conversion;
        TEST_EXPECT(ctx, conversion.init());
        CHostRestrictions restriction;
        const threading::ThreadConfig config{ thread_ids::rendering, module_ids::rendering,
            platform::threading::EThreadPriority::Normal, &unexpected_entry, &reject_prepare };
        threading::CThreadPackage package{ config, conversion };
        tests::SEndpointFixture client{ module_ids::rendering, 75u, &package };
        TEST_EXPECT(ctx, client.install_host_execution(restriction.reader()));
        TEST_EXPECT(ctx, package.startup());
        TEST_EXPECT(ctx, tests::wait_until(conversion, [&] { return package.execution_ended(); }));
        TEST_EXPECT(ctx, (package.preparation() == 2u));
        threading::SThreadPackageTestAccess::fail_join(package);
        client.terminal_before_drain = client.incoming_drained = true;
        TEST_EXPECT(ctx, (!package.join_terminated_execution() && !client.retire_host_execution(false)));
        TEST_EXPECT(ctx, threading::SThreadPackageTestAccess::retained(package));
        threading::SThreadPackageTestAccess::fail_join(package, false);
        TEST_EXPECT(ctx, (client.retire_host_execution(false) && package.shutdown()));
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
        TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
        const auto count = maintenance.run();
        TEST_EXPECT(ctx, ((count.status == modules::EMaintenanceStatus::success) && (count.operations == 32u) && count.continuation));
        const auto cursor = maintenance.m_lane;
        maintenance.m_running = true;
        const auto nested = maintenance.run();
        TEST_EXPECT(ctx, ((nested.status == modules::EMaintenanceStatus::reentrant) && !nested.operations && (maintenance.m_lane == cursor)));
        maintenance.m_running = false;
        clock.step = 600000u;
        const auto duration = maintenance.run();
        TEST_EXPECT(ctx, ((duration.status == modules::EMaintenanceStatus::success) && (duration.operations == 1u)));
        TEST_EXPECT(ctx, ((restriction.leave(scope.scope) == modules::ERestrictionStatus::success) && restriction.end_invocation()));
        TEST_EXPECT(ctx, (maintenance.run().status == modules::EMaintenanceStatus::invalid_context));
    }

    static void deferred_retirement(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        const auto index = module_ids::executive_index.raw_value();
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::executive, 10u);
        auto& client = *owned.coordinator;
        client.phase = EClientPhase::stopped;
        auto& maintenance = runtime.m_maintenance;
        TEST_EXPECT(ctx, client.install_host_execution(maintenance.restrictions().reader()));
        TEST_EXPECT(ctx, maintenance.restrictions().begin_invocation(client.instance, true));
        const auto scope = maintenance.restrictions().enter({ 0u });
        TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
        modules::ModuleRequest* retained[32]{};
        for (unsigned position = 0u; position < 32u; ++position)
        {
            auto& entry = maintenance.m_owners[position];
            auto owner = CErasedOwner::create<modules::ModuleRequest>();
            retained[position] = owner.payload<modules::ModuleRequest>();
            TEST_EXPECT(ctx, ((retained[position] != nullptr) && client.reserve_replies(2u)));
            entry.message.set_message_type<modules::ModuleRequest>();
            entry.message.set_owner(std::move(owner));
            entry.source.client = &client;
            entry.source.credits = 2u;
            maintenance.accepted(entry.source);
        }
        //  Even a caller claiming empty source queues cannot bypass retained captures.
        client.terminal_before_drain = client.incoming_drained = true;
        client.invocation_active = true;
        TEST_EXPECT(ctx, (!client.execution_quiescent() && !client.retire_host_execution(true)));
        const auto* ambient = memory::get_ambient_memory_context();
        const auto allocations = ambient->get_live_allocation_count();
        const auto full = maintenance.run();
        TEST_EXPECT(ctx, ((full.operations <= 32u) && full.capacity_pressure && (client.deferred_records == 32u)));
        TEST_EXPECT(ctx, (ambient->get_live_allocation_count() == allocations));
        TEST_EXPECT(ctx, client.host_execution.stop.request());
        const auto stopping = maintenance.run();
        TEST_EXPECT(ctx, (stopping.shutdown_requested && stopping.cancel_native && runtime.m_work_admission_closed));
        TEST_EXPECT(ctx, (runtime.m_shutdown_deadline.set_after_milliseconds(0u).code == platform::threading::EWaitError::none));
        (void)maintenance.run();
        TEST_EXPECT(ctx, runtime.m_runtime_failed);
        TEST_EXPECT(ctx, (client.subscription_live && (client.deferred_records == 32u) && !maintenance.empty()));
        for (unsigned position = 0u; position < 32u; ++position)
        {
            auto& entry = maintenance.m_owners[position];
            TEST_EXPECT(ctx, (entry.message.owner().payload<modules::ModuleRequest>() == retained[position]));
        }
        TEST_EXPECT(ctx, (maintenance.restrictions().leave(scope.scope) == modules::ERestrictionStatus::success));
        TEST_EXPECT(ctx, maintenance.restrictions().end_invocation());
        //  Simulate safe rejection after the native invocation: discharge every
        //  accepted owner and credit explicitly, never merely clear a queue count.
        client.invocation_active = false;
        for (unsigned position = 0u; position < 32u; ++position)
        {
            auto& entry = maintenance.m_owners[position];
            entry.message = {};
            client.cancel_reservation(entry.source.credits);
            maintenance.released(entry.source);
        }
        TEST_EXPECT(ctx, (maintenance.empty() && client.execution_quiescent() && client.retire_host_execution(true)));
        runtime.m_retain_runtime = false; //  Test proved and performed all deferred retirement.
        TEST_EXPECT(ctx, runtime.shutdown());
    }

    struct SSubscriber
    {
        std::atomic<unsigned> ready{ 0u }, restricted{ 0u }, resumed{ 0u }, failed{ 0u };
        std::atomic<unsigned> restricted_observations{ 0u };
        std::atomic<unsigned> fanout_complete{ 0u }, quiet_wait{ 0u };
    };

    static std::uint32_t MV_STD_ABI_CALL subscriber(void* const data) noexcept
    {
        auto& resources = *static_cast<threading::CThreadResources*>(data);
        SSubscriber& state = tests::TThreadFixtureState<SSubscriber>::get();
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
            if (read == modules::ERestrictionRead::failed)
            {
                state.failed.store(1u);
                break;
            }
            if (read == modules::ERestrictionRead::retry)
            {
                continue;
            }
            if (snapshot.depth)
            {
                state.restricted_observations.fetch_add(1u);
                state.restricted.store(1u, std::memory_order_release);
            }
            else if (state.restricted.load(std::memory_order_acquire))
            {
                state.resumed.store(1u, std::memory_order_release);
            }
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
            {
                state.failed.store(1u);
                break;
            }
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
        TEST_EXPECT(ctx, (runtime.m_perf_count_conversion.init() && runtime.m_thread_packages.initialise()));
        SSubscriber state;
        const threading::ThreadConfig config{ thread_ids::executive, module_ids::executive,
            platform::threading::EThreadPriority::Normal, &subscriber, &tests::TThreadFixtureState<SSubscriber>::prepare, &state };
        const auto index = module_ids::executive_index.raw_value();
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::executive, 28u);
        const std::int32_t slot = owned.threads.emplace();
        SRequestedThread& attempt = *owned.threads.get_object(slot);
        TEST_EXPECT(ctx, attempt.package.emplace(config, runtime.m_perf_count_conversion));
        auto* const package = &*attempt.package;
        TEST_EXPECT(ctx, attempt.client.emplace(module_ids::executive, 30u, package, owned.state.instance));
        auto& client = *attempt.client;
        TEST_EXPECT(ctx, client.initialise());
        TEST_EXPECT(ctx, (client.install_host_execution(runtime.m_maintenance.restrictions().reader()) && package->startup()));
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.ready.load(std::memory_order_acquire) != 0u; }));
        auto& restriction = runtime.m_maintenance.restrictions();
        TEST_EXPECT(ctx, restriction.begin_invocation({ 30u }, true));
        const auto scope = restriction.enter({ 5u });
        TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.restricted.load(std::memory_order_acquire) != 0u; }));
        runtime.m_maintenance.fanout();
        state.fanout_complete.store(1u, std::memory_order_release);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            return (state.quiet_wait.load(std::memory_order_acquire) != 0u) &&
                (package->query_state() == threading::EThreadRunState::Waiting);
        }));
        const auto observations = state.restricted_observations.load();
        TEST_EXPECT(ctx, (observations == 2u));
        std::atomic<std::uint32_t> unchanged{ 0u };
        threading::CWaitDeadline pause;
        TEST_EXPECT(ctx, (pause.set_after_milliseconds(20u).code == platform::threading::EWaitError::none));
        TEST_EXPECT(ctx, (platform::threading::wait_until_not_equal(unchanged, 0u, pause).status == threading::EWaitStatus::timed_out));
        TEST_EXPECT(ctx, (state.restricted_observations.load() == observations));
        TEST_EXPECT(ctx, ((restriction.leave(scope.scope) == modules::ERestrictionStatus::success) && restriction.end_invocation()));
        runtime.m_maintenance.fanout();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.resumed.load(std::memory_order_acquire) != 0u; }));
        client.request_exit();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return package->execution_ended(); }));
        client.terminal_before_drain = client.incoming_drained = true;
        TEST_EXPECT(ctx, !package->shutdown());
        threading::SThreadPackageTestAccess::fail_join(*package);
        TEST_EXPECT(ctx, (!client.retire_host_execution(false) && client.subscription_live &&
            threading::SThreadPackageTestAccess::retained(*package)));
        threading::SThreadPackageTestAccess::fail_join(*package, false);
        TEST_EXPECT(ctx, (client.retire_host_execution(false) && package->shutdown() && !state.failed.load()));
        TEST_EXPECT(ctx, runtime.shutdown());
    }
};

}   //  namespace host

void run_host_maintenance_tests(tests::TTestContext& ctx)
{
    host::SHostMaintenanceTestAccess::startup_module_requests(ctx);
    host::SHostMaintenanceTestAccess::early_self_change(ctx);
    host::SHostMaintenanceTestAccess::startup_rollback(ctx);
    host::SHostMaintenanceTestAccess::failed_preparation_join(ctx);
    host::SHostMaintenanceTestAccess::bounds(ctx);
    host::SHostMaintenanceTestAccess::deferred_retirement(ctx);
    host::SHostMaintenanceTestAccess::installed_subscription(ctx);
    host::SHostMaintenanceTestAccess::thread_outcomes(ctx, false);
    host::SHostMaintenanceTestAccess::thread_outcomes(ctx, true);
    host::SHostMaintenanceTestAccess::seal_lanes(ctx, false);
    host::SHostMaintenanceTestAccess::seal_lanes(ctx, true);
    host::SHostMaintenanceTestAccess::terminal_batch(ctx, false);
    host::SHostMaintenanceTestAccess::terminal_batch(ctx, true);
    host::SHostMaintenanceTestAccess::child_host_stop(ctx);
    host::SHostMaintenanceTestAccess::ordered_shutdown(ctx);
    host::SHostMaintenanceTestAccess::direct_pump_pressure(ctx, false);
    host::SHostMaintenanceTestAccess::direct_pump_pressure(ctx, true);
    host::SHostMaintenanceTestAccess::direct_pump_pressure(ctx, false, true);
    host::SHostMaintenanceTestAccess::ready_fifo_request(ctx, false);
    host::SHostMaintenanceTestAccess::ready_fifo_request(ctx, true);
    host::SHostMaintenanceTestAccess::stop_identity(ctx);
    host::SHostMaintenanceTestAccess::rejected_creation(ctx);
    for (unsigned scenario = 0u; scenario < 10u; ++scenario) host::SHostMaintenanceTestAccess::readiness(ctx, scenario);
}
