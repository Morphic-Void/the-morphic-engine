
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TextureBatch_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Oct 26
//
//  Drive the real rendering DLL with a controlled batch queue. Holding a
//  dequeued item makes discard and exit ordering deterministic.

#include <atomic>
#include <cstring>
#include <type_traits>

#include "debug/service.hpp"
#include "host/runtime/asset_service.hpp"
#include "module/bound_module.hpp"
#include "platform/path/native_path.hpp"
#include "platform/threading/processor_relax.hpp"
#include "rendering/runtime/texture_jobs.hpp"
#include "assets/asset_messages.hpp"
#include "rendering/texture/texture_requests.hpp"
#include "system/service_messages.hpp"
#include "threading/messages/message_helpers.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/test_suites/TextureBatch_test_suite.hpp"
#include "threading/CThreadPackage.hpp"
#include "types/fp16data_t.hpp"

namespace texture_batch_tests
{

static_assert(!std::is_copy_constructible_v<rendering::CTextureEncodeJob> &&
    !std::is_move_constructible_v<rendering::CTextureEncodeJob>);
static_assert(!std::is_copy_constructible_v<rendering::CTextureDecodeJob> &&
    !std::is_move_constructible_v<rendering::CTextureDecodeJob>);

struct SRealFixture
{
    platform::system::CPerfCountConversion conversion;
    TInstance<debug_system::CDebugServiceState> debug_service;
    modules::CBoundModule module;
    threading::SBatchShared shared;
    TInstance<threading::CThreadPackage> package;
    threading::SBatchWork held;
    platform::threading::FThreadEntry entry{ nullptr };

    ~SRealFixture() noexcept
    {
        stop();
        if (debug_service) (void)debug_service->stop();
        (void)module.unbind();
        shared.wake.release_control();
    }

    bool initialise() noexcept
    {
        constexpr modules::SAdvertisedIdentity identity{
            module_ids::executable, { modules::k_binding_abi_major, 0u },
            modules::k_binding_abi_major, modules::k_binding_abi_major };
        debug_service = TInstance<debug_system::CDebugServiceState>::create();
        if (!debug_service || !conversion.init() || !shared.wake.acquire_control()) return false;
        const std::string log = test_environment::test_log_path("texture_batch");
        const std::string direct_log = test_environment::test_log_path("texture_batch_direct");
        const std::string filename = test_environment::binary_path(test_environment::module_filename("MorphicRendering").c_str());
        const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
        modules::FModuleFunction function = nullptr;
        if (!debug_service->configure_log_paths(log.c_str(), direct_log.c_str()) ||
            !debug_service->open_logs() || !debug_service->start() || !path.is_ready() ||
            !module.bind(path, module_ids::rendering, identity) ||
            !module.install(test_environment::system_registry_view(), module_ids::rendering,
                test_environment::rendering_memory_context(), debug_service.operator->()) ||
            !module.query_function(system_type_ids::rendering_thread_function, function))
        {
            return false;
        }
        entry = reinterpret_cast<platform::threading::FThreadEntry>(function);
        return true;
    }

    bool start(const bool queued) noexcept
    {
        shared.runner_count = queued ? 1u : 0u;
        const threading::ThreadConfig config{ thread_ids::rendering, module_ids::rendering,
            platform::threading::EThreadPriority::Normal, entry, (&modules::CBoundModule::prepare_thread), (&module) };
        return package.emplace(config, conversion) &&
            package->install_batch_client(shared, module_ids::rendering, (&module)) && package->startup();
    }

    bool acquire() noexcept
    {
        threading::transports::TAcquiredArenaSlot<threading::SBatchWork,
            threading::k_batch_channel_capacity> acquired(shared.work);
        if (!acquired) return false;
        held = *acquired;
        return acquired.recycle();
    }

    bool complete(const threading::EBatchCompletion completion) noexcept
    {
        if (held.requester == nullptr) return false;
        const bool delivered = held.requester->complete({ held.correlation, completion });
        held = {};
        return delivered;
    }

