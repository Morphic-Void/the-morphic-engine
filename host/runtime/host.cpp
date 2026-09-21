
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    15 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  The main host service thread for the engine.

#include <cstdint>      //  std::int32_t, std::uint8_t, std::uint64_t

#include "host/runtime/host.hpp"
#include "host/system/host_context.hpp"
#include "host/runtime/host_worker_thread.hpp"
#include "host/system/system_id_definitions.hpp"
#include "executive/module/binding/executive_binding.hpp"
#include "platform/system/performance_counter.hpp"
#include "platform/threading/processor_relax.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"

#include "debug/macros.hpp"
#include "debug/log_path.hpp"
#include "debug/service.hpp"
#include "platform/system/process_id.hpp"

namespace host
{

//==============================================================================
//  Runtime startup
//==============================================================================

CHost::~CHost() noexcept
{
    (void)shutdown();
}

void CHost::initialise_debug_service(const char* const log_tag) noexcept
{
    m_debug_service_owner = TInstance<debug_system::CDebugServiceState>::create();
    if (m_debug_service_owner)
    {
        m_debug_service = m_debug_service_owner.operator->();
        char event_log_path[debug_system::k_log_path_capacity]{};
        char direct_log_path[debug_system::k_log_path_capacity]{};
        const platform::system::CPlatformProcessId process_id = platform::system::query_current_process_id();
        const bool debug_logs_opened =
            process_id.is_valid() &&
            debug_system::format_process_log_path(
                event_log_path, sizeof(event_log_path),
                "logs/morphic_debug", log_tag, process_id.value()) &&
            debug_system::format_process_log_path(
                direct_log_path, sizeof(direct_log_path),
                "logs/morphic_debug_direct", log_tag, process_id.value()) &&
            m_debug_service->configure_log_paths(
                event_log_path,
                direct_log_path) &&
            m_debug_service->open_logs();
        if (debug_logs_opened)
        {
            m_debug_service_installed = debug_system::install_service(m_debug_service);
            if (m_debug_service_installed)
            {
                m_debug_service_started = m_debug_service->start();
            }
        }
    }
}

bool CHost::start_threads() noexcept
{
    const threading::ThreadConfig configurations[]{
        { thread_ids::bg_file_io, module_ids::executable, platform::threading::EThreadPriority::Background, host_worker_thread_entry_point() },
        { thread_ids::bg_conditioning, module_ids::executable, platform::threading::EThreadPriority::Background, host_worker_thread_entry_point() } };

    for (std::uint32_t index = 0u; index < 2u; ++index)
    {
        const std::int32_t slot = m_thread_packages.emplace(configurations[index], m_perf_count_conversion);
        if (slot < 0)
        {
            return false;
        }

        m_thread_slots[index] = slot;
        if (!m_thread_packages.get_object(slot)->startup())
        {
            return false;
        }
    }

    return true;
}

bool CHost::start_executive() noexcept
{
    modules::CBoundModule* const binding = m_module_service.executive_binding();
    const auto entry = reinterpret_cast<executive::FExecutiveThread>(m_module_service.executive_function());
    if ((binding == nullptr) || (entry == nullptr))
    {
        return false;
    }

    const threading::ThreadConfig configuration{
        thread_ids::executive, module_ids::executive, platform::threading::EThreadPriority::Normal,
        entry, &modules::CBoundModule::prepare_thread, binding };
    const std::int32_t slot = m_thread_packages.emplace(configuration, m_perf_count_conversion);
    if (slot < 0)
    {
        return false;
    }

    m_thread_slots[static_cast<std::uint32_t>(EWorkerThreadID::executive)] = slot;
    return m_thread_packages.get_object(slot)->startup();
}

bool CHost::initialise_runtime(const char* const executive_file) noexcept
{
    if (!m_perf_count_conversion.init() || !m_thread_packages.initialise() ||
        !m_asset_service.initialise() || !start_threads())
    {
        return false;
    }

    //  The Host can service worker completions before any Executive code runs.
    CErasedOwner owner = CErasedOwner::create<ModuleRequest>();
    ModuleRequest* const request = owner.payload<ModuleRequest>();
    if ((request == nullptr) || !request->file.set(executive_file))
    {
        return false;
    }

    request->module = module_ids::executive;
    request->required_function = system_type_ids::executive_thread_function;
    m_module_service.request(std::move(owner), -1, nullptr, CModuleService::EPurpose::bootstrap);
    return true;
}

threading::CThreadPackage* CHost::thread_package(const EWorkerThreadID id) noexcept
{
    const std::size_t index = static_cast<std::size_t>(id);
    if ((index >= k_thread_count) || (m_thread_slots[index] < 0))
    {
        return nullptr;
    }
    return m_thread_packages.get_object(m_thread_slots[index]);
}

int CHost::execute(const char* const log_tag, const char* const executive_file) noexcept
{
    initialise_debug_service(log_tag);
    MV_INFO("Host: Starting");

    const bool initialised = initialise_runtime(executive_file);
    if (initialised)
    {
        run();
    }

    const bool shutdown_clean = shutdown();
    return (initialised && shutdown_clean && !m_runtime_failed) ? 0 : 1;
}

//==============================================================================
//  Client admission and Executive lifecycle
//==============================================================================

void CHost::receive_request(threading::CErasedOwnerMsg& message, threading::CThreadPackage& executive) noexcept
{
    if (message.is_message_a<ModuleRequest>())
    {
        const ModuleRequest* const request = message.owner().payload<ModuleRequest>();
        const bool self = (request != nullptr) &&
            (module_ids::ops::get_mount_point_id(request->module) == mount_point_ids::executive) &&
            (request->action != EModuleAction::load);
        if (self)
        {
            //  Set this before validation, allocation or waiting for any other operation.
            executive.request_exit();
            if (m_phase == EPhase::running)
            {
                m_executive_request = message.take_owner();
                m_phase = EPhase::stopping_executive;
            }
            return;
        }

        if (m_phase == EPhase::running)
        {
            m_module_service.request(message.take_owner(), message.query_async_slot(), &executive, CModuleService::EPurpose::client);
        }

        return;
    }

    m_asset_service.request(
        message, executive, *thread_package(EWorkerThreadID::bg_file_io),
        *thread_package(EWorkerThreadID::bg_conditioning));
}

void CHost::executive_failure(const EModuleStatus status) noexcept
{
    m_runtime_failed = true;
    (void)debug_system::submit_event<debug_system::EEventLevel::assert, debug_system::EEventType::condition>(
        MV_INTERNAL_USAGE_POINT, "Executive module lifecycle failed, status {}", static_cast<std::uint32_t>(status));
    if (threading::CThreadPackage* const executive = thread_package(EWorkerThreadID::executive))
    {
        executive->request_exit();
        m_phase = EPhase::stopping_executive;
    }
    else
    {
        m_phase = EPhase::shutting_down;
    }
    m_executive_request.destroy();
}

void CHost::advance_lifecycle(const threading::EThreadRunState executive_state) noexcept
{
    ModuleResult result;
    CModuleService::EPurpose purpose;
    if (m_module_service.take_internal_result(result, purpose))
    {
        if (m_runtime_failed && ((purpose == CModuleService::EPurpose::bootstrap) ||
            (purpose == CModuleService::EPurpose::executive_change)))
        {
            m_phase = EPhase::shutting_down;
            return;
        }

        switch (purpose)
        {
            case CModuleService::EPurpose::bootstrap:
            case CModuleService::EPurpose::executive_change:
            {
                if ((result.status != EModuleStatus::success) || !start_executive())
                {
                    executive_failure((result.status != EModuleStatus::success) ?
                        result.status : EModuleStatus::installation_failed);
                }
                else
                {
                    m_phase = EPhase::running;
                    MV_REPORT("Host: Executive started after asynchronous binding");
                    return;
                }
                break;
            }
            case CModuleService::EPurpose::shutdown:
            {
                if (result.status != EModuleStatus::success)
                {
                    m_runtime_failed = true;
                    (void)debug_system::submit_event<debug_system::EEventLevel::assert, debug_system::EEventType::condition>(
                        MV_INTERNAL_USAGE_POINT, "Module shutdown failed, status {}", static_cast<std::uint32_t>(result.status));
                }
                break;
            }
            default:
            {   //  Client results are delivered directly, never returned to this state machine.
                executive_failure(EModuleStatus::invalid_request);
                break;
            }
        }
    }

    threading::CThreadPackage* const executive = thread_package(EWorkerThreadID::executive);
    if (executive != nullptr)
    {
        const threading::EThreadRunState state = executive_state;
        const bool stopped =
            (state == threading::EThreadRunState::Exited) || (state == threading::EThreadRunState::Failed) ||
            (state == threading::EThreadRunState::Empty);
        if (stopped && (m_phase == EPhase::running))
        {
            m_phase = EPhase::stopping_executive;
        }

        if ((m_phase == EPhase::stopping_executive) && stopped && m_asset_service.is_idle() && m_module_service.is_idle())
        {
            //  Join before dropping queued messages or allowing the worker to
            //  unbind the outgoing Executive. No asset operation still borrows it.
            m_runtime_failed = m_runtime_failed || (state == threading::EThreadRunState::Failed);
            (void)executive->shutdown();
            std::int32_t& slot = m_thread_slots[static_cast<std::uint32_t>(EWorkerThreadID::executive)];
            (void)m_thread_packages.erase(slot);
            slot = -1;

            ModuleRequest* const replacement = m_executive_request.payload<ModuleRequest>();
            if (!m_runtime_failed && (replacement != nullptr) && (replacement->action == EModuleAction::replace))
            {
                replacement->required_function = system_type_ids::executive_thread_function;
                m_module_service.request(std::move(m_executive_request), -1, nullptr, CModuleService::EPurpose::executive_change);
                m_phase = EPhase::replacing_executive;
            }
            else
            {
                if ((replacement != nullptr) && (replacement->action != EModuleAction::unload))
                {
                    executive_failure(EModuleStatus::invalid_request);
                }
                m_executive_request.destroy();
                m_phase = EPhase::shutting_down;
            }
        }
    }

    if ((m_phase == EPhase::shutting_down) && m_asset_service.is_idle() && m_module_service.is_idle())
    {
        if (!m_module_service.request_shutdown())
        {
            //  Dependent assets were disposed of before each module unload.
            m_asset_service.deallocate();
            m_phase = EPhase::complete;
        }
    }
}

//==============================================================================
//  Host message pump
//==============================================================================

void CHost::run() noexcept
{
    threading::CThreadPackage& file_io = *thread_package(EWorkerThreadID::bg_file_io);
    threading::CThreadPackage& conditioning = *thread_package(EWorkerThreadID::bg_conditioning);

    while (m_phase != EPhase::complete)
    {
        if ((file_io.query_state() == threading::EThreadRunState::Failed) ||
            (conditioning.query_state() == threading::EThreadRunState::Failed) ||
            m_module_service.failed() || m_asset_service.failed())
        {
            m_runtime_failed = true;
            break;
        }

        if ((m_debug_service != nullptr) &&
            (m_debug_service->read_shutdown_request() != debug_system::EShutdownReason::none) &&
            (m_phase != EPhase::shutting_down))
        {
            m_runtime_failed = true;
            m_executive_request.destroy();
            if (threading::CThreadPackage* const executive = thread_package(EWorkerThreadID::executive))
            {
                executive->request_exit();
                m_phase = EPhase::stopping_executive;
            }
            else if (m_module_service.is_idle())
            {
                m_phase = EPhase::shutting_down;
            }
        }

        threading::CThreadPackage* const executive = thread_package(EWorkerThreadID::executive);

        //  Observe terminal state before draining queues, then advance the lifecycle.
        //  A terminal publication makes all of that Executive's preceding requests visible.
        const threading::EThreadRunState executive_state = (executive != nullptr) ?
            executive->query_state() : threading::EThreadRunState::Empty;

        for (std::int32_t slot = m_thread_packages.first_live(); slot >= 0; slot = m_thread_packages.next_live(slot))
        {
            threading::CThreadPackage& package = *m_thread_packages.get_object(slot);
            threading::CErasedPodMsg message;
            while (package.read(message))
            {
                AssetDisposeRequest disposal;
                if (message.copy_payload_to(disposal))
                {
                    m_asset_service.request_disposal(disposal, message.query_async_slot(), package);
                }
                else if (&package == executive)
                {
                    AssetResult result;
                    result.status = EAssetStatus::invalid_request;
                    threading::CErasedPodMsg response;
                    response.set_async_slot(message.query_async_slot());
                    response.assign_payload(result);
                    if (!package.post(response))
                    {
                        m_runtime_failed = true;
                    }
                }
                else if (message.query_message_type_id() == k_type_id_v<ModuleWorkResult>)
                {
                    m_module_service.complete(message);
                }
                else
                {
                    m_asset_service.complete(message);
                }
            }

            threading::CErasedOwnerMsg owned;
            while (package.read(owned))
            {
                if (&package == executive)
                {
                    receive_request(owned, package);
                }
                else
                {
                    m_asset_service.complete(owned);
                }
            }
        }

        //  Complete disposal after draining worker replies, then advance module
        //  teardown only when no asset operation retains a worker borrow.
        m_asset_service.complete_disposals();
        advance_lifecycle(executive_state);
        if (m_asset_service.is_idle() && !m_module_service.is_idle() && !m_module_service.is_in_flight())
        {
            if (m_module_service.releases_binding())
            {
                m_asset_service.dispose_dependencies(m_module_service.pending_mount_point());
            }
            m_module_service.dispatch(file_io, m_debug_service);
        }

        platform::threading::processor_relax();
    }
}

//==============================================================================
//  Final runtime cleanup
//==============================================================================

void CHost::shutdown_threads() noexcept
{
    for (std::size_t thread_index = k_thread_count; thread_index > 0u; --thread_index)
    {
        std::int32_t& controller_slot = m_thread_slots[thread_index - 1u];
        if (controller_slot >= 0)
        {
            threading::CThreadPackage* const worker_package = m_thread_packages.get_object(controller_slot);
            if (worker_package != nullptr)
            {
                (void)worker_package->shutdown();
            }
            controller_slot = -1;
        }
    }
}

void CHost::shutdown_debug_service() noexcept
{
    if (m_debug_service_started)
    {
        if (!m_debug_service->stop())
        {
            MV_ERROR("Host failed to stop the debug service cleanly");
        }
        m_debug_service_started = false;
    }
    if (m_debug_service_installed)
    {
        if (!debug_system::uninstall_service(m_debug_service))
        {
            MV_ERROR("Host failed to uninstall the debug service cleanly");
        }
        m_debug_service_installed = false;
    }
    m_debug_service = nullptr;
    m_debug_service_owner.reset();
}

bool CHost::shutdown() noexcept
{
    shutdown_threads();
    m_module_service.cancel_pending();
    m_asset_service.fail_pending();
    m_asset_service.deallocate();
    m_thread_packages.deallocate();
    const bool modules_unloaded = m_module_service.release_records();
    shutdown_debug_service();
    return modules_unloaded;
}

int host(const char* const log_tag, const char* const executive_file) noexcept
{
    CHost runtime;
    return runtime.execute(log_tag, executive_file);
}

}   //  namespace host
