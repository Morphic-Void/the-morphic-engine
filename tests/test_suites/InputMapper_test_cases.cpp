
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    InputMapper_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    08 Nov 26
//
//  Synthetic portable mapping traces, partition invariance and bounded recovery.

#include <cmath>
#include <cstring>
#include <limits>
#include "platform/runtime/input_connection.hpp"
#include "platform/runtime/desktop_input.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{

using tests::TTestContext;
using namespace platform::input;
using namespace platform::runtime;

struct SFixture
{
    TTestContext& tests;
    platform::threading::COwnerWait wake;
    std::atomic<std::uint32_t> work{};
    threading::CNotification notification;
    SInputPlan plan;
    SInputInstallation installation;
    CInputConnection connection{ {} };
    SInputObservation observation;
    SActionInputV1 records[1024]{};
    std::uint32_t count{}, credits{ 2048u }, attempts{ 64u };

    explicit SFixture(TTestContext& tests) noexcept : tests{ tests } {}
    void add(const std::uint32_t action, const std::uint32_t id, const std::uint32_t control,
        const EInputActivation activation = EInputActivation::regular, const std::uint32_t modifier = 0u,
        const bool allow_text = false, const bool repeat = false, const std::uint32_t direction = 0u,
        const SInputParameters parameters = {}) noexcept
    {
        SInputBinding binding;
        binding.id = id;
        binding.action = action;
        binding.context = 1u;
        binding.control = control;
        binding.activation = activation;
        binding.source = (control < 128u) ? EInputSource::key : ((control <= 132u) ? EInputSource::button :
            ((control == 133u) ? EInputSource::position : ((control == 134u) ? EInputSource::wheel :
            ((control <= 269u) ? EInputSource::pad_button : ((control <= 271u) ? EInputSource::stick : EInputSource::trigger)))));
        if ((activation == EInputActivation::start) || (activation == EInputActivation::release))
        {
            binding.action_kind = EInputAction::pulse;
        }
        else if (activation != EInputActivation::direct) binding.action_kind = EInputAction::digital;
        else if (binding.source == EInputSource::wheel) binding.action_kind = EInputAction::delta;
        else if ((binding.source == EInputSource::stick) && !direction) binding.action_kind = EInputAction::vector;
        else binding.action_kind = EInputAction::scalar;
        binding.allow_text = allow_text;
        binding.repeat = repeat;
        binding.direction = static_cast<EInputDirection>(direction);
        binding.parameters = parameters;
        binding.modifier_begin = plan.modifiers.size();
        if (modifier) TEST_EXPECT(tests, plan.modifiers.push(modifier));
        binding.modifier_end = plan.modifiers.size();
        TEST_EXPECT(tests, plan.bindings.push(binding));
        if (!installation.subjects.size() || (installation.subjects[installation.subjects.size() - 1u] != action))
        {
            TEST_EXPECT(tests, installation.subjects.push_back(action));
        }
    }
    void start(const EConnection kind = EConnection::desktop, const bool prepare = true) noexcept
    {
        TEST_EXPECT(tests, (wake.initialise().code == platform::threading::EWaitError::none));
        TEST_EXPECT(tests, notification.initialise(work));
        installation.token.value = 1u;
        installation.profile = 1u;
        installation.kind = kind;
        installation.plan = &plan;
        installation.binding_count = plan.bindings.size();
        TEST_EXPECT(tests, installation.contexts.push_back(1u));
        TEST_EXPECT(tests, installation.contexts.push_back(2u));
        SInputEndpointV3 identity;
        identity.kind = kind;
        identity.connection.value = identity.provider.value = identity.consumer.value = 1u;
        identity.window.value = (kind == EConnection::desktop) ? 1u : 0u;
        identity.capacity_wake = wake.endpoint();
        TEST_EXPECT(tests, connection.initialise(identity, installation, 1u, 1u));
        if (!prepare) return;
        while (!connection.mapper().prepared()) TEST_EXPECT(tests, connection.mapper().prepare_step());
        TEST_EXPECT(tests, connection.start(notification.endpoint()));
        recover();
        observation.device = 51u;
        observation.kind = EObservationKind::snapshot;
    }
    void drain() noexcept
    {
        SActionInputV1 action;
        while (connection.view.actions->read(action))
        {
            if (action.kind == EActionKind::boundary) continue;
            TEST_EXPECT(tests, (count < 1024u));
            if (count < 1024u) records[count++] = action;
        }
        STextInputV1 text;
        if (connection.view.text)
        {
            while (connection.view.text->read(text)) {}
        }
    }
    void recover() noexcept
    {
        connection.progress(100u);
        drain();
        connection.progress(100u); // A full stream needs returned space before its marker.
        drain();
        const SInputSnapshot& state = connection.snapshot();
        TEST_EXPECT(tests, (connection.acknowledge({ connection.view.connection, state.action_generation,
            state.text_generation }) == services::EServiceStatus::success));
    }
    void set(const std::uint32_t control, const float x, const float y = 0.0f, const bool repeat = false) noexcept
    {
        std::uint32_t index = 0u;
        while ((index < observation.count) && (observation.controls[index].control < control)) ++index;
        if ((index == observation.count) || (observation.controls[index].control != control))
        {
            for (std::uint32_t move = observation.count; move > index; --move)
            {
                observation.controls[move] = observation.controls[move - 1u];
            }
            ++observation.count;
        }
        observation.controls[index] = { control, x, y, repeat };
    }
    void finish(const bool collect = true) noexcept
    {
        for (std::uint32_t iteration = 0u; connection.mapper().pending() && (iteration < 100000u); ++iteration)
        {
            SMapperBudget budget{ credits, attempts };
            while (connection.mapper().advance(budget)) {}
            if (collect) drain();
        }
        TEST_EXPECT(tests, !connection.mapper().pending());
    }
    void send(const bool baseline = false, const bool collect = true) noexcept
    {
        ++observation.ordinal;
        observation.time_ns = 200u + observation.ordinal;
        observation.baseline = baseline;
        TEST_EXPECT(tests, (connection.mapper().submit(observation) == EInputAdmission::accepted));
        finish(collect);
    }
    std::uint32_t digital(const std::uint32_t index) const noexcept
    {
        SDigitalAction value;
        std::memcpy(&value, records[index].payload, sizeof(value));
        return value.active;
    }
    float scalar(const std::uint32_t index) const noexcept
    {
        SScalarAction value;
        std::memcpy(&value, records[index].payload, sizeof(value));
        return value.value;
    }
};

