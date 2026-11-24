
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    endpoint_fixture.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    20 Nov 26
//
//  Test readers collect actual FIFO deliveries for assertions across Host turns.

#pragma once

#include "host/runtime/services/service_router.hpp"
#include <vector>

namespace tests
{

class CEndpointProbe
{
public:
    explicit CEndpointProbe(host::SClientRecord& client) noexcept : m_client{ client } {}
    [[nodiscard]] std::uint32_t pending() const
    {
        threading::CErasedPodMsg message;
        while (m_client.channel->read_preamble_pod(message)) m_received.push_back(message);
        return static_cast<std::uint32_t>(m_received.size());
    }
    [[nodiscard]] const threading::CErasedPodMsg& received(const std::uint32_t index) const
    {
        (void)pending();
        return (index < m_received.size()) ? m_received[index] : m_empty;
    }
    void discard(const std::uint32_t count)
    {
        (void)pending();
        MV_ASSERT(count <= m_received.size());
        m_received.erase(m_received.begin(), (m_received.begin() + count));
    }
private:
    host::SClientRecord& m_client;
    mutable std::vector<threading::CErasedPodMsg> m_received;
    threading::CErasedPodMsg m_empty;
};

// Controlled Host-service tests own both SPSC roles sequentially. No module
// callback is activated; execution and affine retirement have separate fixtures.
struct SCommonEndpointFixture : host::SClientRecord
{
    SCommonEndpointFixture(const module_ids::id_type module, const std::uint64_t identity,
        const std::uint32_t capacity = host::k_module_pod_capacity) noexcept
        : SClientRecord{ module, identity, nullptr, { identity + UINT64_C(0x100000000) } }, inbox{ *this }
    {
        const bool waiting = m_wake.initialise().code == platform::threading::EWaitError::none;
        owner_wake = m_wake.endpoint();
        const bool initialised = waiting && initialise_execution(m_binding, {}, nullptr, capacity);
        MV_ASSERT(initialised);
        (void)initialised;
    }
    [[nodiscard]] std::uint32_t pending() const { return inbox.pending(); }
    [[nodiscard]] const threading::CErasedPodMsg& received(const std::uint32_t index) const { return inbox.received(index); }
    void discard(const std::uint32_t count) { inbox.discard(count); }
    CEndpointProbe inbox;
private:
    platform::threading::COwnerWait m_wake;
    modules::CBoundModule m_binding;
};

// Supplied-facade adapter for controlled domain fixtures. Authentication and
// execution affinity are exercised through actual common-preamble fixtures.
[[nodiscard]] inline modules::ESubmission submit_common_pod(host::SClientRecord& client,
    const threading::CErasedPodMsg& message) noexcept
{
    if (client.terminal()) return modules::ESubmission::closed;
    switch (client.channel->to_host.post(message))
    {
        case host::EModulePipePost::published:
        {
            ++client.endpoint->published_pods;
            return client.channel->signal_host() ? modules::ESubmission::accepted : modules::ESubmission::published_wake_failed;
        }
        case host::EModulePipePost::full:
        {
            return modules::ESubmission::busy;
        }
        case host::EModulePipePost::invalid:
        {
            return modules::ESubmission::invalid;
        }
        case host::EModulePipePost::not_ready:
        {
            return modules::ESubmission::transport_failed;
        }
    }
    return modules::ESubmission::transport_failed;
}

inline void progress_service_outputs(host::CServiceRouter& router, host::SClientRecord& client,
    const std::uint32_t allowance = 64u) noexcept
{
    client.flush_replies();
    for (std::uint32_t count = 0u; count < allowance; ++count)
    {
        if (!client.output_ready()) break;
        const bool borrow = router.borrow_output_ready(client);
        if (borrow) router.retry_borrow_output(client);
        const bool operation = client.output_ready() && router.output_ready(client);
        if (operation) router.retry_output(client);
        if (!borrow && !operation) break;
    }
}

//  The provider consumes notices from its ordinary FIFO. Copying the immutable
//  descriptor here models a provider retaining its own accepted operation state.
[[nodiscard]] inline std::uint32_t receive_commands(host::CServiceRouter& router, host::SClientRecord& provider,
    services::SDispatchedCommand* const commands, const std::uint32_t capacity) noexcept
{
    router.progress();
    std::uint32_t count = 0u;
    threading::CErasedPodMsg message;
    while (count < capacity)
    {
        if (provider.output_ready() && router.output_ready(provider)) router.retry_output(provider);
        if (!provider.channel->read_preamble_pod(message)) break;
        services::ServiceCommand notice;
        const bool decoded = message.copy_payload_to(notice) && notice.descriptor && notice.operation;
        MV_ASSERT(decoded);
        if (!decoded) break;
        commands[count++] = *notice.descriptor;
    }
    return count;
}

}   //  namespace tests
