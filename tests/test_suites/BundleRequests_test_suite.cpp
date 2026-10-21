
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BundleRequests_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    12 Oct 26
//
//  Real worker requests, deterministic admission interleavings and persistence.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "host/runtime/asset_service.hpp"
#include "host/runtime/host_worker_thread.hpp"
#include "platform/filesystem/file.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "schema/baked_instances_block.hpp"
#include "schema/baked_bulk_data_block.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/BundleRequests_test_suite.hpp"

namespace
{

using tests::TTestContext;
using filesystem_image::ERootState;

bool write(const std::string& file, const void* const bytes, const std::size_t size)
{
    return platform::filesystem::saveFile(file.c_str(), CByteConstView{ static_cast<const std::uint8_t*>(bytes), size });
}

struct SFixture
{
    platform::system::CPerfCountConversion conversion;
    threading::CThreadPackage client{ { thread_ids::executive, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &tests::echo_pod_messages }, conversion };
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

    explicit SFixture(const bool echo_io = false) :
        io({ thread_ids::bg_file_io, module_ids::executable, platform::threading::EThreadPriority::Normal,
            echo_io ? &tests::echo_pod_messages : host::host_worker_thread_entry_point() }, conversion), controlled(echo_io) {}

    ~SFixture()
    {
        (void)io.shutdown();
        (void)conditioning.shutdown();
        (void)client.shutdown();
        service.detach_client(client);
        service.deallocate();
    }
    bool initialise(const bool separate, const bool attach_image = true)
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
        if (!block.resize_buffer(document_size + 11u)) return false;
        std::memset(block.buffer_bytes().data() + document_size, 0xa5, 11u);
        if (!write(directory + "/Input/Raw", "abc", 3u) ||
            !write(directory + "/Input/Data.json", json, sizeof(json) - 1u) ||
            !write(directory + "/Input/Image.tga", tga, sizeof(tga)) ||
            !write(directory + "/Input/Document.baked", block.buffer_bytes().data(), block.buffer_bytes().size()) ||
            !write(directory + "/invalid.bdl", "bad", 3u)) return false;
        //  The native loader rejects empty files; the builder silently drops them.
        auto* empty = platform::filesystem::openFile(platform::path::makeNativePath((directory + "/Input/Zero").c_str()),
            platform::filesystem::EOpenMode::BinaryWrite);
        if (!empty) return false;
        std::fclose(empty);
        filesystem_image::SRootScan scan;
        scan.writable = true;
        if (!scan.logical_root.set("work:") || !scan.physical_path.set(directory.c_str()) ||
            (filesystem_image::scan_root(scan, doc) != filesystem_image::EScanStatus::success) || !image.adopt(std::move(doc))) return false;
        if (attach_image) service.set_filesystem(image);
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
        return tests::wait_until(conversion, [this, &message]() noexcept
        {
            return io.read(message);
        }, 10u);
    }
    template<typename T> void complete(const threading::CErasedPodMsg& work, CErasedOwner&& owner)
    {
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(work.query_async_slot());
        message.set_owner(std::move(owner));
        service.complete(message, io);
    }
    template<typename T> bool receive(T& result, const std::int32_t slot)
    {
        threading::CErasedPodMsg message;
        const bool received = tests::wait_until(conversion, [this, &message]()
        {
            pump(io);
            if (dedicated) pump(conditioning);
            service.complete_disposals();
            return client.read(message);
        }, 10u);
        return received && (message.query_async_slot() == slot) && message.copy_payload_to(result);
    }
    template<typename T> void request(const std::int32_t slot, CErasedOwner&& owner)
    {
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(slot);
        message.set_owner(std::move(owner));
        service.request(message, client);
    }
    void bake(TTestContext& ctx, const std::int32_t slot, const char* const root = "pack:", const char* const directory_name = "Input")
    {
        auto owner = CErasedOwner::create<BundleBakeRequest>();
        auto* value = owner.payload<BundleBakeRequest>();
        TEST_EXPECT(ctx, value && value->paths.source_root.set("work:") &&
            value->paths.subdirectory.set(directory_name) && value->paths.root.set(root));
        request<BundleBakeRequest>(slot, std::move(owner));
    }
    void load_bundle(TTestContext& ctx, const std::int32_t slot, const char* const file, const char* const root = "pack:")
    {
        auto owner = CErasedOwner::create<BundleLoadRequest>();
        auto* value = owner.payload<BundleLoadRequest>();
        TEST_EXPECT(ctx, value && value->paths.file.set(file) && value->paths.root.set(root));
        request<BundleLoadRequest>(slot, std::move(owner));
    }
    void load(TTestContext& ctx, const std::int32_t slot, const char* const file, const EAssetFileFormat format, const std::size_t alignment = 16u)
    {
        auto owner = CErasedOwner::create<AssetLoadRequest>();
        auto* value = owner.payload<AssetLoadRequest>();
        TEST_EXPECT(ctx, value && value->file.set(file));
        value->format = format;
        value->alignment = alignment;
        request<AssetLoadRequest>(slot, std::move(owner));
    }
    AssetResult result(TTestContext& ctx, const std::int32_t slot, const EAssetStatus status = EAssetStatus::success)
    {
        AssetResult value;
        TEST_EXPECT(ctx, receive(value, slot) && value.status == status);
        if (status != EAssetStatus::success) TEST_EXPECT(ctx, !value.asset && value.kind == EAssetKind::none);
        return value;
    }
    void save(TTestContext& ctx, const std::int32_t slot, const CAssetId asset, const char* const file,
        const EAssetFileFormat format = EAssetFileFormat::raw)
    {
        auto owner = CErasedOwner::create<AssetSaveRequest>();
        auto* value = owner.payload<AssetSaveRequest>();
        TEST_EXPECT(ctx, value && value->file.set(file));
        value->source = asset;
        value->settings.format = format;
        request<AssetSaveRequest>(slot, std::move(owner));
    }
    void dispose(TTestContext& ctx, const CAssetId asset)
    {
        service.request_disposal({ asset }, 99, client);
        AssetDisposeResult value;
        TEST_EXPECT(ctx, receive(value, 99) && value.asset == asset && value.status == EAssetStatus::success);
    }
    void closed(TTestContext& ctx, const CAssetId asset)
    {
        BundleDisposedNotification notice;
        TEST_EXPECT(ctx, receive(notice, -1) && notice.asset == asset);
        TEST_EXPECT(ctx, image.root_state("pack:") == ERootState::missing);
    }
};

