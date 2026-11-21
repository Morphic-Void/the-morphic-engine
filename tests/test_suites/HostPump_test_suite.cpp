
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostPump_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    02 Nov 26
//
//  FIFO routing, exact requester lifetime, retained descriptors and reply accounting.

#include "framework/system/system_ids.hpp"
#include "tests/support/endpoint_fixture.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/module/service_thread_pump.hpp"
#include "framework/services/provider_client.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "host/runtime/service_router.hpp"
#include "tests/test_suites/HostPump_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "framework/threading/messages/message_helpers.hpp"

namespace modules
{
struct SServiceThreadPumpTestAccess
{
    template<typename TPump>
    static void* install(threading::CEndpointContext& endpoint) noexcept
    {
        TPump::state() = {};
        TPump::state().m_context = &endpoint;
        return &TPump::state();
    }
    template<typename TPump>
    static void clear() noexcept { TPump::state() = {}; }
};
}

namespace
{

using namespace services;
using tests::TTestContext;

static_assert(threading::CErasedPodMsg::is_payload_compatible_with<ServiceRequest>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<ServiceReply>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<OpenServiceRequest>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<OpenServiceResult>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<CloseServiceRequest>());
static_assert(offsetof(ServiceRequest, payload) == 24u);
static_assert(offsetof(ServiceReply, payload) == 24u);

struct SFixture
{
    tests::SEndpointFixture client{ module_ids::executive, 1u };
    tests::SEndpointFixture replacement{ module_ids::executive, 2u };
    tests::SEndpointFixture provider{ module_ids::rendering, 3u };
    host::CServiceRouter router;
    SServiceContract contract{ 1u, 1u };
    host::SServiceProvider advertisement;
    SServiceRoute route;

