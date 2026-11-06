
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File: HostWait_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date: 06 Nov 26
//
//  Controlled publication interleavings and native/portable Host wait behaviour.

#include <atomic>
#include <chrono>
#include <thread>
#include "tests/test_suites/HostWait_test_cases.hpp"
#include "platform/threading/owner_wait.hpp"
#include "host/runtime/host_cadence.hpp"
#include "threading/CThreadPackage.hpp"
#include "threading/messages/message_errors.hpp"
#include "debug/service.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

namespace platform::threading
{
struct SOwnerWaitTestAccess
{
    static void hooks(COwnerWait& owner, void* const context,
        void (*const ack)(void*), void (*const signal)(void*), void (*const park)(void*)) noexcept
    {
        owner.m_test_context = context;
        owner.m_test_before_ack = ack;
        owner.m_test_before_signal = signal;
        owner.m_test_before_park = park;
    }
    static void fail(COwnerWait& owner) noexcept { owner.m_failure.store(123u, std::memory_order_release); }
};
}

namespace
{
using tests::TTestContext;
using namespace platform::threading;

CWaitDeadline limit(TTestContext& ctx, const std::uint64_t milliseconds)
{
    CWaitDeadline deadline;
    TEST_EXPECT(ctx, (deadline.set_after_milliseconds(milliseconds).code == EWaitError::none));
    return deadline;
}

struct SPublication
{
    SWakeEndpoint wake;
    std::atomic<std::uint32_t> value{ 0u };
    std::atomic<bool> entered{ false }, proceed{ false };

    static void publish(void* const context)
    {
        SPublication& self = *static_cast<SPublication*>(context);
        self.value.store(42u, std::memory_order_relaxed);
        (void)self.wake.signal();
    }
    static void delay_signal(void* const context)
    {
        SPublication& self = *static_cast<SPublication*>(context);
        self.entered.store(true, std::memory_order_release);
        while (!self.proceed.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    static void resume_signal(void* const context)
    {
        static_cast<SPublication*>(context)->proceed.store(true, std::memory_order_release);
    }
    static void release_owner(void* const context)
    {
        static_cast<COwnerWait*>(context)->release();
    }
};

bool await(const std::atomic<bool>& flag)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!flag.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::yield();
    }
    return true;
}

void publication_boundaries(TTestContext& ctx)
{
    COwnerWait owner;
    TEST_EXPECT(ctx, !owner.endpoint().is_valid());
    TEST_EXPECT(ctx, (owner.initialise().code == EWaitError::none));
    SPublication publication;
    publication.wake = owner.endpoint();
    const CWaitDeadline immediate = limit(ctx, 0u);

    //  Coalesced work preceding acknowledgement belongs to the durable pass.
    SPublication::publish(&publication);
    SPublication::publish(&publication);
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    TEST_EXPECT(ctx, (publication.value.load(std::memory_order_relaxed) == 42u));
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::timed_out));

    //  Publication between native reset and pending acknowledgement is acquired.
    publication.value.store(0u, std::memory_order_relaxed);
    SOwnerWaitTestAccess::hooks(owner, &publication, &SPublication::publish, nullptr, nullptr);
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    TEST_EXPECT(ctx, (publication.value.load(std::memory_order_relaxed) == 42u));
    SOwnerWaitTestAccess::hooks(owner, nullptr, nullptr, nullptr, nullptr);
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));

    //  A publication after the final check remains observable even at expiry.
    TEST_EXPECT(ctx, publication.wake.signal());
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    SOwnerWaitTestAccess::hooks(owner, &publication, nullptr, nullptr, &SPublication::publish);
    TEST_EXPECT(ctx, (owner.wait(limit(ctx, 1000u)).status == EOwnerWaitStatus::activity));
    SOwnerWaitTestAccess::hooks(owner, nullptr, nullptr, nullptr, nullptr);

    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    SOwnerWaitTestAccess::hooks(owner, &owner, nullptr, nullptr, &SPublication::release_owner);
    TEST_EXPECT(ctx, (owner.wait(limit(ctx, 1000u)).status == EOwnerWaitStatus::released));
    SOwnerWaitTestAccess::hooks(owner, nullptr, nullptr, nullptr, nullptr);
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::released));
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::released));
}

