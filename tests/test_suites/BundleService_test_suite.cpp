
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BundleService_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Oct 26
//
//  Controlled worker completions and retained-record bundle lifetimes.

#include <cstring>
#include <iostream>
#include <vector>

#include "assets/internal/bundle_builder_io.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "host/runtime/asset_service.hpp"
#include "rendering/interface/texture_messages.hpp"
#include "platform/threading/processor_relax.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/BundleService_test_suite.hpp"

namespace
{

using tests::TTestContext;
using filesystem_image::ERootState;
using host::CAssetService;

bool parse(const char* text, CLiveDocument& document)
{
    return document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(text), std::strlen(text) }, document).accepted();
}

bool make_bundle(CBakedDocumentBlock& block)
{
    CLiveDocument inventory, document;
    CBakedDocumentBlock baked;
    if (!parse(R"({"raw":{},"baked":{}})", inventory) || !parse(R"({"answer":42})", document) ||
        !document_translation::bake(document, baked)) return false;
    const std::size_t prefix_size = baked.bytes().size();
    if (!baked.resize_buffer(prefix_size + 19u, 128u)) return false;
    std::memset(baked.buffer_bytes().data() + prefix_size, 0x5a, 19u);
    TPodVector<bundle::SBuildSource> sources;
    if (!sources.push_back({ inventory.object_child(inventory.root(), CStringView{ "raw" }), "raw" }) ||
        !sources.push_back({ inventory.object_child(inventory.root(), CStringView{ "baked" }), "baked" })) return false;
    const bundle::detail::SBuildIo io{ &baked,
        [](void* state, const char* name, std::size_t& size) noexcept
        {
            size = (name[0] == 'r') ? 3u : static_cast<CBakedDocumentBlock*>(state)->buffer_bytes().size();
            return true;
        },
        [](void* state, const char* name, const CByteView& bytes) noexcept
        {
            const void* source = (name[0] == 'r') ? static_cast<const void*>("abc") : static_cast<CBakedDocumentBlock*>(state)->buffer_bytes().data();
            std::memcpy(bytes.data(), source, bytes.size());
            return true;
        } };
    bundle::CBundleView view;
    return bundle::detail::build(inventory, inventory.root(), sources.const_view(), block, view, io).succeeded();
}

std::uint32_t MV_STD_ABI_CALL echo_messages(void* data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    threading::CThreadContext context(resources);
    context.startup();
    context.mark_running();
    while (!context.exit_requested())
    {
        const auto epoch = resources.wait_predicate.get_word();
        threading::CErasedPodMsg message;
        while (context.read(message))
        {
            if (!context.post(message)) { context.mark_failed(1u); return 1u; }
        }
        if (!context.exit_requested()) (void)context.wait_for_new_epoch(epoch);
    }
    context.mark_exited();
    return 0u;
}

struct SFixture
{
    platform::system::CPerfCountConversion conversion;
    threading::CThreadPackage package{ threading::ThreadConfig{ thread_ids::rendering, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &echo_messages }, conversion };
    filesystem_image::CImage image;
    CAssetService service;
    std::uint64_t binding{ 0u };

