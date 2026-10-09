//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostWorkers_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Oct 26

#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>

#include "host/runtime/batch_pool.hpp"
#include "host/runtime/worker_policy.hpp"
#include "platform/threading/processor_relax.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "threading/CBatchWork.hpp"
#include "threading/CThreadPackage.hpp"
#include "tests/test_suites/HostWorkers_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace threading
{

struct SBatchClientTestAccess
{
    //  Split complete() at the publication/wake boundary to reproduce a
    //  requester exiting while its final return publisher still borrows it.
    static bool publish_before_wake(CBatchClient& client, const SBatchResponse& response)
    {
        client.m_return_publishers.fetch_add(1u, std::memory_order_acq_rel);
        transports::TReservedArenaSlot<SBatchResponse, k_batch_channel_capacity> reserved(client.m_returns);
        if (!reserved) return false;
        *reserved = response;
        return reserved.publish();
    }

    static bool finish_wake(CBatchClient& client)
    {
        const bool woken = client.m_wake->poke_epoch_and_wake_one();
        client.m_return_publishers.fetch_sub(1u, std::memory_order_release);
        return woken;
    }
};

}   // namespace threading

namespace
{

using TTestContext = tests::TTestContext;

void test_hardware_limits(TTestContext& ctx)
{
    struct SLimit { std::uint32_t hardware; std::uint32_t workers; };
    const SLimit limits[]{
        { 0u, 1u }, { 1u, 1u }, { 4u, 1u }, { 7u, 1u },
        { 8u, 2u }, { 9u, 2u }, { 10u, 2u }, { 11u, 2u },
        { 12u, 2u }, { 16u, 2u }, { 17u, 2u }, { 18u, 2u },
        { 64u, 2u }, { 128u, 2u }, { std::numeric_limits<std::uint32_t>::max(), 2u } };
    for (const auto& limit : limits)
    {
        TEST_EXPECT(ctx, host::worker_count_limit(limit.hardware) == limit.workers);
    }
    TEST_EXPECT(ctx, host::k_default_worker_count == 2u);
    TEST_EXPECT(ctx, host::k_max_worker_count == 2u);
}

void test_batch_policy(TTestContext& ctx)
{
    TEST_EXPECT(ctx, host::batch_runner_limit(8u, 2u) == 0u);
    TEST_EXPECT(ctx, host::batch_runner_limit(16u, 2u) == 8u);
    TEST_EXPECT(ctx, host::batch_runner_limit(16u, 2u, 4u) == 6u);
    TEST_EXPECT(ctx, host::batch_runner_limit(64u, 2u) == 32u);
    TEST_EXPECT(ctx, host::batch_runner_limit(128u, 2u) == 32u);
    TEST_EXPECT(ctx, host::k_default_batch_runner_count == 8u);
}

void MV_STD_ABI_CALL run_batch_item(void* const object) noexcept
{
    ++*static_cast<std::uint32_t*>(object);
}

void test_batch_routes(TTestContext& ctx)
{
    threading::SBatchShared shared;
    threading::CThreadControlState control;
    threading::CWaitPredicate requester_wake;
    TEST_EXPECT(ctx, shared.wake.acquire_control());
    TEST_EXPECT(ctx, requester_wake.acquire_control());
    threading::CBatchClient client(shared, control, requester_wake,
        module_ids::executable, &shared);
    std::uint32_t objects[threading::k_batch_channel_capacity + 1u]{};

    const threading::SBatchSubmission inline_result = client.submit(&run_batch_item, &objects[0], 1u);
    TEST_EXPECT(ctx, inline_result.status == threading::EBatchSubmission::completed_inline);
    TEST_EXPECT(ctx, objects[0] == 1u);
    TEST_EXPECT(ctx, client.outstanding() == 0u);

    shared.runner_count = 1u;
    const threading::SBatchSubmission first = client.submit(&run_batch_item, &objects[0], 1u);
    TEST_EXPECT(ctx, first.status == threading::EBatchSubmission::queued);
    TEST_EXPECT(ctx, client.submit(&run_batch_item, &objects[0], 2u).reason ==
        threading::EBatchRejection::duplicate_object);
    TEST_EXPECT(ctx, client.submit(&run_batch_item, &objects[1], 1u).reason ==
        threading::EBatchRejection::duplicate_correlation);

    for (std::uint32_t index = 1u; index < threading::k_batch_channel_capacity; ++index)
    {
        TEST_EXPECT(ctx, client.submit(&run_batch_item, &objects[index], index + 1u).status ==
            threading::EBatchSubmission::queued);
    }
    TEST_EXPECT(ctx, client.outstanding() == threading::k_batch_channel_capacity);

    for (std::uint32_t index = 0u; index < threading::k_batch_channel_capacity; ++index)
    {
        threading::transports::TAcquiredArenaSlot<threading::SBatchWork,
            threading::k_batch_channel_capacity> work(shared.work);
        TEST_EXPECT(ctx, work.is_ready());
        if (work)
        {
            const threading::SBatchWork item = *work;
            item.function(item.object);
            TEST_EXPECT(ctx, item.requester->complete({ item.correlation, threading::EBatchCompletion::executed }));
        }
    }
    TEST_EXPECT(ctx, client.submit(&run_batch_item, &objects[threading::k_batch_channel_capacity], 129u).status ==
        threading::EBatchSubmission::completed_inline);
    TEST_EXPECT(ctx, objects[threading::k_batch_channel_capacity] == 1u);

    for (std::uint32_t index = 0u; index < threading::k_batch_channel_capacity; ++index)
    {
        threading::SBatchResponse response;
        TEST_EXPECT(ctx, client.receive(response));
        TEST_EXPECT(ctx, response.correlation == index + 1u);
        TEST_EXPECT(ctx, response.completion == threading::EBatchCompletion::executed);
        TEST_EXPECT(ctx, objects[index] == ((index == 0u) ? 2u : 1u));
    }
    TEST_EXPECT(ctx, client.outstanding() == 0u);
    TEST_EXPECT(ctx, client.return_publishers() == 0u);
    requester_wake.release_control();
    shared.wake.release_control();
}

struct SBatchProducer
{
    threading::CBatchClient* client{ nullptr };
    std::atomic<bool>* start{ nullptr };
    std::uint32_t* objects{ nullptr };
    std::uint32_t first_correlation{ 0u };
    bool queued_all{ false };
};

std::uint32_t MV_STD_ABI_CALL batch_producer_entry(void* const data) noexcept
{
    SBatchProducer& producer = *static_cast<SBatchProducer*>(data);
    while (!producer.start->load(std::memory_order_acquire))
    {
        platform::threading::processor_relax();
    }
    producer.queued_all = true;
    for (std::uint32_t index = 0u; index < 64u; ++index)
    {
        if (producer.client->submit(&run_batch_item, &producer.objects[index],
            producer.first_correlation + index).status != threading::EBatchSubmission::queued)
        {
            producer.queued_all = false;
            break;
        }
    }
    return producer.queued_all ? 0u : 1u;
}

void test_batch_concurrent_producers(TTestContext& ctx)
{
    threading::SBatchShared shared;
    threading::CThreadControlState controls[2];
    threading::CWaitPredicate requester_wakes[2];
    TEST_EXPECT(ctx, shared.wake.acquire_control());
    TEST_EXPECT(ctx, requester_wakes[0].acquire_control());
    TEST_EXPECT(ctx, requester_wakes[1].acquire_control());
    shared.runner_count = 1u;
    threading::CBatchClient clients[]{
        { shared, controls[0], requester_wakes[0], module_ids::executable, &shared },
        { shared, controls[1], requester_wakes[1], module_ids::executable, &shared }
    };
    std::uint32_t objects[2][64]{};
    std::atomic<bool> start{ false };
    SBatchProducer producers[]{
        { &clients[0], &start, objects[0], 1u },
        { &clients[1], &start, objects[1], 1u }
    };
    platform::threading::CThread threads[2];
    const bool first_created = threads[0].create(&batch_producer_entry, &producers[0]);
    const bool second_created = threads[1].create(&batch_producer_entry, &producers[1]);
    start.store(true, std::memory_order_release);
    TEST_EXPECT(ctx, first_created);
    TEST_EXPECT(ctx, second_created);
    if (first_created)
    {
        TEST_EXPECT(ctx, threads[0].join_and_close());
    }
    if (second_created)
    {
        TEST_EXPECT(ctx, threads[1].join_and_close());
    }
    TEST_EXPECT(ctx, producers[0].queued_all);
    TEST_EXPECT(ctx, producers[1].queued_all);
    TEST_EXPECT(ctx, shared.wake.get_word() == 128u);
    TEST_EXPECT(ctx, shared.work.outstanding_count() == 128u);

    for (std::uint32_t index = 0u; index < 128u; ++index)
    {
        threading::transports::TAcquiredArenaSlot<threading::SBatchWork,
            threading::k_batch_channel_capacity> work(shared.work);
        TEST_EXPECT(ctx, work.is_ready());
        if (work)
        {
            const threading::SBatchWork item = *work;
            item.function(item.object);
            TEST_EXPECT(ctx, item.requester->complete({ item.correlation, threading::EBatchCompletion::executed }));
        }
    }
    for (std::uint32_t producer = 0u; producer < 2u; ++producer)
    {
        for (std::uint32_t index = 0u; index < 64u; ++index)
        {
            threading::SBatchResponse response;
            TEST_EXPECT(ctx, clients[producer].receive(response));
            TEST_EXPECT(ctx, response.correlation == index + 1u);
            TEST_EXPECT(ctx, response.completion == threading::EBatchCompletion::executed);
            TEST_EXPECT(ctx, objects[producer][index] == 1u);
        }
        TEST_EXPECT(ctx, clients[producer].outstanding() == 0u);
        requester_wakes[producer].release_control();
    }
    TEST_EXPECT(ctx, shared.work.outstanding_count() == 0u);
    shared.wake.release_control();
}

struct SBatchReturnRunners
{
    threading::SBatchShared& shared;
    std::atomic<bool> stopping{ false };
    std::atomic<bool> failed{ false };