    void stop() noexcept
    {
        if (!package) return;
        package->request_exit();
        //  Also release work held when a preceding assertion could not proceed.
        if (held.requester != nullptr)
        {
            (void)complete(threading::EBatchCompletion::discarded_requester_exiting);
        }
        (void)tests::wait_until(conversion, [this]() noexcept
        {
            while (acquire()) (void)complete(threading::EBatchCompletion::discarded_requester_exiting);
            const threading::EThreadRunState state = package->query_state();
            return (state == threading::EThreadRunState::Exited) || (state == threading::EThreadRunState::Failed);
        });
        //  Transport owners must be destroyed before unbinding the real DLL.
        threading::CErasedOwnerMsg response;
        while (package->read(response)) response = {};
        (void)package->shutdown();
        package.reset();
    }


};

struct SStopRealRendering
{
    SRealFixture& fixture;
    ~SStopRealRendering() noexcept { fixture.stop(); }
};

static void test_module_test_protocol(tests::TTestContext& ctx, SRealFixture& fixture)
{
    SStopRealRendering stop{ fixture };
    const bool started = fixture.start(false);
    TEST_EXPECT(ctx, started);
    if (!started) return;
    auto send = [&fixture](const std::int32_t slot, const module_ids::id_type module) noexcept
    {
        return fixture.package->post(threading::make_pod_message(slot, RunTestsRequest{ module }));
    };
    auto receive = [&fixture](threading::CErasedPodMsg& message) noexcept
    {
        return tests::wait_until(fixture.conversion, [&]() noexcept { return fixture.package->read(message); });
    };
    threading::CErasedPodMsg reply;
    RunTestsResult result;
    TEST_EXPECT(ctx, send(80, module_ids::executive));
    TEST_EXPECT(ctx, receive(reply) && reply.copy_payload_to(result));
    TEST_EXPECT(ctx, reply.query_async_slot() == 80 && result.status == ETestRunStatus::invalid_request);

    // Test callers may use the full positive correlation range; resource
    // acquisition replies are distinguished by type, not the caller's slot.
    constexpr std::int32_t run_slot = INT32_MAX;
    constexpr std::int32_t busy_slot = INT32_MAX - 1;
    TEST_EXPECT(ctx, send(run_slot, module_ids::rendering));
    threading::CErasedOwnerMsg input;
    TEST_EXPECT(ctx, tests::wait_until(fixture.conversion, [&]() noexcept { return fixture.package->read(input); }));
    TEST_EXPECT(ctx, input.is_message_a<RawAssetTransfer>());
    TEST_EXPECT(ctx, send(busy_slot, module_ids::rendering));
    TEST_EXPECT(ctx, receive(reply) && reply.copy_payload_to(result));
    TEST_EXPECT(ctx, reply.query_async_slot() == busy_slot && result.status == ETestRunStatus::busy);

    //  Failed setup reports a terminal result and permits another run.
    AssetResult failed;
    failed.status = EAssetStatus::allocation_failed;
    TEST_EXPECT(ctx, fixture.package->post(threading::make_pod_message(input.query_async_slot(), failed)));
    input = {};
    TEST_EXPECT(ctx, receive(reply) && reply.copy_payload_to(result));
    TEST_EXPECT(ctx, reply.query_async_slot() == run_slot && result.status == ETestRunStatus::failed &&
        result.passed == 0u && result.failed == 0u && result.skipped == 23u);

    TEST_EXPECT(ctx, send(83, module_ids::rendering));
    TEST_EXPECT(ctx, tests::wait_until(fixture.conversion, [&]() noexcept { return fixture.package->read(input); }));
    fixture.package->request_exit();
    TEST_EXPECT(ctx, tests::wait_until(fixture.conversion, [&]() noexcept
    {
        return fixture.package->query_state() == threading::EThreadRunState::Exiting;
    }));
    host::CAssetService service;
    TEST_EXPECT(ctx, service.initialise());
    //  The fixture reply arrives after exit. Rendering must release the new
    //  retained asset and wait for disposal before completing and terminating.
    service.request(input, *fixture.package);
    TEST_EXPECT(ctx, receive(reply));
    AssetDisposeRequest disposal;
    TEST_EXPECT(ctx, reply.copy_payload_to(disposal) && disposal.asset);
    threading::CErasedPodMsg premature;
    TEST_EXPECT(ctx, !fixture.package->read(premature));
    TEST_EXPECT(ctx, fixture.package->query_state() == threading::EThreadRunState::Exiting);
    service.request_disposal(disposal, reply.query_async_slot(), *fixture.package);
    service.complete_disposals();
    TEST_EXPECT(ctx, receive(reply) && reply.copy_payload_to(result));
    TEST_EXPECT(ctx, reply.query_async_slot() == 83 && result.module == module_ids::rendering &&
        result.status == ETestRunStatus::cancelled && result.passed == 0u && result.failed == 0u && result.skipped == 23u);
    TEST_EXPECT(ctx, service.is_idle() && !service.failed());
    service.deallocate();
    TEST_EXPECT(ctx, tests::wait_until(fixture.conversion, [&]() noexcept
    {
        return fixture.package->query_state() == threading::EThreadRunState::Exited;
    }));
    fixture.stop();
    TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
}

} // namespace texture_batch_tests