void test_native_loads(TTestContext& ctx, const bool dedicated, const bool attach_image)
{
    SFixture fixture;
    const bool ready = fixture.initialise(dedicated, attach_image);
    TEST_EXPECT(ctx, ready);
    if (!ready) return;

    //  Sibling of the only configured root: none of these files is inventoried.
    const std::string external = fixture.directory + "-native";
    std::error_code error;
    std::filesystem::create_directories(external, error);
    TEST_EXPECT(ctx, !error);
    for (const char* name : { "Raw", "Data.json", "Image.tga", "Document.baked" })
    {
        TEST_EXPECT(ctx, std::filesystem::copy_file(fixture.directory + "/Input/" + name, external + "/" + name,
            std::filesystem::copy_options::overwrite_existing, error) && !error);
    }
    const std::string physical = external + "/Mixed Case \xc3\xa9.bin";
    TEST_EXPECT(ctx, write(physical, "abc", 3u));
    std::string native = "NaTiVe:" + physical;
#if MV_PLATFORM_WINDOWS
    //  A file-picker spelling is accepted without applying logical-path rules.
    for (char& byte : native) if (byte == '/') byte = '\\';
#endif
    CAssetId indexed;
    if (attach_image)
    {
        fixture.load(ctx, 1, "work:/Input/Raw", EAssetFileFormat::raw);
        indexed = fixture.result(ctx, 1).asset;
        fixture.bake(ctx, 13, "NaTiVe:");
        fixture.result(ctx, 13, EAssetStatus::invalid_request);
    }
    CBakedDocumentBlock before;
    TEST_EXPECT(ctx, document_translation::bake(fixture.image.document(), before));
    const auto serial = fixture.image.write_serial();

    fixture.load(ctx, 2, native.c_str(), EAssetFileFormat::raw, 128u);
    const auto first = fixture.result(ctx, 2);
    TEST_EXPECT(ctx, first.kind == EAssetKind::raw && first.byte_view().size() == 3u &&
        first.byte_view().align() >= 128u && std::memcmp(first.byte_view().data(), "abc", 3u) == 0);
    TEST_EXPECT(ctx, write(physical, "xyz", 3u));
    fixture.load(ctx, 3, native.c_str(), EAssetFileFormat::raw);
    const auto second = fixture.result(ctx, 3);
    TEST_EXPECT(ctx, second.asset != first.asset && second.byte_view().size() == 3u &&
        std::memcmp(second.byte_view().data(), "xyz", 3u) == 0);
    TEST_EXPECT(ctx, first.byte_view().size() == 3u && std::memcmp(first.byte_view().data(), "abc", 3u) == 0);
    fixture.dispose(ctx, first.asset);
    fixture.dispose(ctx, second.asset);

    //  Even a physical file with an existing logical cache is loaded independently.
    const std::string indexed_native = "native:" + fixture.directory + "/Input/Raw";
    fixture.load(ctx, 4, indexed_native.c_str(), EAssetFileFormat::raw);
    const auto independent = fixture.result(ctx, 4);
    TEST_EXPECT(ctx, independent.asset && independent.asset != indexed);
    fixture.dispose(ctx, independent.asset);
    if (attach_image)
    {
        fixture.load(ctx, 5, "work:/Input/Raw", EAssetFileFormat::raw);
        TEST_EXPECT(ctx, fixture.result(ctx, 5).asset == indexed);
    }

    for (const auto format : { EAssetFileFormat::json, EAssetFileFormat::baked, EAssetFileFormat::tga })
    {
        const char* name = (format == EAssetFileFormat::json) ? "Data.json" :
            ((format == EAssetFileFormat::baked) ? "Document.baked" : "Image.tga");
        const std::string file = "native:" + external + "/" + name;
        fixture.load(ctx, 6, file.c_str(), format, 128u);
        const auto loaded = fixture.result(ctx, 6);
        if (format == EAssetFileFormat::tga)
        {
            TEST_EXPECT(ctx, loaded.kind == EAssetKind::image && loaded.image_view().is_ready() &&
                loaded.image_view().width() == 1 && loaded.image_view().height() == 1);
        }
        else
        {
            const auto doc = loaded.document_view();
            std::uint64_t answer = 0u;
            TEST_EXPECT(ctx, loaded.kind == EAssetKind::baked && doc.check_integrity() &&
                doc.unsigned_integer_value(doc.object_child(doc.root(), CStringView{ "answer" }), answer) && answer == 42u);
            if (format == EAssetFileFormat::baked)
                TEST_EXPECT(ctx, loaded.byte_view().align() >= 128u && loaded.byte_view().size() == fixture.document_size + 11u);
            else TEST_EXPECT(ctx, loaded.document_policy == EDocumentPolicyStatus::accepted);
        }
        fixture.dispose(ctx, loaded.asset);
    }

    //  JSON policy is still evaluated by the normal conditioning worker.
    const std::string policy_file = external + "/Policy.json";
    constexpr char policy_json[] = "{\"answer\":+42}";
    TEST_EXPECT(ctx, write(policy_file, policy_json, sizeof(policy_json) - 1u));
    const std::string policy_native = "native:" + policy_file;
    auto owner = CErasedOwner::create<AssetLoadRequest>();
    auto* request = owner.payload<AssetLoadRequest>();
    TEST_EXPECT(ctx, request && request->file.set(policy_native.c_str()));
    if (request == nullptr) return;
    request->format = EAssetFileFormat::json;
    request->policy.allowed_features = document_policy::k_ascii;
    fixture.request<AssetLoadRequest>(7, std::move(owner));
    const auto rejected = fixture.result(ctx, 7, EAssetStatus::policy_rejected);
    TEST_EXPECT(ctx, rejected.document_policy == EDocumentPolicyStatus::rejected);
    fixture.load(ctx, 8, policy_native.c_str(), EAssetFileFormat::json);
    const auto accepted = fixture.result(ctx, 8);
    TEST_EXPECT(ctx, accepted.document_policy == EDocumentPolicyStatus::accepted);
    fixture.dispose(ctx, accepted.asset);

    for (const char* invalid : { "native:", "native:relative.bin", "native:C:relative.bin" })
    {
        fixture.load(ctx, 9, invalid, EAssetFileFormat::raw);
        fixture.result(ctx, 9, EAssetStatus::invalid_request);
    }
#if MV_PLATFORM_WINDOWS
    for (const char* invalid : { "native:/root-relative.bin", "native:\\root-relative.bin",
        "native:\\\\server\\share\\file.bin", "native:\\\\?\\C:\\file.bin" })
    {
        fixture.load(ctx, 9, invalid, EAssetFileFormat::raw);
        fixture.result(ctx, 9, EAssetStatus::invalid_request);
    }
#endif
    const std::string missing = "native:" + external + "/missing.bin";
    fixture.load(ctx, 10, missing.c_str(), EAssetFileFormat::raw);
    fixture.result(ctx, 10, EAssetStatus::read_failed);
    fixture.load(ctx, 11, physical.c_str(), EAssetFileFormat::raw);
    fixture.result(ctx, 11, EAssetStatus::read_failed);
    fixture.load(ctx, 12, "work:/missing.bin", EAssetFileFormat::raw);
    fixture.result(ctx, 12, EAssetStatus::read_failed);

    CBakedDocumentBlock after;
    TEST_EXPECT(ctx, document_translation::bake(fixture.image.document(), after));
    TEST_EXPECT(ctx, before.bytes().size() == after.bytes().size() &&
        std::memcmp(before.bytes().data(), after.bytes().data(), before.bytes().size()) == 0);
    TEST_EXPECT(ctx, fixture.image.write_serial() == serial && fixture.image.root_state("native:") == ERootState::missing);
    if (indexed) fixture.dispose(ctx, indexed);
    TEST_EXPECT(ctx, fixture.service.is_idle() && !fixture.service.failed());
}

