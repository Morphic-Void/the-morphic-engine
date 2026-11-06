
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DirectConnections_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    05 Nov 26
//
//  Deterministic service/Host interleavings using the production pipes and helpers.

#include "framework/communications/module_tests.hpp"
#include "framework/communications/direct_client.hpp"
#include "host/runtime/direct_connections.hpp"
#include "host/runtime/host_restrictions.hpp"
#include "module/module_messages.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/support/test_allocator.hpp"
#include "platform/threading/processor_relax.hpp"

namespace
{
using connections::EStatus;
using connections::SToken;
static const connections::SMessageRule rules[]{
    { system_type_ids::run_tests_request, module_ids::executive },
    { system_type_ids::run_tests_result, module_ids::rendering }
};
static const connections::SContractPolicy policies[]{
    { connections::contract(module_ids::executive, module_ids::rendering, 1u), rules, 2u },
    { connections::contract(module_ids::executive, module_ids::rendering, 2u), rules, 2u }
};

struct SFixture
{
    tests::TTestContext& test;
    platform::system::CPerfCountConversion conversion;
    host::CHostRestrictions restrictions;
    threading::CThreadPackage first{ { thread_ids::executive, module_ids::executive }, conversion };
    threading::CThreadPackage second{ { thread_ids::rendering, module_ids::rendering }, conversion };
    host::SClientRecord a{ module_ids::executive, 11u, &first }, b{ module_ids::rendering, 22u, &second };
    threading::CThreadContext context_a{ threading::SDirectConnectionTestAccess::resources(first) };
    threading::CThreadContext context_b{ threading::SDirectConnectionTestAccess::resources(second) };
    connections::CClient client_a{ context_a, module_ids::executive, policies, 2u };
    connections::CClient client_b{ context_b, module_ids::rendering, policies, 2u };
    host::CDirectConnections coordinator;

