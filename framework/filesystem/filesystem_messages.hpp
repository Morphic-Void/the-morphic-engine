
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    filesystem_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Logical filesystem refresh service messages.

#pragma once

#include "framework/system/system_ids.hpp"
#include "foundation/system/system_type_registration.hpp"
#include "foundation/containers/StringBuffers.hpp"
#include "foundation/system/erased_owner_registration.hpp"

namespace filesystem
{

//  Executive refreshes name one logical root, e.g. "dev-source:". Completion
//  follows Host integration, not merely worker enumeration.
enum class EFilesystemStatus : std::uint8_t { success = 0, invalid_root, queue_full, scan_failed, integration_failed, delivery_failed };
struct FilesystemRefreshRequest { CSimpleString root; };
struct FilesystemRefreshResult { EFilesystemStatus status{ EFilesystemStatus::scan_failed }; };

}   //  namespace filesystem

MV_REGISTER_SYSTEM_TYPE(filesystem::FilesystemRefreshRequest, system_type_ids::filesystem_refresh_request);
MV_REGISTER_SYSTEM_TYPE(filesystem::FilesystemRefreshResult, system_type_ids::filesystem_refresh_result);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "framework/filesystem/filesystem_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD
