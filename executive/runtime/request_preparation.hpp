
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    request_preparation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Executive request preparation. Failure leaves caller-owned state unchanged.

#pragma once

#ifndef EXECUTIVE_REQUEST_PREPARATION_HPP_INCLUDED
#define EXECUTIVE_REQUEST_PREPARATION_HPP_INCLUDED

#include "assets/asset_messages.hpp"
#include "debug/macros.hpp"
#include "system/service_messages.hpp"
#include "image/texture_messages.hpp"
#include "threading/messages/message_helpers.hpp"

namespace executive::request_preparation
{

//  Also used before fixture generation, so occupied destinations fail immediately.
[[nodiscard]] bool can_prepare_request(const threading::CErasedOwnerMsg& pending, const std::int32_t slot) noexcept;

[[nodiscard]] bool prepare_load_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const EAssetFileFormat format, const char* file, const CDocumentParseOptions policy = {},
    const bool decode_top_down = true) noexcept;

[[nodiscard]] bool prepare_save_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const CAssetId asset, const char* file, const AssetSaveSettings& settings) noexcept;

[[nodiscard]] bool prepare_module_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const module_ids::id_type module, const EModuleAction action, const char* file = nullptr) noexcept;

[[nodiscard]] bool prepare_filesystem_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const char* root) noexcept;

[[nodiscard]] bool prepare_bundle_load_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const char* file, const char* root) noexcept;

[[nodiscard]] bool prepare_bundle_bake_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const char* source_root, const char* root) noexcept;

[[nodiscard]] bool prepare_texture_decode_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const CAssetId source, const image::texture::EStorageFormat target, const std::uint32_t level = 0u) noexcept;

//  The public transfer contract already supplies the resource-to-request mapping.
//  Only after every fallible step succeeds may the caller's storage move.
template<typename TResource>
[[nodiscard]] bool prepare_asset_transfer(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    TResource& source, const EAssetRetention retention, const bool save, const char* file,
    const AssetSaveSettings& settings) noexcept
{
    if (!can_prepare_request(pending, slot)) return false;
    using TRequest = TAssetTransferRequest<TResource>;
    auto candidate = threading::make_owner_message<TRequest>(slot);
    auto* const request = candidate.owner().template payload<TRequest>();
    if ((request == nullptr) || (save && !request->storage.file.set(file)))
    {
        MV_LOG_RICH(error, "Executive: asset transfer preparation failed at slot %d for %s",
            slot, (file != nullptr) ? file : "<no file>");
        return false;
    }
    request->settings = settings;
    request->save = save;
    request->retention = retention;
    request->storage.value = std::move(source);
    pending = std::move(candidate);
    return true;
}

}   //  namespace executive::request_preparation

#endif  //  EXECUTIVE_REQUEST_PREPARATION_HPP_INCLUDED
