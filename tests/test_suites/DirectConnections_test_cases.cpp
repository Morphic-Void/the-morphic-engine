
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DirectConnections_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    05 Nov 26
//
//  Deterministic service/Host interleavings using the production pipes and helpers.

#include "framework/threading/CThreadContext.hpp"
#include "tests/support/thread_helpers.hpp"
#include "framework/system/system_ids.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/communications/direct_client.hpp"
#include "host/runtime/direct_connections.hpp"
#include "host/runtime/host_restrictions.hpp"
#include "framework/module/module_messages.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/support/test_allocator.hpp"
#include "platform/threading/processor_relax.hpp"
#include "platform/threading/thread_lifetime.hpp"

#include <cstring>

namespace
{
using connections::EStatus;
using connections::SToken;
static const connections::SBulkPolicy policies[]{
    { connections::contract(module_ids::executive, module_ids::rendering, 1u), { 129u, module_ids::executive } },
    { connections::contract(module_ids::executive, module_ids::rendering, 1u), { 257u, module_ids::executive } },
    { connections::contract(module_ids::executive, module_ids::rendering, 1u), { 129u, module_ids::rendering } },
    { connections::contract(module_ids::executive, module_ids::rendering, 2u), { 129u, module_ids::executive } }
};

//  Assertions which focus on data operations still use the common receiver.
static connections::ERead read_direct(connections::CClient& client, const SToken token, threading::CErasedPodMsg& message)
{
    if (!client.active(token))
    {
        return connections::ERead::inactive;
    }
    connections::SReceived received;
    if (!client.receive(received))
    {
        return client.active(token) ? connections::ERead::empty : connections::ERead::inactive;
    }
    if (received.source.is_host() || (received.source.connection.value != token.value))
    {
        return connections::ERead::invalid_message;
    }
    message = received.message;
    return client.active(token) ? connections::ERead::record : connections::ERead::record_wake_failed;
}
static constexpr std::uint32_t policy_count = static_cast<std::uint32_t>(std::size(policies));

struct SFixture
{
    tests::TTestContext& test;
    platform::system::CPerfCountConversion conversion;
    host::CHostRestrictions restrictions;
    threading::CThreadPackage first{ { thread_ids::executive, module_ids::executive }, conversion };
    threading::CThreadPackage second{ { thread_ids::rendering, module_ids::rendering }, conversion };
    host::SClientRecord a{ module_ids::executive, 11u, &first, modules::SModuleInstance{ 1011u } }, b{ module_ids::rendering, 22u, &second, modules::SModuleInstance{ 1022u } };
    threading::CThreadContext context_a{ threading::SDirectConnectionTestAccess::resources(first) };
    threading::CThreadContext context_b{ threading::SDirectConnectionTestAccess::resources(second) };
    connections::CClient client_a{ context_a, policies, policy_count };
    connections::CClient client_b{ context_b, policies, policy_count };
    host::CDirectConnections coordinator;
    tests::SDirectResultObservations results_a, results_b;

    explicit SFixture(tests::TTestContext& ctx) : test{ ctx }
    {
        a.phase = b.phase = host::EClientPhase::ready;
        a.host_execution.instance = a.instance;
        b.host_execution.instance = b.instance;
        TEST_EXPECT(test, (a.initialise() && b.initialise()));
        TEST_EXPECT(test, threading::SDirectConnectionTestAccess::initialise(first, a.host_execution));
        TEST_EXPECT(test, threading::SDirectConnectionTestAccess::initialise(second, b.host_execution));
        TEST_EXPECT(test, (client_a.enable() && client_b.enable()));
        drain_host(a, &b);
        drain_host(b, &a);
        deliver(context_a, client_a);
        deliver(context_b, client_b);
        TEST_EXPECT(test, ((a.direct_connections == host::EDirectConnectionState::available) &&
            (b.direct_connections == host::EDirectConnectionState::available)));
    }
    ~SFixture()
    {
        threading::SDirectConnectionTestAccess::end_execution(first);
        threading::SDirectConnectionTestAccess::end_execution(second);
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
                TEST_EXPECT(test, source.reserve_replies(1u));
                coordinator.register_client(source, registration, message.query_async_slot());
            }
            else if (message.copy_payload_to(request))
            {
                TEST_EXPECT(test, source.reserve_replies(1u));
                coordinator.request(source, &source, peer, request, message.query_async_slot(), restrictions.reader());
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
        (void)context;
        connections::SReceived received;
        while (client.receive(received, false))
        {
            TEST_EXPECT(test, (received.source.is_host() && connections::is_control(received.message)));
            (void)((&client == &client_a) ? results_a : results_b).observe(received.message);
            connections::Offer offer;
            if (received.message.copy_payload_to(offer) && !offer.bulk.is_bulk())
            {
                (void)client.decide(offer.token, EStatus::success);
            }
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
        TEST_EXPECT(test, client_a.open(modules::test_connections::address(module_ids::rendering), 7));
        if (reciprocal)
        {
            TEST_EXPECT(test, client_b.open(modules::test_connections::address(module_ids::executive), 9));
        }
        for (unsigned i = 0u; i < 5u; ++i) step();
        const auto token = client_a.active(modules::test_connections::address(module_ids::rendering));
        TEST_EXPECT(test, (token.value && client_b.active(token)));
        connections::Result result; std::int32_t slot = -1;
        TEST_EXPECT(test, (results_a.read(result, slot) && (slot == 7) && (result.status == EStatus::success) && (result.token.value == token.value)));
        if (reciprocal)
        {
            TEST_EXPECT(test, (results_b.read(result, slot) && (slot == 9) && (result.status == EStatus::success) && (result.token.value == token.value)));
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
    TEST_EXPECT(ctx, (fixture.client_a.post(token, request) == connections::EPost::queued));
    threading::CErasedPodMsg received;
    TEST_EXPECT(ctx, ((read_direct(fixture.client_b, token, received) == connections::ERead::record) && (received.query_async_slot() == 31)));
    TEST_EXPECT(ctx, (fixture.client_b.post(token, reply) == connections::EPost::queued));
    TEST_EXPECT(ctx, (read_direct(fixture.client_a, token, received) == connections::ERead::record));
    fixture.finish(token);
    TEST_EXPECT(ctx, (fixture.client_a.post(token, request) == connections::EPost::no_pipe));
    const auto successor = fixture.establish();
    TEST_EXPECT(ctx, (successor.value != token.value));
    fixture.coordinator.close(fixture.a, { token });
    fixture.coordinator.quiescent(fixture.a, { token });
    TEST_EXPECT(ctx, fixture.client_a.active(successor));
    fixture.finish(successor);
}

static SToken establish_bulk(SFixture& fixture, const bool consumer_first, const bool reciprocal)
{
    TEST_EXPECT(fixture.test, (consumer_first ? fixture.client_b.open_bulk(module_ids::executive, 1u, 9, 129u, false) :
        fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, 129u, true)));
    if (reciprocal)
    {
        TEST_EXPECT(fixture.test, (consumer_first ? fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, 129u, true) :
            fixture.client_b.open_bulk(module_ids::executive, 1u, 9, 129u, false)));
    }
    for (unsigned index = 0u; index < 5u; ++index) fixture.step();
    const SToken token = fixture.client_a.active_bulk(module_ids::rendering);
    TEST_EXPECT(fixture.test, (token.value && fixture.client_b.active(token)));
    connections::Result result;
    std::int32_t slot = -1;
    if (!consumer_first || reciprocal)
    {
        TEST_EXPECT(fixture.test, (fixture.results_a.read(result, slot) && (slot == 7) &&
            (result.status == EStatus::success) && (result.token.value == token.value)));
    }
    if (consumer_first || reciprocal)
    {
        TEST_EXPECT(fixture.test, (fixture.results_b.read(result, slot) && (slot == 9) &&
            (result.status == EStatus::success) && (result.token.value == token.value)));
    }
    return token;
}

static void bulk_buffers(tests::TTestContext& ctx, const bool consumer_first, const bool reciprocal)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, !fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, 0u, true));
    TEST_EXPECT(ctx, !fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, UINT64_MAX, true));
    const SToken token = establish_bulk(fixture, consumer_first, reciprocal);
    TEST_EXPECT(ctx, !fixture.client_b.has_data());
    TEST_EXPECT(ctx, !fixture.client_b.discard_and_acquire(token));
    TEST_EXPECT(ctx, !fixture.client_b.consumer_data(token));
    TEST_EXPECT(ctx, !fixture.client_b.producer_data(token));
    TEST_EXPECT(ctx, !fixture.client_a.discard_and_acquire(token));
    TEST_EXPECT(ctx, (fixture.client_b.publish_bulk(token) == connections::EBulkPublish::inactive));
    const threading::CErasedPodMsg message = threading::make_pod_message(0, modules::RunTestsRequest{ module_ids::rendering });
    threading::CErasedPodMsg received;
    TEST_EXPECT(ctx, (fixture.client_a.post(token, message) == connections::EPost::no_pipe));
    TEST_EXPECT(ctx, (read_direct(fixture.client_b, token, received) == connections::ERead::empty));

    void* slots[3]{};
    for (unsigned index = 0u; index < 2u; ++index)
    {
        const memory::CMemoryView output = fixture.client_a.producer_data(token);
        TEST_EXPECT(ctx, ((output.bytes() == 129u) && (output.storage_alignment() == 128u)));
        if (!output) return;
        slots[index] = output.data();
        std::memset(output.data(), static_cast<int>(index + 1u), output.bytes());
        TEST_EXPECT(ctx, (fixture.client_a.publish_bulk(token) == connections::EBulkPublish::published));
    }
    TEST_EXPECT(ctx, fixture.client_b.has_data());
    fixture.client_b.clear_notifications(); //  Durable staged data survives a cleared hint.
    TEST_EXPECT(ctx, fixture.client_b.has_data());
    TEST_EXPECT(ctx, fixture.client_b.discard_and_acquire(token));
    const memory::CMemoryConstView held = fixture.client_b.consumer_data(token);
    TEST_EXPECT(ctx, ((held.bytes() == 129u) && (held.data() == slots[1])));
    if (!held) return;
    TEST_EXPECT(ctx, (*static_cast<const unsigned char*>(held.data()) == 2u));
    for (unsigned index = 0u; index < 32u; ++index)
    {
        const memory::CMemoryView output = fixture.client_a.producer_data(token);
        TEST_EXPECT(ctx, (output.data() != held.data()));
        TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(output.data()) & 127u) == 0u));
        if (!output) return;
        if (index == 1u) slots[2] = output.data();
        std::memset(output.data(), static_cast<int>(index + 3u), output.bytes());
        TEST_EXPECT(ctx, (fixture.client_a.publish_bulk(token) == connections::EBulkPublish::published));
        const unsigned char* const bytes = static_cast<const unsigned char*>(held.data());
        for (std::size_t byte = 0u; byte < held.bytes(); ++byte)
        {
            TEST_EXPECT(ctx, (bytes[byte] == 2u));
        }
    }
    TEST_EXPECT(ctx, ((slots[0] != slots[1]) && (slots[0] != slots[2]) && (slots[1] != slots[2])));
    TEST_EXPECT(ctx, fixture.client_b.discard_and_acquire(token));
    const memory::CMemoryConstView latest = fixture.client_b.consumer_data(token);
    TEST_EXPECT(ctx, (latest && (*static_cast<const unsigned char*>(latest.data()) == 34u)));
    TEST_EXPECT(ctx, !fixture.client_b.discard_and_acquire(token));
    TEST_EXPECT(ctx, !fixture.client_b.consumer_data(token));
    fixture.finish(token);
    TEST_EXPECT(ctx, !fixture.client_a.producer_data(token));
    TEST_EXPECT(ctx, (fixture.client_a.publish_bulk(token) == connections::EBulkPublish::inactive));
    const SToken successor = establish_bulk(fixture, !consumer_first, reciprocal);
    TEST_EXPECT(ctx, (successor.value != token.value));
    TEST_EXPECT(ctx, !fixture.client_b.has_data());
    fixture.finish(successor);
}

