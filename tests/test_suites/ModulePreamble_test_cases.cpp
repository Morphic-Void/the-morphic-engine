
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ModulePreamble_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    22 Nov 26
//
//  Execution boundary, bounded pipe storage and persistent input fairness.

#include "host/runtime/execution/module_lifecycle.hpp"
#include "host/runtime/execution/module_preamble.hpp"
#include "host/runtime/execution/pressure_selection.hpp"
#include "host/runtime/execution/retained_replies.hpp"
#include "host/runtime/execution/output_retry_turn.hpp"
#include "host/threading/CThreadPackage.hpp"
#include "host/runtime/host.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/support/endpoint_fixture.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "framework/services/service_protocol.hpp"
#include "framework/services/provider_client.hpp"
#include "framework/communications/module_tests.hpp"
#include "foundation/platform/threading/thread_lifetime.hpp"
#include "foundation/debug/service.hpp"
#include "executive/runtime/execution_timing.hpp"
#include "tests/support/module_preamble_fixture.hpp"
#include "tests/test_suites/ExecutionFixture_test_cases.hpp"

using tests::SDllAllocation;

namespace preamble_test_types
{
struct SLocalPod { std::uint32_t value; };
}

MV_REGISTER_LOCAL_TYPE(preamble_test_types::SLocalPod,
    local_type_ids::ops::encode_id(local_type_ids::ops::encode_index(1235u)));

namespace
{
struct SProviderFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    threading::CErasedPodMsg published;
    unsigned submissions{ 0u }, publications{ 0u }, failures{ 0u };
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SProviderFacade& self = *static_cast<SProviderFacade*>(cookie);
        if (destination.kind != modules::SDestination::EKind::host)
        {
            return modules::ESubmission::invalid;
        }
        ++self.submissions;
        if (modules::accepted(self.result))
        {
            self.published = message;
            ++self.publications;
        }
        return self.result;
    }
    static modules::ESubmission MV_STD_ABI_CALL intent(void* const cookie, const modules::SExecutionIntent& request) noexcept
    {
        SProviderFacade& self = *static_cast<SProviderFacade*>(cookie);
        if (request.kind == modules::SExecutionIntent::EKind::fail)
        {
            ++self.failures;
            return modules::ESubmission::accepted;
        }
        return modules::ESubmission::invalid;
    }
};

void provider_facade_requests(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SProviderFacade::pod, nullptr, &SProviderFacade::intent };
    for (const modules::ESubmission result : { modules::ESubmission::accepted, modules::ESubmission::published_wake_failed })
    {
        SProviderFacade state;
        modules::CCommunication communication{ functions, &state };
        services::CProviderClient provider{ communication };
        services::SServiceBorrowOffer offer;
        offer.route = { 1u };
        offer.request = { 2u };
        services::SServiceBorrow borrow;
        for (unsigned attempt = 0u; attempt < 3u; ++attempt)
        {
            TEST_EXPECT(ctx, (provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::busy));
            TEST_EXPECT(ctx, (provider.idle() && !provider.preparing(offer) && (state.publications == 0u)));
        }
        state.result = result;
        TEST_EXPECT(ctx, (provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::busy));
        TEST_EXPECT(ctx, (!provider.idle() && provider.preparing(offer) && (state.publications == 1u)));
        const unsigned submissions = state.submissions;
        TEST_EXPECT(ctx, (provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::busy));
        TEST_EXPECT(ctx, (state.submissions == submissions));
        services::ProviderOperationResult completed;
        completed.status = services::EServiceStatus::success;
        completed.borrow = { 3u };
        TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(state.published.query_async_slot(), completed)));
        TEST_EXPECT(ctx, ((provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::success) &&
            (borrow.value == 3u) && provider.idle() && (state.publications == 1u)));
        state.result = modules::ESubmission::closed;
        TEST_EXPECT(ctx, ((provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::closing) && provider.idle()));
        state.result = modules::ESubmission::invalid;
        TEST_EXPECT(ctx, ((provider.prepare_borrow(&offer, &borrow) == services::EServiceStatus::delivery_failed) && provider.idle()));
        TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(-1, completed)));
        TEST_EXPECT(ctx, (state.failures == 1u));
    }
}

void provider_pending_lifetimes(tests::TTestContext& ctx)
{
    static const modules::SCommunicationV1 functions{ &SProviderFacade::pod, nullptr, &SProviderFacade::intent };
    SProviderFacade state;
    state.result = modules::ESubmission::accepted;
    const modules::CCommunication communication{ functions, &state };
    services::CProviderClient provider{ communication };
    threading::CErasedPodMsg published[256];
    services::ServiceReply completion;
    completion.route = { 1u };
    completion.status = services::EServiceStatus::success;
    for (std::uint64_t request = 1u; request <= 257u; ++request)
    {
        completion.request = { request };
        TEST_EXPECT(ctx, (provider.complete(&completion) == services::EServiceStatus::busy));
        if (request <= 256u) published[request - 1u] = state.published;
    }
    TEST_EXPECT(ctx, ((state.submissions == 256u) && (state.publications == 256u)));
    services::ProviderOperationResult result;
    result.status = services::EServiceStatus::success;
    for (std::uint64_t request = 1u; request <= 256u; ++request)
    {
        TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(published[request - 1u].query_async_slot(), result)));
        TEST_EXPECT(ctx, !provider.idle());
        completion.request = { request };
        TEST_EXPECT(ctx, (provider.complete(&completion) == services::EServiceStatus::success));
        TEST_EXPECT(ctx, (provider.idle() == (request == 256u)));
    }
    TEST_EXPECT(ctx, (state.submissions == 256u));
    threading::SNotificationEndpoint notification;
    TEST_EXPECT(ctx, (provider.bind_borrow_notification({ 3u }, { 4u }, &notification) == services::EServiceStatus::busy));
    TEST_EXPECT(ctx, (!provider.retire_bindings({ 3u }) && !provider.idle()));
    TEST_EXPECT(ctx, provider.receive(threading::make_pod_message(state.published.query_async_slot(), result)));
    TEST_EXPECT(ctx, !provider.idle());
    TEST_EXPECT(ctx, (provider.retire_bindings({ 3u }) && provider.idle() && (state.submissions == 257u)));
}

void pressure_diagnostics(tests::TTestContext& ctx)
{
    using Diagnostic = host::CPressureDiagnostic;
    using Report = Diagnostic::EReport;
    Diagnostic pressure;
    constexpr std::uint64_t second = 1000000000u;
    TEST_EXPECT(ctx, (pressure.observe(0u, true, 0u).kind == Report::none));
    TEST_EXPECT(ctx, (pressure.observe((second - 1u), false, 1u).kind == Report::none));
    TEST_EXPECT(ctx, (pressure.observe(second, true, 1u).kind == Report::none));
    std::uint64_t wait = UINT64_MAX;
    pressure.limit_wait(second, wait);
    TEST_EXPECT(ctx, (wait == second));
    const Diagnostic::SReport first = pressure.observe((second + second), true, 2u);
    TEST_EXPECT(ctx, ((first.kind == Report::warning) && (first.duration_ns == second) && (first.progress == 1u)));
    for (std::uint64_t tick = 1u; tick < 5000u; ++tick)
    {
        TEST_EXPECT(ctx, (pressure.observe(((second + second) + (tick * 1000000u)), true, 2u).kind == Report::none));
    }
    TEST_EXPECT(ctx, (pressure.observe((7u * second), true, 3u).kind == Report::warning));
    const Diagnostic::SReport recovery = pressure.observe((8u * second), false, 4u);
    TEST_EXPECT(ctx, ((recovery.kind == Report::recovery) && (recovery.progress == 3u)));
    TEST_EXPECT(ctx, (pressure.observe((8u * second), true, 4u).kind == Report::none));
    TEST_EXPECT(ctx, (pressure.observe((10u * second), true, 4u).kind == Report::none));
    TEST_EXPECT(ctx, (pressure.observe((12u * second), true, 4u).kind == Report::warning));
    TEST_EXPECT(ctx, (pressure.observe((12u * second), false, 4u).kind == Report::recovery));
    wait = UINT64_MAX;
    pressure.limit_wait((12u * second), wait);
    TEST_EXPECT(ctx, (wait == UINT64_MAX));

    Diagnostic starvation;
    TEST_EXPECT(ctx, (starvation.observe(0u, true, 0u, true).kind == Report::none));
    TEST_EXPECT(ctx, (starvation.observe(second, true, 1u, true).kind == Report::none));
    TEST_EXPECT(ctx, (starvation.observe((second + second), true, 1u, true).kind == Report::warning));
    TEST_EXPECT(ctx, (starvation.observe((3u * second), true, 2u, true).kind == Report::recovery));
    TEST_EXPECT(ctx, (starvation.observe((4u * second), false, 2u, true).kind == Report::none));
    //  Blocked/paused sources are not eligible-source starvation, and a retired
    //  identity does not transfer its wait age into the replacement's record.
    starvation = {};
    TEST_EXPECT(ctx, (starvation.observe((20u * second), false, 0u, true).kind == Report::none));
    TEST_EXPECT(ctx, (starvation.observe((21u * second), true, 0u, true).kind == Report::none));
}

void executive_timing_policy(tests::TTestContext& ctx)
{
    struct SClock
    {
        std::uint64_t now{ 100u };
        bool failed{ false };
        static platform::threading::WaitError read(void* const cookie, std::uint64_t& time) noexcept
        {
            SClock& clock = *static_cast<SClock*>(cookie);
            time = clock.now;
            return clock.failed ? platform::threading::WaitError{ platform::threading::EWaitError::invalid_argument, 0u } :
                platform::threading::WaitError{};
        }
    } clock;
    using Status = modules::EBudgetStatus;
    using Event = executive::CTimingAvailability::EEvent;
    const platform::threading::SAwakeClock awake{ &clock, &SClock::read };
    modules::CHostRestrictionPublication publication;
    modules::SHostRestrictionSnapshot snapshot;
    snapshot.generation = 1u;
    TEST_EXPECT(ctx, publication.publish(snapshot));
    executive::TExecutionBudget<> budget{ awake };
    executive::CTimingAvailability availability{ awake };
    TEST_EXPECT(ctx, (budget.start(publication.reader()) == Status::running));
    const modules::SEligibleBudgetAnchor original = budget.anchor();
    TEST_EXPECT(ctx, (budget.publishable() && (original.awake_start_ns == clock.now)));
    snapshot.sampling = 1u;
    TEST_EXPECT(ctx, publication.publish(snapshot));
    TEST_EXPECT(ctx, (budget.observe().status == Status::retry));
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::none));
    clock.now += UINT64_C(1000000000);
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::warning));
    //  A domain completion and next-phase start are allowed during retry. The
    //  new publication is held, and neither transition erases the old anchor.
    TEST_EXPECT(ctx, (budget.start(publication.reader()) == Status::retry));
    TEST_EXPECT(ctx, (!budget.publishable() && budget.starting() && (budget.anchor().awake_start_ns == original.awake_start_ns)));
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::none));
    for (unsigned phase = 0u; phase < 29u; ++phase)
    {
        clock.now += UINT64_C(1000000000);
        TEST_EXPECT(ctx, (budget.start(publication.reader()) == Status::retry));
        TEST_EXPECT(ctx, (budget.anchor().awake_start_ns == original.awake_start_ns));
        const executive::CTimingAvailability::SObservation observed = availability.observe(Status::retry);
        TEST_EXPECT(ctx, (observed.event == ((phase == 28u) ? Event::failure :
            ((((phase + 2u) % 5u) == 1u) ? Event::warning : Event::none))));
    }
    snapshot.sampling = 0u;
    TEST_EXPECT(ctx, publication.publish(snapshot));
    TEST_EXPECT(ctx, (budget.observe().status == Status::running));
    TEST_EXPECT(ctx, (budget.publishable() && !budget.starting() && (budget.anchor().awake_start_ns == clock.now)));
    const executive::CTimingAvailability::SObservation recovery = availability.observe(Status::running);
    TEST_EXPECT(ctx, ((recovery.event == Event::recovery) && (recovery.duration_ns == executive::k_operation_stall_ns) &&
        !availability.retrying()));
    clock.now += 1u;
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::none));
    clock.now += 1u;
    TEST_EXPECT(ctx, ((availability.observe(Status::restricted).event == Event::none) && !availability.retrying()));
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::none));
    clock.failed = true;
    TEST_EXPECT(ctx, (availability.observe(Status::retry).event == Event::failure));
    TEST_EXPECT(ctx, (availability.observe(Status::failed).event == Event::failure));
}

void pressure_and_fairness(tests::TTestContext& ctx)
{
    host::SPressureLane lanes[4];
    host::CPressureSelection selector{ lanes, 4u };
    lanes[0] = { 32u, 32u, 0u, 0u, true, false };
    lanes[1] = { 63u, 64u, 0u, 1u, true, false };
    lanes[2] = { 1u, 1024u, 0u, 0u, true, true };
    lanes[3] = { 0u, 32u, 0u, 1u, true, false }; //  Runnable retained head; its ring is empty.
    std::uint32_t last[4]{};
    std::uint32_t visits[4]{};
    for (std::uint32_t turn = 1u; turn <= 160u; ++turn)
    {
        const host::SPressureSelection selected = selector.select();
        TEST_EXPECT(ctx, (selected.lane < 4u));
        if (selected.lane >= 4u) return;
        if (turn == 1u) TEST_EXPECT(ctx, (selected.lane == 0u));
        ++visits[selected.lane];
        last[selected.lane] = turn;
        selector.served(selected.lane);
        //  Model an outer budget that permits only one message on every pump.
        selector.finish(selected, true);
        for (std::uint32_t lane = 0u; lane < 4u; ++lane)
        {
            TEST_EXPECT(ctx, ((turn - last[lane]) <= 36u));
        }
        if (turn == 80u)
        {
            lanes[0].occupied = 1u;
            lanes[1].occupied = 64u; //  Host pressure now leads.
        }
    }
    for (const std::uint32_t count : visits) TEST_EXPECT(ctx, (count != 0u));

    lanes[0].runnable = lanes[1].runnable = lanes[2].runnable = false;
    lanes[0].occupied = 32u; //  A full ring behind a blocked head is not work.
    const host::SPressureSelection retained = selector.select();
    TEST_EXPECT(ctx, (retained.lane == 3u));
    lanes[3].runnable = false;
    TEST_EXPECT(ctx, (selector.select().lane == UINT32_MAX));
    lanes[0].runnable = true; //  Storage/admission change restores eligibility.
    TEST_EXPECT(ctx, (selector.select().lane == 0u));

    host::SPressureLane equal[3];
    host::CPressureSelection rotating{ equal, 3u };
    equal[0] = equal[1] = equal[2] = { 32u, 32u, 0u, 0u, true, false };
    for (std::uint32_t turn = 0u; turn < 3u; ++turn)
    {
        const host::SPressureSelection selected = rotating.select();
        TEST_EXPECT(ctx, (selected.lane == turn));
        rotating.served(selected.lane);
        rotating.finish(selected, true);
    }

    //  Endpoint populations change inside a fixed address table. Remove a
    //  ranked slot before reuse; surviving identities keep their relative order.
    const std::uint32_t first_order = equal[0].order;
    const std::uint32_t last_order = equal[2].order;
    rotating.withdraw(1u);
    equal[1] = {};
    TEST_EXPECT(ctx, (equal[0].order < equal[2].order));
    TEST_EXPECT(ctx, (first_order < last_order));
    equal[1] = { 32u, 32u, 0u, 0u, true, false };
    (void)rotating.select();
    TEST_EXPECT(ctx, ((equal[1].order > equal[0].order) && (equal[1].order > equal[2].order)));
    rotating.withdraw(2u);
    rotating.withdraw(1u);
    TEST_EXPECT(ctx, (rotating.select().lane == 0u));
    rotating.withdraw(0u);
    TEST_EXPECT(ctx, (rotating.select().lane == UINT32_MAX));
    equal[2] = { 1u, 32u, 0u, 0u, true, false };
    TEST_EXPECT(ctx, (rotating.select().lane == 2u));
    TEST_EXPECT(ctx, (equal[2].order == 1u));
}

void bounded_storage(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator, system_ids::host };
    for (const std::uint32_t limit : { 3u, 32u, 1024u })
    {
        host::TModuleFifo<std::uint32_t> fifo{ context, limit };
        for (std::uint32_t index = 0u; index < limit; ++index)
        {
            TEST_EXPECT(ctx, fifo.ensure_free(1u));
            TEST_EXPECT(ctx, fifo.push(index));
            TEST_EXPECT(ctx, (fifo.capacity() <= limit));
        }
        TEST_EXPECT(ctx, !fifo.ensure_free(1u));
        TEST_EXPECT(ctx, !fifo.push(9999u));
        for (std::uint32_t index = 0u; index < limit; ++index)
        {
            std::uint32_t value = UINT32_MAX;
            TEST_EXPECT(ctx, (fifo.pop(value) && (value == index)));
        }
        TEST_EXPECT(ctx, fifo.ensure_free(limit));
    }
    host::TModuleFifo<std::uint32_t> refused{ context };
    allocation.reject_all = true;
    TEST_EXPECT(ctx, !refused.ensure_free(1u));
    TEST_EXPECT(ctx, ((refused.size() == 0u) && (refused.capacity() == 0u)));
    allocation.reject_all = false;

    host::CModulePodPipe pipe{ module_ids::executive };
    TEST_EXPECT(ctx, (pipe.post({}) == host::EModulePipePost::invalid));
    TEST_EXPECT(ctx, (pipe.post(threading::make_pod_message(0, services::OpenServiceResult{})) == host::EModulePipePost::not_ready));
    TEST_EXPECT(ctx, !pipe.initialise(context, 33u));
    TEST_EXPECT(ctx, pipe.initialise(context, 32u));
    TEST_EXPECT(ctx, (pipe.post(threading::make_pod_message(0, preamble_test_types::SLocalPod{})) == host::EModulePipePost::invalid));
    TEST_EXPECT(ctx, (pipe.writable_count() == 32u));
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, (pipe.post(threading::make_pod_message(index, services::OpenServiceResult{})) == host::EModulePipePost::published));
    }
    TEST_EXPECT(ctx, ((pipe.readable_count() == 32u) && (pipe.writable_count() == 0u)));
    TEST_EXPECT(ctx, (pipe.post(threading::make_pod_message(32, services::OpenServiceResult{})) == host::EModulePipePost::full));
    TEST_EXPECT(ctx, (pipe.post({}) == host::EModulePipePost::invalid));
    for (std::int32_t index = 0; index < 32; ++index)
    {
        threading::CErasedPodMsg message;
        TEST_EXPECT(ctx, (pipe.read(message) && (message.query_async_slot() == index)));
    }
    TEST_EXPECT(ctx, (pipe.writable_count() == 32u));

    host::SModuleLifecycle lifecycle;
    TEST_EXPECT(ctx, lifecycle.initialise(context));
    for (std::uint32_t index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        host::SLifecycleMessage control;
        control.frontier.pods = index;
        TEST_EXPECT(ctx, lifecycle.to_preamble.post(control));
        TEST_EXPECT(ctx, lifecycle.to_host.post(control));
    }
    TEST_EXPECT(ctx, !lifecycle.to_preamble.post({}));
    TEST_EXPECT(ctx, !lifecycle.to_host.post({}));
    for (std::uint32_t index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        host::SLifecycleMessage control;
        TEST_EXPECT(ctx, (lifecycle.to_host.read(control) && (control.frontier.pods == index)));
        TEST_EXPECT(ctx, (lifecycle.to_preamble.read(control) && (control.frontier.pods == index)));
    }
}

void finite_output_turns(tests::TTestContext& ctx)
{
    host::COutputRetryTurn turn;
    constexpr std::uint32_t producers = host::COutputRetryTurn::k_producers;
    constexpr std::uint32_t ready = (1u << producers) - 1u;
    std::uint32_t operation[producers]{};
    std::uint32_t visits[producers][3]{};
    std::uint32_t admissions = 0u;
    for (std::uint32_t tick = 0u; tick < 1000u; ++tick)
    {
        const host::EOutputProducer selected = turn.next(ready);
        if (selected == host::EOutputProducer::count)
        {
            TEST_EXPECT(ctx, (turn.admission_due() && turn.admission_allowed()));
            ++admissions; //  A continually arriving fallback request can enter here.
            turn.admitted_or_examined();
            continue;
        }
        TEST_EXPECT(ctx, !turn.admission_allowed());
        const std::uint32_t allowance = turn.remaining();
        TEST_EXPECT(ctx, ((allowance > 0u) && (allowance <= host::COutputRetryTurn::k_allowance)));
        //  A capacity wake / exhausted outer budget cannot reset this allowance.
        TEST_EXPECT(ctx, (turn.next(ready) == selected));
        TEST_EXPECT(ctx, (turn.remaining() == allowance));
        const std::uint32_t producer = static_cast<std::uint32_t>(selected);
        ++visits[producer][operation[producer]];
        if (++operation[producer] == 3u) operation[producer] = 0u;
        turn.attempted(selected); //  One output per outer pump.
    }
    TEST_EXPECT(ctx, (admissions == 111u));
    for (std::uint32_t producer = 0u; producer < producers; ++producer)
    {
        for (const std::uint32_t count : visits[producer]) TEST_EXPECT(ctx, (count > (500u / (3u * producers))));
    }
    TEST_EXPECT(ctx, (turn.next(0u) == host::EOutputProducer::count));
    TEST_EXPECT(ctx, turn.admission_allowed()); //  Waiting on a later event cannot hold a pause.
    turn.admitted_or_examined();
}

void retained_reply_storage(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator, system_ids::host };
    const threading::CErasedPodMsg value = threading::make_pod_message(123, services::OpenServiceResult{});
    for (const std::uint32_t limit : { 3u, 1024u })
    {
        host::CModulePodPipe pipe{ module_ids::executive };
        host::CRetainedReplies replies{ context, limit };
        TEST_EXPECT(ctx, (replies.publish(pipe, value) == host::EReplyWrite::unavailable));
        TEST_EXPECT(ctx, pipe.initialise(context, 32u));
        for (std::int32_t index = 0; index < 32; ++index)
        {
            TEST_EXPECT(ctx, (replies.publish(pipe, threading::make_pod_message(index, services::OpenServiceResult{})) == host::EReplyWrite::published));
        }
        TEST_EXPECT(ctx, (replies.publish(pipe, value) == host::EReplyWrite::busy));
        TEST_EXPECT(ctx, ((replies.issued() == 32u) && (replies.published() == 32u)));
        {
            host::CRetainedReplies::CDispatch dispatch{ replies, limit };
            TEST_EXPECT(ctx, static_cast<bool>(dispatch));
            TEST_EXPECT(ctx, (replies.reservations() == limit));
            host::CRetainedReplies::CDispatch nested{ replies, 0u };
            TEST_EXPECT(ctx, !nested);
            for (std::uint32_t index = 0u; index < limit; ++index)
            {
                const std::int32_t correlation = static_cast<std::int32_t>(32u + index);
                TEST_EXPECT(ctx, (replies.retain(pipe, threading::make_pod_message(correlation, services::OpenServiceResult{})) == host::EReplyWrite::retained));
            }
            TEST_EXPECT(ctx, ((replies.size() == limit) && (replies.reservations() == 0u)));
        }
        {
            host::CRetainedReplies::CDispatch full{ replies, 1u };
            TEST_EXPECT(ctx, !full);
        }
        {
            host::CRetainedReplies::CDispatch zero_reply{ replies, 0u, false };
            TEST_EXPECT(ctx, static_cast<bool>(zero_reply));
        }
        TEST_EXPECT(ctx, (replies.publish(pipe, {}) == host::EReplyWrite::invalid));
        TEST_EXPECT(ctx, (replies.publish(pipe, value) == host::EReplyWrite::busy));
        TEST_EXPECT(ctx, (replies.issued() == (32u + limit)));
        std::int32_t expected = 0;
        threading::CErasedPodMsg message;
        while (replies.size())
        {
            TEST_EXPECT(ctx, pipe.read(message));
            TEST_EXPECT(ctx, (message.query_async_slot() == expected));
            ++expected;
            std::uint32_t published = UINT32_MAX;
            (void)replies.flush(pipe, 1u, published);
            TEST_EXPECT(ctx, (published == 1u));
        }
        while (pipe.read(message))
        {
            TEST_EXPECT(ctx, (message.query_async_slot() == expected));
            ++expected;
        }
        TEST_EXPECT(ctx, (static_cast<std::uint32_t>(expected) == (32u + limit)));
        TEST_EXPECT(ctx, (replies.idle() && (replies.issued() == replies.published())));
        {
            host::CRetainedReplies::CDispatch unused{ replies, limit };
            TEST_EXPECT(ctx, static_cast<bool>(unused));
            TEST_EXPECT(ctx, (replies.retain(pipe, {}) == host::EReplyWrite::invalid));
            TEST_EXPECT(ctx, (replies.reservations() == limit));
        }
        TEST_EXPECT(ctx, (replies.idle() && (replies.reservations() == 0u)));
        TEST_EXPECT(ctx, (replies.retain(pipe, value) == host::EReplyWrite::lost));
    }
    host::CRetainedReplies refused{ context, 3u };
    allocation.reject_all = true;
    {
        host::CRetainedReplies::CDispatch failed{ refused, 3u };
        TEST_EXPECT(ctx, !failed);
    }
    TEST_EXPECT(ctx, (refused.idle() && (refused.issued() == 0u)));
    allocation.reject_all = false;
    {
        host::CRetainedReplies::CDispatch paused{ refused, 1u, false };
        TEST_EXPECT(ctx, !paused);
    }
    {
        host::CRetainedReplies::CDispatch recovered{ refused, 3u };
        TEST_EXPECT(ctx, static_cast<bool>(recovered));
    }
    TEST_EXPECT(ctx, refused.idle());
}

modules::EExecutionStatus MV_STD_ABI_CALL create_state(const modules::SExecutionContext* const, void** const) noexcept
{
    return modules::EExecutionStatus::success;
}

void execution_boundary(tests::TTestContext& ctx)
{
    modules::SExecutionV1 execution;
    TEST_EXPECT(ctx, execution.is_valid()); //  No-child Host coordinator.
    execution.version = 2u;
    TEST_EXPECT(ctx, !execution.is_valid());
    execution.version = 1u;
    --execution.size;
    TEST_EXPECT(ctx, !execution.is_valid());
    execution.size = sizeof(execution);
    execution.flags = modules::k_execution_native_events;
    TEST_EXPECT(ctx, !execution.is_valid()); //  A coordinator cannot own native events.
    execution.flags = 0u;
    execution.create = &create_state;
    TEST_EXPECT(ctx, !execution.is_valid()); //  A partial callback set is never a coordinator.
    execution.create = nullptr;
    execution.flags = 2u;
    TEST_EXPECT(ctx, !execution.is_valid());
    const modules::CCommunication unbound;
    TEST_EXPECT(ctx, (unbound.submit({}, threading::CErasedPodMsg{}) == modules::ESubmission::invalid));
    TEST_EXPECT(ctx, modules::accepted(modules::ESubmission::published_wake_failed));
    TEST_EXPECT(ctx, !modules::accepted(modules::ESubmission::busy));
    TEST_EXPECT(ctx, !connections::SSource{ connections::SSource::EKind::self }.is_host());
}

struct SExecutionFixture
{
    const modules::SExecutionContext* context{ nullptr };
    modules::CCommunication copied;
    std::uint32_t self_count{ 0u }, host_count{ 0u }, async_count{ 0u }, admission_count{ 0u }, destroy_count{ 0u };
    bool stopping{ false }, pending{ false }, valid_sources{ true }, foreign_rejected{ false };
    std::atomic<bool> self_complete{ false };
    platform::threading::CPlatformThreadId created_on, destroyed_on;