    explicit SFixture(tests::TTestContext& ctx) : test{ ctx }
    {
        a.phase = b.phase = host::EClientPhase::ready;
        a.host_execution.instance = a.instance;
        b.host_execution.instance = b.instance;
        TEST_EXPECT(test, threading::SDirectConnectionTestAccess::initialise(first, a.host_execution));
        TEST_EXPECT(test, threading::SDirectConnectionTestAccess::initialise(second, b.host_execution));
        TEST_EXPECT(test, (client_a.register_service() && client_b.register_service()));
        drain_host(a, &b);
        drain_host(b, &a);
        TEST_EXPECT(test, ((a.direct_connections == host::EDirectConnectionState::available) &&
            (b.direct_connections == host::EDirectConnectionState::available)));
    }
    ~SFixture()
    {
        context_a.mark_exited(); context_b.mark_exited();
        a.terminal_before_drain = b.terminal_before_drain = true;
        a.incoming_drained = b.incoming_drained = true;
        coordinator.close_all(); coordinator.progress();
        TEST_EXPECT(test, coordinator.empty());
        TEST_EXPECT(test, (first.shutdown() && second.shutdown()));
    }
    void drain_host(host::SClientRecord& source, host::SClientRecord* const peer)
    {
        threading::CErasedPodMsg message;
        while (source.thread->read(message))
        {
            connections::Register registration;
            connections::Request request;
            connections::Decision decision;
            connections::Ready ready;
            connections::Close close;
            connections::Quiescent quiescent;
            if (message.copy_payload_to(registration))
            {
                coordinator.register_client(source, registration);
            }
            else if (message.copy_payload_to(request))
            {
                TEST_EXPECT(test, source.reserve_replies(1u));
                coordinator.request(source, peer, request, message.query_async_slot(), restrictions.reader());
            }
            else if (message.copy_payload_to(decision))
            {
                coordinator.decision(source, decision);
            }
            else if (message.copy_payload_to(ready))
            {
                coordinator.ready(source, ready);
            }
            else if (message.copy_payload_to(close))
            {
                coordinator.close(source, close);
            }
            else if (message.copy_payload_to(quiescent))
            {
                coordinator.quiescent(source, quiescent);
            }
            else TEST_EXPECT(test, false);
        }
    }
    void deliver(threading::CThreadContext& context, connections::CClient& client)
    {
        threading::CErasedPodMsg message;
        while (context.read(message))
        {
            TEST_EXPECT(test, client.handle(message));
        }
        client.progress();
    }
    void step(const bool deliver_a = true, const bool deliver_b = true)
    {
        drain_host(a, &b); drain_host(b, &a);
        coordinator.progress();
        if (deliver_a) deliver(context_a, client_a);
        if (deliver_b) deliver(context_b, client_b);
    }
    SToken establish(const bool reciprocal = false)
    {
        TEST_EXPECT(test, client_a.open(module_ids::rendering, 1u, 7));
        if (reciprocal)
        {
            TEST_EXPECT(test, client_b.open(module_ids::executive, 1u, 9));
        }
        for (unsigned i = 0u; i < 5u; ++i) step();
        const auto token = client_a.active(module_ids::rendering);
        TEST_EXPECT(test, (token.value && client_b.active(token)));
        connections::Result result; std::int32_t slot = -1;
        TEST_EXPECT(test, (client_a.take_result(result, slot) && (slot == 7) && (result.status == EStatus::success) && (result.token.value == token.value)));
        if (reciprocal)
        {
            TEST_EXPECT(test, (client_b.take_result(result, slot) && (slot == 9) && (result.status == EStatus::success) && (result.token.value == token.value)));
        }
        return token;
    }
    void finish(const SToken token)
    {
        client_a.close(token); client_a.progress();
        step();
        TEST_EXPECT(test, (client_a.closing(token) && client_b.closing(token)));
        TEST_EXPECT(test, (client_a.quiesce(token) && client_b.quiesce(token)));
        step(); step();
        TEST_EXPECT(test, (coordinator.empty() && client_a.idle() && client_b.idle()));
        TEST_EXPECT(test, (!a.reserved_replies && !b.reserved_replies));
        TEST_EXPECT(test, (!threading::SDirectConnectionTestAccess::pins(first) && !threading::SDirectConnectionTestAccess::pins(second)));
    }
};

static void reciprocal_and_data(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    const auto token = fixture.establish(true);
    const auto request = threading::make_pod_message(31, modules::RunTestsRequest{ module_ids::rendering });
    const auto reply = threading::make_pod_message(31, modules::RunTestsResult{ module_ids::rendering, modules::ETestRunStatus::success });
    TEST_EXPECT(ctx, (fixture.client_a.post(token, reply) == connections::EPost::invalid_message));
    TEST_EXPECT(ctx, (fixture.client_b.post(token, request) == connections::EPost::invalid_message));
    TEST_EXPECT(ctx, (fixture.client_a.post(token, request) == connections::EPost::queued));
    threading::CErasedPodMsg received;
    TEST_EXPECT(ctx, ((fixture.client_b.read(token, received) == connections::ERead::record) && (received.query_async_slot() == 31)));
    TEST_EXPECT(ctx, (fixture.client_b.post(token, reply) == connections::EPost::queued));
    TEST_EXPECT(ctx, (fixture.client_a.read(token, received) == connections::ERead::record));
    fixture.finish(token);
    TEST_EXPECT(ctx, (fixture.client_a.post(token, request) == connections::EPost::inactive));
    const auto successor = fixture.establish();
    TEST_EXPECT(ctx, (successor.value != token.value));
    fixture.coordinator.close(fixture.a, { token });
    fixture.coordinator.quiescent(fixture.a, { token });
    TEST_EXPECT(ctx, fixture.client_a.active(successor));
    fixture.finish(successor);
}

static void capacity_and_unread_close(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    const auto token = fixture.establish();
    host::SDirectConnectionTestAccess::limit(fixture.coordinator, token);
    const auto message = threading::make_pod_message(0, modules::RunTestsRequest{ module_ids::rendering });
    for (unsigned i = 0u; i < 32u; ++i)
    {
        TEST_EXPECT(ctx, (fixture.client_a.post(token, message) == connections::EPost::queued));
    }
    TEST_EXPECT(ctx, (fixture.client_a.post(token, message) == connections::EPost::full));
    threading::CErasedPodMsg received;
    TEST_EXPECT(ctx, (fixture.client_b.read(token, received) == connections::ERead::record));
    TEST_EXPECT(ctx, (fixture.client_a.post(token, message) == connections::EPost::queued));
    fixture.finish(token); // Unread POD is deliberately discarded after quiescence.
}

static void capacity_wake_ordering(tests::TTestContext& ctx, const bool before_clear)
{
    SFixture fixture{ ctx };
    const auto token = fixture.establish();
    host::SDirectConnectionTestAccess::limit(fixture.coordinator, token);
    const auto request = threading::make_pod_message(0, modules::RunTestsRequest{ module_ids::rendering });
    const auto retained = threading::make_pod_message(99, modules::RunTestsRequest{ module_ids::rendering });
    const auto reply = threading::make_pod_message(1, modules::RunTestsResult{ module_ids::rendering, modules::ETestRunStatus::success });
    for (unsigned index = 0u; index < 32u; ++index)
    {
        TEST_EXPECT(ctx, (fixture.client_a.post(token, request) == connections::EPost::queued));
    }
    //  An already-consumed reply leaves a pending hint, with no incoming data
    //  to keep the producer awake while it is waiting for outgoing capacity.
    threading::CErasedPodMsg received;
    TEST_EXPECT(ctx, (fixture.client_b.post(token, reply) == connections::EPost::queued));
    TEST_EXPECT(ctx, (fixture.client_a.read(token, received) == connections::ERead::record));
    TEST_EXPECT(ctx, !fixture.client_a.has_data());
    TEST_EXPECT(ctx, (fixture.client_a.post(token, retained) == connections::EPost::full));
    const auto epoch = fixture.context_a.observe_epoch();
    if (before_clear)
    {
        TEST_EXPECT(ctx, (fixture.client_b.read(token, received) == connections::ERead::record));
        TEST_EXPECT(ctx, (fixture.context_a.observe_epoch() == epoch)); // Capacity hint coalesced.
        fixture.client_a.clear_notifications();
    }
    else
    {
        fixture.client_a.clear_notifications();
        TEST_EXPECT(ctx, (fixture.client_a.post(token, retained) == connections::EPost::full));
        TEST_EXPECT(ctx, (fixture.client_b.read(token, received) == connections::ERead::record));
        TEST_EXPECT(ctx, (fixture.context_a.observe_epoch() != epoch));
        threading::CWaitDeadline deadline;
        TEST_EXPECT(ctx, (deadline.set_after_milliseconds(100u).code == platform::threading::EWaitError::none));
        TEST_EXPECT(ctx, (fixture.context_a.wait_for_new_epoch(epoch, deadline).status == threading::EWaitStatus::satisfied));
        fixture.client_a.clear_notifications();
    }
    //  A durable retry follows clearing even when the epoch did not change.
    TEST_EXPECT(ctx, (fixture.client_a.post(token, retained) == connections::EPost::queued));
    unsigned records = 0u, retained_records = 0u;
    while (connections::was_read(fixture.client_b.read(token, received)))
    {
        ++records;
        if (received.query_async_slot() == 99)
        {
            ++retained_records;
        }
    }
    TEST_EXPECT(ctx, ((records == 32u) && (retained_records == 1u)));
    fixture.finish(token);
}

static void partial_installation(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, fixture.client_a.open(module_ids::rendering, 1u, 7));
    fixture.step(false, true); // Offer accepted; initiator has seen no token.
    fixture.step(false, true); // Both installations queued, only B has consumed one.
    TEST_EXPECT(ctx, !fixture.client_b.active(module_ids::executive).value);
    fixture.client_a.close_all();
    fixture.deliver(fixture.context_a, fixture.client_a); // Late install cannot reopen A.
    fixture.step();
    const auto a_token = fixture.client_a.next_closing();
    const auto b_token = fixture.client_b.next_closing();
    TEST_EXPECT(ctx, (a_token.value && (a_token.value == b_token.value)));
    TEST_EXPECT(ctx, (!fixture.client_a.active(a_token) && !fixture.client_b.active(a_token)));
    TEST_EXPECT(ctx, fixture.client_a.quiesce(a_token));
    fixture.step();
    TEST_EXPECT(ctx, (!fixture.coordinator.empty() && (threading::SDirectConnectionTestAccess::pins(fixture.first) == 1u)));
    TEST_EXPECT(ctx, fixture.client_b.quiesce(b_token));
    fixture.step();
    connections::Result result; std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.client_a.take_result(result, slot) && (result.status == EStatus::cancelled)));
    TEST_EXPECT(ctx, (fixture.coordinator.empty() && fixture.client_a.idle() && fixture.client_b.idle()));
}