void delayed_signal(TTestContext& ctx)
{
    COwnerWait owner;
    TEST_EXPECT(ctx, (owner.initialise().code == EWaitError::none));
    SPublication publication;
    publication.wake = owner.endpoint();
    SOwnerWaitTestAccess::hooks(owner, &publication, nullptr, &SPublication::delay_signal, &SPublication::resume_signal);
    std::thread producer([&] { SPublication::publish(&publication); });
    const bool entered = await(publication.entered);
    TEST_EXPECT(ctx, entered);
    if (entered)
    {
        TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
        TEST_EXPECT(ctx, (publication.value.load(std::memory_order_relaxed) == 42u));
        //  Windows remembers the late SetEvent; wait words may legitimately
        //  deliver a spurious return or reach the unchanged finite deadline.
        const SOwnerWaitResult result = owner.wait(limit(ctx, 10u));
        TEST_EXPECT(ctx, ((result.status == EOwnerWaitStatus::activity) || (result.status == EOwnerWaitStatus::timed_out)));
    }
    publication.proceed.store(true, std::memory_order_release);
    producer.join();
    SOwnerWaitTestAccess::hooks(owner, nullptr, nullptr, nullptr, nullptr);
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    TEST_EXPECT(ctx, (owner.wait(limit(ctx, 0u)).status == EOwnerWaitStatus::timed_out));
    SOwnerWaitTestAccess::fail(owner);
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::native_failure));
    TEST_EXPECT(ctx, !owner.endpoint().signal());
    TEST_EXPECT(ctx, (owner.wait(limit(ctx, 0u)).status == EOwnerWaitStatus::native_failure));
}

void adapters(TTestContext& ctx)
{
    COwnerWait owner;
    TEST_EXPECT(ctx, (owner.initialise().code == EWaitError::none));
    const CWaitDeadline immediate = limit(ctx, 0u);
#if !MV_PLATFORM_WINDOWS
    TEST_EXPECT(ctx, (owner.wait(immediate, true).status == EOwnerWaitStatus::unsupported));
#endif
    std::atomic<std::uint32_t> pending{ 0u }, stop{ 0u };
    threading::CNotification notification;
    TEST_EXPECT(ctx, notification.initialise(pending, owner.endpoint()));
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    TEST_EXPECT(ctx, notification.endpoint().signal());
    TEST_EXPECT(ctx, notification.endpoint().signal());
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));
    TEST_EXPECT(ctx, (pending.load(std::memory_order_acquire) == 1u));
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    notification.reader().clear_pending();
    TEST_EXPECT(ctx, notification.endpoint().signal());
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));

    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    const modules::SHostStopEndpoint endpoint{ &stop, owner.endpoint() };
    TEST_EXPECT(ctx, endpoint.request());
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));
    TEST_EXPECT(ctx, (stop.load(std::memory_order_acquire) == 1u));

    threading::CThreadControlState control;
    control.install_owner_wake(owner.endpoint());
    control.mark_startup();
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    control.mark_running();
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    control.mark_waiting();
    control.mark_running();
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::timed_out));
    control.mark_exited();
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));

    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    const threading::ThreadConfig config{ thread_ids::executive, module_ids::executable };
    threading::CThreadResources resources(config, conversion);
    resources.owner_wake = owner.endpoint();
    TEST_EXPECT(ctx, resources.worker_to_host_msgs.initialise_growable(0u));
    TEST_EXPECT(ctx, resources.host_to_worker_msgs.initialise_growable(0u));
    threading::CThreadContext context(resources);
    threading::CErasedPodMsg message;
    message.assign_payload(threading::UnrecognisedMsg{});
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    TEST_EXPECT(ctx, context.post(message));
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));
    TEST_EXPECT(ctx, resources.worker_to_host_msgs.read(message));
    TEST_EXPECT(ctx, resources.host_to_worker_msgs.post(message));
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    TEST_EXPECT(ctx, context.read(message));
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));

    debug_system::CDebugServiceState debug;
    debug.install_shutdown_wake(owner.endpoint());
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    debug.request_shutdown(debug_system::EShutdownReason::fatal_incident);
    TEST_EXPECT(ctx, (owner.wait(immediate).status == EOwnerWaitStatus::activity));
}

