
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    fixture.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    02 Nov 26
//
//  Real DLL/SO fixtures for Host-thread execution and cooperative retirement.

#include "framework/system/system_ids.hpp"
#include "framework/system/storage_type_bindings.hpp"
#include "framework/filesystem/filesystem_messages.hpp"
#include "framework/module/module_messages.hpp"
#include "tests/host_pump/fixture_types.hpp"
#if MV_PUMP_EXECUTIVE
#include "tests/host_pump/executive_execution.hpp"
#else
#include "tests/host_pump/provider_execution.hpp"
#endif
#include <cstdlib>
#include <cstring>
#include <limits>
#include "tests/module_lifecycle/local_type_ids.hpp"
#include "tests/environment/test_paths.hpp"
#include "framework/module/module_binding_context.hpp"
#include "framework/services/provider_client.hpp"
#include "framework/module/host_eligible_budget.hpp"
#include "framework/module/service_provider.hpp"
#include "framework/services/service_borrow_client.hpp"
#include "system/system_context.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/platform/report_messages.hpp"
#if MV_PUMP_EXECUTIVE
#include "framework/assets/schema_messages.hpp"
#include "data_model/document_parser.hpp"
#include "schema/data_block_view.hpp"
#include "platform/filesystem/file.hpp"
#endif
#include "containers/TInstance.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "platform/threading/processor_relax.hpp"
#include "debug/macros.hpp"
#include "debug/service.hpp"
#if MV_PLATFORM_WINDOWS && MV_PUMP_EXECUTIVE
#include "platform/windows_include.hpp"
#include "framework/platform/window.hpp"
#endif

namespace pump_tests
{

using namespace modules;
using namespace services;
static const char* selected() noexcept
{
#if MV_PLATFORM_WINDOWS
    static thread_local char value[64]{};
    std::size_t required = 0u;
    return ((getenv_s(&required, value, sizeof(value), "MORPHIC_PUMP_CASE") == 0) && (required != 0u)) ? value : "normal";
#else
    const char* value = std::getenv("MORPHIC_PUMP_CASE");
    return (value != nullptr) ? value : "normal";
#endif
}
static bool scenario(const char* const value) noexcept { return std::strcmp(selected(), value) == 0; }

#if MV_PUMP_EXECUTIVE
static constexpr auto k_advertised_module_id = module_ids::executive;

class CExecutiveFixture
{
public:
    CExecutiveFixture(memory::CMemoryToken&& allocation, const SExecutionContext& context) noexcept
        : m_allocation{ std::move(allocation) }, m_execution{ context, selected(), (MV_PUMP_REPLACEMENT != 0) },
          m_thread{ platform::threading::query_current_thread_id() } {}
    static EExecutionStatus MV_STD_ABI_CALL create(const SExecutionContext* const context, void** const runtime) noexcept
    {
        if (!context || !runtime || !context->communication || !context->execution ||
            (context->size != sizeof(*context)) || (context->version != 1u))
        {
            return EExecutionStatus::invalid_argument;
        }
        *runtime = nullptr;
        if (!context->execution->restriction.is_valid() || !context->execution->notification.is_valid() ||
            (context->instance.value != context->execution->instance.value) ||
            (context->endpoint.value != context->execution->client.value) ||
            (system_context::get_ambient_thread_id() != thread_ids::executive) ||
            (system_context::get_ambient_module_id() != module_ids::executive))
        {
            return EExecutionStatus::failed;
        }
        memory::CMemoryContext* const memory = memory::get_ambient_memory_context();
        if (!memory) return EExecutionStatus::failed;
        memory::CMemoryToken allocation{ 1u, alignof(CExecutiveFixture), memory };
        if (!allocation.allocate(sizeof(CExecutiveFixture)))
        {
            return EExecutionStatus::failed;
        }
        void* const storage = allocation.data();
        *runtime = ::new (storage) CExecutiveFixture{ std::move(allocation), *context };
        return EExecutionStatus::success;
    }
    static EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const SInvocation* const invocation,
        SExecutionProgress* const progress) noexcept
    {
        if (!runtime || !invocation || !progress)
        {
            return EExecutionStatus::invalid_argument;
        }
        CExecutiveFixture& self = *static_cast<CExecutiveFixture*>(runtime);
        if (!self.context_valid()) return EExecutionStatus::failed;
        return self.m_execution.handle(*invocation, *progress);
    }
    static EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        CExecutiveFixture* const self = static_cast<CExecutiveFixture*>(runtime);
        if (!self || !self->context_valid() || !self->m_execution.complete())
        {
            return EExecutionStatus::failed;
        }
        memory::CMemoryToken allocation{ std::move(self->m_allocation) };
        self->~CExecutiveFixture();
        return EExecutionStatus::success;
    }
