
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
#include "runtime/execution/pumped_client.hpp"
#include "system/system_id_registry.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/platform/report_messages.hpp"
#include "framework/assets/schema_messages.hpp"

namespace host
{

static thread_local CPumpedClient* t_invocation{ nullptr };

CPumpedClient::CPumpedClient(SClientRecord& client, modules::CBoundModule& binding,
    const modules::SExecutionV1& execution, CHostMaintenance& maintenance) noexcept
    : m_client{ client }, m_binding{ binding }, m_execution{ execution }, m_maintenance{ maintenance }
{
    client.host_execution.native_context = this;
    client.host_execution.enter_restriction = &enter_restriction;
    client.host_execution.leave_restriction = &leave_restriction;
    client.host_execution.maintain_host = &maintain_host;
}

bool CPumpedClient::initialise() noexcept
{
    return m_client.initialise_execution(m_binding, m_execution) &&
        m_client.install_host_execution(m_maintenance.restrictions().reader());
}

bool CPumpedClient::enter() noexcept
{
    if ((t_invocation != nullptr) || !m_maintenance.restrictions().begin_invocation(m_client.instance, true))
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
        m_stop_reason = modules::EExecutionStop::execution_failure;
    }
}

void CPumpedClient::finish_frame() noexcept
{
    m_client.invocation_active = false;
    m_maintenance.replay();
    m_maintenance.fanout();
    if (!m_call_failed && m_client.preamble->destroy_refused())
    {
        m_client.destroy_refusal_unwound = true;
    }
}

void CPumpedClient::observe() noexcept
{
    CModulePreamble& preamble = *m_client.preamble;
    m_failed = m_failed || m_call_failed || preamble.failed();
    m_retained = m_retained || preamble.retained();
    m_work_pending = preamble.work_pending();
    m_native_events = preamble.native_events_active();
    m_native_stop = m_native_stop || preamble.application_stop_requested();
    if (preamble.destroyed())
    {
        m_client.phase = EClientPhase::stopped;
        m_deactivated = true;
    }
    else if (preamble.stopping() || m_failed)
    {
        m_client.phase = EClientPhase::stopping;
        m_client.stop_requested = true;
    }
    else if (preamble.ready()) m_client.phase = EClientPhase::ready;
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
    (void)m_client.preamble->activate();
    leave();
    observe();
    finish_frame();
}

void CPumpedClient::request_stop(const modules::EExecutionStop reason) noexcept
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
            request_stop(modules::EExecutionStop::creation_failure);
        }
    }
    if (!enter()) return;
    if (m_client.stop_requested && !m_stop_begun)
    {
        m_stop_begun = true;
        m_client.preamble->request_stop(m_stop_reason);
    }
    (void)m_client.notifications_pending.exchange(0u, std::memory_order_acq_rel);
    m_client.preamble->process();
    leave();
    observe();
    finish_frame();
}

void CPumpedClient::limit_wait(std::uint64_t& remaining) noexcept
{
    if (m_client.preamble) m_client.preamble->limit_wait(remaining);
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

modules::SRestrictionEnterResult MV_STD_ABI_CALL CPumpedClient::enter_restriction(void* const context, const modules::SRestrictionReason reason) noexcept
{
    auto* const self = static_cast<CPumpedClient*>(context);
    if (!self || (t_invocation != self) || self->m_maintenance.running() ||
        self->m_client.preamble->destroying())
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
