
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_protocol.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Execution-independent routed service messages and provider dispatch context.

#pragma once

#include "framework/system/system_ids.hpp"
#include <cstdint>
#include "framework/module/module_identity.hpp"
#include "foundation/system/system_type_registration.hpp"

namespace services
{

struct SClientToken { std::uint64_t value{ 0u }; };
struct SServiceRoute { std::uint64_t value{ 0u }; };
struct SServiceRequestId { std::uint64_t value{ 0u }; };

enum class EServiceStatus : std::uint16_t
{
    success = 0u, invalid_request, unavailable, unsupported, busy, occupied,
    already_connected, closing, stale, timed_out, cancelled, allocation_failed,
    native_failure, protocol_failure, delivery_failed
};
enum class EServiceNotice : std::uint16_t { acknowledged = 0u, prepared, completed, event };

struct SServiceContract
{
    std::uint32_t number{ 0u };
    std::uint32_t version{ 0u };
};

struct OpenServiceRequest
{
    module_ids::id_type module{};
    std::uint32_t contract{ 0u };
    std::uint32_t version{ 0u };
    SServiceRequestId request;
};

struct OpenServiceResult
{
    SServiceRequestId request;
    SServiceRoute route;
    modules::SModuleInstance provider;
    EServiceStatus status{ EServiceStatus::invalid_request };
};

struct CloseServiceRequest
{
    SServiceRoute route;
    SServiceRequestId request;
};

struct alignas(8) ServiceRequest
{
    SServiceRoute route;
    SServiceRequestId request;
    std::uint16_t opcode{ 0u };
    std::uint16_t flags{ 0u };
    std::uint16_t payload_size{ 0u };
    std::uint16_t reserved{ 0u };
    unsigned char payload[24]{};
};

struct alignas(8) ServiceReply
{
    SServiceRoute route;
    SServiceRequestId request;
    std::uint16_t opcode{ 0u };
    EServiceNotice notice{ EServiceNotice::completed };
    EServiceStatus status{ EServiceStatus::invalid_request };
    std::uint16_t payload_size{ 0u };
    unsigned char payload[24]{};
};

struct SDispatchedCommand
{
    ServiceRequest command;
    modules::SModuleInstance requester;
    SClientToken client;
    SServiceContract contract;
};

static_assert((sizeof(ServiceRequest) == 48u) && (alignof(ServiceRequest) == 8u));
static_assert((sizeof(ServiceReply) == 48u) && (alignof(ServiceReply) == 8u));

}   //  namespace services

MV_REGISTER_SYSTEM_TYPE(services::OpenServiceRequest, system_type_ids::open_service_request);
MV_REGISTER_SYSTEM_TYPE(services::OpenServiceResult, system_type_ids::open_service_result);
MV_REGISTER_SYSTEM_TYPE(services::CloseServiceRequest, system_type_ids::close_service_request);
MV_REGISTER_SYSTEM_TYPE(services::ServiceRequest, system_type_ids::service_request);
MV_REGISTER_SYSTEM_TYPE(services::ServiceReply, system_type_ids::service_reply);
