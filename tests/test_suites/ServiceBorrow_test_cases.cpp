
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ServiceBorrow_test_cases.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Publication, exact-instance protection, cancellation and two-sided retirement.

#include <cstring>
#include "host/runtime/service_router.hpp"
#include "services/service_borrow_client.hpp"
#include "tests/support/test_context.hpp"
#include "threading/messages/message_helpers.hpp"

namespace
{

using namespace services;
using tests::TTestContext;

static_assert(threading::CErasedPodMsg::is_payload_compatible_with<ReleaseServiceBorrow>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<ServiceBorrowQuiescent>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<CancelServiceRequest>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<ServiceBorrowControl>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<ServiceRequestCancelled>());

struct SFixture
{
    host::SClientRecord client{ module_ids::executive, 101u };
    host::SClientRecord replacement{ module_ids::executive, 102u };
    host::SClientRecord provider{ module_ids::rendering, 103u };
    host::CServiceRouter router;
    CServiceBorrowClient consumer;
    SServiceRoute route;
    unsigned descriptor{ 42u };
    SDispatchedCommand command;
    SServiceBorrow token;

    explicit SFixture(TTestContext& ctx)
    {
        client.phase = replacement.phase = provider.phase = host::EClientPhase::ready;
        const SServiceContract contract{ 1u, 1u };
        const host::SServiceProvider advertisement{ &provider, &contract, 1u };
        TEST_EXPECT(ctx, client.reserve_replies(1u));
        router.open({ module_ids::rendering, 1u, 1u, { 1u } }, 1, client, &advertisement);
        OpenServiceResult result;
        TEST_EXPECT(ctx, client.messages[0].copy_payload_to(result) && result.status == EServiceStatus::success);
        route = result.route;
        client.consume(1u);
    }

    void submit(TTestContext& ctx)
    {
        TEST_EXPECT(ctx, consumer.begin(route, { 1u }, 1u));
        TEST_EXPECT(ctx, client.reserve_replies(2u));
        ServiceRequest request;
        request.route = route;
        request.request.value = 1u;
        request.opcode = 1u;
        router.request(request, 4, client);
        observe(ctx);
        TEST_EXPECT(ctx, router.prepare_batch(provider, &command, 1u) == 1u);
    }

    SServiceBorrowOffer offer() const
    {
        return { route, { 1u }, { &descriptor, sizeof(descriptor), 1u } };
    }

    ServiceReply result() const
    {
        ServiceReply reply;
        reply.route = route;
        reply.request.value = 1u;
        reply.opcode = 1u;
        reply.status = EServiceStatus::success;
        return reply;
    }

    void observe(TTestContext& ctx)
    {
        for (std::uint32_t index = 0u; index < client.control_count; ++index)
        {
            ServiceBorrowControl control;
            TEST_EXPECT(ctx, client.control_messages[index].copy_payload_to(control) && consumer.observe(control));
        }
        client.consume_messages(client.control_count, client.control_count);
        for (std::uint32_t index = 0u; index < client.message_count; ++index)
        {
            ServiceReply reply;
            TEST_EXPECT(ctx, client.messages[index].copy_payload_to(reply) && consumer.observe(reply));
        }
        client.consume(client.message_count);
    }

    EServiceQuiescence proof() const
    {
        for (std::uint32_t index = 0u; index < provider.control_count; ++index)
        {
            ServiceBorrowControl control;
            if (provider.control_messages[index].copy_payload_to(control) &&
                control.control == EServiceBorrowControl::consumer_quiescent) return control.quiescence;
        }
        return EServiceQuiescence::none;
    }

    void prepare(TTestContext& ctx)
    {
        TEST_EXPECT(ctx, router.prepare_borrow(provider, offer(), token) == EServiceStatus::success);
        TEST_EXPECT(ctx, token.value != 0u);
    }

    void publish(TTestContext& ctx)
    {
        TEST_EXPECT(ctx, router.finish_batch(provider, &command, 1u, 1u));
        router.progress();
        observe(ctx);
        TEST_EXPECT(ctx, consumer.view(token).descriptor == &descriptor);
    }

