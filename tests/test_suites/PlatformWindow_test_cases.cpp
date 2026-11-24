
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    PlatformWindow_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    04 Nov 26
//
//  Tests window protocols and lifetimes through real Host routing with a simulated
//  native adapter, plus explicitly invoked Windows geometry and modal acceptance.

#include "framework/system/system_ids.hpp"
#include "tests/support/provider_endpoint_fixture.hpp"
#include "platform/runtime/platform_runtime.hpp"
#include "host/runtime/services/service_router.hpp"
#include "host/runtime/host_restrictions.hpp"
#include "host/runtime/host.hpp"
#include "framework/module/module_messages.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "containers/TInstance.hpp"
#include <cstring>
#include <cstdio>
#include "platform/platform_defines.hpp"
#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#include "platform/threading/thread_lifetime.hpp"
#endif

namespace platform::runtime
{
struct SPlatformTestAccess
{
    static bool seed_released_native_window(CPlatformRuntime& runtime) noexcept
    {
        runtime.m_window.token.value = 1u;
        const auto scope = runtime.enter_native(0u);
        if (!scope.value) return false;
        const auto status = runtime.m_adapter.create(runtime.m_window.native);
        runtime.leave_native(scope);
        return status == services::EServiceStatus::success;
    }
    static void native_first(CPlatformRuntime& runtime) noexcept { runtime.m_native_first = false; }
    static std::uint32_t pending_commands(const CPlatformRuntime& runtime) noexcept { return runtime.m_command_count; }
    static std::uint32_t uses(const CPlatformRuntime& runtime) noexcept { return runtime.m_use_count; }
    static void permit_test_cleanup_retry(CPlatformRuntime& runtime) noexcept { runtime.m_window.destruction_failed = false; }
    static void discharge_fake_external_resources(CPlatformRuntime& runtime) noexcept
    {
        //  Test teardown explicitly destroys its simulated external dependency.
        //  This is not the production execution-ended path.
        for (auto slot = runtime.m_uses.first_live(); slot >= 0; slot = runtime.m_uses.next_live(slot))
        {
            auto& use = *runtime.m_uses.get_object(slot);
            if (use.quiescence == services::EServiceQuiescence::execution_ended)
            {
                use.quiescence = services::EServiceQuiescence::relinquished;
            }
        }
    }
};
}

namespace
{

using tests::TTestContext;
using namespace services;
using namespace platform::windows;

struct SClock
{
    std::uint64_t time{ 100u };
    bool failed{ false }, realtime{ false };
    static platform::threading::WaitError read(void* const context, std::uint64_t& value) noexcept
    {
        auto& clock = *static_cast<SClock*>(context);
        if (clock.realtime)
        {
            return platform::threading::query_awake_nanoseconds(value);
        }
        value = clock.time;
        return clock.failed ? platform::threading::WaitError{ platform::threading::EWaitError::native_failure, 1u } : platform::threading::WaitError{};
    }
    platform::threading::SAwakeClock source() noexcept { return { this, &read }; }
};

struct SNative
{
    unsigned created{ 0u }, destroyed{ 0u }, callbacks{ 0u }, dispatches{ 0u };
    SClock* clock{ nullptr };
    std::uint64_t dispatch_cost{ 0u };
    bool fail_create{ false }, fail_destroy{ false }, pending_close{ false }, pending_events{ false };
    bool native_loss_requested{ false };
    static inline SNative* current{ nullptr };
    static EServiceStatus create(platform::native::SWindow& window) noexcept
    {
        ++current->created;
        window.handle = 71u;
        window.instance = 72u;
        window.native_events = true;
        window.runtime->enter_native_callback();
        ++current->callbacks;
        window.runtime->native_extent(1920u, 1080u, 144u, false);
        if (current->pending_close)
        {
            window.runtime->native_close();
        }
        window.runtime->leave_native_callback();
        return current->fail_create ? EServiceStatus::native_failure : EServiceStatus::success;
    }
    static bool dispatch(platform::native::SWindow& window, const std::uint64_t) noexcept
    {
        ++current->dispatches;
        window.work_pending = current->pending_events;
        if (current->clock)
        {
            current->clock->time += current->dispatch_cost;
        }
        return true;
    }
    static bool destroy(platform::native::SWindow& window) noexcept
    {
        if (current->fail_destroy) return false;
        ++current->destroyed;
        window.runtime->enter_native_callback();
        ++current->callbacks;
        window.runtime->leave_native_callback();
        window.handle = window.instance = 0u;
        return true;
    }
    static const platform::native::SWindowAdapter& adapter() noexcept
    {
        static const platform::native::SWindowAdapter value{ &create, &dispatch, &destroy };
        return value;
    }
};

struct SFixture
{
    TTestContext& tests;
    SClock clock;
    SNative native;
    host::CHostRestrictions restrictions{ clock.source() };
    tests::SCommonEndpointFixture client{ module_ids::executive, 101u }, other{ module_ids::rendering, 102u };
    tests::SCommonEndpointFixture provider{ module_ids::platform, 103u };
    host::CServiceRouter router;
    modules::SHostExecutionContext execution;
    tests::CProviderEndpointFixture exchanges;
    static modules::ESubmission MV_STD_ABI_CALL facade_post(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SFixture& self = *static_cast<SFixture*>(cookie);
        if (destination.kind != modules::SDestination::EKind::host)
        {
            return modules::ESubmission::invalid;
        }
        if (self.transport_busy) return modules::ESubmission::busy;
        return tests::submit_common_pod(self.provider, message);
    }
    static inline const modules::SCommunicationV1 facade_functions{ &facade_post };
    modules::CCommunication communication{ facade_functions, this };
    modules::SExecutionContext context;
    TInstance<platform::runtime::CPlatformRuntime> runtime;
    SServiceRoute route, other_route;
    std::uint64_t sequence{ 0u }, other_sequence{ 0u };
    bool completion_busy{ false };
    bool post_busy{ false };
    bool transport_busy{ false };
    threading::CErasedPodMsg posted[2];
    unsigned posted_count{ 0u };
    std::uint64_t binding_cost{ 0u };
    std::atomic<unsigned> native_modes{ 0u }, cancel_requested{ 0u }, maintenance_calls{ 0u };
    modules::SExecutionProgress progress;
    bool callback_failed{ false }, expect_failure{ false };
    void invoke(const modules::SInvocation& invocation, modules::SExecutionProgress& result) noexcept
    {
        const modules::EExecutionStatus status = runtime->handle(invocation, result);
        if (status != modules::EExecutionStatus::success)
        {
            callback_failed = true;
            TEST_EXPECT(tests, (expect_failure && (status == modules::EExecutionStatus::failed)));
        }
    }

