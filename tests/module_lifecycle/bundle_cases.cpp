//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bundle_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    12 Oct 26
//
//  Public bundle persistence, closure, batch borrowing and replacement cases.

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "tests/module_lifecycle/bundle_cases.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "debug/macros.hpp"
#include "platform/filesystem/file.hpp"
#include "platform/threading/processor_relax.hpp"
#include "assets/asset_messages.hpp"
#include "system/service_messages.hpp"
#include "tests/support/host_test_paths.hpp"

namespace module_lifecycle_tests
{

static constexpr const char* k_root = "bundle-test:";
static constexpr const char* k_saved = "dev-workspace:/acceptance/lifecycle-bundle.bdl";

class CBundleClient
{
public:
    threading::CThreadContext& context;
    std::int32_t slot{ 2000 };

    bool read(threading::CErasedPodMsg& message) noexcept
    {
        platform::system::CPerfCounter timer;
        (void)timer.update();
        while (!context.exit_requested() && (timer.query_delta() < context.perf_count_conversion().query_ticks_per_second() * 10u))
        {
            if (context.read(message)) return true;
            context.advance_heartbeat();
            platform::threading::processor_relax();
        }
        MV_LOG(error, "Bundle fixture: reply timed out at slot {}", slot);
        return false;
    }
    template<typename T> bool receive(T& value, const std::int32_t expected) noexcept
    {
        threading::CErasedPodMsg message;
        const bool valid = read(message) && (message.query_async_slot() == expected) && message.copy_payload_to(value);
        if (!valid) MV_LOG(error, "Bundle fixture: unexpected reply at slot {} (expected {})", message.query_async_slot(), expected);
        return valid;
    }
    template<typename T> bool post(const T& value) noexcept
    {
        threading::CErasedPodMsg message;
        message.set_async_slot(++slot);
        message.assign_payload(value);
        return context.post(message);
    }
    template<typename T> bool post(CErasedOwner&& owner) noexcept
    {
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(++slot);
        message.set_owner(std::move(owner));
        return context.post(std::move(message));
    }
    bool result(AssetResult& value, EAssetStatus expected = EAssetStatus::success) noexcept
    {
        if (!receive(value, slot)) return false;
        if (value.status == expected) return true;
        MV_LOG(error, "Bundle fixture: slot {} status {} expected {}", slot,
            static_cast<unsigned int>(value.status), static_cast<unsigned int>(expected));
        return false;
    }
    bool bake(AssetResult& value, EAssetStatus expected = EAssetStatus::success, const char* directory = "BundleInput") noexcept
    {
        auto owner = CErasedOwner::create<BundleBakeRequest>();
        auto* request = owner.payload<BundleBakeRequest>();
        return request && request->paths.source_root.set("dev-source:") && request->paths.subdirectory.set(directory) &&
            request->paths.root.set(k_root) && post<BundleBakeRequest>(std::move(owner)) && result(value, expected);
    }
    bool bind(AssetResult& value, const char* file = k_saved, EAssetStatus expected = EAssetStatus::success) noexcept
    {
        auto owner = CErasedOwner::create<BundleLoadRequest>();
        auto* request = owner.payload<BundleLoadRequest>();
        return request && request->paths.file.set(file) && request->paths.root.set(k_root) &&
            post<BundleLoadRequest>(std::move(owner)) && result(value, expected);
    }
    bool post_load(const char* file, EAssetFileFormat format, std::size_t alignment = 16u) noexcept
    {
        auto owner = CErasedOwner::create<AssetLoadRequest>();
        auto* request = owner.payload<AssetLoadRequest>();
        if (!request || !request->file.set(file)) return false;
        request->format = format;
        request->alignment = alignment;
        return post<AssetLoadRequest>(std::move(owner));
    }
    bool load(AssetResult& value, const char* file, EAssetFileFormat format = EAssetFileFormat::raw,
        EAssetStatus expected = EAssetStatus::success, std::size_t alignment = 16u) noexcept
    {
        return post_load(file, format, alignment) && result(value, expected);
    }
    bool save(CAssetId asset, const char* file = k_saved, EAssetStatus expected = EAssetStatus::success) noexcept
    {
        auto owner = CErasedOwner::create<AssetSaveRequest>();
        auto* request = owner.payload<AssetSaveRequest>();
        if (!request || !request->file.set(file)) return false;
        request->source = asset;
        request->settings.format = EAssetFileFormat::raw;
        AssetResult value;
        return post<AssetSaveRequest>(std::move(owner)) && result(value, expected);
    }
    bool dispose(CAssetId asset) noexcept
    {
        AssetDisposeResult value;
        return post(AssetDisposeRequest{ asset }) && receive(value, slot) &&
            (value.asset == asset) && (value.status == EAssetStatus::success);
    }
    bool notification(CAssetId original) noexcept
    {
        BundleDisposedNotification value;
        return receive(value, -1) && (value.asset == original);
    }
    bool refresh(EFilesystemStatus expected) noexcept
    {
        auto owner = CErasedOwner::create<FilesystemRefreshRequest>();
        auto* request = owner.payload<FilesystemRefreshRequest>();
        FilesystemRefreshResult value;
        return request && request->root.set(k_root) && post<FilesystemRefreshRequest>(std::move(owner)) &&
            receive(value, slot) && (value.status == expected);
    }
    //  A following POD rejection establishes closure across the owning queue.
    bool close(CAssetId original) noexcept
    {
        AssetResult value;
        return post(AssetDisposeRequest{ original }) && post(BundleReferenceRequest{ original }) && result(value, EAssetStatus::invalid_asset);
    }
};

static bool same_bytes(CByteConstView a, CByteConstView b) noexcept
{
    return a.is_ready() && b.is_ready() && (a.size() == b.size()) && (std::memcmp(a.data(), b.data(), a.size()) == 0);
}

static bool make_document(CBundleClient& client) noexcept
{
    constexpr char text[] = "{\"answer\":42}";
    CLiveDocument document;
    auto owner = CErasedOwner::create<BakedAssetTransfer>();
    auto* request = owner.payload<BakedAssetTransfer>();
    if (!request || !document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(text), sizeof(text) - 1u }, document).accepted() ||
        !document_translation::bake(document, request->storage.value) ||
        !request->storage.file.set("dev-source:/BundleInput/Document.baked")) return false;
    request->save = true;
    request->retention = EAssetRetention::discard;
    request->settings.format = EAssetFileFormat::baked;
    AssetResult result;
    return client.post<BakedAssetTransfer>(std::move(owner)) && client.result(result);
}

