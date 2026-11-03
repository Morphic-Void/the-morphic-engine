
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    Notification_test_cases.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Deterministic clear/publication ordering and pinned package teardown.

#include <thread>
#include "threading/CThreadPackage.hpp"
#include "threading/messages/message_errors.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using tests::TTestContext;

template<typename F> bool await(F condition)
{
    threading::CWaitDeadline limit;
    if (limit.set_after_milliseconds(5000u).code != platform::threading::EWaitError::none) return false;
    while (!condition())
    {
        std::uint64_t remaining = 0u;
        if (limit.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none || !remaining) return false;
        std::this_thread::yield();
    }
    return true;
}

void publication_order(TTestContext& ctx, const unsigned placement)
{
    threading::CWaitPredicate predicate;
    threading::CNotification notification;
    TEST_EXPECT(ctx, predicate.acquire_control() && notification.initialise(predicate));
    const auto endpoint = notification.endpoint();
    const auto reader = notification.reader();
    TEST_EXPECT(ctx, endpoint.signal());
    const auto epoch = predicate.get_word();
    std::atomic<unsigned> durable{ 0u };
    std::atomic<bool> publish{ false }, done{ false };
    bool signalled = false;
    std::thread producer([&] {
        if (await([&] { return publish.load(std::memory_order_acquire); }))
        {
            durable.store(1u, std::memory_order_release);
            signalled = endpoint.signal();
        }
        done.store(true, std::memory_order_release);
    });
    auto release_producer = [&] {
        publish.store(true, std::memory_order_release);
        TEST_EXPECT(ctx, await([&] { return done.load(std::memory_order_acquire); }));
    };
    if (placement == 0u) release_producer(); //  Coalesced before clearing.
    reader.clear_pending();
    if (placement == 1u) release_producer(); //  Between clear and recheck.
    const auto observed = durable.load(std::memory_order_acquire);
    if (placement == 2u) release_producer(); //  After final check, before wait.
    if (!observed)
    {
        threading::CParkingTicket ticket;
        threading::CWaitDeadline limit;
        TEST_EXPECT(ctx, limit.set_after_milliseconds(500u).code == platform::threading::EWaitError::none);
        TEST_EXPECT(ctx, predicate.wait_until_not_equal(ticket, epoch, limit).status == threading::EWaitStatus::satisfied);
    }
    producer.join();
    TEST_EXPECT(ctx, signalled && durable.load(std::memory_order_acquire) == 1u && !reader.failed());
    TEST_EXPECT(ctx, placement ? predicate.get_word() != epoch : predicate.get_word() == epoch);
    if (placement == 0u) TEST_EXPECT(ctx, observed == 1u && !reader.pending());
    else TEST_EXPECT(ctx, reader.pending());
    reader.clear_pending();
    predicate.release_control();
}

void coalescing_and_failure(TTestContext& ctx)
{
    threading::CWaitPredicate predicate;
    threading::CNotification notification;
    TEST_EXPECT(ctx, predicate.acquire_control() && notification.initialise(predicate));
    TEST_EXPECT(ctx, !notification.initialise(predicate));
    const auto endpoint = notification.endpoint();
    const auto reader = notification.reader();
    const auto epoch = predicate.get_word();
    for (unsigned index = 0u; index < 100u; ++index) TEST_EXPECT(ctx, endpoint.signal());
    TEST_EXPECT(ctx, predicate.get_word() == epoch + 1u && reader.pending());
    reader.clear_pending();
    TEST_EXPECT(ctx, endpoint.signal() && predicate.get_word() == epoch + 2u);
    reader.clear_pending();
    //  Deliberately violate the target-lifetime precondition to check failure reporting.
    predicate.release_control();
    TEST_EXPECT(ctx, !endpoint.signal() && endpoint.failed() && reader.failed());
    reader.clear_pending();
    TEST_EXPECT(ctx, reader.failed() && !endpoint.signal());
    TEST_EXPECT(ctx, !threading::SNotificationEndpoint{}.signal());
}

struct SProbe
{
    std::atomic<unsigned> stage{ 0u };
    bool message_received{ false };
    bool exit_received{ false };
};

std::uint32_t MV_STD_ABI_CALL waiting_consumer(void* data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    auto& probe = *static_cast<SProbe*>(resources.config.prepare_context);
    threading::CThreadContext context{ resources };
    context.startup();
    context.mark_running();
    threading::CWaitDeadline limit;
    (void)limit.set_after_milliseconds(5000u);
    for (;;)
    {
        const auto epoch = context.observe_epoch();
        threading::CErasedPodMsg message;
        if (context.read(message)) probe.message_received = message.is_payload_a<threading::UnrecognisedMsg>();
        probe.stage.store(probe.message_received ? 2u : 1u, std::memory_order_release);
        if (context.exit_requested()) { probe.exit_received = true; break; }
        if (context.wait_for_new_epoch(epoch, limit).status != threading::EWaitStatus::satisfied) break;
    }
    context.mark_exited();
    return 0u;
}

void pinned_package(TTestContext& ctx)
{
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    SProbe probe;
    threading::ThreadConfig config{ thread_ids::executive, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &waiting_consumer, nullptr, &probe };
    threading::CThreadPackage package{ config, conversion };
    if (!package.startup()) { TEST_EXPECT(ctx, false); return; }
    threading::CNotification notification;
    TEST_EXPECT(ctx, package.pin_notification(notification));
    TEST_EXPECT(ctx, await([&] { return probe.stage.load(std::memory_order_acquire) >= 1u; }));
    TEST_EXPECT(ctx, notification.endpoint().signal());
    threading::CErasedPodMsg message;
    message.assign_payload(threading::UnrecognisedMsg{});
    TEST_EXPECT(ctx, package.post(message)); //  A pending latch must not suppress message or exit wakes.
    TEST_EXPECT(ctx, await([&] { return probe.stage.load(std::memory_order_acquire) >= 2u; }));
    package.request_exit();
    TEST_EXPECT(ctx, await([&] { return package.query_state() == threading::EThreadRunState::Exited; }));
    TEST_EXPECT(ctx, !package.shutdown());
    notification.reader().clear_pending();
    TEST_EXPECT(ctx, notification.endpoint().signal()); //  Final publisher after consumer exit remains safe.
    package.unpin_notification();
    TEST_EXPECT(ctx, package.shutdown());
    TEST_EXPECT(ctx, probe.message_received && probe.exit_received);
}

}   //  namespace

void run_notification_tests(tests::TTestContext& ctx)
{
    for (unsigned placement = 0u; placement < 3u; ++placement) publication_order(ctx, placement);
    coalescing_and_failure(ctx);
    pinned_package(ctx);
}
