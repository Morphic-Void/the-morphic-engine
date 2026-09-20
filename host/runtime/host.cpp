
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
//
//  This is currently only a test/sketch/prototype
//  to validate the existing codebase features.
//
//  The code is placeholder and not final.

#include <cstdint>      //  std::int32_t, std::uint8_t, std::uint64_t
#include <limits>       //  std::numeric_limits

#include "host/runtime/host.hpp"
#include "host/system/host_context.hpp"
#include "host/runtime/host_worker_thread.hpp"
#include "host/system/system_id_definitions.hpp"
#include "executive/module/binding/executive_binding.hpp"
#include "platform/path/native_path.hpp"
#include "platform/system/performance_counter.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"

#include "debug/macros.hpp"
#include "debug/log_path.hpp"
#include "debug/service.hpp"
#include "platform/system/process_id.hpp"

namespace host
{

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
    const threading::ThreadConfig thread_configs[k_thread_count]{
        {thread_ids::bg_file_io, module_ids::executable, platform::threading::EThreadPriority::Background, host_worker_thread_entry_point()},
        {thread_ids::bg_conditioning, module_ids::executable, platform::threading::EThreadPriority::Background, host_worker_thread_entry_point()},
        {thread_ids::executive, module_ids::executive, platform::threading::EThreadPriority::Normal, m_executive_thread, &modules::CBoundModule::prepare_thread, &m_executive_module} };

    for (std::size_t thread_index = 0u; thread_index < k_thread_count; ++thread_index)
    {
        const std::int32_t thread_slot = m_thread_packages.emplace(thread_configs[thread_index], m_perf_count_conversion);
        if (thread_slot < 0)
        {
            return false;
        }
        m_thread_slots[thread_index] = thread_slot;
        threading::CThreadPackage& package = *m_thread_packages.get_object(thread_slot);
        if (!package.startup())
        {
            return false;
        }
    }
    return true;
}

bool CHost::bind_executive_module() noexcept
{
    constexpr modules::SAdvertisedIdentity advertised_host_identity{
        module_ids::executable, { modules::k_binding_abi_major, 0u },
        modules::k_binding_abi_major, modules::k_binding_abi_major };
    constexpr std::uint32_t expected_module_major = modules::k_binding_abi_major;

    const platform::path::NativePath module_path = platform::path::makeNativePath("MorphicExecutive.dll");
    if (!module_path.is_ready() ||
        !m_executive_module.bind(module_path, module_ids::executive, advertised_host_identity) ||
        !m_executive_module.install(system_registry_view(), module_ids::executive, executive_memory_context(), m_debug_service))
    {
        MV_ERROR("Host failed to bind and install the executive module");
        return false;
    }

    executive::FExecutiveThread executive_thread = nullptr;
    if (!validate_executive_module_compatibility(advertised_host_identity, expected_module_major, executive_thread))
    {
        MV_ERROR("Host failed the executive module compatibility checks");
        return false;
    }

    m_executive_thread = executive_thread;
    return true;
}

bool CHost::validate_executive_module_compatibility(
    const modules::SAdvertisedIdentity& advertised_host_identity,
    const std::uint32_t expected_module_major,
    executive::FExecutiveThread& executive_thread) noexcept
{
    executive_thread = nullptr;

    const modules::SAdvertisedIdentity& module_identity = m_executive_module.advertised_module_identity();
    const std::uint32_t functional_major = m_executive_module.negotiated_functional_major();
    MV_INFO("Executive module version {}.{} supports functional majors [{},{}]",
        module_identity.version.major,
        module_identity.version.minor,
        module_identity.minimum_functional_major,
        module_identity.maximum_functional_major);

    if ((module_identity.version.major != expected_module_major) ||
        (functional_major != modules::highest_common_functional_major(advertised_host_identity, module_identity)))
    {
        return false;
    }

    // The next representable major cannot belong to the negotiated range.
    if (functional_major != std::numeric_limits<std::uint32_t>::max())
    {
        modules::SCoreFunctions unsupported_core;
        const modules::EBindingResult unsupported_result = m_executive_module.populate_core_functions((functional_major + 1u), unsupported_core);

        // Rejection must be explicit and must not expose callable functions.
        if ((unsupported_result != modules::EBindingResult::unsupported_version) || !unsupported_core.is_empty())
        {
            return false;
        }
    }

    modules::FModuleFunction raw_executive_thread = nullptr;
    modules::FModuleFunction unknown_thread = nullptr;
    if (!m_executive_module.query_function(system_type_ids::executive_thread_function, raw_executive_thread) ||
        m_executive_module.query_function(system_type_ids::undefined, unknown_thread) || (unknown_thread != nullptr))
    {
        return false;
    }

    executive_thread = reinterpret_cast<executive::FExecutiveThread>(raw_executive_thread);
    return executive_thread != nullptr;
}