    static std::uint32_t MV_STD_ABI_CALL foreign(void* const runtime) noexcept
    {
        SExecutionFixture& self = *static_cast<SExecutionFixture*>(runtime);
        self.foreign_rejected = self.copied.submit({}, threading::make_pod_message(42, services::OpenServiceResult{})) == modules::ESubmission::invalid;
        return 0u;
    }

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context, void** const runtime) noexcept
    {
        SExecutionFixture& self = *static_cast<SExecutionFixture*>(context->creation_context);
        self.context = context;
        self.created_on = platform::threading::query_current_thread_id();
        self.copied = *context->communication;
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime,
        const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
    {
        SExecutionFixture& self = *static_cast<SExecutionFixture*>(runtime);
        const modules::CCommunication& communication = *self.context->communication;
        const threading::CErasedPodMsg message = threading::make_pod_message(42, services::OpenServiceResult{});
        switch (invocation->event)
        {
            case modules::EExecutionEvent::start:
            {
                (void)communication.submit({ modules::SDestination::EKind::self }, message);
                for (std::uint32_t index = 0u; index < 32u; ++index)
                {
                    self.valid_sources = self.valid_sources && modules::accepted(communication.submit({}, message));
                }
                self.pending = communication.submit({}, message) == modules::ESubmission::busy;
                platform::threading::CThread worker;
                if (worker.create(&foreign, &self))
                {
                    self.valid_sources = self.valid_sources && worker.join_and_close();
                }
                else self.valid_sources = false;
                break;
            }
            case modules::EExecutionEvent::message:
            {
                if (invocation->source.kind == connections::SSource::EKind::self)
                {
                    ++self.self_count;
                    self.valid_sources = self.valid_sources && (invocation->source.address == self.context->address) &&
                        (invocation->source.endpoint.value == self.context->endpoint.value);
                    if (self.self_count < 100u)
                    {
                        self.valid_sources = self.valid_sources && modules::accepted(
                            communication.submit({ modules::SDestination::EKind::self }, message));
                    }
                    else
                    {
                        self.self_complete.store(true, std::memory_order_release);
                        (void)self.context->owner_wake.signal();
                    }
                }
                else
                {
                    ++self.host_count;
                    self.valid_sources = self.valid_sources && invocation->source.is_host() &&
                        (invocation->source.address == system_ids::host);
                }
                break;
            }
            case modules::EExecutionEvent::service_async:
            {
                ++self.async_count;
                if (self.pending)
                {
                    const modules::ESubmission posted = communication.submit({}, message);
                    if (modules::accepted(posted)) self.pending = false;
                }
                break;
            }
            case modules::EExecutionEvent::stop:
            {
                self.stopping = true;
                break;
            }
            case modules::EExecutionEvent::provider_admission_closed:
            {
                ++self.admission_count;
                break;
            }
            default:
            {
                break;
            }
        }
        *progress = {};
        progress->ready = 1u;
        progress->work_complete = self.stopping ? 1u : 0u;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        SExecutionFixture& self = *static_cast<SExecutionFixture*>(runtime);
        ++self.destroy_count;
        self.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
    static platform::threading::WaitError clock(void* const context, std::uint64_t& now) noexcept
    {
        now = ++*static_cast<std::uint64_t*>(context);
        return {};
    }
};

void common_dispatch_and_frontiers(tests::TTestContext& ctx)
{
    platform::threading::COwnerWait wake;
    TEST_EXPECT(ctx, (wake.initialise().code == platform::threading::EWaitError::none));
    threading::CEndpointResources resources{ module_ids::executable, module_ids::executive, thread_ids::host };
    resources.identity = { 581u };
    resources.instance = { 991u };
    resources.owner_wake = wake.endpoint();
    host::CModuleChannel channel{ resources };
    TEST_EXPECT(ctx, channel.initialise(*memory::get_ambient_memory_context(), 32u, 32u));
    TEST_EXPECT(ctx, resources.worker_to_host_owned_msgs.initialise(32u));
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &SExecutionFixture::create;
    execution.handle = &SExecutionFixture::handle;
    execution.destroy = &SExecutionFixture::destroy;
    SExecutionFixture state;
    std::uint64_t ticks = 0u;
    {
        host::CModulePreamble preamble{ resources, channel, binding, execution, *memory::get_ambient_memory_context(),
            &state, { &ticks, &SExecutionFixture::clock } };
        TEST_EXPECT(ctx, preamble.activate());
        preamble.process();
        TEST_EXPECT(ctx, ((state.self_count == 64u) && (state.async_count == 3u)));
        TEST_EXPECT(ctx, (state.foreign_rejected && state.valid_sources && state.pending));
        TEST_EXPECT(ctx, (resources.published_pods == 32u));
        TEST_EXPECT(ctx, (state.copied.submit({}, threading::make_pod_message(42, services::OpenServiceResult{})) == modules::ESubmission::invalid));
        preamble.process();
        TEST_EXPECT(ctx, ((state.self_count == 100u) && !preamble.work_pending()));
        std::uint64_t diagnostic_wait = UINT64_MAX;
        preamble.limit_wait(diagnostic_wait);
        TEST_EXPECT(ctx, ((diagnostic_wait != 0u) && (diagnostic_wait <= host::CPressureDiagnostic::k_threshold_ns)));
        ticks += host::CPressureDiagnostic::k_threshold_ns;
        TEST_EXPECT(ctx, preamble.work_pending()); //  A parked full pipe gets a bounded diagnostic turn.
        preamble.process(0u);
        TEST_EXPECT(ctx, (!preamble.failed() && state.pending && !preamble.work_pending()));
        diagnostic_wait = UINT64_MAX;
        preamble.limit_wait(diagnostic_wait);
        TEST_EXPECT(ctx, ((diagnostic_wait > host::CPressureDiagnostic::k_threshold_ns) &&
            (diagnostic_wait <= host::CPressureDiagnostic::k_repeat_ns)));
        threading::CErasedPodMsg message;
        TEST_EXPECT(ctx, channel.read_host_pod(message));
        TEST_EXPECT(ctx, preamble.work_pending());
        preamble.process(0u);
        TEST_EXPECT(ctx, (!state.pending && (resources.published_pods == 33u)));
        TEST_EXPECT(ctx, !preamble.work_pending());
        diagnostic_wait = UINT64_MAX;
        preamble.limit_wait(diagnostic_wait);
        TEST_EXPECT(ctx, (diagnostic_wait == UINT64_MAX));

        host::SLifecycleMessage control;
        while (channel.read_host_control(control)) {}
        preamble.request_stop(modules::EExecutionStop::requested);
        preamble.process();
        TEST_EXPECT(ctx, state.stopping);
        bool closed = false;
        while (channel.read_host_control(control))
        {
            if (control.operation == host::ELifecycle::close_provider_admission)
            {
                closed = true;
                TEST_EXPECT(ctx, (control.frontier.pods == 33u));
            }
            TEST_EXPECT(ctx, (control.operation != host::ELifecycle::seal));
        }
        TEST_EXPECT(ctx, closed);
        control = {};
        control.operation = host::ELifecycle::provider_admission_closed;
        control.frontier.host_output = 2u;
        TEST_EXPECT(ctx, channel.host_control(control));
        preamble.process();
        TEST_EXPECT(ctx, ((state.admission_count == 0u) && !preamble.destroyed() && !preamble.work_pending()));
        TEST_EXPECT(ctx, (channel.to_module.post(threading::make_pod_message(3, services::OpenServiceResult{})) == host::EModulePipePost::published));
        preamble.process();
        TEST_EXPECT(ctx, ((state.host_count == 1u) && (state.admission_count == 0u)));
        TEST_EXPECT(ctx, (channel.to_module.post(threading::make_pod_message(4, services::OpenServiceResult{})) == host::EModulePipePost::published));
        preamble.process();
        TEST_EXPECT(ctx, ((state.host_count == 2u) && (state.admission_count == 1u)));
        bool sealed = false;
        while (channel.read_host_control(control))
        {
            if (control.operation == host::ELifecycle::seal)
            {
                sealed = true;
                TEST_EXPECT(ctx, ((control.frontier.pods == 33u) && (control.frontier.owners == 0u)));
            }
        }
        TEST_EXPECT(ctx, (sealed && !preamble.destroyed()));
        control = {};
        control.operation = host::ELifecycle::sealed;
        TEST_EXPECT(ctx, channel.host_control(control));
        preamble.process();
        TEST_EXPECT(ctx, (preamble.destroyed() && (state.destroy_count == 1u) && !preamble.failed()));
    }
    TEST_EXPECT(ctx, (state.copied.submit({}, threading::make_pod_message(42, services::OpenServiceResult{})) == modules::ESubmission::invalid));
}

void host_retained_ingress(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    host::SClientRecord* const other = host::SModulePreambleTestAccess::install(runtime, module_ids::audio, binding);
    TEST_EXPECT(ctx, (client && other));
    if (!client || !other)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(index, services::OpenServiceResult{})));
    }
    for (std::int32_t index = 0; index < 3; ++index)
    {
        TEST_EXPECT(ctx, (client->channel->to_host.post(threading::make_pod_message(index,
            services::OpenServiceRequest{ module_ids::rendering, 1u, 1u, { 1u } })) == host::EModulePipePost::published));
    }
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, ((client->pending_replies->size() == 3u) && (client->accounted_pods == 3u)));
    TEST_EXPECT(ctx, ((client->reserved_replies == 0u) && (client->pending_replies->reservations() == 0u)));
    TEST_EXPECT(ctx, (client->channel->to_host.post(threading::make_pod_message(99,
        services::OpenServiceRequest{ module_ids::rendering, 1u, 1u, { 2u } })) == host::EModulePipePost::published));
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime));
    TEST_EXPECT(ctx, (client->pod_head_live && !client->channel->to_host.readable_count()));
    const threading::CErasedPodMsg settlement = threading::make_pod_message(-1, services::ReleaseServiceBorrow{});
    for (std::uint32_t index = 0u; index < 32u; ++index)
    {
        TEST_EXPECT(ctx, (client->channel->to_host.post(settlement) == host::EModulePipePost::published));
    }
    TEST_EXPECT(ctx, (other->channel->to_host.post(settlement) == host::EModulePipePost::published));
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime));
    TEST_EXPECT(ctx, ((client->channel->to_host.readable_count() == 32u) && (client->accounted_pods == 3u)));
    TEST_EXPECT(ctx, (other->accounted_pods == 1u));
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, client->channel->read_preamble_pod(message));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, ((client->accounted_pods == 36u) && !client->pod_head_live && !client->channel->to_host.readable_count()));
    TEST_EXPECT(ctx, ((client->pending_replies->size() == 3u) && (client->reserved_replies == 0u) &&
        (client->pending_replies->reservations() == 0u)));
    TEST_EXPECT(ctx, (client->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::none));
    host::SModulePreambleTestAccess::clear(runtime);
}

void fallback_continuation_and_terminal(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding, 16u);
    TEST_EXPECT(ctx, client);
    if (!client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(index, services::OpenServiceResult{})));
    }
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 12u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(12u)));
        for (std::int32_t index = 32; index < 44; ++index)
        {
            TEST_EXPECT(ctx, client->reply(threading::make_pod_message(index, services::OpenServiceResult{})));
        }
    }
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime)); //  Full is not runnable.
    threading::CErasedPodMsg message;
    while (client->channel->read_preamble_pod(message)) {}
    TEST_EXPECT(ctx, host::SModulePreambleTestAccess::drain(runtime)); //  Eight published, four remain runnable.
    TEST_EXPECT(ctx, ((client->channel->to_module.readable_count() == 8u) && (client->pending_replies->size() == 4u)));
    const std::uint64_t published = client->pending_replies->published();
    client->phase = host::EClientPhase::stopped;
    client->terminal_before_drain = true;
    client->incoming_drained = false;
    client->flush_replies();
    TEST_EXPECT(ctx, ((client->pending_replies->published() == published) &&
        (client->channel->to_module.readable_count() == 8u) && (client->pending_replies->size() == 4u)));
    client->incoming_drained = true;
    client->flush_replies();
    TEST_EXPECT(ctx, (client->pending_replies->idle() && (client->pending_replies->published() == published)));
    TEST_EXPECT(ctx, (client->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::none));
    host::SModulePreambleTestAccess::clear(runtime);
}

void host_private_frontiers(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    TEST_EXPECT(ctx, client);
    if (!client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(index, services::OpenServiceResult{})));
    }
    host::SLifecycleMessage close;
    close.operation = host::ELifecycle::close_provider_admission;
    close.frontier = { 1u, 0u, 0u };
    TEST_EXPECT(ctx, client->channel->preamble_control(close));
    close.operation = host::ELifecycle::seal;
    TEST_EXPECT(ctx, client->channel->preamble_control(close));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, (client->seal_pending && !client->provider_closed && !client->seal_acknowledged));
    TEST_EXPECT(ctx, !client->channel->lifecycle.to_preamble.readable_count());
    TEST_EXPECT(ctx, (client->channel->to_host.post(threading::make_pod_message(40,
        services::OpenServiceRequest{ module_ids::audio, 1u, 1u, { 1u } })) == host::EModulePipePost::published));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, ((client->accounted_pods == 1u) && client->seal_acknowledged && client->provider_closed));
    TEST_EXPECT(ctx, ((client->pending_replies->size() == 1u) && (client->pending_replies->issued() == 33u)));
    host::SLifecycleMessage result;
    TEST_EXPECT(ctx, client->channel->read_preamble_control(result));
    TEST_EXPECT(ctx, ((result.operation == host::ELifecycle::provider_admission_closed) && (result.frontier.host_output == 33u)));
    TEST_EXPECT(ctx, client->channel->read_preamble_control(result));
    TEST_EXPECT(ctx, (result.operation == host::ELifecycle::sealed));
    TEST_EXPECT(ctx, (client->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::none));
    host::SModulePreambleTestAccess::clear(runtime);
}

void private_connection_activation(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const first = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    host::SClientRecord* const second = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding);
    host::SClientRecord* const requester = host::SModulePreambleTestAccess::install(runtime, module_ids::audio, binding);
    TEST_EXPECT(ctx, (first && second && requester));
    if (!first || !second || !requester)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    host::CDirectConnections& connections = host::SModulePreambleTestAccess::connections(runtime);
    const auto progress = [&]()
    {
        (void)host::SModulePreambleTestAccess::drain(runtime);
        connections.progress();
    };
    const auto post = [&](host::SClientRecord& client, const host::ELifecycle operation, const connections::SToken token)
    {
        host::SLifecycleMessage control;
        control.operation = operation;
        control.correlation = 1;
        control.notification.connection = token;
        control.notification.connection_status = connections::EStatus::success;
        TEST_EXPECT(ctx, client.channel->preamble_control(control));
    };
    const auto read = [&](host::SClientRecord& client, const host::ELifecycle expected)
    {
        host::SLifecycleMessage control;
        TEST_EXPECT(ctx, client.channel->read_preamble_control(control));
        TEST_EXPECT(ctx, (control.operation == expected));
        return control;
    };
    post(*first, host::ELifecycle::participate, {});
    post(*second, host::ELifecycle::participate, {});
    progress();
    (void)read(*first, host::ELifecycle::participated);
    (void)read(*second, host::ELifecycle::participated);
    for (std::uint32_t attempt = 0u; attempt < 2u; ++attempt)
    {
        host::SLifecycleMessage request;
        request.operation = host::ELifecycle::connect;
        request.correlation = 8;
        request.first_address = system_ids::ops::make_system_id(first->module, thread_ids::host);
        request.second_address = system_ids::ops::make_system_id(second->module, thread_ids::host);
        TEST_EXPECT(ctx, requester->channel->preamble_control(request));
        progress();
        const connections::SToken token = read(*first, host::ELifecycle::offer).notification.connection;
        TEST_EXPECT(ctx, (read(*second, host::ELifecycle::offer).notification.connection.value == token.value));
        post(*first, host::ELifecycle::decision, token);
        post(*second, host::ELifecycle::decision, token);
        progress();
        const host::SDirectEndpointDescriptor* const descriptor = read(*first, host::ELifecycle::install).descriptor;
        (void)read(*second, host::ELifecycle::install);
        TEST_EXPECT(ctx, (descriptor && (descriptor->incoming->capacity() == 1024u) && (descriptor->outgoing->capacity() == 1024u)));
        post(*first, host::ELifecycle::installed, token);
        post(*second, host::ELifecycle::installed, token);
        progress();
        (void)read(*first, host::ELifecycle::activate);
        (void)read(*second, host::ELifecycle::activate);
        post(*first, host::ELifecycle::activated, token);
        progress();
        TEST_EXPECT(ctx, !requester->channel->lifecycle.to_preamble.readable_count());
        TEST_EXPECT(ctx, (descriptor && descriptor->admission->available()));
        if (attempt == 1u)
        {
            host::SDirectConnectionTestAccess::expire(connections, token, requester->host_execution.restriction);
            std::uint64_t remaining = UINT64_MAX;
            connections.limit_wait(remaining);
            TEST_EXPECT(ctx, (remaining == 0u));
            connections.progress();
            post(*second, host::ELifecycle::activated, token); //  Late after timeout cannot restore success.
            progress();
        }
        else
        {
            post(*second, host::ELifecycle::activated, token);
            progress();
        }
        const host::SLifecycleMessage result = read(*requester, host::ELifecycle::connection_result);
        TEST_EXPECT(ctx, (result.notification.connection_status ==
            ((attempt == 0u) ? connections::EStatus::success : connections::EStatus::timed_out)));
        if (attempt == 0u)
        {
            post(*first, host::ELifecycle::close, token);
            progress();
        }
        (void)read(*first, host::ELifecycle::closing);
        (void)read(*second, host::ELifecycle::closing);
        TEST_EXPECT(ctx, (descriptor && !descriptor->admission->available()));
        post(*first, host::ELifecycle::quiescent, token);
        post(*second, host::ELifecycle::quiescent, token);
        progress();
        (void)read(*first, host::ELifecycle::retired);
        (void)read(*second, host::ELifecycle::retired);
        TEST_EXPECT(ctx, (connections.empty() && !connections.failed()));
        TEST_EXPECT(ctx, !requester->channel->lifecycle.to_preamble.readable_count());
    }
    host::SModulePreambleTestAccess::clear(runtime);
}

class CPeerExecution
{
public:
    modules::CCommunication communication;
    system_ids::id_type address{}, peer{};
    connections::SToken token;
    connections::EStatus result{ connections::EStatus::busy };
    bool participated{ false }, participation_sent{ false }, request{ false }, requested{ false };
    bool active{ false }, pause_input{ false }, close{ false }, closing{ false }, hold{ false }, quiescent{ false };
    bool stopping{ false }, valid{ true }, send_on_available{ false };
    bool allow_host{ false };
    module_ids::id_type test_module{};
    std::atomic<bool> test_start{ false }, test_done{ false };
    bool test_sent{ false };
    const threading::CErasedPodMsg* outgoing{ nullptr };
    threading::CErasedPodMsg* observations{ nullptr };
    std::uint32_t observation_capacity{ 0u };
    platform::threading::CPlatformThreadId created_on, destroyed_on;
    std::uint32_t received{ 0u }, retired{ 0u }, send_count{ 0u }, destroyed{ 0u };

    void handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        using EEvent = modules::EExecutionEvent;
        using EKind = modules::SExecutionIntent::EKind;
        modules::SExecutionIntent intent;
        intent.connection = invocation.notification.connection;
        switch (invocation.event)
        {
            case EEvent::start:
            case EEvent::service_async:
            {
                if (!participation_sent)
                {
                    intent.kind = EKind::participate;
                    intent.correlation = 0;
                    participation_sent = modules::accepted(communication.submit(intent));
                }
                if (test_start.load(std::memory_order_acquire)) request = true;
                if (request && participated && !requested)
                {
                    intent.kind = EKind::connect;
                    intent.correlation = 1;
                    intent.first = address;
                    intent.second = peer;
                    requested = modules::accepted(communication.submit(intent));
                }
                if (active && module_ids::ops::is_valid_id(test_module) && !test_sent)
                {
                    test_sent = modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, token },
                        threading::make_pod_message(77, modules::RunTestsRequest{ test_module })));
                }
                if (active && outgoing)
                {
                    const modules::ESubmission status = communication.submit({ modules::SDestination::EKind::connection, {}, token }, *outgoing);
                    if (modules::accepted(status)) outgoing = nullptr;
                    else valid = valid && (status == modules::ESubmission::busy);
                }
                for (std::uint32_t count = 0u; active && send_count && (count < 100u); ++count)
                {
                    const modules::ESubmission sent = communication.submit({ modules::SDestination::EKind::connection, {}, token },
                        threading::make_pod_message(7, services::OpenServiceResult{}));
                    if (sent == modules::ESubmission::busy)
                    {
                        break;
                    }
                    valid = valid && modules::accepted(sent);
                    --send_count;
                }
                if (close)
                {
                    intent.kind = EKind::close_connection;
                    intent.connection = token;
                    if (modules::accepted(communication.submit(intent))) close = false;
                }
                break;
            }
            case EEvent::participation:
            {
                participated = invocation.notification.connection_status == connections::EStatus::success;
                break;
            }
            case EEvent::connection_offered:
            {
                token = invocation.notification.connection;
                intent.kind = EKind::consent;
                intent.decision = connections::EStatus::success;
                valid = valid && modules::accepted(communication.submit(intent));
                break;
            }
            case EEvent::connection_available:
            {
                active = true;
                if (send_on_available)
                {
                    valid = valid && modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, token },
                        threading::make_pod_message(6, services::OpenServiceResult{})));
                }
                break;
            }
            case EEvent::message:
            {
                if (allow_host && invocation.source.is_host())
                {
                    break;
                }
                valid = valid && active && (invocation.source.kind == connections::SSource::EKind::direct) &&
                    (invocation.source.address == peer) && (invocation.source.connection.value == token.value);
                ++received;
                if (observations)
                {
                    if (received <= observation_capacity) observations[received - 1u] = *invocation.message;
                    else valid = false;
                }
                if (module_ids::ops::is_valid_id(test_module))
                {
                    modules::RunTestsResult tested;
                    valid = valid && invocation.message && invocation.message->copy_payload_to(tested) &&
                        (invocation.message->query_async_slot() == 77) && (tested.module == test_module) &&
                        (tested.status == modules::ETestRunStatus::success) && (tested.passed == 23u) &&
                        (tested.failed == 0u) && (tested.skipped == 0u);
                    test_done.store(true, std::memory_order_release);
                }
                break;
            }
            case EEvent::connection_result:
            {
                result = invocation.notification.connection_status;
                break;
            }
            case EEvent::connection_closing:
            {
                active = false;
                closing = true;
                send_count = 0u;
                break;
            }
            case EEvent::connection_retired:
            {
                ++retired;
                break;
            }
            case EEvent::stop:
            {
                stopping = true;
                break;
            }
            default:
            {
                break;
            }
        }
        if (closing && !hold && !quiescent)
        {
            intent.kind = EKind::connection_quiescent;
            intent.connection = token;
            quiescent = modules::accepted(communication.submit(intent));
        }
        progress = {};
        progress.ready = participated ? 1u : 0u;
        progress.direct_input = pause_input ? 0u : 1u;
        progress.work_complete = stopping ? 1u : 0u;
    }

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        CPeerExecution& self = *static_cast<CPeerExecution*>(context->creation_context);
        self.communication = *context->communication;
        self.address = context->address;
        self.created_on = platform::threading::query_current_thread_id();
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL dispatch(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        static_cast<CPeerExecution*>(runtime)->handle(*invocation, *progress);
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        CPeerExecution& self = *static_cast<CPeerExecution*>(runtime);
        ++self.destroyed;
        self.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
};

void preamble_connection_cancellation(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &CPeerExecution::create;
    execution.handle = &CPeerExecution::dispatch;
    execution.destroy = &CPeerExecution::destroy;
    CPeerExecution a, b;
    a.peer = system_ids::ops::make_system_id(module_ids::rendering, thread_ids::host);
    b.peer = system_ids::ops::make_system_id(module_ids::executive, thread_ids::host);
    host::SClientRecord* const first = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding, 3u, execution, &a);
    host::SClientRecord* const second = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding, 3u, execution, &b);
    TEST_EXPECT(ctx, (first && second));
    if (!first || !second)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    host::CDirectConnections& connections = host::SModulePreambleTestAccess::connections(runtime);
    TEST_EXPECT(ctx, (first->preamble->activate() && second->preamble->activate()));
    const auto turn = [&]()
    {
        first->preamble->process();
        second->preamble->process();
        (void)host::SModulePreambleTestAccess::drain(runtime);
        connections.progress();
    };
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, (a.participated && b.participated));
    a.request = a.send_on_available = true;
    for (std::uint32_t count = 0u; count < 12u; ++count) turn();
    TEST_EXPECT(ctx, (a.valid && b.valid && a.active && b.active && (b.received == 1u)));
    TEST_EXPECT(ctx, (a.result == connections::EStatus::success));
    //  A prior data/capacity notification must not suppress a later wake after
    //  the owner has drained its queue and parked. Exercise both directions.
    for (std::uint32_t count = 0u; count < 2u; ++count)
    {
        first->endpoint->work_pending.store(0u, std::memory_order_release);
        second->endpoint->work_pending.store(0u, std::memory_order_release);
        a.send_count = 1u;
        first->preamble->process();
        TEST_EXPECT(ctx, (second->endpoint->work_pending.load(std::memory_order_acquire) == 1u));
        second->preamble->process();
        TEST_EXPECT(ctx, (b.received == (count + 2u)));
        TEST_EXPECT(ctx, (first->endpoint->work_pending.load(std::memory_order_acquire) == 1u));
        first->preamble->process();
    }
    b.pause_input = b.hold = true;
    second->preamble->process();
    a.send_count = 100u;
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, ((a.send_count == 0u) && (b.received == 3u)));
    a.close = true;
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, (a.closing && b.closing && !connections.empty() && (b.retired == 0u)));
    b.hold = false;
    for (std::uint32_t count = 0u; count < 8u; ++count) turn();
    TEST_EXPECT(ctx, (connections.empty() && (a.retired == 1u) && (b.retired == 1u) && (b.received == 3u)));
    TEST_EXPECT(ctx, (a.valid && b.valid && !first->preamble->failed() && !second->preamble->failed()));
    first->preamble->request_stop(modules::EExecutionStop::requested);
    second->preamble->request_stop(modules::EExecutionStop::requested);
    for (std::uint32_t count = 0u; count < 8u; ++count) turn();
    TEST_EXPECT(ctx, ((a.destroyed == 1u) && (b.destroyed == 1u)));
    host::SModulePreambleTestAccess::clear(runtime);
}

// Each test action runs once inside the actual module callback. No test helper
// invokes a bound facade outside its authenticated execution context.
class CBulkPeerExecution
{
public:
    modules::CCommunication communication;
    system_ids::id_type address{};
    services::SClientToken endpoint;
    modules::SExecutionNotification results[8]{};
    std::uint32_t result_count{ 0u }, received{ 0u }, available{ 0u }, retired{ 0u }, destroyed{ 0u };
    bool participated{ false }, stopping{ false }, hold{ false }, valid{ true }, pause_input{ false }, ordered{ false };
    bool consent{ true }, result_before_available{ false };
    bool allow_host{ false }, track_sources{ false };
    services::SClientToken source_ids[2]{};
    std::int32_t source_counts[3]{};
    unsigned offers{ 0u };
    std::int32_t next_slot{ 0 };
    threading::CErasedPodMsg last_message;
    connections::SSource last_source;
    connections::SToken closing, offered;
    void* action_context{ nullptr };
    void (*action)(CBulkPeerExecution&, void* const) noexcept{ nullptr };
    platform::threading::CPlatformThreadId created_on, destroyed_on;

