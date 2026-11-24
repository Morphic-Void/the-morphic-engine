
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_preamble.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Affine callbacks and bounded pressure dispatch shared by both execution modes.

#include "host/runtime/execution/module_preamble.hpp"
#include "framework/threading/CThreadContext.hpp"

namespace host
{

thread_local CModulePreamble* CModulePreamble::t_invocation{ nullptr };

CModulePreamble::CModulePreamble(threading::CEndpointResources& resources, CModuleChannel& channel,
    modules::CBoundModule& binding, const modules::SExecutionV1& execution,
    memory::CMemoryContext& host_memory, void* const creation_context,
    const platform::threading::SAwakeClock clock) noexcept
    : m_resources{ resources }, m_channel{ channel }, m_binding{ binding }, m_execution{ execution }
    , m_communication{ s_communication, this }, m_self{ host_memory }, m_clock{ clock }
{
    m_context.instance = resources.instance;
    m_context.endpoint = resources.identity;
    m_context.address = system_ids::ops::make_system_id(resources.worker_module_id, resources.thread_id);
    m_context.communication = &m_communication;
    m_context.execution = resources.host_execution;
    m_context.batch = resources.batch_client;
    m_context.owner_wake = resources.owner_wake;
    m_context.creation_context = creation_context;
}

void CModulePreamble::fail(const threading::EEndpointFailure reason) noexcept
{
    m_failed = true;
    m_resources.fail(reason);
    request_stop(modules::EExecutionStop::execution_failure);
}

bool CModulePreamble::enter_module_invocation() noexcept
{
    if (t_invocation || (m_native_owner != platform::threading::query_current_thread_id()) ||
        !m_binding.prepare_host_call(m_resources.thread_id, &m_context))
    {
        fail(threading::EEndpointFailure::execution_failed);
        m_retained = m_runtime != nullptr;
        return false;
    }
    t_invocation = this;
    return true;
}

void CModulePreamble::leave_module_invocation() noexcept
{
    t_invocation = nullptr;
}

bool CModulePreamble::activate() noexcept
{
    if (m_created)
    {
        fail(threading::EEndpointFailure::execution_failed);
        m_retained = !m_destroyed;
        return false;
    }
    if (!m_execution.is_valid() ||
        ((!m_execution.create || (m_execution.flags & modules::k_execution_native_events)) &&
            (m_resources.thread_id != thread_ids::host)))
    {
        fail(threading::EEndpointFailure::execution_failed);
        m_destroyed = true;
        return false;
    }
    m_native_owner = platform::threading::query_current_thread_id();
    if (!m_native_owner.is_valid())
    {
        fail(threading::EEndpointFailure::execution_failed);
        m_destroyed = true;
        return false;
    }
    //  Provisioning may have been installed after construction but before launch.
    m_context.execution = m_resources.host_execution;
    m_context.batch = m_resources.batch_client;
    m_context.owner_wake = m_resources.owner_wake;
    if (m_execution.create)
    {
        if (!enter_module_invocation())
        {
            m_destroyed = true;
            return false;
        }
        const modules::EExecutionStatus status = m_execution.create(&m_context, &m_runtime);
        leave_module_invocation();
        if (status != modules::EExecutionStatus::success)
        {
            fail(threading::EEndpointFailure::execution_failed);
            m_retained = m_runtime != nullptr;
            m_destroyed = !m_retained;
            return false;
        }
    }
    m_created = true;
    m_progress.work_pending = 1u;
    return true;
}

void CModulePreamble::request_stop(const modules::EExecutionStop reason) noexcept
{
    if (!m_stopping) m_stop_reason = reason;
    m_stopping = true;
    for (SConnection& connection : m_connections)
    {
        if (connection.phase != EConnectionPhase::empty)
        {
            close_connection(connection, connections::EStatus::cancelled);
        }
    }
}

std::uint32_t CModulePreamble::run_serviced(threading::CThreadResources& resources) noexcept
{
    if ((&resources != &m_resources) || (resources.thread_id == thread_ids::host) || !m_created)
    {
        fail(threading::EEndpointFailure::execution_failed);
        m_retained = m_created && !m_destroyed;
        return 1u;
    }
    threading::CThreadContext wait_context{ resources };
    while (!m_destroyed && !m_retained)
    {
        //  Observe before processing and recheck runnable work before parking.
        //  Capacity returned on either side of that recheck changes this epoch.
        const std::uint32_t epoch = wait_context.observe_epoch();
        (void)resources.work_pending.exchange(0u, std::memory_order_acq_rel);
        process();
        wait_context.advance_heartbeat();
        if (m_destroyed || m_retained)
        {
            break;
        }
        if (m_stopping) wait_context.mark_exiting();
        if (work_pending()) continue;
        std::uint64_t remaining = UINT64_MAX;
        limit_wait(remaining);
        if (!remaining) continue;
        if (remaining == UINT64_MAX)
        {
            if (m_stopping) (void)wait_context.wait_for_new_epoch_preserving_state(epoch);
            else (void)wait_context.wait_for_new_epoch(epoch);
            continue;
        }
        threading::CWaitDeadline deadline;
        if (deadline.set_after_nanoseconds(remaining).code != platform::threading::EWaitError::none)
        {
            fail(threading::EEndpointFailure::execution_failed);
            m_retained = true;
            break;
        }
        const threading::WaitResult result = m_stopping ?
            wait_context.wait_for_new_epoch_preserving_state(epoch, deadline) : wait_context.wait_for_new_epoch(epoch, deadline);
        if ((result.status != threading::EWaitStatus::satisfied) && (result.status != threading::EWaitStatus::timed_out))
        {
            fail(threading::EEndpointFailure::execution_failed);
            m_retained = true;
            break;
        }
    }
    return m_failed ? 1u : 0u;
}

void CModulePreamble::observe(const modules::EExecutionStatus status, const modules::SExecutionProgress& progress) noexcept
{
    if ((status != modules::EExecutionStatus::success) || (progress.ready > 1u) ||
        (progress.work_pending > 1u) || (progress.direct_input > 1u) || (progress.work_complete > 1u) ||
        (progress.native_events_active > 1u) || (progress.stop_requested > 1u) ||
        (progress.work_complete && !m_stopping) ||
        ((progress.native_events_active || progress.stop_requested) && !(m_execution.flags & modules::k_execution_native_events)))
    {
        fail(threading::EEndpointFailure::execution_failed);
        return;
    }
    if (progress.ready && !m_ready && !m_stopping && !m_failed)
    {
        m_ready = m_ready_pending = true;
    }
    m_progress = progress;
    m_progress.ready = m_ready ? 1u : 0u;
    m_application_stop = m_application_stop || (progress.stop_requested != 0u);
    m_next_wake = UINT64_MAX;
    if (progress.next_wake_ns != UINT64_MAX)
    {
        std::uint64_t now = 0u;
        if (m_clock.read(now).code != platform::threading::EWaitError::none)
        {
            fail(threading::EEndpointFailure::execution_failed);
            return;
        }
        m_next_wake = (progress.next_wake_ns < (UINT64_MAX - now)) ? (now + progress.next_wake_ns) : UINT64_MAX;
    }
}

bool CModulePreamble::invoke(const modules::SInvocation& invocation) noexcept
{
    if (!m_execution.handle)
    {
        progress_coordinator();
        return true;
    }
    if (!enter_module_invocation()) return false;
    modules::SExecutionProgress progress;
    const modules::EExecutionStatus status = m_execution.handle(m_runtime, &invocation, &progress);
    leave_module_invocation();
    observe(status, progress);
    return status == modules::EExecutionStatus::success;
}

std::uint64_t CModulePreamble::remaining_budget() noexcept
{
    std::uint64_t now = 0u;
    if ((m_clock.read(now).code != platform::threading::EWaitError::none) || (now < m_turn_start))
    {
        fail(threading::EEndpointFailure::execution_failed);
        return 0u;
    }
    const std::uint64_t elapsed = now - m_turn_start;
    return (elapsed < m_turn_budget) ? (m_turn_budget - elapsed) : 0u;
}

bool CModulePreamble::service_async() noexcept
{
    if (!drain_controls()) return false;
    if (m_stopping && !m_stop_notified)
    {
        return false;
    }
    if (m_event_pending) return false;
    if (m_async_since_work) return true;
    if (m_host_output_interest && m_channel.to_host.writable_count()) m_host_output_interest = false;
    if (m_owner_output_interest && m_resources.worker_to_host_owned_msgs.writable_count()) m_owner_output_interest = false;
    if (m_control_output_interest && m_channel.lifecycle.to_host.writable_count()) m_control_output_interest = false;
    for (SConnection& connection : m_connections)
    {
        if (connection.output_interest && connection.descriptor && connection.descriptor->outgoing &&
            connection.descriptor->outgoing->writable_count())
        {
            connection.output_interest = false;
        }
    }
    modules::SInvocation invocation;
    invocation.event = modules::EExecutionEvent::service_async;
    invocation.time_budget_ns = remaining_budget();
    const bool success = invoke(invocation);
    m_async_since_work = true;
    return success;
}

void CModulePreamble::refresh_lanes() noexcept
{
    for (std::uint32_t index = 0u; index < k_connections; ++index)
    {
        const SConnection& connection = m_connections[index];
        SPressureLane& lane = m_lanes[index];
        lane.runnable = false;
        if ((connection.phase != EConnectionPhase::active) || !connection.descriptor ||
            !m_progress.direct_input || m_stopping)
        {
            continue;
        }
        const SDirectEndpointDescriptor& descriptor = *connection.descriptor;
        const CDirectOperation operation{ descriptor.admission };
        if (!operation || !descriptor.incoming)
        {
            continue;
        }
        lane.occupied = descriptor.incoming->readable_count();
        lane.capacity = descriptor.incoming->capacity();
        lane.runnable = lane.occupied != 0u;
    }
    SPressureLane& host = m_lanes[k_host_lane];
    host.occupied = m_channel.to_module.readable_count();
    host.capacity = m_channel.to_module.capacity();
    host.preference = 1u;
    host.runnable = host.occupied != 0u;
    SPressureLane& self = m_lanes[k_self_lane];
    self.local = true;
    self.occupied = m_self.size();
    self.runnable = self.occupied != 0u;
}

bool CModulePreamble::dispatch_ordinary(const std::uint32_t lane) noexcept
{
    threading::CErasedPodMsg message;
    modules::SInvocation invocation;
    invocation.event = modules::EExecutionEvent::message;
    invocation.message = &message;
    invocation.time_budget_ns = remaining_budget();
    if (lane == k_host_lane)
    {
        if (!m_channel.read_preamble_pod(message))
        {
            return false;
        }
        if (m_host_dispatched == UINT64_MAX)
        {
            fail(threading::EEndpointFailure::publication_overflow);
            return false;
        }
        ++m_host_dispatched;
    }
    else if (lane == k_self_lane)
    {
        if (!m_self.pop(message)) return false;
        invocation.source = { connections::SSource::EKind::self, m_context.address, m_context.endpoint, {} };
    }
    else
    {
        SConnection& connection = m_connections[lane];
        if ((connection.phase != EConnectionPhase::active) || !connection.descriptor || !m_progress.direct_input || m_stopping)
        {
            return false;
        }
        const SDirectEndpointDescriptor& descriptor = *connection.descriptor;
        const CDirectOperation operation{ descriptor.admission };
        if (!operation || !descriptor.incoming || !descriptor.incoming->read(message))
        {
            return false;
        }
        invocation.source = { connections::SSource::EKind::direct, descriptor.peer_address, descriptor.peer, descriptor.token };
        if (!descriptor.peer_notification.signal()) fail(threading::EEndpointFailure::wake_failed);
    }
    (void)invoke(invocation);
    m_async_since_work = false;
    ++m_since_async;
    return true;
}

void CModulePreamble::process(const std::uint64_t time_budget_ns) noexcept
{
    if (!m_created || m_destroyed || m_retained)
    {
        return;
    }
    if (m_processing || (m_native_owner != platform::threading::query_current_thread_id()))
    {
        fail(threading::EEndpointFailure::execution_failed);
        return;
    }
    m_processing = true;
    m_turn_budget = (time_budget_ns < 2000000u) ? time_budget_ns : 2000000u;
    m_control_count = 0u;
    m_async_since_work = false;
    if (m_clock.read(m_turn_start).code != platform::threading::EWaitError::none)
    {
        fail(threading::EEndpointFailure::execution_failed);
        m_processing = false;
        return;
    }
    m_resources.work_pending.exchange(0u, std::memory_order_acq_rel);
    //  Re-arm coalesced notifications before inspecting queues/capacity. The
    //  serviced driver observes its wait epoch before this clear/recheck, so
    //  publication on either side cannot leave it parked with runnable work.
    //  A sent quiescence acknowledgement relinquishes the descriptor lifetime.
    for (const SConnection& connection : m_connections)
    {
        if (connection.descriptor && !connection.quiescence_sent)
        {
            connection.descriptor->notification.clear_pending();
            if (connection.descriptor->notification.failed())
            {
                fail(threading::EEndpointFailure::wake_failed);
            }
        }
    }
    if (m_context.execution)
    {
        m_context.execution->notification.clear_pending();
    }
    if (m_resources.control_state.exit_requested()) request_stop(modules::EExecutionStop::requested);
    if (m_resources.failure.load(std::memory_order_acquire) != threading::EEndpointFailure::none)
    {
        m_failed = true;
        request_stop(modules::EExecutionStop::execution_failure);
    }
    if (drain_controls())
    {
        if (!m_started)
        {
            m_started = true;
            if (!m_stopping)
            {
                modules::SInvocation invocation;
                invocation.event = modules::EExecutionEvent::start;
                invocation.time_budget_ns = remaining_budget();
                (void)invoke(invocation);
            }
        }
        std::uint32_t ordinary = 0u, notifications = 0u;
        bool initial_service = false;
        while (!m_retained && !m_destroyed && drain_controls())
        {
            if (m_stopping && !m_stop_notified)
            {
                m_stop_notified = true;
                modules::SInvocation invocation;
                invocation.event = modules::EExecutionEvent::stop;
                invocation.notification.stop_reason = m_stop_reason;
                invocation.time_budget_ns = remaining_budget();
                (void)invoke(invocation);
                m_async_since_work = false;
                continue;
            }
            progress_controls();
            if ((notifications < k_control_budget) && dispatch_notification())
            {
                ++notifications;
                m_async_since_work = false;
                continue;
            }
            if (!initial_service)
            {
                if (!service_async()) break;
                initial_service = true;
            }
            progress_completion();
            if (m_destroyed || m_retained || (ordinary == k_ordinary_budget) || !remaining_budget())
            {
                break;
            }
            refresh_lanes();
            const SPressureSelection selected = m_selection.select();
            if (selected.lane == UINT32_MAX) break;
            bool dispatched = false;
            for (std::uint32_t count = 0u; count < selected.allowance; ++count)
            {
                if ((ordinary == k_ordinary_budget) || !remaining_budget() || !drain_controls())
                {
                    break;
                }
                if (m_event_pending || (m_stopping && !m_stop_notified))
                {
                    break;
                }
                if (!dispatch_ordinary(selected.lane))
                {
                    break;
                }
                dispatched = true;
                ++ordinary;
                m_selection.served(selected.lane);
                if (m_since_async == k_async_cadence)
                {
                    m_since_async = 0u;
                    if (!service_async()) break;
                }
            }
            m_selection.finish(selected, dispatched);
            if (!dispatched) break;
        }
        if (!m_destroyed && !m_retained)
        {
            (void)service_async();
            progress_controls();
            progress_completion();
        }
    }
    diagnose_pressure();
    m_processing = false;
}

void CModulePreamble::diagnose_pressure() noexcept
{
    std::uint64_t now = 0u;
    if (m_clock.read(now).code != platform::threading::EWaitError::none)
    {
        return; //  Diagnostics never create a new failure or admission decision.
    }
    const std::uint64_t address = m_context.address.raw_value();
    const auto report = [&](CPressureDiagnostic& diagnostic, const bool waiting, const char* const kind,
        const std::uint64_t source, const std::uint32_t occupied, const std::uint32_t capacity,
        const std::uint64_t progress)
    {
        CPressureDiagnostic::log(diagnostic.observe(now, waiting, progress), kind, address,
            m_context.endpoint.value, source, occupied, capacity, 0u);
    };
    report(m_pressure, (m_host_output_interest && !m_channel.to_host.writable_count()), "module Host-output pressure",
        0u, m_channel.to_host.readable_count(), m_channel.to_host.capacity(), m_resources.published_pods);
    report(m_owner_pressure, (m_owner_output_interest && !m_resources.worker_to_host_owned_msgs.writable_count()),
        "module owning-output pressure", 0u, 0u, 0u, m_resources.published_owners);
    report(m_control_pressure, !m_channel.lifecycle.to_host.writable_count(),
        "module lifecycle pressure", 0u, m_channel.lifecycle.to_host.readable_count(), k_module_lifecycle_capacity, 0u);
    report(m_self_pressure, (m_self.size() == m_self.limit()), "module self pressure",
        k_self_lane, m_self.size(), m_self.limit(), m_lanes[k_self_lane].dispatched);
    for (SConnection& connection : m_connections)
    {
        const SDirectEndpointDescriptor* const descriptor = connection.quiescence_sent ? nullptr : connection.descriptor;
        const CModulePodPipe* const pipe = descriptor ? descriptor->outgoing : nullptr;
        report(connection.pressure, (connection.output_interest && pipe && !pipe->writable_count()),
            "module direct-output pressure", connection.notification.connection.value,
            (pipe ? pipe->readable_count() : 0u), (pipe ? pipe->capacity() : 0u), 0u);
    }
    refresh_lanes();
    for (std::uint32_t index = 0u; index < k_lanes; ++index)
    {
        const SPressureLane& lane = m_lanes[index];
        const std::uint64_t source = (index < k_connections) ? m_connections[index].notification.connection.value : index;
        if (m_starvation_sources[index] != source)
        {
            CPressureDiagnostic::log(m_starvation[index].observe(now, false, lane.dispatched), "module input starvation",
                address, m_context.endpoint.value, m_starvation_sources[index], 0u, 0u, 0u);
            m_starvation[index] = {};
            m_starvation_sources[index] = source;
        }
        CPressureDiagnostic::log(m_starvation[index].observe(now, lane.runnable, lane.dispatched, true),
            "module input starvation", address, m_context.endpoint.value, source, lane.occupied, lane.capacity, 0u);
    }
}

bool CModulePreamble::capacity_ready() noexcept
{
    if ((m_host_output_interest && m_channel.to_host.writable_count()) ||
        (m_owner_output_interest && m_resources.worker_to_host_owned_msgs.writable_count()) ||
        (m_control_output_interest && m_channel.lifecycle.to_host.writable_count()))
    {
        return true;
    }
    for (const SConnection& connection : m_connections)
    {
        if (connection.output_interest && connection.descriptor && connection.descriptor->outgoing &&
            connection.descriptor->outgoing->writable_count())
        {
            return true;
        }
    }
    return false;
}

bool CModulePreamble::work_pending() noexcept
{
    if (!m_created || m_destroyed || m_retained)
    {
        return false;
    }
    if (m_channel.lifecycle.to_preamble.readable_count() || (m_stopping && !m_stop_notified) ||
        m_progress.work_pending || capacity_ready() || m_participation_pending || m_seal_seen ||
        (m_admission_seen && !m_admission_notified && (m_host_dispatched >= m_admission_frontier)))
    {
        return true;
    }
    const bool control_writable = m_channel.lifecycle.to_host.writable_count() != 0u;
    if (control_writable && (m_ready_pending || m_module_stop_pending || (m_close_requested && !m_close_sent)))
    {
        return true;
    }
    for (const SConnection& connection : m_connections)
    {
        if (connection.offer_pending || connection.availability_pending || connection.closing_pending || connection.retirement_pending ||
            (control_writable && (connection.decision_pending || connection.installed_pending || connection.activated_pending ||
                connection.close_pending || connection.quiescence_pending)))
        {
            return true;
        }
    }
    for (const SRequest& request : m_requests)
    {
        if (request.live && (request.result || request.ready || request.ended || request.retired))
        {
            return true;
        }
    }
    refresh_lanes();
    for (const SPressureLane& lane : m_lanes)
    {
        if (lane.runnable) return true;
    }
    std::uint64_t remaining = UINT64_MAX;
    limit_wait(remaining);
    return remaining == 0u;
}

void CModulePreamble::limit_wait(std::uint64_t& remaining) noexcept
{
    std::uint64_t now = 0u;
    if (m_clock.read(now).code != platform::threading::EWaitError::none)
    {
        if (m_next_wake != UINT64_MAX)
        {
            fail(threading::EEndpointFailure::execution_failed);
            remaining = 0u;
        }
        return;
    }
    if (m_next_wake != UINT64_MAX)
    {
        const std::uint64_t delay = (now < m_next_wake) ? (m_next_wake - now) : 0u;
        if (delay < remaining) remaining = delay;
    }
    m_pressure.limit_wait(now, remaining);
    m_owner_pressure.limit_wait(now, remaining);
    m_control_pressure.limit_wait(now, remaining);
    m_self_pressure.limit_wait(now, remaining);
    for (const SConnection& connection : m_connections) connection.pressure.limit_wait(now, remaining);
    for (const CPressureDiagnostic& diagnostic : m_starvation) diagnostic.limit_wait(now, remaining);
}

}   //  namespace host
