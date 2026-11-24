
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostMaintenance_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    03 Nov 26
//
//  Actual Host lifetime retention, bounded probes and installed subscriptions.

#include "framework/system/system_ids.hpp"
#include "host/runtime/host.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/endpoint_fixture.hpp"
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
        const std::uint64_t identity, modules::CBoundModule* const binding)
    {
        const auto index = module_ids::ops::decode_id(id).raw_value();
        TEST_EXPECT(ctx, host.m_modules[index].emplace());
        SModuleEndpoints& result = *host.m_modules[index];
        result.state.instance.value = identity;
        result.binding = binding;
        TEST_EXPECT(ctx, result.threads.initialise());
        TEST_EXPECT(ctx, result.coordinator.emplace(id, (identity + 1u), nullptr, result.state.instance));
        result.coordinator->endpoint->module_state = &result.state;
        return result;
    }

    static SModuleEndpoints& common_module(tests::TTestContext& ctx, CHost& host,
        const module_ids::id_type id, const std::uint64_t identity, modules::CBoundModule& binding)
    {
        SModuleEndpoints& result = module(ctx, host, id, identity, &binding);
        result.coordinator->owner_wake = host.m_owner_wait.endpoint();
        TEST_EXPECT(ctx, result.coordinator->initialise_execution(binding, {}, nullptr));
        result.coordinator->phase = EClientPhase::ready;
        return result;
    }

    static bool submit_owner(SClientRecord& client, threading::CErasedOwnerMsg&& message) noexcept
    {
        if (!client.endpoint->worker_to_host_owned_msgs.post(std::move(message)))
        {
            return false;
        }
        ++client.endpoint->published_owners;
        return client.channel->signal_host();
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
            modules::CBoundModule binding;
            CHost runtime{ allocator };
            runtime.m_phase = CHost::EPhase::starting;
            SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::executive, (1200u + scenario), binding);
            SClientRecord& client = *owned.coordinator;
            auto first = module_request(module_ids::rendering, modules::EModuleAction::load, 10);
            auto second = module_request(module_ids::audio, modules::EModuleAction::load, 11);
            TEST_EXPECT(ctx, (submit_owner(client, std::move(first)) && submit_owner(client, std::move(second))));
            runtime.drain_module_inputs();
            TEST_EXPECT(ctx, ((client.consumed_owners == 1u) && !client.accounted_owners &&
                client.owner_head_live && !client.reserved_replies &&
                !client.pending_replies->reservations() && !runtime.module_owner_runnable(client)));
            // A blocked owning head leaves the private control lane available.
            SLifecycleMessage stop;
            stop.operation = ELifecycle::stop_child;
            stop.correlation = 12u;
            stop.notification.first = { 9000u };
            TEST_EXPECT(ctx, client.channel->preamble_control(stop));
            runtime.drain_module_inputs();
            SLifecycleMessage outcome;
            TEST_EXPECT(ctx, (client.channel->read_preamble_control(outcome) &&
                (outcome.operation == ELifecycle::child_stop_result) && (outcome.correlation == 12u) &&
                (outcome.notification.status == services::EServiceStatus::stale) && !client.accounted_pods &&
                (client.consumed_owners == 1u)));
            threading::CErasedPodMsg reply;
            runtime.m_turn_pending = false;
            runtime.schedule_module_work();
            TEST_EXPECT(ctx, (!runtime.m_turn_pending && client.owner_head_live && !client.accounted_owners));
            if (scenario == 0u) runtime.m_phase = CHost::EPhase::running;
            else if (scenario == 1u) client.request_exit();
            else if (scenario == 2u) client.phase = EClientPhase::stopped;
            else if (scenario == 3u) runtime.begin_emergency_shutdown();
            else owned.state.stopping.store(1u);
            runtime.schedule_module_work();
            TEST_EXPECT(ctx, (runtime.m_turn_pending && runtime.module_owner_runnable(client)));
            runtime.drain_module_inputs();
            TEST_EXPECT(ctx, ((client.accounted_owners == 1u) && (client.consumed_owners == 2u) &&
                client.owner_head_live && !client.endpoint->worker_to_host_owned_msgs.readable_count() &&
                runtime.module_owner_runnable(client) && !client.pending_replies->reservations()));
            if (scenario == 0u)
            {
                TEST_EXPECT(ctx, !runtime.m_module_service.is_idle());
                runtime.m_module_service.cancel_pending();
            }
            while (client.channel->read_preamble_pod(reply)) {}
            runtime.drain_module_inputs();
            TEST_EXPECT(ctx, ((client.accounted_owners == 2u) && !client.owner_head_live));
            runtime.m_module_service.cancel_pending();
            runtime.progress_module_output(client);
            while (client.channel->read_preamble_pod(reply)) {}
            TEST_EXPECT(ctx, (client.replies_idle() && (client.endpoint->failure.load() == threading::EEndpointFailure::none)));
            runtime.m_phase = CHost::EPhase::complete;
        }
    }

    static void early_self_change(tests::TTestContext& ctx)
    {
        for (const bool replacing : { false, true })
        {
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            modules::CBoundModule binding;
            CHost runtime{ allocator };
            runtime.m_phase = replacing ? CHost::EPhase::replacing_executive : CHost::EPhase::starting;
            SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::executive, 1300u, binding);
            SClientRecord& client = *owned.coordinator;
            tests::SCommonEndpointFixture stale{ module_ids::executive, 1300u };
            auto invalid = module_request(module_ids::executive, modules::EModuleAction::replace, 20);
            {
                CRetainedReplies::CDispatch dispatch{ *stale.pending_replies, 2u };
                TEST_EXPECT(ctx, (dispatch && stale.reserve_replies(2u)));
                runtime.receive_request(invalid, stale);
            }
            TEST_EXPECT(ctx, (runtime.m_executive_request.is_empty() && stale.replies_idle()));
            auto first = module_request(module_ids::executive, modules::EModuleAction::replace, 21);
            const modules::ModuleRequest* const retained = first.owner().payload<modules::ModuleRequest>();
            auto second = module_request(module_ids::executive, modules::EModuleAction::unload, 22);
            TEST_EXPECT(ctx, (submit_owner(client, std::move(first)) && submit_owner(client, std::move(second))));
            runtime.drain_module_inputs();
            runtime.drain_module_inputs();
            TEST_EXPECT(ctx, ((runtime.m_executive_request.payload<modules::ModuleRequest>() == retained) &&
                !client.stop_requested && client.replies_idle() && !owned.state.stopping.load() &&
                (client.accounted_owners == 2u) && (client.endpoint->failure.load() == threading::EEndpointFailure::none)));
            // Fatal startup wins over the pending successor request.
            runtime.executive_failure(modules::EModuleStatus::installation_failed);
            TEST_EXPECT(ctx, (runtime.m_executive_request.is_empty() && owned.state.stopping.load()));
            runtime.m_phase = CHost::EPhase::complete;
        }
    }

    class CChildExecution
    {
    public:
        std::atomic<bool> started{ false }, finish{ false }, stopped{ false }, published{ false };
        bool hold_cleanup{}, request_host_stop{}, publish_ready{ true }, participate{};
        system_ids::id_type peer{}, address{};
        std::atomic<bool> active{ false };
        CChildExecution* child{ nullptr };
        modules::CCommunication communication;
        const modules::SHostExecutionContext* execution{ nullptr };
        platform::threading::CPlatformThreadId created_on, destroyed_on;
        unsigned destroyed{}, child_created{}, child_ended{}, child_retired{};
        services::SClientToken identity, child_identity;
        bool child_requested{};
        unsigned ordinary_sent{};
        bool participation_sent{}, participated{}, connect_sent{}, closing{}, quiescent{};
        connections::SToken connection;

        modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
        {
            if (invocation.event == modules::EExecutionEvent::start) started.store(true, std::memory_order_release);
            if (invocation.event == modules::EExecutionEvent::stop) stopped.store(true, std::memory_order_release);
            switch (invocation.event)
            {
                case modules::EExecutionEvent::participation:
                {
                    participated = invocation.notification.connection_status == connections::EStatus::success;
                    if (!participated)
                    {
                        return modules::EExecutionStatus::failed;
                    }
                    break;
                }
                case modules::EExecutionEvent::connection_offered:
                {
                    connection = invocation.notification.connection;
                    modules::SExecutionIntent consent;
                    consent.kind = modules::SExecutionIntent::EKind::consent;
                    consent.connection = connection;
                    consent.decision = connections::EStatus::success;
                    if (!modules::accepted(communication.submit(consent)))
                    {
                        return modules::EExecutionStatus::failed;
                    }
                    break;
                }
                case modules::EExecutionEvent::connection_available:
                {
                    active.store(true, std::memory_order_release);
                    break;
                }
                case modules::EExecutionEvent::connection_closing:
                {
                    active.store(false, std::memory_order_release);
                    closing = true;
                    break;
                }
                case modules::EExecutionEvent::child_created:
                {
                    if ((invocation.notification.status != services::EServiceStatus::success) || child_created++)
                    {
                        return modules::EExecutionStatus::failed;
                    }
                    child_identity = invocation.notification.first;
                    break;
                }
                case modules::EExecutionEvent::child_ended:
                {
                    if (!child_created || (invocation.notification.first.value != child_identity.value) || child_ended++)
                    {
                        return modules::EExecutionStatus::failed;
                    }
                    break;
                }
                case modules::EExecutionEvent::child_retired:
                {
                    if (!child_ended || (invocation.notification.first.value != child_identity.value) || child_retired++)
                    {
                        return modules::EExecutionStatus::failed;
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }
            if (participate && !participation_sent && !stopped.load(std::memory_order_acquire))
            {
                modules::SExecutionIntent request;
                request.kind = modules::SExecutionIntent::EKind::participate;
                request.correlation = 0;
                participation_sent = modules::accepted(communication.submit(request));
            }
            if (participated && system_ids::ops::is_valid_id(peer) && !connect_sent && !stopped.load(std::memory_order_acquire))
            {
                modules::SExecutionIntent request;
                request.kind = modules::SExecutionIntent::EKind::connect;
                request.correlation = 33;
                request.first = address;
                request.second = peer;
                connect_sent = modules::accepted(communication.submit(request));
            }
            if (closing && !quiescent)
            {
                modules::SExecutionIntent request;
                request.kind = modules::SExecutionIntent::EKind::connection_quiescent;
                request.connection = connection;
                quiescent = modules::accepted(communication.submit(request));
            }
            if (child && !child_requested && !stopped.load(std::memory_order_acquire))
            {
                modules::SExecutionIntent request;
                request.kind = modules::SExecutionIntent::EKind::create_child;
                request.correlation = 19;
                request.child = { &child_execution, child, thread_ids::audio };
                const modules::ESubmission status = communication.submit(request);
                child_requested = modules::accepted(status);
                if (!child_requested && (status != modules::ESubmission::busy))
                {
                    return modules::EExecutionStatus::failed;
                }
            }
            if (request_host_stop && !published.load(std::memory_order_acquire))
            {
                while (ordinary_sent < 64u)
                {
                    const modules::ESubmission status = communication.submit({}, threading::make_pod_message(
                        static_cast<std::int32_t>(ordinary_sent), services::CloseServiceRequest{}));
                    if (status == modules::ESubmission::busy) break;
                    if (!modules::accepted(status))
                    {
                        return modules::EExecutionStatus::failed;
                    }
                    ++ordinary_sent;
                }
                if (ordinary_sent == 64u)
                {
                    if (!execution || !execution->stop.request())
                    {
                        return modules::EExecutionStatus::failed;
                    }
                    published.store(true, std::memory_order_release);
                }
            }
            progress = {};
            const bool done = finish.load(std::memory_order_acquire);
            progress.ready = (publish_ready && !stopped.load(std::memory_order_acquire)) ? 1u : 0u;
            progress.work_complete = (stopped.load(std::memory_order_acquire) && (!hold_cleanup || done)) ? 1u : 0u;
            return modules::EExecutionStatus::success;
        }
    };
    static modules::EExecutionStatus MV_STD_ABI_CALL child_create(const modules::SExecutionContext* const context,
        void** const value) noexcept
    {
        CChildExecution& state = *static_cast<CChildExecution*>(context->creation_context);
        state.communication = *context->communication;
        state.execution = context->execution;
        state.identity = context->endpoint;
        state.address = context->address;
        state.created_on = platform::threading::query_current_thread_id();
        *value = &state;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL child_handle(void* const value,
        const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
    {
        return static_cast<CChildExecution*>(value)->handle(*invocation, *progress);
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL child_destroy(void* const value) noexcept
    {
        CChildExecution& state = *static_cast<CChildExecution*>(value);
        ++state.destroyed;
        state.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
    static inline const modules::SExecutionV1 child_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &child_create, &child_handle, &child_destroy };

    static void submit_child(tests::TTestContext& ctx, SClientRecord& requester, CChildExecution& state,
        const thread_ids::id_type thread, const std::int32_t correlation, const bool designated = false)
    {
        SLifecycleMessage request;
        request.operation = ELifecycle::create_child;
        request.correlation = correlation;
        request.child = { &child_execution, &state, thread, platform::threading::EThreadPriority::Normal, designated ? 1u : 0u };
        TEST_EXPECT(ctx, requester.channel->preamble_control(request));
    }
    static bool progress_children(CHost& runtime, SModuleEndpoints& owned) noexcept
    {
        runtime.drain_module_inputs();
        for (std::int32_t slot = owned.threads.first_live(); slot >= 0; slot = owned.threads.next_live(slot))
        {
            SRequestedThread& attempt = *owned.threads.get_object(slot);
            if (attempt.client) runtime.drain_client(*attempt.client);
        }
        runtime.progress_threads(owned);
        return owned.threads.is_empty();
    }
    static void expect_child_destroyed(tests::TTestContext& ctx, const CChildExecution& state)
    {
        TEST_EXPECT(ctx, ((state.destroyed == 1u) && (state.created_on == state.destroyed_on) &&
            (state.created_on != platform::threading::query_current_thread_id())));
    }

    static void thread_outcomes(tests::TTestContext& ctx, const bool requester_stops)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::rendering, 100u, binding);
        CChildExecution first, second;
        if (requester_stops) first.child = &second;
        submit_child(ctx, *owned.coordinator, first, thread_ids::rendering, 11);
        if (!requester_stops)
        {
            submit_child(ctx, *owned.coordinator, second, thread_ids::audio, 12);
            submit_child(ctx, *owned.coordinator, first, thread_ids::rendering, 13);
        }
        runtime.drain_module_inputs();
        if (requester_stops)
        {
            TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
            {
                (void)progress_children(runtime, owned);
                return second.started.load(std::memory_order_acquire);
            }));
            SClientRecord* const parent = runtime.endpoint_client(system_ids::ops::make_system_id(module_ids::rendering, thread_ids::rendering));
            TEST_EXPECT(ctx, (parent && (parent->identity.value == first.identity.value)));
            if (!parent) return;
            parent->request_exit();
            TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
            {
                (void)progress_children(runtime, owned);
                return first.stopped.load(std::memory_order_acquire);
            }));
            TEST_EXPECT(ctx, (!parent->thread->execution_ended() && !parent->execution_quiescent()));
            // The requester has finished domain work, but its preamble keeps the
            // accepted child request and module state alive through retirement.
        }
        for (std::int32_t slot = owned.threads.first_live(); slot >= 0; slot = owned.threads.next_live(slot))
        {
            SRequestedThread& attempt = *owned.threads.get_object(slot);
            if (attempt.client) attempt.client->request_exit();
        }
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return progress_children(runtime, owned); }));
        unsigned created = 0u, ended = 0u, retired = 0u, rejected = 0u;
        services::SClientToken identities[2];
        unsigned phases[2]{};
        SLifecycleMessage message;
        while (owned.coordinator->channel->read_preamble_control(message))
        {
            if (message.notification.status != services::EServiceStatus::success)
            {
                TEST_EXPECT(ctx, ((message.operation == ELifecycle::child_created) &&
                    (message.notification.status == services::EServiceStatus::occupied) && (message.correlation == 13)));
                ++rejected;
                continue;
            }
            if (message.operation == ELifecycle::child_ready)
            {
                continue;
            }
            unsigned index = 0u;
            unsigned phase = 0u;
            if (message.operation == ELifecycle::child_created)
            {
                index = created++;
                TEST_EXPECT(ctx, (index < 2u));
                if (index >= 2u) continue;
                identities[index] = message.notification.first;
                TEST_EXPECT(ctx, (identities[index].value && (identities[index].value != owned.state.instance.value) &&
                    (message.notification.second.value == owned.coordinator->identity.value)));
            }
            else
            {
                index = (identities[0].value == message.notification.first.value) ? 0u : 1u;
                TEST_EXPECT(ctx, (identities[index].value == message.notification.first.value));
                if (message.operation == ELifecycle::child_ended)
                {
                    ++ended;
                    phase = 1u;
                }
                else
                {
                    TEST_EXPECT(ctx, (message.operation == ELifecycle::child_retired));
                    ++retired;
                    phase = 2u;
                }
            }
            TEST_EXPECT(ctx, (phases[index]++ == phase));
        }
        const unsigned expected = requester_stops ? 1u : 2u;
        TEST_EXPECT(ctx, ((created == expected) && (ended == expected) && (retired == expected) &&
            (rejected == (requester_stops ? 0u : 1u))));
        if (requester_stops)
        {
            TEST_EXPECT(ctx, ((first.child_created == 1u) && (first.child_ended == 1u) && (first.child_retired == 1u) &&
                first.child_identity.value && (first.child_identity.value != identities[0].value)));
        }
        else TEST_EXPECT(ctx, (identities[0].value != identities[1].value));
        expect_child_destroyed(ctx, first);
        expect_child_destroyed(ctx, second);
        TEST_EXPECT(ctx, (owned.coordinator->replies_idle() && !runtime.m_emergency_shutdown));
    }
    class CDirectPump
    {
    public:
        modules::CCommunication communication;
        system_ids::id_type address{}, peer{};
        connections::SToken token;
        bool participated{}, participation_sent{}, connect{}, connect_sent{}, active{}, closing{}, quiescent{};
        bool stopping{}, close{}, direct_data{ true }, waiting_output{}, valid{ true };
        std::uint32_t send_next{}, send_limit{}, received{}, markers{}, destroyed{};
        platform::threading::CPlatformThreadId created_on, destroyed_on;

        void handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
        {
            using Event = modules::EExecutionEvent;
            using Intent = modules::SExecutionIntent::EKind;
            modules::SExecutionIntent request;
            request.connection = invocation.notification.connection;
            switch (invocation.event)
            {
                case Event::participation:
                {
                    participated = invocation.notification.connection_status == connections::EStatus::success;
                    valid = valid && participated;
                    break;
                }
                case Event::connection_offered:
                {
                    token = invocation.notification.connection;
                    request.kind = Intent::consent;
                    request.decision = connections::EStatus::success;
                    valid = valid && modules::accepted(communication.submit(request));
                    break;
                }
                case Event::connection_available:
                {
                    active = true;
                    break;
                }
                case Event::connection_closing:
                {
                    active = waiting_output = false;
                    closing = true;
                    send_limit = send_next;
                    break;
                }
                case Event::connection_retired:
                {
                    closing = false;
                    break;
                }
                case Event::message:
                {
                    if (invocation.source.is_host()) ++markers;
                    else
                    {
                        valid = valid && active && (invocation.source.address == peer) &&
                            (invocation.source.connection.value == token.value) &&
                            (invocation.message->query_async_slot() == static_cast<std::int32_t>(received));
                        ++received;
                    }
                    break;
                }
                case Event::stop:
                {
                    stopping = true;
                    break;
                }
                default:
                {
                    break;
                }
            }
            if (!stopping && !participation_sent)
            {
                request.kind = Intent::participate;
                request.correlation = 1;
                participation_sent = modules::accepted(communication.submit(request));
            }
            if (!stopping && connect && participated && !connect_sent)
            {
                request.kind = Intent::connect;
                request.correlation = 2;
                request.first = address;
                request.second = peer;
                connect_sent = modules::accepted(communication.submit(request));
            }
            for (unsigned count = 0u; active && !stopping && (send_next < send_limit) && (count < 8u); ++count)
            {
                const modules::ESubmission status = communication.submit({ modules::SDestination::EKind::connection, {}, token },
                    threading::make_pod_message(static_cast<std::int32_t>(send_next), services::OpenServiceResult{}));
                waiting_output = status == modules::ESubmission::busy;
                if (waiting_output) break;
                valid = valid && modules::accepted(status);
                if (!modules::accepted(status)) break;
                ++send_next;
            }
            if (close)
            {
                request.kind = Intent::close_connection;
                request.connection = token;
                if (modules::accepted(communication.submit(request))) close = false;
            }
            if (closing && !quiescent)
            {
                request.kind = Intent::connection_quiescent;
                request.connection = token;
                quiescent = modules::accepted(communication.submit(request));
            }
            progress = {};
            progress.ready = participated ? 1u : 0u;
            progress.direct_input = (direct_data && !waiting_output) ? 1u : 0u;
            progress.work_pending = (active && !stopping && !waiting_output && (send_next < send_limit)) ? 1u : 0u;
            progress.work_complete = stopping ? 1u : 0u;
        }
    };
    static inline CDirectPump* direct_pumps[2]{};
    static modules::EExecutionStatus MV_STD_ABI_CALL direct_create(const modules::SExecutionContext* const context,
        void** const value) noexcept
    {
        CDirectPump& state = *direct_pumps[(system_ids::ops::get_module_id(context->address) == module_ids::rendering) ? 0u : 1u];
        state.communication = *context->communication;
        state.address = context->address;
        state.created_on = platform::threading::query_current_thread_id();
        *value = &state;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL direct_handle(void* const value, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        static_cast<CDirectPump*>(value)->handle(*invocation, *progress);
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL direct_destroy(void* const value) noexcept
    {
        CDirectPump& state = *static_cast<CDirectPump*>(value);
        ++state.destroyed;
        state.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
    static void direct_pump_pressure(tests::TTestContext& ctx, const bool close_under_pressure)
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
        SModuleEndpoints& a = module(ctx, runtime, module_ids::rendering, 1400u, &binding);
        SModuleEndpoints& b = module(ctx, runtime, module_ids::audio, 1500u, &binding);
        const auto step = [&]
        {
            runtime.pump_clients();
            runtime.m_direct_connections.progress();
        };
        CDirectPump first, second;
        first.peer = system_ids::ops::make_system_id(module_ids::audio, thread_ids::host);
        second.peer = system_ids::ops::make_system_id(module_ids::rendering, thread_ids::host);
        direct_pumps[0] = &first;
        direct_pumps[1] = &second;
        const modules::SExecutionV1 capability{ sizeof(modules::SExecutionV1), 1u, 0u,
            &direct_create, &direct_handle, &direct_destroy };
        for (SModuleEndpoints* const owned : { &a, &b })
        {
            owned->coordinator->owner_wake = runtime.m_owner_wait.endpoint();
            TEST_EXPECT(ctx, owned->pump.emplace(*owned->coordinator, binding, capability, runtime.m_maintenance));
            TEST_EXPECT(ctx, owned->pump->initialise());
            owned->pump->activate();
        }
        for (unsigned round = 0u; round < 8u; ++round) step();
        TEST_EXPECT(ctx, (first.participated && second.participated));
        first.connect = true;
        a.coordinator->endpoint->signal_consumer();
        for (unsigned round = 0u; round < 16u; ++round) step();
        TEST_EXPECT(ctx, (first.active && second.active && (first.token.value == second.token.value)));
        second.direct_data = false;
        b.coordinator->endpoint->signal_consumer();
        step();
        first.send_limit = k_module_pod_capacity + 1u;
        a.coordinator->endpoint->signal_consumer();
        const std::size_t attempts = allocation.attempts;
        allocation.fail_on = attempts;
        for (unsigned round = 0u; round < 160u; ++round) step();
        allocation.fail_on = SIZE_MAX;
        TEST_EXPECT(ctx, (first.waiting_output && (first.send_next == k_module_pod_capacity) && !second.received &&
            !allocation.failed && (allocation.attempts == attempts) && !runtime.m_emergency_shutdown));
        second.send_limit = 4u;
        b.coordinator->endpoint->signal_consumer();
        TEST_EXPECT(ctx, a.coordinator->post(threading::make_pod_message(42, services::OpenServiceResult{})));
        for (unsigned round = 0u; round < 4u; ++round) step();
        TEST_EXPECT(ctx, (first.waiting_output && !first.received && (first.markers == 1u) && !second.received));
        runtime.m_turn_pending = false;
        step();
        TEST_EXPECT(ctx, (!a.pump->work_pending() && !b.pump->work_pending() && !runtime.m_turn_pending));
        if (close_under_pressure)
        {
            second.close = true;
            b.coordinator->endpoint->signal_consumer();
            for (unsigned round = 0u; round < 16u; ++round) step();
            TEST_EXPECT(ctx, (runtime.m_direct_connections.empty() && !first.waiting_output && !first.received && !second.received));
        }
        else
        {
            second.direct_data = true;
            b.coordinator->endpoint->signal_consumer();
            for (unsigned round = 0u; round < 64u; ++round) step();
            TEST_EXPECT(ctx, (!first.waiting_output && (first.received == 4u) && (second.received == (k_module_pod_capacity + 1u))));
        }
        runtime.stop_module(a, modules::EExecutionStop::requested);
        runtime.stop_module(b, modules::EExecutionStop::requested);
        for (unsigned round = 0u; round < 24u; ++round) step();
        TEST_EXPECT(ctx, (a.pump->stopped() && b.pump->stopped() && runtime.m_direct_connections.empty() &&
            !runtime.m_emergency_shutdown && first.valid && second.valid));
        for (const CDirectPump* const state : { &first, &second })
        {
            TEST_EXPECT(ctx, ((state->destroyed == 1u) && (state->created_on == state->destroyed_on) &&
                (state->created_on == platform::threading::query_current_thread_id())));
        }
        for (SModuleEndpoints* const owned : { &a, &b })
        {
            TEST_EXPECT(ctx, owned->pump->deactivated());
            runtime.drain_client(*owned->coordinator);
            TEST_EXPECT(ctx, owned->coordinator->retire_host_execution(true));
            owned->pump.reset();
            owned->coordinator.reset();
        }
        direct_pumps[0] = direct_pumps[1] = nullptr;
    }
    static void readiness(tests::TTestContext& ctx, const unsigned scenario)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::rendering, 300u, binding);
        CChildExecution probe;
        probe.publish_ready = scenario != 1u;
        probe.hold_cleanup = true;
        // Inject the already accepted creation boundary, then delay only Host's
        // consumption of the child's actual private readiness publication.
        runtime.create_child(*owned.coordinator, { &child_execution, &probe, thread_ids::rendering,
            platform::threading::EThreadPriority::Normal, 1u }, 5);
        SRequestedThread& child = *owned.threads.get_object(owned.threads.first_live());
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            return probe.started.load(std::memory_order_acquire) && ((scenario == 1u) ?
                (child.package->query_state() == threading::EThreadRunState::Waiting) :
                (child.client->channel->lifecycle.to_host.readable_count() != 0u));
        }));
        if (scenario == 2u) owned.state.stopping.store(1u);
        if ((scenario == 3u) || (scenario == 4u)) child.client->request_exit();
        if (scenario == 5u) child.client->seal_pending = true;
        if (scenario == 6u) child.client->provider_closed = true;
        if (scenario == 7u) threading::SThreadPackageTestAccess::preparation(*child.package, 2u);
        if (scenario == 8u) child.client->direct_connections = EDirectConnectionState::closing;
        if (scenario == 3u)
        {
            TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
            {
                return probe.stopped.load(std::memory_order_acquire);
            }));
        }
        SLifecycleMessage message;
        while (child.client->channel->read_host_control(message)) runtime.receive_module_control(*child.client, message);
        const bool ready_observed = (scenario == 0u) || (scenario == 6u) || (scenario == 8u) || (scenario == 9u);
        TEST_EXPECT(ctx, ((child.client->phase == EClientPhase::ready) == ready_observed));
        // The synthetic seal guard tests readiness rejection, not a fabricated
        // destruction grant. Restore it before normal frontier processing.
        if (scenario == 5u) child.client->seal_pending = false;
        if (scenario == 7u) threading::SThreadPackageTestAccess::preparation(*child.package, 1u);
        if (scenario == 9u) child.client->request_exit();
        runtime.drain_client(*child.client);
        runtime.progress_threads(owned);
        unsigned readiness_count = 0u;
        while (owned.coordinator->channel->read_preamble_control(message))
        {
            if (message.operation == ELifecycle::child_ready)
            {
                ++readiness_count;
                TEST_EXPECT(ctx, ((message.notification.first.value == owned.designated_service.value) &&
                    (message.notification.status == services::EServiceStatus::success)));
            }
        }
        TEST_EXPECT(ctx, (readiness_count == ((scenario == 0u) ? 1u : 0u)));
        TEST_EXPECT(ctx, (runtime.m_emergency_shutdown == (scenario == 7u)));
        probe.finish.store(true, std::memory_order_release);
        child.client->request_exit();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return progress_children(runtime, owned); }));
        expect_child_destroyed(ctx, probe);
        runtime.m_phase = CHost::EPhase::complete;
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
        modules::CBoundModule binding;
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::executable, 850u, binding);
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
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::executive, 900u, binding);
        CChildExecution probe;
        probe.hold_cleanup = probe.request_host_stop = true;
        submit_child(ctx, *owned.coordinator, probe, thread_ids::executive, 5, true);
        std::uint32_t controls = 0u;
        TEST_EXPECT(ctx, runtime.drain_module_controls(controls));
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return probe.published.load(std::memory_order_acquire); }));
        SRequestedThread& child = *owned.threads.get_object(owned.threads.first_live());
        TEST_EXPECT(ctx, (child.client->host_stop_requested.load() && !owned.coordinator->host_stop_requested.load()));
        TEST_EXPECT(ctx, ((child.client->channel->to_host.readable_count() == 64u) && !child.client->consumed_pods));
        CHostRestrictions& restrictions = runtime.m_maintenance.restrictions();
        TEST_EXPECT(ctx, restrictions.begin_invocation(owned.state.instance, true));
        const modules::SRestrictionEnterResult scope = restrictions.enter({ 1u });
        TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
        runtime.m_maintenance.poll_stop();
        TEST_EXPECT(ctx, (runtime.m_maintenance.stop_requested() && runtime.m_work_admission_closed));
        TEST_EXPECT(ctx, ((child.client->channel->to_host.readable_count() == 64u) && !child.client->consumed_pods));
        runtime.apply_host_stop();
        TEST_EXPECT(ctx, (owned.state.stopping.load() && !child.client->stop_requested));
        TEST_EXPECT(ctx, ((restrictions.leave(scope.scope) == modules::ERestrictionStatus::success) && restrictions.end_invocation()));
        child.client->request_exit();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            (void)progress_children(runtime, owned);
            return probe.stopped.load(std::memory_order_acquire);
        }));
        TEST_EXPECT(ctx, (!child.package->execution_ended() && !child.client->execution_quiescent()));
        probe.finish.store(true, std::memory_order_release);
        child.client->endpoint->signal_consumer();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return progress_children(runtime, owned); }));
        expect_child_destroyed(ctx, probe);
        TEST_EXPECT(ctx, !runtime.m_emergency_shutdown);
        runtime.m_phase = CHost::EPhase::complete;
    }

    struct SOrderedStop
    {
        struct SChild
        {
            SOrderedStop* owner{ nullptr };
            unsigned index{};
            bool stopping{};
            platform::threading::CPlatformThreadId created_on, destroyed_on;
        };
        modules::CCommunication communication;
        services::SClientToken children[2];
        SChild child[2];
        bool stopping{}, requested[2]{}, ready[2]{}, stop_sent[2]{}, ended[2]{}, retired[2]{};
        std::atomic<bool> a_started{ false }, b_started{ false }, a_cleaned{ false }, b_stopped{ false }, bad_order{ false };
        unsigned destroyed[2]{};
    };
    static inline SOrderedStop* ordered{ nullptr };
    static modules::EExecutionStatus MV_STD_ABI_CALL ordered_child_create(const modules::SExecutionContext* const context,
        void** const value) noexcept
    {
        auto& child = *static_cast<SOrderedStop::SChild*>(context->creation_context);
        child.created_on = platform::threading::query_current_thread_id();
        *value = &child;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL ordered_child_handle(void* const value,
        const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
    {
        auto& child = *static_cast<SOrderedStop::SChild*>(value);
        SOrderedStop& state = *child.owner;
        if (invocation->event == modules::EExecutionEvent::start)
        {
            (child.index ? state.b_started : state.a_started).store(true, std::memory_order_release);
        }
        if (invocation->event == modules::EExecutionEvent::stop)
        {
            child.stopping = true;
            if (child.index == 0u)
            {
                if (!state.b_started.load(std::memory_order_acquire) || state.b_stopped.load(std::memory_order_acquire))
                {
                    state.bad_order.store(true, std::memory_order_release);
                }
                state.a_cleaned.store(true, std::memory_order_release);
            }
            else
            {
                if (!state.a_cleaned.load(std::memory_order_acquire)) state.bad_order.store(true, std::memory_order_release);
                state.b_stopped.store(true, std::memory_order_release);
            }
        }
        *progress = {};
        progress->ready = child.stopping ? 0u : 1u;
        progress->work_complete = child.stopping ? 1u : 0u;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL ordered_child_destroy(void* const value) noexcept
    {
        auto& child = *static_cast<SOrderedStop::SChild*>(value);
        ++child.owner->destroyed[child.index];
        child.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
    static inline const modules::SExecutionV1 ordered_child{ sizeof(modules::SExecutionV1), 1u, 0u,
        &ordered_child_create, &ordered_child_handle, &ordered_child_destroy };
    static modules::EExecutionStatus MV_STD_ABI_CALL ordered_create(const modules::SExecutionContext* const context,
        void** const value) noexcept
    {
        ordered->communication = *context->communication;
        for (unsigned index = 0u; index < 2u; ++index)
        {
            ordered->child[index].owner = ordered;
            ordered->child[index].index = index;
        }
        *value = ordered;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL ordered_handle(void* const value,
        const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
    {
        SOrderedStop& state = *static_cast<SOrderedStop*>(value);
        const modules::SExecutionNotification& notice = invocation->notification;
        switch (invocation->event)
        {
            case modules::EExecutionEvent::stop:
            {
                state.stopping = true;
                break;
            }
            case modules::EExecutionEvent::child_created:
            case modules::EExecutionEvent::child_ready:
            case modules::EExecutionEvent::child_ended:
            case modules::EExecutionEvent::child_retired:
            {
                if ((notice.correlation < 0) || (notice.correlation > 1) || (notice.status != services::EServiceStatus::success))
                {
                    return modules::EExecutionStatus::failed;
                }
                const unsigned index = static_cast<unsigned>(notice.correlation);
                if (invocation->event == modules::EExecutionEvent::child_created) state.children[index] = notice.first;
                if (invocation->event == modules::EExecutionEvent::child_ready) state.ready[index] = true;
                if (invocation->event == modules::EExecutionEvent::child_ended) state.ended[index] = true;
                if (invocation->event == modules::EExecutionEvent::child_retired) state.retired[index] = true;
                break;
            }
            default:
            {
                break;
            }
        }
        for (unsigned index = 0u; index < 2u; ++index)
        {
            if (!state.stopping && !state.requested[index])
            {
                modules::SExecutionIntent request;
                request.kind = modules::SExecutionIntent::EKind::create_child;
                request.correlation = static_cast<std::int32_t>(index);
                request.child = { &ordered_child, &state.child[index], index ? thread_ids::audio : thread_ids::rendering };
                const modules::ESubmission status = state.communication.submit(request);
                state.requested[index] = modules::accepted(status);
                if (!state.requested[index] && (status != modules::ESubmission::busy))
                {
                    return modules::EExecutionStatus::failed;
                }
            }
            if (state.stopping && state.children[index].value && !state.stop_sent[index] && ((index == 0u) || state.ended[0]))
            {
                modules::SExecutionIntent request;
                request.kind = modules::SExecutionIntent::EKind::stop_child;
                request.correlation = static_cast<std::int32_t>(10u + index);
                request.endpoint = state.children[index];
                const modules::ESubmission status = state.communication.submit(request);
                state.stop_sent[index] = modules::accepted(status);
                if (!state.stop_sent[index] && (status != modules::ESubmission::busy))
                {
                    return modules::EExecutionStatus::failed;
                }
            }
        }
        *progress = {};
        progress->ready = (!state.stopping && state.ready[0] && state.ready[1]) ? 1u : 0u;
        progress->work_complete = (state.stopping && state.retired[0] && state.retired[1]) ? 1u : 0u;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL ordered_destroy(void* const value) noexcept
    {
        const SOrderedStop& state = *static_cast<SOrderedStop*>(value);
        return (state.retired[0] && state.retired[1]) ? modules::EExecutionStatus::success : modules::EExecutionStatus::failed;
    }
    static void ordered_shutdown(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, runtime.m_perf_count_conversion.init());
        SModuleEndpoints& owned = module(ctx, runtime, module_ids::rendering, 1000u, &binding);
        owned.coordinator->owner_wake = runtime.m_owner_wait.endpoint();
        SOrderedStop state;
        ordered = &state;
        const modules::SExecutionV1 capability{ sizeof(modules::SExecutionV1), 1u, 0u,
            &ordered_create, &ordered_handle, &ordered_destroy };
        TEST_EXPECT(ctx, owned.pump.emplace(*owned.coordinator, binding, capability, runtime.m_maintenance));
        TEST_EXPECT(ctx, owned.pump->initialise());
        owned.pump->activate();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            runtime.pump_clients();
            return owned.pump->ready() && state.a_started.load() && state.b_started.load();
        }));
        runtime.stop_module(owned, modules::EExecutionStop::requested);
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
        for (unsigned index = 0u; index < 2u; ++index)
        {
            TEST_EXPECT(ctx, ((state.destroyed[index] == 1u) && (state.child[index].created_on == state.child[index].destroyed_on) &&
                (state.child[index].created_on != platform::threading::query_current_thread_id())));
        }
        TEST_EXPECT(ctx, owned.pump->deactivated());
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
        SModuleEndpoints& primary = common_module(ctx, runtime, module_ids::rendering, 700u, binding);
        SModuleEndpoints& peer = common_module(ctx, runtime, module_ids::audio, 800u, binding);
        CChildExecution peer_probe, primary_probe;
        peer_probe.participate = primary_probe.participate = true;
        primary_probe.peer = system_ids::ops::make_system_id(module_ids::audio, thread_ids::audio);
        submit_child(ctx, *peer.coordinator, peer_probe, thread_ids::audio, 5, true);
        runtime.drain_module_inputs();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            (void)progress_children(runtime, peer);
            const SRequestedThread& child = *peer.threads.get_object(peer.threads.first_live());
            return (child.client->phase == EClientPhase::ready) &&
                (child.client->direct_connections == EDirectConnectionState::available);
        }));
        runtime.create_child(*primary.coordinator, { &child_execution, &primary_probe, thread_ids::rendering,
            platform::threading::EThreadPriority::Normal, 1u }, 6);
        SRequestedThread& child = *primary.threads.get_object(primary.threads.first_live());
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            return primary_probe.started.load(std::memory_order_acquire) &&
                (child.package->query_state() == threading::EThreadRunState::Waiting);
        }));
        if (preparation_first) runtime.progress_threads(primary);
        TEST_EXPECT(ctx, ((child.package->preparation() == 1u) && (child.client->phase == EClientPhase::starting)));
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            (void)progress_children(runtime, primary);
            (void)progress_children(runtime, peer);
            runtime.m_direct_connections.progress();
            return primary_probe.active.load(std::memory_order_acquire) && peer_probe.active.load(std::memory_order_acquire);
        }));
        // Common participation/readiness/connect controls never become ordinary
        // domain messages, and creation-outcome timing cannot reject this peer.
        TEST_EXPECT(ctx, ((child.client->phase == EClientPhase::ready) && !child.client->accounted_pods &&
            !runtime.m_direct_connections.client_idle(*child.client)));
        for (SModuleEndpoints* const owned : { &primary, &peer })
        {
            unsigned ready = 0u;
            SLifecycleMessage message;
            while (owned->coordinator->channel->read_preamble_control(message))
            {
                if (message.operation == ELifecycle::child_ready)
                {
                    ++ready;
                    TEST_EXPECT(ctx, (message.notification.first.value == owned->designated_service.value));
                }
            }
            TEST_EXPECT(ctx, (ready == 1u));
            owned->threads.get_object(owned->threads.first_live())->client->request_exit();
        }
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            (void)progress_children(runtime, primary);
            (void)progress_children(runtime, peer);
            runtime.m_direct_connections.progress();
            return primary.threads.is_empty() && peer.threads.is_empty();
        }));
        expect_child_destroyed(ctx, primary_probe);
        expect_child_destroyed(ctx, peer_probe);
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
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::rendering, 500u, binding);
        SModuleEndpoints& other = common_module(ctx, runtime, module_ids::audio, 600u, binding);
        services::SClientToken previous;
        for (unsigned generation = 0u; generation < 2u; ++generation)
        {
            CChildExecution probe;
            submit_child(ctx, *owned.coordinator, probe, thread_ids::rendering, 5, true);
            runtime.drain_module_inputs();
            TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
            {
                (void)progress_children(runtime, owned);
                return probe.started.load(std::memory_order_acquire);
            }));
            SRequestedThread& child = *owned.threads.get_object(owned.threads.first_live());
            const services::SClientToken identity = child.identity;
            SLifecycleMessage message;
            while (owned.coordinator->channel->read_preamble_control(message)) {}
            if (generation)
            {
                TEST_EXPECT(ctx, (identity.value != previous.value));
                const auto stop = [&](SClientRecord& requester, const services::SClientToken target,
                    const std::int32_t correlation, const services::EServiceStatus expected)
                {
                    SLifecycleMessage request;
                    request.operation = ELifecycle::stop_child;
                    request.correlation = correlation;
                    request.notification.first = target;
                    TEST_EXPECT(ctx, requester.channel->preamble_control(request));
                    runtime.drain_module_inputs();
                    TEST_EXPECT(ctx, (requester.channel->read_preamble_control(message) &&
                        (message.operation == ELifecycle::child_stop_result) && (message.correlation == correlation) &&
                        (message.notification.first.value == target.value) && (message.notification.status == expected)));
                };
                stop(*owned.coordinator, previous, 10, services::EServiceStatus::stale);
                TEST_EXPECT(ctx, !child.client->stop_requested);
                stop(*other.coordinator, identity, 11, services::EServiceStatus::invalid_request);
                TEST_EXPECT(ctx, !child.client->stop_requested);
                stop(*owned.coordinator, identity, 12, services::EServiceStatus::success);
                TEST_EXPECT(ctx, child.client->stop_requested);
            }
            else child.client->request_exit();
            previous = identity;
            TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return progress_children(runtime, owned); }));
            expect_child_destroyed(ctx, probe);
            while (owned.coordinator->channel->read_preamble_control(message)) {}
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
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::rendering, 400u, binding);
        SLifecycleMessage invalid;
        invalid.operation = ELifecycle::create_child;
        invalid.correlation = 1;
        TEST_EXPECT(ctx, owned.coordinator->channel->preamble_control(invalid));
        CChildExecution state;
        submit_child(ctx, *owned.coordinator, state, thread_ids::rendering, 2);
        runtime.drain_module_inputs();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return progress_children(runtime, owned); }));
        unsigned outcomes = 0u;
        SLifecycleMessage message;
        while (owned.coordinator->channel->read_preamble_control(message))
        {
            ++outcomes;
            TEST_EXPECT(ctx, ((message.operation == ELifecycle::child_created) && !message.notification.first.value &&
                (message.notification.status == ((message.correlation == 1) ?
                    services::EServiceStatus::invalid_request : services::EServiceStatus::native_failure))));
        }
        TEST_EXPECT(ctx, ((outcomes == 2u) && owned.coordinator->replies_idle() && runtime.m_emergency_shutdown));
        TEST_EXPECT(ctx, (!state.created_on.is_valid() && !state.destroyed && !state.destroyed_on.is_valid()));
        runtime.m_phase = CHost::EPhase::complete;
    }
    static void seal_lanes(tests::TTestContext& ctx, const bool owner_first)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::rendering, 200u, binding);
        SClientRecord& client = *owned.coordinator;
        auto request = threading::make_owner_message<modules::ModuleRequest>(3);
        TEST_EXPECT(ctx, submit_owner(client, std::move(request)));
        TEST_EXPECT(ctx, modules::accepted(tests::submit_common_pod(client,
            threading::make_pod_message(4, services::OpenServiceRequest{}))));
        SLifecycleMessage seal;
        seal.operation = ELifecycle::seal;
        seal.frontier = { 1u, 1u, 0u };
        TEST_EXPECT(ctx, client.channel->preamble_control(seal));
        auto settlement = module_request(module_ids::audio, modules::EModuleAction::unload, 5);
        TEST_EXPECT(ctx, submit_owner(client, std::move(settlement)));
        auto& maintenance = runtime.m_maintenance;
        maintenance.m_lane = owner_first ? 2u : 0u;
        client.invocation_active = true;
        TEST_EXPECT(ctx, maintenance.restrictions().begin_invocation(client.instance, true));
        const auto scope = maintenance.restrictions().enter({ 0u });
        for (unsigned count = 0u; count < 16u; ++count) (void)maintenance.run();
        TEST_EXPECT(ctx, ((client.consumed_pods == 1u) && (client.consumed_owners == 1u) &&
            client.pod_head_live && client.owner_head_live && !client.accounted_pods && !client.accounted_owners &&
            !client.reserved_replies && !client.pending_replies->reservations()));
        TEST_EXPECT(ctx, (!client.seal_acknowledged && !client.seal_pending));
        TEST_EXPECT(ctx, (maintenance.restrictions().leave(scope.scope) == modules::ERestrictionStatus::success));
        TEST_EXPECT(ctx, maintenance.restrictions().end_invocation());
        client.invocation_active = false;
        runtime.drain_module_inputs();
        TEST_EXPECT(ctx, (client.seal_pending && client.seal_acknowledged && (client.accounted_pods == 1u) &&
            (client.accounted_owners == 1u) && (client.seal_boundary.owners == 1u) && client.owner_head_live));
        runtime.drain_module_inputs();
        TEST_EXPECT(ctx, ((client.accounted_owners == 2u) && !client.owner_head_live && client.replies_idle()));
        threading::CErasedPodMsg message;
        while (client.channel->read_preamble_pod(message)) {}
        SLifecycleMessage outcome;
        TEST_EXPECT(ctx, (client.channel->read_preamble_control(outcome) && (outcome.operation == ELifecycle::sealed) &&
            !client.channel->read_preamble_control(outcome)));
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

    static void failed_preparation_join(tests::TTestContext& ctx)
    {
        platform::system::CPerfCountConversion conversion;
        TEST_EXPECT(ctx, conversion.init());
        CHostRestrictions restriction;
        const threading::ThreadConfig config{ thread_ids::rendering, module_ids::rendering,
            platform::threading::EThreadPriority::Normal };
        threading::CThreadPackage package{ config, conversion };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding, true);
        platform::threading::COwnerWait wake;
        TEST_EXPECT(ctx, (wake.initialise().code == platform::threading::EWaitError::none));
        SClientRecord client{ module_ids::rendering, 75u, &package, { 74u } };
        client.owner_wake = wake.endpoint();
        SOrderedStop::SChild state;
        TEST_EXPECT(ctx, client.initialise_execution(binding, ordered_child, &state));
        TEST_EXPECT(ctx, client.install_host_execution(restriction.reader()));
        TEST_EXPECT(ctx, package.startup());
        TEST_EXPECT(ctx, tests::wait_until(conversion, [&] { return package.execution_ended(); }));
        TEST_EXPECT(ctx, ((package.preparation() == 2u) && !state.created_on.is_valid() && !state.destroyed_on.is_valid()));
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
        modules::CBoundModule binding;
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::executive, 10u, binding);
        SClientRecord& client = *owned.coordinator;
        CHostMaintenance& maintenance = runtime.m_maintenance;
        TEST_EXPECT(ctx, client.install_host_execution(maintenance.restrictions().reader()));
        TEST_EXPECT(ctx, maintenance.restrictions().begin_invocation(client.instance, true));
        const modules::SRestrictionEnterResult scope = maintenance.restrictions().enter({ 0u });
        TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
        const modules::ModuleRequest* retained = nullptr;
        for (unsigned position = 0u; position < 32u; ++position)
        {
            auto request = module_request(module_ids::audio, modules::EModuleAction::load,
                static_cast<std::int32_t>(position));
            if (!position) retained = request.owner().payload<modules::ModuleRequest>();
            TEST_EXPECT(ctx, submit_owner(client, std::move(request)));
        }
        client.phase = EClientPhase::stopped;
        client.invocation_active = true;
        const memory::CMemoryContext* const ambient = memory::get_ambient_memory_context();
        const std::uint64_t allocations = ambient->get_live_allocation_count();
        for (unsigned pass = 0u; pass < 16u; ++pass)
        {
            TEST_EXPECT(ctx, (maintenance.run().operations <= 32u));
        }
        TEST_EXPECT(ctx, (client.owner_head_live && (client.consumed_owners == 1u) && !client.accounted_owners &&
            (client.owner_head.owner().payload<modules::ModuleRequest>() == retained) &&
            !client.reserved_replies && !client.pending_replies->reservations()));
        TEST_EXPECT(ctx, (ambient->get_live_allocation_count() == allocations));
        // Even an incorrect caller claim of empty queues cannot bypass the head
        // or the active native invocation. Classification did not dispatch it.
        client.terminal_before_drain = client.incoming_drained = true;
        TEST_EXPECT(ctx, (!client.execution_quiescent() && !client.retire_host_execution(true)));
        TEST_EXPECT(ctx, client.host_execution.stop.request());
        const modules::SHostMaintenanceResult stopping = maintenance.run();
        TEST_EXPECT(ctx, (stopping.shutdown_requested && stopping.cancel_native && runtime.m_work_admission_closed));
        TEST_EXPECT(ctx, (runtime.m_shutdown_deadline.set_after_milliseconds(0u).code == platform::threading::EWaitError::none));
        (void)maintenance.run();
        TEST_EXPECT(ctx, (runtime.m_runtime_failed && client.subscription_live && client.owner_head_live &&
            (client.owner_head.owner().payload<modules::ModuleRequest>() == retained)));
        TEST_EXPECT(ctx, (maintenance.restrictions().leave(scope.scope) == modules::ERestrictionStatus::success));
        TEST_EXPECT(ctx, maintenance.restrictions().end_invocation());
        client.invocation_active = false;
        // The normal Host rejection path accounts every accepted owner only
        // after returning from native code, then releases the notification pin.
        for (unsigned pass = 0u; pass < 32u; ++pass) runtime.drain_module_inputs();
        runtime.drain_client(client);
        TEST_EXPECT(ctx, ((client.accounted_owners == 32u) && !client.owner_head_live &&
            client.replies_idle() && client.execution_quiescent() && client.retire_host_execution(true)));
        TEST_EXPECT(ctx, (!client.endpoint->notification_pins &&
            (client.endpoint->failure.load() == threading::EEndpointFailure::none)));
        runtime.m_retain_runtime = false; // Test proved and performed all deferred retirement.
        TEST_EXPECT(ctx, runtime.shutdown());
    }

    static void retained_capture_pressure(tests::TTestContext& ctx)
    {
        for (const bool owner : { false, true })
        {
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            modules::CBoundModule binding;
            CHost runtime{ allocator };
            runtime.m_phase = CHost::EPhase::complete;
            SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::executive, 10u, binding);
            SClientRecord& client = *owned.coordinator;
            CHostMaintenance& maintenance = runtime.m_maintenance;
            TEST_EXPECT(ctx, client.install_host_execution(maintenance.restrictions().reader()));
            TEST_EXPECT(ctx, maintenance.restrictions().begin_invocation(client.instance, true));
            const modules::SRestrictionEnterResult scope = maintenance.restrictions().enter({ 0u });
            TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
            client.invocation_active = true;
            const auto submit = [&](const std::int32_t slot)
            {
                if (owner)
                {
                    TEST_EXPECT(ctx, submit_owner(client, module_request(module_ids::audio, modules::EModuleAction::load, slot)));
                }
                else
                {
                    TEST_EXPECT(ctx, (client.channel->to_host.post(threading::make_pod_message(slot, AssetReleaseRequest{})) == EModulePipePost::published));
                    ++client.endpoint->published_pods;
                }
            };
            submit(11);
            for (unsigned pass = 0u; pass < 8u; ++pass)
            {
                const modules::SHostMaintenanceResult result = maintenance.run();
                TEST_EXPECT(ctx, ((result.status == modules::EMaintenanceStatus::success) && !result.capacity_pressure && (result.operations <= 32u)));
            }
            const modules::ModuleRequest* const retained = owner ? client.owner_head.owner().payload<modules::ModuleRequest>() : nullptr;
            TEST_EXPECT(ctx, (owner ? (client.owner_head_live && (client.owner_head.query_async_slot() == 11)) :
                (client.pod_head_live && (client.pod_head.query_async_slot() == 11))));
            submit(12);
            bool pressure = false;
            for (unsigned pass = 0u; pass < 8u; ++pass)
            {
                const modules::SHostMaintenanceResult result = maintenance.run();
                pressure |= result.capacity_pressure != 0u;
                TEST_EXPECT(ctx, ((result.status == modules::EMaintenanceStatus::success) && (result.operations <= 32u)));
            }
            TEST_EXPECT(ctx, (pressure && !client.accounted_pods && !client.accounted_owners && !client.reserved_replies &&
                !client.pending_replies->reservations()));
            TEST_EXPECT(ctx, (owner ? ((client.consumed_owners == 1u) &&
                (client.owner_head.owner().payload<modules::ModuleRequest>() == retained) &&
                (client.endpoint->worker_to_host_owned_msgs.readable_count() == 1u)) :
                ((client.consumed_pods == 1u) && (client.pod_head.query_async_slot() == 11) && (client.channel->to_host.readable_count() == 1u))));
            // Controlled capture of the queued message into test-owned storage,
            // without dispatch/accounting, isolates pressure reset with the head
            // still retained. Both records are cancelled after native unwind.
            threading::CErasedOwnerMsg captured_owner;
            threading::CErasedPodMsg captured_pod;
            if (owner) TEST_EXPECT(ctx, (client.capture(captured_owner) && (captured_owner.query_async_slot() == 12)));
            else TEST_EXPECT(ctx, (client.read(captured_pod) && (captured_pod.query_async_slot() == 12)));
            for (unsigned pass = 0u; pass < 8u; ++pass)
            {
                TEST_EXPECT(ctx, !maintenance.run().capacity_pressure);
            }
            TEST_EXPECT(ctx, (!client.accounted_pods && !client.accounted_owners && !client.execution_quiescent()));
            TEST_EXPECT(ctx, (maintenance.restrictions().leave(scope.scope) == modules::ERestrictionStatus::success));
            TEST_EXPECT(ctx, maintenance.restrictions().end_invocation());
            client.invocation_active = false;
            client.phase = EClientPhase::stopped;
            if (owner)
            {
                client.owner_head = {};
                captured_owner = {};
                client.owner_head_live = false;
                client.accounted_owner();
                client.accounted_owner();
            }
            else
            {
                client.pod_head = {};
                captured_pod = {};
                client.pod_head_live = false;
                client.accounted_pod();
                client.accounted_pod();
            }
            runtime.drain_client(client);
            TEST_EXPECT(ctx, (client.execution_quiescent() && client.retire_host_execution(true) && client.replies_idle() &&
                !client.endpoint->notification_pins && (client.endpoint->failure.load() == threading::EEndpointFailure::none)));
            TEST_EXPECT(ctx, runtime.shutdown());
        }
    }

    class CSubscriber
    {
    public:
        std::atomic<unsigned> ready{ 0u }, restricted{ 0u }, resumed{ 0u }, failed{ 0u };
        std::atomic<unsigned> restricted_observations{ 0u };
        std::atomic<unsigned> fanout_complete{ 0u }, quiet_wait{ 0u };
        const platform::system::CPerfCountConversion* conversion{ nullptr };
        platform::threading::CPlatformThreadId created_on, destroyed_on;
        unsigned destroyed{};

        modules::EExecutionStatus initialise(const modules::SExecutionContext& context) noexcept
        {
            m_communication = *context.communication;
            m_execution = context.execution;
            created_on = platform::threading::query_current_thread_id();
            if (!m_execution || !m_execution->notification.pending())
            {
                failed.store(1u, std::memory_order_release);
                return modules::EExecutionStatus::failed;
            }
            ready.store(1u, std::memory_order_release);
            return modules::EExecutionStatus::success;
        }
        modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
        {
            progress = {};
            if (invocation.event == modules::EExecutionEvent::stop) m_stopping = true;
            progress.ready = m_stopping ? 0u : 1u;
            progress.work_complete = m_stopping ? 1u : 0u;
            if (m_stopping || (invocation.event != modules::EExecutionEvent::service_async))
            {
                return modules::EExecutionStatus::success;
            }
            modules::SHostRestrictionSnapshot snapshot;
            const modules::ERestrictionRead read = m_execution->restriction.read(snapshot);
            if (read == modules::ERestrictionRead::failed)
            {
                failed.store(1u, std::memory_order_release);
                return modules::EExecutionStatus::failed;
            }
            if (read == modules::ERestrictionRead::retry)
            {
                progress.next_wake_ns = 1000000u;
                return modules::EExecutionStatus::success;
            }
            if (snapshot.depth)
            {
                const bool already_signalled = fanout_complete.load(std::memory_order_acquire) != 0u;
                restricted_observations.fetch_add(1u);
                restricted.store(1u, std::memory_order_release);
                // The test deliberately signals between the serviced driver's
                // epoch observation and its final work check. This callback must
                // return before any second invocation; it does not dispatch.
                if (!already_signalled &&
                    !tests::wait_until(*conversion, [&] { return fanout_complete.load(std::memory_order_acquire) != 0u; }))
                {
                    failed.store(1u, std::memory_order_release);
                    return modules::EExecutionStatus::failed;
                }
                if (already_signalled) quiet_wait.store(1u, std::memory_order_release);
            }
            else if (restricted.load(std::memory_order_acquire))
            {
                resumed.store(1u, std::memory_order_release);
            }
            return modules::EExecutionStatus::success;
        }
    private:
        modules::CCommunication m_communication;
        const modules::SHostExecutionContext* m_execution{ nullptr };
        bool m_stopping{};
    };

    static modules::EExecutionStatus MV_STD_ABI_CALL subscriber_create(const modules::SExecutionContext* const context,
        void** const value) noexcept
    {
        CSubscriber& state = *static_cast<CSubscriber*>(context->creation_context);
        *value = &state;
        return state.initialise(*context);
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL subscriber_handle(void* const value,
        const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
    {
        return static_cast<CSubscriber*>(value)->handle(*invocation, *progress);
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL subscriber_destroy(void* const value) noexcept
    {
        CSubscriber& state = *static_cast<CSubscriber*>(value);
        ++state.destroyed;
        state.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
    static void installed_subscription(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::prepare(binding);
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, (runtime.m_perf_count_conversion.init() && runtime.m_thread_packages.initialise()));
        CSubscriber state;
        state.conversion = &runtime.m_perf_count_conversion;
        const threading::ThreadConfig config{ thread_ids::executive, module_ids::executive,
            platform::threading::EThreadPriority::Normal };
        SModuleEndpoints& owned = common_module(ctx, runtime, module_ids::executive, 28u, binding);
        const std::int32_t slot = owned.threads.emplace();
        SRequestedThread& attempt = *owned.threads.get_object(slot);
        TEST_EXPECT(ctx, attempt.package.emplace(config, runtime.m_perf_count_conversion));
        auto* const package = &*attempt.package;
        TEST_EXPECT(ctx, attempt.client.emplace(module_ids::executive, 30u, package, owned.state.instance));
        auto& client = *attempt.client;
        client.owner_wake = runtime.m_owner_wait.endpoint();
        const modules::SExecutionV1 capability{ sizeof(modules::SExecutionV1), 1u, 0u,
            &subscriber_create, &subscriber_handle, &subscriber_destroy };
        TEST_EXPECT(ctx, client.initialise_execution(binding, capability, &state));
        TEST_EXPECT(ctx, (client.install_host_execution(runtime.m_maintenance.restrictions().reader()) && package->startup()));
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.ready.load(std::memory_order_acquire) != 0u; }));
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            runtime.drain_module_inputs();
            return package->query_state() == threading::EThreadRunState::Waiting;
        }));
        auto& restriction = runtime.m_maintenance.restrictions();
        TEST_EXPECT(ctx, restriction.begin_invocation({ 30u }, true));
        const auto scope = restriction.enter({ 5u });
        TEST_EXPECT(ctx, (scope.status == modules::ERestrictionStatus::success));
        runtime.m_maintenance.fanout();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.restricted.load(std::memory_order_acquire) != 0u; }));
        TEST_EXPECT(ctx, client.restriction_notification.endpoint().signal());
        state.fanout_complete.store(1u, std::memory_order_release);
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            return (state.quiet_wait.load(std::memory_order_acquire) != 0u) &&
                (package->query_state() == threading::EThreadRunState::Waiting);
        }));
        const auto observations = state.restricted_observations.load();
        TEST_EXPECT(ctx, (observations >= 2u));
        std::atomic<std::uint32_t> unchanged{ 0u };
        threading::CWaitDeadline pause;
        TEST_EXPECT(ctx, (pause.set_after_milliseconds(20u).code == platform::threading::EWaitError::none));
        TEST_EXPECT(ctx, (platform::threading::wait_until_not_equal(unchanged, 0u, pause).status == threading::EWaitStatus::timed_out));
        TEST_EXPECT(ctx, (state.restricted_observations.load() == observations));
        TEST_EXPECT(ctx, ((restriction.leave(scope.scope) == modules::ERestrictionStatus::success) && restriction.end_invocation()));
        runtime.m_maintenance.fanout();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&] { return state.resumed.load(std::memory_order_acquire) != 0u; }));
        client.request_exit();
        TEST_EXPECT(ctx, tests::wait_until(runtime.m_perf_count_conversion, [&]
        {
            runtime.drain_module_inputs();
            return package->execution_ended();
        }));
        client.terminal_before_drain = client.incoming_drained = true;
        TEST_EXPECT(ctx, !package->shutdown());
        threading::SThreadPackageTestAccess::fail_join(*package);
        TEST_EXPECT(ctx, (!client.retire_host_execution(false) && client.subscription_live &&
            threading::SThreadPackageTestAccess::retained(*package)));
        threading::SThreadPackageTestAccess::fail_join(*package, false);
        TEST_EXPECT(ctx, (client.retire_host_execution(false) && package->shutdown() && !state.failed.load()));
        TEST_EXPECT(ctx, ((state.destroyed == 1u) && (state.created_on == state.destroyed_on) &&
            (state.created_on != platform::threading::query_current_thread_id())));
        attempt.client.reset();
        attempt.package.reset();
        TEST_EXPECT(ctx, owned.threads.erase(slot));
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
    host::SHostMaintenanceTestAccess::retained_capture_pressure(ctx);
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
    host::SHostMaintenanceTestAccess::ready_fifo_request(ctx, false);
    host::SHostMaintenanceTestAccess::ready_fifo_request(ctx, true);
    host::SHostMaintenanceTestAccess::stop_identity(ctx);
    host::SHostMaintenanceTestAccess::rejected_creation(ctx);
    for (unsigned scenario = 0u; scenario < 10u; ++scenario) host::SHostMaintenanceTestAccess::readiness(ctx, scenario);
}

