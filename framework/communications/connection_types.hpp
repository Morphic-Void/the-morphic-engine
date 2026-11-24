
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    connection_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Public connection identities, consent and token-based bulk contracts.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/services/service_protocol.hpp"

namespace connections
{

struct SToken { std::uint64_t value{ 0u }; };
enum class EStatus : std::uint32_t
{
    success, invalid_request, unavailable, unsupported, version_mismatch, rejected,
    busy, already_connected, closing, cancelled, timed_out, allocation_failed,
    transport_failed, notification_failed, configuration_mismatch
};

struct SBulkConfiguration
{
    std::uint64_t capacity{ 0u };
    module_ids::id_type producer{};

    [[nodiscard]] bool matches(const SBulkConfiguration& other) const noexcept
    {
        return (capacity == other.capacity) && (producer == other.producer);
    }
    [[nodiscard]] bool is_bulk() const noexcept { return capacity != 0u; }
};

struct SSource
{
    enum class EKind : std::uint8_t { host, direct, self };
    EKind kind{ EKind::host };
    system_ids::id_type address{ system_ids::host };
    services::SClientToken endpoint;
    SToken connection;
    [[nodiscard]] bool is_host() const noexcept { return kind == EKind::host; }
};

enum class EBulkPublish : std::uint8_t { published, inactive, published_wake_failed };

}   //  namespace connections
