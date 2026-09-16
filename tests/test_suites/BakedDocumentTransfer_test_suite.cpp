
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BakedDocumentTransfer_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "tests/test_suites/BakedDocumentTransfer_test_suite.hpp"

#include <cstring>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "assets/asset_repository.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/live_document.hpp"
#include "platform/filesystem/file.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "system/transported_types.hpp"
#include "tests/environment/test_paths.hpp"
#include "threading/messages/CErasedMessageTransports.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace baked_document_transfer_tests
{

using tests::TTestContext;

struct CAllocationState
{
    std::size_t allocations{ 0u };
    std::size_t deallocations{ 0u };
};

//  File-local callbacks and helpers are grouped in the suite's named namespace.
static void* MV_STD_ABI_CALL allocate(void* const state, const std::size_t alignment, const std::size_t size) noexcept
{
    ++static_cast<CAllocationState*>(state)->allocations;
    return tests::allocate_test_memory(nullptr, alignment, size);
}

static bool MV_STD_ABI_CALL deallocate(void* const state, const std::size_t alignment, void* const pointer) noexcept
{
    ++static_cast<CAllocationState*>(state)->deallocations;
    return tests::deallocate_test_memory(nullptr, alignment, pointer);
}

struct CContexts
{
    CAllocationState state;
    memory::CMemoryAllocator allocator{ &state, &allocate, &deallocate, system_ids::host };
    memory::CMemoryAllocator other_allocator{ &state, &allocate, &deallocate, system_ids::host };
    memory::CMemoryContext executive{ allocator, system_ids::executive };
    memory::CMemoryContext transport{ allocator, system_ids::host };
    memory::CMemoryContext host{ allocator, system_ids::host };
    memory::CMemoryContext incompatible{ other_allocator, system_ids::host };
};

static void expect_empty(TTestContext& ctx, const CContexts& contexts)
{
    TEST_EXPECT(ctx, contexts.executive.is_attribution_empty());
    TEST_EXPECT(ctx, contexts.transport.is_attribution_empty());
    TEST_EXPECT(ctx, contexts.host.is_attribution_empty());
    TEST_EXPECT(ctx, contexts.incompatible.is_attribution_empty());
    TEST_EXPECT(ctx, contexts.state.allocations == contexts.state.deallocations);
}

static void bake(TTestContext& ctx, CBakedDocumentBlock& block)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey recovery = live.create_array(CStringView{ "values" });
    TEST_EXPECT(ctx, live.append_child(live.root(), recovery).succeeded());
    TEST_EXPECT(ctx, live.append_child(recovery, live.create_signed_integer(-128)).succeeded());
    TEST_EXPECT(ctx, live.append_child(recovery, live.create_floating_point(-0.0)).succeeded());
    const std::uint8_t text[]{ 'a', 0u, 0xc3u, 0xa9u };
    TEST_EXPECT(ctx, live.append_child(recovery, live.create_string(CStringView{ text, sizeof(text) })).succeeded());
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    TEST_EXPECT(ctx, block.is_ready() && block.document().check_integrity());
    TEST_EXPECT(ctx, block.memory_attribution().allocation_count == 1u);
}

static void expect_bytes(TTestContext& ctx, const CBakedDocumentBlock& block,
    const std::uint8_t* const pointer, const std::vector<std::uint8_t>& bytes)
{
    TEST_EXPECT(ctx, block.bytes().data() == pointer && block.bytes().size() == bytes.size());
    TEST_EXPECT(ctx, block.document().is_ready());
    if (block.bytes().size() == bytes.size())
    {
        TEST_EXPECT(ctx, std::memcmp(block.bytes().data(), bytes.data(), bytes.size()) == 0);
    }
}

static CErasedOwner make_owner(TTestContext& ctx)
{
    CErasedOwner owner = CErasedOwner::create<BakedDocumentAsset>();
    BakedDocumentAsset* const asset = owner.payload<BakedDocumentAsset>();
    TEST_EXPECT(ctx, asset != nullptr);
    if (asset != nullptr)
    {
        bake(ctx, asset->block);
    }
    return owner;
}