static void bulk_configuration_and_coexistence(tests::TTestContext& ctx, const bool wrong_producer)
{
    SFixture fixture{ ctx };
    const SToken pod = fixture.establish();
    TEST_EXPECT(ctx, fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, 129u, true));
    TEST_EXPECT(ctx, fixture.client_b.open_bulk(module_ids::executive, 1u, 9,
        (wrong_producer ? 129u : 257u), wrong_producer));
    for (unsigned index = 0u; index < 5u; ++index) fixture.step();
    connections::Result result;
    std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::success)));
    const SToken bulk = result.token;
    TEST_EXPECT(ctx, (fixture.results_b.read(result, slot) && (result.status == EStatus::configuration_mismatch)));
    TEST_EXPECT(ctx, (fixture.client_a.active(modules::test_connections::address(module_ids::rendering)).value == pod.value));
    TEST_EXPECT(ctx, (fixture.client_a.active_bulk(module_ids::rendering).value == bulk.value));
    const threading::CErasedPodMsg message = threading::make_pod_message(0, modules::RunTestsRequest{ module_ids::rendering });
    TEST_EXPECT(ctx, (fixture.client_a.post(pod, message) == connections::EPost::queued));
    threading::CErasedPodMsg received;
    TEST_EXPECT(ctx, (read_direct(fixture.client_b, pod, received) == connections::ERead::record));
    fixture.client_a.close(bulk);
    fixture.client_a.progress();
    fixture.step();
    TEST_EXPECT(ctx, (fixture.client_a.quiesce(bulk) && fixture.client_b.quiesce(bulk)));
    fixture.step();
    TEST_EXPECT(ctx, (fixture.client_a.active(pod) && fixture.client_b.active(pod)));
    fixture.finish(pod);
}

static void bulk_wake_failure(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    const SToken token = establish_bulk(fixture, false, false);
    const memory::CMemoryView output = fixture.client_a.producer_data(token);
    if (!output) return;
    std::memset(output.data(), 42, output.bytes());
    threading::SDirectConnectionTestAccess::resources(fixture.second).wait_predicate.release_control();
    TEST_EXPECT(ctx, (fixture.client_a.publish_bulk(token) == connections::EBulkPublish::published_wake_failed));
    TEST_EXPECT(ctx, fixture.client_b.discard_and_acquire(token));
    const memory::CMemoryConstView held = fixture.client_b.consumer_data(token);
    TEST_EXPECT(ctx, (held && (*static_cast<const unsigned char*>(held.data()) == 42u)));
    TEST_EXPECT(ctx, (fixture.client_a.publish_bulk(token) == connections::EBulkPublish::inactive));
    fixture.finish(token);
}

static void bulk_allocation_failure(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext memory{ allocator };
    tests::TMemoryContextScope scope{ &memory };
    {
        SFixture fixture{ ctx };
        TEST_EXPECT(ctx, fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, 129u, true));
        fixture.step(); //  Accept the offer before failing backing preparation.
        allocation.fail_on = allocation.attempts;
        for (unsigned index = 0u; index < 5u; ++index) fixture.step();
        connections::Result result;
        std::int32_t slot = -1;
        TEST_EXPECT(ctx, allocation.failed);
        TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::allocation_failed)));
        TEST_EXPECT(ctx, (fixture.coordinator.empty() && fixture.client_a.idle() && fixture.client_b.idle()));
        TEST_EXPECT(ctx, (!threading::SDirectConnectionTestAccess::pins(fixture.first) &&
            !threading::SDirectConnectionTestAccess::pins(fixture.second)));
    }
    TEST_EXPECT(ctx, memory.is_attribution_empty());
}

static void bulk_backing_ownership(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext owner{ allocator }, participant{ allocator };
    {
        tests::TMemoryContextScope scope{ &owner };
        threading::transports::CBulkBufferTransport bulk;
        TEST_EXPECT(ctx, !bulk.initialise(0u));
        TEST_EXPECT(ctx, !bulk.initialise(UINT64_MAX));
        TEST_EXPECT(ctx, !bulk.initialise(memory::k_byte_size_ceiling));
        TEST_EXPECT(ctx, bulk.initialise(129u));
        TEST_EXPECT(ctx, (bulk.memory_context() == &owner));
        TEST_EXPECT(ctx, (owner.get_live_allocated_bytes() == 768u));
        TEST_EXPECT(ctx, !bulk.initialise(257u));
        const std::size_t attempts = allocation.attempts;
        tests::TMemoryContextScope participant_scope{ &participant };
        for (unsigned index = 0u; index < 32u; ++index)
        {
            const memory::CMemoryView output = bulk.producer_data();
            TEST_EXPECT(ctx, (output.bytes() == 129u));
            if (!output) return;
            std::memset(output.data(), static_cast<int>(index), output.bytes());
            (void)bulk.publish();
            TEST_EXPECT(ctx, bulk.discard_and_acquire());
            const memory::CMemoryConstView input = bulk.consumer_data();
            TEST_EXPECT(ctx, (input && (*static_cast<const unsigned char*>(input.data()) == index)));
        }
        TEST_EXPECT(ctx, ((allocation.attempts == attempts) && participant.is_attribution_empty()));
    }
    TEST_EXPECT(ctx, (owner.is_attribution_empty() && participant.is_attribution_empty()));
}