static void crossed_versions(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, fixture.client_a.open(module_ids::rendering, 1u, 7));
    TEST_EXPECT(ctx, fixture.client_b.open(module_ids::executive, 2u, 9));
    for (unsigned i = 0u; i < 5u; ++i) fixture.step();
    connections::Result result; std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.client_b.take_result(result, slot) && (slot == 9) && (result.status == EStatus::version_mismatch) && !result.token.value));
    TEST_EXPECT(ctx, (fixture.client_a.take_result(result, slot) && (result.status == EStatus::success)));
    const auto token = fixture.client_a.active(module_ids::rendering);
    TEST_EXPECT(ctx, (token.value && fixture.client_b.active(token)));
    fixture.finish(token);
}

static void unsupported_and_busy(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.b.direct_connections = host::EDirectConnectionState::unregistered;
    TEST_EXPECT(ctx, fixture.client_a.open(module_ids::rendering, 1u, 7));
    fixture.step();
    connections::Result result; std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.client_a.take_result(result, slot) && (result.status == EStatus::unsupported)));
    TEST_EXPECT(ctx, (fixture.client_a.idle() && fixture.coordinator.empty()));
    fixture.coordinator.register_client(fixture.b, {});
    TEST_EXPECT(ctx, fixture.client_a.open(module_ids::rendering, 1u, 7));
    fixture.drain_host(fixture.a, &fixture.b);
    TEST_EXPECT(ctx, fixture.a.reserve_replies(1u));
    fixture.coordinator.request(fixture.a, &fixture.b, { module_ids::rendering, 1u }, 99, fixture.restrictions.reader());
    threading::CErasedPodMsg refusal;
    TEST_EXPECT(ctx, (fixture.context_a.read(refusal) && refusal.copy_payload_to(result) && (result.status == EStatus::busy) && !result.token.value));
    for (unsigned i = 0u; i < 5u; ++i) fixture.step();
    TEST_EXPECT(ctx, (fixture.client_a.take_result(result, slot) && (result.status == EStatus::success)));
    fixture.finish(result.token);
}

