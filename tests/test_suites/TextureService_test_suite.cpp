
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TextureService_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Oct 26
//
//  Hold renderer completions explicitly to test concurrent admission and source
//  disposal independently of codec speed or thread scheduling.

#include <iostream>

#include "host/runtime/asset_service.hpp"
#include "platform/threading/processor_relax.hpp"
#include "tests/support/test_context.hpp"
#include "tests/test_suites/TextureService_test_suite.hpp"

namespace texture_service_tests
{

//  Echo PODs so the test can inspect both dispatched work and client replies.
//  The test supplies renderer results only after checking the pending state.
static std::uint32_t MV_STD_ABI_CALL echo_messages(void* const data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    threading::CThreadContext context(resources);
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

struct SFixture
{
    platform::system::CPerfCountConversion conversion;
    threading::CThreadPackage package;
    host::CAssetService service;

    SFixture() noexcept : package{
        threading::ThreadConfig{ thread_ids::rendering, module_ids::executable,
            platform::threading::EThreadPriority::Normal, (&echo_messages) }, conversion }
    {
    }

    ~SFixture() noexcept
    {
        (void)package.shutdown();
        service.deallocate();
    }

    bool initialise() noexcept
    {
        service.set_rendering(&package);
        return conversion.init() && service.initialise() && package.startup();
    }

    template<typename T>
    bool request(const std::int32_t slot, CErasedOwner&& owner) noexcept
    {
        if (owner.payload<T>() == nullptr) return false;
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(slot);
        message.set_owner(std::move(owner));
        service.request(message, package);
        return true;
    }

    template<typename T>
    bool receive(T& value, std::int32_t& slot) noexcept
    {
        platform::system::CPerfCounter timer;
        (void)timer.update();
        while (timer.query_delta() < (conversion.query_ticks_per_second() * 5u))
        {
            threading::CErasedPodMsg message;
            if (package.read(message))
            {
                slot = message.query_async_slot();
                return message.copy_payload_to(value);
            }
            platform::threading::processor_relax();
        }
        return false;
    }

    template<typename T>
    bool complete(const std::int32_t slot) noexcept
    {
        CErasedOwner owner = CErasedOwner::create<T>();
        T* const result = owner.payload<T>();
        if (result == nullptr) return false;
        result->status = ETextureStatus::invalid_input;
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(slot);
        message.set_owner(std::move(owner));
        service.complete(message, package);
        return true;
    }

    bool decode(const std::int32_t slot, const CAssetId asset) noexcept
    {
        CErasedOwner owner = CErasedOwner::create<TextureDecodeRequest>();
        TextureDecodeRequest* const request = owner.payload<TextureDecodeRequest>();
        if (request == nullptr) return false;
        request->source = asset;
        return this->request<TextureDecodeRequest>(slot, std::move(owner));
    }

    bool expect_result(const std::int32_t expected_slot, const EAssetStatus expected_status) noexcept
    {
        AssetResult result;
        std::int32_t slot = -1;
        return receive(result, slot) && (slot == expected_slot) &&
            (result.status == expected_status) && !result.asset && (result.kind == EAssetKind::none);
    }
};

static void test_admission_and_disposal(tests::TTestContext& ctx)
{
    SFixture fixture;
    const bool ready = fixture.initialise();
    TEST_EXPECT(ctx, ready);
    if (!ready) return;

    CErasedOwner owner = CErasedOwner::create<RawAssetTransfer>();
    RawAssetTransfer* const raw = owner.payload<RawAssetTransfer>();
    const bool allocated = (raw != nullptr) && raw->storage.value.resize(16u, 16u);
    TEST_EXPECT(ctx, allocated);
    if (!allocated) return;
    raw->storage.value.data()[0] = 0x5au;
    TEST_EXPECT(ctx, fixture.request<RawAssetTransfer>(10, std::move(owner)));
    AssetResult retained;
    std::int32_t slot = -1;
    const bool received = fixture.receive(retained, slot);
    TEST_EXPECT(ctx, (received && (slot == 10) && (retained.status == EAssetStatus::success) && retained.asset));
    if (!received || !retained.asset) return;

    TEST_EXPECT(ctx, fixture.decode(11, retained.asset));
    TextureDecodeWork decode;
    const bool dispatched = fixture.receive(decode, slot);
    TEST_EXPECT(ctx, dispatched);
    if (!dispatched) return;
    const std::int32_t decode_slot = slot;
    TEST_EXPECT(ctx, (decode.request->source == retained.asset));
    TEST_EXPECT(ctx, !fixture.service.is_idle());

    TEST_EXPECT(ctx, fixture.decode(12, retained.asset));
    TextureDecodeWork second_decode;
    TEST_EXPECT(ctx, fixture.receive(second_decode, slot));
    const std::int32_t second_decode_slot = slot;
    TEST_EXPECT(ctx, (second_decode_slot != decode_slot));
    TEST_EXPECT(ctx, fixture.request<TextureEncodeRequest>(17, CErasedOwner::create<TextureEncodeRequest>()));
    TextureEncodeWork encode;
    TEST_EXPECT(ctx, fixture.receive(encode, slot));
    const std::int32_t encode_slot = slot;
    TEST_EXPECT(ctx, fixture.request<TextureEncodeRequest>(19, CErasedOwner::create<TextureEncodeRequest>()));
    TEST_EXPECT(ctx, fixture.receive(encode, slot));
    const std::int32_t second_encode_slot = slot;
    TEST_EXPECT(ctx, (second_encode_slot != encode_slot));

    fixture.service.request_disposal(AssetDisposeRequest{ retained.asset }, 13, fixture.package);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.decode(14, retained.asset));
    //  Disposal closes new borrowing while both existing decodes retain input.
    TEST_EXPECT(ctx, fixture.expect_result(14, EAssetStatus::invalid_asset));
    TEST_EXPECT(ctx, ((decode.input->size() == 16u) && (decode.input->data()[0] == 0x5au)));
    TEST_EXPECT(ctx, ((second_decode.input->size() == 16u) && (second_decode.input->data()[0] == 0x5au)));

    //  Complete in a different order. One failure must not release other jobs.
    TEST_EXPECT(ctx, fixture.complete<TextureEncodeResult>(second_encode_slot));
    TEST_EXPECT(ctx, fixture.expect_result(19, EAssetStatus::invalid_request));
    TEST_EXPECT(ctx, fixture.complete<TextureDecodeResult>(second_decode_slot));
    TEST_EXPECT(ctx, fixture.expect_result(12, EAssetStatus::invalid_request));
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.decode(15, retained.asset));
    TEST_EXPECT(ctx, fixture.expect_result(15, EAssetStatus::invalid_asset));
    TEST_EXPECT(ctx, ((decode.input->size() == 16u) && (decode.input->data()[0] == 0x5au)));
    TEST_EXPECT(ctx, fixture.complete<TextureDecodeResult>(decode_slot));
    TEST_EXPECT(ctx, fixture.expect_result(11, EAssetStatus::invalid_request));
    fixture.service.complete_disposals();
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, (fixture.receive(disposed, slot) && (slot == 13) &&
        (disposed.asset == retained.asset) && (disposed.status == EAssetStatus::success)));
    TEST_EXPECT(ctx, !fixture.service.is_idle());
    TEST_EXPECT(ctx, fixture.decode(18, retained.asset));
    TEST_EXPECT(ctx, fixture.expect_result(18, EAssetStatus::invalid_asset));
    TEST_EXPECT(ctx, fixture.complete<TextureEncodeResult>(encode_slot));
    TEST_EXPECT(ctx, fixture.expect_result(17, EAssetStatus::invalid_request));
    TEST_EXPECT(ctx, fixture.service.is_idle());

    TEST_EXPECT(ctx, fixture.request<TextureEncodeRequest>(20, CErasedOwner::create<TextureEncodeRequest>()));
    const bool retried = fixture.receive(encode, slot);
    TEST_EXPECT(ctx, retried);
    if (!retried) return;
    TEST_EXPECT(ctx, fixture.complete<TextureEncodeResult>(slot));
    TEST_EXPECT(ctx, fixture.expect_result(20, EAssetStatus::invalid_request));
    fixture.service.set_rendering(nullptr);
    TEST_EXPECT(ctx, fixture.decode(21, retained.asset));
    TEST_EXPECT(ctx, fixture.expect_result(21, EAssetStatus::busy));
    TEST_EXPECT(ctx, fixture.request<TextureEncodeRequest>(22, CErasedOwner::create<TextureEncodeRequest>()));
    TEST_EXPECT(ctx, fixture.expect_result(22, EAssetStatus::busy));
    TEST_EXPECT(ctx, fixture.service.is_idle());
    TEST_EXPECT(ctx, !fixture.service.failed());
}

}   //  namespace texture_service_tests

int run_texture_service_tests()
{
    tests::TTestContext ctx;
    texture_service_tests::test_admission_and_disposal(ctx);
    std::cout << "TextureService: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