    void handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept
    {
        using EEvent = modules::EExecutionEvent;
        using EKind = modules::SExecutionIntent::EKind;
        modules::SExecutionIntent intent;
        switch (invocation.event)
        {
            case EEvent::start:
            {
                intent.correlation = 0;
                valid = modules::accepted(communication.submit(intent)) && valid;
                break;
            }
            case EEvent::participation:
            {
                participated = invocation.notification.connection_status == connections::EStatus::success;
                valid = participated && valid;
                break;
            }
            case EEvent::connection_offered:
            {
                offered = invocation.notification.connection;
                ++offers;
                intent.kind = EKind::consent;
                intent.connection = invocation.notification.connection;
                intent.decision = connections::EStatus::success;
                if (consent) valid = modules::accepted(communication.submit(intent)) && valid;
                break;
            }
            case EEvent::connection_available:
            {
                ++available;
                break;
            }
            case EEvent::connection_result:
            {
                if ((invocation.notification.connection_status == connections::EStatus::success) &&
                    ((invocation.notification.first.value == endpoint.value) ||
                        (invocation.notification.second.value == endpoint.value)) && !available)
                {
                    result_before_available = true;
                }
                if (result_count < std::size(results)) results[result_count++] = invocation.notification;
                else valid = false;
                break;
            }
            case EEvent::connection_closing:
            {
                closing = invocation.notification.connection;
                break;
            }
            case EEvent::connection_retired:
            {
                ++retired;
                break;
            }
            case EEvent::message:
            {
                valid = valid && ((invocation.source.kind == connections::SSource::EKind::direct) ||
                    (allow_host && invocation.source.is_host()));
                last_message = *invocation.message;
                last_source = invocation.source;
                if (ordered) valid = (last_message.query_async_slot() == next_slot++) && valid;
                if (track_sources)
                {
                    const unsigned source = last_source.is_host() ? 0u :
                        ((last_source.endpoint.value == source_ids[0].value) ? 1u : 2u);
                    valid = (last_source.is_host() || (last_source.endpoint.value == source_ids[source - 1u].value)) && valid;
                    valid = (last_message.query_async_slot() == source_counts[source]++) && valid;
                }
                ++received;
                break;
            }
            case EEvent::stop:
            {
                stopping = true;
                break;
            }
            case EEvent::service_async:
            {
                if (action)
                {
                    const auto invoke = action;
                    action = nullptr;
                    invoke(*this, action_context);
                }
                break;
            }
            default:
            {
                break;
            }
        }
        if (closing.value && !hold)
        {
            intent.kind = EKind::connection_quiescent;
            intent.connection = closing;
            const bool accepted = modules::accepted(communication.submit(intent));
            valid = accepted && valid;
            if (accepted) closing = {};
        }
        progress = {};
        progress.ready = participated ? 1u : 0u;
        progress.direct_input = pause_input ? 0u : 1u;
        progress.work_complete = stopping ? 1u : 0u;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        CBulkPeerExecution& self = *static_cast<CBulkPeerExecution*>(context->creation_context);
        self.communication = *context->communication;
        self.address = context->address;
        self.endpoint = context->endpoint;
        self.created_on = platform::threading::query_current_thread_id();
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL dispatch(void* const runtime,
        const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
    {
        static_cast<CBulkPeerExecution*>(runtime)->handle(*invocation, *progress);
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        CBulkPeerExecution& self = *static_cast<CBulkPeerExecution*>(runtime);
        ++self.destroyed;
        self.destroyed_on = platform::threading::query_current_thread_id();
        return modules::EExecutionStatus::success;
    }
};

struct SCommonBulkFixture
{
    tests::TTestContext& test;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    CBulkPeerExecution a, b;
    host::SClientRecord* first{ nullptr };
    host::SClientRecord* second{ nullptr };

    explicit SCommonBulkFixture(tests::TTestContext& ctx) noexcept : test{ ctx }
    {
        modules::SBoundModuleTestAccess::install(binding);
        modules::SExecutionV1 execution;
        execution.create = &CBulkPeerExecution::create;
        execution.handle = &CBulkPeerExecution::dispatch;
        execution.destroy = &CBulkPeerExecution::destroy;
        first = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding, 3u, execution, &a);
        second = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding, 3u, execution, &b);
        TEST_EXPECT(test, (first && second));
        if (!first || !second)
        {
            return;
        }
        TEST_EXPECT(test, (first->preamble->activate() && second->preamble->activate()));
        step(4u);
        TEST_EXPECT(test, (a.participated && b.participated));
    }
    host::CDirectConnections& connections() noexcept { return host::SModulePreambleTestAccess::connections(runtime); }
    void step(const unsigned count = 1u) noexcept
    {
        for (unsigned index = 0u; index < count; ++index)
        {
            first->preamble->process();
            second->preamble->process();
            (void)host::SModulePreambleTestAccess::drain(runtime);
            connections().progress();
        }
    }
    template<typename T> void invoke(CBulkPeerExecution& peer, T&& operation) noexcept
    {
        peer.action_context = &operation;
        peer.action = [](CBulkPeerExecution& state, void* const context) noexcept
        {
            (*static_cast<std::remove_reference_t<T>*>(context))(state.communication);
        };
        ((&peer == &a) ? first : second)->preamble->process();
        TEST_EXPECT(test, !peer.action);
        peer.action_context = nullptr;
    }
    void request(CBulkPeerExecution& peer, const connections::SBulkConfiguration bulk,
        const std::int32_t correlation, const std::uint32_t version = 1u) noexcept
    {
        invoke(peer, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::connect;
            intent.correlation = correlation;
            intent.first = peer.address;
            intent.second = (&peer == &a) ? b.address : a.address;
            intent.bulk = bulk;
            intent.connection_version = version;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
    }
    connections::SToken establish(const bool consumer_first, const bool reciprocal,
        const connections::SBulkConfiguration bulk = { 129u, module_ids::executive }) noexcept
    {
        a.result_count = b.result_count = 0u;
        request(consumer_first ? b : a, bulk, consumer_first ? 9 : 7);
        if (reciprocal) request(consumer_first ? a : b, bulk, consumer_first ? 7 : 9);
        step(12u);
        const modules::SExecutionNotification& result = consumer_first ? b.results[0] : a.results[0];
        TEST_EXPECT(test, ((result.connection_status == connections::EStatus::success) && result.connection.value));
        TEST_EXPECT(test, (a.result_count == ((!consumer_first || reciprocal) ? 1u : 0u)));
        TEST_EXPECT(test, (b.result_count == ((consumer_first || reciprocal) ? 1u : 0u)));
        if (reciprocal)
        {
            TEST_EXPECT(test, ((a.results[0].connection.value == b.results[0].connection.value) &&
                (a.results[0].connection_status == connections::EStatus::success) &&
                (b.results[0].connection_status == connections::EStatus::success)));
        }
        return result.connection;
    }
    void close(const connections::SToken token) noexcept
    {
        invoke(a, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::close_connection;
            intent.connection = token;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
        step(8u);
    }
    ~SCommonBulkFixture() noexcept
    {
        if (first && second)
        {
            a.hold = b.hold = false;
            first->preamble->request_stop(modules::EExecutionStop::requested);
            second->preamble->request_stop(modules::EExecutionStop::requested);
            step(16u);
            TEST_EXPECT(test, (connections().empty() && a.valid && b.valid));
            TEST_EXPECT(test, ((a.destroyed == 1u) && (b.destroyed == 1u) &&
                (a.created_on == a.destroyed_on) && (b.created_on == b.destroyed_on)));
            TEST_EXPECT(test, ((first->endpoint->notification_pins == 0u) && (second->endpoint->notification_pins == 0u)));
        }
        host::SModulePreambleTestAccess::clear(runtime);
    }
};

void common_bulk_buffers(tests::TTestContext& ctx, const bool consumer_first, const bool reciprocal)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(consumer_first, reciprocal);
    const modules::SDestination destination{ modules::SDestination::EKind::connection, {}, token };
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query(destination).active);
        TEST_EXPECT(ctx, !communication.discard_and_acquire(token));
        TEST_EXPECT(ctx, !communication.consumer_data(token));
        TEST_EXPECT(ctx, !communication.producer_data(token));
        TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::inactive));
    });
    void* slots[3]{};
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.discard_and_acquire(token));
        TEST_EXPECT(ctx, (communication.submit(destination, threading::make_pod_message(0,
            modules::RunTestsRequest{ module_ids::rendering })) == modules::ESubmission::no_pipe));
        for (unsigned index = 0u; index < 2u; ++index)
        {
            const memory::CMemoryView output = communication.producer_data(token);
            TEST_EXPECT(ctx, ((output.bytes() == 129u) && (output.storage_alignment() == 128u)));
            if (!output) return;
            slots[index] = output.data();
            std::memset(output.data(), static_cast<int>(index + 1u), output.bytes());
            TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::published));
        }
    });
    memory::CMemoryConstView held;
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        // process() has cleared the notification hint before this callback.
        TEST_EXPECT(ctx, communication.discard_and_acquire(token));
        held = communication.consumer_data(token);
        TEST_EXPECT(ctx, ((held.bytes() == 129u) && (held.data() == slots[1])));
    });
    if (!held) return;
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        for (unsigned index = 0u; index < 32u; ++index)
        {
            const memory::CMemoryView output = communication.producer_data(token);
            TEST_EXPECT(ctx, (output && (output.data() != held.data()) &&
                ((reinterpret_cast<std::uintptr_t>(output.data()) & 127u) == 0u)));
            if (!output) return;
            if (index == 1u) slots[2] = output.data();
            std::memset(output.data(), static_cast<int>(index + 3u), output.bytes());
            TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::published));
            for (std::size_t byte = 0u; byte < held.bytes(); ++byte)
            {
                TEST_EXPECT(ctx, (static_cast<const unsigned char*>(held.data())[byte] == 2u));
            }
        }
    });
    TEST_EXPECT(ctx, ((slots[0] != slots[1]) && (slots[0] != slots[2]) && (slots[1] != slots[2])));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.discard_and_acquire(token));
        const memory::CMemoryConstView latest = communication.consumer_data(token);
        TEST_EXPECT(ctx, (latest && (*static_cast<const unsigned char*>(latest.data()) == 34u)));
        TEST_EXPECT(ctx, !communication.discard_and_acquire(token));
        TEST_EXPECT(ctx, !communication.consumer_data(token));
    });
    fixture.close(token);
    TEST_EXPECT(ctx, fixture.connections().empty());
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.producer_data(token));
        TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::inactive));
    });
    const connections::SToken successor = fixture.establish(!consumer_first, reciprocal);
    TEST_EXPECT(ctx, (successor.value != token.value));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.discard_and_acquire(successor));
        TEST_EXPECT(ctx, !communication.consumer_data(successor));
    });
    fixture.close(successor);
}

void common_bulk_configuration(tests::TTestContext& ctx, const bool wrong_producer)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken pod = fixture.establish(false, false, {});
    fixture.a.result_count = fixture.b.result_count = 0u;
    fixture.request(fixture.a, { 129u, module_ids::executive }, 7);
    fixture.request(fixture.b, { wrong_producer ? 129u : 257u,
        wrong_producer ? module_ids::rendering : module_ids::executive }, 9);
    fixture.step(12u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 1u)));
    TEST_EXPECT(ctx, (fixture.a.results[0].connection_status == connections::EStatus::success));
    TEST_EXPECT(ctx, (fixture.b.results[0].connection_status == connections::EStatus::configuration_mismatch));
    const connections::SToken bulk = fixture.a.results[0].connection;
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, pod }).active);
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, bulk }).active);
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, pod },
            threading::make_pod_message(31, modules::RunTestsRequest{ module_ids::rendering }))));
    });
    fixture.step();
    TEST_EXPECT(ctx, (fixture.b.received == 1u));
    fixture.close(bulk);
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, pod }).active);
    });
    fixture.close(pod);
}

void common_bulk_validation(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SBulkConfiguration invalid[]{
        { 0u, module_ids::executive }, { UINT64_MAX, module_ids::executive },
        { memory::k_byte_size_ceiling, module_ids::executive }, { 129u, module_ids::audio }
    };
    for (const connections::SBulkConfiguration configuration : invalid)
    {
        fixture.a.result_count = 0u;
        fixture.request(fixture.a, configuration, 7);
        fixture.step(4u);
        TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::invalid_request) &&
            !fixture.a.results[0].connection.value && fixture.connections().empty()));
    }
}

void common_bulk_allocation(tests::TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext memory{ allocator };
    {
        tests::TMemoryContextScope scope{ &memory };
        SCommonBulkFixture fixture{ ctx };
        fixture.request(fixture.a, { 129u, module_ids::executive }, 7);
        fixture.step(); // Host creates the offered operation before allocation failure.
        allocation.fail_on = allocation.attempts;
        fixture.step(12u);
        allocation.fail_on = SIZE_MAX;
        TEST_EXPECT(ctx, allocation.failed);
        TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::allocation_failed)));
        TEST_EXPECT(ctx, (fixture.connections().empty() && !fixture.connections().failed()));
    }
    TEST_EXPECT(ctx, memory.is_attribution_empty());
}

void common_bulk_retained_closure(tests::TTestContext& ctx, const bool wake_failed)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(false, false);
    fixture.b.hold = true;
    if (wake_failed)
    {
        // Inject an unavailable notification target after installation. The
        // bulk publication itself must still commit exactly once.
        host::SDirectEndpointDescriptor* const descriptor =
            host::SDirectConnectionTestAccess::private_descriptor(fixture.connections(), token, 0u);
        TEST_EXPECT(ctx, descriptor);
        if (!descriptor) return;
        descriptor->peer_notification = {};
    }
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        const memory::CMemoryView output = communication.producer_data(token);
        TEST_EXPECT(ctx, output);
        if (!output) return;
        std::memset(output.data(), 42, output.bytes());
        TEST_EXPECT(ctx, (communication.publish_bulk(token) == (wake_failed ?
            connections::EBulkPublish::published_wake_failed : connections::EBulkPublish::published)));
        if (wake_failed)
        {
            TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::inactive));
        }
    });
    memory::CMemoryConstView held;
    // The peer runs before Host consumes the closure. Even failed notification
    // does not roll back the already-published buffer.
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.discard_and_acquire(token));
        held = communication.consumer_data(token);
        TEST_EXPECT(ctx, (held && (*static_cast<const unsigned char*>(held.data()) == 42u)));
    });
    if (!held) return;
    if (wake_failed) fixture.step(8u);
    else fixture.close(token);
    TEST_EXPECT(ctx, (fixture.b.closing.value == token.value));
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::bulk_capacity(fixture.connections(), token) == 129u));
    TEST_EXPECT(ctx, ((fixture.first->endpoint->notification_pins == 1u) &&
        (fixture.second->endpoint->notification_pins == 1u)));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.discard_and_acquire(token));
        TEST_EXPECT(ctx, !communication.consumer_data(token));
        for (std::size_t byte = 0u; byte < held.bytes(); ++byte)
        {
            TEST_EXPECT(ctx, (static_cast<const unsigned char*>(held.data())[byte] == 42u));
        }
    });
    TEST_EXPECT(ctx, (fixture.first->preamble->failed() == wake_failed));
    if (wake_failed)
    {
        TEST_EXPECT(ctx, (fixture.first->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::wake_failed));
    }
    held = memory::CMemoryConstView{};
    fixture.b.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, (fixture.connections().empty() && (fixture.a.retired == 1u) && (fixture.b.retired == 1u)));
    TEST_EXPECT(ctx, ((fixture.first->endpoint->notification_pins == 0u) &&
        (fixture.second->endpoint->notification_pins == 0u)));
}

void common_partial_installation(tests::TTestContext& ctx, const bool bulk)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.b.hold = true;
    fixture.request(fixture.a, bulk ? connections::SBulkConfiguration{ 129u, module_ids::executive } :
        connections::SBulkConfiguration{}, 7);
    fixture.step(2u); // Both offers accepted; neither installation consumed yet.
    const connections::SToken token = fixture.a.offered;
    TEST_EXPECT(ctx, (token.value && (fixture.b.offered.value == token.value)));
    fixture.second->preamble->process(); // Only B acknowledges installation.
    TEST_EXPECT(ctx, ((fixture.a.available == 0u) && (fixture.b.available == 0u)));
    fixture.first->preamble->request_stop(modules::EExecutionStop::requested);
    fixture.step(8u); // A's late installation cannot override stop.
    TEST_EXPECT(ctx, ((fixture.a.available == 0u) && (fixture.b.available == 0u)));
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::cancelled)));
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::contains(fixture.connections(), token) &&
        (fixture.first->endpoint->notification_pins == 1u) && (fixture.a.destroyed == 0u)));
    fixture.b.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, (fixture.connections().empty() && (fixture.a.destroyed == 1u) &&
        (fixture.a.retired == 1u) && (fixture.b.retired == 1u)));
}

void common_activation_and_timeout(tests::TTestContext& ctx, const bool expire, const bool bulk)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.b.hold = true;
    fixture.request(fixture.a, bulk ? connections::SBulkConfiguration{ 129u, module_ids::executive } :
        connections::SBulkConfiguration{}, 7);
    fixture.step(2u);
    fixture.first->preamble->process();
    fixture.second->preamble->process();
    // Delay B's actual installed acknowledgement without converting it to
    // ordinary traffic or counting capture as dispatch.
    host::SLifecycleMessage installed;
    TEST_EXPECT(ctx, fixture.second->channel->read_host_control(installed));
    TEST_EXPECT(ctx, (installed.operation == host::ELifecycle::installed));
    const connections::SToken token = installed.notification.connection;
    (void)host::SModulePreambleTestAccess::drain(fixture.runtime);
    fixture.connections().progress();
    const modules::SDestination destination{ modules::SDestination::EKind::connection, {}, token };
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.query(destination).active);
        TEST_EXPECT(ctx, !communication.producer_data(token));
        TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::inactive));
    });
    TEST_EXPECT(ctx, ((fixture.a.result_count == 0u) && (fixture.a.available == 0u) && (fixture.b.available == 0u)));
    if (expire)
    {
        host::SDirectConnectionTestAccess::expire(fixture.connections(), token, fixture.first->host_execution.restriction);
        fixture.connections().progress();
        TEST_EXPECT(ctx, fixture.second->channel->preamble_control(installed));
        fixture.step(8u);
        TEST_EXPECT(ctx, ((fixture.a.available == 0u) && (fixture.b.available == 0u) &&
            (fixture.a.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::timed_out)));
        TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
        fixture.b.hold = false;
        fixture.step(8u);
        TEST_EXPECT(ctx, fixture.connections().empty());
        return;
    }
    TEST_EXPECT(ctx, fixture.second->channel->preamble_control(installed));
    (void)host::SModulePreambleTestAccess::drain(fixture.runtime);
    fixture.connections().progress();
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query(destination).active);
        if (bulk)
        {
            const memory::CMemoryView output = communication.producer_data(token);
            TEST_EXPECT(ctx, output);
            if (!output) return;
            std::memset(output.data(), 42, output.bytes());
            TEST_EXPECT(ctx, (communication.publish_bulk(token) == connections::EBulkPublish::published));
        }
        else
        {
            TEST_EXPECT(ctx, modules::accepted(communication.submit(destination,
                threading::make_pod_message(4, modules::RunTestsRequest{ module_ids::rendering }))));
        }
    });
    (void)host::SModulePreambleTestAccess::drain(fixture.runtime);
    fixture.connections().progress();
    TEST_EXPECT(ctx, ((fixture.a.available == 1u) && (fixture.b.available == 0u) &&
        (fixture.b.received == 0u) && (fixture.a.result_count == 0u)));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query(destination).active);
        TEST_EXPECT(ctx, (fixture.b.available == 1u));
        if (bulk)
        {
            TEST_EXPECT(ctx, communication.discard_and_acquire(token));
            const memory::CMemoryConstView input = communication.consumer_data(token);
            TEST_EXPECT(ctx, (input && (*static_cast<const unsigned char*>(input.data()) == 42u)));
        }
    });
    fixture.step(4u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::success)));
    TEST_EXPECT(ctx, (fixture.b.received == (bulk ? 0u : 1u)));
    fixture.b.hold = false;
    fixture.close(token);
}

void common_request_conflicts(tests::TTestContext& ctx)
{
    {
        SCommonBulkFixture fixture{ ctx };
        fixture.request(fixture.a, { 129u, module_ids::executive }, 7);
        fixture.request(fixture.b, { 129u, module_ids::executive }, 9, 2u);
        fixture.step(12u);
        TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::success) &&
            (fixture.b.results[0].connection_status == connections::EStatus::version_mismatch)));
        fixture.close(fixture.a.results[0].connection);
    }
    {
        SCommonBulkFixture fixture{ ctx };
        fixture.second->direct_connections = host::EDirectConnectionState::unregistered;
        fixture.request(fixture.a, {}, 7);
        fixture.step(4u);
        TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) &&
            (fixture.a.results[0].connection_status == connections::EStatus::unavailable) && fixture.connections().empty()));
        fixture.second->direct_connections = host::EDirectConnectionState::available;
        fixture.a.result_count = 0u;
        fixture.request(fixture.a, {}, 7);
        fixture.request(fixture.a, {}, 99);
        fixture.step(12u);
        TEST_EXPECT(ctx, (fixture.a.result_count == 2u));
        connections::SToken token;
        unsigned successes = 0u, refused = 0u;
        for (unsigned index = 0u; index < fixture.a.result_count; ++index)
        {
            const modules::SExecutionNotification& result = fixture.a.results[index];
            if (result.correlation == 7)
            {
                TEST_EXPECT(ctx, (result.connection_status == connections::EStatus::success));
                token = result.connection;
                ++successes;
            }
            else
            {
                TEST_EXPECT(ctx, ((result.correlation == 99) &&
                    (result.connection_status == connections::EStatus::busy) && !result.connection.value));
                ++refused;
            }
        }
        TEST_EXPECT(ctx, ((successes == 1u) && (refused == 1u)));
        fixture.close(token);
    }
}

void common_delayed_request(tests::TTestContext& ctx, const bool after_retirement, const bool successor)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.request(fixture.a, {}, 7);
    fixture.request(fixture.b, {}, 9);
    host::SLifecycleMessage held;
    TEST_EXPECT(ctx, fixture.second->channel->read_host_control(held));
    TEST_EXPECT(ctx, (held.operation == host::ELifecycle::connect));
    fixture.step(12u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 0u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::success)));
    const connections::SToken old = fixture.a.results[0].connection;
    fixture.b.hold = true;
    fixture.close(old);
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), old));
    if (!after_retirement)
    {
        TEST_EXPECT(ctx, fixture.second->channel->preamble_control(held));
        fixture.step(4u);
        TEST_EXPECT(ctx, ((fixture.b.result_count == 1u) &&
            (fixture.b.results[0].connection_status == connections::EStatus::closing) &&
            !fixture.b.results[0].connection.value));
    }
    fixture.b.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, fixture.connections().empty());
    if (after_retirement)
    {
        if (!successor) fixture.connections().close_admission();
        TEST_EXPECT(ctx, fixture.second->channel->preamble_control(held));
        fixture.step(12u);
        TEST_EXPECT(ctx, ((fixture.b.result_count == 1u) && (fixture.b.results[0].correlation == 9) &&
            (fixture.b.results[0].connection_status ==
                (successor ? connections::EStatus::success : connections::EStatus::closing))));
        if (successor)
        {
            const connections::SToken next = fixture.b.results[0].connection;
            TEST_EXPECT(ctx, (next.value && (next.value != old.value)));
            fixture.close(next);
        }
    }
    TEST_EXPECT(ctx, fixture.connections().empty());
}

void common_activation_pressure(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.request(fixture.a, {}, 7);
    fixture.step(2u);
    fixture.first->preamble->process();
    fixture.second->preamble->process();
    const connections::SToken token = fixture.a.offered;
    // Controlled padding occupies the private lane only. The test removes it
    // before invoking the module, so no synthetic stop is delivered.
    host::SLifecycleMessage padding;
    padding.operation = host::ELifecycle::stop;
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        TEST_EXPECT(ctx, fixture.second->channel->host_control(padding));
    }
    (void)host::SModulePreambleTestAccess::drain(fixture.runtime);
    fixture.connections().progress();
    fixture.first->preamble->process();
    TEST_EXPECT(ctx, ((fixture.a.available == 1u) && (fixture.b.available == 0u) &&
        (fixture.a.result_count == 0u) && (fixture.b.result_count == 0u)));
    host::SLifecycleMessage discarded;
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        TEST_EXPECT(ctx, (fixture.second->channel->read_preamble_control(discarded) &&
            (discarded.operation == host::ELifecycle::stop)));
    }
    fixture.request(fixture.b, {}, 9);
    TEST_EXPECT(ctx, ((fixture.b.available == 0u) && (fixture.b.result_count == 0u)));
    fixture.step(8u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 1u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::success) &&
        (fixture.b.results[0].connection_status == connections::EStatus::success) &&
        (fixture.b.results[0].connection.value == token.value) && (fixture.b.available == 1u)));
    fixture.request(fixture.b, {}, 10);
    fixture.step(4u);
    TEST_EXPECT(ctx, ((fixture.b.result_count == 2u) && (fixture.b.available == 1u) &&
        (fixture.b.results[1].connection_status == connections::EStatus::already_connected) &&
        (fixture.b.results[1].connection.value == token.value)));
    fixture.close(token);
}

void common_pod_exchange(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(false, true, {});
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, token },
            threading::make_pod_message(31, modules::RunTestsRequest{ module_ids::rendering }))));
    });
    fixture.step();
    modules::RunTestsRequest request;
    TEST_EXPECT(ctx, ((fixture.b.received == 1u) && fixture.b.last_message.copy_payload_to(request) &&
        (request.module == module_ids::rendering) && (fixture.b.last_message.query_async_slot() == 31) &&
        (fixture.b.last_source.address == fixture.a.address) &&
        (fixture.b.last_source.endpoint.value == fixture.first->identity.value) &&
        (fixture.b.last_source.connection.value == token.value)));
    fixture.invoke(fixture.b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::peer, fixture.a.address, token },
            threading::make_pod_message(31, modules::RunTestsResult{ module_ids::rendering, modules::ETestRunStatus::success }))));
    });
    fixture.step();
    modules::RunTestsResult result;
    TEST_EXPECT(ctx, ((fixture.a.received == 1u) && fixture.a.last_message.copy_payload_to(result) &&
        (result.status == modules::ETestRunStatus::success) && (fixture.a.last_message.query_async_slot() == 31) &&
        (fixture.a.last_source.address == fixture.b.address) &&
        (fixture.a.last_source.endpoint.value == fixture.second->identity.value)));
    fixture.close(token);
    const connections::SToken successor = fixture.establish(false, false, {});
    TEST_EXPECT(ctx, (successor.value != token.value));
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        modules::SExecutionIntent close;
        close.kind = modules::SExecutionIntent::EKind::close_connection;
        close.connection = token;
        TEST_EXPECT(ctx, (communication.submit(close) == modules::ESubmission::no_pipe));
        close.kind = modules::SExecutionIntent::EKind::connection_quiescent;
        TEST_EXPECT(ctx, (communication.submit(close) == modules::ESubmission::invalid));
        TEST_EXPECT(ctx, (communication.submit({ modules::SDestination::EKind::peer, fixture.b.address, token },
            threading::make_pod_message(32, modules::RunTestsRequest{ module_ids::rendering })) == modules::ESubmission::no_pipe));
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, successor }).active);
    });
    fixture.close(successor);
}

void common_pod_pressure(tests::TTestContext& ctx, const bool cancel)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext memory{ allocator };
    {
        tests::TMemoryContextScope scope{ &memory };
        SCommonBulkFixture fixture{ ctx };
        const connections::SToken token = fixture.establish(false, false, {});
        const modules::SDestination destination{ modules::SDestination::EKind::connection, {}, token };
        fixture.b.pause_input = fixture.b.ordered = true;
        fixture.second->preamble->process();
        const std::size_t attempts = allocation.attempts;
        allocation.fail_on = attempts;
        fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
        {
            for (std::uint32_t index = 0u; index < host::k_module_pod_capacity; ++index)
            {
                TEST_EXPECT(ctx, (communication.submit(destination, threading::make_pod_message(static_cast<std::int32_t>(index),
                    modules::RunTestsRequest{ module_ids::rendering })) == modules::ESubmission::accepted));
            }
            TEST_EXPECT(ctx, (communication.submit(destination, threading::make_pod_message(
                static_cast<std::int32_t>(host::k_module_pod_capacity), modules::RunTestsRequest{ module_ids::rendering })) ==
                modules::ESubmission::busy));
        });
        TEST_EXPECT(ctx, ((allocation.attempts == attempts) && !allocation.failed && !fixture.first->preamble->failed()));
        allocation.fail_on = SIZE_MAX;
        TEST_EXPECT(ctx, (!fixture.first->preamble->work_pending() && !fixture.second->preamble->work_pending() &&
            (fixture.b.received == 0u)));
        if (cancel)
        {
            fixture.close(token);
            // Unread cancellation is bounded per turn just like dispatch.
            // Complete the remaining prefix without reopening direct input.
            fixture.step(32u);
            TEST_EXPECT(ctx, ((fixture.b.received == 0u) && fixture.connections().empty()));
        }
        else
        {
            fixture.b.pause_input = false;
            fixture.second->preamble->process();
            TEST_EXPECT(ctx, ((fixture.b.received > 0u) && fixture.first->preamble->work_pending()));
            fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
            {
                TEST_EXPECT(ctx, (communication.submit(destination, threading::make_pod_message(
                    static_cast<std::int32_t>(host::k_module_pod_capacity), modules::RunTestsRequest{ module_ids::rendering })) ==
                    modules::ESubmission::accepted));
            });
            fixture.step(32u);
            TEST_EXPECT(ctx, (fixture.b.valid && (fixture.b.received == (host::k_module_pod_capacity + 1u))));
            fixture.close(token);
        }
    }
    TEST_EXPECT(ctx, memory.is_attribution_empty());
}

void common_pod_wake_failure(tests::TTestContext& ctx, const bool after_read)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(false, false, {});
    host::SDirectEndpointDescriptor* const descriptor = host::SDirectConnectionTestAccess::private_descriptor(
        fixture.connections(), token, after_read ? 1u : 0u);
    TEST_EXPECT(ctx, descriptor);
    if (!descriptor) return;
    descriptor->peer_notification = {};
    fixture.invoke(fixture.a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, (communication.submit({ modules::SDestination::EKind::connection, {}, token },
            threading::make_pod_message(4, modules::RunTestsRequest{ module_ids::rendering })) ==
            (after_read ? modules::ESubmission::accepted : modules::ESubmission::published_wake_failed)));
    });
    fixture.second->preamble->process(); // Accepted data may dispatch before Host closure.
    TEST_EXPECT(ctx, ((fixture.b.received == 1u) && (fixture.b.last_message.query_async_slot() == 4)));
    const host::SClientRecord& reporter = after_read ? *fixture.second : *fixture.first;
    TEST_EXPECT(ctx, (reporter.preamble->failed() &&
        (reporter.endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::wake_failed)));
    fixture.step(12u);
    TEST_EXPECT(ctx, ((fixture.b.received == 1u) && fixture.connections().empty()));
}

void common_retirement_admission(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    fixture.connections().close_client(*fixture.second);
    fixture.request(fixture.a, {}, 7);
    fixture.request(fixture.b, {}, 9);
    fixture.step(4u);
    TEST_EXPECT(ctx, ((fixture.a.result_count == 1u) && (fixture.b.result_count == 1u) &&
        (fixture.a.results[0].connection_status == connections::EStatus::unavailable) &&
        (fixture.b.results[0].connection_status == connections::EStatus::closing) && fixture.connections().empty()));
    // Registration belongs to an exact endpoint, so an independently controlled
    // replacement at the same logical address can participate after old closure.
    tests::SCommonEndpointFixture replacement{ module_ids::rendering, fixture.second->identity.value + 1000u };
    fixture.connections().register_client(replacement, 0);
    replacement.flush_controls();
    host::SLifecycleMessage registered;
    TEST_EXPECT(ctx, replacement.channel->read_preamble_control(registered));
    TEST_EXPECT(ctx, ((registered.operation == host::ELifecycle::participated) &&
        (registered.notification.connection_status == connections::EStatus::success) &&
        (replacement.direct_connections == host::EDirectConnectionState::available) &&
        (fixture.second->direct_connections == host::EDirectConnectionState::closing)));
}

