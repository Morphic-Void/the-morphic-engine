
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ErasedOwner_test_suite.cpp
//  Author:  OpenAI Codex
//  Date:    12 Aug 26

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "containers/TInstance.hpp"
#include "debug/macros.hpp"
#include "debug/service.hpp"
#include "data_model/document_translation.hpp"
#include "module/bound_module.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/environment/local_type_ids.hpp"
#include "tests/environment/test_paths.hpp"
#include "memory/memory_context.hpp"
#include "platform/path/native_path.hpp"
#include "platform/threading/processor_relax.hpp"
#include "system/erased_owner.hpp"
#include "system/erased_owner_transport.hpp"
#include "system/transported_types.hpp"
#include "tests/test_suites/ErasedOwner_test_suite.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/memory_context_test_access.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/file_helpers.hpp"
#include "tests/support/test_scopes.hpp"
#include "threading/messages/CErasedMessageTransports.hpp"
#include "threading/CThreadPackage.hpp"
#include "threading/transports/TOwningTransport.hpp"

namespace executive_startup_tests
{

static bool read_request(threading::CThreadPackage& package, threading::CErasedOwnerMsg& message,
    const platform::system::CPerfCountConversion& conversion) noexcept
{
    platform::system::CPerfCounter timer;
    (void)timer.update();
    while (timer.query_delta() < (conversion.query_ticks_per_second() * 5u))
    {
        if (package.read(message))
        {
            return true;
        }
        platform::threading::processor_relax();
    }
    return false;
}

static void check_rendering_notices(tests::TTestContext& ctx, modules::CBoundModule& module,
    const platform::threading::FThreadEntry entry, const platform::system::CPerfCountConversion& conversion)
{
    enum class ECase : std::uint8_t
    {
        loaded = 0, already_loaded, unavailable, wrong_implementation, load_failure,
        missing_acknowledgement, wrong_correlation, duplicate_acknowledgement
    };
    for (const ECase scenario : { ECase::loaded, ECase::already_loaded, ECase::unavailable,
        ECase::wrong_implementation, ECase::load_failure, ECase::missing_acknowledgement,
        ECase::wrong_correlation, ECase::duplicate_acknowledgement })
    {
        const threading::ThreadConfig configuration{
            thread_ids::executive, module_ids::executive, platform::threading::EThreadPriority::Normal,
            entry, &modules::CBoundModule::prepare_thread, &module };
        threading::CThreadPackage package(configuration, conversion);
        const bool started = package.startup();
        TEST_EXPECT(ctx, started);
        if (!started)
        {
            continue;
        }

        threading::CErasedOwnerMsg request;
        const bool received = read_request(package, request, conversion);
        TEST_EXPECT(ctx, received);
        const ModuleRequest* const load = request.owner().payload<ModuleRequest>();
        TEST_EXPECT(ctx, (load != nullptr) && (load->action == EModuleAction::load) &&
            (load->module == module_ids::render_vulkan_windows) && (load->file.length() != 0u) &&
            (std::strcmp(load->file.cstring(), "package:/bin/MorphicRendering.dll") == 0));
        const std::int32_t slot = request.query_async_slot();
        request.take_owner().destroy();

        ModuleResult result;
        result.module = module_ids::render_vulkan_windows;
        result.notice = EModuleNotice::acknowledged;
        result.status = EModuleStatus::success;
        threading::CErasedPodMsg reply;
        reply.set_async_slot(slot);
        if (scenario != ECase::missing_acknowledgement)
        {
            reply.assign_payload(result);
            TEST_EXPECT(ctx, package.post(reply));
        }
        result.notice = (scenario == ECase::duplicate_acknowledgement) ?
            EModuleNotice::acknowledged : EModuleNotice::completed;
        result.status = (scenario == ECase::load_failure) ? EModuleStatus::binding_failed :
            (((scenario == ECase::loaded) || (scenario == ECase::duplicate_acknowledgement)) ?
                EModuleStatus::success : EModuleStatus::already_loaded);
        result.available = (scenario != ECase::unavailable) && (scenario != ECase::load_failure);
        if (scenario == ECase::wrong_implementation)
        {
            result.module = module_ids::render_vulkan_linux;
        }
        if (scenario == ECase::wrong_correlation)
        {
            reply.set_async_slot(slot + 1);
        }
        reply.assign_payload(result);
        TEST_EXPECT(ctx, package.post(reply));
        TEST_EXPECT(ctx, read_request(package, request, conversion));
        if ((scenario == ECase::loaded) || (scenario == ECase::already_loaded))
        {
            TEST_EXPECT(ctx, request.is_message_a<RawAssetTransfer>());
            TEST_EXPECT(ctx, request.query_async_slot() == slot + 1);
        }
        else
        {
            const ModuleRequest* const shutdown = request.owner().payload<ModuleRequest>();
            TEST_EXPECT(ctx, (shutdown != nullptr) && (shutdown->action == EModuleAction::unload) &&
                (shutdown->module == module_ids::executive));
        }
        request.take_owner().destroy();
        package.request_exit();
        TEST_EXPECT(ctx, package.shutdown());
    }
}

}   //  namespace executive_startup_tests

