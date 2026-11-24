
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_preamble_lifecycle.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Retained private transitions and deliberate module-facing lifecycle events.

#include "host/runtime/execution/module_preamble.hpp"

namespace host
{

CModulePreamble::SRequest* CModulePreamble::find_request(const std::int32_t correlation) noexcept
{
    for (SRequest& request : m_requests)
    {
        if (request.live && (request.correlation == correlation))
        {
            return &request;
        }
    }
    return nullptr;
}

CModulePreamble::SRequest* CModulePreamble::vacant_request() noexcept
{
    for (SRequest& request : m_requests)
    {
        if (!request.live) return &request;
    }
    return nullptr;
}

modules::ESubmission CModulePreamble::submit_intent(const modules::SExecutionIntent& intent) noexcept
{
    using modules::ESubmission;
    using EKind = modules::SExecutionIntent::EKind;
    SLifecycleMessage message;
    message.correlation = intent.correlation;
    SRequest* request = nullptr;
    switch (intent.kind)
    {
        case EKind::fail:
        {
            fail(threading::EEndpointFailure::execution_failed);
            return ESubmission::accepted;
        }
        case EKind::stop_module:
        {
            m_module_stop_pending = true;
            return ESubmission::accepted;
        }
        case EKind::close_provider_admission:
        {
            if (!m_close_requested)
            {
                m_close_frontier = { m_resources.published_pods, m_resources.published_owners, 0u };
                m_close_requested = true;
            }
            return ESubmission::accepted;
        }
        case EKind::consent:
        {
            SConnection* const connection = find_connection(intent.connection);
            if (!connection || (connection->phase != EConnectionPhase::offered) || connection->decision_pending ||
                ((intent.decision != connections::EStatus::success) && (intent.decision != connections::EStatus::rejected) &&
                    (intent.decision != connections::EStatus::unsupported)))
            {
                return ESubmission::invalid;
            }
            connection->decision = m_stopping ? connections::EStatus::cancelled : intent.decision;
            connection->decision_pending = true;
            connection->phase = (connection->decision == connections::EStatus::success) ? EConnectionPhase::accepted : EConnectionPhase::closing;
            return ESubmission::accepted;
        }
        case EKind::close_connection:
        {
            SConnection* const connection = find_connection(intent.connection);
            if (!connection) return ESubmission::no_pipe;
            close_connection(*connection, connections::EStatus::cancelled);
            return ESubmission::accepted;
        }
        case EKind::connection_quiescent:
        {
            SConnection* const connection = find_connection(intent.connection);
            if (!connection || (connection->phase != EConnectionPhase::closing) || !connection->closing_seen || connection->closing_pending)
            {
                return ESubmission::invalid;
            }
            connection->module_quiescent = true;
            connection->quiescence_pending = !connection->quiescence_sent;
            return ESubmission::accepted;
        }
        case EKind::participate:
        {
            if (m_stopping) return ESubmission::closed;
            if (m_participation_requested || (intent.correlation < 0))
            {
                return ESubmission::invalid;
            }
            message.operation = ELifecycle::participate;
            break;
        }
        case EKind::connect:
        case EKind::create_child:
        case EKind::stop_child:
        {
            if ((intent.correlation < 0) || find_request(intent.correlation))
            {
                return ESubmission::invalid;
            }
            if (m_stopping && (intent.kind != EKind::stop_child))
            {
                return ESubmission::closed;
            }
            request = vacant_request();
            if (!request) return ESubmission::busy;
            if (intent.kind == EKind::connect)
            {
                message.operation = ELifecycle::connect;
                message.first_address = intent.first;
                message.second_address = intent.second;
                message.notification.bulk = intent.bulk;
                message.notification.connection_version = intent.connection_version;
            }
            else if (intent.kind == EKind::create_child)
            {
                if (!intent.child.descriptor || !intent.child.descriptor->is_valid() || !intent.child.descriptor->create ||
                    (intent.child.descriptor->flags != 0u))
                {
                    return ESubmission::invalid;
                }
                message.operation = ELifecycle::create_child;
                message.child = intent.child;
            }
            else
            {
                if (!intent.endpoint.value)
                {
                    return ESubmission::invalid;
                }
                message.operation = ELifecycle::stop_child;
                message.notification.first = intent.endpoint;
            }
            break;
        }
        default:
        {
            return ESubmission::invalid;
        }
    }
    if (!m_channel.preamble_control(message))
    {
        m_control_output_interest = true;
        return ESubmission::busy;
    }
    if (request)
    {
        request->live = true;
        request->kind = intent.kind;
        request->correlation = intent.correlation;
    }
    if (intent.kind == EKind::participate) m_participation_requested = true;
    return (m_resources.failure.load(std::memory_order_acquire) == threading::EEndpointFailure::wake_failed) ?
        ESubmission::published_wake_failed : ESubmission::accepted;
}

void CModulePreamble::close_connection(SConnection& connection, const connections::EStatus reason) noexcept
{
    if (connection.phase == EConnectionPhase::empty) return;
    if (connection.phase != EConnectionPhase::closing)
    {
        connection.phase = EConnectionPhase::closing;
        connection.notification.connection_status = reason;
        connection.close_pending = true;
    }
    connection.availability_pending = false;
    connection.output_interest = false;
}

bool CModulePreamble::connections_idle() const noexcept
{
    for (const SConnection& connection : m_connections)
    {
        if (connection.phase != EConnectionPhase::empty)
        {
            return false;
        }
    }
    return true;
}

bool CModulePreamble::drain_controls() noexcept
{
    if (m_resources.control_state.exit_requested()) request_stop(modules::EExecutionStop::requested);
    if ((m_resources.thread_id == thread_ids::host) && m_resources.module_state &&
        m_resources.module_state->stopping.load(std::memory_order_acquire))
    {
        request_stop(modules::EExecutionStop::requested);
    }
    while (m_control_count < k_control_budget)
    {
        SLifecycleMessage message;
        if (!m_channel.read_preamble_control(message))
        {
            return true;
        }
        ++m_control_count;
        consume_control(message);
    }
    return m_channel.lifecycle.to_preamble.readable_count() == 0u;
}

void CModulePreamble::consume_control(const SLifecycleMessage& message) noexcept
{
    SConnection* const connection = find_connection(message.notification.connection);
    switch (message.operation)
    {
        case ELifecycle::stop:
        {
            request_stop(message.notification.stop_reason);
            return;
        }
        case ELifecycle::participated:
        {
            if (!m_participation_requested || m_participation_pending)
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            m_participation = message.notification;
            m_participation.correlation = message.correlation;
            m_participating = message.notification.connection_status == connections::EStatus::success;
            m_participation_pending = m_event_pending = true;
            return;
        }
        case ELifecycle::provider_admission_closed:
        {
            if (!m_close_sent || m_admission_seen)
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            m_admission_seen = true;
            m_admission_frontier = message.frontier.host_output;
            m_event_pending = true;
            return;
        }
        case ELifecycle::sealed:
        {
            if (!m_seal_sent || m_seal_seen)
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            m_seal_seen = true;
            return;
        }
        case ELifecycle::offer:
        {
            if (connection || !message.notification.connection.value)
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            for (SConnection& vacant : m_connections)
            {
                if (vacant.phase != EConnectionPhase::empty)
                {
                    continue;
                }
                vacant.notification = message.notification;
                vacant.phase = EConnectionPhase::offered;
                if (m_stopping || !m_participating)
                {
                    vacant.phase = EConnectionPhase::closing;
                    vacant.decision = connections::EStatus::cancelled;
                    vacant.decision_pending = true;
                }
                else vacant.offer_pending = m_event_pending = true;
                return;
            }
            //  Global connection admission must never expose more than the
            //  endpoint's bounded retained transition storage can hold.
            fail(threading::EEndpointFailure::lifecycle_failure);
            return;
        }
        case ELifecycle::install:
        {
            if (!connection || (connection->phase != EConnectionPhase::accepted))
            {
                return; //  A local stop may have cancelled consent before installation arrived.
            }
            const SDirectEndpointDescriptor* const descriptor = message.descriptor;
            if (!descriptor || (descriptor->size != sizeof(*descriptor)) || (descriptor->version != 1u) ||
                (descriptor->token.value != connection->notification.connection.value) ||
                (descriptor->local.value != m_context.endpoint.value) || (descriptor->local_address != m_context.address) ||
                (descriptor->peer.value != connection->notification.first.value) ||
                (descriptor->peer_address != connection->notification.peer) || !descriptor->admission ||
                !descriptor->notification.is_valid() || !descriptor->peer_notification.is_valid() ||
                !descriptor->bulk.matches(connection->notification.bulk))
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            const bool valid = descriptor->bulk.is_bulk() ?
                (!descriptor->incoming && !descriptor->outgoing &&
                    ((descriptor->bulk.producer == m_resources.worker_module_id) ?
                        (descriptor->bulk_producer && !descriptor->bulk_consumer) : (!descriptor->bulk_producer && descriptor->bulk_consumer))) :
                (descriptor->incoming && descriptor->outgoing && !descriptor->bulk_producer && !descriptor->bulk_consumer);
            if (!valid)
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            connection->descriptor = descriptor;
            connection->phase = EConnectionPhase::installed;
            connection->installed_pending = true;
            return;
        }
        case ELifecycle::activate:
        {
            if (connection && (connection->phase == EConnectionPhase::installed) && !m_stopping)
            {
                connection->availability_pending = m_event_pending = true;
            }
            return;
        }
        case ELifecycle::closing:
        {
            if (!connection)
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            connection->phase = EConnectionPhase::closing;
            connection->notification.connection_status = message.notification.connection_status;
            connection->offer_pending = connection->availability_pending = false;
            connection->decision_pending = connection->installed_pending = connection->activated_pending = connection->close_pending = false;
            connection->closing_seen = true;
            connection->closing_pending = m_event_pending = true;
            connection->output_interest = false;
            return;
        }
        case ELifecycle::retired:
        {
            if (!connection || !connection->quiescence_sent)
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            connection->descriptor = nullptr;
            connection->retirement_pending = m_event_pending = true;
            return;
        }
        case ELifecycle::connection_result:
        case ELifecycle::child_created:
        case ELifecycle::child_ready:
        case ELifecycle::child_ended:
        case ELifecycle::child_retired:
        case ELifecycle::child_stop_result:
        {
            SRequest* const request = find_request(message.correlation);
            if (!request)
            {
                fail(threading::EEndpointFailure::lifecycle_failure);
                return;
            }
            if (message.operation == ELifecycle::child_created)
            {
                if ((request->kind != modules::SExecutionIntent::EKind::create_child) || request->child.value || request->result ||
                    ((message.notification.status == services::EServiceStatus::success) != (message.notification.first.value != 0u)))
                {
                    fail(threading::EEndpointFailure::lifecycle_failure);
                    return;
                }
                request->notification = message.notification;
                request->notification.correlation = message.correlation;
                request->child = message.notification.first;
                request->result = true;
            }
            else if (message.operation == ELifecycle::child_ready)
            {
                // Host publishes creation first on this same private SPSC lane.
                // An unrelated or premature child notice cannot establish readiness.
                if ((request->kind != modules::SExecutionIntent::EKind::create_child) || !request->child.value ||
                    (request->child.value != message.notification.first.value) ||
                    (message.notification.status != services::EServiceStatus::success))
                {
                    fail(threading::EEndpointFailure::lifecycle_failure);
                    return;
                }
                request->ready_status = message.notification.status;
                request->ready = true;
            }
            else if (message.operation == ELifecycle::child_ended)
            {
                if ((request->kind != modules::SExecutionIntent::EKind::create_child) || !request->child.value ||
                    (request->child.value != message.notification.first.value))
                {
                    fail(threading::EEndpointFailure::lifecycle_failure);
                    return;
                }
                request->ended_status = message.notification.status;
                request->ended = true;
            }
            else if (message.operation == ELifecycle::child_retired)
            {
                if ((request->kind != modules::SExecutionIntent::EKind::create_child) || !request->child.value ||
                    (request->child.value != message.notification.first.value))
                {
                    fail(threading::EEndpointFailure::lifecycle_failure);
                    return;
                }
                request->retired_status = message.notification.status;
                request->retired = true;
            }
            else
            {
                request->notification = message.notification;
                request->notification.correlation = message.correlation;
                request->result = true;
            }
            m_event_pending = true;
            return;
        }
        default:
        {
            fail(threading::EEndpointFailure::lifecycle_failure);
            return;
        }
    }
}

void CModulePreamble::progress_controls() noexcept
{
    SLifecycleMessage message;
    if (m_ready_pending)
    {
        message.operation = ELifecycle::ready;
        if (m_channel.preamble_control(message)) m_ready_pending = false;
    }
    if (m_module_stop_pending)
    {
        message.operation = ELifecycle::stop_module;
        if (m_channel.preamble_control(message)) m_module_stop_pending = false;
    }
    if (m_stopping && !m_close_requested)
    {
        m_close_frontier = { m_resources.published_pods, m_resources.published_owners, 0u };
        m_close_requested = true;
    }
    if (m_close_requested && !m_close_sent)
    {
        message.operation = ELifecycle::close_provider_admission;
        message.frontier = m_close_frontier;
        m_close_sent = m_channel.preamble_control(message);
    }
    for (SConnection& connection : m_connections)
    {
        if (connection.phase == EConnectionPhase::empty)
        {
            continue;
        }
        message = {};
        message.notification.connection = connection.notification.connection;
        if (connection.decision_pending)
        {
            message.operation = ELifecycle::decision;
            message.notification.connection_status = connection.decision;
            if (!m_channel.preamble_control(message))
            {
                continue;
            }
            connection.decision_pending = false;
        }
        if (connection.installed_pending)
        {
            message.operation = ELifecycle::installed;
            if (!m_channel.preamble_control(message))
            {
                continue;
            }
            connection.installed_pending = false;
        }
        if (connection.activated_pending)
        {
            message.operation = ELifecycle::activated;
            if (!m_channel.preamble_control(message))
            {
                continue;
            }
            connection.activated_pending = false;
        }
        if (connection.close_pending)
        {
            message.operation = ELifecycle::close;
            message.notification.connection_status = connection.notification.connection_status;
            if (!m_channel.preamble_control(message))
            {
                continue;
            }
            connection.close_pending = false;
        }
        if (connection.quiescence_pending)
        {
            //  Closure cancels unread direct PODs on their sole consumer. Wait
            //  for the closed gate's in-flight publishers before taking the
            //  final empty snapshot; the module has separately released its
            //  accepted conversations and held bulk views.
            if (connection.descriptor)
            {
                const SDirectEndpointDescriptor& descriptor = *connection.descriptor;
                if (!descriptor.admission->drained())
                {
                    continue;
                }
                if (descriptor.incoming)
                {
                    threading::CErasedPodMsg cancelled;
                    std::uint32_t count = 0u;
                    while ((count < k_ordinary_budget) && descriptor.incoming->read(cancelled)) ++count;
                    if (count && !descriptor.peer_notification.signal()) fail(threading::EEndpointFailure::wake_failed);
                    if (descriptor.incoming->readable_count())
                    {
                        continue;
                    }
                }
            }
            message.operation = ELifecycle::quiescent;
            if (m_channel.preamble_control(message))
            {
                connection.quiescence_pending = false;
                connection.quiescence_sent = true;
            }
        }
    }
}

bool CModulePreamble::dispatch_notification() noexcept
{
    m_event_pending = false;
    modules::SInvocation invocation;
    invocation.time_budget_ns = remaining_budget();
    if (m_participation_pending)
    {
        m_participation_pending = false;
        invocation.event = modules::EExecutionEvent::participation;
        invocation.notification = m_participation;
        (void)invoke(invocation);
        return m_event_pending = true;
    }
    if (m_admission_seen && !m_admission_notified && (m_host_dispatched >= m_admission_frontier))
    {
        m_admission_notified = true;
        invocation.event = modules::EExecutionEvent::provider_admission_closed;
        (void)invoke(invocation);
        return m_event_pending = true;
    }
    for (std::uint32_t offset = 0u; offset < k_connections; ++offset)
    {
        const std::uint32_t index = (m_notification_cursor + offset) & (k_connections - 1u);
        SConnection& connection = m_connections[index];
        if (connection.offer_pending)
        {
            connection.offer_pending = false;
            invocation.event = modules::EExecutionEvent::connection_offered;
        }
        else if (connection.availability_pending)
        {
            connection.availability_pending = false;
            invocation.event = modules::EExecutionEvent::connection_available;
            //  The callback may send immediately; reads cannot occur until it returns.
            connection.phase = EConnectionPhase::active;
        }
        else if (connection.closing_pending)
        {
            connection.closing_pending = false;
            invocation.event = modules::EExecutionEvent::connection_closing;
        }
        else if (connection.retirement_pending)
        {
            connection.retirement_pending = false;
            invocation.event = modules::EExecutionEvent::connection_retired;
        }
        else continue;
        invocation.notification = connection.notification;
        if (!m_execution.handle)
        {
            if (invocation.event == modules::EExecutionEvent::connection_offered)
            {
                connection.decision = connections::EStatus::rejected;
                connection.decision_pending = true;
                connection.phase = EConnectionPhase::closing;
            }
            if (invocation.event == modules::EExecutionEvent::connection_closing)
            {
                connection.module_quiescent = connection.quiescence_pending = true;
            }
        }
        else if (invoke(invocation) && (invocation.event == modules::EExecutionEvent::connection_available) &&
            (connection.phase == EConnectionPhase::active))
        {
            connection.activated_pending = true;
        }
        if (invocation.event == modules::EExecutionEvent::connection_retired)
        {
            diagnose_pressure(); //  Summarize this identity before its slot is reused.
            m_selection.withdraw(index);
            connection = {};
            m_lanes[index] = {};
        }
        m_notification_cursor = (index + 1u) & (k_connections - 1u);
        return m_event_pending = true;
    }
    for (SRequest& request : m_requests)
    {
        if (!request.live) continue;
        bool release = false;
        if (request.result)
        {
            request.result = false;
            if (request.kind == modules::SExecutionIntent::EKind::connect)
            {
                invocation.event = modules::EExecutionEvent::connection_result;
                release = true;
            }
            else if (request.kind == modules::SExecutionIntent::EKind::stop_child)
            {
                invocation.event = modules::EExecutionEvent::child_stop_result;
                release = true;
            }
            else
            {
                invocation.event = modules::EExecutionEvent::child_created;
                release = request.notification.status != services::EServiceStatus::success;
            }
        }
        else if (request.ready)
        {
            request.ready = false;
            invocation.event = modules::EExecutionEvent::child_ready;
        }
        else if (request.ended)
        {
            request.ended = false;
            invocation.event = modules::EExecutionEvent::child_ended;
        }
        else if (request.retired)
        {
            request.retired = false;
            invocation.event = modules::EExecutionEvent::child_retired;
            release = true;
        }
        else continue;
        invocation.notification = request.notification;
        if (invocation.event == modules::EExecutionEvent::child_ready) invocation.notification.status = request.ready_status;
        if (invocation.event == modules::EExecutionEvent::child_ended) invocation.notification.status = request.ended_status;
        if (invocation.event == modules::EExecutionEvent::child_retired) invocation.notification.status = request.retired_status;
        if (!m_execution.handle && (request.kind == modules::SExecutionIntent::EKind::create_child))
        {
            if (invocation.event == modules::EExecutionEvent::child_created)
            {
                m_standard_child = request.child;
                if (release)
                {
                    m_standard_retired = true;
                    if (!m_stopping) fail(threading::EEndpointFailure::execution_failed);
                }
            }
            if ((invocation.event == modules::EExecutionEvent::child_ready) && !request.ended && !m_standard_ended && !m_stopping)
            {
                m_standard_ready = true;
            }
            if (invocation.event == modules::EExecutionEvent::child_ended)
            {
                m_standard_ended = true;
                m_module_stop_pending = true;
                request_stop(modules::EExecutionStop::requested);
            }
            if (invocation.event == modules::EExecutionEvent::child_retired) m_standard_retired = true;
        }
        if (release) request = {};
        (void)invoke(invocation);
        return m_event_pending = true;
    }
    return false;
}

void CModulePreamble::progress_coordinator() noexcept
{
    if (!m_standard_requested && !m_stopping && m_execution.initial_child)
    {
        modules::SExecutionIntent intent;
        intent.kind = modules::SExecutionIntent::EKind::create_child;
        intent.correlation = 0;
        intent.child = *m_execution.initial_child;
        const modules::ESubmission status = submit_intent(intent);
        m_standard_requested = modules::accepted(status);
        if (!m_standard_requested && (status != modules::ESubmission::busy))
        {
            fail(threading::EEndpointFailure::execution_failed);
        }
    }
    if (!m_stopping && (!m_execution.initial_child || m_standard_ready) && !m_ready)
    {
        m_ready = m_ready_pending = true;
    }
    if (m_stopping && m_standard_child.value && !m_standard_ended && !m_standard_retired && !m_standard_stop_sent)
    {
        modules::SExecutionIntent intent;
        intent.kind = modules::SExecutionIntent::EKind::stop_child;
        intent.correlation = 1;
        intent.endpoint = m_standard_child;
        const modules::ESubmission status = submit_intent(intent);
        m_standard_stop_sent = modules::accepted(status);
        if (!m_standard_stop_sent && (status != modules::ESubmission::busy))
        {
            fail(threading::EEndpointFailure::execution_failed);
        }
    }
    m_progress.work_complete = (m_stopping && (!m_standard_requested || m_standard_retired)) ? 1u : 0u;
    m_progress.work_pending = (m_channel.lifecycle.to_host.writable_count() &&
        ((!m_stopping && m_execution.initial_child && !m_standard_requested) ||
            (m_stopping && m_standard_child.value && !m_standard_ended && !m_standard_retired && !m_standard_stop_sent))) ? 1u : 0u;
}

void CModulePreamble::progress_completion() noexcept
{
    if (!m_stopping || !m_stop_notified || !m_progress.work_complete || !m_admission_notified ||
        m_self.size() || !connections_idle())
    {
        return;
    }
    if (m_resources.batch_client && m_resources.batch_client->outstanding())
    {
        return;
    }
    for (const SRequest& request : m_requests)
    {
        if (request.live) return; //  Child contexts and accepted control results still have users.
    }
    if (!m_resources.sealing)
    {
        m_resources.sealing = true;
        if (m_resources.batch_client) m_resources.batch_client->close_admission();
        m_seal_frontier = { m_resources.published_pods, m_resources.published_owners, 0u };
    }
    if (!m_seal_sent)
    {
        SLifecycleMessage message;
        message.operation = ELifecycle::seal;
        message.frontier = m_seal_frontier;
        m_seal_sent = m_channel.preamble_control(message);
    }
    if (!m_seal_seen) return;
    //  Host grants this only after module-state users have settled. It cannot
    //  require this thread's join: destroy runs here, before native return.
    if (m_execution.destroy)
    {
        if (!enter_module_invocation()) return;
        m_destroying = true;
        const modules::EExecutionStatus status = m_execution.destroy(m_runtime);
        m_destroying = false;
        leave_module_invocation();
        if (status != modules::EExecutionStatus::success)
        {
            fail(threading::EEndpointFailure::execution_failed);
            m_retained = true;
            m_destroy_refused = true;
            return;
        }
    }
    m_runtime = nullptr;
    m_resources.sealed = true;
    m_destroyed = true;
}

}   //  namespace host
