
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

#include "framework/system/system_ids.hpp"
#include "framework/system/storage_type_bindings.hpp"
#include "framework/module/module_messages.hpp"
#include "containers/TInstance.hpp"
#include "host/filesystem/filesystem_image.hpp"
#include "host/module/bound_module.hpp"
#include "runtime/client_record.hpp"

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
    const modules::ModuleRequest* request{ nullptr };
    const char* physical_file{ nullptr };
    modules::FModuleFunction function{ nullptr };
    modules::SServiceThread service_thread;
    modules::SHostPumpV2 host_pump;
    modules::SServiceProviderV1 service_provider;
    bool cleanup_only{ false };
};

//==============================================================================
//  CModuleService
//  Serialises module transitions and routes client or internal completions.
//==============================================================================

class CModuleService
{
public:
    explicit CModuleService(memory::CMemoryAllocator& allocator) noexcept;
    enum class EPurpose : std::uint8_t { client = 0, bootstrap, executive_change, shutdown };
    void set_filesystem(filesystem_image::CImage& image) noexcept { m_filesystem = &image; }

    void request(
        CErasedOwner&& owner, const std::int32_t slot,
        SClientRecord* const client, const EPurpose purpose) noexcept;
    void dispatch(
        threading::CThreadPackage& worker,
        debug_system::CDebugServiceState* const debug_service) noexcept;
    void complete(const threading::CErasedPodMsg& message) noexcept;
    void complete_thread_start(const bool started) noexcept;
    void fail_retained(const modules::EModuleStatus status) noexcept { finish(status); }
    [[nodiscard]] EPurpose pending_purpose() const noexcept { return m_purpose; }
    [[nodiscard]] modules::EModuleAction pending_action() const noexcept
    {
        return (m_work.request != nullptr) ? m_work.request->action : modules::EModuleAction::unload;
    }
    void thread_stopped(const mount_point_ids::id_type mount) noexcept;
    [[nodiscard]] bool take_internal_result(modules::ModuleResult& result, EPurpose& purpose) noexcept;
    [[nodiscard]] bool request_shutdown() noexcept;
    [[nodiscard]] bool shutdown_enumerated() const noexcept { return m_shutdown_index == module_ids::k_count; }
    void cancel_pending() noexcept;
    [[nodiscard]] bool release_records() noexcept;

    [[nodiscard]] bool is_idle() const noexcept { return !m_pending; }
    [[nodiscard]] bool is_in_flight() const noexcept { return m_in_flight; }
    [[nodiscard]] bool has_internal_result() const noexcept { return m_internal_result_ready; }
    [[nodiscard]] bool needs_thread_start() const noexcept { return m_start_pending; }
    [[nodiscard]] modules::CBoundModule* pending_binding() noexcept { return m_work.next; }
    [[nodiscard]] const modules::SServiceThread& pending_service_thread() const noexcept { return m_work.service_thread; }
    [[nodiscard]] const modules::SHostPumpV2& pending_host_pump() const noexcept { return m_work.host_pump; }
    [[nodiscard]] const modules::SServiceProviderV1& pending_service_provider() const noexcept { return m_work.service_provider; }
    [[nodiscard]] bool releases_binding() const noexcept { return m_work.previous != nullptr; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] mount_point_ids::id_type pending_mount_point() const noexcept;
    [[nodiscard]] modules::CBoundModule* executive_binding() noexcept;
    [[nodiscard]] modules::FModuleFunction executive_function() const noexcept;

private:
#if defined(MV_HOST_RUNTIME_TESTS)
    friend struct SHostMaintenanceTestAccess;
#endif
    struct SModuleRecord
    {
        SModuleRecord(memory::CMemoryAllocator& allocator, const module_ids::id_type identity) noexcept;

        memory::CMemoryContext memory_context;
        modules::CBoundModule binding;
        modules::FModuleFunction function{ nullptr };
        module_ids::id_type identity{};
        bool thread_started{ false };
        bool has_service_thread{ false };
        bool has_host_pump{ false };
    };

    [[nodiscard]] SModuleRecord* find(const mount_point_ids::id_type mount) noexcept;
    void finish(const modules::EModuleStatus status) noexcept;
    void reply(SClientRecord& client, const std::int32_t slot, const modules::ModuleResult& result) noexcept;

    //  Records survive replacement; their contexts stay at fixed addresses.
    memory::CMemoryAllocator& m_allocator;
    TInstance<SModuleRecord> m_records[module_ids::k_count];

    //  A single request owns the filename and backs the worker's borrowed job.
    CErasedOwner m_request_owner;
    filesystem_image::CImage* m_filesystem{ nullptr };
    CSimpleString m_physical_file;
    SModuleWork m_work;
    SClientRecord* m_client{ nullptr };
    std::int32_t m_client_slot{ -1 };
    std::int32_t m_worker_slot{ 0 };
    EPurpose m_purpose{ EPurpose::client };

    //  Internal completions are consumed by the Host lifecycle state machine.
    modules::ModuleResult m_internal_result;
    bool m_internal_result_ready{ false };
    bool m_pending{ false };
    bool m_in_flight{ false };
    bool m_start_pending{ false };
    bool m_failed{ false };
    std::uint32_t m_shutdown_index{ 0u };
};

}   //  namespace host

#endif  //  HOST_MODULE_SERVICE_HPP_INCLUDED