    static EServiceStatus MV_STD_ABI_CALL post(void* const context, const threading::CErasedPodMsg* const message) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        if (self.post_busy || (self.posted_count == 2u))
        {
            return EServiceStatus::busy;
        }
        self.posted[self.posted_count++] = *message;
        return EServiceStatus::success;
    }

    static EServiceStatus MV_STD_ABI_CALL complete(void* const context, const ServiceReply* const reply) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        return self.completion_busy ? EServiceStatus::busy : self.router.complete(self.provider, *reply);
    }
    static EServiceStatus MV_STD_ABI_CALL commit(void* const context, const ServiceReply* const reply) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        return self.completion_busy ? EServiceStatus::busy : self.router.commit_resource(self.provider, *reply);
    }
    static EServiceStatus MV_STD_ABI_CALL prepare(void* const context, const SServiceBorrowOffer* const offer,
        SServiceBorrow* const borrow) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        return self.router.prepare_borrow(self.provider, *offer, *borrow);
    }
    static EServiceStatus MV_STD_ABI_CALL begin(void* const context, const SServiceBorrow borrow) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        return self.router.begin_borrow_retirement(self.provider, borrow);
    }
    static EServiceStatus MV_STD_ABI_CALL finish(void* const context, const SServiceBorrow borrow) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        return self.router.finish_borrow_retirement(self.provider, borrow);
    }
    static EServiceStatus MV_STD_ABI_CALL bind(void* const context, const SServiceBorrow borrow,
        const SNotificationRegistration registration, threading::SNotificationEndpoint* const endpoint) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        self.clock.time += self.binding_cost;
        return self.router.bind_borrow_notification(self.provider, borrow, registration, *endpoint);
    }
    static modules::SRestrictionEnterResult MV_STD_ABI_CALL enter(void* const context, const modules::SRestrictionReason reason) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        if ((reason.value == 3u) || (reason.value == 4u))
        {
            self.native_modes.fetch_add(1u);
        }
        return self.restrictions.enter(reason);
    }
    static modules::ERestrictionStatus MV_STD_ABI_CALL leave(void* const context, const modules::SRestrictionScope scope) noexcept
    { return static_cast<SFixture*>(context)->restrictions.leave(scope); }
    static modules::SHostMaintenanceResult MV_STD_ABI_CALL maintain(void* const context) noexcept
    {
        auto& self = *static_cast<SFixture*>(context);
        self.maintenance_calls.fetch_add(1u);
        modules::SHostMaintenanceResult result;
        result.status = modules::EMaintenanceStatus::success;
        result.cancel_native = result.shutdown_requested = self.cancel_requested.load();
        return result;
    }

    explicit SFixture(TTestContext& tests,
        const platform::native::SWindowAdapter& adapter = SNative::adapter()) noexcept : tests{ tests }
    {
        SNative::current = &native;
        native.clock = &clock;
        client.phase = other.phase = provider.phase = host::EClientPhase::ready;
        execution.restriction = restrictions.reader();
        context.instance = provider.instance;
        context.endpoint = provider.identity;
        context.communication = &communication;
        execution.native_context = this;
        execution.enter_restriction = &enter;
        execution.leave_restriction = &leave;
        execution.maintain_host = &maintain;
        context.execution = &execution;
        TEST_EXPECT(tests, (runtime.emplace(context, adapter, clock.source()) && runtime->initialise()));
        host::SServiceProvider advertisement{ &provider, &k_window_contract, 1u };
        for (auto* recipient : { &client, &other })
        {
            host::CRetainedReplies::CDispatch dispatch{ *recipient->pending_replies, 1u };
            TEST_EXPECT(tests, dispatch);
            TEST_EXPECT(tests, recipient->reserve_replies(1u));
            router.open({ module_ids::platform, 1u, 1u, { 1u } }, 1, *recipient, &advertisement);
            OpenServiceResult result;
            TEST_EXPECT(tests, (recipient->received(0).copy_payload_to(result) && (result.status == EServiceStatus::success)));
            ((recipient == &client) ? route : other_route) = result.route;
            recipient->discard(1u);
        }
    }

    std::uint64_t submit(const EWindowCommand opcode, const void* const payload = nullptr,
        const std::uint16_t size = 0u, const bool second = false) noexcept
    {
        auto& recipient = second ? other : client;
        ServiceRequest request;
        request.route = second ? other_route : route;
        request.request.value = ++(second ? other_sequence : sequence);
        request.opcode = static_cast<std::uint16_t>(opcode);
        request.payload_size = size;
        if (size)
        {
            std::memcpy(request.payload, payload, size);
        }
        TEST_EXPECT(tests, recipient.reserve_replies(2u));
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 2u };
        TEST_EXPECT(tests, dispatch);
        router.request(request, static_cast<std::int32_t>(request.request.value), recipient);
        return request.request.value;
    }
    template<typename T> std::uint64_t submit(const EWindowCommand opcode, const T& request, const bool second = false) noexcept
    { return submit(opcode, &request, sizeof(request), second); }

    void progress_routes() noexcept
    {
        router.progress();
        for (host::SClientRecord* const recipient : { &client, &other, &provider })
        {
            tests::progress_service_outputs(router, *recipient);
        }
        router.progress();
    }

    void step(const unsigned turns = 256u) noexcept
    {
        for (unsigned turn = 0u; turn < turns; ++turn)
        {
            progress_routes();
            (void)exchanges.progress(*this);
            TEST_EXPECT(tests, restrictions.begin_invocation(provider.instance, true));
            modules::SExecutionProgress result;
            modules::SInvocation invocation;
            invocation.time_budget_ns = 2000000u;
            threading::CErasedPodMsg incoming;
            for (std::uint32_t count = 0u; (count < 64u) && provider.channel->read_preamble_pod(incoming); ++count)
            {
                invocation.event = modules::EExecutionEvent::message;
                invocation.message = &incoming;
                invoke(invocation, result);
            }
            invocation.event = modules::EExecutionEvent::service_async;
            invocation.message = nullptr;
            invoke(invocation, result);
            progress = result;
            TEST_EXPECT(tests, restrictions.end_invocation());
            const bool work = exchanges.progress(*this);
            progress_routes();
            if (!work && !provider.channel->to_module.readable_count())
            {
                break;
            }
        }
    }

    SPreparedServiceBorrow prepared(const std::uint64_t request, const bool second = false) const noexcept
    {
        const auto& recipient = second ? other : client;
        for (std::uint32_t index = 0u; index < recipient.pending(); ++index)
        {
            ServiceReply reply;
            if (recipient.received(index).copy_payload_to(reply) && (reply.request.value == request) && (reply.notice == EServiceNotice::prepared))
            {
                SPreparedServiceBorrow result;
                std::memcpy(&result, reply.payload, sizeof(result));
                return result;
            }
        }
        return {};
    }
    EServiceStatus terminal(const std::uint64_t request, const bool second = false) const noexcept
    {
        const auto& recipient = second ? other : client;
        for (std::uint32_t index = 0u; index < recipient.pending(); ++index)
        {
            ServiceReply reply;
            if (recipient.received(index).copy_payload_to(reply) && (reply.request.value == request) && (reply.notice == EServiceNotice::completed))
            {
                return reply.status;
            }
        }
        return EServiceStatus::unavailable;
    }
    ServiceNotificationRegistered registration(const SPreparedServiceBorrow& borrow, const bool second = false) noexcept
    {
        auto& recipient = second ? other : client;
        TEST_EXPECT(tests, recipient.reserve_replies(1u));
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(tests, dispatch);
        router.register_notification(recipient, { (second ? other_route : route), borrow.borrow }, 400);
        ServiceNotificationRegistered result;
        TEST_EXPECT(tests, recipient.received(recipient.pending() - 1u).copy_payload_to(result));
        return result;
    }
    void ready(const std::uint64_t acquisition, const bool second = false) noexcept
    {
        const auto borrow = prepared(acquisition, second);
        TEST_EXPECT(tests, (borrow.view.descriptor != nullptr));
        if (!borrow.view.descriptor) return;
        const auto& view = *static_cast<const SWindowViewV1*>(borrow.view.descriptor);
        const auto registered = registration(borrow, second);
        TEST_EXPECT(tests, (registered.status == EServiceStatus::success));
        (void)submit(EWindowCommand::window_ready, SWindowReady{ view.window, borrow.borrow, registered.registration }, second);
    }
    void release(const SPreparedServiceBorrow& borrow, const bool second = false) noexcept
    {
        auto& recipient = second ? other : client;
        const auto source = second ? other_route : route;
        router.release_borrow(recipient, { source, borrow.borrow });
        router.quiesce_borrow(recipient, { source, borrow.borrow });
    }
    ~SFixture() noexcept
    {
        completion_busy = false;
        transport_busy = false;
        for (auto* recipient : { &client, &other })
        {
            for (std::uint32_t index = 0u; index < recipient->pending(); ++index)
            {
                ServiceReply reply;
                if (recipient->received(index).copy_payload_to(reply) && (reply.notice == EServiceNotice::prepared))
                {
                    SPreparedServiceBorrow borrow;
                    std::memcpy(&borrow, reply.payload, sizeof(borrow));
                    release(borrow, (recipient == &other));
                }
            }
        }
        platform::runtime::SPlatformTestAccess::discharge_fake_external_resources(*runtime);
        TEST_EXPECT(tests, restrictions.begin_invocation(provider.instance, true));
        router.close_provider_admission(provider);
        modules::SExecutionProgress stopped;
        modules::SInvocation stop;
        stop.event = modules::EExecutionEvent::stop;
        invoke(stop, stopped);
        TEST_EXPECT(tests, restrictions.end_invocation());
        for (unsigned iteration = 0u; (iteration < 40u) && !runtime->stopped(); ++iteration)
        {
            client.discard(client.pending());
            other.discard(other.pending());
            step();
        }
        TEST_EXPECT(tests, (runtime->stopped() && router.provider_borrows_retired(provider)));
        TEST_EXPECT(tests, (callback_failed == expect_failure));
    }
};