void test_public_workflow(TTestContext& ctx, const bool dedicated)
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
    TEST_EXPECT(ctx, baked.byte_view().size() == fixture.document_size + 11u && baked.document_view().byte_count() == fixture.document_size);
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

void test_complete_baked_buffers(TTestContext& ctx, const bool dedicated)
{
    SFixture fixture;
    if (!fixture.initialise(dedicated)) { TEST_EXPECT(ctx, false); return; }
    const auto physical = platform::filesystem::loadFile((fixture.directory + "/Input/Document.baked").c_str(), 0u, 128u);
    const auto matches = [&](const CByteConstView bytes)
    {
        return bytes.size() == physical.size() && std::memcmp(bytes.data(), physical.data(), physical.size()) == 0;
    };
    const auto check = [&](const AssetResult& result)
    {
        TEST_EXPECT(ctx, result.kind == EAssetKind::baked && result.document_view().check_integrity());
        TEST_EXPECT(ctx, result.document_view().byte_count() == fixture.document_size && matches(result.byte_view()));
    };
    fixture.load(ctx, 1, "work:/Input/Document.baked", EAssetFileFormat::baked);
    const auto loose = fixture.result(ctx, 1);
    check(loose);
    fixture.load(ctx, 2, "work:/Input/Document.baked", EAssetFileFormat::baked, 128u);
    const auto aligned = fixture.result(ctx, 2);
    check(aligned);
    TEST_EXPECT(ctx, aligned.byte_view().align() >= 128u && aligned.asset != loose.asset);
    fixture.load(ctx, 3, "work:/Input/Document.baked", EAssetFileFormat::baked, 128u);
    TEST_EXPECT(ctx, fixture.result(ctx, 3).asset == aligned.asset);

    //  A stale/mismatched cache association cannot turn a raw view into a baked result.
    fixture.load(ctx, 4, "work:/Input/Document.baked", EAssetFileFormat::raw, 128u);
    const auto raw = fixture.result(ctx, 4);
    TEST_EXPECT(ctx, fixture.image.loaded("work:/Input/Document.baked", raw.asset.query_value(),
        static_cast<std::uint64_t>(EAssetFileFormat::baked), 0u, fixture.image.write_serial()));
    fixture.load(ctx, 5, "work:/Input/Document.baked", EAssetFileFormat::baked, 128u);
    const auto corrected = fixture.result(ctx, 5);
    check(corrected);
    TEST_EXPECT(ctx, corrected.asset != raw.asset);

    auto transfer = CErasedOwner::create<BakedAssetTransfer>();
    auto* request = transfer.payload<BakedAssetTransfer>();
    TEST_EXPECT(ctx, request != nullptr);
    if (!request) return;
    CByteBuffer storage;
    TEST_EXPECT(ctx, storage.construct_and_copy_from(physical.const_view()) && storage.reserve(physical.size() + 256u));
    TEST_EXPECT(ctx, request->storage.value.adopt_buffer(std::move(storage)));
    const void* transferred = request->storage.value.buffer_bytes().data();
    TEST_EXPECT(ctx, request->storage.value.buffer_capacity() > physical.size());
    fixture.request<BakedAssetTransfer>(6, std::move(transfer));
    const auto admitted = fixture.result(ctx, 6);
    check(admitted);
    TEST_EXPECT(ctx, admitted.byte_view().data() == transferred);
    fixture.save(ctx, 7, admitted.asset, "work:/complete.baked", EAssetFileFormat::baked);
    TEST_EXPECT(ctx, fixture.result(ctx, 7).asset == admitted.asset);
    auto saved = platform::filesystem::loadFile((fixture.directory + "/complete.baked").c_str());
    TEST_EXPECT(ctx, matches(saved.const_view()));
    fixture.load(ctx, 8, "work:/complete.baked", EAssetFileFormat::baked, 128u);
    TEST_EXPECT(ctx, fixture.result(ctx, 8).asset == admitted.asset);
    //  A stronger alignment forces a physical reload and must retain the same extent.
    fixture.load(ctx, 9, "work:/complete.baked", EAssetFileFormat::baked, 256u);
    const auto reloaded = fixture.result(ctx, 9);
    check(reloaded);
    TEST_EXPECT(ctx, reloaded.asset != admitted.asset && reloaded.byte_view().align() >= 256u);

    fixture.save(ctx, 10, admitted.asset, "work:/document.json", EAssetFileFormat::json);
    TEST_EXPECT(ctx, fixture.result(ctx, 10).asset == admitted.asset && matches(admitted.byte_view()));
    TEST_EXPECT(ctx, fixture.image.cached_asset("work:/document.json", static_cast<std::uint64_t>(EAssetFileFormat::json)) == 0u);
    const auto json = platform::filesystem::loadFile((fixture.directory + "/document.json").c_str());
    CLiveDocument parsed;
    TEST_EXPECT(ctx, document_parser::parse(json.const_view(), parsed).accepted());
    std::uint64_t answer{};
    TEST_EXPECT(ctx, parsed.unsigned_integer_value(parsed.object_child(parsed.root(), CStringView{ "answer" }), answer) && answer == 42u);
    fixture.load(ctx, 11, "work:/document.json", EAssetFileFormat::json);
    const auto json_loaded = fixture.result(ctx, 11);
    TEST_EXPECT(ctx, json_loaded.asset != admitted.asset && json_loaded.byte_view().size() == json_loaded.document_view().byte_count());
    for (const auto id : { loose.asset, aligned.asset, raw.asset, corrected.asset, admitted.asset, reloaded.asset, json_loaded.asset }) fixture.dispose(ctx, id);

    fixture.bake(ctx, 12);
    const auto bundle = fixture.result(ctx, 12);
    fixture.load(ctx, 13, "pack:/document.baked", EAssetFileFormat::baked, 128u);
    const auto borrowed = fixture.result(ctx, 13);
    check(borrowed);
    const auto entry = bundle.bundle_view().lookup(CStringView{ "document.baked" });
    TEST_EXPECT(ctx, borrowed.byte_view().data() == entry.bytes.data() && borrowed.byte_view().size() == entry.bytes.count());
    fixture.load(ctx, 14, "pack:/document.baked", EAssetFileFormat::baked, 128u);
    TEST_EXPECT(ctx, fixture.result(ctx, 14).asset == borrowed.asset);
    fixture.save(ctx, 15, borrowed.asset, "pack:/document.baked", EAssetFileFormat::baked);
    AssetResult rejected;
    TEST_EXPECT(ctx, fixture.receive(rejected, 15) && rejected.status == EAssetStatus::write_failed && rejected.asset == borrowed.asset);
    TEST_EXPECT(ctx, matches(borrowed.byte_view()));
    fixture.service.request_disposal({ bundle.asset }, 16, fixture.client);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::closed);
    fixture.save(ctx, 17, borrowed.asset, "work:/retained.baked", EAssetFileFormat::baked);
    TEST_EXPECT(ctx, fixture.result(ctx, 17).asset == borrowed.asset);
    saved = platform::filesystem::loadFile((fixture.directory + "/retained.baked").c_str());
    TEST_EXPECT(ctx, matches(saved.const_view()));
    fixture.dispose(ctx, borrowed.asset);
    fixture.closed(ctx, bundle.asset);
    fixture.load(ctx, 18, "work:/retained.baked", EAssetFileFormat::baked, 128u);
    const auto independent = fixture.result(ctx, 18);
    check(independent);
    fixture.dispose(ctx, independent.asset);
    TEST_EXPECT(ctx, fixture.service.is_idle() && !fixture.service.failed());
}