    static std::uint32_t MV_STD_ABI_CALL run(void* const data) noexcept
    {
        auto& self = *static_cast<SBatchReturnRunners*>(data);
        while (!self.stopping.load(std::memory_order_acquire))
        {
            threading::transports::TAcquiredArenaSlot<threading::SBatchWork,
                threading::k_batch_channel_capacity> acquired(self.shared.work);
            if (!acquired)
            {
                platform::threading::processor_relax();
                continue;
            }
            const auto item = *acquired;
            if (!acquired.recycle()) self.failed.store(true, std::memory_order_release);
            item.function(item.object);
            if (!item.requester->complete({ item.correlation, threading::EBatchCompletion::executed }))
            {
                self.failed.store(true, std::memory_order_release);
            }
        }
        return 0u;
    }
};

void test_batch_concurrent_returns(TTestContext& ctx)
{
    threading::SBatchShared shared;
    threading::CThreadControlState control;
    threading::CWaitPredicate wake;
    TEST_EXPECT(ctx, shared.wake.acquire_control());
    TEST_EXPECT(ctx, wake.acquire_control());
    shared.runner_count = 4u;
    threading::CBatchClient client(shared, control, wake, module_ids::executable, &shared);
    std::uint32_t objects[threading::k_batch_channel_capacity]{};
    SBatchReturnRunners state{ shared };
    platform::threading::CThread runners[4];
    for (auto& runner : runners)
    {
        TEST_EXPECT(ctx, runner.create(&SBatchReturnRunners::run, &state));
    }
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    platform::system::CPerfCounter timer;
    (void)timer.update();
    for (std::uint32_t round = 0u; round < 32u; ++round)
    {
        bool seen[threading::k_batch_channel_capacity]{};
        for (std::uint32_t index = 0u; index < threading::k_batch_channel_capacity; ++index)
        {
            const auto result = client.submit(&run_batch_item, &objects[index], index);
            TEST_EXPECT(ctx, result.status != threading::EBatchSubmission::rejected);
            seen[index] = result.status == threading::EBatchSubmission::completed_inline;
        }
        while ((client.outstanding() != 0u) && !state.failed.load(std::memory_order_acquire) &&
            (timer.query_delta() < conversion.query_ticks_per_second() * 10u))
        {
            threading::SBatchResponse response;
            if (client.receive(response))
            {
                TEST_EXPECT(ctx, response.correlation < threading::k_batch_channel_capacity);
                if (response.correlation < threading::k_batch_channel_capacity)
                {
                    TEST_EXPECT(ctx, !seen[response.correlation]);
                    seen[response.correlation] = true;
                }
                TEST_EXPECT(ctx, response.completion == threading::EBatchCompletion::executed);
            }
            else platform::threading::processor_relax();
        }
        TEST_EXPECT(ctx, client.outstanding() == 0u);
        if ((client.outstanding() != 0u) || state.failed.load(std::memory_order_acquire)) break;
        for (std::uint32_t index = 0u; index < threading::k_batch_channel_capacity; ++index)
        {
            TEST_EXPECT(ctx, seen[index] && (objects[index] == round + 1u));
        }
    }
    state.stopping.store(true, std::memory_order_release);
    for (auto& runner : runners)
    {
        if (runner.is_valid()) TEST_EXPECT(ctx, runner.join_and_close());
    }
    TEST_EXPECT(ctx, !state.failed.load(std::memory_order_acquire));
    TEST_EXPECT(ctx, client.return_publishers() == 0u);
    TEST_EXPECT(ctx, shared.work.outstanding_count() == 0u);
    wake.release_control();
    shared.wake.release_control();
}

struct SBatchShutdownFixture
{
    std::uint32_t object{ 0u };
    std::atomic<bool> submitted{ false };
    bool received{ false };
};

struct SBatchShutdownController
{
    threading::CThreadPackage& package;
    std::atomic<bool> started{ false };
    std::atomic<bool> finished{ false };

