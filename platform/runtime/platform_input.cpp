
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_input.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Routes input admission, readiness and retirement through generic Host borrows.

#include <cstring>
#include "runtime/platform_runtime.hpp"
#include "debug/macros.hpp"

namespace platform::runtime
{

using namespace input;
using services::EServiceStatus;

template<typename T>
static bool input_payload(const services::ServiceRequest& command, T& value) noexcept
{
    if (command.payload_size != sizeof(T)) return false;
    std::memcpy(&value, command.payload, sizeof(T));
    return true;
}

static bool input_owner(const CInputConnection& connection, const services::SDispatchedCommand& command) noexcept
{
    return (connection.acquisition.client.value == command.client.value) &&
        (connection.acquisition.requester.value == command.requester.value) &&
        (connection.acquisition.command.route.value == command.command.route.value);
}

CInputConnection* CPlatformRuntime::find_input(const SConnectionToken token) noexcept
{
    if (!token.value) return nullptr;
    for (TInstance<CInputConnection>& connection : m_inputs)
    {
        if (connection && (connection->view.connection.value == token.value))
        {
            return &*connection;
        }
    }
    return nullptr;
}

bool CPlatformRuntime::input_command(const services::SDispatchedCommand& dispatched) noexcept
{
    const services::ServiceRequest& command = dispatched.command;
    const ECommand opcode = static_cast<ECommand>(command.opcode);
    const bool desktop = opcode == ECommand::acquire_desktop;
    const bool acquisition = desktop || (opcode == ECommand::acquire_controller) || (opcode == ECommand::acquire_catalogue);
    //  Credit precedes every mutation, including ownership reservation.
    const std::int32_t completion = reserve(command, acquisition);
    if (completion < 0)
    {
        if (!acquisition) return false;
        const std::int32_t rejection = reserve(command, false);
        if (rejection < 0) return false;
        complete(rejection, EServiceStatus::busy);
        return true;
    }
    EServiceStatus status = EServiceStatus::success;
    if (command.flags || command.reserved || (command.payload_size > sizeof(command.payload)) ||
        (dispatched.contract.version != k_input_contract.version))
    {
        status = EServiceStatus::invalid_request;
    }
    for (std::uint32_t index = command.payload_size; index < sizeof(command.payload); ++index)
    {
        if (command.payload[index]) status = EServiceStatus::invalid_request;
    }
    if (status != EServiceStatus::success)
    {
        complete(completion, status);
        return true;
    }
    if (m_stopping)
    {
        complete(completion, EServiceStatus::closing);
        return true;
    }
    if ((opcode == ECommand::acquire_catalogue) || (opcode == ECommand::catalogue_ready) ||
        (opcode == ECommand::enumerate_device) || (opcode == ECommand::device_metadata))
    {
        return catalogue_command(dispatched, completion);
    }
    if ((opcode == ECommand::install_configuration) || (opcode == ECommand::retire_configuration))
    {
        return configuration_command(dispatched, completion);
    }
    if (acquisition)
    {
        SAcquireDesktop request;
        SAcquireController controller;
        const EConnection kind = desktop ? EConnection::desktop : EConnection::controller;
        if (desktop ? !input_payload(command, request) : !input_payload(command, controller))
        {
            status = EServiceStatus::invalid_request;
        }
        if (!desktop)
        {
            request.installation = controller.installation;
            request.profile = controller.profile;
            request.context = controller.context;
            start_controllers();
        }
        const SInputInstallation* installation = nullptr;
        for (const SConfiguration& configuration : m_configurations)
        {
            if (configuration.token.value && (configuration.token.value == request.installation.value))
            {
                const SInputInstallation* const candidate = configuration.data->profile(request.profile, kind);
                if (candidate && candidate->contains(request.context)) installation = candidate;
                break;
            }
        }
        for (std::int32_t slot = m_input_installations.first_live(); slot >= 0; slot = m_input_installations.next_live(slot))
        {
            const SInputInstallation& candidate = *m_input_installations.get_object(slot);
            if ((candidate.token.value == request.installation.value) && (candidate.kind == kind) &&
                (candidate.profile == request.profile) && candidate.contains(request.context))
            {
                installation = &candidate;
                break;
            }
        }
        std::uint32_t input_slot = 0u;
        if (!desktop)
        {
            input_slot = 1u;
            while ((input_slot < k_input_slots) && m_inputs[input_slot]) ++input_slot;
            if (input_slot == k_input_slots)
            {
                complete(completion, EServiceStatus::busy);
                return true;
            }
        }
        std::uint64_t selected_device = 0u;
        if (!desktop && (status == EServiceStatus::success)) status = select_device(controller.device, selected_device);
        TInstance<CInputConnection>& owned = m_inputs[input_slot];
        if (!installation) status = EServiceStatus::invalid_request;
        else if (desktop && (!request.window.value || (request.window.value != m_window.token.value)))
        {
            status = EServiceStatus::stale;
        }
        else if (desktop && (m_window.snapshot.phase == windows::EWindowPhase::closing))
        {
            status = EServiceStatus::closing;
        }
        else if (owned)
        {
            status = (owned->snapshot().phase == EPhase::closing) ? EServiceStatus::closing :
                ((owned->snapshot().phase == EPhase::reserved) ? EServiceStatus::busy :
                    (input_owner(*owned, dispatched) ? EServiceStatus::already_connected : EServiceStatus::occupied));
        }
        else if (!m_next_input || !m_context.owner_wake.is_valid())
        {
            status = EServiceStatus::unavailable;
        }
        if (status == EServiceStatus::success)
        {
            if (!owned.emplace(m_clock)) status = EServiceStatus::allocation_failed;
            else
            {
                CInputConnection& connection = *owned;
                connection.acquisition = dispatched;
                connection.completion = completion;
                SInputEndpointV4 identity;
                identity.kind = kind;
                if (!desktop) identity.clock = EClock::source_nanoseconds;
                identity.connection.value = m_next_input;
                m_next_input = (m_next_input == UINT64_MAX) ? 0u : (m_next_input + 1u);
                identity.provider = m_context.instance;
                identity.consumer = dispatched.requester;
                identity.window = request.window;
                identity.capacity_wake = m_context.owner_wake;
                if (!connection.initialise(identity, *installation, request.context, command.request.value))
                {
                    close_input(connection, EServiceStatus::allocation_failed);
                }
                else
                {
                    if (!desktop && (assign_device(connection, (selected_device ? selected_device : k_unassigned_device)) != EServiceStatus::success))
                    {
                        close_input(connection, EServiceStatus::unavailable);
                        return true;
                    }
                    connection.mapper().eligible(m_input_focus && !m_modal_depth[0] && !m_modal_depth[1]);
                    if (desktop && m_adapter.capture) m_desktop.attach(connection, m_window.token.value);
                    const modules::EBudgetStatus budget = connection.budget.start(m_context.execution->restriction, 5000000000u);
                    if ((budget != modules::EBudgetStatus::running) && (budget != modules::EBudgetStatus::restricted))
                    {
                        close_input(connection, EServiceStatus::native_failure);
                    }
                }
                return true;
            }
        }
        complete(completion, status);
        return true;
    }
    SConnectionToken token;
    if (command.payload_size >= sizeof(token)) std::memcpy(&token, command.payload, sizeof(token));
    CInputConnection* const connection = find_input(token);
    if (!connection) status = EServiceStatus::stale;
    else if (!input_owner(*connection, dispatched)) status = EServiceStatus::invalid_request;
    else if (connection->snapshot().phase == EPhase::closing) status = EServiceStatus::closing;
    else
    {
        //  Every initial-handshake control observes the same non-restarting budget.
        if (connection->snapshot().phase == EPhase::reserved)
        {
            const modules::EBudgetStatus budget = connection->budget.observe().status;
            if (budget == modules::EBudgetStatus::retry) status = EServiceStatus::busy;
            else if ((budget == modules::EBudgetStatus::expired) || (budget == modules::EBudgetStatus::failed))
            {
                close_input(*connection, ((budget == modules::EBudgetStatus::expired) ? EServiceStatus::timed_out : EServiceStatus::native_failure));
                status = EServiceStatus::closing;
            }
        }
        if (status == EServiceStatus::success)
        {
            switch (opcode)
            {
                case ECommand::input_ready:
                {
                    SInputReady ready;
                    if (!input_payload(command, ready)) status = EServiceStatus::invalid_request;
                    else if (ready.acquisition.value != connection->acquisition.command.request.value) status = EServiceStatus::stale;
                    else if (connection->ready)
                    {
                        status = (ready.registration.value == connection->registration.value) ? EServiceStatus::success : EServiceStatus::stale;
                    }
                    else
                    {
                        ::threading::SNotificationEndpoint endpoint;
                        status = m_context.services->bind_borrow_notification(m_context.service_context,
                            connection->borrow, ready.registration, &endpoint);
                        if (status == EServiceStatus::success)
                        {
                            connection->registration = ready.registration;
                            if (!connection->start(endpoint)) status = EServiceStatus::delivery_failed;
                        }
                    }
                    break;
                }
                case ECommand::boundary_ready:
                {
                    SBoundaryReady ready;
                    status = input_payload(command, ready) ? connection->acknowledge(ready) : EServiceStatus::invalid_request;
                    if ((status == EServiceStatus::success) && (connection->snapshot().phase == EPhase::active))
                    {
                        complete(connection->completion, EServiceStatus::success);
                    }
                    break;
                }
                case ECommand::select_context:
                {
                    SSelectContext select;
                    status = (input_payload(command, select) && !select.reserved) ?
                        connection->select_context(select.context, command.request.value) : EServiceStatus::invalid_request;
                    if ((status == EServiceStatus::success) && (connection->view.kind == EConnection::desktop))
                    {
                        m_desktop.cutover(k_desktop);
                        release_native_capture();
                    }
                    break;
                }
                case ECommand::enable_text:
                case ECommand::disable_text:
                {
                    STextTarget target;
                    status = (input_payload(command, target) && ((opcode == ECommand::enable_text) == (target.target != 0u))) ?
                        connection->text_target(target) : EServiceStatus::invalid_request;
                    if (status == EServiceStatus::success) m_desktop.cutover(k_text);
                    break;
                }
                case ECommand::set_capture:
                {
                    SSetCapture request;
                    if (!input_payload(command, request)) status = EServiceStatus::invalid_request;
                    else if ((connection->view.kind != EConnection::desktop) || !m_adapter.capture) status = EServiceStatus::unsupported;
                    else if ((request.transition != connection->snapshot().transition) ||
                        (request.action_generation != connection->snapshot().action_generation)) status = EServiceStatus::stale;
                    else if ((connection->snapshot().phase != EPhase::active) || connection->snapshot().pending ||
                        connection->snapshot().ingress_pending || !m_desktop_eligible || connection->snapshot().capture_grant) status = EServiceStatus::busy;
                    else
                    {
                        const std::uint64_t epoch = m_capture_epoch;
                        const modules::SRestrictionScope scope = enter_native(1u);
                        if (!scope.value) status = EServiceStatus::native_failure;
                        else
                        {
                            m_native_capture = m_adapter.capture(m_window.native, true);
                            leave_native(scope);
                            if (!m_native_capture) status = EServiceStatus::native_failure;
                            else if ((epoch != m_capture_epoch) || m_stopping || !m_desktop_eligible ||
                                (connection->snapshot().phase != EPhase::active) ||
                                (request.transition != connection->snapshot().transition) ||
                                (request.action_generation != connection->snapshot().action_generation)) status = EServiceStatus::stale;
                            else
                            {
                                connection->capture(command.request.value, ECaptureReason::none);
                                if (connection->snapshot().phase == EPhase::closing) status = EServiceStatus::delivery_failed;
                            }
                            if ((status != EServiceStatus::success) || !connection->snapshot().capture_grant) release_native_capture();
                        }
                    }
                    break;
                }
                case ECommand::assign_device:
                {
                    SAssignDevice request;
                    if (!input_payload(command, request)) status = EServiceStatus::invalid_request;
                    else if (connection->view.kind != EConnection::controller) status = EServiceStatus::unsupported;
                    else if (request.action_generation != connection->snapshot().action_generation) status = EServiceStatus::stale;
                    else if (connection->snapshot().pending) status = EServiceStatus::busy;
                    else status = assign_device(*connection, request.device);
                    break;
                }
                case ECommand::release_capture:
                {
                    SReleaseCapture request;
                    if (!input_payload(command, request) || !request.grant) status = EServiceStatus::invalid_request;
                    else if ((connection->view.kind != EConnection::desktop) || !m_adapter.capture) status = EServiceStatus::unsupported;
                    else if ((request.transition != connection->snapshot().transition) ||
                        (request.grant != connection->snapshot().capture_grant)) status = EServiceStatus::stale;
                    else
                    {
                        connection->capture(0u, ECaptureReason::released);
                        release_native_capture();
                        if (m_native_capture) status = EServiceStatus::native_failure;
                    }
                    break;
                }
                default:
                {
                    status = EServiceStatus::unsupported;
                    break;
                }
            }
        }
    }
    complete(completion, status);
    return true;
}

void CPlatformRuntime::close_input(CInputConnection& connection, const EServiceStatus status) noexcept
{
    if (!connection.closing_started)
    {
        connection.closing_started = true;
        connection.clock_failed = m_clock.read(connection.closing_awake).code != platform::threading::EWaitError::none;
    }
    connection.close();
    complete(connection.completion, status);
}

void CPlatformRuntime::input_message(const ::threading::CErasedPodMsg& message) noexcept
{
    services::ServiceBorrowControl control;
    services::ServiceRequestCancelled cancelled;
    const bool borrow_control = message.copy_payload_to(control);
    const bool cancellation = !borrow_control && message.copy_payload_to(cancelled);
    for (TInstance<CInputConnection>& owned : m_inputs)
    {
        if (!owned) continue;
        CInputConnection& connection = *owned;
        if (borrow_control && (control.borrow.value == connection.borrow.value) &&
            (control.route.value == connection.acquisition.command.route.value) &&
            (control.request.value == connection.acquisition.command.request.value))
        {
            close_input(connection, EServiceStatus::closing);
            if (control.control == services::EServiceBorrowControl::consumer_quiescent)
            {
                connection.quiescence = control.quiescence;
            }
        }
        else if (cancellation && (cancelled.route.value == connection.acquisition.command.route.value) &&
            (cancelled.request.value == connection.acquisition.command.request.value))
        {
            close_input(connection, EServiceStatus::cancelled);
        }
    }
    for (TInstance<SCatalogueUse>& owned : m_catalogues)
    {
        if (!owned) continue;
        SCatalogueUse& use = *owned;
        if (borrow_control && (control.borrow.value == use.borrow.value) &&
            (control.route.value == use.acquisition.command.route.value) && (control.request.value == use.acquisition.command.request.value))
        {
            use.closing = true;
            if (control.control == services::EServiceBorrowControl::consumer_quiescent) use.quiescence = control.quiescence;
        }
        else if (cancellation && (cancelled.route.value == use.acquisition.command.route.value) &&
            (cancelled.request.value == use.acquisition.command.request.value))
        {
            use.closing = true;
            complete(use.completion, EServiceStatus::cancelled);
        }
    }
}

void CPlatformRuntime::progress_inputs() noexcept
{
    if (m_native_capture && (!m_inputs[0] || !m_inputs[0]->snapshot().capture_grant ||
        (m_inputs[0]->snapshot().phase == EPhase::closing))) release_native_capture();
    for (TInstance<CInputConnection>& owned : m_inputs)
    {
        if (!owned) continue;
        CInputConnection& connection = *owned;
        if (m_stopping || ((connection.view.kind == EConnection::desktop) &&
            (m_window.snapshot.phase == windows::EWindowPhase::closing)))
        {
            close_input(connection, EServiceStatus::closing);
        }
        if (connection.snapshot().phase == EPhase::reserved)
        {
            const modules::EBudgetStatus budget = connection.budget.observe().status;
            if ((budget == modules::EBudgetStatus::expired) || (budget == modules::EBudgetStatus::failed))
            {
                close_input(connection, ((budget == modules::EBudgetStatus::expired) ? EServiceStatus::timed_out : EServiceStatus::native_failure));
            }
            else if (!connection.borrow.value && connection.mapper().prepared() && (budget != modules::EBudgetStatus::retry))
            {
                const services::SServiceBorrowOffer offer{ connection.acquisition.command.route, connection.acquisition.command.request,
                    { &connection.view, sizeof(connection.view), 4u } };
                const EServiceStatus status = m_context.services->prepare_borrow(m_context.service_context, &offer, &connection.borrow);
                if ((status != EServiceStatus::success) && (status != EServiceStatus::busy))
                {
                    close_input(connection, status);
                }
            }
        }
        if (connection.snapshot().phase != EPhase::closing)
        {
            std::uint64_t awake = 0u;
            if (m_clock.read(awake).code != platform::threading::EWaitError::none)
            {
                close_input(connection, EServiceStatus::native_failure);
            }
            else connection.progress(awake);
        }
        if (connection.snapshot().phase != EPhase::closing)
        {
            continue;
        }
        close_input(connection, (connection.snapshot().failure ? EServiceStatus::delivery_failed : EServiceStatus::closing));
        if (!connection.retirement_started && connection.borrow.value)
        {
            const EServiceStatus status = m_context.services->begin_borrow_retirement(m_context.service_context, connection.borrow);
            if (status == EServiceStatus::success) connection.retirement_started = true;
            else if (status != EServiceStatus::busy) native_failure(static_cast<std::uint32_t>(status));
        }
        std::uint64_t awake = 0u;
        const bool clock_ok = m_clock.read(awake).code == platform::threading::EWaitError::none;
        if (!connection.diagnosed && (connection.clock_failed || !clock_ok || (awake < connection.closing_awake) ||
            ((awake - connection.closing_awake) >= 5000000000u)))
        {
            connection.diagnosed = true;
            MV_LOG(error, "Platform: input use {} cleanup remains unresolved, provenance {}",
                connection.borrow.value, static_cast<std::uint32_t>(connection.quiescence));
        }
        const bool cleanup = !connection.borrow.value || (connection.quiescence == services::EServiceQuiescence::never_published) ||
            (connection.quiescence == services::EServiceQuiescence::relinquished) ||
            (connection.quiescence == services::EServiceQuiescence::execution_ended);
        if (cleanup && (connection.completion < 0) && !m_native_depth && !m_callback_depth && !m_modal_depth[0] && !m_modal_depth[1])
        {
            connection.notification = {};
            if ((connection.view.kind == EConnection::desktop) && m_native_capture) continue;
            if (!connection.mapper().clear_step()) continue;
            if (!connection.borrow.value || (m_context.services->finish_borrow_retirement(m_context.service_context,
                connection.borrow) == EServiceStatus::success))
            {
                if (connection.view.kind == EConnection::desktop)
                {
                    m_desktop.detach();
                }
                owned.reset();
                catalogue_changed();
            }
        }
    }
}

void CPlatformRuntime::progress_mappers(const std::uint64_t budget_ns) noexcept
{
    if (!budget_ns || m_stopping)
    {
        return;
    }
    std::uint64_t start = 0u;
    if (m_clock.read(start).code != platform::threading::EWaitError::none)
    {
        native_failure(UINT32_MAX);
        return;
    }
    const std::uint64_t limit = std::min(budget_ns, UINT64_C(500000));
    SMapperBudget budget;
    std::uint32_t idle = 0u;
    while (budget.work && (idle < k_input_slots))
    {
        std::uint64_t now = 0u;
        if ((m_clock.read(now).code != platform::threading::EWaitError::none) || (now < start))
        {
            native_failure(UINT32_MAX);
            return;
        }
        if ((now - start) >= limit) break;
        TInstance<CInputConnection>& owned = m_inputs[m_mapper_turn];
        ++m_mapper_turn;
        if (m_mapper_turn == k_input_slots) m_mapper_turn = 0u;
        bool advanced = false;
        if (owned && (owned->snapshot().phase != EPhase::closing) &&
            ((owned->view.kind != EConnection::desktop) || (m_window.snapshot.phase != windows::EWindowPhase::closing)))
        {
            CInputMapper& mapper = owned->mapper();
            if (!mapper.prepared())
            {
                const std::uint32_t cost = mapper.prepare_cost();
                if (budget.work >= cost)
                {
                    budget.work -= cost;
                    advanced = true;
                    if (!mapper.prepare_step()) close_input(*owned, EServiceStatus::allocation_failed);
                }
            }
            else
            {
                if ((owned->view.kind == EConnection::desktop) && !mapper.pending()) advanced = m_desktop.advance(budget);
                if (!advanced) advanced = mapper.advance(budget);
            }
        }
        idle = advanced ? 0u : (idle + 1u);
    }
}

void CPlatformRuntime::input_eligibility() noexcept
{
    const bool eligible = m_input_focus && !m_modal_depth[0] && !m_modal_depth[1];
    for (TInstance<CInputConnection>& owned : m_inputs)
    {
        if (owned) owned->mapper().eligible(eligible);
    }
    if (m_desktop_eligible != eligible)
    {
        m_desktop_eligible = eligible;
        m_desktop.cutover(k_desktop);
        if (!eligible) release_native_capture();
    }
}

void CPlatformRuntime::release_native_capture() noexcept
{
    if (!m_native_capture || m_releasing_capture || !m_adapter.capture) return;
    m_releasing_capture = true;
    const modules::SRestrictionScope scope = enter_native(1u);
    if (scope.value)
    {
        if (m_adapter.capture(m_window.native, false)) m_native_capture = false;
        else native_failure(m_window.native.error);
        leave_native(scope);
    }
    m_releasing_capture = false;
}

void CPlatformRuntime::desktop_capture_lost() noexcept
{
    if (m_capture_epoch == UINT64_MAX) native_failure(UINT32_MAX);
    else ++m_capture_epoch;
    m_native_capture = false;
    if (!m_releasing_capture && m_inputs[0] && m_inputs[0]->snapshot().capture_grant)
    {
        m_inputs[0]->capture_lost();
        m_desktop.cutover(k_actions);
    }
}

void CPlatformRuntime::desktop_observation(const SDesktopTransaction& transaction) noexcept
{
    if (cancelling()) return;
    SDesktopTransaction timed = transaction;
    if (m_clock.read(timed.time_ns).code != platform::threading::EWaitError::none)
    {
        native_failure(UINT32_MAX);
        return;
    }
    (void)m_desktop.observe(timed);
}

void CPlatformRuntime::desktop_quiet(const SInputObservation& baseline, const std::uint64_t epoch) noexcept
{
    if (cancelling() || !m_desktop_eligible) return;
    SInputObservation timed = baseline;
    if (m_clock.read(timed.time_ns).code != platform::threading::EWaitError::none)
    {
        native_failure(UINT32_MAX);
        return;
    }
    m_desktop.quiet(timed, epoch);
}

void CPlatformRuntime::input_focus(const bool focused) noexcept
{
    m_input_focus = focused;
    input_eligibility();
}

EInputAdmission CPlatformRuntime::input_observation(const SConnectionToken token, const SInputObservation& observation) noexcept
{
    CInputConnection* const connection = find_input(token);
    if (!connection || m_stopping || ((connection->view.kind == EConnection::desktop) &&
        (m_window.snapshot.phase == windows::EWindowPhase::closing)))
    {
        return EInputAdmission::closed;
    }
    return connection->mapper().submit(observation);
}

void CPlatformRuntime::input_device(const SConnectionToken token, const std::uint64_t instance) noexcept
{
    CInputConnection* const connection = find_input(token);
    if (connection) connection->mapper().device(instance);
}

void CPlatformRuntime::input_discontinuity(const SConnectionToken token) noexcept
{
    CInputConnection* const connection = find_input(token);
    if (connection) connection->mapper().discontinuity();
}

}   //  namespace platform::runtime
