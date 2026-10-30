
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TextureService_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Oct 26
//
//  Typed Rendering services against the generic Host acquisition boundary.

#include <atomic>
#include <cstring>
#include "tests/support/resource_service_fixture.hpp"
#include "tests/test_suites/TextureService_test_suite.hpp"
#include "tests/support/test_allocator.hpp"
#include "types/fp16data_t.hpp"

namespace
{
using namespace texture_batch_tests;
TextureEncodeRequest input(tests::TTestContext& ctx, bool hdr, std::uint32_t width)
{
    TextureEncodeRequest request;
    request.format = hdr ? image::texture::EInputFormat::rgba32f : image::texture::EInputFormat::rgba8;
    request.options.encoding = hdr ? image::texture::EEncoding::uastc_hdr_4x4 : image::texture::EEncoding::uastc_ldr_4x4;
    if (width == 0u) return request;
    TEST_EXPECT(ctx, request.input.allocate(width * (hdr ? 16u : 4u), 4u, 16u));
    if (!request.input.is_ready()) return request;
    for (std::uint32_t y = 0; y < 4u; ++y)
    {
        if (hdr)
        {
            const float pixel[]{ 4.0f, 2.0f, 1.0f, 1.0f };
            for (std::uint32_t x = 0; x < width; ++x) std::memcpy(request.input.row_data(y) + x * sizeof(pixel), pixel, sizeof(pixel));
        }
        else std::memset(request.input.row_data(y), 255, width * 4u);
    }
    return request;
}
struct SJobs
{
    struct SJob { SJobs* owner{}; threading::SBatchWork work; platform::threading::CThread thread; } jobs[4];
    std::atomic<unsigned> entered{ 0u };
    std::atomic<bool> start{ false };
    static std::uint32_t MV_STD_ABI_CALL run(void* value) noexcept
    {
        auto& job = *static_cast<SJob*>(value);
        tests::TMemoryContextScope memory{ job.work.memory_context };
        job.owner->entered.fetch_add(1u, std::memory_order_release);
        while (!job.owner->start.load(std::memory_order_acquire)) platform::threading::processor_relax();
        job.work.function(job.work.object);
        return 0u;
    }
    bool acquire(SResourceFixture& fixture)
    {
        for (auto& job : jobs)
        {
            if (!fixture.acquire_job()) return false;
            job.owner = this; job.work = fixture.rendering.held; fixture.rendering.held = {};
        }
        return true;
    }
    bool execute(SFixture& fixture)
    {
        for (auto& job : jobs) if (!job.thread.create(&run, &job)) return false;
        const bool all = tests::wait_until(fixture.conversion, [&]() noexcept { return entered.load(std::memory_order_acquire) == 4u; });
        start.store(true, std::memory_order_release);
        for (auto& job : jobs) (void)job.thread.join_and_close();
        return all;
    }
    bool complete(unsigned index)
    {
        auto& work = jobs[index].work;
        const bool result = work.requester && work.requester->complete({ work.correlation, threading::EBatchCompletion::executed });
        work = {}; return result;
    }
    ~SJobs()
    {
        start.store(true, std::memory_order_release);
        for (auto& job : jobs)
        {
            if (job.thread.is_valid()) (void)job.thread.join_and_close();
            if (job.work.requester) (void)job.work.requester->complete({ job.work.correlation, threading::EBatchCompletion::discarded_requester_exiting });
        }
    }
};
bool receive(tests::TTestContext& ctx, SResourceFixture& fixture, rendering::SResourceResult& result, std::int32_t expected)
{
    std::int32_t slot = -1;
    const bool got = tests::wait_until(fixture.rendering.conversion, [&]() noexcept { return fixture.receive(result, slot); });
    TEST_EXPECT(ctx, got && slot == expected && fixture.valid && !fixture.assets.failed());
    return got && slot == expected;
}
void concurrent_lifetimes(tests::TTestContext& ctx, SFixture& rendering)
{
    SResourceFixture fixture{ rendering };
    TEST_EXPECT(ctx, fixture.initialise());
    CAssetId sources[2];
    {
        SJobs jobs;
        for (int i = 0; i < 4; ++i) TEST_EXPECT(ctx, fixture.submit(input(ctx, i == 1, (i == 3) ? 0u : (4u + i * 4u)), 100 + i));
        TEST_EXPECT(ctx, jobs.acquire(fixture) && jobs.execute(rendering));
        for (unsigned i : { 3u, 1u, 2u, 0u })
        {
            TEST_EXPECT(ctx, jobs.complete(i));
            rendering::SResourceResult result;
            if (!receive(ctx, fixture, result, 100 + i)) return;
            TEST_EXPECT(ctx, result.status == (i == 3 ? EAssetStatus::invalid_request : EAssetStatus::success));
            if (i < 3) TEST_EXPECT(ctx, result.encoded.view().is_ready() && result.encoded.description.width == 4u + i * 4u);
            if (i < 2) sources[i] = fixture.raw(ctx, std::move(result.encoded.buffer));
        }
    }
    {
        SJobs jobs;
        TEST_EXPECT(ctx, fixture.submit(TextureDecodeRequest{ sources[0], image::texture::EStorageFormat::rgba8 }, 200));
        TEST_EXPECT(ctx, fixture.submit(TextureDecodeRequest{ sources[1], image::texture::EStorageFormat::rgba16f }, 201));
        TEST_EXPECT(ctx, tests::wait_until(rendering.conversion, [&]() noexcept {
            fixture.pump(); return rendering.package->batch_client()->outstanding() == 2u;
        }));
        TEST_EXPECT(ctx, fixture.submit(input(ctx, false, 12u), 202));
        TEST_EXPECT(ctx, fixture.submit(TextureEncodeRequest{}, 203));
        TEST_EXPECT(ctx, jobs.acquire(fixture));
        fixture.assets.request_disposal({ sources[0] }, 204, *fixture.client);
        fixture.assets.complete_disposals();
        TEST_EXPECT(ctx, fixture.submit(TextureDecodeRequest{ sources[0] }, 205));
        rendering::SResourceResult rejected;
        TEST_EXPECT(ctx, receive(ctx, fixture, rejected, 205) && rejected.status == EAssetStatus::invalid_asset);
        rendering.package->request_exit();
        TEST_EXPECT(ctx, jobs.execute(rendering));
        for (unsigned i : { 3u, 2u, 1u, 0u })
        {
            TEST_EXPECT(ctx, jobs.complete(i));
            rendering::SResourceResult result;
            if (!receive(ctx, fixture, result, 200 + i)) return;
            TEST_EXPECT(ctx, result.status == (i == 3 ? EAssetStatus::invalid_request : EAssetStatus::success));
            if (i < 2)
            {
                const auto view = result.decoded.view();
                TEST_EXPECT(ctx, view.is_ready() && view.description.width == (i ? 8u : 4u));
                if (view.is_ready() && i == 0) TEST_EXPECT(ctx, view.bytes.data()[0] > 250u);
                if (view.is_ready() && i == 1)
                {
                    std::uint16_t bits; std::memcpy(&bits, view.bytes.data(), sizeof(bits));
                    const float red = static_cast<float>(fp16data_t::fromBits(bits)) * view.description.hdr_scale;
                    TEST_EXPECT(ctx, red > 3.5f && red < 4.5f);
                }
            }
            if (i != 0) TEST_EXPECT(ctx, fixture.messages.empty());
        }
        AssetDisposeResult disposed;
        std::int32_t slot = -1;
        TEST_EXPECT(ctx, tests::wait_until(rendering.conversion, [&]() noexcept { return fixture.receive_host(disposed, slot); }));
        TEST_EXPECT(ctx, slot == 204 && disposed.asset == sources[0] && disposed.status == EAssetStatus::success);
    }
    TEST_EXPECT(ctx, !fixture.requests->pending() && rendering.package->batch_client()->outstanding() == 0u);
}
void release_protocol(tests::TTestContext& ctx, SFixture& rendering)
{
    SResourceFixture fixture{ rendering };
    TEST_EXPECT(ctx, fixture.initialise());
    CByteBuffer bytes; TEST_EXPECT(ctx, bytes.resize(16u)); std::memset(bytes.data(), 0x5a, bytes.size());
    const auto source = fixture.raw(ctx, std::move(bytes));
    TEST_EXPECT(ctx, fixture.submit(TextureDecodeRequest{ source }, 40));
    TEST_EXPECT(ctx, fixture.acquire_job());
    const auto held = rendering.held;
    rendering.held = {};
    fixture.assets.request_disposal({ source }, 41, *fixture.client);
    fixture.hold_release = true;
    { tests::TMemoryContextScope memory{ held.memory_context }; held.function(held.object); }
    TEST_EXPECT(ctx, held.requester->complete({ held.correlation, threading::EBatchCompletion::executed }));
    fixture.pump();
    AssetReleaseRequest release;
    TEST_EXPECT(ctx, fixture.deferred_release.copy_payload_to(release) && release.acquisition);
    const auto identity = fixture.deferred_release.query_async_slot();
    auto bad = threading::make_pod_message(identity, AssetReleaseResult{ CAssetAcquisitionId{}, EAssetStatus::success });
    TEST_EXPECT(ctx, !fixture.requests->complete(bad));
    bad = threading::make_pod_message(identity, AssetAcquireResult{});
    TEST_EXPECT(ctx, !fixture.requests->complete(bad));
    bad = threading::make_pod_message(identity + 500, AssetReleaseResult{ release.acquisition, EAssetStatus::success });
    TEST_EXPECT(ctx, !fixture.requests->complete(bad));
    rendering::SResourceResult premature;
    std::int32_t slot = -1;
    TEST_EXPECT(ctx, !fixture.receive(premature, slot) && fixture.messages.empty() && !fixture.assets.is_idle());
    fixture.hold_release = false;
    TEST_EXPECT(ctx, SResourceFixture::post(&fixture, fixture.deferred_release));
    TEST_EXPECT(ctx, receive(ctx, fixture, premature, 40) && premature.status == EAssetStatus::invalid_request);
    TEST_EXPECT(ctx, !fixture.requests->complete(threading::make_pod_message(identity, AssetReleaseResult{ release.acquisition, EAssetStatus::success })));
    TEST_EXPECT(ctx, !fixture.requests->complete(threading::SBatchResponse{ held.correlation, threading::EBatchCompletion::executed }));
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, tests::wait_until(rendering.conversion, [&]() noexcept { return fixture.receive_host(disposed, slot); }));
    TEST_EXPECT(ctx, slot == 41 && disposed.status == EAssetStatus::success && fixture.assets.is_idle());
}