    ~SFixture() noexcept
    {
        (void)package.shutdown();
        service.detach_client(package);
        service.deallocate();
    }
    bool initialise()
    {
        CLiveDocument document;
        service.set_filesystem(image);
        service.set_rendering(&package);
        return parse(R"({"roots":{"work:":{"source":"unused","writable":true,"content":{}}}})", document) &&
            image.adopt(std::move(document)) && conversion.init() && service.initialise() &&
            service.configure_workers(package, nullptr) && package.startup();
    }
    AssetResult publish(TTestContext& ctx, threading::CThreadPackage* owner = nullptr)
    {
        BundlePreparation prepared;
        TEST_EXPECT(ctx, make_bundle(prepared.block));
        const void* before = prepared.block.buffer_bytes().data();
        TEST_EXPECT(ctx, prepared.view.reset(before, prepared.block.buffer_bytes().size()).succeeded());
        TEST_EXPECT(ctx, prepared.directory.prepare(prepared.view));
        TEST_EXPECT(ctx, image.reserve_bundle("pack:", binding));
        const auto result = service.publish_bundle(prepared, binding, (owner != nullptr) ? *owner : package);
        TEST_EXPECT(ctx, result.status == EAssetStatus::success && result.asset && !prepared.block.is_ready());
        TEST_EXPECT(ctx, result.bundle_view().bytes().data() == before);
        return result;
    }
    filesystem_image::SResolvedSource resolve(TTestContext& ctx, const char* path = "pack:/raw")
    {
        filesystem_image::SResolvedSource result;
        TEST_EXPECT(ctx, image.resolve(path, false, result));
        return result;
    }
    template<typename T> bool receive(T& result, std::int32_t expected_slot, threading::CThreadPackage* recipient = nullptr)
    {
        platform::system::CPerfCounter timer;
        (void)timer.update();
        while (timer.query_delta() < conversion.query_ticks_per_second() * 5u)
        {
            threading::CErasedPodMsg message;
            if (((recipient != nullptr) ? *recipient : package).read(message))
                return (message.query_async_slot() == expected_slot) && message.copy_payload_to(result);
            platform::threading::processor_relax();
        }
        return false;
    }
    template<typename T> void request(TTestContext& ctx, std::int32_t slot, CErasedOwner&& owner)
    {
        TEST_EXPECT(ctx, owner.payload<T>() != nullptr);
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(slot);
        message.set_owner(std::move(owner));
        service.request(message, package);
    }
    void dispose(TTestContext& ctx, CAssetId asset)
    {
        service.request_disposal({ asset }, 7, package);
        service.complete_disposals();
        AssetDisposeResult result;
        TEST_EXPECT(ctx, receive(result, 7) && result.asset == asset && result.status == EAssetStatus::success);
    }
    void notification(TTestContext& ctx, CAssetId asset)
    {
        BundleDisposedNotification result;
        TEST_EXPECT(ctx, receive(result, -1) && result.asset == asset);
        TEST_EXPECT(ctx, image.root_state("pack:") == ERootState::missing);
        service.complete_disposals();
        //  FIFO barrier proves no duplicate notification is queued.
        service.request_disposal({ asset }, 8, package);
        AssetDisposeResult rejected;
        TEST_EXPECT(ctx, receive(rejected, 8) && rejected.status == EAssetStatus::invalid_asset);
    }
    void save(TTestContext& ctx, CAssetId asset, std::int32_t slot)
    {
        auto owner = CErasedOwner::create<AssetSaveRequest>();
        auto* save = owner.payload<AssetSaveRequest>();
        TEST_EXPECT(ctx, save != nullptr && save->file.set("work:/saved.bdl"));
        if (save == nullptr) return;
        save->source = asset;
        save->settings.format = EAssetFileFormat::raw;
        request<AssetSaveRequest>(ctx, slot, std::move(owner));
    }
    void complete_save(std::int32_t slot)
    {
        threading::CErasedPodMsg message;
        message.set_async_slot(slot);
        message.assign_payload(FileSaveResult{ true });
        service.complete(message, package);
    }
};

