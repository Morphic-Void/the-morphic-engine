
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BundleRequests_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Oct 26
//
//  Real worker requests, deterministic admission interleavings and persistence.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "host/runtime/asset_service.hpp"
#include "host/runtime/host_worker_thread.hpp"
#include "platform/filesystem/file.hpp"
#include "platform/threading/processor_relax.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/test_context.hpp"
#include "tests/test_suites/BundleRequests_test_suite.hpp"

namespace
{

using tests::TTestContext;
using filesystem_image::ERootState;

std::uint32_t MV_STD_ABI_CALL echo(void* data) noexcept
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

bool write(const std::string& file, const void* bytes, std::size_t size)
{
    return platform::filesystem::saveFile(file.c_str(), CByteConstView{ static_cast<const std::uint8_t*>(bytes), size });
}

struct SFixture
{
    platform::system::CPerfCountConversion conversion;
    threading::CThreadPackage client{ { thread_ids::executive, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &echo }, conversion };
    threading::CThreadPackage io{ { thread_ids::bg_file_io, module_ids::executable,
        platform::threading::EThreadPriority::Normal, host::host_worker_thread_entry_point() }, conversion };
    threading::CThreadPackage conditioning{ { thread_ids::bg_conditioning, module_ids::executable,
        platform::threading::EThreadPriority::Normal, host::host_worker_thread_entry_point() }, conversion };
    filesystem_image::CImage image;
    host::CAssetService service;
    std::string directory;
    bool dedicated{ false };
    bool controlled{ false };
    std::size_t document_size{ 0u };

    explicit SFixture(bool echo_io = false) :
        io({ thread_ids::bg_file_io, module_ids::executable, platform::threading::EThreadPriority::Normal,
            echo_io ? &echo : host::host_worker_thread_entry_point() }, conversion), controlled(echo_io) {}

