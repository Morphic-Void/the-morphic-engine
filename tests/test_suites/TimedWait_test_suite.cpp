
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TimedWait_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26

#include "threading/messages/message_errors.hpp"
#include <atomic>
#include <chrono>
#include <limits>
#include <thread>

#include "platform/platform_defines.hpp"
#include "platform/threading/wait_deadline_math.hpp"
#include "platform/threading/wait_word.hpp"
#include "platform/threading/wait_word_internal.hpp"
#include "threading/CThreadPackage.hpp"
#include "tests/support/test_context.hpp"
#include "tests/test_suites/TimedWait_test_suite.hpp"

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
#include <csignal>
#include <pthread.h>
#endif

namespace
{
using tests::TTestContext;
using namespace platform::threading;
using namespace std::chrono_literals;

template<typename F>
bool await(F predicate)
{
    const auto limit = std::chrono::steady_clock::now() + 2s;
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= limit) return false;
        std::this_thread::yield();
    }
    return true;
}

CWaitDeadline deadline(TTestContext& ctx, const std::uint64_t milliseconds)
{
    CWaitDeadline result;
    TEST_EXPECT(ctx, result.set_after_milliseconds(milliseconds).code == EWaitError::none);
    return result;
}

void test_conversion_and_invalid_requests(TTestContext& ctx)
{
    std::uint64_t result = 99u;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    TEST_EXPECT(ctx, internal::scale_wait_duration(maximum, 3u, 3u, result) && result == maximum);
    TEST_EXPECT(ctx, internal::scale_wait_duration(1u, 1u, 100u, result) && result == 1u);
    TEST_EXPECT(ctx, internal::scale_wait_duration(101u, 1u, 100u, result) && result == 2u);
    TEST_EXPECT(ctx, internal::scale_wait_duration(0u, 1u, 100u, result) && result == 0u);
    TEST_EXPECT(ctx, !internal::scale_wait_duration(maximum, 2u, 1u, result));
    TEST_EXPECT(ctx, !internal::scale_wait_duration(1u, 1u, 0u, result));
    TEST_EXPECT(ctx, internal::finite_wait_milliseconds(0u) == 0u);
    TEST_EXPECT(ctx, internal::finite_wait_milliseconds(1u) == 1u);
    TEST_EXPECT(ctx, internal::finite_wait_milliseconds(1000000u) == 1u);
    TEST_EXPECT(ctx, internal::finite_wait_milliseconds(1000001u) == 2u);
    TEST_EXPECT(ctx, internal::finite_wait_milliseconds(maximum) == 0xfffffffeu);

    CWaitDeadline invalid;
    std::atomic<std::uint32_t> word{ 0u };
    TEST_EXPECT(ctx, !invalid.is_valid());
    TEST_EXPECT(ctx, invalid.remaining_nanoseconds(result).code == EWaitError::invalid_argument && result == 0u);
    TEST_EXPECT(ctx, wait_until_equal(word, 0u, invalid).status == EWaitStatus::invalid_argument);
    TEST_EXPECT(ctx, wait_while_equal(word, 0u, invalid).status == EWaitWordStatus::invalid_argument);
    TEST_EXPECT(ctx, invalid.set_after_nanoseconds(1u).code == EWaitError::none);
    TEST_EXPECT(ctx, invalid.set_after_nanoseconds(maximum).code == EWaitError::invalid_argument);
    TEST_EXPECT(ctx, !invalid.is_valid());
    TEST_EXPECT(ctx, invalid.set_after_milliseconds(maximum).code == EWaitError::invalid_argument);
    TEST_EXPECT(ctx, !invalid.is_valid());
#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
    TEST_EXPECT(ctx, invalid.set_after_nanoseconds(static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max())).code == EWaitError::invalid_argument);
    TEST_EXPECT(ctx, !invalid.is_valid());