    explicit SFixture(TTestContext& ctx)
    {
        client.phase = replacement.phase = provider.phase = host::EClientPhase::ready;
        advertisement = { &provider, &contract, 1u };
        TEST_EXPECT(ctx, client.reserve_replies(1u));
        router.open({ module_ids::rendering, 1u, 1u, { 1u } }, 10, client, &advertisement);
        OpenServiceResult result;
        TEST_EXPECT(ctx, ((client.pending() == 1u) && client.received(0).copy_payload_to(result)));
        TEST_EXPECT(ctx, ((result.status == EServiceStatus::success) && (result.provider.value == provider.instance.value)));
        route = result.route;
        client.discard(client.pending());
    }
    void submit(TTestContext& ctx, const std::uint64_t id)
    {
        TEST_EXPECT(ctx, client.reserve_replies(2u));
        ServiceRequest request;
        request.route = route;
        request.request.value = id;
        request.opcode = 1u;
        router.request(request, static_cast<std::int32_t>(id), client);
    }
    ServiceReply completion(const std::uint64_t id)
    {
        ServiceReply reply;
        reply.route = route;
        reply.request.value = id;
        reply.opcode = 1u;
        reply.status = EServiceStatus::success;
        return reply;
    }
};

std::uint32_t MV_STD_ABI_CALL unused_child(threading::CThreadContext&, void* const) noexcept { return 0u; }

void coordinator_readiness(TTestContext& ctx)
{
    using Pump = modules::TServiceThreadPump<&unused_child, &thread_ids::executive>;
    const modules::SHostPumpV1 capability = Pump::capability();
    for (unsigned scenario = 0u; scenario < 6u; ++scenario)
    {
        tests::SEndpointFixture client{ module_ids::executive, 71u };
        threading::SModuleEndpointState module;
        client.endpoint->module_state = &module;
        threading::CEndpointContext endpoint{ *client.endpoint };
        void* const runtime = modules::SServiceThreadPumpTestAccess::install<Pump>(endpoint);
        modules::SPumpInput input;
        modules::SPumpResult result;
        auto pump = [&] { return capability.pump(runtime, &input, &result); };
        TEST_EXPECT(ctx, (pump() == modules::ECallStatus::success));
        threading::CErasedPodMsg message;
        TEST_EXPECT(ctx, (client.endpoint->worker_to_host_msgs.read(message) && message.is_payload_a<threading::CreateThreadRequest>()));
        const threading::CreateThreadResult created{ { 81u }, EServiceStatus::success };
        const threading::EndpointReady ready{ { 81u } };
        TEST_EXPECT(ctx, client.post((scenario == 0u) ? threading::make_pod_message(-1, ready) : threading::make_pod_message(0, created)));
        TEST_EXPECT(ctx, ((pump() == modules::ECallStatus::success) && (result.progress.phase == modules::EPumpPhase::starting)));
        if (scenario == 2u)
        {
            TEST_EXPECT(ctx, client.post(threading::make_pod_message(-1, threading::EndpointReady{ { 82u } })));
            TEST_EXPECT(ctx, ((pump() == modules::ECallStatus::success) && (result.progress.phase == modules::EPumpPhase::starting)));
        }
        if (scenario == 3u)
        {
            TEST_EXPECT(ctx, client.post(threading::make_pod_message(0,
                threading::CreateThreadResult{ {}, EServiceStatus::native_failure })));
            TEST_EXPECT(ctx, (pump() == modules::ECallStatus::failed));
        }
        else
        {
            if (scenario == 4u)
            {
                threading::EndpointLifecycle ended;
                ended.requester = client.identity;
                ended.endpoint = created.endpoint;
                ended.correlation = 0;
                ended.event = threading::EEndpointLifecycle::execution_ended;
                TEST_EXPECT(ctx, client.post(threading::make_pod_message(-1, ended)));
            }
            if (scenario == 5u) module.stopping.store(1u);
            TEST_EXPECT(ctx, client.post((scenario == 0u) ? threading::make_pod_message(0, created) : threading::make_pod_message(-1, ready)));
            const modules::EPumpPhase expected = (scenario >= 4u) ? modules::EPumpPhase::stopping : modules::EPumpPhase::ready;
            TEST_EXPECT(ctx, ((pump() == modules::ECallStatus::success) && (result.progress.phase == expected)));
        }
        modules::SServiceThreadPumpTestAccess::clear<Pump>();
    }
}

void coordinator_stopping_creation(TTestContext& ctx)
{
    using Pump = modules::TServiceThreadPump<&unused_child, &thread_ids::executive>;
    const modules::SHostPumpV1 capability = Pump::capability();
    for (const bool failed : { false, true })
    {
        tests::SEndpointFixture client{ module_ids::executive, 72u };
        threading::CEndpointContext endpoint{ *client.endpoint };
        void* const runtime = modules::SServiceThreadPumpTestAccess::install<Pump>(endpoint);
        modules::SPumpInput input;
        modules::SPumpResult result;
        TEST_EXPECT(ctx, (capability.pump(runtime, &input, &result) == modules::ECallStatus::success));
        threading::CErasedPodMsg message;
        TEST_EXPECT(ctx, (client.endpoint->worker_to_host_msgs.read(message) && message.is_payload_a<threading::CreateThreadRequest>()));
        modules::SProgress progress;
        TEST_EXPECT(ctx, (capability.begin_stop(runtime, modules::EStopReason::shutdown, &progress) == modules::ECallStatus::success));
        TEST_EXPECT(ctx, ((capability.pump(runtime, &input, &result) == modules::ECallStatus::success) &&
            (result.progress.phase == modules::EPumpPhase::stopping) && !client.endpoint->worker_to_host_msgs.read(message)));
        const threading::CreateThreadResult created{ { failed ? 0u : 82u },
            failed ? EServiceStatus::native_failure : EServiceStatus::success };
        TEST_EXPECT(ctx, client.post(threading::make_pod_message(0, created)));
        TEST_EXPECT(ctx, (capability.pump(runtime, &input, &result) == modules::ECallStatus::success));
        if (!failed)
        {
            threading::StopThreadRequest stop;
            TEST_EXPECT(ctx, (client.endpoint->worker_to_host_msgs.read(message) && message.copy_payload_to(stop) &&
                (stop.endpoint.value == created.endpoint.value)));
            TEST_EXPECT(ctx, !client.endpoint->worker_to_host_msgs.read(message));
            threading::EndpointLifecycle lifecycle;
            lifecycle.requester = client.identity;
            lifecycle.endpoint = created.endpoint;
            lifecycle.correlation = 0;
            lifecycle.event = threading::EEndpointLifecycle::execution_ended;
            TEST_EXPECT(ctx, client.post(threading::make_pod_message(-1, lifecycle)));
            lifecycle.event = threading::EEndpointLifecycle::retired;
            TEST_EXPECT(ctx, client.post(threading::make_pod_message(-1, lifecycle)));
            TEST_EXPECT(ctx, (capability.pump(runtime, &input, &result) == modules::ECallStatus::success));
        }
        TEST_EXPECT(ctx, (client.endpoint->worker_to_host_msgs.read(message) && message.is_payload_a<threading::CloseProviderAdmission>()));
        TEST_EXPECT(ctx, client.post(threading::make_pod_message(1, threading::ProviderAdmissionClosed{})));
        TEST_EXPECT(ctx, ((capability.pump(runtime, &input, &result) == modules::ECallStatus::success) &&
            (result.progress.phase == modules::EPumpPhase::stopping)));
        TEST_EXPECT(ctx, (client.endpoint->worker_to_host_msgs.read(message) && message.is_payload_a<threading::SealEndpoint>()));
        TEST_EXPECT(ctx, client.post(threading::make_pod_message(-1, threading::EndpointSealed{})));
        TEST_EXPECT(ctx, ((capability.pump(runtime, &input, &result) == modules::ECallStatus::success) &&
            (result.progress.phase == modules::EPumpPhase::stopped)));
        modules::SServiceThreadPumpTestAccess::clear<Pump>();
    }
}

void uncertain_provider_request(TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, tests::allocate_with_failure, tests::deallocate_test_memory, system_ids::host };
    memory::CMemoryContext memory{ allocator, system_ids::host };
    tests::TMemoryContextScope scope{ &memory };
    tests::SEndpointFixture client{ module_ids::rendering, 91u };
    threading::CEndpointContext endpoint{ *client.endpoint };
    services::CProviderClient provider{ endpoint };
    const threading::CErasedPodMsg filler = threading::make_pod_message(-1, threading::EndpointReady{});
    do { TEST_EXPECT(ctx, endpoint.post(filler)); }
    while (!client.endpoint->worker_to_host_msgs.post_would_reallocate(1u));
    allocation.fail_on = allocation.attempts + 1u;
    SServiceBorrowOffer offer;
    offer.route = { 1u };
    offer.request = { 2u };
    SServiceBorrow borrow;
    TEST_EXPECT(ctx, (provider.prepare_borrow(&offer, &borrow) == EServiceStatus::busy));
    TEST_EXPECT(ctx, (allocation.failed && endpoint.pod_submission_poisoned() && !provider.idle() && provider.preparing(offer)));
    threading::CErasedPodMsg message;
    unsigned exposed = 0u;
    std::int32_t slot = -1;
    while (client.endpoint->worker_to_host_msgs.read(message))
    {
        if (message.is_payload_a<PrepareServiceBorrow>())
        {
            ++exposed;
            slot = message.query_async_slot();
        }
    }
    TEST_EXPECT(ctx, (exposed == 1u));
    TEST_EXPECT(ctx, (provider.prepare_borrow(&offer, &borrow) == EServiceStatus::busy));
    TEST_EXPECT(ctx, !client.endpoint->worker_to_host_msgs.read(message));
    ProviderOperationResult result;
    result.status = EServiceStatus::success;
    result.borrow = { 3u };
    TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(slot, result)));
    TEST_EXPECT(ctx, !provider.idle());
    TEST_EXPECT(ctx, ((provider.prepare_borrow(&offer, &borrow) == EServiceStatus::success) && (borrow.value == 3u) && provider.idle()));
}