struct SBatchRead
{
    CByteConstView bytes;
    std::atomic<bool> release{ false };
    bool wait{ false };
    bool valid{ false };
};

static void MV_STD_ABI_CALL read_bundle_bytes(void* object) noexcept
{
    auto& item = *static_cast<SBatchRead*>(object);
    while (item.wait && !item.release.load(std::memory_order_acquire)) platform::threading::processor_relax();
    item.valid = item.bytes.size() == 128u;
    for (std::size_t index = 0; index < item.bytes.size(); ++index) item.valid &= item.bytes.data()[index] == 0x5au;
}

static bool workflow(CBundleClient& client, threading::CThreadResources& resources, bool require_inline) noexcept
{
    AssetResult original, raw, cached, baked, image, shader, scratch;
    if (!make_document(client) || !client.bake(original)) return false;
    const auto view = original.bundle_view();
    if (!view.is_ready() || (view.lookup(CStringView{ "empty" }).status != bundle::ELookupStatus::not_found) ||
        (view.lookup(CStringView{ "zero" }).status != bundle::ELookupStatus::not_found) ||
        (view.lookup(CStringView{ "unsupported" }).status != bundle::ELookupStatus::not_found)) return false;
    for (const char* name : { "offset", "content", "size", "MiXeD/Child" })
        if (view.lookup(CStringView{ name }).status != bundle::ELookupStatus::success) return false;
    for (std::size_t size : { 127u, 128u, 129u })
    {
        const char* name = (size == 127u) ? "Before" : (size == 128u) ? "Aligned" : "After";
        const auto found = view.lookup(CStringView{ name });
        if ((found.status != bundle::ELookupStatus::success) || (found.bytes.bytes() != size) ||
            (reinterpret_cast<std::uintptr_t>(found.bytes.data()) % 128u != 0u)) return false;
        const auto* bytes = static_cast<const std::uint8_t*>(found.bytes.data());
        for (std::size_t index = 0; index < size; ++index) if (bytes[index] != 0x5au) return false;
        if (bytes[size] != 0u) return false;
    }
    CByteBuffer snapshot;
    const auto bytes = original.byte_view();
    if (!snapshot.allocate(bytes.size()) || !snapshot.set_size(bytes.size())) return false;
    std::memcpy(snapshot.data(), bytes.data(), bytes.size());
    if (!client.post(BundleReferenceRequest{ original.asset }) || !client.result(scratch) ||
        (scratch.asset == original.asset) || !same_bytes(scratch.byte_view(), bytes) || !client.dispose(scratch.asset) ||
        !client.load(shader, "dev-source:/BundleInput/Shader.hlsl") ||
        !same_bytes(shader.byte_view(), CByteConstView{ static_cast<const std::uint8_t*>(view.lookup(CStringView{ "shader.hlsl" }).bytes.data()),
            view.lookup(CStringView{ "shader.hlsl" }).bytes.bytes() }) || !client.dispose(shader.asset) ||
        !client.load(raw, "bundle-test:/Aligned") || !client.refresh(EFilesystemStatus::success) ||
        !client.load(cached, "BUNDLE-TEST:/ALIGNED") || (cached.asset != raw.asset) ||
        !client.load(scratch, "bundle-test:/Aligned", EAssetFileFormat::raw, EAssetStatus::invalid_request, 256u) ||
        !client.load(baked, "bundle-test:/Document.baked", EAssetFileFormat::baked) || !baked.document_view().is_ready() ||
        (baked.byte_view().data() != view.lookup(CStringView{ "document.baked" }).bytes.data()) ||
        !client.load(image, "bundle-test:/Image.tga", EAssetFileFormat::tga) || !image.image_view().is_ready()) return false;

    //  Both requests are outstanding together; cache hits may depend on scheduling.
    AssetResult parsed[2];
    if (!client.post_load("bundle-test:/Data.json", EAssetFileFormat::json)) return false;
    const auto first_slot = client.slot;
    if (!client.post_load("bundle-test:/Data.json", EAssetFileFormat::json) ||
        !client.receive(parsed[0], first_slot) || !client.receive(parsed[1], first_slot + 1) ||
        (parsed[0].status != EAssetStatus::success) || (parsed[1].status != EAssetStatus::success) ||
        !parsed[0].document_view().is_ready() || !parsed[1].document_view().is_ready() ||
        !client.save(original.asset) || !client.dispose(baked.asset) || !same_bytes(bytes, snapshot.const_view())) return false;

    auto* batch = resources.batch_client;
    if (!batch || (require_inline && batch->has_runners())) return false;
    SBatchRead item;
    item.bytes = raw.byte_view();
    item.wait = batch->has_runners();
    const auto submitted = batch->submit(&read_bundle_bytes, &item, 700u);
    const bool queued = submitted.status == threading::EBatchSubmission::queued;
    if (!queued && submitted.status != threading::EBatchSubmission::completed_inline) return false;
    //  Always release/drain the callback, including failed assertions, before its stack state dies.
    bool success = client.close(original.asset) && client.refresh(EFilesystemStatus::invalid_root) &&
        client.bake(scratch, EAssetStatus::busy) && client.save(original.asset, k_saved, EAssetStatus::invalid_asset) &&
        client.load(scratch, "bundle-test:/Aligned", EAssetFileFormat::raw, EAssetStatus::read_failed);
    item.release.store(true, std::memory_order_release);
    if (queued)
    {
        threading::SBatchResponse response;
        while (!batch->receive(response)) platform::threading::processor_relax();
        success &= (response.correlation == 700u) && (response.completion == threading::EBatchCompletion::executed);
    }
    if (!success || !item.valid || (batch->outstanding() != 0u) ||
        !client.save(raw.asset, "test-output:/bundle-subfile.bin") || !client.dispose(raw.asset) ||
        !client.notification(original.asset) || !parsed[0].document_view().check_integrity() || !image.image_view().is_ready()) return false;
    MV_LOG_RICH(info, "Bundle fixture: batch borrow %s survived closure", queued ? "queued" : "inline");
    if (!client.dispose(parsed[0].asset) || ((parsed[1].asset != parsed[0].asset) && !client.dispose(parsed[1].asset)) ||
        !client.dispose(image.asset) || !client.bind(scratch) || !same_bytes(scratch.byte_view(), snapshot.const_view()) ||
        !client.post(AssetDisposeRequest{ scratch.asset }) || !client.notification(scratch.asset) ||
        !client.bake(scratch) || !same_bytes(scratch.byte_view(), snapshot.const_view()) ||
        !client.post(AssetDisposeRequest{ scratch.asset }) || !client.notification(scratch.asset)) return false;
    MV_LOG(info, "Bundle fixture: persistence, refresh, cache and root reuse passed");
    return true;
}

