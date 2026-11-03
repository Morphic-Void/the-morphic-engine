
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostPump_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Portable routing, retained prefixes, exact requester lifetime and reply admission.

#include "module/module_messages.hpp"
#include "host/runtime/service_router.hpp"
#include "tests/test_suites/HostPump_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "threading/messages/message_helpers.hpp"

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
    host::SClientRecord client{ module_ids::executive, 1u };
    host::SClientRecord replacement{ module_ids::executive, 2u };
    host::SClientRecord provider{ module_ids::rendering, 3u };
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
        TEST_EXPECT(ctx, client.message_count == 1u && client.messages[0].copy_payload_to(result));
        TEST_EXPECT(ctx, result.status == EServiceStatus::success && result.provider.value == provider.instance.value);
        route = result.route;
        client.consume(client.message_count);
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

void prefix_and_authentication(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx, 1u); f.submit(ctx, 2u); f.submit(ctx, 3u);
    TEST_EXPECT(ctx, f.client.message_count == 3u && f.client.reserved_replies == 3u);
    f.client.consume(3u);
    SDispatchedCommand commands[3];
    TEST_EXPECT(ctx, f.router.prepare_batch(f.provider, commands, 3u) == 3u);
    TEST_EXPECT(ctx, commands[0].requester.value == f.client.instance.value && commands[2].command.request.value == 3u);
    TEST_EXPECT(ctx, f.router.complete(f.replacement, f.completion(1u)) == EServiceStatus::stale);
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.completion(1u)) == EServiceStatus::success);
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == 0u); //  Wait for confirmed consumption.
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, commands, 3u, 1u));
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == 1u && f.client.reserved_replies == 2u);
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.completion(1u)) == EServiceStatus::stale);
    TEST_EXPECT(ctx, f.router.prepare_batch(f.provider, commands, 3u) == 2u && commands[0].command.request.value == 2u);
    TEST_EXPECT(ctx, !f.router.finish_batch(f.provider, commands, 2u, 3u));
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, commands, 2u, 2u));
    f.client.consume(1u);
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.close({ f.route, { 4u } }, 4, f.client);
    ServiceReply busy;
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(busy) && busy.status == EServiceStatus::busy);
    f.client.consume(1u);
    //  The old instance can die while a provider still retains accepted work.
    f.client.phase = host::EClientPhase::stopped;
    f.router.retire_client(f.client);
    f.router.progress();
    TEST_EXPECT(ctx, !f.router.client_idle(f.client));
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.completion(2u)) == EServiceStatus::success);
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.completion(3u)) == EServiceStatus::success);
    f.router.progress();
    TEST_EXPECT(ctx, f.router.client_idle(f.client) && f.client.reserved_replies == 0u);
    TEST_EXPECT(ctx, f.replacement.message_count == 0u && !f.router.provider_in_use(f.provider));
    TEST_EXPECT(ctx, f.replacement.reserve_replies(1u));
    f.router.close({ f.route, { 5u } }, 5, f.replacement);
    TEST_EXPECT(ctx, f.replacement.messages[0].copy_payload_to(busy) && busy.status == EServiceStatus::stale);
    TEST_EXPECT(ctx, !f.router.failed());
}

void reserved_completion_and_cancellation(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx, 1u);
    f.client.consume(1u);
    SDispatchedCommand command;
    TEST_EXPECT(ctx, f.router.prepare_batch(f.provider, &command, 1u) == 1u);
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, &command, 1u, 1u));
    const auto filler = threading::make_pod_message(-1, modules::RunTestsResult{});
    std::uint32_t filled = 0u;
    while (f.client.post_reply(filler)) ++filled;
    TEST_EXPECT(ctx, filled == host::SClientRecord::k_reply_capacity - 1u);
    TEST_EXPECT(ctx, !f.client.reserve_replies(1u));
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.completion(1u)) == EServiceStatus::success);
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == filled + 1u && f.client.reserved_replies == 0u);
    ServiceReply reply;
    TEST_EXPECT(ctx, f.client.messages[filled].copy_payload_to(reply) && reply.request.value == 1u);
    f.client.consume(f.client.message_count);
    f.submit(ctx, 2u); f.submit(ctx, 3u);
    f.client.consume(2u);
    TEST_EXPECT(ctx, f.router.prepare_batch(f.provider, &command, 1u) == 1u);
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, &command, 1u, 1u));
    f.router.stop_provider(f.provider, false);
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == 1u && f.client.messages[0].copy_payload_to(reply));
    TEST_EXPECT(ctx, reply.request.value == 3u && reply.status == EServiceStatus::cancelled);
    TEST_EXPECT(ctx, f.client.reserved_replies == 1u && !f.router.client_idle(f.provider));
    f.router.stop_provider(f.provider, true); //  Stopped, not a timeout, proves provider borrows have retired.
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == 2u && f.client.reserved_replies == 0u && f.router.client_idle(f.provider));
}

