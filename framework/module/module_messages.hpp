
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Module lifecycle messages.

#pragma once

#include "framework/system/system_ids.hpp"
#include "foundation/system/system_type_registration.hpp"
#include "framework/module/module_binding.hpp"
#include "foundation/containers/StringBuffers.hpp"
#include "foundation/system/erased_owner_registration.hpp"

namespace modules
{

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
    bool require_host_pump{ false };      //  Mutually exclusive with require_service_thread in v1.
};

struct ModuleResult
{
    module_ids::id_type module{};
    modules::FModuleFunction function{ nullptr };
    EModuleNotice notice{ EModuleNotice::completed };
    EModuleStatus status{ EModuleStatus::invalid_request };
    bool available{ false };
};

}   //  namespace modules

MV_REGISTER_SYSTEM_TYPE(modules::ModuleRequest, system_type_ids::module_request);
MV_REGISTER_SYSTEM_TYPE(modules::ModuleResult, system_type_ids::module_result);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "framework/module/module_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD
