
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_tests.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Executive-owned test conversations with Rendering and Audio service threads.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/communications/direct_connection.hpp"

namespace modules
{

//  Completion follows all accepted work and fixture cleanup. Closing cancels
//  the conversation locally; no terminal data reply is required after closing.
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

namespace test_connections
{
static constexpr std::uint32_t version = 1u;
static const connections::SMessageRule rendering_rules[]{
    { system_type_ids::run_tests_request, module_ids::executive },
    { system_type_ids::run_tests_result, module_ids::rendering }
};
static const connections::SMessageRule audio_rules[]{
    { system_type_ids::run_tests_request, module_ids::executive },
    { system_type_ids::run_tests_result, module_ids::audio }
};
static const connections::SContractPolicy policies[]{
    { connections::contract(module_ids::executive, module_ids::rendering, version), rendering_rules, 2u },
    { connections::contract(module_ids::executive, module_ids::audio, version), audio_rules, 2u }
};
static constexpr std::uint32_t policy_count = 2u;
}   //  namespace test_connections

}   //  namespace modules

MV_REGISTER_SYSTEM_TYPE(modules::RunTestsRequest, system_type_ids::run_tests_request);
MV_REGISTER_SYSTEM_TYPE(modules::RunTestsResult, system_type_ids::run_tests_result);