private:
    bool context_valid() const noexcept
    {
        return (m_thread == platform::threading::query_current_thread_id()) &&
            (system_context::get_ambient_thread_id() == thread_ids::executive) &&
            (system_context::get_ambient_module_id() == module_ids::executive) && memory::get_ambient_memory_context();
    }
    memory::CMemoryToken m_allocation;
    CExecutiveExecution m_execution;
    const platform::threading::CPlatformThreadId m_thread;
};
#else
static constexpr auto k_advertised_module_id = module_ids::rendering;
class CProviderFixture
{
public:
    CProviderFixture(memory::CMemoryToken&& allocation, const SExecutionContext& context) noexcept
        : m_allocation{ std::move(allocation) }, m_provider{ context, selected() } {}
    static EExecutionStatus MV_STD_ABI_CALL create(const SExecutionContext* const context, void** const runtime) noexcept
    {
        if (!context || !runtime || !context->communication || (context->size != sizeof(*context)) || (context->version != 1u))
        {
            return EExecutionStatus::invalid_argument;
        }
        *runtime = nullptr;
        if (scenario("activation-null")) return EExecutionStatus::failed;
        memory::CMemoryContext* const memory = memory::get_ambient_memory_context();
        if (!memory) return EExecutionStatus::failed;
        memory::CMemoryToken allocation{ 1u, alignof(CProviderFixture), memory };
        if (!allocation.allocate(sizeof(CProviderFixture)))
        {
            return EExecutionStatus::failed;
        }
        void* const storage = allocation.data();
        CProviderFixture* const self = ::new (storage) CProviderFixture{ std::move(allocation), *context };
        if (!self->m_provider.context_valid())
        {
            memory::CMemoryToken failed{ std::move(self->m_allocation) };
            self->~CProviderFixture();
            return EExecutionStatus::failed;
        }
        *runtime = self;
        return EExecutionStatus::success;
    }
    static EExecutionStatus MV_STD_ABI_CALL handle(void* const runtime, const SInvocation* const invocation,
        SExecutionProgress* const progress) noexcept
    {
        if (!runtime || !invocation || !progress)
        {
            return EExecutionStatus::invalid_argument;
        }
        return static_cast<CProviderFixture*>(runtime)->m_provider.handle(*invocation, *progress);
    }
    static EExecutionStatus MV_STD_ABI_CALL destroy(void* const runtime) noexcept
    {
        CProviderFixture* const self = static_cast<CProviderFixture*>(runtime);
        if (!self || !self->m_provider.context_valid() || !self->m_provider.complete())
        {
            return EExecutionStatus::failed;
        }
        if (scenario("deactivate-failed"))
        {
            MV_LOG(info, "Pump provider: quiescent state retained");
            return EExecutionStatus::failed;
        }
        memory::CMemoryToken allocation{ std::move(self->m_allocation) };
        self->~CProviderFixture();
        MV_LOG(info, "Pump provider: deactivated");
        return EExecutionStatus::success;
    }
private:
    memory::CMemoryToken m_allocation;
    CProviderExecution m_provider;
};
#endif

static constexpr std::uint32_t k_advertised_version_minor{0u};
static EBindingResult MV_STD_ABI_CALL query_function(const system_type_id, const std::uint32_t, FModuleFunction* const function) noexcept
{
    if (!function) return EBindingResult::invalid_argument;
    *function = nullptr;
    return EBindingResult::unsupported_function;
}
static constexpr SModuleBindingConfig k_binding_config = MV_MODULE_BINDING_CONFIG();
static CModuleBindingContext s_binding{k_binding_config};

}   //  namespace pump_tests

MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_bootstrap_v1(modules::SBootstrapFunctions* const functions) noexcept
{
    return pump_tests::s_binding.bootstrap(functions);
}
#if MV_PUMP_EXECUTIVE
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_execution_v1(
    const std::uint32_t capacity, modules::SExecutionV1* const value) noexcept
{
    if (!value || (capacity != sizeof(*value)))
    {
        return modules::EBindingResult::invalid_argument;
    }
    using pump_tests::CExecutiveFixture;
    static const modules::SExecutionV1 execution{ sizeof(modules::SExecutionV1), 1u, 0u,
        &CExecutiveFixture::create, &CExecutiveFixture::handle, &CExecutiveFixture::destroy };
    static const modules::SChildExecution child{ &execution, nullptr, thread_ids::executive };
    *value = {};
    value->initial_child = &child;
    return modules::EBindingResult::success;
}
#else
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_execution_v1(const std::uint32_t capacity, modules::SExecutionV1* const value) noexcept
{
    using namespace pump_tests;
    if ((capacity != sizeof(*value)) || !value)
    {
        return EBindingResult::invalid_argument;
    }
    *value = { sizeof(*value), 1u, 0u, &CProviderFixture::create, &CProviderFixture::handle, &CProviderFixture::destroy };
    if (scenario("bad-size")) --value->size;
    if (scenario("bad-version")) ++value->version;
    if (scenario("bad-flags")) value->flags = 2u;
    if (scenario("bad-callback")) value->handle = nullptr;
    return EBindingResult::success;
}
MV_MODULE_EXPORT modules::EBindingResult MV_STD_ABI_CALL morphic_module_service_provider_v1(
    const std::uint32_t capacity, modules::SServiceProviderV1* const value) noexcept
{
    using namespace pump_tests;
    if ((value == nullptr) || (capacity != sizeof(*value)))
    {
        return EBindingResult::invalid_argument;
    }
    static const SServiceContract contracts[]{ {1u, 1u}, {2u, 3u} };
    static const SServiceContract invalid[]{ {1u, 0u} };
    static const SServiceContract duplicate[]{ {1u, 1u}, {1u, 2u} };
    *value = {sizeof(*value), 1u, 0u, 2u, contracts};
    if (scenario("bad-provider-size")) --value->size;
    if (scenario("bad-provider-version")) ++value->version;
    if (scenario("bad-provider-flags")) value->flags = 1u;
    if (scenario("bad-provider-count"))
    {
        value->contract_count = 17u;
    }
    if (scenario("bad-provider-null"))
    {
        value->contracts = nullptr;
    }
    if (scenario("bad-provider-contract"))
    {
        value->contracts = invalid;
        value->contract_count = 1u;
    }
    if (scenario("bad-provider-duplicate"))
    {
        value->contracts = duplicate;
    }
    if (scenario("bad-provider-query"))
    {
        return EBindingResult::unsupported_function;
    }
    if (scenario("empty-provider"))
    {
        value->contracts = nullptr;
        value->contract_count = 0u;
    }
    return EBindingResult::success;
}
#endif