struct SConcurrentBulk
{
    threading::transports::CBulkBufferTransport bulk;
    std::atomic<bool> done{ false };
    static std::uint32_t MV_STD_ABI_CALL write(void* const context) noexcept
    {
        SConcurrentBulk& shared = *static_cast<SConcurrentBulk*>(context);
        for (unsigned index = 1u; index <= 20000u; ++index)
        {
            const memory::CMemoryView output = shared.bulk.producer_data();
            std::memset(output.data(), static_cast<int>(index & 255u), output.bytes());
            (void)shared.bulk.publish();
        }
        shared.done.store(true, std::memory_order_release);
        return 0u;
    }
};

static void bulk_concurrent_exchange(tests::TTestContext& ctx)
{
    SConcurrentBulk shared;
    TEST_EXPECT(ctx, shared.bulk.initialise(129u));
    if (!shared.bulk.capacity()) return;
    platform::threading::CThread producer;
    const bool created = producer.create(&SConcurrentBulk::write, &shared);
    TEST_EXPECT(ctx, created);
    if (!created) return;
    bool consistent = true;
    unsigned acquired = 0u;
    do
    {
        if (shared.bulk.discard_and_acquire())
        {
            ++acquired;
            const memory::CMemoryConstView input = shared.bulk.consumer_data();
            const unsigned char* const bytes = static_cast<const unsigned char*>(input.data());
            const unsigned char expected = bytes[0];
            platform::threading::processor_relax();
            for (std::size_t byte = 0u; byte < input.bytes(); ++byte)
            {
                if (bytes[byte] != expected) consistent = false;
            }
        }
        else
        {
            platform::threading::processor_relax();
        }
    } while (!shared.done.load(std::memory_order_acquire) || shared.bulk.has_staged_data());
    TEST_EXPECT(ctx, producer.join_and_close());
    TEST_EXPECT(ctx, (consistent && (acquired != 0u)));
}

static void bulk_request_validation(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    const connections::SBulkConfiguration invalid[]{
        { 0u, module_ids::executive },
        { UINT64_MAX, module_ids::executive },
        { memory::k_byte_size_ceiling, module_ids::executive },
        { 129u, module_ids::audio }
    };
    for (const connections::SBulkConfiguration configuration : invalid)
    {
        TEST_EXPECT(ctx, fixture.a.reserve_replies(1u));
        fixture.coordinator.request(fixture.a, &fixture.a, &fixture.b, { {}, {}, module_ids::rendering, 1u, configuration },
            7, fixture.restrictions.reader());
        threading::CErasedPodMsg message;
        connections::Result result;
        TEST_EXPECT(ctx, (fixture.context_a.read(message) && message.copy_payload_to(result) &&
            (result.status == EStatus::invalid_request) && !result.token.value));
        TEST_EXPECT(ctx, fixture.coordinator.empty());
    }
    fixture.b.direct_connections = host::EDirectConnectionState::unregistered;
    TEST_EXPECT(ctx, fixture.b.reserve_replies(1u));
    fixture.coordinator.register_client(fixture.b, { 2u }, 0);
    fixture.deliver(fixture.context_b, fixture.client_b);
    TEST_EXPECT(ctx, (fixture.b.direct_connections == host::EDirectConnectionState::unregistered));
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
    TEST_EXPECT(ctx, (read_direct(fixture.client_b, token, received) == connections::ERead::record));
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
    TEST_EXPECT(ctx, (read_direct(fixture.client_a, token, received) == connections::ERead::record));
    TEST_EXPECT(ctx, !fixture.client_a.has_data());
    TEST_EXPECT(ctx, (fixture.client_a.post(token, retained) == connections::EPost::full));
    const auto epoch = fixture.context_a.observe_epoch();
    if (before_clear)
    {
        TEST_EXPECT(ctx, (read_direct(fixture.client_b, token, received) == connections::ERead::record));
        TEST_EXPECT(ctx, (fixture.context_a.observe_epoch() == epoch)); // Capacity hint coalesced.
        fixture.client_a.clear_notifications();
    }
    else
    {
        fixture.client_a.clear_notifications();
        TEST_EXPECT(ctx, (fixture.client_a.post(token, retained) == connections::EPost::full));
        TEST_EXPECT(ctx, (read_direct(fixture.client_b, token, received) == connections::ERead::record));
        TEST_EXPECT(ctx, (fixture.context_a.observe_epoch() != epoch));
        threading::CWaitDeadline deadline;
        TEST_EXPECT(ctx, (deadline.set_after_milliseconds(100u).code == platform::threading::EWaitError::none));
        TEST_EXPECT(ctx, (fixture.context_a.wait_for_new_epoch(epoch, deadline).status == threading::EWaitStatus::satisfied));
        fixture.client_a.clear_notifications();
    }
    //  A durable retry follows clearing even when the epoch did not change.
    TEST_EXPECT(ctx, (fixture.client_a.post(token, retained) == connections::EPost::queued));
    unsigned records = 0u, retained_records = 0u;
    while (connections::was_read(read_direct(fixture.client_b, token, received)))
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

static void partial_installation(tests::TTestContext& ctx, const bool bulk = false)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, (bulk ? fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, 129u, true) : fixture.client_a.open(modules::test_connections::address(module_ids::rendering), 7)));
    fixture.step(); // Both participants explicitly accept before any installation.
    fixture.step(false, true); // Both installations queued, only B has consumed one.
    TEST_EXPECT(ctx, !fixture.client_b.active(modules::test_connections::address(module_ids::executive)).value);
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
    TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::cancelled)));
    TEST_EXPECT(ctx, (fixture.coordinator.empty() && fixture.client_a.idle() && fixture.client_b.idle()));
}

static void crossed_versions(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, 129u, true));
    TEST_EXPECT(ctx, fixture.client_b.open_bulk(module_ids::executive, 2u, 9, 129u, false));
    for (unsigned i = 0u; i < 5u; ++i) fixture.step();
    connections::Result result;
    std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.results_b.read(result, slot) && (slot == 9) && (result.status == EStatus::version_mismatch)));
    TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::success)));
    fixture.finish(result.token);
}
static void unsupported_and_busy(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.b.direct_connections = host::EDirectConnectionState::unregistered;
    TEST_EXPECT(ctx, fixture.client_a.open(modules::test_connections::address(module_ids::rendering), 7));
    fixture.step();
    connections::Result result; std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::unavailable)));
    TEST_EXPECT(ctx, (fixture.client_a.idle() && fixture.coordinator.empty()));
    TEST_EXPECT(ctx, fixture.b.reserve_replies(1u));
    fixture.coordinator.register_client(fixture.b, {}, 0);
    fixture.deliver(fixture.context_b, fixture.client_b);
    TEST_EXPECT(ctx, fixture.client_a.open(modules::test_connections::address(module_ids::rendering), 7));
    fixture.drain_host(fixture.a, &fixture.b);
    TEST_EXPECT(ctx, fixture.a.reserve_replies(1u));
    fixture.coordinator.request(fixture.a, &fixture.a, &fixture.b, { modules::test_connections::address(module_ids::executive), modules::test_connections::address(module_ids::rendering) }, 99, fixture.restrictions.reader());
    threading::CErasedPodMsg refusal;
    TEST_EXPECT(ctx, (fixture.context_a.read(refusal) && refusal.copy_payload_to(result) && (result.status == EStatus::busy) && !result.token.value));
    for (unsigned i = 0u; i < 5u; ++i) fixture.step();
    TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::success)));
    fixture.finish(result.token);
}