static void retirement_admission(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.coordinator.close_client(fixture.b); // No connection exists yet.
    fixture.coordinator.register_client(fixture.b, {});
    TEST_EXPECT(ctx, (fixture.b.direct_connections == host::EDirectConnectionState::closing));
    TEST_EXPECT(ctx, fixture.client_a.open(module_ids::rendering, 1u, 7));
    TEST_EXPECT(ctx, fixture.client_b.open(module_ids::executive, 1u, 9));
    fixture.step();
    connections::Result result; std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.client_a.take_result(result, slot) && (result.status == EStatus::closing) && !result.token.value));
    TEST_EXPECT(ctx, (fixture.client_b.take_result(result, slot) && (result.status == EStatus::closing) && !result.token.value));
    TEST_EXPECT(ctx, (fixture.coordinator.empty() && (fixture.a.reserved_replies == 0u) && (fixture.b.reserved_replies == 0u)));
    host::SClientRecord replacement{ module_ids::rendering, 23u, &fixture.second };
    fixture.coordinator.register_client(replacement, {});
    TEST_EXPECT(ctx, (replacement.direct_connections == host::EDirectConnectionState::available));
}

static void delayed_reciprocal(tests::TTestContext& ctx, const bool after_retirement, const bool accept_successor = false)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, fixture.client_a.open(module_ids::rendering, 1u, 7));
    TEST_EXPECT(ctx, fixture.client_b.open(module_ids::executive, 1u, 9));
    threading::CErasedPodMsg held;
    connections::Request request;
    TEST_EXPECT(ctx, (fixture.second.read(held) && held.copy_payload_to(request)));
    for (unsigned i = 0u; i < 5u; ++i) fixture.step();
    connections::Result result; std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.client_a.take_result(result, slot) && (result.status == EStatus::success)));
    const auto token = result.token;
    fixture.client_a.close(token); fixture.client_a.progress();
    fixture.drain_host(fixture.a, &fixture.b);
    if (!after_retirement)
    {
        TEST_EXPECT(ctx, fixture.b.reserve_replies(1u));
        fixture.coordinator.request(fixture.b, &fixture.a, request, 9, fixture.restrictions.reader());
        fixture.coordinator.progress();
        threading::CErasedPodMsg refusal;
        TEST_EXPECT(ctx, (fixture.context_b.read(refusal) && fixture.client_b.handle(refusal)));
        TEST_EXPECT(ctx, (fixture.client_b.take_result(result, slot) && (result.status == EStatus::closing) && !result.token.value));
        TEST_EXPECT(ctx, (fixture.client_b.active(token) && !fixture.coordinator.empty()));
    }
    fixture.step();
    TEST_EXPECT(ctx, (fixture.client_b.next_closing().value == token.value));
    TEST_EXPECT(ctx, fixture.client_a.quiesce(token));
    fixture.step();
    TEST_EXPECT(ctx, !fixture.coordinator.empty());
    TEST_EXPECT(ctx, fixture.client_b.quiesce(token));
    fixture.step();
    TEST_EXPECT(ctx, fixture.coordinator.empty());
    if (after_retirement)
    {
        TEST_EXPECT(ctx, !fixture.client_b.idle());
        if (!accept_successor)
        {
            fixture.coordinator.close_admission();
        }
        TEST_EXPECT(ctx, fixture.b.reserve_replies(1u));
        fixture.coordinator.request(fixture.b, &fixture.a, request, 9, fixture.restrictions.reader());
        for (unsigned i = 0u; i < 5u; ++i) fixture.step();
        TEST_EXPECT(ctx, (fixture.client_b.take_result(result, slot) && (slot == 9) &&
            (result.status == (accept_successor ? EStatus::success : EStatus::closing))));
        if (accept_successor)
        {
            TEST_EXPECT(ctx, ((result.token.value != token.value) && fixture.client_a.active(result.token)));
            fixture.finish(result.token);
        }
    }
    TEST_EXPECT(ctx, (fixture.client_a.idle() && fixture.client_b.idle()));
}

