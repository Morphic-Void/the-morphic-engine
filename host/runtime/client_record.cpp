
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    client_record.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Common Host-side transport access and service obligation accounting.

#include "runtime/client_record.hpp"

namespace host
{

SClientRecord::SClientRecord(const module_ids::id_type module, const std::uint64_t token,
    threading::CThreadPackage* const package, const modules::SModuleInstance instance) noexcept
    : instance{ instance }, identity{ token }, module{ module }, thread{ package }
    , pump_resources{ module_ids::executable, module, thread_ids::host }
    , endpoint{ package ? &package->resources() : &pump_resources }
    , notifications_pending{ endpoint->work_pending }
{}

bool SClientRecord::initialise() noexcept
{
    endpoint->instance = instance;
    endpoint->identity = identity;
    endpoint->owner_wake = owner_wake;
    endpoint->control_state.install_owner_wake(owner_wake);
    return thread || endpoint->initialise();
}

void SClientRecord::request_exit() noexcept
{
    stop_requested = true;
    endpoint->request_exit();
}

threading::EThreadRunState SClientRecord::query_state() const noexcept
{
    if (thread) return thread->query_state();
    return ((phase == EClientPhase::stopped) || (phase == EClientPhase::retired)) ?
        threading::EThreadRunState::Exited : threading::EThreadRunState::Running;
}

bool SClientRecord::terminal() const noexcept
{
    if (thread)
    {
        return thread->execution_ended() || (phase == EClientPhase::stopped) || (phase == EClientPhase::retired);
    }
    return (phase == EClientPhase::stopped) || (phase == EClientPhase::retired);
}

bool SClientRecord::reserve_replies(const std::uint32_t count) noexcept
{
    if (count > (UINT32_MAX - reserved_replies))
    {
        endpoint->fail(threading::EEndpointFailure::accounting_failure);
        return false;
    }
    reserved_replies += count;
    return true;
}

void SClientRecord::cancel_reservation(const std::uint32_t count) noexcept
{
    if (count > reserved_replies)
    {
        endpoint->fail(threading::EEndpointFailure::accounting_failure);
        return;
    }
    reserved_replies -= count;
}

bool SClientRecord::post(const threading::CErasedPodMsg& message) noexcept
{
    if (terminal()) return false;
    if (endpoint->host_to_worker_msgs.posting_poisoned())
    {
        endpoint->fail(threading::EEndpointFailure::transport_poisoned);
        return false;
    }
    const bool posted = endpoint->host_to_worker_msgs.post(message);
    if (endpoint->host_to_worker_msgs.posting_poisoned()) endpoint->fail(threading::EEndpointFailure::transport_poisoned);
    if (posted) endpoint->signal_consumer();
    return posted;
}

bool SClientRecord::reply(const threading::CErasedPodMsg& message) noexcept
{
    if (reserved_replies != 0u) cancel_reservation(1u);
    if (terminal() || post(message))
    {
        return true;
    }
    endpoint->fail(threading::EEndpointFailure::accounting_failure);
    return false;
}

bool SClientRecord::retry_reply(const threading::CErasedPodMsg& message) noexcept
{
    if (!terminal() && !post(message))
    {
        return false;
    }
    cancel_reservation(1u);
    return true;
}

EReplyPublication SClientRecord::publish_reply(const threading::CErasedPodMsg& message) noexcept
{
    if (terminal())
    {
        cancel_reservation(1u);
        return EReplyPublication::cancelled;
    }
    if (!post(message)) return EReplyPublication::busy;
    cancel_reservation(1u);
    return EReplyPublication::queued;
}

bool SClientRecord::read(threading::CErasedPodMsg& message) noexcept
{
    if (!endpoint->worker_to_host_msgs.read(message))
    {
        return false;
    }
    if (consumed_pods == UINT64_MAX) endpoint->fail(threading::EEndpointFailure::publication_overflow);
    else ++consumed_pods;
    endpoint->signal_consumer();
    return true;
}

bool SClientRecord::capture(threading::CErasedOwnerMsg& message) noexcept
{
    if (!endpoint->worker_to_host_owned_msgs.capture(message))
    {
        return false;
    }
    if (consumed_owners == UINT64_MAX) endpoint->fail(threading::EEndpointFailure::publication_overflow);
    else ++consumed_owners;
    endpoint->signal_consumer();
    return true;
}

void SClientRecord::accounted_pod() noexcept
{
    if (accounted_pods >= consumed_pods) endpoint->fail(threading::EEndpointFailure::accounting_failure);
    else ++accounted_pods;
}

void SClientRecord::accounted_owner() noexcept
{
    if (accounted_owners >= consumed_owners) endpoint->fail(threading::EEndpointFailure::accounting_failure);
    else ++accounted_owners;
}

bool SClientRecord::execution_quiescent() const noexcept
{
    if (!terminal_before_drain || !terminal() || !incoming_drained || deferred_records || invocation_active)
    {
        return false;
    }
    const threading::CBatchClient* const batch = batch_client();
    return !batch || ((batch->outstanding() == 0u) && (batch->return_publishers() == 0u));
}

bool SClientRecord::install_host_execution(const modules::SHostRestrictionReader reader) noexcept
{
    if (subscription_live || !reader.is_valid()) return false;
    host_execution.instance = instance;
    host_execution.client = identity;
    host_execution.subscription = { identity.value };
    host_execution.restriction = reader;
    if (module == module_ids::executive) host_execution.stop = { &host_stop_requested, owner_wake };
    if (thread)
    {
        if (!thread->install_host_execution(host_execution, restriction_notification)) return false;
    }
    else
    {
        if (!endpoint->pin_notification(restriction_notification))
        {
            return false;
        }
        host_execution.notification = restriction_notification.reader();
        if (!restriction_notification.endpoint().signal()) return false;
        endpoint->host_execution = &host_execution;
    }
    subscription_live = true;
    return true;
}

bool SClientRecord::signal_restriction(const std::uint32_t revision) noexcept
{
    if (!subscription_live || (restriction_revision == revision)) return true;
    if (!restriction_notification.endpoint().signal()) return false;
    restriction_revision = revision;
    return true;
}

bool SClientRecord::retire_host_execution(const bool deactivated) noexcept
{
    if (!subscription_live) return true;
    if (!execution_quiescent() || !replies_idle() || (!thread && !deactivated)) return false;
    if (thread && !thread->retire_host_execution()) return false;
    if (!thread) endpoint->unpin_notification();
    subscription_live = false;
    endpoint->host_execution = nullptr;
    host_execution = {};
    return true;
}

void SClientRecord::flush() noexcept
{
    if (!execution_quiescent()) return;
    threading::CErasedPodMsg message;
    while (endpoint->host_to_worker_msgs.read(message)) {}
}

}   //  namespace host
