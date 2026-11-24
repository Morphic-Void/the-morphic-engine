
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BundleService_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    12 Oct 26
//
//  Controlled worker completions and retained-record bundle lifetimes.

#include "framework/system/system_ids.hpp"
#include <cstring>
#include <vector>

#include "assets/internal/bundle_builder_io.hpp"
#include "data_model/document_translation.hpp"
#include "host/assets/asset_service.hpp"
#include "tests/support/endpoint_fixture.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/BundleService_test_suite.hpp"

namespace
{

using tests::TTestContext;
using filesystem_image::ERootState;
using host::CAssetService;

bool make_bundle(CBakedDocumentBlock& block)
{
    CLiveDocument inventory, document;
    CBakedDocumentBlock baked;
    if (!tests::parse_document(R"({"raw":{},"baked":{}})", inventory) || !tests::parse_document(R"({"answer":42})", document) ||
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

std::uint64_t next_recipient_identity() noexcept
{
    static std::uint64_t identity = 2000u;
    return ++identity;
}

// These controlled recipients have no native execution or outstanding ingress.
// Actual affine termination/join is covered by common-preamble domain fixtures.
void end_recipient(host::SClientRecord& client) noexcept
{
    client.phase = host::EClientPhase::stopped;
    client.terminal_before_drain = client.incoming_drained = true;
    client.flush_replies();
}

struct SFixture
{
    platform::system::CPerfCountConversion conversion;
    threading::CThreadPackage worker{ threading::ThreadConfig{ thread_ids::bg_file_io, module_ids::executable,
        platform::threading::EThreadPriority::Normal, &tests::echo_pod_messages }, conversion };
    tests::SCommonEndpointFixture client{ module_ids::executable, next_recipient_identity(), 32u };
    filesystem_image::CImage image;
    CAssetService service;
    std::uint64_t binding{ 0u };

    ~SFixture() noexcept
    {
        (void)tests::stop_and_join(worker, conversion);
        end_recipient(client);
        service.detach_client(client);
        service.deallocate();
    }
    bool initialise()
    {
        CLiveDocument document;
        service.set_filesystem(image);
        return tests::parse_document(R"({"roots":{"work:":{"source":"unused","writable":true,"content":{}}}})", document) &&
            image.adopt(std::move(document)) && conversion.init() && service.initialise() &&
            service.configure_workers(worker, nullptr) && worker.startup();
    }
    void progress(host::SClientRecord& recipient)
    {
        service.complete_disposals();
        recipient.flush_replies();
        for (unsigned count = 0u; (count < 8u) && recipient.output_ready(); ++count)
        {
            const bool asset = service.output_ready(recipient);
            if (asset) service.retry_output(recipient);
            const bool bundle = recipient.output_ready() && service.bundle_output_ready(recipient);
            if (bundle) service.retry_bundle_output(recipient);
            if (!asset && !bundle)
            {
                break;
            }
        }
        service.complete_disposals();
    }
    void request_disposal(TTestContext& ctx, const AssetDisposeRequest& request, const std::int32_t slot, host::SClientRecord& recipient)
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_disposal(request, slot, recipient);
    }
    void request_reference(TTestContext& ctx, const BundleReferenceRequest& request, const std::int32_t slot, host::SClientRecord& recipient)
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_reference(request, slot, recipient);
    }
    void request_acquire(TTestContext& ctx, const AssetAcquireRequest& request, const std::int32_t slot, host::SClientRecord& recipient)
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_acquire(request, slot, recipient);
    }
    void request_release(TTestContext& ctx, const AssetReleaseRequest& request, const std::int32_t slot, host::SClientRecord& recipient)
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_release(request, slot, recipient);
    }
    AssetResult publish(TTestContext& ctx, host::SClientRecord* const owner = nullptr)
    {
        BundlePreparation prepared;
        TEST_EXPECT(ctx, make_bundle(prepared.block));
        const void* const before = prepared.block.buffer_bytes().data();
        TEST_EXPECT(ctx, prepared.view.reset(before, prepared.block.buffer_bytes().size()).succeeded());
        TEST_EXPECT(ctx, prepared.directory.prepare(prepared.view));
        TEST_EXPECT(ctx, image.reserve_bundle("pack:", binding));
        const AssetResult result = service.publish_bundle(prepared, binding, owner ? *owner : client);
        TEST_EXPECT(ctx, ((result.status == EAssetStatus::success) && result.asset && !prepared.block.is_ready()));
        TEST_EXPECT(ctx, (result.bundle_view().bytes().data() == before));
        return result;
    }
    filesystem_image::SResolvedSource resolve(TTestContext& ctx, const char* const path = "pack:/raw")
    {
        filesystem_image::SResolvedSource result;
        TEST_EXPECT(ctx, image.resolve(path, false, result));
        return result;
    }
    template<typename T> bool receive(T& result, const std::int32_t expected_slot, host::SClientRecord* const recipient = nullptr)
    {
        host::SClientRecord& target = recipient ? *recipient : client;
        progress(target);
        threading::CErasedPodMsg message;
        return target.channel->read_preamble_pod(message) && (message.query_async_slot() == expected_slot) && message.copy_payload_to(result);
    }
    bool receive_work(FileSaveRequest& work, const std::int32_t expected_slot)
    {
        threading::CErasedPodMsg message;
        return tests::wait_until(conversion, [&] { return worker.read(message); }) &&
            (message.query_async_slot() == expected_slot) && message.copy_payload_to(work);
    }
    template<typename T> void request(TTestContext& ctx, const std::int32_t slot, CErasedOwner&& owner)
    {
        TEST_EXPECT(ctx, (owner.payload<T>() != nullptr));
        threading::CErasedOwnerMsg message;
        message.set_message_type<T>();
        message.set_async_slot(slot);
        message.set_owner(std::move(owner));
        host::CRetainedReplies::CDispatch dispatch{ *client.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && client.reserve_replies(1u)));
        service.request(message, client);
    }
    void dispose(TTestContext& ctx, const CAssetId asset)
    {
        request_disposal(ctx, { asset }, 7, client);
        service.complete_disposals();
        AssetDisposeResult result;
        TEST_EXPECT(ctx, (receive(result, 7) && (result.asset == asset) && (result.status == EAssetStatus::success)));
    }
    void notification(TTestContext& ctx, const CAssetId asset)
    {
        BundleDisposedNotification result;
        TEST_EXPECT(ctx, (receive(result, -1) && (result.asset == asset)));
        TEST_EXPECT(ctx, (image.root_state("pack:") == ERootState::missing));
        service.complete_disposals();
        request_disposal(ctx, { asset }, 8, client);
        AssetDisposeResult rejected;
        TEST_EXPECT(ctx, (receive(rejected, 8) && (rejected.status == EAssetStatus::invalid_asset)));
    }
    void save(TTestContext& ctx, const CAssetId asset, const std::int32_t slot)
    {
        auto owner = CErasedOwner::create<AssetSaveRequest>();
        AssetSaveRequest* const save = owner.payload<AssetSaveRequest>();
        TEST_EXPECT(ctx, (save && save->file.set("work:/saved.bdl")));
        if (!save) return;
        save->source = asset;
        save->settings.format = EAssetFileFormat::raw;
        request<AssetSaveRequest>(ctx, slot, std::move(owner));
    }
    void complete_save(const std::int32_t slot)
    {
        threading::CErasedPodMsg message;
        message.set_async_slot(slot);
        message.assign_payload(FileSaveResult{ true });
        service.complete(message, worker);
    }
};
AssetResult retain_acquisition_fixture(TTestContext& ctx, SFixture& fixture, const mount_point_ids::id_type dependency = {})
{
    auto owner = CErasedOwner::create<RawAssetTransfer>();
    if (dependency.is_valid()) owner.add_hazard(dependency);
    auto* raw = owner.payload<RawAssetTransfer>();
    TEST_EXPECT(ctx, raw && raw->storage.value.resize(32u, 32u));
    if (raw == nullptr || !raw->storage.value.is_ready()) return {};
    std::memset(raw->storage.value.data(), 0x5a, 32u);
    fixture.request<RawAssetTransfer>(ctx, 10, std::move(owner));
    AssetResult result;
    TEST_EXPECT(ctx, fixture.receive(result, 10) && result.status == EAssetStatus::success && result.asset);
    return result;
}