void provider_pending_lifetimes(TTestContext& ctx)
{
    tests::SEndpointFixture client{ module_ids::rendering, 92u };
    threading::CEndpointContext endpoint{ *client.endpoint };
    CProviderClient provider{ endpoint };
    ServiceReply completion;
    completion.route = { 1u };
    completion.status = EServiceStatus::success;
    for (std::uint64_t request = 1u; request <= 257u; ++request)
    {
        completion.request = { request };
        TEST_EXPECT(ctx, (provider.complete(&completion) == EServiceStatus::busy));
    }
    threading::CErasedPodMsg message;
    ProviderOperationResult result;
    result.status = EServiceStatus::success;
    for (std::uint64_t request = 1u; request <= 256u; ++request)
    {
        TEST_EXPECT(ctx, client.endpoint->worker_to_host_msgs.read(message));
        TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(message.query_async_slot(), result)));
        TEST_EXPECT(ctx, !provider.idle());
        completion.request = { request };
        TEST_EXPECT(ctx, (provider.complete(&completion) == EServiceStatus::success));
        TEST_EXPECT(ctx, (provider.idle() == (request == 256u)));
    }
    TEST_EXPECT(ctx, !client.endpoint->worker_to_host_msgs.read(message));
    threading::SNotificationEndpoint notification;
    TEST_EXPECT(ctx, (provider.bind_borrow_notification({ 3u }, { 4u }, &notification) == EServiceStatus::busy));
    TEST_EXPECT(ctx, (!provider.retire_bindings({ 3u }) && !provider.idle()));
    TEST_EXPECT(ctx, client.endpoint->worker_to_host_msgs.read(message));
    TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(message.query_async_slot(), result)));
    TEST_EXPECT(ctx, !provider.idle());
    TEST_EXPECT(ctx, (provider.retire_bindings({ 3u }) && provider.idle()));
}