namespace
{

using TTestContext = tests::TTestContext;
using TAllocatorState = tests::TAllocatorFixture;
using tests::allocate_test_memory;
using tests::deallocate_test_memory;
using tests::TMemoryContextScope;
using tests::TModuleIdScope;

struct SExecutiveSubmissionFailure
{
    modules::CBoundModule* module{ nullptr };
    platform::threading::FThreadEntry entry{ nullptr };
    threading::EThreadRunState final_state{ threading::EThreadRunState::Empty };
    std::uint32_t failure_code{ 0u };
    std::uint32_t exit_code{ 0u };
};

static bool MV_STD_ABI_CALL prepare_executive_without_outbound_queue(void* const context,
    const thread_ids::id_type thread_id, void* const provisioning) noexcept
{
    SExecutiveSubmissionFailure& test = *static_cast<SExecutiveSubmissionFailure*>(context);
    threading::CThreadResources& resources = *static_cast<threading::CThreadResources*>(provisioning);
    //  Fail the first post without changing the production Executive entry point.
    resources.worker_to_host_owned_msgs.deallocate();
    return modules::CBoundModule::prepare_thread(test.module, thread_id, provisioning);
}

static std::uint32_t MV_STD_ABI_CALL capture_executive_submission_failure(void* const provisioning) noexcept
{
    threading::CThreadResources& resources = *static_cast<threading::CThreadResources*>(provisioning);
    SExecutiveSubmissionFailure& test = *static_cast<SExecutiveSubmissionFailure*>(resources.config.prepare_context);
    test.exit_code = test.entry(provisioning);
    //  Capture before the Host-side startup rejection cleans up the package.
    test.final_state = resources.control_state.query_state();
    test.failure_code = resources.control_state.query_failure_code();
    return test.exit_code;
}

void test_registration_and_empty_state(TTestContext& ctx)
{
    static_assert(k_system_type_id_v<CByteBuffer> == system_type_ids::byte_buffer);
    static_assert(k_system_type_id_v<CByteRectBuffer> == system_type_ids::byte_rect_buffer);
    static_assert(k_system_type_id_v<CSimpleString> == system_type_ids::simple_string);
    static_assert(k_system_type_id_v<CStringBuffer> == system_type_ids::string_buffer);
    static_assert(k_system_type_id_v<CStableStrings> == system_type_ids::stable_strings);
    static_assert(k_is_erased_owner_payload_v<LoadedFile>);
    static_assert(k_is_erased_owner_payload_v<AssetLoadRequest>);
    static_assert(k_is_erased_owner_payload_v<AssetSaveRequest>);
    static_assert(k_is_erased_owner_payload_v<test_environment::STestAssetLoadState>);
    static_assert(!k_is_erased_owner_payload_v<FileLoadRequest>);
    static_assert(k_type_id_v<test_environment::STestAssetLoadState>.is_local());
    static_assert(k_type_id_v<test_environment::STestAssetLoadState>.is_valid());
    static_assert(std::is_nothrow_default_constructible_v<test_environment::STestAssetLoadState>);
    static_assert(std::is_nothrow_move_constructible_v<test_environment::STestAssetLoadState>);
    static_assert(std::is_nothrow_move_assignable_v<test_environment::STestAssetLoadState>);
    static_assert(std::is_nothrow_destructible_v<test_environment::STestAssetLoadState>);
    static_assert(std::is_same_v<decltype(CErasedOwner{}.query_type_id()), type_id>);

    CErasedOwner owner;
    const CErasedOwner& const_owner = owner;

    TEST_EXPECT(ctx, owner.is_empty());
    TEST_EXPECT(ctx, !owner.is_ready());
    TEST_EXPECT(ctx, !owner);
    TEST_EXPECT(ctx, owner.query_type_id() == type_ids::undefined);
    TEST_EXPECT(ctx, owner.payload<LoadedFile>() == nullptr);
    TEST_EXPECT(ctx, const_owner.payload<LoadedFile>() == nullptr);
    TEST_EXPECT(ctx, !owner.has_any_hazard());
    TEST_EXPECT(ctx, owner.hazard_mask() == 0u);

    owner.destroy();
    TEST_EXPECT(ctx, owner.is_empty());
}

void test_operation_registry(TTestContext& ctx)
{
    const erased_owner_operations::SRegistryView expected_view{
        erased_owner_operations::system_operations_view(),
        erased_owner_operations::local_operations_view()
    };
    TEST_EXPECT(ctx, erased_owner_operations::validate_view(expected_view));
    TEST_EXPECT(ctx, erased_owner_operations::view_is_installed());
    TEST_EXPECT(ctx, erased_owner_operations::installed_view() != nullptr);
    TEST_EXPECT(ctx, !erased_owner_operations::install_view(expected_view));

    const erased_owner_operations::SRegistration* const loaded_file =
        erased_owner_operations::find(k_type_id_v<LoadedFile>);
    const erased_owner_operations::SRegistration* const encoded_tga =
        erased_owner_operations::find(k_type_id_v<EncodedTga>);
    const erased_owner_operations::SRegistration* const decoded_tga =
        erased_owner_operations::find(k_type_id_v<DecodedTga>);
    const erased_owner_operations::SRegistration* const asset_load_request =
        erased_owner_operations::find(k_type_id_v<AssetLoadRequest>);
    const erased_owner_operations::SRegistration* const asset_save_request =
        erased_owner_operations::find(k_type_id_v<AssetSaveRequest>);
    const erased_owner_operations::SRegistration* const host_file_load =
        erased_owner_operations::find(k_type_id_v<test_environment::STestAssetLoadState>);
    TEST_EXPECT(ctx, (loaded_file != nullptr) &&
        loaded_file->operations.is_complete());
    TEST_EXPECT(ctx, (encoded_tga != nullptr) &&
        encoded_tga->operations.is_complete());
    TEST_EXPECT(ctx, (decoded_tga != nullptr) &&
        decoded_tga->operations.is_complete());
    TEST_EXPECT(ctx, (asset_load_request != nullptr) &&
        asset_load_request->operations.is_complete());
    TEST_EXPECT(ctx, (asset_save_request != nullptr) &&
        asset_save_request->operations.is_complete());
    TEST_EXPECT(ctx, (host_file_load != nullptr) &&
        host_file_load->operations.is_complete());
    TEST_EXPECT(ctx,
        erased_owner_operations::find(k_type_id_v<FileLoadRequest>) == nullptr);
    TEST_EXPECT(ctx,
        erased_owner_operations::find(k_type_id_v<test_environment::CTestRuntime>) == nullptr);
    TEST_EXPECT(ctx,
        erased_owner_operations::find(type_ids::undefined) == nullptr);
}

void test_operation_registry_failure_boundaries(TTestContext& ctx)
{
    std::array<erased_owner_operations::SRegistration,
        local_type_ids::k_count> missing_local_operations{};
    const erased_owner_operations::SRegistryView missing_view{
        erased_owner_operations::system_operations_view(),
        { missing_local_operations.data(),
            static_cast<std::uint32_t>(missing_local_operations.size()) }
    };
    TEST_EXPECT(ctx, erased_owner_operations::validate_view(missing_view));
    TEST_EXPECT(ctx, erased_owner_operations::find(
        &missing_view, k_type_id_v<test_environment::STestAssetLoadState>) == nullptr);
    TEST_EXPECT(ctx, erased_owner_operations::find(
        &missing_view, k_type_id_v<LoadedFile>) != nullptr);

    auto incomplete_local_operations = missing_local_operations;
    const std::uint32_t local_index = local_type_ids::ops::decode_index(
        local_type_ids::ops::decode_id(
            k_local_type_id_v<test_environment::STestAssetLoadState>));
    incomplete_local_operations[local_index].identity =
        k_type_id_v<test_environment::STestAssetLoadState>;
    const erased_owner_operations::SRegistryView incomplete_view{
        erased_owner_operations::system_operations_view(),
        { incomplete_local_operations.data(),
            static_cast<std::uint32_t>(incomplete_local_operations.size()) }
    };
    TEST_EXPECT(ctx, !erased_owner_operations::validate_view(incomplete_view));
    TEST_EXPECT(ctx, erased_owner_operations::find(
        &incomplete_view, k_type_id_v<test_environment::STestAssetLoadState>) == nullptr);

    TEST_EXPECT(ctx, erased_owner_operations::view_is_installed());
    TEST_EXPECT(ctx, erased_owner_operations::find(
        k_type_id_v<test_environment::STestAssetLoadState>) != nullptr);
}

void test_executive_context_and_module_unload_gate(TTestContext& ctx)
{
    memory::CMemoryContext* const host_context = test_environment::executable_memory_context();
    memory::CMemoryContext* const executive_context =
        test_environment::executive_memory_context();
    TEST_EXPECT(ctx, host_context != nullptr);
    TEST_EXPECT(ctx, executive_context != nullptr);
    TEST_EXPECT(ctx, executive_context != host_context);
    TEST_EXPECT(ctx, host_context->get_system_id() == system_ids::host);
    TEST_EXPECT(ctx,
        executive_context->get_system_id() == system_ids::executive);
    TEST_EXPECT(ctx, host_context->is_compatible_with(*executive_context));
    TEST_EXPECT(ctx, host_context->belongs_to_module(module_ids::executable));
    TEST_EXPECT(ctx, !host_context->belongs_to_module(module_ids::executive));
    TEST_EXPECT(ctx, executive_context->belongs_to_module(module_ids::executive));
    TEST_EXPECT(ctx, !executive_context->belongs_to_module(module_ids::executable));
    TEST_EXPECT(ctx, executive_context->is_attribution_empty());

    TInstance<debug_system::CDebugServiceState> debug_service =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, debug_service.is_ready());
    const std::string submission_log = test_environment::test_log_path("executive_submission_failure");
    const std::string submission_direct_log = test_environment::test_log_path("executive_submission_failure_direct");
    if (debug_service)
    {
        TEST_EXPECT(ctx, debug_service->configure_log_paths(submission_log.c_str(), submission_direct_log.c_str()));
        TEST_EXPECT(ctx, debug_service->open_logs());
    }

    constexpr modules::SAdvertisedIdentity host_identity{
        module_ids::executable,
        { modules::k_binding_abi_major, 0u },
        modules::k_binding_abi_major,
        modules::k_binding_abi_major
    };
    const std::string module_filename =
        test_environment::binary_path("MorphicExecutive.dll");
    const platform::path::NativePath module_path =
        platform::path::makeNativePath(module_filename.c_str());
    modules::CBoundModule module;
    const bool bound = module_path.is_ready() && module.bind(
        module_path, module_ids::executive, host_identity);
    TEST_EXPECT(ctx, bound);
    if (!bound || !debug_service)
    {
        return;
    }

    TEST_EXPECT(ctx, !module.install(
        test_environment::system_registry_view(), module_ids::executive,
        host_context, debug_service.operator->()));
    TEST_EXPECT(ctx, module.install(
        test_environment::system_registry_view(), module_ids::executive,
        executive_context, debug_service.operator->()));
    TEST_EXPECT(ctx, module.is_ready());

    constexpr std::size_t allocation_alignment = alignof(std::max_align_t);
    constexpr std::size_t allocation_size = 64u;
    {
        modules::FModuleFunction function = nullptr;
        const bool found = module.query_function(system_type_ids::executive_thread_function, function);
        TEST_EXPECT(ctx, found);
        platform::system::CPerfCountConversion conversion;
        const bool clock_ready = conversion.init();
        TEST_EXPECT(ctx, clock_ready);
        if (found && clock_ready)
        {
            TEST_EXPECT(ctx, debug_service->start());
            SExecutiveSubmissionFailure failure;
            failure.module = &module;
            failure.entry = reinterpret_cast<platform::threading::FThreadEntry>(function);
            threading::ThreadConfig config;
            config.thread_id = thread_ids::executive;
            config.worker_module_id = module_ids::executive;
            config.entry_point = &capture_executive_submission_failure;
            config.prepare = &prepare_executive_without_outbound_queue;
            config.prepare_context = &failure;
            threading::CThreadPackage package(config, conversion);
            //  The real Host-side package must reject the failed Executive startup.
            TEST_EXPECT(ctx, !package.startup());
            TEST_EXPECT(ctx, failure.final_state == threading::EThreadRunState::Failed);
            TEST_EXPECT(ctx, failure.failure_code != 0u);
            TEST_EXPECT(ctx, failure.exit_code == failure.failure_code);
            TEST_EXPECT(ctx, executive_context->is_attribution_empty());
            executive_startup_tests::check_rendering_notices(ctx, module, failure.entry, conversion);
            TEST_EXPECT(ctx, executive_context->is_attribution_empty());
            TEST_EXPECT(ctx, debug_service->stop());
            TEST_EXPECT(ctx, tests::file_contains(submission_log.c_str(), "rendering_startup failed: rendering load submission"));
        }
    }
    void* const allocation = executive_context->allocate(
        allocation_alignment, allocation_size);
    TEST_EXPECT(ctx, allocation != nullptr);
    TEST_EXPECT(ctx, executive_context->get_live_allocation_count() == 1u);
    TEST_EXPECT(ctx, executive_context->get_live_allocated_bytes() != 0u);
    TEST_EXPECT(ctx, !executive_context->is_attribution_empty());
    TEST_EXPECT(ctx, !module.unbind());
    TEST_EXPECT(ctx, module.is_ready());

    executive_context->deallocate(
        allocation_alignment, allocation_size, allocation);
    TEST_EXPECT(ctx, executive_context->is_attribution_empty());
    TEST_EXPECT(ctx, module.unbind());
    TEST_EXPECT(ctx, !module.is_ready());
}

