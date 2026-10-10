
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TextureBatch_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Oct 26
//
//  Drive the real rendering DLL with a controlled batch queue. Holding a
//  dequeued item makes discard and exit ordering deterministic.

#include <atomic>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "debug/service.hpp"
#include "host/runtime/asset_service.hpp"
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
#include "types/fp16data_t.hpp"

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

struct SStopRendering
{
    SFixture& fixture;
    ~SStopRendering() noexcept { fixture.stop(); }
};

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

//  A client echo keeps the real Host reply transport in the integration path.
static std::uint32_t MV_STD_ABI_CALL echo_client(void* const data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    threading::CThreadContext context{ resources };
    context.startup();
    context.mark_running();
    while (!context.exit_requested())
    {
        const std::uint32_t epoch = resources.wait_predicate.get_word();
        threading::CErasedPodMsg message;
        while (context.read(message))
        {
            if (!context.post(message))
            {
                context.mark_failed(1u);
                return 1u;
            }
        }
        if (!context.exit_requested()) (void)context.wait_for_new_epoch(epoch);
    }
    context.mark_exited();
    return 0u;
}

struct SServiceFixture
{
    SFixture& rendering;
    host::CAssetService service;
    TInstance<threading::CThreadPackage> client;

    explicit SServiceFixture(SFixture& source) noexcept : rendering{ source } {}
    ~SServiceFixture() noexcept
    {
        rendering.stop();
        if (client) (void)client->shutdown();
        service.deallocate();
    }

    bool initialise() noexcept
    {
        if (!rendering.start(true) || !service.initialise()) return false;
        service.set_rendering(rendering.package.operator->());
        const threading::ThreadConfig config{ thread_ids::executive, module_ids::executable,
            platform::threading::EThreadPriority::Normal, (&echo_client) };
        return client.emplace(config, rendering.conversion) && client->startup();
    }

    template<typename T>
    bool request(const std::int32_t slot, CErasedOwner&& owner) noexcept
    {
        if (owner.payload<T>() == nullptr) return false;
        threading::CErasedOwnerMsg message;
        message.set_async_slot(slot);
        message.set_message_type<T>();
        message.set_owner(std::move(owner));
        service.request(message, (*client));
        return true;
    }

    bool encode(const std::int32_t slot, const bool hdr, const std::uint32_t width) noexcept
    {
        CErasedOwner owner = CErasedOwner::create<TextureEncodeRequest>();
        TextureEncodeRequest* const request = owner.payload<TextureEncodeRequest>();
        if (request == nullptr) return false;
        request->format = hdr ? image::texture::EInputFormat::rgba32f : image::texture::EInputFormat::rgba8;
        request->options.encoding = hdr ? image::texture::EEncoding::uastc_hdr_4x4 : image::texture::EEncoding::uastc_ldr_4x4;
        if (!request->input.allocate((width * (hdr ? 16u : 4u)), 4u, 16u)) return false;
        for (std::uint32_t y = 0u; y < 4u; ++y)
        {
            if (hdr)
            {
                const float pixel[]{ 4.0f, 2.0f, 1.0f, 1.0f };
                for (std::uint32_t x = 0u; x < width; ++x)
                    std::memcpy((request->input.row_data(y) + (x * sizeof(pixel))), pixel, sizeof(pixel));
            }
            else std::memset(request->input.row_data(y), 255, (width * 4u));
        }
        return this->request<TextureEncodeRequest>(slot, std::move(owner));
    }

    bool decode(const std::int32_t slot, const CAssetId asset, const bool hdr) noexcept
    {
        CErasedOwner owner = CErasedOwner::create<TextureDecodeRequest>();
        TextureDecodeRequest* const request = owner.payload<TextureDecodeRequest>();
        if (request == nullptr) return false;
        request->source = asset;
        request->target = hdr ? image::texture::EStorageFormat::rgba16f : image::texture::EStorageFormat::rgba8;
        return this->request<TextureDecodeRequest>(slot, std::move(owner));
    }

