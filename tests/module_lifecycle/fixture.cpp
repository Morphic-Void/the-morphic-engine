
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    fixture.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    20 Sep 26
//
//  Real DLL fixtures for the Host's asynchronous module lifecycle.

#include "framework/system/system_ids.hpp"
#include <cstdlib>
#include <cstring>

#include "tests/module_lifecycle/local_type_ids.hpp"
#include "framework/module/module_binding_context.hpp"
#include "framework/module/TExecutionInstance.hpp"
#include "tests/module_lifecycle/service_execution.hpp"
#include "tests/module_lifecycle/executive_execution.hpp"
#include "memory/memory_context.hpp"
#include "debug/macros.hpp"

namespace module_lifecycle_tests
{

#if MV_LIFECYCLE_EXECUTIVE
static constexpr auto k_advertised_module_id = module_ids::executive;

#elif MV_LIFECYCLE_SERVICE || MV_LIFECYCLE_PASSIVE
static constexpr auto k_advertised_module_id = module_ids::conditioning_general;

#else
static constexpr auto k_advertised_module_id = module_ids::rendering;

#endif

static constexpr std::uint32_t k_advertised_version_minor{ 0u };

static modules::EBindingResult MV_STD_ABI_CALL query_function(const system_type_id type,
    const std::uint32_t functional_major, modules::FModuleFunction* const function) noexcept
{
    (void)type;
    (void)functional_major;
    if (function == nullptr)
    {
        return modules::EBindingResult::invalid_argument;
    }
    *function = nullptr;
    return modules::EBindingResult::unsupported_function;
}

static constexpr modules::SModuleBindingConfig k_binding_config = MV_MODULE_BINDING_CONFIG();
static modules::CModuleBindingContext s_binding{ k_binding_config };

#if MV_LIFECYCLE_EXECUTIVE || MV_LIFECYCLE_SERVICE || MV_LIFECYCLE_RENDERING_FAILURE || MV_LIFECYCLE_RENDERING_DISPOSAL
class CNativeLifecycleExecution
{
public:
    CNativeLifecycleExecution(const modules::SExecutionContext& context,
        const char* const selected) noexcept
#if MV_LIFECYCLE_EXECUTIVE
        : m_executive{ context, selected }
#elif MV_LIFECYCLE_SERVICE
        : m_service{ context, (std::strcmp(selected, "service-lifecycle") == 0),
            (std::strcmp(selected, "service-start-failure") == 0) }
#elif MV_LIFECYCLE_RENDERING_DISPOSAL
        : m_service{ context, false, false, scenario(selected) }
        , m_final{ context }, m_bundle{ context }
        , m_final_mode{ std::strcmp(selected, "render-exit-disposal") == 0 }
        , m_bundle_mode{ std::strcmp(selected, "bundle-rendering") == 0 }
#endif
    {
        (void)context;
        (void)selected;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context,
        void** const runtime) noexcept
    {
        if (!context || !runtime || !context->communication || (context->size != sizeof(*context)) || (context->version != 1u))
        {
            return modules::EExecutionStatus::invalid_argument;
        }
        *runtime = nullptr;
        memory::CMemoryContext* const memory = memory::get_ambient_memory_context();
        if (!memory)
        {
            return modules::EExecutionStatus::failed;
        }
        char selected[40]{};
        std::size_t required = 0u;
        (void)getenv_s(&required, selected, sizeof(selected), "MORPHIC_LIFECYCLE_CASE");
#if MV_LIFECYCLE_RENDERING_DISPOSAL
        // This fixture role accepts only its maintained Rendering scenarios.
        if ((std::strncmp(selected, "acquire-render-", 15u) != 0) &&
            (std::strcmp(selected, "tests-routing") != 0) && (std::strcmp(selected, "tests-self-stop") != 0) &&
            (std::strcmp(selected, "tests-client-unload") != 0) && (std::strcmp(selected, "normal-tests-malformed") != 0) &&
            (std::strcmp(selected, "render-exit-disposal") != 0) && (std::strcmp(selected, "bundle-rendering") != 0))
        {
            return modules::EExecutionStatus::failed;
        }
#endif
        void* const handle = modules::TExecutionInstance<CNativeLifecycleExecution>::create_in(*memory, *context, selected);
        if (!handle)
        {
            return modules::EExecutionStatus::failed;
        }
#if MV_LIFECYCLE_RENDERING_DISPOSAL
        CNativeLifecycleExecution* const self = modules::TExecutionInstance<CNativeLifecycleExecution>::get(handle);
        if (self->m_final_mode && !self->m_final.configure())
        {
            modules::TExecutionInstance<CNativeLifecycleExecution>::destroy(handle);
            return modules::EExecutionStatus::failed;
        }
#endif
        *runtime = handle;
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        if (!runtime || !invocation || !progress)
        {
            return modules::EExecutionStatus::invalid_argument;
        }
        CNativeLifecycleExecution& self = *modules::TExecutionInstance<CNativeLifecycleExecution>::get(runtime);
#if MV_LIFECYCLE_EXECUTIVE
        return self.m_executive.handle(*invocation, *progress);
#elif MV_LIFECYCLE_SERVICE
        return self.m_service.handle(*invocation, *progress);
#elif MV_LIFECYCLE_RENDERING_DISPOSAL
        if (invocation->event == modules::EExecutionEvent::start) MV_LOG(info, "Rendering: Running");
        if (self.m_bundle_mode)
        {
            return self.m_bundle.handle_rendering(*invocation, *progress);
        }
        return self.m_final_mode ? self.m_final.handle(*invocation, *progress) : self.m_service.handle(*invocation, *progress);
#else
        // A deliberate once-reported startup failure still permits cleanup.
        if (invocation->event == modules::EExecutionEvent::start)
        {
            return modules::EExecutionStatus::failed;
        }
        if (invocation->event == modules::EExecutionEvent::stop) self.m_stopping = true;
        progress->work_complete = self.m_stopping;
        return modules::EExecutionStatus::success;
#endif
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        CNativeLifecycleExecution* const self = modules::TExecutionInstance<CNativeLifecycleExecution>::get(runtime);
        if (!self)
        {
            return modules::EExecutionStatus::invalid_argument;
        }
#if MV_LIFECYCLE_EXECUTIVE
        if (!self->m_executive.complete())
        {
            return modules::EExecutionStatus::failed;
        }
#elif MV_LIFECYCLE_RENDERING_DISPOSAL
        const bool complete = self->m_bundle_mode ? self->m_bundle.complete() :
            (self->m_final_mode ? self->m_final.complete() : self->m_service.complete());
        if (!complete)
        {
            return modules::EExecutionStatus::failed;
        }
#elif MV_LIFECYCLE_SERVICE
        if (!self->m_service.complete())
        {
            return modules::EExecutionStatus::failed;
        }
#else
        if (!self->m_stopping)
        {
            return modules::EExecutionStatus::failed;
        }
#endif
#if MV_LIFECYCLE_RENDERING_DISPOSAL
        MV_LOG(info, "Rendering: Exited");
#endif
        modules::TExecutionInstance<CNativeLifecycleExecution>::destroy(runtime);
        return modules::EExecutionStatus::success;
    }
private:
#if MV_LIFECYCLE_RENDERING_DISPOSAL
    static EAcquisitionScenario scenario(const char* const selected) noexcept
    {
        if (std::strcmp(selected, "normal-tests-malformed") == 0)
        {
            return EAcquisitionScenario::rendering_malformed;
        }
        if (std::strcmp(selected, "tests-routing") == 0)
        {
            return EAcquisitionScenario::rendering_tests;
        }
        if ((std::strcmp(selected, "tests-self-stop") == 0) || (std::strcmp(selected, "tests-client-unload") == 0))
        {
            return EAcquisitionScenario::rendering_tests_cancel;
        }
        return (std::strcmp(selected, "acquire-render-release") == 0) ?
            EAcquisitionScenario::rendering_release : EAcquisitionScenario::rendering_abandon;
    }
#endif
#if MV_LIFECYCLE_EXECUTIVE
    CExecutiveExecution m_executive;
#elif MV_LIFECYCLE_RENDERING_DISPOSAL
    CServiceExecution m_service;
    CFinalDisposalExecution m_final;
    CBundleExecution m_bundle;
    const bool m_final_mode;
    const bool m_bundle_mode;
#elif MV_LIFECYCLE_SERVICE
    CServiceExecution m_service;
#else
    bool m_stopping{ false };
#endif
};
#endif

}   //  namespace module_lifecycle_tests

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v1(
    modules::SBootstrapFunctions* const functions) noexcept
{
    return module_lifecycle_tests::s_binding.bootstrap(functions);
}