void coherent_state_and_close(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.native.pending_close = true;
    const auto created = f.submit(EWindowCommand::create_window);
    f.step();
    const auto first = f.prepared(created);
    TEST_EXPECT(ctx, (first.view.descriptor && (f.native.created == 1u)));
    if (!first.view.descriptor) return;
    const auto& view = *static_cast<const SWindowViewV1*>(first.view.descriptor);
    TEST_EXPECT(ctx, (view.is_valid() && view.controller));
    SWindowSnapshot snapshot;
    TEST_EXPECT(ctx, ((view.state->read(snapshot) == EWindowRead::success) && (snapshot.phase == EWindowPhase::close_pending)));
    TEST_EXPECT(ctx, ((snapshot.logical_width == 1280u) && (snapshot.logical_height == 720u) && (snapshot.pixel_width == 1920u) && (snapshot.dpi == 144u)));
    const auto initial = f.registration(first);
    TEST_EXPECT(ctx, !initial.reader.pending());
    const auto ready = f.submit(EWindowCommand::window_ready, SWindowReady{ view.window, first.borrow, initial.registration });
    f.step();
    TEST_EXPECT(ctx, (initial.reader.pending() && (f.terminal(created) == EServiceStatus::success) && (f.terminal(ready) == EServiceStatus::success)));
    const auto acquired = f.submit(EWindowCommand::acquire_window, SWindowRequest{ view.window }, true);
    f.step();
    const auto second = f.prepared(acquired, true);
    f.ready(acquired, true);
    f.step();
    const auto foreign = f.submit(EWindowCommand::answer_close, SAnswerClose{ view.window, snapshot.close_request, 1u, 0u }, true);
    const auto rejected = f.submit(EWindowCommand::answer_close, SAnswerClose{ view.window, snapshot.close_request, 0u, 0u });
    f.step();
    TEST_EXPECT(ctx, ((f.terminal(foreign, true) == EServiceStatus::invalid_request) && (f.terminal(rejected) == EServiceStatus::success)));
    initial.reader.clear_pending();
    f.runtime->native_extent(800u, 600u, 96u, false);
    f.runtime->native_close();
    f.runtime->native_close();
    TEST_EXPECT(ctx, (initial.reader.pending() && (view.state->read(snapshot) == EWindowRead::success) && (snapshot.close_request == 2u)));
    const auto stale = f.submit(EWindowCommand::answer_close, SAnswerClose{ view.window, 1u, 1u, 0u });
    const auto accepted = f.submit(EWindowCommand::answer_close, SAnswerClose{ view.window, 2u, 1u, 0u });
    f.step();
    TEST_EXPECT(ctx, ((f.terminal(stale) == EServiceStatus::stale) && (f.terminal(accepted) == EServiceStatus::success)));
    TEST_EXPECT(ctx, ((view.state->read(snapshot) == EWindowRead::success) && (snapshot.phase == EWindowPhase::closing)));
    f.release(first);
    f.step(); f.step();
    TEST_EXPECT(ctx, ((f.native.destroyed == 0u) && (platform::runtime::SPlatformTestAccess::uses(*f.runtime) == 1u)));
    f.release(second, true);
    f.step(); f.step();
    TEST_EXPECT(ctx, ((f.native.destroyed == 1u) && !f.runtime->stopped()));
}