#include "tests/support/rendering_service_fixture.hpp"

namespace texture_batch_tests
{
static void test_jobs(tests::TTestContext& ctx, SFixture& fixture)
{
    TextureEncodeRequest input;
    SStopRendering stop{ fixture };
    const bool allocated = input.input.allocate(16u, 4u, 16u);
    TEST_EXPECT(ctx, allocated);
    if (!allocated) return;
    for (std::uint32_t y = 0u; y < 4u; ++y) std::memset(input.input.row_data(y), 255, 16u);
    const TextureEncodeWork encode{ &input };
    using threading::EBatchCompletion;
    for (const bool queued : { false, true })
    {
        const bool started = fixture.start(queued);
        TEST_EXPECT(ctx, started);
        if (!started) return;
        {
            if (queued)
            {
                SLocalOwner discarded = fixture.operation<TextureEncodeWork, TextureEncodeResult>(
                    ctx, encode, 9, true, EBatchCompletion::discarded_context_install_failed, false);
                const TextureEncodeResult* const result = discarded.payload<TextureEncodeResult>();
                TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::codec_failed) &&
                    !result->texture.view().is_ready()));
            }
            SLocalOwner encoded = fixture.operation<TextureEncodeWork, TextureEncodeResult>(
                ctx, encode, 10, queued, EBatchCompletion::executed, false);
            const TextureEncodeResult* const source = encoded.payload<TextureEncodeResult>();
            TEST_EXPECT(ctx, ((source != nullptr) && (source->status == ETextureStatus::success) &&
                source->texture.view().is_ready()));
            if ((source == nullptr) || (source->status != ETextureStatus::success)) return;
            const CByteConstView bytes = source->texture.buffer.const_view();
            TextureDecodeRequest request;
            const TextureDecodeWork decode{ (&request), (&bytes) };
            if (queued)
            {
                SLocalOwner discarded = fixture.operation<TextureDecodeWork, TextureDecodeResult>(
                    ctx, decode, 11, true, EBatchCompletion::discarded_context_install_failed, false);
                const TextureDecodeResult* const result = discarded.payload<TextureDecodeResult>();
                TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::codec_failed) &&
                    !result->texture.view().is_ready()));
            }
            SLocalOwner decoded = fixture.operation<TextureDecodeWork, TextureDecodeResult>(
                ctx, decode, 12, queued, EBatchCompletion::executed, queued);
            const TextureDecodeResult* const result = decoded.payload<TextureDecodeResult>();
            TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::success) &&
                result->texture.view().is_ready() &&
                (result->texture.buffer.size() == 64u) && (result->texture.buffer.data()[3] > 250u)));
        }
        fixture.stop();
        TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
    }
    for (const bool decode_job : { false, true })
    {
        const bool started = fixture.start(true);
        TEST_EXPECT(ctx, started);
        if (!started) return;
        {
            SLocalOwner discarded;
            if (decode_job)
            {
                TextureDecodeRequest request;
                const CByteConstView empty;
                const TextureDecodeWork decode{ (&request), (&empty) };
                discarded = fixture.operation<TextureDecodeWork, TextureDecodeResult>(
                    ctx, decode, 20, true, EBatchCompletion::discarded_requester_exiting, true);
                const TextureDecodeResult* const result = discarded.payload<TextureDecodeResult>();
                TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::codec_failed)));
            }
            else
            {
                discarded = fixture.operation<TextureEncodeWork, TextureEncodeResult>(
                    ctx, encode, 20, true, EBatchCompletion::discarded_requester_exiting, true);
                const TextureEncodeResult* const result = discarded.payload<TextureEncodeResult>();
                TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::codec_failed)));
            }
        }
        fixture.stop();
        TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
    }
}

