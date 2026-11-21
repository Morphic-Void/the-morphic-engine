
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    endpoint_runtime.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Module-owned endpoint creation, common dispatch and safe execution retirement.

#include "runtime/host.hpp"
#include "framework/services/provider_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "foundation/system/system_id_registry.hpp"

namespace host
{

std::uint64_t CHost::allocate_identity() noexcept
{
    const std::uint64_t identity = m_next_identity;
    if (!identity)
    {
        begin_emergency_shutdown();
        return 0u;
    }
    m_next_identity = (identity == UINT64_MAX) ? 0u : (identity + 1u);
    return identity;
}

SModuleEndpoints* CHost::module_endpoints(const module_ids::id_type module) noexcept
{
    if (!module_ids::ops::is_valid_id(module))
    {
        return nullptr;
    }
    const std::uint64_t index = module_ids::ops::decode_id(module).raw_value();
    return ((index < module_ids::k_count) && m_modules[index]) ? &*m_modules[index] : nullptr;
}

SClientRecord* CHost::module_client(const module_ids::id_type module) noexcept
{
    SModuleEndpoints* const record = module_endpoints(module);
    return (record && record->coordinator) ? &*record->coordinator : nullptr;
}

SClientRecord* CHost::first_client() noexcept
{
    for (TInstance<SModuleEndpoints>& module : m_modules)
    {
        if (module && module->coordinator)
        {
            return &*module->coordinator;
        }
    }
    return nullptr;
}

SClientRecord* CHost::next_client(const SClientRecord& previous) noexcept
{
    bool found = false;
    for (TInstance<SModuleEndpoints>& module : m_modules)
    {
        if (!module) continue;
        if (module->coordinator)
        {
            if (found) return &*module->coordinator;
            found = &*module->coordinator == &previous;
        }
        for (std::int32_t slot = module->threads.first_live(); slot >= 0; slot = module->threads.next_live(slot))
        {
            SRequestedThread& thread = *module->threads.get_object(slot);
            if (!thread.client) continue;
            if (found) return &*thread.client;
            found = &*thread.client == &previous;
        }
    }
    return nullptr;
}

SClientRecord* CHost::client_for(const threading::CThreadPackage& package) noexcept
{
    for (SClientRecord* client = first_client(); client; client = next_client(*client))
    {
        if (client->thread == &package) return client;
    }
    return nullptr;
}

SClientRecord* CHost::direct_client(const module_ids::id_type module) noexcept
{
    SModuleEndpoints* const record = module_endpoints(module);
    if (!record || !record->direct_role.value)
    {
        return nullptr;
    }
    for (std::int32_t slot = record->threads.first_live(); slot >= 0; slot = record->threads.next_live(slot))
    {
        SRequestedThread& thread = *record->threads.get_object(slot);
        if ((thread.identity.value == record->direct_role.value) && thread.client &&
            !thread.client->stop_requested && !thread.client->terminal())
        {
            return &*thread.client;
        }
    }
    return nullptr;
}

threading::CThreadPackage* CHost::module_thread_package(const module_ids::id_type module) noexcept
{
    SClientRecord* const client = direct_client(module);
    return client ? client->thread : nullptr;
}

void CHost::create_thread(SClientRecord& requester, const threading::CreateThreadRequest& request,
    const std::int32_t correlation) noexcept
{
    using services::EServiceStatus;
    SModuleEndpoints* const module = module_endpoints(requester.module);
    if (!module || (module->state.instance.value != requester.instance.value))
    {
        requester.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
        return;
    }
    //  Allocate the authoritative attempt before admitting execution, even for rejection.
    const std::int32_t slot = module->threads.emplace();
    if (slot < 0)
    {
        requester.endpoint->fail(threading::EEndpointFailure::accounting_failure);
        return;
    }
    SRequestedThread& attempt = *module->threads.get_object(slot);
    attempt.request = request;
    attempt.requester = &requester;
    attempt.origin = requester.identity;
    attempt.correlation = correlation;
    attempt.outcome_ready = true;
    attempt.outcome = EServiceStatus::invalid_request;
    if ((correlation < 0) || !request.entry || (request.thread == thread_ids::host) ||
        !thread_ids::ops::is_valid_id(request.thread) || !system_id_registry::lookup_thread_name(request.thread) ||
        (request.priority > platform::threading::EThreadPriority::Realtime) || (request.direct_service > 1u))
    {
        return;
    }
    attempt.outcome = EServiceStatus::closing;
    if (module->state.stopping.load(std::memory_order_acquire) || requester.seal_pending || m_work_admission_closed)
    {
        return;
    }
    attempt.outcome = EServiceStatus::occupied;
    if (request.direct_service && module->direct_role.value)
    {
        return;
    }
    for (std::int32_t other = module->threads.first_live(); other >= 0; other = module->threads.next_live(other))
    {
        const SRequestedThread& existing = *module->threads.get_object(other);
        if ((other != slot) && existing.identity.value && (existing.request.thread == request.thread))
        {
            return;
        }
    }
    attempt.outcome = EServiceStatus::allocation_failed;
    attempt.identity.value = allocate_identity();
    if (!attempt.identity.value) return;
    threading::ThreadConfig config;
    config.thread_id = request.thread;
    config.worker_module_id = requester.module;
    config.priority = request.priority;
    config.prepare = &modules::CBoundModule::prepare_thread;
    config.prepare_context = module->binding;
    config.endpoint_entry = request.entry;
    config.module_context = request.context;
    if (!attempt.package.emplace(config, m_perf_count_conversion) ||
        !attempt.client.emplace(requester.module, attempt.identity.value, &*attempt.package, module->state.instance))
    {
        return;
    }
    SClientRecord& client = *attempt.client;
    client.owner_wake = m_owner_wait.endpoint();
    client.endpoint->module_state = &module->state;
    const bool prepared = client.initialise() && attempt.package->install_owner_wake(m_owner_wait.endpoint()) &&
        client.install_host_execution(m_maintenance.restrictions().reader()) &&
        attempt.package->install_batch_client(m_batch_pool.shared(), requester.module, module->binding);
#if defined(MV_HOST_RUNTIME_TESTS)
    if (prepared && m_test_before_client_start) m_test_before_client_start(*attempt.package);
#endif
    if (!prepared || !attempt.package->startup())
    {
        client.phase = EClientPhase::stopped;
        attempt.outcome = EServiceStatus::native_failure;
        return;
    }
    if (request.direct_service) module->direct_role = attempt.identity;
    attempt.outcome_ready = false;
}

void CHost::stop_module(SModuleEndpoints& module, const modules::EStopReason reason) noexcept
{
    module.state.stopping.store(1u, std::memory_order_release);
    if (module.pump) module.pump->request_stop(reason);
    //  The coordinator orders ordinary child shutdown. Only failure handling
    //  bypasses that ordering to attempt emergency quiescence.
    if (!m_emergency_shutdown && (reason != modules::EStopReason::activation_failure) &&
        (reason != modules::EStopReason::pump_failure))
    {
        return;
    }
    for (std::int32_t slot = module.threads.first_live(); slot >= 0; slot = module.threads.next_live(slot))
    {
        SRequestedThread& thread = *module.threads.get_object(slot);
        if (thread.client) thread.client->request_exit();
    }
}

bool CHost::retire_endpoint(SClientRecord& client) noexcept
{
    if (!client.execution_quiescent()) return false;
    m_direct_connections.close_client(client);
    m_service_router.stop_provider(client, true);
    m_service_router.retire_client(client);
    m_asset_service.release_client_acquisitions(client);
    if (!m_direct_connections.client_idle(client) || !m_service_router.client_idle(client) ||
        !m_asset_service.client_idle(client) || !client.replies_idle())
    {
        return false;
    }
    client.flush();
    return true;
}

void CHost::progress_threads(SModuleEndpoints& module) noexcept
{
    using services::EServiceStatus;
    for (std::int32_t slot = module.threads.first_live(); slot >= 0;)
    {
        const std::int32_t next = module.threads.next_live(slot);
        SRequestedThread& attempt = *module.threads.get_object(slot);
        if (!attempt.outcome_ready && attempt.package->preparation())
        {
            attempt.outcome = (attempt.package->preparation() == 1u) ? EServiceStatus::success : EServiceStatus::native_failure;
            attempt.outcome_ready = true;
            if (attempt.outcome == EServiceStatus::success)
            {
                MV_LOG_RICH(info, "Host: Module %s service thread started, endpoint %llu",
                    system_id_registry::lookup_module_name(attempt.client->module), attempt.identity.value);
            }
        }
        if (!attempt.outcome_ready)
        {
            slot = next;
            continue;
        }
        const bool success = attempt.outcome == EServiceStatus::success;
        if (!success && attempt.package && attempt.package->retains_execution() && !attempt.package->join_terminated_execution())
        {
            slot = next;
            continue;
        }
        if (!attempt.result_sent)
        {
            const threading::CreateThreadResult result{ success ? attempt.identity : services::SClientToken{}, attempt.outcome };
            attempt.result_sent = attempt.requester->retry_reply(threading::make_pod_message(attempt.correlation, result));
            if (attempt.result_sent) attempt.requester = nullptr;
        }
        threading::EndpointLifecycle notice{ attempt.origin, success ? attempt.identity : services::SClientToken{},
            attempt.request.thread, attempt.correlation, threading::EEndpointLifecycle::created, attempt.outcome };
        if (!attempt.creation_sent)
        {
            attempt.creation_sent = module.coordinator->post(threading::make_pod_message(-1, notice));
            if (module.coordinator->terminal()) module.coordinator->endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
        }
        if (success && attempt.client && attempt.ready_observed && !attempt.ready_sent &&
            (attempt.client->phase == EClientPhase::ready) && !attempt.client->stop_requested &&
            !attempt.client->seal_pending && !attempt.client->provider_closed && !attempt.client->terminal() &&
            (attempt.client->direct_connections != EDirectConnectionState::closing) &&
            !module.state.stopping.load(std::memory_order_acquire))
        {
            attempt.ready_sent = module.coordinator->post(threading::make_pod_message(-1, threading::EndpointReady{ attempt.identity }));
        }
        if (attempt.client && (!success || attempt.package->execution_ended()))
        {
            if (!attempt.joined)
            {
                attempt.joined = attempt.package->join_terminated_execution();
                if (attempt.joined)
                {
                    MV_LOG_RICH(info, "Host: Module %s service thread joined, endpoint %llu",
                        system_id_registry::lookup_module_name(attempt.client->module), attempt.identity.value);
                }
            }
            if (attempt.joined)
            {
                attempt.client->phase = EClientPhase::stopped;
                if (success && (attempt.package->query_state() == threading::EThreadRunState::Failed))
                {
                    attempt.client->endpoint->fail(threading::EEndpointFailure::execution_failed);
                    if (!m_emergency_shutdown)
                    {
                        MV_LOG(error, "Host: requested execution failed, module {}, instance {}, endpoint {}, correlation {}",
                            attempt.client->module.raw_value(), attempt.client->instance.value, attempt.identity.value, attempt.correlation);
                        begin_emergency_shutdown();
                    }
                }
                if (success && attempt.creation_sent && !attempt.ended_sent)
                {
                    notice.event = threading::EEndpointLifecycle::execution_ended;
                    attempt.ended_sent = module.coordinator->post(threading::make_pod_message(-1, notice));
                }
                if ((!success || attempt.ended_sent) && retire_endpoint(*attempt.client) &&
                    attempt.client->retire_host_execution(false) && attempt.package->shutdown())
                {
                    MV_LOG_RICH(info, "Host: Module %s endpoint retired, endpoint %llu",
                        system_id_registry::lookup_module_name(attempt.client->module), attempt.identity.value);
                    m_asset_service.detach_client(*attempt.client);
                    attempt.client.reset();
                    attempt.package.reset();
                    attempt.retired = true;
                }
            }
        }
        if (!attempt.client && !success)
        {
            attempt.package.reset();
            attempt.retired = true;
        }
        if (attempt.retired && attempt.result_sent && attempt.creation_sent)
        {
            notice.event = threading::EEndpointLifecycle::retired;
            if (!success || module.coordinator->post(threading::make_pod_message(-1, notice)))
            {
                if (module.direct_role.value == attempt.identity.value) module.direct_role = {};
                (void)module.threads.erase(slot);
            }
        }
        slot = next;
    }
}

void CHost::start_pending_module() noexcept
{
    modules::CBoundModule* const binding = m_module_service.pending_binding();
    if (!binding) return;
    const module_ids::id_type identity = binding->advertised_module_identity().advertised_module_id;
    const std::uint64_t index = module_ids::ops::decode_id(identity).raw_value();
    const modules::SHostPumpV1& capability = m_module_service.pending_host_pump();
    if (!m_modules[index])
    {
        if (capability.flags & modules::k_host_pump_native_events)
        {
            for (const TInstance<SModuleEndpoints>& existing : m_modules)
            {
                if (existing && existing->pump && existing->pump->owns_native_queue())
                {
                    m_module_service.complete_thread_start(false);
                    return;
                }
            }
        }
        const std::uint64_t instance = allocate_identity(), endpoint = allocate_identity();
        if (!instance || !endpoint || !m_modules[index].emplace())
        {
            m_module_service.complete_thread_start(false);
            return;
        }
        SModuleEndpoints& module = *m_modules[index];
        module.state.instance = { instance };
        module.binding = binding;
        if (!module.threads.initialise() || !module.coordinator.emplace(identity, endpoint, nullptr, module.state.instance))
        {
            m_modules[index].reset();
            m_module_service.complete_thread_start(false);
            return;
        }
        SClientRecord& client = *module.coordinator;
        client.owner_wake = m_owner_wait.endpoint();
        client.endpoint->module_state = &module.state;
        if (!module.pump.emplace(client, *binding, capability, m_service_router, m_maintenance) || !module.pump->initialise() ||
            !client.endpoint->batch_owner.emplace(m_batch_pool.shared(), client.endpoint->control_state,
                client.endpoint->work_pending, identity, binding))
        {
            m_modules[index].reset();
            m_module_service.complete_thread_start(false);
            return;
        }
        client.endpoint->batch_client = &*client.endpoint->batch_owner;
        module.pump->activate();
    }
    SModuleEndpoints& module = *m_modules[index];
    if (module.pump->ready())
    {
        const modules::SServiceProviderV1& advertisement = m_module_service.pending_service_provider();
        module.provider = { &*module.coordinator, advertisement.contracts, advertisement.contract_count };
        module.ready = true;
        m_module_service.complete_thread_start(true);
        if (identity == module_ids::platform) m_asset_service.install_report_provider(*module.coordinator);
        MV_LOG_RICH(info, "Host: Module %s host pump activated", system_id_registry::lookup_module_name(identity));
    }
    else if (module.pump->failed() || module.coordinator->stop_requested || module.pump->stopped())
    {
        stop_module(module, modules::EStopReason::activation_failure);
        if (stop_module_thread(module_ids::ops::get_mount_point_id(identity))) m_module_service.complete_thread_start(false);
    }
}

bool CHost::stop_module_thread(const mount_point_ids::id_type mount) noexcept
{
    for (TInstance<SModuleEndpoints>& owned : m_modules)
    {
        if (!owned || (module_ids::ops::get_mount_point_id(owned->coordinator->module) != mount))
        {
            continue;
        }
        SModuleEndpoints& module = *owned;
        SClientRecord& client = *module.coordinator;
        if (!client.stop_requested && m_service_router.provider_in_use(client) &&
            (m_module_service.pending_purpose() == CModuleService::EPurpose::client))
        {
            m_module_service.fail_retained(modules::EModuleStatus::busy);
            return false;
        }
        m_asset_service.close_report_provider(client);
        stop_module(module, (m_module_service.pending_action() == modules::EModuleAction::replace) ?
            modules::EStopReason::replacement : modules::EStopReason::shutdown);
        if (!module.threads.is_empty() || !module.pump->stopped() || !retire_endpoint(client))
        {
            return false;
        }
        if (!module.pump->deactivate())
        {
            if (module.pump->retained())
            {
                m_retain_runtime = true;
                m_module_service.fail_retained(modules::EModuleStatus::unload_failed);
            }
            return false;
        }
        if (!client.retire_host_execution(true))
        {
            return false;
        }
        m_asset_service.detach_client(client);
        module.pump.reset();
        module.coordinator.reset();
        owned.reset();
        m_module_service.thread_stopped(mount);
        return true;
    }
    return true;
}

std::uint32_t client_reply_count(const threading::CErasedPodMsg& message) noexcept
{
    if (message.is_payload_a<SchemaProcessCancel>() || message.is_payload_a<platform::reports::PlatformReportCaptureFailed>() ||
        connections::zero_reply_control(message) || message.is_payload_a<services::ReleaseServiceBorrow>() ||
        message.is_payload_a<services::ServiceBorrowQuiescent>() || message.is_payload_a<services::CancelServiceRequest>() ||
        message.is_payload_a<threading::SealEndpoint>() || message.is_payload_a<threading::EndpointReady>())
    {
        return 0u;
    }
    return message.is_payload_a<services::ServiceRequest>() ? 2u : 1u;
}

bool CHost::receive_endpoint_message(const threading::CErasedPodMsg& message, SClientRecord& client) noexcept
{
    using services::EServiceStatus;
    const std::int32_t slot = message.query_async_slot();
    threading::CreateThreadRequest create;
    threading::StopThreadRequest stop;
    threading::SealEndpoint seal;
    services::CompleteServiceRequest complete;
    services::CommitServiceResource commit;
    services::PrepareServiceBorrow prepare;
    services::BeginServiceBorrowRetirement begin;
    services::FinishServiceBorrowRetirement finish;
    services::BindServiceNotification bind;
    threading::EndpointReady ready;
    if (message.copy_payload_to(ready))
    {
        SModuleEndpoints* const module = module_endpoints(client.module);
        if (!module || !client.thread || (module->state.instance.value != client.instance.value) ||
            (ready.endpoint.value != client.identity.value))
        {
            client.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
            return true;
        }
        if (client.terminal() || client.stop_requested || (client.phase == EClientPhase::stopping) ||
            client.seal_pending || client.seal_acknowledged || client.provider_closed ||
            (client.direct_connections == EDirectConnectionState::closing) ||
            module->state.stopping.load(std::memory_order_acquire))
        {
            return true;
        }
        for (std::int32_t index = module->threads.first_live(); index >= 0; index = module->threads.next_live(index))
        {
            SRequestedThread& attempt = *module->threads.get_object(index);
            if (!attempt.client || (&*attempt.client != &client))
            {
                continue;
            }
            if (attempt.ready_observed || (client.phase != EClientPhase::starting) ||
                (attempt.package->preparation() != 1u) || (attempt.request.direct_service &&
                (client.direct_connections != EDirectConnectionState::available)))
            {
                client.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
            }
            else
            {
                attempt.ready_observed = true;
                //  Later requests in this FIFO observe validated child readiness,
                //  even before the next native-preparation progress pass.
                client.phase = EClientPhase::ready;
            }
            return true;
        }
        client.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
        return true;
    }
    if (message.copy_payload_to(create))
    {
        create_thread(client, create, slot);
        return true;
    }
    if (message.copy_payload_to(stop))
    {
        threading::StopThreadResult result{ stop.endpoint, EServiceStatus::stale };
        for (SClientRecord* target = first_client(); target; target = next_client(*target))
        {
            if (target->identity.value != stop.endpoint.value)
            {
                continue;
            }
            if ((target->instance.value == client.instance.value) && target->thread)
            {
                target->request_exit();
                result.status = EServiceStatus::success;
            }
            else result.status = EServiceStatus::invalid_request;
            break;
        }
        (void)client.reply(threading::make_pod_message(slot, result));
        return true;
    }
    if (message.is_payload_a<threading::CloseProviderAdmission>())
    {
        if (client.admission_notice_pending)
        {
            client.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
            return true;
        }
        m_service_router.close_provider_admission(client);
        client.admission_notice_pending = true;
        client.admission_slot = slot;
        return true;
    }
    if (message.copy_payload_to(seal))
    {
        if (client.seal_pending || (seal.pods != client.accounted_pods) || (seal.owners < client.last_independent_owner))
        {
            client.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
            return true;
        }
        client.seal_pending = true;
        client.seal_boundary = seal;
        m_service_router.close_provider_admission(client);
        m_direct_connections.close_client(client);
        return true;
    }
    services::ProviderOperationResult result;
    if (message.copy_payload_to(complete)) result.status = m_service_router.complete(client, complete.reply);
    else if (message.copy_payload_to(commit)) result.status = m_service_router.commit_resource(client, commit.reply);
    else if (message.copy_payload_to(prepare)) result.status = m_service_router.prepare_borrow(client, prepare.offer, result.borrow);
    else if (message.copy_payload_to(begin)) result.status = m_service_router.begin_borrow_retirement(client, begin.borrow);
    else if (message.copy_payload_to(finish)) result.status = m_service_router.finish_borrow_retirement(client, finish.borrow);
    else if (message.copy_payload_to(bind)) result.status = m_service_router.bind_borrow_notification(client, bind.borrow, bind.registration, result.notification);
    else return false;
    (void)client.reply(threading::make_pod_message(slot, result));
    return true;
}

void CHost::drain_client(SClientRecord& client) noexcept
{
    client.terminal_before_drain = client.terminal();
    if (client.thread && client.terminal_before_drain && !client.thread->join_terminated_execution())
    {
        client.endpoint->fail(threading::EEndpointFailure::execution_failed);
        return;
    }
    const threading::CBatchClient* const batch = client.batch_client();
    if (client.terminal_before_drain && !client.invocation_active && batch && batch->outstanding())
    {
        //  Only the endpoint owner can consume batch returns. Execution has
        //  ended, so these obligations cannot settle through ordinary progress.
        client.endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
    }
    bool pods_drained = false, owners_drained = false;
    for (std::uint32_t count = 0u; (count < 64u) && !m_maintenance.captured_pod(client); ++count)
    {
        if (!client.reserve_replies(2u)) break;
        threading::CErasedPodMsg message;
        if (!client.read(message))
        {
            client.cancel_reservation(2u);
            pods_drained = true;
            break;
        }
        const std::uint32_t replies = client_reply_count(message);
        client.cancel_reservation(2u - replies);
        receive_message(message, client);
        client.accounted_pod();
        if (count == 63u) m_turn_pending = true;
    }
    for (std::uint32_t count = 0u; (count < 64u) && !m_maintenance.captured_owner(client); ++count)
    {
        if (!m_asset_service.schema_ingress_available() || !client.reserve_replies(2u))
        {
            break;
        }
        threading::CErasedOwnerMsg message;
        if (!client.capture(message))
        {
            client.cancel_reservation(2u);
            owners_drained = true;
            break;
        }
        const std::uint32_t replies = (message.is_message_a<modules::ModuleRequest>() || message.is_message_a<SchemaProcessRequest>()) ?
            2u : (message.is_message_a<platform::reports::PlatformReportCaptured>() ? 0u : 1u);
        client.cancel_reservation(2u - replies);
        if (defer_startup_module_request(message, client) && m_maintenance.defer_owner(client, message))
        {
            break;
        }
        receive_request(message, client);
        client.accounted_owner();
        if (count == 63u) m_turn_pending = true;
    }
    client.incoming_drained = pods_drained && owners_drained && !client.deferred_records;
    if (client.admission_notice_pending && client.retry_reply(threading::make_pod_message(client.admission_slot, threading::ProviderAdmissionClosed{})))
    {
        client.admission_notice_pending = false;
    }
    if (client.seal_pending && !client.seal_acknowledged && (client.accounted_pods > client.seal_boundary.pods) &&
        (client.accounted_owners >= client.seal_boundary.owners) && m_direct_connections.client_idle(client))
    {
        client.seal_acknowledged = client.terminal() || client.post(threading::make_pod_message(-1, threading::EndpointSealed{}));
    }
    if (client.execution_quiescent()) (void)retire_endpoint(client);
    const threading::EEndpointFailure failure = client.endpoint->failure.load(std::memory_order_acquire);
    if ((failure != threading::EEndpointFailure::none) && !m_emergency_shutdown)
    {
        MV_LOG(error, "Host: endpoint failure module {}, instance {}, endpoint {}, stage {}",
            client.module.raw_value(), client.instance.value, client.identity.value, static_cast<std::uint32_t>(failure));
        begin_emergency_shutdown();
    }
}

void CHost::pump_clients() noexcept
{
    for (TInstance<SModuleEndpoints>& owned : m_modules)
    {
        if (!owned) continue;
        SModuleEndpoints& module = *owned;
        if (module.coordinator->stop_requested || module.state.stopping.load(std::memory_order_acquire))
        {
            stop_module(module, modules::EStopReason::requested);
        }
        module.pump->pump();
        m_turn_pending = m_turn_pending || module.pump->work_pending();
        m_native_stop_requested = m_native_stop_requested || module.pump->native_stop_requested();
        if (module.pump->stopped() && !module.threads.is_empty())
        {
            module.coordinator->endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
        }
        if (module.pump->failed() && !m_emergency_shutdown)
        {
            MV_LOG(error, "Host: module coordinator failed, module {}, instance {}, endpoint {}",
                module.coordinator->module.raw_value(), module.state.instance.value, module.coordinator->identity.value);
            begin_emergency_shutdown();
        }
    }
    for (SClientRecord* client = first_client(); client; client = next_client(*client)) drain_client(*client);
    for (TInstance<SModuleEndpoints>& module : m_modules)
    {
        if (module) progress_threads(*module);
    }
}

}   //  namespace host
