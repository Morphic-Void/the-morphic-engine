
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    Audio_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    31 Oct 26
//
//  Hardware-free signal, allocation, budget and real service-thread exercises.

#include "framework/system/system_ids.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/module/module_messages.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>
#include "audio/runtime/audio_execution.hpp"
#include "containers/TInstance.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "audio/runtime/miniaudio_adapter.hpp"
#include "audio/runtime/vendor_allocator.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/Audio_test_suite.hpp"

void run_audio_preamble_service(tests::TTestContext& ctx, const modules::SExecutionV1& execution, void* const context,
    std::atomic<unsigned>& opens, std::atomic<unsigned>& starts, std::atomic<unsigned>& closes,
    std::atomic<std::uint32_t>& complete, std::atomic<bool>& output_wait);

namespace audio
{
struct SMiniaudioTestAccess
{
    //  Exercise native API and mutex ownership with a device-free test backend.
    static modules::ETestRunStatus cleanup_guard(CMiniaudioOutput& output) noexcept { return output.open(true); }
    static bool mutex_failure(CMiniaudioOutput& output, const unsigned failure) noexcept
    {
        SContextProbe probe;
        probe.fail_mutex = failure;
        const auto result = output.open(true, &probe);
        const bool closed = output.close() && output.close();
        const unsigned acquired = (failure == 1u) ? 0u : ((failure == 2u) ? 1u : 2u);
        return (result == modules::ETestRunStatus::failed) && closed &&
            (probe.mutex_attempts == ((failure == 1u) ? 1u : 2u)) &&
            (probe.mutex_acquisitions == acquired) && (probe.mutex_releases == acquired) &&
            (probe.api_attempts == 1u) && (probe.api_releases == 1u) &&
            (probe.backend_acquisitions == 1u) && (probe.backend_releases == 1u);
    }
};
}

