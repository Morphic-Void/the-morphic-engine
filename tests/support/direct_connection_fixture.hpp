
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_connection_fixture.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    05 Nov 26
//
//  Production connection controls with a test Host and synchronous service peers.

#pragma once

#if !defined(MV_HOST_RUNTIME_TESTS)
#error This fixture requires the MorphicTests Host runtime seams.
#endif

#include "framework/system/system_ids.hpp"
#include "framework/communications/direct_client.hpp"
#include "host/runtime/direct_connections.hpp"
#include "host/runtime/host_restrictions.hpp"

namespace threading
{
struct SDirectConnectionTestAccess
{
    static CThreadResources& resources(CThreadPackage& package) { return package.m_resources; }
    static bool initialise(CThreadPackage& package, const modules::SHostExecutionContext& execution)
    {
        auto& resources = package.m_resources;
        if (!resources.host_to_worker_msgs.initialise_growable(0u) || !resources.worker_to_host_msgs.initialise_growable(0u) ||
            !resources.wait_predicate.acquire_control())
        {
            return false;
        }
        package.m_created = true;
        resources.host_execution = &execution;
        resources.control_state.mark_running();
        return true;
    }
    static std::uint32_t pins(const CThreadPackage& package) { return package.m_notification_pins; }
    static void fail_join(CThreadPackage& package, const bool fail) { package.m_test_fail_join = fail; }
};
}   //  namespace threading

namespace host
{
struct SDirectConnectionTestAccess
{
    static void limit(CDirectConnections& connections, const connections::SToken token, const std::uint32_t capacity = 32u)
    {
        auto* record = connections.find(token);
        if (!record) return;
        record->forward.deallocate();
        record->reverse.deallocate();
        (void)record->forward.initialise_growable(capacity, capacity);
        (void)record->reverse.initialise_growable(capacity, capacity);
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
    static void limit_installing(CDirectConnections& connections, const connections::SToken token, const std::uint32_t capacity)
    {
        const auto* record = connections.find(token);
        if (record && (record->phase == CDirectConnections::EPhase::installing) &&
            !record->sides[0].exposed && !record->sides[1].exposed)
        {
            limit(connections, token, capacity);
        }
    }
};
}   //  namespace host

namespace tests
{

struct SDirectHostFixture
{
    host::CHostRestrictions restrictions;
    host::CDirectConnections connections;
    host::SClientRecord* clients[4]{};
    threading::CErasedPodMsg deferred[4];
    bool deferred_pending[4]{};
    bool failed{ false };
    std::uint32_t data_capacity{ 0u };