AssetAcquireResult acquire(TTestContext& ctx, SFixture& fixture, const CAssetId source,
    const EAssetAcquireView view = EAssetAcquireView::bytes, const EAssetStatus status = EAssetStatus::success)
{
    fixture.request_acquire(ctx, { source, view }, 11, fixture.client);
    AssetAcquireResult result;
    TEST_EXPECT(ctx, fixture.receive(result, 11) && result.status == status);
    TEST_EXPECT(ctx, static_cast<bool>(result.acquisition) == (status == EAssetStatus::success));
    return result;
}

void release(TTestContext& ctx, SFixture& fixture, const CAssetAcquisitionId id,
    const EAssetStatus status = EAssetStatus::success, host::SClientRecord* const client = nullptr)
{
    fixture.request_release(ctx, { id }, 12, client ? *client : fixture.client);
    AssetReleaseResult result;
    TEST_EXPECT(ctx, fixture.receive(result, 12, client) && result.acquisition == id && result.status == status);
}

void test_fixed_ordinary_acquisition(TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, tests::allocate_with_failure, tests::deallocate_test_memory, system_ids::host };
    memory::CMemoryContext memory{ allocator, system_ids::host };
    tests::TMemoryContextScope scope{ &memory };
    tests::SCommonEndpointFixture recipient{ module_ids::executable, 701u, 32u };
    recipient.phase = host::EClientPhase::ready;
    CAssetService service;
    TEST_EXPECT(ctx, service.initialise());
    auto owner = CErasedOwner::create<RawAssetTransfer>();
    RawAssetTransfer* const raw = owner.payload<RawAssetTransfer>();
    TEST_EXPECT(ctx, (raw && raw->storage.value.resize(32u, 32u)));
    std::memset(raw->storage.value.data(), 0x5a, 32u);
    threading::CErasedOwnerMsg request;
    request.set_message_type<RawAssetTransfer>();
    request.set_async_slot(1);
    request.set_owner(std::move(owner));
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request(request, recipient);
    }
    service.retry_output(recipient);
    threading::CErasedPodMsg message;
    AssetResult retained;
    TEST_EXPECT(ctx, (recipient.channel->read_preamble_pod(message) && message.copy_payload_to(retained) &&
        (retained.status == EAssetStatus::success)));
    // Prime acquisition storage so publication pressure performs no allocation.
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_acquire({ retained.asset }, 2, recipient);
    }
    AssetAcquireResult primed;
    TEST_EXPECT(ctx, (recipient.channel->read_preamble_pod(message) && message.copy_payload_to(primed)));
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_release({ primed.acquisition }, 3, recipient);
    }
    TEST_EXPECT(ctx, recipient.channel->read_preamble_pod(message));
    const threading::CErasedPodMsg filler = threading::make_pod_message(-1, AssetResult{});
    for (unsigned count = 0u; count < 32u; ++count) TEST_EXPECT(ctx, recipient.post(filler));
    TEST_EXPECT(ctx, !recipient.post(filler));
    const std::size_t attempts = allocation.attempts;
    const std::uint64_t published = recipient.pending_replies->published();
    allocation.fail_on = attempts;
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_acquire({ retained.asset }, 4, recipient);
    }
    TEST_EXPECT(ctx, (!allocation.failed && (allocation.attempts == attempts) && !service.failed() &&
        !recipient.reserved_replies && (recipient.pending_replies->size() == 1u) &&
        (recipient.pending_replies->published() == published)));
    allocation.fail_on = SIZE_MAX;
    unsigned fillers = 0u;
    while (recipient.channel->read_preamble_pod(message))
    {
        TEST_EXPECT(ctx, message.is_payload_a<AssetResult>());
        ++fillers;
    }
    TEST_EXPECT(ctx, (fillers == 32u));
    recipient.flush_replies();
    AssetAcquireResult exposed;
    TEST_EXPECT(ctx, (recipient.channel->read_preamble_pod(message) && message.copy_payload_to(exposed) &&
        (message.query_async_slot() == 4) && exposed.acquisition && (exposed.byte_view().data()[31] == 0x5a)));
    TEST_EXPECT(ctx, (!recipient.channel->read_preamble_pod(message) && recipient.replies_idle()));
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_disposal({ retained.asset }, 5, recipient);
    }
    service.release_client_acquisitions(recipient);
    service.complete_disposals();
    TEST_EXPECT(ctx, (!service.client_idle(recipient) && (exposed.byte_view().data()[0] == 0x5a)));
    {
        host::CRetainedReplies::CDispatch dispatch{ *recipient.pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && recipient.reserve_replies(1u)));
        service.request_release({ exposed.acquisition }, 6, recipient);
    }
    AssetReleaseResult released;
    TEST_EXPECT(ctx, (recipient.channel->read_preamble_pod(message) && message.copy_payload_to(released) &&
        (released.status == EAssetStatus::success)));
    service.complete_disposals();
    service.retry_output(recipient);
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, (recipient.channel->read_preamble_pod(message) && message.copy_payload_to(disposed) &&
        (disposed.status == EAssetStatus::success)));
    TEST_EXPECT(ctx, (service.client_idle(recipient) && service.is_idle() && recipient.replies_idle() && !service.failed()));
    service.deallocate();
}
void test_generic_acquisitions(TTestContext& ctx)
{
    SFixture fixture, other;
    TEST_EXPECT(ctx, fixture.initialise() && other.initialise());
    const auto original = retain_acquisition_fixture(ctx, fixture);
    const auto first = acquire(ctx, fixture, original.asset);
    const auto second = acquire(ctx, fixture, original.asset);
    TEST_EXPECT(ctx, first.acquisition != second.acquisition);
    TEST_EXPECT(ctx, first.byte_view().data() == original.byte_view().data() && first.byte_view().align() == 32u);
    TEST_EXPECT(ctx, fixture.service.work_idle() && !fixture.service.is_idle() && !fixture.service.client_idle(fixture.client));
    TEST_EXPECT(ctx, fixture.service.dependencies_idle(mount_point_ids::executive));
    fixture.service.dispose_dependencies(mount_point_ids::executive);
    TEST_EXPECT(ctx, second.byte_view().data()[31] == 0x5a);
    fixture.service.release_client_acquisitions(fixture.client); // A live client cannot be reclaimed.
    TEST_EXPECT(ctx, !fixture.service.is_idle());
    (void)acquire(ctx, fixture, original.asset, EAssetAcquireView::bundle, EAssetStatus::invalid_request);
    (void)acquire(ctx, fixture, original.asset, static_cast<EAssetAcquireView>(255u), EAssetStatus::invalid_request);
    (void)acquire(ctx, fixture, {}, EAssetAcquireView::bytes, EAssetStatus::invalid_asset);
    release(ctx, fixture, {}, EAssetStatus::invalid_request);
    release(ctx, fixture, first.acquisition, EAssetStatus::invalid_request, &other.client);

    fixture.request_disposal(ctx, { original.asset }, 20, fixture.client);
    fixture.service.complete_disposals();
    (void)acquire(ctx, fixture, original.asset, EAssetAcquireView::bytes, EAssetStatus::invalid_asset);
    TEST_EXPECT(ctx, second.byte_view().data()[31] == 0x5a && fixture.service.work_idle());
    release(ctx, fixture, first.acquisition);
    fixture.service.complete_disposals();
    //  The invalid acquire reply is a FIFO barrier: disposal is still deferred.
    (void)acquire(ctx, fixture, original.asset, EAssetAcquireView::bytes, EAssetStatus::invalid_asset);
    TEST_EXPECT(ctx, second.byte_view().data()[0] == 0x5a);
    release(ctx, fixture, second.acquisition);
    fixture.service.complete_disposals();
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, fixture.receive(disposed, 20) && disposed.status == EAssetStatus::success);
    release(ctx, fixture, second.acquisition, EAssetStatus::invalid_request);
    TEST_EXPECT(ctx, fixture.service.is_idle());

    const auto retained = retain_acquisition_fixture(ctx, fixture);
    const auto third = acquire(ctx, fixture, retained.asset);
    release(ctx, fixture, third.acquisition);
    const auto fourth = acquire(ctx, fixture, retained.asset);
    TEST_EXPECT(ctx, third.acquisition != fourth.acquisition && fourth.byte_view().data()[0] == 0x5a);
    release(ctx, fixture, third.acquisition, EAssetStatus::invalid_request);
    release(ctx, fixture, fourth.acquisition);
    fixture.dispose(ctx, retained.asset);

    const auto dependent = retain_acquisition_fixture(ctx, fixture, mount_point_ids::render);
    const auto protected_dependency = acquire(ctx, fixture, dependent.asset);
    TEST_EXPECT(ctx, !fixture.service.dependencies_idle(mount_point_ids::render));
    TEST_EXPECT(ctx, fixture.service.dependencies_idle(mount_point_ids::executive));
    release(ctx, fixture, protected_dependency.acquisition);
    TEST_EXPECT(ctx, fixture.service.dependencies_idle(mount_point_ids::render));
    fixture.dispose(ctx, dependent.asset);
}