void test_closure_and_retained_records(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    const auto original = fixture.publish(ctx);
    const auto bytes = original.byte_view();
    const std::vector<std::uint8_t> snapshot(bytes.data(), bytes.data() + bytes.size());
    auto resolved = fixture.resolve(ctx, "PACK:/RAW");
    CAssetService::CBundleOperation operation;
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(resolved, 256u, operation) == EAssetStatus::invalid_request);
    TEST_EXPECT(ctx, !fixture.image.resolve("pack:/../raw", false, resolved));
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(resolved, 128u, operation) == EAssetStatus::success);
    const auto first = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
    const auto second = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
    TEST_EXPECT(ctx, first.asset && second.asset && first.asset != second.asset && first.byte_view().data() == second.byte_view().data());
    TEST_EXPECT(ctx, !fixture.service.acquire_bundle(first.asset).asset);
    TEST_EXPECT(ctx, !fixture.service.retain_bundle_file(operation, EAssetFileFormat::baked).asset);
    TEST_EXPECT(ctx, fixture.image.loaded("pack:/raw", first.asset.query_value(), 0u, 0u, 0u, fixture.binding));
    TEST_EXPECT(ctx, fixture.image.loaded("pack:/raw", second.asset.query_value(), 0u, 0u, 0u, fixture.binding));
    operation.reset();
    TEST_EXPECT(ctx, fixture.service.is_idle());
    fixture.dispose(ctx, first.asset);
    TEST_EXPECT(ctx, fixture.image.cached_asset("pack:/raw", 0u) == second.asset.query_value());

    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx, "pack:/baked"), 16u, operation) == EAssetStatus::success);
    CAssetService::CBundleOperation moved{ std::move(operation) };
    TEST_EXPECT(ctx, !operation.bytes().is_ready() && moved.bytes().is_ready());
    fixture.service.request_disposal({ original.asset }, 9, fixture.package);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::closed && !fixture.service.is_idle());
    TEST_EXPECT(ctx, !fixture.service.acquire_bundle(original.asset).asset);
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(resolved, 128u, operation) == EAssetStatus::invalid_asset);
    TEST_EXPECT(ctx, fixture.image.cached_asset("pack:/raw", 0u) == 0u);
    const auto late = fixture.service.retain_bundle_file(moved, EAssetFileFormat::baked);
    TEST_EXPECT(ctx, late.asset && late.document_view().is_ready());
    TEST_EXPECT(ctx, late.byte_view().size() == late.document_view().byte_count() + 19u &&
        late.byte_view().size() == moved.bytes().size() && late.byte_view().data() == moved.bytes().data());
    TEST_EXPECT(ctx, late.byte_view().data()[late.byte_view().size() - 1u] == 0x5au);
    moved.reset();
    TEST_EXPECT(ctx, fixture.service.is_idle());
    TEST_EXPECT(ctx, std::memcmp(snapshot.data(), bytes.data(), bytes.size()) == 0);
    TEST_EXPECT(ctx, std::memcmp(second.byte_view().data(), "abc", 3u) == 0);
    fixture.dispose(ctx, second.asset);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::closed);
    fixture.dispose(ctx, late.asset);
    fixture.notification(ctx, original.asset);
    const auto replacement = fixture.publish(ctx);
    TEST_EXPECT(ctx, replacement.asset.query_value() > late.asset.query_value());
    fixture.service.request_disposal({ replacement.asset }, 10, fixture.package);
    fixture.service.complete_disposals();
    fixture.notification(ctx, replacement.asset);
    TEST_EXPECT(ctx, !fixture.service.failed());
}