//  Workers enter together, but terminal responses are published explicitly so
//  result ordering and draining do not depend on relative codec speeds.
struct SConcurrentJobs
{
    struct SWorker
    {
        SConcurrentJobs* group{ nullptr };
        threading::SBatchWork work;
        platform::threading::CThread thread;
        thread_ids::id_type identity{ thread_ids::batch_runner_00 };
        bool prepared{ false };
    };

    SFixture& fixture;
    SWorker workers[4];
    std::atomic<std::uint32_t> entered{ 0u };
    std::atomic<bool> release{ false };

    explicit SConcurrentJobs(SFixture& source) noexcept : fixture{ source } {}
    ~SConcurrentJobs() noexcept
    {
        join();
        for (SWorker& worker : workers)
        {
            if (worker.work.requester != nullptr)
                (void)complete(worker, threading::EBatchCompletion::discarded_requester_exiting);
        }
    }

    static std::uint32_t MV_STD_ABI_CALL run(void* const data) noexcept
    {
        SWorker& worker = *static_cast<SWorker*>(data);
        tests::TMemoryContextScope memory{ worker.work.memory_context };
        worker.prepared = worker.group->fixture.module.prepare_batch_thread(
            worker.identity, worker.work.memory_context);
        worker.group->entered.fetch_add(1u, std::memory_order_release);
        while (!worker.group->release.load(std::memory_order_acquire)) platform::threading::processor_relax();
        if (worker.prepared) worker.work.function(worker.work.object);
        return worker.prepared ? 0u : 1u;
    }

    bool acquire() noexcept
    {
        for (SWorker& worker : workers)
        {
            if (!tests::wait_until(fixture.conversion, [this]() noexcept { return fixture.acquire(); })) return false;
            worker.work = fixture.held;
            fixture.held = {};
            worker.group = this;
        }
        return true;
    }

    bool start(const std::uint32_t count) noexcept
    {
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            workers[index].identity = thread_ids::ops::make_id(thread_ids::ops::make_index(
                thread_ids::batch_runner_00_index.raw_value() + index));
            if (!workers[index].thread.create((&run), (&workers[index]))) return false;
        }
        return tests::wait_until(fixture.conversion, [this, count]() noexcept
        {
            return entered.load(std::memory_order_acquire) == count;
        });
    }

    void join() noexcept
    {
        release.store(true, std::memory_order_release);
        for (SWorker& worker : workers)
        {
            if (worker.thread.is_valid()) (void)worker.thread.join_and_close();
        }
    }

    static bool complete(SWorker& worker, const threading::EBatchCompletion completion) noexcept
    {
        if (worker.work.requester == nullptr) return false;
        const bool delivered = worker.work.requester->complete({ worker.work.correlation, completion });
        worker.work = {};
        return delivered;
    }
};