void test_acquired_bundle_closure(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    const auto original = fixture.publish(ctx);
    const auto complete = acquire(ctx, fixture, original.asset, EAssetAcquireView::bundle);
    const auto bytes = acquire(ctx, fixture, original.asset);
    CAssetService::CBundleOperation operation;
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx), 128u, operation) == EAssetStatus::success);
    const auto file = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
    operation.reset();
    const auto acquired_file = acquire(ctx, fixture, file.asset);
    fixture.request_disposal(ctx, { original.asset }, 20, fixture.client);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::closed);
    TEST_EXPECT(ctx, complete.bundle_view().lookup(CStringView{ "raw" }).status == bundle::ELookupStatus::success);
    TEST_EXPECT(ctx, bytes.byte_view().data() == original.byte_view().data());
    TEST_EXPECT(ctx, std::memcmp(acquired_file.byte_view().data(), "abc", 3u) == 0);
    (void)acquire(ctx, fixture, original.asset, EAssetAcquireView::bundle, EAssetStatus::invalid_asset);
    (void)acquire(ctx, fixture, file.asset, EAssetAcquireView::bytes, EAssetStatus::invalid_asset);
    fixture.request_disposal(ctx, { file.asset }, 21, fixture.client);
    release(ctx, fixture, complete.acquisition);
    release(ctx, fixture, bytes.acquisition);
    fixture.service.complete_disposals();
    (void)acquire(ctx, fixture, file.asset, EAssetAcquireView::bytes, EAssetStatus::invalid_asset);
    release(ctx, fixture, acquired_file.acquisition);
    fixture.service.complete_disposals();
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, fixture.receive(disposed, 21) && disposed.status == EAssetStatus::success);
    fixture.notification(ctx, original.asset);
    TEST_EXPECT(ctx, fixture.service.is_idle());
}