static void retirement_admission(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.coordinator.close_client(fixture.b); // No connection exists yet.
    TEST_EXPECT(ctx, fixture.b.reserve_replies(1u));
    fixture.coordinator.register_client(fixture.b, {}, 0);
    fixture.deliver(fixture.context_b, fixture.client_b);
    TEST_EXPECT(ctx, (fixture.b.direct_connections == host::EDirectConnectionState::closing));
    TEST_EXPECT(ctx, fixture.client_a.open(modules::test_connections::address(module_ids::rendering), 7));
    TEST_EXPECT(ctx, fixture.client_b.open(modules::test_connections::address(module_ids::executive), 9));
    fixture.step();
    connections::Result result; std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::unavailable) && !result.token.value));
    TEST_EXPECT(ctx, (fixture.results_b.read(result, slot) && (result.status == EStatus::closing) && !result.token.value));
    TEST_EXPECT(ctx, (fixture.coordinator.empty() && (fixture.a.reserved_replies == 0u) && (fixture.b.reserved_replies == 0u)));
    host::SClientRecord replacement{ module_ids::rendering, 23u, &fixture.second, modules::SModuleInstance{ 1023u } };
    TEST_EXPECT(ctx, replacement.reserve_replies(1u));
    fixture.coordinator.register_client(replacement, {}, 0);
    fixture.deliver(fixture.context_b, fixture.client_b);
    TEST_EXPECT(ctx, (replacement.direct_connections == host::EDirectConnectionState::available));
}

static void delayed_reciprocal(tests::TTestContext& ctx, const bool after_retirement, const bool accept_successor = false)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, fixture.client_a.open(modules::test_connections::address(module_ids::rendering), 7));
    TEST_EXPECT(ctx, fixture.client_b.open(modules::test_connections::address(module_ids::executive), 9));
    threading::CErasedPodMsg held;
    connections::Request request;
    TEST_EXPECT(ctx, (fixture.second.read(held) && held.copy_payload_to(request)));
    for (unsigned i = 0u; i < 5u; ++i) fixture.step();
    connections::Result result; std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::success)));
    const auto token = result.token;
    fixture.client_a.close(token); fixture.client_a.progress();
    fixture.drain_host(fixture.a, &fixture.b);
    if (!after_retirement)
    {
        TEST_EXPECT(ctx, fixture.b.reserve_replies(1u));
        fixture.coordinator.request(fixture.b, &fixture.b, &fixture.a, request, 9, fixture.restrictions.reader());
        fixture.coordinator.progress();
        connections::SReceived refusal;
        TEST_EXPECT(ctx, (fixture.client_b.receive(refusal, false)));
        TEST_EXPECT(ctx, (refusal.message.copy_payload_to(result) && (result.status == EStatus::closing) && !result.token.value));
        TEST_EXPECT(ctx, (!fixture.client_b.active(token) && !fixture.coordinator.empty()));
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
        fixture.coordinator.request(fixture.b, &fixture.b, &fixture.a, request, 9, fixture.restrictions.reader());
        for (unsigned i = 0u; i < 5u; ++i) fixture.step();
        TEST_EXPECT(ctx, (fixture.results_b.read(result, slot) && (slot == 9) &&
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
        TEST_EXPECT(ctx, fixture.context_a.post(threading::make_pod_message(0, connections::Register{})));
    }
    //  Growth publishes the request before its staged-buffer allocation fails.
    allocation.fail_on = allocation.attempts + 1u;
    TEST_EXPECT(ctx, !fixture.client_a.open(modules::test_connections::address(module_ids::rendering), 7));
    TEST_EXPECT(ctx, (allocation.failed && pipe.posting_poisoned() && fixture.client_a.failed()));
    TEST_EXPECT(ctx, (!fixture.client_a.idle() && !fixture.client_a.needs_progress()));
    TEST_EXPECT(ctx, !fixture.client_a.open(modules::test_connections::address(module_ids::rendering), 8));
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

static void activation_and_timeout(tests::TTestContext& ctx, const bool expire, const bool bulk = false)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, (bulk ? fixture.client_a.open_bulk(module_ids::rendering, 1u, 7, 129u, true) : fixture.client_a.open(modules::test_connections::address(module_ids::rendering), 7)));
    fixture.step(); // Offer.
    fixture.step(); // Installations and readiness submissions.
    threading::CErasedPodMsg pending;
    connections::Ready ready;
    TEST_EXPECT(ctx, (fixture.second.read(pending) && pending.copy_payload_to(ready)));
    fixture.drain_host(fixture.a, &fixture.b);
    fixture.coordinator.progress();
    fixture.deliver(fixture.context_a, fixture.client_a);
    TEST_EXPECT(ctx, (!fixture.client_a.active(ready.token) && !fixture.client_b.active(ready.token)));
    TEST_EXPECT(ctx, !fixture.client_a.producer_data(ready.token));
    TEST_EXPECT(ctx, (fixture.client_a.publish_bulk(ready.token) == connections::EBulkPublish::inactive));
    if (expire)
    {
        host::SDirectConnectionTestAccess::expire(fixture.coordinator, ready.token, fixture.restrictions.reader());
        fixture.step();
        TEST_EXPECT(ctx, (!fixture.coordinator.empty() && fixture.client_a.closing(ready.token)));
        connections::Result result; std::int32_t slot = -1;
        TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::timed_out)));
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
        if (bulk)
        {
            const memory::CMemoryView output = fixture.client_a.producer_data(ready.token);
            TEST_EXPECT(ctx, output);
            if (output) std::memset(output.data(), 42, output.bytes());
            TEST_EXPECT(ctx, (fixture.client_a.publish_bulk(ready.token) == connections::EBulkPublish::published));
            TEST_EXPECT(ctx, !fixture.client_b.discard_and_acquire(ready.token));
            fixture.deliver(fixture.context_b, fixture.client_b);
            TEST_EXPECT(ctx, fixture.client_b.discard_and_acquire(ready.token));
            const memory::CMemoryConstView input = fixture.client_b.consumer_data(ready.token);
            TEST_EXPECT(ctx, (input && (*static_cast<const unsigned char*>(input.data()) == 42u)));
        }
        else
        {
            const threading::CErasedPodMsg message = threading::make_pod_message(4, modules::RunTestsRequest{ module_ids::rendering });
            TEST_EXPECT(ctx, (fixture.client_a.post(ready.token, message) == connections::EPost::queued));
            threading::CErasedPodMsg received;
            TEST_EXPECT(ctx, (read_direct(fixture.client_b, ready.token, received) == connections::ERead::inactive));
            fixture.deliver(fixture.context_b, fixture.client_b);
            TEST_EXPECT(ctx, (read_direct(fixture.client_b, ready.token, received) == connections::ERead::record));
        }
        connections::Result result; std::int32_t slot = -1;
        TEST_EXPECT(ctx, (fixture.results_a.read(result, slot) && (result.status == EStatus::success)));
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
    const auto read = read_direct(fixture.client_b, token, received);
    TEST_EXPECT(ctx, (read == (after_read ? connections::ERead::record_wake_failed : connections::ERead::inactive)));
    if (after_read) TEST_EXPECT(ctx, (received.query_async_slot() == 4));
    const host::SClientRecord& reporter = after_read ? fixture.b : fixture.a;
    TEST_EXPECT(ctx, ((after_read ? fixture.client_b : fixture.client_a).failed() &&
        (reporter.endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::wake_failed)));
    fixture.finish(token);
}