struct SCommonNativeJoin
{
    platform::system::CPerfCountConversion conversion;
    threading::CEndpointResources* resources{ nullptr };
    threading::SNotificationEndpoint notification;
    std::atomic<bool> entered{ false }, terminal{ false }, published{ false }, signal{ false }, signalled{ false }, finish{ false };

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        SCommonNativeJoin& self = *static_cast<SCommonNativeJoin*>(context->creation_context);
        *runtime = nullptr; // Deliberately fail creation without any module-owned state.
        self.resources->control_state.mark_running();
        self.entered.store(true, std::memory_order_release);
        if (!tests::wait_until(self.conversion, [&]() noexcept { return self.terminal.load(std::memory_order_acquire); }))
        {
            std::fputs("Native join fixture did not receive terminal command.\n", stderr);
            std::_Exit(1);
        }
        self.resources->control_state.mark_exited();
        self.published.store(true, std::memory_order_release);
        const bool finished = tests::wait_until(self.conversion, [&]() noexcept
        {
            if (self.signal.load(std::memory_order_acquire) && !self.signalled.load(std::memory_order_relaxed))
            {
                self.signalled.store(self.notification.signal(), std::memory_order_release);
            }
            return self.finish.load(std::memory_order_acquire);
        });
        if (!finished)
        {
            std::fputs("Native join fixture retained live callback at timeout.\n", stderr);
            std::_Exit(1);
        }
        return modules::EExecutionStatus::failed;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const,
        const modules::SInvocation* const, modules::SExecutionProgress* const) noexcept
    {
        std::fputs("Native join fixture invoked a handler after failed create.\n", stderr);
        std::_Exit(1);
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const) noexcept
    {
        std::fputs("Native join fixture destroyed absent module state.\n", stderr);
        std::_Exit(1);
    }
};

void common_native_join_retention(tests::TTestContext& ctx, const bool bulk)
{
    SCommonNativeJoin state;
    TEST_EXPECT(ctx, state.conversion.init());
    platform::threading::COwnerWait owner;
    TEST_EXPECT(ctx, (owner.initialise().code == platform::threading::EWaitError::none));
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &SCommonNativeJoin::create;
    execution.handle = &SCommonNativeJoin::handle;
    execution.destroy = &SCommonNativeJoin::destroy;
    threading::CThreadPackage package{ { thread_ids::executive, module_ids::executive }, state.conversion };
    host::SClientRecord first{ module_ids::executive, 731u, &package, { 1731u } };
    tests::SCommonEndpointFixture second{ module_ids::rendering, 732u };
    host::CHostRestrictions restrictions;
    host::CDirectConnections connections;
    first.owner_wake = owner.endpoint();
    first.host_execution.instance = first.instance;
    first.host_execution.restriction = restrictions.reader();
    first.endpoint->host_execution = &first.host_execution;
    state.resources = first.endpoint;
    TEST_EXPECT(ctx, first.initialise_execution(binding, execution, &state));
    TEST_EXPECT(ctx, package.startup());
    const auto require = [&](const bool success) noexcept
    {
        TEST_EXPECT(ctx, success);
        if (!success)
        {
            std::fputs("Native join fixture cannot prove safe teardown.\n", stderr);
            std::_Exit(1);
        }
    };
    require(tests::wait_until(state.conversion, [&]() noexcept { return state.entered.load(std::memory_order_acquire); }));
    first.phase = second.phase = host::EClientPhase::ready;
    first.direct_connections = second.direct_connections = host::EDirectConnectionState::available;
    // Controlled Host participation/consent exposes descriptors while actual
    // native create is held. The native side never consumes its private lane.
    host::SDirectRequest request;
    request.first = system_ids::ops::make_system_id(first.module, first.endpoint->thread_id);
    request.second = system_ids::ops::make_system_id(second.module, second.endpoint->thread_id);
    if (bulk)
    {
        request.bulk_peer = second.module;
        request.bulk = { 129u, first.module };
    }
    connections.request(first, &first, &second, request, 7, restrictions.reader());
    connections.progress();
    host::SLifecycleMessage message;
    require(second.channel->read_preamble_control(message));
    TEST_EXPECT(ctx, (message.operation == host::ELifecycle::offer));
    const connections::SToken token = message.notification.connection;
    connections.decision(first, token, connections::EStatus::success);
    connections.decision(second, token, connections::EStatus::success);
    connections.progress();
    require(second.channel->read_preamble_control(message));
    TEST_EXPECT(ctx, ((message.operation == host::ELifecycle::install) && message.descriptor));
    state.notification = host::SDirectConnectionTestAccess::notification(connections, token);
    threading::SDirectConnectionTestAccess::fail_join(package, true);
    state.terminal.store(true, std::memory_order_release);
    require(tests::wait_until(state.conversion, [&]() noexcept { return state.published.load(std::memory_order_acquire); }));
    connections.close_client(first);
    first.terminal_before_drain = first.incoming_drained = true; // No ordinary ingress was submitted.
    connections.progress();
    require(second.channel->read_preamble_control(message));
    TEST_EXPECT(ctx, (message.operation == host::ELifecycle::closing));
    connections.quiescent(second, token);
    connections.progress();
    TEST_EXPECT(ctx, (!connections.empty() && !package.execution_ended() &&
        (first.endpoint->notification_pins == 1u) && (second.endpoint->notification_pins == 1u)));
    TEST_EXPECT(ctx, (!package.join_terminated_execution() && !package.shutdown()));
    if (bulk)
    {
        TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::bulk_capacity(connections, token) == 129u));
    }
    state.signal.store(true, std::memory_order_release);
    require(tests::wait_until(state.conversion, [&]() noexcept { return state.signalled.load(std::memory_order_acquire); }));
    state.finish.store(true, std::memory_order_release);
    require(tests::wait_until(state.conversion, [&]() noexcept { return package.execution_ended(); }));
    connections.progress();
    TEST_EXPECT(ctx, (!package.join_terminated_execution() && !connections.empty() &&
        (first.endpoint->notification_pins == 1u) && (second.endpoint->notification_pins == 1u)));
    threading::SDirectConnectionTestAccess::fail_join(package, false);
    require(package.join_terminated_execution());
    TEST_EXPECT(ctx, (first.endpoint->notification_pins == 1u));
    connections.progress();
    TEST_EXPECT(ctx, (connections.empty() && (first.endpoint->notification_pins == 0u) &&
        (second.endpoint->notification_pins == 0u)));
    require(second.channel->read_preamble_control(message));
    TEST_EXPECT(ctx, (message.operation == host::ELifecycle::retired));
    TEST_EXPECT(ctx, (first.preamble->failed() && first.preamble->destroyed()));
    require(package.shutdown());
}

struct SCommonMultiFixture
{
    struct SPeer
    {
        CBulkPeerExecution state;
        host::SClientRecord* client{ nullptr };
        bool deliver{ true }, activated{ false };
        TInstance<host::SClientRecord> retained;
    };
    tests::TTestContext& test;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    SPeer peers[12];
    unsigned count{ 0u };

    explicit SCommonMultiFixture(tests::TTestContext& ctx) noexcept : test{ ctx }
    {
        modules::SBoundModuleTestAccess::install(binding);
    }
    SPeer& add(const module_ids::id_type module, const thread_ids::id_type thread, const bool activate = true) noexcept
    {
        if (count == std::size(peers)) std::_Exit(1);
        SPeer& peer = peers[count++];
        modules::SExecutionV1 execution;
        execution.create = &CBulkPeerExecution::create;
        execution.handle = &CBulkPeerExecution::dispatch;
        execution.destroy = &CBulkPeerExecution::destroy;
        peer.client = host::SModulePreambleTestAccess::controlled_endpoint(runtime, module, thread, binding, execution, &peer.state);
        TEST_EXPECT(test, peer.client);
        if (!peer.client) std::_Exit(1);
        peer.activated = peer.deliver = activate;
        if (activate) TEST_EXPECT(test, peer.client->preamble->activate());
        return peer;
    }
    host::CDirectConnections& connections() noexcept { return host::SModulePreambleTestAccess::connections(runtime); }
    void step(const unsigned rounds = 1u) noexcept
    {
        for (unsigned round = 0u; round < rounds; ++round)
        {
            for (unsigned index = 0u; index < count; ++index)
            {
                if (peers[index].deliver && peers[index].activated) peers[index].client->preamble->process();
            }
            (void)host::SModulePreambleTestAccess::drain(runtime);
            connections().progress();
        }
    }
    template<typename T> void invoke(SPeer& peer, T&& operation) noexcept
    {
        peer.state.action_context = &operation;
        peer.state.action = [](CBulkPeerExecution& state, void* const context) noexcept
        {
            (*static_cast<std::remove_reference_t<T>*>(context))(state.communication);
        };
        peer.client->preamble->process();
        TEST_EXPECT(test, !peer.state.action);
        peer.state.action_context = nullptr;
    }
    void request(SPeer& requester, const SPeer& first, const SPeer& second, const std::int32_t slot) noexcept
    {
        invoke(requester, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::connect;
            intent.first = first.state.address;
            intent.second = second.state.address;
            intent.correlation = slot;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
    }
    connections::SToken establish(SPeer& requester, SPeer& first, SPeer& second, const std::int32_t slot = 7) noexcept
    {
        step(4u);
        requester.state.result_count = 0u;
        request(requester, first, second, slot);
        step(12u);
        const modules::SExecutionNotification& result = requester.state.results[0];
        TEST_EXPECT(test, ((requester.state.result_count == 1u) && (result.correlation == slot) &&
            (result.connection_status == connections::EStatus::success)));
        return result.connection;
    }
    void consent(SPeer& peer, const connections::SToken token, const connections::EStatus decision = connections::EStatus::success) noexcept
    {
        invoke(peer, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::consent;
            intent.connection = token;
            intent.decision = decision;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
    }
    void close(SPeer& peer, const connections::SToken token) noexcept
    {
        invoke(peer, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent intent;
            intent.kind = modules::SExecutionIntent::EKind::close_connection;
            intent.connection = token;
            TEST_EXPECT(test, modules::accepted(communication.submit(intent)));
        });
        step(8u);
        TEST_EXPECT(test, !host::SDirectConnectionTestAccess::contains(connections(), token));
    }
    ~SCommonMultiFixture() noexcept
    {
        for (unsigned index = 0u; index < count; ++index)
        {
            peers[index].deliver = true;
            peers[index].state.hold = false;
            peers[index].client->preamble->request_stop(modules::EExecutionStop::requested);
        }
        step(32u);
        TEST_EXPECT(test, connections().empty());
        for (unsigned index = 0u; index < count; ++index)
        {
            const SPeer& peer = peers[index];
            TEST_EXPECT(test, (peer.state.valid && (peer.state.destroyed == (peer.activated ? 1u : 0u)) &&
                (peer.state.created_on == peer.state.destroyed_on) && (peer.client->endpoint->notification_pins == 0u)));
        }
        host::SModulePreambleTestAccess::clear(runtime);
    }
};

void common_endpoint_pairings(tests::TTestContext& ctx)
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
        SCommonMultiFixture fixture{ ctx };
        SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, first_pump ? thread_ids::host : thread_ids::executive);
        SCommonMultiFixture::SPeer& b = fixture.add(same_module ? module_ids::executive : module_ids::rendering,
            second_pump ? thread_ids::host : thread_ids::rendering);
        SCommonMultiFixture::SPeer& requester = fixture.add(module_ids::audio, thread_ids::host);
        const connections::SToken token = fixture.establish(requester, a, b);
        TEST_EXPECT(ctx, ((a.state.offers == 1u) && (b.state.offers == 1u) && !a.state.result_count &&
            !b.state.result_count && !requester.state.offers));
        const threading::CErasedPodMsg message = threading::make_pod_message(23, modules::RunTestsRequest{ module_ids::audio });
        fixture.invoke(a, [&](const modules::CCommunication& communication) noexcept
        {
            TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::peer, b.state.address, token }, message)));
        });
        fixture.step();
        TEST_EXPECT(ctx, ((b.state.received == 1u) && (b.state.last_source.address == a.state.address) &&
            (b.state.last_source.endpoint.value == a.client->identity.value) &&
            (b.state.last_source.connection.value == token.value) && (b.state.last_message.query_async_slot() == 23)));
        fixture.invoke(b, [&](const modules::CCommunication& communication) noexcept
        {
            TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::peer, a.state.address }, message)));
        });
        fixture.step();
        TEST_EXPECT(ctx, ((a.state.received == 1u) && (a.state.last_source.endpoint.value == b.client->identity.value)));
        fixture.invoke(requester, [&](const modules::CCommunication& communication) noexcept
        {
            modules::SExecutionIntent close;
            close.kind = modules::SExecutionIntent::EKind::close_connection;
            close.connection = token;
            TEST_EXPECT(ctx, (communication.submit(close) == modules::ESubmission::no_pipe));
        });
        // Also exercise Host authentication of a forged private transition.
        host::SLifecycleMessage close;
        close.operation = host::ELifecycle::close;
        close.notification.connection = token;
        TEST_EXPECT(ctx, requester.client->channel->preamble_control(close));
        fixture.step();
        TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
        fixture.close(a, token);
        const connections::SToken successor = fixture.establish(requester, a, b);
        TEST_EXPECT(ctx, (successor.value != token.value));
        fixture.invoke(a, [&](const modules::CCommunication& communication) noexcept
        {
            TEST_EXPECT(ctx, (communication.submit({ modules::SDestination::EKind::peer, b.state.address, token }, message) ==
                modules::ESubmission::no_pipe));
        });
        fixture.close(b, successor);
    }
}

void common_explicit_consent(tests::TTestContext& ctx, const bool reject)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::executive, thread_ids::executive);
    SCommonMultiFixture::SPeer& requester = fixture.add(module_ids::rendering, thread_ids::host);
    fixture.step(4u);
    a.state.consent = b.state.consent = false;
    fixture.request(a, a, b, 1);
    fixture.request(b, b, a, 2);
    fixture.request(requester, a, b, 3);
    fixture.request(requester, b, a, 4);
    fixture.step(3u);
    const connections::SToken token = a.state.offered;
    TEST_EXPECT(ctx, ((a.state.offers == 1u) && (b.state.offers == 1u) && (token.value == b.state.offered.value)));
    TEST_EXPECT(ctx, ((requester.state.result_count == 1u) && (requester.state.results[0].correlation == 4) &&
        (requester.state.results[0].connection_status == connections::EStatus::busy)));
    fixture.consent(a, token);
    fixture.step(2u);
    TEST_EXPECT(ctx, ((a.state.available == 0u) && (b.state.available == 0u)));
    fixture.consent(b, token, reject ? connections::EStatus::rejected : connections::EStatus::success);
    fixture.step(12u);
    const connections::EStatus expected = reject ? connections::EStatus::rejected : connections::EStatus::success;
    TEST_EXPECT(ctx, ((a.state.result_count == 1u) && (b.state.result_count == 1u) && (requester.state.result_count == 2u) &&
        (a.state.results[0].connection_status == expected) && (b.state.results[0].connection_status == expected) &&
        (requester.state.results[1].connection_status == expected) && (requester.state.results[1].correlation == 3)));
    TEST_EXPECT(ctx, (!a.state.result_before_available && !b.state.result_before_available));
    if (reject) TEST_EXPECT(ctx, fixture.connections().empty());
    else fixture.close(a, token);
}

void common_requester_limit(tests::TTestContext& ctx, const bool departing)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    const thread_ids::id_type threads[]{ thread_ids::host, thread_ids::executive, thread_ids::rendering, thread_ids::audio,
        thread_ids::simulation, thread_ids::debug_service, thread_ids::bg_file_io, thread_ids::bg_conditioning, thread_ids::batch_runner_00 };
    for (unsigned index = 0u; index < std::size(threads); ++index)
    {
        (void)fixture.add(module_ids::audio, threads[index], !departing || (index != 0u));
    }
    fixture.step(4u);
    a.state.consent = b.state.consent = false;
    for (unsigned index = 2u; index < fixture.count; ++index)
    {
        if (departing && (index == 2u))
        {
            // A controlled requester with no activated module state can become
            // terminal independently. Live preambles instead await their result.
            host::SLifecycleMessage request;
            request.operation = host::ELifecycle::connect;
            request.first_address = a.state.address;
            request.second_address = b.state.address;
            request.correlation = static_cast<std::int32_t>(index);
            TEST_EXPECT(ctx, fixture.peers[index].client->channel->preamble_control(request));
        }
        else fixture.request(fixture.peers[index], a, b, static_cast<std::int32_t>(index));
    }
    fixture.step(3u);
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::requests(fixture.connections()) == 8u));
    // Host traverses stable unordered endpoint slots, not the requester's test
    // insertion order. Exactly one of the nine requests is refused.
    unsigned refused = 0u;
    unsigned refused_index = 0u;
    for (unsigned index = 2u; index < fixture.count; ++index)
    {
        const CBulkPeerExecution& requester = fixture.peers[index].state;
        if (requester.result_count)
        {
            TEST_EXPECT(ctx, ((requester.result_count == 1u) &&
                (requester.results[0].connection_status == connections::EStatus::busy) &&
                (requester.results[0].correlation == static_cast<std::int32_t>(index))));
            ++refused;
            refused_index = index;
        }
    }
    TEST_EXPECT(ctx, (refused == 1u));
    if (departing)
    {
        host::SClientRecord& requester = *fixture.peers[2].client;
        fixture.connections().close_client(requester);
        requester.phase = host::EClientPhase::stopped;
        requester.terminal_before_drain = requester.incoming_drained = true;
        fixture.connections().progress();
        TEST_EXPECT(ctx, (fixture.connections().client_idle(requester) &&
            (host::SDirectConnectionTestAccess::requests(fixture.connections()) == 7u)));
    }
    const connections::SToken token = a.state.offered;
    fixture.consent(a, token);
    fixture.consent(b, token);
    fixture.step(12u);
    TEST_EXPECT(ctx, ((a.state.offers == 1u) && (b.state.offers == 1u) &&
        (a.state.available == 1u) && (b.state.available == 1u)));
    for (unsigned index = departing ? 3u : 2u; index < fixture.count; ++index)
    {
        if (index == refused_index) continue;
        const CBulkPeerExecution& requester = fixture.peers[index].state;
        TEST_EXPECT(ctx, ((requester.result_count == 1u) && (requester.results[0].connection_status == connections::EStatus::success) &&
            (requester.results[0].connection.value == token.value) &&
            (requester.results[0].correlation == static_cast<std::int32_t>(index))));
    }
    fixture.close(a, token);
}

void common_connection_limits(tests::TTestContext& ctx, const bool overall)
{
    SCommonMultiFixture fixture{ ctx };
    const thread_ids::id_type threads[]{ thread_ids::host, thread_ids::executive, thread_ids::rendering, thread_ids::audio,
        thread_ids::simulation, thread_ids::debug_service, thread_ids::bg_file_io, thread_ids::bg_conditioning,
        thread_ids::batch_runner_00, thread_ids::batch_runner_01 };
    for (const thread_ids::id_type thread : threads) (void)fixture.add(module_ids::audio, thread);
    if (overall)
    {
        unsigned established = 0u;
        for (unsigned first = 0u; (first < 9u) && (established < 32u); ++first)
        {
            for (unsigned second = first + 1u; (second < 9u) && (established < 32u); ++second)
            {
                (void)fixture.establish(fixture.peers[first], fixture.peers[first], fixture.peers[second]);
                ++established;
            }
        }
        fixture.peers[5].state.result_count = 0u;
        fixture.request(fixture.peers[5], fixture.peers[5], fixture.peers[8], 99);
    }
    else
    {
        for (unsigned second = 1u; second <= 8u; ++second)
        {
            (void)fixture.establish(fixture.peers[0], fixture.peers[0], fixture.peers[second]);
        }
        fixture.peers[0].state.result_count = 0u;
        fixture.request(fixture.peers[0], fixture.peers[0], fixture.peers[9], 99);
    }
    fixture.step(4u);
    const CBulkPeerExecution& requester = fixture.peers[overall ? 5u : 0u].state;
    TEST_EXPECT(ctx, ((requester.result_count == 1u) && (requester.results[0].correlation == 99) &&
        (requester.results[0].connection_status == connections::EStatus::busy)));
    fixture.connections().close_all();
    fixture.step(16u);
    TEST_EXPECT(ctx, fixture.connections().empty());
}

void common_result_after_retirement(tests::TTestContext& ctx)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    SCommonMultiFixture::SPeer& requester = fixture.add(module_ids::audio, thread_ids::host);
    fixture.step(4u);
    fixture.request(requester, a, b, 23);
    requester.deliver = false;
    host::SLifecycleMessage padding;
    padding.operation = host::ELifecycle::stop;
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        TEST_EXPECT(ctx, requester.client->channel->host_control(padding));
    }
    fixture.step(12u);
    const connections::SToken token = a.state.offered;
    TEST_EXPECT(ctx, ((a.state.available == 1u) && (b.state.available == 1u) &&
        (requester.state.result_count == 0u) && (host::SDirectConnectionTestAccess::requests(fixture.connections()) == 1u)));
    fixture.close(a, token);
    TEST_EXPECT(ctx, (!fixture.connections().empty() && fixture.connections().client_idle(*a.client) &&
        fixture.connections().client_idle(*b.client) && !fixture.connections().client_idle(*requester.client)));
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        host::SLifecycleMessage discarded;
        TEST_EXPECT(ctx, (requester.client->channel->read_preamble_control(discarded) &&
            (discarded.operation == host::ELifecycle::stop)));
    }
    requester.deliver = true;
    fixture.step(4u);
    const modules::SExecutionNotification& result = requester.state.results[0];
    TEST_EXPECT(ctx, ((requester.state.result_count == 1u) && (result.correlation == 23) &&
        (result.connection_status == connections::EStatus::success) && (result.connection.value == token.value) &&
        (result.first.value == a.client->identity.value) && (result.second.value == b.client->identity.value) &&
        fixture.connections().empty()));
}

void common_reused_correlation(tests::TTestContext& ctx)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& peer = fixture.add(module_ids::audio, thread_ids::host);
    fixture.step(4u);
    for (unsigned index = 0u; index < 48u; ++index)
    {
        peer.state.result_count = 0u;
        fixture.request(peer, peer, peer, 7);
        fixture.step(4u);
        TEST_EXPECT(ctx, ((peer.state.result_count == 1u) && (peer.state.results[0].correlation == 7) &&
            (peer.state.results[0].connection_status == connections::EStatus::invalid_request) && fixture.connections().empty()));
    }
}

void common_authenticated_selection(tests::TTestContext& ctx, const bool pump)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, pump ? thread_ids::host : thread_ids::executive);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    SCommonMultiFixture::SPeer& c = fixture.add(module_ids::audio, thread_ids::host);
    const connections::SToken ab = fixture.establish(a, a, b);
    const connections::SToken ac = fixture.establish(a, a, c, 8);
    fixture.invoke(b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, ab },
            threading::make_pod_message(99, services::OpenServiceResult{}))));
    });
    fixture.step();
    TEST_EXPECT(ctx, (!a.state.last_source.is_host() && (a.state.last_source.connection.value == ab.value) &&
        a.state.last_message.is_payload_a<services::OpenServiceResult>()));
    a.state.allow_host = a.state.track_sources = a.state.pause_input = true;
    a.state.source_ids[0] = b.client->identity;
    a.state.source_ids[1] = c.client->identity;
    a.client->preamble->process();
    for (SCommonMultiFixture::SPeer* const peer : { &b, &c })
    {
        const connections::SToken token = (peer == &b) ? ab : ac;
        fixture.invoke(*peer, [&](const modules::CCommunication& communication) noexcept
        {
            for (std::int32_t index = 0; index < 6; ++index)
            {
                TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, token },
                    threading::make_pod_message(index, modules::RunTestsRequest{}))));
            }
        });
    }
    for (std::int32_t index = 0; index < 6; ++index)
    {
        TEST_EXPECT(ctx, a.client->post(threading::make_pod_message(index, modules::RunTestsRequest{})));
    }
    a.client->preamble->process();
    TEST_EXPECT(ctx, (a.state.valid && (a.state.source_counts[0] == 6) &&
        (a.state.source_counts[1] == 0) && (a.state.source_counts[2] == 0) && !a.client->preamble->work_pending()));
    for (std::int32_t index = 6; index < 12; ++index)
    {
        TEST_EXPECT(ctx, a.client->post(threading::make_pod_message(index, modules::RunTestsRequest{})));
    }
    a.state.pause_input = false;
    fixture.step(4u);
    // Pressure selection replaces the historical round-robin order. Every
    // source still preserves its own FIFO and receives a dispatch opportunity.
    TEST_EXPECT(ctx, (a.state.valid && (a.state.source_counts[0] == 12) &&
        (a.state.source_counts[1] == 6) && (a.state.source_counts[2] == 6)));
    a.state.track_sources = false;
    a.state.pause_input = a.state.hold = true;
    a.client->preamble->process();
    fixture.invoke(b, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::connection, {}, ab },
            threading::make_pod_message(77, modules::RunTestsRequest{}))));
        modules::SExecutionIntent close;
        close.kind = modules::SExecutionIntent::EKind::close_connection;
        close.connection = ab;
        TEST_EXPECT(ctx, modules::accepted(communication.submit(close)));
    });
    fixture.step(4u);
    TEST_EXPECT(ctx, (a.state.closing.value == ab.value));
    fixture.invoke(a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, !communication.query({ modules::SDestination::EKind::connection, {}, ab }).active);
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, ac }).active);
    });
    a.state.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, !host::SDirectConnectionTestAccess::contains(fixture.connections(), ab));
    TEST_EXPECT(ctx, (a.state.received == 25u)); // Forged Host-shaped data + 24 ordinary records.
    fixture.close(c, ac);
}

struct SPrivateAdmittedOperation
{
    const host::SDirectEndpointDescriptor* endpoint{ nullptr };
    platform::system::CPerfCountConversion conversion;
    std::atomic<bool> entered{ false }, finish{ false }, posted{ false }, signalled{ false };
    static std::uint32_t MV_STD_ABI_CALL run(void* const context) noexcept
    {
        SPrivateAdmittedOperation& self = *static_cast<SPrivateAdmittedOperation*>(context);
        const host::CDirectOperation operation{ self.endpoint->admission };
        if (!operation) return 1u;
        self.posted.store(self.endpoint->outgoing->post(threading::make_pod_message(91, modules::RunTestsRequest{})) ==
            host::EModulePipePost::published, std::memory_order_release);
        self.entered.store(true, std::memory_order_release);
        if (!tests::wait_until(self.conversion, [&]() noexcept { return self.finish.load(std::memory_order_acquire); }))
        {
            std::fputs("Admitted signal fixture retained native operation at timeout.\n", stderr);
            std::_Exit(1);
        }
        self.signalled.store(self.endpoint->peer_notification.signal(), std::memory_order_release);
        return 0u;
    }
};

void common_close_during_signal(tests::TTestContext& ctx, const bool pump)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::executive);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, pump ? thread_ids::host : thread_ids::rendering);
    const connections::SToken token = fixture.establish(a, a, b);
    a.state.hold = true;
    SPrivateAdmittedOperation operation;
    TEST_EXPECT(ctx, operation.conversion.init());
    operation.endpoint = host::SDirectConnectionTestAccess::private_descriptor(fixture.connections(), token, 0u);
    TEST_EXPECT(ctx, operation.endpoint);
    if (!operation.endpoint) return;
    // Only this controlled native operation produces on A's pipe until join.
    // It exercises Host admission lifetime, not foreign-thread facade access.
    platform::threading::CThread worker;
    const bool created = worker.create(&SPrivateAdmittedOperation::run, &operation);
    TEST_EXPECT(ctx, created);
    if (!created) return;
    const bool entered = tests::wait_until(operation.conversion, [&]() noexcept { return operation.entered.load(std::memory_order_acquire); });
    TEST_EXPECT(ctx, entered);
    if (!entered) std::_Exit(1);
    fixture.connections().close(*b.client, token, connections::EStatus::cancelled);
    fixture.step(3u);
    TEST_EXPECT(ctx, (!operation.endpoint->admission->available() && !operation.endpoint->admission->drained()));
    {
        const host::CDirectOperation refused{ operation.endpoint->admission };
        TEST_EXPECT(ctx, !refused);
    }
    TEST_EXPECT(ctx, (host::SDirectConnectionTestAccess::contains(fixture.connections(), token) &&
        (a.client->endpoint->notification_pins == 1u) && (b.client->endpoint->notification_pins == 1u)));
    operation.finish.store(true, std::memory_order_release);
    const bool joined = worker.join_and_close();
    TEST_EXPECT(ctx, joined);
    if (!joined) std::_Exit(1);
    TEST_EXPECT(ctx, (operation.posted.load(std::memory_order_acquire) && operation.signalled.load(std::memory_order_acquire) &&
        operation.endpoint->admission->drained()));
    fixture.connections().progress();
    TEST_EXPECT(ctx, host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
    a.state.hold = false;
    fixture.step(8u);
    TEST_EXPECT(ctx, !host::SDirectConnectionTestAccess::contains(fixture.connections(), token));
}

