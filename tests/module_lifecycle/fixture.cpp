
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    fixture.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Sep 26
//
//  Real DLL fixtures for the Host's asynchronous module lifecycle.

#include <cstdlib>
#include <cstring>
#include "platform/system/performance_counter.hpp"
#include "platform/threading/processor_relax.hpp"

#include "tests/environment/local_type_ids.hpp"
#include "module/module_binding_context.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"
#include "debug/macros.hpp"

namespace module_lifecycle_tests
{

#if MV_LIFECYCLE_EXECUTIVE
static constexpr auto k_advertised_module_id = module_ids::executive;

static bool post(threading::CThreadContext& context, const EModuleAction action,
    const module_ids::id_type module, const char* const file, const std::int32_t slot,
    const system_type_id function = system_type_ids::undefined) noexcept
{
    CErasedOwner owner = CErasedOwner::create<ModuleRequest>();
    ModuleRequest* const request = owner.payload<ModuleRequest>();
    if ((request == nullptr) || ((file != nullptr) && !request->file.set(file)))
    {
        return false;
    }
    request->action = action;
    request->module = module;
    request->required_function = function;
    threading::CErasedOwnerMsg message;
    message.set_message_type<ModuleRequest>();
    message.set_async_slot(slot);
    message.set_owner(std::move(owner));
    return context.post(std::move(message));
}

static bool receive(threading::CThreadContext& context, threading::CErasedPodMsg& message) noexcept
{
    platform::system::CPerfCounter timer;
    (void)timer.update();
    const auto deadline = context.perf_count_conversion().query_ticks_per_second() * 10u;
    while (!context.exit_requested() && (timer.query_delta() < deadline))
    {
        if (context.read(message))
        {
            return true;
        }
        platform::threading::processor_relax();
    }
    return false;
}

static bool operation(threading::CThreadContext& context, const EModuleAction action,
    const char* const file, const std::int32_t slot, const EModuleStatus expected, const bool available,
    const system_type_id function = system_type_ids::undefined) noexcept
{
    if (!post(context, action, module_ids::conditioning_general, file, slot, function))
    {
        return false;
    }
    for (const EModuleNotice notice : { EModuleNotice::acknowledged, EModuleNotice::completed })
    {
        threading::CErasedPodMsg message;
        ModuleResult result;
        if (!receive(context, message) || !message.copy_payload_to(result) || (message.query_async_slot() != slot) ||
            (result.notice != notice) || (result.status != ((notice == EModuleNotice::acknowledged) ? EModuleStatus::success : expected)) ||
            ((notice == EModuleNotice::completed) && (result.available != available)))
        {
            MV_REPORT("Lifecycle fixture: operation %d notification failed", slot);
            return false;
        }
    }
    MV_REPORT("Lifecycle fixture: operation %d passed", slot);
    return true;
}

static bool self_terminate(threading::CThreadContext& context, const char* const replacement) noexcept
{
    if (!post(context, (replacement != nullptr) ? EModuleAction::replace : EModuleAction::unload,
        module_ids::executive, replacement, 100))
    {
        return false;
    }
    platform::system::CPerfCounter timer;
    (void)timer.update();
    const auto deadline = context.perf_count_conversion().query_ticks_per_second() * 10u;
    while (!context.exit_requested() && (timer.query_delta() < deadline))
    {
        threading::CErasedPodMsg unexpected;
        if (context.read(unexpected))
        {
            return false;
        }
        platform::threading::processor_relax();
    }
    threading::CErasedPodMsg unexpected;
    const bool success = context.exit_requested() && !context.read(unexpected);
    if (success)
    {
        MV_REPORT("Lifecycle fixture: Host requested Executive exit without notifications");
    }
    return success;
}

static bool retain_raw_asset(threading::CThreadContext& context, const bool dependency,
    CAssetId* const asset = nullptr, const mount_point_ids::id_type mount = mount_point_ids::conditioning) noexcept
{
    CErasedOwner owner = CErasedOwner::create<RawAssetTransfer>();
    RawAssetTransfer* const request = owner.payload<RawAssetTransfer>();
    if ((request == nullptr) || !request->storage.value.allocate(16u) || !request->storage.value.set_size(16u))
    {
        return false;
    }
    if (dependency)
    {
        owner.add_hazard(mount);
    }
    std::memset(request->storage.value.data(), 0x5a, 16u);
    threading::CErasedOwnerMsg transfer;
    transfer.set_message_type<RawAssetTransfer>();
    transfer.set_async_slot(20);
    transfer.set_owner(std::move(owner));
    threading::CErasedPodMsg completion;
    AssetResult result;
    const bool success = context.post(std::move(transfer)) && receive(context, completion) && completion.copy_payload_to(result) &&
        (completion.query_async_slot() == 20) && (result.status == EAssetStatus::success) && static_cast<bool>(result.asset);
    if (success && (asset != nullptr))
    {
        *asset = result.asset;
    }
    return success;
}

static bool dispose_asset(threading::CThreadContext& context, const CAssetId asset, const EAssetStatus expected) noexcept
{
    threading::CErasedPodMsg request;
    request.set_async_slot(30);
    request.assign_payload(AssetDisposeRequest{ asset });
    threading::CErasedPodMsg completion;
    AssetDisposeResult result;
    return context.post(request) && receive(context, completion) && completion.copy_payload_to(result) &&
        (completion.query_async_slot() == 30) && (result.asset == asset) && (result.status == expected);
}

static bool post_save(threading::CThreadContext& context, const CAssetId asset, const std::int32_t slot) noexcept
{
    CErasedOwner owner = CErasedOwner::create<AssetSaveRequest>();
    AssetSaveRequest* const request = owner.payload<AssetSaveRequest>();
    if ((request == nullptr) || !request->file.set("build/lifecycle-disposal.bin"))
    {
        return false;
    }
    request->source = asset;
    request->settings.format = EAssetFileFormat::raw;
    threading::CErasedOwnerMsg message;
    message.set_message_type<AssetSaveRequest>();
    message.set_async_slot(slot);
    message.set_owner(std::move(owner));
    return context.post(std::move(message));
}

static bool disposal_during_saves(threading::CThreadContext& context) noexcept
{
    CAssetId asset;
    if (!retain_raw_asset(context, true, &asset))
    {
        return false;
    }
    constexpr std::int32_t k_save_count{ 32 };
    for (std::int32_t index = 0; index < k_save_count; ++index)
    {
        if (!post_save(context, asset, 200 + index))
        {
            return false;
        }
    }
    //  An owning request behind the saves establishes their admission before
    //  disposal crosses the separate POD queue. The service is already loaded.
    if (!post(context, EModuleAction::load, module_ids::conditioning_general, "MorphicLifecycleService.dll", 40))
    {
        return false;
    }
    bool saved[k_save_count]{};
    std::int32_t save_count{ 0 };
    bool acknowledged{ false };
    bool completed{ false };
    bool disposed{ false };
    bool duplicate_rejected{ false };
    bool rejected_save{ false };
    while (!completed || !disposed || !duplicate_rejected || !rejected_save || (save_count != k_save_count))
    {
        threading::CErasedPodMsg message;
        if (!receive(context, message))
        {
            return false;
        }
        ModuleResult module;
        AssetDisposeResult disposal;
        AssetResult result;
        const std::int32_t slot = message.query_async_slot();
        if (message.copy_payload_to(module))
        {
            if ((slot != 40) || completed)
            {
                return false;
            }
            if (module.notice == EModuleNotice::acknowledged)
            {
                if (acknowledged || (module.status != EModuleStatus::success))
                {
                    return false;
                }
                acknowledged = true;
                threading::CErasedPodMsg request;
                request.set_async_slot(30);
                request.assign_payload(AssetDisposeRequest{ asset });
                if (!context.post(request))
                {
                    return false;
                }
                request.set_async_slot(31);
                if (!context.post(request))
                {
                    return false;
                }
            }
            else
            {
                if (!acknowledged || (module.status != EModuleStatus::already_loaded))
                {
                    return false;
                }
                completed = true;
            }
        }
        else if (message.copy_payload_to(disposal))
        {
            if (slot == 31)
            {
                if (duplicate_rejected || (disposal.asset != asset) || (disposal.status != EAssetStatus::invalid_asset))
                {
                    return false;
                }
                duplicate_rejected = true;
                //  The Host has accepted the earlier disposal. This save must
                //  fail whether that disposal is still waiting or has completed.
                if (!post_save(context, asset, 300))
                {
                    return false;
                }
                continue;
            }
            if ((slot != 30) || disposed || (disposal.asset != asset) ||
                (disposal.status != EAssetStatus::success) || (save_count != k_save_count))
            {
                return false;
            }
            disposed = true;
        }
        else if (message.copy_payload_to(result))
        {
            if (slot == 300)
            {
                if (!duplicate_rejected || rejected_save || (result.status != EAssetStatus::invalid_asset) || result.asset)
                {
                    return false;
                }
                rejected_save = true;
            }
            else
            {
                const std::int32_t index = slot - 200;
                if ((index < 0) || (index >= k_save_count) || saved[index] || disposed ||
                    (result.status != EAssetStatus::success) || (result.asset != asset))
                {
                    return false;
                }
                saved[index] = true;
                ++save_count;
            }
        }
        else
        {
            return false;
        }
    }
    return dispose_asset(context, asset, EAssetStatus::invalid_asset);
}

static bool run_case(threading::CThreadContext& context, const char* const selected) noexcept
{
    if (std::strcmp(selected, "replace") == 0)
    {
        return retain_raw_asset(context, false) && self_terminate(context, "MorphicExecutive.dll");
    }
    if (std::strcmp(selected, "replace-startup-failure") == 0)
    {
        return (_putenv_s("MORPHIC_LIFECYCLE_CASE", "startup-failure") == 0) &&
            self_terminate(context, "MorphicLifecycleExecutive.dll");
    }
    if (std::strcmp(selected, "replace-missing") == 0)
    {
        return self_terminate(context, "MorphicMissingExecutive.dll");
    }
    if (std::strcmp(selected, "shutdown") == 0)
    {
        return self_terminate(context, nullptr);
    }
    if (std::strcmp(selected, "replace-dependency") == 0)
    {
        return retain_raw_asset(context, true, nullptr, mount_point_ids::executive) &&
            self_terminate(context, "MorphicExecutive.dll");
    }
    if (std::strcmp(selected, "shutdown-dependency") == 0)
    {
        return retain_raw_asset(context, true, nullptr, mount_point_ids::executive) && self_terminate(context, nullptr);
    }
    if (std::strcmp(selected, "disposal") == 0)
    {
        CAssetId asset;
        return operation(context, EModuleAction::load, "MorphicLifecycleService.dll", 1, EModuleStatus::success, true) &&
            retain_raw_asset(context, true, &asset) && dispose_asset(context, asset, EAssetStatus::success) &&
            dispose_asset(context, asset, EAssetStatus::invalid_asset) &&
            dispose_asset(context, CAssetId{}, EAssetStatus::invalid_asset) &&
            operation(context, EModuleAction::unload, nullptr, 2, EModuleStatus::success, false) && self_terminate(context, nullptr);
    }
    if (std::strcmp(selected, "disposal-during-save") == 0)
    {
        return operation(context, EModuleAction::load, "MorphicLifecycleService.dll", 1, EModuleStatus::success, true) &&
            disposal_during_saves(context) &&
            operation(context, EModuleAction::unload, nullptr, 2, EModuleStatus::success, false) && self_terminate(context, nullptr);
    }
    if (std::strcmp(selected, "dependency") == 0)
    {
        CAssetId dependent;
        CAssetId independent;
        return operation(context, EModuleAction::load, "MorphicLifecycleService.dll", 1, EModuleStatus::success, true) &&
            retain_raw_asset(context, true, &dependent) && retain_raw_asset(context, false, &independent) &&
            operation(context, EModuleAction::unload, nullptr, 2, EModuleStatus::success, false) &&
            dispose_asset(context, dependent, EAssetStatus::invalid_asset) &&
            dispose_asset(context, independent, EAssetStatus::success) && self_terminate(context, nullptr);
    }
    if (std::strcmp(selected, "ordinary") != 0)
    {
        return false;
    }
    return operation(context, EModuleAction::load, "MorphicLifecycleService.dll", 1, EModuleStatus::success, true) &&
        operation(context, EModuleAction::load, "MorphicLifecycleService.dll", 2, EModuleStatus::already_loaded, true) &&
        operation(context, EModuleAction::replace, "MorphicLifecycleService.dll", 3, EModuleStatus::success, true) &&
        operation(context, EModuleAction::unload, nullptr, 4, EModuleStatus::success, false) &&
        operation(context, EModuleAction::unload, nullptr, 5, EModuleStatus::not_loaded, false) &&
        operation(context, EModuleAction::load, "MorphicMissingService.dll", 6, EModuleStatus::binding_failed, false) &&
        operation(context, EModuleAction::load, "MorphicLifecycleService.dll", 7, EModuleStatus::function_unavailable, false,
            system_type_ids::executive_thread_function) &&
        operation(context, EModuleAction::load, "MorphicLifecycleService.dll", 8, EModuleStatus::success, true) &&
        operation(context, EModuleAction::replace, "MorphicMissingService.dll", 9, EModuleStatus::binding_failed, false) &&
        self_terminate(context, nullptr);
}

static std::uint32_t MV_STD_ABI_CALL executive_entry(void* const data) noexcept
{
    threading::CThreadContext context{ *static_cast<threading::CThreadResources*>(data) };
    context.startup();
    char selected[32]{};
    std::size_t required{ 0u };
    const bool configured = (getenv_s(&required, selected, sizeof(selected), "MORPHIC_LIFECYCLE_CASE") == 0) && (required != 0u);
    if (configured && (std::strcmp(selected, "startup-failure") == 0))
    {
        context.mark_failed(1u);
        return 1u;
    }
    context.mark_running();
    const bool success = configured && run_case(context, selected);
    if (success)
    {
        MV_REPORT("Lifecycle fixture: %s passed", selected);
        context.mark_exiting();
        context.mark_exited();
    }
    else
    {
        MV_REPORT("Lifecycle fixture failed");
        context.mark_failed(1u);
    }
    return success ? 0u : 1u;
}
#else
static constexpr auto k_advertised_module_id = module_ids::conditioning_general;
#endif

static constexpr std::uint32_t k_advertised_version_minor{ 0u };

static modules::EBindingResult MV_STD_ABI_CALL query_function(const system_type_id type,
    const std::uint32_t functional_major, modules::FModuleFunction* const function) noexcept
{
    (void)type;
    (void)functional_major;
    if (function == nullptr)
    {
        return modules::EBindingResult::invalid_argument;
    }
    *function = nullptr;
#if MV_LIFECYCLE_EXECUTIVE
    if (type == system_type_ids::executive_thread_function)
    {
        *function = reinterpret_cast<modules::FModuleFunction>(&executive_entry);
        return modules::EBindingResult::success;
    }
#endif
    return modules::EBindingResult::unsupported_function;
}

static constexpr modules::SModuleBindingConfig k_binding_config = MV_MODULE_BINDING_CONFIG();
static modules::CModuleBindingContext s_binding{ k_binding_config };

}   //  namespace module_lifecycle_tests

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v3(
    modules::SBootstrapFunctions* const functions) noexcept
{
    return module_lifecycle_tests::s_binding.bootstrap(functions);
}