static void test_saturated_requests(tests::TTestContext& ctx, SFixture& fixture)
{
    //  Keep borrowed storage alive even if fixture cleanup follows a failed check.
    TextureDecodeRequest request;
    const CByteConstView empty;
    SStopRendering stop{ fixture };
    const bool ready = fixture.start(true);
    TEST_EXPECT(ctx, ready);
    if (!ready) return;
    constexpr std::uint32_t count = threading::k_batch_channel_capacity;
    for (std::uint32_t index = 0u; index <= count; ++index)
    {
        SLocalWork message;
        message.set_async_slot(static_cast<std::int32_t>(1000u + index));
        message.assign_payload(TextureDecodeWork{ (&request), (&empty) });
        const bool posted = tests::wait_until(fixture.conversion, [&fixture, &message]() noexcept
        {
            return fixture.package->post(message);
        });
        TEST_EXPECT(ctx, posted);
        if (!posted) return;
    }
    //  The last request executes inline while the first 128 remain queued.
    SLocalReply response;
    const bool received = tests::wait_until(fixture.conversion, [&fixture, &response]() noexcept
    {
        return fixture.package->read(response);
    });
    TEST_EXPECT(ctx, received);
    if (!received) return;
    TEST_EXPECT(ctx, (response.query_async_slot() == static_cast<std::int32_t>(1000u + count)));
    {
        SLocalOwner owner = response.take_owner();
        const auto* const result = owner.payload<TextureDecodeResult>();
        TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::invalid_input)));
    }
    TEST_EXPECT(ctx, (fixture.package->batch_client()->outstanding() == count));
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        const bool acquired = fixture.acquire();
        TEST_EXPECT(ctx, acquired);
        if (!acquired) return;
        const bool prepared = fixture.module.prepare_batch_thread(thread_ids::batch_runner_00, fixture.held.memory_context);
        TEST_EXPECT(ctx, prepared);
        if (!prepared) return;
        tests::TMemoryContextScope memory{ fixture.held.memory_context };
        fixture.held.function(fixture.held.object);
        TEST_EXPECT(ctx, fixture.complete(threading::EBatchCompletion::executed));
        const bool completed = tests::wait_until(fixture.conversion, [&fixture, &response]() noexcept
        {
            return fixture.package->read(response);
        });
        TEST_EXPECT(ctx, completed);
        if (!completed) return;
        TEST_EXPECT(ctx, (response.query_async_slot() == static_cast<std::int32_t>(1000u + index)));
        SLocalOwner owner = response.take_owner();
        const auto* const result = owner.payload<TextureDecodeResult>();
        TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::invalid_input)));
    }
    TEST_EXPECT(ctx, (fixture.package->batch_client()->outstanding() == 0u));
    fixture.stop();
    TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
}

}   //  namespace texture_batch_tests

#include "tests/test_suites/ShaderBatch_cases.hpp"

int run_texture_batch_tests()
{
    tests::TTestContext ctx;
    //  Module installation is once per binding lifetime, including on loaders
    //  that retain the native image after dlclose. Restart only its thread.
    texture_batch_tests::SRealFixture real;
    TEST_EXPECT(ctx, real.initialise());
    texture_batch_tests::test_module_test_protocol(ctx, real);
    texture_batch_tests::SFixture fixture;
    const bool ready = fixture.initialise();
    TEST_EXPECT(ctx, ready);
    if (ready)
    {
        texture_batch_tests::test_jobs(ctx, fixture);
        texture_batch_tests::test_saturated_requests(ctx, fixture);
        shader_batch_tests::run(ctx, fixture);
        shader_batch_tests::resource_groups(ctx, fixture);
        shader_batch_tests::options_and_admission(ctx, fixture);
        shader_batch_tests::poc(ctx, fixture);
        shader_batch_tests::limits(ctx);
        shader_batch_tests::compiler_scratch(ctx);
    }
    return ctx.report("TextureBatch");
}