void test_acquisition_save_and_client_exit(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    const auto original = retain_acquisition_fixture(ctx, fixture);
    const auto acquired = acquire(ctx, fixture, original.asset);
    fixture.save(ctx, original.asset, 30);
    FileSaveRequest work;
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, tests::wait_until(fixture.conversion, [&]() noexcept { return fixture.worker.read(message); }));
    TEST_EXPECT(ctx, message.copy_payload_to(work));
    const auto work_slot = message.query_async_slot();
    fixture.request_disposal(ctx, { original.asset }, 31, fixture.client);
    release(ctx, fixture, acquired.acquisition);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, !fixture.service.work_idle());
    fixture.complete_save(work_slot);
    AssetResult saved;
    TEST_EXPECT(ctx, fixture.receive(saved, 30) && saved.status == EAssetStatus::success);
    fixture.service.complete_disposals();
    AssetDisposeResult disposed;
    TEST_EXPECT(ctx, fixture.receive(disposed, 31) && disposed.status == EAssetStatus::success);

    const auto retained = retain_acquisition_fixture(ctx, fixture);
    auto client = TInstance<tests::SCommonEndpointFixture>::create(module_ids::executable, next_recipient_identity(), 32u);
    TEST_EXPECT(ctx, static_cast<bool>(client));
    fixture.request_acquire(ctx, { retained.asset }, 40, *client);
    AssetAcquireResult abandoned;
    TEST_EXPECT(ctx, fixture.receive(abandoned, 40, &*client) && abandoned.status == EAssetStatus::success);
    end_recipient(*client);
    fixture.service.detach_client(*client);
    TEST_EXPECT(ctx, fixture.service.is_idle());
    //  Reuse the same recipient address. A previous instance's token stays stale.
    const auto* address = &*client;
    TEST_EXPECT(ctx, client.emplace(module_ids::executable, next_recipient_identity(), 32u) && (&*client == address));
    fixture.request_acquire(ctx, { retained.asset }, 41, *client);
    AssetAcquireResult fresh;
    TEST_EXPECT(ctx, fixture.receive(fresh, 41, &*client) && fresh.status == EAssetStatus::success);
    TEST_EXPECT(ctx, abandoned.acquisition != fresh.acquisition);
    release(ctx, fixture, abandoned.acquisition, EAssetStatus::invalid_request, &*client);
    release(ctx, fixture, fresh.acquisition, EAssetStatus::success, &*client);
    fixture.dispose(ctx, retained.asset);
    end_recipient(*client);
    fixture.service.detach_client(*client);
}

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
    fixture.request_disposal(ctx, { original.asset }, 9, fixture.client);
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
    fixture.request_disposal(ctx, { replacement.asset }, 10, fixture.client);
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
    fixture.request_disposal(ctx, { original.asset }, 60, fixture.client);
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
    fixture.request_disposal(ctx, { replacement.asset }, 61, fixture.client);
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
    TEST_EXPECT(ctx, fixture.receive_work(work, 0) && work.view.size() == original.byte_view().size());
    CAssetService::CBundleOperation operation;
    TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx), 128u, operation) == EAssetStatus::success);
    const auto file = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
    operation.reset();
    {
        tests::TAssertionTestScope assertions(ctx);
        assertions.expect_unconditional_assertion(ctx, [&] { fixture.request_disposal(ctx, { original.asset }, 9, fixture.client); });
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
    TEST_EXPECT(ctx, fixture.receive_work(work, 0) && work.view.size() == 3u && std::memcmp(work.view.data(), "abc", 3u) == 0);
    fixture.request_disposal(ctx, { file.asset }, 24, fixture.client);
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
        fixture.request_disposal(ctx, { original.asset }, 9, fixture.client);
        TEST_EXPECT(ctx, fixture.service.is_idle());
        {
            tests::TAssertionTestScope assertions(ctx);
            assertions.expect_unconditional_assertion(ctx, [&] { fixture.service.dispose_dependencies(mount_point_ids::executive); });
        }
        TEST_EXPECT(ctx, fixture.service.is_idle() && fixture.image.root_state("pack:") == ERootState::closed);
        TEST_EXPECT(ctx, std::memcmp(survivor.byte_view().data(), "abc", 3u) == 0);
        if (detach) fixture.service.detach_client(fixture.client);
        else end_recipient(fixture.client);
        //  Disposal can be requested by a different, still-live client.
        SFixture client;
        TEST_EXPECT(ctx, client.initialise());
        fixture.request_disposal(ctx, { survivor.asset }, 7, client.client);
        fixture.service.complete_disposals();
        AssetDisposeResult disposed;
        TEST_EXPECT(ctx, fixture.receive(disposed, 7, &client.client) && disposed.status == EAssetStatus::success);
        TEST_EXPECT(ctx, fixture.image.root_state("pack:") == ERootState::missing && !fixture.service.failed());
        if (detach)
        {
            fixture.request_disposal(ctx, { original.asset }, 8, fixture.client);
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
    owner.request_disposal(ctx, { original.asset }, 40, borrower.client);
    AssetDisposeResult rejected;
    TEST_EXPECT(ctx, owner.receive(rejected, 40, &borrower.client) && rejected.asset == original.asset &&
        rejected.status == EAssetStatus::invalid_request);
    owner.service.complete_disposals();
    TEST_EXPECT(ctx, owner.image.root_state("pack:") == ERootState::open);
    owner.request_reference(ctx, { original.asset }, 41, borrower.client);
    AssetResult reference;
    TEST_EXPECT(ctx, owner.receive(reference, 41, &borrower.client) && reference.status == EAssetStatus::success && reference.asset);
    TEST_EXPECT(ctx, reference.bundle_view().lookup(CStringView{ "raw" }).bytes.bytes() == 3u);
    owner.request_disposal(ctx, { reference.asset }, 42, borrower.client);
    owner.service.complete_disposals();
    TEST_EXPECT(ctx, owner.receive(rejected, 42, &borrower.client) && rejected.status == EAssetStatus::success);
    CAssetService::CBundleOperation operation;
    TEST_EXPECT(ctx, owner.service.begin_bundle_file(owner.resolve(ctx), 128u, operation) == EAssetStatus::success);
    const auto file = owner.service.retain_bundle_file(operation, EAssetFileFormat::raw);
    TEST_EXPECT(ctx, file.asset);
    operation.reset();
    owner.request_disposal(ctx, { original.asset }, 43, owner.client);
    owner.service.complete_disposals();
    TEST_EXPECT(ctx, owner.image.root_state("pack:") == ERootState::closed);
    TEST_EXPECT(ctx, std::memcmp(file.byte_view().data(), "abc", 3u) == 0);
    owner.request_disposal(ctx, { file.asset }, 44, borrower.client);
    owner.service.complete_disposals();
    TEST_EXPECT(ctx, owner.receive(rejected, 44, &borrower.client) && rejected.status == EAssetStatus::success);
    owner.notification(ctx, original.asset);
    //  A reply on the borrower's FIFO proves it did not receive the notification.
    owner.request_reference(ctx, { original.asset }, 45, borrower.client);
    TEST_EXPECT(ctx, owner.receive(reference, 45, &borrower.client) && reference.status == EAssetStatus::invalid_asset);
    TEST_EXPECT(ctx, !owner.service.failed());
}

void test_recipient_reuse(TTestContext& ctx)
{
    for (bool close : { false, true })
    {
        SFixture fixture;
        TEST_EXPECT(ctx, fixture.initialise());
        auto package = TInstance<tests::SCommonEndpointFixture>::create(module_ids::executable, next_recipient_identity(), 32u);
        TEST_EXPECT(ctx, static_cast<bool>(package));
        const auto original = fixture.publish(ctx, &*package);
        CAssetService::CBundleOperation operation;
        TEST_EXPECT(ctx, fixture.service.begin_bundle_file(fixture.resolve(ctx), 128u, operation) == EAssetStatus::success);
        const auto file = fixture.service.retain_bundle_file(operation, EAssetFileFormat::raw);
        TEST_EXPECT(ctx, file.asset);
        operation.reset();
        if (close) fixture.request_disposal(ctx, { original.asset }, 50, *package);
        end_recipient(*package);
        fixture.service.detach_client(*package);
        //  Reuse the exact recipient address and role, as a collection slot can do.
        const auto* address = &*package;
        TEST_EXPECT(ctx, package.emplace(module_ids::executable, next_recipient_identity(), 32u) && (&*package == address));
        fixture.request_disposal(ctx, { original.asset }, 51, *package);
        AssetDisposeResult rejected;
        TEST_EXPECT(ctx, fixture.receive(rejected, 51, &*package) &&
            rejected.status == (close ? EAssetStatus::invalid_asset : EAssetStatus::invalid_request));
        fixture.dispose(ctx, file.asset);
        //  An open orphan remains for full shutdown; detach does not transfer or close it.
        TEST_EXPECT(ctx, fixture.image.root_state("pack:") == (close ? ERootState::missing : ERootState::open));
        fixture.request_disposal(ctx, { original.asset }, 52, *package);
        TEST_EXPECT(ctx, fixture.receive(rejected, 52, &*package) &&
            rejected.status == (close ? EAssetStatus::invalid_asset : EAssetStatus::invalid_request));
        TEST_EXPECT(ctx, !fixture.service.failed());
        end_recipient(*package);
        fixture.service.detach_client(*package);
    }
}

void test_conversion_lifetime(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    const auto source = fixture.publish(ctx);
    const auto borrowed = acquire(ctx, fixture, source.asset);
    fixture.request_disposal(ctx, { source.asset }, 31, fixture.client);
    (void)acquire(ctx, fixture, source.asset, EAssetAcquireView::bytes, EAssetStatus::invalid_asset);
    CByteBuffer independent;
    TEST_EXPECT(ctx, independent.resize(borrowed.byte_view().size()));
    std::memcpy(independent.data(), borrowed.byte_view().data(), independent.size());
    release(ctx, fixture, borrowed.acquisition);
    fixture.service.complete_disposals();
    fixture.notification(ctx, source.asset);
    TEST_EXPECT(ctx, fixture.service.is_idle() && independent.size() != 0u);
}

void test_live_notification_pressure(TTestContext& ctx)
{
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    tests::SCommonEndpointFixture recipient{ module_ids::executable, next_recipient_identity(), 32u };
    const AssetResult original = fixture.publish(ctx, &recipient);
    const threading::CErasedPodMsg filler = threading::make_pod_message(90, AssetDisposeResult{});
    for (unsigned count = 0u; count < 32u; ++count) TEST_EXPECT(ctx, recipient.post(filler));
    TEST_EXPECT(ctx, !recipient.post(filler));
    const std::uint64_t published = recipient.pending_replies->published();
    fixture.request_disposal(ctx, { original.asset }, 31, recipient);
    fixture.progress(recipient);
    TEST_EXPECT(ctx, (!fixture.service.failed() && fixture.service.bundle_output_ready(recipient) &&
        (recipient.reserved_replies == 1u) && !recipient.pending_replies->reservations() &&
        (recipient.pending_replies->published() == published) && (fixture.image.root_state("pack:") == ERootState::missing)));
    threading::CErasedPodMsg message;
    unsigned fillers = 0u;
    while (recipient.channel->read_preamble_pod(message))
    {
        ++fillers;
        TEST_EXPECT(ctx, (message.query_async_slot() == 90));
    }
    TEST_EXPECT(ctx, (fillers == 32u));
    BundleDisposedNotification result;
    TEST_EXPECT(ctx, (fixture.receive(result, -1, &recipient) && (result.asset == original.asset) && recipient.replies_idle()));
    fixture.request_disposal(ctx, { original.asset }, 32, recipient);
    AssetDisposeResult rejected;
    TEST_EXPECT(ctx, (fixture.receive(rejected, 32, &recipient) && (rejected.status == EAssetStatus::invalid_asset)));
    TEST_EXPECT(ctx, (!recipient.channel->read_preamble_pod(message) && !fixture.service.bundle_output_ready(recipient)));
    end_recipient(recipient);
    fixture.service.detach_client(recipient);
}
void test_acquisition_failure(TTestContext& ctx)
{
    bool succeeded = false;
    std::size_t failures = 0u;
    for (std::size_t attempt = 1u; attempt < 32u && !succeeded; ++attempt)
    {
        tests::TAllocationFailureFixture state;
        memory::CMemoryAllocator allocator{ &state, &tests::allocate_with_failure, &tests::deallocate_test_memory, system_ids::host };
        memory::CMemoryContext memory{ allocator, system_ids::host };
        {
            tests::TMemoryContextScope scope{ &memory };
            SFixture fixture;
            TEST_EXPECT(ctx, fixture.initialise());
            const auto original = fixture.publish(ctx);
            state.fail_on = state.attempts + attempt - 1u;
            fixture.request_acquire(ctx, { original.asset, EAssetAcquireView::bundle }, 40, fixture.client);
            state.fail_on = SIZE_MAX;
            AssetAcquireResult result;
            TEST_EXPECT(ctx, fixture.receive(result, 40));
            succeeded = result.status == EAssetStatus::success;
            if (succeeded) release(ctx, fixture, result.acquisition);
            else
            {
                ++failures;
                TEST_EXPECT(ctx, result.status == EAssetStatus::allocation_failed && !result.acquisition && !result.bundle_view().is_ready());
            }
            TEST_EXPECT(ctx, fixture.service.is_idle() && !fixture.service.failed());
            fixture.request_disposal(ctx, { original.asset }, 41, fixture.client);
            fixture.service.complete_disposals();
            fixture.notification(ctx, original.asset);
        }
        TEST_EXPECT(ctx, memory.is_attribution_empty());
    }
    TEST_EXPECT(ctx, succeeded && failures != 0u);

    SFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    const auto original = fixture.publish(ctx);
    tests::SCommonEndpointFixture recipient{ module_ids::executable, next_recipient_identity(), 32u };
    const threading::CErasedPodMsg filler = threading::make_pod_message(90, AssetReleaseResult{});
    for (unsigned count = 0u; count < 32u; ++count) TEST_EXPECT(ctx, recipient.post(filler));
    TEST_EXPECT(ctx, !recipient.post(filler));
    fixture.request_acquire(ctx, { original.asset, EAssetAcquireView::bundle }, 42, recipient);
    TEST_EXPECT(ctx, (!fixture.service.failed() && !fixture.service.is_idle() &&
        (recipient.pending_replies->size() == 1u) && !recipient.reserved_replies));
    fixture.request_disposal(ctx, { original.asset }, 43, fixture.client);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, (fixture.image.root_state("pack:") == ERootState::closed));
    const std::uint64_t published = recipient.pending_replies->published();
    recipient.phase = host::EClientPhase::stopped;
    fixture.service.release_client_acquisitions(recipient);
    // For raw/bundle views the existing no-execution/no-batch proof can cancel
    // acquisitions now. The unread result still cannot publish after termination,
    // and its fallback storage waits for the final ingress-quiescence proof.
    TEST_EXPECT(ctx, (fixture.service.client_idle(recipient) && !recipient.execution_quiescent() &&
        (recipient.pending_replies->size() == 1u)));
    fixture.notification(ctx, original.asset);
    recipient.flush_replies();
    TEST_EXPECT(ctx, ((recipient.pending_replies->published() == published) && (recipient.pending_replies->size() == 1u)));
    end_recipient(recipient);
    fixture.service.detach_client(recipient);
    TEST_EXPECT(ctx, (fixture.service.is_idle() && !fixture.service.failed() && recipient.replies_idle()));
}

