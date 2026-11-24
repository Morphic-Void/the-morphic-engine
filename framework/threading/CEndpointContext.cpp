
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CEndpointContext.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Queue publication accounting and typed settlement admission.

#include "framework/threading/CEndpointContext.hpp"
#include "framework/services/provider_messages.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/assets/schema_messages.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/platform/report_messages.hpp"
#include "foundation/system/erased_transport_admission.hpp"

namespace threading
{

bool endpoint_settlement(const CErasedPodMsg& message) noexcept
{
    return message.is_payload_a<services::CompleteServiceRequest>() ||
        message.is_payload_a<services::CommitServiceResource>() ||
        message.is_payload_a<services::BeginServiceBorrowRetirement>() ||
        message.is_payload_a<services::FinishServiceBorrowRetirement>() ||
        message.is_payload_a<services::ReleaseServiceBorrow>() ||
        message.is_payload_a<services::ServiceBorrowQuiescent>() ||
        message.is_payload_a<services::CancelServiceRequest>() ||
        message.is_payload_a<services::CloseServiceRequest>() ||
        message.is_payload_a<AssetReleaseRequest>() || message.is_payload_a<AssetDisposeRequest>() ||
        message.is_payload_a<SchemaProcessCancel>() ||
        message.is_payload_a<platform::reports::PlatformReportCaptureFailed>();
}

bool endpoint_settlement(const CErasedOwnerMsg& message) noexcept
{
    const modules::ModuleRequest* const request = message.owner().payload<modules::ModuleRequest>();
    return message.is_message_a<platform::reports::PlatformReportCaptured>() ||
        (message.is_message_a<modules::ModuleRequest>() && request && (request->action == modules::EModuleAction::unload));
}

SEndpointPost CEndpointContext::publish(const CErasedPodMsg& message) noexcept
{
    //  A direct queue failure is fatal to the endpoint, but does not poison this
    //  independent Host lane or prevent safe cleanup and seal publication.
    const EEndpointFailure failure = m_endpoint.failure.load(std::memory_order_acquire);
    if ((failure != EEndpointFailure::none) && (failure != EEndpointFailure::wake_failed) &&
        (failure != EEndpointFailure::direct_transport_poisoned))
    {
        return { EEndpointPost::transport_failed };
    }
    if (m_endpoint.worker_to_host_msgs.posting_poisoned())
    {
        fail(EEndpointFailure::transport_poisoned);
        return { EEndpointPost::transport_failed };
    }
    if (m_endpoint.published_pods == UINT64_MAX)
    {
        fail(EEndpointFailure::publication_overflow);
        return { EEndpointPost::transport_failed };
    }
    if (!erased_transport_admission::classify(message.query_message_type_id(), m_endpoint.owner_module_id).is_admitted())
    {
        return { EEndpointPost::invalid };
    }
    if (!m_endpoint.worker_to_host_msgs.post(message))
    {
        if (m_endpoint.worker_to_host_msgs.posting_poisoned())
        {
            fail(EEndpointFailure::transport_poisoned);
            return { EEndpointPost::transport_failed };
        }
        return { EEndpointPost::busy };
    }
    ++m_endpoint.published_pods;
    (void)m_endpoint.owner_wake.signal();
    return { EEndpointPost::queued };
}

SEndpointPost CEndpointContext::post(const CErasedPodMsg& message) noexcept
{
    return publish(message);
}

SEndpointPost CEndpointContext::post(CErasedOwnerMsg&& message) noexcept
{
    const EEndpointFailure failure = m_endpoint.failure.load(std::memory_order_acquire);
    if ((failure != EEndpointFailure::none) && (failure != EEndpointFailure::wake_failed) &&
        (failure != EEndpointFailure::direct_transport_poisoned))
    {
        return { EEndpointPost::transport_failed };
    }
    if (m_endpoint.published_owners == UINT64_MAX)
    {
        fail(EEndpointFailure::publication_overflow);
        return { EEndpointPost::transport_failed };
    }
    if (!erased_transport_admission::classify(message.query_message_type_id(), m_endpoint.owner_module_id).is_admitted() ||
        (message.has_owner() && !erased_transport_admission::classify(message.query_owner_type_id(), m_endpoint.owner_module_id).is_admitted()))
    {
        return { EEndpointPost::invalid };
    }
    if (!m_endpoint.worker_to_host_owned_msgs.posting_is_valid())
    {
        return { EEndpointPost::invalid };
    }
    if (!m_endpoint.worker_to_host_owned_msgs.writable_count())
    {
        return { EEndpointPost::busy };
    }
    if (!m_endpoint.worker_to_host_owned_msgs.post(std::move(message)))
    {
        return { EEndpointPost::invalid };
    }
    ++m_endpoint.published_owners;
    (void)m_endpoint.owner_wake.signal();
    return { EEndpointPost::queued };
}

}   //  namespace threading