void delivery_and_authentication(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx, 1u); f.submit(ctx, 2u); f.submit(ctx, 3u);
    TEST_EXPECT(ctx, ((f.client.pending() == 3u) && (f.client.reserved_replies == 3u)));
    f.client.discard(3u);
    SDispatchedCommand commands[3];
    TEST_EXPECT(ctx, (tests::receive_commands(f.router, f.provider, commands, 3u) == 3u));
    TEST_EXPECT(ctx, ((commands[0].requester.value == f.client.instance.value) && (commands[2].command.request.value == 3u)));
    TEST_EXPECT(ctx, (f.router.complete(f.replacement, f.completion(1u)) == EServiceStatus::stale));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(1u)) == EServiceStatus::success));
    f.router.progress();
    TEST_EXPECT(ctx, ((f.client.pending() == 1u) && (f.client.reserved_replies == 2u)));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(1u)) == EServiceStatus::stale));
    TEST_EXPECT(ctx, (tests::receive_commands(f.router, f.provider, commands, 3u) == 0u));
    f.client.discard(1u);
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.close({ f.route, { 4u } }, 4, f.client);
    ServiceReply busy;
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(busy) && (busy.status == EServiceStatus::busy)));
    f.client.discard(1u);
    //  The old instance can die while a provider still retains accepted work.
    f.client.phase = host::EClientPhase::stopped;
    f.router.retire_client(f.client);
    f.router.progress();
    TEST_EXPECT(ctx, !f.router.client_idle(f.client));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(2u)) == EServiceStatus::success));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(3u)) == EServiceStatus::success));
    f.router.progress();
    TEST_EXPECT(ctx, (f.router.client_idle(f.client) && (f.client.reserved_replies == 0u)));
    TEST_EXPECT(ctx, ((f.replacement.pending() == 0u) && !f.router.provider_in_use(f.provider)));
    TEST_EXPECT(ctx, f.replacement.reserve_replies(1u));
    f.router.close({ f.route, { 5u } }, 5, f.replacement);
    TEST_EXPECT(ctx, (f.replacement.received(0).copy_payload_to(busy) && (busy.status == EServiceStatus::stale)));
    TEST_EXPECT(ctx, !f.router.failed());
}