void test_resolved_ranges(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    const auto original = fixture.publish(ctx);
    auto source = fixture.resolve(ctx);
    const auto offset = source.offset;
    const auto size = source.size;
    const auto binding = source.binding;
    CAssetService::CBundleOperation operation;
    for (const std::size_t alignment : { 0u, 3u, 256u })
    {
        TEST_EXPECT(ctx, fixture.service.begin_bundle_file(source, alignment, operation) == EAssetStatus::invalid_request);
        TEST_EXPECT(ctx, !operation.bytes().is_ready() && fixture.service.is_idle());
    }
    struct SRange { std::size_t offset; std::size_t size; };
    const SRange invalid[]{ { 0u, size }, { offset + 1u, size }, { offset, 0u },
        { SIZE_MAX, size }, { SIZE_MAX - 127u, size }, { offset, SIZE_MAX },
        { original.byte_view().size(), size }, { offset, original.byte_view().size() - offset } };
    for (const auto range : invalid)
    {
        source.offset = range.offset;
        source.size = range.size;
        TEST_EXPECT(ctx, fixture.service.begin_bundle_file(source, 128u, operation) == EAssetStatus::invalid_request);
        TEST_EXPECT(ctx, !operation.bytes().is_ready() && fixture.service.is_idle());
    }
    source.offset = offset;
    source.size = size;
    source.binding = 0u;
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(source, 128u, operation) == EAssetStatus::invalid_asset);
    source.binding = binding;
    const auto complete = fixture.service.acquire_bundle(original.asset);
    TEST_EXPECT(ctx, complete.asset);
    source.bundle = complete.asset.query_value();
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(source, 128u, operation) == EAssetStatus::invalid_asset);
    fixture.dispose(ctx, complete.asset);
    source.bundle = original.asset.query_value();

    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(source, 128u, operation) == EAssetStatus::success);
    const auto direct = original.bundle_view().lookup(CStringView{ "raw" });
    TEST_EXPECT(ctx, operation.bytes().data() == direct.bytes.data() && operation.bytes().size() == direct.bytes.bytes());
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx, "pack:/baked"), 128u, operation) == EAssetStatus::invalid_request);
    TEST_EXPECT(ctx, operation.bytes().data() == direct.bytes.data());
    fixture.service.request_disposal({ original.asset }, 60, fixture.package);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::closed && !fixture.service.is_idle());
    CAssetService::CBundleOperation rejected;
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(source, 128u, rejected) == EAssetStatus::invalid_asset);
    TEST_EXPECT(ctx, std::memcmp(operation.bytes().data(), "abc", 3u) == 0);
    operation.reset();
    fixture.service.complete_disposals();
    fixture.notification(ctx, original.asset);

    const auto replacement = fixture.publish(ctx);
    TEST_EXPECT(ctx, fixture.binding != binding);
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(source, 128u, operation) == EAssetStatus::invalid_asset);
    //  Even a live original ID cannot legitimise the previous binding's range.
    source.bundle = replacement.asset.query_value();
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(source, 128u, operation) == EAssetStatus::invalid_asset);
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx), 128u, operation) == EAssetStatus::success);
    operation.reset();
    fixture.service.request_disposal({ replacement.asset }, 61, fixture.package);
    fixture.service.complete_disposals();
    fixture.notification(ctx, replacement.asset);
    TEST_EXPECT(ctx, fixture.service.is_idle() && !fixture.service.failed());
}

void test_complete_views_and_saves(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    const auto original = fixture.publish(ctx);
    const auto retained = fixture.service.acquire_bundle(original.asset);
    const auto another = fixture.service.acquire_bundle(retained.asset);
    TEST_EXPECT(ctx, retained.asset && another.asset && retained.asset != another.asset && retained.asset != original.asset);
    fixture.dispose(ctx, another.asset);
    fixture.save(ctx, original.asset, 20);
    FileSaveRequest work;
    //  First operation gets slot zero; nothing else is outstanding.
    TEST_EXPECT(ctx, fixture.receive(work, 0) && work.view.size() == original.byte_view().size());
    CAssetService::CBundleOperation operation;
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx), 128u, operation) == EAssetStatus::success);
    const auto file = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
    operation.reset();
    {
        tests::TAssertionTestScope assertions(ctx);
        assertions.expect_unconditional_assertion(ctx, [&] { fixture.service.request_disposal({ original.asset }, 9, fixture.package); });
    }
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, !fixture.service.acquire_bundle(retained.asset).asset);
    fixture.save(ctx, original.asset, 21);
    AssetResult rejected;
    TEST_EXPECT(ctx, fixture.receive(rejected, 21) && rejected.status == EAssetStatus::invalid_asset);
    fixture.save(ctx, retained.asset, 22);
    TEST_EXPECT(ctx, fixture.receive(rejected, 22) && rejected.status == EAssetStatus::invalid_asset);
    TEST_EXPECT(ctx, work.view.data()[0] == original.byte_view().data()[0]);
    fixture.complete_save(0);
    AssetResult saved;
    TEST_EXPECT(ctx, fixture.receive(saved, 20) && saved.status == EAssetStatus::success);
    fixture.dispose(ctx, retained.asset);
    fixture.save(ctx, file.asset, 23);
    TEST_EXPECT(ctx, fixture.receive(work, 0) && work.view.size() == 3u && std::memcmp(work.view.data(), "abc", 3u) == 0);
    fixture.service.request_disposal({ file.asset }, 24, fixture.package);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::closed);
    fixture.complete_save(0);
    TEST_EXPECT(ctx, fixture.receive(saved, 23) && saved.status == EAssetStatus::success);
    fixture.service.complete_disposals();
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, fixture.receive(disposed, 24) && disposed.status == EAssetStatus::success);
    fixture.notification(ctx, original.asset);
}