#if MV_LIFECYCLE_EXECUTIVE || MV_LIFECYCLE_SERVICE || MV_LIFECYCLE_PASSIVE || MV_LIFECYCLE_RENDERING_FAILURE || MV_LIFECYCLE_RENDERING_DISPOSAL
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_execution_v1(
    const std::uint32_t capacity, modules::SExecutionV1* const value) noexcept
{
    if (!value || (capacity != sizeof(*value)))
    {
        return modules::EBindingResult::invalid_argument;
    }
    *value = {};
#if !MV_LIFECYCLE_PASSIVE
    using module_lifecycle_tests::CNativeLifecycleExecution;
    static const modules::SExecutionV1 service{ sizeof(modules::SExecutionV1), 1u, 0u,
        &CNativeLifecycleExecution::create, &CNativeLifecycleExecution::handle, &CNativeLifecycleExecution::destroy };
#if MV_LIFECYCLE_EXECUTIVE
    static const modules::SChildExecution child{ &service, nullptr, thread_ids::executive };
#elif MV_LIFECYCLE_SERVICE
    static const modules::SChildExecution child{ &service, nullptr, thread_ids::simulation };
#else
    static const modules::SChildExecution child{ &service, nullptr, thread_ids::rendering };
#endif
    value->initial_child = &child;
#endif
    return modules::EBindingResult::success;
}
#endif