#endif

    threading::CWaitPredicate predicate;
    threading::CParkingTicket ticket;
    TEST_EXPECT(ctx, predicate.acquire_control());
    const auto now = deadline(ctx, 0u);
    TEST_EXPECT(ctx, predicate.wait_until_equal(ticket, ~0u, now).status == EWaitStatus::invalid_argument);
    TEST_EXPECT(ctx, predicate.wait_until_not_equal(ticket, 0u, invalid).status == EWaitStatus::invalid_argument);
    predicate.release_control();
}

void test_immediate_and_expired(TTestContext& ctx)
{
    std::atomic<std::uint32_t> word{ 7u };
    const auto now = deadline(ctx, 0u);
    TEST_EXPECT(ctx, wait_until_equal(word, 7u, now).status == EWaitStatus::satisfied);
    const auto changed = wait_until_not_equal(word, 0u, now);
    TEST_EXPECT(ctx, changed.status == EWaitStatus::satisfied && changed.observed == 7u);
    TEST_EXPECT(ctx, wait_until_equal(word, 8u, now).status == EWaitStatus::timed_out);
    TEST_EXPECT(ctx, wait_until_not_equal(word, 7u, now).status == EWaitStatus::timed_out);
    TEST_EXPECT(ctx, wait_while_equal(word, 0u, deadline(ctx, 1000u)).status == EWaitWordStatus::awoken_or_changed);
    TEST_EXPECT(ctx, wait_while_equal(word, 7u, now).status == EWaitWordStatus::timed_out);

    threading::CWaitPredicate predicate;
    threading::CParkingTicket ticket;
    TEST_EXPECT(ctx, predicate.acquire_control());
    TEST_EXPECT(ctx, predicate.set_and_wake_all(7u)); // Notification precedes registration.
    TEST_EXPECT(ctx, predicate.wait_until_equal(ticket, 7u, now).status == EWaitStatus::satisfied);
    TEST_EXPECT(ctx, predicate.wait_until_not_equal(ticket, 0u, now).observed == 7u);
    TEST_EXPECT(ctx, predicate.wait_until_not_equal(ticket, 7u, now).status == EWaitStatus::timed_out);
    const auto expires = deadline(ctx, 20u);
    const auto result = predicate.wait_until_equal(ticket, 8u, expires);
    std::uint64_t remaining = 1u;
    TEST_EXPECT(ctx, result.status == EWaitStatus::timed_out && result.observed == 7u);
    TEST_EXPECT(ctx, expires.remaining_nanoseconds(remaining).code == EWaitError::none && remaining == 0u);
    TEST_EXPECT(ctx, predicate.wait_until_equal(ticket, 7u, expires).status == EWaitStatus::satisfied);
    predicate.release_control();
    TEST_EXPECT(ctx, predicate.wait_until_equal(ticket, 7u, now).status == EWaitStatus::control_released);
    TEST_EXPECT(ctx, predicate.wait_until_not_equal(ticket, 7u, now).status == EWaitStatus::control_released);
}

void test_notification_and_release(TTestContext& ctx)
{
    for (unsigned mode = 0u; mode != 4u; ++mode)
    {
        threading::CWaitPredicate predicate;
        TEST_EXPECT(ctx, predicate.acquire_control());
        const auto limit = deadline(ctx, 2000u);
        std::atomic<bool> entered{ false };
        WaitResult result;
        std::thread waiter([&] {
            threading::CParkingTicket ticket;
            entered.store(true, std::memory_order_release);
            result = (mode % 2u == 0u) ? predicate.wait_until_equal(ticket, 9u, limit) :
                predicate.wait_until_not_equal(ticket, 0u, limit);
        });
        TEST_EXPECT(ctx, await([&] { return entered.load(std::memory_order_acquire); }));
        //  Give the native wait an opportunity to park. Correctness also holds
        //  if the worker was preempted before its compare-and-park.
        std::this_thread::sleep_for(5ms);
        if (mode < 2u) TEST_EXPECT(ctx, predicate.set_and_wake_all(9u));
        else predicate.release_control();
        waiter.join();
        TEST_EXPECT(ctx, result.status == (mode < 2u ? EWaitStatus::satisfied : EWaitStatus::control_released));
        TEST_EXPECT(ctx, result.observed == (mode < 2u ? 9u : ~0u));
        if (mode < 2u) predicate.release_control();
    }
}