void test_local_creation_moves_and_destruction(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const TModuleIdScope module_scope(module_ids::executable);
    const TMemoryContextScope context_scope(&context);

    {
        CErasedOwner source =
            CErasedOwner::create<test_environment::STestAssetLoadState>();
        test_environment::STestAssetLoadState* const payload =
            source.payload<test_environment::STestAssetLoadState>();
        TEST_EXPECT(ctx, source.is_ready());
        TEST_EXPECT(ctx, source.query_type_id() ==
            k_type_id_v<test_environment::STestAssetLoadState>);
        TEST_EXPECT(ctx, payload != nullptr);
        TEST_EXPECT(ctx, source.payload<LoadedFile>() == nullptr);
        TEST_EXPECT(ctx, context.get_live_allocation_count() == 1u);

        payload->executive_slot = 41;
        source.add_hazard(mount_point_ids::asset);

        CErasedOwner moved{ std::move(source) };
        TEST_EXPECT(ctx, source.is_empty());
        TEST_EXPECT(ctx, moved.payload<test_environment::STestAssetLoadState>() == payload);
        TEST_EXPECT(ctx, moved.has_hazard(mount_point_ids::asset));

        CErasedOwner destination =
            CErasedOwner::create<test_environment::STestAssetLoadState>();
        TEST_EXPECT(ctx, context.get_live_allocation_count() == 2u);
        destination = std::move(moved);
        TEST_EXPECT(ctx, moved.is_empty());
        TEST_EXPECT(ctx, destination.payload<test_environment::STestAssetLoadState>() == payload);
        const CErasedOwner& const_destination = destination;
        const test_environment::STestAssetLoadState* const const_payload =
            const_destination.payload<test_environment::STestAssetLoadState>();
        TEST_EXPECT(ctx, const_payload != nullptr);
        TEST_EXPECT(ctx, const_payload->executive_slot == 41);
        TEST_EXPECT(ctx, !const_payload->request);
        TEST_EXPECT(ctx, context.get_live_allocation_count() == 1u);
    }

    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);
}

void test_local_context_boundaries(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext source_context(allocator, system_ids::host);
    memory::CMemoryContext target_context(allocator, system_ids::host);
    memory::CMemoryContext executive_context(allocator, system_ids::executive);
    const TModuleIdScope module_scope(module_ids::executable);
    const TMemoryContextScope context_scope(&source_context);

    CErasedOwner owner = CErasedOwner::create<test_environment::STestAssetLoadState>();
    test_environment::STestAssetLoadState* const payload =
        owner.payload<test_environment::STestAssetLoadState>();
    payload->executive_slot = 72;
    owner.add_hazard(mount_point_ids::conditioning);

    const std::uint32_t source_count = source_context.get_live_allocation_count();
    const std::uint64_t source_bytes = source_context.get_live_allocated_bytes();
    TEST_EXPECT(ctx, owner.can_reattribute_to(&target_context));
    TEST_EXPECT(ctx, owner.reattribute(&target_context));
    TEST_EXPECT(ctx, owner.memory_context() == &target_context);
    TEST_EXPECT(ctx, owner.payload<test_environment::STestAssetLoadState>() == payload);
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == source_count);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == source_bytes);

    const std::uint32_t target_count = target_context.get_live_allocation_count();
    const std::uint64_t target_bytes = target_context.get_live_allocated_bytes();
    TEST_EXPECT(ctx, !owner.can_reattribute_to(&executive_context));
    TEST_EXPECT(ctx, !owner.reattribute(&executive_context));
    TEST_EXPECT(ctx, owner.is_ready());
    TEST_EXPECT(ctx, owner.memory_context() == &target_context);
    TEST_EXPECT(ctx, owner.payload<test_environment::STestAssetLoadState>() == payload);
    TEST_EXPECT(ctx, payload->executive_slot == 72);
    TEST_EXPECT(ctx, !payload->request);
    TEST_EXPECT(ctx, owner.has_hazard(mount_point_ids::conditioning));
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == target_count);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == target_bytes);
    TEST_EXPECT(ctx, executive_context.get_live_allocation_count() == 0u);

    CErasedOwner wrong_component =
        CErasedOwner::create<test_environment::STestAssetLoadState>(&executive_context);
    TEST_EXPECT(ctx, wrong_component.is_empty());
    TEST_EXPECT(ctx, wrong_component.query_type_id() == type_ids::undefined);
    TEST_EXPECT(ctx, executive_context.get_live_allocation_count() == 0u);

    owner.destroy();
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == 0u);
}

void test_creation_accounting_and_destruction(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const TMemoryContextScope context_scope(&context);

    {
        CErasedOwner owner = CErasedOwner::create<LoadedFile>();
        LoadedFile* const payload = owner.payload<LoadedFile>();

        TEST_EXPECT(ctx, owner.is_ready());
        TEST_EXPECT(ctx, owner.query_type_id() == k_type_id_v<LoadedFile>);
        TEST_EXPECT(ctx, payload != nullptr);
        TEST_EXPECT(ctx, owner.payload<EncodedTga>() == nullptr);
        TEST_EXPECT(ctx, context.get_live_allocation_count() == 1u);

        TEST_EXPECT(ctx, payload->buffer.allocate(64u, 16u));
        TEST_EXPECT(ctx, context.get_live_allocation_count() == 2u);
    }

    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocated_bytes() == 0u);
}

void test_moves_and_hazards(TTestContext& ctx)
{
    CErasedOwner source = CErasedOwner::create<LoadedFile>();
    LoadedFile* const original_payload = source.payload<LoadedFile>();
    TEST_EXPECT(ctx, original_payload->buffer.allocate(29u));
    source.add_hazard(mount_point_ids::executable);
    source.add_hazard(mount_point_ids::render);

    CErasedOwner moved{ std::move(source) };
    TEST_EXPECT(ctx, source.is_empty());
    TEST_EXPECT(ctx, !source.has_any_hazard());
    TEST_EXPECT(ctx, moved.payload<LoadedFile>() == original_payload);
    TEST_EXPECT(ctx, moved.has_hazard(mount_point_ids::executable));
    TEST_EXPECT(ctx, moved.has_hazard(mount_point_ids::render));
    TEST_EXPECT(ctx, moved.payload<LoadedFile>()->buffer.is_ready());

    CErasedOwner destination = CErasedOwner::create<EncodedTga>();
    destination = std::move(moved);
    TEST_EXPECT(ctx, moved.is_empty());
    TEST_EXPECT(ctx, destination.payload<LoadedFile>() == original_payload);

    destination.remove_hazard(mount_point_ids::render);
    TEST_EXPECT(ctx, destination.has_hazard(mount_point_ids::executable));
    TEST_EXPECT(ctx, !destination.has_hazard(mount_point_ids::render));
    TEST_EXPECT(ctx, destination.has_any_hazard());
}

void test_allocation_failure_is_canonical(TTestContext& ctx)
{
    TAllocatorState state;
    state.reject_allocation = true;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    const TModuleIdScope module_scope(module_ids::executable);

    CErasedOwner owner = CErasedOwner::create<LoadedFile>(&context);

    TEST_EXPECT(ctx, owner.is_empty());
    TEST_EXPECT(ctx, !owner.is_ready());
    TEST_EXPECT(ctx, owner.query_type_id() == type_ids::undefined);
    TEST_EXPECT(ctx, owner.hazard_mask() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);

    CErasedOwner local_owner =
        CErasedOwner::create<test_environment::STestAssetLoadState>(&context);
    TEST_EXPECT(ctx, local_owner.is_empty());
    TEST_EXPECT(ctx, !local_owner.is_ready());
    TEST_EXPECT(ctx, local_owner.query_type_id() == type_ids::undefined);
    TEST_EXPECT(ctx, local_owner.hazard_mask() == 0u);
    TEST_EXPECT(ctx, context.get_live_allocation_count() == 0u);
}

void test_owner_reattribution(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryAllocator incompatible_allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext source_context(allocator, system_ids::host);
    memory::CMemoryContext target_context(allocator, system_ids::host);
    memory::CMemoryContext incompatible_context(incompatible_allocator, system_ids::host);
    const TMemoryContextScope context_scope(&source_context);

    CErasedOwner owner = CErasedOwner::create<LoadedFile>();
    LoadedFile* const payload = owner.payload<LoadedFile>();
    TEST_EXPECT(ctx, payload->buffer.allocate(64u, 16u));
    owner.add_hazard(mount_point_ids::asset);

    const std::uint32_t allocation_count = source_context.get_live_allocation_count();
    const std::uint64_t allocation_size = source_context.get_live_allocated_bytes();
    TEST_EXPECT(ctx, allocation_count == 2u);
    TEST_EXPECT(ctx, allocation_size != 0u);
    TEST_EXPECT(ctx, owner.memory_context() == &source_context);
    TEST_EXPECT(ctx, owner.can_reattribute_to(&target_context));
    TEST_EXPECT(ctx, !owner.can_reattribute_to(&incompatible_context));
    TEST_EXPECT(ctx, !owner.reattribute(&incompatible_context));
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == allocation_count);
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == 0u);

    TEST_EXPECT(ctx, owner.reattribute(&target_context));
    TEST_EXPECT(ctx, owner.memory_context() == &target_context);
    TEST_EXPECT(ctx, owner.payload<LoadedFile>() == payload);
    TEST_EXPECT(ctx, owner.has_hazard(mount_point_ids::asset));
    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == allocation_count);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == allocation_size);

    owner.destroy();
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocated_bytes() == 0u);

    CErasedOwner empty;
    TEST_EXPECT(ctx, empty.reattribute(&target_context));
    TEST_EXPECT(ctx, empty.is_empty());
    TEST_EXPECT(ctx, empty.memory_context() == nullptr);
}