    ~SFixture()
    {
        (void)io.shutdown();
        (void)conditioning.shutdown();
        (void)client.shutdown();
        service.detach_client(client);
        service.deallocate();
    }
    bool initialise(bool separate)
    {
        dedicated = separate;
        directory = std::filesystem::path(test_environment::test_output_path(separate ? "bundle-requests-two" : "bundle-requests-one")).generic_string();
        std::error_code error;
        std::filesystem::create_directories(directory + "/Input/Empty", error);
        if (error) return false;
        constexpr char json[] = "{\"answer\":42}";
        const std::uint8_t tga[]{ 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 32, 0x28, 3, 2, 1, 255 };
        CLiveDocument doc;
        CBakedDocumentBlock block;
        if (!document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(json), sizeof(json) - 1u }, doc).accepted() ||
            !document_translation::bake(doc, block)) return false;
        document_size = block.bytes().size();
        if (!block.resize_buffer(document_size + 11u) || !write(directory + "/Input/Raw", "abc", 3u) ||
            !write(directory + "/Input/Data.json", json, sizeof(json) - 1u) ||
            !write(directory + "/Input/Image.tga", tga, sizeof(tga)) ||
            !write(directory + "/Input/Document.baked", block.buffer_bytes().data(), block.buffer_bytes().size()) ||
            !write(directory + "/invalid.bdl", "bad", 3u)) return false;
        //  The native loader rejects empty files; the builder silently drops them.
        std::FILE* empty = nullptr;
        if (fopen_s(&empty, (directory + "/Input/Zero").c_str(), "wb") != 0) return false;
        std::fclose(empty);
        filesystem_image::SRootScan scan;
        scan.writable = true;
        if (!scan.logical_root.set("work:") || !scan.physical_path.set(directory.c_str()) ||
            (filesystem_image::scan_root(scan, doc) != filesystem_image::EScanStatus::success) || !image.adopt(std::move(doc))) return false;
        service.set_filesystem(image);
        return conversion.init() && service.initialise() && client.startup() && io.startup() &&
            (!dedicated || conditioning.startup()) && service.configure_workers(io, dedicated ? &conditioning : nullptr);
    }
    void pump(threading::CThreadPackage& worker)
    {
        if (controlled && (&worker == &io)) return;
        threading::CErasedPodMsg pod;
        while (worker.read(pod)) service.complete(pod, worker);
        threading::CErasedOwnerMsg owner;
        while (worker.read(owner)) service.complete(owner, worker);
    }
    bool take_work(threading::CErasedPodMsg& message)
    {
        platform::system::CPerfCounter timer;
        (void)timer.update();
        while (timer.query_delta() < conversion.query_ticks_per_second() * 10u)
        {
            if (io.read(message)) return true;
            platform::threading::processor_relax();
        }
        return false;
    }
    template<typename T> void complete(const threading::CErasedPodMsg& work, CErasedOwner&& owner)
    {
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(work.query_async_slot());
        message.set_owner(std::move(owner));
        service.complete(message, io);
    }
    template<typename T> bool receive(T& result, std::int32_t slot)
    {
        platform::system::CPerfCounter timer;
        (void)timer.update();
        while (timer.query_delta() < conversion.query_ticks_per_second() * 10u)
        {
            pump(io);
            if (dedicated) pump(conditioning);
            service.complete_disposals();
            threading::CErasedPodMsg message;
            if (client.read(message)) return (message.query_async_slot() == slot) && message.copy_payload_to(result);
            platform::threading::processor_relax();
        }
        return false;
    }
    template<typename T> void request(std::int32_t slot, CErasedOwner&& owner)
    {
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(slot);
        message.set_owner(std::move(owner));
        service.request(message, client);
    }
    void bake(TTestContext& ctx, std::int32_t slot, const char* root = "pack:", const char* directory_name = "Input")
    {
        auto owner = CErasedOwner::create<BundleBakeRequest>();
        auto* value = owner.payload<BundleBakeRequest>();
        TEST_EXPECT(ctx, value && value->paths.source_root.set("work:") &&
            value->paths.subdirectory.set(directory_name) && value->paths.root.set(root));
        request<BundleBakeRequest>(slot, std::move(owner));
    }
    void load_bundle(TTestContext& ctx, std::int32_t slot, const char* file, const char* root = "pack:")
    {
        auto owner = CErasedOwner::create<BundleLoadRequest>();
        auto* value = owner.payload<BundleLoadRequest>();
        TEST_EXPECT(ctx, value && value->paths.file.set(file) && value->paths.root.set(root));
        request<BundleLoadRequest>(slot, std::move(owner));
    }
    void load(TTestContext& ctx, std::int32_t slot, const char* file, EAssetFileFormat format, std::size_t alignment = 16u)
    {
        auto owner = CErasedOwner::create<AssetLoadRequest>();
        auto* value = owner.payload<AssetLoadRequest>();
        TEST_EXPECT(ctx, value && value->file.set(file));
        value->format = format;
        value->alignment = alignment;
        request<AssetLoadRequest>(slot, std::move(owner));
    }
    AssetResult result(TTestContext& ctx, std::int32_t slot, EAssetStatus status = EAssetStatus::success)
    {
        AssetResult value;
        TEST_EXPECT(ctx, receive(value, slot) && value.status == status);
        if (status != EAssetStatus::success) TEST_EXPECT(ctx, !value.asset && value.kind == EAssetKind::none);
        return value;
    }
    void save(TTestContext& ctx, std::int32_t slot, CAssetId asset, const char* file)
    {
        auto owner = CErasedOwner::create<AssetSaveRequest>();
        auto* value = owner.payload<AssetSaveRequest>();
        TEST_EXPECT(ctx, value && value->file.set(file));
        value->source = asset;
        value->settings.format = EAssetFileFormat::raw;
        request<AssetSaveRequest>(slot, std::move(owner));
    }
    void dispose(TTestContext& ctx, CAssetId asset)
    {
        service.request_disposal({ asset }, 99, client);
        AssetDisposeResult value;
        TEST_EXPECT(ctx, receive(value, 99) && value.asset == asset && value.status == EAssetStatus::success);
    }
    void closed(TTestContext& ctx, CAssetId asset)
    {
        BundleDisposedNotification notice;
        TEST_EXPECT(ctx, receive(notice, -1) && notice.asset == asset);
        TEST_EXPECT(ctx, image.root_state("pack:") == ERootState::missing);
    }
};