static void test_block_attribution(TTestContext& ctx)
{
    CContexts contexts;
    const tests::TMemoryContextScope source_scope{ &contexts.executive };
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, memory::can_reattribute_to(block, &contexts.incompatible));
    TEST_EXPECT(ctx, memory::reattribute(block, &contexts.incompatible));
    TEST_EXPECT(ctx, !block.is_ready() && !block.document().is_ready());
    TEST_EXPECT(ctx, block.memory_attribution().allocation_count == 0u);
    bake(ctx, block);
    const auto bytes = block.bytes();
    const std::vector<std::uint8_t> snapshot(bytes.data(), bytes.data() + bytes.size());
    const CBakedDocument view = block.document();
    const std::uint64_t allocated_bytes = block.memory_attribution().allocation_size;
    const std::size_t allocation_calls = contexts.state.allocations;
    const std::size_t deallocation_calls = contexts.state.deallocations;
    TEST_EXPECT(ctx, contexts.executive.get_live_allocation_count() == 1u);
    TEST_EXPECT(ctx, memory::can_reattribute_to(block) && memory::reattribute(block));
    TEST_EXPECT(ctx, !memory::can_reattribute_to(block, &contexts.incompatible) && !memory::reattribute(block, &contexts.incompatible));
    TEST_EXPECT(ctx, contexts.executive.get_live_allocated_bytes() == allocated_bytes);
    TEST_EXPECT(ctx, contexts.incompatible.is_attribution_empty());
    {
        const tests::TMemoryContextScope target_scope{ &contexts.host };
        TEST_EXPECT(ctx, memory::can_reattribute_to(block) && memory::reattribute(block));
    }
    TEST_EXPECT(ctx, contexts.executive.is_attribution_empty());
    TEST_EXPECT(ctx, contexts.host.get_live_allocation_count() == 1u && contexts.host.get_live_allocated_bytes() == allocated_bytes);
    TEST_EXPECT(ctx, memory::reattribute(block, &contexts.host));
    expect_bytes(ctx, block, bytes.data(), snapshot);
    TEST_EXPECT(ctx, view.is_ready() && view.value_count() == block.document().value_count());
    CBakedDocumentBlock moved{ std::move(block) };
    TEST_EXPECT(ctx, !block.is_ready() && !block.document().is_ready() && block.memory_attribution().allocation_count == 0u);
    TEST_EXPECT(ctx, memory::reattribute(block, &contexts.incompatible));
    expect_bytes(ctx, moved, bytes.data(), snapshot);
    TEST_EXPECT(ctx, memory::reattribute(moved, &contexts.executive));
    TEST_EXPECT(ctx, contexts.host.is_attribution_empty());
    TEST_EXPECT(ctx, contexts.state.allocations == allocation_calls && contexts.state.deallocations == deallocation_calls);
    //  Full validation uses its own scratch; keep it outside the measurement
    //  proving that moving and reattributing the block allocate nothing.
    TEST_EXPECT(ctx, view.check_integrity());
    moved.deallocate();
    TEST_EXPECT(ctx, !moved.is_ready() && !moved.document().is_ready());
    expect_empty(ctx, contexts);
}