bool same_bytes(const CByteConstView bytes, const std::vector<std::uint8_t>& expected)
{
    return bytes.size() == expected.size() && std::memcmp(bytes.data(), expected.data(), expected.size()) == 0;
}

template<bool Instances>
void test_schema_persistence(TTestContext& ctx, SFixture& fixture, schema::CBakedSchema& schema)
{
    using Block = std::conditional_t<Instances, schema::CBakedInstancesBlock, schema::CBakedBulkDataBlock>;
    using Live = std::conditional_t<Instances, schema::CLiveInstances, schema::CLiveBulkData>;
    using Diagnostic = std::conditional_t<Instances, schema::SInstancesBlockDiagnostic, schema::SBulkDataBlockDiagnostic>;
    using RoleDiagnostic = std::conditional_t<Instances, schema::SInstanceDiagnostic, schema::SBulkDataDiagnostic>;
    using Role = std::conditional_t<Instances, schema::CBakedInstances, schema::CBakedBulkData>;
    const char* file = Instances ? "work:/Input/instances.bid" : "work:/Input/bulk-data.bbd";
    const char* entry_file = Instances ? "pack:/instances.bid" : "pack:/bulk-data.bbd";
    const char* entry_name = Instances ? "instances.bid" : "bulk-data.bbd";
    const char* copy_file = Instances ? "work:/copy.bid" : "work:/copy.bbd";
    const char* copy_name = Instances ? "/copy.bid" : "/copy.bbd";
    const std::uint8_t values[]{ 7u, 9u, 11u, 13u };
    const auto check_role = [&](const Block& block)
    {
        TEST_EXPECT(ctx, block.loaded_ready());
        Role role;
        RoleDiagnostic error;
        TEST_EXPECT(ctx, role.set_document(block.document()) && role.attach_schema(schema, block.payload_view(), true, error));
        if constexpr (Instances)
        {
            schema::SInstanceEntryView entry;
            TEST_EXPECT(ctx, role.entry(role.find_base(CStringView{ "Pair" }, CStringView{ "sample" }), entry));
            TEST_EXPECT(ctx, entry.byte_count == 2u && std::memcmp(entry.bytes, values, 2u) == 0);
        }
        else
        {
            schema::SBulkDataEntryView entry;
            TEST_EXPECT(ctx, role.entry(role.find_entry(CStringView{ "Pair" }, CStringView{ "sample" }), entry));
            TEST_EXPECT(ctx, entry.count == 2u && entry.byte_count == 4u && std::memcmp(entry.bytes, values, 4u) == 0);
        }
    };
    Live live;
    RoleDiagnostic role_diagnostic;
    Diagnostic diagnostic;
    Block local;
    TEST_EXPECT(ctx, live.initialise(schema));
    if constexpr (Instances)
        TEST_EXPECT(ctx, live.capture_base(CStringView{ "Pair" }, CStringView{ "sample" }, CByteConstView{ values, 2u }, role_diagnostic));
    else
        TEST_EXPECT(ctx, live.capture(CStringView{ "Pair" }, CStringView{ "sample" }, CByteConstView{ values, 4u }, 2u, role_diagnostic));
    TEST_EXPECT(ctx, live.bake(local, schema, schema::EDataOutputForm::embedded, diagnostic));
    check_role(local);
    const auto bytes = local.file_bytes();
    const std::vector<std::uint8_t> snapshot(bytes.data(), bytes.data() + bytes.size());
    auto owner = CErasedOwner::create<BakedAssetTransfer>();
    auto* transfer = owner.payload<BakedAssetTransfer>();
    TEST_EXPECT(ctx, transfer && local.release_local(transfer->storage.value));
    live.clear();
    TEST_EXPECT(ctx, schema.reference_count() == 0u);
    fixture.request<BakedAssetTransfer>(1, std::move(owner));
    const auto admitted = fixture.result(ctx, 1);
    Block attached;
    TEST_EXPECT(ctx, attached.attach_asset(admitted.asset, admitted.byte_view(), diagnostic));
    TEST_EXPECT(ctx, same_bytes(attached.file_bytes(), snapshot) && attached.file_bytes().data() == bytes.data());
    check_role(attached);
    fixture.save(ctx, 2, admitted.asset, file, EAssetFileFormat::baked);
    TEST_EXPECT(ctx, fixture.result(ctx, 2).asset == admitted.asset);
    attached.clear();
    fixture.dispose(ctx, admitted.asset);
    fixture.load(ctx, 3, file, EAssetFileFormat::baked, 128u);
    const auto loaded = fixture.result(ctx, 3);
    TEST_EXPECT(ctx, same_bytes(loaded.byte_view(), snapshot));

    //  Successful I/O does not imply successful caller-side schema attachment.
    schema::CLiveSchema missing;
    CLiveDocument empty;
    schema::SDiagnostic schema_diagnostic;
    constexpr char empty_types[] = R"({"types":{}})";
    TEST_EXPECT(ctx, document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(empty_types), sizeof(empty_types) - 1u }, empty).accepted() &&
        missing.try_adopt(std::move(empty)) && missing.resolve(schema_diagnostic));
    TEST_EXPECT(ctx, attached.attach_asset(loaded.asset, loaded.byte_view(), diagnostic));
    Role rejected;
    TEST_EXPECT(ctx, rejected.set_document(attached.document()) &&
        !rejected.attach_schema(missing, attached.payload_view(), true, role_diagnostic));
    TEST_EXPECT(ctx, attached.loaded_ready() && same_bytes(loaded.byte_view(), snapshot) && missing.reference_count() == 0u);
    check_role(attached);

    //  Parsing and materialisation are separate operations with independent owners.
    fixture.save(ctx, 4, loaded.asset, "work:/description.json", EAssetFileFormat::json);
    TEST_EXPECT(ctx, fixture.result(ctx, 4).asset == loaded.asset);
    fixture.load(ctx, 5, "work:/description.json", EAssetFileFormat::json);
    const auto parsed = fixture.result(ctx, 5);
    Block materialised;
    TEST_EXPECT(ctx, materialised.materialise(parsed.document_view(), schema, schema::EDataOutputForm::stripped, diagnostic));
    fixture.dispose(ctx, parsed.asset);
    check_role(materialised);
    const auto materialised_bytes = materialised.file_bytes();
    const std::vector<std::uint8_t> stripped(materialised_bytes.data(), materialised_bytes.data() + materialised_bytes.size());
    owner = CErasedOwner::create<BakedAssetTransfer>();
    transfer = owner.payload<BakedAssetTransfer>();
    TEST_EXPECT(ctx, transfer && materialised.release_local(transfer->storage.value) && transfer->storage.file.set(copy_file));
    transfer->save = true;
    transfer->retention = EAssetRetention::discard;
    fixture.request<BakedAssetTransfer>(6, std::move(owner));
    const auto discarded = fixture.result(ctx, 6);
    TEST_EXPECT(ctx, !discarded.asset && discarded.kind == EAssetKind::none);
    const auto saved = platform::filesystem::loadFile((fixture.directory + copy_name).c_str());
    TEST_EXPECT(ctx, same_bytes(saved.const_view(), stripped));
    fixture.load(ctx, 7, copy_file, EAssetFileFormat::baked, 128u);
    const auto stripped_load = fixture.result(ctx, 7);
    TEST_EXPECT(ctx, materialised.attach_asset(stripped_load.asset, stripped_load.byte_view(), diagnostic));
    check_role(materialised);
    materialised.clear();
    fixture.dispose(ctx, stripped_load.asset);

    fixture.bake(ctx, 8);
    const auto bundle = fixture.result(ctx, 8);
    fixture.load(ctx, 9, entry_file, EAssetFileFormat::baked, 128u);
    const auto borrowed = fixture.result(ctx, 9);
    Block bundled;
    TEST_EXPECT(ctx, bundled.attach_asset(borrowed.asset, borrowed.byte_view(), diagnostic));
    const auto entry = bundle.bundle_view().lookup(CStringView{ entry_name });
    TEST_EXPECT(ctx, same_bytes(bundled.file_bytes(), snapshot) && bundled.file_bytes().data() == entry.bytes.data() &&
        bundled.file_bytes().size() == entry.bytes.count());
    check_role(bundled);
    CLiveDocument promoted;
    TEST_EXPECT(ctx, document_translation::promote(borrowed.document_view(), promoted) &&
        promoted.append_child(promoted.root(), promoted.create_unsigned_integer(1u, CStringView{ "independent" })).succeeded());
    TEST_EXPECT(ctx, same_bytes(bundled.file_bytes(), snapshot));
    fixture.save(ctx, 10, borrowed.asset, entry_file, EAssetFileFormat::baked);
    AssetResult failed_save;
    TEST_EXPECT(ctx, fixture.receive(failed_save, 10) && failed_save.status == EAssetStatus::write_failed && failed_save.asset == borrowed.asset);
    fixture.service.request_disposal({ bundle.asset }, 11, fixture.client);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::closed);
    fixture.save(ctx, 12, borrowed.asset, copy_file, EAssetFileFormat::baked);
    TEST_EXPECT(ctx, fixture.result(ctx, 12).asset == borrowed.asset);
    check_role(bundled);
    bundled.clear();
    fixture.dispose(ctx, borrowed.asset);
    fixture.closed(ctx, bundle.asset);
    fixture.load(ctx, 13, copy_file, EAssetFileFormat::baked, 128u);
    const auto restored = fixture.result(ctx, 13);
    TEST_EXPECT(ctx, same_bytes(restored.byte_view(), snapshot) && bundled.attach_asset(restored.asset, restored.byte_view(), diagnostic));
    check_role(bundled);
    bundled.clear();
    fixture.dispose(ctx, restored.asset);

    //  Admission happens synchronously; disposal must wait for that save's borrow.
    attached.clear();
    fixture.save(ctx, 14, loaded.asset, copy_file, EAssetFileFormat::baked);
    fixture.service.request_disposal({ loaded.asset }, 15, fixture.client);
    TEST_EXPECT(ctx, fixture.result(ctx, 14).asset == loaded.asset);
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, fixture.receive(disposed, 15) && disposed.status == EAssetStatus::success && disposed.asset == loaded.asset);
    const auto drained = platform::filesystem::loadFile((fixture.directory + copy_name).c_str());
    TEST_EXPECT(ctx, same_bytes(drained.const_view(), snapshot));

    //  Failed saving retains source ownership only when requested.
    for (const auto retention : { EAssetRetention::source, EAssetRetention::discard })
    {
        owner = CErasedOwner::create<BakedAssetTransfer>();
        transfer = owner.payload<BakedAssetTransfer>();
        CByteBuffer copy;
        TEST_EXPECT(ctx, transfer && copy.resize(snapshot.size(), 128u));
        std::memcpy(copy.data(), snapshot.data(), snapshot.size());
        TEST_EXPECT(ctx, transfer->storage.value.adopt_buffer(std::move(copy)) && transfer->storage.file.set("absent:/failure"));
        transfer->save = true;
        transfer->retention = retention;
        fixture.request<BakedAssetTransfer>(16, std::move(owner));
        AssetResult failure;
        TEST_EXPECT(ctx, fixture.receive(failure, 16) && failure.status == EAssetStatus::write_failed);
        if (retention == EAssetRetention::source)
        {
            TEST_EXPECT(ctx, failure.asset && same_bytes(failure.byte_view(), snapshot));
            fixture.dispose(ctx, failure.asset);
        }
        else TEST_EXPECT(ctx, !failure.asset && failure.kind == EAssetKind::none);
    }
    TEST_EXPECT(ctx, schema.reference_count() == 0u && fixture.service.is_idle() && !fixture.service.failed());
}