    void finish(TTestContext& ctx)
    {
        TEST_EXPECT(ctx, router.finish_borrow_retirement(provider, token) == EServiceStatus::success);
        TEST_EXPECT(ctx, router.finish_borrow_retirement(provider, token) == EServiceStatus::success);
        router.progress();
        observe(ctx);
        TEST_EXPECT(ctx, consumer.retired(token));
        TEST_EXPECT(ctx, consumer.forget(route, { 1u }));
        provider.consume_messages(provider.control_count, provider.control_count);
    }
};

void independent_lifetime(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx);
    SServiceBorrow invalid{ 9u };
    TEST_EXPECT(ctx, f.router.prepare_borrow(f.replacement, f.offer(), invalid) == EServiceStatus::stale && invalid.value == 0u);
    auto bad = f.offer();
    bad.view.descriptor = nullptr;
    TEST_EXPECT(ctx, f.router.prepare_borrow(f.provider, bad, invalid) == EServiceStatus::invalid_request);
    f.prepare(ctx);
    TEST_EXPECT(ctx, f.router.prepare_borrow(f.provider, f.offer(), invalid) == EServiceStatus::invalid_request);
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, f.token) == EServiceStatus::busy);
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.result()) == EServiceStatus::success);
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == 0u); //  Prefix consumption still unconfirmed.
    f.publish(ctx);
    TEST_EXPECT(ctx, f.client.replies_idle() && !f.consumer.forget(f.route, { 1u }));
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.close({ f.route, { 2u } }, 7, f.client);
    ServiceReply reply;
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(reply) && reply.status == EServiceStatus::busy);
    f.client.consume(1u);
    f.router.release_borrow(f.replacement, { f.route, f.token });
    f.router.quiesce_borrow(f.replacement, { f.route, f.token });
    f.router.progress();
    TEST_EXPECT(ctx, f.provider.control_count == 0u && !f.router.provider_borrows_retired(f.provider));
    ReleaseServiceBorrow release;
    TEST_EXPECT(ctx, f.consumer.release(f.token, release));
    f.router.release_borrow(f.client, release);
    f.router.progress();
    f.observe(ctx);
    TEST_EXPECT(ctx, f.consumer.view(f.token).descriptor == nullptr && f.proof() == EServiceQuiescence::none);
    ServiceBorrowQuiescent quiescent;
    TEST_EXPECT(ctx, f.consumer.quiesce(f.token, quiescent));
    f.router.quiesce_borrow(f.client, quiescent);
    f.router.progress();
    TEST_EXPECT(ctx, f.proof() == EServiceQuiescence::relinquished);
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.replacement, f.token) == EServiceStatus::stale);
    f.finish(ctx);
    TEST_EXPECT(ctx, f.router.provider_borrows_retired(f.provider));
    TEST_EXPECT(ctx, f.client.reserve_replies(1u));
    f.router.close({ f.route, { 3u } }, 8, f.client);
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(reply) && reply.status == EServiceStatus::success);
    TEST_EXPECT(ctx, f.router.client_idle(f.client) && !f.router.failed());
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, f.token) == EServiceStatus::stale);
}

void cancellation_before_publication(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx);
    f.prepare(ctx);
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, &f.command, 1u, 1u));
    CancelServiceRequest cancel;
    TEST_EXPECT(ctx, f.consumer.cancel(f.route, { 1u }, cancel));
    f.router.cancel_request(f.client, cancel);
    f.router.progress();
    f.observe(ctx);
    TEST_EXPECT(ctx, f.consumer.view(f.token).descriptor == nullptr && f.client.replies_idle());
    TEST_EXPECT(ctx, f.proof() == EServiceQuiescence::never_published);
    TEST_EXPECT(ctx, !f.router.client_idle(f.client));
    SServiceBorrow late;
    TEST_EXPECT(ctx, f.router.prepare_borrow(f.provider, f.offer(), late) == EServiceStatus::closing);
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, f.token) == EServiceStatus::success);
    f.router.progress();
    f.observe(ctx);
    TEST_EXPECT(ctx, !f.router.client_idle(f.client)); //  Provider work still owes completion.
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.result()) == EServiceStatus::success);
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == 0u); //  Exactly one terminal result.
    TEST_EXPECT(ctx, f.consumer.forget(f.route, { 1u }));
    TEST_EXPECT(ctx, !f.router.failed());
}