static void submission_poison(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, tests::allocate_with_failure, tests::deallocate_test_memory, system_ids::host };
    memory::CMemoryContext memory{ allocator, system_ids::host };
    tests::TMemoryContextScope scope{ &memory };
    SFixture fixture{ ctx };
    auto& pipe = threading::SDirectConnectionTestAccess::resources(fixture.first).worker_to_host_msgs;
    pipe.deallocate();
    TEST_EXPECT(ctx, pipe.initialise_growable(32u, 128u));
    for (unsigned i = 0u; i < 32u; ++i)
    {
        TEST_EXPECT(ctx, fixture.context_a.post(threading::make_pod_message(-1, connections::Register{})));
    }
    //  Growth publishes the request before its staged-buffer allocation fails.
    allocation.fail_on = allocation.attempts + 1u;
    TEST_EXPECT(ctx, !fixture.client_a.open(module_ids::rendering, 1u, 7));
    TEST_EXPECT(ctx, (allocation.failed && pipe.posting_poisoned() && fixture.client_a.failed()));
    TEST_EXPECT(ctx, (!fixture.client_a.idle() && !fixture.client_a.needs_progress()));
    TEST_EXPECT(ctx, !fixture.client_a.open(module_ids::rendering, 1u, 8));
    threading::CErasedPodMsg message;
    unsigned requests = 0u;
    while (fixture.first.read(message))
    {
        if (message.is_payload_a<connections::Request>())
        {
            ++requests;
        }
    }
    TEST_EXPECT(ctx, (requests == 1u)); // No automatic retransmission after ambiguous failure.
}

