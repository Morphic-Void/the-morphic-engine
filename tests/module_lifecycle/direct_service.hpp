
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

#include "framework/communications/direct_client.hpp"
#include "framework/communications/module_tests.hpp"
#include "platform/threading/processor_relax.hpp"

namespace module_lifecycle_tests
{

//  Reverse conversations and conditioning are fixture contracts only.
static const connections::SMessageRule rendering_rules[]{
    { system_type_ids::run_tests_request, module_ids::executive },
    { system_type_ids::run_tests_result, module_ids::rendering },
    { system_type_ids::run_tests_request, module_ids::rendering },
    { system_type_ids::run_tests_result, module_ids::executive }
};
static const connections::SMessageRule service_rules[]{
    { system_type_ids::run_tests_request, module_ids::executive },
    { system_type_ids::run_tests_result, module_ids::conditioning_general }
};
static const connections::SContractPolicy direct_policies[]{
    { connections::contract(module_ids::executive, module_ids::rendering, 1u), rendering_rules, 4u },
    { connections::contract(module_ids::executive, module_ids::conditioning_general, 1u), service_rules, 2u },
    { connections::contract(module_ids::executive, module_ids::audio, 1u), modules::test_connections::audio_rules, 2u }
};

class SDirectService;
static thread_local SDirectService* s_direct{ nullptr };

class SDirectService
{
public:
    threading::CThreadContext& context;
    connections::CClient client;
    bool ready;

    SDirectService(threading::CThreadContext& context, const module_ids::id_type role, const bool enabled = true) noexcept
        : context{ context }, client{ context, role, direct_policies, 3u }, ready{ !enabled || client.register_service() },
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
        threading::CErasedPodMsg message;
        for (unsigned count = 0u; (count < 32u) && (m_count < 16u) && context.read(message); ++count)
        {
            if (!client.handle(message))
            {
                m_messages[m_count++] = message;
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
        if (!client.open(peer, 1u, correlation))
        {
            return false;
        }
        connections::Result result;
        std::int32_t slot = -1;
        if (!wait([&] { return client.take_result(result, slot); }) || (slot != correlation) ||
            (result.peer_role != peer) || (result.status != expected))
        {
            return false;
        }
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
            token = client.active(peer);
            return token.value && connections::was_read(client.read(token, message));
        });
    }
    bool quiesce() noexcept
    {
        for (auto token = client.next_closing(); token.value; token = client.next_closing())
        {
            if (!client.quiesce(token)) return false;
        }
        return true;
    }
    bool retire() noexcept
    {
        client.close_all();
        return wait([&] { return quiesce() && client.idle(); });
    }
private:
    SDirectService* m_previous;
    bool m_enabled;
    threading::CErasedPodMsg m_messages[16];
    unsigned m_count{ 0u };
};

static bool read_service_host(threading::CThreadContext& context, threading::CErasedPodMsg& message) noexcept
{
    return s_direct ? s_direct->read_host(message) : context.read(message);
}

}   //  namespace module_lifecycle_tests
