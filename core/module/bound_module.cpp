
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bound_module.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Aug 26
//
//  Validated module binding and native module lifetime.

#include "module/bound_module.hpp"

#include "debug/macros.hpp"
#include "memory/memory_context.hpp"

namespace modules
{

CBoundModule::~CBoundModule() noexcept
{
    if (!unbind())
    {
        void* const retained_module = m_native_module.release_without_unload();
        MV_CRITICAL_EVENT("CBoundModule retained a native module after safe unload failed");
        (void)retained_module;
    }
}

EBindingResult CBoundModule::populate_core_functions(const std::uint32_t functional_major, SCoreFunctions& functions) const noexcept
{
    functions = {};
    if (m_bootstrap.populate_core_functions == nullptr)
    {
        return EBindingResult::invalid_argument;
    }

    return m_bootstrap.populate_core_functions(functional_major, &functions);
}

bool CBoundModule::bind(
    const platform::path::NativePath& path,
    const module_ids::id_type expected_advertised_module_id,
    const SAdvertisedIdentity& peer_identity) noexcept
{
    if (m_native_module.is_bound() ||
        !module_ids::ops::is_valid_id(expected_advertised_module_id) ||
        !is_valid_advertised_identity(peer_identity) ||
        !m_native_module.bind(path))
    {
        return false;
    }

    const platform::module::FModuleFunction symbol = m_native_module.find_function(k_bootstrap_symbol_name);
    const FBootstrap bootstrap = reinterpret_cast<FBootstrap>(symbol);
    SBootstrapFunctions bootstrap_functions;
    if ((bootstrap == nullptr) ||
        (bootstrap(&bootstrap_functions) != EBindingResult::success) ||
        (bootstrap_functions.query_advertised_identity == nullptr) ||
        (bootstrap_functions.install_peer_identity == nullptr) ||
        (bootstrap_functions.populate_core_functions == nullptr))
    {
        (void)unbind();
        return false;
    }

    SAdvertisedIdentity module_identity;
    if ((bootstrap_functions.query_advertised_identity(&module_identity) != EBindingResult::success) ||
        !is_valid_advertised_identity(module_identity) ||
        (module_identity.advertised_module_id != expected_advertised_module_id) ||
        !functional_ranges_overlap(peer_identity, module_identity) ||
        (bootstrap_functions.install_peer_identity(&peer_identity) != EBindingResult::success))
    {
        (void)unbind();
        return false;
    }

    m_bootstrap = bootstrap_functions;
    m_negotiated_functional_major = highest_common_functional_major(peer_identity, module_identity);
    SCoreFunctions core_functions;
    if ((populate_core_functions(m_negotiated_functional_major, core_functions) != EBindingResult::success) ||
        !core_functions.is_complete())
    {
        (void)unbind();
        return false;
    }

    m_core = core_functions;
    m_advertised_peer_identity = peer_identity;
    m_advertised_module_identity = module_identity;
    return true;
}

bool CBoundModule::install(
    const system_id_registry::SSystemRegistryView& system_registry,
    const module_ids::id_type ambient_module_id,
    memory::CMemoryContext* const module_memory_context,
    debug_system::CDebugServiceState* const debug_service) noexcept
{
    if (!m_native_module.is_bound() || m_installed ||
        !module_ids::ops::is_valid_id(ambient_module_id) ||
        (module_memory_context == nullptr) ||
        !module_memory_context->is_usable() ||
        !module_memory_context->belongs_to_module(ambient_module_id) ||
        !module_memory_context->is_attribution_empty() ||
        (m_core.install_system_registry_view(&system_registry) != EBindingResult::success) ||
        (m_core.install_ambient_module_id(ambient_module_id) != EBindingResult::success) ||
        (m_core.install_module_memory_context(module_memory_context) != EBindingResult::success))
    {
        return false;
    }

    m_module_memory_context = module_memory_context;
    if (m_core.install_debug_service(debug_service) != EBindingResult::success)
    {
        return false;
    }

    m_installed = true;
    return true;
}

bool CBoundModule::query_function(const system_type_id function_type, FModuleFunction& function) const noexcept
{
    function = nullptr;
    if (!m_installed)
    {
        return false;
    }

    const EBindingResult result = m_core.query_function(function_type, m_negotiated_functional_major, &function);
    return (result == EBindingResult::success) && (function != nullptr);
}

bool MV_STD_ABI_CALL CBoundModule::prepare_thread(void* const context, const thread_ids::id_type thread_id, void* const thread_resources) noexcept
{
    CBoundModule* const module = static_cast<CBoundModule*>(context);
    return (module != nullptr) && module->prepare_thread(thread_id, thread_resources);
}

bool CBoundModule::prepare_thread(const thread_ids::id_type thread_id, void* const thread_resources) noexcept
{
    return m_installed &&
        (m_core.install_ambient_thread_id(thread_id) == EBindingResult::success) &&
        (m_core.install_thread_provisioning(thread_resources) == EBindingResult::success) &&
        (m_core.install_thread_memory_context(nullptr) == EBindingResult::success);
}

bool CBoundModule::unbind() noexcept
{
    if ((m_module_memory_context != nullptr) && !m_module_memory_context->is_attribution_empty())
    {
        return false;
    }

    if (!m_native_module.unbind())
    {
        return false;
    }

    m_installed = false;
    m_module_memory_context = nullptr;
    m_negotiated_functional_major = 0u;
    m_advertised_peer_identity = {};
    m_advertised_module_identity = {};
    m_core = {};
    m_bootstrap = {};
    return true;
}

void CBoundModule::retain_until_process_exit() noexcept
{
    (void)m_native_module.release_without_unload();
    m_installed = false;
    m_module_memory_context = nullptr;
    m_core = {};
    m_bootstrap = {};
}

}   //  namespace modules