void facade_output_pressure(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.transport_busy = true;
    const std::uint64_t created = f.submit(EWindowCommand::create_window);
    f.step(2u);
    TEST_EXPECT(ctx, (!f.prepared(created).view.descriptor && !f.runtime->failed()));
    TEST_EXPECT(ctx, (f.progress.work_pending == 0u));
    f.transport_busy = false;
    f.step();
    TEST_EXPECT(ctx, (f.prepared(created).view.descriptor && (f.native.created == 1u)));
    f.ready(created);
    f.step();
    TEST_EXPECT(ctx, (f.terminal(created) == EServiceStatus::success));
}

void fifo_capacity_and_late_ready(TTestContext& ctx)
{
    SFixture f{ ctx };
    const auto created = f.submit(EWindowCommand::create_window);
    f.step();
    const auto prepared = f.prepared(created);
    if (!prepared.view.descriptor)
    {
        TEST_EXPECT(ctx, false);
        return;
    }
    const auto window = static_cast<const SWindowViewV1*>(prepared.view.descriptor)->window;
    std::uint64_t acquisitions[32]{ created };
    for (unsigned index = 1u; index < 32u; ++index)
    {
        acquisitions[index] = f.submit(EWindowCommand::acquire_window, SWindowRequest{ window });
    }
    f.step();
    TEST_EXPECT(ctx, (platform::runtime::SPlatformTestAccess::uses(*f.runtime) == 32u));
    const auto excess = f.submit(EWindowCommand::acquire_window, SWindowRequest{ window });
    for (const auto acquisition : acquisitions)
    {
        f.ready(acquisition);
    }
    f.step();
    TEST_EXPECT(ctx, (f.terminal(excess) == EServiceStatus::busy));
    for (const auto acquisition : acquisitions)
    {
        TEST_EXPECT(ctx, (f.terminal(acquisition) == EServiceStatus::success));
    }
    TEST_EXPECT(ctx, ((f.clock.time == 100u) && (f.native.created == 1u) && !f.runtime->failed()));
}

void deadlines_and_partial_failure(TTestContext& ctx)
{
    {
        SFixture f{ ctx };
        f.native.fail_create = true;
        const auto failed = f.submit(EWindowCommand::create_window);
        f.step(); f.step();
        TEST_EXPECT(ctx, ((f.terminal(failed) == EServiceStatus::native_failure) && !f.prepared(failed).borrow.value));
        TEST_EXPECT(ctx, ((f.native.created == 1u) && (f.native.destroyed == 1u)));
    }
    {
        SFixture f{ ctx };
        const auto acquisition = f.submit(EWindowCommand::create_window);
        f.step();
        const auto prepared = f.prepared(acquisition);
        if (!prepared.view.descriptor)
        {
            TEST_EXPECT(ctx, false);
            return;
        }
        const auto window = static_cast<const SWindowViewV1*>(prepared.view.descriptor)->window;
        const auto registration = f.registration(prepared);
        f.clock.time += 5000000000u;
        const auto late = f.submit(EWindowCommand::window_ready, SWindowReady{ window, prepared.borrow, registration.registration });
        f.step();
        TEST_EXPECT(ctx, ((f.terminal(acquisition) == EServiceStatus::timed_out) && (f.terminal(late) == EServiceStatus::closing)));
        TEST_EXPECT(ctx, (!f.native.destroyed && (platform::runtime::SPlatformTestAccess::uses(*f.runtime) == 1u)));
    }
    {
        SFixture f{ ctx };
        const auto acquisition = f.submit(EWindowCommand::create_window);
        f.step();
        f.ready(acquisition);
        f.completion_busy = true;
        f.step();
        f.clock.time += 9000000000u;
        f.runtime->native_close();
        f.step();
        f.completion_busy = false;
        f.step();
        TEST_EXPECT(ctx, ((f.terminal(acquisition) == EServiceStatus::success) && (f.native.created == 1u)));
    }
}