void test_wake_storm_and_shared_deadline(TTestContext& ctx, const bool change_word)
{
    threading::CWaitPredicate predicate;
    TEST_EXPECT(ctx, predicate.acquire_control());
    std::atomic<bool> entered{ false }, done{ false };
    WaitResult result;
    CWaitDeadline limit;
    std::thread waiter([&] {
        threading::CParkingTicket ticket;
        const auto error = limit.set_after_milliseconds(40u);
        if (error.code != EWaitError::none) return;
        entered.store(true, std::memory_order_release);
        result = predicate.wait_until_equal(ticket, 9u, limit);
        done.store(true, std::memory_order_release);
    });
    TEST_EXPECT(ctx, await([&] { return entered.load(std::memory_order_acquire); }));
    const bool bounded = await([&] {
        if (change_word) (void)predicate.set(predicate.get_word() ^ 1u);
        (void)predicate.wake_all_waiters();
        std::this_thread::sleep_for(1ms);
        return done.load(std::memory_order_acquire);
    });
    TEST_EXPECT(ctx, bounded);
    //  Cleanup also lets a broken implementation stop after the bounded probe.
    if (!bounded) (void)predicate.set_and_wake_all(9u);
    waiter.join();
    TEST_EXPECT(ctx, result.status == EWaitStatus::timed_out && result.observed <= 1u);
    threading::CParkingTicket ticket;
    TEST_EXPECT(ctx, predicate.wait_until_equal(ticket, 9u, limit).status == EWaitStatus::timed_out);
    predicate.release_control();
}

void test_unnotified_completion_and_races(TTestContext& ctx)
{
    //  Completion without notification must be observed at the timeout boundary.
    for (unsigned iteration = 0u; iteration != 18u; ++iteration)
    {
        threading::CWaitPredicate predicate;
        TEST_EXPECT(ctx, predicate.acquire_control());
        std::atomic<bool> entered{ false };
        WaitResult result;
        std::thread waiter([&] {
            threading::CParkingTicket ticket;
            CWaitDeadline limit;
            (void)limit.set_after_milliseconds(6u);
            entered.store(true, std::memory_order_release);
            result = predicate.wait_until_not_equal(ticket, 0u, limit);
        });
        TEST_EXPECT(ctx, await([&] { return entered.load(std::memory_order_acquire); }));
        std::this_thread::sleep_for(std::chrono::milliseconds(iteration % 3u * 3u));
        TEST_EXPECT(ctx, predicate.set(1u));
        waiter.join();
        TEST_EXPECT(ctx, (result.status == EWaitStatus::satisfied && result.observed == 1u) ||
            (result.status == EWaitStatus::timed_out && result.observed == 0u));
        //  Whichever racing result won, an expired follow-up sees completion.
        threading::CParkingTicket ticket;
        TEST_EXPECT(ctx, predicate.wait_until_not_equal(ticket, 0u, deadline(ctx, 0u)).status == EWaitStatus::satisfied);
        predicate.release_control();
    }
}