    bool attach(host::SClientRecord& client) noexcept
    {
        if (!client.thread) return false;
        for (const auto* slot : clients)
        {
            if (slot && ((slot == &client) || (slot->module == client.module) || (slot->instance.value == client.instance.value)))
            {
                return false;
            }
        }
        for (auto*& slot : clients)
        {
            if (!slot)
            {
                slot = &client;
                client.phase = host::EClientPhase::ready;
                return true;
            }
        }
        return false;
    }
    bool detach(const host::SClientRecord& client) noexcept
    {
        if (!connections.client_idle(client)) return false;
        for (unsigned index = 0u; index < 4u; ++index)
        {
            if (clients[index] == &client)
            {
                if (deferred_pending[index]) return false;
                clients[index] = nullptr;
                return true;
            }
        }
        return false;
    }
    host::SClientRecord* peer(const module_ids::id_type role) noexcept
    {
        for (auto* client : clients)
        {
            if (client && (client->module == role))
            {
                return client;
            }
        }
        return nullptr;
    }
    bool dispatch(host::SClientRecord& source, const threading::CErasedPodMsg& message) noexcept
    {
        connections::Register registration;
        connections::Request request;
        connections::Decision decision;
        connections::Ready ready;
        connections::Close close;
        connections::Quiescent quiescent;
        if (message.copy_payload_to(registration))
        {
            connections.register_client(source, registration);
        }
        else if (message.copy_payload_to(request))
        {
            if (!source.reserve_replies(1u)) failed = true;
            else
            {
                connections.request(source, peer(request.peer), request, message.query_async_slot(), restrictions.reader());
            }
        }
        else if (message.copy_payload_to(decision))
        {
            connections.decision(source, decision);
            //  Cap before descriptors/activation can escape to native peers.
            if (data_capacity)
            {
                host::SDirectConnectionTestAccess::limit_installing(connections, decision.token, data_capacity);
            }
        }
        else if (message.copy_payload_to(ready))
        {
            connections.ready(source, ready);
        }
        else if (message.copy_payload_to(close))
        {
            connections.close(source, close);
        }
        else if (message.copy_payload_to(quiescent))
        {
            connections.quiescent(source, quiescent);
        }
        else return false;
        return true;
    }
    //  Leave owning messages and every unrelated POD under the caller's control.
    bool read_other(host::SClientRecord& source, threading::CErasedPodMsg& message) noexcept
    {
        for (unsigned index = 0u; index < 4u; ++index)
        {
            if ((clients[index] == &source) && deferred_pending[index])
            {
                message = deferred[index];
                deferred_pending[index] = false;
                return true;
            }
        }
        while (source.thread->read(message))
        {
            if (!dispatch(source, message))
            {
                return true;
            }
        }
        return false;
    }
    void drain_controls(host::SClientRecord& source) noexcept
    {
        threading::CErasedPodMsg message;
        if (!read_other(source, message)) return;
        failed = true;
        for (unsigned index = 0u; index < 4u; ++index)
        {
            if (clients[index] == &source)
            {
                deferred[index] = message;
                deferred_pending[index] = true;
                return;
            }
        }
    }
    void progress() noexcept
    {
        connections.progress();
        failed = failed || connections.failed();
    }
};

//  These peers use production queues but no native execution. Native join proof
//  remains covered separately by the real-thread retirement tests.
struct SDirectPeerFixture
{
    SDirectHostFixture& host;
    threading::CThreadPackage package;
    host::SClientRecord record;
    threading::CThreadContext context;
    connections::CClient client;
    bool ready{ false };
    bool attached{ false };
    threading::CErasedPodMsg other;
    bool other_pending{ false };

    SDirectPeerFixture(SDirectHostFixture& driver, const platform::system::CPerfCountConversion& conversion,
        const module_ids::id_type role, const std::uint64_t instance,
        const connections::SContractPolicy* const policies, const std::uint32_t policy_count) noexcept
        : host{ driver }, package{ { thread_ids::executive, role }, conversion }, record{ role, instance, &package },
          context{ threading::SDirectConnectionTestAccess::resources(package) }, client{ context, role, policies, policy_count }
    {
        record.host_execution.instance = record.instance;
        if (!threading::SDirectConnectionTestAccess::initialise(package, record.host_execution))
        {
            return;
        }
        attached = host.attach(record);
        ready = attached && client.register_service();
        if (ready) host.drain_controls(record);
    }
    void pump(const bool quiesce = true) noexcept
    {
        host.drain_controls(record);
        threading::CErasedPodMsg message;
        while (!other_pending && context.read(message))
        {
            if (!client.handle(message))
            {
                other = message;
                other_pending = true;
                host.failed = true;
            }
        }
        if (quiesce)
        {
            for (auto token = client.next_closing(); token.value; token = client.next_closing())
            {
                if (!client.quiesce(token)) break;
            }
        }
        client.progress();
        host.failed = host.failed || client.failed();
    }
    bool shutdown() noexcept
    {
        if (attached) pump();
        if (!host.connections.client_idle(record) || !client.idle() || other_pending)
        {
            return false;
        }
        context.mark_exited();
        if (!package.shutdown() && package.retains_execution())
        {
            return false;
        }
        if (attached && !host.detach(record))
        {
            return false;
        }
        attached = false;
        return true;
    }
};

}   //  namespace tests
