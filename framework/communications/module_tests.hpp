
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
#include "system/system_type_registration.hpp"

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
//  These application conversations know their receiving execution addresses.
[[nodiscard]] inline system_ids::id_type address(const module_ids::id_type module) noexcept
{
    const thread_ids::id_type thread = (module == module_ids::executive) ? thread_ids::executive :
        ((module == module_ids::rendering) ? thread_ids::rendering : thread_ids::audio);
    return system_ids::ops::make_system_id(module, thread);
}

}   //  namespace test_connections

}   //  namespace modules

MV_REGISTER_SYSTEM_TYPE(modules::RunTestsRequest, system_type_ids::run_tests_request);
MV_REGISTER_SYSTEM_TYPE(modules::RunTestsResult, system_type_ids::run_tests_result);