static void existing_activation_order(tests::TTestContext& ctx)
{
    SFixture fixture{ ctx };
    TEST_EXPECT(ctx, fixture.client_a.open(fixture.client_b.address(), 7));
    fixture.step();
    fixture.step();
    fixture.drain_host(fixture.a, &fixture.b);
    fixture.drain_host(fixture.b, &fixture.a);
    threading::transports::CErasedPodMsgTransport& inbox = fixture.b.endpoint->host_to_worker_msgs;
    inbox.deallocate();
    TEST_EXPECT(ctx, inbox.initialise_fixed(32u));
    const threading::CErasedPodMsg padding = threading::make_pod_message(-1, threading::EndpointReady{});
    while (inbox.post(padding)) {}
    fixture.coordinator.progress();
    fixture.deliver(fixture.context_a, fixture.client_a);
    const SToken token = fixture.client_a.active(fixture.client_b.address());
    TEST_EXPECT(ctx, (token.value && !fixture.client_b.active(token)));
    connections::SReceived received;
    while (fixture.client_b.receive(received, false))
    {
        TEST_EXPECT(ctx, received.message.is_payload_a<threading::EndpointReady>());
    }
    TEST_EXPECT(ctx, fixture.client_b.open(fixture.client_a.address(), 9));
    fixture.drain_host(fixture.b, &fixture.a);
    TEST_EXPECT(ctx, !fixture.client_b.receive(received, false));
    fixture.coordinator.progress();
    TEST_EXPECT(ctx, (fixture.client_b.receive(received, false) && received.message.is_payload_a<connections::Activate>() &&
        fixture.client_b.active(token)));
    connections::Result result;
    TEST_EXPECT(ctx, (fixture.client_b.receive(received, false) && received.message.copy_payload_to(result) &&
        (received.message.query_async_slot() == 9) && (result.status == EStatus::already_connected) &&
        (result.token.value == token.value)));
    fixture.finish(token);
    TEST_EXPECT(ctx, (fixture.client_a.idle() && fixture.client_b.idle()));
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
    SNativeState& state = tests::TThreadFixtureState<SNativeState>::get();
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

static void native_join_retention(tests::TTestContext& ctx, const bool bulk = false)
{
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    SNativeState state;
    threading::CThreadPackage first{ { thread_ids::executive, module_ids::executive,
        platform::threading::EThreadPriority::Normal, &terminal_worker, &tests::TThreadFixtureState<SNativeState>::prepare, &state }, conversion };
    threading::CThreadPackage second{ { thread_ids::rendering, module_ids::rendering }, conversion };
    host::SClientRecord a{ module_ids::executive, 31u, &first, modules::SModuleInstance{ 1031u } }, b{ module_ids::rendering, 32u, &second, modules::SModuleInstance{ 1032u } };
    a.phase = b.phase = host::EClientPhase::ready;
    a.direct_connections = b.direct_connections = host::EDirectConnectionState::available;
    b.host_execution.instance = b.instance;
    TEST_EXPECT(ctx, (a.initialise() && b.initialise()));
    TEST_EXPECT(ctx, threading::SDirectConnectionTestAccess::initialise(second, b.host_execution));
    if (!first.startup())
    {
        TEST_EXPECT(ctx, false);
        return;
    }
    threading::CThreadContext context_b{ threading::SDirectConnectionTestAccess::resources(second) };
    connections::CClient client_b{ context_b, policies, policy_count };
    host::CHostRestrictions restrictions;
    host::CDirectConnections coordinator;
    TEST_EXPECT(ctx, b.reserve_replies(1u));
    coordinator.register_client(b, {}, 0);
    connections::SReceived received;
    TEST_EXPECT(ctx, client_b.receive(received, false));
    TEST_EXPECT(ctx, a.reserve_replies(1u));
    connections::Request request{ modules::test_connections::address(module_ids::executive),
        modules::test_connections::address(module_ids::rendering) };
    if (bulk) request = { {}, {}, module_ids::rendering, 1u, { 129u, module_ids::executive } };
    coordinator.request(a, &a, &b, request, 7, restrictions.reader());
    coordinator.progress();
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, client_b.receive(received, false));
    connections::Offer offer;
    TEST_EXPECT(ctx, received.message.copy_payload_to(offer));
    if (!bulk) TEST_EXPECT(ctx, client_b.decide(offer.token, EStatus::success));
    coordinator.decision(a, { offer.token, EStatus::success });
    connections::Decision decision;
    TEST_EXPECT(ctx, (second.read(message) && message.copy_payload_to(decision)));
    coordinator.decision(b, decision);
    coordinator.progress();
    while (client_b.receive(received, false))
    {
        TEST_EXPECT(ctx, connections::is_control(received.message));
    }
    state.endpoint = host::SDirectConnectionTestAccess::notification(coordinator, offer.token);
    threading::SDirectConnectionTestAccess::fail_join(first, true);
    state.terminal.store(true, std::memory_order_release);
    TEST_EXPECT(ctx, await([&] { return state.published.load(std::memory_order_acquire); }));
    coordinator.close_client(a); // Progress-state exit does not prove native execution ended.
    a.terminal_before_drain = a.incoming_drained = true;
    coordinator.progress();
    while (client_b.receive(received, false))
    {
        TEST_EXPECT(ctx, connections::is_control(received.message));
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
    if (bulk)
    {
        TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::bulk_capacity(coordinator, offer.token) == 129u));
    }
    state.signal.store(true, std::memory_order_release);
    TEST_EXPECT(ctx, await([&] { return state.signalled.load(std::memory_order_acquire); }));
    state.finish.store(true, std::memory_order_release);
    threading::SDirectConnectionTestAccess::fail_join(first, false);
    TEST_EXPECT(ctx, await([&] { return first.execution_ended(); }));
    TEST_EXPECT(ctx, first.join_terminated_execution());
    TEST_EXPECT(ctx, (threading::SDirectConnectionTestAccess::pins(first) == 1u));
    coordinator.progress();
    TEST_EXPECT(ctx, coordinator.empty());
    while (client_b.receive(received, false))
    {
        TEST_EXPECT(ctx, connections::is_control(received.message));
    }
    threading::SDirectConnectionTestAccess::end_execution(second);
    TEST_EXPECT(ctx, (first.shutdown() && second.shutdown()));
}

//  Pump and thread endpoints exercise identical helpers and Host coordination.
struct SEndpointPeer
{
    tests::SDirectHostFixture& host;
    threading::CThreadPackage package;
    host::SClientRecord record;
    threading::CEndpointContext context;
    connections::CClient client;
    connections::Offer offer;
    tests::SDirectResultObservations outcomes;
    bool consent{ true }, cleanup{ true }, deliver{ true };
    unsigned offers{ 0u }, activations{ 0u }, results{ 0u }, closings{ 0u }, retired{ 0u };
    bool result_before_activation{ false };

    SEndpointPeer(tests::SDirectHostFixture& driver, const platform::system::CPerfCountConversion& conversion,
        const module_ids::id_type module, const thread_ids::id_type thread, const std::uint64_t identity) noexcept
        : host{ driver }, package{ { thread, module }, conversion },
          record{ module, identity, ((thread == thread_ids::host) ? nullptr : &package),
            { module.raw_value() } }, context{ *record.endpoint }, client{ context }
    {
        if (record.thread) (void)threading::SDirectConnectionTestAccess::initialise(package, record.host_execution);
        (void)host.attach(record);
        (void)client.enable(1000);
    }
    void pump()
    {
        if (!deliver) return;
        connections::SReceived received;
        for (unsigned count = 0u; (count < 32u) && client.receive(received, false); ++count)
        {
            connections::Offer incoming;
            connections::Result result;
            if (received.message.copy_payload_to(incoming))
            {
                offer = incoming;
                ++offers;
                if (consent) (void)client.decide(offer.token, EStatus::success);
            }
            else if (received.message.is_payload_a<connections::Activate>()) ++activations;
            else if (received.message.copy_payload_to(result))
            {
                (void)outcomes.observe(received.message);
                ++results;
                if ((result.status == EStatus::success) &&
                    ((result.first.value == record.identity.value) || (result.second.value == record.identity.value)) &&
                    !activations)
                {
                    result_before_activation = true;
                }
            }
            else if (received.message.is_payload_a<connections::Closing>()) ++closings;
            else if (received.message.is_payload_a<connections::Retired>()) ++retired;
        }
        if (cleanup)
        {
            for (SToken token = client.next_closing(); token.value; token = client.next_closing())
            {
                if (!client.quiesce(token)) break;
            }
        }
        client.progress();
    }
};

struct SEndpointFixture
{
    tests::TTestContext& test;
    platform::system::CPerfCountConversion conversion;
    tests::SDirectHostFixture host;
    TInstance<SEndpointPeer> peers[12];
    unsigned count{ 0u };