struct SClock
{
    std::uint64_t now{ 0u };
    bool failed{ false };
    static WaitError read(void* const context, std::uint64_t& value) noexcept
    {
        SClock& self = *static_cast<SClock*>(context);
        value = self.now;
        return self.failed ? WaitError{ EWaitError::native_failure, 71u } : WaitError{};
    }
};

void cadence(TTestContext& ctx)
{
    SClock clock;
    host::CHostCadence schedule({ &clock, &SClock::read });
    std::uint64_t remaining = 0u;
    TEST_EXPECT(ctx, (schedule.remaining(remaining).code == EWaitError::none));
    TEST_EXPECT(ctx, (remaining == 8333334u));
    clock.now = 2000000u;
    TEST_EXPECT(ctx, (schedule.remaining(remaining).code == EWaitError::none));
    TEST_EXPECT(ctx, (remaining == 6333334u));
    clock.now = 8333334u;
    TEST_EXPECT(ctx, (schedule.remaining(remaining).code == EWaitError::none));
    TEST_EXPECT(ctx, (remaining == 8333333u));
    clock.now = 50000000u;
    TEST_EXPECT(ctx, (schedule.remaining(remaining).code == EWaitError::none));
    TEST_EXPECT(ctx, (remaining == 8333334u));
    clock.now = 1000000000u;
    TEST_EXPECT(ctx, (schedule.remaining(remaining).code == EWaitError::none));
    TEST_EXPECT(ctx, (remaining == 8333334u));
    --clock.now;
    TEST_EXPECT(ctx, (schedule.remaining(remaining).code == EWaitError::native_failure));
    clock.failed = true;
    TEST_EXPECT(ctx, (schedule.remaining(remaining).native_error == 71u));
    clock = { UINT64_MAX - 4u, false };
    host::CHostCadence overflow({ &clock, &SClock::read });
    TEST_EXPECT(ctx, (overflow.remaining(remaining).code == EWaitError::overflow));
}

#if MV_PLATFORM_WINDOWS
void native_queue(TTestContext& ctx)
{
    COwnerWait owner;
    TEST_EXPECT(ctx, (owner.initialise().code == EWaitError::none));
    MSG message{};
    constexpr UINT wake_message = WM_APP + 170u;
    (void)PeekMessageW(&message, nullptr, wake_message, wake_message, PM_NOREMOVE);
    TEST_EXPECT(ctx, PostThreadMessageW(GetCurrentThreadId(), wake_message, 0u, 0));
    TEST_EXPECT(ctx, PeekMessageW(&message, nullptr, wake_message, wake_message, PM_NOREMOVE));
    TEST_EXPECT(ctx, (owner.begin_cycle().code == EWaitError::none));
    TEST_EXPECT(ctx, (owner.wait(limit(ctx, 1u), false).status == EOwnerWaitStatus::timed_out));
    TEST_EXPECT(ctx, (owner.wait(limit(ctx, 1000u), true).status == EOwnerWaitStatus::native_activity));
    TEST_EXPECT(ctx, PeekMessageW(&message, nullptr, wake_message, wake_message, PM_REMOVE));
    TEST_EXPECT(ctx, (message.message == wake_message));
}
#endif
}

void run_host_wait_tests(TTestContext& ctx)
{
    publication_boundaries(ctx);
    delayed_signal(ctx);
    adapters(ctx);
    cadence(ctx);
#if MV_PLATFORM_WINDOWS
    native_queue(ctx);
#endif
}