void delivery_failures(tests::TTestContext& ctx, SFixture& rendering)
{
    for (const bool exiting : { false, true })
    {
        SResourceFixture fixture{ rendering };
        TEST_EXPECT(ctx, fixture.initialise());
        fixture.reject_acquire = true;
        TEST_EXPECT(ctx, fixture.submit(TextureDecodeRequest{}, 70));
        rendering::SResourceResult result;
        TEST_EXPECT(ctx, receive(ctx, fixture, result, 70) && result.status == EAssetStatus::delivery_failed);
        TEST_EXPECT(ctx, !fixture.requests->pending());
        fixture.reject_acquire = false;
        CByteBuffer bytes;
        TEST_EXPECT(ctx, bytes.resize(16u));
        std::memset(bytes.data(), 0x5a, bytes.size());
        const auto source = fixture.raw(ctx, std::move(bytes));
        TEST_EXPECT(ctx, fixture.submit(TextureDecodeRequest{ source }, 71));
        fixture.assets.request_disposal({ source }, 72, *fixture.client);
        if (exiting)
        {
            // The acquisition has been accepted, but no batch work is admitted
            // after exit. Its failure still releases the source before completion.
            rendering.package->request_exit();
            TEST_EXPECT(ctx, receive(ctx, fixture, result, 71) && result.status == EAssetStatus::conditioning_failed);
            AssetDisposeResult disposed;
            std::int32_t slot = -1;
            TEST_EXPECT(ctx, tests::wait_until(rendering.conversion, [&]() noexcept { return fixture.receive_host(disposed, slot); }));
            TEST_EXPECT(ctx, slot == 72 && disposed.status == EAssetStatus::success && fixture.assets.is_idle());
        }
        else
        {
            TEST_EXPECT(ctx, fixture.acquire_job());
            fixture.reject_release = true;
            TEST_EXPECT(ctx, rendering.complete(threading::EBatchCompletion::discarded_context_install_failed));
            fixture.pump();
            std::int32_t slot = -1;
            TEST_EXPECT(ctx, fixture.requests->failed() && fixture.requests->pending() && !fixture.requests->jobs_pending());
            TEST_EXPECT(ctx, !fixture.receive(result, slot) && fixture.messages.empty() && !fixture.assets.is_idle());
            // Terminal teardown owns the abandoned acquisition after jobs drain.
        }
    }
}