void test_native_return_boundary(TTestContext& ctx)
{
    //  Exercise the production predicate loop with a controlled native-return
    //  boundary. No scheduling assumption is needed for the final recheck.
    for (const bool equal : { false, true })
    {
        std::atomic<std::uint32_t> word{ 0u };
        const auto completed = internal::wait_predicate_loop(word, equal ? 1u : 0u, equal, true,
            [&](std::uint32_t) noexcept {
                word.store(1u, std::memory_order_release);
                return WaitWordResult{ EWaitWordStatus::timed_out, 0u };
            });
        TEST_EXPECT(ctx, completed.status == EWaitStatus::satisfied && completed.observed == 1u);
        word.store(0u);
        const auto released = internal::wait_predicate_loop(word, equal ? 1u : 0u, equal, true,
            [&](std::uint32_t) noexcept {
                word.store(~0u, std::memory_order_release);
                return WaitWordResult{ EWaitWordStatus::timed_out, 0u };
            });
        TEST_EXPECT(ctx, released.status == EWaitStatus::control_released && released.observed == ~0u);
    }
    const EWaitWordStatus native_statuses[]{ EWaitWordStatus::timed_out,
        EWaitWordStatus::native_failure, EWaitWordStatus::unsupported, EWaitWordStatus::invalid_argument };
    const EWaitStatus expected[]{ EWaitStatus::timed_out, EWaitStatus::native_failure,
        EWaitStatus::unsupported, EWaitStatus::invalid_argument };
    for (unsigned index = 0u; index != 4u; ++index)
    {
        std::atomic<std::uint32_t> word{ 0u };
        unsigned calls = 0u;
        const auto result = internal::wait_predicate_loop(word, 1u, true, false,
            [&](std::uint32_t) noexcept {
                //  A spurious return must not be reported as satisfaction.
                if (++calls == 1u) return WaitWordResult{ EWaitWordStatus::awoken_or_changed, 0u };
                return WaitWordResult{ native_statuses[index], 123u };
            });
        TEST_EXPECT(ctx, calls == 2u && result.status == expected[index] && result.observed == 0u);
        TEST_EXPECT(ctx, result.native_error == (index == 0u ? 0u : 123u));
    }
}

void test_indefinite_waits(TTestContext& ctx)
{
    threading::CWaitPredicate predicate;
    TEST_EXPECT(ctx, predicate.acquire_control());
    std::atomic<bool> entered{ false }, done{ false };
    bool equal = false;
    std::thread waiter([&] {
        threading::CParkingTicket ticket;
        entered.store(true, std::memory_order_release);
        equal = predicate.wait_until_equal(ticket, 3u);
        done.store(true, std::memory_order_release);
    });
    TEST_EXPECT(ctx, await([&] { return entered.load(std::memory_order_acquire); }));
    TEST_EXPECT(ctx, predicate.wake_all_waiters());
    std::this_thread::sleep_for(10ms);
    TEST_EXPECT(ctx, !done.load(std::memory_order_acquire));
    TEST_EXPECT(ctx, predicate.set_and_wake_one(3u));
    waiter.join();
    TEST_EXPECT(ctx, equal);
    threading::CParkingTicket ticket;
    TEST_EXPECT(ctx, predicate.wait_until_not_equal(ticket, 0u) == 3u);
    predicate.release_control();
    TEST_EXPECT(ctx, !predicate.wait_until_equal(ticket, 3u));
    TEST_EXPECT(ctx, predicate.wait_until_not_equal(ticket, 3u) == ~0u);

    std::atomic<std::uint32_t> word{ 4u };
    wait_until_equal(word, 4u);
    TEST_EXPECT(ctx, wait_until_not_equal(word, 0u) == 4u);
}

struct ServiceProbe
{
    std::atomic<unsigned> stage{ 0u };
    WaitResult timeout, message, exit;
    bool running_after_timeout{ false };
    bool received{ false };
};

std::uint32_t MV_STD_ABI_CALL timed_service(void* data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    auto& probe = *static_cast<ServiceProbe*>(resources.config.prepare_context);
    threading::CThreadContext context(resources);
    context.startup();
    context.mark_running();
    CWaitDeadline limit;
    (void)limit.set_after_milliseconds(20u);
    probe.timeout = context.wait_for_new_epoch(0u, limit);
    probe.running_after_timeout = resources.control_state.query_state() == threading::EThreadRunState::Running;
    (void)limit.set_after_milliseconds(5000u);
    probe.stage.store(1u, std::memory_order_release);
    probe.message = context.wait_for_new_epoch(0u, limit);
    threading::CErasedPodMsg message;
    probe.received = context.read(message) && message.is_payload_a<threading::UnrecognisedMsg>();
    const auto epoch = probe.message.observed;
    probe.stage.store(2u, std::memory_order_release);
    probe.exit = context.wait_for_new_epoch(epoch, limit);
    if (context.exit_requested()) context.mark_exited();
    else context.mark_failed(1u);
    return 0u;
}