void test_all_registered_payload_reattribution(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext source_context(allocator, system_ids::host);
    memory::CMemoryContext target_context(allocator, system_ids::host);
    const TMemoryContextScope context_scope(&source_context);

    CErasedOwner encoded = CErasedOwner::create<EncodedTga>();
    TEST_EXPECT(ctx, encoded.payload<EncodedTga>()->buffer.allocate(32u));
    TEST_EXPECT(ctx, encoded.reattribute(&target_context));
    TEST_EXPECT(ctx, encoded.memory_context() == &target_context);
    encoded.destroy();

    CErasedOwner decoded = CErasedOwner::create<DecodedTga>();
    TEST_EXPECT(ctx, decoded.payload<DecodedTga>()->buffer.allocate(8u, 4u));
    TEST_EXPECT(ctx, decoded.reattribute(&target_context));
    TEST_EXPECT(ctx, decoded.memory_context() == &target_context);
    decoded.destroy();

    CErasedOwner load_request = CErasedOwner::create<AssetLoadRequest>();
    TEST_EXPECT(ctx, load_request.payload<AssetLoadRequest>()->file.set(
        "reattributed-load-request.tga"));
    TEST_EXPECT(ctx, load_request.reattribute(&target_context));
    TEST_EXPECT(ctx, load_request.memory_context() == &target_context);
    load_request.destroy();

    CErasedOwner save_request = CErasedOwner::create<AssetSaveRequest>();
    TEST_EXPECT(ctx, save_request.payload<AssetSaveRequest>()->file.set(
        "reattributed-save-request.tga"));
    TEST_EXPECT(ctx, save_request.reattribute(&target_context));
    TEST_EXPECT(ctx, save_request.memory_context() == &target_context);
    save_request.destroy();

    TEST_EXPECT(ctx, source_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, target_context.get_live_allocation_count() == 0u);
}

void test_existing_owning_transport(TTestContext& ctx)
{
    threading::transports::TOwning<CErasedOwner> transport;
    TEST_EXPECT(ctx, transport.initialise(32u));

    CErasedOwner posted = CErasedOwner::create<LoadedFile>();
    LoadedFile* const original_payload = posted.payload<LoadedFile>();
    TEST_EXPECT(ctx, original_payload->buffer.allocate(43u));
    posted.add_hazard(mount_point_ids::conditioning);

    TEST_EXPECT(ctx, transport.post(std::move(posted)));
    TEST_EXPECT(ctx, posted.is_empty());

    CErasedOwner received;
    TEST_EXPECT(ctx, transport.read(received));
    TEST_EXPECT(ctx, received.payload<LoadedFile>() == original_payload);
    TEST_EXPECT(ctx, received.payload<LoadedFile>()->buffer.is_ready());
    TEST_EXPECT(ctx, received.has_hazard(mount_point_ids::conditioning));

    transport.deallocate();
}

void test_erased_owner_transport_attribution(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext producer_context(allocator, system_ids::host);
    memory::CMemoryContext transport_context(allocator, system_ids::host);
    memory::CMemoryContext recipient_context(allocator, system_ids::host);

    threading::transports::CErasedOwnerTransport transport(
        module_ids::executable, &transport_context, &recipient_context);
    threading::transports::CErasedOwnerProducerEndpoint producer(transport);
    threading::transports::CErasedOwnerConsumerEndpoint consumer(transport);

    TEST_EXPECT(ctx, transport.initialise(32u));
    TEST_EXPECT(ctx, transport.memory_context() == &transport_context);
    TEST_EXPECT(ctx, transport.recipient_memory_context() == &recipient_context);
    TEST_EXPECT(ctx, producer.is_valid());
    TEST_EXPECT(ctx, consumer.is_valid());
    TEST_EXPECT(ctx, transport_context.get_live_allocation_count() == 1u);

    const TMemoryContextScope context_scope(&producer_context);
    CErasedOwner posted = CErasedOwner::create<LoadedFile>();
    LoadedFile* const payload = posted.payload<LoadedFile>();
    TEST_EXPECT(ctx, payload->buffer.allocate(48u));
    posted.add_hazard(mount_point_ids::conditioning);

    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == 2u);
    TEST_EXPECT(ctx, producer.post(std::move(posted)));
    TEST_EXPECT(ctx, posted.is_empty());
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, transport_context.get_live_allocation_count() == 3u);

    CErasedOwner received;
    TEST_EXPECT(ctx, consumer.read(received));
    TEST_EXPECT(ctx, received.payload<LoadedFile>() == payload);
    TEST_EXPECT(ctx, received.has_hazard(mount_point_ids::conditioning));
    TEST_EXPECT(ctx, received.memory_context() == &recipient_context);
    TEST_EXPECT(ctx, transport_context.get_live_allocation_count() == 1u);
    TEST_EXPECT(ctx, recipient_context.get_live_allocation_count() == 2u);

    received.destroy();
    transport.deallocate();
    TEST_EXPECT(ctx, transport_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, recipient_context.get_live_allocation_count() == 0u);
}

void test_erased_owner_transport_rejection(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator transport_allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryAllocator incompatible_allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext transport_context(transport_allocator, system_ids::host);
    memory::CMemoryContext incompatible_context(incompatible_allocator, system_ids::host);

    threading::transports::CErasedOwnerTransport transport(
        module_ids::executable, &transport_context);
    threading::transports::CErasedOwnerTransport mismatched_destination(
        module_ids::executive, &transport_context);
    TEST_EXPECT(ctx, !mismatched_destination.initialise(32u));
    TEST_EXPECT(ctx, !mismatched_destination.is_valid());
    CErasedOwner unready_owner = CErasedOwner::create<LoadedFile>(&incompatible_context);
    LoadedFile* const unready_payload = unready_owner.payload<LoadedFile>();
    TEST_EXPECT(ctx, !transport.post(std::move(unready_owner)));
    TEST_EXPECT(ctx, unready_owner.payload<LoadedFile>() == unready_payload);
    TEST_EXPECT(ctx, unready_owner.memory_context() == &incompatible_context);

    TEST_EXPECT(ctx, transport.initialise(32u));
    CErasedOwner incompatible_owner =
        CErasedOwner::create<LoadedFile>(&incompatible_context);
    LoadedFile* const incompatible_payload = incompatible_owner.payload<LoadedFile>();
    TEST_EXPECT(ctx, !transport.post(std::move(incompatible_owner)));
    TEST_EXPECT(ctx, incompatible_owner.payload<LoadedFile>() == incompatible_payload);
    TEST_EXPECT(ctx, incompatible_owner.memory_context() == &incompatible_context);

    const TMemoryContextScope context_scope(&transport_context);
    CErasedOwner retained = CErasedOwner::create<LoadedFile>(&transport_context);
    LoadedFile* const retained_payload = retained.payload<LoadedFile>();
    TEST_EXPECT(ctx, transport.post(std::move(retained)));

    CErasedOwner received;
    TEST_EXPECT(ctx, transport.read(received));
    TEST_EXPECT(ctx, received.payload<LoadedFile>() == retained_payload);
    TEST_EXPECT(ctx, received.memory_context() == &transport_context);
    received.destroy();

    CErasedOwner unread = CErasedOwner::create<LoadedFile>(&transport_context);
    TEST_EXPECT(ctx, unread.payload<LoadedFile>()->buffer.allocate(24u));
    TEST_EXPECT(ctx, transport.post(std::move(unread)));
    transport.deallocate();
    TEST_EXPECT(ctx, transport_context.get_live_allocation_count() == 0u);
}