void test_dependency_cleanup_and_recipient_exit(TTestContext& ctx)
{
    for (bool detach : { false, true })
    {
        SFixture fixture;
        TEST_EXPECT(ctx, fixture.initialise());
        const auto original = fixture.publish(ctx);
        CAssetService::CBundleOperation operation;
        TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx), 128u, operation) == EAssetStatus::success);
        const auto dependent = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw, mount_point_ids::executive);
        const auto survivor = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
        TEST_EXPECT(ctx, dependent.asset && survivor.asset);
        operation.reset();
        fixture.service.request_disposal({ original.asset }, 9, fixture.package);
        TEST_EXPECT(ctx, fixture.service.is_idle());
        {
            tests::TAssertionTestScope assertions(ctx);
            assertions.expect_unconditional_assertion(ctx, [&] { fixture.service.dispose_dependencies(mount_point_ids::executive); });
        }
        TEST_EXPECT(ctx, fixture.service.is_idle() && fixture.image.root_state("pack:") == ERootState::closed);
        TEST_EXPECT(ctx, std::memcmp(survivor.byte_view().data(), "abc", 3u) == 0);
        if (detach) fixture.service.detach_client(fixture.package);
        else (void)fixture.package.shutdown();
        //  Disposal can be requested by a different, still-live client.
        SFixture client;
        TEST_EXPECT(ctx, client.initialise());
        fixture.service.request_disposal({ survivor.asset }, 7, client.package);
        fixture.service.complete_disposals();
        AssetDisposeResult disposed;
        TEST_EXPECT(ctx, client.receive(disposed, 7) && disposed.status == EAssetStatus::success);
        TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing && !fixture.service.failed());
        if (detach)
        {
            fixture.service.request_disposal({ original.asset }, 8, fixture.package);
            TEST_EXPECT(ctx, fixture.receive(disposed, 8) && disposed.status == EAssetStatus::invalid_asset);
        }
    }
}