    explicit SEndpointFixture(tests::TTestContext& context) : test{ context } {}
    SEndpointPeer& add(const module_ids::id_type module, const thread_ids::id_type thread)
    {
        const unsigned index = count++;
        TEST_EXPECT(test, peers[index].emplace(host, conversion, module, thread, (100u + index)));
        return *peers[index];
    }
    void step(const unsigned rounds = 1u)
    {
        for (unsigned round = 0u; round < rounds; ++round)
        {
            for (unsigned index = 0u; index < count; ++index) host.drain_controls(peers[index]->record);
            host.progress();
            for (unsigned index = 0u; index < count; ++index) peers[index]->pump();
        }
    }
    SToken establish(SEndpointPeer& requester, SEndpointPeer& first, SEndpointPeer& second, const int slot = 7)
    {
        step();
        TEST_EXPECT(test, requester.client.open(first.client.address(), second.client.address(), slot));
        step(5u);
        connections::Result result;
        std::int32_t correlation = -1;
        TEST_EXPECT(test, (requester.outcomes.read(result, correlation) && (correlation == slot) &&
            (result.status == EStatus::success)));
        TEST_EXPECT(test, (first.client.active(result.token) && second.client.active(result.token)));
        return result.token;
    }
    void finish(SEndpointPeer& participant, const SToken token)
    {
        participant.client.close(token);
        participant.client.progress();
        step(4u);
        TEST_EXPECT(test, !host::SDirectConnectionTestAccess::contains(host.connections, token));
    }
    ~SEndpointFixture()
    {
        for (unsigned index = 0u; index < count; ++index)
        {
            SEndpointPeer& peer = *peers[index];
            if (peer.record.thread) threading::SDirectConnectionTestAccess::end_execution(peer.package);
            else peer.record.phase = host::EClientPhase::stopped;
            peer.record.terminal_before_drain = peer.record.incoming_drained = true;
        }
        host.connections.close_all();
        host.connections.progress();
        TEST_EXPECT(test, host.connections.empty());
        for (unsigned index = 0u; index < count; ++index)
        {
            if (peers[index]->record.thread) TEST_EXPECT(test, peers[index]->package.shutdown());
        }
    }
};

static void endpoint_pairings(tests::TTestContext& ctx)
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
        SEndpointFixture fixture{ ctx };
        SEndpointPeer& a = fixture.add(module_ids::executive, first_pump ? thread_ids::host : thread_ids::executive);
        SEndpointPeer& b = fixture.add(same_module ? module_ids::executive : module_ids::rendering,
            second_pump ? thread_ids::host : thread_ids::rendering);
        SEndpointPeer& requester = fixture.add(module_ids::audio, thread_ids::host);
        const SToken token = fixture.establish(requester, a, b);
        TEST_EXPECT(ctx, ((a.offers == 1u) && (b.offers == 1u) && !a.results && !b.results && !requester.offers));
        const threading::CErasedPodMsg message = threading::make_pod_message(23, modules::RunTestsRequest{ module_ids::audio });
        TEST_EXPECT(ctx, (a.client.post(b.client.address(), message, token) == connections::EPost::queued));
        connections::SReceived received;
        TEST_EXPECT(ctx, (b.client.receive(received) && !received.source.is_host() &&
            (received.source.address == a.client.address()) && (received.source.endpoint.value == a.record.identity.value) &&
            (received.source.connection.value == token.value) && (received.message.query_async_slot() == 23)));
        TEST_EXPECT(ctx, (b.client.post(a.client.address(), message) == connections::EPost::queued));
        TEST_EXPECT(ctx, (a.client.receive(received) && (received.source.endpoint.value == b.record.identity.value)));
        requester.client.close(token);
        TEST_EXPECT(ctx, requester.context.post(threading::make_pod_message(-1, connections::Close{ token })));
        fixture.step();
        TEST_EXPECT(ctx, (a.client.active(token) && b.client.active(token)));
        fixture.finish(a, token);
        TEST_EXPECT(ctx, (a.client.post(b.client.address(), message, token) == connections::EPost::no_pipe));
        const SToken successor = fixture.establish(requester, a, b);
        TEST_EXPECT(ctx, (successor.value != token.value));
        TEST_EXPECT(ctx, (a.client.post(b.client.address(), message, token) == connections::EPost::no_pipe));
        fixture.finish(b, successor);
    }
}

static void explicit_consent_and_requests(tests::TTestContext& ctx)
{
    SEndpointFixture fixture{ ctx };
    SEndpointPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SEndpointPeer& b = fixture.add(module_ids::executive, thread_ids::executive);
    SEndpointPeer& r = fixture.add(module_ids::rendering, thread_ids::host);
    fixture.step();
    a.consent = b.consent = false;
    TEST_EXPECT(ctx, a.client.open(b.client.address(), 1));
    TEST_EXPECT(ctx, b.client.open(a.client.address(), 2));
    TEST_EXPECT(ctx, r.client.open(a.client.address(), b.client.address(), 3));
    TEST_EXPECT(ctx, r.client.open(b.client.address(), a.client.address(), 4));
    fixture.step(2u);
    TEST_EXPECT(ctx, ((a.offers == 1u) && (b.offers == 1u) && (a.offer.token.value == b.offer.token.value)));
    connections::Result result;
    std::int32_t slot = -1;
    TEST_EXPECT(ctx, (r.outcomes.read(result, slot) && (slot == 4) && (result.status == EStatus::busy)));
    const SToken token = a.offer.token;
    TEST_EXPECT(ctx, (a.client.post(b.client.address(), threading::make_pod_message(0, connections::Registered{})) == connections::EPost::no_pipe));
    TEST_EXPECT(ctx, a.client.decide(token, EStatus::success));
    fixture.step(2u);
    TEST_EXPECT(ctx, (!a.activations && !b.activations));
    TEST_EXPECT(ctx, b.client.decide(token, EStatus::success));
    fixture.step(4u);
    TEST_EXPECT(ctx, (a.outcomes.read(result, slot) && (slot == 1) && (result.status == EStatus::success)));
    TEST_EXPECT(ctx, (b.outcomes.read(result, slot) && (slot == 2) && (result.status == EStatus::success)));
    TEST_EXPECT(ctx, (r.outcomes.read(result, slot) && (slot == 3) && (result.status == EStatus::success)));
    TEST_EXPECT(ctx, (!a.result_before_activation && !b.result_before_activation));
    fixture.finish(a, token);
}

static void request_limit_and_departure(tests::TTestContext& ctx, const bool departing)
{
    SEndpointFixture fixture{ ctx };
    SEndpointPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SEndpointPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    const thread_ids::id_type threads[]{ thread_ids::host, thread_ids::executive, thread_ids::rendering, thread_ids::audio,
        thread_ids::simulation, thread_ids::debug_service, thread_ids::bg_file_io, thread_ids::bg_conditioning, thread_ids::batch_runner_00 };
    for (unsigned index = 0u; index < 9u; ++index) (void)fixture.add(module_ids::audio, threads[index]);
    fixture.step();
    a.consent = b.consent = false;
    for (unsigned index = 2u; index < fixture.count; ++index)
    {
        TEST_EXPECT(ctx, fixture.peers[index]->client.open(a.client.address(), b.client.address(), static_cast<int>(index)));
    }
    fixture.step(2u);
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::requests(fixture.host.connections) == 8u));
    connections::Result result;
    std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.peers[10]->outcomes.read(result, slot) && (result.status == EStatus::busy)));
    if (departing)
    {
        SEndpointPeer& requester = *fixture.peers[2];
        fixture.host.connections.close_client(requester.record);
        requester.record.phase = host::EClientPhase::stopped;
        requester.deliver = false;
        fixture.host.connections.progress();
        TEST_EXPECT(ctx, (!requester.record.reserved_replies &&
            (host::SDirectConnectionTestAccess::requests(fixture.host.connections) == 7u)));
    }
    TEST_EXPECT(ctx, a.client.decide(a.offer.token, EStatus::success));
    TEST_EXPECT(ctx, b.client.decide(b.offer.token, EStatus::success));
    fixture.step(4u);
    TEST_EXPECT(ctx, ((a.offers == 1u) && (b.offers == 1u) && a.client.active(a.offer.token)));
    for (unsigned index = departing ? 3u : 2u; index < 10u; ++index)
    {
        TEST_EXPECT(ctx, (fixture.peers[index]->outcomes.read(result, slot) && (result.status == EStatus::success) &&
            (result.token.value == a.offer.token.value)));
    }
    fixture.finish(a, a.offer.token);
}

