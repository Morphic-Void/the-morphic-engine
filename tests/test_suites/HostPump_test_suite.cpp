
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
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "host/runtime/services/service_router.hpp"
#include "tests/test_suites/HostPump_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "framework/threading/messages/message_helpers.hpp"

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
    tests::SCommonEndpointFixture client{ module_ids::executive, 1u };
    tests::SCommonEndpointFixture replacement{ module_ids::executive, 2u };
    tests::SCommonEndpointFixture provider{ module_ids::rendering, 3u };
    host::CServiceRouter router;
    SServiceContract contract{ 1u, 1u };
    host::SServiceProvider advertisement;
    SServiceRoute route;

    explicit SFixture(TTestContext& ctx)
    {
        client.phase = replacement.phase = provider.phase = host::EClientPhase::ready;
        advertisement = { &provider, &contract, 1u };
        TEST_EXPECT(ctx, client.reserve_replies(1u));
        open({ module_ids::rendering, 1u, 1u, { 1u } }, 10, client, &advertisement);
        OpenServiceResult result;
        TEST_EXPECT(ctx, ((client.pending() == 1u) && client.received(0).copy_payload_to(result)));
        TEST_EXPECT(ctx, ((result.status == EServiceStatus::success) && (result.provider.value == provider.instance.value)));
        route = result.route;
        client.discard(client.pending());
    }
    void submit(TTestContext& ctx, const std::uint64_t id)
    {
        host::CRetainedReplies::CDispatch dispatch{ *client.pending_replies, 2u };
        TEST_EXPECT(ctx, dispatch);
        TEST_EXPECT(ctx, client.reserve_replies(2u));
        ServiceRequest request;
        request.route = route;
        request.request.value = id;
        request.opcode = 1u;
        router.request(request, static_cast<std::int32_t>(id), client);
    }
    void open(const OpenServiceRequest& request, const std::int32_t slot,
        host::SClientRecord& recipient, const host::SServiceProvider* const source)
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        MV_ASSERT(dispatch);
        router.open(request, slot, recipient, source);
    }
    void close(const CloseServiceRequest& request, const std::int32_t slot, host::SClientRecord& recipient)
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        MV_ASSERT(dispatch);
        router.close(request, slot, recipient);
    }
    void progress()
    {
        router.progress();
        // A controlled fixture drain, bounded by the router's operation count.
        // Production uses its finite output turn and interleaved ingress instead.
        for (host::SClientRecord* const endpoint : { &client, &replacement, &provider })
        {
            for (std::uint32_t count = 0u; count < 256u; ++count)
            {
                if (!endpoint->output_ready() || !router.output_ready(*endpoint)) break;
                router.retry_output(*endpoint);
            }
        }
        router.progress();
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
    f.progress();
    TEST_EXPECT(ctx, ((f.client.pending() == 1u) && (f.client.reserved_replies == 2u)));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(1u)) == EServiceStatus::stale));
    TEST_EXPECT(ctx, (tests::receive_commands(f.router, f.provider, commands, 3u) == 0u));
    f.client.discard(1u);
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.close({ f.route, { 4u } }, 4, f.client);
    ServiceReply busy;
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(busy) && (busy.status == EServiceStatus::busy)));
    f.client.discard(1u);
    //  The old instance can die while a provider still retains accepted work.
    f.client.phase = host::EClientPhase::stopped;
    f.router.retire_client(f.client);
    f.progress();
    TEST_EXPECT(ctx, !f.router.client_idle(f.client));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(2u)) == EServiceStatus::success));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(3u)) == EServiceStatus::success));
    f.progress();
    TEST_EXPECT(ctx, (f.router.client_idle(f.client) && (f.client.reserved_replies == 0u)));
    TEST_EXPECT(ctx, ((f.replacement.pending() == 0u) && !f.router.provider_in_use(f.provider)));
    TEST_EXPECT(ctx, f.replacement.reserve_replies(1u));
    f.close({ f.route, { 5u } }, 5, f.replacement);
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
        fixture.progress();
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
    const auto filler = threading::make_pod_message(-1, services::ServiceReply{});
    std::uint32_t filled = 0u;
    while (f.client.post(filler)) ++filled;
    TEST_EXPECT(ctx, (filled != 0u));
    TEST_EXPECT(ctx, (f.router.complete(f.provider, f.completion(1u)) == EServiceStatus::success));
    f.progress();
    TEST_EXPECT(ctx, (f.client.reserved_replies == 1u));
    f.client.discard(f.client.pending());
    f.progress();
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
    f.progress();
    TEST_EXPECT(ctx, ((f.client.pending() == 0u) && (f.client.reserved_replies == 2u)));
    f.router.stop_provider(f.provider, true);
    f.progress();
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
    f.progress();
    TEST_EXPECT(ctx, ((f.client.pending() == 256u) && (f.client.reserved_replies == 0u)));
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(reply) && (reply.request.value == 1u) && (reply.status == EServiceStatus::success)));
    TEST_EXPECT(ctx, (f.client.received(255).copy_payload_to(reply) && (reply.status == EServiceStatus::cancelled)));
    TEST_EXPECT(ctx, (f.router.client_idle(f.provider) && !f.router.failed()));
}

