
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    audio_binding.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Audio module identity, exported functions, and bootstrap entry point.

#include <cstdint>      //  std::uint32_t

#include "module/module_binding.hpp"

#include "module/types/local_type_ids.hpp"
#include "runtime/audio_thread.hpp"
#include "module/module_binding_context.hpp"

namespace audio::module_binding
{

//  Module-local configuration.
static constexpr module_ids::id_type k_advertised_module_id{ module_ids::audio };
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

}   //  namespace audio::module_binding

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v3(
    modules::SBootstrapFunctions* const functions) noexcept
{
    return audio::module_binding::s_binding.bootstrap(functions);
}

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_service_thread_v1(
    modules::SServiceThread* const service) noexcept
{
    if (service == nullptr) return modules::EBindingResult::invalid_argument;
    *service = { thread_ids::audio, audio::audio_thread_entry_point() };
    return modules::EBindingResult::success;
}
