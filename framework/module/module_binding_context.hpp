
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_binding_context.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Aug 26
//
//  Common module-owned binding state and installation machinery.

#pragma once

#ifndef MODULE_BINDING_CONTEXT_HPP_INCLUDED
#define MODULE_BINDING_CONTEXT_HPP_INCLUDED

#include "framework/system/system_ids.hpp"
#include "framework/module/module_binding.hpp"
#include "foundation/system/erased_owner_operations.hpp"
#include "foundation/system/local_type_registry.hpp"

namespace modules
{

struct SModuleBindingConfig
{
    using FQueryLocalTypeRegistryView = const local_type_registry::SLocalTypeRegistryView&(MV_STD_ABI_CALL*)() noexcept;
    using FQueryLocalErasedOwnerOperationsView = const erased_owner_operations::SCategoryView&(MV_STD_ABI_CALL*)() noexcept;

    SAdvertisedIdentity advertised_identity{};
    module_ids::id_type compatible_advertised_peer_id{};
    std::uint32_t minimum_peer_version_major{ 0u };
    std::uint32_t maximum_peer_version_major{ 0u };
    FQueryFunction query_function{ nullptr };
    FQueryLocalTypeRegistryView query_local_type_registry_view{ nullptr };
    FQueryLocalErasedOwnerOperationsView query_local_erased_owner_operations_view{ nullptr };
};

//  Standard configuration for a non-Host module. The component supplies these
//  translation-unit-local names before expanding the macro:
//  - k_advertised_module_id
//  - k_advertised_version_minor
//  - query_function
#define MV_MODULE_BINDING_CONFIG() \
    { \
        { k_advertised_module_id, \
            { modules::k_binding_abi_major, k_advertised_version_minor }, \
            modules::k_binding_abi_major, modules::k_binding_abi_major }, \
        module_ids::executable, \
        modules::k_binding_abi_major, \
        modules::k_binding_abi_major, \
        &query_function, \
        &local_type_registry::component_view, \
        &erased_owner_operations::local_operations_view \
    }

class CModuleBindingContext
{
public:
    explicit CModuleBindingContext(const SModuleBindingConfig& config) noexcept : m_config{ config } {}
    ~CModuleBindingContext() noexcept = default;

    CModuleBindingContext(const CModuleBindingContext&) = delete;
    CModuleBindingContext& operator=(const CModuleBindingContext&) = delete;
    CModuleBindingContext(CModuleBindingContext&&) = delete;
    CModuleBindingContext& operator=(CModuleBindingContext&&) = delete;

    EBindingResult bootstrap(SBootstrapFunctions* const functions) noexcept;
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_thread_context_ready(const void* const provisioning) const noexcept;

private:
    static EBindingResult MV_STD_ABI_CALL query_advertised_identity(SAdvertisedIdentity* const identity) noexcept;
    static EBindingResult MV_STD_ABI_CALL install_peer_identity(const SAdvertisedIdentity* const identity) noexcept;
    static EBindingResult MV_STD_ABI_CALL populate_core_functions(
        const std::uint32_t functional_major,
        SCoreFunctions* const functions) noexcept;

    static EBindingResult MV_STD_ABI_CALL install_ambient_module_id(const module_ids::id_type module_id) noexcept;
    static EBindingResult MV_STD_ABI_CALL install_system_registry_view(const system_id_registry::SSystemRegistryView* const view) noexcept;
    static EBindingResult MV_STD_ABI_CALL install_module_memory_context(memory::CMemoryContext* const context) noexcept;
    static EBindingResult MV_STD_ABI_CALL install_debug_service(debug_system::CDebugServiceState* const service) noexcept;
    static EBindingResult MV_STD_ABI_CALL install_ambient_thread_id(const thread_ids::id_type thread_id) noexcept;
    static EBindingResult MV_STD_ABI_CALL install_thread_memory_context(memory::CMemoryContext* const context) noexcept;
    static EBindingResult MV_STD_ABI_CALL install_thread_provisioning(void* const provisioning) noexcept;
    static EBindingResult MV_STD_ABI_CALL query_function(
        const system_type_id function_type,
        const std::uint32_t functional_major,
        FModuleFunction* const function) noexcept;

    SModuleBindingConfig m_config;
    SAdvertisedIdentity m_peer_identity;
    bool m_peer_identity_installed{ false };
    std::uint32_t m_negotiated_functional_major{ 0u };
    bool m_functional_major_negotiated{ false };
    bool m_local_type_registry_installed{ false };
    bool m_erased_owner_operations_installed{ false };
    bool m_system_registry_installed{ false };
    bool m_ambient_module_id_installed{ false };
    bool m_module_memory_context_installed{ false };
    bool m_debug_service_installed{ false };
};

[[nodiscard]] bool is_thread_context_ready(const void* const provisioning) noexcept;

}   //  namespace modules

#endif  //  #ifndef MODULE_BINDING_CONTEXT_HPP_INCLUDED