void durable_resource_commit(TTestContext& ctx)
{
    for (unsigned scenario = 0u; scenario < 3u; ++scenario)
    {
        SFixture fixture{ ctx };
        fixture.submit(ctx, 1u);
        fixture.client.discard(fixture.client.pending());
        SDispatchedCommand command;
        TEST_EXPECT(ctx, (tests::receive_commands(fixture.router, fixture.provider, &command, 1u) == 1u));
        const ServiceReply reply = fixture.completion(1u);
        TEST_EXPECT(ctx, (fixture.router.commit_resource(fixture.replacement, reply) == EServiceStatus::stale));
        if (scenario == 0u) fixture.router.cancel_request(fixture.client, { fixture.route, { 1u } });
        if (scenario == 2u) fixture.client.stop_requested = true;
        const EServiceStatus expected = (scenario == 1u) ? EServiceStatus::success : EServiceStatus::cancelled;
        TEST_EXPECT(ctx, (fixture.router.commit_resource(fixture.provider, reply) == expected));
        if (scenario == 1u) fixture.router.cancel_request(fixture.client, { fixture.route, { 1u } });
        else
        {
            ServiceReply cancelled = reply;
            cancelled.status = EServiceStatus::cancelled;
            TEST_EXPECT(ctx, (fixture.router.complete(fixture.provider, cancelled) == EServiceStatus::success));
        }
        fixture.router.progress();
        ServiceReply result;
        TEST_EXPECT(ctx, ((fixture.client.pending() == 1u) && fixture.client.received(0).copy_payload_to(result)));
        TEST_EXPECT(ctx, (result.status == expected));
    }
}

void reserved_completion_and_cancellation(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx, 1u);
    f.client.discard(1u);
    SDispatchedCommand command;
    TEST_EXPECT(ctx, (tests::receive_commands(f.router, f.provider, &command, 1u) == 1u));
    //  Logical obligations do not reserve physical inbox slots. Force a bounded
    //  test transport to exercise retained completion after definite backpressure.
    f.client.endpoint->host_to_worker_msgs.deallocate();
    TEST_EXPECT(ctx, f.client.endpoint->host_to_worker_msgs.initialise_fixed(8u));
    const auto filler = threading::make_pod_message(-1, services::ServiceReply{});
    std::uint32_t filled = 0u;
    while (f.client.post(filler)) ++filled;
    TEST_EXPECT(ctx, (filled != 0u));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(1u)) == EServiceStatus::success));
    f.router.progress();
    TEST_EXPECT(ctx, (f.client.reserved_replies == 1u));
    f.client.discard(f.client.pending());
    f.router.progress();
    ServiceReply reply;
    TEST_EXPECT(ctx, ((f.client.pending() == 1u) && f.client.received(0u).copy_payload_to(reply) && (reply.request.value == 1u)));
    TEST_EXPECT(ctx, f.client.replies_idle());
    f.client.discard(1u);
    f.submit(ctx, 2u);
    f.submit(ctx, 3u);
    f.client.discard(2u);
    SDispatchedCommand commands[2];
    TEST_EXPECT(ctx, (tests::receive_commands(f.router, f.provider, commands, 2u) == 2u));
    f.router.stop_provider(f.provider, false);
    f.router.progress();
    TEST_EXPECT(ctx, ((f.client.pending() == 0u) && (f.client.reserved_replies == 2u)));
    f.router.stop_provider(f.provider, true);
    f.router.progress();
    TEST_EXPECT(ctx, ((f.client.pending() == 2u) && f.client.replies_idle() && f.router.client_idle(f.provider)));
}