bool CHost::initialise_runtime() noexcept
{
    return
        m_perf_count_conversion.init() &&
        m_thread_packages.initialise() &&
        m_asset_service.initialise() &&
        bind_executive_module() &&
        start_threads();
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

int CHost::execute(const char* const log_tag) noexcept
{
    initialise_debug_service(log_tag);
    MV_INFO("Host: Starting");

    const bool initialised = initialise_runtime();
    if (initialised)
    {
        run();
    }
    const bool shutdown_clean = shutdown();
    return (initialised && shutdown_clean && !m_runtime_failed) ? 0 : 1;
}

void CHost::run() noexcept
{
    threading::CThreadPackage& executive = *thread_package(EWorkerThreadID::executive);
    threading::CThreadPackage& file_io = *thread_package(EWorkerThreadID::bg_file_io);
    threading::CThreadPackage& conditioning = *thread_package(EWorkerThreadID::bg_conditioning);

    while (!m_runtime_failed && !m_asset_service.failed())
    {
        //  Observe client termination before draining: requests published before
        //  its terminal state must be read before an idle Host can stop.
        const threading::EThreadRunState state = executive.query_state();
        if ((m_debug_service != nullptr) && (m_debug_service->read_shutdown_request() != debug_system::EShutdownReason::none))
        {
            m_runtime_failed = true;
            break;
        }
        for (std::int32_t slot = m_thread_packages.first_live(); slot >= 0; slot = m_thread_packages.next_live(slot))
        {
            threading::CThreadPackage& package = *m_thread_packages.get_object(slot);
            threading::CErasedPodMsg message;
            while (package.read(message))
            {
                if (&package != &executive)
                {
                    m_asset_service.complete(message);
                }
                else
                {
                    AssetResult result;
                    result.status = EAssetStatus::invalid_request;
                    threading::CErasedPodMsg response;
                    response.set_async_slot(message.query_async_slot());
                    response.assign_payload(result);
                    if (!executive.post(response))
                    {
                        m_runtime_failed = true;
                    }
                }
            }
            threading::CErasedOwnerMsg owned;
            while (package.read(owned))
            {
                if (&package == &executive)
                {
                    m_asset_service.request(owned, executive, file_io, conditioning);
                }
                else
                {
                    m_asset_service.complete(owned);
                }
            }
        }

        //  Drain accepted requests even if their client has already exited.
        if (((state == threading::EThreadRunState::Exited) || (state == threading::EThreadRunState::Failed)) && m_asset_service.is_idle())
        {
            m_runtime_failed = m_runtime_failed || (state == threading::EThreadRunState::Failed);
            break;
        }
        if ((file_io.query_state() == threading::EThreadRunState::Failed) || (conditioning.query_state() == threading::EThreadRunState::Failed))
        {
            m_runtime_failed = true;
            break;
        }
    }
    m_runtime_failed = m_runtime_failed || m_asset_service.failed();
    if (!m_asset_service.is_idle())
    {   //  Stop borrowers before releasing temporary operation storage. Emit a
        //  failure for every remaining request while the client still exists.
        (void)file_io.shutdown();
        (void)conditioning.shutdown();
        m_asset_service.fail_pending();
    }
}

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
    m_asset_service.deallocate();
    m_thread_packages.deallocate();
    m_executive_thread = nullptr;
    const bool executive_unloaded = m_executive_module.unbind();
    if (!executive_unloaded)
    {
        memory::CMemoryContext* const context = executive_memory_context();
        MV_CRITICAL_EVENT(
            "Executive module safe unload failed with {} live allocations and {} attributed bytes",
            context->get_live_allocation_count(),
            context->get_live_allocated_bytes());
    }
    shutdown_debug_service();
    return executive_unloaded;
}

int host(const char* const log_tag) noexcept
{
    CHost runtime;
    return runtime.execute(log_tag);
}

}   //  namespace host
