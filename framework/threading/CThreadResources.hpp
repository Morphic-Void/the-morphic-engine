
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CThreadResources.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Host-owned shared worker resources without native thread ownership.

#pragma once

#ifndef CTHREADRESOURCES_HPP_INCLUDED
#define CTHREADRESOURCES_HPP_INCLUDED

#include "platform/system/performance_counter.hpp"
#include "foundation/system/system_ids.hpp"
#include "framework/threading/CBatchWork.hpp"
#include "threading/CParkingGate.hpp"
#include "threading/CThreadControlState.hpp"
#include "threading/CWaitPredicate.hpp"
#include "framework/module/host_execution_context.hpp"
#include "framework/threading/messages/CErasedMessageTransports.hpp"

namespace threading
{

class CThreadResources
{
public:
    CThreadResources(const module_ids::id_type owner_module_id,
        const module_ids::id_type worker_module_id, const thread_ids::id_type thread_id,
        const platform::system::CPerfCountConversion& perf_count_conversion) noexcept;
    ~CThreadResources() noexcept = default;

    //  Assigned identities and conversion; native launch configuration is Host-only.
    const module_ids::id_type owner_module_id;
    const module_ids::id_type worker_module_id;
    const thread_ids::id_type thread_id;
    const platform::system::CPerfCountConversion perf_count_conversion;

    //  Common resources available to the thread package and context.
    CParkingTicket parking_ticket;
    CWaitPredicate wait_predicate;
    transports::CErasedPodMsgTransport host_to_worker_msgs;
    transports::CErasedPodMsgTransport worker_to_host_msgs;
    transports::CErasedOwnerMsgTransport worker_to_host_owned_msgs;
    CThreadControlState control_state;
    CBatchClient* batch_client{ nullptr };
    const modules::SHostExecutionContext* host_execution{ nullptr };
    platform::threading::SWakeEndpoint owner_wake;
};

inline CThreadResources::CThreadResources(
    const module_ids::id_type owner_module_id, const module_ids::id_type worker_module_id,
    const thread_ids::id_type thread_id,
    const platform::system::CPerfCountConversion& perf_count_conversion) noexcept
    : owner_module_id{ owner_module_id }
    , worker_module_id{ worker_module_id }
    , thread_id{ thread_id }
    , perf_count_conversion{ perf_count_conversion }
    , host_to_worker_msgs{ worker_module_id }
    , worker_to_host_msgs{ owner_module_id }
    , worker_to_host_owned_msgs{ owner_module_id, nullptr }
{
}

}   //  namespace threading

#endif  //  #ifndef CTHREADRESOURCES_HPP_INCLUDED