void digital_traces(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.add(1u, 1u, 1u);
    fixture.add(1u, 2u, 2u);
    fixture.add(2u, 3u, 3u, EInputActivation::chord, 98u);
    fixture.add(3u, 4u, 4u, EInputActivation::release, 98u);
    fixture.add(4u, 5u, 39u, EInputActivation::start, 0u, true, true);
    fixture.start();
    for (const std::uint32_t code : { 1u, 2u, 3u, 4u, 39u, 98u }) fixture.set(code, 0.0f);
    fixture.send(true);
    TEST_EXPECT(ctx, !fixture.count);
    fixture.set(1u, 1.0f);
    fixture.send();
    fixture.set(1u, 0.0f);
    fixture.set(2u, 1.0f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 1u) && (fixture.digital(0u) == 1u)));
    fixture.set(2u, 0.0f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 2u) && !fixture.digital(1u)));
    fixture.set(3u, 1.0f);
    fixture.send();
    fixture.set(98u, 1.0f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 2u));
    fixture.set(3u, 0.0f);
    fixture.send();
    fixture.set(3u, 1.0f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 3u) && (fixture.records[2u].subject == 2u)));
    fixture.set(4u, 1.0f);
    fixture.send();
    fixture.set(4u, 0.0f);
    fixture.set(98u, 0.0f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 5u) && !fixture.digital(3u) && (fixture.records[4u].kind == EActionKind::pulse)));
    fixture.set(4u, 1.0f);
    fixture.set(98u, 1.0f);
    fixture.send();
    fixture.set(98u, 0.0f);
    fixture.send();
    fixture.set(4u, 0.0f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 5u));
    fixture.set(39u, 1.0f);
    fixture.send();
    fixture.send();
    fixture.set(39u, 1.0f, 0.0f, true);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 7u) && (fixture.records[5u].kind == EActionKind::pulse) &&
        (fixture.records[6u].kind == EActionKind::pulse)));
}