void test_publication_failure_and_shutdown(TTestContext& ctx)
{
    bool succeeded = false;
    std::size_t failures = 0u;
    for (std::size_t attempt = 1u; attempt < 500u && !succeeded; ++attempt)
    {
        tests::TAllocationFailureFixture allocator_state;
        memory::CMemoryAllocator allocator{ &allocator_state, &tests::allocate_with_failure, &tests::deallocate_test_memory, system_ids::host };
        memory::CMemoryContext context{ allocator, system_ids::host };
        {
            tests::TMemoryContextScope scope{ &context };
            filesystem_image::CImage image;
            CAssetService service;
            CLiveDocument doc;
            TEST_EXPECT(ctx, tests::parse_document(R"({"roots":{}})", doc) && image.adopt(std::move(doc)) && service.initialise());
            service.set_filesystem(image);
            BundlePreparation prepared;
            TEST_EXPECT(ctx, make_bundle(prepared.block));
            const auto* bytes = prepared.block.buffer_bytes().data();
            TEST_EXPECT(ctx, prepared.view.reset(bytes, prepared.block.buffer_bytes().size()).succeeded());
            TEST_EXPECT(ctx, prepared.directory.prepare(prepared.view));
            std::uint64_t binding = 0u;
            TEST_EXPECT(ctx, image.reserve_bundle("pack:", binding));
            tests::SCommonEndpointFixture client{ module_ids::executable, next_recipient_identity(), 32u };
            allocator_state.fail_on = allocator_state.attempts + attempt - 1u;
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
    test_fixed_ordinary_acquisition(ctx);
    test_generic_acquisitions(ctx);
    test_acquired_bundle_closure(ctx);
    test_acquisition_save_and_client_exit(ctx);
    test_acquisition_failure(ctx);
    test_closure_and_retained_records(ctx);
    test_resolved_ranges(ctx);
    test_complete_views_and_saves(ctx);
    test_dependency_cleanup_and_recipient_exit(ctx);
    test_management_owner(ctx);
    test_recipient_reuse(ctx);
    test_conversion_lifetime(ctx);
    test_live_notification_pressure(ctx);
    test_publication_failure_and_shutdown(ctx);
    return ctx.report("BundleService");
}