static void test_owner_attribution(TTestContext& ctx)
{
    static_assert(k_is_erased_owner_payload_v<BakedDocumentAsset>);
    static_assert(k_system_type_id_v<BakedDocumentAsset> == system_type_ids::baked_document_asset);
    static_assert(std::is_nothrow_move_constructible_v<BakedDocumentAsset>);
    static_assert(!std::is_copy_constructible_v<BakedDocumentAsset>);
    CContexts contexts;
    const tests::TMemoryContextScope source_scope{ &contexts.executive };
    CErasedOwner owner = make_owner(ctx);
    BakedDocumentAsset* const payload = owner.payload<BakedDocumentAsset>();
    if (payload == nullptr)
    {
        return;
    }
    const auto bytes = payload->block.bytes();
    const std::vector<std::uint8_t> snapshot(bytes.data(), bytes.data() + bytes.size());
    const std::uint64_t total_bytes = contexts.executive.get_live_allocated_bytes();
    const std::size_t allocation_calls = contexts.state.allocations;
    TEST_EXPECT(ctx, contexts.executive.get_live_allocation_count() == 2u);
    TEST_EXPECT(ctx, total_bytes > payload->block.memory_attribution().allocation_size);
    owner.add_hazard(mount_point_ids::asset);
    TEST_EXPECT(ctx, !owner.can_reattribute_to(&contexts.incompatible) && !owner.reattribute(&contexts.incompatible));
    TEST_EXPECT(ctx, owner.memory_context() == &contexts.executive && owner.payload<BakedDocumentAsset>() == payload);
    TEST_EXPECT(ctx, owner.reattribute(&contexts.host));
    TEST_EXPECT(ctx, contexts.executive.is_attribution_empty());
    TEST_EXPECT(ctx, contexts.host.get_live_allocation_count() == 2u && contexts.host.get_live_allocated_bytes() == total_bytes);
    TEST_EXPECT(ctx, owner.has_hazard(mount_point_ids::asset));
    expect_bytes(ctx, payload->block, bytes.data(), snapshot);
    TEST_EXPECT(ctx, owner.reattribute(&contexts.executive));
    TEST_EXPECT(ctx, contexts.host.is_attribution_empty());

    //  A nested block moved independently into a different context must not
    //  let the owner silently attribute both allocations to a single source.
    TEST_EXPECT(ctx, memory::reattribute(payload->block, &contexts.host));
    TEST_EXPECT(ctx, !owner.can_reattribute_to(&contexts.transport) && !owner.reattribute(&contexts.transport));
    TEST_EXPECT(ctx, contexts.executive.get_live_allocation_count() == 1u && contexts.host.get_live_allocation_count() == 1u);
    TEST_EXPECT(ctx, contexts.transport.is_attribution_empty());
    TEST_EXPECT(ctx, memory::reattribute(payload->block, &contexts.executive));
    TEST_EXPECT(ctx, owner.can_reattribute_to(&contexts.transport));
    TEST_EXPECT(ctx, contexts.state.allocations == allocation_calls);
    owner.destroy();
    expect_empty(ctx, contexts);

    CErasedOwner empty_block = CErasedOwner::create<BakedDocumentAsset>();
    TEST_EXPECT(ctx, !empty_block.payload<BakedDocumentAsset>()->block.is_ready());
    TEST_EXPECT(ctx, empty_block.reattribute(&contexts.host));
    TEST_EXPECT(ctx, contexts.executive.is_attribution_empty() && contexts.host.get_live_allocation_count() == 1u);
    empty_block.destroy();
    expect_empty(ctx, contexts);
}