static void authenticated_fair_receive(tests::TTestContext& ctx, const bool pump)
{
    SEndpointFixture fixture{ ctx };
    SEndpointPeer& a = fixture.add(module_ids::executive, pump ? thread_ids::host : thread_ids::executive);
    SEndpointPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    SEndpointPeer& c = fixture.add(module_ids::audio, thread_ids::host);
    const SToken ab = fixture.establish(a, a, b);
    const SToken ac = fixture.establish(a, a, c, 8);
    //  A peer's forged Closing is data and cannot affect either connection.
    TEST_EXPECT(ctx, (b.client.post(ab, threading::make_pod_message(99, connections::Closing{ ac })) == connections::EPost::queued));
    connections::SReceived received;
    TEST_EXPECT(ctx, (a.client.receive(received) && !received.source.is_host() && (received.source.connection.value == ab.value)));
    TEST_EXPECT(ctx, (a.client.active(ab) && a.client.active(ac)));
    a.client.clear_notifications();
    a.record.notifications_pending.store(0u, std::memory_order_release);
    for (int index = 0; index < 6; ++index)
    {
        const threading::CErasedPodMsg message = threading::make_pod_message(index, modules::RunTestsRequest{});
        TEST_EXPECT(ctx, (b.client.post(ab, message) == connections::EPost::queued));
        TEST_EXPECT(ctx, (c.client.post(ac, message) == connections::EPost::queued));
        TEST_EXPECT(ctx, a.record.post(message));
    }
    if (pump) TEST_EXPECT(ctx, (a.record.notifications_pending.load(std::memory_order_acquire) != 0u));
    for (int index = 0; index < 6; ++index)
    {
        TEST_EXPECT(ctx, (a.client.receive(received, false) && received.source.is_host() && (received.message.query_async_slot() == index)));
    }
    TEST_EXPECT(ctx, (!a.client.has_data(false) && a.client.has_data() && !a.client.receive(received, false)));
    int counts[3]{};
    //  Restore Host traffic; every three consecutive receives visits all sources.
    for (int index = 0; index < 6; ++index) TEST_EXPECT(ctx, a.record.post(threading::make_pod_message(index, modules::RunTestsRequest{})));
    for (unsigned batch = 0u; batch < 6u; ++batch)
    {
        unsigned mask = 0u;
        for (unsigned index = 0u; index < 3u; ++index)
        {
            TEST_EXPECT(ctx, a.client.receive(received));
            const unsigned source = received.source.is_host() ? 0u : ((received.source.endpoint.value == b.record.identity.value) ? 1u : 2u);
            TEST_EXPECT(ctx, (received.message.query_async_slot() == counts[source]));
            ++counts[source];
            mask |= 1u << source;
        }
        TEST_EXPECT(ctx, (mask == 7u));
    }
    TEST_EXPECT(ctx, !a.client.receive(received));
    //  Closing remains reachable with queued data excluded by caller pressure.
    TEST_EXPECT(ctx, (b.client.post(ab, threading::make_pod_message(77, modules::RunTestsRequest{})) == connections::EPost::queued));
    a.cleanup = false;
    fixture.host.connections.close(b.record, { ab });
    fixture.step();
    TEST_EXPECT(ctx, (a.closings && a.client.closing(ab) && !a.client.active(ab)));
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.host.connections, ab));
    TEST_EXPECT(ctx, a.client.quiesce(ab));
    fixture.step(3u);
    TEST_EXPECT(ctx, !host::SDirectConnectionTestAccess::contains(fixture.host.connections, ab));
    a.cleanup = true;
    fixture.finish(c, ac);
}

static void result_after_retirement(tests::TTestContext& ctx)
{
    SEndpointFixture fixture{ ctx };
    SEndpointPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SEndpointPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    SEndpointPeer& r = fixture.add(module_ids::audio, thread_ids::host);
    fixture.step();
    r.deliver = false;
    threading::transports::CErasedPodMsgTransport& replies = r.record.endpoint->host_to_worker_msgs;
    replies.deallocate();
    TEST_EXPECT(ctx, replies.initialise_fixed(32u));
    const threading::CErasedPodMsg padding = threading::make_pod_message(-1, modules::RunTestsRequest{});
    while (replies.post(padding)) {}
    TEST_EXPECT(ctx, r.client.open(a.client.address(), b.client.address(), 23));
    fixture.step(5u);
    const SToken token = a.offer.token;
    TEST_EXPECT(ctx, (a.client.active(token) && b.client.active(token) && (r.record.reserved_replies == 1u)));
    fixture.finish(a, token);
    TEST_EXPECT(ctx, (!fixture.host.connections.empty() && fixture.host.connections.client_idle(a.record) &&
        fixture.host.connections.client_idle(b.record) && !fixture.host.connections.client_idle(r.record)));
    connections::SReceived received;
    while (r.client.receive(received, false)) {}
    fixture.host.connections.progress();
    TEST_EXPECT(ctx, (r.client.receive(received, false) && received.message.is_payload_a<connections::Result>()));
    connections::Result result;
    std::int32_t slot = -1;
    TEST_EXPECT(ctx, (received.message.copy_payload_to(result) && (received.message.query_async_slot() == 23) && (result.status == EStatus::success) &&
        (result.token.value == token.value) && (result.first.value == a.record.identity.value) &&
        (result.second.value == b.record.identity.value)));
    TEST_EXPECT(ctx, (fixture.host.connections.empty() && !r.record.reserved_replies));
}
struct SAdmittedOperation
{
    const connections::SEndpointDescriptor* endpoint{ nullptr };
    std::atomic<bool> entered{ false }, finish{ false }, posted{ false }, signalled{ false };
    static std::uint32_t MV_STD_ABI_CALL run(void* const context) noexcept
    {
        SAdmittedOperation& state = *static_cast<SAdmittedOperation*>(context);
        const connections::COperation operation{ state.endpoint->admission };
        if (!operation) return 1u;
        state.posted.store(state.endpoint->outgoing->post(threading::make_pod_message(91, modules::RunTestsRequest{})),
            std::memory_order_release);
        state.entered.store(true, std::memory_order_release);
        while (!state.finish.load(std::memory_order_acquire)) platform::threading::processor_relax();
        state.signalled.store(state.endpoint->peer_notification.signal(), std::memory_order_release);
        return 0u;
    }
};

static void close_during_signalling(tests::TTestContext& ctx, const bool pump)
{
    SEndpointFixture fixture{ ctx };
    SEndpointPeer& a = fixture.add(module_ids::executive, thread_ids::executive);
    SEndpointPeer& b = fixture.add(module_ids::rendering, pump ? thread_ids::host : thread_ids::rendering);
    const SToken token = fixture.establish(a, a, b);
    a.deliver = false;
    a.cleanup = false;
    a.client.clear_notifications();
    b.client.clear_notifications();
    SAdmittedOperation operation;
    operation.endpoint = host::SDirectConnectionTestAccess::descriptor(fixture.host.connections, token, 0u);
    TEST_EXPECT(ctx, operation.endpoint);
    platform::threading::CThread worker;
    const bool created = worker.create(&SAdmittedOperation::run, &operation);
    TEST_EXPECT(ctx, created);
    if (!created) return;
    TEST_EXPECT(ctx, await([&] { return operation.entered.load(std::memory_order_acquire); }));
    fixture.host.connections.close(b.record, { token });
    fixture.step(3u);
    TEST_EXPECT(ctx, (!operation.endpoint->admission->available() && !operation.endpoint->admission->drained()));
    {
        const connections::COperation refused{ operation.endpoint->admission };
        TEST_EXPECT(ctx, !refused);
    }
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::contains(fixture.host.connections, token) &&
        (a.record.endpoint->notification_pins == 1u) && (b.record.endpoint->notification_pins == 1u)));
    operation.finish.store(true, std::memory_order_release);
    TEST_EXPECT(ctx, worker.join_and_close());
    TEST_EXPECT(ctx, (operation.posted.load(std::memory_order_acquire) && operation.signalled.load(std::memory_order_acquire)));
    TEST_EXPECT(ctx, operation.endpoint->admission->drained());
    fixture.host.connections.progress();
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.host.connections, token));
    a.deliver = true;
    a.pump();
    TEST_EXPECT(ctx, a.client.quiesce(token));
    fixture.step(3u);
    TEST_EXPECT(ctx, !host::SDirectConnectionTestAccess::contains(fixture.host.connections, token));
}