struct SHandoff { CAssetId original; CAssetId file; };

static bool save_handoff(CBundleClient& client, const SHandoff& value,
    const char* path = "test-output:/bundle-handoff.bin") noexcept
{
    auto owner = CErasedOwner::create<RawAssetTransfer>();
    auto* request = owner.payload<RawAssetTransfer>();
    if (!request || !request->storage.file.set(path) ||
        !request->storage.value.allocate(sizeof(value)) || !request->storage.value.set_size(sizeof(value))) return false;
    std::memcpy(request->storage.value.data(), &value, sizeof(value));
    request->save = true;
    request->retention = EAssetRetention::discard;
    request->settings.format = EAssetFileFormat::raw;
    AssetResult result;
    return client.post<RawAssetTransfer>(std::move(owner)) && client.result(result);
}

bool rendering_bundle_case(threading::CThreadContext& context, threading::CThreadResources& resources) noexcept
{
    CBundleClient client{ context };
    AssetResult original, loaded, file;
    if (!client.bake(original) || !client.save(original.asset) ||
        !client.post(AssetDisposeRequest{ original.asset }) || !client.notification(original.asset) ||
        !client.bind(loaded) || !client.load(file, "bundle-test:/Aligned") ||
        !save_handoff(client, { loaded.asset, file.asset }, "test-output:/bundle-rendering-handoff.bin")) return false;
    MV_LOG(info, "Bundle fixture: Rendering owns loaded bundle");
    while (!context.exit_requested())
    {
        const auto epoch = resources.wait_predicate.get_word();
        if (!context.exit_requested()) (void)context.wait_for_new_epoch(epoch);
    }
    //  Post closure at exit; the Executive keeps the sub-file until after join.
    return client.post(AssetDisposeRequest{ loaded.asset });
}