void bounded_admission_and_terminal_precedence(TTestContext& ctx)
{
    SFixture f{ ctx };
    for (std::uint64_t id = 1u; id <= 256u; ++id)
    {
        f.submit(ctx, id);
        f.client.consume(f.client.message_count);
    }
    f.submit(ctx, 257u);
    ServiceReply reply;
    TEST_EXPECT(ctx, f.client.message_count == 2u && f.client.messages[1].copy_payload_to(reply));
    TEST_EXPECT(ctx, reply.status == EServiceStatus::busy && f.client.reserved_replies == 256u);
    f.client.consume(2u);
    SDispatchedCommand first;
    TEST_EXPECT(ctx, f.router.prepare_batch(f.provider, &first, 1u) == 1u);
    auto completed = f.completion(1u);
    completed.notice = EServiceNotice::prepared;
    TEST_EXPECT(ctx, f.router.complete(f.provider, completed) == EServiceStatus::invalid_request);
    completed.notice = EServiceNotice::completed;
    TEST_EXPECT(ctx, f.router.complete(f.provider, completed) == EServiceStatus::success);
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, &first, 1u, 1u));
    f.router.stop_provider(f.provider, true);
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == 256u && f.client.reserved_replies == 0u);
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(reply) && reply.request.value == 1u && reply.status == EServiceStatus::success);
    TEST_EXPECT(ctx, f.client.messages[255].copy_payload_to(reply) && reply.status == EServiceStatus::cancelled);
    TEST_EXPECT(ctx, f.router.client_idle(f.provider) && !f.router.failed());
}

void independent_control_credits(TTestContext& ctx)
{
    host::SClientRecord client{module_ids::rendering, 1u};
    client.phase = host::EClientPhase::ready;
    const auto ordinary = threading::make_pod_message(1, modules::RunTestsResult{});
    const auto control = threading::make_pod_message(2, modules::RunTestsResult{});
    TEST_EXPECT(ctx, client.reserve_replies(512u));
    TEST_EXPECT(ctx, !client.reserve_replies(1u) && !client.post_reply(ordinary));
    TEST_EXPECT(ctx, client.reserve_control_replies(16u));
    TEST_EXPECT(ctx, !client.reserve_control_replies(1u) && !client.post(control));
    TEST_EXPECT(ctx, client.reserve_control_replies(0u)); //  RunTestsResult needs no return credit.
    for (unsigned index = 0u; index < 16u; ++index) TEST_EXPECT(ctx, client.reply_control(control));
    TEST_EXPECT(ctx, client.reserved_replies == 512u && client.reserved_control_replies == 0u);
    TEST_EXPECT(ctx, !client.reserve_control_replies(1u));
    TEST_EXPECT(ctx, client.retry_reply(ordinary));
    threading::CErasedPodMsg messages[20];
    std::uint32_t controls = 0u;
    TEST_EXPECT(ctx, client.copy_messages(messages, 20u, controls) == 17u && controls == 16u);
    TEST_EXPECT(ctx, messages[0].query_async_slot() == 2 && messages[16].query_async_slot() == 1);
    client.consume_messages(3u, controls);
    TEST_EXPECT(ctx, client.control_count == 13u && client.message_count == 1u);
    TEST_EXPECT(ctx, client.reserve_control_replies(3u));
    client.cancel_control_reservation(2u);
    TEST_EXPECT(ctx, client.post(control)); //  Retry after a consumed prefix returned space.
    TEST_EXPECT(ctx, client.copy_messages(messages, 20u, controls) == 15u && controls == 14u);
    client.consume_messages(15u, controls);
    TEST_EXPECT(ctx, client.message_count == 0u && client.control_count == 0u);
    client.phase = host::EClientPhase::stopped;
    TEST_EXPECT(ctx, client.reply_control(control)); //  Cancel exact terminal recipient's credit.
    client.cancel_reservation(511u);
    TEST_EXPECT(ctx, client.replies_idle() && client.control_count == 0u);
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
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(opened) && opened.status == EServiceStatus::success);
    f.client.consume(1u);
    f.submit(ctx, 1u);
    f.route = opened.route;
    f.submit(ctx, 1u); //  Identical opcode/request, distinct route and contract.
    SDispatchedCommand commands[2];
    TEST_EXPECT(ctx, f.router.prepare_batch(f.provider, commands, 2u) == 2u);
    TEST_EXPECT(ctx, commands[0].contract.number == 1u && commands[0].contract.version == 1u);
    TEST_EXPECT(ctx, commands[1].contract.number == 2u && commands[1].contract.version == 3u);
    TEST_EXPECT(ctx, commands[0].command.opcode == commands[1].command.opcode);
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, commands, 2u, 2u));
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
        TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(result) && result.status == EServiceStatus::unavailable);
        TEST_EXPECT(ctx, result.route.value == 0u && result.provider.value == 0u);
        f.client.consume(1u);
    }
    f.provider.phase = host::EClientPhase::ready;
    f.provider.stop_requested = true;
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.open({module_ids::rendering, 1u, 1u, {3u}}, 12, f.client, &f.advertisement);
    OpenServiceResult result;
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(result) && result.status == EServiceStatus::unavailable);
    f.client.consume(1u);
    f.provider.stop_requested = false;
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.open({module_ids::audio, 1u, 1u, {4u}}, 13, f.client, &f.advertisement);
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(result) && result.status == EServiceStatus::unavailable);
    f.client.consume(1u);
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.open({module_ids::rendering, 1u, 1u, {5u}}, 14, f.client, nullptr);
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(result) && result.status == EServiceStatus::unavailable);
    f.client.consume(1u);
    TEST_EXPECT(ctx, !f.router.failed() && f.client.replies_idle());
}

}   //  namespace

void run_host_runtime_tests(tests::TTestContext& ctx);

int run_host_pump_tests()
{
    TTestContext ctx;
    prefix_and_authentication(ctx);
    reserved_completion_and_cancellation(ctx);
    bounded_admission_and_terminal_precedence(ctx);
    independent_control_credits(ctx);
    contract_identity(ctx);
    provider_availability(ctx);
    run_host_runtime_tests(ctx);
    return ctx.report("HostPump");
}