void bounded_admission_and_terminal_precedence(TTestContext& ctx)
{
    SFixture f{ ctx };
    for (std::uint64_t id = 1u; id <= 256u; ++id)
    {
        f.submit(ctx, id);
        f.client.discard(f.client.pending());
    }
    f.submit(ctx, 257u);
    ServiceReply reply;
    TEST_EXPECT(ctx, ((f.client.pending() == 2u) && f.client.received(1).copy_payload_to(reply)));
    TEST_EXPECT(ctx, ((reply.status == EServiceStatus::busy) && (f.client.reserved_replies == 256u)));
    f.client.discard(2u);
    SDispatchedCommand first;
    TEST_EXPECT(ctx, (tests::receive_commands(f.router, f.provider, &first, 1u) == 1u));
    auto completed = f.completion(1u);
    completed.notice = EServiceNotice::prepared;
    TEST_EXPECT(ctx, (f.router.complete(f.provider, completed) == EServiceStatus::invalid_request));
    completed.notice = EServiceNotice::completed;
    TEST_EXPECT(ctx, (f.router.complete(f.provider, completed) == EServiceStatus::success));
    f.router.stop_provider(f.provider, true);
    f.router.progress();
    TEST_EXPECT(ctx, ((f.client.pending() == 256u) && (f.client.reserved_replies == 0u)));
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(reply) && (reply.request.value == 1u) && (reply.status == EServiceStatus::success)));
    TEST_EXPECT(ctx, (f.client.received(255).copy_payload_to(reply) && (reply.status == EServiceStatus::cancelled)));
    TEST_EXPECT(ctx, (f.router.client_idle(f.provider) && !f.router.failed()));
}

void common_fifo_and_reply_accounting(TTestContext& ctx)
{
    tests::SEndpointFixture client{ module_ids::rendering, 1u };
    client.phase = host::EClientPhase::ready;
    TEST_EXPECT(ctx, client.reserve_replies(1024u));
    TEST_EXPECT(ctx, client.reserve_replies(17u));
    for (std::int32_t index = 0; index < 1024; ++index)
    {
        const auto message = threading::make_pod_message(index, services::ServiceReply{});
        TEST_EXPECT(ctx, client.retry_reply(message));
        if (index == 15)
        {
            TEST_EXPECT(ctx, client.reply(threading::make_pod_message(-1, services::CancelServiceRequest{})));
        }
    }
    TEST_EXPECT(ctx, ((client.pending() == 1025u) && (client.reserved_replies == 16u)));
    TEST_EXPECT(ctx, (client.received(15u).query_async_slot() == 15));
    TEST_EXPECT(ctx, client.received(16u).is_payload_a<services::CancelServiceRequest>());
    TEST_EXPECT(ctx, (client.received(17u).query_async_slot() == 16));
    client.discard(client.pending());
    client.phase = host::EClientPhase::stopped;
    TEST_EXPECT(ctx, (client.publish_reply(threading::make_pod_message(-1, services::ServiceReply{})) == host::EReplyPublication::cancelled));
    client.cancel_reservation(15u);
    TEST_EXPECT(ctx, client.replies_idle());
}

