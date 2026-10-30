
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Public module and filesystem services, common storage identities and fallback.

#pragma once

#ifndef SERVICE_MESSAGES_HPP_INCLUDED
#define SERVICE_MESSAGES_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "module/module_binding.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/system_type_registration.hpp"

//==============================================================================
//  Transport fallback
//==============================================================================

struct UnrecognisedMsg { system_type_id msg_id; };

//==============================================================================
//  Module tests: the destination owns test selection, execution and diagnostics.
//  A correlated completion follows all accepted work and fixture cleanup.
//==============================================================================

enum class ETestRunStatus : std::uint8_t { success = 0, failed, busy, unavailable, invalid_request, cancelled, delivery_failed };
struct RunTestsRequest { module_ids::id_type module{}; };
struct RunTestsResult
{
    module_ids::id_type module{};
    ETestRunStatus status{ ETestRunStatus::invalid_request };
    std::uint32_t passed{ 0u };
    std::uint32_t failed{ 0u };
    std::uint32_t skipped{ 0u };
};

//==============================================================================
//  Module lifecycle: owning client requests and correlated notifications
//==============================================================================

enum class EModuleAction : std::uint8_t { load = 0, unload, replace };
enum class EModuleNotice : std::uint8_t { acknowledged = 0, completed };

enum class EModuleStatus : std::uint8_t
{
    success = 0,
    invalid_request,
    busy,
    not_loaded,
    already_loaded,
    allocation_failed,
    binding_failed,
    installation_failed,
    function_unavailable,
    unload_failed,
    delivery_failed
};

//  The module identifies the implementation to load, or the current implementation
//  to unload. Replacement occupies the same mounting point, without rollback.
struct ModuleRequest
{
    CSimpleString file;
    module_ids::id_type module{};
    system_type_id required_function{ system_type_ids::undefined };
    EModuleAction action{ EModuleAction::load };
    bool require_service_thread{ false }; //  Passive modules otherwise remain valid load destinations.
};

struct ModuleResult
{
    module_ids::id_type module{};
    modules::FModuleFunction function{ nullptr };
    EModuleNotice notice{ EModuleNotice::completed };
    EModuleStatus status{ EModuleStatus::invalid_request };
    bool available{ false };
};

//  Executive refreshes name one logical root, e.g. "dev-source:". Completion
//  follows Host integration, not merely worker enumeration.
enum class EFilesystemStatus : std::uint8_t { success = 0, invalid_root, queue_full, scan_failed, integration_failed, delivery_failed };
struct FilesystemRefreshRequest { CSimpleString root; };
struct FilesystemRefreshResult { EFilesystemStatus status{ EFilesystemStatus::scan_failed }; };

//  Type bindings precede owning-payload eligibility.
MV_REGISTER_SYSTEM_TYPE(UnrecognisedMsg, system_type_ids::unrecognised_msg);
MV_REGISTER_SYSTEM_TYPE(RunTestsRequest, system_type_ids::run_tests_request);
MV_REGISTER_SYSTEM_TYPE(RunTestsResult, system_type_ids::run_tests_result);
MV_REGISTER_SYSTEM_TYPE(CByteBuffer, system_type_ids::byte_buffer);
MV_REGISTER_SYSTEM_TYPE(CByteRectBuffer, system_type_ids::byte_rect_buffer);
MV_REGISTER_SYSTEM_TYPE(CSimpleString, system_type_ids::simple_string);
MV_REGISTER_SYSTEM_TYPE(CStringBuffer, system_type_ids::string_buffer);
MV_REGISTER_SYSTEM_TYPE(CStableStrings, system_type_ids::stable_strings);
MV_REGISTER_SYSTEM_TYPE(ModuleRequest, system_type_ids::module_request);
MV_REGISTER_SYSTEM_TYPE(ModuleResult, system_type_ids::module_result);
MV_REGISTER_SYSTEM_TYPE(FilesystemRefreshRequest, system_type_ids::filesystem_refresh_request);
MV_REGISTER_SYSTEM_TYPE(FilesystemRefreshResult, system_type_ids::filesystem_refresh_result);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "system/service_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

#endif  //  SERVICE_MESSAGES_HPP_INCLUDED
