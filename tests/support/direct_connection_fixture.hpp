
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
#include "framework/communications/module_tests.hpp"
#include "framework/threading/CThreadContext.hpp"
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
        resources.wait_target = &resources.wait_predicate;
        resources.control_state.mark_running();
        return true;
    }
    static std::uint32_t pins(const CThreadPackage& package) { return package.m_resources.notification_pins; }
    static void end_execution(CThreadPackage& package)
    {
        package.m_resources.control_state.mark_exited();
        package.m_returned.store(true, std::memory_order_release);
    }
    static void fail_join(CThreadPackage& package, const bool fail) { package.m_test_fail_join = fail; }
};
}   //  namespace threading

namespace host
{
struct SDirectConnectionTestAccess
{
    static const connections::SEndpointDescriptor* descriptor(CDirectConnections& connections,
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

//  Observations copied by fixture dispatchers from the common receiver. They
//  impose no additional consumption obligation on the production helper.
struct SDirectResultObservations
{
    threading::CErasedPodMsg messages[32];
    unsigned count{ 0u };
    bool observe(const threading::CErasedPodMsg& message) noexcept
    {
        if (!message.is_payload_a<connections::Result>()) return false;
        MV_ASSERT(count < 32u);
        if (count < 32u) messages[count++] = message;
        return true;
    }
    bool read(connections::Result& result, std::int32_t& slot) noexcept
    {
        if (!count) return false;
        slot = messages[0].query_async_slot();
        const bool copied = messages[0].copy_payload_to(result);
        --count;
        for (unsigned index = 0u; index < count; ++index) messages[index] = messages[index + 1u];
        return copied;
    }
};

struct SDirectHostFixture
{
    host::CHostRestrictions restrictions;
    host::CDirectConnections connections;
    static constexpr unsigned capacity = 16u;
    host::SClientRecord* clients[capacity]{};
    threading::CErasedPodMsg deferred[capacity];
    bool deferred_pending[capacity]{};
    bool failed{ false };
    std::uint32_t data_capacity{ 0u };

    bool attach(host::SClientRecord& client) noexcept
    {
        for (const auto* slot : clients)
        {
            if (slot && ((slot == &client) || (slot->identity.value == client.identity.value) ||
                ((slot->module == client.module) && (slot->endpoint->thread_id == client.endpoint->thread_id))))
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
                return client.initialise();
            }
        }
        return false;
    }
    bool detach(const host::SClientRecord& client) noexcept
    {
        if (!connections.client_idle(client)) return false;
        for (unsigned index = 0u; index < capacity; ++index)
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
    host::SClientRecord* endpoint(const system_ids::id_type address) noexcept
    {
        for (host::SClientRecord* client : clients)
        {
            if (client && (system_ids::ops::make_system_id(client->module, client->endpoint->thread_id) == address))
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
            if (!source.reserve_replies(1u)) failed = true;
            else connections.register_client(source, registration, message.query_async_slot());
        }
        else if (message.is_payload_a<threading::EndpointReady>())
        {
            if (source.direct_connections == host::EDirectConnectionState::unregistered) failed = true;
        }
        else if (message.copy_payload_to(request))
        {
            if (!source.reserve_replies(1u)) failed = true;
            else
            {
                connections.request(source, (request.bulk.is_bulk() ? &source : endpoint(request.first)),
                    (request.bulk.is_bulk() ? peer(request.bulk_peer) : endpoint(request.second)),
                    request, message.query_async_slot(), restrictions.reader());
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
        for (unsigned index = 0u; index < capacity; ++index)
        {
            if ((clients[index] == &source) && deferred_pending[index])
            {
                message = deferred[index];
                deferred_pending[index] = false;
                return true;
            }
        }
        while (source.read(message))
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
        for (unsigned index = 0u; index < capacity; ++index)
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
    SDirectResultObservations results;
    bool ready{ false };
    bool attached{ false };
    connections::SReceived other;
    bool other_pending{ false };

    SDirectPeerFixture(SDirectHostFixture& driver, const platform::system::CPerfCountConversion& conversion,
        const module_ids::id_type role, const std::uint64_t instance) noexcept
        : host{ driver }, package{ { system_ids::ops::get_thread_id(modules::test_connections::address(role)), role }, conversion },
          record{ role, instance, &package, { instance + UINT64_C(0x100000000) } },
          context{ threading::SDirectConnectionTestAccess::resources(package) }, client{ context }
    {
        record.host_execution.instance = record.instance;
        if (!threading::SDirectConnectionTestAccess::initialise(package, record.host_execution))
        {
            return;
        }
        attached = host.attach(record);
        ready = attached && client.enable();
        if (ready) host.drain_controls(record);
    }
    void pump(const bool quiesce = true) noexcept
    {
        host.drain_controls(record);
        if (other_pending && !other.source.is_host() && !client.active(other.source.connection)) other_pending = false;
        connections::SReceived received;
        while (!other_pending && client.receive(received))
        {
            if (received.source.is_host() && results.observe(received.message)) continue;
            connections::Offer offer;
            if (received.source.is_host() && received.message.copy_payload_to(offer) && !offer.bulk.is_bulk())
            {
                (void)client.decide(offer.token, connections::EStatus::success);
            }
            else if (!received.source.is_host() || !connections::is_control(received.message))
            {
                other = received;
                other_pending = true;
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
    bool read(const connections::SToken token, threading::CErasedPodMsg& message) noexcept
    {
        pump();
        if (!other_pending || other.source.is_host() || (other.source.connection.value != token.value))
        {
            return false;
        }
        message = other.message;
        other_pending = false;
        return true;
    }
    bool shutdown() noexcept
    {
        if (attached) pump();
        if (!host.connections.client_idle(record) || !client.idle() || other_pending)
        {
            return false;
        }
        threading::SDirectConnectionTestAccess::end_execution(package);
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
