
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    HostRestriction_test_cases.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Deterministic restriction accounting, publication boundaries and budget expiry.

#include "framework/module/host_eligible_budget.hpp"
#include "host/runtime/host_restrictions.hpp"
#include "tests/support/test_context.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "platform/threading/processor_relax.hpp"
#include "platform/threading/wait_deadline_math.hpp"

namespace modules
{
struct SHostRestrictionTestAccess
{
    static void revision(CHostRestrictionPublication& publication, const std::uint32_t value) noexcept
    { publication.m_revision.store(value); }
};
}

namespace host
{
struct SHostRestrictionTestAccess
{
    static void exhaust_tokens(CHostRestrictions& host) noexcept
    { host.m_next_scope = host.m_next_invocation = UINT64_MAX; }
    static void reserve_last_revision(CHostRestrictions& host) noexcept
    { modules::SHostRestrictionTestAccess::revision(host.m_publication, 0xfffffffau); }
};
}

namespace
{

using tests::TTestContext;
using modules::EBudgetStatus;
using modules::ERestrictionStatus;
using platform::threading::EWaitError;

struct SClock
{
    std::uint64_t time{ 100u };
    platform::threading::WaitError error;
    modules::CHostEligibleBudget* sampling_budget{ nullptr };
    bool sampling_retried{ false };
    static platform::threading::WaitError read(void* const context, std::uint64_t& value) noexcept
    {
        auto& clock = *static_cast<SClock*>(context);
        if (clock.sampling_budget)
            clock.sampling_retried = clock.sampling_budget->observe().status == EBudgetStatus::retry;
        if (clock.error.code != EWaitError::none) return clock.error;
        value = clock.time;
        return {};
    }
    platform::threading::SAwakeClock source() noexcept { return { this, &read }; }
};

void overlap_and_missed_notifications(TTestContext& ctx)
{
    SClock clock;
    host::CHostRestrictions host{ clock.source() };
    modules::CHostEligibleBudget before{ clock.source() }, during{ clock.source() };
    TEST_EXPECT(ctx, before.start(host.reader(), 100u) == EBudgetStatus::running);
    TEST_EXPECT(ctx, host.begin_invocation({ 7u }, true));
    clock.time = 150u;
    const auto first = host.enter({ 2u });
    TEST_EXPECT(ctx, first.status == ERestrictionStatus::success);
    clock.time = 1000u;
    TEST_EXPECT(ctx, during.start(host.reader(), 100u) == EBudgetStatus::restricted);
    const auto second = host.enter({ 3u });
    const auto third = host.enter({ 2u });
    modules::SHostRestrictionSnapshot snapshot;
    TEST_EXPECT(ctx, host.reader().read(snapshot) == modules::ERestrictionRead::success);
    TEST_EXPECT(ctx, snapshot.depth == 3u && snapshot.counts[2] == 2u && snapshot.counts[3] == 1u);
    TEST_EXPECT(ctx, host.leave(first.scope) == ERestrictionStatus::success);
    TEST_EXPECT(ctx, host.leave(first.scope) == ERestrictionStatus::invalid_scope);
    clock.time = 2000u;
    TEST_EXPECT(ctx, host.leave(third.scope) == ERestrictionStatus::success);
    TEST_EXPECT(ctx, before.observe().remaining_ns == 50u);
    TEST_EXPECT(ctx, host.leave(second.scope) == ERestrictionStatus::success);
    TEST_EXPECT(ctx, host.end_invocation());
    TEST_EXPECT(ctx, host.reader().read(snapshot) == modules::ERestrictionRead::success);
    TEST_EXPECT(ctx, snapshot.completed_ns == 1850u && snapshot.depth == 0u && snapshot.reasons == 0u);
    //  Neither budget consumes any notification; cumulative accounting suffices.
    clock.time = 2025u;
    TEST_EXPECT(ctx, before.observe().remaining_ns == 25u && during.observe().remaining_ns == 75u);
    clock.time = 2050u;
    TEST_EXPECT(ctx, before.observe().status == EBudgetStatus::expired);
    TEST_EXPECT(ctx, host.begin_invocation({ 8u }, true));
    const auto next = host.enter({ 1u });
    clock.time = 10000u;
    TEST_EXPECT(ctx, before.observe().status == EBudgetStatus::expired);
    TEST_EXPECT(ctx, during.observe().remaining_ns == 50u);
    TEST_EXPECT(ctx, host.leave(first.scope) == ERestrictionStatus::invalid_scope);
    TEST_EXPECT(ctx, host.leave(next.scope) == ERestrictionStatus::success && host.end_invocation());
}

void scope_limits_and_unwind(TTestContext& ctx)
{
    SClock clock;
    host::CHostRestrictions host{ clock.source() };
    TEST_EXPECT(ctx, host.enter({ 0u }).status == ERestrictionStatus::invalid_context);
    TEST_EXPECT(ctx, host.begin_invocation({ 1u }, false));
    TEST_EXPECT(ctx, host.enter({ 0u }).status == ERestrictionStatus::invalid_context && host.end_invocation());
    TEST_EXPECT(ctx, host.begin_invocation({ 1u }, true));
    TEST_EXPECT(ctx, !host.begin_invocation({ 2u }, true));
    TEST_EXPECT(ctx, host.enter({ 32u }).status == ERestrictionStatus::invalid_context);
    modules::SRestrictionScope scopes[64];
    for (auto& scope : scopes)
    {
        const auto entered = host.enter({ 31u });
        TEST_EXPECT(ctx, entered.status == ERestrictionStatus::success);
        scope = entered.scope;
    }
    TEST_EXPECT(ctx, host.enter({ 0u }).status == ERestrictionStatus::busy);
    clock.time = 500u;
    TEST_EXPECT(ctx, !host.end_invocation());
    modules::SHostRestrictionSnapshot snapshot;
    TEST_EXPECT(ctx, host.reader().read(snapshot) == modules::ERestrictionRead::success);
    TEST_EXPECT(ctx, snapshot.depth == 0u && snapshot.completed_ns == 400u);
    TEST_EXPECT(ctx, host.begin_invocation({ 2u }, true));
    TEST_EXPECT(ctx, host.leave(scopes[63]) == ERestrictionStatus::invalid_scope);
    TEST_EXPECT(ctx, host.end_invocation());
}

void sampling_and_failures(TTestContext& ctx)
{
    SClock clock;
    host::CHostRestrictions host{ clock.source() };
    modules::CHostEligibleBudget budget{ clock.source() };
    TEST_EXPECT(ctx, budget.start(host.reader(), 100u) == EBudgetStatus::running);
    TEST_EXPECT(ctx, host.begin_invocation({ 1u }, true));
    clock.time = 125u;
    clock.sampling_budget = &budget;
    const auto scope = host.enter({ 0u });
    clock.sampling_budget = nullptr;
    TEST_EXPECT(ctx, scope.status == ERestrictionStatus::success && clock.sampling_retried);
    TEST_EXPECT(ctx, budget.observe().remaining_ns == 75u);
    clock.time = 124u;
    TEST_EXPECT(ctx, host.leave(scope.scope) == ERestrictionStatus::failed);
    TEST_EXPECT(ctx, !host.end_invocation());
    TEST_EXPECT(ctx, budget.observe().status == EBudgetStatus::failed);

    SClock failed_clock;
    host::CHostRestrictions failed_host{ failed_clock.source() };
    modules::CHostEligibleBudget invalid{ failed_clock.source() };
    TEST_EXPECT(ctx, invalid.start(failed_host.reader(), UINT64_MAX) == EBudgetStatus::failed);
    TEST_EXPECT(ctx, invalid.observe().error.code == EWaitError::overflow);
    failed_clock.error = { EWaitError::native_failure, 19u };
    TEST_EXPECT(ctx, invalid.start(failed_host.reader(), 10u) == EBudgetStatus::failed);
    TEST_EXPECT(ctx, invalid.observe().error.native_error == 19u);
    failed_clock.error = {};
    TEST_EXPECT(ctx, invalid.start(failed_host.reader(), 0u) == EBudgetStatus::expired);
    TEST_EXPECT(ctx, invalid.observe().status == EBudgetStatus::expired);
    TEST_EXPECT(ctx, failed_host.begin_invocation({ 2u }, true));
    failed_clock.error = { EWaitError::unsupported, 7u };
    TEST_EXPECT(ctx, failed_host.enter({ 0u }).status == ERestrictionStatus::failed);
    modules::SHostRestrictionSnapshot snapshot;
    TEST_EXPECT(ctx, failed_host.reader().read(snapshot) == modules::ERestrictionRead::failed);
    TEST_EXPECT(ctx, snapshot.error.code == EWaitError::unsupported && snapshot.revision == 0xfffffffeu);
    TEST_EXPECT(ctx, !failed_host.end_invocation());
}

void durable_snapshot_and_generation(TTestContext& ctx)
{
    modules::CHostRestrictionPublication publication;
    modules::SHostRestrictionSnapshot state;
    state.generation = 0x1122334455667788u;
    state.authority = { 0x123456789abcdef0u };
    state.invocation = UINT64_MAX - 4u;
    state.completed_ns = 0xfedcba9876543210u;
    state.active_start_ns = UINT64_MAX;
    state.depth = 32u;
    state.reasons = UINT32_MAX;
    for (auto& count : state.counts) count = 1u;
    TEST_EXPECT(ctx, publication.publish(state));
    modules::SHostRestrictionSnapshot observed;
    TEST_EXPECT(ctx, publication.reader().read(observed) == modules::ERestrictionRead::success);
    TEST_EXPECT(ctx, observed.generation == state.generation && observed.authority.value == state.authority.value &&
        observed.invocation == state.invocation && observed.completed_ns == state.completed_ns &&
        observed.active_start_ns == state.active_start_ns && observed.counts[31] == 1u && observed.reasons == UINT32_MAX);
    publication.fail(state, { EWaitError::overflow, 3u });
    TEST_EXPECT(ctx, !publication.can_publish() && !publication.publish(state));
    TEST_EXPECT(ctx, publication.reader().read(observed) == modules::ERestrictionRead::failed);
    TEST_EXPECT(ctx, observed.revision == 0xfffffffeu && observed.error.native_error == 3u);

    SClock clock;
    modules::CHostRestrictionPublication replacement;
    state = {};
    state.generation = 1u;
    TEST_EXPECT(ctx, replacement.publish(state));
    modules::CHostEligibleBudget budget{ clock.source() };
    TEST_EXPECT(ctx, budget.start(replacement.reader(), 100u) == EBudgetStatus::running);
    state.generation = 2u;
    TEST_EXPECT(ctx, replacement.publish(state));
    TEST_EXPECT(ctx, budget.observe().status == EBudgetStatus::failed);
}

void exhaustion_and_regression(TTestContext& ctx)
{
    SClock clock;
    host::CHostRestrictions host{ clock.source() };
    host::SHostRestrictionTestAccess::exhaust_tokens(host);
    TEST_EXPECT(ctx, host.begin_invocation({ 1u }, true));
    const auto scope = host.enter({ 0u });
    TEST_EXPECT(ctx, scope.status == ERestrictionStatus::success && scope.scope.value == UINT64_MAX);
    TEST_EXPECT(ctx, host.enter({ 1u }).status == ERestrictionStatus::busy);
    modules::CHostEligibleBudget budget{ clock.source() };
    TEST_EXPECT(ctx, budget.start(host.reader(), 100u) == EBudgetStatus::restricted);
    clock.time = 200u;
    TEST_EXPECT(ctx, budget.observe().remaining_ns == 100u);
    --clock.time; //  Eligible time is frozen, but backwards awake time is still an error.
    TEST_EXPECT(ctx, budget.observe().status == EBudgetStatus::failed);
    TEST_EXPECT(ctx, host.leave(scope.scope) == ERestrictionStatus::success && host.end_invocation());
    TEST_EXPECT(ctx, !host.begin_invocation({ 2u }, true));

    host::CHostRestrictions exhausted{ clock.source() };
    host::SHostRestrictionTestAccess::reserve_last_revision(exhausted);
    TEST_EXPECT(ctx, exhausted.begin_invocation({ 3u }, true));
    TEST_EXPECT(ctx, exhausted.enter({ 0u }).status == ERestrictionStatus::failed);
    modules::SHostRestrictionSnapshot snapshot;
    TEST_EXPECT(ctx, exhausted.reader().read(snapshot) == modules::ERestrictionRead::failed);
    TEST_EXPECT(ctx, snapshot.revision == 0xfffffffeu && snapshot.depth == 0u &&
        snapshot.error.code == EWaitError::overflow && !exhausted.end_invocation());

    modules::CHostRestrictionPublication publication;
    modules::SHostRestrictionTestAccess::revision(publication, 3u);
    snapshot.revision = 99u;
    TEST_EXPECT(ctx, publication.reader().read(snapshot) == modules::ERestrictionRead::retry && snapshot.revision == 99u);
    modules::SHostRestrictionTestAccess::revision(publication, 0xfffffffau);
    TEST_EXPECT(ctx, publication.can_publish() && !publication.can_publish(2u));
    snapshot = {};
    snapshot.generation = 1u;
    TEST_EXPECT(ctx, publication.publish(snapshot) && !publication.can_publish());
    publication.fail(snapshot, { EWaitError::overflow, 0u });
    TEST_EXPECT(ctx, publication.reader().read(snapshot) == modules::ERestrictionRead::failed && snapshot.revision == 0xfffffffeu);
}

struct SConcurrentPublication
{
    modules::CHostRestrictionPublication publication;
    std::atomic<bool> done{ false }, started{ false };
    static std::uint32_t MV_STD_ABI_CALL write(void* const context) noexcept
    {
        auto& shared = *static_cast<SConcurrentPublication*>(context);
        while (!shared.started.load()) platform::threading::processor_relax();
        for (std::uint32_t value = 1u; value <= 4096u; ++value)
        {
            modules::SHostRestrictionSnapshot state;
            state.generation = 1u;
            state.depth = value;
            state.reasons = ~value;
            state.authority.value = (static_cast<std::uint64_t>(value) << 32u) | ~value;
            state.completed_ns = state.active_start_ns = state.invocation = state.authority.value;
            for (auto& count : state.counts) count = value;
            (void)shared.publication.publish(state);
        }
        shared.done.store(true);
        return 0u;
    }
};

void concurrent_publication(TTestContext& ctx)
{
    SConcurrentPublication shared;
    modules::SHostRestrictionSnapshot initial;
    initial.generation = 1u;
    TEST_EXPECT(ctx, shared.publication.publish(initial));
    platform::threading::CThread writer;
    const bool created = writer.create(&SConcurrentPublication::write, &shared);
    TEST_EXPECT(ctx, created);
    if (!created) return;
    bool coherent = true;
    std::uint32_t accepted = 0u;
    shared.started.store(true);
    do
    {
        modules::SHostRestrictionSnapshot state;
        const auto result = shared.publication.reader().read(state);
        if (result == modules::ERestrictionRead::retry) continue;
        coherent = coherent && result == modules::ERestrictionRead::success;
        if (!state.depth) continue;
        ++accepted;
        const auto wide = (static_cast<std::uint64_t>(state.depth) << 32u) | ~state.depth;
        coherent = coherent && state.generation == 1u && state.reasons == ~state.depth &&
            state.authority.value == wide && state.completed_ns == wide && state.active_start_ns == wide && state.invocation == wide;
        for (const auto count : state.counts) coherent = coherent && count == state.depth;
    } while (!shared.done.load() || !accepted);
    TEST_EXPECT(ctx, writer.join_and_close());
    TEST_EXPECT(ctx, coherent && accepted != 0u);
}

void awake_conversion(TTestContext& ctx)
{
    using platform::threading::internal::scale_awake_observation;
    std::uint64_t value = 77u;
    TEST_EXPECT(ctx, scale_awake_observation(0u, 100u, 1u, value) && value == 0u);
    TEST_EXPECT(ctx, scale_awake_observation(5u, 2u, 3u, value) && value == 3u);
    TEST_EXPECT(ctx, scale_awake_observation(UINT64_MAX, UINT32_MAX, UINT32_MAX, value) && value == UINT64_MAX);
    TEST_EXPECT(ctx, scale_awake_observation(UINT64_MAX / 100u, 100u, 1u, value) && value == UINT64_MAX - 15u);
    const auto preserved = value;
    TEST_EXPECT(ctx, !scale_awake_observation(UINT64_MAX / 100u + 1u, 100u, 1u, value) && value == preserved);
    TEST_EXPECT(ctx, !scale_awake_observation(1u, 0u, 1u, value) && value == preserved);
    TEST_EXPECT(ctx, !scale_awake_observation(1u, 1u, 0u, value) && value == preserved);
}

}   //  namespace

void run_host_restriction_tests(tests::TTestContext& ctx)
{
    overlap_and_missed_notifications(ctx);
    scope_limits_and_unwind(ctx);
    sampling_and_failures(ctx);
    durable_snapshot_and_generation(ctx);
    exhaustion_and_regression(ctx);
    concurrent_publication(ctx);
    awake_conversion(ctx);
}
