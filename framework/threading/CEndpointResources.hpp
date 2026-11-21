
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CEndpointResources.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Host-owned endpoint storage retained independently of module execution.

#pragma once

#include "framework/module/host_execution_context.hpp"
#include "framework/threading/CBatchWork.hpp"
#include "framework/threading/endpoint_messages.hpp"
#include "framework/threading/messages/CErasedMessageTransports.hpp"
#include "foundation/containers/TInstance.hpp"

namespace threading
{

enum class EEndpointFailure : std::uint32_t
{
    none, transport_poisoned, publication_overflow, accounting_failure, execution_failed, lifecycle_failure, wake_failed
};

struct SModuleEndpointState
{
    modules::SModuleInstance instance;
    std::atomic<std::uint32_t> stopping{ 0u };
};

class CEndpointResources
{
public:
    CEndpointResources(const module_ids::id_type owner, const module_ids::id_type module,
        const thread_ids::id_type thread) noexcept
        : owner_module_id{ owner }, worker_module_id{ module }, thread_id{ thread }
        , host_to_worker_msgs{ module }, worker_to_host_msgs{ owner }, worker_to_host_owned_msgs{ owner, nullptr }
    {}

    [[nodiscard]] bool initialise() noexcept
    {
        return host_to_worker_msgs.initialise_growable(0u) && worker_to_host_msgs.initialise_growable(0u) &&
            worker_to_host_owned_msgs.initialise(0u);
    }
    void fail(const EEndpointFailure reason) noexcept
    {
        EEndpointFailure expected = EEndpointFailure::none;
        (void)failure.compare_exchange_strong(expected, reason, std::memory_order_acq_rel);
        (void)owner_wake.signal();
    }
    void signal_consumer() noexcept
    {
        work_pending.store(1u, std::memory_order_release);
        if (wait_target)
        {
            if (!wait_target->poke_epoch_and_wake_one()) fail(EEndpointFailure::wake_failed);
        }
        else (void)owner_wake.signal();
    }
    void request_exit() noexcept
    {
        control_state.request_exit();
        signal_consumer();
    }
    [[nodiscard]] bool pin_notification(CNotification& notification) noexcept
    {
        if (notification_pins == UINT32_MAX) return false;
        if (!(wait_target ? notification.initialise(*wait_target) : notification.initialise(work_pending, owner_wake)))
        {
            return false;
        }
        ++notification_pins;
        return true;
    }
    void unpin_notification() noexcept
    {
        MV_ASSERT(notification_pins != 0u);
        if (notification_pins != 0u) --notification_pins;
    }

    const module_ids::id_type owner_module_id;
    const module_ids::id_type worker_module_id;
    const thread_ids::id_type thread_id;
    modules::SModuleInstance instance;
    services::SClientToken identity;
    SModuleEndpointState* module_state{ nullptr };
    transports::CErasedPodMsgTransport host_to_worker_msgs;
    transports::CErasedPodMsgTransport worker_to_host_msgs;
    transports::CErasedOwnerMsgTransport worker_to_host_owned_msgs;
    CThreadControlState control_state;
    CBatchClient* batch_client{ nullptr };
    TInstance<CBatchClient> batch_owner;
    const modules::SHostExecutionContext* host_execution{ nullptr };
    platform::threading::SWakeEndpoint owner_wake;
    CWaitPredicate* wait_target{ nullptr };
    std::atomic<std::uint32_t> work_pending{ 0u };
    std::atomic<EEndpointFailure> failure{ EEndpointFailure::none };
    std::uint32_t notification_pins{ 0u }; //  Host owned; all publishers relinquish before the final unpin.

    //  Producer-owned publication boundaries. Host learns them through SealEndpoint.
    std::uint64_t published_pods{ 0u }, published_owners{ 0u };
    SealEndpoint seal;
    bool sealing{ false }, seal_posted{ false }, sealed{ false };
};

}   //  namespace threading