void common_deferred_quiescence(tests::TTestContext& ctx)
{
    SCommonBulkFixture fixture{ ctx };
    const connections::SToken token = fixture.establish(false, false, {});
    const host::SDirectEndpointDescriptor* const descriptor =
        host::SDirectConnectionTestAccess::private_descriptor(fixture.connections(), token, 0u);
    fixture.a.hold = true;
    fixture.connections().close(*fixture.second, token, connections::EStatus::cancelled);
    fixture.step(4u);
    host::SLifecycleMessage padding;
    padding.operation = host::ELifecycle::activated;
    padding.notification.connection = token;
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        TEST_EXPECT(ctx, fixture.first->channel->preamble_control(padding));
    }
    fixture.a.hold = false;
    fixture.first->preamble->process();
    TEST_EXPECT(ctx, (!fixture.first->preamble->work_pending() &&
        host::SDirectConnectionTestAccess::contains(fixture.connections(), token)));
    host::SLifecycleMessage stale;
    stale.operation = host::ELifecycle::activate;
    stale.notification.connection = token;
    TEST_EXPECT(ctx, fixture.first->channel->host_control(stale));
    stale.operation = host::ELifecycle::install;
    stale.descriptor = descriptor;
    TEST_EXPECT(ctx, fixture.first->channel->host_control(stale));
    fixture.first->preamble->process();
    TEST_EXPECT(ctx, ((fixture.a.available == 1u) && !fixture.first->preamble->failed()));
    for (unsigned index = 0u; index < host::k_module_lifecycle_capacity; ++index)
    {
        host::SLifecycleMessage discarded;
        TEST_EXPECT(ctx, (fixture.first->channel->read_host_control(discarded) &&
            (discarded.operation == host::ELifecycle::activated)));
    }
    fixture.step(8u);
    TEST_EXPECT(ctx, (fixture.connections().empty() && (fixture.a.retired == 1u)));
    // Exact-token lookup must reject this now-obsolete descriptor before read.
    TEST_EXPECT(ctx, fixture.first->channel->host_control(stale));
    fixture.first->preamble->process();
    TEST_EXPECT(ctx, (!fixture.first->preamble->failed() && (fixture.a.available == 1u) && !fixture.first->preamble->work_pending()));
}

void common_address_replacement(tests::TTestContext& ctx)
{
    SCommonMultiFixture fixture{ ctx };
    SCommonMultiFixture::SPeer& a = fixture.add(module_ids::executive, thread_ids::host);
    SCommonMultiFixture::SPeer& b = fixture.add(module_ids::rendering, thread_ids::host);
    fixture.step(4u);
    TEST_EXPECT(ctx, ((a.client->phase == host::EClientPhase::starting) &&
        (b.client->phase == host::EClientPhase::starting) && a.state.participated && b.state.participated));
    const connections::SToken previous = fixture.establish(a, a, b);
    fixture.close(a, previous);
    b.client->preamble->request_stop(modules::EExecutionStop::requested);
    fixture.step(16u);
    const bool detached = host::SModulePreambleTestAccess::detach_coordinator(fixture.runtime, *b.client, b.retained);
    TEST_EXPECT(ctx, detached);
    if (!detached) return;
    b.deliver = false;
    SCommonMultiFixture::SPeer& replacement = fixture.add(module_ids::rendering, thread_ids::host);
    const connections::SToken current = fixture.establish(a, a, replacement);
    TEST_EXPECT(ctx, ((current.value != previous.value) && (b.client->identity.value != replacement.client->identity.value)));
    fixture.connections().close(*b.client, current, connections::EStatus::cancelled);
    fixture.connections().close(*a.client, previous, connections::EStatus::cancelled);
    fixture.connections().quiescent(*b.client, current);
    fixture.invoke(a, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, communication.query({ modules::SDestination::EKind::connection, {}, current }).active);
        TEST_EXPECT(ctx, (communication.submit({ modules::SDestination::EKind::peer, replacement.state.address, previous },
            threading::make_pod_message(7, modules::RunTestsRequest{})) == modules::ESubmission::no_pipe));
    });
    fixture.invoke(replacement, [&](const modules::CCommunication& communication) noexcept
    {
        TEST_EXPECT(ctx, modules::accepted(communication.submit({ modules::SDestination::EKind::peer, a.state.address, current },
            threading::make_pod_message(7, modules::RunTestsRequest{}))));
    });
    fixture.step();
    TEST_EXPECT(ctx, (a.state.last_source.endpoint.value == replacement.client->identity.value));
    fixture.close(replacement, current);
}

void provider_borrow_destruction_gate(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &CPeerExecution::create;
    execution.handle = &CPeerExecution::dispatch;
    execution.destroy = &CPeerExecution::destroy;
    CPeerExecution state;
    state.allow_host = true;
    host::SClientRecord* const provider = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding, 3u, execution, &state);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    TEST_EXPECT(ctx, (provider && client));
    if (!provider || !client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    TEST_EXPECT(ctx, provider->preamble->activate());
    const auto turn = [&]()
    {
        provider->preamble->process();
        (void)host::SModulePreambleTestAccess::drain(runtime);
    };
    turn();
    turn();
    host::CServiceRouter& router = host::SModulePreambleTestAccess::router(runtime);
    const services::SServiceContract contract{ 1u, 1u };
    const host::SServiceProvider advertisement{ provider, &contract, 1u };
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(1u)));
        router.open({ module_ids::rendering, 1u, 1u, { 1u } }, 0, *client, &advertisement);
    }
    threading::CErasedPodMsg message;
    services::OpenServiceResult opened;
    TEST_EXPECT(ctx, (client->channel->read_preamble_pod(message) && message.copy_payload_to(opened)));
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 2u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(2u)));
        services::ServiceRequest request;
        request.route = opened.route;
        request.request = { 1u };
        request.opcode = 1u;
        router.request(request, 1, *client);
    }
    turn();
    turn();
    services::SServiceBorrow borrow;
    TEST_EXPECT(ctx, (router.prepare_borrow(*provider, { opened.route, { 1u }, { &state, sizeof(state), 1u } }, borrow) ==
        services::EServiceStatus::success));
    services::ServiceReply completed;
    completed.route = opened.route;
    completed.request = { 1u };
    completed.opcode = 1u;
    TEST_EXPECT(ctx, (router.complete(*provider, completed) == services::EServiceStatus::success));
    for (std::uint32_t count = 0u; count < 3u; ++count) turn();
    while (client->channel->read_preamble_pod(message)) {}
    provider->preamble->request_stop(modules::EExecutionStop::requested);
    for (std::uint32_t count = 0u; count < 6u; ++count) turn();
    TEST_EXPECT(ctx, (state.stopping && provider->seal_pending && !provider->seal_acknowledged && (state.destroyed == 0u)));
    TEST_EXPECT(ctx, !router.provider_borrows_retired(*provider));
    TEST_EXPECT(ctx, (router.begin_borrow_retirement(*provider, borrow) == services::EServiceStatus::success));
    turn();
    router.release_borrow(*client, { opened.route, borrow });
    router.quiesce_borrow(*client, { opened.route, borrow });
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, (state.destroyed == 0u));
    TEST_EXPECT(ctx, (router.finish_borrow_retirement(*provider, borrow) == services::EServiceStatus::success));
    for (std::uint32_t count = 0u; count < 4u; ++count) turn();
    TEST_EXPECT(ctx, (router.provider_borrows_retired(*provider) && provider->seal_acknowledged && (state.destroyed == 1u)));
    TEST_EXPECT(ctx, ((state.created_on == state.destroyed_on) &&
        (state.destroyed_on == platform::threading::query_current_thread_id()) && !provider->preamble->failed()));
    host::SModulePreambleTestAccess::clear(runtime);
}

void standard_coordinator_controls(tests::TTestContext& ctx)
{
    // Host is the only producer of these private records. The fixture controls
    // that boundary; actual child launch/join is covered by the following case.
    for (unsigned scenario = 0u; scenario < 12u; ++scenario)
    {
        platform::threading::COwnerWait wake;
        TEST_EXPECT(ctx, (wake.initialise().code == platform::threading::EWaitError::none));
        threading::SModuleEndpointState module;
        threading::CEndpointResources resources{ module_ids::executable, module_ids::audio, thread_ids::host };
        resources.identity = { 751u };
        resources.instance = { 851u };
        resources.owner_wake = wake.endpoint();
        resources.module_state = &module;
        host::CModuleChannel channel{ resources };
        TEST_EXPECT(ctx, channel.initialise(*memory::get_ambient_memory_context(), 32u, 32u));
        modules::CBoundModule binding;
        modules::SBoundModuleTestAccess::install(binding);
        modules::SExecutionV1 child_execution;
        child_execution.create = &CPeerExecution::create;
        child_execution.handle = &CPeerExecution::dispatch;
        child_execution.destroy = &CPeerExecution::destroy;
        const modules::SChildExecution child{ &child_execution, nullptr, thread_ids::audio };
        modules::SExecutionV1 execution;
        execution.initial_child = &child;
        host::CModulePreamble preamble{ resources, channel, binding, execution, *memory::get_ambient_memory_context() };
        TEST_EXPECT(ctx, preamble.activate());
        preamble.process();
        host::SLifecycleMessage request;
        TEST_EXPECT(ctx, (channel.read_host_control(request) && (request.operation == host::ELifecycle::create_child) &&
            (request.correlation == 0) && (request.child.descriptor == &child_execution)));
        TEST_EXPECT(ctx, (!channel.read_host_control(request) && !preamble.ready()));
        unsigned stops = 0u, readiness = 0u, seals = 0u, closures = 0u;
        const auto post = [&](const host::ELifecycle operation, const std::uint64_t endpoint = 951u,
            const services::EServiceStatus status = services::EServiceStatus::success)
        {
            host::SLifecycleMessage message;
            message.operation = operation;
            message.correlation = 0;
            message.notification.first = { endpoint };
            message.notification.status = status;
            TEST_EXPECT(ctx, channel.host_control(message));
        };
        const auto controls = [&]()
        {
            host::SLifecycleMessage message;
            while (channel.read_host_control(message))
            {
                host::SLifecycleMessage response;
                switch (message.operation)
                {
                    case host::ELifecycle::stop_child:
                    {
                        ++stops;
                        TEST_EXPECT(ctx, (message.notification.first.value == 951u));
                        response.operation = host::ELifecycle::child_stop_result;
                        response.correlation = message.correlation;
                        response.notification = message.notification;
                        TEST_EXPECT(ctx, channel.host_control(response));
                        break;
                    }
                    case host::ELifecycle::close_provider_admission:
                    {
                        ++closures;
                        response.operation = host::ELifecycle::provider_admission_closed;
                        TEST_EXPECT(ctx, channel.host_control(response));
                        break;
                    }
                    case host::ELifecycle::seal:
                    {
                        ++seals;
                        response.operation = host::ELifecycle::sealed;
                        TEST_EXPECT(ctx, channel.host_control(response));
                        break;
                    }
                    case host::ELifecycle::ready:
                    {
                        ++readiness;
                        break;
                    }
                    case host::ELifecycle::stop_module:
                    {
                        break;
                    }
                    default:
                    {
                        TEST_EXPECT(ctx, false);
                        break;
                    }
                }
            }
        };
        const bool creation_failed = (scenario == 3u) || (scenario == 7u);
        const bool invalid = (scenario == 2u) || (scenario >= 8u);
        if ((scenario == 6u) || (scenario == 7u))
        {
            preamble.request_stop(modules::EExecutionStop::requested);
            preamble.process();
            controls();
            TEST_EXPECT(ctx, (!stops && !seals && !preamble.destroyed()));
        }
        if (scenario == 8u)
        {
            post(host::ELifecycle::child_ready);
            preamble.process();
            TEST_EXPECT(ctx, (preamble.failed() && !preamble.ready()));
        }
        post(host::ELifecycle::child_created, creation_failed ? 0u : 951u,
            creation_failed ? services::EServiceStatus::native_failure : services::EServiceStatus::success);
        if (scenario != 1u) preamble.process();
        TEST_EXPECT(ctx, !preamble.ready());
        if (scenario == 4u) post(host::ELifecycle::child_ended);
        if (scenario == 5u) module.stopping.store(1u, std::memory_order_release);
        if ((scenario <= 5u) && !creation_failed)
        {
            post(host::ELifecycle::child_ready, (scenario == 2u) ? 952u : 951u);
        }
        if (scenario == 9u) post(host::ELifecycle::child_created);
        if (scenario == 10u) post(host::ELifecycle::child_ended, 952u);
        if (scenario == 11u) post(host::ELifecycle::child_retired, 952u);
        preamble.process();
        controls();
        TEST_EXPECT(ctx, (preamble.ready() == (scenario <= 1u)));
        TEST_EXPECT(ctx, (preamble.failed() == (invalid || (scenario == 3u))));
        preamble.request_stop(modules::EExecutionStop::requested);
        preamble.process();
        controls();
        if (!creation_failed)
        {
            TEST_EXPECT(ctx, (!seals && !preamble.destroyed()));
            if (scenario != 4u) post(host::ELifecycle::child_ended);
            post(host::ELifecycle::child_retired);
        }
        for (unsigned turn = 0u; turn < 8u; ++turn)
        {
            preamble.process();
            controls();
        }
        TEST_EXPECT(ctx, (preamble.destroyed() && (seals == 1u) && (closures == 1u)));
        TEST_EXPECT(ctx, (readiness == ((scenario <= 1u) ? 1u : 0u)));
        TEST_EXPECT(ctx, (stops == ((creation_failed || (scenario == 4u)) ? 0u : 1u)));
    }
}

void standard_coordinator_child(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    TEST_EXPECT(ctx, host::SModulePreambleTestAccess::prepare_threads(runtime));
    modules::SExecutionV1 child_execution;
    child_execution.create = &CPeerExecution::create;
    child_execution.handle = &CPeerExecution::dispatch;
    child_execution.destroy = &CPeerExecution::destroy;
    CPeerExecution child_state;
    const modules::SChildExecution child{ &child_execution, &child_state, thread_ids::audio };
    modules::SExecutionV1 coordinator_execution;
    coordinator_execution.initial_child = &child;
    host::SClientRecord* const coordinator = host::SModulePreambleTestAccess::install(runtime,
        module_ids::audio, binding, 3u, coordinator_execution);
    TEST_EXPECT(ctx, (coordinator && coordinator->preamble->activate()));
    if (!coordinator)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    threading::CWaitDeadline deadline;
    TEST_EXPECT(ctx, (deadline.set_after_milliseconds(5000u).code == platform::threading::EWaitError::none));
    bool ready = false;
    while (!coordinator->preamble->destroyed())
    {
        coordinator->preamble->process();
        host::SModulePreambleTestAccess::progress_threads(runtime);
        if (!ready && coordinator->preamble->ready())
        {
            ready = true;
            TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::children_idle(runtime, module_ids::audio));
            coordinator->preamble->request_stop(modules::EExecutionStop::requested);
        }
        std::uint64_t remaining = 0u;
        if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            break;
        }
        threading::CWaitDeadline pause;
        (void)pause.set_after_milliseconds(1u);
        std::atomic<std::uint32_t> unchanged{ 0u };
        (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
    }
    TEST_EXPECT(ctx, (ready && coordinator->preamble->destroyed() && !coordinator->preamble->failed()));
    TEST_EXPECT(ctx, host::SModulePreambleTestAccess::children_idle(runtime, module_ids::audio));
    //  Successful retirement includes native join before these callback writes
    //  are inspected on Host. The standard coordinator waits for that outcome.
    TEST_EXPECT(ctx, ((child_state.destroyed == 1u) && (child_state.created_on == child_state.destroyed_on) &&
        (child_state.created_on != platform::threading::query_current_thread_id())));
    host::SModulePreambleTestAccess::clear(runtime);
}

void module_dll_execution(tests::TTestContext& ctx, const module_ids::id_type module, const char* const stem,
    const thread_ids::id_type service_thread)
{
    TInstance<debug_system::CDebugServiceState> debug_service = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, debug_service.is_ready());
    if (!debug_service) return;
    const std::string log = test_environment::test_log_path(stem);
    const std::string direct_log = test_environment::test_log_path((std::string{ stem } + "_direct").c_str());
    TEST_EXPECT(ctx, (debug_service->configure_log_paths(log.c_str(), direct_log.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "preamble_test" })));
    const system_ids::id_type caller_identity = system_context::get_ambient_system_id();
    memory::CMemoryContext* const caller_memory = memory::get_ambient_memory_context();
    const platform::threading::CPlatformThreadId caller_thread = platform::threading::query_current_thread_id();
    memory::CMemoryAllocator host_allocator{ nullptr, &tests::allocate_scalar_test_memory,
        &tests::deallocate_scalar_test_memory, system_ids::host };
    SDllAllocation attribution;
    const system_ids::id_type module_identity = system_ids::ops::make_system_id(module, service_thread);
    memory::CMemoryAllocator module_allocator{ &attribution, &SDllAllocation::allocate, &SDllAllocation::free, module_identity };
    memory::CMemoryContext module_memory{ module_allocator, module_identity };
    //  Owning transfers require the same allocator on both sides, as in
    //  production Host. Keep distinct attribution contexts for each image.
    memory::CMemoryContext shared_host_memory{ module_allocator, system_ids::host };
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string filename = test_environment::binary_path(test_environment::module_filename(stem).c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
    const bool bound = path.is_ready() && binding.bind(path, module, host_identity);
    const bool installed = bound && binding.install(test_environment::system_registry_view(), module, &module_memory,
        system_context::sequence_counter, debug_service.operator->());
    TEST_EXPECT(ctx, installed);
    if (!installed)
    {
        std::cerr << "DLL fixture installation failed: " << stem << ", bound " << bound << '\n';
        TEST_EXPECT(ctx, binding.unbind());
        TEST_EXPECT(ctx, debug_service->stop());
        return;
    }
    modules::SExecutionV1 advertised;
    TEST_EXPECT(ctx, (binding.query_execution(advertised) == modules::EBindingResult::success));
    //  Bootstrap is once per binding, including when the ELF loader retains an
    //  image after dlclose. Exercise fresh contexts under that single binding.
    const unsigned cycles = ((module == module_ids::audio) || (module == module_ids::rendering)) ? 3u : 1u;
    for (unsigned cycle = 0u; cycle < cycles; ++cycle)
    {
        const bool serviced = (cycle == 1u) || (module == module_ids::conditioning_general);
        const bool failure = cycle == 2u;
        const bool exercise = (module == module_ids::rendering) && !failure;
        const bool empty = module_memory.is_attribution_empty() && shared_host_memory.is_attribution_empty();
        TEST_EXPECT(ctx, empty);
        if (!empty)
        {
            std::cerr << "DLL fixture retained allocations before next execution: " << stem << '\n';
            std::_Exit(1);
        }
        attribution.allocations.store(0u);
        attribution.frees.store(0u);
        attribution.primary = nullptr;
        attribution.allocated_on = platform::threading::CPlatformThreadId{};
        attribution.freed_on = platform::threading::CPlatformThreadId{};
        attribution.track_primary = !exercise;
        tests::TMemoryContextScope host_scope{ exercise ? &shared_host_memory : caller_memory };
        modules::SExecutionV1 execution = advertised;
        if (module == module_ids::platform)
        {
            TEST_EXPECT(ctx, (execution.create && (execution.flags == modules::k_execution_native_events)));
        }
        else
        {
            TEST_EXPECT(ctx, (execution.initial_child && execution.initial_child->descriptor));
            if (!execution.initial_child || !execution.initial_child->descriptor)
            {
                continue;
            }
            if (!serviced) execution = *execution.initial_child->descriptor;
        }
        host::CHost runtime{ host_allocator };
        TEST_EXPECT(ctx, host::SModulePreambleTestAccess::prepare_threads(runtime));
        host::SClientRecord* const coordinator = host::SModulePreambleTestAccess::install(runtime,
            module, binding, 3u, execution);
        TEST_EXPECT(ctx, coordinator);
        const bool batch_ready = coordinator && host::SModulePreambleTestAccess::prepare_batch(runtime, *coordinator, binding);
        TEST_EXPECT(ctx, batch_ready);
        const bool activated = batch_ready && coordinator->preamble->activate();
        TEST_EXPECT(ctx, activated);
        if (!activated)
        {
            std::cerr << "DLL execution activation failed: " << stem << ", batch " << batch_ready
                << ", allocations " << attribution.allocations << ", frees " << attribution.frees << '\n';
        }
        if (!coordinator)
        {
            host::SModulePreambleTestAccess::clear(runtime);
            continue;
        }
        modules::CBoundModule requester_binding;
        modules::SBoundModuleTestAccess::install(requester_binding);
        CPeerExecution requester_state;
        requester_state.test_module = module;
        requester_state.peer = system_ids::ops::make_system_id(module, serviced ? service_thread : thread_ids::host);
        modules::SExecutionV1 requester_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
            &CPeerExecution::create, &CPeerExecution::dispatch, &CPeerExecution::destroy };
        const modules::SChildExecution requester_child{ &requester_execution, &requester_state, thread_ids::executive };
        modules::SExecutionV1 requester_coordinator;
        requester_coordinator.initial_child = &requester_child;
        host::SClientRecord* requester = nullptr;
        if (exercise)
        {
            TEST_EXPECT(ctx, host::SModulePreambleTestAccess::start_batch(runtime));
            requester = host::SModulePreambleTestAccess::install(runtime, module_ids::executive,
                requester_binding, 3u, requester_coordinator);
            TEST_EXPECT(ctx, (requester && requester->preamble->activate()));
        }
        threading::CWaitDeadline deadline;
        TEST_EXPECT(ctx, (deadline.set_after_milliseconds(exercise ? 60000u : 5000u).code == platform::threading::EWaitError::none));
        bool ready = false, stopping = false, timed_out = false;
        while (!coordinator->preamble->destroyed() || (requester && !requester->preamble->destroyed()))
        {
            coordinator->preamble->process();
            if (requester) requester->preamble->process();
            host::SModulePreambleTestAccess::progress_threads(runtime);
            if (exercise)
            {
                host::SModulePreambleTestAccess::connections(runtime).progress();
                host::SModulePreambleTestAccess::assets(runtime).complete_disposals();
            }
            TEST_EXPECT(ctx, ((system_context::get_ambient_system_id() == caller_identity) &&
                (memory::get_ambient_memory_context() == (exercise ? &shared_host_memory : caller_memory))));
            if (!ready && coordinator->preamble->ready())
            {
                ready = true;
                TEST_EXPECT(ctx, ((module_memory.get_live_allocation_count() != 0u) && (attribution.allocations != 0u)));
                if (module == module_ids::audio)
                {
                    //  This is the class allocation only: idle Audio never opens a device.
                    TEST_EXPECT(ctx, ((module_memory.get_live_allocation_count() == 1u) && (attribution.allocations == 1u)));
                }
                if (exercise)
                {
                    requester_state.test_start.store(true, std::memory_order_release);
                    host::SModulePreambleTestAccess::wake(runtime, system_ids::executive);
                }
                if (failure)
                {
                    TEST_EXPECT(ctx, (coordinator->channel->to_module.post(threading::make_pod_message(-1,
                        modules::RunTestsRequest{ module })) == host::EModulePipePost::published));
                }
            }
            if (ready && !stopping && !failure && (!exercise || requester_state.test_done.load(std::memory_order_acquire)))
            {
                stopping = true;
                coordinator->preamble->request_stop(modules::EExecutionStop::requested);
                if (requester) requester->preamble->request_stop(modules::EExecutionStop::requested);
            }
            std::uint64_t remaining = 0u;
            if ((!failure && !timed_out && (coordinator->endpoint->failure.load(std::memory_order_acquire) != threading::EEndpointFailure::none)) ||
                (deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                TEST_EXPECT(ctx, false);
                if (timed_out)
                {
                    //  Do not destroy affine live DLL state on the test owner.
                    std::cerr << "DLL fixture could not retire after timeout: " << stem << '\n';
                    std::_Exit(1);
                }
                timed_out = stopping = true;
                coordinator->preamble->request_stop(modules::EExecutionStop::requested);
                if (requester) requester->preamble->request_stop(modules::EExecutionStop::requested);
                (void)deadline.set_after_milliseconds(5000u);
            }
            threading::CWaitDeadline pause;
            (void)pause.set_after_milliseconds(1u);
            std::atomic<std::uint32_t> unchanged{ 0u };
            (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
        }
        TEST_EXPECT(ctx, (ready && coordinator->preamble->destroyed() && (coordinator->preamble->failed() == failure)));
        TEST_EXPECT(ctx, host::SModulePreambleTestAccess::children_idle(runtime, module));
        if (exercise)
        {
            TEST_EXPECT(ctx, (requester_state.test_done.load(std::memory_order_acquire) &&
                host::SModulePreambleTestAccess::children_idle(runtime, module_ids::executive) && requester_state.valid));
            TEST_EXPECT(ctx, host::SModulePreambleTestAccess::stop_batch(runtime));
            TEST_EXPECT(ctx, host::SModulePreambleTestAccess::assets(runtime).is_idle());
        }
        TEST_EXPECT(ctx, module_memory.is_attribution_empty());
        if (!exercise)
        {
            TEST_EXPECT(ctx, (attribution.frees == attribution.allocations));
            TEST_EXPECT(ctx, ((attribution.allocated_on == attribution.freed_on) &&
                ((attribution.allocated_on == caller_thread) != serviced)));
        }
        host::SModulePreambleTestAccess::clear(runtime);
    }
    TEST_EXPECT(ctx, (module_memory.is_attribution_empty() && shared_host_memory.is_attribution_empty()));
    TEST_EXPECT(ctx, binding.unbind());
    TEST_EXPECT(ctx, debug_service->stop());
}

struct SExecutiveFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    unsigned owner_attempts{ 0u }, owners{ 0u }, requests{ 0u }, connects{ 0u }, closes{ 0u }, quiescences{ 0u };
    std::int32_t load_slot{ -1 }, connect_slot{ -1 }, request_slot{ -1 };
    bool valid{ true };
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SExecutiveFacade& self = *static_cast<SExecutiveFacade*>(cookie);
        ++self.owner_attempts;
        self.valid = self.valid && (destination.kind == modules::SDestination::EKind::host) &&
            (message.owner().payload<modules::ModuleRequest>() != nullptr);
        if (modules::accepted(self.result))
        {
            self.load_slot = message.query_async_slot();
            ++self.owners;
            message = {};
        }
        return self.result;
    }
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SExecutiveFacade& self = *static_cast<SExecutiveFacade*>(cookie);
        self.valid = self.valid && (destination.kind == modules::SDestination::EKind::connection) &&
            (destination.token.value == 77u) && message.is_payload_a<modules::RunTestsRequest>();
        if (modules::accepted(self.result))
        {
            self.request_slot = message.query_async_slot();
            ++self.requests;
        }
        return self.result;
    }
    static modules::ESubmission MV_STD_ABI_CALL intent(void* const cookie, const modules::SExecutionIntent& intent) noexcept
    {
        SExecutiveFacade& self = *static_cast<SExecutiveFacade*>(cookie);
        switch (intent.kind)
        {
            case modules::SExecutionIntent::EKind::connect:
            {
                ++self.connects;
                self.connect_slot = intent.correlation;
                break;
            }
            case modules::SExecutionIntent::EKind::close_connection:
            {
                ++self.closes;
                break;
            }
            case modules::SExecutionIntent::EKind::connection_quiescent:
            {
                ++self.quiescences;
                break;
            }
            default:
            {
                break;
            }
        }
        return modules::ESubmission::accepted;
    }
    static modules::SConnectionQuery MV_STD_ABI_CALL query(void*, const modules::SDestination& destination) noexcept
    {
        return { destination.token, modules::test_connections::address(module_ids::rendering), 1u, 0u };
    }
};