void test_management_owner(TTestContext& ctx)
{
    SFixture owner, borrower;
    TEST_EXPECT(ctx, owner.initialise() && borrower.initialise());
    const auto original = owner.publish(ctx);
    //  Both fixtures use the same thread role; authority belongs to the instance.
    owner.service.request_disposal({ original.asset }, 40, borrower.package);
    AssetDisposeResult rejected;
    TEST_EXPECT(ctx, borrower.receive(rejected, 40) && rejected.asset == original.asset &&
        rejected.status == EAssetStatus::invalid_request);
    owner.service.complete_disposals();
    TEST_EXPECT(ctx, owner.image.root_state("pack:") == ERootState::open);
    owner.service.request_reference({ original.asset }, 41, borrower.package);
    AssetResult reference;
    TEST_EXPECT(ctx, borrower.receive(reference, 41) && reference.status == EAssetStatus::success && reference.asset);
    TEST_EXPECT(ctx, reference.bundle_view().lookup(CStringView{ "raw" }).bytes.bytes() == 3u);
    owner.service.request_disposal({ reference.asset }, 42, borrower.package);
    owner.service.complete_disposals();
    TEST_EXPECT(ctx, borrower.receive(rejected, 42) && rejected.status == EAssetStatus::success);
    CAssetService::CBundleOperation operation;
    TEST_EXPECT(ctx, owner.service.begin_bundle_file(owner.resolve(ctx), 128u, operation) == EAssetStatus::success);
    const auto file = owner.service.retain_bundle_file(operation, EAssetFileFormat::raw);
    TEST_EXPECT(ctx, file.asset);
    operation.reset();
    owner.service.request_disposal({ original.asset }, 43, owner.package);
    owner.service.complete_disposals();
    TEST_EXPECT(ctx, owner.image.root_state("pack:") == ERootState::closed);
    TEST_EXPECT(ctx, std::memcmp(file.byte_view().data(), "abc", 3u) == 0);
    owner.service.request_disposal({ file.asset }, 44, borrower.package);
    owner.service.complete_disposals();
    TEST_EXPECT(ctx, borrower.receive(rejected, 44) && rejected.status == EAssetStatus::success);
    owner.notification(ctx, original.asset);
    //  A reply on the borrower's FIFO proves it did not receive the notification.
    owner.service.request_reference({ original.asset }, 45, borrower.package);
    TEST_EXPECT(ctx, borrower.receive(reference, 45) && reference.status == EAssetStatus::invalid_asset);
    TEST_EXPECT(ctx, !owner.service.failed());
}

void test_package_reuse(TTestContext& ctx)
{
    for (bool close : { false, true })
    {
        SFixture fixture;
        TEST_EXPECT(ctx, fixture.initialise());
        const threading::ThreadConfig config{ thread_ids::rendering, module_ids::executable,
            platform::threading::EThreadPriority::Normal, &echo_messages };
        auto package = TInstance<threading::CThreadPackage>::create(config, fixture.conversion);
        TEST_EXPECT(ctx, package && package->startup());
        const auto original = fixture.publish(ctx, &*package);
        CAssetService::CBundleOperation operation;
        TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx), 128u, operation) == EAssetStatus::success);
        const auto file = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
        TEST_EXPECT(ctx, file.asset);
        operation.reset();
        if (close) fixture.service.request_disposal({ original.asset }, 50, *package);
        TEST_EXPECT(ctx, package->shutdown());
        fixture.service.detach_client(*package);
        //  Reuse the exact package address and role, as a collection slot can do.
        const auto* address = &*package;
        TEST_EXPECT(ctx, package.emplace(config, fixture.conversion) && (&*package == address) && package->startup());
        fixture.service.request_disposal({ original.asset }, 51, *package);
        AssetDisposeResult rejected;
        TEST_EXPECT(ctx, fixture.receive(rejected, 51, &*package) &&
            rejected.status == (close ? EAssetStatus::invalid_asset : EAssetStatus::invalid_request));
        fixture.dispose(ctx, file.asset);
        //  An open orphan remains for full shutdown; detach does not transfer or close it.
        TEST_EXPECT(ctx, fixture.image.root_state("pack:") == (close ? ERootState::missing : ERootState::open));
        fixture.service.request_disposal({ original.asset }, 52, *package);
        TEST_EXPECT(ctx, fixture.receive(rejected, 52, &*package) &&
            rejected.status == (close ? EAssetStatus::invalid_asset : EAssetStatus::invalid_request));
        TEST_EXPECT(ctx, !fixture.service.failed());
        TEST_EXPECT(ctx, package->shutdown());
        fixture.service.detach_client(*package);
    }
}

