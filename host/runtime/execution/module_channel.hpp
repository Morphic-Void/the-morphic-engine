
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_channel.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Private module communication storage with explicit SPSC roles and wakeups.

#pragma once

#include "host/runtime/execution/module_lifecycle.hpp"
#include "framework/threading/CEndpointResources.hpp"

namespace host
{

class CModuleChannel
{
public:
    explicit CModuleChannel(threading::CEndpointResources& resources) noexcept
        : to_module{ resources.worker_module_id }, to_host{ resources.owner_module_id }, m_resources{ resources } {}

    [[nodiscard]] bool initialise(memory::CMemoryContext& context,
        const std::uint32_t ordinary_capacity = k_module_pod_capacity,
        const std::uint32_t lifecycle_capacity = k_module_lifecycle_capacity) noexcept
    {
        return to_module.initialise(context, ordinary_capacity) && to_host.initialise(context, ordinary_capacity) &&
            lifecycle.initialise(context, lifecycle_capacity);
    }

    //  Host main is the sole caller of these operations. The preamble performs
    //  their opposite roles even for a pumped endpoint on the same native thread.
    [[nodiscard]] bool host_control(const SLifecycleMessage& message) noexcept
    {
        if (!lifecycle.to_preamble.post(message))
        {
            return false;
        }
        m_resources.signal_consumer();
        return true;
    }
    [[nodiscard]] bool read_host_control(SLifecycleMessage& message) noexcept
    {
        if (!lifecycle.to_host.read(message)) return false;
        m_resources.signal_consumer();
        return true;
    }
    [[nodiscard]] bool read_host_pod(threading::CErasedPodMsg& message) noexcept
    {
        if (!to_host.read(message)) return false;
        m_resources.signal_consumer();
        return true;
    }

    [[nodiscard]] bool preamble_control(const SLifecycleMessage& message) noexcept
    {
        if (!lifecycle.to_host.post(message)) return false;
        (void)signal_host();
        return true;
    }
    [[nodiscard]] bool read_preamble_control(SLifecycleMessage& message) noexcept
    {
        if (!lifecycle.to_preamble.read(message))
        {
            return false;
        }
        (void)signal_host();
        return true;
    }
    [[nodiscard]] bool read_preamble_pod(threading::CErasedPodMsg& message) noexcept
    {
        if (!to_module.read(message)) return false;
        (void)signal_host();
        return true;
    }
    [[nodiscard]] bool signal_host() noexcept
    {
        if (m_resources.owner_wake.signal()) return true;
        m_resources.fail(threading::EEndpointFailure::wake_failed);
        return false;
    }

    CModulePodPipe to_module, to_host;
    SModuleLifecycle lifecycle;

private:
    threading::CEndpointResources& m_resources;
};

}   //  namespace host