static bool rendering_module(CBundleClient& client, EModuleAction action) noexcept
{
    auto owner = CErasedOwner::create<ModuleRequest>();
    auto* request = owner.payload<ModuleRequest>();
    if (!request) return false;
    request->action = action;
    request->module = module_ids::render_vulkan;
    if (action == EModuleAction::load)
    {
        if (!request->file.set("package:/bin/MorphicLifecycleRenderingDisposal.dll")) return false;
        request->required_function = system_type_ids::rendering_thread_function;
    }
    if (!client.post<ModuleRequest>(std::move(owner))) return false;
    for (const auto notice : { EModuleNotice::acknowledged, EModuleNotice::completed })
    {
        ModuleResult result;
        if (!client.receive(result, client.slot) || (result.notice != notice) ||
            (result.status != EModuleStatus::success)) return false;
    }
    return true;
}

static bool rendering_owner(CBundleClient& client) noexcept
{
    if (!rendering_module(client, EModuleAction::load)) return false;
    SHandoff handoff;
    platform::system::CPerfCounter timer;
    (void)timer.update();
    bool ready = false;
    while (!ready && (timer.query_delta() < client.context.perf_count_conversion().query_ticks_per_second() * 10u))
    {
        ready = platform::filesystem::loadFileInto("development/logical-roots/test-output/bundle-rendering-handoff.bin",
            CByteView{ reinterpret_cast<std::uint8_t*>(&handoff), sizeof(handoff), alignof(SHandoff) });
        if (!ready) platform::threading::processor_relax();
    }
    if (!ready || !handoff.original || !handoff.file) return false;
    AssetDisposeResult rejected;
    AssetResult reference, file, result;
    if (!client.post(AssetDisposeRequest{ handoff.original }) || !client.receive(rejected, client.slot) ||
        (rejected.status != EAssetStatus::invalid_request) ||
        !client.post(BundleReferenceRequest{ handoff.original }) || !client.result(reference) ||
        !reference.bundle_view().is_ready() || !client.dispose(reference.asset) ||
        !client.load(file, "bundle-test:/Aligned") || (file.asset != handoff.file) ||
        !rendering_module(client, EModuleAction::unload) ||
        !client.post(BundleReferenceRequest{ handoff.original }) || !client.result(result, EAssetStatus::invalid_asset) ||
        !client.bake(result, EAssetStatus::busy) || !client.save(file.asset, "test-output:/bundle-rendering-survivor.bin") ||
        !client.dispose(file.asset) || !client.bake(result) ||
        !client.post(AssetDisposeRequest{ result.asset }) || !client.notification(result.asset)) return false;
    MV_LOG(info, "Bundle fixture: Rendering ownership and post-join sub-file release passed");
    return true;
}

