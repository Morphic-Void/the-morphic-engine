
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_preamble_communication.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Authenticated DLL-to-Host submissions; Host routing itself uses direct calls.

#include "host/runtime/execution/module_preamble.hpp"
#include "framework/threading/CEndpointContext.hpp"

namespace host
{

const modules::SCommunicationV1 CModulePreamble::s_communication{
    &post_pod, &post_owner, &post_intent, &query, &producer_data, &publish_bulk, &acquire_bulk, &consumer_data
};

CModulePreamble* CModulePreamble::authenticated(void* const cookie) noexcept
{
    //  Do not dereference cookie until it equals this thread's active invocation.
    //  The function table has Host lifetime even when a copied facade is stale.
    return (t_invocation && (cookie == t_invocation)) ? t_invocation : nullptr;
}

modules::ESubmission MV_STD_ABI_CALL CModulePreamble::post_pod(void* const cookie,
    const modules::SDestination& destination, const threading::CErasedPodMsg& message) noexcept
{
    CModulePreamble* const self = authenticated(cookie);
    return self ? self->submit_pod(destination, message) : modules::ESubmission::invalid;
}

modules::ESubmission MV_STD_ABI_CALL CModulePreamble::post_owner(void* const cookie,
    const modules::SDestination& destination, threading::CErasedOwnerMsg&& message) noexcept
{
    CModulePreamble* const self = authenticated(cookie);
    return self ? self->submit_owner(destination, std::move(message)) : modules::ESubmission::invalid;
}

modules::ESubmission MV_STD_ABI_CALL CModulePreamble::post_intent(void* const cookie,
    const modules::SExecutionIntent& intent) noexcept
{
    CModulePreamble* const self = authenticated(cookie);
    return self ? self->submit_intent(intent) : modules::ESubmission::invalid;
}

modules::SConnectionQuery MV_STD_ABI_CALL CModulePreamble::query(void* const cookie,
    const modules::SDestination& destination) noexcept
{
    CModulePreamble* const self = authenticated(cookie);
    return self ? self->query_connection(destination) : modules::SConnectionQuery{};
}

CModulePreamble::SConnection* CModulePreamble::find_connection(const connections::SToken token) noexcept
{
    if (!token.value) return nullptr;
    for (SConnection& connection : m_connections)
    {
        if ((connection.phase != EConnectionPhase::empty) && (connection.notification.connection.value == token.value))
        {
            return &connection;
        }
    }
    return nullptr;
}

CModulePreamble::SConnection* CModulePreamble::route(const modules::SDestination& destination) noexcept
{
    if (destination.kind == modules::SDestination::EKind::connection)
    {
        return (destination.peer == system_ids::id_type{}) ? find_connection(destination.token) : nullptr;
    }
    if (destination.kind != modules::SDestination::EKind::peer)
    {
        return nullptr;
    }
    SConnection* pending = nullptr;
    for (SConnection& connection : m_connections)
    {
        if ((connection.phase != EConnectionPhase::empty) && (connection.notification.peer == destination.peer) &&
            (!destination.token.value || (connection.notification.connection.value == destination.token.value)))
        {
            if (destination.token.value || (connection.phase == EConnectionPhase::active))
            {
                return &connection;
            }
            if (!pending || (pending->phase == EConnectionPhase::closing)) pending = &connection;
        }
    }
    return pending;
}

modules::SConnectionQuery CModulePreamble::query_connection(const modules::SDestination& destination) noexcept
{
    SConnection* const connection = route(destination);
    if (!connection) return {};
    const bool active = (connection->phase == EConnectionPhase::active) && connection->descriptor &&
        connection->descriptor->admission->available() && !m_stopping;
    return { connection->notification.connection, connection->notification.peer,
        (active ? 1u : 0u), ((connection->phase == EConnectionPhase::closing) ? 1u : 0u) };
}

static bool valid_destination(const modules::SDestination& destination) noexcept
{
    switch (destination.kind)
    {
        case modules::SDestination::EKind::host:
        case modules::SDestination::EKind::self:
        {
            return (destination.peer == system_ids::id_type{}) && !destination.token.value;
        }
        case modules::SDestination::EKind::peer:
        {
            return system_ids::ops::is_valid_id(destination.peer);
        }
        case modules::SDestination::EKind::connection:
        {
            return (destination.peer == system_ids::id_type{}) && destination.token.value;
        }
    }
    return false;
}

modules::ESubmission CModulePreamble::submit_pod(const modules::SDestination& destination,
    const threading::CErasedPodMsg& message) noexcept
{
    using modules::ESubmission;
    system_type_id type;
    if (!valid_destination(destination) || !message.query_message_type_id().try_system_type_id(type))
    {
        return ESubmission::invalid;
    }
    if (destination.kind == modules::SDestination::EKind::self)
    {
        if (!erased_transport_admission::classify(message.query_message_type_id(), m_resources.worker_module_id).is_admitted())
        {
            return ESubmission::invalid;
        }
        if (m_stopping) return ESubmission::closed;
        if (!m_self.ensure_free(1u))
        {
            return ESubmission::busy;
        }
        if (!m_self.push(message))
        {
            fail(threading::EEndpointFailure::accounting_failure);
            return ESubmission::transport_failed;
        }
        return ESubmission::accepted;
    }
    if (destination.kind == modules::SDestination::EKind::host)
    {
        if (m_stopping && !threading::endpoint_settlement(message))
        {
            return ESubmission::closed;
        }
        if (m_resources.published_pods == UINT64_MAX)
        {
            fail(threading::EEndpointFailure::publication_overflow);
            return ESubmission::transport_failed;
        }
        const EModulePipePost result = m_channel.to_host.post(message);
        if (result == EModulePipePost::invalid)
        {
            return ESubmission::invalid;
        }
        if (result == EModulePipePost::not_ready)
        {
            return ESubmission::transport_failed;
        }
        if (result == EModulePipePost::full)
        {
            m_host_output_interest = true;
            return ESubmission::busy;
        }
        ++m_resources.published_pods;
        return m_channel.signal_host() ? ESubmission::accepted : ESubmission::published_wake_failed;
    }
    SConnection* const connection = route(destination);
    if (!connection || !connection->descriptor || (connection->phase != EConnectionPhase::active))
    {
        return ESubmission::no_pipe;
    }
    if (m_stopping) return ESubmission::closed;
    const SDirectEndpointDescriptor& descriptor = *connection->descriptor;
    const CDirectOperation operation{ descriptor.admission };
    if (!operation || !descriptor.outgoing)
    {
        return ESubmission::no_pipe;
    }
    const EModulePipePost result = descriptor.outgoing->post(message);
    if (result == EModulePipePost::invalid)
    {
        return ESubmission::invalid;
    }
    if (result == EModulePipePost::full)
    {
        connection->output_interest = true;
        return ESubmission::busy;
    }
    if (result == EModulePipePost::not_ready)
    {
        fail(threading::EEndpointFailure::direct_transport_poisoned);
        return ESubmission::transport_failed;
    }
    if (descriptor.peer_notification.signal())
    {
        return ESubmission::accepted;
    }
    fail(threading::EEndpointFailure::wake_failed);
    return ESubmission::published_wake_failed;
}

modules::ESubmission CModulePreamble::submit_owner(const modules::SDestination& destination,
    threading::CErasedOwnerMsg&& message) noexcept
{
    using modules::ESubmission;
    if (!valid_destination(destination) || (destination.kind != modules::SDestination::EKind::host))
    {
        return ESubmission::invalid;
    }
    if (m_stopping && !threading::endpoint_settlement(message))
    {
        return ESubmission::closed;
    }
    if (!erased_transport_admission::classify(message.query_message_type_id(), m_resources.owner_module_id).is_admitted() ||
        (message.has_owner() && !erased_transport_admission::classify(message.query_owner_type_id(), m_resources.owner_module_id).is_admitted()))
    {
        return ESubmission::invalid;
    }
    if (m_resources.published_owners == UINT64_MAX)
    {
        fail(threading::EEndpointFailure::publication_overflow);
        return ESubmission::transport_failed;
    }
    if (!m_resources.worker_to_host_owned_msgs.writable_count())
    {
        m_owner_output_interest = true;
        return ESubmission::busy;
    }
    if (!m_resources.worker_to_host_owned_msgs.post(std::move(message)))
    {
        return ESubmission::invalid;
    }
    ++m_resources.published_owners;
    return m_channel.signal_host() ? ESubmission::accepted : ESubmission::published_wake_failed;
}

memory::CMemoryView MV_STD_ABI_CALL CModulePreamble::producer_data(void* const cookie, const connections::SToken token) noexcept
{
    CModulePreamble* const self = authenticated(cookie);
    SConnection* const connection = self ? self->find_connection(token) : nullptr;
    if (!connection || self->m_stopping || (connection->phase != EConnectionPhase::active) || !connection->descriptor)
    {
        return {};
    }
    const SDirectEndpointDescriptor& descriptor = *connection->descriptor;
    const CDirectOperation operation{ descriptor.admission };
    return (operation && descriptor.bulk_producer) ? descriptor.bulk_producer->producer_data() : memory::CMemoryView{};
}

connections::EBulkPublish MV_STD_ABI_CALL CModulePreamble::publish_bulk(void* const cookie, const connections::SToken token) noexcept
{
    CModulePreamble* const self = authenticated(cookie);
    SConnection* const connection = self ? self->find_connection(token) : nullptr;
    if (!connection || self->m_stopping || (connection->phase != EConnectionPhase::active) || !connection->descriptor)
    {
        return connections::EBulkPublish::inactive;
    }
    const SDirectEndpointDescriptor& descriptor = *connection->descriptor;
    const CDirectOperation operation{ descriptor.admission };
    if (!operation || !descriptor.bulk_producer)
    {
        return connections::EBulkPublish::inactive;
    }
    (void)descriptor.bulk_producer->publish();
    if (descriptor.peer_notification.signal())
    {
        return connections::EBulkPublish::published;
    }
    self->fail(threading::EEndpointFailure::wake_failed);
    return connections::EBulkPublish::published_wake_failed;
}

bool MV_STD_ABI_CALL CModulePreamble::acquire_bulk(void* const cookie, const connections::SToken token) noexcept
{
    CModulePreamble* const self = authenticated(cookie);
    SConnection* const connection = self ? self->find_connection(token) : nullptr;
    if (!connection || self->m_stopping || (connection->phase != EConnectionPhase::active) || !connection->descriptor)
    {
        return false;
    }
    const SDirectEndpointDescriptor& descriptor = *connection->descriptor;
    const CDirectOperation operation{ descriptor.admission };
    return operation && descriptor.bulk_consumer && descriptor.bulk_consumer->discard_and_acquire();
}

memory::CMemoryConstView MV_STD_ABI_CALL CModulePreamble::consumer_data(void* const cookie, const connections::SToken token) noexcept
{
    CModulePreamble* const self = authenticated(cookie);
    SConnection* const connection = self ? self->find_connection(token) : nullptr;
    if (!connection || self->m_stopping || (connection->phase != EConnectionPhase::active) || !connection->descriptor)
    {
        return {};
    }
    const SDirectEndpointDescriptor& descriptor = *connection->descriptor;
    const CDirectOperation operation{ descriptor.admission };
    return (operation && descriptor.bulk_consumer) ? descriptor.bulk_consumer->consumer_data() : memory::CMemoryConstView{};
}

}   //  namespace host