int run_ordered_execution_focused()
{
    tests::TTestContext ctx;
    host::SHostMaintenanceTestAccess::ordered_shutdown(ctx);
    return ctx.report("OrderedExecution");
}

int run_maintenance_pressure_focused()
{
    tests::TTestContext ctx;
    host::SHostMaintenanceTestAccess::retained_capture_pressure(ctx);
    return ctx.report("MaintenancePressure");
}

int run_host_maintenance_focused()
{
    tests::TTestContext ctx;
    host::SHostMaintenanceTestAccess::startup_module_requests(ctx);
    host::SHostMaintenanceTestAccess::early_self_change(ctx);
    host::SHostMaintenanceTestAccess::startup_rollback(ctx);
    host::SHostMaintenanceTestAccess::failed_preparation_join(ctx);
    host::SHostMaintenanceTestAccess::bounds(ctx);
    host::SHostMaintenanceTestAccess::deferred_retirement(ctx);
    host::SHostMaintenanceTestAccess::installed_subscription(ctx);
    host::SHostMaintenanceTestAccess::thread_outcomes(ctx, false);
    host::SHostMaintenanceTestAccess::thread_outcomes(ctx, true);
    host::SHostMaintenanceTestAccess::stop_identity(ctx);
    host::SHostMaintenanceTestAccess::rejected_creation(ctx);
    host::SHostMaintenanceTestAccess::child_host_stop(ctx);
    host::SHostMaintenanceTestAccess::direct_pump_pressure(ctx, false);
    host::SHostMaintenanceTestAccess::direct_pump_pressure(ctx, true);
    host::SHostMaintenanceTestAccess::ready_fifo_request(ctx, false);
    host::SHostMaintenanceTestAccess::ready_fifo_request(ctx, true);
    for (unsigned scenario = 0u; scenario < 10u; ++scenario) host::SHostMaintenanceTestAccess::readiness(ctx, scenario);
    host::SHostMaintenanceTestAccess::seal_lanes(ctx, false);
    host::SHostMaintenanceTestAccess::seal_lanes(ctx, true);
    host::SHostMaintenanceTestAccess::terminal_batch(ctx, false);
    host::SHostMaintenanceTestAccess::terminal_batch(ctx, true);
    return ctx.report("HostMaintenance");
}
