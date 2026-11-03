
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    pumped_client.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Lifecycle calls never run on workers and never recursively dispatch Host work.

#include "services/service_protocol.hpp"
#include "module/module_messages.hpp"
#include "runtime/pumped_client.hpp"
#include "system/system_id_registry.hpp"
#include "assets/asset_messages.hpp"

namespace host
{

static thread_local CPumpedClient* t_invocation{ nullptr };

static bool system_message(const type_id identity) noexcept
{
    system_type_id system;
    return identity.try_system_type_id(system) && (system_id_registry::find_type(system) != nullptr);
}

CPumpedClient::CPumpedClient(SClientRecord& client, modules::CBoundModule& binding,
    const modules::SHostPumpV1& capability, CServiceRouter& router) noexcept
    : m_client{ client }, m_binding{ binding }, m_capability{ capability }, m_router{ router }
{
    static const modules::SHostPumpServices services{
        sizeof(modules::SHostPumpServices), 1u, &post_pod, &post_owner, &complete,
        &prepare_borrow, &begin_borrow_retirement, &finish_borrow_retirement };
    m_context.instance = client.instance;
    m_context.client = client.identity;
    m_context.host_thread = thread_ids::host;
    m_context.services = &services;
    m_context.service_context = this;
}

bool CPumpedClient::initialise() noexcept
{
    return m_pods.initialise(256u) && m_controls.initialise(32u) && m_owners.initialise(64u);
}

const threading::CErasedPodMsg* CPumpedClient::peek_pod(const bool control) noexcept
{
    auto& pending = control ? m_pending_control : m_pending_pod;
    auto& ready = control ? m_has_pending_control : m_has_pending_pod;
    if (!ready) ready = (control ? m_controls : m_pods).read(pending);
    return ready ? &pending : nullptr;
}

void CPumpedClient::consume_pod(const bool control) noexcept
{
    (control ? m_has_pending_control : m_has_pending_pod) = false;
}

bool CPumpedClient::enter() noexcept
{
    if ((t_invocation != nullptr) || !m_binding.prepare_host_call(thread_ids::host, &m_context))
    {
        m_failed = true;
        m_retained = m_activated;
        return false;
    }
    t_invocation = this;
    return true;
}

void CPumpedClient::leave() noexcept { t_invocation = nullptr; }

void CPumpedClient::observe(const modules::ECallStatus status, const modules::SProgress& progress) noexcept
{
    using modules::EPumpPhase;
    if ((status != modules::ECallStatus::success) || (progress.phase == EPumpPhase::failed) ||
        (progress.phase > EPumpPhase::failed) || (progress.work_pending > 1u) ||
        (m_stop_begun && ((progress.phase == EPumpPhase::starting) || (progress.phase == EPumpPhase::ready))))
    {
        m_failed = true;
        m_client.stop_requested = true;
        if (!m_stop_begun) m_stop_reason = modules::EStopReason::pump_failure;
        m_client.phase = EClientPhase::stopping;
        return;
    }
    if (progress.phase == EPumpPhase::stopped)
    {
        if (!m_stop_begun) m_failed = true;
        if (!m_router.provider_borrows_retired(m_client))
        {
            //  A stopped claim cannot release escaped views. Keep the complete
            //  graph; Host still needs proof that live consumers have quiesced.
            m_failed = true;
            m_retained = true;
        }
        m_client.phase = EClientPhase::stopped;
        m_router.stop_provider(m_client, true);
    }
    else if (progress.phase == EPumpPhase::ready) m_client.phase = EClientPhase::ready;
    else if (progress.phase == EPumpPhase::stopping)
    {
        m_client.phase = EClientPhase::stopping;
        m_client.stop_requested = true;
    }
}

void CPumpedClient::activate() noexcept
{
    if (m_activation_deadline.set_after_milliseconds(5000u).code != platform::threading::EWaitError::none)
    {
        m_failed = true;
        m_client.phase = EClientPhase::stopped;
        m_deactivated = true;
        return;
    }
    if (m_activated || !enter())
    {
        m_failed = true;
        m_client.phase = EClientPhase::stopped;
        m_deactivated = !m_activated;
        return;
    }
    m_activated = true;
    modules::SProgress progress;
    const auto status = m_capability.activate(&m_context, &m_runtime, &progress);
    leave();
    observe(status, progress);
    if (m_failed)
    {
        m_stop_reason = modules::EStopReason::activation_failure;
        if (m_runtime == nullptr)
        {
            m_client.phase = EClientPhase::stopped;
            m_deactivated = true;
        }
    }
}

void CPumpedClient::request_stop(const modules::EStopReason reason) noexcept
{
    if (!m_client.stop_requested) m_stop_reason = reason;
    m_client.stop_requested = true;
    if (!stopped()) m_client.phase = EClientPhase::stopping;
    m_router.stop_provider(m_client, stopped());
}

void CPumpedClient::pump() noexcept
{
    if (!m_activated || stopped() || m_retained || m_deactivated) return;
    if (m_client.phase == EClientPhase::starting)
    {
        std::uint64_t remaining = 0u;
        if ((m_activation_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) ||
            (remaining == 0u))
        {
            m_failed = true;
            request_stop(modules::EStopReason::activation_failure);
        }
    }
    if (m_client.stop_requested && !m_stop_begun)
    {
        m_router.stop_provider(m_client, false);
        if (!enter()) return;
        m_stop_begun = true;
        modules::SProgress progress;
        const auto status = m_capability.begin_stop(m_runtime, m_stop_reason, &progress);
        leave();
        observe(status, progress);
        if (stopped()) return;
    }
    services::SDispatchedCommand commands[64];
    threading::CErasedPodMsg messages[64];
    std::uint32_t controls = 0u;
    modules::SPumpInput input;
    input.command_count = (!m_client.stop_requested && ready()) ? m_router.prepare_batch(m_client, commands, 64u) : 0u;
    input.commands = commands;
    input.messages = messages;
    input.message_count = m_protocol_failed ? 0u : m_client.copy_messages(messages, 64u, controls);
    if (!enter()) return;
    modules::SPumpResult result;
    const auto status = m_capability.pump(m_runtime, &input, &result);
    leave();
    const bool valid = (result.consumed_messages <= input.message_count) &&
        m_router.finish_batch(m_client, commands, input.command_count, result.consumed_commands);
    if (valid) m_client.consume_messages(result.consumed_messages, controls);
    else
    {
        //  An invalid prefix cannot authorise dropping or replaying messages.
        //  Keep their storage until the provider establishes stopped/quiescent.
        m_protocol_failed = true;
        m_failed = true;
        request_stop(modules::EStopReason::pump_failure);
    }
    observe(valid ? status : modules::ECallStatus::failed, result.progress);
}

bool CPumpedClient::deactivate() noexcept
{
    if (m_deactivated) return true;
    if (!stopped() || m_retained || !m_router.provider_borrows_retired(m_client) || !enter()) return false;
    const auto status = m_capability.deactivate(m_runtime);
    leave();
    if (status != modules::ECallStatus::success)
    {
        m_failed = true;
        m_retained = true;
        return false;
    }
    m_runtime = nullptr;
    m_deactivated = true;
    return true;
}

services::EServiceStatus MV_STD_ABI_CALL CPumpedClient::post_pod(void* context, const threading::CErasedPodMsg* message) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if ((self == nullptr) || (t_invocation != self) || (message == nullptr) ||
        !system_message(message->query_message_type_id())) return services::EServiceStatus::invalid_request;
    if (self->m_client.terminal()) return services::EServiceStatus::closing;
    auto& queue = (message->is_payload_a<AssetReleaseRequest>() || message->is_payload_a<modules::RunTestsResult>() ||
        message->is_payload_a<services::ReleaseServiceBorrow>() || message->is_payload_a<services::ServiceBorrowQuiescent>() ||
        message->is_payload_a<services::CancelServiceRequest>()) ?
        self->m_controls : self->m_pods;
    return queue.post(*message) ? services::EServiceStatus::success : services::EServiceStatus::busy;
}