void executive_dll_retries(tests::TTestContext& ctx)
{
    TInstance<debug_system::CDebugServiceState> debug_service = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, debug_service.is_ready());
    if (!debug_service) return;
    const std::string log = test_environment::test_log_path("ExecutiveRetries");
    const std::string direct = test_environment::test_log_path("ExecutiveRetries_direct");
    TEST_EXPECT(ctx, (debug_service->configure_log_paths(log.c_str(), direct.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "executive_retry" })));
    const system_ids::id_type caller_identity = system_context::get_ambient_system_id();
    memory::CMemoryContext* const caller_memory = memory::get_ambient_memory_context();
    SDllAllocation attribution;
    memory::CMemoryAllocator allocator{ &attribution, &SDllAllocation::allocate, &SDllAllocation::free, system_ids::executive };
    memory::CMemoryContext module_memory{ allocator, system_ids::executive };
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string filename = test_environment::binary_path(test_environment::module_filename("MorphicExecutive").c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
    const bool installed = path.is_ready() && binding.bind(path, module_ids::executive, host_identity) &&
        binding.install(test_environment::system_registry_view(), module_ids::executive, &module_memory,
            system_context::sequence_counter, debug_service.operator->());
    TEST_EXPECT(ctx, installed);
    if (installed)
    {
        modules::SExecutionV1 coordinator;
        TEST_EXPECT(ctx, (binding.query_execution(coordinator) == modules::EBindingResult::success));
        TEST_EXPECT(ctx, (coordinator.initial_child && coordinator.initial_child->descriptor));
        if (coordinator.initial_child && coordinator.initial_child->descriptor)
        {
            const modules::SExecutionV1 execution = *coordinator.initial_child->descriptor;
            static const modules::SCommunicationV1 functions{ &SExecutiveFacade::pod, &SExecutiveFacade::owner,
                &SExecutiveFacade::intent, &SExecutiveFacade::query };
            for (const bool early_stop : { true, false })
            {
                SExecutiveFacade state;
                modules::CCommunication communication{ functions, &state };
                modules::CHostRestrictionPublication restrictions;
                modules::SHostRestrictionSnapshot snapshot;
                snapshot.generation = 1u;
                snapshot.sampling = 1u;
                TEST_EXPECT(ctx, restrictions.publish(snapshot));
                std::atomic<std::uint32_t> work{ 0u };
                threading::CNotification notification;
                TEST_EXPECT(ctx, notification.initialise(work));
                modules::SHostExecutionContext host_execution;
                host_execution.restriction = restrictions.reader();
                host_execution.notification = notification.reader();
                modules::SExecutionContext context;
                context.address = system_ids::executive;
                context.communication = &communication;
                context.execution = &host_execution;
                void* runtime = nullptr;
                TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
                TEST_EXPECT(ctx, (execution.create(&context, &runtime) == modules::EExecutionStatus::success));
                TEST_EXPECT(ctx, runtime);
                if (!runtime) continue;
                modules::SExecutionProgress progress;
                const auto invoke = [&](const modules::EExecutionEvent event, const modules::SExecutionNotification detail = {},
                    const threading::CErasedPodMsg* const message = nullptr, const connections::SSource source = {})
                {
                    modules::SInvocation invocation;
                    invocation.event = event;
                    invocation.notification = detail;
                    invocation.message = message;
                    invocation.source = source;
                    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
                    TEST_EXPECT(ctx, (execution.handle(runtime, &invocation, &progress) == modules::EExecutionStatus::success));
                    TEST_EXPECT(ctx, ((system_context::get_ambient_system_id() == caller_identity) &&
                        (memory::get_ambient_memory_context() == caller_memory)));
                };
                invoke(modules::EExecutionEvent::start);
                TEST_EXPECT(ctx, ((state.owner_attempts == 0u) && !progress.work_pending &&
                    (progress.next_wake_ns == executive::k_timing_retry_ns)));
                if (!early_stop)
                {
                    snapshot.sampling = 0u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.owner_attempts == 1u) && (state.owners == 0u) && !progress.work_pending));
                    state.result = modules::ESubmission::accepted;
                    invoke(modules::EExecutionEvent::service_async);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.owners == 1u) && (state.owner_attempts == 2u)));
                    snapshot.sampling = 1u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    modules::ModuleResult loaded;
                    loaded.module = module_ids::rendering;
                    loaded.status = modules::EModuleStatus::success;
                    loaded.notice = modules::EModuleNotice::acknowledged;
                    threading::CErasedPodMsg message = threading::make_pod_message(state.load_slot, loaded);
                    connections::SSource source;
                    source.kind = connections::SSource::EKind::host;
                    source.address = system_ids::host;
                    invoke(modules::EExecutionEvent::message, {}, &message, source);
                    loaded.notice = modules::EModuleNotice::completed;
                    loaded.available = true;
                    message = threading::make_pod_message(state.load_slot, loaded);
                    invoke(modules::EExecutionEvent::message, {}, &message, source);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.connects == 0u) && !progress.work_pending &&
                        (progress.next_wake_ns == executive::k_timing_retry_ns)));
                    snapshot.sampling = 0u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, (state.connects == 1u));
                    snapshot.sampling = 1u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    modules::SExecutionNotification connected;
                    connected.correlation = state.connect_slot;
                    connected.connection = { 77u };
                    connected.connection_status = connections::EStatus::success;
                    invoke(modules::EExecutionEvent::connection_result, connected);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.requests == 0u) && !progress.work_pending));
                    snapshot.sampling = 0u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    state.result = modules::ESubmission::published_wake_failed;
                    invoke(modules::EExecutionEvent::service_async);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, (state.requests == 1u));
                    snapshot.sampling = 1u;
                    TEST_EXPECT(ctx, restrictions.publish(snapshot));
                    message = threading::make_pod_message(state.request_slot,
                        modules::RunTestsResult{ module_ids::rendering, modules::ETestRunStatus::success, 23u, 0u, 0u });
                    source.kind = connections::SSource::EKind::direct;
                    source.address = modules::test_connections::address(module_ids::rendering);
                    source.connection = connected.connection;
                    invoke(modules::EExecutionEvent::message, {}, &message, source);
                    TEST_EXPECT(ctx, (state.closes == 1u));
                    invoke(modules::EExecutionEvent::connection_closing, connected);
                    invoke(modules::EExecutionEvent::connection_retired, connected);
                    invoke(modules::EExecutionEvent::service_async);
                    TEST_EXPECT(ctx, ((state.quiescences == 1u) && (state.owner_attempts == 2u) &&
                        !progress.work_pending && (progress.next_wake_ns == executive::k_timing_retry_ns)));
                }
                invoke(modules::EExecutionEvent::stop);
                TEST_EXPECT(ctx, (progress.work_complete && !progress.work_pending && (progress.next_wake_ns == UINT64_MAX)));
                TEST_EXPECT(ctx, (execution.destroy(runtime) == modules::EExecutionStatus::success));
                TEST_EXPECT(ctx, (state.valid && module_memory.is_attribution_empty()));
            }
        }
        TEST_EXPECT(ctx, binding.unbind());
    }
    TEST_EXPECT(ctx, (attribution.frees == attribution.allocations));
    TEST_EXPECT(ctx, debug_service->stop());
}

struct SFailureExecution
{
    static inline SFailureExecution* next{ nullptr };
    modules::CCommunication communication;
    bool refuse_create{}, fail_start{}, fail_message{}, fill_output{}, stopping{}, allow_completion{}, participate{};
    bool refuse_destroy{}, fail_create_with_state{};
    unsigned created{}, destroyed{}, published{}, handled{};
    platform::threading::CPlatformThreadId created_on, destroyed_on;
    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        *runtime = nullptr;
        if (next->refuse_create) return modules::EExecutionStatus::failed;
        next->communication = *context->communication;
        next->created_on = platform::threading::query_current_thread_id();
        ++next->created;
        *runtime = next;
        return next->fail_create_with_state ? modules::EExecutionStatus::failed : modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        SFailureExecution& self = *static_cast<SFailureExecution*>(runtime);
        ++self.handled;
        *progress = {};
        if (invocation->event == modules::EExecutionEvent::stop) self.stopping = true;
        if (self.participate && (invocation->event == modules::EExecutionEvent::start))
        {
            modules::SExecutionIntent request;
            request.kind = modules::SExecutionIntent::EKind::participate;
            request.correlation = 0;
            if (!modules::accepted(self.communication.submit(request)))
            {
                return modules::EExecutionStatus::failed;
            }
        }
        if (self.fail_start && (invocation->event == modules::EExecutionEvent::start))
        {
            self.fail_start = false;
            return modules::EExecutionStatus::failed;
        }
        if (self.fill_output && !self.stopping)
        {
            const auto filler = threading::make_pod_message(-1, platform::reports::PlatformReportCaptureFailed{});
            for (unsigned index = 0u; index <= host::k_module_pod_capacity; ++index)
            {
                const modules::ESubmission status = self.communication.submit({}, filler);
                if (status == modules::ESubmission::busy) break;
                if (status != modules::ESubmission::accepted)
                {
                    return modules::EExecutionStatus::failed;
                }
                ++self.published;
            }
            self.fill_output = false;
        }
        if (self.fail_message && (invocation->event == modules::EExecutionEvent::message))
        {
            self.fail_message = false;
            return modules::EExecutionStatus::failed;
        }
        progress->ready = self.stopping ? 0u : 1u;
        progress->work_complete = (self.stopping && self.allow_completion) ? 1u : 0u;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        SFailureExecution& self = *static_cast<SFailureExecution*>(runtime);
        ++self.destroyed;
        self.destroyed_on = platform::threading::query_current_thread_id();
        return self.refuse_destroy ? modules::EExecutionStatus::failed : modules::EExecutionStatus::success;
    }
    static modules::SExecutionV1 descriptor() noexcept
    {
        return { sizeof(modules::SExecutionV1), 1u, 0u, &create, &handle, &destroy };
    }
};

void pumped_destroy_refusal(tests::TTestContext& ctx)
{
    using Access = host::SModulePreambleTestAccess;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    SFailureExecution retained, other;
    retained.allow_completion = retained.refuse_destroy = true;
    SFailureExecution::next = &retained;
    host::SClientRecord* const client = Access::driver(runtime, module_ids::rendering, binding, SFailureExecution::descriptor());
    SFailureExecution::next = &other;
    host::SClientRecord* const unrelated = Access::driver(runtime, module_ids::audio, binding, SFailureExecution::descriptor());
    tests::SCommonEndpointFixture requester{ module_ids::executive, 700u, 32u };
    TEST_EXPECT(ctx, (client && unrelated && Access::assets(runtime).initialise() && Access::prepare_turns(runtime)));
    if (!client || !unrelated)
    {
        Access::clear(runtime);
        return;
    }
    TEST_EXPECT(ctx, requester.reserve_replies(1u));
    Access::pending_unload(runtime, requester, binding, module_ids::rendering);
    host::CModuleService& service = Access::module_service(runtime);
    for (unsigned turn = 0u; turn < 20u; ++turn) Access::turn(runtime);
    TEST_EXPECT(ctx, (Access::emergency(runtime) && Access::retaining(runtime) && !Access::complete(runtime)));
    TEST_EXPECT(ctx, (client->preamble->destroy_refused() && client->preamble->retained() &&
        client->preamble->failed() && !client->preamble->destroyed() && !client->endpoint->sealed &&
        !client->execution_quiescent() && client->process_retention_eligible()));
    TEST_EXPECT(ctx, ((retained.destroyed == 1u) && (retained.created_on == retained.destroyed_on) &&
        client->subscription_live && service.output_ready(requester) && (requester.reserved_replies == 1u)));
    const unsigned handled = retained.handled;
    for (unsigned turn = 0u; turn < 8u; ++turn) Access::turn(runtime);
    service.retry_output();
    service.retry_output();
    modules::ModuleResult result;
    TEST_EXPECT(ctx, ((requester.pending() == 1u) && (requester.received(0).query_async_slot() == 94) &&
        requester.received(0).copy_payload_to(result) && (result.status == modules::EModuleStatus::unload_failed) &&
        (result.module == module_ids::rendering) && !requester.reserved_replies && service.is_idle()));
    requester.discard(requester.pending());
    // An unrelated live execution still blocks final process retention.
    Access::lifecycle(runtime);
    TEST_EXPECT(ctx, (!Access::complete(runtime) && !other.destroyed));
    other.allow_completion = true;
    for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
    TEST_EXPECT(ctx, (other.destroyed == 1u));
    TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
    // Controlled outer-frame hold: even a returned destroy callback does not
    // permit process completion while its caller can still access the graph.
    client->invocation_active = true;
    TEST_EXPECT(ctx, !client->process_retention_eligible());
    Access::lifecycle(runtime);
    TEST_EXPECT(ctx, !Access::complete(runtime));
    client->invocation_active = false;
    client->destroy_refusal_unwound = false;
    TEST_EXPECT(ctx, !client->process_retention_eligible());
    client->destroy_refusal_unwound = true;
    ++client->endpoint->published_pods;
    TEST_EXPECT(ctx, !client->process_retention_eligible());
    --client->endpoint->published_pods;
    client->incoming_drained = false;
    TEST_EXPECT(ctx, !client->process_retention_eligible());
    client->incoming_drained = true;
    Access::lifecycle(runtime);
    TEST_EXPECT(ctx, Access::complete(runtime));
    TEST_EXPECT(ctx, ((Access::endpoint(runtime, system_ids::ops::make_system_id(module_ids::rendering, thread_ids::host)) == client) &&
        (retained.destroyed == 1u) && (retained.handled == handled) && !requester.pending() &&
        client->subscription_live && !client->retire_host_execution(true) && !client->execution_quiescent()));
    // This fixture owns static callback state and a fake binding. Dismantling
    // its controlled graph is not the production retained-process teardown.
    Access::clear(runtime);

    host::CHost unsafe_host{ allocator };
    SFailureExecution unsafe;
    unsafe.fail_create_with_state = true;
    SFailureExecution::next = &unsafe;
    host::SClientRecord* const unsafe_client = Access::driver(unsafe_host, module_ids::rendering, binding, SFailureExecution::descriptor());
    TEST_EXPECT(ctx, unsafe_client);
    if (unsafe_client)
    {
        Access::pump(unsafe_host);
        TEST_EXPECT(ctx, (unsafe_client->preamble->retained() && !unsafe_client->preamble->destroy_refused() &&
            !unsafe_client->process_retention_eligible() && !unsafe_client->execution_quiescent()));
        TEST_EXPECT(ctx, (!Access::retire(unsafe_host, module_ids::rendering) && !Access::retaining(unsafe_host) && !unsafe.destroyed));
    }
    Access::clear(unsafe_host);
}

void global_failure_shutdown(tests::TTestContext& ctx)
{
    using Access = host::SModulePreambleTestAccess;
    const modules::SExecutionV1 execution = SFailureExecution::descriptor();
    for (const module_ids::id_type target : { module_ids::executive, module_ids::rendering })
    {
        //  Both initial binding and replacement can fail before any ready event.
        for (unsigned pending_case = 0u; pending_case < 4u; ++pending_case)
        {
            const bool replacement = (pending_case & 1u) != 0u;
            const bool refuse_create = pending_case < 2u;
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            host::CHost runtime{ allocator };
            modules::CBoundModule binding;
            modules::SBoundModuleTestAccess::install(binding);
            modules::SBoundModuleTestAccess::identity(binding, target);
            SFailureExecution unaffected, failed;
            SFailureExecution::next = &unaffected;
            host::SClientRecord* const other = Access::driver(runtime, module_ids::audio, binding, execution);
            TEST_EXPECT(ctx, other);
            Access::pump(runtime);
            failed.refuse_create = refuse_create;
            failed.fail_start = !refuse_create;
            SFailureExecution::next = &failed;
            Access::pending(runtime, binding, execution, target, replacement);
            if (!refuse_create) Access::pump(runtime);
            //  A failed create escalates immediately; a failed startup callback
            //  escalates on that pump, without a later worker result or readiness.
            TEST_EXPECT(ctx, (Access::emergency(runtime) && other->stop_requested));
            for (unsigned turn = 0u; turn < 8u; ++turn) Access::pump(runtime);
            TEST_EXPECT(ctx, (unaffected.stopping && !unaffected.destroyed &&
                (failed.created == (refuse_create ? 0u : 1u)) && !failed.destroyed));
            failed.allow_completion = unaffected.allow_completion = true;
            for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
            TEST_EXPECT(ctx, ((unaffected.destroyed == 1u) && (unaffected.created_on == unaffected.destroyed_on)));
            TEST_EXPECT(ctx, (failed.destroyed == (refuse_create ? 0u : 1u)));
            TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
            TEST_EXPECT(ctx, Access::retire(runtime, target));
            Access::finish_pending(runtime);
            Access::clear(runtime);
        }
        for (const bool lost_reply : { false, true })
        {
            memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
            host::CHost runtime{ allocator };
            modules::CBoundModule binding;
            modules::SBoundModuleTestAccess::install(binding);
            SFailureExecution unaffected, failed;
            SFailureExecution::next = &unaffected;
            host::SClientRecord* const other = Access::driver(runtime, module_ids::audio, binding, execution);
            SFailureExecution::next = &failed;
            host::SClientRecord* const client = Access::driver(runtime, target, binding, execution);
            TEST_EXPECT(ctx, (other && client));
            Access::pump(runtime);
            TEST_EXPECT(ctx, (client->preamble->ready() && !Access::emergency(runtime)));
            const auto filler = threading::make_pod_message(-1, modules::RunTestsResult{});
            if (lost_reply)
            {
                failed.fill_output = true;
                Access::pump_one(runtime, target);
                for (unsigned index = 0u; index < host::k_module_pod_capacity; ++index)
                {
                    TEST_EXPECT(ctx, (client->channel->to_module.post(filler) == host::EModulePipePost::published));
                }
                TEST_EXPECT(ctx, ((failed.published == host::k_module_pod_capacity) &&
                    !client->channel->to_host.writable_count() && !client->channel->to_module.writable_count()));
                //  Deliberately violate the accepted-reply storage contract. A
                //  missing reservation is loss, unlike the ordinary busy above.
                TEST_EXPECT(ctx, !client->reply(filler));
                TEST_EXPECT(ctx, (client->endpoint->failure.load() == threading::EEndpointFailure::accounting_failure));
            }
            else
            {
                failed.fail_message = true;
                TEST_EXPECT(ctx, (client->channel->to_module.post(filler) == host::EModulePipePost::published));
            }
            Access::pump(runtime);
            TEST_EXPECT(ctx, (Access::emergency(runtime) && client->stop_requested && other->stop_requested));
            for (unsigned turn = 0u; turn < 8u; ++turn) Access::pump(runtime);
            TEST_EXPECT(ctx, (failed.stopping && unaffected.stopping && !failed.destroyed && !unaffected.destroyed));
            failed.allow_completion = unaffected.allow_completion = true;
            for (unsigned turn = 0u; turn < 48u; ++turn) Access::pump(runtime);
            TEST_EXPECT(ctx, ((failed.destroyed == 1u) && (unaffected.destroyed == 1u) && client->preamble->failed()));
            TEST_EXPECT(ctx, ((failed.created_on == failed.destroyed_on) && (unaffected.created_on == unaffected.destroyed_on)));
            TEST_EXPECT(ctx, Access::retire(runtime, target));
            TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
            Access::clear(runtime);
        }
    }
    //  Ordinary replacement closes only the outgoing Executive and preserves
    //  other modules. A new instance can then run without fatal state leaking.
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    SFailureExecution unaffected, old, replacement;
    SFailureExecution::next = &unaffected;
    host::SClientRecord* const other = Access::driver(runtime, module_ids::audio, binding, execution);
    SFailureExecution::next = &old;
    host::SClientRecord* const outgoing = Access::driver(runtime, module_ids::executive, binding, execution);
    TEST_EXPECT(ctx, (other && outgoing));
    Access::pump(runtime);
    const modules::SModuleInstance previous = outgoing->instance;
    Access::replace(runtime, module_ids::executive);
    for (unsigned turn = 0u; turn < 8u; ++turn) Access::pump(runtime);
    TEST_EXPECT(ctx, (old.stopping && !old.destroyed && !other->stop_requested && !Access::emergency(runtime)));
    old.allow_completion = true;
    for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
    TEST_EXPECT(ctx, Access::retire(runtime, module_ids::executive));
    SFailureExecution::next = &replacement;
    host::SClientRecord* const incoming = Access::driver(runtime, module_ids::executive, binding, execution);
    TEST_EXPECT(ctx, (incoming && (incoming->instance.value != previous.value)));
    Access::pump(runtime);
    TEST_EXPECT(ctx, (incoming->preamble->ready() && !other->stop_requested && !Access::emergency(runtime)));
    replacement.allow_completion = unaffected.allow_completion = true;
    Access::replace(runtime, module_ids::executive);
    Access::replace(runtime, module_ids::audio);
    for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
    TEST_EXPECT(ctx, ((replacement.destroyed == 1u) && (old.destroyed == 1u) && (unaffected.destroyed == 1u)));
    TEST_EXPECT(ctx, Access::retire(runtime, module_ids::executive));
    TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
    Access::clear(runtime);
}

void replacement_result_retention(tests::TTestContext& ctx)
{
    using Access = host::SModulePreambleTestAccess;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    SFailureExecution successor;
    successor.allow_completion = successor.participate = true;
    SFailureExecution::next = &successor;
    host::SClientRecord* const client = Access::driver(runtime, module_ids::audio, binding, SFailureExecution::descriptor());
    tests::SCommonEndpointFixture requester{ module_ids::executive, 700u, 32u };
    host::CAssetService& assets = Access::assets(runtime);
    TEST_EXPECT(ctx, (client && assets.initialise() && Access::prepare_turns(runtime)));
    if (!client)
    {
        Access::clear(runtime);
        return;
    }
    Access::pump(runtime);
    Access::pump(runtime);
    TEST_EXPECT(ctx, (client->preamble->ready() && (client->direct_connections == host::EDirectConnectionState::available)));
    threading::CErasedOwnerMsg transfer = threading::make_owner_message<RawAssetTransfer>(91);
    TEST_EXPECT(ctx, transfer.owner().payload<RawAssetTransfer>()->storage.value.resize(16u));
    transfer.owner().add_hazard(mount_point_ids::audio);
    {
        host::CRetainedReplies::CDispatch dispatch{ *requester.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && requester.reserve_replies(1u)));
        assets.request(transfer, requester);
    }
    assets.retry_output(requester);
    AssetResult transferred;
    TEST_EXPECT(ctx, ((requester.pending() == 1u) && requester.received(0).copy_payload_to(transferred) &&
        (transferred.status == EAssetStatus::success) && transferred.asset.is_valid()));
    requester.discard(requester.pending());
    const auto filler = threading::make_pod_message(-1, services::OpenServiceResult{});
    for (unsigned index = 0u; index < 32u; ++index)
    {
        TEST_EXPECT(ctx, (requester.channel->to_module.post(filler) == host::EModulePipePost::published));
    }
    TEST_EXPECT(ctx, requester.reserve_replies(1u));
    // Inject the completed replacement boundary after its successor is active.
    // The actual Host turn must not interpret this retained request as new work.
    Access::completed_replacement(runtime, requester, binding, module_ids::audio);
    host::CModuleService& service = Access::module_service(runtime);
    for (unsigned turn = 0u; turn < 12u; ++turn)
    {
        Access::turn(runtime);
        service.retry_output(); // Controlled recipient is outside the module registry.
        host::SClientRecord* const current = Access::endpoint(runtime, system_ids::ops::make_system_id(module_ids::audio, thread_ids::host));
        TEST_EXPECT(ctx, ((current == client) && current && !current->stop_requested &&
            !current->endpoint->module_state->stopping.load() &&
            (current->direct_connections == host::EDirectConnectionState::available)));
        TEST_EXPECT(ctx, (service.output_ready(requester) && !service.is_in_flight() &&
            (requester.reserved_replies == 1u) && !successor.stopping && !successor.destroyed && !Access::emergency(runtime)));
    }
    TEST_EXPECT(ctx, (requester.pending() == 32u));
    requester.discard(32u);
    service.retry_output();
    service.retry_output();
    modules::ModuleResult completed;
    TEST_EXPECT(ctx, ((requester.pending() == 1u) && (requester.received(0).query_async_slot() == 93) &&
        requester.received(0).copy_payload_to(completed) && (completed.status == modules::EModuleStatus::success) &&
        (completed.module == module_ids::audio) && service.is_idle() && !requester.reserved_replies));
    requester.discard(requester.pending());
    // A second dependency-disposal pass would have invalidated this asset.
    {
        host::CRetainedReplies::CDispatch dispatch{ *requester.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && requester.reserve_replies(1u)));
        assets.request_disposal({ transferred.asset }, 94, requester);
    }
    assets.complete_disposals();
    assets.retry_output(requester);
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, ((requester.pending() == 1u) && requester.received(0).copy_payload_to(disposed) &&
        (disposed.status == EAssetStatus::success)));
    requester.discard(requester.pending());
    if (Access::endpoint(runtime, system_ids::ops::make_system_id(module_ids::audio, thread_ids::host)))
    {
        Access::replace(runtime, module_ids::audio);
        for (unsigned turn = 0u; turn < 16u; ++turn) Access::pump(runtime);
        TEST_EXPECT(ctx, Access::retire(runtime, module_ids::audio));
    }
    TEST_EXPECT(ctx, ((successor.destroyed == 1u) && (successor.created_on == successor.destroyed_on)));
    assets.detach_client(requester);
    Access::clear(runtime);
}

struct SReportFixtureFacade
{
    modules::ESubmission read_status{ modules::ESubmission::busy }, release_status{ modules::ESubmission::busy };
    unsigned reads{}, releases{}, failures{}, read_attempts{}, release_attempts{};
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SReportFixtureFacade& self = *static_cast<SReportFixtureFacade*>(cookie);
        if (destination.kind != modules::SDestination::EKind::host)
        {
            return modules::ESubmission::invalid;
        }
        if (message.is_payload_a<platform::reports::PlatformReportCaptureFailed>())
        {
            ++self.failures;
            return modules::ESubmission::accepted;
        }
        if (message.is_payload_a<platform::reports::PlatformReportRequest>())
        {
            ++self.read_attempts;
            if (modules::accepted(self.read_status)) ++self.reads;
            return self.read_status;
        }
        AssetReleaseRequest release;
        if (message.copy_payload_to(release) && (release.acquisition.query_value() == 91u))
        {
            ++self.release_attempts;
            if (modules::accepted(self.release_status)) ++self.releases;
            return self.release_status;
        }
        return modules::ESubmission::invalid;
    }
};

//  Exercise the rebuilt acceptance DLL with controlled Host replies. Its native
//  window remains idle; this isolates fixture report ownership from native input.
void platform_fixture_report_stop(tests::TTestContext& ctx)
{
#if MV_PLATFORM_WINDOWS
    char previous[512]{};
    std::size_t previous_size{};
    TEST_EXPECT(ctx, (getenv_s(&previous_size, previous, sizeof(previous), "MORPHIC_PUMP_CASE") == 0));
    TEST_EXPECT(ctx, (_putenv_s("MORPHIC_PUMP_CASE", "platform-report-injected") == 0));
    TInstance<debug_system::CDebugServiceState> debug_service = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, debug_service.is_ready());
    if (!debug_service)
    {
        (void)_putenv_s("MORPHIC_PUMP_CASE", previous);
        return;
    }
    const std::string log = test_environment::test_log_path("PlatformFixtureStop");
    const std::string direct = test_environment::test_log_path("PlatformFixtureStop_direct");
    TEST_EXPECT(ctx, (debug_service->configure_log_paths(log.c_str(), direct.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "fixture_stop" })));
    SDllAllocation attribution;
    memory::CMemoryAllocator allocator{ &attribution, &SDllAllocation::allocate, &SDllAllocation::free, system_ids::ops::make_system_id(module_ids::platform, thread_ids::host) };
    memory::CMemoryContext module_memory{ allocator, system_ids::ops::make_system_id(module_ids::platform, thread_ids::host) };
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string filename = test_environment::binary_path(test_environment::module_filename("MorphicInputPlatform").c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
    const bool installed = path.is_ready() && binding.bind(path, module_ids::platform, host_identity) &&
        binding.install(test_environment::system_registry_view(), module_ids::platform, &module_memory,
            system_context::sequence_counter, debug_service.operator->());
    TEST_EXPECT(ctx, installed);
    if (installed)
    {
        modules::SExecutionV1 execution;
        TEST_EXPECT(ctx, (binding.query_execution(execution) == modules::EBindingResult::success));
        static const modules::SCommunicationV1 functions{ &SReportFixtureFacade::pod };
        for (const modules::ESubmission accepted : { modules::ESubmission::busy,
            modules::ESubmission::accepted, modules::ESubmission::published_wake_failed })
        {
            SReportFixtureFacade state;
            modules::CCommunication communication{ functions, &state };
            modules::CHostRestrictionPublication restrictions;
            modules::SHostExecutionContext host_execution;
            host_execution.restriction = restrictions.reader();
            modules::SExecutionContext context;
            context.instance = { 1u };
            context.communication = &communication;
            context.execution = &host_execution;
            void* runtime = nullptr;
            TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::host, &context));
            TEST_EXPECT(ctx, (execution.create(&context, &runtime) == modules::EExecutionStatus::success));
            if (!runtime) continue;
            modules::SExecutionProgress progress;
            const auto invoke = [&](const modules::EExecutionEvent event, const threading::CErasedPodMsg* const message = nullptr)
            {
                modules::SInvocation invocation;
                invocation.event = event;
                invocation.message = message;
                invocation.time_budget_ns = 2000000u;
                TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::host, &context));
                TEST_EXPECT(ctx, (execution.handle(runtime, &invocation, &progress) == modules::EExecutionStatus::success));
            };
            invoke(modules::EExecutionEvent::start);
            //  An invalid capture deadline yields a deterministic failure outcome.
            //  The supplied published receipt drives the fixture's independent read.
            const platform::reports::SCaptureIdentity identity{ context.instance, 1u };
            const auto capture = threading::make_pod_message(-1, platform::reports::PlatformReportCaptureRequest{ identity, {} });
            invoke(modules::EExecutionEvent::message, &capture);
            invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (state.failures == 1u));
            const auto receipt = threading::make_pod_message(-1,
                platform::reports::PlatformReportCaptureReceipt{ identity, platform::reports::EDisposition::published });
            invoke(modules::EExecutionEvent::message, &receipt);
            for (unsigned attempt = 0u; attempt < 3u; ++attempt) invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, ((state.read_attempts == 3u) && !state.reads && !progress.work_pending));
            state.read_status = accepted;
            invoke(modules::EExecutionEvent::service_async);
            invoke(modules::EExecutionEvent::stop);
            const unsigned stopped_attempts = state.read_attempts;
            for (unsigned attempt = 0u; attempt < 3u; ++attempt) invoke(modules::EExecutionEvent::service_async);
            TEST_EXPECT(ctx, (state.read_attempts == stopped_attempts));
            if (accepted == modules::ESubmission::busy)
            {
                TEST_EXPECT(ctx, (progress.work_complete && !state.reads && !state.releases));
            }
            else
            {
                TEST_EXPECT(ctx, ((state.reads == 1u) && !progress.work_complete && !progress.work_pending));
                const std::uint8_t byte = 1u;
                platform::reports::PlatformReportResult result;
                result.status = platform::reports::EStatus::success;
                result.acquisition = CAssetAcquisitionId{ 91u };
                result.bytes = { &byte, 1u };
                const auto acquired = threading::make_pod_message(710, result);
                invoke(modules::EExecutionEvent::message, &acquired);
                for (unsigned attempt = 0u; attempt < 3u; ++attempt) invoke(modules::EExecutionEvent::service_async);
                TEST_EXPECT(ctx, ((state.release_attempts == 3u) && !state.releases && !progress.work_complete && !progress.work_pending));
                state.release_status = accepted;
                for (unsigned attempt = 0u; attempt < 3u; ++attempt) invoke(modules::EExecutionEvent::service_async);
                TEST_EXPECT(ctx, ((state.releases == 1u) && !progress.work_complete && !progress.work_pending));
                const auto released = threading::make_pod_message(711, AssetReleaseResult{ result.acquisition, EAssetStatus::success });
                invoke(modules::EExecutionEvent::message, &released);
                invoke(modules::EExecutionEvent::service_async);
                TEST_EXPECT(ctx, (progress.work_complete && (state.reads == 1u) && (state.releases == 1u)));
            }
            TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::host, &context));
            TEST_EXPECT(ctx, (execution.destroy(runtime) == modules::EExecutionStatus::success));
            TEST_EXPECT(ctx, module_memory.is_attribution_empty());
        }
        TEST_EXPECT(ctx, binding.unbind());
    }
    TEST_EXPECT(ctx, (attribution.frees == attribution.allocations));
    TEST_EXPECT(ctx, debug_service->stop());
    TEST_EXPECT(ctx, (_putenv_s("MORPHIC_PUMP_CASE", previous) == 0));