void test_schema_workflows(TTestContext& ctx, const bool dedicated)
{
    SFixture fixture;
    if (!fixture.initialise(dedicated)) { TEST_EXPECT(ctx, false); return; }
    constexpr char types[] = R"({"types":{"structures":{"Pair":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u8"}}]}}}})";
    CLiveDocument document;
    schema::CLiveSchema live_schema;
    CBakedDocumentBlock storage;
    schema::CBakedSchema schema;
    schema::SDiagnostic diagnostic;
    const bool ready = document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(types), sizeof(types) - 1u }, document).accepted() &&
        live_schema.try_adopt(std::move(document)) && live_schema.resolve(diagnostic) && live_schema.bake(storage, schema) && schema.resolve(diagnostic);
    TEST_EXPECT(ctx, ready);
    if (!ready) return;
    test_schema_persistence<true>(ctx, fixture, schema);
    test_schema_persistence<false>(ctx, fixture, schema);
}

void test_current_bundle_save(TTestContext& ctx, const bool dedicated)
{
    SFixture fixture;
    if (!fixture.initialise(dedicated)) { TEST_EXPECT(ctx, false); return; }
    fixture.bake(ctx, 1);
    const auto source = fixture.result(ctx, 1);
    //  Retain a mutable view from owned test storage before Host publication.
    //  Mutate only while no worker/client borrows it; no production access is added.
    BundlePreparation preparation;
    CByteBuffer copy;
    TEST_EXPECT(ctx, copy.construct_and_copy_from(source.byte_view()) && preparation.block.adopt_buffer(std::move(copy)));
    const auto writable = preparation.block.buffer_bytes();
    TEST_EXPECT(ctx, preparation.view.reset(writable.data(), writable.size()).succeeded() && preparation.directory.prepare(preparation.view));
    const auto entry = preparation.view.lookup(CStringView{ "document.baked" });
    const auto offset = static_cast<const std::uint8_t*>(entry.bytes.data()) - writable.data();
    CMutableBakedDocument editor(CByteView{ writable.data() + offset, fixture.document_size, 128u });
    TEST_EXPECT(ctx, editor.is_ready());
    fixture.service.request_disposal({ source.asset }, 2, fixture.client);
    fixture.closed(ctx, source.asset);
    std::uint64_t binding{};
    TEST_EXPECT(ctx, fixture.image.reserve_bundle("pack:", binding));
    const auto published = fixture.service.publish_bundle(preparation, binding, fixture.client);
    TEST_EXPECT(ctx, published.status == EAssetStatus::success && published.byte_view().data() == writable.data());
    fixture.save(ctx, 3, published.asset, "work:/current.bdl");
    TEST_EXPECT(ctx, fixture.result(ctx, 3).asset == published.asset);
    const std::vector<std::uint8_t> before(writable.data(), writable.data() + writable.size());
    const auto serial = fixture.image.write_serial();
    TEST_EXPECT(ctx, editor.set_unsigned_integer_value(editor.baked().object_child(editor.baked().root(), CStringView{ "answer" }), 43u));
    TEST_EXPECT(ctx, editor.baked().check_integrity() && fixture.image.write_serial() == serial);
    const auto unchanged = platform::filesystem::loadFile((fixture.directory + "/current.bdl").c_str());
    TEST_EXPECT(ctx, same_bytes(unchanged.const_view(), before));
    const std::vector<std::uint8_t> current(writable.data(), writable.data() + writable.size());
    TEST_EXPECT(ctx, current != before);
    editor.clear();
    fixture.save(ctx, 4, published.asset, "work:/current.bdl");
    TEST_EXPECT(ctx, fixture.result(ctx, 4).asset == published.asset);
    fixture.service.request_disposal({ published.asset }, 5, fixture.client);
    fixture.closed(ctx, published.asset);
    fixture.load_bundle(ctx, 6, "work:/current.bdl");
    const auto reloaded = fixture.result(ctx, 6);
    TEST_EXPECT(ctx, same_bytes(reloaded.byte_view(), current));
    fixture.load(ctx, 7, "pack:/document.baked", EAssetFileFormat::baked, 128u);
    const auto changed = fixture.result(ctx, 7);
    std::uint64_t answer{};
    TEST_EXPECT(ctx, changed.document_view().unsigned_integer_value(changed.document_view().object_child(changed.document_view().root(), CStringView{ "answer" }), answer) && answer == 43u);
    fixture.dispose(ctx, changed.asset);
    fixture.service.request_disposal({ reloaded.asset }, 8, fixture.client);
    fixture.closed(ctx, reloaded.asset);
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

void test_preparation_handoff(TTestContext& ctx)
{
    //  Echo workers let the Host observe the precise boundary between building,
    //  preparation and publication, including shutdown with a borrowed job.
    for (unsigned scenario = 0u; scenario != 4u; ++scenario)
    {
        SFixture fixture(true);
        const bool ready = fixture.initialise(false);
        TEST_EXPECT(ctx, ready);
        if (!ready) return;
        threading::CThreadPackage unavailable{ { thread_ids::bg_conditioning, module_ids::executable,
            platform::threading::EThreadPriority::Normal, &tests::echo_pod_messages }, fixture.conversion };
        if (scenario == 3u) TEST_EXPECT(ctx, fixture.service.configure_workers(fixture.io, &unavailable));
        fixture.bake(ctx, 1);
        threading::CErasedPodMsg work;
        BundleBuildWork build;
        TEST_EXPECT(ctx, fixture.take_work(work) && work.copy_payload_to(build) && build.input);
        auto owner = CErasedOwner::create<BundleBuildResult>();
        auto* built = owner.payload<BundleBuildResult>();
        TEST_EXPECT(ctx, built != nullptr);
        if (!built || !build.input) return;
        built->result = bundle::build(build.input->inventory(), build.input->content(),
            build.input->sources(), built->block, built->view);
        TEST_EXPECT(ctx, built->result.succeeded());
        fixture.complete<BundleBuildResult>(work, std::move(owner));
        if (scenario == 3u)
        {
            //  Failed preparation dispatch must release the reservation and bytes.
            fixture.result(ctx, 1, EAssetStatus::delivery_failed);
        }
        else
        {
            BundlePrepareWork prepare;
            TEST_EXPECT(ctx, fixture.take_work(work) && work.copy_payload_to(prepare) && prepare.preparation);
            if (!prepare.preparation) return;
            TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::reserved && !fixture.service.is_idle());
            filesystem_image::SResolvedSource source;
            TEST_EXPECT(ctx, !fixture.image.resolve("pack:/raw", false, source));
            if (scenario == 2u)
            {
                TEST_EXPECT(ctx, fixture.io.shutdown());
                fixture.service.fail_pending();
                fixture.result(ctx, 1, EAssetStatus::delivery_failed);
            }
            else
            {
                auto& preparation = *prepare.preparation;
                TEST_EXPECT(ctx, preparation.view.is_ready() && preparation.directory.prepare(preparation.view));
                //  A concurrent Host update must survive committing the prepared subtree.
                TEST_EXPECT(ctx, fixture.image.written("work:/new", (fixture.directory + "/new").c_str(), 501u, 1u));
                threading::CErasedPodMsg completion;
                completion.set_async_slot(work.query_async_slot());
                completion.assign_payload(BundlePrepareResult{ scenario == 0u ? EAssetStatus::success : EAssetStatus::allocation_failed });
                fixture.service.complete(completion, fixture.io);
                TEST_EXPECT(ctx, fixture.image.cached_asset("work:/new", 1u) == 501u);
                const auto result = fixture.result(ctx, 1, scenario == 0u ? EAssetStatus::success : EAssetStatus::allocation_failed);
                if (scenario == 0u)
                {
                    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::open && result.bundle_view().is_ready());
                    fixture.service.request_disposal({ result.asset }, 99, fixture.client);
                    fixture.closed(ctx, result.asset);
                }
            }
        }
        TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing && fixture.service.is_idle());
    }
}