void controller_departure_and_cancel(TTestContext& ctx)
{
    {
        SFixture f{ ctx };
        const auto acquisition = f.submit(EWindowCommand::create_window);
        f.step();
        f.ready(acquisition); f.step();
        f.client.phase = host::EClientPhase::stopped;
        f.client.incoming_drained = f.client.terminal_before_drain = true;
        f.router.retire_client(f.client);
        f.step(); f.step();
        TEST_EXPECT(ctx, (!f.native.destroyed && (platform::runtime::SPlatformTestAccess::uses(*f.runtime) == 1u)));
        const auto prepared = f.prepared(acquisition);
        SWindowSnapshot snapshot;
        TEST_EXPECT(ctx, (prepared.view.descriptor && (static_cast<const SWindowViewV1*>(prepared.view.descriptor)->state->read(snapshot) == EWindowRead::success)));
        TEST_EXPECT(ctx, (snapshot.phase == EWindowPhase::closing));
    }
    {
        SFixture f{ ctx };
        const auto acquisition = f.submit(EWindowCommand::create_window);
        f.step();
        const auto prepared = f.prepared(acquisition);
        f.router.cancel_request(f.client, { f.route, { acquisition } });
        f.step();
        TEST_EXPECT(ctx, ((f.terminal(acquisition) == EServiceStatus::cancelled) && !f.native.destroyed));
        f.release(prepared); f.step(); f.step();
        TEST_EXPECT(ctx, (f.native.destroyed == 1u));
    }
}

void shared_slice_and_loss(TTestContext& ctx)
{
    SFixture f{ ctx };
    const auto acquisition = f.submit(EWindowCommand::create_window);
    f.step();
    f.ready(acquisition); f.step();
    const auto prepared = f.prepared(acquisition);
    if (!prepared.view.descriptor)
    {
        TEST_EXPECT(ctx, false);
        return;
    }
    const auto window = static_cast<const SWindowViewV1*>(prepared.view.descriptor)->window;
    f.native.dispatch_cost = 2000000u;
    const auto close = f.submit(EWindowCommand::request_close, SWindowRequest{ window });
    platform::runtime::SPlatformTestAccess::native_first(*f.runtime);
    f.step(1u); // Native-first slice consumes the available time.
    TEST_EXPECT(ctx, ((platform::runtime::SPlatformTestAccess::pending_commands(*f.runtime) == 1u) &&
        (f.terminal(close) == EServiceStatus::unavailable)));
    f.step(1u); // Alternation lets the retained command run before the next native batch.
    TEST_EXPECT(ctx, ((platform::runtime::SPlatformTestAccess::pending_commands(*f.runtime) == 0u) &&
        (f.terminal(close) == EServiceStatus::success)));
    f.runtime->native_failure(1400u); // Unexpected established HWND loss.
    SWindowSnapshot snapshot;
    TEST_EXPECT(ctx, (static_cast<const SWindowViewV1*>(prepared.view.descriptor)->state->read(snapshot) == EWindowRead::failed));
    TEST_EXPECT(ctx, ((snapshot.phase == EWindowPhase::closing) && (snapshot.failure == 1400u)));
    f.step();
    TEST_EXPECT(ctx, (!f.native.destroyed && (platform::runtime::SPlatformTestAccess::uses(*f.runtime) == 1u)));
}

void command_slice_and_failure_retention(TTestContext& ctx)
{
    {
        SFixture f{ ctx };
        const auto first = f.submit(EWindowCommand::create_window);
        f.step();
        const auto prepared = f.prepared(first);
        if (!prepared.view.descriptor)
        {
            TEST_EXPECT(ctx, false);
            return;
        }
        const auto window = static_cast<const SWindowViewV1*>(prepared.view.descriptor)->window;
        const auto second = f.submit(EWindowCommand::acquire_window, SWindowRequest{ window });
        f.step();
        f.ready(first); f.ready(second);
        f.binding_cost = 2000000u;
        f.step(1u); // Binding is submitted asynchronously; the next invocation receives its outcome.
        TEST_EXPECT(ctx, ((f.terminal(first) == EServiceStatus::unavailable) &&
            (f.terminal(second) == EServiceStatus::unavailable)));
        f.step();
        TEST_EXPECT(ctx, ((f.terminal(first) == EServiceStatus::success) &&
            (f.terminal(second) == EServiceStatus::success)));
    }
    {
        SFixture f{ ctx };
        const auto acquisition = f.submit(EWindowCommand::create_window);
        f.step(); f.ready(acquisition); f.step();
        const auto prepared = f.prepared(acquisition);
        f.expect_failure = true;
        f.clock.failed = true;
        f.step();
        TEST_EXPECT(ctx, (f.callback_failed && f.runtime->failed() && !f.runtime->stopped() && !f.native.destroyed));
        TEST_EXPECT(ctx, (platform::runtime::SPlatformTestAccess::uses(*f.runtime) == 1u));
        f.clock.failed = false; // Restore the deterministic clock for explicit test cleanup.
        f.release(prepared);
    }
    {
        SFixture f{ ctx };
        const auto acquisition = f.submit(EWindowCommand::create_window);
        f.step(); f.ready(acquisition); f.step();
        const auto prepared = f.prepared(acquisition);
        f.expect_failure = true;
        f.native.fail_destroy = true;
        f.native.pending_events = true;
        f.release(prepared); f.step(); f.step();
        TEST_EXPECT(ctx, (f.callback_failed && f.runtime->failed() && !f.runtime->stopped() && !f.native.destroyed &&
            (platform::runtime::SPlatformTestAccess::uses(*f.runtime) == 0u)));
        TEST_EXPECT(ctx, (!f.progress.work_pending && !f.progress.native_events_active));
        f.native.fail_destroy = false;
        platform::runtime::SPlatformTestAccess::permit_test_cleanup_retry(*f.runtime);
    }
}

