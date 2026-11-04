
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_binding.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Exposes Platform module binding, service discovery and Host-pumped lifecycle
//  entry points, and owns the runtime instance through activation and shutdown.

#include "module/types/local_type_ids.hpp"
#include "module/module_binding_context.hpp"
#include "module/service_provider.hpp"
#include "runtime/platform_runtime.hpp"
#include "containers/TInstance.hpp"

namespace platform::module_binding
{

static constexpr module_ids::id_type k_advertised_module_id{ module_ids::platform };
static constexpr std::uint32_t k_advertised_version_minor{ 0u };
static modules::EBindingResult MV_STD_ABI_CALL query_function(const system_type_id,
    const std::uint32_t, modules::FModuleFunction* const function) noexcept
{
    if (!function) return modules::EBindingResult::invalid_argument;
    *function = nullptr;
    return modules::EBindingResult::unsupported_function;
}
static constexpr modules::SModuleBindingConfig k_binding_config = MV_MODULE_BINDING_CONFIG();
static modules::CModuleBindingContext s_binding{ k_binding_config };

static TInstance<runtime::CPlatformRuntime>& runtime_state() noexcept
{
    static TInstance<runtime::CPlatformRuntime> state;
    return state;
}

static modules::ECallStatus MV_STD_ABI_CALL activate(const modules::SHostPumpContext* const context,
    void** const runtime, modules::SProgress* const progress) noexcept
{
    if (!context || !runtime || !progress || (context->size != sizeof(*context)) || (context->version != 1u) ||
        !context->services || (context->services->size != sizeof(*context->services)) || (context->services->version != 1u) ||
        !s_binding.is_thread_context_ready(context) || runtime_state()) return modules::ECallStatus::invalid_argument;
    *runtime = nullptr;
    if (!runtime_state().emplace(*context)) return modules::ECallStatus::failed;
    if (!runtime_state()->initialise()) { runtime_state().reset(); return modules::ECallStatus::failed; }
    *runtime = &*runtime_state();
    *progress = {};
    progress->phase = modules::EPumpPhase::ready;
    return modules::ECallStatus::success;
}

static modules::ECallStatus MV_STD_ABI_CALL pump(void* const runtime, const modules::SPumpInput* const input,
    modules::SPumpResult* const result) noexcept
{
    if (!runtime_state() || (runtime != &*runtime_state()) || !input || !result ||
        (input->size != sizeof(*input)) || (input->version != 1u) ||
        (input->command_count && !input->commands) || (input->message_count && !input->messages))
        return modules::ECallStatus::invalid_argument;
    const bool failed = runtime_state()->failed();
    runtime_state()->pump(*input, *result);
    return (!failed && runtime_state()->failed()) ? modules::ECallStatus::failed : modules::ECallStatus::success;
}

static modules::ECallStatus MV_STD_ABI_CALL begin_stop(void* const runtime, const modules::EStopReason,
    modules::SProgress* const progress) noexcept
{
    if (!runtime_state() || (runtime != &*runtime_state()) || !progress) return modules::ECallStatus::invalid_argument;
    runtime_state()->begin_stop(*progress);
    return modules::ECallStatus::success;
}

static modules::ECallStatus MV_STD_ABI_CALL deactivate(void* const runtime) noexcept
{
    if (!runtime_state() || (runtime != &*runtime_state()) || !runtime_state()->stopped()) return modules::ECallStatus::failed;
    runtime_state().reset();
    return modules::ECallStatus::success;
}

}   //  namespace platform::module_binding

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v3(modules::SBootstrapFunctions* const functions) noexcept
{
    return platform::module_binding::s_binding.bootstrap(functions);
}

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_host_pump_v1(const std::uint32_t capacity,
    modules::SHostPumpV1* const value) noexcept
{
    if (!value || (capacity != sizeof(*value))) return modules::EBindingResult::invalid_argument;
    using namespace platform::module_binding;
    *value = { sizeof(*value), 1u, 0u, &activate, &pump, &begin_stop, &deactivate };
    return modules::EBindingResult::success;
}

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_service_provider_v1(const std::uint32_t capacity,
    modules::SServiceProviderV1* const value) noexcept
{
    if (!value || (capacity != sizeof(*value))) return modules::EBindingResult::invalid_argument;
    *value = { sizeof(*value), 1u, 0u, 1u, &platform::windows::k_window_contract };
    return modules::EBindingResult::success;
}