void test_local_erased_owner_transport_boundaries(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext producer_context(allocator, system_ids::host);
    memory::CMemoryContext transport_context(allocator, system_ids::host);
    memory::CMemoryContext recipient_context(allocator, system_ids::host);
    memory::CMemoryContext executive_context(allocator, system_ids::executive);
    const TModuleIdScope module_scope(module_ids::executable);
    const TMemoryContextScope context_scope(&producer_context);

    threading::transports::CErasedOwnerTransport same_component(
        module_ids::executable, &transport_context, &recipient_context);
    TEST_EXPECT(ctx, same_component.initialise(4u));

    CErasedOwner posted = CErasedOwner::create<test_environment::STestAssetLoadState>();
    test_environment::STestAssetLoadState* const payload =
        posted.payload<test_environment::STestAssetLoadState>();
    payload->executive_slot = 91;
    posted.add_hazard(mount_point_ids::render);

    TEST_EXPECT(ctx, same_component.post(std::move(posted)));
    TEST_EXPECT(ctx, posted.is_empty());
    CErasedOwner received;
    TEST_EXPECT(ctx, same_component.read(received));
    TEST_EXPECT(ctx, received.payload<test_environment::STestAssetLoadState>() == payload);
    TEST_EXPECT(ctx, received.memory_context() == &recipient_context);
    TEST_EXPECT(ctx, payload->executive_slot == 91);
    TEST_EXPECT(ctx, !payload->request);
    TEST_EXPECT(ctx, received.has_hazard(mount_point_ids::render));
    received.destroy();
    same_component.deallocate();

    threading::transports::CErasedOwnerTransport cross_component(
        module_ids::executive, &executive_context);
    TEST_EXPECT(ctx, cross_component.initialise(4u));
    CErasedOwner rejected = CErasedOwner::create<test_environment::STestAssetLoadState>();
    test_environment::STestAssetLoadState* const rejected_payload =
        rejected.payload<test_environment::STestAssetLoadState>();
    rejected_payload->executive_slot = 92;
    rejected.add_hazard(mount_point_ids::asset);
    memory::CMemoryContext* const rejected_context = rejected.memory_context();
    const std::uint32_t producer_count = producer_context.get_live_allocation_count();
    const std::uint64_t producer_bytes = producer_context.get_live_allocated_bytes();

    TEST_EXPECT(ctx, !cross_component.post(std::move(rejected)));
    TEST_EXPECT(ctx, rejected.is_ready());
    TEST_EXPECT(ctx, rejected.payload<test_environment::STestAssetLoadState>() == rejected_payload);
    TEST_EXPECT(ctx, rejected.memory_context() == rejected_context);
    TEST_EXPECT(ctx, rejected_payload->executive_slot == 92);
    TEST_EXPECT(ctx, !rejected_payload->request);
    TEST_EXPECT(ctx, rejected.has_hazard(mount_point_ids::asset));
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == producer_count);
    TEST_EXPECT(ctx, producer_context.get_live_allocated_bytes() == producer_bytes);
    TEST_EXPECT(ctx, executive_context.get_live_allocation_count() == 1u);

    rejected.destroy();
    cross_component.deallocate();
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, executive_context.get_live_allocation_count() == 0u);
}

void test_erased_owner_transport_diagnostics(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryAllocator incompatible_allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext producer_context(allocator, system_ids::host);
    memory::CMemoryContext transport_context(allocator, system_ids::host);
    memory::CMemoryContext executive_context(allocator, system_ids::executive);
    memory::CMemoryContext incompatible_context(
        incompatible_allocator, system_ids::host);
    const TModuleIdScope module_scope(module_ids::executable);

    TInstance<debug_system::CDebugServiceState> service_owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, service_owner.is_ready());
    debug_system::CDebugServiceState* const service =
        service_owner.operator->();
    service->publish_configuration(0u);
    TEST_EXPECT(ctx, debug_system::install_service(service));
    const TMemoryContextScope context_scope(&producer_context);

    threading::transports::CErasedOwnerTransport cross_component(
        module_ids::executive, &executive_context);
    TEST_EXPECT(ctx, cross_component.initialise(1u));
    const std::uint32_t cross_component_writable_count =
        cross_component.writable_count();

    CErasedOwner local =
        CErasedOwner::create<test_environment::STestAssetLoadState>();
    test_environment::STestAssetLoadState* const local_payload =
        local.payload<test_environment::STestAssetLoadState>();
    local_payload->executive_slot = 101;
    local.add_hazard(mount_point_ids::asset);
    memory::CMemoryContext* const local_context = local.memory_context();
    const std::uint32_t producer_count =
        producer_context.get_live_allocation_count();
    const std::uint64_t producer_bytes =
        producer_context.get_live_allocated_bytes();
    const std::uint32_t executive_count =
        executive_context.get_live_allocation_count();
    const std::uint32_t local_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !cross_component.post(std::move(local)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == local_before + 2u);
    TEST_EXPECT(ctx, local.payload<test_environment::STestAssetLoadState>() == local_payload);
    TEST_EXPECT(ctx, local.memory_context() == local_context);
    TEST_EXPECT(ctx, local_payload->executive_slot == 101);
    TEST_EXPECT(ctx, !local_payload->request);
    TEST_EXPECT(ctx, local.has_hazard(mount_point_ids::asset));
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == producer_count);
    TEST_EXPECT(ctx, producer_context.get_live_allocated_bytes() == producer_bytes);
    TEST_EXPECT(ctx, executive_context.get_live_allocation_count() == executive_count);
    TEST_EXPECT(ctx, cross_component.readable_count() == 0u);
    TEST_EXPECT(ctx, cross_component.writable_count() == cross_component_writable_count);
    local.destroy();

    CErasedOwner system = CErasedOwner::create<LoadedFile>();
    LoadedFile* const system_payload = system.payload<LoadedFile>();
    TEST_EXPECT(ctx, system_payload->buffer.allocate(24u));
    const std::uint32_t system_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, cross_component.post(std::move(system)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == system_before + 1u);
    CErasedOwner received;
    TEST_EXPECT(ctx, cross_component.read(received));
    TEST_EXPECT(ctx, received.payload<LoadedFile>() == system_payload);
    TEST_EXPECT(ctx, received.memory_context() == &executive_context);
    received.destroy();
    cross_component.deallocate();

    threading::transports::CErasedOwnerTransport capacity_transport(
        module_ids::executable, &transport_context);
    TEST_EXPECT(ctx, capacity_transport.initialise(1u));
    const std::uint32_t success_before = service->allocate_incident_id();
    const std::uint32_t capacity = capacity_transport.writable_count();
    for (std::uint32_t index = 0u; index < capacity; ++index)
    {
        CErasedOwner queued = CErasedOwner::create<LoadedFile>();
        TEST_EXPECT(ctx, capacity_transport.post(std::move(queued)));
    }
    TEST_EXPECT(ctx, service->allocate_incident_id() == success_before + 1u);

    CErasedOwner retained = CErasedOwner::create<LoadedFile>();
    LoadedFile* const retained_payload = retained.payload<LoadedFile>();
    memory::CMemoryContext* const retained_context = retained.memory_context();
    const std::uint32_t retained_count =
        producer_context.get_live_allocation_count();
    const std::uint64_t retained_bytes =
        producer_context.get_live_allocated_bytes();
    const std::uint32_t capacity_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !capacity_transport.post(std::move(retained)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == capacity_before + 1u);
    TEST_EXPECT(ctx, retained.payload<LoadedFile>() == retained_payload);
    TEST_EXPECT(ctx, retained.memory_context() == retained_context);
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == retained_count);
    TEST_EXPECT(ctx, producer_context.get_live_allocated_bytes() == retained_bytes);
    TEST_EXPECT(ctx, capacity_transport.readable_count() == capacity);
    retained.destroy();
    capacity_transport.deallocate();

    threading::transports::CErasedOwnerTransport compatibility_transport(
        module_ids::executable, &transport_context);
    TEST_EXPECT(ctx, compatibility_transport.initialise(1u));
    CErasedOwner incompatible =
        CErasedOwner::create<LoadedFile>(&incompatible_context);
    LoadedFile* const incompatible_payload = incompatible.payload<LoadedFile>();
    const std::uint32_t compatibility_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !compatibility_transport.post(std::move(incompatible)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == compatibility_before + 1u);
    TEST_EXPECT(ctx, incompatible.payload<LoadedFile>() == incompatible_payload);
    TEST_EXPECT(ctx, incompatible.memory_context() == &incompatible_context);
    TEST_EXPECT(ctx, compatibility_transport.readable_count() == 0u);
    incompatible.destroy();

    state.reject_allocation = true;
    CErasedOwner empty = CErasedOwner::create<LoadedFile>();
    state.reject_allocation = false;
    const std::uint32_t empty_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !compatibility_transport.post(std::move(empty)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == empty_before + 1u);
    TEST_EXPECT(ctx, empty.is_empty());
    compatibility_transport.deallocate();

    TEST_EXPECT(ctx, debug_system::uninstall_service(service));
    TEST_EXPECT(ctx, producer_context.is_attribution_empty());
    TEST_EXPECT(ctx, transport_context.is_attribution_empty());
    TEST_EXPECT(ctx, executive_context.is_attribution_empty());
    TEST_EXPECT(ctx, incompatible_context.is_attribution_empty());
}

void test_erased_owner_message(TTestContext& ctx)
{
    static_assert(std::is_same_v<
        decltype(threading::CErasedOwnerMsg{}.query_message_type_id()), type_id>);
    static_assert(std::is_same_v<
        decltype(threading::CErasedOwnerMsg{}.query_owner_type_id()), type_id>);
    threading::CErasedOwnerMsg source;
    TEST_EXPECT(ctx, !source.has_message_type());
    TEST_EXPECT(ctx, !source.has_owner());
    TEST_EXPECT(ctx, source.query_message_type_id() == type_ids::undefined);
    TEST_EXPECT(ctx, source.query_async_slot() == 0);

    CErasedOwner content = CErasedOwner::create<DecodedTga>();
    DecodedTga* const payload = content.payload<DecodedTga>();
    TEST_EXPECT(ctx, payload != nullptr);
    TEST_EXPECT(ctx, payload->buffer.allocate(8u, 4u));

    source.set_message_type<TgaDecodeResult>();
    source.set_async_slot(-7);
    source.set_owner(std::move(content));

    TEST_EXPECT(ctx, source.is_message_a<TgaDecodeResult>());
    TEST_EXPECT(ctx, source.query_owner_type_id() == k_type_id_v<DecodedTga>);
    TEST_EXPECT(ctx, source.owner().payload<DecodedTga>() == payload);

    threading::CErasedOwnerMsg moved{ std::move(source) };
    TEST_EXPECT(ctx, !source.has_message_type());
    TEST_EXPECT(ctx, !source.has_owner());
    TEST_EXPECT(ctx, source.query_async_slot() == 0);
    TEST_EXPECT(ctx, moved.query_async_slot() == -7);
    TEST_EXPECT(ctx, moved.is_message_a<TgaDecodeResult>());
    TEST_EXPECT(ctx, moved.owner().payload<DecodedTga>() == payload);

    CErasedOwner detached = moved.take_owner();
    TEST_EXPECT(ctx, !moved.has_owner());
    TEST_EXPECT(ctx, moved.has_message_type());
    TEST_EXPECT(ctx, detached.payload<DecodedTga>() == payload);
}