bool bundle_case(threading::CThreadContext& context, threading::CThreadResources& resources, const char* selected) noexcept
{
    CBundleClient client{ context };
    AssetResult original, file, result;
    if (std::strcmp(selected, "bundle-rendering") == 0) return rendering_owner(client);
    if (std::strcmp(selected, "bundle") == 0 || std::strcmp(selected, "bundle-inline") == 0)
        return workflow(client, resources, std::strcmp(selected, "bundle-inline") == 0);
    if (std::strcmp(selected, "bundle-resume") == 0)
    {
        SHandoff handoff;
        CSimpleString native;
        if (!host_test_paths::native_output("bundle-handoff.bin", native) ||
            !client.load(result, native.cstring()) || (result.byte_view().size() != sizeof(handoff))) return false;
        std::memcpy(&handoff, result.byte_view().data(), sizeof(handoff));
        if (!client.dispose(result.asset) || !client.refresh(EFilesystemStatus::invalid_root) ||
            !client.post(BundleReferenceRequest{ handoff.original }) || !client.result(result, EAssetStatus::invalid_asset) ||
            !client.bake(result, EAssetStatus::busy) || !client.save(handoff.file, "test-output:/bundle-replacement.bin") ||
            !client.dispose(handoff.file) || !client.bake(result) ||
            !client.post(AssetDisposeRequest{ result.asset }) || !client.notification(result.asset)) return false;
        MV_LOG(info, "Bundle fixture: replacement released old storage without inheriting its notification");
        return true;
    }
    if (std::strcmp(selected, "bundle-failures") == 0)
    {
        //  Each failure must release the same destination name for the next request.
        //  Remove an already inventoried fixture to force a genuine worker I/O failure.
        return (std::remove("development/logical-roots/dev-source/BundleMissing/Vanished") == 0) &&
            client.bind(result, "dev-source:/BundleInput/Data.json", EAssetStatus::conditioning_failed) &&
            client.bind(result, "dev-source:/missing.bdl", EAssetStatus::read_failed) &&
            client.bake(result, EAssetStatus::invalid_request, "BundleInput/Empty") &&
            client.bake(result, EAssetStatus::read_failed, "BundleMissing") && client.bake(result) &&
            client.post(AssetDisposeRequest{ result.asset }) && client.notification(result.asset);
    }
    if (!client.bake(original) || !client.load(file, "bundle-test:/Aligned")) return false;
    if (std::strcmp(selected, "bundle-shutdown") == 0) return true; //  Full shutdown owns undisposed records.
    if (std::strcmp(selected, "bundle-replace") == 0)
    {
        return save_handoff(client, { original.asset, file.asset }) && client.close(original.asset) &&
            (_putenv_s("MORPHIC_LIFECYCLE_CASE", "bundle-resume") == 0);
    }
    if (std::strcmp(selected, "bundle-late-reference") == 0)
    {
        //  All three use the same FIFO. Defer using the accepted reference until closure is confirmed.
        if (!client.post(BundleReferenceRequest{ original.asset })) return false;
        const auto acquired_slot = client.slot;
        if (!client.post(AssetDisposeRequest{ original.asset }) || !client.post(BundleReferenceRequest{ original.asset })) return false;
        AssetResult acquired;
        if (!client.receive(acquired, acquired_slot) || (acquired.status != EAssetStatus::success) ||
            !client.result(result, EAssetStatus::invalid_asset) || !client.dispose(file.asset) ||
            !client.dispose(acquired.asset) || !client.notification(original.asset)) return false;
        return true;
    }
    return false;
}

}   // namespace module_lifecycle_tests