void closing_overtakes_prepared(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx);
    f.prepare(ctx);
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, &f.command, 1u, 1u));
    f.router.progress(); //  Prepared is queued but the consumer has not read it.
    TEST_EXPECT(ctx, f.router.begin_borrow_retirement(f.provider, f.token) == EServiceStatus::success);
    f.router.progress();
    ServiceBorrowControl closing;
    TEST_EXPECT(ctx, f.client.control_messages[0].copy_payload_to(closing) && f.consumer.observe(closing));
    f.client.consume_messages(1u, 1u);
    ServiceBorrowQuiescent quiescent;
    TEST_EXPECT(ctx, !f.consumer.quiesce(f.token, quiescent));
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.result()) == EServiceStatus::success);
    f.router.progress();
    f.observe(ctx); //  Late prepared is recorded, never exposed through view().
    TEST_EXPECT(ctx, f.consumer.view(f.token).descriptor == nullptr);
    TEST_EXPECT(ctx, f.consumer.quiesce(f.token, quiescent));
    f.router.quiesce_borrow(f.client, quiescent);
    f.router.progress();
    f.finish(ctx);
}

void saturated_cleanup_and_terminal_proof(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx);
    f.prepare(ctx);
    f.publish(ctx);
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.result()) == EServiceStatus::success);
    f.router.progress();
    f.observe(ctx);
    f.provider.control_count = host::SClientRecord::k_control_capacity;
    TEST_EXPECT(ctx, f.router.begin_borrow_retirement(f.provider, f.token) == EServiceStatus::success);
    f.client.phase = host::EClientPhase::stopped;
    f.client.incoming_drained = true;
    f.router.retire_client(f.client);
    f.router.progress();
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, f.token) == EServiceStatus::busy);
    f.provider.consume_messages(f.provider.control_count, f.provider.control_count);
    f.router.progress();
    TEST_EXPECT(ctx, f.proof() == EServiceQuiescence::none); //  Terminal was not observed before draining.
    f.client.terminal_before_drain = true;
    f.router.retire_client(f.client);
    f.router.progress();
    TEST_EXPECT(ctx, f.proof() == EServiceQuiescence::execution_ended);
    //  A provider can retain native obligations even after CPU readers end.
    TEST_EXPECT(ctx, !f.router.client_idle(f.client) && !f.router.provider_borrows_retired(f.provider));
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, f.token) == EServiceStatus::success);
    f.router.progress();
    TEST_EXPECT(ctx, f.router.client_idle(f.client) && f.router.provider_borrows_retired(f.provider));
    TEST_EXPECT(ctx, f.replacement.message_count == 0u && f.replacement.control_count == 0u);
}

void prepared_credit_and_bad_prefix(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx);
    f.client.message_count = host::SClientRecord::k_reply_capacity - f.client.reserved_replies;
    SServiceBorrow token;
    TEST_EXPECT(ctx, f.router.prepare_borrow(f.provider, f.offer(), token) == EServiceStatus::busy && token.value == 0u);
    f.client.consume(f.client.message_count);
    f.prepare(ctx);
    TEST_EXPECT(ctx, !f.router.finish_batch(f.provider, &f.command, 1u, 0u));
    f.router.progress();
    TEST_EXPECT(ctx, f.client.message_count == 0u);
    f.router.stop_provider(f.provider, false);
    f.router.progress();
    TEST_EXPECT(ctx, f.proof() == EServiceQuiescence::never_published);
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, f.token) == EServiceStatus::success);
    f.router.stop_provider(f.provider, true);
    f.router.progress();
    TEST_EXPECT(ctx, f.router.client_idle(f.client) && f.client.replies_idle() && !f.router.failed());
}

void cancellation_and_completion_precedence(TTestContext& ctx)
{
    CServiceBorrowClient pending;
    CancelServiceRequest cancel;
    TEST_EXPECT(ctx, pending.begin({ 900u }, { 1u }, 1u));
    TEST_EXPECT(ctx, !pending.cancel({ 900u }, { 1u }, cancel));
    TEST_EXPECT(ctx, pending.abandon_unsubmitted({ 900u }, { 1u }));
    SFixture f{ ctx };
    f.submit(ctx);
    TEST_EXPECT(ctx, !f.consumer.abandon_unsubmitted(f.route, { 1u }));
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, &f.command, 1u, 1u));
    f.router.cancel_request(f.replacement, { f.route, { 1u } });
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.result()) == EServiceStatus::success);
    f.router.cancel_request(f.client, { f.route, { 1u } });
    f.router.progress();
    ServiceReply reply;
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(reply) && reply.status == EServiceStatus::success);
    TEST_EXPECT(ctx, f.provider.control_count == 0u);
    f.observe(ctx);
    TEST_EXPECT(ctx, f.consumer.forget(f.route, { 1u }));
}

