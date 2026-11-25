
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_binding.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Exposes Platform module binding, service discovery and Host-pumped lifecycle
//  entry points, and owns the runtime instance through activation and shutdown.

#include "framework/system/system_ids.hpp"
#include "module/types/local_type_ids.hpp"
#include "framework/module/module_binding_context.hpp"
#include "framework/module/TExecutionInstance.hpp"
#include "framework/module/service_provider.hpp"
#include "runtime/platform_runtime.hpp"
#include "runtime/input_acceptance.hpp"

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

class CPlatformExecution
{
public:
    CPlatformExecution(const modules::SExecutionContext& context) noexcept;

    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context, void** const runtime) noexcept;
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept;
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept;

private:
    runtime::CPlatformRuntime m_runtime;
};

CPlatformExecution::CPlatformExecution(const modules::SExecutionContext& context) noexcept
    : m_runtime{ context
#if defined(MV_INPUT_ACCEPTANCE)
        , acceptance::window_adapter(), platform::threading::SAwakeClock{},
        acceptance::controller_adapter(), acceptance::report_adapter()
#endif
    }
{
}

modules::EExecutionStatus MV_STD_ABI_CALL CPlatformExecution::create(const modules::SExecutionContext* const context, void** const runtime) noexcept
{
    if (!context || !runtime || (context->size != sizeof(*context)) || (context->version != 1u) ||
        !context->communication || !context->execution || !s_binding.is_thread_context_ready(context))
    {
        return modules::EExecutionStatus::invalid_argument;
    }
    *runtime = nullptr;
#if defined(MV_INPUT_ACCEPTANCE)
    if (!acceptance::initialise(*context))
    {
        return modules::EExecutionStatus::failed;
    }
#endif
    void* const handle = modules::TExecutionInstance<CPlatformExecution>::create(*context);
    CPlatformExecution* const self = modules::TExecutionInstance<CPlatformExecution>::get(handle);
    if (!self || !self->m_runtime.initialise())
    {
        modules::TExecutionInstance<CPlatformExecution>::destroy(handle);
#if defined(MV_INPUT_ACCEPTANCE)
        acceptance::destroy();
#endif
        return modules::EExecutionStatus::failed;
    }
    *runtime = handle;
    return modules::EExecutionStatus::success;
}

modules::EExecutionStatus MV_STD_ABI_CALL CPlatformExecution::handle(void* const runtime, const modules::SInvocation* const invocation, modules::SExecutionProgress* const progress) noexcept
{
    if (!runtime || !invocation || !progress)
    {
        return modules::EExecutionStatus::invalid_argument;
    }
    return modules::TExecutionInstance<CPlatformExecution>::get(runtime)->m_runtime.handle(*invocation, *progress);
}

modules::EExecutionStatus MV_STD_ABI_CALL CPlatformExecution::destroy(void* const runtime) noexcept
{
    CPlatformExecution* const self = modules::TExecutionInstance<CPlatformExecution>::get(runtime);
    if (!self || !self->m_runtime.stopped())
    {
        return modules::EExecutionStatus::failed;
    }
    modules::TExecutionInstance<CPlatformExecution>::destroy(runtime);
#if defined(MV_INPUT_ACCEPTANCE)
    acceptance::destroy();
#endif
    return modules::EExecutionStatus::success;
}
}   //  namespace platform::module_binding

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v1(modules::SBootstrapFunctions* const functions) noexcept
{
    return platform::module_binding::s_binding.bootstrap(functions);
}

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_execution_v1(const std::uint32_t capacity, modules::SExecutionV1* const execution) noexcept
{
    if (!execution || (capacity != sizeof(*execution)))
    {
        return modules::EBindingResult::invalid_argument;
    }
    using platform::module_binding::CPlatformExecution;
    *execution = { sizeof(*execution), 1u, modules::k_execution_native_events,
        &CPlatformExecution::create, &CPlatformExecution::handle, &CPlatformExecution::destroy };
    return modules::EBindingResult::success;
}
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_service_provider_v1(const std::uint32_t capacity, modules::SServiceProviderV1* const value) noexcept
{
    if (!value || (capacity != sizeof(*value))) return modules::EBindingResult::invalid_argument;
    static constexpr services::SServiceContract contracts[]{ platform::windows::k_window_contract, platform::input::k_input_contract
#if defined(MV_INPUT_ACCEPTANCE)
        , platform::acceptance::k_contract
#endif
    };
    *value = { sizeof(*value), 1u, 0u, static_cast<std::uint32_t>(sizeof(contracts) / sizeof(contracts[0])), contracts };
    return modules::EBindingResult::success;
}
