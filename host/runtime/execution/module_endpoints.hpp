
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_endpoints.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Host-owned, address-stable per-module endpoint and creation-outcome records.

#pragma once

#include "runtime/execution/pumped_client.hpp"
#include "runtime/services/service_router.hpp"
#include "foundation/containers/TUnorderedCollection.hpp"

namespace host
{

struct SRequestedThread
{
    thread_ids::id_type thread{};
    modules::SExecutionV1 execution;
    SClientRecord* requester{ nullptr };
    services::SClientToken origin;
    services::SClientToken identity;
    std::int32_t correlation{ -1 };
    TInstance<threading::CThreadPackage> package;
    TInstance<SClientRecord> client;
    services::EServiceStatus outcome{ services::EServiceStatus::busy };
    bool outcome_ready{ false }, creation_sent{ false };
    bool ended_sent{ false }, retired{ false }, joined{ false };
    bool ready_observed{ false }, ready_sent{ false };
};

struct SModuleEndpoints
{
    threading::SModuleEndpointState state;
    modules::CBoundModule* binding{ nullptr };
    TInstance<SClientRecord> coordinator;
    TInstance<CPumpedClient> pump;
    SServiceProvider provider;
    TUnorderedCollection<SRequestedThread> threads;
    services::SClientToken designated_service;
    bool ready{ false };
};

}   //  namespace host