void common_fifo_and_reply_accounting(TTestContext& ctx)
{
    tests::SCommonEndpointFixture client{ module_ids::rendering, 1u };
    client.phase = host::EClientPhase::ready;
    TEST_EXPECT(ctx, client.reserve_replies(1024u));
    TEST_EXPECT(ctx, client.reserve_replies(17u));
    for (std::int32_t index = 0; index < 1024; ++index)
    {
        const auto message = threading::make_pod_message(index, services::ServiceReply{});
        if (index == 1023)
        {
            TEST_EXPECT(ctx, !client.retry_reply(message));
            TEST_EXPECT(ctx, (client.reserved_replies == 17u));
            (void)client.pending(); // Return ring capacity while preserving observed FIFO order.
        }
        TEST_EXPECT(ctx, client.retry_reply(message));
        if (index == 15)
        {
            host::CRetainedReplies::CDispatch dispatch{ *client.pending_replies, 1u };
            TEST_EXPECT(ctx, dispatch);
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
    f.open({module_ids::rendering, 2u, 3u, {2u}}, 11, f.client, &f.advertisement);
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
    f.progress();
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
        f.open({module_ids::rendering, 1u, 1u, {2u}}, 11, f.client, &f.advertisement);
        OpenServiceResult result;
        TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(result) && (result.status == EServiceStatus::unavailable)));
        TEST_EXPECT(ctx, ((result.route.value == 0u) && (result.provider.value == 0u)));
        f.client.discard(1u);
    }
    f.provider.phase = host::EClientPhase::ready;
    f.provider.stop_requested = true;
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.open({module_ids::rendering, 1u, 1u, {3u}}, 12, f.client, &f.advertisement);
    OpenServiceResult result;
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(result) && (result.status == EServiceStatus::unavailable)));
    f.client.discard(1u);
    f.provider.stop_requested = false;
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.open({module_ids::audio, 1u, 1u, {4u}}, 13, f.client, &f.advertisement);
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(result) && (result.status == EServiceStatus::unavailable)));
    f.client.discard(1u);
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.open({module_ids::rendering, 1u, 1u, {5u}}, 14, f.client, nullptr);
    TEST_EXPECT(ctx, (f.client.received(0).copy_payload_to(result) && (result.status == EServiceStatus::unavailable)));
    f.client.discard(1u);
    TEST_EXPECT(ctx, (!f.router.failed() && f.client.replies_idle()));
}

}   //  namespace

void run_host_runtime_tests(tests::TTestContext& ctx);
void run_module_preamble_tests(tests::TTestContext& ctx);
void run_service_borrow_tests(tests::TTestContext& ctx);
void run_notification_tests(tests::TTestContext& ctx);
void run_direct_connection_tests(tests::TTestContext& ctx);
void run_host_restriction_tests(tests::TTestContext& ctx);
void run_host_maintenance_tests(tests::TTestContext& ctx);
void run_platform_window_tests(tests::TTestContext& ctx);
void run_platform_report_tests(tests::TTestContext& ctx);
void run_platform_input_tests(tests::TTestContext& ctx);
void run_provider_facade_tests(tests::TTestContext& ctx);
void run_standard_coordinator_tests(tests::TTestContext& ctx);

static void run_service_router_tests(TTestContext& ctx)
{
    delivery_and_authentication(ctx);
    reserved_completion_and_cancellation(ctx);
    durable_resource_commit(ctx);
    bounded_admission_and_terminal_precedence(ctx);
    common_fifo_and_reply_accounting(ctx);
    contract_identity(ctx);
    provider_availability(ctx);
}

int run_host_services_focused()
{
    TTestContext ctx;
    run_provider_facade_tests(ctx);
    run_standard_coordinator_tests(ctx);
    run_service_router_tests(ctx);
    run_service_borrow_tests(ctx);
    run_host_runtime_tests(ctx);
    return ctx.report("HostServices");
}

int run_host_pump_tests()
{
    TTestContext ctx;
    run_module_preamble_tests(ctx);
    run_service_router_tests(ctx);
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
