
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_runtime.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Implements Platform command handling, borrowed window lifetimes and retained
//  replies, coordinating native calls with Host restrictions and shutdown.

#include <cstring>
#include "runtime/platform_runtime.hpp"
#include "threading/messages/CErasedPodMsg.hpp"
#include "threading/messages/message_helpers.hpp"
#include "module/module_messages.hpp"
#include "debug/macros.hpp"

namespace platform::runtime
{

using services::EServiceStatus;
using windows::EWindowCommand;
using windows::EWindowPhase;

template<typename T>
static bool payload(const services::ServiceRequest& command, T& value) noexcept
{
    if (command.payload_size != sizeof(T)) return false;
    std::memcpy(&value, command.payload, sizeof(T));
    return true;
}

CPlatformRuntime::CPlatformRuntime(const modules::SHostPumpContext& context,
    const native::SWindowAdapter& adapter, const platform::threading::SAwakeClock clock) noexcept
    : m_context{ context }, m_adapter{ adapter }, m_clock{ clock }
{
    m_window.native.runtime = this;
}

bool CPlatformRuntime::initialise() noexcept
{
    return m_context.services && m_context.execution && m_context.execution->restriction.is_valid() &&
        m_adapter.create && m_adapter.dispatch && m_adapter.destroy && m_uses.initialise();
}

std::int32_t CPlatformRuntime::reserve(const services::ServiceRequest& command, const bool acquisition) noexcept
{
    const std::int32_t first = acquisition ? 0 : 32, end = acquisition ? 32 : 96;
    for (auto index = first; index < end; ++index)
    {
        auto& completion = m_completions[index];
        if (completion.live) continue;
        completion = {};
        completion.live = true;
        completion.reply.route = command.route;
        completion.reply.request = command.request;
        completion.reply.opcode = command.opcode;
        return index;
    }
    return -1;
}

void CPlatformRuntime::complete(const std::int32_t slot, const EServiceStatus status) noexcept
{
    if ((slot < 0) || !m_completions[slot].live || m_completions[slot].terminal)
    {
        return;
    }
    m_completions[slot].reply.status = status;
    m_completions[slot].terminal = true;
}

void CPlatformRuntime::flush_completions() noexcept
{
    for (std::int32_t index = 0; index < 96; ++index)
    {
        auto& completion = m_completions[index];
        if (!completion.live || !completion.terminal)
        {
            continue;
        }
        const auto status = m_context.services->complete(m_context.service_context, &completion.reply);
        if (status == EServiceStatus::busy) continue;
        if (status != EServiceStatus::success)
        {
            native_failure(static_cast<std::uint32_t>(status));
            continue;
        }
        for (auto slot = m_uses.first_live(); slot >= 0; slot = m_uses.next_live(slot))
        {
            auto& use = *m_uses.get_object(slot);
            if (use.completion == index)
            {
                use.completion = -1;
            }
        }
        completion = {};
    }
}

SWindowUse* CPlatformRuntime::find(const services::SServiceBorrow borrow) noexcept
{
    if (!borrow.value) return nullptr;
    for (auto slot = m_uses.first_live(); slot >= 0; slot = m_uses.next_live(slot))
    {
        auto* const use = m_uses.get_object(slot);
        if (use->borrow.value == borrow.value) return use;
    }
    return nullptr;
}

bool CPlatformRuntime::owns(const SWindowUse& use, const services::SDispatchedCommand& command) const noexcept
{
    return (use.acquisition.client.value == command.client.value) &&
        (use.acquisition.requester.value == command.requester.value) &&
        (use.acquisition.command.route.value == command.command.route.value);
}

bool CPlatformRuntime::holder(const services::SDispatchedCommand& command, const bool controller) const noexcept
{
    for (auto slot = m_uses.first_live(); slot >= 0; slot = m_uses.next_live(slot))
    {
        const auto& use = *m_uses.get_object(slot);
        if (!use.closing && owns(use, command) && (!controller || use.view.controller))
        {
            return true;
        }
    }
    return false;
}

void CPlatformRuntime::acquire(const services::SDispatchedCommand& command, const bool create,
    const std::int32_t completion) noexcept
{
    const auto slot = m_uses.emplace(m_clock);
    if (slot < 0)
    {
        complete(completion, EServiceStatus::allocation_failed);
        return;
    }
    ++m_use_count;
    auto& use = *m_uses.get_object(slot);
    use.acquisition = command;
    use.completion = completion;
    const auto budget = use.budget.start(m_context.execution->restriction, 5000000000u);
    if ((budget != modules::EBudgetStatus::running) && (budget != modules::EBudgetStatus::restricted))
    {
        close_use(use, EServiceStatus::native_failure);
        return;
    }
    if (create)
    {
        m_window.token.value = m_next_window;
        m_next_window = (m_next_window == UINT64_MAX) ? 0u : (m_next_window + 1u);
        m_window.snapshot = {};
        m_window.destruction_failed = false;
        const auto scope = enter_native(0u);
        if (!scope.value)
        {
            close_use(use, EServiceStatus::native_failure);
            return;
        }
        const auto status = m_adapter.create(m_window.native);
        leave_native(scope);
        if (status != EServiceStatus::success)
        {
            close_window();
            close_use(use, status);
            return;
        }
    }
    use.view.provider = m_context.instance;
    use.view.window = m_window.token;
    use.view.controller = create ? 1u : 0u;
    use.view.native_window = m_window.native.handle;
    use.view.native_instance = m_window.native.instance;
    use.view.state = &m_window.publication;
    publish();
}

EServiceStatus CPlatformRuntime::ready(const services::SDispatchedCommand& command,
    const windows::SWindowReady& request) noexcept
{
    auto* const use = find(request.borrow);
    if (!use || (request.window.value != m_window.token.value))
    {
        return EServiceStatus::stale;
    }
    if (!owns(*use, command))
    {
        return EServiceStatus::invalid_request;
    }
    if (use->closing || (m_window.snapshot.phase == EWindowPhase::closing))
    {
        return EServiceStatus::closing;
    }
    if (use->ready)
    {
        return (use->registration.value == request.registration.value) ? EServiceStatus::success : EServiceStatus::stale;
    }
    const auto budget = use->budget.observe().status;
    if (budget == modules::EBudgetStatus::retry)
    {
        return EServiceStatus::busy;
    }
    if ((budget == modules::EBudgetStatus::expired) || (budget == modules::EBudgetStatus::failed))
    {
        close_use(*use, ((budget == modules::EBudgetStatus::expired) ? EServiceStatus::timed_out : EServiceStatus::native_failure));
        return EServiceStatus::closing;
    }
    const auto status = m_context.services->bind_borrow_notification(m_context.service_context,
        use->borrow, request.registration, &use->notification);
    if (status != EServiceStatus::success) return status;
    use->registration = request.registration;
    if (!use->notification.signal())
    {
        native_failure(static_cast<std::uint32_t>(EServiceStatus::delivery_failed));
        return EServiceStatus::delivery_failed;
    }
    use->ready = true;
    complete(use->completion, EServiceStatus::success);
    return EServiceStatus::success;
}

bool CPlatformRuntime::command(const services::SDispatchedCommand& dispatched) noexcept
{
    const auto& command = dispatched.command;
    const auto opcode = static_cast<EWindowCommand>(command.opcode);
    EServiceStatus rejection = EServiceStatus::success;
    if (command.flags || command.reserved || (command.payload_size > sizeof(command.payload)) ||
        (dispatched.contract.number != windows::k_window_contract.number) ||
        (dispatched.contract.version != windows::k_window_contract.version))
    {
        rejection = EServiceStatus::invalid_request;
    }
    for (auto index = command.payload_size; index < sizeof(command.payload); ++index)
    {
        if (command.payload[index])
        {
            rejection = EServiceStatus::invalid_request;
        }
    }
    const bool create = opcode == EWindowCommand::create_window;
    const bool acquisition = create || (opcode == EWindowCommand::acquire_window);
    windows::SWindowRequest window;
    if (acquisition && (rejection == EServiceStatus::success))
    {
        if (m_stopping) rejection = EServiceStatus::closing;
        else if (create && command.payload_size)
        {
            rejection = EServiceStatus::invalid_request;
        }
        else if (create && m_window.token.value)
        {
            rejection = EServiceStatus::occupied;
        }
        else if (create && !m_next_window)
        {
            rejection = EServiceStatus::busy;
        }
        else if (!create && !payload(command, window))
        {
            rejection = EServiceStatus::invalid_request;
        }
        else if (!create && (!window.window.value || (window.window.value != m_window.token.value)))
        {
            rejection = EServiceStatus::stale;
        }
        else if (!create && (m_window.snapshot.phase == EWindowPhase::closing))
        {
            rejection = EServiceStatus::closing;
        }
        else if (m_use_count == 32u)
        {
            rejection = EServiceStatus::busy;
        }
        if (rejection == EServiceStatus::success)
        {
            const auto slot = reserve(command, true);
            if (slot >= 0)
            {
                acquire(dispatched, create, slot);
                return true;
            }
            rejection = EServiceStatus::busy;
        }
    }
    const auto slot = reserve(command, false);
    if (slot < 0) return false;
    if (rejection != EServiceStatus::success)
    {
        complete(slot, rejection);
        return true;
    }
    if (m_stopping)
    {
        complete(slot, EServiceStatus::closing);
        return true;
    }
    EServiceStatus status = EServiceStatus::unsupported;
    switch (opcode)
    {
    case EWindowCommand::window_ready:
    {
        windows::SWindowReady request;
        status = payload(command, request) ? ready(dispatched, request) : EServiceStatus::invalid_request;
        break;
    }
    case EWindowCommand::request_close:
    {
        if (!payload(command, window))
        {
            status = EServiceStatus::invalid_request;
        }
        else if (!window.window.value || (window.window.value != m_window.token.value))
        {
            status = EServiceStatus::stale;
        }
        else if (!holder(dispatched, false))
        {
            status = EServiceStatus::invalid_request;
        }
        else if (m_window.snapshot.phase == EWindowPhase::closing)
        {
            status = EServiceStatus::closing;
        }
        else
        {
            native_close();
            status = EServiceStatus::success;
        }
        break;
    }
    case EWindowCommand::answer_close:
    {
        windows::SAnswerClose request;
        if (!payload(command, request))
        {
            status = EServiceStatus::invalid_request;
        }
        else if (!request.window.value || (request.window.value != m_window.token.value))
        {
            status = EServiceStatus::stale;
        }
        else if (!holder(dispatched, true))
        {
            status = EServiceStatus::invalid_request;
        }
        else
        {
            status = m_window.answer_close(request);
            publish();
        }
        break;
    }
    default:
    {
        break;
    }
    }
    complete(slot, status);
    return true;
}

void CPlatformRuntime::close_use(SWindowUse& use, const EServiceStatus status) noexcept
{
    if (!use.closing)
    {
        use.closing = true;
        use.clock_failed = m_clock.read(use.closing_awake).code != platform::threading::EWaitError::none;
        if (use.view.controller) close_window();
    }
    complete(use.completion, status);
}

bool CPlatformRuntime::message(const ::threading::CErasedPodMsg& message) noexcept
{
    services::ServiceBorrowControl control;
    services::ServiceRequestCancelled cancelled;
    if (message.copy_payload_to(control))
    {
        auto* const use = find(control.borrow);
        if (!use || (control.route.value != use->acquisition.command.route.value) ||
            (control.request.value != use->acquisition.command.request.value))
        {
            return true;
        }
        close_use(*use, EServiceStatus::closing);
        if (control.control == services::EServiceBorrowControl::consumer_quiescent)
        {
            use->quiescence = control.quiescence;
        }
    }
    else if (message.copy_payload_to(cancelled))
    {
        for (auto slot = m_uses.first_live(); slot >= 0; slot = m_uses.next_live(slot))
        {
            auto& use = *m_uses.get_object(slot);
            if ((use.acquisition.command.route.value == cancelled.route.value) &&
                (use.acquisition.command.request.value == cancelled.request.value))
            {
                close_use(use, EServiceStatus::cancelled);
            }
        }
    }
    return true;
}

void CPlatformRuntime::progress_uses() noexcept
{
    for (auto slot = m_uses.first_live(); slot >= 0;)
    {
        const auto next = m_uses.next_live(slot);
        auto& use = *m_uses.get_object(slot);
        if (m_stopping || (m_window.snapshot.phase == EWindowPhase::closing))
        {
            close_use(use, EServiceStatus::closing);
        }
        if (!use.closing && !use.ready)
        {
            const auto budget = use.budget.observe().status;
            if (budget == modules::EBudgetStatus::expired)
            {
                close_use(use, EServiceStatus::timed_out);
            }
            else if (budget == modules::EBudgetStatus::failed)
            {
                close_use(use, EServiceStatus::native_failure);
            }
            else if (!use.borrow.value && (budget != modules::EBudgetStatus::retry))
            {
                const services::SServiceBorrowOffer offer{ use.acquisition.command.route, use.acquisition.command.request,
                    { &use.view, sizeof(use.view), 1u } };
                const auto status = m_context.services->prepare_borrow(m_context.service_context, &offer, &use.borrow);
                if ((status != EServiceStatus::success) && (status != EServiceStatus::busy))
                {
                    close_use(use, status);
                }
            }
        }
        if (use.closing)
        {
            if (!use.retirement_started && use.borrow.value)
            {
                const auto status = m_context.services->begin_borrow_retirement(m_context.service_context, use.borrow);
                if (status == EServiceStatus::success)
                {
                    use.retirement_started = true;
                }
                else if (status != EServiceStatus::busy)
                {
                    native_failure(static_cast<std::uint32_t>(status));
                }
            }
            std::uint64_t awake = 0u;
            const bool clock_ok = m_clock.read(awake).code == platform::threading::EWaitError::none;
            if (!use.diagnosed && (use.clock_failed || !clock_ok || (awake < use.closing_awake) || ((awake - use.closing_awake) >= 5000000000u)))
            {
                use.diagnosed = true;
                MV_LOG(error, "Platform: window use {} cleanup remains unresolved, provenance {}",
                    use.borrow.value, static_cast<std::uint32_t>(use.quiescence));
            }
            const bool cleanup = !use.borrow.value || (use.quiescence == services::EServiceQuiescence::never_published) ||
                (use.quiescence == services::EServiceQuiescence::relinquished);
            if (cleanup && (use.completion < 0) && !m_callback_depth && !m_native_depth)
            {
                use.notification = {};
                if (!use.borrow.value || (m_context.services->finish_borrow_retirement(m_context.service_context, use.borrow) == EServiceStatus::success))
                {
                    (void)m_uses.erase(slot);
                    --m_use_count;
                }
            }
        }
        slot = next;
    }
}

void CPlatformRuntime::publish() noexcept
{
    if (!m_window.publication.publish(m_window.snapshot))
    {
        m_window.close(UINT32_MAX);
        m_failed = m_stopping = true;
    }
    bool failed = false;
    for (auto slot = m_uses.first_live(); slot >= 0; slot = m_uses.next_live(slot))
    {
        auto& use = *m_uses.get_object(slot);
        if (!use.closing && use.notification.is_valid() && !use.notification.signal())
        {
            failed = true;
        }
    }
    if (failed)
    {
        m_failed = m_stopping = true;
        m_window.close(static_cast<std::uint32_t>(EServiceStatus::delivery_failed));
        (void)m_window.publication.publish(m_window.snapshot);
    }
}

void CPlatformRuntime::close_window(const std::uint32_t failure) noexcept
{
    if ((m_window.snapshot.phase == EWindowPhase::closing) && (!failure || m_window.snapshot.failure))
    {
        return;
    }
    m_window.close(failure);
    publish();
}

void CPlatformRuntime::native_extent(const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t dpi, const bool minimised) noexcept
{
    m_window.extent(width, height, dpi, minimised);
    publish();
}

void CPlatformRuntime::native_close() noexcept
{
    if (m_window.request_close()) publish();
}

void CPlatformRuntime::native_failure(const std::uint32_t error) noexcept
{
    m_failed = m_stopping = true;
    close_window(error ? error : UINT32_MAX);
}

modules::SRestrictionScope CPlatformRuntime::enter_native(const std::uint32_t reason) noexcept
{
    const auto entered = m_context.services->enter_restriction(m_context.service_context, { reason });
    if (entered.status != modules::ERestrictionStatus::success)
    {
        native_failure(static_cast<std::uint32_t>(EServiceStatus::native_failure));
        return {};
    }
    ++m_native_depth;
    return entered.scope;
}

void CPlatformRuntime::leave_native(const modules::SRestrictionScope scope) noexcept
{
    if (!scope.value || !m_native_depth)
    {
        native_failure(UINT32_MAX);
        return;
    }
    --m_native_depth;
    if (m_context.services->leave_restriction(m_context.service_context, scope) != modules::ERestrictionStatus::success)
    {
        native_failure(UINT32_MAX);
    }
}

void CPlatformRuntime::enter_callback() noexcept { ++m_callback_depth; }
void CPlatformRuntime::leave_callback() noexcept
{
    if (!m_callback_depth) native_failure(UINT32_MAX);
    else --m_callback_depth;
}

void CPlatformRuntime::enter_modal(const bool menu) noexcept
{
    const auto index = menu ? 1u : 0u;
    if (m_modal_depth[index] == 8u)
    {
        native_failure(UINT32_MAX);
        return;
    }
    const auto scope = m_context.services->enter_restriction(m_context.service_context, { menu ? 4u : 3u });
    if (scope.status != modules::ERestrictionStatus::success)
    {
        native_failure(UINT32_MAX);
        return;
    }
    m_modal[index][m_modal_depth[index]++] = scope.scope;
}

void CPlatformRuntime::leave_modal(const bool menu) noexcept
{
    const auto index = menu ? 1u : 0u;
    if (!m_modal_depth[index])
    {
        native_failure(UINT32_MAX);
        return;
    }
    const auto scope = m_modal[index][--m_modal_depth[index]];
    if (m_context.services->leave_restriction(m_context.service_context, scope) != modules::ERestrictionStatus::success)
    {
        native_failure(UINT32_MAX);
    }
}

bool CPlatformRuntime::cancelling() const noexcept
{
    return m_stopping || (m_window.snapshot.phase == EWindowPhase::closing);
}

bool CPlatformRuntime::maintain_native() noexcept
{
    if (m_maintaining) return cancelling();
    m_maintaining = true;
    const auto result = m_context.services->maintain_host(m_context.service_context);
    if (result.status != modules::EMaintenanceStatus::success)
    {
        native_failure(UINT32_MAX);
    }
    if (result.cancel_native || result.shutdown_requested)
    {
        m_stopping = true;
        close_window();
    }
    m_maintaining = false;
    return cancelling();
}

void CPlatformRuntime::progress_window() noexcept
{
    if (!m_window.token.value || (m_window.snapshot.phase != EWindowPhase::closing) || m_use_count ||
        m_native_depth || m_callback_depth || m_modal_depth[0] || m_modal_depth[1] || m_window.destruction_failed)
    {
        return;
    }
    if (m_window.native.live())
    {
        const auto scope = enter_native(2u);
        if (!scope.value) return;
        const bool destroyed = m_adapter.destroy(m_window.native);
        leave_native(scope);
        if (!destroyed || m_window.native.live())
        {
            m_window.destruction_failed = true;
            native_failure(m_window.native.error);
            return;
        }
    }
    m_window.token = {};
}

bool CPlatformRuntime::stopped() const noexcept
{
    if (!m_stopping || m_window.token.value || m_use_count || m_native_depth || m_callback_depth ||
        m_modal_depth[0] || m_modal_depth[1])
    {
        return false;
    }
    for (const auto& completion : m_completions)
    {
        if (completion.live)
        {
            return false;
        }
    }
    return true;
}

void CPlatformRuntime::progress(modules::SProgress& progress) const noexcept
{
    progress = {};
    progress.phase = stopped() ? modules::EPumpPhase::stopped :
        (m_stopping ? modules::EPumpPhase::stopping : modules::EPumpPhase::ready);
    progress.work_pending = stopped() ? 0u : 1u;
    progress.error_code = m_failed ? 1u : 0u;
}

void CPlatformRuntime::begin_stop(modules::SProgress& result) noexcept
{
    m_stopping = true;
    close_window();
    progress_uses();
    flush_completions();
    progress_window();
    progress(result);
}

void CPlatformRuntime::pump(const modules::SPumpInput& input, modules::SPumpResult& result) noexcept
{
    result = {};
    std::uint64_t start = 0u;
    const bool clock_valid = m_clock.read(start).code == platform::threading::EWaitError::none;
    if (!clock_valid) native_failure(UINT32_MAX);
    const auto limit = (input.time_budget_ns < 2000000u) ? input.time_budget_ns : 2000000u;
    const auto remaining = [&]() noexcept -> std::uint64_t
    {
        std::uint64_t now = 0u;
        if (!clock_valid || (m_clock.read(now).code != platform::threading::EWaitError::none) || (now < start))
        {
            native_failure(UINT32_MAX);
            return 0u;
        }
        return ((now - start) < limit) ? (limit - (now - start)) : 0u;
    };
    flush_completions();
    for (; result.consumed_messages < input.message_count; ++result.consumed_messages)
    {
        if (!message(input.messages[result.consumed_messages]))
        {
            break;
        }
    }
    progress_uses();
    const auto dispatch = [&]() noexcept
    {
        if (!m_window.native.handle || m_window.destruction_failed)
        {
            return;
        }
        const auto budget = remaining();
        if (!budget) return;
        const auto scope = enter_native(1u);
        if (!scope.value) return;
        if (!m_adapter.dispatch(m_window.native, budget))
        {
            native_failure(m_window.native.error);
        }
        leave_native(scope);
    };
    m_native_first = !m_native_first;
    if (m_native_first) dispatch();
    for (; (result.consumed_commands < input.command_count) && (result.consumed_commands < 64u) && remaining(); ++result.consumed_commands)
    {
        if (!command(input.commands[result.consumed_commands]))
        {
            break;
        }
    }
    progress_uses();
    flush_completions();
    if (!m_native_first) dispatch();
    progress_uses();
    progress_window();
    progress(result.progress);
}

}   //  namespace platform::runtime