void native_progress(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.step();
    TEST_EXPECT(ctx, (!f.progress.work_pending && !f.progress.native_events_active));
    const std::uint64_t acquisition = f.submit(EWindowCommand::create_window);
    f.step();
    f.ready(acquisition);
    f.step();
    TEST_EXPECT(ctx, (!f.progress.work_pending && f.progress.native_events_active));
    f.native.pending_events = true;
    f.step();
    TEST_EXPECT(ctx, (f.progress.work_pending && f.progress.native_events_active));
    f.native.pending_events = false;
    f.step();
    TEST_EXPECT(ctx, (!f.progress.work_pending && f.progress.native_events_active));
    modules::SExecutionProgress stopping;
    modules::SInvocation stop;
    stop.event = modules::EExecutionEvent::stop;
    TEST_EXPECT(ctx, f.restrictions.begin_invocation(f.provider.instance, true));
    f.invoke(stop, stopping);
    TEST_EXPECT(ctx, f.restrictions.end_invocation());
    TEST_EXPECT(ctx, (stopping.native_events_active && stopping.work_pending));
    f.release(f.prepared(acquisition));
    f.step();
    f.step();
    TEST_EXPECT(ctx, (!f.progress.work_pending && !f.progress.native_events_active));
}

void notification_pressure(TTestContext& ctx)
{
    SFixture f{ ctx };
    const auto acquisition = f.submit(EWindowCommand::create_window);
    f.step(); f.ready(acquisition); f.step();
    const auto prepared = f.prepared(acquisition);
    if (!prepared.view.descriptor)
    {
        TEST_EXPECT(ctx, false);
        return;
    }
    const auto& view = *static_cast<const SWindowViewV1*>(prepared.view.descriptor);
    const auto registration = f.registration(prepared);
    registration.reader.clear_pending();
    // The fixed ordinary ring is full while the separate notification remains live.
    f.client.discard(f.client.pending());
    constexpr std::uint32_t k_pressure_capacity = host::k_module_pod_capacity;
    const threading::CErasedPodMsg filler = threading::make_pod_message(-1, ServiceReply{});
    for (std::uint32_t count = 0u; count < k_pressure_capacity; ++count)
    {
        TEST_EXPECT(ctx, f.client.post(filler));
    }
    TEST_EXPECT(ctx, !f.client.post(filler));
    f.runtime->native_close();
    SWindowSnapshot snapshot;
    TEST_EXPECT(ctx, (registration.reader.pending() && (view.state->read(snapshot) == EWindowRead::success) &&
        (snapshot.phase == EWindowPhase::close_pending)));
    registration.reader.clear_pending();
    TEST_EXPECT(ctx, ((view.state->read(snapshot) == EWindowRead::success) && (snapshot.close_request == 1u)));
    f.router.release_borrow(f.client, { f.route, prepared.borrow });
    f.step();
    TEST_EXPECT(ctx, (!f.native.destroyed && !f.router.provider_borrows_retired(f.provider)));
    f.client.discard(f.client.pending());
    f.router.quiesce_borrow(f.client, { f.route, prepared.borrow });
    f.step(); f.step();
    TEST_EXPECT(ctx, ((f.native.destroyed == 1u) && f.router.provider_borrows_retired(f.provider)));
}

void unsupported_completions_under_pressure(TTestContext& ctx)
{
    SFixture f{ ctx };
    f.completion_busy = true;
    const auto first = f.submit(static_cast<EWindowCommand>(65535u));
    const auto second = f.submit(static_cast<EWindowCommand>(65535u));
    f.step();
    TEST_EXPECT(ctx, ((f.client.pending() == 2u) && (f.client.reserved_replies == 2u)));
    modules::SExecutionProgress progress;
    modules::SInvocation stop;
    stop.event = modules::EExecutionEvent::stop;
    f.invoke(stop, progress);
    TEST_EXPECT(ctx, (!f.runtime->stopped() && !progress.work_complete));
    f.step();
    TEST_EXPECT(ctx, ((f.client.pending() == 2u) && (f.client.reserved_replies == 2u) && !f.runtime->stopped()));
    f.completion_busy = false;
    f.step();
    TEST_EXPECT(ctx, ((f.client.pending() == 4u) && !f.client.reserved_replies && f.runtime->stopped()));
    TEST_EXPECT(ctx, ((f.terminal(first) == EServiceStatus::unsupported) && (f.terminal(second) == EServiceStatus::unsupported)));
    TEST_EXPECT(ctx, !f.native.created);
}

#if MV_PLATFORM_WINDOWS
struct SNativeDriver
{
    SFixture* fixture{ nullptr };
    HWND window{ nullptr };
    bool continuous{ false };
    std::atomic<unsigned> finished{ 0u }, fallback{ 0u }, moved{ 0u };
    static std::uint32_t MV_STD_ABI_CALL run(void* const context) noexcept
    {
        auto& driver = *static_cast<SNativeDriver*>(context);
        const auto start = GetTickCount64();
        while (!driver.fixture->native_modes.load() && ((GetTickCount64() - start) < 3000u))
        {
            Sleep(1u);
        }
        if (!driver.fixture->native_modes.load())
        {
            driver.fallback.store(1u);
            (void)PostMessageW(driver.window, WM_CANCELMODE, 0u, 0);
            driver.finished.store(1u);
            return 1u;
        }
        //  Keep a real native modal frame stationary long enough for timer-only
        //  opportunities, then optionally feed continuous pointer movement.
        Sleep(150u);
        if (driver.continuous)
        {
            for (unsigned index = 0u; index < 20u; ++index)
            {
                INPUT input{};
                input.type = INPUT_MOUSE;
                input.mi.dx = (index < 10u) ? 2 : -2;
                input.mi.dy = (index < 10u) ? 1 : -1;
                input.mi.dwFlags = MOUSEEVENTF_MOVE;
                if (SendInput(1u, &input, sizeof(input)) == 1u)
                {
                    driver.moved.fetch_add(1u);
                }
                Sleep(10u);
            }
        }
        driver.fixture->cancel_requested.store(1u);
        const auto stopping = GetTickCount64();
        while (!driver.finished.load() && ((GetTickCount64() - stopping) < 2000u))
        {
            Sleep(1u);
        }
        if (!driver.finished.load())
        {
            driver.fallback.store(1u);
            (void)PostMessageW(driver.window, WM_KEYDOWN, VK_ESCAPE, 0);
            (void)PostMessageW(driver.window, WM_LBUTTONUP, 0u, 0);
        }
        return 0u;
    }
};