void test_owned_asset_request_transport_and_asset_lifetime(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext executive_context(allocator, system_ids::executive);
    memory::CMemoryContext host_context(allocator, system_ids::host);
    const TModuleIdScope module_scope(module_ids::executive);
    const TMemoryContextScope executive_context_scope(&executive_context);

    threading::transports::CErasedOwnerMsgTransport transport(
        module_ids::executable, &host_context);
    TEST_EXPECT(ctx, transport.initialise(4u));

    CErasedOwner owner = CErasedOwner::create<AssetLoadRequest>();
    AssetLoadRequest* const request = owner.payload<AssetLoadRequest>();
    TEST_EXPECT(ctx, request != nullptr);
    TEST_EXPECT(ctx, request->file.set("temporary-owned-request.tga"));
    request->format = EAssetFileFormat::tga;
    request->decode_top_down = false;
    TEST_EXPECT(ctx, executive_context.get_live_allocation_count() == 2u);

    threading::CErasedOwnerMsg posted;
    posted.set_message_type<AssetLoadRequest>();
    posted.set_async_slot(47);
    posted.set_owner(std::move(owner));
    TEST_EXPECT(ctx, transport.post(std::move(posted)));
    TEST_EXPECT(ctx, executive_context.is_attribution_empty());

    threading::CErasedOwnerMsg received;
    TEST_EXPECT(ctx, transport.read(received));
    TEST_EXPECT(ctx, received.is_message_a<AssetLoadRequest>());
    TEST_EXPECT(ctx, received.query_async_slot() == 47);
    TEST_EXPECT(ctx, received.owner().memory_context() == &host_context);
    AssetLoadRequest* const received_request =
        received.owner().payload<AssetLoadRequest>();
    TEST_EXPECT(ctx, received_request == request);
    TEST_EXPECT(ctx, received_request->file.view() ==
        CStringView{ "temporary-owned-request.tga" });
    TEST_EXPECT(ctx, received_request->format == EAssetFileFormat::tga);
    TEST_EXPECT(ctx, !received_request->decode_top_down);

    CAssetRepository assets;
    {
        const TMemoryContextScope host_context_scope(&host_context);
        TEST_EXPECT(ctx, assets.initialise());
        const CAssetId request_asset = assets.insert(received.take_owner());
        TEST_EXPECT(ctx, request_asset);
        const CAssetRecord* const record = assets.resolve(request_asset);
        TEST_EXPECT(ctx, record != nullptr);
        TEST_EXPECT(ctx, record->payload<AssetLoadRequest>() == received_request);
        TEST_EXPECT(ctx, assets.erase(request_asset));
        assets.deallocate();
    }

    TEST_EXPECT(ctx, !received.has_owner());
    transport.deallocate();
    TEST_EXPECT(ctx, executive_context.is_attribution_empty());
    TEST_EXPECT(ctx, host_context.is_attribution_empty());
}

void test_erased_owner_message_transport(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext producer_context(allocator, system_ids::host);
    memory::CMemoryContext transport_context(allocator, system_ids::host);
    memory::CMemoryContext recipient_context(allocator, system_ids::host);
    const TModuleIdScope module_scope(module_ids::executable);

    threading::transports::CErasedOwnerMsgTransport transport(
        module_ids::executable, &transport_context, &recipient_context);
    threading::transports::CErasedOwnerMsgProducerEndpoint producer(transport);
    threading::transports::CErasedOwnerMsgConsumerEndpoint consumer(transport);
    TEST_EXPECT(ctx, transport.initialise(32u));
    TEST_EXPECT(ctx, producer.is_valid());
    TEST_EXPECT(ctx, consumer.is_valid());

    const TMemoryContextScope context_scope(&producer_context);
    threading::CErasedOwnerMsg posted;
    posted.set_message_type<FileLoadResult>();
    posted.set_async_slot(31);
    CErasedOwner content = CErasedOwner::create<LoadedFile>();
    LoadedFile* const payload = content.payload<LoadedFile>();
    TEST_EXPECT(ctx, payload->buffer.allocate(48u));
    posted.set_owner(std::move(content));

    TEST_EXPECT(ctx, producer.post(std::move(posted)));
    TEST_EXPECT(ctx, !posted.has_message_type());
    TEST_EXPECT(ctx, !posted.has_owner());

    threading::CErasedOwnerMsg received;
    TEST_EXPECT(ctx, consumer.read(received));
    TEST_EXPECT(ctx, received.is_message_a<FileLoadResult>());
    TEST_EXPECT(ctx, received.query_async_slot() == 31);
    TEST_EXPECT(ctx, received.owner().payload<LoadedFile>() == payload);
    TEST_EXPECT(ctx, received.owner().memory_context() == &recipient_context);

    threading::CErasedOwnerMsg empty_completion;
    empty_completion.set_message_type<TgaEncodeResult>();
    empty_completion.set_async_slot(32);
    TEST_EXPECT(ctx, producer.post(std::move(empty_completion)));
    TEST_EXPECT(ctx, consumer.read(received));
    TEST_EXPECT(ctx, received.is_message_a<TgaEncodeResult>());
    TEST_EXPECT(ctx, received.query_async_slot() == 32);
    TEST_EXPECT(ctx, !received.has_owner());

    threading::CErasedOwnerMsg untyped;
    TEST_EXPECT(ctx, !producer.post(std::move(untyped)));

    threading::CErasedOwnerMsg local_message;
    local_message.set_message_type<test_environment::CTestRuntime>();
    local_message.set_async_slot(33);
    TEST_EXPECT(ctx, producer.post(std::move(local_message)));
    TEST_EXPECT(ctx, consumer.read(received));
    TEST_EXPECT(ctx, received.is_message_a<test_environment::CTestRuntime>());
    TEST_EXPECT(ctx, received.query_message_type_id().is_local());
    TEST_EXPECT(ctx, received.query_async_slot() == 33);

    threading::CErasedOwnerMsg local_owned_message;
    local_owned_message.set_message_type<test_environment::CTestRuntime>();
    local_owned_message.set_async_slot(36);
    CErasedOwner local_content =
        CErasedOwner::create<test_environment::STestAssetLoadState>();
    test_environment::STestAssetLoadState* const local_payload =
        local_content.payload<test_environment::STestAssetLoadState>();
    local_payload->executive_slot = 36;
    local_content.add_hazard(mount_point_ids::conditioning);
    local_owned_message.set_owner(std::move(local_content));
    TEST_EXPECT(ctx, producer.post(std::move(local_owned_message)));
    TEST_EXPECT(ctx, consumer.read(received));
    TEST_EXPECT(ctx, received.is_message_a<test_environment::CTestRuntime>());
    TEST_EXPECT(ctx, received.query_async_slot() == 36);
    TEST_EXPECT(ctx, received.owner().payload<test_environment::STestAssetLoadState>() == local_payload);
    TEST_EXPECT(ctx, received.owner().memory_context() == &recipient_context);
    TEST_EXPECT(ctx, received.owner().has_hazard(mount_point_ids::conditioning));

    memory::CMemoryContext executive_transport_context(
        allocator, system_ids::executive);
    threading::transports::CErasedOwnerMsgTransport cross_component(
        module_ids::executive, &executive_transport_context);
    TEST_EXPECT(ctx, cross_component.initialise(32u));

    threading::CErasedOwnerMsg rejected_local;
    rejected_local.set_message_type<test_environment::CTestRuntime>();
    rejected_local.set_async_slot(34);
    TEST_EXPECT(ctx, !cross_component.post(std::move(rejected_local)));
    TEST_EXPECT(ctx, rejected_local.is_message_a<test_environment::CTestRuntime>());
    TEST_EXPECT(ctx, rejected_local.query_async_slot() == 34);

    threading::CErasedOwnerMsg rejected_local_owner;
    rejected_local_owner.set_message_type<FileLoadResult>();
    rejected_local_owner.set_async_slot(37);
    CErasedOwner rejected_content =
        CErasedOwner::create<test_environment::STestAssetLoadState>();
    test_environment::STestAssetLoadState* const rejected_payload =
        rejected_content.payload<test_environment::STestAssetLoadState>();
    rejected_payload->executive_slot = 37;
    rejected_content.add_hazard(mount_point_ids::asset);
    memory::CMemoryContext* const rejected_context = rejected_content.memory_context();
    rejected_local_owner.set_owner(std::move(rejected_content));
    const std::uint32_t producer_count = producer_context.get_live_allocation_count();
    const std::uint64_t producer_bytes = producer_context.get_live_allocated_bytes();
    TEST_EXPECT(ctx, !cross_component.post(std::move(rejected_local_owner)));
    TEST_EXPECT(ctx, rejected_local_owner.is_message_a<FileLoadResult>());
    TEST_EXPECT(ctx, rejected_local_owner.query_async_slot() == 37);
    TEST_EXPECT(ctx, rejected_local_owner.owner().payload<test_environment::STestAssetLoadState>() == rejected_payload);
    TEST_EXPECT(ctx, rejected_local_owner.owner().memory_context() == rejected_context);
    TEST_EXPECT(ctx, rejected_payload->executive_slot == 37);
    TEST_EXPECT(ctx, !rejected_payload->request);
    TEST_EXPECT(ctx, rejected_local_owner.owner().has_hazard(mount_point_ids::asset));
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == producer_count);
    TEST_EXPECT(ctx, producer_context.get_live_allocated_bytes() == producer_bytes);

    threading::CErasedOwnerMsg admitted_system;
    admitted_system.set_message_type<FileLoadResult>();
    admitted_system.set_async_slot(35);
    TEST_EXPECT(ctx, cross_component.post(std::move(admitted_system)));
    TEST_EXPECT(ctx, cross_component.read(received));
    TEST_EXPECT(ctx, received.is_message_a<FileLoadResult>());
    TEST_EXPECT(ctx, received.query_async_slot() == 35);
    cross_component.deallocate();

    rejected_local_owner.owner().destroy();
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, producer_context.get_live_allocated_bytes() == 0u);
    TEST_EXPECT(ctx, executive_transport_context.get_live_allocation_count() == 0u);

    transport.deallocate();
    TEST_EXPECT(ctx, transport_context.get_live_allocation_count() == 0u);
    TEST_EXPECT(ctx, recipient_context.get_live_allocation_count() == 0u);
}