void allocation_failures(tests::TTestContext& ctx, SFixture& rendering)
{
    TEST_EXPECT(ctx, rendering.start(false));
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, tests::allocate_with_failure, tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator,
        system_ids::ops::make_system_id(module_ids::rendering, thread_ids::rendering) };
    auto attempt = [&]() noexcept {
        tests::TMemoryContextScope memory{ &context };
        rendering::CResourceRequests service{ *rendering.package->batch_client(), {} };
        if (!service.initialise()) return;
        if (!service.submit(TextureEncodeRequest{}, 80)) { TEST_EXPECT(ctx, !service.pending()); return; }
        rendering::SResourceResult result;
        std::int32_t slot = -1;
        TEST_EXPECT(ctx, service.receive(result, slot) && slot == 80);
        TEST_EXPECT(ctx, result.status == (allocation.failed ? EAssetStatus::allocation_failed : EAssetStatus::invalid_request));
        TEST_EXPECT(ctx, !service.pending() && !service.jobs_pending());
    };
    attempt();
    const auto count = allocation.attempts;
    TEST_EXPECT(ctx, count != 0u && context.is_attribution_empty());
    for (std::size_t i = 0; i < count; ++i)
    {
        allocation = {};
        allocation.fail_on = i;
        attempt();
        TEST_EXPECT(ctx, allocation.failed && context.is_attribution_empty());
    }
    rendering.stop();
}
}

int run_texture_service_tests()
{
    tests::TTestContext ctx;
    SFixture rendering;
    TEST_EXPECT(ctx, rendering.initialise());
    concurrent_lifetimes(ctx, rendering);
    release_protocol(ctx, rendering);
    delivery_failures(ctx, rendering);
    allocation_failures(ctx, rendering);
    return ctx.report("TextureService");
}
