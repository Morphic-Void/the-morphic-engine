
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_binding.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Aug 26
//
//  Shared host/module binding ABI.

#pragma once

#ifndef MODULE_BINDING_ABI_HPP_INCLUDED
#define MODULE_BINDING_ABI_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t
#include <type_traits>  //  std::is_standard_layout_v, std::is_trivially_copyable_v

#include "platform/platform_defines.hpp"
#include "system/system_id_registry.hpp"
#include "system/system_ids.hpp"

//==============================================================================
//  External declarations
//==============================================================================

namespace debug_system { class CDebugServiceState; }

namespace memory { class CMemoryContext; }

namespace modules
{

inline constexpr char k_bootstrap_symbol_name[]{ "morphic_module_bootstrap_v3" };
inline constexpr std::uint32_t k_binding_abi_major = 3u;

struct SVersion
{
    std::uint32_t major{ 0u };
    std::uint32_t minor{ 0u };
};

struct SAdvertisedIdentity
{
    module_ids::id_type advertised_module_id{}; // Claimed identity; distinct from an installed ambient assignment.
    SVersion version{};
    std::uint32_t minimum_functional_major{ 0u };
    std::uint32_t maximum_functional_major{ 0u };
};

[[nodiscard]] constexpr bool is_valid_advertised_identity(const SAdvertisedIdentity& identity) noexcept
{
    return module_ids::ops::is_valid_id(identity.advertised_module_id) &&
        (identity.minimum_functional_major <= identity.maximum_functional_major);
}

[[nodiscard]] constexpr bool functional_ranges_overlap(const SAdvertisedIdentity& lhs, const SAdvertisedIdentity& rhs) noexcept
{
    return (lhs.minimum_functional_major <= rhs.maximum_functional_major) && (rhs.minimum_functional_major <= lhs.maximum_functional_major);
}

[[nodiscard]] constexpr std::uint32_t highest_common_functional_major(const SAdvertisedIdentity& lhs, const SAdvertisedIdentity& rhs) noexcept
{
    return (lhs.maximum_functional_major < rhs.maximum_functional_major) ? lhs.maximum_functional_major : rhs.maximum_functional_major;
}

enum class EBindingResult : std::uint32_t
{
    success = 0u,
    invalid_argument,
    incompatible_peer,
    unsupported_version,
    unsupported_function,
    already_installed
};

using FModuleFunction = void(MV_STD_ABI_CALL*)() noexcept;

//  Optional, independently versioned service-thread capability. Absence means
//  a passive module. A successful query must supply both fields; Host owns the
//  thread and keeps the binding installed until its work drains and it joins.
inline constexpr char k_service_thread_symbol_name[]{ "morphic_module_service_thread_v1" };
using FServiceThreadEntry = std::uint32_t(MV_STD_ABI_CALL*)(void* const resources) noexcept;
struct SServiceThread
{
    thread_ids::id_type thread{};
    FServiceThreadEntry entry{ nullptr };
};
using FQueryServiceThread = EBindingResult(MV_STD_ABI_CALL*)(SServiceThread* const service) noexcept;
static_assert(std::is_standard_layout_v<SServiceThread> && std::is_trivially_copyable_v<SServiceThread>);

using FQueryAdvertisedIdentity = EBindingResult(MV_STD_ABI_CALL*)(SAdvertisedIdentity* const identity) noexcept;
using FInstallPeerIdentity = EBindingResult(MV_STD_ABI_CALL*)(const SAdvertisedIdentity* const identity) noexcept;

using FInstallAmbientModuleId = EBindingResult(MV_STD_ABI_CALL*)(const module_ids::id_type module_id) noexcept;
using FInstallSystemRegistryView = EBindingResult(MV_STD_ABI_CALL*)(const system_id_registry::SSystemRegistryView* const view) noexcept;
using FInstallModuleMemoryContext = EBindingResult(MV_STD_ABI_CALL*)(memory::CMemoryContext* const context) noexcept;
using FInstallDebugService = EBindingResult(MV_STD_ABI_CALL*)(debug_system::CDebugServiceState* const service) noexcept;
using FInstallAmbientThreadId = EBindingResult(MV_STD_ABI_CALL*)(const thread_ids::id_type thread_id) noexcept;
using FInstallThreadMemoryContext = EBindingResult(MV_STD_ABI_CALL*)(memory::CMemoryContext* const context) noexcept;
using FInstallThreadProvisioning = EBindingResult(MV_STD_ABI_CALL*)(void* const provisioning) noexcept;
using FQueryFunction = EBindingResult(MV_STD_ABI_CALL*)(const system_type_id function_type, const std::uint32_t functional_major, FModuleFunction* const function) noexcept;

struct SCoreFunctions
{
    FInstallSystemRegistryView install_system_registry_view{ nullptr };
    FInstallAmbientModuleId install_ambient_module_id{ nullptr };
    FInstallModuleMemoryContext install_module_memory_context{ nullptr };
    FInstallDebugService install_debug_service{ nullptr };
    FInstallAmbientThreadId install_ambient_thread_id{ nullptr };
    FInstallThreadMemoryContext install_thread_memory_context{ nullptr };
    FInstallThreadProvisioning install_thread_provisioning{ nullptr };
    FQueryFunction query_function{ nullptr };

    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_complete() const noexcept;
};

inline bool SCoreFunctions::is_empty() const noexcept
{
    return
        (install_system_registry_view == nullptr) &&
        (install_ambient_module_id == nullptr) &&
        (install_module_memory_context == nullptr) &&
        (install_debug_service == nullptr) &&
        (install_ambient_thread_id == nullptr) &&
        (install_thread_memory_context == nullptr) &&
        (install_thread_provisioning == nullptr) &&
        (query_function == nullptr);
}

inline bool SCoreFunctions::is_complete() const noexcept
{
    return
        (install_system_registry_view != nullptr) &&
        (install_ambient_module_id != nullptr) &&
        (install_module_memory_context != nullptr) &&
        (install_debug_service != nullptr) &&
        (install_ambient_thread_id != nullptr) &&
        (install_thread_memory_context != nullptr) &&
        (install_thread_provisioning != nullptr) &&
        (query_function != nullptr);
}

using FPopulateCoreFunctions = EBindingResult(MV_STD_ABI_CALL*)(const std::uint32_t functional_major, SCoreFunctions* const functions) noexcept;

struct SBootstrapFunctions
{
    FQueryAdvertisedIdentity query_advertised_identity{ nullptr };
    FInstallPeerIdentity install_peer_identity{ nullptr };
    FPopulateCoreFunctions populate_core_functions{ nullptr };
};

using FBootstrap = EBindingResult(MV_STD_ABI_CALL*)(SBootstrapFunctions* const functions) noexcept;

static_assert(std::is_standard_layout_v<SVersion> && std::is_trivially_copyable_v<SVersion>);
static_assert(std::is_standard_layout_v<SAdvertisedIdentity> && std::is_trivially_copyable_v<SAdvertisedIdentity>);
static_assert((sizeof(SAdvertisedIdentity) == 24u), "The version-3 advertised identity must retain its fixed ABI representation.");
static_assert(std::is_standard_layout_v<SCoreFunctions> && std::is_trivially_copyable_v<SCoreFunctions>);
static_assert(std::is_standard_layout_v<SBootstrapFunctions> && std::is_trivially_copyable_v<SBootstrapFunctions>);
static_assert((sizeof(SCoreFunctions) == (sizeof(void*) * 8u)), "Version-2 core functions must contain exactly eight ABI function pointers.");
static_assert(sizeof(SBootstrapFunctions) == (sizeof(void*) * 3u));

}   //  namespace modules

#endif  //  #ifndef MODULE_BINDING_ABI_HPP_INCLUDED