void test_erased_owner_message_transport_diagnostics(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator(
        &state, allocate_test_memory, deallocate_test_memory, system_ids::host);
    memory::CMemoryContext producer_context(allocator, system_ids::host);
    memory::CMemoryContext executive_context(allocator, system_ids::executive);
    const TModuleIdScope module_scope(module_ids::executable);

    TInstance<debug_system::CDebugServiceState> service_owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, service_owner.is_ready());
    debug_system::CDebugServiceState* const service =
        service_owner.operator->();
    service->publish_configuration(0u);
    TEST_EXPECT(ctx, debug_system::install_service(service));
    const TMemoryContextScope context_scope(&producer_context);

    threading::transports::CErasedOwnerMsgTransport cross_component(
        module_ids::executive, &executive_context);
    TEST_EXPECT(ctx, cross_component.initialise(4u));

    threading::CErasedOwnerMsg rejected_message;
    rejected_message.set_message_type<test_environment::CTestRuntime>();
    rejected_message.set_async_slot(111);
    const std::uint32_t message_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !cross_component.post(std::move(rejected_message)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == message_before + 2u);
    TEST_EXPECT(ctx, rejected_message.is_message_a<test_environment::CTestRuntime>());
    TEST_EXPECT(ctx, rejected_message.query_async_slot() == 111);
    TEST_EXPECT(ctx, !rejected_message.has_owner());
    TEST_EXPECT(ctx, cross_component.readable_count() == 0u);

    threading::CErasedOwnerMsg rejected_payload_message;
    rejected_payload_message.set_message_type<FileLoadResult>();
    rejected_payload_message.set_async_slot(112);
    CErasedOwner rejected_payload_owner =
        CErasedOwner::create<test_environment::STestAssetLoadState>();
    test_environment::STestAssetLoadState* const rejected_payload =
        rejected_payload_owner.payload<test_environment::STestAssetLoadState>();
    rejected_payload->executive_slot = 112;
    rejected_payload_owner.add_hazard(mount_point_ids::asset);
    memory::CMemoryContext* const rejected_context =
        rejected_payload_owner.memory_context();
    rejected_payload_message.set_owner(std::move(rejected_payload_owner));
    const std::uint32_t producer_count =
        producer_context.get_live_allocation_count();
    const std::uint64_t producer_bytes =
        producer_context.get_live_allocated_bytes();
    const std::uint32_t payload_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !cross_component.post(std::move(rejected_payload_message)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == payload_before + 2u);
    TEST_EXPECT(ctx, rejected_payload_message.is_message_a<FileLoadResult>());
    TEST_EXPECT(ctx, rejected_payload_message.query_async_slot() == 112);
    TEST_EXPECT(ctx, rejected_payload_message.owner().payload<
        test_environment::STestAssetLoadState>() == rejected_payload);
    TEST_EXPECT(ctx, rejected_payload_message.owner().memory_context() ==
        rejected_context);
    TEST_EXPECT(ctx, rejected_payload->executive_slot == 112);
    TEST_EXPECT(ctx, !rejected_payload->request);
    TEST_EXPECT(ctx, rejected_payload_message.owner().has_hazard(
        mount_point_ids::asset));
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == producer_count);
    TEST_EXPECT(ctx, producer_context.get_live_allocated_bytes() == producer_bytes);
    TEST_EXPECT(ctx, cross_component.readable_count() == 0u);

    threading::CErasedOwnerMsg first_failure_message;
    first_failure_message.set_message_type<test_environment::CTestRuntime>();
    first_failure_message.set_async_slot(113);
    CErasedOwner first_failure_owner =
        CErasedOwner::create<test_environment::STestAssetLoadState>();
    test_environment::STestAssetLoadState* const first_failure_payload =
        first_failure_owner.payload<test_environment::STestAssetLoadState>();
    first_failure_payload->executive_slot = 113;
    first_failure_message.set_owner(std::move(first_failure_owner));
    const std::uint32_t first_failure_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !cross_component.post(std::move(first_failure_message)));
    TEST_EXPECT(ctx,
        service->allocate_incident_id() == first_failure_before + 2u);
    TEST_EXPECT(ctx, first_failure_message.is_message_a<test_environment::CTestRuntime>());
    TEST_EXPECT(ctx, first_failure_message.query_async_slot() == 113);
    TEST_EXPECT(ctx, first_failure_message.owner().payload<
        test_environment::STestAssetLoadState>() == first_failure_payload);
    TEST_EXPECT(ctx, first_failure_payload->executive_slot == 113);
    TEST_EXPECT(ctx, !first_failure_payload->request);
    TEST_EXPECT(ctx, cross_component.readable_count() == 0u);

    threading::CErasedOwnerMsg admitted_system;
    admitted_system.set_message_type<FileLoadResult>();
    admitted_system.set_async_slot(114);
    CErasedOwner admitted_owner = CErasedOwner::create<LoadedFile>();
    LoadedFile* const admitted_payload = admitted_owner.payload<LoadedFile>();
    TEST_EXPECT(ctx, admitted_payload->buffer.allocate(32u));
    admitted_system.set_owner(std::move(admitted_owner));
    const std::uint32_t admitted_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, cross_component.post(std::move(admitted_system)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == admitted_before + 1u);
    threading::CErasedOwnerMsg received;
    TEST_EXPECT(ctx, cross_component.read(received));
    TEST_EXPECT(ctx, received.is_message_a<FileLoadResult>());
    TEST_EXPECT(ctx, received.query_async_slot() == 114);
    TEST_EXPECT(ctx, received.owner().payload<LoadedFile>() == admitted_payload);
    TEST_EXPECT(ctx, received.owner().memory_context() == &executive_context);
    received.owner().destroy();
    cross_component.deallocate();

    threading::transports::CErasedOwnerMsgTransport capacity_transport(
        module_ids::executive, &executive_context);
    TEST_EXPECT(ctx, capacity_transport.initialise(1u));
    const std::uint32_t capacity = capacity_transport.writable_count();
    for (std::uint32_t index = 0u; index < capacity; ++index)
    {
        threading::CErasedOwnerMsg queued;
        queued.set_message_type<FileLoadResult>();
        TEST_EXPECT(ctx, capacity_transport.post(std::move(queued)));
    }

    threading::CErasedOwnerMsg retained;
    retained.set_message_type<FileLoadResult>();
    retained.set_async_slot(115);
    CErasedOwner retained_owner = CErasedOwner::create<LoadedFile>();
    LoadedFile* const retained_payload = retained_owner.payload<LoadedFile>();
    memory::CMemoryContext* const retained_context = retained_owner.memory_context();
    retained.set_owner(std::move(retained_owner));
    const std::uint32_t retained_count =
        producer_context.get_live_allocation_count();
    const std::uint64_t retained_bytes =
        producer_context.get_live_allocated_bytes();
    const std::uint32_t capacity_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !capacity_transport.post(std::move(retained)));
    TEST_EXPECT(ctx, service->allocate_incident_id() == capacity_before + 1u);
    TEST_EXPECT(ctx, retained.is_message_a<FileLoadResult>());
    TEST_EXPECT(ctx, retained.query_async_slot() == 115);
    TEST_EXPECT(ctx, retained.owner().payload<LoadedFile>() == retained_payload);
    TEST_EXPECT(ctx, retained.owner().memory_context() == retained_context);
    TEST_EXPECT(ctx, producer_context.get_live_allocation_count() == retained_count);
    TEST_EXPECT(ctx, producer_context.get_live_allocated_bytes() == retained_bytes);
    TEST_EXPECT(ctx, capacity_transport.readable_count() == capacity);
    retained.owner().destroy();
    capacity_transport.deallocate();

    rejected_payload_message.owner().destroy();
    first_failure_message.owner().destroy();
    TEST_EXPECT(ctx, debug_system::uninstall_service(service));
    TEST_EXPECT(ctx, producer_context.is_attribution_empty());
    TEST_EXPECT(ctx, executive_context.is_attribution_empty());
}