    bool receive(const std::int32_t expected_slot, AssetResult& result) noexcept
    {
        threading::CErasedPodMsg reply;
        const bool received = wait_until(rendering.conversion, [this, &reply]() noexcept
        {
            threading::CErasedOwnerMsg completion;
            while (rendering.package->read(completion)) service.complete(completion, (*rendering.package));
            return client->read(reply);
        });
        return received && (reply.query_async_slot() == expected_slot) && reply.copy_payload_to(result);
    }
};

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
            if (!wait_until(fixture.conversion, [this]() noexcept { return fixture.acquire(); })) return false;
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
        return wait_until(fixture.conversion, [this, count]() noexcept
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
        threading::CErasedPodMsg message;
        message.set_async_slot(static_cast<std::int32_t>(1000u + index));
        message.assign_payload(TextureDecodeWork{ (&request), (&empty) });
        const bool posted = wait_until(fixture.conversion, [&fixture, &message]() noexcept
        {
            return fixture.package->post(message);
        });
        TEST_EXPECT(ctx, posted);
        if (!posted) return;
    }
    //  The last request executes inline while the first 128 remain queued.
    threading::CErasedOwnerMsg response;
    const bool received = wait_until(fixture.conversion, [&fixture, &response]() noexcept
    {
        return fixture.package->read(response);
    });
    TEST_EXPECT(ctx, received);
    if (!received) return;
    TEST_EXPECT(ctx, (response.query_async_slot() == static_cast<std::int32_t>(1000u + count)));
    {
        CErasedOwner owner = response.take_owner();
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
        fixture.held.function(fixture.held.object);
        TEST_EXPECT(ctx, fixture.complete(threading::EBatchCompletion::executed));
        const bool completed = wait_until(fixture.conversion, [&fixture, &response]() noexcept
        {
            return fixture.package->read(response);
        });
        TEST_EXPECT(ctx, completed);
        if (!completed) return;
        TEST_EXPECT(ctx, (response.query_async_slot() == static_cast<std::int32_t>(1000u + index)));
        CErasedOwner owner = response.take_owner();
        const auto* const result = owner.payload<TextureDecodeResult>();
        TEST_EXPECT(ctx, ((result != nullptr) && (result->status == ETextureStatus::invalid_input)));
    }
    TEST_EXPECT(ctx, (fixture.package->batch_client()->outstanding() == 0u));
    fixture.stop();
    TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
}