void separate_consumers(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.submit(ctx);
    f.prepare(ctx);
    f.publish(ctx);
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.result()) == EServiceStatus::success);
    f.router.progress();
    f.observe(ctx);
    const SServiceContract contract{ 1u, 1u };
    const host::SServiceProvider advertisement{ &f.provider, &contract, 1u };
    TEST_EXPECT(ctx, f.replacement.reserve_replies(1u));
    f.router.open({ module_ids::rendering, 1u, 1u, { 1u } }, 9, f.replacement, &advertisement);
    OpenServiceResult opened;
    TEST_EXPECT(ctx, f.replacement.messages[0].copy_payload_to(opened));
    f.replacement.consume(1u);
    TEST_EXPECT(ctx, f.replacement.reserve_replies(2u));
    ServiceRequest request;
    request.route = opened.route;
    request.request.value = 1u;
    request.opcode = 1u;
    f.router.request(request, 10, f.replacement);
    f.replacement.consume(1u);
    SDispatchedCommand command;
    TEST_EXPECT(ctx, f.router.prepare_batch(f.provider, &command, 1u) == 1u);
    SServiceBorrow other;
    TEST_EXPECT(ctx, f.router.prepare_borrow(f.provider, { opened.route, { 1u }, f.offer().view }, other) == EServiceStatus::success);
    auto reply = f.result();
    reply.route = opened.route;
    TEST_EXPECT(ctx, f.router.complete(f.provider, reply) == EServiceStatus::success);
    TEST_EXPECT(ctx, f.router.finish_batch(f.provider, &command, 1u, 1u));
    f.router.progress();
    ServiceReply prepared;
    TEST_EXPECT(ctx, f.replacement.messages[0].copy_payload_to(prepared) && prepared.notice == EServiceNotice::prepared);
    SPreparedServiceBorrow view;
    std::memcpy(&view, prepared.payload, sizeof(view));
    TEST_EXPECT(ctx, view.view.descriptor == &f.descriptor && other.value != f.token.value);
    f.replacement.consume(f.replacement.message_count);
    ReleaseServiceBorrow release;
    ServiceBorrowQuiescent quiescent;
    TEST_EXPECT(ctx, f.consumer.release(f.token, release) && f.consumer.quiesce(f.token, quiescent));
    f.router.release_borrow(f.client, release);
    f.router.quiesce_borrow(f.client, quiescent);
    f.router.progress();
    f.finish(ctx);
    TEST_EXPECT(ctx, !f.router.provider_borrows_retired(f.provider) && *static_cast<const unsigned*>(view.view.descriptor) == 42u);
    f.router.quiesce_borrow(f.client, { opened.route, other }); //  Same module ID is not the same consumer.
    TEST_EXPECT(ctx, f.router.begin_borrow_retirement(f.provider, other) == EServiceStatus::success);
    f.router.progress();
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, other) == EServiceStatus::busy);
    f.router.quiesce_borrow(f.replacement, { opened.route, other });
    f.router.progress();
    TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, other) == EServiceStatus::success);
    f.router.progress();
    TEST_EXPECT(ctx, f.router.provider_borrows_retired(f.provider));
}