void test_conversion_lifetime(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    const auto original = fixture.publish(ctx);
    CAssetService::CBundleOperation operation;
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx), 128u, operation) == EAssetStatus::success);
    const auto source = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
    operation.reset();
    auto request = CErasedOwner::create<TextureDecodeRequest>();
    request.payload<TextureDecodeRequest>()->source = source.asset;
    fixture.request<TextureDecodeRequest>(ctx, 30, std::move(request));
    TextureDecodeWork work;
    TEST_EXPECT(ctx, fixture.receive(work, 0) && work.input->size() == 3u);
    fixture.service.request_disposal({ original.asset }, 31, fixture.package);
    auto rejected_request = CErasedOwner::create<TextureDecodeRequest>();
    rejected_request.payload<TextureDecodeRequest>()->source = source.asset;
    fixture.request<TextureDecodeRequest>(ctx, 32, std::move(rejected_request));
    AssetResult rejected;
    TEST_EXPECT(ctx, fixture.receive(rejected, 32) && rejected.status == EAssetStatus::invalid_asset);
    fixture.service.request_disposal({ source.asset }, 33, fixture.package);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, !fixture.service.is_idle() && std::memcmp(work.input->data(), "abc", 3u) == 0);
    auto owner = CErasedOwner::create<TextureDecodeResult>();
    auto* decoded = owner.payload<TextureDecodeResult>();
    TEST_EXPECT(ctx, decoded != nullptr && decoded->texture.buffer.resize(4u, 16u));
    if (decoded == nullptr) return;
    std::memset(decoded->texture.buffer.data(), 0x5a, 4u);
    decoded->texture.description.width = 1u;
    decoded->texture.description.height = 1u;
    decoded->texture.description.row_pitch = 4u;
    decoded->status = ETextureStatus::success;
    threading::CErasedOwnerMsg completion;
    completion.set_message_type<TextureDecodeResult>();
    completion.set_async_slot(0);
    completion.set_owner(std::move(owner));
    fixture.service.complete(completion, fixture.package);
    AssetResult independent;
    TEST_EXPECT(ctx, fixture.receive(independent, 30) && independent.asset && independent.status == EAssetStatus::success);
    fixture.service.complete_disposals();
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, fixture.receive(disposed, 33) && disposed.status == EAssetStatus::success);
    fixture.notification(ctx, original.asset);
    TEST_EXPECT(ctx, fixture.service.is_idle() && independent.decoded_texture_view().bytes.data()[0] == 0x5a);
    fixture.dispose(ctx, independent.asset);
}

std::uint32_t MV_STD_ABI_CALL blocked_recipient(void* data) noexcept
{
    auto& resources = *static_cast<threading::CThreadResources*>(data);
    threading::CThreadContext context(resources);
    context.startup();
    //  Before startup completes the Host cannot post. Make a bounded inbox and
    //  deliberately leave it unread to exercise a live recipient's failed post.
    resources.host_to_worker_msgs.deallocate();
    if (!resources.host_to_worker_msgs.initialise_fixed(1u)) { context.mark_failed(1u); return 1u; }
    context.mark_running();
    while (!context.exit_requested())
    {
        const auto epoch = resources.wait_predicate.get_word();
        if (!context.exit_requested()) (void)context.wait_for_new_epoch(epoch);
    }
    context.mark_exited();
    return 0u;
}

void test_live_notification_failure(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    threading::CThreadPackage recipient{ threading::ThreadConfig{ thread_ids::executive, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &blocked_recipient }, fixture.conversion };
    TEST_EXPECT(ctx, recipient.startup());
    const auto original = fixture.publish(ctx, &recipient);
    threading::CErasedPodMsg filler;
    filler.assign_payload(AssetDisposeResult{});
    bool full = false;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        if (!recipient.post(filler)) { full = true; break; }
    }
    TEST_EXPECT(ctx, full);
    fixture.service.request_disposal({ original.asset }, 31, recipient);
    {
        tests::TAssertionTestScope diagnostics(ctx);
        fixture.service.complete_disposals();
    }
    TEST_EXPECT(ctx, fixture.service.failed() && fixture.image.root_state("pack:") == ERootState::missing);
    fixture.service.detach_client(recipient);
    (void)recipient.shutdown();
}

