
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_service.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    05 Nov 26
//
//  DLL-local connection participant for lifecycle fixtures; no Host internals.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/communications/direct_client.hpp"
#include "framework/communications/module_tests.hpp"
#include "framework/threading/CThreadContext.hpp"
#include "platform/threading/processor_relax.hpp"

namespace module_lifecycle_tests
{

//  Fixture service addresses are application knowledge, not transport role selection.
static system_ids::id_type direct_address(const module_ids::id_type role) noexcept
{
    return (role == module_ids::conditioning_general) ? system_ids::ops::make_system_id(role, thread_ids::simulation) :
        modules::test_connections::address(role);
}
class SDirectService;
static thread_local SDirectService* s_direct{ nullptr };

class SDirectService
{
public:
    threading::CThreadContext& context;
    connections::CClient client;
    bool ready;

    SDirectService(threading::CThreadContext& context, const module_ids::id_type, const bool enabled = true) noexcept
        : context{ context }, client{ context }, ready{ !enabled || client.enable() },
          m_previous{ s_direct }, m_enabled{ enabled }
    {
        if (enabled) s_direct = this;
    }
    ~SDirectService() noexcept
    {
        if (m_enabled)
        {
            s_direct = m_previous;
        }
    }

    bool pump() noexcept
    {
        if (!m_enabled) return true;
        client.clear_notifications();
        connections::SReceived received;
        for (unsigned count = 0u; (count < 32u) && (m_count < 16u) && client.receive(received, (m_direct_count < 16u)); ++count)
        {
            connections::Offer offer;
            if (!received.source.is_host())
            {
                m_direct[m_direct_count++] = received;
            }
            else if (received.message.copy_payload_to(offer))
            {
                m_connection = offer.token;
                (void)client.decide(offer.token, connections::EStatus::success);
            }
            else if (received.message.is_payload_a<connections::Result>())
            {
                m_result = received.message;
                m_result_pending = true;
            }
            else if (!connections::is_control(received.message))
            {
                m_messages[m_count++] = received.message;
            }
        }
        client.progress();
        context.advance_heartbeat();
        return !client.failed();
    }
    bool read_host(threading::CErasedPodMsg& message) noexcept
    {
        if (!m_enabled) return context.read(message);
        if (!pump() || !m_count)
        {
            return false;
        }
        message = m_messages[0];
        --m_count;
        for (unsigned index = 0u; index < m_count; ++index)
        {
            m_messages[index] = m_messages[index + 1u];
        }
        return true;
    }
    template<typename Predicate> bool wait(const Predicate predicate) noexcept
    {
        threading::CWaitDeadline deadline;
        if (deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
        {
            return false;
        }
        for (;;)
        {
            if (!pump()) return false;
            if (predicate()) return true;
            std::uint64_t remaining = 0u;
            if ((deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
            {
                return false;
            }
            platform::threading::processor_relax();
        }
    }
    bool open(const module_ids::id_type peer, const std::int32_t correlation,
        const connections::EStatus expected, connections::SToken& token) noexcept
    {
        if (!client.open(direct_address(peer), correlation))
        {
            return false;
        }
        connections::Result result;
        if (!wait([&] { return m_result_pending; }) || !m_result.copy_payload_to(result) ||
            (m_result.query_async_slot() != correlation) ||
            (result.status != expected))
        {
            return false;
        }
        m_result_pending = false;
        token = result.token;
        return true;
    }
    bool post(const connections::SToken token, const threading::CErasedPodMsg& message) noexcept
    {
        connections::EPost result = connections::EPost::full;
        return wait([&]
        {
            result = client.post(token, message);
            return result != connections::EPost::full;
        }) &&
            (result == connections::EPost::queued);
    }
    bool receive(const module_ids::id_type peer, connections::SToken& token, threading::CErasedPodMsg& message) noexcept
    {
        return wait([&] {
            token = client.active(direct_address(peer));
            for (unsigned index = 0u; index < m_direct_count; ++index)
            {
                if ((m_direct[index].source.connection.value != token.value) ||
                    (m_direct[index].source.address != direct_address(peer)))
                {
                    continue;
                }
                message = m_direct[index].message;
                --m_direct_count;
                for (unsigned next = index; next < m_direct_count; ++next) m_direct[next] = m_direct[next + 1u];
                return true;
            }
            return false;
        });
    }
    bool quiesce() noexcept
    {
        for (connections::SToken token = client.next_closing(); token.value; token = client.next_closing())
        {
            unsigned retained = 0u;
            for (unsigned index = 0u; index < m_direct_count; ++index)
            {
                if (m_direct[index].source.connection.value != token.value)
                {
                    m_direct[retained++] = m_direct[index];
                }
            }
            m_direct_count = retained;
            if (!client.quiesce(token)) return false;
        }
        return true;
    }
    unsigned buffered_direct() const noexcept { return m_direct_count; }
    bool retire() noexcept
    {
        //  Fixtures run one conversation at a time and may establish another
        //  after replacement. Retire that token without shutting the helper down.
        client.close(m_connection);
        return wait([&] { return quiesce() && client.idle(); });
    }
private:
    SDirectService* m_previous;
    bool m_enabled;
    threading::CErasedPodMsg m_messages[16];
    connections::SReceived m_direct[16];
    unsigned m_count{ 0u };
    unsigned m_direct_count{ 0u };
    threading::CErasedPodMsg m_result;
    bool m_result_pending{ false };
    connections::SToken m_connection;
};

static bool read_service_host(threading::CThreadContext& context, threading::CErasedPodMsg& message) noexcept
{
    return s_direct ? s_direct->read_host(message) : context.read(message);
}

}   //  namespace module_lifecycle_tests
