
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    endpoint_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Ordinary endpoint lifecycle exchanges; no separate control transport.

#pragma once

#include "framework/services/service_protocol.hpp"
#include "foundation/platform/platform_defines.hpp"
#include "foundation/platform/threading/thread_priority.hpp"

namespace threading
{

class CThreadContext;
using FEndpointThreadEntry = std::uint32_t(MV_STD_ABI_CALL*)(CThreadContext& context, void* const module_context) noexcept;

struct CreateThreadRequest
{
    FEndpointThreadEntry entry{ nullptr };
    void* context{ nullptr };
    thread_ids::id_type thread{};
    platform::threading::EThreadPriority priority{ platform::threading::EThreadPriority::Normal };
    std::uint32_t designated_service{ 0u };
};

struct CreateThreadResult
{
    services::SClientToken endpoint;
    services::EServiceStatus status{ services::EServiceStatus::invalid_request };
};

enum class EEndpointLifecycle : std::uint32_t { created, execution_ended, retired };
struct EndpointLifecycle
{
    services::SClientToken requester;
    services::SClientToken endpoint;
    thread_ids::id_type thread{};
    std::int32_t correlation{ -1 };
    EEndpointLifecycle event{ EEndpointLifecycle::created };
    services::EServiceStatus status{ services::EServiceStatus::invalid_request };
};

struct StopThreadRequest { services::SClientToken endpoint; };
//  Child publishes after local initialization; Host forwards its exact identity.
struct EndpointReady { services::SClientToken endpoint; };
struct StopThreadResult { services::SClientToken endpoint; services::EServiceStatus status{}; };
struct CloseProviderAdmission {};
struct ProviderAdmissionClosed {};
struct SealEndpoint { std::uint64_t pods{ 0u }; std::uint64_t owners{ 0u }; };
struct EndpointSealed {};

static_assert(sizeof(CreateThreadRequest) <= 48u);
static_assert(sizeof(EndpointLifecycle) <= 48u);

}   //  namespace threading

MV_REGISTER_SYSTEM_TYPE(threading::CreateThreadRequest, system_type_ids::create_thread_request);
MV_REGISTER_SYSTEM_TYPE(threading::CreateThreadResult, system_type_ids::create_thread_result);
MV_REGISTER_SYSTEM_TYPE(threading::EndpointLifecycle, system_type_ids::endpoint_lifecycle);
MV_REGISTER_SYSTEM_TYPE(threading::StopThreadRequest, system_type_ids::stop_thread_request);
MV_REGISTER_SYSTEM_TYPE(threading::StopThreadResult, system_type_ids::stop_thread_result);
MV_REGISTER_SYSTEM_TYPE(threading::CloseProviderAdmission, system_type_ids::close_provider_admission);
MV_REGISTER_SYSTEM_TYPE(threading::ProviderAdmissionClosed, system_type_ids::provider_admission_closed);
MV_REGISTER_SYSTEM_TYPE(threading::SealEndpoint, system_type_ids::seal_endpoint);
MV_REGISTER_SYSTEM_TYPE(threading::EndpointSealed, system_type_ids::endpoint_sealed);
MV_REGISTER_SYSTEM_TYPE(threading::EndpointReady, system_type_ids::endpoint_ready);
