
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    client_record.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Common Host-side transport access and service obligation accounting.

#include "runtime/execution/client_record.hpp"

namespace host
{

SClientRecord::SClientRecord(const module_ids::id_type module, const std::uint64_t token,
    threading::CThreadPackage* const package, const modules::SModuleInstance instance) noexcept
    : instance{ instance }, identity{ token }, module{ module }, thread{ package }
    , pump_resources{ module_ids::executable, module, thread_ids::host }
    , endpoint{ package ? &package->resources() : &pump_resources }
    , notifications_pending{ endpoint->work_pending }
{
    for (std::int32_t& cursor : output_cursors) cursor = -1;
}

bool SClientRecord::initialise_execution(modules::CBoundModule& binding, const modules::SExecutionV1& execution,
    void* const creation_context, const std::uint32_t ordinary_capacity, const std::uint32_t fallback_limit) noexcept
{
    if (channel || pending_replies || preamble || !execution.is_valid())
    {
        return false;
    }
    endpoint->instance = instance;
    endpoint->identity = identity;
    endpoint->owner_wake = owner_wake;
    endpoint->control_state.install_owner_wake(owner_wake);
    memory::CMemoryContext& memory = *memory::get_ambient_memory_context();
    if (!channel.emplace(*endpoint) || !channel->initialise(memory, ordinary_capacity) ||
        !pending_replies.emplace(memory, fallback_limit) || (!thread && !endpoint->worker_to_host_owned_msgs.initialise(0u)) ||
        !preamble.emplace(*endpoint, *channel, binding, execution, memory, creation_context) ||
        (thread && !thread->install_preamble(*preamble)))
    {
        //  No execution has started. Publish the common-path invariant only
        //  when every companion exists and the driver has accepted its preamble.
        preamble.reset();
        pending_replies.reset();
        channel.reset();
        if (!thread) endpoint->worker_to_host_owned_msgs.deallocate();
        return false;
    }
    return true;
}

void SClientRecord::request_exit() noexcept
{
    stop_requested = true;
    endpoint->request_exit();
}

void SClientRecord::retain_control(const SLifecycleMessage& message) noexcept
{
    if (terminal()) return;
    for (SControlOutcome& outcome : control_outcomes)
    {
        if (outcome.pending) continue;
        outcome.message = message;
        outcome.pending = true;
        return;
    }
    endpoint->fail(threading::EEndpointFailure::lifecycle_failure);
}

void SClientRecord::flush_controls() noexcept
{
    for (SControlOutcome& outcome : control_outcomes)
    {
        if (!outcome.pending) continue;
        if (!terminal() && !channel->host_control(outcome.message))
        {
            break;
        }
        outcome = {};
    }
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
    if (channel)
    {
        const EReplyWrite result = pending_replies->publish(channel->to_module, message);
        if (result == EReplyWrite::published)
        {
            endpoint->signal_consumer();
            return true;
        }
        if (result != EReplyWrite::busy) endpoint->fail(threading::EEndpointFailure::accounting_failure);
        return false;
    }
    return false; //  Setup has not produced an endpoint transport.
}

bool SClientRecord::reply(const threading::CErasedPodMsg& message) noexcept
{
    if (channel && !terminal())
    {
        const EReplyWrite result = pending_replies->retain(channel->to_module, message);
        if ((result != EReplyWrite::published) && (result != EReplyWrite::retained))
        {
            endpoint->fail(threading::EEndpointFailure::accounting_failure);
            return false;
        }
        cancel_reservation(1u);
        if (result == EReplyWrite::published) endpoint->signal_consumer();
        return true;
    }
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

bool SClientRecord::output_ready() const noexcept
{
    return terminal() || (channel && pending_replies->idle() && channel->to_module.writable_count());
}

void SClientRecord::flush_replies() noexcept
{
    if (!pending_replies) return;
    if (execution_quiescent())
    {
        pending_replies->cancel_after_quiescence();
        return;
    }
    if (terminal()) return; //  Retain until quiescence permits explicit cancellation.
    std::uint32_t published = 0u;
    const EReplyWrite result = pending_replies->flush(channel->to_module, 8u, published);
    if (published) endpoint->signal_consumer();
    if ((result != EReplyWrite::published) && (result != EReplyWrite::busy))
    {
        endpoint->fail(threading::EEndpointFailure::accounting_failure);
    }
}

bool SClientRecord::read(threading::CErasedPodMsg& message) noexcept
{
    if (!channel || !channel->read_host_pod(message))
    {
        return false;
    }
    if (consumed_pods == UINT64_MAX) endpoint->fail(threading::EEndpointFailure::publication_overflow);
    else ++consumed_pods;
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

bool SClientRecord::capture_pod_head() noexcept
{
    if (pod_head_live) return false;
    pod_head_live = read(pod_head);
    if (pod_head_live) incoming_drained = false;
    return pod_head_live;
}

bool SClientRecord::capture_owner_head() noexcept
{
    if (owner_head_live) return false;
    owner_head_live = capture(owner_head);
    if (owner_head_live) incoming_drained = false;
    return owner_head_live;
}

void SClientRecord::accounted_owner() noexcept
{
    if (accounted_owners >= consumed_owners) endpoint->fail(threading::EEndpointFailure::accounting_failure);
    else ++accounted_owners;
}

bool SClientRecord::execution_quiescent() const noexcept
{
    if (!terminal_before_drain || !terminal() || !incoming_drained || invocation_active ||
        pod_head_live || owner_head_live || (preamble && preamble->retained()))
    {
        return false;
    }
    const threading::CBatchClient* const batch = batch_client();
    return !batch || ((batch->outstanding() == 0u) && (batch->return_publishers() == 0u));
}

bool SClientRecord::process_retention_eligible() const noexcept
{
    //  Pumped only: the seal grant and returned destroy callback prove domain
    //  completion; the outer Host invocation must also have unwound. Other
    //  retained reasons (and serviced execution without a join) are unsafe.
    if (thread || !channel || !preamble || !preamble->destroy_refused() || !destroy_refusal_unwound || !seal_acknowledged ||
        invocation_active || !incoming_drained || pod_head_live || owner_head_live ||
        (accounted_pods != endpoint->published_pods) || (accounted_owners != endpoint->published_owners) ||
        (accounted_pods != seal_boundary.pods) || (accounted_owners != seal_boundary.owners))
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
    if (channel)
    {
        pending_replies->cancel_after_quiescence();
        while (channel->to_module.read(message)) {}
        return;
    }
}

}   //  namespace host