void test_controlled_failures(TTestContext& ctx)
{
    SFixture fixture(true);
    const bool ready = fixture.initialise(true);
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
    auto loaded = CErasedOwner::create<ByteStorage>();
    auto* file = loaded.payload<ByteStorage>();
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
    TEST_EXPECT(ctx, fixture.io.shutdown() && fixture.conditioning.shutdown());
    fixture.service.fail_pending();
    fixture.result(ctx, 4, EAssetStatus::delivery_failed);
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing && fixture.service.is_idle());
    threading::CThreadPackage unavailable{ { thread_ids::bg_file_io, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &tests::echo_pod_messages }, fixture.conversion };
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

void test_completion_contracts(TTestContext& ctx)
{
    tests::TAssertionTestScope assertions{ ctx };
    enum class EReply { empty, wrong_payload, wrong_byte_protocol };
    // File, bundle load/build, TGA decode/encode and document replies all use
    // the same admission gate but retain their distinct ordinary failure codes.
    for (int protocol = 0; protocol < 6; ++protocol)
    {
        for (const EReply scenario : { EReply::empty, EReply::wrong_payload, EReply::wrong_byte_protocol })
        {
            if ((scenario == EReply::wrong_byte_protocol) && (protocol != 0) && (protocol != 4)) continue;
            SFixture fixture(true);
            const bool ready = fixture.initialise(false);
            TEST_EXPECT(ctx, ready);
            if (!ready) return;
            if (protocol == 1) fixture.load_bundle(ctx, 80, "work:/Input/Document.baked");
            else if (protocol == 2) fixture.bake(ctx, 80);
            else if (protocol == 4)
            {
                auto owner = CErasedOwner::create<ImageAssetTransfer>();
                auto* const transfer = owner.payload<ImageAssetTransfer>();
                if (transfer == nullptr) { TEST_EXPECT(ctx, false); return; }
                TEST_EXPECT(ctx, transfer->storage.value.allocate(4u, 1u, 16u));
                TEST_EXPECT(ctx, transfer->storage.file.set("work:/encoded.tga"));
                transfer->description = image::codec::tga::decoded_image_desc::RGBA;
                transfer->settings.format = EAssetFileFormat::tga;
                transfer->save = true;
                transfer->retention = EAssetRetention::discard;
                fixture.request<ImageAssetTransfer>(80, std::move(owner));
            }
            else fixture.load(ctx, 80, protocol == 3 ? "work:/Input/Image.tga" :
                (protocol == 5 ? "work:/Input/Data.json" : "work:/Input/Raw"),
                protocol == 3 ? EAssetFileFormat::tga : (protocol == 5 ? EAssetFileFormat::json : EAssetFileFormat::raw));

            threading::CErasedPodMsg work;
            if (!fixture.take_work(work)) { TEST_EXPECT(ctx, false); return; }
            if ((protocol == 3) || (protocol == 5))
            {
                FileLoadRequest load;
                TEST_EXPECT(ctx, work.copy_payload_to(load));
                auto owner = CErasedOwner::create<ByteStorage>();
                auto* const loaded = owner.payload<ByteStorage>();
                if (loaded == nullptr) { TEST_EXPECT(ctx, false); return; }
                loaded->buffer = platform::filesystem::loadFile(load.file, 0u, load.alignment);
                TEST_EXPECT(ctx, loaded->buffer.is_ready());
                fixture.complete<FileLoadResult>(work, std::move(owner));
                if (!fixture.take_work(work)) { TEST_EXPECT(ctx, false); return; }
            }
            threading::CErasedOwnerMsg reply;
            switch (protocol)
            {
                case 2: reply.set_message_type<BundleBuildResult>(); break;
                case 3: reply.set_message_type<TgaDecodeResult>(); break;
                case 4: reply.set_message_type<TgaEncodeResult>(); break;
                case 5: reply.set_message_type<DocumentConditionResult>(); break;
                default: reply.set_message_type<FileLoadResult>(); break;
            }
            reply.set_async_slot(work.query_async_slot());
            if (scenario == EReply::wrong_byte_protocol)
            {
                //  The payload is valid for both protocols, but the completion tag is not.
                if (protocol == 0) reply.set_message_type<TgaEncodeResult>();
                else reply.set_message_type<FileLoadResult>();
                reply.set_owner(CErasedOwner::create<ByteStorage>());
                auto* const payload = reply.owner().payload<ByteStorage>();
                TEST_EXPECT(ctx, payload != nullptr);
                if (payload == nullptr) return;
                TEST_EXPECT(ctx, payload->buffer.allocate(32u, 128u));
                reply.owner().add_hazard(mount_point_ids::asset);
                const auto* const context = reply.owner().memory_context();
                assertions.expect_unconditional_assertion(ctx, [&] { fixture.service.complete(reply, fixture.io); });
                TEST_EXPECT(ctx, fixture.service.failed() && !fixture.service.is_idle());
                TEST_EXPECT(ctx, reply.owner().payload<ByteStorage>() == payload);
                TEST_EXPECT(ctx, reply.query_async_slot() == work.query_async_slot());
                TEST_EXPECT(ctx, reply.owner().memory_context() == context);
                TEST_EXPECT(ctx, reply.owner().has_hazard(mount_point_ids::asset));
                TEST_EXPECT(ctx, payload->buffer.is_ready() && payload->buffer.align() == 128u);
            }
            else if (scenario == EReply::wrong_payload)
            {
                reply.set_owner(CErasedOwner::create<AssetLoadRequest>());
                const auto* const payload = reply.owner().payload<AssetLoadRequest>();
                assertions.expect_unconditional_assertion(ctx, [&] { fixture.service.complete(reply, fixture.io); });
                TEST_EXPECT(ctx, fixture.service.failed() && !fixture.service.is_idle());
                TEST_EXPECT(ctx, reply.owner().payload<AssetLoadRequest>() == payload);
                if ((protocol == 1) || (protocol == 2))
                    TEST_EXPECT(ctx, fixture.image.root_state("pack:") != ERootState::missing);
                // Inputs and bundle reservations stay alive until joined-worker cleanup.
                if (protocol == 3)
                {
                    TgaDecodeRequest decode;
                    TEST_EXPECT(ctx, work.copy_payload_to(decode) && decode.view.is_ready() && decode.view.data()[2] == 2u);
                }
            }
            else
            {
                fixture.service.complete(reply, fixture.io);
                fixture.result(ctx, 80, protocol < 2 ? EAssetStatus::read_failed :
                    ((protocol == 3) || (protocol == 4) ? EAssetStatus::conditioning_failed : EAssetStatus::allocation_failed));
                TEST_EXPECT(ctx, !fixture.service.failed() && fixture.service.is_idle());
            }
        }
    }
}

}   // namespace

int run_bundle_request_tests()
{
    TTestContext ctx;
    test_native_loads(ctx, false, true);
    test_native_loads(ctx, true, true);
    test_native_loads(ctx, false, false);
    test_native_loads(ctx, true, false);
    test_public_workflow(ctx, false);
    test_public_workflow(ctx, true);
    test_complete_baked_buffers(ctx, false);
    test_complete_baked_buffers(ctx, true);
    test_schema_workflows(ctx, false);
    test_schema_workflows(ctx, true);
    test_current_bundle_save(ctx, false);
    test_current_bundle_save(ctx, true);
    test_failed_work(ctx);
    test_preparation_handoff(ctx);
    test_controlled_failures(ctx);
    test_completion_contracts(ctx);
    return ctx.report("BundleRequests");
}
