
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    platform_access.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    24 Nov 26
//
//  Fixture-only Platform access shared with deterministic freeze regressions.

#pragma once

#include "tests/input_host/control.hpp"
#include "platform/runtime/platform_runtime.hpp"

namespace platform::runtime
{
struct SInputAcceptanceAccess
{
    static services::CProviderClient& services(CPlatformRuntime& runtime) noexcept { return runtime.m_services; }
    static std::int32_t reserve(CPlatformRuntime& runtime, const services::ServiceRequest& request) noexcept
    { return runtime.reserve(request, true); }
    static void complete(CPlatformRuntime& runtime, const std::int32_t slot, const services::EServiceStatus status) noexcept
    { runtime.complete(slot, status); }
    static const CInputConnection* connection(const CPlatformRuntime& runtime, const std::uint32_t index) noexcept
    { return runtime.m_inputs[index] ? &*runtime.m_inputs[index] : nullptr; }
    static bool idle(const CPlatformRuntime& runtime) noexcept
    {
        const CDesktopInput& desktop = runtime.m_desktop;
        if (desktop.m_count || desktop.m_baseline_pending || desktop.m_baseline_inflight || desktop.m_cutover || desktop.m_losing)
        {
            return false;
        }
        for (const TInstance<CInputConnection>& connection : runtime.m_inputs)
        {
            if (connection && (connection->mapper().pending() || connection->source_baseline_pending ||
                connection->source_baseline_inflight || connection->snapshot().pending || connection->snapshot().ingress_pending))
            {
                return false;
            }
        }
        return true;
    }
    static bool history_runnable(const CPlatformRuntime& runtime, const std::uint64_t instance) noexcept
    {
        if (!runtime.m_input_focus || runtime.m_modal_depth[0] || runtime.m_modal_depth[1] || runtime.m_stopping)
        {
            return false;
        }
        for (const CPlatformRuntime::SDevice& device : runtime.m_devices)
        {
            if (!device.token || (device.info.device != instance))
            {
                continue;
            }
            for (const TInstance<CInputConnection>& current : runtime.m_inputs)
            {
                if (current && (current->snapshot().device == device.token) &&
                    (current->snapshot().phase != input::EPhase::closing))
                {
                    return true;
                }
            }
        }
        return false;
    }
    static std::uint32_t occupancy(const CInputConnection& connection, const std::uint32_t stream) noexcept
    {
        return (stream == input::k_actions) ? (input::k_action_capacity - connection.m_actions.writable_count()) :
            (connection.view.text ? (input::k_text_capacity - connection.m_text.writable_count()) : 0u);
    }
    static bool freeze_idle(const CPlatformRuntime& runtime, input_host::SFreezeProgress& progress, const bool native) noexcept
    {
        const CDesktopInput& desktop = runtime.m_desktop;
        const bool desktop_busy = desktop.m_count || desktop.m_baseline_pending || desktop.m_baseline_inflight ||
            desktop.m_cutover || desktop.m_losing;
        std::uint32_t mappers = 0u, baselines = 0u, boundaries = 0u, ingress = 0u, actions = 0u, text = 0u;
        for (std::uint32_t index = 0u; index < input_host::k_pipe_count; ++index)
        {
            const CInputConnection* const current = connection(runtime, index);
            if (!current) continue;
            const std::uint32_t bit = 1u << index;
            if (current->mapper().pending()) mappers |= bit;
            if (current->source_baseline_pending || current->source_baseline_inflight) baselines |= bit;
            if (current->snapshot().pending) boundaries |= bit;
            if (current->snapshot().ingress_pending) ingress |= bit;
            if (occupancy(*current, input::k_actions)) actions |= bit;
            if (occupancy(*current, input::k_text)) text |= bit;
        }
        // A native observation can finish while focus is elsewhere. Its trace
        // freeze does not certify native quiet or retire the production cutover.
        const CInputConnection* const sampled = connection(runtime, 0u);
        const bool dormant = native && !runtime.cancelling() && !runtime.m_desktop_eligible && sampled &&
            (desktop.m_connection == sampled) && (sampled->snapshot().phase == input::EPhase::active) &&
            !sampled->snapshot().failure && desktop.m_cutover &&
            (sampled->snapshot().ingress_pending == desktop.m_cutover) &&
            !desktop.m_count && !desktop.m_baseline_pending && !desktop.m_baseline_inflight &&
            !desktop.m_losing && !desktop.runnable();
        progress.eligible.store(runtime.m_desktop_eligible, std::memory_order_relaxed);
        progress.cutover.store(desktop.m_cutover, std::memory_order_relaxed);
        progress.desktop_queued.store(desktop.m_count, std::memory_order_relaxed);
        progress.desktop_baseline.store((desktop.m_baseline_pending || desktop.m_baseline_inflight), std::memory_order_relaxed);
        progress.desktop_loss.store(desktop.m_losing, std::memory_order_relaxed);
        progress.desktop_runnable.store(desktop.runnable(), std::memory_order_relaxed);
        progress.desktop.store(desktop_busy, std::memory_order_relaxed);
        progress.mappers.store(mappers, std::memory_order_relaxed);
        progress.baselines.store(baselines, std::memory_order_relaxed);
        progress.boundaries.store(boundaries, std::memory_order_relaxed);
        progress.ingress.store(ingress, std::memory_order_relaxed);
        progress.actions.store(actions, std::memory_order_relaxed);
        progress.text.store(text, std::memory_order_relaxed);
        const std::uint32_t blocking_ingress = dormant ? (ingress & ~UINT32_C(1)) : ingress;
        return (!desktop_busy || dormant) && !(mappers | baselines | boundaries | blocking_ingress | actions | text);
    }
#if defined(MV_INPUT_ACCEPTANCE)
    static acceptance::SObservationTrace origin(const CInputConnection& connection) noexcept
    {
        const CInputMapper& mapper = connection.m_mapper;
        const bool admitted = (mapper.m_observation.kind == runtime::EObservationKind::text) ? mapper.m_text_admitted : mapper.m_admitted;
        return (admitted && !mapper.m_selective && !mapper.m_observation.baseline) ?
            mapper.m_observation.trace : acceptance::SObservationTrace{};
    }
#endif
};
}