static bool native_loss_dispatch(platform::native::SWindow& window, const std::uint64_t budget) noexcept
{
    if (!SNative::current->native_loss_requested)
    {
        return platform::native::window_adapter().dispatch(window, budget);
    }
    return DestroyWindow(reinterpret_cast<HWND>(window.handle)) != FALSE;
}

void actual_native_window(TTestContext& ctx, const unsigned scenario)
{
    auto adapter = platform::native::window_adapter();
    if (scenario == 4u)
    {
        adapter.dispatch = &native_loss_dispatch;
    }
    SFixture f{ ctx, adapter };
    f.clock.realtime = true;
    const auto acquisition = f.submit(EWindowCommand::create_window);
    f.step();
    const auto prepared = f.prepared(acquisition);
    TEST_EXPECT(ctx, (prepared.view.descriptor != nullptr));
    if (!prepared.view.descriptor) return;
    const auto& view = *static_cast<const SWindowViewV1*>(prepared.view.descriptor);
    const auto handle = reinterpret_cast<HWND>(view.native_window);
    TEST_EXPECT(ctx, (view.is_valid() && IsWindow(handle) &&
        AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(handle), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)));
    RECT client{}, outer{};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    TEST_EXPECT(ctx, (GetClientRect(handle, &client) && GetWindowRect(handle, &outer) &&
        GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor)));
    SWindowSnapshot snapshot;
    TEST_EXPECT(ctx, (view.state->read(snapshot) == EWindowRead::success));
    TEST_EXPECT(ctx, ((snapshot.pixel_width == static_cast<unsigned>(client.right)) && (snapshot.pixel_height == static_cast<unsigned>(client.bottom))));
    TEST_EXPECT(ctx, ((snapshot.logical_width <= 1280u) && (snapshot.logical_height <= 720u) &&
        (outer.left >= monitor.rcWork.left) && (outer.top >= monitor.rcWork.top) && (outer.right <= monitor.rcWork.right) && (outer.bottom <= monitor.rcWork.bottom)));
    f.ready(acquisition);
    f.step();
    TEST_EXPECT(ctx, (f.terminal(acquisition) == EServiceStatus::success));
    if (scenario == 4u)
    {
        f.native.native_loss_requested = true;
        f.step();
        TEST_EXPECT(ctx, (!IsWindow(handle) && (view.state->read(snapshot) == EWindowRead::failed) && (snapshot.phase == EWindowPhase::closing)));
        TEST_EXPECT(ctx, (platform::runtime::SPlatformTestAccess::uses(*f.runtime) == 1u));
        f.release(prepared);
        return;
    }
    if (scenario == 5u)
    {
        //  Windowless records share the subscribed queue and must be consumed.
        constexpr UINT thread_message = WM_APP + 171u;
        for (unsigned index = 0u; index < 129u; ++index)
        {
            TEST_EXPECT(ctx, PostThreadMessageW(GetCurrentThreadId(), thread_message, 0u, 0));
        }
        PostQuitMessage(0);
        f.step();
        TEST_EXPECT(ctx, (f.progress.work_pending && f.progress.native_events_active));
        for (unsigned index = 0u; (index < 16u) && !f.progress.stop_requested; ++index)
        {
            f.step();
        }
        TEST_EXPECT(ctx, f.progress.stop_requested);
        f.step();
        TEST_EXPECT(ctx, f.progress.stop_requested); //  Stop survives consumption.
        MSG pending{};
        TEST_EXPECT(ctx, !PeekMessageW(&pending, nullptr, thread_message, thread_message, PM_NOREMOVE));
        TEST_EXPECT(ctx, !PeekMessageW(&pending, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE));
    }
    else if (scenario == 0u)
    {
        (void)PostMessageW(handle, WM_CLOSE, 0u, 0);
        f.step();
        TEST_EXPECT(ctx, ((view.state->read(snapshot) == EWindowRead::success) && (snapshot.phase == EWindowPhase::close_pending)));
        (void)f.submit(EWindowCommand::answer_close, SAnswerClose{ view.window, snapshot.close_request, 0u, 0u });
        f.step();
        TEST_EXPECT(ctx, (IsWindow(handle) && (view.state->read(snapshot) == EWindowRead::success) && (snapshot.phase == EWindowPhase::open)));
        (void)PostMessageW(handle, WM_CLOSE, 0u, 0);
        f.step();
        TEST_EXPECT(ctx, ((view.state->read(snapshot) == EWindowRead::success) && (snapshot.close_request == 2u)));
        (void)f.submit(EWindowCommand::answer_close, SAnswerClose{ view.window, snapshot.close_request, 1u, 0u });
        f.step();
    }
    else
    {
        SNativeDriver driver;
        driver.fixture = &f;
        driver.window = handle;
        driver.continuous = scenario == 2u;
        platform::threading::CThread thread;
        TEST_EXPECT(ctx, thread.create(&SNativeDriver::run, &driver));
        //  Use the system-menu Move command. The HTCAPTION variant assumes a
        //  physically held left button and can return without entering a loop.
        const WPARAM command = (scenario == 3u) ? SC_KEYMENU : ((scenario == 2u) ? (SC_SIZE + WMSZ_BOTTOMRIGHT) : SC_MOVE);
        TEST_EXPECT(ctx, PostMessageW(handle, WM_SYSCOMMAND, command, ((scenario == 3u) ? VK_SPACE : 0)));
        const auto start = GetTickCount64();
        while (!f.cancel_requested.load() && ((GetTickCount64() - start) < 4000u))
        {
            f.step();
            if (!f.cancel_requested.load()) Sleep(1u);
        }
        driver.finished.store(1u);
        TEST_EXPECT(ctx, thread.join_and_close());
        std::printf("Platform native scenario %u: modes=%u fallback=%u maintenance=%u moved=%u\n", scenario,
            f.native_modes.load(), driver.fallback.load(), f.maintenance_calls.load(), driver.moved.load());
        TEST_EXPECT(ctx, (f.native_modes.load() && !driver.fallback.load() && (f.maintenance_calls.load() >= 5u)));
        if (driver.continuous)
        {
            TEST_EXPECT(ctx, (driver.moved.load() == 20u));
        }
        TEST_EXPECT(ctx, !f.restrictions.active());
    }
    TEST_EXPECT(ctx, IsWindow(handle)); // Live use still owns native dependencies.
    f.release(prepared);
    f.step(); f.step();
    TEST_EXPECT(ctx, !IsWindow(handle));
}
#endif

}   //  namespace