static void deferred_quiescence_and_obsolete_controls(tests::TTestContext& ctx)
{
    SEndpointFixture fixture{ ctx };
    SEndpointPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SEndpointPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    const SToken token = fixture.establish(a, a, b);
    const connections::SEndpointDescriptor* const old_descriptor = host::SDirectConnectionTestAccess::descriptor(fixture.host.connections, token, 0u);
    a.cleanup = false;
    fixture.host.connections.close(b.record, { token });
    fixture.step(2u);
    threading::transports::CErasedPodMsgTransport& controls = a.record.endpoint->worker_to_host_msgs;
    controls.deallocate();
    TEST_EXPECT(ctx, controls.initialise_fixed(32u));
    while (controls.post(threading::make_pod_message(-1, connections::Ready{ token }))) {}
    TEST_EXPECT(ctx, !a.client.quiesce(token));
    TEST_EXPECT(ctx, (a.client.needs_progress() && !a.client.active(token)));
    fixture.host.connections.progress();
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.host.connections, token));
    TEST_EXPECT(ctx, a.record.post(threading::make_pod_message(-1, connections::Activate{ token })));
    TEST_EXPECT(ctx, a.record.post(threading::make_pod_message(-1, connections::Install{ token, old_descriptor })));
    a.pump();
    TEST_EXPECT(ctx, !a.client.active(token));
    fixture.step(4u);
    TEST_EXPECT(ctx, (!host::SDirectConnectionTestAccess::contains(fixture.host.connections, token) && !a.client.needs_progress()));
    //  The exact-token check precedes dereferencing this now obsolete pointer.
    TEST_EXPECT(ctx, a.record.post(threading::make_pod_message(-1, connections::Install{ token, old_descriptor })));
    a.pump();
    TEST_EXPECT(ctx, a.client.idle());
}
static void readiness_and_address_replacement(tests::TTestContext& ctx)
{
    SEndpointFixture fixture{ ctx };
    SEndpointPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SEndpointPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    a.record.phase = b.record.phase = host::EClientPhase::starting;
    fixture.step();
    TEST_EXPECT(ctx, (a.client.enabled() && b.client.enabled()));
    connections::Result result;
    std::int32_t slot = -1;
    TEST_EXPECT(ctx, a.client.open(a.client.address(), 1));
    fixture.step();
    TEST_EXPECT(ctx, (a.outcomes.read(result, slot) && (result.status == EStatus::invalid_request)));
    const system_ids::id_type missing = system_ids::ops::make_system_id(module_ids::conditioning_general, thread_ids::simulation);
    TEST_EXPECT(ctx, a.client.open(missing, 2));
    fixture.step();
    TEST_EXPECT(ctx, (a.outcomes.read(result, slot) && (result.status == EStatus::unavailable)));
    b.record.direct_connections = host::EDirectConnectionState::unregistered;
    TEST_EXPECT(ctx, a.client.open(b.client.address(), 3));
    fixture.step();
    TEST_EXPECT(ctx, (a.outcomes.read(result, slot) && (result.status == EStatus::unavailable)));
    TEST_EXPECT(ctx, b.client.enable(99));
    fixture.step();
    const SToken previous = fixture.establish(a, a, b);
    fixture.finish(a, previous);
    TEST_EXPECT(ctx, fixture.host.detach(b.record));
    b.deliver = false;
    b.record.phase = host::EClientPhase::stopped;
    SEndpointPeer& replacement = fixture.add(module_ids::rendering, thread_ids::host);
    const SToken current = fixture.establish(a, a, replacement);
    TEST_EXPECT(ctx, ((previous.value != current.value) && (b.record.identity.value != replacement.record.identity.value)));
    fixture.host.connections.close(b.record, { current });
    fixture.host.connections.close(a.record, { previous });
    fixture.host.connections.quiescent(b.record, { current });
    TEST_EXPECT(ctx, (a.client.active(current) && replacement.client.active(current)));
    const threading::CErasedPodMsg message = threading::make_pod_message(7, modules::RunTestsRequest{});
    TEST_EXPECT(ctx, (a.client.post(replacement.client.address(), message, previous) == connections::EPost::no_pipe));
    TEST_EXPECT(ctx, (replacement.client.post(a.client.address(), message, current) == connections::EPost::queued));
    connections::SReceived received;
    TEST_EXPECT(ctx, (a.client.receive(received) && (received.source.endpoint.value == replacement.record.identity.value)));
    fixture.finish(replacement, current);
}

static void shared_connection_limits(tests::TTestContext& ctx, const bool overall)
{
    SEndpointFixture fixture{ ctx };
    const thread_ids::id_type threads[]{ thread_ids::host, thread_ids::executive, thread_ids::rendering, thread_ids::audio,
        thread_ids::simulation, thread_ids::debug_service, thread_ids::bg_file_io, thread_ids::bg_conditioning,
        thread_ids::batch_runner_00, thread_ids::batch_runner_01 };
    for (unsigned index = 0u; index < 10u; ++index) (void)fixture.add(module_ids::audio, threads[index]);
    fixture.step();
    if (overall)
    {
        unsigned established = 0u;
        for (unsigned first = 0u; (first < 9u) && (established < 32u); ++first)
        {
            for (unsigned second = first + 1u; (second < 9u) && (established < 32u); ++second)
            {
                (void)fixture.establish(*fixture.peers[first], *fixture.peers[first], *fixture.peers[second]);
                ++established;
            }
        }
        TEST_EXPECT(ctx, fixture.peers[5]->client.open(fixture.peers[8]->client.address(), 99));
    }
    else
    {
        for (unsigned second = 1u; second <= 8u; ++second)
        {
            (void)fixture.establish(*fixture.peers[0], *fixture.peers[0], *fixture.peers[second]);
        }
        TEST_EXPECT(ctx, fixture.peers[0]->client.open(fixture.peers[9]->client.address(), 99));
    }
    fixture.step();
    connections::Result result;
    std::int32_t slot = -1;
    TEST_EXPECT(ctx, (fixture.peers[overall ? 5u : 0u]->outcomes.read(result, slot) &&
        (slot == 99) && (result.status == EStatus::busy)));
    for (unsigned index = 0u; index < fixture.count; ++index) fixture.peers[index]->client.close_all();
    fixture.step(5u);
    TEST_EXPECT(ctx, fixture.host.connections.empty());
}

static void receiver_only_results(tests::TTestContext& ctx)
{
    SEndpointFixture fixture{ ctx };
    SEndpointPeer& a = fixture.add(module_ids::audio, thread_ids::host);
    fixture.step();
    a.deliver = false;
    for (unsigned index = 0u; index < 48u; ++index)
    {
        //  Reuse the same correlation immediately after receiving its result.
        TEST_EXPECT(ctx, a.client.open(a.client.address(), 7));
        fixture.step();
        connections::SReceived received;
        connections::Result result;
        TEST_EXPECT(ctx, (a.client.receive(received) && received.source.is_host() &&
            received.message.copy_payload_to(result) && (received.message.query_async_slot() == 7) &&
            (result.status == EStatus::invalid_request)));
        TEST_EXPECT(ctx, (a.client.idle() && fixture.host.connections.empty()));
    }
}
}   //  namespace

void run_direct_connection_tests(tests::TTestContext& ctx)
{
    endpoint_pairings(ctx);
    receiver_only_results(ctx);
    existing_activation_order(ctx);
    readiness_and_address_replacement(ctx);
    shared_connection_limits(ctx, false);
    shared_connection_limits(ctx, true);
    explicit_consent_and_requests(ctx);
    request_limit_and_departure(ctx, false);
    request_limit_and_departure(ctx, true);
    authenticated_fair_receive(ctx, false);
    authenticated_fair_receive(ctx, true);
    result_after_retirement(ctx);
    close_during_signalling(ctx, false);
    close_during_signalling(ctx, true);
    deferred_quiescence_and_obsolete_controls(ctx);
    bulk_backing_ownership(ctx);
    bulk_concurrent_exchange(ctx);
    bulk_request_validation(ctx);
    bulk_buffers(ctx, false, false);
    bulk_buffers(ctx, true, false);
    bulk_buffers(ctx, false, true);
    bulk_buffers(ctx, true, true);
    bulk_configuration_and_coexistence(ctx, false);
    bulk_configuration_and_coexistence(ctx, true);
    bulk_allocation_failure(ctx);
    bulk_wake_failure(ctx);
    partial_installation(ctx, true);
    activation_and_timeout(ctx, false, true);
    activation_and_timeout(ctx, true, true);
    native_join_retention(ctx, true);
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