static void test_message_and_repository(TTestContext& ctx)
{
    CContexts contexts;
    const tests::TMemoryContextScope source_scope{ &contexts.executive };
    threading::transports::CErasedOwnerMsgTransport transport{ module_ids::executable, &contexts.transport, &contexts.host };
    TEST_EXPECT(ctx, transport.initialise(1u));
    const std::uint64_t queue_bytes = contexts.transport.get_live_allocated_bytes();
    CErasedOwner owner = make_owner(ctx);
    BakedDocumentAsset* const payload = owner.payload<BakedDocumentAsset>();
    if (payload == nullptr)
    {
        return;
    }
    const auto bytes = payload->block.bytes();
    const std::vector<std::uint8_t> snapshot(bytes.data(), bytes.data() + bytes.size());
    const CBakedDocument view = payload->block.document();
    const std::uint64_t owned_bytes = contexts.executive.get_live_allocated_bytes();
    const std::size_t allocation_calls = contexts.state.allocations;
    threading::CErasedOwnerMsg message;
    message.set_message_type<BakedDocumentAsset>();
    message.set_async_slot(17);
    message.set_owner(std::move(owner));
    TEST_EXPECT(ctx, transport.post(std::move(message)));
    TEST_EXPECT(ctx, !message.has_owner() && !message.has_message_type() && owner.is_empty());
    TEST_EXPECT(ctx, contexts.executive.is_attribution_empty());
    TEST_EXPECT(ctx, contexts.transport.get_live_allocation_count() == 3u);
    TEST_EXPECT(ctx, contexts.transport.get_live_allocated_bytes() == queue_bytes + owned_bytes);
    threading::CErasedOwnerMsg received;
    TEST_EXPECT(ctx, transport.read(received));
    TEST_EXPECT(ctx, received.is_message_a<BakedDocumentAsset>() && received.query_async_slot() == 17);
    TEST_EXPECT(ctx, received.owner().memory_context() == &contexts.host && received.owner().payload<BakedDocumentAsset>() == payload);
    TEST_EXPECT(ctx, contexts.transport.get_live_allocation_count() == 1u && contexts.transport.get_live_allocated_bytes() == queue_bytes);
    TEST_EXPECT(ctx, contexts.host.get_live_allocation_count() == 2u && contexts.host.get_live_allocated_bytes() == owned_bytes);
    TEST_EXPECT(ctx, contexts.state.allocations == allocation_calls);
    expect_bytes(ctx, payload->block, bytes.data(), snapshot);
    TEST_EXPECT(ctx, view.check_integrity());
    {
        const tests::TMemoryContextScope host_scope{ &contexts.host };
        CAssetRepository assets;
        TEST_EXPECT(ctx, assets.initialise());
        CErasedOwner stored = received.take_owner();
        const CAssetId id = assets.insert(std::move(stored));
        TEST_EXPECT(ctx, id.is_valid() && stored.is_empty());
        const CAssetRecord* const record = assets.resolve(id);
        TEST_EXPECT(ctx, record != nullptr && record->payload<BakedDocumentAsset>() == payload);
        expect_bytes(ctx, payload->block, bytes.data(), snapshot);
        const std::uint32_t live_allocations = contexts.host.get_live_allocation_count();
        const std::uint64_t live_bytes = contexts.host.get_live_allocated_bytes();
        TEST_EXPECT(ctx, assets.erase(id) && assets.resolve(id) == nullptr);
        TEST_EXPECT(ctx, contexts.host.get_live_allocation_count() == live_allocations - 2u);
        TEST_EXPECT(ctx, contexts.host.get_live_allocated_bytes() == live_bytes - owned_bytes);
        //  The borrowed view is no longer used after erasing its owner.
        assets.deallocate();
    }
    transport.deallocate();
    expect_empty(ctx, contexts);
}

static void test_rejected_and_unread_messages(TTestContext& ctx)
{
    CContexts contexts;
    const tests::TMemoryContextScope source_scope{ &contexts.executive };
    threading::transports::CErasedOwnerMsgTransport transport{ module_ids::executable, &contexts.transport, &contexts.host };
    threading::transports::CErasedOwnerMsgTransport incompatible{ module_ids::executable, &contexts.incompatible };
    TEST_EXPECT(ctx, transport.initialise(1u) && incompatible.initialise(1u));
    CErasedOwner owner = make_owner(ctx);
    BakedDocumentAsset* const payload = owner.payload<BakedDocumentAsset>();
    if (payload == nullptr)
    {
        return;
    }
    const auto bytes = payload->block.bytes();
    const std::vector<std::uint8_t> snapshot(bytes.data(), bytes.data() + bytes.size());
    const std::uint64_t owned_bytes = contexts.executive.get_live_allocated_bytes();
    threading::CErasedOwnerMsg message;
    message.set_message_type<BakedDocumentAsset>();
    message.set_async_slot(29);
    message.set_owner(std::move(owner));
    TEST_EXPECT(ctx, !incompatible.post(std::move(message)));
    TEST_EXPECT(ctx, message.owner().payload<BakedDocumentAsset>() == payload && message.query_async_slot() == 29);
    TEST_EXPECT(ctx, contexts.executive.get_live_allocation_count() == 2u && contexts.executive.get_live_allocated_bytes() == owned_bytes);
    while (transport.writable_count() != 0u)
    {
        threading::CErasedOwnerMsg filler;
        filler.set_message_type<FileSaveResult>();
        TEST_EXPECT(ctx, transport.post(std::move(filler)));
    }
    const std::size_t allocation_calls = contexts.state.allocations;
    TEST_EXPECT(ctx, !transport.post(std::move(message)));
    TEST_EXPECT(ctx, message.is_message_a<BakedDocumentAsset>() && message.query_async_slot() == 29);
    TEST_EXPECT(ctx, message.owner().memory_context() == &contexts.executive && message.owner().payload<BakedDocumentAsset>() == payload);
    TEST_EXPECT(ctx, contexts.executive.get_live_allocation_count() == 2u && contexts.executive.get_live_allocated_bytes() == owned_bytes);
    expect_bytes(ctx, payload->block, bytes.data(), snapshot);
    threading::CErasedOwnerMsg drained;
    while (transport.read(drained))
    {
        TEST_EXPECT(ctx, !drained.has_owner());
    }
    TEST_EXPECT(ctx, transport.post(std::move(message)));
    TEST_EXPECT(ctx, contexts.executive.is_attribution_empty() && !message.has_owner());
    TEST_EXPECT(ctx, contexts.state.allocations == allocation_calls);
    //  Unread owners must be destroyed under the transport's attribution.
    transport.deallocate();
    incompatible.deallocate();
    expect_empty(ctx, contexts);
}