void prepare_platform_test_binding(modules::CBoundModule& binding) noexcept;

namespace host
{
struct SPlatformHostTestAccess
{
    struct SFrame
    {
        TInstance<platform::runtime::CPlatformRuntime> value;
        bool deactivated{ false };
    };
    static inline SFrame* current{ nullptr };
    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const value) noexcept
    {
        if (!current->value.emplace(*context, SNative::adapter()) || !current->value->initialise() ||
            !platform::runtime::SPlatformTestAccess::seed_released_native_window(*current->value))
        {
            return modules::EExecutionStatus::failed;
        }
        *value = current;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const value, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        return static_cast<SFrame*>(value)->value->handle(*invocation, *progress);
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const value) noexcept
    {
        auto& frame = *static_cast<SFrame*>(value);
        if (!frame.value->stopped())
        {
            return modules::EExecutionStatus::failed;
        }
        frame.value.reset();
        frame.deactivated = true;
        return modules::EExecutionStatus::success;
    }
    static void shutdown_scope(tests::TTestContext& ctx)
    {
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        modules::CBoundModule binding;
        prepare_platform_test_binding(binding);
        CHost host{ allocator };
        host.m_phase = CHost::EPhase::complete;
        SFrame frame;
        SNative native;
        SNative::current = &native;
        current = &frame;
        const auto index = module_ids::platform_index.raw_value();
        TEST_EXPECT(ctx, host.m_modules[index].emplace());
        auto& owned = *host.m_modules[index];
        TEST_EXPECT(ctx, owned.coordinator.emplace(module_ids::platform, 901u, nullptr, modules::SModuleInstance{ 900u }));
        auto& client = *owned.coordinator;
        owned.state.instance = client.instance;
        owned.binding = &binding;
        client.endpoint->module_state = &owned.state;
        client.owner_wake = host.m_owner_wait.endpoint();
        const modules::SExecutionV1 capability{ sizeof(modules::SExecutionV1), 1u, modules::k_execution_native_events,
            &create, &handle, &destroy };
        TEST_EXPECT(ctx, owned.pump.emplace(client, binding, capability, host.m_maintenance));
        auto& adapter = *owned.pump;
        TEST_EXPECT(ctx, adapter.initialise());
        adapter.activate();
        adapter.pump();
        host.drain_module_inputs();
        TEST_EXPECT(ctx, (adapter.ready() && (native.created == 1u) && !native.destroyed));
        host.m_emergency_shutdown = true;
        host.m_maintenance.poll_stop();
        TEST_EXPECT(ctx, host.m_maintenance.stop_requested());
        adapter.request_stop(modules::EExecutionStop::shutdown);
        for (unsigned turn = 0u; (turn < 16u) && !adapter.stopped(); ++turn)
        {
            adapter.pump();
            host.drain_module_inputs();
            host.drain_client(client);
        }
        TEST_EXPECT(ctx, (adapter.stopped() && !adapter.failed() && (native.destroyed == 1u) && (native.callbacks == 2u)));
        TEST_EXPECT(ctx, !host.m_maintenance.restrictions().active());
        TEST_EXPECT(ctx, (adapter.deactivated() && frame.deactivated));
        client.terminal_before_drain = client.incoming_drained = true;
        TEST_EXPECT(ctx, client.retire_host_execution(true));
        owned.pump.reset();
        owned.coordinator.reset();
        host.m_phase = CHost::EPhase::complete;
        TEST_EXPECT(ctx, host.shutdown());
    }
};
}   //  namespace host

void run_platform_window_tests(tests::TTestContext& ctx)
{
    coherent_state_and_close(ctx);
    facade_output_pressure(ctx);
    fifo_capacity_and_late_ready(ctx);
    deadlines_and_partial_failure(ctx);
    controller_departure_and_cancel(ctx);
    shared_slice_and_loss(ctx);
    command_slice_and_failure_retention(ctx);
    native_progress(ctx);
    notification_pressure(ctx);
    unsupported_completions_under_pressure(ctx);
    host::SPlatformHostTestAccess::shutdown_scope(ctx);
}

int run_platform_window_focused()
{
    tests::TTestContext ctx;
    run_platform_window_tests(ctx);
    return ctx.report("PlatformWindow");
}

int run_platform_native_tests()
{
#if MV_PLATFORM_WINDOWS
    tests::TTestContext ctx;
    for (unsigned scenario = 0u; scenario < 6u; ++scenario)
    {
        actual_native_window(ctx, scenario);
    }
    return ctx.report("PlatformNative");
#else
    return 2;
#endif
}
