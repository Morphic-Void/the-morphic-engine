
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_connection_fixture.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    05 Nov 26
//
//  Host-private connection seams and controlled Foundation worker resources.

#pragma once

#if !defined(MV_HOST_RUNTIME_TESTS)
#error This fixture requires the MorphicTests Host runtime seams.
#endif

#include "framework/system/system_ids.hpp"
#include "host/runtime/execution/direct_connections.hpp"
#include "host/runtime/host_restrictions.hpp"

namespace threading
{
struct SDirectConnectionTestAccess
{
    static CThreadResources& resources(CThreadPackage& package) { return package.m_resources; }
    static bool initialise_worker(CThreadPackage& package, const modules::SHostExecutionContext& execution)
    {
        auto& resources = package.m_resources;
        if (!resources.host_to_worker_msgs.initialise_growable(0u) || !resources.worker_to_host_msgs.initialise_growable(0u) ||
            !resources.wait_predicate.acquire_control())
        {
            return false;
        }
        package.m_created = true;
        resources.host_execution = &execution;
        resources.wait_target = &resources.wait_predicate;
        resources.control_state.mark_running();
        return true;
    }
    static void end_execution(CThreadPackage& package)
    {
        package.m_resources.control_state.mark_exited();
        package.m_returned.store(true, std::memory_order_release);
    }
    static void fail_join(CThreadPackage& package, const bool fail) { package.m_test_fail_join = fail; }
    static void reject_create(CThreadPackage& package) { package.m_test_reject_create = true; }
    // Sequential interleaving fixtures drive process() themselves. No native
    // execution is created, and these cases do not qualify native affinity.
    static void prepare_controlled(CThreadPackage& package)
    {
        package.m_preparation.store(1u, std::memory_order_release);
        package.m_resources.control_state.mark_running();
    }
};
}   //  namespace threading

namespace host
{
struct SDirectConnectionTestAccess
{
    static SDirectEndpointDescriptor* private_descriptor(CDirectConnections& connections,
        const connections::SToken token, const std::uint32_t side)
    {
        CDirectConnections::SRecord* const record = connections.find(token);
        return (record && (side < 2u)) ? &record->sides[side].descriptor : nullptr;
    }
    static bool contains(CDirectConnections& connections, const connections::SToken token)
    {
        return connections.find(token) != nullptr;
    }
    static std::uint32_t requests(const CDirectConnections& connections) { return connections.m_request_count; }
    static std::size_t bulk_capacity(CDirectConnections& connections, const connections::SToken token)
    {
        const CDirectConnections::SRecord* const record = connections.find(token);
        return record ? record->bulk.capacity() : 0u;
    }
    static threading::SNotificationEndpoint notification(CDirectConnections& connections, const connections::SToken token)
    {
        auto* record = connections.find(token);
        return record ? record->sides[1].notification.endpoint() : threading::SNotificationEndpoint{};
    }
    static void expire(CDirectConnections& connections, const connections::SToken token, const modules::SHostRestrictionReader reader)
    {
        if (auto* const record = connections.find(token))
        {
            (void)record->budget.start(reader, 0u);
        }
    }
};
}   //  namespace host