void contract_identity(TTestContext& ctx)
{
    SFixture f{ctx};
    const SServiceContract contracts[]{ {1u, 1u}, {2u, 3u} };
    f.advertisement.contracts = contracts;
    f.advertisement.contract_count = 2u;
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.open({module_ids::rendering, 2u, 3u, {2u}}, 11, f.client, &f.advertisement);
    OpenServiceResult opened;
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(opened) && (opened.status == EServiceStatus::success)));
    f.client.discard(1u);
    f.submit(ctx, 1u);
    f.route = opened.route;
    f.submit(ctx, 1u); //  Identical opcode/request, distinct route and contract.
    SDispatchedCommand commands[2];
    TEST_EXPECT(ctx, (tests::receive_commands(f.router, f.provider, commands, 2u) == 2u));
    TEST_EXPECT(ctx, ((commands[0].contract.number == 1u) && (commands[0].contract.version == 1u)));
    TEST_EXPECT(ctx, ((commands[1].contract.number == 2u) && (commands[1].contract.version == 3u)));
    TEST_EXPECT(ctx, (commands[0].command.opcode == commands[1].command.opcode));
    f.router.stop_provider(f.provider, true);
    f.router.progress();
    TEST_EXPECT(ctx, f.client.replies_idle());
}

void provider_availability(TTestContext& ctx)
{
    SFixture f{ctx};
    //  Router admission needs provider identity/contracts, with no pump descriptor.
    for (const auto phase : { host::EClientPhase::starting, host::EClientPhase::stopping, host::EClientPhase::stopped })
    {
        f.provider.phase = phase;
        TEST_EXPECT(ctx, f.client.reserve_replies(1u));
        f.router.open({module_ids::rendering, 1u, 1u, {2u}}, 11, f.client, &f.advertisement);
        OpenServiceResult result;
        TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(result) && (result.status == EServiceStatus::unavailable)));
        TEST_EXPECT(ctx, ((result.route.value == 0u) && (result.provider.value == 0u)));
        f.client.discard(1u);
    }
    f.provider.phase = host::EClientPhase::ready;
    f.provider.stop_requested = true;
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.open({module_ids::rendering, 1u, 1u, {3u}}, 12, f.client, &f.advertisement);
    OpenServiceResult result;
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(result) && (result.status == EServiceStatus::unavailable)));
    f.client.discard(1u);
    f.provider.stop_requested = false;
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.open({module_ids::audio, 1u, 1u, {4u}}, 13, f.client, &f.advertisement);
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(result) && (result.status == EServiceStatus::unavailable)));
    f.client.discard(1u);
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.open({module_ids::rendering, 1u, 1u, {5u}}, 14, f.client, nullptr);
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(result) && (result.status == EServiceStatus::unavailable)));
    f.client.discard(1u);
    TEST_EXPECT(ctx, (!f.router.failed() && f.client.replies_idle()));
}

}   //  namespace

void run_host_runtime_tests(tests::TTestContext& ctx);
void run_service_borrow_tests(tests::TTestContext& ctx);
void run_notification_tests(tests::TTestContext& ctx);
void run_direct_connection_tests(tests::TTestContext& ctx);
void run_host_restriction_tests(tests::TTestContext& ctx);
void run_host_maintenance_tests(tests::TTestContext& ctx);
void run_platform_window_tests(tests::TTestContext& ctx);
void run_platform_report_tests(tests::TTestContext& ctx);
void run_platform_input_tests(tests::TTestContext& ctx);

int run_host_pump_tests()
{
    TTestContext ctx;
    coordinator_readiness(ctx);
    coordinator_stopping_creation(ctx);
    uncertain_provider_request(ctx);
    provider_pending_lifetimes(ctx);
    delivery_and_authentication(ctx);
    reserved_completion_and_cancellation(ctx);
    durable_resource_commit(ctx);
    bounded_admission_and_terminal_precedence(ctx);
    common_fifo_and_reply_accounting(ctx);
    contract_identity(ctx);
    provider_availability(ctx);
    run_host_runtime_tests(ctx);
    run_service_borrow_tests(ctx);
    run_notification_tests(ctx);
    run_direct_connection_tests(ctx);
    run_host_restriction_tests(ctx);
    run_host_maintenance_tests(ctx);
    run_platform_window_tests(ctx);
    run_platform_report_tests(ctx);
    run_platform_input_tests(ctx);
    return ctx.report("HostPump");
}