void test_service_message_and_exit(TTestContext& ctx)
{
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    ServiceProbe probe;
    threading::ThreadConfig config{ thread_ids::executive, module_ids::executable,
        EThreadPriority::Normal, &timed_service, nullptr, &probe };
    threading::CThreadPackage package(config, conversion);
    const bool started = package.startup();
    TEST_EXPECT(ctx, started);
    if (!started) return;
    TEST_EXPECT(ctx, await([&] { return probe.stage.load(std::memory_order_acquire) >= 1u &&
        package.query_state() == threading::EThreadRunState::Waiting; }));
    threading::CErasedPodMsg message;
    message.assign_payload(threading::UnrecognisedMsg{});
    TEST_EXPECT(ctx, package.post(message));
    TEST_EXPECT(ctx, await([&] { return probe.stage.load(std::memory_order_acquire) >= 2u &&
        package.query_state() == threading::EThreadRunState::Waiting; }));
    package.request_exit();
    TEST_EXPECT(ctx, await([&] { return package.query_state() == threading::EThreadRunState::Exited; }));
    TEST_EXPECT(ctx, package.shutdown());
    TEST_EXPECT(ctx, probe.timeout.status == EWaitStatus::timed_out && probe.running_after_timeout);
    TEST_EXPECT(ctx, probe.message.status == EWaitStatus::satisfied && probe.received);
    TEST_EXPECT(ctx, probe.exit.status == EWaitStatus::satisfied);
}

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
std::atomic<unsigned> signal_count{ 0u };
void interrupt_wait(int) { signal_count.fetch_add(1u, std::memory_order_relaxed); }

void test_interruptions(TTestContext& ctx)
{
    struct sigaction action{}, previous{};
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    signal_count.store(0u);
    action.sa_handler = interrupt_wait;
    sigemptyset(&action.sa_mask);
    const bool installed = sigaction(SIGUSR1, &action, &previous) == 0;
    TEST_EXPECT(ctx, installed);
    if (!installed) return;
    std::atomic<std::uint32_t> word{ 0u };
    std::atomic<bool> entered{ false }, done{ false }, leave{ false };
    WaitResult result;
    std::thread waiter([&] {
        CWaitDeadline limit;
        (void)limit.set_after_milliseconds(40u);
        entered.store(true, std::memory_order_release);
        result = wait_until_not_equal(word, 0u, limit);
        done.store(true, std::memory_order_release);
        //  Keep the pthread handle alive through the final pthread_kill.
        while (!leave.load(std::memory_order_acquire)) std::this_thread::yield();
    });
    TEST_EXPECT(ctx, await([&] { return entered.load(std::memory_order_acquire); }));
    const bool bounded = await([&] {
        (void)pthread_kill(waiter.native_handle(), SIGUSR1);
        std::this_thread::sleep_for(1ms);
        return done.load(std::memory_order_acquire);
    });
    TEST_EXPECT(ctx, bounded);
    if (!bounded)
    {
        word.store(1u, std::memory_order_release);
        wake_all_waiters(word);
    }
    leave.store(true, std::memory_order_release);
    waiter.join();
    TEST_EXPECT(ctx, result.status == EWaitStatus::timed_out);
    TEST_EXPECT(ctx, signal_count.load() != 0u);
    TEST_EXPECT(ctx, sigaction(SIGUSR1, &previous, nullptr) == 0);
}
#endif
}   //  namespace

int run_timed_wait_tests()
{
    TTestContext ctx;
    test_conversion_and_invalid_requests(ctx);
    test_immediate_and_expired(ctx);
    test_notification_and_release(ctx);
    test_wake_storm_and_shared_deadline(ctx, false);
    test_wake_storm_and_shared_deadline(ctx, true);
    test_unnotified_completion_and_races(ctx);
    test_native_return_boundary(ctx);
    test_indefinite_waits(ctx);
    test_service_message_and_exit(ctx);
#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
    test_interruptions(ctx);
#endif
    return ctx.report("TimedWait");
}