void rearm_and_selective_cancellation(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.add(1u, 1u, 1u);
    fixture.add(1u, 2u, 2u);
    fixture.add(2u, 3u, 3u);
    fixture.add(2u, 4u, 128u);
    fixture.add(3u, 5u, 4u, EInputActivation::release);
    fixture.start();
    for (const std::uint32_t code : { 1u, 2u, 3u, 4u, 128u }) fixture.set(code, 0.0f);
    fixture.send(true);
    fixture.set(1u, 1.0f);
    fixture.set(4u, 1.0f);
    fixture.send();
    fixture.connection.mapper().eligible(false);
    fixture.recover();
    fixture.set(2u, 1.0f);
    fixture.send();
    fixture.connection.mapper().eligible(true);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 1u));
    fixture.set(2u, 0.0f);
    fixture.set(4u, 0.0f);
    fixture.send();
    fixture.set(2u, 1.0f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 2u) && (fixture.records[1u].subject == 1u) && fixture.digital(1u)));
    fixture.set(3u, 1.0f);
    fixture.set(128u, 1.0f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.connection.text_target({ fixture.connection.view.connection, 1u, 7u }) == services::EServiceStatus::success));
    fixture.finish();
    fixture.recover();
    TEST_EXPECT(ctx, ((fixture.count == 4u) && (fixture.records[3u].subject == 1u) &&
        (fixture.records[3u].flags == k_action_cancelled) && !fixture.digital(3u)));
    fixture.set(128u, 0.0f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 5u) && (fixture.records[4u].subject == 2u) && !fixture.records[4u].flags));
    TEST_EXPECT(ctx, (fixture.connection.text_target({ fixture.connection.view.connection, 1u, 0u }) == services::EServiceStatus::success));
    fixture.finish();
    fixture.recover();
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 5u));
    fixture.set(3u, 0.0f);
    fixture.send();
    fixture.set(3u, 1.0f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 6u));
    TEST_EXPECT(ctx, (fixture.connection.text_target({ fixture.connection.view.connection, 1u, 8u }) == services::EServiceStatus::success));
    fixture.finish();
    TEST_EXPECT(ctx, ((fixture.count == 7u) && (fixture.records[6u].flags == k_action_cancelled)));
}

void analog_traces(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    const SInputParameters parameters{ 0.0f, 0.125f, 0.25f, 0.5f, 0.25f, -2.0f };
    fixture.add(1u, 1u, 270u, EInputActivation::direct, 0u, false, false, 2u, parameters);
    fixture.add(2u, 2u, 272u, EInputActivation::threshold, 0u, false, false, 0u, parameters);
    fixture.start(EConnection::controller);
    fixture.set(270u, 0.0f);
    fixture.set(272u, 0.0f);
    fixture.send(true);
    fixture.set(270u, -0.5f);
    fixture.set(272u, 0.5f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 2u) && (fixture.scalar(0u) == -1.0f) && fixture.digital(1u)));
    fixture.set(272u, 0.375f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 2u));
    fixture.set(272u, 0.25f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 3u) && !fixture.digital(2u)));
    fixture.set(270u, std::numeric_limits<float>::quiet_NaN());
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 4u) && (fixture.records[3u].flags == k_action_cancelled) &&
        (fixture.scalar(3u) == 0.0f)));

    SFixture desktop{ ctx };
    desktop.add(1u, 1u, 133u, EInputActivation::direct, 0u, false, false, 1u);
    desktop.add(2u, 2u, 134u, EInputActivation::direct, 0u, false, false, 0u,
        SInputParameters{ 0.0f, 0.0f, 0.0f, 0.55f, 0.45f, 2.0f });
    desktop.start();
    desktop.set(133u, -10.0f);
    desktop.set(134u, 3.0f);
    desktop.send(true);
    TEST_EXPECT(ctx, !desktop.count);
    desktop.set(133u, -12.0f);
    desktop.set(134u, 0.25f);
    desktop.send();
    TEST_EXPECT(ctx, ((desktop.count == 2u) && (desktop.scalar(0u) == -12.0f) && (desktop.scalar(1u) == 0.5f)));
}