    static std::uint32_t MV_STD_ABI_CALL run(void* const data) noexcept
    {
        auto& self = *static_cast<SBatchShutdownController*>(data);
        self.started.store(true, std::memory_order_release);
        (void)self.package.shutdown();
        self.finished.store(true, std::memory_order_release);
        return 0u;
    }
};

std::uint32_t MV_STD_ABI_CALL batch_shutdown_requester(void* const data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    auto& fixture = *static_cast<SBatchShutdownFixture*>(resources.config.prepare_context);
    threading::CThreadContext context(resources);
    context.startup();
    context.mark_running();
    const auto result = resources.batch_client->submit(&run_batch_item, &fixture.object, 1u);
    fixture.submitted.store(result.status == threading::EBatchSubmission::queued, std::memory_order_release);
    while (resources.batch_client->outstanding() != 0u)
    {
        threading::SBatchResponse response;
        if (resources.batch_client->receive(response))
        {
            fixture.received = (response.correlation == 1u) && (fixture.object == 1u);
        }
        else platform::threading::processor_relax();
    }
    context.mark_exited();
    return 0u;
}

void test_batch_shutdown_waits_for_publisher(TTestContext& ctx)
{
    threading::SBatchShared shared;
    TEST_EXPECT(ctx, shared.wake.acquire_control());
    shared.runner_count = 1u;
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    SBatchShutdownFixture fixture;
    const threading::ThreadConfig config{ thread_ids::executive, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &batch_shutdown_requester, nullptr, &fixture };
    threading::CThreadPackage package(config, conversion);
    TEST_EXPECT(ctx, package.install_batch_client(shared, module_ids::executable, &shared));
    const bool started = package.startup();
    TEST_EXPECT(ctx, started);
    if (!started)
    {
        shared.wake.release_control();
        return;
    }
    while (!fixture.submitted.load(std::memory_order_acquire)) platform::threading::processor_relax();
    {
        threading::transports::TAcquiredArenaSlot<threading::SBatchWork,
            threading::k_batch_channel_capacity> work(shared.work);
        TEST_EXPECT(ctx, work.is_ready());
        if (work) work->function(work->object);
    }
    auto& client = *package.batch_client();
    TEST_EXPECT(ctx, threading::SBatchClientTestAccess::publish_before_wake(client,
        { 1u, threading::EBatchCompletion::executed }));
    while (package.query_state() != threading::EThreadRunState::Exited) platform::threading::processor_relax();
    TEST_EXPECT(ctx, fixture.received);
    TEST_EXPECT(ctx, client.outstanding() == 0u);
    SBatchShutdownController state{ package };
    platform::threading::CThread shutdown;
    const bool controller_created = shutdown.create(&SBatchShutdownController::run, &state);
    TEST_EXPECT(ctx, controller_created);
    if (!controller_created)
    {
        (void)threading::SBatchClientTestAccess::finish_wake(client);
        (void)package.shutdown();
        shared.wake.release_control();
        return;
    }
    while (!state.started.load(std::memory_order_acquire)) platform::threading::processor_relax();
    platform::system::CPerfCounter timer;
    (void)timer.update();
    while (!state.finished.load(std::memory_order_acquire) &&
        (timer.query_delta() < conversion.query_ticks_per_second() / 20u))
    {
        platform::threading::processor_relax();
    }
    TEST_EXPECT(ctx, !state.finished.load(std::memory_order_acquire));
    TEST_EXPECT(ctx, threading::SBatchClientTestAccess::finish_wake(client));
    TEST_EXPECT(ctx, shutdown.join_and_close());
    TEST_EXPECT(ctx, client.return_publishers() == 0u);
    shared.wake.release_control();
}

}   //  namespace

int run_host_workers_tests()
{
    TTestContext ctx;
    test_hardware_limits(ctx);
    test_batch_policy(ctx);
    test_batch_routes(ctx);
    test_batch_concurrent_producers(ctx);
    test_batch_concurrent_returns(ctx);
    test_batch_shutdown_waits_for_publisher(ctx);
    std::cout << "HostWorkers: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