static void test_multiple_requests(tests::TTestContext& ctx, SFixture& rendering)
{
    SServiceFixture fixture{ rendering };
    const bool ready = fixture.initialise();
    TEST_EXPECT(ctx, ready);
    if (!ready) return;
    AssetResult sources[2];
    {
        SConcurrentJobs jobs{ fixture.rendering };
        TEST_EXPECT(ctx, fixture.encode(100, false, 4u));
        TEST_EXPECT(ctx, fixture.encode(101, true, 8u));
        TEST_EXPECT(ctx, fixture.request<TextureEncodeRequest>(102, CErasedOwner::create<TextureEncodeRequest>()));
        TEST_EXPECT(ctx, fixture.encode(103, false, 12u));
        const bool acquired = jobs.acquire();
        TEST_EXPECT(ctx, acquired);
        if (!acquired) return;
        TEST_EXPECT(ctx, (fixture.rendering.package->batch_client()->outstanding() == 4u));
        const bool started = jobs.start(3u);
        TEST_EXPECT(ctx, started);
        if (!started) return;
        jobs.join();
        for (std::uint32_t index = 0u; index < 3u; ++index) TEST_EXPECT(ctx, jobs.workers[index].prepared);
        threading::CErasedOwnerMsg premature;
        TEST_EXPECT(ctx, !fixture.rendering.package->read(premature));
        for (std::int32_t index = 3; index >= 0; --index)
        {
            TEST_EXPECT(ctx, SConcurrentJobs::complete(jobs.workers[index], (index == 3 ?
                threading::EBatchCompletion::discarded_context_install_failed : threading::EBatchCompletion::executed)));
            AssetResult result;
            const bool received = fixture.receive((100 + index), result);
            TEST_EXPECT(ctx, received);
            if (!received) return;
            const EAssetStatus expected = (index == 3) ? EAssetStatus::conditioning_failed :
                ((index == 2) ? EAssetStatus::invalid_request : EAssetStatus::success);
            TEST_EXPECT(ctx, (result.status == expected));
            if (index < 2)
            {
                sources[index] = result;
                const auto view = result.encoded_texture_view();
                TEST_EXPECT(ctx, (view.is_ready() && (view.description.width == ((index == 0) ? 4u : 8u)) &&
                    (view.description.encoding == ((index == 0) ? image::texture::EEncoding::uastc_ldr_4x4 :
                        image::texture::EEncoding::uastc_hdr_4x4))));
            }
        }
    }
    TEST_EXPECT(ctx, fixture.service.is_idle());
    if (!sources[0].asset || !sources[1].asset) return;
    {
        SConcurrentJobs jobs{ fixture.rendering };
        TEST_EXPECT(ctx, fixture.decode(200, sources[0].asset, false));
        TEST_EXPECT(ctx, fixture.decode(201, sources[1].asset, true));
        TEST_EXPECT(ctx, fixture.encode(202, false, 12u));
        TEST_EXPECT(ctx, fixture.decode(203, sources[1].asset, false)); // Incompatible target.
        const bool acquired = jobs.acquire();
        TEST_EXPECT(ctx, acquired);
        if (!acquired) return;
        const bool started = jobs.start(4u);
        TEST_EXPECT(ctx, started);
        if (!started) return;
        fixture.service.request_disposal(AssetDisposeRequest{ sources[0].asset }, 204, (*fixture.client));
        fixture.service.complete_disposals();
        fixture.rendering.package->request_exit();
        TEST_EXPECT(ctx, wait_until(fixture.rendering.conversion, [&fixture]() noexcept
        {
            return fixture.rendering.package->query_state() == threading::EThreadRunState::Exiting;
        }));
        //  These workers were admitted before exit. Their callbacks finish afterward.
        jobs.join();
        for (const auto& worker : jobs.workers) TEST_EXPECT(ctx, worker.prepared);
        for (std::int32_t index = 3; index >= 0; --index)
        {
            TEST_EXPECT(ctx, SConcurrentJobs::complete(jobs.workers[index], threading::EBatchCompletion::executed));
            AssetResult result;
            const bool received = fixture.receive((200 + index), result);
            TEST_EXPECT(ctx, received);
            if (!received) return;
            TEST_EXPECT(ctx, (result.status == ((index == 3) ? EAssetStatus::invalid_request : EAssetStatus::success)));
            if (index == 2)
            {
                const auto view = result.encoded_texture_view();
                TEST_EXPECT(ctx, (view.is_ready() && (view.description.width == 12u)));
            }
            if (index < 2)
            {
                const auto view = result.decoded_texture_view();
                TEST_EXPECT(ctx, (view.is_ready() && (view.description.width == ((index == 0) ? 4u : 8u)) &&
                    (view.description.format == ((index == 0) ? image::texture::EStorageFormat::rgba8 :
                        image::texture::EStorageFormat::rgba16f))));
                if (view.is_ready() && (index == 0)) TEST_EXPECT(ctx, (view.bytes.data()[0] > 250u));
                if (view.is_ready() && (index == 1))
                {
                    std::uint16_t red_bits = 0u;
                    std::memcpy((&red_bits), view.bytes.data(), sizeof(red_bits));
                    const float red = static_cast<float>(fp16data_t::fromBits(red_bits)) * view.description.hdr_scale;
                    TEST_EXPECT(ctx, ((red > 3.5f) && (red < 4.5f)));
                }
            }
            fixture.service.complete_disposals();
            if (index != 0)
            {
                TEST_EXPECT(ctx, (fixture.rendering.package->query_state() == threading::EThreadRunState::Exiting));
                threading::CErasedPodMsg premature;
                TEST_EXPECT(ctx, !fixture.client->read(premature));
            }
        }
        threading::CErasedPodMsg reply;
        TEST_EXPECT(ctx, wait_until(fixture.rendering.conversion, [&fixture, &reply]() noexcept
        {
            return fixture.client->read(reply);
        }));
        AssetDisposeResult disposed;
        TEST_EXPECT(ctx, ((reply.query_async_slot() == 204) && reply.copy_payload_to(disposed) &&
            (disposed.status == EAssetStatus::success) && (disposed.asset == sources[0].asset)));
    }
    TEST_EXPECT(ctx, fixture.service.is_idle());
    TEST_EXPECT(ctx, !fixture.service.failed());
    TEST_EXPECT(ctx, wait_until(fixture.rendering.conversion, [&fixture]() noexcept
    {
        return fixture.rendering.package->query_state() == threading::EThreadRunState::Exited;
    }));
    TEST_EXPECT(ctx, (fixture.rendering.package->batch_client()->outstanding() == 0u));
    fixture.rendering.stop();
    fixture.service.deallocate();
    TEST_EXPECT(ctx, test_environment::rendering_memory_context()->is_attribution_empty());
}

}   //  namespace texture_batch_tests

int run_texture_batch_tests()
{
    tests::TTestContext ctx;
    //  Module installation is once per binding lifetime, including on loaders
    //  that retain the native image after dlclose. Restart only its thread.
    texture_batch_tests::SFixture fixture;
    const bool ready = fixture.initialise();
    TEST_EXPECT(ctx, ready);
    if (ready)
    {
        texture_batch_tests::test_jobs(ctx, fixture);
        texture_batch_tests::test_multiple_requests(ctx, fixture);
        texture_batch_tests::test_saturated_requests(ctx, fixture);
    }
    std::cout << "TextureBatch: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