void partitions_pressure_and_allocations(TTestContext& ctx)
{
    SActionInputV1 reference[12]{};
    std::uint32_t reference_count = 0u;
    for (const std::uint32_t credits : { 1u, 2u, 7u, 2048u })
    {
        tests::TAllocationFailureFixture allocations;
        memory::CMemoryAllocator allocator{ &allocations, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        tests::TMemoryContextScope scope{ &context };
        SFixture fixture{ ctx };
        fixture.credits = credits;
        fixture.attempts = (credits == 1u) ? 1u : 3u;
        fixture.add(1u, 1u, 1u);
        fixture.add(1u, 2u, 2u);
        fixture.add(2u, 3u, 3u, EInputActivation::start, 98u);
        fixture.add(2u, 4u, 3u, EInputActivation::start, 98u);
        fixture.start();
        const std::size_t before = allocations.attempts;
        allocations.reject_all = true;
        for (const std::uint32_t code : { 1u, 2u, 3u, 98u }) fixture.set(code, 0.0f);
        fixture.send(true);
        fixture.set(1u, 1.0f);
        fixture.set(3u, 1.0f);
        fixture.set(98u, 1.0f);
        fixture.send();
        fixture.set(1u, 0.0f);
        fixture.set(2u, 1.0f);
        fixture.send();
        fixture.set(2u, 0.0f);
        fixture.send();
        TEST_EXPECT(ctx, ((allocations.attempts == before) && !allocations.failed));
        TEST_EXPECT(ctx, (fixture.count == 4u));
        if (!reference_count)
        {
            reference_count = fixture.count;
            std::memcpy(reference, fixture.records, (reference_count * sizeof(SActionInputV1)));
        }
        else TEST_EXPECT(ctx, (std::memcmp(reference, fixture.records, (reference_count * sizeof(SActionInputV1))) == 0));
    }
    SFixture fanout{ ctx };
    for (std::uint32_t index = 1u; index <= 300u; ++index) fanout.add(1u, index, 1u, EInputActivation::start);
    fanout.start();
    fanout.set(1u, 0.0f);
    fanout.send(true);
    fanout.set(1u, 1.0f);
    fanout.send(false, false);
    TEST_EXPECT(ctx, ((fanout.connection.snapshot().pending & k_actions) && (fanout.connection.snapshot().action_loss == 1u)));
    fanout.drain();
    TEST_EXPECT(ctx, (fanout.count == 256u));
    for (std::uint32_t index = 0u; index < fanout.count; ++index)
    {
        SPulseAction pulse;
        std::memcpy(&pulse, fanout.records[index].payload, sizeof(pulse));
        TEST_EXPECT(ctx, (pulse.binding == (index + 1u)));
    }
    fanout.recover();
    fanout.send();
    TEST_EXPECT(ctx, (fanout.count == 256u));
    fanout.set(1u, 0.0f);
    fanout.send();
    fanout.set(1u, 1.0f);
    fanout.send();
    TEST_EXPECT(ctx, (fanout.count == 556u));
}

void ordering_and_interruption(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.add(1u, 1u, 1u);
    fixture.start();
    fixture.set(1u, 0.0f);
    fixture.send(true);
    fixture.set(1u, 1.0f);
    ++fixture.observation.ordinal;
    fixture.observation.baseline = false;
    TEST_EXPECT(ctx, (fixture.connection.mapper().submit(fixture.observation) == EInputAdmission::accepted));
    TEST_EXPECT(ctx, (fixture.connection.mapper().submit(fixture.observation) == EInputAdmission::busy));
    TEST_EXPECT(ctx, (fixture.connection.text_target({ fixture.connection.view.connection, 1u, 1u }) == services::EServiceStatus::busy));
    SMapperBudget budget{ 8u, 1u };
    while (fixture.connection.mapper().advance(budget)) {}
    fixture.connection.mapper().eligible(false);
    fixture.recover();
    fixture.observation.kind = EObservationKind::transition;
    ++fixture.observation.ordinal;
    TEST_EXPECT(ctx, (fixture.connection.mapper().submit(fixture.observation) == EInputAdmission::baseline_required));
    fixture.connection.mapper().eligible(true);
    fixture.observation.kind = EObservationKind::snapshot;
    fixture.send(true);
    fixture.send();
    TEST_EXPECT(ctx, !fixture.count);
    fixture.connection.mapper().device(52u);
    TEST_EXPECT(ctx, (fixture.connection.mapper().submit(fixture.observation) == EInputAdmission::stale));
    fixture.recover();
    fixture.observation.device = 52u;
    fixture.set(1u, 0.0f);
    fixture.send(true);
    fixture.set(1u, 1.0f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 1u));
}