static void test_asset_request_aggregate_attribution(TTestContext& ctx)
{
    TAllocatorState state;
    memory::CMemoryAllocator allocator{ &state, allocate_test_memory, deallocate_test_memory, system_ids::host };
    memory::CMemoryAllocator other_allocator{ &state, allocate_test_memory, deallocate_test_memory, system_ids::host };
    memory::CMemoryContext source{ allocator, system_ids::host };
    memory::CMemoryContext target{ allocator, system_ids::host };
    memory::CMemoryContext incompatible{ other_allocator, system_ids::host };
    const TModuleIdScope module_scope{ module_ids::executable };
    const TMemoryContextScope context_scope{ &source };

    CErasedOwner owner = CErasedOwner::create<LiveAssetTransfer>();
    LiveAssetTransfer* const request = owner.payload<LiveAssetTransfer>();
    TEST_EXPECT(ctx, request != nullptr);
    if (request == nullptr) return;
    TEST_EXPECT(ctx, request->storage.value.initialise());
    TEST_EXPECT(ctx, request->storage.file.set("asset.json"));
    const std::uint32_t count = source.get_live_allocation_count();
    const std::uint64_t bytes = source.get_live_allocated_bytes();
    TEST_EXPECT(ctx, !owner.reattribute(&incompatible));
    TEST_EXPECT(ctx, source.get_live_allocation_count() == count);
    TEST_EXPECT(ctx, source.get_live_allocated_bytes() == bytes);
    TEST_EXPECT(ctx, request->storage.value.is_ready());
    TEST_EXPECT(ctx, request->storage.file.length() == 10u);
    TEST_EXPECT(ctx, owner.reattribute(&target));
    TEST_EXPECT(ctx, source.is_attribution_empty());
    TEST_EXPECT(ctx, target.get_live_allocation_count() == count);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == bytes);
    TEST_EXPECT(ctx, request->storage.memory_attribution().source == &target);
    owner.destroy();
    TEST_EXPECT(ctx, target.is_attribution_empty());

    //  A conditioning result owns both the baked snapshot and optional JSON.
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    owner = CErasedOwner::create<DocumentConditionResult>();
    DocumentConditionResult* const result = owner.payload<DocumentConditionResult>();
    TEST_EXPECT(ctx, result != nullptr);
    if (result == nullptr) return;
    TEST_EXPECT(ctx, document_translation::bake(live, result->storage.baked));
    TEST_EXPECT(ctx, result->storage.text.resize(32u));
    live.deallocate();
    const std::uint32_t result_count = source.get_live_allocation_count();
    const std::uint64_t result_bytes = source.get_live_allocated_bytes();
    TEST_EXPECT(ctx, owner.reattribute(&target));
    TEST_EXPECT(ctx, source.is_attribution_empty());
    TEST_EXPECT(ctx, target.get_live_allocation_count() == result_count);
    TEST_EXPECT(ctx, target.get_live_allocated_bytes() == result_bytes);
    TEST_EXPECT(ctx, result->storage.baked.document().check_integrity());
    TEST_EXPECT(ctx, result->storage.text.size() == 32u);
    owner.destroy();
    TEST_EXPECT(ctx, target.is_attribution_empty());
}

CErasedOwner& accounting_owner(CErasedOwner& owner) noexcept { return owner; }
CErasedOwner& accounting_owner(threading::CErasedOwnerMsg& message) noexcept { return message.owner(); }

template<typename TTransport, typename TEnvelope>
void test_transport_with_accounting_discrepancies(TTestContext& ctx)
{
    using Access = memory::SMemoryContextTestAccess;
    TAllocatorState state;
    memory::CMemoryAllocator allocator{&state, allocate_test_memory, deallocate_test_memory, system_ids::host};
    memory::CMemoryContext source{allocator, system_ids::host};
    memory::CMemoryContext channel{allocator, system_ids::host};
    memory::CMemoryContext recipient{allocator, system_ids::host};
    const TModuleIdScope module_scope{module_ids::executable};
    auto service_owner = TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, service_owner.is_ready());
    if (!service_owner.is_ready()) return;
    auto& service = *service_owner;
    service.publish_configuration(debug_system::k_critical_shutdown_enabled);
    TEST_EXPECT(ctx, debug_system::install_service(&service));
    const TMemoryContextScope scope{&source};
    TTransport transport{module_ids::executable, &channel, &recipient};
    TEST_EXPECT(ctx, transport.initialise(1u));
    TEnvelope posted;
    TEnvelope received;
    CErasedOwner content = CErasedOwner::create<LoadedFile>();
    auto* const payload = content.payload<LoadedFile>();
    TEST_EXPECT(ctx, payload != nullptr);
    TEST_EXPECT(ctx, payload->buffer.allocate(48u));
    void* const buffer_address = payload->buffer.data();
    content.add_hazard(mount_point_ids::conditioning);
    const std::uint32_t count = source.get_live_allocation_count();
    const std::uint64_t bytes = source.get_live_allocated_bytes();
    if constexpr (std::is_same_v<TEnvelope, threading::CErasedOwnerMsg>)
    {
        posted.template set_message_type<FileLoadResult>();
        posted.set_async_slot(77);
        posted.set_owner(std::move(content));
    }
    else
    {
        posted = std::move(content);
    }
    Access::seed(source, 0u, 0u);
    Access::seed(channel, UINT32_MAX, UINT64_MAX);
    const std::uint32_t incident = service.allocate_incident_id();
    TEST_EXPECT(ctx, transport.post(std::move(posted)));
    TEST_EXPECT(ctx, accounting_owner(posted).is_empty());
    TEST_EXPECT(ctx, source.get_live_allocation_count() == std::uint32_t{0u} - count);
    TEST_EXPECT(ctx, source.get_live_allocated_bytes() == std::uint64_t{0u} - bytes);
    TEST_EXPECT(ctx, channel.get_live_allocation_count() == count - 1u);
    TEST_EXPECT(ctx, channel.get_live_allocated_bytes() == bytes - 1u);
    TEST_EXPECT(ctx, service.allocate_incident_id() == incident + 5u);

    Access::seed(channel, 0u, 0u);
    Access::seed(recipient, 0x7fffffffu, 0x7fffffffffffffffull);
    const std::uint32_t read_incident = service.allocate_incident_id();
    TEST_EXPECT(ctx, transport.read(received));
    CErasedOwner& received_owner = accounting_owner(received);
    TEST_EXPECT(ctx, received_owner.memory_context() == &recipient);
    TEST_EXPECT(ctx, received_owner.payload<LoadedFile>() == payload);
    TEST_EXPECT(ctx, payload->buffer.data() == buffer_address);
    TEST_EXPECT(ctx, received_owner.has_hazard(mount_point_ids::conditioning));
    TEST_EXPECT(ctx, service.allocate_incident_id() == read_incident + 5u);
    TEST_EXPECT(ctx, channel.get_live_allocation_count() == std::uint32_t{0u} - count);
    TEST_EXPECT(ctx, channel.get_live_allocated_bytes() == std::uint64_t{0u} - bytes);
    if constexpr (std::is_same_v<TEnvelope, threading::CErasedOwnerMsg>)
    {
        TEST_EXPECT(ctx, received.template is_message_a<FileLoadResult>());
        TEST_EXPECT(ctx, received.query_async_slot() == 77);
    }
    received_owner.destroy();
    TEST_EXPECT(ctx, recipient.get_live_allocation_count() == 0x7fffffffu);
    TEST_EXPECT(ctx, recipient.get_live_allocated_bytes() == 0x7fffffffffffffffull);
    transport.deallocate();
    TEST_EXPECT(ctx, service.read_shutdown_request() == debug_system::EShutdownReason::none);
    TEST_EXPECT(ctx, debug_system::uninstall_service(&service));
    //  All actual owners are destroyed; discard only the deliberately injected offsets.
    Access::seed(source, 0u, 0u);
    Access::seed(channel, 0u, 0u);
    Access::seed(recipient, 0u, 0u);
}

}   //  namespace

int run_erased_owner_tests()
{
    TTestContext ctx;
    test_registration_and_empty_state(ctx);
    test_operation_registry(ctx);
    test_operation_registry_failure_boundaries(ctx);
    test_executive_context_and_module_unload_gate(ctx);
    test_local_creation_moves_and_destruction(ctx);
    test_local_context_boundaries(ctx);
    test_creation_accounting_and_destruction(ctx);
    test_moves_and_hazards(ctx);
    test_allocation_failure_is_canonical(ctx);
    test_owner_reattribution(ctx);
    test_all_registered_payload_reattribution(ctx);
    test_asset_request_aggregate_attribution(ctx);
    test_existing_owning_transport(ctx);
    test_erased_owner_transport_attribution(ctx);
    test_erased_owner_transport_rejection(ctx);
    test_local_erased_owner_transport_boundaries(ctx);
    test_erased_owner_transport_diagnostics(ctx);
    test_erased_owner_message(ctx);
    test_owned_asset_request_transport_and_asset_lifetime(ctx);
    test_erased_owner_message_transport(ctx);
    test_erased_owner_message_transport_diagnostics(ctx);
    test_transport_with_accounting_discrepancies<threading::transports::CErasedOwnerTransport, CErasedOwner>(ctx);
    test_transport_with_accounting_discrepancies<threading::transports::CErasedOwnerMsgTransport, threading::CErasedOwnerMsg>(ctx);

    std::cout << "ErasedOwner: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