static void activation_and_timeout(tests::TTestContext& ctx, const bool expire)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, fixture.client_a.open(module_ids::rendering, 1u, 7));
    fixture.step(); // Offer.
    fixture.step(); // Installations and readiness submissions.
    threading::CErasedPodMsg pending;
    connections::Ready ready;
    TEST_EXPECT(ctx, (fixture.second.read(pending) && pending.copy_payload_to(ready)));
    fixture.drain_host(fixture.a, &fixture.b);
    fixture.coordinator.progress();
    fixture.deliver(fixture.context_a, fixture.client_a);
    TEST_EXPECT(ctx, (!fixture.client_a.active(ready.token) && !fixture.client_b.active(ready.token)));
    if (expire)
    {
        host::SDirectConnectionTestAccess::expire(fixture.coordinator, ready.token, fixture.restrictions.reader());
        fixture.step();
        TEST_EXPECT(ctx, (!fixture.coordinator.empty() && fixture.client_a.closing(ready.token)));
        connections::Result result; std::int32_t slot = -1;
        TEST_EXPECT(ctx, (fixture.client_a.take_result(result, slot) && (result.status == EStatus::timed_out)));
        TEST_EXPECT(ctx, fixture.client_a.quiesce(ready.token));
        fixture.step();
        TEST_EXPECT(ctx, !fixture.coordinator.empty());
        TEST_EXPECT(ctx, fixture.client_b.quiesce(ready.token));
        fixture.step();
        TEST_EXPECT(ctx, fixture.coordinator.empty());
    }
    else
    {
        fixture.coordinator.ready(fixture.b, ready);
        fixture.coordinator.progress();
        fixture.deliver(fixture.context_a, fixture.client_a);
        TEST_EXPECT(ctx, (fixture.client_a.active(ready.token) && !fixture.client_b.active(ready.token)));
        const auto message = threading::make_pod_message(4, modules::RunTestsRequest{ module_ids::rendering });
        TEST_EXPECT(ctx, (fixture.client_a.post(ready.token, message) == connections::EPost::queued));
        threading::CErasedPodMsg received;
        TEST_EXPECT(ctx, (fixture.client_b.read(ready.token, received) == connections::ERead::inactive));
        fixture.deliver(fixture.context_b, fixture.client_b);
        TEST_EXPECT(ctx, (fixture.client_b.read(ready.token, received) == connections::ERead::record));
        connections::Result result; std::int32_t slot = -1;
        TEST_EXPECT(ctx, (fixture.client_a.take_result(result, slot) && (result.status == EStatus::success)));
        fixture.finish(ready.token);
    }
}

static void wake_failure(tests::TTestContext& ctx, const bool after_read)
{
    SFixture fixture{ ctx };
    const auto token = fixture.establish();
    auto& failed_target = threading::SDirectConnectionTestAccess::resources(after_read ? fixture.first : fixture.second);
    failed_target.wait_predicate.release_control();
    const auto message = threading::make_pod_message(4, modules::RunTestsRequest{ module_ids::rendering });
    const auto posted = fixture.client_a.post(token, message);
    TEST_EXPECT(ctx, (posted == (after_read ? connections::EPost::queued : connections::EPost::queued_wake_failed)));
    threading::CErasedPodMsg received;
    const auto read = fixture.client_b.read(token, received);
    TEST_EXPECT(ctx, (read == (after_read ? connections::ERead::record_wake_failed : connections::ERead::record)));
    TEST_EXPECT(ctx, (received.query_async_slot() == 4));
    fixture.finish(token);
}

template<typename Predicate> static bool await(const Predicate predicate)
{
    platform::threading::CWaitDeadline deadline;
    if (deadline.set_after_milliseconds(5000u).code != platform::threading::EWaitError::none)
    {
        return false;
    }
    while (!predicate())
    {
        std::uint64_t remaining = 0u;
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            return false;
        }
        platform::threading::processor_relax();
    }
    return true;
}

struct SNativeState
{
    std::atomic<bool> terminal{ false }, published{ false }, signal{ false }, signalled{ false }, finish{ false };
    threading::SNotificationEndpoint endpoint;
};

static std::uint32_t MV_STD_ABI_CALL terminal_worker(void* const data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    auto& state = *static_cast<SNativeState*>(resources.config.prepare_context);
    threading::CThreadContext context{ resources };
    context.startup(); context.mark_running();
    while (!state.terminal.load(std::memory_order_acquire))
    {
        platform::threading::processor_relax();
    }
    context.mark_exited();
    state.published.store(true, std::memory_order_release);
    while (!state.finish.load(std::memory_order_acquire))
    {
        if (state.signal.load(std::memory_order_acquire) && !state.signalled.load(std::memory_order_relaxed))
        {
            state.signalled.store(state.endpoint.signal(), std::memory_order_release);
        }
        platform::threading::processor_relax();
    }
    return 0u;
}