void cancellation_regressions(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.add(1u, 1u, 1u, EInputActivation::chord, 98u);
    fixture.start();
    fixture.set(1u, 0.0f);
    fixture.set(98u, 0.0f);
    fixture.send(true);
    fixture.set(1u, 1.0f);
    fixture.set(98u, 1.0f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 1u));
    SInputObservation unsolicited = fixture.observation;
    ++unsolicited.ordinal;
    unsolicited.baseline = true;
    TEST_EXPECT(ctx, (fixture.connection.mapper().submit(unsolicited) == EInputAdmission::invalid));
    fixture.set(98u, std::numeric_limits<float>::quiet_NaN());
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 2u) && !fixture.digital(1u) && (fixture.records[1u].flags == k_action_cancelled)));
    fixture.connection.mapper().discontinuity();
    fixture.recover();
    unsolicited = fixture.observation;
    unsolicited.baseline = true;
    TEST_EXPECT(ctx, (fixture.connection.mapper().submit(unsolicited) == EInputAdmission::stale));
    fixture.connection.mapper().device(0u);
    const std::uint64_t disconnected = fixture.connection.snapshot().action_generation;
    fixture.connection.mapper().device(0u);
    TEST_EXPECT(ctx, (fixture.connection.snapshot().action_generation == disconnected));
    ++unsolicited.ordinal;
    TEST_EXPECT(ctx, (fixture.connection.mapper().submit(unsolicited) == EInputAdmission::stale));

    SFixture pressure{ ctx };
    pressure.add(1u, 1u, 1u);
    for (std::uint32_t id = 2u; id <= 256u; ++id) pressure.add(2u, id, 1u, EInputActivation::start);
    pressure.start();
    pressure.set(1u, 0.0f);
    pressure.send(true);
    pressure.set(1u, 1.0f);
    pressure.send(false, false);
    TEST_EXPECT(ctx, !pressure.connection.snapshot().pending);
    TEST_EXPECT(ctx, (pressure.connection.text_target({ pressure.connection.view.connection, 1u, 1u }) == services::EServiceStatus::success));
    pressure.finish(false);
    TEST_EXPECT(ctx, ((pressure.connection.snapshot().pending & k_actions) && (pressure.connection.snapshot().action_loss == 1u)));

    SFixture text{ ctx };
    text.add(1u, 1u, 1u);
    text.start();
    text.set(1u, 0.0f);
    text.send(true);
    TEST_EXPECT(ctx, (text.connection.text_target({ text.connection.view.connection, 1u, 1u }) == services::EServiceStatus::success));
    text.finish();
    text.recover();
    text.connection.mapper().eligible(false);
    text.recover();
    SInputObservation decoded;
    decoded.device = text.observation.device;
    decoded.ordinal = 2u;
    decoded.time_ns = 202u;
    decoded.kind = EObservationKind::text;
    decoded.text.byte_count = 1u;
    decoded.text.payload[0] = 'a';
    TEST_EXPECT(ctx, (text.connection.mapper().submit(decoded) == EInputAdmission::accepted));
    text.connection.mapper().eligible(true);
    text.finish(false);
    STextInputV1 result;
    TEST_EXPECT(ctx, !text.connection.view.text->read(result));
    ++decoded.ordinal;
    TEST_EXPECT(ctx, (text.connection.mapper().submit(decoded) == EInputAdmission::accepted));
    text.finish(false);
    TEST_EXPECT(ctx, (text.connection.view.text->read(result) && (result.payload[0] == 'a') && (result.time_ns == 202u)));
}

void analog_rearm_and_transition_order(TTestContext& ctx)
{
    SFixture pad{ ctx };
    const SInputParameters parameters{ 0.0f, 0.125f, 0.25f, 0.5f, 0.25f, 1.0f };
    pad.add(1u, 1u, 270u, EInputActivation::direct, 0u, false, false, 0u, parameters);
    pad.start(EConnection::controller);
    pad.set(270u, 0.0f);
    pad.send(true);
    pad.set(270u, 1.0f, 1.0f);
    pad.send();
    SVectorAction value;
    std::memcpy(&value, pad.records[0u].payload, sizeof(value));
    TEST_EXPECT(ctx, ((pad.count == 1u) && (std::abs(value.x - 0.70710678f) < 0.000001f) && (value.x == value.y)));
    pad.connection.mapper().eligible(false);
    for (const float x : { 0.125f, 0.249f, 0.25f })
    {
        pad.set(270u, x);
        pad.send();
    }
    pad.recover();
    pad.connection.mapper().eligible(true);
    pad.set(270u, 0.5f);
    pad.send();
    TEST_EXPECT(ctx, (pad.count == 1u));
    pad.set(270u, 0.125f);
    pad.send();
    pad.set(270u, 0.5f);
    pad.send();
    TEST_EXPECT(ctx, ((pad.count == 3u) && (pad.scalar(1u) == 0.125f) && (pad.scalar(2u) == 0.5f)));

    SFixture desktop{ ctx };
    desktop.add(1u, 1u, 1u, EInputActivation::regular, 0u, true);
    desktop.start();
    desktop.set(1u, 0.0f);
    desktop.send(true);
    TEST_EXPECT(ctx, (desktop.connection.text_target({ desktop.connection.view.connection, 1u, 1u }) == services::EServiceStatus::success));
    desktop.finish();
    desktop.recover();
    SInputObservation transition;
    transition.device = desktop.observation.device;
    transition.ordinal = 2u;
    transition.time_ns = 201u;
    transition.count = 1u;
    transition.controls[0] = { 1u, 1.0f };
    TEST_EXPECT(ctx, (desktop.connection.mapper().submit(transition) == EInputAdmission::accepted));
    SInputObservation text = transition;
    text.kind = EObservationKind::text;
    text.ordinal = 3u;
    text.count = 0u;
    text.text.byte_count = 1u;
    text.text.payload[0] = 'b';
    TEST_EXPECT(ctx, (desktop.connection.mapper().submit(text) == EInputAdmission::busy));
    desktop.finish(false);
    TEST_EXPECT(ctx, (desktop.connection.mapper().submit(text) == EInputAdmission::accepted));
    desktop.finish(false);
    SActionInputV1 action;
    STextInputV1 decoded;
    TEST_EXPECT(ctx, (desktop.connection.view.actions->read(action) && desktop.connection.view.text->read(decoded)));
    TEST_EXPECT(ctx, ((action.sequence < decoded.sequence) && (action.time_ns == decoded.time_ns)));
    TEST_EXPECT(ctx, (desktop.connection.mapper().submit(text) == EInputAdmission::stale));
    ++text.ordinal;
    text.time_ns = 200u;
    TEST_EXPECT(ctx, (desktop.connection.mapper().submit(text) == EInputAdmission::stale));
    text.time_ns = 201u;
    text.text.payload[0] = 0xffu;
    TEST_EXPECT(ctx, (desktop.connection.mapper().submit(text) == EInputAdmission::invalid));
}

