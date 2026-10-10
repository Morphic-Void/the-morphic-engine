
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TextureBatch_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Oct 26
//
//  Drive the real rendering DLL with a controlled batch queue. Holding a
//  dequeued item makes discard and exit ordering deterministic.

#include <cstring>
#include <iostream>
#include <type_traits>

#include "debug/service.hpp"
#include "module/bound_module.hpp"
#include "platform/path/native_path.hpp"
#include "platform/threading/processor_relax.hpp"
#include "rendering/runtime/texture_jobs.hpp"
#include "system/transported_types.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/test_context.hpp"
#include "tests/test_suites/TextureBatch_test_suite.hpp"
#include "threading/CThreadPackage.hpp"

namespace texture_batch_tests
{

static_assert(!std::is_copy_constructible_v<rendering::CTextureEncodeJob> &&
    !std::is_move_constructible_v<rendering::CTextureEncodeJob>);
static_assert(!std::is_copy_constructible_v<rendering::CTextureDecodeJob> &&
    !std::is_move_constructible_v<rendering::CTextureDecodeJob>);

template<typename TPredicate>
static bool wait_until(const platform::system::CPerfCountConversion& conversion, TPredicate&& predicate) noexcept
{
    platform::system::CPerfCounter timer;
    (void)timer.update();
    while (timer.query_delta() < (conversion.query_ticks_per_second() * 5u))
    {
        if (predicate()) return true;
        platform::threading::processor_relax();
    }
    return false;
}

struct SFixture
{
    platform::system::CPerfCountConversion conversion;
    TInstance<debug_system::CDebugServiceState> debug_service;
    modules::CBoundModule module;
    threading::SBatchShared shared;
    TInstance<threading::CThreadPackage> package;
    threading::SBatchWork held;
    platform::threading::FThreadEntry entry{ nullptr };

    ~SFixture() noexcept
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
        const std::string filename = test_environment::binary_path("MorphicRendering.dll");
        const platform::path::NativePath path = platform::path::makeNativePath(filename.c_str());
        modules::FModuleFunction function = nullptr;
        if (!debug_service->configure_log_paths(log.c_str(), direct_log.c_str()) ||
            !debug_service->open_logs() || !debug_service->start() || !path.is_ready() ||
            !module.bind(path, module_ids::render_vulkan, identity) ||
            !module.install(test_environment::system_registry_view(), module_ids::render_vulkan,
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
        const threading::ThreadConfig config{ thread_ids::rendering, module_ids::render_vulkan,
            platform::threading::EThreadPriority::Normal, entry, (&modules::CBoundModule::prepare_thread), (&module) };
        return package.emplace(config, conversion) &&
            package->install_batch_client(shared, module_ids::render_vulkan, (&module)) && package->startup();
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
        (void)wait_until(conversion, [this]() noexcept
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

    template<typename TWork, typename TResult>
    CErasedOwner operation(tests::TTestContext& ctx, const TWork& work, const std::int32_t slot,
        const bool queued, const threading::EBatchCompletion completion, const bool exit_before_completion) noexcept
    {
        threading::CErasedPodMsg request;
        request.set_async_slot(slot);
        request.assign_payload(work);
        const bool posted = package->post(request);
        TEST_EXPECT(ctx, posted);
        if (!posted) return {};
        if (queued)
        {
            const bool acquired = wait_until(conversion, [this]() noexcept { return acquire(); });
            TEST_EXPECT(ctx, acquired);
            if (!acquired) return {};
            TEST_EXPECT(ctx, (held.correlation == static_cast<std::uint32_t>(slot)));
            TEST_EXPECT(ctx, (held.memory_context == test_environment::rendering_memory_context()));
            threading::CErasedOwnerMsg premature;
            TEST_EXPECT(ctx, !package->read(premature));
            TEST_EXPECT(ctx, (package->batch_client()->outstanding() == 1u));
            if (exit_before_completion)
            {
                package->request_exit();
                TEST_EXPECT(ctx, wait_until(conversion, [this]() noexcept
                {
                    return package->query_state() == threading::EThreadRunState::Exiting;
                }));
                TEST_EXPECT(ctx, (package->batch_client()->outstanding() == 1u));
            }
            if (completion == threading::EBatchCompletion::executed)
            {
                const bool prepared = module.prepare_batch_thread(thread_ids::batch_runner_00, held.memory_context);
                TEST_EXPECT(ctx, prepared);
                if (!prepared) return {};
                //  Models a runner admitted before the exit request, finishing afterward.
                held.function(held.object);
            }
            TEST_EXPECT(ctx, complete(completion));
        }
        threading::CErasedOwnerMsg response;
        const bool received = wait_until(conversion, [this, &response]() noexcept { return package->read(response); });
        TEST_EXPECT(ctx, received);
        if (!received) return {};
        TEST_EXPECT(ctx, (response.query_async_slot() == slot));
        TEST_EXPECT(ctx, response.is_message_a<TResult>());
        TEST_EXPECT(ctx, (package->batch_client()->outstanding() == 0u));
        return response.take_owner();
    }
};

static void test_jobs(tests::TTestContext& ctx)
{
    SFixture fixture;
    const bool ready = fixture.initialise();
    TEST_EXPECT(ctx, ready);
    if (!ready) return;
    TextureEncodeRequest input;
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
                CErasedOwner discarded = fixture.operation<TextureEncodeWork, TextureEncodeResult>(
                    ctx, encode, 9, true, EBatchCompletion::discarded_context_install_failed, false);
                const TextureEncodeResult* const result = discarded.payload<TextureEncodeResult>();
                TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::codec_failed) &&
                    !result->texture.view().is_ready()));
            }
            CErasedOwner encoded = fixture.operation<TextureEncodeWork, TextureEncodeResult>(
                ctx, encode, 10, queued, EBatchCompletion::executed, false);
            const TextureEncodeResult* const source = encoded.payload<TextureEncodeResult>();
            TEST_EXPECT(ctx, ((source != nullptr) && (source->status == ETextureStatus::success)));
            if ((source == nullptr) || (source->status != ETextureStatus::success)) return;
            const CByteConstView bytes = source->texture.buffer.const_view();
            TextureDecodeRequest request;
            const TextureDecodeWork decode{ (&request), (&bytes) };
            if (queued)
            {
                CErasedOwner discarded = fixture.operation<TextureDecodeWork, TextureDecodeResult>(
                    ctx, decode, 11, true, EBatchCompletion::discarded_context_install_failed, false);
                const TextureDecodeResult* const result = discarded.payload<TextureDecodeResult>();
                TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::codec_failed) &&
                    !result->texture.view().is_ready()));
            }
            CErasedOwner decoded = fixture.operation<TextureDecodeWork, TextureDecodeResult>(
                ctx, decode, 12, queued, EBatchCompletion::executed, queued);
            const TextureDecodeResult* const result = decoded.payload<TextureDecodeResult>();
            TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::success) &&
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
            CErasedOwner discarded;
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

}   //  namespace texture_batch_tests

int run_texture_batch_tests()
{
    tests::TTestContext ctx;
    texture_batch_tests::test_jobs(ctx);
    std::cout << "TextureBatch: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
