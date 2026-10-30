
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    request_preparation.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Transactional request preparation, independent of posting and retry policy.

#include "runtime/request_preparation.hpp"
#include "debug/macros.hpp"

namespace executive::request_preparation
{

bool can_prepare_request(const threading::CErasedOwnerMsg& pending, const std::int32_t slot) noexcept
{
    if (pending.has_message_type() || pending.has_owner())
    {
        MV_ASSERT_REPORT("Executive: preparation at slot {} would replace pending message at slot {}",
            slot, pending.query_async_slot());
        return false;
    }
    return true;
}

bool prepare_load_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const EAssetFileFormat format, const char* const file, const CDocumentParseOptions policy,
    const bool decode_top_down) noexcept
{
    if (!can_prepare_request(pending, slot)) return false;
    auto candidate = threading::make_owner_message<AssetLoadRequest>(slot);
    auto* const request = candidate.owner().payload<AssetLoadRequest>();
    if ((request == nullptr) || !request->file.set(file))
    {
        MV_LOG_RICH(error, "Executive: load request preparation failed at slot %d for %s",
            slot, (file != nullptr) ? file : "<no file>");
        return false;
    }
    request->format = format;
    request->alignment = 32u;
    request->policy = policy;
    request->decode_top_down = decode_top_down;
    pending = std::move(candidate);
    return true;
}

bool prepare_save_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const CAssetId asset, const char* const file, const AssetSaveSettings& settings) noexcept
{
    if (!can_prepare_request(pending, slot)) return false;
    auto candidate = threading::make_owner_message<AssetSaveRequest>(slot);
    auto* const request = candidate.owner().payload<AssetSaveRequest>();
    if ((request == nullptr) || !request->file.set(file))
    {
        MV_LOG_RICH(error, "Executive: save request preparation failed at slot %d for %s",
            slot, (file != nullptr) ? file : "<no file>");
        return false;
    }
    request->source = asset;
    request->settings = settings;
    pending = std::move(candidate);
    return true;
}

bool prepare_module_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const module_ids::id_type module, const EModuleAction action, const char* const file,
    const bool require_service_thread) noexcept
{
    if (!can_prepare_request(pending, slot)) return false;
    auto candidate = threading::make_owner_message<ModuleRequest>(slot);
    auto* const request = candidate.owner().payload<ModuleRequest>();
    if ((request == nullptr) || ((file != nullptr) && !request->file.set(file)))
    {
        MV_LOG_RICH(error, "Executive: module request preparation failed at slot %d for %s",
            slot, (file != nullptr) ? file : "<no file>");
        return false;
    }
    request->module = module;
    request->action = action;
    request->require_service_thread = require_service_thread;
    pending = std::move(candidate);
    return true;
}

bool prepare_filesystem_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const char* const root) noexcept
{
    if (!can_prepare_request(pending, slot)) return false;
    auto candidate = threading::make_owner_message<FilesystemRefreshRequest>(slot);
    auto* const request = candidate.owner().payload<FilesystemRefreshRequest>();
    if ((request == nullptr) || !request->root.set(root))
    {
        MV_LOG_RICH(error, "Executive: filesystem request preparation failed at slot %d for %s",
            slot, (root != nullptr) ? root : "<no root>");
        return false;
    }
    pending = std::move(candidate);
    return true;
}

bool prepare_bundle_load_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const char* const file, const char* const root) noexcept
{
    if (!can_prepare_request(pending, slot)) return false;
    auto candidate = threading::make_owner_message<BundleLoadRequest>(slot);
    auto* const request = candidate.owner().payload<BundleLoadRequest>();
    if ((request == nullptr) || !request->paths.file.set(file) || !request->paths.root.set(root))
    {
        MV_LOG_RICH(error, "Executive: bundle load preparation failed at slot %d for %s into %s",
            slot, (file != nullptr) ? file : "<no file>", (root != nullptr) ? root : "<no root>");
        return false;
    }
    pending = std::move(candidate);
    return true;
}

bool prepare_bundle_bake_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const char* const source_root, const char* const root) noexcept
{
    if (!can_prepare_request(pending, slot)) return false;
    auto candidate = threading::make_owner_message<BundleBakeRequest>(slot);
    auto* const request = candidate.owner().payload<BundleBakeRequest>();
    if ((request == nullptr) || !request->paths.source_root.set(source_root) || !request->paths.root.set(root))
    {
        MV_LOG_RICH(error, "Executive: bundle bake preparation failed at slot %d from %s into %s",
            slot, (source_root != nullptr) ? source_root : "<no source>", (root != nullptr) ? root : "<no root>");
        return false;
    }
    pending = std::move(candidate);
    return true;
}

}   //  namespace executive::request_preparation