struct SAllocator { std::size_t calls{ 0u }; std::size_t fail_on{ SIZE_MAX }; };
void* MV_STD_ABI_CALL allocate(void* data, std::size_t alignment, std::size_t size) noexcept
{
    auto& state = *static_cast<SAllocator*>(data);
    if (++state.calls == state.fail_on) return nullptr;
    return tests::allocate_test_memory(nullptr, alignment, size);
}

void test_publication_failure_and_shutdown(TTestContext& ctx)
{
    bool succeeded = false;
    std::size_t failures = 0u;
    for (std::size_t attempt = 1u; attempt < 500u && !succeeded; ++attempt)
    {
        SAllocator allocator_state;
        memory::CMemoryAllocator allocator{ &allocator_state, &allocate, &tests::deallocate_test_memory, system_ids::host };
        memory::CMemoryContext context{ allocator, system_ids::host };
        {
            tests::TMemoryContextScope scope{ &context };
            filesystem_image::CImage image;
            CAssetService service;
            CLiveDocument doc;
            TEST_EXPECT(ctx, parse(R"({"roots":{}})", doc) && image.adopt(std::move(doc)) && service.initialise());
            service.set_filesystem(image);
            BundlePreparation prepared;
            TEST_EXPECT(ctx, make_bundle(prepared.block));
            const auto* bytes = prepared.block.buffer_bytes().data();
            TEST_EXPECT(ctx, prepared.view.reset(bytes, prepared.block.buffer_bytes().size()).succeeded());
            TEST_EXPECT(ctx, prepared.directory.prepare(prepared.view));
            std::uint64_t binding = 0u;
            TEST_EXPECT(ctx, image.reserve_bundle("pack:", binding));
            platform::system::CPerfCountConversion conversion;
            threading::CThreadPackage client{ {}, conversion };
            allocator_state.fail_on = allocator_state.calls + attempt;
            const auto result = service.publish_bundle(prepared, binding, client);
            allocator_state.fail_on = SIZE_MAX;
            succeeded = result.asset.is_valid();
            if (!succeeded)
            {
                ++failures;
                TEST_EXPECT(ctx, prepared.block.buffer_bytes().data() == bytes && image.root_state("pack:") == ERootState::missing);
                TEST_EXPECT(ctx, result.status == EAssetStatus::allocation_failed);
            }
            else
            {
                const auto reference = service.acquire_bundle(result.asset);
                TEST_EXPECT(ctx, reference.asset && service.is_idle());
                CAssetService::CBundleOperation operation;
                filesystem_image::SResolvedSource resolved;
                TEST_EXPECT(ctx, image.resolve("pack:/raw", false, resolved));
                TEST_EXPECT(ctx, service.begin_bundle_file(resolved, 128u, operation) == EAssetStatus::success);
                TEST_EXPECT(ctx, service.retain_bundle_file(operation, EAssetFileFormat::raw).asset);
                operation.reset();
            }
            //  Shutdown retires even undisposed records; there are no borrowers.
            service.deallocate();
            TEST_EXPECT(ctx, image.root_state("pack:") == ERootState::missing);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, succeeded && failures != 0u);
}

}   // namespace

int run_bundle_service_tests()
{
    TTestContext ctx;
    test_closure_and_retained_records(ctx);
    test_resolved_ranges(ctx);
    test_complete_views_and_saves(ctx);
    test_dependency_cleanup_and_recipient_exit(ctx);
    test_management_owner(ctx);
    test_package_reuse(ctx);
    test_conversion_lifetime(ctx);
    test_live_notification_failure(ctx);
    test_publication_failure_and_shutdown(ctx);
    std::cout << "BundleService: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.exit_code();
}
