
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    controller_input.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    09 Nov 26
//
//  Exclusive controller claims and bounded, fair native observation ingestion.

#include <algorithm>
#include <cmath>
#include "runtime/platform_runtime.hpp"
#include "debug/macros.hpp"

namespace platform::runtime
{

using namespace input;
using services::EServiceStatus;

void CPlatformRuntime::start_controllers() noexcept
{
    if (m_controllers_started) return;
    m_controllers_started = true;
    if (!m_controller_adapter.initialise || !m_controller_adapter.dispatch || !m_controller_adapter.read ||
        !m_controller_adapter.destroy || !m_controller_adapter.initialise(m_controllers))
    {
        controller_backend(EControllerBackend::unavailable, m_controllers.error);
    }
    else if (m_controller_adapter.eligibility)
    {
        m_controller_adapter.eligibility(m_controllers, (m_input_focus && !m_modal_depth[0] && !m_modal_depth[1]));
    }
}

void CPlatformRuntime::controller_backend(const EControllerBackend state, const std::uint32_t error) noexcept
{
    if ((m_catalogue.backend == state) && (m_catalogue.error == error))
    {
        return;
    }
    m_catalogue.backend = state;
    m_catalogue.error = error;
    if ((state == EControllerBackend::unavailable) || (state == EControllerBackend::failed) || (state == EControllerBackend::closing))
    {
        for (SDevice& device : m_devices)
        {
            if (device.token) controller_departed(device.info.device);
        }
    }
    catalogue_changed();
}

void CPlatformRuntime::controller_overflow() noexcept
{
    if (!m_catalogue.overflow)
    {
        m_catalogue.overflow = 1u;
        catalogue_changed();
    }
}

void CPlatformRuntime::controller_arrived(const native::SControllerInfo& info) noexcept
{
    if (!info.device || !info.clock_domain || !info.clock_epoch || m_stopping)
    {
        return;
    }
    SDevice* free = nullptr;
    for (SDevice& device : m_devices)
    {
        if (device.token && (device.info.device == info.device))
        {
            if ((device.info.clock_domain != info.clock_domain) || (device.info.clock_epoch != info.clock_epoch))
            {
                device.info = info;
                for (TInstance<CInputConnection>& owned : m_inputs)
                {
                    if (owned && (owned->snapshot().device == device.token))
                    {
                        (void)owned->assign(device.token, info.clock_domain, info.clock_epoch,
                            EControllerState::assigned, EReason::source_clock);
                    }
                }
                catalogue_changed();
            }
            return;
        }
        if (!device.token && !free) free = &device;
    }
    if (!free)
    {
        controller_overflow();
        return;
    }
    if (m_next_device == k_unassigned_device)
    {
        controller_backend(EControllerBackend::failed, UINT32_MAX);
        return;
    }
    free->info = info;
    free->info.name[sizeof(free->info.name) - 1u] = 0;
    free->token = m_next_device++;
    ++m_catalogue.count;
    catalogue_changed();
}

void CPlatformRuntime::controller_departed(const std::uint64_t native_device) noexcept
{
    for (SDevice& device : m_devices)
    {
        if (!device.token || (device.info.device != native_device))
        {
            continue;
        }
        for (TInstance<CInputConnection>& owned : m_inputs)
        {
            if (owned && (owned->snapshot().device == device.token))
            {
                const bool failed = (m_catalogue.backend == EControllerBackend::unavailable) ||
                    (m_catalogue.backend == EControllerBackend::failed);
                (void)owned->assign(0u, 0u, 0u,
                    (failed ? EControllerState::unavailable : EControllerState::disconnected),
                    (failed ? EReason::native_history : EReason::disconnected));
            }
        }
        device = {};
        --m_catalogue.count;
        catalogue_changed();
        return;
    }
}

EServiceStatus CPlatformRuntime::select_device(const std::uint64_t requested, std::uint64_t& selected,
    const CInputConnection* const owner) const noexcept
{
    selected = 0u;
    if (requested == k_unassigned_device)
    {
        return EServiceStatus::success;
    }
    if (m_catalogue.backend == EControllerBackend::initialising)
    {
        return EServiceStatus::busy;
    }
    if (m_catalogue.backend != EControllerBackend::ready)
    {
        return EServiceStatus::unavailable;
    }
    for (const SDevice& device : m_devices)
    {
        if (!device.token || (requested && (device.token != requested)))
        {
            continue;
        }
        if (!device.info.eligible)
        {
            if (requested)
            {
                return EServiceStatus::unsupported;
            }
            continue;
        }
        bool claimed = false;
        for (const TInstance<CInputConnection>& connection : m_inputs)
        {
            claimed = claimed || (connection && (&*connection != owner) && (connection->snapshot().device == device.token));
        }
        if (claimed)
        {
            if (requested) return EServiceStatus::occupied;
            continue;
        }
        if (!selected || (device.token < selected)) selected = device.token;
    }
    return (requested && !selected) ? EServiceStatus::stale : EServiceStatus::success;
}

EServiceStatus CPlatformRuntime::assign_device(CInputConnection& connection, const std::uint64_t requested) noexcept
{
    std::uint64_t selected = 0u;
    const EServiceStatus status = select_device(requested, selected, &connection);
    if (status != EServiceStatus::success) return status;
    std::uint64_t domain = 0u, epoch = 0u;
    for (const SDevice& device : m_devices)
    {
        if (selected && (device.token == selected))
        {
            domain = device.info.clock_domain;
            epoch = device.info.clock_epoch;
        }
    }
    const bool assigned = connection.assign(selected, domain, epoch,
        (selected ? EControllerState::assigned : EControllerState::unassigned), EReason::assignment);
    catalogue_changed();
    return assigned ? EServiceStatus::success : EServiceStatus::closing;
}

void CPlatformRuntime::progress_controllers(const std::uint64_t budget_ns) noexcept
{
    if (!m_controllers_started) return;
    if (m_stopping)
    {
        controller_backend(EControllerBackend::closing);
        m_controllers.work_pending = false;
        if (m_controllers.state && m_controller_adapter.destroy && !m_controller_adapter.destroy(m_controllers))
        {
            native_failure(m_controllers.error);
        }
        return;
    }
    if (!budget_ns || !m_controller_adapter.dispatch)
    {
        return;
    }
    std::uint64_t start = 0u;
    if (m_clock.read(start).code != platform::threading::EWaitError::none)
    {
        controller_backend(EControllerBackend::failed, UINT32_MAX);
        return;
    }
    m_controller_adapter.dispatch(m_controllers, (budget_ns >> 1u));
    if (m_catalogue.backend != EControllerBackend::ready)
    {
        return;
    }
    const bool eligible = m_input_focus && !m_modal_depth[0] && !m_modal_depth[1];
    const bool resumed = eligible && !m_controller_eligible;
    m_controller_eligible = eligible;
    if (!eligible) return;
    if (resumed)
    {
        for (TInstance<CInputConnection>& owned : m_inputs)
        {
            if (owned && owned->snapshot().device)
            {
                owned->source_baseline_pending = true;
                owned->source_baseline_inflight = false;
                owned->mapper().require_baseline();
                owned->ingress(k_actions);
            }
        }
    }
    constexpr std::uint32_t k_readings_per_pump = 64u;
    std::uint32_t idle = 0u;
    for (std::uint32_t work = 0u; (work < k_readings_per_pump) && (idle < k_controller_capacity); ++work)
    {
        std::uint64_t awake = 0u;
        if ((m_clock.read(awake).code != platform::threading::EWaitError::none) || (awake < start))
        {
            controller_backend(EControllerBackend::failed, UINT32_MAX);
            return;
        }
        if ((awake - start) >= budget_ns) break;
        TInstance<CInputConnection>& owned = m_inputs[1u + m_controller_turn];
        m_controller_turn = (m_controller_turn + 1u) & (k_controller_capacity - 1u);
        ++idle;
        if (!owned || !owned->snapshot().device || (owned->snapshot().phase == EPhase::closing) ||
            !owned->mapper().prepared())
        {
            continue;
        }
        CInputConnection& connection = *owned;
        if (connection.source_generation != connection.snapshot().action_generation)
        {
            connection.source_generation = connection.snapshot().action_generation;
            connection.source_baseline_pending = true;
            connection.source_baseline_inflight = false;
            connection.mapper().require_baseline();
            connection.ingress(k_actions);
        }
        if (connection.mapper().pending()) continue;
        if (connection.source_baseline_inflight)
        {
            connection.source_baseline_inflight = false;
            connection.ingress(0u);
        }
        const SDevice* selected = nullptr;
        for (const SDevice& device : m_devices)
        {
            if (device.token == connection.snapshot().device) selected = &device;
        }
        if (!selected) continue;
        native::SControllerReading reading;
        const native::EControllerRead result = m_controller_adapter.read(m_controllers, selected->info.device,
            connection.source_baseline_pending, reading);
        if (result == native::EControllerRead::empty)
        {
            continue;
        }
        idle = 0u;
        m_controllers.work_pending = true;
        if (result == native::EControllerRead::disconnected)
        {
            controller_departed(selected->info.device);
            continue;
        }
        if ((result == native::EControllerRead::history_lost) || (result == native::EControllerRead::failed))
        {
            connection.history_loss();
            connection.mapper().require_baseline();
            connection.source_baseline_pending = true;
            connection.ingress(k_actions);
            if (result == native::EControllerRead::failed)
            {
                (void)connection.assign(0u, 0u, 0u, EControllerState::unavailable, EReason::native_history);
                catalogue_changed();
            }
            continue;
        }
        if (!std::isfinite(reading.left_x) || !std::isfinite(reading.left_y) || !std::isfinite(reading.right_x) ||
            !std::isfinite(reading.right_y) || !std::isfinite(reading.left_trigger) || !std::isfinite(reading.right_trigger))
        {
            MV_LOG(warning, "Platform controller {}: nonfinite sample invalidated its assignment", selected->token);
            (void)connection.assign(0u, 0u, 0u, EControllerState::invalid_sample, EReason::native_history);
            catalogue_changed();
            continue;
        }
        SInputObservation observation;
        observation.device = selected->token;
        observation.ordinal = reading.ordinal;
        observation.time_ns = reading.time_valid ? reading.time_ns : 0u;
        observation.source_time_valid = reading.time_valid;
        observation.kind = EObservationKind::snapshot;
        observation.baseline = connection.source_baseline_pending;
        using namespace input_control;
        for (std::uint32_t code = k_first_pad_button; code <= k_last_pad_button; ++code)
        {
            observation.controls[observation.count++] = { code, ((reading.buttons & (1u << (code - k_first_pad_button))) ? 1.0f : 0.0f) };
        }
        observation.controls[observation.count++] = { k_left_stick, reading.left_x, reading.left_y };
        observation.controls[observation.count++] = { k_right_stick, reading.right_x, reading.right_y };
        observation.controls[observation.count++] = { k_left_trigger, reading.left_trigger };
        observation.controls[observation.count++] = { k_right_trigger, reading.right_trigger };
        connection.received(awake);
        if (connection.snapshot().phase == EPhase::closing)
        {
            continue;
        }
        const EInputAdmission admitted = connection.mapper().submit(observation);
        if (admitted == EInputAdmission::accepted)
        {
            if (observation.baseline)
            {
                connection.source_baseline_pending = false;
                connection.source_baseline_inflight = true;
            }
        }
        else if (admitted != EInputAdmission::closed)
        {
            //  A backwards/invalid source cannot silently reuse the old clock.
            MV_LOG(warning, "Platform controller {}: source order/clock rejected (admission {})",
                selected->token, static_cast<unsigned>(admitted));
            (void)connection.assign(0u, 0u, 0u, EControllerState::invalid_sample, EReason::source_clock);
            catalogue_changed();
        }
    }
}

}   //  namespace platform::runtime