static void test_aligned_file_round_trip(TTestContext& ctx)
{
    const FileLoadRequest default_request{};
    TEST_EXPECT(ctx, default_request.alignment == 16u);
    const std::string path = test_environment::test_log_path("baked_storage_round_trip");
    const std::string empty_path = test_environment::test_log_path("baked_storage_empty");
    const std::string missing_path = test_environment::test_log_path("baked_storage_missing");
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    CBakedDocumentBlock original;
    TEST_EXPECT(ctx, document_translation::bake(live, original));
    TEST_EXPECT(ctx, original.bytes().size() == baked_document_format::k_min_document_size);
    const bool saved = platform::filesystem::saveFile(path.c_str(), original.bytes());
    TEST_EXPECT(ctx, saved);
    if (!saved) return;

    const auto native_path = platform::path::makeNativePath(path.c_str());
    std::FILE* handle = platform::filesystem::openFile(native_path);
    TEST_EXPECT(ctx, handle != nullptr);
    if (handle)
    {
        TEST_EXPECT(ctx, platform::filesystem::getFileSize(handle) == original.bytes().size());
        TEST_EXPECT(ctx, std::fclose(handle) == 0);
    }

    CByteBuffer default_load = platform::filesystem::loadFile(path.c_str());
    TEST_EXPECT(ctx, default_load.align() == 16u && default_load.size() == baked_document_format::k_min_document_size && default_load.capacity() == baked_document_format::k_min_block_capacity);
    struct SAlignment { std::size_t request; std::size_t expected; };
    constexpr SAlignment alignments[]{ {0u, 16u}, {1u, 16u}, {8u, 16u}, {16u, 16u},
        {24u, 16u}, {32u, 32u}, {48u, 16u}, {64u, 64u}, {256u, 256u} };
    for (const SAlignment& alignment : alignments)
    {
        CByteBuffer loaded = platform::filesystem::loadFile(path.c_str(), 5u, alignment.request);
        TEST_EXPECT(ctx, loaded.is_ready());
        if (!loaded.is_ready()) continue;
        TEST_EXPECT(ctx, loaded.align() == alignment.expected);
        TEST_EXPECT(ctx, (reinterpret_cast<std::uintptr_t>(loaded.data()) & (alignment.expected - 1u)) == 0u);
        TEST_EXPECT(ctx, loaded.size() == original.bytes().size() + 5u);
        TEST_EXPECT(ctx, loaded.capacity() == memory::condition_bytes(alignment.expected, loaded.size()));
        TEST_EXPECT(ctx, std::memcmp(loaded.data(), original.bytes().data(), original.bytes().size()) == 0);
        for (std::size_t index = original.bytes().size(); index < loaded.capacity(); ++index)
        {
            TEST_EXPECT(ctx, loaded.data()[index] == 0u);
        }
        if (alignment.expected >= 32u)
        {
            const auto pointer = loaded.data();
            const auto attribution = loaded.memory_attribution();
            CBakedDocumentBlock adopted;
            TEST_EXPECT(ctx, adopted.adopt(std::move(loaded)));
            TEST_EXPECT(ctx, !loaded.is_ready() && adopted.bytes().data() == pointer);
            TEST_EXPECT(ctx, adopted.memory_attribution().source == attribution.source);
            TEST_EXPECT(ctx, adopted.memory_attribution().allocation_size == attribution.allocation_size);
            TEST_EXPECT(ctx, adopted.bytes().size() == original.bytes().size());
            TEST_EXPECT(ctx, adopted.document().check_integrity());
            TEST_EXPECT(ctx, std::memcmp(adopted.bytes().data(), original.bytes().data(), original.bytes().size()) == 0);
        }
    }

    //  No padding: the loader allocates the final block directly and adoption keeps it.
    CByteBuffer direct = platform::filesystem::loadFile(path.c_str(), 0u, 32u);
    const auto direct_pointer = direct.data();
    CBakedDocumentBlock reloaded;
    TEST_EXPECT(ctx, reloaded.adopt(std::move(direct)));
    TEST_EXPECT(ctx, reloaded.bytes().data() == direct_pointer && reloaded.bytes().size() == baked_document_format::k_min_document_size);
    TEST_EXPECT(ctx, platform::filesystem::saveFile(path.c_str(), reloaded.bytes()));
    handle = platform::filesystem::openFile(native_path);
    TEST_EXPECT(ctx, handle != nullptr);
    if (handle)
    {
        TEST_EXPECT(ctx, platform::filesystem::getFileSize(handle) == baked_document_format::k_min_document_size);
        TEST_EXPECT(ctx, std::fclose(handle) == 0);
    }

    TEST_EXPECT(ctx, !platform::filesystem::loadFile(path.c_str(), 0u, memory::k_byte_size_ceiling + 1u).is_ready());
    TEST_EXPECT(ctx, !platform::filesystem::loadFile(path.c_str(), 0u, std::numeric_limits<std::size_t>::max()).is_ready());
    TEST_EXPECT(ctx, !platform::filesystem::loadFile(path.c_str(), std::numeric_limits<std::size_t>::max(), 32u).is_ready());
    TEST_EXPECT(ctx, !platform::filesystem::loadFile(path.c_str(), memory::k_byte_size_ceiling, 32u).is_ready());
    TEST_EXPECT(ctx, !platform::filesystem::loadFile(missing_path.c_str(), 0u, 32u).is_ready());

    const auto native_empty_path = platform::path::makeNativePath(empty_path.c_str());
    handle = platform::filesystem::openFile(native_empty_path, platform::filesystem::EOpenMode::BinaryWrite);
    TEST_EXPECT(ctx, handle != nullptr);
    if (handle)
    {
        TEST_EXPECT(ctx, std::fclose(handle) == 0);
        TEST_EXPECT(ctx, !platform::filesystem::loadFile(empty_path.c_str(), 0u, 32u).is_ready());
        TEST_EXPECT(ctx, !platform::filesystem::loadFile(empty_path.c_str(), 17u, 32u).is_ready());
        platform::filesystem::removeFile(native_empty_path);
    }

    tests::TAllocatorFixture failing{ true };
    memory::CMemoryAllocator allocator{ &failing, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        TEST_EXPECT(ctx, !platform::filesystem::loadFile(path.c_str(), 0u, 32u).is_ready());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    platform::filesystem::removeFile(native_path);
}

}   //  namespace baked_document_transfer_tests

int run_baked_document_transfer_tests()
{
    tests::TTestContext ctx;
    baked_document_transfer_tests::test_block_attribution(ctx);
    baked_document_transfer_tests::test_owner_attribution(ctx);
    baked_document_transfer_tests::test_message_and_repository(ctx);
    baked_document_transfer_tests::test_rejected_and_unread_messages(ctx);
    baked_document_transfer_tests::test_aligned_file_round_trip(ctx);
    std::cout << "BakedDocumentTransfer: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0u) ? 0 : 1;
}
