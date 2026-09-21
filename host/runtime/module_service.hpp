
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_service.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Sep 26
//
//  Serial module lifecycle requests over stable Host-owned binding records.

#pragma once

#ifndef HOST_MODULE_SERVICE_HPP_INCLUDED
#define HOST_MODULE_SERVICE_HPP_INCLUDED

#include "containers/TInstance.hpp"
#include "module/bound_module.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"

namespace host
{

//==============================================================================
//  SModuleWork
//  Address-stable Host state borrowed exclusively by a shared Host worker.
//  The owning request and module records outlive the worker's completion.
//==============================================================================

struct SModuleWork
{
    modules::CBoundModule* previous{ nullptr };
    modules::CBoundModule* next{ nullptr };
    memory::CMemoryContext* memory_context{ nullptr };
    const system_id_registry::SSystemRegistryView* registry{ nullptr };
    debug_system::CDebugServiceState* debug_service{ nullptr };
    const ModuleRequest* request{ nullptr };
    modules::FModuleFunction function{ nullptr };
};

//==============================================================================
//  CModuleService
//  Serialises module transitions and routes client or internal completions.
//==============================================================================

class CModuleService
{
public:
    enum class EPurpose : std::uint8_t { client = 0, bootstrap, executive_change, shutdown };

    void request(
        CErasedOwner&& owner, const std::int32_t slot,
        threading::CThreadPackage* const client, const EPurpose purpose) noexcept;
    void dispatch(
        threading::CThreadPackage& worker,
        debug_system::CDebugServiceState* const debug_service) noexcept;
    void complete(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool take_internal_result(ModuleResult& result, EPurpose& purpose) noexcept;
    [[nodiscard]] bool request_shutdown() noexcept;
    void cancel_pending() noexcept;
    [[nodiscard]] bool release_records() noexcept;

    [[nodiscard]] bool is_idle() const noexcept { return !m_pending; }
    [[nodiscard]] bool is_in_flight() const noexcept { return m_in_flight; }
    [[nodiscard]] bool releases_binding() const noexcept { return m_work.previous != nullptr; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] mount_point_ids::id_type pending_mount_point() const noexcept;
    [[nodiscard]] modules::CBoundModule* executive_binding() noexcept;
    [[nodiscard]] modules::FModuleFunction executive_function() const noexcept;

private:
    struct SModuleRecord
    {
        explicit SModuleRecord(const module_ids::id_type identity) noexcept;

        memory::CMemoryContext memory_context;
        modules::CBoundModule binding;
        modules::FModuleFunction function{ nullptr };
        module_ids::id_type identity{};
    };

    [[nodiscard]] SModuleRecord* find(const mount_point_ids::id_type mount) noexcept;
    void finish(const EModuleStatus status) noexcept;
    void reply(threading::CThreadPackage& client, const std::int32_t slot, const ModuleResult& result) noexcept;

    //  Records survive replacement; their contexts stay at fixed addresses.
    TInstance<SModuleRecord> m_records[module_ids::k_count];

    //  A single request owns the filename and backs the worker's borrowed job.
    CErasedOwner m_request_owner;
    SModuleWork m_work;
    threading::CThreadPackage* m_client{ nullptr };
    std::int32_t m_client_slot{ -1 };
    std::int32_t m_worker_slot{ 0 };
    EPurpose m_purpose{ EPurpose::client };

    //  Internal completions are consumed by the Host lifecycle state machine.
    ModuleResult m_internal_result;
    bool m_internal_result_ready{ false };
    bool m_pending{ false };
    bool m_in_flight{ false };
    bool m_failed{ false };
    std::uint32_t m_shutdown_index{ 0u };
};

}   //  namespace host

#endif  //  HOST_MODULE_SERVICE_HPP_INCLUDED