void preparation_failure(TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocations;
    memory::CMemoryAllocator allocator{ &allocations, &tests::allocate_with_failure, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    tests::TMemoryContextScope scope{ &context };
    SFixture fixture{ ctx };
    fixture.add(1u, 1u, 1u);
    fixture.start(EConnection::desktop, false);
    allocations.reject_all = true;
    TEST_EXPECT(ctx, (!fixture.connection.mapper().prepared() && !fixture.connection.mapper().prepare_step() && allocations.failed));
    TEST_EXPECT(ctx, !fixture.connection.start(fixture.notification.endpoint()));
    fixture.connection.close();
    TEST_EXPECT(ctx, (fixture.connection.mapper().submit(fixture.observation) == EInputAdmission::closed));
    while (!fixture.connection.mapper().clear_step()) {}
}

void delta_cancellation(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.add(1u, 1u, 134u, EInputActivation::direct);
    fixture.add(1u, 2u, 134u, EInputActivation::direct);
    fixture.start();
    fixture.set(134u, 0.0f);
    fixture.send(true);
    fixture.set(134u, 0.25f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 1u) && (fixture.scalar(0u) == 0.5f)));
    fixture.set(134u, std::numeric_limits<float>::quiet_NaN());
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 2u) && (fixture.scalar(1u) == 0.0f) &&
        (fixture.records[1u].flags == k_action_cancelled)));
    fixture.send();
    fixture.set(134u, 0.0f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 2u));
    fixture.set(134u, 0.5f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 3u) && (fixture.scalar(2u) == 1.0f) && !fixture.records[2u].flags));
    fixture.set(134u, std::numeric_limits<float>::max());
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 4u) && (fixture.scalar(3u) == 0.0f) &&
        (fixture.records[3u].flags == k_action_cancelled)));
    fixture.set(134u, 0.0f);
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 4u));
}

void grouped_raw_observations(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.add(1u, 1u, 1u, EInputActivation::chord, input_control::k_either_shift);
    fixture.add(2u, 2u, input_control::k_mouse_wheel, EInputActivation::direct);
    fixture.start();
    fixture.set(1u, 0.0f);
    fixture.set(input_control::k_either_shift, 0.0f);
    fixture.set(input_control::k_mouse_wheel, 0.0f);
    fixture.send(true);
    fixture.observation.kind = EObservationKind::transition;
    fixture.observation.count = 0u;
    fixture.set(1u, 1.0f);
    fixture.set(input_control::k_either_shift, 1.0f);
    fixture.observation.raw_count = 1u;
    fixture.observation.raw[0].kind = EActionKind::modifiers;
    fixture.credits = fixture.attempts = 1u;
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 2u) && (fixture.records[0].kind == EActionKind::modifiers) &&
        (fixture.records[1].kind == EActionKind::digital) && (fixture.digital(1u) == 1u)));
    fixture.observation.raw_count = 0u;
    fixture.observation.count = 0u;
    fixture.set(input_control::k_mouse_wheel, 0.25f, -0.5f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 3u) && (fixture.records[2].kind == EActionKind::delta)));
    fixture.observation.count = 0u;
    fixture.set(2u, 1.0f); // Omitted chord state survives; wheel does not replay.
    fixture.send();
    TEST_EXPECT(ctx, (fixture.count == 3u));
    fixture.observation.count = 0u;
    fixture.set(1u, 0.0f);
    fixture.send();
    TEST_EXPECT(ctx, ((fixture.count == 4u) && (fixture.digital(3u) == 0u)));
}

void raw_prefix_pressure(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    for (std::uint32_t id = 1u; id <= k_action_capacity; ++id)
    {
        fixture.add(id, id, 1u, EInputActivation::start);
    }
    fixture.start();
    fixture.set(1u, 0.0f);
    fixture.send(true);
    fixture.set(1u, 1.0f);
    fixture.observation.raw_count = 2u;
    fixture.observation.raw[0].kind = EActionKind::modifiers;
    fixture.observation.raw[1].kind = EActionKind::pointer;
    fixture.observation.raw[1].subject = input_control::k_mouse_position;
    fixture.send(false, false);
    TEST_EXPECT(ctx, ((fixture.connection.snapshot().action_loss == 1u) &&
        (fixture.connection.snapshot().pending & k_actions)));
    fixture.recover();
    TEST_EXPECT(ctx, ((fixture.count == k_action_capacity) && (fixture.records[0].kind == EActionKind::modifiers) &&
        (fixture.records[1].kind == EActionKind::pointer) && (fixture.records[2].kind == EActionKind::pulse)));
    const std::uint64_t committed = fixture.connection.snapshot().committed;
    fixture.finish();
    TEST_EXPECT(ctx, ((fixture.count == k_action_capacity) && (fixture.connection.snapshot().committed == committed)));
}

