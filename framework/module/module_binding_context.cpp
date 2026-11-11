
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_binding_context.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Aug 26
//
//  Common module-owned binding state and installation machinery.

#include "framework/system/system_erased_owner_operations.hpp"
#include "framework/module/module_binding_context.hpp"

#include "foundation/debug/service.hpp"
#include "foundation/memory/memory_context.hpp"
#include "foundation/system/erased_owner_operations.hpp"
#include "foundation/system/system_context.hpp"

namespace modules
{

static CModuleBindingContext* s_active_binding{ nullptr };
static thread_local void* t_thread_provisioning{ nullptr };

bool is_thread_context_ready(const void* const provisioning) noexcept
{
    return (s_active_binding != nullptr) && s_active_binding->is_thread_context_ready(provisioning);
}

bool CModuleBindingContext::is_ready() const noexcept
{
    return
        m_local_type_registry_installed &&
        m_erased_owner_operations_installed &&
        m_peer_identity_installed &&
        m_functional_major_negotiated &&
        m_system_registry_installed &&
        m_ambient_module_id_installed &&
        m_module_memory_context_installed &&
        m_debug_service_installed;
}

bool CModuleBindingContext::is_thread_context_ready(const void* const provisioning) const noexcept
{
    return is_ready() &&
        (provisioning != nullptr) &&
        (t_thread_provisioning == provisioning) &&
        module_ids::ops::is_valid_id(system_context::get_ambient_module_id()) &&
        thread_ids::ops::is_valid_id(system_context::get_ambient_thread_id()) &&
        (memory::get_ambient_memory_context() != nullptr) &&
        (debug_system::get_service() != nullptr);
}

EBindingResult CModuleBindingContext::bootstrap(SBootstrapFunctions* const functions) noexcept
{
    if ((functions == nullptr) || (m_config.query_function == nullptr) ||
        (m_config.query_local_type_registry_view == nullptr) ||
        (m_config.query_local_erased_owner_operations_view == nullptr) ||
        !is_valid_advertised_identity(m_config.advertised_identity) ||
        !module_ids::ops::is_valid_id(m_config.compatible_advertised_peer_id) ||
        (m_config.minimum_peer_version_major > m_config.maximum_peer_version_major))
    {
        return EBindingResult::invalid_argument;
    }

    if ((s_active_binding != nullptr) && (s_active_binding != this))
    {
        return EBindingResult::already_installed;
    }
    const local_type_registry::SLocalTypeRegistryView& local_view = m_config.query_local_type_registry_view();
    const erased_owner_operations::SRegistryView owner_operations{
        erased_owner_operations::system_operations_view(),
        m_config.query_local_erased_owner_operations_view() };
    if (!local_type_registry::validate_view(local_view) || !erased_owner_operations::validate_view(owner_operations))
    {
        return EBindingResult::invalid_argument;
    }

    s_active_binding = this;
    if (!local_type_registry::install_view(local_view))
    {
        s_active_binding = nullptr;
        return EBindingResult::already_installed;
    }
    m_local_type_registry_installed = true;
    if (!erased_owner_operations::install_view(owner_operations))
    {
        s_active_binding = nullptr;
        return EBindingResult::already_installed;
    }
    m_erased_owner_operations_installed = true;

    SBootstrapFunctions populated;
    populated.query_advertised_identity = &query_advertised_identity;
    populated.install_peer_identity = &install_peer_identity;
    populated.populate_core_functions = &populate_core_functions;
    *functions = populated;
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::query_advertised_identity(SAdvertisedIdentity* const identity) noexcept
{
    if ((identity == nullptr) || (s_active_binding == nullptr))
    {
        return EBindingResult::invalid_argument;
    }

    *identity = s_active_binding->m_config.advertised_identity;
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::install_peer_identity(const SAdvertisedIdentity* const identity) noexcept
{
    if ((identity == nullptr) || (s_active_binding == nullptr))
    {
        return EBindingResult::invalid_argument;
    }

    CModuleBindingContext& binding = *s_active_binding;
    if (!is_valid_advertised_identity(*identity) ||
        (identity->advertised_module_id != binding.m_config.compatible_advertised_peer_id) ||
        (identity->version.major < binding.m_config.minimum_peer_version_major) ||
        (identity->version.major > binding.m_config.maximum_peer_version_major) ||
        !functional_ranges_overlap(binding.m_config.advertised_identity, *identity))
    {
        return EBindingResult::incompatible_peer;
    }

    if (binding.m_peer_identity_installed)
    {
        return EBindingResult::already_installed;
    }

    binding.m_peer_identity = *identity;
    binding.m_peer_identity_installed = true;
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::install_ambient_module_id(const module_ids::id_type module_id) noexcept
{
    if ((s_active_binding == nullptr) || !module_ids::ops::is_valid_id(module_id))
    {
        return EBindingResult::invalid_argument;
    }

    (void)system_context::set_ambient_module_id(module_id);
    s_active_binding->m_ambient_module_id_installed = true;
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::install_system_registry_view(const system_id_registry::SSystemRegistryView* const view) noexcept
{
    if ((s_active_binding == nullptr) || !s_active_binding->m_peer_identity_installed || (view == nullptr))
    {
        return EBindingResult::invalid_argument;
    }
    if (s_active_binding->m_system_registry_installed)
    {
        return EBindingResult::already_installed;
    }
    if (!system_id_registry::install_view(*view))
    {
        return EBindingResult::invalid_argument;
    }
    s_active_binding->m_system_registry_installed = true;
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::install_module_memory_context(memory::CMemoryContext* const context) noexcept
{
    const module_ids::id_type ambient_module_id = system_context::get_ambient_module_id();
    if ((s_active_binding == nullptr) ||
        s_active_binding->m_module_memory_context_installed ||
        (context == nullptr) || !context->is_usable() ||
        !module_ids::ops::is_valid_id(ambient_module_id) ||
        !context->belongs_to_module(ambient_module_id) ||
        !context->is_attribution_empty())
    {
        return EBindingResult::invalid_argument;
    }

    (void)memory::set_module_memory_context(context);
    s_active_binding->m_module_memory_context_installed = true;
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::install_debug_service(debug_system::CDebugServiceState* const service) noexcept
{
    if ((s_active_binding == nullptr) || (service == nullptr))
    {
        return EBindingResult::invalid_argument;
    }
    if (s_active_binding->m_debug_service_installed)
    {
        return EBindingResult::already_installed;
    }
    if (!debug_system::install_service(service))
    {
        return EBindingResult::already_installed;
    }

    s_active_binding->m_debug_service_installed = true;
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::install_ambient_thread_id(const thread_ids::id_type thread_id) noexcept
{
    if ((s_active_binding == nullptr) || !s_active_binding->is_ready() ||
        !thread_ids::ops::is_valid_id(thread_id))
    {
        return EBindingResult::invalid_argument;
    }

    (void)system_context::set_ambient_thread_id(thread_id);
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::install_thread_memory_context(memory::CMemoryContext* const context) noexcept
{
    const module_ids::id_type ambient_module_id = system_context::get_ambient_module_id();
    if ((s_active_binding == nullptr) || !s_active_binding->is_ready() ||
        ((context != nullptr) &&
            (!context->is_usable() ||
                !module_ids::ops::is_valid_id(ambient_module_id) ||
                !context->belongs_to_module(ambient_module_id))))
    {
        return EBindingResult::invalid_argument;
    }

    (void)memory::set_thread_memory_context(context);
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::install_thread_provisioning(void* const provisioning) noexcept
{
    if ((s_active_binding == nullptr) || !s_active_binding->is_ready() || (provisioning == nullptr))
    {
        return EBindingResult::invalid_argument;
    }

    t_thread_provisioning = provisioning;
    return EBindingResult::success;
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::query_function(
    const system_type_id function_type,
    const std::uint32_t functional_major,
    FModuleFunction* const function) noexcept
{
    if ((s_active_binding == nullptr) || !s_active_binding->is_ready() || (function == nullptr))
    {
        return EBindingResult::invalid_argument;
    }

    *function = nullptr;
    if (!s_active_binding->m_functional_major_negotiated ||
        (functional_major != s_active_binding->m_negotiated_functional_major))
    {
        return EBindingResult::unsupported_version;
    }

    return s_active_binding->m_config.query_function(function_type, functional_major, function);
}

EBindingResult MV_STD_ABI_CALL CModuleBindingContext::populate_core_functions(
    const std::uint32_t functional_major,
    SCoreFunctions* const functions) noexcept
{
    if ((functions == nullptr) || (s_active_binding == nullptr))
    {
        return EBindingResult::invalid_argument;
    }
    *functions = {};

    if (!s_active_binding->m_peer_identity_installed)
    {
        return EBindingResult::incompatible_peer;
    }

    const SAdvertisedIdentity& identity = s_active_binding->m_config.advertised_identity;
    if ((functional_major < identity.minimum_functional_major) ||
        (functional_major > identity.maximum_functional_major) ||
        (functional_major < s_active_binding->m_peer_identity.minimum_functional_major) ||
        (functional_major > s_active_binding->m_peer_identity.maximum_functional_major))
    {
        return EBindingResult::unsupported_version;
    }

    if (s_active_binding->m_functional_major_negotiated && (functional_major != s_active_binding->m_negotiated_functional_major))
    {
        return EBindingResult::already_installed;
    }

    SCoreFunctions populated;
    populated.install_system_registry_view = &install_system_registry_view;
    populated.install_ambient_module_id = &install_ambient_module_id;
    populated.install_module_memory_context = &install_module_memory_context;
    populated.install_debug_service = &install_debug_service;
    populated.install_ambient_thread_id = &install_ambient_thread_id;
    populated.install_thread_memory_context = &install_thread_memory_context;
    populated.install_thread_provisioning = &install_thread_provisioning;
    populated.query_function = &query_function;
    *functions = populated;
    s_active_binding->m_negotiated_functional_major = functional_major;
    s_active_binding->m_functional_major_negotiated = true;
    return EBindingResult::success;
}

}   //  namespace modules