static void native_join_retention(tests::TTestContext& ctx)
{
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    SNativeState state;
    threading::CThreadPackage first{ { thread_ids::executive, module_ids::executive,
        platform::threading::EThreadPriority::Normal, &terminal_worker, nullptr, &state }, conversion };
    threading::CThreadPackage second{ { thread_ids::rendering, module_ids::rendering }, conversion };
    host::SClientRecord a{ module_ids::executive, 31u, &first }, b{ module_ids::rendering, 32u, &second };
    a.phase = b.phase = host::EClientPhase::ready;
    a.direct_connections = b.direct_connections = host::EDirectConnectionState::available;
    b.host_execution.instance = b.instance;
    TEST_EXPECT(ctx, threading::SDirectConnectionTestAccess::initialise(second, b.host_execution));
    if (!first.startup())
    {
        TEST_EXPECT(ctx, false);
        return;
    }
    threading::CThreadContext context_b{ threading::SDirectConnectionTestAccess::resources(second) };
    connections::CClient client_b{ context_b, module_ids::rendering, policies, 2u };
    host::CHostRestrictions restrictions;
    host::CDirectConnections coordinator;
    TEST_EXPECT(ctx, a.reserve_replies(1u));
    coordinator.request(a, &b, { module_ids::rendering, 1u }, 7, restrictions.reader());
    coordinator.progress();
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, context_b.read(message));
    connections::Offer offer;
    TEST_EXPECT(ctx, (message.copy_payload_to(offer) && client_b.handle(message)));
    connections::Decision decision;
    TEST_EXPECT(ctx, (second.read(message) && message.copy_payload_to(decision)));
    coordinator.decision(b, decision);
    coordinator.progress();
    while (context_b.read(message))
    {
        TEST_EXPECT(ctx, client_b.handle(message));
    }
    state.endpoint = host::SDirectConnectionTestAccess::notification(coordinator, offer.token);
    threading::SDirectConnectionTestAccess::fail_join(first, true);
    state.terminal.store(true, std::memory_order_release);
    TEST_EXPECT(ctx, await([&] { return state.published.load(std::memory_order_acquire); }));
    a.terminal_before_drain = a.incoming_drained = true;
    coordinator.progress();
    while (context_b.read(message))
    {
        TEST_EXPECT(ctx, client_b.handle(message));
    }
    TEST_EXPECT(ctx, client_b.quiesce(offer.token));
    while (second.read(message))
    {
        connections::Quiescent quiescent;
        if (message.copy_payload_to(quiescent))
        {
            coordinator.quiescent(b, quiescent);
        }
    }
    coordinator.progress();
    TEST_EXPECT(ctx, (!coordinator.empty() && (threading::SDirectConnectionTestAccess::pins(first) == 1u) &&
        (threading::SDirectConnectionTestAccess::pins(second) == 1u)));
    TEST_EXPECT(ctx, (!first.join_terminated_execution() && !first.shutdown()));
    state.signal.store(true, std::memory_order_release);
    TEST_EXPECT(ctx, await([&] { return state.signalled.load(std::memory_order_acquire); }));
    state.finish.store(true, std::memory_order_release);
    threading::SDirectConnectionTestAccess::fail_join(first, false);
    TEST_EXPECT(ctx, first.join_terminated_execution());
    TEST_EXPECT(ctx, (threading::SDirectConnectionTestAccess::pins(first) == 1u));
    coordinator.progress();
    TEST_EXPECT(ctx, coordinator.empty());
    while (context_b.read(message))
    {
        TEST_EXPECT(ctx, client_b.handle(message));
    }
    context_b.mark_exited();
    TEST_EXPECT(ctx, (first.shutdown() && second.shutdown()));
}

}   //  namespace

void run_direct_connection_tests(tests::TTestContext& ctx)
{
    reciprocal_and_data(ctx);
    capacity_and_unread_close(ctx);
    capacity_wake_ordering(ctx, true);
    capacity_wake_ordering(ctx, false);
    partial_installation(ctx);
    crossed_versions(ctx);
    unsupported_and_busy(ctx);
    retirement_admission(ctx);
    delayed_reciprocal(ctx, false);
    delayed_reciprocal(ctx, true);
    delayed_reciprocal(ctx, true, true);
    submission_poison(ctx);
    activation_and_timeout(ctx, false);
    activation_and_timeout(ctx, true);
    wake_failure(ctx, false);
    wake_failure(ctx, true);
    native_join_retention(ctx);
}
