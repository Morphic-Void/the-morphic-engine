
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bound_module.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Aug 26
//
//  Validated module binding and native module lifetime.

#pragma once

#ifndef BOUND_MODULE_HPP_INCLUDED
#define BOUND_MODULE_HPP_INCLUDED

#include "module/module_binding.hpp"
#include "platform/module/binding.hpp"

namespace modules
{

class CBoundModule
{
public:
    CBoundModule() noexcept = default;
    CBoundModule(const CBoundModule&) = delete;
    CBoundModule& operator=(const CBoundModule&) = delete;
    CBoundModule(CBoundModule&&) = delete;
    CBoundModule& operator=(CBoundModule&&) = delete;
    ~CBoundModule() noexcept;

    [[nodiscard]] bool bind(
        const platform::path::NativePath& path,
        const module_ids::id_type expected_advertised_module_id,
        const SAdvertisedIdentity& peer_identity) noexcept;
    [[nodiscard]] bool install(
        const system_id_registry::SSystemRegistryView& system_registry,
        const module_ids::id_type ambient_module_id,
        memory::CMemoryContext* const module_memory_context,
        debug_system::CDebugServiceState* const debug_service) noexcept;
    [[nodiscard]] bool unbind() noexcept;

    //  Terminal cleanup only: prevent destructor-side unloading after the worker
    //  could not safely release the DLL. The process retains its native reference.
    void retain_until_process_exit() noexcept;

    [[nodiscard]] EBindingResult populate_core_functions(
        const std::uint32_t functional_major, SCoreFunctions& functions) const noexcept;
    [[nodiscard]] bool query_function(const system_type_id function_type, FModuleFunction& function) const noexcept;
    [[nodiscard]] const SAdvertisedIdentity& advertised_peer_identity() const noexcept { return m_advertised_peer_identity; }
    [[nodiscard]] const SAdvertisedIdentity& advertised_module_identity() const noexcept { return m_advertised_module_identity; }
    [[nodiscard]] std::uint32_t negotiated_functional_major() const noexcept { return m_negotiated_functional_major; }
    [[nodiscard]] bool is_ready() const noexcept { return m_installed; }
    [[nodiscard]] bool is_bound() const noexcept { return m_native_module.is_bound(); }

    static bool MV_STD_ABI_CALL prepare_thread(void* const context, const thread_ids::id_type thread_id, void* const thread_resources) noexcept;

private:
    [[nodiscard]] bool prepare_thread(const thread_ids::id_type thread_id, void* const thread_resources) noexcept;

    platform::module::CPlatformModule m_native_module;
    SBootstrapFunctions m_bootstrap;
    SCoreFunctions m_core;
    SAdvertisedIdentity m_advertised_peer_identity;
    SAdvertisedIdentity m_advertised_module_identity;
    memory::CMemoryContext* m_module_memory_context{ nullptr };
    std::uint32_t m_negotiated_functional_major{ 0u };
    bool m_installed{ false };
};

}   //  namespace modules

#endif  //  #ifndef BOUND_MODULE_HPP_INCLUDED
