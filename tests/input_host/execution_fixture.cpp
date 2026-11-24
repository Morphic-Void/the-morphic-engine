
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    execution_fixture.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Thin affine DLL adapters for the common Input Host consumer context.

#include "framework/system/system_ids.hpp"
#include "framework/system/storage_type_bindings.hpp"
#include "tests/module_lifecycle/local_type_ids.hpp"
#include "tests/input_host/execution.hpp"
#include "framework/module/module_binding_context.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "system/system_context.hpp"

namespace input_host
{
#if defined(MV_INPUT_SECONDARY)
static constexpr module_ids::id_type k_advertised_module_id{ module_ids::rendering };
static constexpr thread_ids::id_type k_thread{ thread_ids::rendering };
static constexpr bool k_secondary = true;
#else
static constexpr module_ids::id_type k_advertised_module_id{ module_ids::executive };
static constexpr thread_ids::id_type k_thread{ thread_ids::executive };
static constexpr bool k_secondary = false;
#endif

class CInputFixture
{
public:
    CInputFixture(memory::CMemoryToken&& allocation, const modules::SExecutionContext& context) noexcept
        : m_allocation{ std::move(allocation) }, m_execution{ context, k_secondary },
          m_thread{ platform::threading::query_current_thread_id() } {}
    static modules::EExecutionStatus MV_STD_ABI_CALL create(const modules::SExecutionContext* const context, void** const runtime) noexcept
    {
        if (!context || !runtime || !context->communication || !context->execution ||
            (context->size != sizeof(*context)) || (context->version != 1u))
        {
            return modules::EExecutionStatus::invalid_argument;
        }
        *runtime = nullptr;
        if ((context->instance.value != context->execution->instance.value) ||
            (context->endpoint.value != context->execution->client.value) ||
            (system_context::get_ambient_thread_id() != k_thread) ||
            (system_context::get_ambient_module_id() != k_advertised_module_id))
        {
            return modules::EExecutionStatus::failed;
        }
        memory::CMemoryContext* const memory = memory::get_ambient_memory_context();
        if (!memory) return modules::EExecutionStatus::failed;
        memory::CMemoryToken allocation{ 1u, alignof(CInputFixture), memory };
        if (!allocation.allocate(sizeof(CInputFixture))) return modules::EExecutionStatus::failed;
        void* const storage = allocation.data();
        *runtime = ::new (storage) CInputFixture{ std::move(allocation), *context };
        return modules::EExecutionStatus::success;
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const modules::SInvocation* const invocation,
        modules::SExecutionProgress* const progress) noexcept
    {
        if (!runtime || !invocation || !progress)
        {
            return modules::EExecutionStatus::invalid_argument;
        }
        CInputFixture& self = *static_cast<CInputFixture*>(runtime);
        if (!self.context_valid()) return modules::EExecutionStatus::failed;
        return self.m_execution.handle(*invocation, *progress);
    }
    static modules::EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        CInputFixture* const self = static_cast<CInputFixture*>(runtime);
        if (!self || !self->context_valid() || !self->m_execution.complete())
        {
            return modules::EExecutionStatus::failed;
        }
        memory::CMemoryToken allocation{ std::move(self->m_allocation) };
        self->~CInputFixture();
        MV_LOG(info, "Input Host: consumer destroyed after local custody cleanup");
        return modules::EExecutionStatus::success;
    }
private:
    bool context_valid() const noexcept
    {
        return (m_thread == platform::threading::query_current_thread_id()) &&
            (system_context::get_ambient_thread_id() == k_thread) &&
            (system_context::get_ambient_module_id() == k_advertised_module_id) && memory::get_ambient_memory_context();
    }
    memory::CMemoryToken m_allocation;
    CInputExecution m_execution;
    const platform::threading::CPlatformThreadId m_thread;
};

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
}

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v1(modules::SBootstrapFunctions* const functions) noexcept
{
    return input_host::s_binding.bootstrap(functions);
}

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_execution_v1(
    const std::uint32_t capacity, modules::SExecutionV1* const value) noexcept
{
    if (!value || (capacity != sizeof(*value))) return modules::EBindingResult::invalid_argument;
    using input_host::CInputFixture;
    static const modules::SExecutionV1 execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &CInputFixture::create, &CInputFixture::handle, &CInputFixture::destroy };
    static const modules::SChildExecution child{ &execution, nullptr, input_host::k_thread };
    *value = {};
    value->initial_child = &child;
    return modules::EBindingResult::success;
}
