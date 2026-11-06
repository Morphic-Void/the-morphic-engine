
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_execution_context.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Immutable borrowed execution facilities installed before exact-client startup.

#pragma once

#include "module/host_restriction.hpp"
#include "services/service_protocol.hpp"
#include "threading/CNotification.hpp"

namespace modules
{

struct SHostRestrictionSubscription { std::uint64_t value{ 0u }; };

struct SHostStopEndpoint
{
    std::atomic<std::uint32_t>* requested{ nullptr };
    platform::threading::SWakeEndpoint wake;
    [[nodiscard]] bool request() const noexcept
    {
        if (!requested) return false;
        requested->store(1u, std::memory_order_release);
        return !wake.is_valid() || wake.signal();
    }
};

struct SHostExecutionContext
{
    SModuleInstance instance;
    services::SClientToken client;
    SHostRestrictionSubscription subscription;
    SHostRestrictionReader restriction;
    threading::SNotificationReader notification;
    SHostStopEndpoint stop;
};

enum class EMaintenanceStatus : std::uint32_t { success, invalid_context, reentrant, failed };
struct SHostMaintenanceResult
{
    EMaintenanceStatus status{ EMaintenanceStatus::invalid_context };
    std::uint32_t operations{ 0u };
    std::uint32_t continuation{ 0u };
    std::uint32_t capacity_pressure{ 0u };
    std::uint32_t shutdown_requested{ 0u };
    std::uint32_t cancel_native{ 0u };
};

}   //  namespace modules
