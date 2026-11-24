
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_binding.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Aug 26
//
//  Executive module identity, exported functions, and bootstrap entry point.

#include "framework/system/system_ids.hpp"
#include <cstdint>      //  std::uint32_t

#include "framework/module/module_binding.hpp"

#include "module/types/local_type_ids.hpp"
#include "runtime/executive_execution.hpp"
#include "framework/module/module_binding_context.hpp"

namespace executive::module_binding
{

//  Module-local configuration.
static constexpr module_ids::id_type k_advertised_module_id{ module_ids::executive };
static constexpr std::uint32_t k_advertised_version_minor{ 0u };

static modules::EBindingResult MV_STD_ABI_CALL query_function(
    const system_type_id function_type,
    const std::uint32_t functional_major,
    modules::FModuleFunction* const function) noexcept
{
    (void)functional_major;

    if (function == nullptr)
    {
        return modules::EBindingResult::invalid_argument;
    }
    *function = nullptr;

    (void)function_type;
    return modules::EBindingResult::unsupported_function;
}

static constexpr modules::SModuleBindingConfig k_binding_config = MV_MODULE_BINDING_CONFIG();

static modules::CModuleBindingContext s_binding{ k_binding_config };

}   //  namespace executive::module_binding

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v1(
    modules::SBootstrapFunctions* const functions) noexcept
{
    return executive::module_binding::s_binding.bootstrap(functions);
}
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_execution_v1(
    const std::uint32_t capacity, modules::SExecutionV1* const execution) noexcept
{
    if (!execution || (capacity != sizeof(*execution)))
    {
        return modules::EBindingResult::invalid_argument;
    }
    static const modules::SExecutionV1 service = executive::executive_execution();
    static const modules::SChildExecution child{ &service, nullptr, thread_ids::executive };
    *execution = {};
    execution->initial_child = &child;
    return modules::EBindingResult::success;
}