void test_public_workflow(TTestContext& ctx, bool dedicated)
{
    SFixture fixture;
    const bool ready = fixture.initialise(dedicated);
    TEST_EXPECT(ctx, ready);
    if (!ready) return;
    fixture.bake(ctx, 1);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::reserved);
    fixture.bake(ctx, 2);
    //  Immediate collision response arrives before the first worker completion.
    fixture.result(ctx, 2, EAssetStatus::busy);
    const auto original = fixture.result(ctx, 1);
    TEST_EXPECT(ctx, original.asset && original.bundle_view().is_ready());
    if (!original.asset) return;
    const auto direct = original.bundle_view().lookup(CStringView{ "raw" });
    TEST_EXPECT(ctx, direct.status == bundle::ELookupStatus::success && direct.bytes.bytes() == 3u);
    TEST_EXPECT(ctx, original.bundle_view().lookup(CStringView{ "zero" }).status == bundle::ELookupStatus::not_found);
    TEST_EXPECT(ctx, original.bundle_view().lookup(CStringView{ "empty" }).status == bundle::ELookupStatus::not_found);
    const auto bytes = original.byte_view();
    std::vector<std::uint8_t> snapshot(bytes.data(), bytes.data() + bytes.size());
    fixture.service.request_reference({ original.asset }, 3, fixture.client);
    const auto reference = fixture.result(ctx, 3);
    TEST_EXPECT(ctx, reference.asset != original.asset && reference.byte_view().data() == bytes.data());
    fixture.service.request_reference({ reference.asset }, 4, fixture.client);
    const auto another = fixture.result(ctx, 4);
    fixture.dispose(ctx, another.asset);
    fixture.dispose(ctx, reference.asset);
    fixture.load(ctx, 5, "PACK:/RAW", EAssetFileFormat::raw);
    const auto raw = fixture.result(ctx, 5);
    TEST_EXPECT(ctx, raw.byte_view().data() == direct.bytes.data());
    fixture.load(ctx, 6, "pack:/raw", EAssetFileFormat::raw, 128u);
    TEST_EXPECT(ctx, fixture.result(ctx, 6).asset == raw.asset);
    fixture.load(ctx, 7, "pack:/raw", EAssetFileFormat::raw, 256u);
    fixture.result(ctx, 7, EAssetStatus::invalid_request);
    fixture.load(ctx, 8, "pack:/document.baked", EAssetFileFormat::baked);
    const auto baked = fixture.result(ctx, 8);
    TEST_EXPECT(ctx, baked.document_view().is_ready() && baked.byte_view().data() ==
        original.bundle_view().lookup(CStringView{ "document.baked" }).bytes.data());
    TEST_EXPECT(ctx, baked.byte_view().size() == fixture.document_size);
    fixture.load(ctx, 81, "work:/Input/Document.baked", EAssetFileFormat::baked);
    const auto ordinary = fixture.result(ctx, 81);
    TEST_EXPECT(ctx, ordinary.byte_view().size() == baked.byte_view().size() &&
        std::memcmp(ordinary.byte_view().data(), baked.byte_view().data(), baked.byte_view().size()) == 0);
    fixture.dispose(ctx, ordinary.asset);
    fixture.load(ctx, 82, "pack:/document.baked", EAssetFileFormat::raw, 24u);
    const auto full_file = fixture.result(ctx, 82);
    TEST_EXPECT(ctx, full_file.byte_view().size() == fixture.document_size + 11u && full_file.byte_view().align() == 128u);
    fixture.dispose(ctx, full_file.asset);
    fixture.load(ctx, 9, "pack:/image.tga", EAssetFileFormat::tga);
    const auto image = fixture.result(ctx, 9);
    TEST_EXPECT(ctx, image.kind == EAssetKind::image && image.image_view().is_ready());
    fixture.save(ctx, 10, original.asset, "work:/saved.bdl");
    fixture.result(ctx, 10);
    auto saved = platform::filesystem::loadFile((fixture.directory + "/saved.bdl").c_str());
    TEST_EXPECT(ctx, saved.size() == snapshot.size() && std::memcmp(saved.data(), snapshot.data(), snapshot.size()) == 0);
    fixture.dispose(ctx, baked.asset);
    fixture.dispose(ctx, raw.asset);

    //  Two misses are admitted before any reply is integrated. Completion after
    //  closure must retain two independent results and leave the cache closed.
    fixture.load(ctx, 11, "pack:/data.json", EAssetFileFormat::json);
    fixture.load(ctx, 12, "pack:/data.json", EAssetFileFormat::json);
    fixture.service.request_disposal({ original.asset }, 13, fixture.client);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::closed && !fixture.service.is_idle());
    const auto first = fixture.result(ctx, 11);
    const auto second = fixture.result(ctx, 12);
    TEST_EXPECT(ctx, first.asset && second.asset && first.asset != second.asset && first.document_view().is_ready());
    fixture.closed(ctx, original.asset);
    TEST_EXPECT(ctx, first.document_view().check_integrity() && image.image_view().is_ready());
    fixture.dispose(ctx, first.asset);
    fixture.dispose(ctx, second.asset);
    fixture.dispose(ctx, image.asset);

    fixture.load_bundle(ctx, 14, "work:/saved.bdl");
    const auto loaded = fixture.result(ctx, 14);
    TEST_EXPECT(ctx, loaded.asset.query_value() > original.asset.query_value());
    TEST_EXPECT(ctx, loaded.byte_view().size() == snapshot.size() &&
        std::memcmp(loaded.byte_view().data(), snapshot.data(), snapshot.size()) == 0);
    fixture.load(ctx, 15, "pack:/data.json", EAssetFileFormat::json);
    fixture.load(ctx, 16, "pack:/data.json", EAssetFileFormat::json);
    const auto old_cached = fixture.result(ctx, 15);
    const auto cached = fixture.result(ctx, 16);
    TEST_EXPECT(ctx, old_cached.asset != cached.asset);
    fixture.dispose(ctx, old_cached.asset);
    fixture.load(ctx, 17, "pack:/data.json", EAssetFileFormat::json);
    TEST_EXPECT(ctx, fixture.result(ctx, 17).asset == cached.asset);
    fixture.dispose(ctx, cached.asset);
    fixture.load(ctx, 18, "pack:/raw", EAssetFileFormat::raw);
    const auto survivor = fixture.result(ctx, 18);
    fixture.service.request_disposal({ loaded.asset }, 19, fixture.client);
    fixture.load(ctx, 20, "pack:/raw", EAssetFileFormat::raw);
    fixture.result(ctx, 20, EAssetStatus::read_failed);
    fixture.save(ctx, 21, survivor.asset, "work:/raw-copy");
    fixture.result(ctx, 21);
    auto copy = platform::filesystem::loadFile((fixture.directory + "/raw-copy").c_str());
    TEST_EXPECT(ctx, copy.size() == 3u && std::memcmp(copy.data(), "abc", 3u) == 0);
    fixture.dispose(ctx, survivor.asset);
    fixture.closed(ctx, loaded.asset);
    TEST_EXPECT(ctx, fixture.service.is_idle() && !fixture.service.failed());
}

