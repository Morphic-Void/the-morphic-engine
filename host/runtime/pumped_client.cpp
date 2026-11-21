
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    pumped_client.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Lifecycle calls never run on workers and never recursively dispatch Host work.

#include "framework/system/system_ids.hpp"
#include "framework/services/service_protocol.hpp"
#include "framework/module/module_messages.hpp"
#include "runtime/pumped_client.hpp"
#include "system/system_id_registry.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/platform/report_messages.hpp"
#include "framework/assets/schema_messages.hpp"

namespace host
{

static thread_local CPumpedClient* t_invocation{ nullptr };

CPumpedClient::CPumpedClient(SClientRecord& client, modules::CBoundModule& binding,
    const modules::SHostPumpV1& capability, CServiceRouter& router, CHostMaintenance& maintenance) noexcept
    : m_client{ client }, m_binding{ binding }, m_capability{ capability }, m_router{ router }, m_maintenance{ maintenance }, m_endpoint{ *client.endpoint }
{
    m_context.instance = client.instance;
    m_context.client = client.identity;
    m_context.host_thread = thread_ids::host;
    m_context.endpoint = &m_endpoint;
    client.host_execution.native_context = this;
    client.host_execution.enter_restriction = &enter_restriction;
    client.host_execution.leave_restriction = &leave_restriction;
    client.host_execution.maintain_host = &maintain_host;
    m_context.execution = &client.host_execution;
    m_context.owner_wake = client.owner_wake;
}

bool CPumpedClient::initialise() noexcept
{
    return m_client.initialise() && m_client.install_host_execution(m_maintenance.restrictions().reader());
}

bool CPumpedClient::enter(const bool allow_restriction) noexcept
{
    if ((t_invocation != nullptr) || !m_binding.prepare_host_call(thread_ids::host, &m_context) ||
        !m_maintenance.restrictions().begin_invocation(m_client.instance, allow_restriction))
    {
        m_failed = true;
        m_retained = m_activated;
        return false;
    }
    t_invocation = this;
    m_call_failed = false;
    m_client.invocation_active = true;
    return true;
}

void CPumpedClient::leave() noexcept
{
    t_invocation = nullptr;
    if (!m_maintenance.restrictions().end_invocation())
    {
        m_call_failed = true;
        m_failed = true;
        m_client.stop_requested = true;
        m_stop_reason = modules::EStopReason::pump_failure;
    }
}

void CPumpedClient::finish_frame() noexcept
{
    m_client.invocation_active = false;
    m_maintenance.replay();
    m_maintenance.fanout();
}

void CPumpedClient::observe(const modules::ECallStatus status, const modules::SProgress& progress) noexcept
{
    using modules::EPumpPhase;
    if (m_call_failed || (status != modules::ECallStatus::success) || (progress.phase == EPumpPhase::failed) ||
        (progress.phase > EPumpPhase::failed) || (progress.work_pending > 1u) ||
        (progress.native_events_active > 1u) || (progress.stop_requested > 1u) ||
        ((progress.native_events_active || progress.stop_requested) &&
        !(m_capability.flags & modules::k_host_pump_native_events)) ||
        (m_stop_begun && ((progress.phase == EPumpPhase::starting) || (progress.phase == EPumpPhase::ready))))
    {
        m_failed = true;
        m_client.stop_requested = true;
        if (!m_stop_begun)
        {
            m_stop_reason = modules::EStopReason::pump_failure;
        }
        m_client.phase = EClientPhase::stopping;
        return;
    }
    m_work_pending = progress.work_pending != 0u;
    m_native_events = progress.native_events_active != 0u;
    m_native_stop = m_native_stop || (progress.stop_requested != 0u);
    if (progress.phase == EPumpPhase::stopped)
    {
        if (!m_stop_begun || !m_client.endpoint->sealed) m_failed = true;
        m_client.phase = EClientPhase::stopped;
    }
    else if (progress.phase == EPumpPhase::ready)
    {
        m_client.phase = EClientPhase::ready;
    }
    else if (progress.phase == EPumpPhase::stopping)
    {
        m_client.phase = EClientPhase::stopping;
        m_client.stop_requested = true;
    }
}

void CPumpedClient::activate() noexcept
{
    const auto budget = m_activation_budget.start(m_client.host_execution.restriction, 5000000000u);
    if ((budget == modules::EBudgetStatus::failed) || (budget == modules::EBudgetStatus::retry))
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
    finish_frame();
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
}

void CPumpedClient::pump() noexcept
{
    if (!m_activated || stopped() || m_retained || m_deactivated)
    {
        return;
    }
    if (m_client.phase == EClientPhase::starting)
    {
        const auto budget = m_activation_budget.observe();
        if ((budget.status == modules::EBudgetStatus::failed) || (budget.status == modules::EBudgetStatus::expired))
        {
            m_failed = true;
            request_stop(modules::EStopReason::activation_failure);
        }
    }
    if (m_client.stop_requested && !m_stop_begun)
    {
        if (!enter()) return;
        m_stop_begun = true;
        modules::SProgress progress;
        const auto status = m_capability.begin_stop(m_runtime, m_stop_reason, &progress);
        leave();
        observe(status, progress);
        finish_frame();
        if (stopped()) return;
    }
    modules::SPumpInput input;
    if (!enter()) return;
    modules::SPumpResult result;
    input.notifications_pending = m_client.notifications_pending.exchange(0u, std::memory_order_acq_rel);
    const modules::ECallStatus status = m_capability.pump(m_runtime, &input, &result);
    leave();
    observe(status, result.progress);
    if (m_client.endpoint->host_to_worker_msgs.refresh_readable_count()) m_work_pending = true;
    finish_frame();
}

void CPumpedClient::limit_wait(std::uint64_t& remaining) noexcept
{
    if (m_client.phase != EClientPhase::starting) return;
    const modules::SEligibleBudgetObservation budget = m_activation_budget.observe();
    if (budget.status == modules::EBudgetStatus::running)
    {
        if (budget.remaining_ns < remaining) remaining = budget.remaining_ns;
    }
    else if (budget.status != modules::EBudgetStatus::restricted)
    {
        remaining = 0u;
    }
}

bool CPumpedClient::deactivate() noexcept
{
    if (m_deactivated) return true;
    if (!stopped() || m_retained || !m_router.provider_borrows_retired(m_client) || !enter(false))
    {
        return false;
    }
    const auto status = m_capability.deactivate(m_runtime);
    leave();
    finish_frame();
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

modules::SRestrictionEnterResult MV_STD_ABI_CALL CPumpedClient::enter_restriction(void* const context, const modules::SRestrictionReason reason) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if (!self || (t_invocation != self) || self->m_maintenance.running())
    {
        return {};
    }
    self->m_maintenance.poll_stop();
    //  Accounting remains available to authenticated stopping invocations for
    //  native cleanup. Scope entry does not reopen ordinary work admission.
    return self->m_maintenance.restrictions().enter(reason);
}

modules::ERestrictionStatus MV_STD_ABI_CALL CPumpedClient::leave_restriction(void* const context, const modules::SRestrictionScope scope) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if (!self || (t_invocation != self) || self->m_maintenance.running())
    {
        return modules::ERestrictionStatus::invalid_context;
    }
    return self->m_maintenance.restrictions().leave(scope);
}

modules::SHostMaintenanceResult MV_STD_ABI_CALL CPumpedClient::maintain_host(void* const context) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if (!self || (t_invocation != self))
    {
        return {};
    }
    return self->m_maintenance.run();
}

}   //  namespace host