#else
    (void)ctx;
#endif
}

void rejected_execution_roles(tests::TTestContext& ctx)
{
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    for (std::uint32_t mode = 0u; mode < 2u; ++mode)
    {
        threading::CEndpointResources resources{ module_ids::executable, module_ids::audio, thread_ids::audio };
        host::CModuleChannel channel{ resources };
        modules::SExecutionV1 execution;
        if (mode == 1u)
        {
            execution.create = &CPeerExecution::create;
            execution.handle = &CPeerExecution::dispatch;
            execution.destroy = &CPeerExecution::destroy;
            execution.flags = modules::k_execution_native_events;
        }
        host::CModulePreamble preamble{ resources, channel, binding, execution, *memory::get_ambient_memory_context() };
        TEST_EXPECT(ctx, !preamble.activate());
        TEST_EXPECT(ctx, (preamble.failed() && preamble.destroyed() && !preamble.retained() && !preamble.work_pending()));
    }
}

void setup_allocation_rollback(tests::TTestContext& ctx)
{
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    std::size_t rejected = 0u;
    bool reached_success = false;
    for (std::size_t failure = 0u; failure < 24u; ++failure)
    {
        tests::TAllocationFailureFixture allocation;
        memory::CMemoryAllocator allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ allocator, system_ids::host };
        {
            tests::TMemoryContextScope scope{ &memory };
            host::SClientRecord client{ module_ids::audio, 98u, nullptr, { 97u } };
            allocation.fail_on = failure;
            const bool ready = client.initialise_execution(binding, {}, nullptr, 32u, 3u);
            if (ready)
            {
                reached_success = true;
                TEST_EXPECT(ctx, (client.channel && client.pending_replies && client.preamble && !allocation.failed));
            }
            else
            {
                ++rejected;
                TEST_EXPECT(ctx, (allocation.failed && !client.channel && !client.pending_replies && !client.preamble));
                TEST_EXPECT(ctx, (memory.get_live_allocation_count() == 0u));
                client.phase = host::EClientPhase::stopped;
                client.incoming_drained = client.terminal_before_drain = true;
                TEST_EXPECT(ctx, (client.execution_quiescent() && client.replies_idle()));
                client.flush();
                TEST_EXPECT(ctx, (client.endpoint->notification_pins == 0u));
            }
        }
        TEST_EXPECT(ctx, memory.is_attribution_empty());
        if (reached_success) break;
    }
    TEST_EXPECT(ctx, (reached_success && (rejected >= 7u))); //  Channel, all four rings, replies and preamble.

    rejected = 0u;
    bool reached_native_create = false;
    for (std::size_t failure = 0u; failure < 32u; ++failure)
    {
        tests::TAllocationFailureFixture allocation;
        memory::CMemoryAllocator fault_allocator{ &allocation, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext memory{ fault_allocator, system_ids::host };
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        host::CHost runtime{ allocator };
        host::SClientRecord* const requester = host::SModulePreambleTestAccess::install(runtime, module_ids::audio, binding);
        TEST_EXPECT(ctx, requester);
        if (!requester)
        {
            host::SModulePreambleTestAccess::clear(runtime);
            return;
        }
        CPeerExecution state;
        modules::SExecutionV1 execution;
        execution.create = &CPeerExecution::create;
        execution.handle = &CPeerExecution::dispatch;
        execution.destroy = &CPeerExecution::destroy;
        const modules::SChildExecution request{ &execution, &state, thread_ids::audio };
        {
            tests::TMemoryContextScope scope{ &memory };
            allocation.fail_on = failure;
            host::SModulePreambleTestAccess::failed_child(runtime, *requester, request);
        }
        for (std::uint32_t turn = 0u; turn < 4u; ++turn) host::SModulePreambleTestAccess::progress_threads(runtime);
        host::SLifecycleMessage result;
        TEST_EXPECT(ctx, requester->channel->read_preamble_control(result));
        TEST_EXPECT(ctx, ((result.operation == host::ELifecycle::child_created) && (result.correlation == 81) &&
            (result.notification.status != services::EServiceStatus::success) && !result.notification.first.value));
        TEST_EXPECT(ctx, !requester->channel->read_preamble_control(result));
        TEST_EXPECT(ctx, host::SModulePreambleTestAccess::children_idle(runtime, module_ids::audio));
        TEST_EXPECT(ctx, (!state.created_on.is_valid() && (state.destroyed == 0u)));
        if (allocation.failed) ++rejected;
        else reached_native_create = true; //  The seam refused native launch after complete setup.
        host::SModulePreambleTestAccess::clear(runtime);
        TEST_EXPECT(ctx, memory.is_attribution_empty());
        if (reached_native_create) break;
    }
    TEST_EXPECT(ctx, (reached_native_create && (rejected >= 9u)));
}

void host_output_admission_turn(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    host::SClientRecord* const provider = host::SModulePreambleTestAccess::install(runtime, module_ids::rendering, binding);
    TEST_EXPECT(ctx, (client && provider));
    if (!client || !provider)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    host::CServiceRouter& router = host::SModulePreambleTestAccess::router(runtime);
    const services::SServiceContract contract{ 1u, 1u };
    const host::SServiceProvider advertisement{ provider, &contract, 1u };
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(1u)));
        router.open({ module_ids::rendering, 1u, 1u, { 1u } }, 0, *client, &advertisement);
    }
    threading::CErasedPodMsg message;
    services::OpenServiceResult opened;
    TEST_EXPECT(ctx, (client->channel->read_preamble_pod(message) && message.copy_payload_to(opened)));
    TEST_EXPECT(ctx, (opened.status == services::EServiceStatus::success));
    for (std::uint64_t index = 1u; index <= 12u; ++index)
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 2u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(2u)));
        services::ServiceRequest request;
        request.route = opened.route;
        request.request = { index };
        request.opcode = 1u;
        router.request(request, static_cast<std::int32_t>(index), *client);
    }
    TEST_EXPECT(ctx, ((client->reserved_replies == 12u) && (client->pending_replies->reservations() == 0u)));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    (void)host::SModulePreambleTestAccess::drain(runtime);
    for (std::uint64_t index = 1u; index <= 12u; ++index)
    {
        services::ServiceCommand command;
        TEST_EXPECT(ctx, (provider->channel->read_preamble_pod(message) && message.copy_payload_to(command)));
        TEST_EXPECT(ctx, (command.descriptor && (command.descriptor->command.request.value == index)));
        services::ServiceReply reply;
        reply.route = opened.route;
        reply.request = { index };
        reply.opcode = 1u;
        TEST_EXPECT(ctx, (router.complete(*provider, reply) == services::EServiceStatus::success));
    }
    for (std::int32_t index = 12; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(index, services::OpenServiceResult{})));
    }
    TEST_EXPECT(ctx, (client->channel->to_host.post(threading::make_pod_message(99,
        services::OpenServiceRequest{ module_ids::audio, 1u, 1u, { 1u } })) == host::EModulePipePost::published));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    TEST_EXPECT(ctx, (client->pod_head_live && (client->accounted_pods == 0u)));
    for (std::uint32_t turn = 0u; turn < 8u; ++turn)
    {
        TEST_EXPECT(ctx, client->channel->read_preamble_pod(message));
        (void)host::SModulePreambleTestAccess::drain(runtime);
        TEST_EXPECT(ctx, (client->accounted_pods == ((turn == 7u) ? 1u : 0u)));
    }
    TEST_EXPECT(ctx, (router.output_ready(*client) && (client->reserved_replies == 4u)));
    TEST_EXPECT(ctx, ((client->pending_replies->size() == 1u) && (client->pending_replies->reservations() == 0u)));
    while (client->channel->read_preamble_pod(message)) {}
    (void)host::SModulePreambleTestAccess::drain(runtime);
    std::uint32_t results = 0u;
    while (client->channel->read_preamble_pod(message))
    {
        services::ServiceReply result;
        if (message.copy_payload_to(result))
        {
            ++results;
            TEST_EXPECT(ctx, (result.request.value == (8u + results)));
        }
        else TEST_EXPECT(ctx, (message.query_async_slot() == 99));
    }
    TEST_EXPECT(ctx, ((results == 4u) && (client->reserved_replies == 0u) && client->pending_replies->idle()));
    TEST_EXPECT(ctx, !router.failed());
    host::SModulePreambleTestAccess::clear(runtime);
}

void operation_owned_outputs(tests::TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::executive, binding);
    host::CAssetService& assets = host::SModulePreambleTestAccess::assets(runtime);
    TEST_EXPECT(ctx, (client && assets.initialise()));
    if (!client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(-1, services::OpenServiceResult{})));
    }
    const auto request = [&](const std::int32_t correlation)
    {
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(1u)));
        threading::CErasedOwnerMsg message = threading::make_owner_message<AssetLoadRequest>(correlation);
        TEST_EXPECT(ctx, message.has_owner());
        assets.request(message, *client); //  Empty filename fixes one terminal result in its operation.
    };
    request(1);
    request(2);
    request(3);
    TEST_EXPECT(ctx, (assets.output_ready(*client) && !assets.work_idle()));
    TEST_EXPECT(ctx, (client->pending_replies->idle() && (client->reserved_replies == 3u)));
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime));
    threading::CErasedPodMsg message;
    for (std::uint32_t turn = 0u; turn < 4u; ++turn)
    {
        TEST_EXPECT(ctx, client->channel->read_preamble_pod(message));
        (void)host::SModulePreambleTestAccess::drain(runtime);
        if (turn == 0u) request(4); //  Reuses a low slot; surviving older operations still rotate.
    }
    std::int32_t result_count = 0;
    while (client->channel->read_preamble_pod(message))
    {
        AssetResult result;
        if (!message.copy_payload_to(result)) continue;
        ++result_count;
        TEST_EXPECT(ctx, (message.query_async_slot() == result_count));
        TEST_EXPECT(ctx, (result.status == EAssetStatus::invalid_request));
    }
    TEST_EXPECT(ctx, ((result_count == 4) && assets.work_idle() && !assets.output_ready(*client)));
    TEST_EXPECT(ctx, (client->pending_replies->idle() && (client->reserved_replies == 0u) && !assets.failed()));
    TEST_EXPECT(ctx, client->reserve_replies(1u));
    TEST_EXPECT(ctx, !client->reply(threading::make_pod_message(99, services::OpenServiceResult{})));
    TEST_EXPECT(ctx, ((client->reserved_replies == 1u) &&
        (client->endpoint->failure.load(std::memory_order_acquire) == threading::EEndpointFailure::accounting_failure)));
    host::SModulePreambleTestAccess::clear(runtime);
}

void report_owned_outputs(tests::TTestContext& ctx)
{
    using namespace platform::reports;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    host::SClientRecord* const client = host::SModulePreambleTestAccess::install(runtime, module_ids::platform, binding);
    host::CAssetService& assets = host::SModulePreambleTestAccess::assets(runtime);
    TEST_EXPECT(ctx, (client && assets.initialise()));
    if (!client)
    {
        host::SModulePreambleTestAccess::clear(runtime);
        return;
    }
    for (std::int32_t index = 0; index < 32; ++index)
    {
        TEST_EXPECT(ctx, client->post(threading::make_pod_message(-1, services::OpenServiceResult{})));
    }
    assets.install_report_provider(*client);
    assets.progress_reports(false);
    TEST_EXPECT(ctx, (assets.report_output_ready(*client) && (client->reserved_replies == 3u)));
    TEST_EXPECT(ctx, !host::SModulePreambleTestAccess::drain(runtime));
    TEST_EXPECT(ctx, (client->pending_replies->idle() && !assets.failed()));
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, client->channel->read_preamble_pod(message));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    PlatformReportCaptureRequest capture;
    std::uint32_t requests = 0u;
    while (client->channel->read_preamble_pod(message))
    {
        if (message.copy_payload_to(capture)) ++requests;
    }
    TEST_EXPECT(ctx, ((requests == 1u) && capture.capture.capture_id && (client->reserved_replies == 2u)));
    assets.report_failed(*client, { capture.capture, { EStage::capture, EReason::native_failure } });
    assets.progress_reports(false);
    TEST_EXPECT(ctx, assets.report_output_ready(*client));
    (void)host::SModulePreambleTestAccess::drain(runtime);
    PlatformReportCaptureReceipt receipt;
    TEST_EXPECT(ctx, (client->channel->read_preamble_pod(message) && message.copy_payload_to(receipt)));
    TEST_EXPECT(ctx, ((receipt.capture.capture_id == capture.capture.capture_id) && (receipt.disposition == EDisposition::rejected)));
    assets.progress_reports(false);
    TEST_EXPECT(ctx, ((client->reserved_replies == 0u) && !assets.report_output_ready(*client) && !assets.failed()));

    //  A later capture cancelled before physical publication never reaches the
    //  module and needs neither a module cancellation nor a transfer receipt.
    assets.install_report_provider(*client);
    assets.progress_reports(false);
    TEST_EXPECT(ctx, (client->reserved_replies == 3u));
    assets.close_report_provider(*client);
    assets.progress_reports(true);
    TEST_EXPECT(ctx, ((client->reserved_replies == 0u) && !client->channel->to_module.readable_count() && !assets.failed()));
    host::SModulePreambleTestAccess::clear(runtime);
}

void serviced_driver(tests::TTestContext& ctx)
{
    platform::threading::COwnerWait wake;
    TEST_EXPECT(ctx, (wake.initialise().code == platform::threading::EWaitError::none));
    threading::ThreadConfig config;
    config.thread_id = thread_ids::executive;
    config.worker_module_id = module_ids::executive;
    threading::CThreadPackage package{ config, {} };
    TEST_EXPECT(ctx, package.install_owner_wake(wake.endpoint()));
    threading::CThreadResources& resources = package.resources();
    resources.identity = { 582u };
    resources.instance = { 992u };
    host::CModuleChannel channel{ resources };
    TEST_EXPECT(ctx, channel.initialise(*memory::get_ambient_memory_context(), 32u, 32u));
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    modules::SExecutionV1 execution;
    execution.create = &SExecutionFixture::create;
    execution.handle = &SExecutionFixture::handle;
    execution.destroy = &SExecutionFixture::destroy;
    SExecutionFixture state;
    host::CModulePreamble preamble{ resources, channel, binding, execution, *memory::get_ambient_memory_context(), &state };
    TEST_EXPECT(ctx, package.install_preamble(preamble));
    const bool started = package.startup();
    TEST_EXPECT(ctx, started);
    if (!started) return;
    threading::CWaitDeadline timeout;
    TEST_EXPECT(ctx, (timeout.set_after_milliseconds(5000u).code == platform::threading::EWaitError::none));
    std::uint32_t received = 0u;
    bool stopped = false, closed = false, sealed = false, valid = true;
    while (!package.execution_ended())
    {
        host::SLifecycleMessage control;
        while (channel.read_host_control(control))
        {
            if (control.operation == host::ELifecycle::close_provider_admission)
            {
                closed = true;
                valid = valid && (control.frontier.pods == 33u);
                control = {};
                control.operation = host::ELifecycle::provider_admission_closed;
                control.frontier.host_output = 2u;
                valid = valid && channel.host_control(control);
                for (std::int32_t index = 0; index < 2; ++index)
                {
                    valid = valid && (channel.to_module.post(threading::make_pod_message(index, services::OpenServiceResult{})) == host::EModulePipePost::published);
                }
                resources.signal_consumer();
            }
            else if (control.operation == host::ELifecycle::seal)
            {
                sealed = true;
                valid = valid && (control.frontier.pods == 33u);
                control = {};
                control.operation = host::ELifecycle::sealed;
                valid = valid && channel.host_control(control);
            }
            else valid = valid && (control.operation == host::ELifecycle::ready);
        }
        threading::CErasedPodMsg message;
        while (channel.read_host_pod(message)) ++received;
        if (!stopped && (received == 33u) && state.self_complete.load(std::memory_order_acquire))
        {
            stopped = true;
            package.request_exit();
        }
        std::uint64_t remaining = 0u;
        if ((timeout.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            valid = false;
            package.request_exit();
            break;
        }
        threading::CWaitDeadline next;
        (void)next.set_after_milliseconds(1u);
        (void)wake.wait(next);
    }
    TEST_EXPECT(ctx, (valid && stopped && closed && sealed && package.execution_ended()));
    //  Join publishes the fixture's non-atomic callback state to this owner.
    TEST_EXPECT(ctx, package.shutdown());
    TEST_EXPECT(ctx, ((state.self_count == 100u) && (state.host_count == 2u) && (state.admission_count == 1u)));
    TEST_EXPECT(ctx, (state.foreign_rejected && state.valid_sources && (state.destroy_count == 1u)));
    TEST_EXPECT(ctx, ((state.created_on == state.destroyed_on) &&
        (state.created_on != platform::threading::query_current_thread_id())));
    TEST_EXPECT(ctx, (preamble.destroyed() && !preamble.failed()));
}

}   //  namespace

void run_module_preamble_tests(tests::TTestContext& ctx)
{
    run_lifecycle_fixture_tests(ctx);
    execution_boundary(ctx);
    provider_facade_requests(ctx);
    provider_pending_lifetimes(ctx);
    pressure_diagnostics(ctx);
    executive_timing_policy(ctx);
    pressure_and_fairness(ctx);
    bounded_storage(ctx);
    retained_reply_storage(ctx);
    finite_output_turns(ctx);
    common_dispatch_and_frontiers(ctx);
    host_retained_ingress(ctx);
    fallback_continuation_and_terminal(ctx);
    host_private_frontiers(ctx);
    private_connection_activation(ctx);
    provider_borrow_destruction_gate(ctx);
    standard_coordinator_child(ctx);
    standard_coordinator_controls(ctx);
    global_failure_shutdown(ctx);
    pumped_destroy_refusal(ctx);
    run_pump_fixture_tests(ctx);
    run_input_fixture_tests(ctx);
    replacement_result_retention(ctx);
    module_dll_execution(ctx, module_ids::audio, "MorphicAudio", thread_ids::audio);
    module_dll_execution(ctx, module_ids::rendering, "MorphicRendering", thread_ids::rendering);
    module_dll_execution(ctx, module_ids::platform, "MorphicPlatform", thread_ids::host);
    executive_dll_retries(ctx);
    rejected_execution_roles(ctx);
    setup_allocation_rollback(ctx);
    host_output_admission_turn(ctx);
    operation_owned_outputs(ctx);
    report_owned_outputs(ctx);
    serviced_driver(ctx);
}

int run_module_preamble_focused()
{
    tests::TTestContext ctx;
    run_module_preamble_tests(ctx);
    return ctx.report("ModulePreamble");
}

void run_common_bulk_connection_tests(tests::TTestContext& ctx)
{
    common_bulk_buffers(ctx, false, false);
    common_bulk_buffers(ctx, true, false);
    common_bulk_buffers(ctx, false, true);
    common_bulk_buffers(ctx, true, true);
    common_bulk_configuration(ctx, false);
    common_bulk_configuration(ctx, true);
    common_bulk_validation(ctx);
    common_bulk_allocation(ctx);
    common_bulk_retained_closure(ctx, false);
    common_bulk_retained_closure(ctx, true);
}

int run_common_bulk_focused()
{
    tests::TTestContext ctx;
    run_common_bulk_connection_tests(ctx);
    return ctx.report("CommonBulk");
}

void run_common_establishment_tests(tests::TTestContext& ctx)
{
    for (const bool bulk : { false, true })
    {
        common_partial_installation(ctx, bulk);
        common_activation_and_timeout(ctx, false, bulk);
        common_activation_and_timeout(ctx, true, bulk);
    }
    common_request_conflicts(ctx);
    common_delayed_request(ctx, false, false);
    common_delayed_request(ctx, true, false);
    common_delayed_request(ctx, true, true);
    common_activation_pressure(ctx);
}

int run_common_establishment_focused()
{
    tests::TTestContext ctx;
    run_common_establishment_tests(ctx);
    return ctx.report("CommonEstablishment");
}

void run_common_pod_connection_tests(tests::TTestContext& ctx)
{
    common_pod_exchange(ctx);
    common_pod_pressure(ctx, false);
    common_pod_pressure(ctx, true);
    common_pod_wake_failure(ctx, false);
    common_pod_wake_failure(ctx, true);
    common_retirement_admission(ctx);
    preamble_connection_cancellation(ctx); // Repeated data/capacity clear-and-wake regression.
}

int run_common_pod_focused()
{
    tests::TTestContext ctx;
    run_common_pod_connection_tests(ctx);
    return ctx.report("CommonPod");
}

void run_common_native_join_tests(tests::TTestContext& ctx)
{
    common_native_join_retention(ctx, false);
    common_native_join_retention(ctx, true);
}

int run_common_native_join_focused()
{
    tests::TTestContext ctx;
    run_common_native_join_tests(ctx);
    return ctx.report("CommonNativeJoin");
}

void run_common_multi_connection_tests(tests::TTestContext& ctx)
{
    common_endpoint_pairings(ctx);
    common_explicit_consent(ctx, false);
    common_explicit_consent(ctx, true);
    common_requester_limit(ctx, false);
    common_requester_limit(ctx, true);
    common_connection_limits(ctx, false);
    common_connection_limits(ctx, true);
    common_result_after_retirement(ctx);
    common_reused_correlation(ctx);
    common_authenticated_selection(ctx, false);
    common_authenticated_selection(ctx, true);
    common_close_during_signal(ctx, false);
    common_close_during_signal(ctx, true);
    common_deferred_quiescence(ctx);
    common_address_replacement(ctx);
}

int run_common_multi_focused()
{
    tests::TTestContext ctx;
    run_common_multi_connection_tests(ctx);
    return ctx.report("CommonMulti");
}

void run_provider_facade_tests(tests::TTestContext& ctx)
{
    provider_facade_requests(ctx);
    provider_pending_lifetimes(ctx);
}

void run_standard_coordinator_tests(tests::TTestContext& ctx)
{
    standard_coordinator_controls(ctx);
    standard_coordinator_child(ctx);
}

int run_platform_execution_focused()
{
    tests::TTestContext ctx;
    module_dll_execution(ctx, module_ids::platform, "MorphicPlatform", thread_ids::host);
    platform_fixture_report_stop(ctx);
    return ctx.report("PlatformExecution");
}

int run_lifecycle_executive_focused()
{
    tests::TTestContext ctx;
    run_lifecycle_executive_fixture_tests(ctx);
    replacement_result_retention(ctx);
    return ctx.report("LifecycleExecutive");
}

int run_lifecycle_service_focused()
{
    tests::TTestContext ctx;
    run_lifecycle_service_fixture_tests(ctx);
    module_dll_execution(ctx, module_ids::conditioning_general, "MorphicLifecycleService", thread_ids::simulation);
    return ctx.report("LifecycleService");
}

int run_audio_execution_focused()
{
    tests::TTestContext ctx;
    module_dll_execution(ctx, module_ids::audio, "MorphicAudio", thread_ids::audio);
    return ctx.report("AudioExecution");
}

int run_rendering_execution_focused()
{
    tests::TTestContext ctx;
    module_dll_execution(ctx, module_ids::rendering, "MorphicRendering", thread_ids::rendering);
    return ctx.report("RenderingExecution");
}

int run_execution_failure_focused()
{
    tests::TTestContext ctx;
    global_failure_shutdown(ctx);
    return ctx.report("ExecutionFailure");
}

int run_destroy_refusal_focused()
{
    tests::TTestContext ctx;
    pumped_destroy_refusal(ctx);
    return ctx.report("DestroyRefusal");
}

namespace
{
//  The test's Host thread exchanges commands and observations with the real
//  Executive service thread. Only that service thread accesses the bound facade.
struct SControlledServicePeer
{
    CPeerExecution peer;
    system_ids::id_type target{ system_ids::ops::make_system_id(module_ids::audio, thread_ids::audio) };
    threading::CErasedPodMsg outgoing;
    threading::CErasedPodMsg observations[host::k_module_pod_capacity + 16u];
    std::atomic<bool> outbound_pending{ false }, connect_requested{ false }, close_requested{ false };
    std::atomic<bool> pause_input{ false }, pause_observed{ false }, active{ false }, participated{ false };
    std::atomic<connections::EStatus> result{ connections::EStatus::busy };
    std::atomic<unsigned> received{ 0u }, retired{ 0u };

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        auto& self = *static_cast<SControlledServicePeer*>(context->creation_context);
        self.peer.communication = *context->communication;
        self.peer.address = context->address;
        self.peer.peer = self.target;
        self.peer.created_on = platform::threading::query_current_thread_id();
        self.peer.observations = self.observations;
        self.peer.observation_capacity = host::k_module_pod_capacity + 16u;
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        auto& self = *static_cast<SControlledServicePeer*>(runtime);
        if (self.connect_requested.exchange(false, std::memory_order_acq_rel))
        {
            self.peer.requested = self.peer.closing = self.peer.quiescent = false;
            self.peer.request = true;
            self.peer.result = connections::EStatus::busy;
        }
        if (self.close_requested.exchange(false, std::memory_order_acq_rel)) self.peer.close = true;
        const bool outbound_pending = self.outbound_pending.load(std::memory_order_acquire);
        if (outbound_pending) self.peer.outgoing = &self.outgoing;
        self.peer.pause_input = self.pause_input.load(std::memory_order_acquire);
        self.peer.handle(*invocation, *progress);
        if (outbound_pending && !self.peer.outgoing)
        {
            self.outbound_pending.store(false, std::memory_order_release);
        }
        self.pause_observed.store(self.peer.pause_input, std::memory_order_release);
        self.active.store(self.peer.active, std::memory_order_release);
        self.participated.store(self.peer.participated, std::memory_order_release);
        self.result.store(self.peer.result, std::memory_order_release);
        self.received.store(self.peer.received, std::memory_order_release);
        self.retired.store(self.peer.retired, std::memory_order_release);
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        auto& self = *static_cast<SControlledServicePeer*>(runtime);
        return CPeerExecution::destroy(&self.peer);
    }
};
}

