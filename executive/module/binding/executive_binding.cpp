
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_binding.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Aug 26
//
//  Executive module identity, exported functions, and bootstrap entry point.

#include <cstdint>      //  std::uint32_t

#include "module/binding/executive_binding.hpp"

#include "module/types/local_type_ids.hpp"
#include "runtime/executive_thread.hpp"
#include "module/module_binding_context.hpp"

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

    if (function_type != system_type_ids::executive_thread_function)
    {
        return modules::EBindingResult::unsupported_function;
    }

    *function = reinterpret_cast<modules::FModuleFunction>(executive_thread_entry_point());
    return modules::EBindingResult::success;
}

static constexpr modules::SModuleBindingConfig k_binding_config = MV_MODULE_BINDING_CONFIG();

static modules::CModuleBindingContext s_binding{ k_binding_config };

}   //  namespace executive::module_binding

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v3(
    modules::SBootstrapFunctions* const functions) noexcept
{
    return executive::module_binding::s_binding.bootstrap(functions);
}
