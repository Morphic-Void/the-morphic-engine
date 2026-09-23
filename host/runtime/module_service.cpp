
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_service.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Sep 26
//
//  Host admission, notification and storage for worker-owned binding transitions.

#include "host/runtime/module_service.hpp"
#include "host/system/host_context.hpp"
#include "host/system/system_id_definitions.hpp"
#include "debug/macros.hpp"

namespace host
{

//==============================================================================
//  Stable module records and client notifications
//==============================================================================

CModuleService::SModuleRecord::SModuleRecord(const module_ids::id_type identity) noexcept
    : memory_context{ host_memory_allocator(), system_ids::ops::make_system_id(identity, thread_ids::host) }
    , identity{ identity }
{
}

CModuleService::SModuleRecord* CModuleService::find(const mount_point_ids::id_type mount) noexcept
{
    for (auto& record : m_records)
    {
        if (record && record->binding.is_bound() && (module_ids::ops::get_mount_point_id(record->identity) == mount))
        {
            return record.operator->();
        }
    }

    return nullptr;
}

void CModuleService::reply(threading::CThreadPackage& client, const std::int32_t slot, const ModuleResult& result) noexcept
{
    threading::CErasedPodMsg message;
    message.set_async_slot(slot);
    message.assign_payload(result);
    if (!client.post(message))
    {
        m_failed = true;
        MV_CRITICAL_EVENT("Host: module notification delivery failed at slot {}", slot);
    }
}

//==============================================================================
//  Admission and worker dispatch
//==============================================================================

void CModuleService::request(
    CErasedOwner&& owner, const std::int32_t slot,
    threading::CThreadPackage* const client, const EPurpose purpose) noexcept
{
    const ModuleRequest* const request = owner.payload<ModuleRequest>();

    //  Acknowledge receipt even when validation or the single pending slot rejects
    //  the request. Internal Executive transitions never notify the outgoing client.
    if ((purpose == EPurpose::client) && (client != nullptr))
    {
        ModuleResult acknowledgement;
        acknowledgement.module = (request != nullptr) ? request->module : module_ids::id_type{};
        acknowledgement.notice = EModuleNotice::acknowledged;
        acknowledgement.status = EModuleStatus::success;
        reply(*client, slot, acknowledgement);
    }

    if (m_pending || m_internal_result_ready)
    {
        if ((purpose == EPurpose::client) && (client != nullptr))
        {
            ModuleResult result;
            result.module = (request != nullptr) ? request->module : module_ids::id_type{};
            result.status = EModuleStatus::busy;
            reply(*client, slot, result);
        }
        else
        {
            //  Internal lifecycle transitions are issued only after the previous job completes.
            m_failed = true;
            MV_CRITICAL_ASSERT_MSG(false, "Host issued overlapping internal module operations");
        }
        return;
    }

    m_request_owner = std::move(owner);
    m_client = client;
    m_client_slot = slot;
    m_purpose = purpose;
    m_pending = true;
    m_work = {};
    m_work.request = request;

    if ((request == nullptr) || !module_ids::ops::is_valid_id(request->module) ||
        (system_id_registry::lookup_module_name(request->module) == nullptr) ||
        (module_ids::ops::get_mount_point_id(request->module) == mount_point_ids::executable) ||
        (request->action > EModuleAction::replace) ||
        ((request->action != EModuleAction::unload) && (request->file.length() == 0u)))
    {
        finish(EModuleStatus::invalid_request);
        return;
    }

    SModuleRecord* const previous = find(pending_mount_point());
    if ((request->action == EModuleAction::load) && (previous != nullptr))
    {
        finish(EModuleStatus::already_loaded);
        return;
    }

    if ((request->action != EModuleAction::load) &&
        ((previous == nullptr) || ((request->action == EModuleAction::unload) && (previous->identity != request->module))))
    {
        finish(EModuleStatus::not_loaded);
        return;
    }

    //  Prepare stable records here; binding changes belong to the worker.
    m_work.previous = (previous != nullptr) ? &previous->binding : nullptr;
    if (request->action != EModuleAction::unload)
    {
        //  A failed replacement still tears down the previous binding, matching
        //  the existing lifecycle contract, but never attempts an unknown file.
        m_physical_file.deallocate();
        if ((m_filesystem != nullptr) && m_filesystem->resolve(request->file.cstring(), false, m_physical_file))
        {
            m_work.physical_file = m_physical_file.cstring();
        }
        const auto index = module_ids::ops::decode_id(request->module).raw_value();
        if ((index >= module_ids::k_count) || (!m_records[index] && !m_records[index].emplace(request->module)))
        {
            finish(EModuleStatus::allocation_failed);
            return;
        }

        m_work.next = &m_records[index]->binding;
        m_work.memory_context = &m_records[index]->memory_context;
    }
}

mount_point_ids::id_type CModuleService::pending_mount_point() const noexcept
{
    return (m_work.request != nullptr) ?
        module_ids::ops::get_mount_point_id(m_work.request->module) : mount_point_ids::id_type{};
}

void CModuleService::dispatch(threading::CThreadPackage& worker, debug_system::CDebugServiceState* const debug_service) noexcept
{
    if (!m_pending || m_in_flight || m_start_pending)
    {
        return;
    }

    m_work.registry = &system_registry_view();
    m_work.debug_service = debug_service;

    //  One outstanding module operation; the message identity separates this
    //  correlation space from asset-worker slots.
    m_worker_slot = (m_worker_slot == INT32_MAX) ? 0 : m_worker_slot + 1;
    threading::CErasedPodMsg message;
    message.set_async_slot(m_worker_slot);
    message.assign_payload(ModuleWorkRequest{ &m_work });
    if (!worker.post(message))
    {
        finish(EModuleStatus::delivery_failed);
        return;
    }

    m_in_flight = true;
}

//==============================================================================
//  Completion routing
//==============================================================================

void CModuleService::complete(const threading::CErasedPodMsg& message) noexcept
{
    ModuleWorkResult result;
    if (!m_in_flight || (message.query_async_slot() != m_worker_slot) || !message.copy_payload_to(result))
    {
        m_failed = true;
        MV_CRITICAL_EVENT("Host: unexpected module-worker completion");
        return;
    }

    m_in_flight = false;
    if (m_work.cleanup_only)
    {
        finish((result.status == EModuleStatus::success) ? EModuleStatus::installation_failed : result.status);
        return;
    }
    if ((result.status == EModuleStatus::success) && (m_work.next != nullptr) &&
        (pending_mount_point() == mount_point_ids::render))
    {
        //  Keep the request and client correlation until the Host starts the thread.
        m_start_pending = true;
        return;
    }
    finish(result.status);
}

void CModuleService::complete_thread_start(const bool started) noexcept
{
    MV_ASSERT(m_start_pending);
    m_start_pending = false;
    if (started)
    {
        SModuleRecord* const record = find(pending_mount_point());
        MV_ASSERT(record != nullptr);
        record->thread_started = true;
        finish(EModuleStatus::success);
        return;
    }

    //  Startup has joined any failed thread. Release the installed DLL on the
    //  same worker before reporting failure to the ordinary client.
    m_work.previous = m_work.next;
    m_work.next = nullptr;
    m_work.cleanup_only = true;
}

void CModuleService::rendering_stopped() noexcept
{
    if (SModuleRecord* const record = find(mount_point_ids::render))
    {
        record->thread_started = false;
    }
}

void CModuleService::finish(const EModuleStatus status) noexcept
{
    ModuleResult result;
    result.status = status;
    result.module = (m_work.request != nullptr) ? m_work.request->module : module_ids::id_type{};

    //  Report what remains available, including an old binding whose unload failed.
    SModuleRecord* const current = find(pending_mount_point());
    if (current != nullptr)
    {
        if ((status == EModuleStatus::success) && (m_work.next == &current->binding))
        {
            current->function = m_work.function;
        }

        result.available = current->binding.is_ready() &&
            ((pending_mount_point() != mount_point_ids::render) || current->thread_started);
        result.module = current->identity;
        result.function = result.available ? current->function : nullptr;
    }

    if ((m_purpose == EPurpose::client) && (m_client != nullptr))
    {
        reply(*m_client, m_client_slot, result);
    }
    else
    {
        m_internal_result = result;
        m_internal_result_ready = true;
    }

    m_pending = false;
    m_start_pending = false;
    m_work = {};
    m_request_owner.destroy();
    m_client = nullptr;
}

bool CModuleService::take_internal_result(ModuleResult& result, EPurpose& purpose) noexcept
{
    if (!m_internal_result_ready)
    {
        return false;
    }

    result = m_internal_result;
    purpose = m_purpose;
    m_internal_result_ready = false;
    return true;
}

//==============================================================================
//  Shutdown and terminal cleanup
//==============================================================================

bool CModuleService::request_shutdown() noexcept
{
    while (m_shutdown_index < module_ids::k_count)
    {
        const auto& record = m_records[m_shutdown_index++];
        if (!record || !record->binding.is_bound())
        {
            continue;
        }

        CErasedOwner owner = CErasedOwner::create<ModuleRequest>();
        ModuleRequest* const request = owner.payload<ModuleRequest>();
        if (request == nullptr)
        {
            m_failed = true;
            return false;
        }

        request->action = EModuleAction::unload;
        request->module = record->identity;
        this->request(std::move(owner), -1, nullptr, EPurpose::shutdown);
        return true;
    }

    return false;
}

void CModuleService::cancel_pending() noexcept
{
    //  Only before dispatch or after the worker has been joined.
    m_in_flight = false;
    if (m_pending)
    {
        finish(EModuleStatus::delivery_failed);
    }
}

modules::CBoundModule* CModuleService::executive_binding() noexcept
{
    SModuleRecord* const record = find(mount_point_ids::executive);
    return (record != nullptr) ? &record->binding : nullptr;
}

bool CModuleService::release_records() noexcept
{
    bool unloaded = true;
    for (auto& record : m_records)
    {
        if (record && record->binding.is_bound())
        {
            unloaded = false;
            record->binding.retain_until_process_exit();
            (void)debug_system::submit_event<debug_system::EEventLevel::assert, debug_system::EEventType::condition>(
                MV_INTERNAL_USAGE_POINT, "Host retained a DLL after worker unload could not complete");
        }

        record.reset();
    }

    return unloaded;
}

modules::FModuleFunction CModuleService::executive_function() const noexcept
{
    const auto& record = m_records[module_ids::executive_index.raw_value()];
    return record ? record->function : nullptr;
}

}   //  namespace host