void run_audio_preamble_service(tests::TTestContext& ctx, const modules::SExecutionV1& execution, void* const context,
    std::atomic<unsigned>& opens, std::atomic<unsigned>& starts, std::atomic<unsigned>& closes,
    std::atomic<std::uint32_t>& complete, std::atomic<bool>& output_wait)
{
    using Access = host::SModulePreambleTestAccess;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule binding;
    modules::SBoundModuleTestAccess::install(binding);
    TEST_EXPECT(ctx, Access::prepare_threads(runtime));
    const modules::SChildExecution child{ &execution, context, thread_ids::audio };
    modules::SExecutionV1 coordinator_execution;
    coordinator_execution.initial_child = &child;
    host::SClientRecord* const audio = Access::install(runtime, module_ids::audio, binding, 3u, coordinator_execution);
    SControlledServicePeer peer;
    const modules::SExecutionV1 peer_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &SControlledServicePeer::create, &SControlledServicePeer::handle, &SControlledServicePeer::destroy };
    const modules::SChildExecution peer_child{ &peer_execution, &peer, thread_ids::executive };
    modules::SExecutionV1 peer_coordinator;
    peer_coordinator.initial_child = &peer_child;
    host::SClientRecord* const executive = Access::install(runtime, module_ids::executive, binding, 3u, peer_coordinator);
    TEST_EXPECT(ctx, (audio && executive && audio->preamble->activate() && executive->preamble->activate()));
    host::CDirectConnections& connections = Access::connections(runtime);
    const auto turn = [&]()
    {
        audio->preamble->process();
        executive->preamble->process();
        Access::progress_threads(runtime);
        connections.progress();
    };
    const auto await = [&](const auto& predicate)
    {
        threading::CWaitDeadline deadline;
        if (deadline.set_after_milliseconds(5000u).code != platform::threading::EWaitError::none) return false;
        while (!predicate())
        {
            turn();
            if (predicate()) return true;
            std::uint64_t remaining{};
            if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                return false;
            }
            threading::CWaitDeadline pause;
            (void)pause.set_after_milliseconds(1u);
            std::atomic<std::uint32_t> unchanged{ 0u };
            (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
        }
        return true;
    };
    TEST_EXPECT(ctx, await([&] { return audio->preamble->ready() && peer.participated.load(std::memory_order_acquire); }));
    const auto connect = [&]()
    {
        peer.result.store(connections::EStatus::busy, std::memory_order_release);
        peer.connect_requested.store(true, std::memory_order_release);
        Access::wake(runtime, system_ids::executive);
        TEST_EXPECT(ctx, await([&] { return peer.active.load(std::memory_order_acquire) &&
            (peer.result.load(std::memory_order_acquire) == connections::EStatus::success); }));
    };
    connect();
    const system_ids::id_type audio_address = system_ids::ops::make_system_id(module_ids::audio, thread_ids::audio);
    host::SClientRecord* const service = Access::endpoint(runtime, audio_address);
    TEST_EXPECT(ctx, (service && service->thread));
    TEST_EXPECT(ctx, await([&] { return service->thread->query_state() == threading::EThreadRunState::Waiting; }));
    TEST_EXPECT(ctx, !opens.load());
    const auto post = [&](const int slot, const module_ids::id_type module = module_ids::audio)
    {
        peer.outgoing = threading::make_pod_message(slot, modules::RunTestsRequest{ module });
        peer.outbound_pending.store(true, std::memory_order_release);
        Access::wake(runtime, system_ids::executive);
        TEST_EXPECT(ctx, await([&] { return !peer.outbound_pending.load(std::memory_order_acquire); }));
    };
    unsigned read = 0u;
    const auto receive = [&](const int slot, const modules::ETestRunStatus status,
        const unsigned passed, const unsigned failed, const unsigned skipped)
    {
        TEST_EXPECT(ctx, await([&] { return peer.received.load(std::memory_order_acquire) > read; }));
        modules::RunTestsResult result;
        TEST_EXPECT(ctx, (peer.observations[read].copy_payload_to(result) && (peer.observations[read].query_async_slot() == slot) &&
            (result.module == module_ids::audio) && (result.status == status) &&
            (result.passed == passed) && (result.failed == failed) && (result.skipped == skipped)));
        ++read;
    };
    post(7, module_ids::rendering); receive(7, modules::ETestRunStatus::invalid_request, 0u, 0u, 0u);
    post(threading::k_uncorrelated_slot); receive(threading::k_uncorrelated_slot, modules::ETestRunStatus::invalid_request, 0u, 0u, 0u);
    post(-2); receive(-2, modules::ETestRunStatus::invalid_request, 0u, 0u, 0u);
    peer.pause_input.store(true, std::memory_order_release);
    Access::wake(runtime, system_ids::executive);
    TEST_EXPECT(ctx, await([&] { return peer.pause_observed.load(std::memory_order_acquire); }));
    for (unsigned index = 0u; index <= host::k_module_pod_capacity; ++index)
    {
        post(static_cast<int>(1000u + index), module_ids::rendering);
    }
    TEST_EXPECT(ctx, await([&]
    {
        return output_wait.load(std::memory_order_acquire) &&
            (service->thread->query_state() == threading::EThreadRunState::Waiting);
    }));
    peer.pause_input.store(false, std::memory_order_release);
    Access::wake(runtime, system_ids::executive);
    for (unsigned index = 0u; index <= host::k_module_pod_capacity; ++index)
    {
        receive(static_cast<int>(1000u + index), modules::ETestRunStatus::invalid_request, 0u, 0u, 0u);
    }
    post(8);
    TEST_EXPECT(ctx, await([&] { return starts.load() == 1u; }));
    post(9); receive(9, modules::ETestRunStatus::busy, 0u, 0u, 0u);
    complete.store(1u);
    Access::wake(runtime, audio_address);
    receive(8, modules::ETestRunStatus::success, 1u, 0u, 0u);
    TEST_EXPECT(ctx, (closes.load() == 1u));
    post(10);
    TEST_EXPECT(ctx, await([&] { return starts.load() == 2u; }));
    peer.close_requested.store(true, std::memory_order_release);
    Access::wake(runtime, system_ids::executive);
    bool cleanup_before_retirement = true;
    TEST_EXPECT(ctx, await([&]
    {
        if (connections.empty() && (closes.load() != 2u)) cleanup_before_retirement = false;
        return connections.empty() && (peer.retired.load(std::memory_order_acquire) == 1u);
    }));
    TEST_EXPECT(ctx, (cleanup_before_retirement && (closes.load() == 2u)));
    connect();
    post(11);
    TEST_EXPECT(ctx, await([&] { return starts.load() == 3u; }));
    audio->preamble->request_stop(modules::EExecutionStop::requested);
    executive->preamble->request_stop(modules::EExecutionStop::requested);
    TEST_EXPECT(ctx, await([&]
    {
        return audio->preamble->destroyed() && executive->preamble->destroyed() &&
            Access::children_idle(runtime, module_ids::audio) &&
            Access::children_idle(runtime, module_ids::executive) && connections.empty();
    }));
    TEST_EXPECT(ctx, ((closes.load() == 3u) && peer.peer.valid && !audio->preamble->failed() && !connections.failed()));
    TEST_EXPECT(ctx, ((peer.peer.destroyed == 1u) && (peer.peer.created_on == peer.peer.destroyed_on) &&
        (peer.peer.created_on != platform::threading::query_current_thread_id())));
    Access::clear(runtime);
}

void run_rendering_preamble_protocol(tests::TTestContext& ctx)
{
    using Access = host::SModulePreambleTestAccess;
    const auto require = [&](const unsigned line, const bool satisfied)
    {
        TEST_EXPECT(ctx, satisfied);
        //  Never unwind live affine state if a test cannot establish retirement.
        if (!satisfied)
        {
            std::cerr << "Rendering protocol requirement at line " << line << '\n';
            std::_Exit(1);
        }
    };
    TInstance<debug_system::CDebugServiceState> debug_service;
    require(__LINE__, debug_service.emplace());
    const std::string log = test_environment::test_log_path("rendering_protocol");
    const std::string direct_log = test_environment::test_log_path("rendering_protocol_direct");
    require(__LINE__, debug_service->configure_log_paths(log.c_str(), direct_log.c_str()) && debug_service->open_logs() &&
        debug_service->start({ thread_ids::debug_service, "render_protocol" }));
    modules::CBoundModule binding;
    const modules::SAdvertisedIdentity host_identity{ module_ids::executable,
        { modules::k_binding_abi_major, 0u }, modules::k_binding_abi_major, modules::k_binding_abi_major };
    const std::string filename = test_environment::binary_path(test_environment::module_filename("MorphicRendering").c_str());
    const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
    require(__LINE__, path.is_ready() && binding.bind(path, module_ids::rendering, host_identity) &&
        binding.install(test_environment::system_registry_view(), module_ids::rendering,
            test_environment::rendering_memory_context(), system_context::sequence_counter, debug_service.operator->()));
    modules::SExecutionV1 execution;
    require(__LINE__, binding.query_execution(execution) == modules::EBindingResult::success);
    require(__LINE__, execution.initial_child && execution.initial_child->descriptor);
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    require(__LINE__, Access::prepare_threads(runtime));
    host::SClientRecord* const rendering = Access::install(runtime, module_ids::rendering, binding, 3u, execution);
    modules::CBoundModule peer_binding;
    modules::SBoundModuleTestAccess::install(peer_binding);
    SControlledServicePeer peer;
    peer.target = modules::test_connections::address(module_ids::rendering);
    const modules::SExecutionV1 peer_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &SControlledServicePeer::create, &SControlledServicePeer::handle, &SControlledServicePeer::destroy };
    const modules::SChildExecution peer_child{ &peer_execution, &peer, thread_ids::executive };
    modules::SExecutionV1 peer_coordinator;
    peer_coordinator.initial_child = &peer_child;
    host::SClientRecord* const executive = Access::install(runtime, module_ids::executive, peer_binding, 3u, peer_coordinator);
    require(__LINE__, rendering && executive && rendering->preamble->activate() && executive->preamble->activate());
    host::CDirectConnections& connections = Access::connections(runtime);
    const auto turn = [&]()
    {
        rendering->preamble->process();
        executive->preamble->process();
        Access::progress_controls(runtime);
    };
    const auto await = [&](const auto& predicate)
    {
        threading::CWaitDeadline deadline;
        require(__LINE__, deadline.set_after_milliseconds(5000u).code == platform::threading::EWaitError::none);
        while (!predicate())
        {
            turn();
            if (predicate()) return true;
            std::uint64_t remaining{};
            if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                return false;
            }
            threading::CWaitDeadline pause;
            (void)pause.set_after_milliseconds(1u);
            std::atomic<std::uint32_t> unchanged{ 0u };
            (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
        }
        return true;
    };
    require(__LINE__, await([&] { return rendering->preamble->ready() && peer.participated.load(std::memory_order_acquire); }));
    peer.connect_requested.store(true, std::memory_order_release);
    Access::wake(runtime, system_ids::executive);
    require(__LINE__, await([&] { return peer.active.load(std::memory_order_acquire) &&
        (peer.result.load(std::memory_order_acquire) == connections::EStatus::success); }));
    host::SClientRecord* const service = Access::endpoint(runtime, peer.target);
    require(__LINE__, service && service->thread);
    const auto send = [&](const std::int32_t slot, const module_ids::id_type module)
    {
        peer.outgoing = threading::make_pod_message(slot, modules::RunTestsRequest{ module });
        peer.outbound_pending.store(true, std::memory_order_release);
        Access::wake(runtime, system_ids::executive);
        require(__LINE__, await([&] { return !peer.outbound_pending.load(std::memory_order_acquire); }));
    };
    unsigned received = 0u;
    const auto receive = [&](const std::int32_t slot, const modules::ETestRunStatus status,
        const std::uint32_t skipped = 0u)
    {
        require(__LINE__, await([&] { return peer.received.load(std::memory_order_acquire) > received; }));
        const threading::CErasedPodMsg& message = peer.observations[received++];
        modules::RunTestsResult result;
        TEST_EXPECT(ctx, (message.copy_payload_to(result) && (message.query_async_slot() == slot) &&
            (result.module == module_ids::rendering) && (result.status == status) &&
            !result.passed && !result.failed && (result.skipped == skipped)));
    };
    const auto capture = [&](threading::CErasedOwnerMsg& message)
    {
        require(__LINE__, await([&] { return service->capture(message); }));
        require(__LINE__, service->reserve_replies(1u));
        service->accounted_owner();
        TEST_EXPECT(ctx, message.is_message_a<RawAssetTransfer>());
    };
    send(80, module_ids::executive);
    receive(80, modules::ETestRunStatus::invalid_request);
    constexpr std::int32_t run_slot = INT32_MAX;
    constexpr std::int32_t busy_slot = INT32_MAX - 1;
    send(run_slot, module_ids::rendering);
    threading::CErasedOwnerMsg input;
    capture(input);
    send(busy_slot, module_ids::rendering);
    receive(busy_slot, modules::ETestRunStatus::busy);
    AssetResult failed;
    failed.status = EAssetStatus::allocation_failed;
    {
        host::CRetainedReplies::CDispatch dispatch{ *service->pending_replies, 1u };
        require(__LINE__, static_cast<bool>(dispatch));
        require(__LINE__, service->reply(threading::make_pod_message(input.query_async_slot(), failed)));
    }
    input = {};
    receive(run_slot, modules::ETestRunStatus::failed, 23u);

    //  Stop after an accepted owning transfer, before its Host result. The late
    //  retained asset must be disposed before connection quiescence and destroy.
    send(83, module_ids::rendering);
    capture(input);
    rendering->preamble->request_stop(modules::EExecutionStop::requested);
    require(__LINE__, await([&] { return service->thread->query_state() == threading::EThreadRunState::Exiting; }));
    host::CAssetService assets;
    require(__LINE__, assets.initialise());
    {
        host::CRetainedReplies::CDispatch dispatch{ *service->pending_replies, 1u };
        require(__LINE__, static_cast<bool>(dispatch));
        assets.request(input, *service);
    }
    TEST_EXPECT(ctx, (assets.output_ready(*service) && (service->reserved_replies == 1u) &&
        (service->pending_replies->reservations() == 0u)));
    assets.retry_output(*service);
    threading::CErasedPodMsg disposal_message;
    require(__LINE__, await([&] { return service->read(disposal_message); }));
    service->accounted_pod();
    require(__LINE__, service->reserve_replies(1u));
    AssetDisposeRequest disposal;
    require(__LINE__, disposal_message.copy_payload_to(disposal) && disposal.asset);
    threading::CErasedPodMsg premature;
    TEST_EXPECT(ctx, (!service->read(premature) && !connections.empty() &&
        (service->thread->query_state() == threading::EThreadRunState::Exiting) &&
        !rendering->preamble->destroyed()));
    {
        host::CRetainedReplies::CDispatch dispatch{ *service->pending_replies, 1u };
        require(__LINE__, static_cast<bool>(dispatch));
        assets.request_disposal(disposal, disposal_message.query_async_slot(), *service);
    }
    assets.complete_disposals();
    TEST_EXPECT(ctx, assets.output_ready(*service));
    assets.retry_output(*service);
    TEST_EXPECT(ctx, (assets.is_idle() && !assets.failed()));
    assets.deallocate();
    executive->preamble->request_stop(modules::EExecutionStop::requested);
    require(__LINE__, await([&]
    {
        return rendering->preamble->destroyed() && executive->preamble->destroyed() &&
            Access::children_idle(runtime, module_ids::rendering) &&
            Access::children_idle(runtime, module_ids::executive) && connections.empty();
    }));
    TEST_EXPECT(ctx, ((peer.received.load(std::memory_order_acquire) == received) && peer.peer.valid &&
        !rendering->preamble->failed() && !executive->preamble->failed() && !connections.failed()));
    TEST_EXPECT(ctx, ((peer.peer.destroyed == 1u) && (peer.peer.created_on == peer.peer.destroyed_on)));
    Access::clear(runtime);
    TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
    TEST_EXPECT(ctx, binding.unbind());
    TEST_EXPECT(ctx, debug_service->stop());
}

void run_executive_submission_failure(tests::TTestContext& ctx, modules::CBoundModule& binding)
{
    modules::SExecutionV1 coordinator;
    TEST_EXPECT(ctx, (binding.query_execution(coordinator) == modules::EBindingResult::success));
    if (!coordinator.initial_child || !coordinator.initial_child->descriptor)
    {
        TEST_EXPECT(ctx, false);
        return;
    }
    const modules::SExecutionV1 execution = *coordinator.initial_child->descriptor;
    static const modules::SCommunicationV1 functions{ &SExecutiveFacade::pod, &SExecutiveFacade::owner,
        &SExecutiveFacade::intent, &SExecutiveFacade::query };
    SExecutiveFacade state;
    state.result = modules::ESubmission::invalid;
    modules::CCommunication communication{ functions, &state };
    modules::CHostRestrictionPublication publication;
    modules::SHostRestrictionSnapshot snapshot;
    snapshot.generation = 1u;
    TEST_EXPECT(ctx, publication.publish(snapshot));
    threading::CNotification notification;
    std::atomic<std::uint32_t> pending{ 0u };
    TEST_EXPECT(ctx, notification.initialise(pending));
    modules::SHostExecutionContext view;
    view.restriction = publication.reader();
    view.notification = notification.reader();
    modules::SExecutionContext context;
    context.address = system_ids::executive;
    context.communication = &communication;
    context.execution = &view;
    void* runtime = nullptr;
    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
    TEST_EXPECT(ctx, (execution.create(&context, &runtime) == modules::EExecutionStatus::success));
    if (!runtime) return;
    modules::SInvocation invocation;
    invocation.event = modules::EExecutionEvent::start;
    modules::SExecutionProgress progress;
    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
    TEST_EXPECT(ctx, (execution.handle(runtime, &invocation, &progress) == modules::EExecutionStatus::failed));
    TEST_EXPECT(ctx, (state.valid && (state.owner_attempts == 1u) && !state.owners));
    invocation.event = modules::EExecutionEvent::stop;
    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
    TEST_EXPECT(ctx, (execution.handle(runtime, &invocation, &progress) == modules::EExecutionStatus::success));
    TEST_EXPECT(ctx, (progress.work_complete && !progress.work_pending));
    TEST_EXPECT(ctx, binding.prepare_host_call(thread_ids::executive, &context));
    TEST_EXPECT(ctx, (execution.destroy(runtime) == modules::EExecutionStatus::success));
}

namespace
{
struct SExecutiveStartupHarness
{
    using Access = host::SModulePreambleTestAccess;
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
    host::CHost runtime{ allocator };
    modules::CBoundModule peer_binding;
    SControlledServicePeer rendering, audio;
    const modules::SExecutionV1 peer_execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &SControlledServicePeer::create, &SControlledServicePeer::handle, &SControlledServicePeer::destroy };
    const modules::SChildExecution rendering_child{ &peer_execution, &rendering, thread_ids::rendering };
    const modules::SChildExecution audio_child{ &peer_execution, &audio, thread_ids::audio };
    host::SClientRecord* coordinators[3]{};
    unsigned rendering_received{ 0u }, audio_received{ 0u };

    bool start(modules::CBoundModule& binding) noexcept
    {
        if (!Access::prepare_threads(runtime)) return false;
        modules::SExecutionV1 execution;
        if (binding.query_execution(execution) != modules::EBindingResult::success)
        {
            return false;
        }
        modules::SBoundModuleTestAccess::install(peer_binding);
        rendering.target = audio.target = system_ids::executive;
        coordinators[0] = Access::install(runtime, module_ids::executive, binding, 3u, execution);
        execution = {};
        execution.initial_child = &rendering_child;
        coordinators[1] = Access::install(runtime, module_ids::rendering, peer_binding, 3u, execution);
        execution.initial_child = &audio_child;
        coordinators[2] = Access::install(runtime, module_ids::audio, peer_binding, 3u, execution);
        for (host::SClientRecord* const coordinator : coordinators)
        {
            if (!coordinator || !coordinator->preamble->activate())
            {
                return false;
            }
        }
        return true;
    }
    void tick() noexcept
    {
        for (host::SClientRecord* const coordinator : coordinators) coordinator->preamble->process();
        Access::progress_controls(runtime);
    }
    template<typename TPredicate> bool until(const TPredicate& predicate) noexcept
    {
        threading::CWaitDeadline deadline;
        if (deadline.set_after_milliseconds(5000u).code != platform::threading::EWaitError::none) return false;
        while (!predicate())
        {
            tick();
            if (predicate()) return true;
            std::uint64_t remaining{};
            if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                return false;
            }
            threading::CWaitDeadline pause;
            (void)pause.set_after_milliseconds(1u);
            std::atomic<std::uint32_t> unchanged{ 0u };
            (void)platform::threading::wait_until_not_equal(unchanged, 0u, pause);
        }
        return true;
    }
    bool capture(threading::CErasedOwnerMsg& message) noexcept
    {
        host::SClientRecord* const client = Access::endpoint(runtime, system_ids::executive);
        if (!client || !client->capture(message)) return false;
        client->accounted_owner();
        return client->reserve_replies(message.is_message_a<modules::ModuleRequest>() ? 2u : 1u);
    }
    bool read_owned(threading::CErasedOwnerMsg& message) noexcept
    {
        return until([&] { return capture(message); });
    }
    bool publish_host(const threading::CErasedPodMsg& message) noexcept
    {
        host::SClientRecord* const client = Access::endpoint(runtime, system_ids::executive);
        if (!client) return false;
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        return dispatch && client->reply(message);
    }
    bool read_peer(SControlledServicePeer& peer, threading::CErasedPodMsg& message) noexcept
    {
        unsigned& consumed = (&peer == &rendering) ? rendering_received : audio_received;
        if (!until([&] { return peer.received.load(std::memory_order_acquire) > consumed; }))
        {
            return false;
        }
        message = peer.observations[consumed++];
        return true;
    }
    bool publish_peer(SControlledServicePeer& peer, const threading::CErasedPodMsg& message) noexcept
    {
        peer.outgoing = message;
        peer.outbound_pending.store(true, std::memory_order_release);
        Access::wake(runtime, modules::test_connections::address((&peer == &rendering) ? module_ids::rendering : module_ids::audio));
        return until([&] { return !peer.outbound_pending.load(std::memory_order_acquire); });
    }
    bool failed() noexcept { return Access::emergency(runtime); }
    bool stop() noexcept
    {
        for (host::SClientRecord* const coordinator : coordinators) coordinator->preamble->request_stop(modules::EExecutionStop::requested);
        const bool stopped = until([&]
        {
            threading::CErasedOwnerMsg cancelled;
            while (capture(cancelled)) cancelled = {};
            host::SClientRecord* const client = Access::endpoint(runtime, system_ids::executive);
            //  This test owns the synthetic service results. Cancel their
            //  remaining credits only after the recipient's actual quiescence.
            if (client && client->execution_quiescent() && client->reserved_replies)
            {
                client->cancel_reservation(client->reserved_replies);
            }
            for (host::SClientRecord* const coordinator : coordinators)
            {
                if (!coordinator->preamble->destroyed() || !Access::children_idle(runtime, coordinator->module))
                {
                    return false;
                }
            }
            return Access::connections(runtime).empty();
        });
        if (stopped) Access::clear(runtime);
        return stopped;
    }
};
}

void run_executive_preamble_protocol(tests::TTestContext& ctx, modules::CBoundModule& binding)
{
    const auto require = [&](const unsigned line, const bool satisfied)
    {
        TEST_EXPECT(ctx, satisfied);
        if (!satisfied)
        {
            std::cerr << "Executive protocol requirement at line " << line << '\n';
            std::_Exit(1);
        }
    };
    enum class ECase : std::uint8_t
    {
        loaded = 0, already_loaded, unavailable, wrong_implementation, load_failure,
        missing_acknowledgement, wrong_correlation, duplicate_acknowledgement,
        audio_unavailable, audio_failed, audio_cancelled, audio_busy, audio_bad_counts, audio_wrong_correlation, audio_load_failure
    };
    for (const ECase scenario : { ECase::loaded, ECase::already_loaded, ECase::unavailable,
        ECase::wrong_implementation, ECase::load_failure, ECase::missing_acknowledgement,
        ECase::wrong_correlation, ECase::duplicate_acknowledgement,
        ECase::audio_unavailable, ECase::audio_failed, ECase::audio_cancelled, ECase::audio_busy,
        ECase::audio_bad_counts, ECase::audio_wrong_correlation, ECase::audio_load_failure })
    {
        SExecutiveStartupHarness fixture;
        require(__LINE__, fixture.start(binding));
        require(__LINE__, fixture.until([&] { return fixture.rendering.participated.load(std::memory_order_acquire) &&
            fixture.audio.participated.load(std::memory_order_acquire); }));
        threading::CErasedOwnerMsg request;
        const bool received = fixture.read_owned(request);
        require(__LINE__, received);
        const modules::ModuleRequest* const load = request.owner().payload<modules::ModuleRequest>();
        TEST_EXPECT(ctx, ((load != nullptr) && (load->action == modules::EModuleAction::load) &&
            (load->module == module_ids::rendering) && (load->file.length() != 0u) &&
            (std::strcmp(load->file.cstring(), test_environment::module_filename("package:/bin/MorphicRendering").c_str()) == 0)));
        const std::int32_t slot = request.query_async_slot();
        request.take_owner().destroy();

        modules::ModuleResult result;
        result.module = module_ids::rendering;
        result.notice = modules::EModuleNotice::acknowledged;
        result.status = modules::EModuleStatus::success;
        threading::CErasedPodMsg reply;
        reply.set_async_slot(slot);
        if (scenario != ECase::missing_acknowledgement)
        {
            reply.assign_payload(result);
            TEST_EXPECT(ctx, fixture.publish_host(reply));
        }
        result.notice = (scenario == ECase::duplicate_acknowledgement) ?
            modules::EModuleNotice::acknowledged : modules::EModuleNotice::completed;
        result.status = (scenario == ECase::load_failure) ? modules::EModuleStatus::binding_failed :
            (((scenario == ECase::loaded) || (scenario == ECase::duplicate_acknowledgement)) ?
                modules::EModuleStatus::success : modules::EModuleStatus::already_loaded);
        result.available = (scenario != ECase::unavailable) && (scenario != ECase::load_failure);
        if (scenario == ECase::wrong_implementation)
        {
            //  A different implementation at the same mount needs no catalogue entry.
            result.module = module_ids::ops::make_id(mount_point_ids::render,
                module_ids::ops::make_index(module_ids::k_count));
        }
        if (scenario == ECase::wrong_correlation)
        {
            reply.set_async_slot(slot + 1);
        }
        reply.assign_payload(result);
        TEST_EXPECT(ctx, fixture.publish_host(reply));
        if ((scenario == ECase::loaded) || (scenario == ECase::already_loaded) || (scenario >= ECase::audio_unavailable))
        {
            threading::CErasedPodMsg tests_request;
            require(__LINE__, fixture.read_peer(fixture.rendering, tests_request));
            modules::RunTestsRequest tests;
            TEST_EXPECT(ctx, (tests_request.copy_payload_to(tests) && (tests.module == module_ids::rendering)));
            TEST_EXPECT(ctx, (tests_request.query_async_slot() == (slot + 1)));
            TEST_EXPECT(ctx, !fixture.capture(request));
            //  Executive only observes generic completion; it does not know
            //  the destination's test cases or require a particular case count.
            modules::RunTestsResult completed;
            completed.module = module_ids::rendering;
            completed.status = modules::ETestRunStatus::success;
            completed.passed = 7u;
            reply.set_async_slot(slot + 1);
            reply.assign_payload(completed);
            TEST_EXPECT(ctx, fixture.publish_peer(fixture.rendering, reply));
            require(__LINE__, fixture.read_owned(request));
            const modules::ModuleRequest* const audio_load = request.owner().payload<modules::ModuleRequest>();
            TEST_EXPECT(ctx, (audio_load && (audio_load->module == module_ids::audio) && (audio_load->action == modules::EModuleAction::load) &&
                (std::strcmp(audio_load->file.cstring(), test_environment::module_filename("package:/bin/MorphicAudio").c_str()) == 0)));
            TEST_EXPECT(ctx, (request.query_async_slot() == (slot + 2)));
            request.take_owner().destroy();
            result.module = module_ids::audio;
            result.notice = modules::EModuleNotice::acknowledged;
            result.status = modules::EModuleStatus::success;
            result.available = false;
            reply.set_async_slot(slot + 2);
            reply.assign_payload(result);
            TEST_EXPECT(ctx, fixture.publish_host(reply));
            result.notice = modules::EModuleNotice::completed;
            result.status = (scenario == ECase::audio_load_failure) ? modules::EModuleStatus::binding_failed : modules::EModuleStatus::success;
            result.available = scenario != ECase::audio_load_failure;
            reply.assign_payload(result);
            TEST_EXPECT(ctx, fixture.publish_host(reply));
            if (scenario != ECase::audio_load_failure)
            {
                require(__LINE__, fixture.read_peer(fixture.audio, tests_request));
                TEST_EXPECT(ctx, (tests_request.copy_payload_to(tests) && (tests.module == module_ids::audio) && (tests_request.query_async_slot() == (slot + 3))));
                TEST_EXPECT(ctx, !fixture.capture(request));
                completed = { module_ids::audio, modules::ETestRunStatus::success, 1u, 0u, 0u };
                if (scenario == ECase::audio_unavailable)
                {
                    completed = { module_ids::audio, modules::ETestRunStatus::unavailable, 0u, 0u, 1u };
                }
                if (scenario == ECase::audio_failed)
                {
                    completed = { module_ids::audio, modules::ETestRunStatus::failed, 0u, 1u, 0u };
                }
                if (scenario == ECase::audio_cancelled)
                {
                    completed = { module_ids::audio, modules::ETestRunStatus::cancelled, 0u, 0u, 1u };
                }
                if (scenario == ECase::audio_busy)
                {
                    completed = { module_ids::audio, modules::ETestRunStatus::busy, 0u, 1u, 0u };
                }
                if (scenario == ECase::audio_bad_counts)
                {
                    completed.passed = 2u;
                }
                reply.set_async_slot(slot + ((scenario == ECase::audio_wrong_correlation) ? 4 : 3));
                reply.assign_payload(completed);
                TEST_EXPECT(ctx, fixture.publish_peer(fixture.audio, reply));
            }
            if ((scenario == ECase::loaded) || (scenario == ECase::already_loaded) || (scenario == ECase::audio_unavailable))
            {
                require(__LINE__, fixture.read_owned(request));
                TEST_EXPECT(ctx, (request.is_message_a<RawAssetTransfer>() && (request.query_async_slot() == (slot + 4))));
            }
            else
            {
                TEST_EXPECT(ctx, fixture.until([&] { return fixture.failed(); }));
                TEST_EXPECT(ctx, !fixture.capture(request));
            }
        }
        else
        {
            TEST_EXPECT(ctx, fixture.until([&] { return fixture.failed(); }));
            TEST_EXPECT(ctx, !fixture.capture(request));
        }
        request.take_owner().destroy();
        const bool expected_failure = (scenario != ECase::loaded) && (scenario != ECase::already_loaded) &&
            (scenario != ECase::audio_unavailable);
        TEST_EXPECT(ctx, (fixture.failed() == expected_failure));
        require(__LINE__, fixture.stop());
        TEST_EXPECT(ctx, (fixture.rendering.peer.valid && fixture.audio.peer.valid &&
            (fixture.rendering.peer.destroyed == 1u) && (fixture.audio.peer.destroyed == 1u) &&
            (fixture.rendering.peer.created_on == fixture.rendering.peer.destroyed_on) &&
            (fixture.audio.peer.created_on == fixture.audio.peer.destroyed_on)));
        TEST_EXPECT(ctx, test_environment::executive_memory_context()->is_attribution_empty());
    }
}