void desktop_ingestion(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.add(1u, 1u, 1u);
    fixture.start();
    CDesktopInput ingress;
    ingress.attach(fixture.connection, 51u);
    SInputObservation baseline;
    baseline.count = 1u;
    baseline.controls[0] = { 1u, 0.0f, 0.0f, false };
    baseline.time_ns = 200u;
    STextInputV1 text[16]{};
    std::uint32_t texts = 0u;
    const auto drive = [&]()
    {
        for (std::uint32_t turn = 0u; turn < 10000u; ++turn)
        {
            SMapperBudget budget{ 31u, 1u };
            while (budget.work)
            {
                bool advanced = false;
                if (!fixture.connection.mapper().pending()) advanced = ingress.advance(budget);
                if (!advanced) advanced = fixture.connection.mapper().advance(budget);
                if (!advanced) break;
            }
            STextInputV1 record;
            while (fixture.connection.view.text->read(record))
            {
                if (record.kind == ETextKind::scalar)
                {
                    TEST_EXPECT(ctx, (texts < 16u));
                    if (texts < 16u) text[texts++] = record;
                }
            }
            fixture.drain();
            if (!ingress.runnable() && !fixture.connection.mapper().pending()) break;
        }
    };
    TEST_EXPECT(ctx, (fixture.connection.snapshot().ingress_pending == k_desktop));
    ingress.quiet(baseline, ingress.epoch() - 1u);
    drive();
    TEST_EXPECT(ctx, (fixture.connection.snapshot().ingress_pending == k_desktop));
    ingress.quiet(baseline, ingress.epoch());
    drive();
    TEST_EXPECT(ctx, !fixture.connection.snapshot().ingress_pending);
    SDesktopTransaction press;
    press.count = 1u;
    press.controls[0] = { 1u, 1.0f, 0.0f, false };
    TEST_EXPECT(ctx, ingress.observe(press));
    drive();
    TEST_EXPECT(ctx, ((fixture.count == 1u) && (fixture.digital(0u) == 1u)));
    TEST_EXPECT(ctx, (fixture.connection.text_target({ fixture.connection.view.connection, 1u, 7u }) == services::EServiceStatus::success));
    ingress.cutover(k_text);
    fixture.finish();
    fixture.recover();
    ingress.quiet(baseline, ingress.epoch());
    drive();
    SDesktopTransaction unit;
    unit.text = true;
    unit.code_unit = 0xd83du;
    unit.repeats = 3u;
    TEST_EXPECT(ctx, ingress.observe(unit));
    drive(); // A pump boundary does not invalidate a high surrogate.
    TEST_EXPECT(ctx, !texts);
    unit.code_unit = 0xde00u;
    TEST_EXPECT(ctx, ingress.observe(unit));
    drive();
    TEST_EXPECT(ctx, ((texts == 3u) && (text[0].byte_count == 4u) && !ingress.malformed()));
    for (std::uint32_t index = 0u; index < texts; ++index)
    {
        const unsigned char expected[]{ 0xf0u, 0x9fu, 0x98u, 0x80u };
        TEST_EXPECT(ctx, (std::memcmp(text[index].payload, expected, sizeof(expected)) == 0));
    }
    unit.repeats = 1u;
    unit.code_unit = 0xdc00u;
    TEST_EXPECT(ctx, ingress.observe(unit));
    unit.code_unit = 'a';
    TEST_EXPECT(ctx, ingress.observe(unit));
    drive();
    TEST_EXPECT(ctx, ((texts == 4u) && (text[3].payload[0] == 'a') && (ingress.malformed() == 1u)));
    TEST_EXPECT(ctx, (fixture.connection.text_target({ fixture.connection.view.connection, 1u, 8u }) == services::EServiceStatus::success));
    ingress.cutover(k_text);
    fixture.finish();
    ingress.quiet(baseline, ingress.epoch());
    drive(); // Native gate clears while the reader's text boundary is unacked.
    unit.code_unit = 0xd83du;
    TEST_EXPECT(ctx, ingress.observe(unit));
    drive();
    fixture.recover();
    unit.code_unit = 0xde00u;
    TEST_EXPECT(ctx, ingress.observe(unit));
    drive();
    TEST_EXPECT(ctx, ((texts == 4u) && (ingress.malformed() == 2u)));
    unit.utf32 = true;
    unit.code_unit = 0xd800u;
    TEST_EXPECT(ctx, ingress.observe(unit));
    drive();
    TEST_EXPECT(ctx, ((texts == 4u) && (ingress.malformed() == 3u)));
    unit.utf32 = false;
    unit.code_unit = 'z';
    unit.repeats = k_text_capacity + 1u;
    TEST_EXPECT(ctx, ingress.observe(unit));
    const std::uint64_t old_text = fixture.connection.snapshot().text_generation;
    const std::uint64_t old_action = fixture.connection.snapshot().action_generation;
    for (std::uint32_t turn = 0u; turn < 1000u; ++turn)
    {
        SMapperBudget budget;
        if (!fixture.connection.mapper().pending()) (void)ingress.advance(budget);
        while (fixture.connection.mapper().advance(budget)) {}
    }
    TEST_EXPECT(ctx, ((fixture.connection.snapshot().text_generation == (old_text + 1u)) &&
        (fixture.connection.snapshot().action_generation == old_action) &&
        (fixture.connection.snapshot().ingress_pending == k_text)));
    fixture.recover();
    ingress.quiet(baseline, ingress.epoch());
    drive();
    TEST_EXPECT(ctx, (texts == 4u)); // Lost repeat suffix is not replayed.
    unit.repeats = 1u;
    unit.code_unit = 'c';
    TEST_EXPECT(ctx, ingress.observe(unit));
    SMapperBudget one_ingress{ 1u, 0u };
    TEST_EXPECT(ctx, ingress.advance(one_ingress));
    TEST_EXPECT(ctx, fixture.connection.mapper().pending());
    fixture.connection.capture(17u, ECaptureReason::none);
    fixture.connection.capture_lost();
    ingress.cutover(k_actions);
    drive();
    TEST_EXPECT(ctx, ((texts == 5u) && (text[4].payload[0] == 'c')));
    const std::uint64_t text_before_loss = fixture.connection.snapshot().text_generation;
    for (std::uint32_t index = 0u; index < k_desktop_capacity; ++index) TEST_EXPECT(ctx, ingress.observe(unit));
    TEST_EXPECT(ctx, !ingress.observe(unit));
    TEST_EXPECT(ctx, ((fixture.connection.snapshot().text_generation == (text_before_loss + 1u)) &&
        (fixture.connection.snapshot().pending == k_desktop)));
    fixture.recover();
    ingress.quiet(baseline, ingress.epoch());
    drive();
    ingress.cutover(k_desktop);
    ingress.quiet(baseline, ingress.epoch());
    SMapperBudget baseline_budget;
    TEST_EXPECT(ctx, ingress.advance(baseline_budget));
    fixture.finish();
    ingress.cutover(k_text); // Supersede a completed-but-not-released baseline.
    drive();
    TEST_EXPECT(ctx, (fixture.connection.snapshot().ingress_pending == k_desktop));
    ingress.quiet(baseline, ingress.epoch());
    drive();
    TEST_EXPECT(ctx, !fixture.connection.snapshot().ingress_pending);
    const std::uint64_t generation = fixture.connection.snapshot().action_generation;
    for (std::uint32_t index = 0u; index < k_desktop_capacity; ++index) TEST_EXPECT(ctx, ingress.observe(press));
    TEST_EXPECT(ctx, !ingress.observe(press));
    TEST_EXPECT(ctx, ((fixture.connection.snapshot().action_generation == (generation + 1u)) &&
        (fixture.connection.snapshot().ingress_loss == 2u)));
    fixture.recover(); // Early acknowledgement cannot reopen native ingress.
    drive();
    TEST_EXPECT(ctx, (fixture.connection.snapshot().ingress_pending == k_desktop));
    baseline.controls[0].x = 1.0f;
    ingress.quiet(baseline, ingress.epoch());
    drive();
    const std::uint32_t before = fixture.count;
    TEST_EXPECT(ctx, ingress.observe(press));
    drive();
    TEST_EXPECT(ctx, (fixture.count == before));
    ingress.detach();
    TEST_EXPECT(ctx, !ingress.observe(press));
}

}   // namespace

void run_input_mapper_tests(tests::TTestContext& ctx)
{
    digital_traces(ctx);
    rearm_and_selective_cancellation(ctx);
    analog_traces(ctx);
    partitions_pressure_and_allocations(ctx);
    ordering_and_interruption(ctx);
    cancellation_regressions(ctx);
    analog_rearm_and_transition_order(ctx);
    preparation_failure(ctx);
    delta_cancellation(ctx);
    grouped_raw_observations(ctx);
    raw_prefix_pressure(ctx);
    desktop_ingestion(ctx);
}
