
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    MessageHelpers_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    20 Oct 26
//
//  Functional message helpers and the actual Executive preparation boundary.

#include "tests/test_suites/MessageHelpers_test_suite.hpp"

#include <cstring>
#include <type_traits>

#include "tests/environment/local_type_ids.hpp"
#include "executive/runtime/request_preparation.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "threading/messages/CErasedMessageTransports.hpp"

namespace message_helper_tests
{

using tests::TTestContext;
using namespace executive::request_preparation;
using namespace threading;

struct SAllocator
{
    std::int32_t remaining{ -1 };
    std::uint32_t allocations{}, releases{};
};

static void* MV_STD_ABI_CALL allocate(void* state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    auto& allocator = *static_cast<SAllocator*>(state);
    if (allocator.remaining == 0) return nullptr;
    if (allocator.remaining > 0) --allocator.remaining;
    void* const result = tests::allocate_test_memory(nullptr, alignment, bytes);
    if (result != nullptr) ++allocator.allocations;
    return result;
}

static bool MV_STD_ABI_CALL release(void* state, const std::size_t alignment, void* pointer) noexcept
{
    ++static_cast<SAllocator*>(state)->releases;
    return tests::deallocate_test_memory(nullptr, alignment, pointer);
}

template<typename T, typename = void>
struct TCanInspect : std::false_type {};
template<typename T>
struct TCanInspect<T, std::void_t<decltype(inspect_owner_message<FileLoadResult>(std::declval<T>()))>> : std::true_type {};

template<typename TMessage, typename TPayload, typename TOtherMessage = ModuleResult>
static void test_pair(TTestContext& ctx, SAllocator& allocator)
{
    static_assert(std::is_same_v<typename TMessagePayload<TMessage>::type, TPayload>);
    CErasedOwnerMsg message = make_owner_message<TMessage>(47);
    auto* const payload = message.owner().template payload<TPayload>();
    TEST_EXPECT(ctx, payload != nullptr);
    message.owner().add_hazard(mount_point_ids::asset);
    const auto hazards = message.owner().hazard_mask();
    const auto* const memory = message.owner().memory_context();
    const auto inspected = inspect_owner_message<TMessage>(message);
    const CErasedOwnerMsg& immutable = message;
    const auto read_only = inspect_owner_message<TMessage>(immutable);
    static_assert(std::is_same_v<decltype(read_only.payload), const TPayload*>);
    TEST_EXPECT(ctx, inspected.state == EOwnerMessageState::present && inspected.payload == payload);
    TEST_EXPECT(ctx, read_only.state == EOwnerMessageState::present && read_only.payload == payload);
    TEST_EXPECT(ctx, message.query_async_slot() == 47 && message.owner().memory_context() == memory);
    TEST_EXPECT(ctx, message.owner().hazard_mask() == hazards);

    message.set_message_type<TOtherMessage>();
    TEST_EXPECT(ctx, inspect_owner_message<TMessage>(message).state == EOwnerMessageState::invalid);
    message.set_message_type<TMessage>();
    message.set_owner(CErasedOwner::create<BundleLoadRequest>());
    const auto wrong = inspect_owner_message<TMessage>(message);
    TEST_EXPECT(ctx, wrong.state == EOwnerMessageState::invalid && wrong.payload == nullptr);
    TEST_EXPECT(ctx, message.owner().payload<BundleLoadRequest>() != nullptr);
    message = {};

    allocator.remaining = 0;
    message = make_owner_message<TMessage>(-17);
    allocator.remaining = -1;
    TEST_EXPECT(ctx, message.is_message_a<TMessage>() && message.query_async_slot() == -17);
    TEST_EXPECT(ctx, !message.has_owner());
    const auto empty = inspect_owner_message<TMessage>(message);
    TEST_EXPECT(ctx, empty.state == EOwnerMessageState::empty && empty.payload == nullptr);
    message.set_message_type<TOtherMessage>();
    TEST_EXPECT(ctx, inspect_owner_message<TMessage>(message).state == EOwnerMessageState::invalid);
}

static void test_helpers(TTestContext& ctx, SAllocator& allocator)
{
    static_assert(TCanInspect<CErasedOwnerMsg&>::value && TCanInspect<const CErasedOwnerMsg&>::value);
    static_assert(!TCanInspect<CErasedOwnerMsg&&>::value && !TCanInspect<const CErasedOwnerMsg&&>::value);
    const auto pod = make_pod_message(31, FileSaveResult{ true });
    FileSaveResult result{};
    TEST_EXPECT(ctx, pod.copy_payload_to(result) && result.success && pod.query_async_slot() == 31);
    const auto notice = make_pod_message(k_uncorrelated_slot, BundleDisposedNotification{});
    TEST_EXPECT(ctx, notice.is_payload_a<BundleDisposedNotification>() && notice.query_async_slot() == -1);
    test_pair<AssetLoadRequest, AssetLoadRequest>(ctx, allocator);
    test_pair<TextureEncodeResult, TextureEncodeResult>(ctx, allocator);
    //  Shared storage must not make the two completion protocols interchangeable.
    test_pair<FileLoadResult, ByteStorage, TgaEncodeResult>(ctx, allocator);
    test_pair<TgaEncodeResult, ByteStorage, FileLoadResult>(ctx, allocator);
    test_pair<TgaDecodeResult, DecodedTga>(ctx, allocator);
}

static void test_preparation_failure(TTestContext& ctx, SAllocator& allocator, memory::CMemoryContext& memory)
{
    tests::TAssertionTestScope assertions{ ctx };
    CErasedOwnerMsg pending;
    pending.set_message_type<AssetLoadRequest>();
    pending.set_async_slot(75);
    assertions.expect_unconditional_assertion(ctx, [&] {
        TEST_EXPECT(ctx, !prepare_load_request(pending, 99, EAssetFileFormat::raw, "test-output:/file"));
    });
    TEST_EXPECT(ctx, pending.is_message_a<AssetLoadRequest>() && pending.query_async_slot() == 75 && !pending.has_owner());
    pending = {};
    pending.set_owner(CErasedOwner::create<RawAssetTransfer>());
    auto* const occupied = pending.owner().payload<RawAssetTransfer>();
    assertions.expect_unconditional_assertion(ctx, [&] {
        TEST_EXPECT(ctx, !prepare_save_request(pending, 99, {}, "test-output:/file", {}));
    });
    TEST_EXPECT(ctx, !pending.has_message_type() && pending.owner().payload<RawAssetTransfer>() == occupied);
    pending = {};

    // Owner allocation, then filename allocation: fail each real preparation step.
    for (std::int32_t allowed = 0; allowed < 2; ++allowed)
    {
        pending.set_async_slot(91); // A destination without identity/owner is still left exactly as supplied.
        const auto before = memory.get_live_allocation_count();
        allocator.remaining = allowed;
        TEST_EXPECT(ctx, !prepare_load_request(pending, 23, EAssetFileFormat::json, "test-output:/preparation-failure.json"));
        allocator.remaining = -1;
        TEST_EXPECT(ctx, !pending.has_message_type() && !pending.has_owner() && pending.query_async_slot() == 91);
        TEST_EXPECT(ctx, memory.get_live_allocation_count() == before);
    }

    // The second path must also fail atomically after the first path was prepared.
    for (std::int32_t allowed = 0; allowed < 3; ++allowed)
    {
        const auto before = memory.get_live_allocation_count();
        allocator.remaining = allowed;
        TEST_EXPECT(ctx, !prepare_bundle_load_request(pending, 23, "source:/preparation-failure.bdl", "bundle:"));
        allocator.remaining = -1;
        TEST_EXPECT(ctx, !pending.has_message_type() && !pending.has_owner() && pending.query_async_slot() == 91);
        TEST_EXPECT(ctx, memory.get_live_allocation_count() == before);
    }

    CByteBuffer source;
    TEST_EXPECT(ctx, source.allocate(32u));
    auto* const bytes = source.data();
    bytes[0] = 0x5a;
    for (std::int32_t allowed = 0; allowed < 2; ++allowed)
    {
        allocator.remaining = allowed;
        TEST_EXPECT(ctx, !prepare_asset_transfer(pending, 24, source, EAssetRetention::source, true,
            "test-output:/transfer-failure.raw", {}));
        allocator.remaining = -1;
        TEST_EXPECT(ctx, source.data() == bytes && bytes[0] == 0x5a);
        TEST_EXPECT(ctx, !pending.has_message_type() && !pending.has_owner() && pending.query_async_slot() == 91);
    }
    TEST_EXPECT(ctx, prepare_asset_transfer(pending, 24, source, EAssetRetention::source, true, "test-output:/transfer.raw", {}));
    TEST_EXPECT(ctx, !source.is_ready());
    TEST_EXPECT(ctx, pending.owner().payload<RawAssetTransfer>()->storage.value.data() == bytes);
    const auto before = allocator.releases;
    pending = {};
    TEST_EXPECT(ctx, allocator.releases == before + 3u); // Carrier, filename, source buffer; one release each.
    pending = {};
    TEST_EXPECT(ctx, allocator.releases == before + 3u);
}

static void test_request_fields(TTestContext& ctx)
{
    CErasedOwnerMsg pending;
    TEST_EXPECT(ctx, prepare_load_request(pending, 10, EAssetFileFormat::tga, "source:/image.tga", {}, false));
    const auto* load = pending.owner().payload<AssetLoadRequest>();
    TEST_EXPECT(ctx, load && load->alignment == 32u && !load->decode_top_down && load->format == EAssetFileFormat::tga);
    pending = {};
    AssetSaveSettings settings;
    settings.format = EAssetFileFormat::json;
    TEST_EXPECT(ctx, prepare_save_request(pending, 11, {}, "output:/saved.json", settings));
    TEST_EXPECT(ctx, pending.owner().payload<AssetSaveRequest>()->settings.format == EAssetFileFormat::json);
    pending = {};
    TEST_EXPECT(ctx, prepare_module_request(pending, 12, module_ids::render_vulkan, EModuleAction::load, "package:/bin/MorphicRendering.dll"));
    TEST_EXPECT(ctx, pending.owner().payload<ModuleRequest>()->module == module_ids::render_vulkan);
    pending = {};
    TEST_EXPECT(ctx, prepare_module_request(pending, k_uncorrelated_slot, module_ids::executive, EModuleAction::unload));
    TEST_EXPECT(ctx, pending.owner().payload<ModuleRequest>()->file.length() == 0u && pending.query_async_slot() == -1);
    pending = {};
    TEST_EXPECT(ctx, prepare_filesystem_request(pending, 13, "source:"));
    TEST_EXPECT(ctx, std::strcmp(pending.owner().payload<FilesystemRefreshRequest>()->root.cstring(), "source:") == 0);
    pending = {};
    TEST_EXPECT(ctx, prepare_bundle_load_request(pending, 14, "source:/bundle.bdl", "bundle:"));
    TEST_EXPECT(ctx, std::strcmp(pending.owner().payload<BundleLoadRequest>()->paths.root.cstring(), "bundle:") == 0);
    pending = {};
    TEST_EXPECT(ctx, prepare_bundle_bake_request(pending, 15, "source:", "baked:"));
    TEST_EXPECT(ctx, pending.owner().payload<BundleBakeRequest>()->paths.subdirectory.length() == 0u);
    pending = {};
    TEST_EXPECT(ctx, prepare_texture_decode_request(pending, 16, {}, image::texture::EStorageFormat::rgba8, 3u));
    TEST_EXPECT(ctx, pending.owner().payload<TextureDecodeRequest>()->level == 3u);
}

static void test_prepared_post(TTestContext& ctx, memory::CMemoryAllocator& allocator, memory::CMemoryContext& source)
{
    memory::CMemoryContext channel(allocator, system_ids::host);
    transports::CErasedOwnerMsgTransport transport(module_ids::executable, &channel);
    TEST_EXPECT(ctx, transport.initialise(1u));
    while (transport.writable_count() != 0u)
    {
        CErasedOwnerMsg filler;
        filler.set_message_type<AssetResult>(); // Intentionally empty completion.
        TEST_EXPECT(ctx, transport.post(std::move(filler)));
    }
    CErasedOwnerMsg pending;
    TEST_EXPECT(ctx, prepare_load_request(pending, 81, EAssetFileFormat::raw, "source:/retained.raw"));
    auto* const payload = pending.owner().payload<AssetLoadRequest>();
    pending.owner().add_hazard(mount_point_ids::asset);
    const auto hazards = pending.owner().hazard_mask();
    const auto count = source.get_live_allocation_count();
    const auto size = source.get_live_allocated_bytes();
    TEST_EXPECT(ctx, !transport.post(std::move(pending)));
    TEST_EXPECT(ctx, pending.is_message_a<AssetLoadRequest>() && pending.query_async_slot() == 81);
    TEST_EXPECT(ctx, pending.owner().payload<AssetLoadRequest>() == payload && pending.owner().memory_context() == &source);
    TEST_EXPECT(ctx, pending.owner().hazard_mask() == hazards);
    TEST_EXPECT(ctx, std::strcmp(payload->file.cstring(), "source:/retained.raw") == 0);
    TEST_EXPECT(ctx, source.get_live_allocation_count() == count && source.get_live_allocated_bytes() == size);
    CErasedOwnerMsg received;
    while (transport.read(received)) {}
    TEST_EXPECT(ctx, transport.post(std::move(pending)));
    TEST_EXPECT(ctx, !pending.has_message_type() && !pending.has_owner() && pending.query_async_slot() == 0);
    TEST_EXPECT(ctx, transport.read(received));
    TEST_EXPECT(ctx, received.owner().payload<AssetLoadRequest>() == payload && received.query_async_slot() == 81);
    TEST_EXPECT(ctx, received.owner().memory_context() == &channel && received.owner().hazard_mask() == hazards);
    received = {};
    TEST_EXPECT(ctx, !transport.read(received)); // Exactly one submitted request.

    while (transport.writable_count() != 0u)
    {
        CErasedOwnerMsg filler;
        filler.set_message_type<AssetResult>();
        TEST_EXPECT(ctx, transport.post(std::move(filler)));
    }
    CByteBuffer storage;
    TEST_EXPECT(ctx, storage.allocate(32u));
    auto* const bytes = storage.data();
    bytes[0] = 0x5a;
    TEST_EXPECT(ctx, prepare_asset_transfer(pending, 82, storage, EAssetRetention::source, false, nullptr, {}));
    TEST_EXPECT(ctx, !storage.is_ready());
    auto* const transfer = pending.owner().payload<RawAssetTransfer>();
    pending.owner().add_hazard(mount_point_ids::asset);
    const auto transfer_hazards = pending.owner().hazard_mask();
    TEST_EXPECT(ctx, !transport.post(std::move(pending)));
    TEST_EXPECT(ctx, pending.owner().payload<RawAssetTransfer>() == transfer && pending.query_async_slot() == 82);
    TEST_EXPECT(ctx, transfer->storage.value.data() == bytes && bytes[0] == 0x5a);
    TEST_EXPECT(ctx, pending.owner().memory_context() == &source && pending.owner().hazard_mask() == transfer_hazards);
    while (transport.read(received)) {}
    TEST_EXPECT(ctx, transport.post(std::move(pending)));
    TEST_EXPECT(ctx, !pending.has_message_type() && !pending.has_owner());
    TEST_EXPECT(ctx, transport.read(received));
    TEST_EXPECT(ctx, received.owner().payload<RawAssetTransfer>() == transfer && transfer->storage.value.data() == bytes);
    TEST_EXPECT(ctx, received.owner().memory_context() == &channel && received.owner().hazard_mask() == transfer_hazards);
    received = {};
    TEST_EXPECT(ctx, !transport.read(received));
    transport.deallocate();
    TEST_EXPECT(ctx, channel.is_attribution_empty());
}

}   //  namespace message_helper_tests

int run_message_helpers_tests()
{
    using namespace message_helper_tests;
    TTestContext ctx;
    SAllocator state;
    memory::CMemoryAllocator allocator(&state, &allocate, &release, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const tests::TModuleIdScope module_scope{ module_ids::executable };
    const tests::TMemoryContextScope memory_scope{ &context };
    test_helpers(ctx, state);
    test_preparation_failure(ctx, state, context);
    test_request_fields(ctx);
    test_prepared_post(ctx, allocator, context);
    TEST_EXPECT(ctx, context.is_attribution_empty() && state.allocations == state.releases);
    return ctx.report("MessageHelpers");
}
