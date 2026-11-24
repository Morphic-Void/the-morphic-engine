
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_lifecycle.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Host-private lifecycle records and independent publication frontiers.

#pragma once

#include "framework/module/module_execution.hpp"
#include "host/runtime/execution/module_pipes.hpp"

namespace host
{

struct SDirectEndpointDescriptor;

enum class ELifecycle : std::uint32_t
{
    create_child, child_created, child_ready, child_ended, child_retired, stop_child, child_stop_result,
    ready, stop, close_provider_admission, provider_admission_closed, seal, sealed,
    participate, participated, connect, offer, decision, install, installed,
    activate, activated, close, closing, quiescent, retired, connection_result, stop_module
};

struct SPublicationFrontier
{
    std::uint64_t pods{ 0u }, owners{ 0u }, host_output{ 0u };
};

struct SLifecycleMessage
{
    ELifecycle operation{ ELifecycle::stop };
    std::int32_t correlation{ -1 };
    modules::SExecutionNotification notification;
    modules::SChildExecution child;
    system_ids::id_type first_address{}, second_address{};
    const SDirectEndpointDescriptor* descriptor{ nullptr }; //  Retained Host storage only.
    SPublicationFrontier frontier;
};

static_assert(std::is_trivially_copyable_v<SLifecycleMessage>);

struct SModuleLifecycle
{
    threading::transports::TRing<SLifecycleMessage> to_preamble, to_host;

    [[nodiscard]] bool initialise(memory::CMemoryContext& context,
        const std::uint32_t capacity = k_module_lifecycle_capacity) noexcept
    {
        return to_preamble.initialise(capacity, &context) && to_host.initialise(capacity, &context);
    }
};

}   //  namespace host
