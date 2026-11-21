
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
#include "framework/threading/CEndpointResources.hpp"
#include "threading/CParkingGate.hpp"
#include "threading/CThreadControlState.hpp"
#include "threading/CWaitPredicate.hpp"
#include "framework/module/host_execution_context.hpp"
#include "framework/threading/messages/CErasedMessageTransports.hpp"

namespace threading
{

class CThreadResources : public CEndpointResources
{
public:
    CThreadResources(const module_ids::id_type owner_module_id,
        const module_ids::id_type worker_module_id, const thread_ids::id_type thread_id,
        const platform::system::CPerfCountConversion& perf_count_conversion) noexcept;
    ~CThreadResources() noexcept = default;

    //  Assigned identities and conversion; native launch configuration is Host-only.
    const platform::system::CPerfCountConversion perf_count_conversion;

    //  Common resources available to the thread package and context.
    CParkingTicket parking_ticket;
    CWaitPredicate wait_predicate;
};

inline CThreadResources::CThreadResources(
    const module_ids::id_type owner_module_id, const module_ids::id_type worker_module_id,
    const thread_ids::id_type thread_id,
    const platform::system::CPerfCountConversion& perf_count_conversion) noexcept
    : CEndpointResources{ owner_module_id, worker_module_id, thread_id }
    , perf_count_conversion{ perf_count_conversion }
{
}

}   //  namespace threading

#endif  //  #ifndef CTHREADRESOURCES_HPP_INCLUDED