void cancelled_completed_borrow(TTestContext& ctx, const bool queued_terminal)
{
    SFixture f{ ctx };
    f.submit(ctx);
    f.prepare(ctx);
    f.publish(ctx);
    TEST_EXPECT(ctx, f.router.complete(f.provider, f.result()) == EServiceStatus::success);
    if (queued_terminal) f.router.progress(); //  Operation is already gone; borrow remains.
    CancelServiceRequest cancel;
    TEST_EXPECT(ctx, f.consumer.cancel(f.route, { 1u }, cancel));
    f.router.cancel_request(f.replacement, cancel);
    TEST_EXPECT(ctx, f.provider.control_count == 0u);
    f.router.cancel_request(f.client, cancel);
    f.router.progress();
    ServiceReply reply;
    TEST_EXPECT(ctx, f.client.messages[0].copy_payload_to(reply) && reply.status == EServiceStatus::success);
    f.observe(ctx);
    TEST_EXPECT(ctx, f.consumer.view(f.token).descriptor == nullptr);
    ServiceBorrowQuiescent quiescent;
    TEST_EXPECT(ctx, f.consumer.quiesce(f.token, quiescent));
    f.router.quiesce_borrow(f.client, quiescent);
    f.router.progress();
    TEST_EXPECT(ctx, f.proof() == EServiceQuiescence::relinquished);
    f.finish(ctx);
    TEST_EXPECT(ctx, f.client.replies_idle() && f.router.provider_borrows_retired(f.provider) && !f.router.failed());
}

void bounded_long_lived_borrows(TTestContext& ctx)
{
    SFixture f{ ctx };
    SServiceBorrow tokens[host::CServiceBorrows::k_capacity];
    for (std::uint32_t index = 0u; index <= host::CServiceBorrows::k_capacity; ++index)
    {
        const SServiceRequestId id{ index + 1u };
        TEST_EXPECT(ctx, f.client.reserve_replies(2u));
        ServiceRequest request;
        request.route = f.route;
        request.request = id;
        request.opcode = 1u;
        f.router.request(request, static_cast<std::int32_t>(index), f.client);
        f.client.consume(f.client.message_count);
        SDispatchedCommand command;
        TEST_EXPECT(ctx, f.router.prepare_batch(f.provider, &command, 1u) == 1u);
        SServiceBorrow token;
        const auto status = f.router.prepare_borrow(f.provider, { f.route, id, f.offer().view }, token);
        TEST_EXPECT(ctx, status == (index < host::CServiceBorrows::k_capacity ? EServiceStatus::success : EServiceStatus::busy));
        if (index < host::CServiceBorrows::k_capacity)
        {
            tokens[index] = token;
            TEST_EXPECT(ctx, token.value > (index == 0u ? 0u : tokens[index - 1u].value));
        }
        auto reply = f.result();
        reply.request = id;
        TEST_EXPECT(ctx, f.router.complete(f.provider, reply) == EServiceStatus::success);
        TEST_EXPECT(ctx, f.router.finish_batch(f.provider, &command, 1u, 1u));
        f.router.progress();
        f.client.consume(f.client.message_count);
    }
    for (const auto token : tokens)
    {
        f.router.release_borrow(f.client, { f.route, token });
        f.router.quiesce_borrow(f.client, { f.route, token });
    }
    //  Only sixteen controls fit at a time; remaining obligations stay durable.
    std::uint32_t finished = 0u;
    for (std::uint32_t turn = 0u; (turn < 512u) && (finished < host::CServiceBorrows::k_capacity); ++turn)
    {
        f.router.progress();
        for (std::uint32_t index = 0u; index < f.provider.control_count; ++index)
        {
            ServiceBorrowControl control;
            TEST_EXPECT(ctx, f.provider.control_messages[index].copy_payload_to(control));
            if (control.control == EServiceBorrowControl::consumer_quiescent)
            {
                TEST_EXPECT(ctx, f.router.finish_borrow_retirement(f.provider, control.borrow) == EServiceStatus::success);
                ++finished;
            }
        }
        f.provider.consume_messages(f.provider.control_count, f.provider.control_count);
        f.client.consume_messages(f.client.control_count, f.client.control_count);
    }
    f.router.progress();
    TEST_EXPECT(ctx, finished == host::CServiceBorrows::k_capacity && f.router.provider_borrows_retired(f.provider) && f.client.replies_idle());
}

}   //  namespace

void run_service_borrow_tests(tests::TTestContext& ctx)
{
    independent_lifetime(ctx);
    cancellation_before_publication(ctx);
    closing_overtakes_prepared(ctx);
    saturated_cleanup_and_terminal_proof(ctx);
    prepared_credit_and_bad_prefix(ctx);
    cancellation_and_completion_precedence(ctx);
    separate_consumers(ctx);
    cancelled_completed_borrow(ctx, false);
    cancelled_completed_borrow(ctx, true);
    bounded_long_lived_borrows(ctx);
}