void test_failed_work(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise(false));
    fixture.load_bundle(ctx, 1, "work:/missing.bdl");
    fixture.result(ctx, 1, EAssetStatus::read_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing);
    fixture.load_bundle(ctx, 2, "work:/invalid.bdl");
    fixture.result(ctx, 2, EAssetStatus::conditioning_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing);
    fixture.bake(ctx, 3, "pack:", "Input/Empty");
    fixture.result(ctx, 3, EAssetStatus::invalid_request);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing);
    fixture.bake(ctx, 4, "pack:", "../Input");
    fixture.result(ctx, 4, EAssetStatus::invalid_request);
    fixture.bake(ctx, 5, "bad/root:");
    fixture.result(ctx, 5, EAssetStatus::invalid_request);
    //  The inventory remains stable but a discovered physical file disappears.
    std::error_code error;
    TEST_EXPECT(ctx, std::filesystem::remove(fixture.directory + "/Input/Raw", error) && !error);
    fixture.bake(ctx, 6);
    fixture.result(ctx, 6, EAssetStatus::read_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing && !fixture.service.failed());
}

void test_controlled_failures(TTestContext& ctx)
{
    SFixture fixture(true);
    const bool ready = fixture.initialise(false);
    TEST_EXPECT(ctx, ready);
    if (!ready) return;
    threading::CErasedPodMsg work;
    fixture.bake(ctx, 1);
    TEST_EXPECT(ctx, fixture.take_work(work));
    BundleBuildWork captured;
    TEST_EXPECT(ctx, work.copy_payload_to(captured) && captured.input && captured.input->sources().size() != 0u);
    //  Allocation failure of the worker's owning result still completes the request.
    fixture.complete<BundleBuildResult>(work, CErasedOwner{});
    fixture.result(ctx, 1, EAssetStatus::allocation_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing);

    fixture.bake(ctx, 2);
    TEST_EXPECT(ctx, fixture.take_work(work));
    auto failed = CErasedOwner::create<BundleBuildResult>();
    auto* failure = failed.payload<BundleBuildResult>();
    TEST_EXPECT(ctx, failure != nullptr);
    failure->result.status = bundle::EBuildStatus::read_failed;
    fixture.complete<BundleBuildResult>(work, std::move(failed));
    fixture.result(ctx, 2, EAssetStatus::read_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing);

    //  A valid baked document can pass adoption but fail bundle publication.
    fixture.load_bundle(ctx, 3, "work:/Input/Document.baked");
    TEST_EXPECT(ctx, fixture.take_work(work));
    auto loaded = CErasedOwner::create<LoadedFile>();
    auto* file = loaded.payload<LoadedFile>();
    TEST_EXPECT(ctx, file != nullptr);
    file->buffer = platform::filesystem::loadFile((fixture.directory + "/Input/Document.baked").c_str(), 0u, 128u);
    CBakedDocument prefix;
    TEST_EXPECT(ctx, file->buffer.is_ready() && file->buffer.align() == 128u &&
        prefix.reset(file->buffer.data(), fixture.document_size));
    fixture.complete<FileLoadResult>(work, std::move(loaded));
    fixture.result(ctx, 3, EAssetStatus::conditioning_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing);

    fixture.bake(ctx, 4);
    TEST_EXPECT(ctx, fixture.take_work(work));
    TEST_EXPECT(ctx, fixture.io.shutdown());
    fixture.service.fail_pending();
    fixture.result(ctx, 4, EAssetStatus::delivery_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing && fixture.service.is_idle());
    threading::CThreadPackage unavailable{ { thread_ids::bg_file_io, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &echo }, fixture.conversion };
    TEST_EXPECT(ctx, fixture.service.configure_workers(unavailable, nullptr));
    fixture.bake(ctx, 5);
    fixture.result(ctx, 5, EAssetStatus::delivery_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing);

    SFixture teardown(true);
    TEST_EXPECT(ctx, teardown.initialise(false));
    teardown.bake(ctx, 6);
    TEST_EXPECT(ctx, teardown.take_work(work));
    TEST_EXPECT(ctx, teardown.io.shutdown() && teardown.client.shutdown());
    teardown.service.detach_client(teardown.client);
    teardown.service.deallocate();
    TEST_EXPECT(ctx, teardown.image.root_state("pack:") == ERootState::missing);
}

}   // namespace

int run_bundle_request_tests()
{
    TTestContext ctx;
    test_public_workflow(ctx, false);
    test_public_workflow(ctx, true);
    test_failed_work(ctx);
    test_controlled_failures(ctx);
    std::cout << "BundleRequests: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.exit_code();
}