namespace
{
using tests::TTestContext;
using namespace platform::threading;

void signal_tests(TTestContext& ctx)
{
    for (const std::uint32_t rate : { 44100u, 48000u, 96000u })
    {
        audio::CToneTest whole, split;
        TEST_EXPECT(ctx, (whole.reset(rate) && split.reset(rate)));
        const auto end = rate * 5u;
        const auto padded = end + (rate / 4u);
        std::vector<float> reference(((padded + 33u) * 2u), 9.0f), chunks(reference.size(), 9.0f);
        whole.fill(reference.data(), (padded + 33u));
        std::uint32_t cursor = 0u;
        while (cursor < (padded + 33u))
        {
            const auto remaining = padded + 33u - cursor;
            const auto count = (remaining < 127u) ? remaining : 127u;
            split.fill((chunks.data() + (cursor * 2u)), count);
            cursor += count;
        }
        TEST_EXPECT(ctx, (reference == chunks));
        TEST_EXPECT(ctx, (whole.observe().complete && (whole.observe().frames == padded)));
        TEST_EXPECT(ctx, ((reference[0] == 0.0f) && (reference[(end - 1u) * 2u] == 0.0f)));
        bool stereo = true, bounded = true, silent = true;
        double energy_up = 0.0, energy_plateau = 0.0, energy_down = 0.0;
        unsigned crossings = 0u;
        for (std::uint32_t i = 0; i < (padded + 33u); ++i)
        {
            const float value = reference[i * 2u];
            stereo = stereo && (value == reference[(i * 2u) + 1u]);
            bounded = bounded && std::isfinite(value) && (std::abs(value) <= 0.1f);
            if (i >= end)
            {
                silent = silent && (value == 0.0f);
            }
            if (i < rate) energy_up += value * value;
            if ((i >= (rate * 2u)) && (i < (rate * 3u)))
            {
                energy_plateau += value * value;
                if ((value > 0.0f) && (reference[(i - 1u) * 2u] <= 0.0f))
                {
                    ++crossings;
                }
            }
            if ((i >= (rate * 4u)) && (i < end))
            {
                energy_down += value * value;
            }
        }
        TEST_EXPECT(ctx, (stereo && bounded && silent));
        TEST_EXPECT(ctx, ((crossings == 261u) || (crossings == 262u)));
        TEST_EXPECT(ctx, ((energy_plateau > (9.0 * energy_up)) && (energy_plateau > (9.0 * energy_down))));
        TEST_EXPECT(ctx, whole.reset(rate));
        whole.fill(reference.data(), end);
        TEST_EXPECT(ctx, (!whole.observe().complete && (whole.observe().frames == end)));
        whole.fill(reference.data(), ((rate / 4u) - 1u));
        TEST_EXPECT(ctx, !whole.observe().complete);
        whole.fill(reference.data(), 1u);
        TEST_EXPECT(ctx, whole.observe().complete);
        TEST_EXPECT(ctx, whole.reset(rate));
        whole.cancel();
        whole.fill(reference.data(), 33u);
        TEST_EXPECT(ctx, ((whole.observe().frames == 0u) && !whole.observe().complete));
        TEST_EXPECT(ctx, ((reference[0] == 0.0f) && (reference[65] == 0.0f)));
        whole.interrupt();
        TEST_EXPECT(ctx, whole.observe().interrupted);
    }
    audio::CToneTest invalid;
    TEST_EXPECT(ctx, (!invalid.reset(0u) && !invalid.reset(~0u)));
}

void allocator_tests(TTestContext& ctx)
{
    tests::TAllocatorFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext memory{ allocator };
    TEST_EXPECT(ctx, (audio::vendor_allocate(0u, &memory) == nullptr));
    TEST_EXPECT(ctx, (audio::vendor_allocate(std::numeric_limits<std::size_t>::max(), &memory) == nullptr));
    void* block = audio::vendor_reallocate(nullptr, 37u, &memory);
    TEST_EXPECT(ctx, (block != nullptr));
    if (!block) return;
    TEST_EXPECT(ctx, ((reinterpret_cast<std::uintptr_t>(block) % alignof(std::max_align_t)) == 0u));
    std::memset(block, 0x5a, 37u);
    fixture.reject_allocation = true;
    TEST_EXPECT(ctx, (audio::vendor_reallocate(block, 100u, &memory) == nullptr));
    TEST_EXPECT(ctx, ((static_cast<unsigned char*>(block)[36] == 0x5a) && (memory.get_live_allocation_count() == 1u)));
    fixture.reject_allocation = false;
    block = audio::vendor_reallocate(block, 100u, &memory);
    TEST_EXPECT(ctx, ((block != nullptr) && (static_cast<unsigned char*>(block)[36] == 0x5a)));
    block = audio::vendor_reallocate(block, 11u, &memory);
    TEST_EXPECT(ctx, ((block != nullptr) && (static_cast<unsigned char*>(block)[10] == 0x5a)));
    //  Free/reallocate on a vendor-like thread with no engine TLS installation.
    std::thread vendor([&] {
        block = audio::vendor_reallocate(block, 64u, &memory);
        audio::vendor_free(block, &memory);
    });
    vendor.join();
    TEST_EXPECT(ctx, memory.is_attribution_empty());
    block = audio::vendor_allocate(13u, &memory);
    TEST_EXPECT(ctx, (audio::vendor_reallocate(block, 0u, &memory) == nullptr));
    audio::vendor_free(nullptr, &memory);
    TEST_EXPECT(ctx, memory.is_attribution_empty());
    audio::CMiniaudioOutput output{ memory };
    for (unsigned i = 0u; i < 3u; ++i)
    {
        TEST_EXPECT(ctx, (audio::SMiniaudioTestAccess::cleanup_guard(output) == modules::ETestRunStatus::failed));
        TEST_EXPECT(ctx, output.close());
        TEST_EXPECT(ctx, memory.is_attribution_empty());
    }
    for (unsigned failure : { 0u, 1u, 2u })
    {
        TEST_EXPECT(ctx, audio::SMiniaudioTestAccess::mutex_failure(output, failure));
        TEST_EXPECT(ctx, memory.is_attribution_empty());
    }
    fixture.reject_allocation = true;
    TEST_EXPECT(ctx, (audio::SMiniaudioTestAccess::cleanup_guard(output) == modules::ETestRunStatus::failed));
    TEST_EXPECT(ctx, (output.close() && memory.is_attribution_empty()));
}

struct FakeOutput : audio::IAudioOutput
{
    modules::ETestRunStatus opening{ modules::ETestRunStatus::success };
    bool start_ok{ true }, close_ok{ true };
    std::atomic<unsigned> opens{ 0u }, starts{ 0u }, closes{ 0u };
    std::atomic<std::uint32_t> frames{ 0u }, complete{ 0u }, interrupted{ 0u };
    modules::ETestRunStatus open() noexcept override
    {
        ++opens;
        frames = 0u;
        complete = 0u;
        interrupted = 0u;
        return opening;
    }
    bool start() noexcept override
    {
        ++starts;
        return start_ok;
    }
    bool close() noexcept override
    {
        ++closes;
        return close_ok;
    }
    audio::SProgress observe() const noexcept override { return { frames.load(), (complete.load() != 0u), (interrupted.load() != 0u) }; }
};

struct FakeClock
{
    struct Entry { const threading::CWaitDeadline* key{}; std::uint64_t end{}; unsigned sets{}; };
    std::array<Entry, 3> entries{};
    std::uint64_t awake_ms{ 0u };
    bool fail_set{ false }, fail_query{ false };
    FakeOutput* complete_on_query{ nullptr };
    audio::SRunClock clock() noexcept
    {
        return { this,
            [](void* const data, threading::CWaitDeadline& deadline, const std::uint64_t ms) noexcept -> WaitError {
                auto& self = *static_cast<FakeClock*>(data);
                if (self.fail_set)
                {
                    return { EWaitError::unsupported, 71u };
                }
                for (auto& entry : self.entries)
                {
                    if (!entry.key || (entry.key == &deadline))
                    {
                        entry.key = &deadline;
                        entry.end = self.awake_ms + ms;
                        ++entry.sets;
                        return {};
                    }
                }
                return { EWaitError::invalid_argument, 0u };
            },
            [](void* const data, const threading::CWaitDeadline& deadline, std::uint64_t& ns) noexcept -> WaitError {
                auto& self = *static_cast<FakeClock*>(data);
                ns = 0u;
                if (self.complete_on_query)
                {
                    self.complete_on_query->complete = 1u;
                }
                if (self.fail_query)
                {
                    return { EWaitError::native_failure, 72u };
                }
                for (auto& entry : self.entries)
                {
                    if (entry.key == &deadline)
                    {
                        ns = (entry.end > self.awake_ms) ? (entry.end - self.awake_ms) * 1000000u : 0u;
                        return {};
                    }
                }
                return { EWaitError::invalid_argument, 0u };
            } };
    }
};

struct SAudioCommunication
{
    std::vector<threading::CErasedPodMsg> replies;
    std::vector<modules::SExecutionIntent> intents;
    bool full{ false }, control_full{ false }, control_invalid{ false }, active{ true };
    FakeOutput* output{ nullptr };
    unsigned closes_at_quiescence{ 0u };

