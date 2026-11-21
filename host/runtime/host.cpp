
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

#include "framework/system/system_ids.hpp"
#include "framework/services/service_protocol.hpp"
#include "framework/filesystem/filesystem_messages.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"

#include <local_type_ids.hpp>

#include <algorithm>    //  std::min
#include <cstdint>      //  std::int32_t, std::uint8_t, std::uint64_t
#include <utility>      //  std::pair

#include "platform/system/debugger.hpp"
#include "runtime/host.hpp"
#include "containers/TProcessLifetime.hpp"
#include "system/host_context.hpp"
#include "runtime/host_worker_thread.hpp"
#include "system/system_id_definitions.hpp"
#include "executive/module/binding/executive_binding.hpp"
#include "platform/system/performance_counter.hpp"
#include "platform/threading/processor_relax.hpp"
#include "platform/threading/hw_thread_count.hpp"
#include "framework/assets/asset_messages.hpp"
#include "runtime/host_payloads.hpp"
#include "host/threading/CThreadPackage.hpp"

#include "debug/macros.hpp"
#include "debug/log_path.hpp"
#include "debug/service.hpp"
#include "platform/system/process_id.hpp"

namespace host
{


//==============================================================================
//  Runtime startup
//==============================================================================

CHost::CHost(memory::CMemoryAllocator& allocator) noexcept
    : m_module_service{ allocator }, m_maintenance{ *this }
{
    m_wait_setup_error = m_owner_wait.initialise();
    m_batch_pool.shared().owner_wake = m_owner_wait.endpoint();
}

CHost::~CHost() noexcept
{
    (void)shutdown();
}

void CHost::initialise_debug_service(const char* const log_tag, const char* const log_directory) noexcept
{
    m_debug_service_owner = TInstance<debug_system::CDebugServiceState>::create();
    if (m_debug_service_owner)
    {
        m_debug_service = m_debug_service_owner.operator->();
        m_debug_service->install_shutdown_wake(m_owner_wait.endpoint());
        char event_log_path[debug_system::k_log_path_capacity]{};
        char direct_log_path[debug_system::k_log_path_capacity]{};
        const platform::system::CPlatformProcessId process_id = platform::system::query_current_process_id();
        const bool debug_logs_opened =
            process_id.is_valid() &&
            debug_system::format_process_log_path(
                event_log_path, sizeof(event_log_path),
                "morphic_debug", log_tag, process_id.value(), log_directory) &&
            debug_system::format_process_log_path(
                direct_log_path, sizeof(direct_log_path),
                "morphic_debug_direct", log_tag, process_id.value(), log_directory) &&
            m_debug_service->configure_log_paths(
                event_log_path,
                direct_log_path) &&
            m_debug_service->open_logs();
        if (debug_logs_opened)
        {
            m_debug_service_installed = debug_system::install_service(m_debug_service);
            if (m_debug_service_installed)
            {
                m_debug_service_started = m_debug_service->start({ thread_ids::debug_service, "debug_service" });
                if (!m_debug_service_started && m_debug_service->retains_execution())
                {
                    m_retain_runtime = true;
                }
            }
        }
    }
}

bool CHost::start_threads() noexcept
{
    const thread_ids::id_type identities[]{ thread_ids::bg_file_io, thread_ids::bg_conditioning };
    static_assert((sizeof(identities) / sizeof(identities[0])) == k_max_worker_count);

    for (std::uint32_t index = 0u; index < m_worker_count; ++index)
    {
        const threading::ThreadConfig configuration{
            identities[index], module_ids::executable, platform::threading::EThreadPriority::Background,
            host_worker_thread_entry_point() };
        const std::int32_t slot = m_thread_packages.emplace(configuration, m_perf_count_conversion);
        if (slot < 0)
        {
            return false;
        }

        m_worker_slots[index] = slot;
        if (!m_thread_packages.get_object(slot)->install_owner_wake(m_owner_wait.endpoint()) ||
            !m_thread_packages.get_object(slot)->startup())
        {
            return false;
        }
    }

    return m_asset_service.configure_workers((*worker_package(0u)), worker_package(1u));
}

bool CHost::workers_failed() noexcept
{
    for (std::uint32_t index = 0u; index < m_worker_count; ++index)
    {
        if ((worker_package(index) == nullptr) || (worker_package(index)->query_state() == threading::EThreadRunState::Failed) ||
            worker_package(index)->pod_delivery_poisoned())
        {
            return true;
        }
    }
    return false;
}

bool CHost::initialise_runtime(const char* const executive_file) noexcept
{
    if (!m_perf_count_conversion.init() || !m_thread_packages.initialise() ||
        !m_asset_service.initialise() || !start_threads() ||
        !m_batch_pool.start(m_batch_runner_count))
    {
        return false;
    }

    //  The Host can service worker completions before any Executive code runs.
    CErasedOwner owner = CErasedOwner::create<modules::ModuleRequest>();
    modules::ModuleRequest* const request = owner.payload<modules::ModuleRequest>();
    if ((request == nullptr) || !request->file.set(executive_file))
    {
        return false;
    }

    request->module = module_ids::executive;
    request->required_function = system_type_ids::undefined;
    m_executive_request = std::move(owner);
    m_asset_service.set_filesystem(m_filesystem);
    m_module_service.set_filesystem(m_filesystem);
    const auto scan = threading::make_pod_message(0, FilesystemScanRequest{});
#if defined(MV_HOST_RUNTIME_TESTS)
    m_scan_in_flight = !m_test_reject_initial_scan && worker_package(0u)->post(scan);
#else
    m_scan_in_flight = worker_package(0u)->post(scan);
#endif
    m_initial_scan = m_scan_in_flight;
    return m_scan_in_flight;
}

threading::CThreadPackage* CHost::worker_package(const std::uint32_t index) noexcept
{
    if ((index >= m_worker_count) || (m_worker_slots[index] < 0))
    {
        return nullptr;
    }
    return m_thread_packages.get_object(m_worker_slots[index]);
}

int CHost::execute(const char* const log_tag, const char* const executive_file, const char* const log_directory, const std::uint32_t worker_count, const std::uint32_t batch_runner_count) noexcept
{
    if (m_wait_setup_error.code != platform::threading::EWaitError::none)
    {
        return 1;
    }
    initialise_debug_service(log_tag, log_directory);
    if (m_retain_runtime) return 1;
    MV_INFO("Host: Starting");
    if (worker_count == 0u)
    {
        MV_LOG(error, "Host: Worker count must be greater than zero");
        (void)shutdown();
        return 2;
    }
    const std::uint32_t hardware_threads = platform::threading::query_hardware_thread_count();
    m_worker_count = std::min(worker_count, worker_count_limit(hardware_threads));
    if (m_worker_count != worker_count)
    {
        MV_LOG(info, "Host: Worker count reduced from {} to {} (reported hardware threads {})",
            worker_count, m_worker_count, hardware_threads);
    }
    MV_LOG(info, "Host: Background workers {}, dedicated conditioning workers {}, reported hardware threads {}",
        m_worker_count, ((m_worker_count > 1u) ? (m_worker_count - 1u) : 0u), hardware_threads);
    const std::uint32_t batch_limit = batch_runner_limit(hardware_threads, m_worker_count, k_expected_service_threads);
    m_batch_runner_count = std::min(batch_runner_count, batch_limit);
#if !MV_PLATFORM_HAS_NATIVE_WAIT_WORD
    m_batch_runner_count = 0u;
#endif
    if (m_batch_runner_count != batch_runner_count)
    {
        MV_LOG(info, "Host: Batch runners reduced from {} to {}", batch_runner_count, m_batch_runner_count);
    }
    MV_LOG(info, "Host: Batch runners {}", m_batch_runner_count);

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

bool CHost::defer_startup_module_request(const threading::CErasedOwnerMsg& message,
    const SClientRecord& client) const noexcept
{
    if (((m_phase != EPhase::starting) && (m_phase != EPhase::replacing_executive)) ||
        m_work_admission_closed || m_emergency_shutdown || client.stop_requested || client.terminal() ||
        (client.endpoint->module_state && client.endpoint->module_state->stopping.load(std::memory_order_acquire)))
    {
        return false;
    }
    const modules::ModuleRequest* const request = message.owner().payload<modules::ModuleRequest>();
    return message.is_message_a<modules::ModuleRequest>() && request &&
        !((client.module == module_ids::executive) &&
            (module_ids::ops::get_mount_point_id(request->module) == mount_point_ids::executive) &&
            (request->action != modules::EModuleAction::load));
}

void CHost::receive_request(threading::CErasedOwnerMsg& message, SClientRecord& executive) noexcept
{
    if (!threading::endpoint_settlement(message))
    {
        executive.last_independent_owner = executive.accounted_owners + 1u;
        if (executive.seal_pending && (executive.accounted_owners >= executive.seal_boundary.owners))
        {
            const std::uint32_t replies = (message.is_message_a<modules::ModuleRequest>() || message.is_message_a<SchemaProcessRequest>()) ? 2u : 1u;
            executive.cancel_reservation(replies);
            executive.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
            return;
        }
    }
    if (message.is_message_a<SchemaProcessRequest>())
    {
        m_asset_service.request_process(message, executive, m_work_admission_closed, m_workers_unavailable);
        return;
    }
    if (message.is_message_a<platform::reports::PlatformReportCaptured>())
    {
        m_asset_service.report_candidate(executive, message);
        return;
    }
    if (message.is_message_a<filesystem::FilesystemRefreshRequest>())
    {
        request_refresh(message, executive);
        return;
    }
    if (message.is_message_a<modules::ModuleRequest>())
    {
        MV_DETAIL("Host module lifecycle request");

        const modules::ModuleRequest* const request = message.owner().payload<modules::ModuleRequest>();
        const bool self = (request != nullptr) &&
            (executive.module_id() == module_ids::executive) &&
            (module_ids::ops::get_mount_point_id(request->module) == mount_point_ids::executive) &&
            (request->action != modules::EModuleAction::load);
        if (self)
        {
            executive.cancel_reservation(2u);
            SModuleEndpoints* const module = module_endpoints(module_ids::executive);
            const bool current = module && (module->state.instance.value == executive.instance.value);
            const bool startup = (m_phase == EPhase::starting) || (m_phase == EPhase::replacing_executive);
            if (!current || m_emergency_shutdown || m_work_admission_closed || !m_executive_request.is_empty() ||
                ((m_phase != EPhase::running) && (!startup || executive.terminal())))
            {
                MV_LOG(info, "Host: rejected Executive self-change from instance {}, endpoint {}",
                    executive.instance.value, executive.identity.value);
                return;
            }
            m_executive_request = message.take_owner();
            if (m_phase == EPhase::running)
            {
                stop_module(*module, modules::EStopReason::requested);
                m_phase = EPhase::stopping_executive;
            }
            return;
        }

        if ((m_phase == EPhase::running) && !m_work_admission_closed && !executive.stop_requested && !executive.terminal() &&
            !(executive.endpoint->module_state && executive.endpoint->module_state->stopping.load(std::memory_order_acquire)))
        {
            m_module_service.request(message.take_owner(), message.query_async_slot(), (&executive), CModuleService::EPurpose::client);
        }
        else
        {
            modules::ModuleResult result;
            result.module = (request != nullptr) ? request->module : module_ids::id_type{};
            result.notice = modules::EModuleNotice::acknowledged;
            result.status = modules::EModuleStatus::success;
            (void)executive.reply(threading::make_pod_message(message.query_async_slot(), result));
            result.notice = modules::EModuleNotice::completed;
            result.status = modules::EModuleStatus::busy;
            (void)executive.reply(threading::make_pod_message(message.query_async_slot(), result));
        }

        return;
    }

    if (m_workers_unavailable || m_work_admission_closed)
    {
        AssetResult result;
        result.status = m_work_admission_closed ? EAssetStatus::busy : EAssetStatus::delivery_failed;
        (void)executive.reply(threading::make_pod_message(message.query_async_slot(), result));
    }
    else m_asset_service.request(message, executive);
}

void CHost::receive_message(const threading::CErasedPodMsg& message, SClientRecord& client) noexcept
{
    if (client.seal_pending && !threading::endpoint_settlement(message))
    {
        client.cancel_reservation(client_reply_count(message));
        client.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
        return;
    }
    if (receive_endpoint_message(message, client)) return;
    AssetDisposeRequest disposal;
    BundleReferenceRequest reference;
    AssetAcquireRequest acquire;
    AssetReleaseRequest release;
    SchemaPrepareRequest prepare_schema;
    SchemaProcessCancel cancel_schema;
    services::OpenServiceRequest open;
    services::CloseServiceRequest close;
    services::ServiceRequest command;
    services::ReleaseServiceBorrow borrow_release;
    services::ServiceBorrowQuiescent borrow_quiescent;
    services::CancelServiceRequest cancel;
    services::RegisterServiceNotification notification;
    connections::Register direct_register;
    connections::Request direct_request;
    connections::Decision direct_decision;
    connections::Ready direct_ready;
    connections::Close direct_close;
    connections::Quiescent direct_quiescent;
    const auto slot = message.query_async_slot();
    platform::reports::PlatformReportCaptureFailed outcome;
    if (message.copy_payload_to(cancel_schema))
    {
        m_asset_service.cancel_process(cancel_schema, client);
        return;
    }
    if (message.copy_payload_to(outcome))
    {
        m_asset_service.report_failed(client, outcome);
        return;
    }
    const bool update = message.is_payload_a<platform::reports::PlatformReportUpdateRequest>();
    if (update || message.is_payload_a<platform::reports::PlatformReportRequest>())
    {
        m_asset_service.request_report(client, slot, update, m_work_admission_closed);
        return;
    }
    if (message.copy_payload_to(direct_register))
    {
        m_direct_connections.register_client(client, direct_register, slot);
    }
    else if (message.copy_payload_to(direct_request))
    {
        SClientRecord* const first = direct_request.bulk.is_bulk() ?
            ((designated_client(client.module) == &client) ? &client : nullptr) : endpoint_client(direct_request.first);
        SClientRecord* const second = direct_request.bulk.is_bulk() ?
            designated_client(direct_request.bulk_peer) : endpoint_client(direct_request.second);
        m_direct_connections.request(client, first, second, direct_request, slot, m_maintenance.restrictions().reader());
    }
    else if (message.copy_payload_to(direct_decision))
    {
        m_direct_connections.decision(client, direct_decision);
    }
    else if (message.copy_payload_to(direct_ready))
    {
        m_direct_connections.ready(client, direct_ready);
    }
    else if (message.copy_payload_to(direct_close))
    {
        m_direct_connections.close(client, direct_close);
    }
    else if (message.copy_payload_to(direct_quiescent))
    {
        m_direct_connections.quiescent(client, direct_quiescent);
    }
    else if (message.copy_payload_to(open))
    {
        const SServiceProvider* provider = nullptr;
        if (module_ids::ops::is_valid_id(open.module))
        {
            const auto index = module_ids::ops::decode_id(open.module).raw_value();
            if (m_modules[index]) provider = &m_modules[index]->provider;
        }
        m_service_router.open(open, slot, client, provider);
    }
    else if (message.copy_payload_to(close))
    {
        m_service_router.close(close, slot, client);
    }
    else if (message.copy_payload_to(command))
    {
        m_service_router.request(command, slot, client);
    }
    else if (message.copy_payload_to(borrow_release))
    {
        m_service_router.release_borrow(client, borrow_release);
    }
    else if (message.copy_payload_to(borrow_quiescent))
    {
        m_service_router.quiesce_borrow(client, borrow_quiescent);
    }
    else if (message.copy_payload_to(cancel))
    {
        m_service_router.cancel_request(client, cancel);
    }
    else if (message.copy_payload_to(notification))
    {
        m_service_router.register_notification(client, notification, slot);
    }
    else if (message.copy_payload_to(prepare_schema))
    {
        if (m_work_admission_closed || m_workers_unavailable)
        {
            SchemaPrepareResult result;
            result.status = m_work_admission_closed ? EAssetStatus::busy : EAssetStatus::delivery_failed;
            if (!client.reply(threading::make_pod_message(slot, result))) m_runtime_failed = true;
        }
        else m_asset_service.request_schema(prepare_schema, slot, client);
    }
    else if (message.copy_payload_to(acquire))
    {
        if (m_work_admission_closed)
        {
            AssetAcquireResult result;
            result.status = EAssetStatus::busy;
            if (!client.reply(threading::make_pod_message(slot, result)))
            {
                m_runtime_failed = true;
            }
        }
        else
        {
            m_asset_service.request_acquire(acquire, slot, client);
        }
    }
    else if (message.copy_payload_to(release))
    {
        m_asset_service.request_release(release, slot, client);
    }
    else if (message.copy_payload_to(disposal))
    {
        m_asset_service.request_disposal(disposal, slot, client);
    }
    else if (message.copy_payload_to(reference))
    {
        if (m_work_admission_closed)
        {
            AssetResult result;
            result.status = EAssetStatus::busy;
            if (!client.reply(threading::make_pod_message(slot, result)))
            {
                m_runtime_failed = true;
            }
        }
        else
        {
            m_asset_service.request_reference(reference, slot, client);
        }
    }
    else
    {
        AssetResult result;
        result.status = EAssetStatus::invalid_request;
        if (!client.reply(threading::make_pod_message(slot, result)))
        {
            m_runtime_failed = true;
        }
    }
}

void CHost::reply_refresh(SClientRecord& client, const std::int32_t slot, const filesystem::EFilesystemStatus status) noexcept
{
    const auto reply = threading::make_pod_message(slot, filesystem::FilesystemRefreshResult{ status });
    if (!client.reply(reply))
    {
        m_filesystem_failed = true;
    }
}

void CHost::request_refresh(threading::CErasedOwnerMsg& message, SClientRecord& executive) noexcept
{
    if (m_workers_unavailable || m_work_admission_closed)
    {
        reply_refresh(executive, message.query_async_slot(), filesystem::EFilesystemStatus::delivery_failed);
        return;
    }
    if (m_refresh_count == k_refresh_capacity)
    {
        reply_refresh(executive, message.query_async_slot(), filesystem::EFilesystemStatus::queue_full);
        return;
    }
    SRefresh& refresh = m_refresh_queue[(m_refresh_head + m_refresh_count) % k_refresh_capacity];
    refresh.owner = message.take_owner();
    refresh.client = &executive;
    refresh.slot = message.query_async_slot();
    ++m_refresh_count;
}

void CHost::dispatch_refresh() noexcept
{
    if (m_workers_unavailable || m_scan_in_flight || m_initial_scan || (m_refresh_count == 0u))
    {
        return;
    }
    SRefresh& refresh = m_refresh_queue[m_refresh_head];
    const filesystem::FilesystemRefreshRequest* const request = refresh.owner.payload<filesystem::FilesystemRefreshRequest>();
    filesystem::EFilesystemStatus failure = filesystem::EFilesystemStatus::invalid_root;
    if ((request != nullptr) && (m_filesystem.root_state(request->root.cstring()) == filesystem_image::ERootState::open))
    {
        //  Complete in queue order without scanning immutable virtual content.
        failure = filesystem::EFilesystemStatus::success;
    }
    else if ((request != nullptr) && m_filesystem.prepare_scan(request->root.cstring(), m_root_scan))
    {
        const auto scan = threading::make_pod_message(0, FilesystemScanRequest{ &m_root_scan });
        m_scan_serial = m_filesystem.write_serial();
        if (worker_package(0u)->post(scan))
        {
            m_scan_in_flight = true;
            return;
        }
        failure = filesystem::EFilesystemStatus::delivery_failed;
    }
    reply_refresh((*refresh.client), refresh.slot, failure);
    refresh = {};
    m_refresh_head = (m_refresh_head + 1u) % k_refresh_capacity;
    --m_refresh_count;
}

void CHost::complete_scan(threading::CErasedOwnerMsg& message) noexcept
{
    const auto inspected = threading::inspect_owner_message<FilesystemScanResult>(message);
    if (!m_scan_in_flight || (message.query_async_slot() != 0) ||
        (inspected.state == threading::EOwnerMessageState::invalid))
    {
        m_filesystem_failed = true;
        MV_CRITICAL_EVENT("Host: Invalid filesystem completion at slot {}, in_flight {}, message {}, payload {}",
            message.query_async_slot(), m_scan_in_flight,
            message.query_message_type_id().raw_value(), message.query_owner_type_id().raw_value());
        return;
    }
    m_scan_in_flight = false;
    FilesystemScanResult* const result = inspected.payload;
    const bool scanned = (result != nullptr) && (result->status == filesystem_image::EScanStatus::success);
    if (m_initial_scan)
    {
        m_initial_scan = false;
        if (!scanned || m_runtime_failed || !m_filesystem.adopt(std::move(result->document)))
        {
            MV_LOG(error, "Host: Initial filesystem image could not be constructed");
            executive_failure(modules::EModuleStatus::binding_failed);
            return;
        }
        MV_LOG(info, "Host: Filesystem image ready before Executive bootstrap");
        m_module_service.request(std::move(m_executive_request), -1, nullptr, CModuleService::EPurpose::bootstrap);
        return;
    }
    SRefresh& refresh = m_refresh_queue[m_refresh_head];
    const filesystem::EFilesystemStatus status = !scanned ? filesystem::EFilesystemStatus::scan_failed :
        (m_filesystem.integrate(result->document, m_scan_serial) ? filesystem::EFilesystemStatus::success : filesystem::EFilesystemStatus::integration_failed);
    reply_refresh((*refresh.client), refresh.slot, status);
    refresh = {};
    m_refresh_head = (m_refresh_head + 1u) % k_refresh_capacity;
    --m_refresh_count;
}

void CHost::executive_failure(const modules::EModuleStatus status) noexcept
{
    MV_LOG(error, "Executive module lifecycle failed, status {}", static_cast<std::uint32_t>(status));
    m_executive_request.destroy();
    begin_emergency_shutdown();
}

void CHost::advance_lifecycle() noexcept
{
    modules::ModuleResult result;
    CModuleService::EPurpose purpose;
    if (m_module_service.take_internal_result(result, purpose))
    {
        if ((purpose == CModuleService::EPurpose::bootstrap) || (purpose == CModuleService::EPurpose::executive_change))
        {
            if ((result.status != modules::EModuleStatus::success) || !module_client(module_ids::executive))
            {
                executive_failure(result.status);
            }
            else if (!m_emergency_shutdown)
            {
                m_phase = EPhase::running;
                m_shutdown_timed = m_shutdown_reported = false;
                MV_LOG(info, "Host: Executive started after asynchronous binding");
            }
        }
        else if ((purpose == CModuleService::EPurpose::shutdown) && (result.status != modules::EModuleStatus::success))
        {
            m_retain_runtime = true;
            begin_emergency_shutdown();
        }
    }
    SModuleEndpoints* const executive = module_endpoints(module_ids::executive);
    if (executive && (m_phase == EPhase::running) && !m_executive_request.is_empty())
    {
        stop_module(*executive, modules::EStopReason::requested);
        m_phase = EPhase::stopping_executive;
    }
    if (executive && ((m_phase == EPhase::stopping_executive) || executive->coordinator->stop_requested || executive->pump->stopped()))
    {
        m_phase = EPhase::stopping_executive;
        stop_module(*executive, modules::EStopReason::requested);
        const modules::ModuleRequest* const change = m_executive_request.payload<modules::ModuleRequest>();
        const bool replacing = change && (change->action == modules::EModuleAction::replace) && !m_emergency_shutdown;
        if (!replacing)
        {
            for (TInstance<SModuleEndpoints>& module : m_modules)
            {
                if (module) stop_module(*module, modules::EStopReason::shutdown);
            }
        }
        if (m_module_service.is_idle() && filesystem_idle() && m_asset_service.work_idle() && stop_module_thread(mount_point_ids::executive))
        {
            if (replacing)
            {
                m_executive_request.payload<modules::ModuleRequest>()->required_function = system_type_ids::undefined;
                m_module_service.request(std::move(m_executive_request), -1, nullptr, CModuleService::EPurpose::executive_change);
                m_phase = EPhase::replacing_executive;
            }
            else
            {
                m_executive_request.destroy();
                m_phase = EPhase::shutting_down;
            }
        }
    }
    if ((m_phase == EPhase::shutting_down) && m_asset_service.work_idle() && m_module_service.is_idle() && filesystem_idle())
    {
        if (!m_module_service.request_shutdown() && m_module_service.shutdown_enumerated())
        {
            for (SClientRecord* endpoint = first_client(); endpoint; endpoint = next_client(*endpoint))
            {
                if (!endpoint->execution_quiescent())
                {
                    return;
                }
            }
            if (!m_retain_runtime)
            {
                m_asset_service.close_schema_outputs();
                if (!m_asset_service.work_idle()) return;
                m_asset_service.deallocate();
            }
            m_phase = EPhase::complete;
        }
    }
}

void CHost::run() noexcept
{
    std::uint64_t initial = 0u;
    const platform::threading::WaitError clock_error = m_cadence.remaining(initial);
    if (clock_error.code != platform::threading::EWaitError::none) fail_wait(clock_error);
    while (m_phase != EPhase::complete)
    {
        if (!m_wait_failed)
        {
            const platform::threading::WaitError error = m_owner_wait.begin_cycle();
            if (error.code != platform::threading::EWaitError::none) fail_wait(error);
        }
        const EPhase phase = m_phase;
        m_turn_pending = false;
        ++m_turn_count;
        run_turn();
        if (m_phase == EPhase::complete) break;
        if (!m_wait_failed)
        {
            const platform::threading::WaitError error = m_owner_wait.error();
            if (error.code != platform::threading::EWaitError::none) fail_wait(error);
        }
        if (m_turn_pending || (m_phase != phase))
        {
            continue;
        }
        wait_for_work();
    }
}

void CHost::run_turn() noexcept
{
    threading::CThreadPackage& file_io = *worker_package(0u);
    m_maintenance.poll_stop();
    apply_host_stop();
    m_maintenance.replay();
    m_maintenance.fanout();
    if (workers_failed() && !m_workers_unavailable)
    {
        m_workers_unavailable = true;
        for (std::uint32_t index = 0u; index < m_worker_count; ++index)
        {
            if (auto* const worker = worker_package(index))
            {
                worker->request_exit();
            }
        }
    }
    if (!m_emergency_shutdown && (m_workers_unavailable || m_batch_pool.failed() || m_batch_contract_failed ||
        m_module_service.failed() || m_asset_service.failed() || m_filesystem_failed || m_service_router.failed() || m_direct_connections.failed()))
    {
        begin_emergency_shutdown();
    }

    if ((m_debug_service != nullptr) &&
        (m_debug_service->read_shutdown_request() != debug_system::EShutdownReason::none) &&
        (m_phase != EPhase::shutting_down))
    {
        m_runtime_failed = true;
        m_executive_request.destroy();
        if (threading::CThreadPackage* const executive = module_thread_package(module_ids::executive))
        {
            executive->request_exit();
            m_phase = EPhase::stopping_executive;
        }
        else if (m_module_service.is_idle())
        {
            m_phase = EPhase::shutting_down;
        }
    }

    bool workers_drained = true;
    for (std::int32_t slot = m_thread_packages.first_live(); slot >= 0; slot = m_thread_packages.next_live(slot))
    {
        threading::CThreadPackage& package = *m_thread_packages.get_object(slot);
        if (m_maintenance.captured(nullptr, &package))
        {
            workers_drained = false;
            continue;
        }
        const bool worker_ended_before_drain = package.execution_ended() ||
            (package.query_state() == threading::EThreadRunState::Empty);
        bool pods_drained = false, owners_drained = false;
        for (std::uint32_t count = 0u; count < 64u; ++count)
        {
            threading::CErasedPodMsg message;
            if (!package.read(message))
            {
                pods_drained = true;
                break;
            }
            if ((&package == &file_io) && message.is_payload_a<ModuleWorkResult>()) m_module_service.complete(message);
            else m_asset_service.complete(message, package);
            if (count == 63u) m_turn_pending = true;
        }
        for (std::uint32_t count = 0u; count < 64u; ++count)
        {
            threading::CErasedOwnerMsg message;
            if (!package.read(message))
            {
                owners_drained = true;
                break;
            }
            if ((&package == &file_io) && message.is_message_a<FilesystemScanResult>()) complete_scan(message);
            else m_asset_service.complete(message, package);
            if (count == 63u) m_turn_pending = true;
        }
#if defined(MV_HOST_RUNTIME_TESTS)
        if (m_test_after_worker_drain) m_test_after_worker_drain(*this, package);
#endif
        workers_drained = workers_drained && pods_drained && owners_drained && !m_maintenance.references(package) &&
            worker_ended_before_drain;
    }

    if (m_workers_unavailable && workers_drained && !m_worker_failure_drained)
    {
        //  Worker inputs may be released only after every borrower and its
        //  final completion queue is quiescent, including the healthy worker.
        m_worker_failure_drained = true;
        if (m_module_service.is_in_flight())
        {
            m_module_service.cancel_pending();
        }
        m_asset_service.fail_pending();
        for (auto& refresh : m_refresh_queue)
        {
            if (refresh.client != nullptr)
            {
                reply_refresh(*refresh.client, refresh.slot, filesystem::EFilesystemStatus::delivery_failed);
            }
            refresh = {};
        }
        m_refresh_count = 0u;
        m_initial_scan = false;
        m_scan_in_flight = false;
#if defined(MV_HOST_RUNTIME_TESTS)
        if (m_test_after_worker_failure != nullptr)
        {
            m_test_after_worker_failure(*this);
        }
#endif
    }
    pump_clients();
    m_maintenance.poll_stop();
    apply_host_stop();
    m_asset_service.progress_reports(m_work_admission_closed || (m_phase == EPhase::shutting_down));
    m_service_router.progress();
    m_direct_connections.progress();
    if (m_direct_connections.failed())
    {
        m_runtime_failed = true;
    }

    //  Complete disposal after draining worker replies, then advance module
    //  teardown only when no asset operation retains a worker borrow.
    if (m_module_service.needs_thread_start())
    {
        start_pending_module();
    }

    m_asset_service.complete_disposals();
    dispatch_refresh();
    advance_lifecycle();
    if ((m_phase == EPhase::shutting_down) || (m_phase == EPhase::stopping_executive))
    {
        check_shutdown_deadline();
    }
    const bool stopping_module = !m_module_service.is_idle() && !m_module_service.is_in_flight() &&
        !m_module_service.needs_thread_start() &&
        m_module_service.releases_binding();
    const bool module_stopped = !stopping_module || stop_module_thread(m_module_service.pending_mount_point());
    if (module_stopped && m_asset_service.work_idle() &&
        (!m_module_service.releases_binding() || m_asset_service.dependencies_idle(m_module_service.pending_mount_point())) &&
        !m_module_service.is_idle() && !m_module_service.is_in_flight() && !m_module_service.needs_thread_start())
    {
        if (m_module_service.releases_binding())
        {
            m_asset_service.dispose_dependencies(m_module_service.pending_mount_point());
        }
        if (m_workers_unavailable)
        {
            if (m_worker_failure_drained)
            {
                m_retain_runtime = true;
                m_module_service.fail_retained(modules::EModuleStatus::unload_failed);
            }
        }
        else
        {
            m_module_service.dispatch(file_io, m_debug_service);
            if (m_module_service.has_internal_result()) m_turn_pending = true;
        }
    }

}

void CHost::fail_wait(const platform::threading::WaitError error) noexcept
{
    if (m_wait_failed) return;
    m_wait_failed = true;
    MV_LOG(error, "Host: wait failure {}, native {}", static_cast<std::uint32_t>(error.code), error.native_error);
    begin_emergency_shutdown();
}

void CHost::wait_for_work() noexcept
{
    if (m_wait_failed)
    {
        //  A failed parking mechanism must not stall failure cleanup.
        platform::threading::processor_relax();
        return;
    }
    std::uint64_t remaining = 0u;
    platform::threading::WaitError error = m_cadence.remaining(remaining);
    if (error.code != platform::threading::EWaitError::none)
    {
        fail_wait(error);
        return;
    }
    std::uint32_t native_owners = 0u;
    for (TInstance<SModuleEndpoints>& module : m_modules)
    {
        if (!module || !module->pump)
        {
            continue;
        }
        module->pump->limit_wait(remaining);
        if (module->pump->native_events_active()) ++native_owners;
    }
    if (native_owners > 1u)
    {
        fail_wait({ platform::threading::EWaitError::invalid_argument, 0u });
        return;
    }
    m_direct_connections.limit_wait(remaining);
    if (m_shutdown_timed && !m_shutdown_reported)
    {
        std::uint64_t shutdown_remaining = 0u;
        error = m_shutdown_deadline.remaining_nanoseconds(shutdown_remaining);
        if (error.code != platform::threading::EWaitError::none)
        {
            fail_wait(error);
            return;
        }
        if (shutdown_remaining < remaining) remaining = shutdown_remaining;
    }
    if (!remaining) return;
    threading::CWaitDeadline deadline;
    error = deadline.set_after_nanoseconds(remaining);
    if (error.code != platform::threading::EWaitError::none)
    {
        fail_wait(error);
        return;
    }
    ++m_wait_count;
    const platform::threading::SOwnerWaitResult result = m_owner_wait.wait(deadline, (native_owners != 0u));
    using platform::threading::EOwnerWaitStatus;
    if ((result.status != EOwnerWaitStatus::activity) && (result.status != EOwnerWaitStatus::native_activity) &&
        (result.status != EOwnerWaitStatus::timed_out))
    {
        fail_wait({ (result.status == EOwnerWaitStatus::unsupported) ? platform::threading::EWaitError::unsupported :
            platform::threading::EWaitError::native_failure, result.native_error });
    }
}

//==============================================================================
//  Final runtime cleanup
//==============================================================================

void CHost::check_shutdown_deadline() noexcept
{
    if (m_fatal_timed)
    {
        std::uint64_t remaining = 0u;
        platform::threading::WaitError error = m_fatal_deadline.remaining_nanoseconds(remaining);
#if defined(MV_HOST_RUNTIME_TESTS)
        if (m_test_clock_failure == 4u) error = { platform::threading::EWaitError::native_failure, 456u };
#endif
        if ((error.code != platform::threading::EWaitError::none) || !remaining)
        {
            terminate_runtime();
        }
    }
    if (m_shutdown_reported) return;
    platform::threading::WaitError error;
    const bool starting = !m_shutdown_timed;
    if (starting)
    {
        m_shutdown_timed = true;
        error = m_shutdown_deadline.set_after_milliseconds(10000u);
#if defined(MV_HOST_RUNTIME_TESTS)
        if (m_test_clock_failure == 1u)
        {
            error = { platform::threading::EWaitError::native_failure, 123u };
        }
#endif
    }
    std::uint64_t remaining = 0u;
    if (error.code == platform::threading::EWaitError::none)
    {
        error = m_shutdown_deadline.remaining_nanoseconds(remaining);
#if defined(MV_HOST_RUNTIME_TESTS)
        if (m_test_clock_failure == 2u)
        {
            error = { platform::threading::EWaitError::native_failure, 456u };
        }
#endif
    }
    if (error.code != platform::threading::EWaitError::none)
    {
        m_shutdown_reported = m_runtime_failed = true;
        MV_LOG(error, "Host: shutdown clock failed (code {}, native {}); continuing progress with live resources retained",
            static_cast<unsigned>(error.code), error.native_error);
    }
    else if (remaining == 0u)
    {
        m_shutdown_reported = m_runtime_failed = true;
        MV_LOG(error, "Host: shutdown grace expired; continuing progress with live resources retained");
    }
}

[[noreturn]] void CHost::terminate_runtime() noexcept
{
    MV_LOG(error, "Host: fatal shutdown could not prove quiescence; terminating with live storage retained");
    platform::system::fail_fast();
}

void CHost::begin_emergency_shutdown() noexcept
{
    m_runtime_failed = true;
    if (!m_emergency_shutdown)
    {
        m_emergency_shutdown = true;
        platform::threading::WaitError error = m_fatal_deadline.set_after_milliseconds(10000u);
#if defined(MV_HOST_RUNTIME_TESTS)
        if (m_test_clock_failure == 3u) error = { platform::threading::EWaitError::native_failure, 123u };
#endif
        if (error.code != platform::threading::EWaitError::none) terminate_runtime();
        m_fatal_timed = true;
    }
    m_work_admission_closed = true;
    m_service_router.close_admission();
    m_direct_connections.close_all();
    for (TInstance<SModuleEndpoints>& module : m_modules)
    {
        if (module) stop_module(*module, modules::EStopReason::shutdown);
    }
    m_phase = module_client(module_ids::executive) ? EPhase::stopping_executive : EPhase::shutting_down;
}

bool CHost::shutdown_threads() noexcept
{
    //  Every package is unique, including the single combined I/O/conditioning
    //  worker. Join all borrowers before any service releases input storage.
    if (!m_direct_connections.empty() || !m_maintenance.empty() || m_maintenance.running() || m_maintenance.restrictions().active())
    {
        return false;
    }
    bool stopped = true;
    for (std::int32_t slot = m_thread_packages.last_live(); slot >= 0; slot = m_thread_packages.prev_live(slot))
    {
        auto& package = *m_thread_packages.get_object(slot);
        if (auto* const client = client_for(package))
        {
            if (!client->retire_host_execution(false))
            {
                stopped = false;
                continue;
            }
        }
        package.request_exit();
        while (package.retains_execution() && !package.execution_ended())
        {
            check_shutdown_deadline();
            platform::threading::processor_relax();
        }
        if ((package.query_state() != threading::EThreadRunState::Empty) && !package.shutdown())
        {
            stopped = false;
        }
    }
    if (!stopped) return false;
    for (auto& slot : m_worker_slots)
    {
        slot = -1;
    }
    m_worker_count = 0u;
    return true;
}

bool CHost::shutdown_debug_service() noexcept
{
    if (m_debug_service_started)
    {
        if (!m_debug_service->stop())
        {
            MV_ERROR("Host failed to stop the debug service cleanly");
            m_retain_runtime = true;
            return false;
        }
        m_debug_service_started = false;
    }
    if (m_debug_service_installed)
    {
        if (!debug_system::uninstall_service(m_debug_service))
        {
            MV_ERROR("Host failed to uninstall the debug service cleanly");
            m_retain_runtime = true;
            return false;
        }
        m_debug_service_installed = false;
    }
    m_debug_service = nullptr;
    m_debug_service_owner.reset();
    return true;
}

bool CHost::shutdown() noexcept
{
    if ((m_phase != EPhase::complete) && (worker_package(0u) != nullptr))
    {
        begin_emergency_shutdown();
        run();
    }
    if (!shutdown_threads()) m_retain_runtime = true;
    const bool batch_stopped = m_batch_pool.stop();
    if (!batch_stopped) m_retain_runtime = true;
    if (m_retain_runtime)
    {
        //  Preserve the complete graph, including callback context and module
        //  memory contexts. Retaining just a native DLL would leave dangling data.
        if (m_debug_service_started)
        {
            (void)m_debug_service->stop();
            m_debug_service_started = false;
        }
        return false;
    }
    m_module_service.cancel_pending();
    m_asset_service.fail_pending();
    m_asset_service.deallocate();
    for (SRefresh& refresh : m_refresh_queue)
    {
        refresh.owner.destroy();
    }
    m_executive_request.destroy();
    m_thread_packages.deallocate();
    const bool modules_unloaded = m_module_service.release_records();
    MV_LOG(info, "Host: scheduler turns {}, wait calls {}", m_turn_count, m_wait_count);
    const bool debug_stopped = shutdown_debug_service();
    return modules_unloaded && batch_stopped && debug_stopped;
}

//  Declaration order installs the context before any runtime containers capture
//  it, and keeps it and its allocator alive until every runtime member is gone.
struct SHostProcessState
{
    CHostContext context;
    CHost runtime{ context.allocator() };
};

int host(const char* const log_tag, const char* const executive_file, const char* const log_directory, const std::uint32_t worker_count, const std::uint32_t batch_runner_count) noexcept
{
    //  The process-facing Host is entered once. All execution is quiescent before
    //  a terminal retention decision; the owner preserves the complete graph.
    static bool entered{ false };
    if (entered) return 1;
    entered = true;
    static TProcessLifetime<SHostProcessState> process;
    if (!process->context.is_installed())
    {
        (void)process.destroy();
        return 1;
    }
    const int result = process->runtime.execute(log_tag, executive_file, log_directory, worker_count, batch_runner_count);
    if (process->runtime.retains_state())
    {
        (void)process.retain_until_process_exit();
    }
    else (void)process.destroy();
    return result;
}

}   //  namespace host