services::EServiceStatus MV_STD_ABI_CALL CPumpedClient::post_owner(void* context, threading::CErasedOwnerMsg* message) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if ((self == nullptr) || (t_invocation != self) || (message == nullptr) ||
        !system_message(message->query_message_type_id()) ||
        (message->has_owner() && !system_message(message->query_owner_type_id()))) return services::EServiceStatus::invalid_request;
    if (self->m_client.terminal()) return services::EServiceStatus::closing;
    return self->m_owners.post(std::move(*message)) ? services::EServiceStatus::success : services::EServiceStatus::busy;
}

services::EServiceStatus MV_STD_ABI_CALL CPumpedClient::complete(void* context, const services::ServiceReply* reply) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if ((self == nullptr) || (t_invocation != self) || (reply == nullptr)) return services::EServiceStatus::invalid_request;
    return self->m_router.complete(self->m_client, *reply);
}

services::EServiceStatus MV_STD_ABI_CALL CPumpedClient::prepare_borrow(void* context,
    const services::SServiceBorrowOffer* offer, services::SServiceBorrow* borrow) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if (borrow != nullptr) *borrow = {};
    if ((self == nullptr) || (t_invocation != self) || (offer == nullptr) || (borrow == nullptr))
        return services::EServiceStatus::invalid_request;
    return self->m_router.prepare_borrow(self->m_client, *offer, *borrow);
}

services::EServiceStatus MV_STD_ABI_CALL CPumpedClient::begin_borrow_retirement(void* context, const services::SServiceBorrow borrow) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if ((self == nullptr) || (t_invocation != self)) return services::EServiceStatus::invalid_request;
    return self->m_router.begin_borrow_retirement(self->m_client, borrow);
}

services::EServiceStatus MV_STD_ABI_CALL CPumpedClient::finish_borrow_retirement(void* context, const services::SServiceBorrow borrow) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if ((self == nullptr) || (t_invocation != self)) return services::EServiceStatus::invalid_request;
    return self->m_router.finish_borrow_retirement(self->m_client, borrow);
}

}   //  namespace host
