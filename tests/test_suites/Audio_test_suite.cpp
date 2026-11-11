
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    Audio_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    31 Oct 26
//
//  Hardware-free signal, allocation, budget and real service-thread exercises.

#include "framework/threading/CThreadContext.hpp"
#include "tests/support/thread_helpers.hpp"
#include "framework/system/system_ids.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/module/module_messages.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>
#include "audio/runtime/audio_thread.hpp"
#include "audio/runtime/miniaudio_adapter.hpp"
#include "audio/runtime/vendor_allocator.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/test_suites/Audio_test_suite.hpp"

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
    bool reject_delivery{ false };
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

template<typename F> bool await(const F predicate)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= end)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

std::uint32_t MV_STD_ABI_CALL fake_service(void* const data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    FakeOutput& output = tests::TThreadFixtureState<FakeOutput>::get();
    tests::TModuleIdScope module_scope{ module_ids::audio };
    if (output.reject_delivery)
    {
        resources.worker_to_host_msgs.deallocate();
    }
    return audio::serve_audio(resources, output);
}

void service_tests(TTestContext& ctx)
{
    FakeOutput output;
    platform::system::CPerfCountConversion conversion;
    TEST_EXPECT(ctx, conversion.init());
    tests::SDirectHostFixture host;
    host.data_capacity = 32u;
    threading::ThreadConfig config{ thread_ids::audio, module_ids::audio, EThreadPriority::Normal, &fake_service, &tests::TThreadFixtureState<FakeOutput>::prepare, &output };
    threading::CThreadPackage package{ config, conversion };
    host::SClientRecord worker{ module_ids::audio, 101u, &package };
    tests::SDirectPeerFixture executive{ host, conversion, module_ids::executive, 102u,
        modules::test_connections::policies, modules::test_connections::policy_count };
    TEST_EXPECT(ctx, (executive.ready && host.attach(worker) && worker.install_host_execution(host.restrictions.reader())));
    const bool started = package.startup();
    TEST_EXPECT(ctx, started);
    if (!started)
    {
        TEST_EXPECT(ctx, executive.shutdown());
        return;
    }
    auto pump = [&] {
        worker.terminal_before_drain = worker.terminal();
        host.drain_controls(worker);
        worker.incoming_drained = worker.terminal_before_drain;
        host.progress(); executive.pump(); host.progress();
    };
    connections::SToken token;
    auto connect = [&](const std::int32_t correlation) {
        TEST_EXPECT(ctx, executive.client.open(module_ids::audio, modules::test_connections::version, correlation));
        connections::Result result;
        std::int32_t slot = -1;
        TEST_EXPECT(ctx, await([&]
        {
            pump();
            return executive.client.take_result(result, slot);
        }));
        TEST_EXPECT(ctx, ((result.status == connections::EStatus::success) && (slot == correlation)));
        token = result.token;
    };
    connect(100);
    TEST_EXPECT(ctx, await([&] { return package.query_state() == threading::EThreadRunState::Waiting; }));
    TEST_EXPECT(ctx, (output.opens == 0u));
    auto post = [&](const int slot, const module_ids::id_type module = module_ids::audio) {
        const auto message = threading::make_pod_message(slot, modules::RunTestsRequest{ module });
        connections::EPost posted = connections::EPost::full;
        TEST_EXPECT(ctx, await([&]
        {
            posted = executive.client.post(token, message);
            return posted != connections::EPost::full;
        }));
        TEST_EXPECT(ctx, (posted == connections::EPost::queued));
    };
    auto receive = [&](const int slot, const modules::ETestRunStatus status, const unsigned passed, const unsigned failed, const unsigned skipped) {
        threading::CErasedPodMsg message;
        const bool got = await([&]
        {
            pump();
            return connections::was_read(executive.client.read(token, message));
        });
        TEST_EXPECT(ctx, got);
        modules::RunTestsResult result;
        TEST_EXPECT(ctx, (got && message.copy_payload_to(result) && (message.query_async_slot() == slot) && (result.module == module_ids::audio) &&
            (result.status == status) && (result.passed == passed) && (result.failed == failed) && (result.skipped == skipped)));
    };
    post(7, module_ids::rendering); receive(7, modules::ETestRunStatus::invalid_request, 0, 0, 0);
    post(threading::k_uncorrelated_slot); receive(threading::k_uncorrelated_slot, modules::ETestRunStatus::invalid_request, 0, 0, 0);
    post(-2); receive(-2, modules::ETestRunStatus::invalid_request, 0, 0, 0);
    //  Fill Audio's result pipe and leave one rejection retained. Freeing a
    //  slot must wake/retry it without any unrelated Host traffic.
    //  At most two distinct 32-record publications can be retained across the
    //  reader and staged buffers; output storage duplicates producer backlog.
    for (int index = 0; index < 65; ++index)
    {
        post((1000 + index), module_ids::rendering);
    }
    TEST_EXPECT(ctx, await([&] {
        return executive.client.has_data() && (package.query_state() == threading::EThreadRunState::Waiting);
    }));
    for (int index = 0; index < 65; ++index)
    {
        receive((1000 + index), modules::ETestRunStatus::invalid_request, 0, 0, 0);
    }
    post(8);
    TEST_EXPECT(ctx, await([&] { return output.starts == 1u; }));
    post(9); receive(9, modules::ETestRunStatus::busy, 0, 0, 0);
    output.complete = 1u;
    receive(8, modules::ETestRunStatus::success, 1, 0, 0);
    TEST_EXPECT(ctx, (output.closes == 1u));
    post(10);
    TEST_EXPECT(ctx, await([&] { return output.starts == 2u; }));
    executive.client.close(token);
    bool cleanup_before_retirement = true;
    TEST_EXPECT(ctx, await([&] {
        pump();
        if (host.connections.empty() && (output.closes != 2u))
        {
            cleanup_before_retirement = false;
        }
        return host.connections.empty() && executive.client.idle();
    }));
    TEST_EXPECT(ctx, (cleanup_before_retirement && (output.closes == 2u)));
    connect(101);
    post(11);
    TEST_EXPECT(ctx, await([&] { return output.starts == 3u; }));
    host.connections.close_client(worker);
    package.request_exit();
    TEST_EXPECT(ctx, await([&]
    {
        pump();
        return worker.execution_quiescent() && host.connections.empty() && executive.client.idle();
    }));
    TEST_EXPECT(ctx, ((output.closes == 3u) && !host.failed));
    TEST_EXPECT(ctx, (worker.retire_host_execution(false) && package.shutdown()));
    TEST_EXPECT(ctx, (host.detach(worker) && executive.shutdown()));

    FakeOutput rejected;
    rejected.reject_delivery = true;
    config.prepare_context = &rejected;
    threading::CThreadPackage broken{ config, conversion };
    TEST_EXPECT(ctx, !broken.startup()); // Registration fails before device work.
    TEST_EXPECT(ctx, ((rejected.starts == 0u) && (rejected.opens == 0u) && !broken.retains_execution()));
}
}   //  namespace

int run_audio_tests()
{
    TTestContext ctx;
    signal_tests(ctx);
    allocator_tests(ctx);
    run_tests(ctx);
    service_tests(ctx);
    return ctx.report("Audio");
}