    static modules::ESubmission MV_STD_ABI_CALL pod(void* const context, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SAudioCommunication& self = *static_cast<SAudioCommunication*>(context);
        if ((destination.kind != modules::SDestination::EKind::connection) || (destination.token.value != 31u))
        {
            return modules::ESubmission::invalid;
        }
        if (!self.active) return modules::ESubmission::closed;
        if (self.full) return modules::ESubmission::busy;
        self.replies.push_back(message);
        return modules::ESubmission::accepted;
    }
    static modules::ESubmission MV_STD_ABI_CALL intent(void* const context, const modules::SExecutionIntent& intent) noexcept
    {
        SAudioCommunication& self = *static_cast<SAudioCommunication*>(context);
        if (self.control_invalid) return modules::ESubmission::invalid;
        if (self.control_full) return modules::ESubmission::busy;
        self.intents.push_back(intent);
        if ((intent.kind == modules::SExecutionIntent::EKind::connection_quiescent) && self.output)
        {
            self.closes_at_quiescence = self.output->closes.load();
        }
        return modules::ESubmission::accepted;
    }
    static modules::SConnectionQuery MV_STD_ABI_CALL query(void* const context, const modules::SDestination& destination) noexcept
    {
        const SAudioCommunication& self = *static_cast<SAudioCommunication*>(context);
        return { destination.token, system_ids::executive, self.active ? 1u : 0u, self.active ? 0u : 1u };
    }
    static const modules::SCommunicationV1 functions;
};
const modules::SCommunicationV1 SAudioCommunication::functions{
    &SAudioCommunication::pod, nullptr, &SAudioCommunication::intent, &SAudioCommunication::query };

void execution_tests(TTestContext& ctx)
{
    FakeOutput output;
    FakeClock clock;
    SAudioCommunication transport;
    transport.output = &output;
    const modules::CCommunication communication{ SAudioCommunication::functions, &transport };
    modules::SExecutionContext context;
    context.communication = &communication;
    audio::CAudioExecution execution{ context, output, clock.clock() };
    modules::SExecutionProgress progress;
    auto invoke = [&](const modules::EExecutionEvent event, const threading::CErasedPodMsg* const message = nullptr)
    {
        modules::SInvocation invocation;
        invocation.event = event;
        invocation.message = message;
        invocation.source.kind = connections::SSource::EKind::direct;
        invocation.source.address = system_ids::executive;
        invocation.source.connection.value = 31u;
        invocation.notification.connection.value = 31u;
        TEST_EXPECT(ctx, (execution.handle(invocation, progress) == modules::EExecutionStatus::success));
    };
    auto request = [&](const int correlation, const module_ids::id_type module = module_ids::audio)
    {
        const threading::CErasedPodMsg message = threading::make_pod_message(correlation, modules::RunTestsRequest{ module });
        invoke(modules::EExecutionEvent::message, &message);
    };
    auto check_reply = [&](const std::size_t index, const int correlation, const modules::ETestRunStatus status)
    {
        TEST_EXPECT(ctx, (transport.replies.size() > index));
        if (transport.replies.size() <= index) return;
        modules::RunTestsResult result;
        TEST_EXPECT(ctx, (transport.replies[index].copy_payload_to(result) &&
            (transport.replies[index].query_async_slot() == correlation) && (result.status == status)));
    };

    transport.control_full = true;
    invoke(modules::EExecutionEvent::start);
    TEST_EXPECT(ctx, ((progress.ready == 0u) && (progress.work_pending == 0u) && (output.opens == 0u)));
    transport.control_full = false;
    invoke(modules::EExecutionEvent::service_async);
    TEST_EXPECT(ctx, ((progress.ready == 1u) && (transport.intents.size() == 1u) && (output.opens == 0u)));
    request(10);
    TEST_EXPECT(ctx, ((output.starts == 1u) && (progress.next_wake_ns == 20000000u)));
    //  Repeated input does not renew the existing overall/progress budgets.
    clock.awake_ms = 11u;
    transport.full = true;
    request(11);
    invoke(modules::EExecutionEvent::service_async);
    TEST_EXPECT(ctx, ((progress.direct_input == 0u) && (progress.work_pending == 0u) &&
        (progress.next_wake_ns == 9000000u) && transport.replies.empty()));
    TEST_EXPECT(ctx, ((clock.entries[0].sets == 1u) && (clock.entries[1].sets == 1u)));
    transport.full = false;
    invoke(modules::EExecutionEvent::service_async);
    check_reply(0u, 11, modules::ETestRunStatus::busy);
    TEST_EXPECT(ctx, (progress.direct_input == 1u));
    output.complete = 1u;
    transport.full = true;
    invoke(modules::EExecutionEvent::service_async);
    TEST_EXPECT(ctx, ((output.closes == 1u) && (transport.replies.size() == 1u) &&
        (progress.next_wake_ns == UINT64_MAX) && (progress.work_pending == 0u)));
    transport.full = false;
    invoke(modules::EExecutionEvent::service_async);
    check_reply(1u, 10, modules::ETestRunStatus::success);
    invoke(modules::EExecutionEvent::service_async);
    TEST_EXPECT(ctx, (transport.replies.size() == 2u));
    request(12, module_ids::rendering);
    invoke(modules::EExecutionEvent::service_async);
    check_reply(2u, 12, modules::ETestRunStatus::invalid_request);
    request(13);
    transport.active = false;
    invoke(modules::EExecutionEvent::connection_closing);
    TEST_EXPECT(ctx, ((output.closes == 2u) && (transport.closes_at_quiescence == 2u) &&
        (transport.replies.size() == 3u)));
    transport.active = true;
    request(14);
    clock.awake_ms += 2000u;
    invoke(modules::EExecutionEvent::service_async);
    check_reply(3u, 14, modules::ETestRunStatus::failed);
    TEST_EXPECT(ctx, (output.closes == 3u));
    request(15);
    modules::SInvocation invalid;
    invalid.event = modules::EExecutionEvent::message;
    TEST_EXPECT(ctx, (execution.handle(invalid, progress) == modules::EExecutionStatus::failed));
    invoke(modules::EExecutionEvent::stop);
    TEST_EXPECT(ctx, (execution.complete() && (progress.work_complete == 1u) && (output.closes == 4u)));
    FakeOutput rejected_output;
    SAudioCommunication rejected_transport;
    rejected_transport.control_invalid = true;
    const modules::CCommunication rejected_communication{ SAudioCommunication::functions, &rejected_transport };
    context.communication = &rejected_communication;
    audio::CAudioExecution rejected{ context, rejected_output, clock.clock() };
    modules::SInvocation start;
    start.event = modules::EExecutionEvent::start;
    TEST_EXPECT(ctx, ((rejected.handle(start, progress) == modules::EExecutionStatus::failed) && !rejected_output.opens.load()));
    start.event = modules::EExecutionEvent::stop;
    TEST_EXPECT(ctx, ((rejected.handle(start, progress) == modules::EExecutionStatus::success) && progress.work_complete));
}

void run_tests(TTestContext& ctx)
{
    for (const auto failure : { modules::ETestRunStatus::unavailable, modules::ETestRunStatus::failed })
    {
        FakeOutput output; output.opening = failure;
        FakeClock clock;
        audio::CAudioRun run{ output, clock.clock() };
        run.begin();
        TEST_EXPECT(ctx, (!run.active() && (run.result() == failure) && (output.closes == 1u) && (output.starts == 0u)));
    }
    for (unsigned scenario = 0; scenario < 10u; ++scenario)
    {
        FakeOutput output;
        FakeClock clock;
        audio::CAudioRun run{ output, clock.clock() };
        if (scenario == 0u) output.start_ok = false;
        if (scenario == 1u) clock.fail_set = true;
        run.begin();
        if (scenario == 2u)
        {
            clock.fail_query = true;
            run.check();
        }
        if (scenario == 3u)
        {
            clock.awake_ms = 2000u;
            run.check();
        }
        if (scenario == 4u)
        {
            output.interrupted = 1u;
            run.check();
        }
        if (scenario == 5u)
        {
            output.complete = 1u;
            output.close_ok = false;
            run.check();
        }
        if (scenario == 6u)
        {
            run.wait_failed({ EWaitStatus::native_failure, 0u, 73u });
        }
        if (scenario == 7u)
        {
            run.wait_failed({ EWaitStatus::control_released, (~0u), 0u });
        }
        if (scenario == 8u)
        {
            run.wait_failed({ EWaitStatus::unsupported, 0u, 0u });
        }
        if (scenario == 9u)
        {
            run.wait_failed({ EWaitStatus::invalid_argument, 0u, 0u });
        }
        TEST_EXPECT(ctx, (!run.active() && (run.result() == modules::ETestRunStatus::failed) && (output.closes == 1u)));
    }
    {
        FakeOutput output; FakeClock clock;
        audio::CAudioRun run{ output, clock.clock() };
        run.begin();
        //  A flood of ordinary wakeups never renews either budget.
        for (unsigned i = 0; i < 5000u; ++i) run.check();
        TEST_EXPECT(ctx, ((clock.entries[0].sets == 1u) && (clock.entries[1].sets == 1u) && (clock.entries[2].sets == 1u)));
        clock.awake_ms = 1999u; output.frames = 1u; run.check();
        TEST_EXPECT(ctx, (run.active() && (clock.entries[1].sets == 2u)));
        clock.awake_ms = 3998u; run.check();
        TEST_EXPECT(ctx, run.active());
        clock.awake_ms = 3999u; run.check();
        TEST_EXPECT(ctx, (!run.active() && (run.result() == modules::ETestRunStatus::failed)));
    }
    {
        FakeOutput output; FakeClock clock;
        audio::CAudioRun run{ output, clock.clock() };
        run.begin();
        for (unsigned i = 1u; i <= 15u; ++i)
        {
            clock.awake_ms = i * 1000u;
            output.frames = i;
            run.check();
        }
        TEST_EXPECT(ctx, (!run.active() && (run.result() == modules::ETestRunStatus::failed) && (clock.entries[0].sets == 1u)));
    }
    {
        FakeOutput output; FakeClock clock;
        audio::CAudioRun run{ output, clock.clock() };
        run.begin(); clock.awake_ms = 2000u; clock.complete_on_query = &output; run.check();
        TEST_EXPECT(ctx, (!run.active() && (run.result() == modules::ETestRunStatus::success)));
        clock.complete_on_query = nullptr;
        run.begin(); run.cancel(); run.cancel();
        TEST_EXPECT(ctx, ((run.result() == modules::ETestRunStatus::cancelled) && (output.closes == 2u) && (output.opens == 2u)));
    }
}

struct SAudioServiceExecution
{
    FakeOutput output;
    std::atomic<bool> output_wait{ false };
    TInstance<audio::CAudioExecution> execution;
    platform::threading::CPlatformThreadId created_on, destroyed_on;
    unsigned destroyed{};
    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        auto& self = *static_cast<SAudioServiceExecution*>(context->creation_context);
        if (!self.execution.emplace(*context, self.output)) return modules::EExecutionStatus::failed;
        self.created_on = platform::threading::query_current_thread_id();
        *runtime = &self;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        auto& self = *static_cast<SAudioServiceExecution*>(runtime);
        const modules::EExecutionStatus status = self.execution->handle(*invocation, *progress);
        self.output_wait.store((progress->direct_input == 0u), std::memory_order_release);
        return status;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        auto& self = *static_cast<SAudioServiceExecution*>(runtime);
        if (!self.execution->complete()) return modules::EExecutionStatus::failed;
        self.execution.reset();
        self.destroyed_on = platform::threading::query_current_thread_id();
        ++self.destroyed;
        return modules::EExecutionStatus::success;
    }
};

void service_tests(TTestContext& ctx)
{
    SAudioServiceExecution state;
    const modules::SExecutionV1 execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &SAudioServiceExecution::create, &SAudioServiceExecution::handle, &SAudioServiceExecution::destroy };
    run_audio_preamble_service(ctx, execution, &state, state.output.opens, state.output.starts,
        state.output.closes, state.output.complete, state.output_wait);
    TEST_EXPECT(ctx, ((state.destroyed == 1u) && (state.created_on == state.destroyed_on) &&
        (state.created_on != platform::threading::query_current_thread_id())));
}
}   //  namespace

int run_audio_tests()
{
    TTestContext ctx;
    signal_tests(ctx);
    allocator_tests(ctx);
    run_tests(ctx);
    execution_tests(ctx);
    service_tests(ctx);
    return ctx.report("Audio");
}
