
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

#include "host/runtime/service_router.hpp"
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
        while (m_client.endpoint->host_to_worker_msgs.read(message)) m_received.push_back(message);
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

struct SEndpointFixture : host::SClientRecord
{
    SEndpointFixture(const module_ids::id_type module, const std::uint64_t identity,
        threading::CThreadPackage* const package = nullptr) noexcept
        : SClientRecord{ module, identity, package, { identity + UINT64_C(0x100000000) } }, inbox{ *this }
    {
        const bool initialised = initialise();
        MV_ASSERT(initialised);
        (void)initialised;
    }
    [[nodiscard]] std::uint32_t pending() const { return inbox.pending(); }
    [[nodiscard]] const threading::CErasedPodMsg& received(const std::uint32_t index) const { return inbox.received(index); }
    void discard(const std::uint32_t count) { inbox.discard(count); }
    CEndpointProbe inbox;
};

//  The provider consumes notices from its ordinary FIFO. Copying the immutable
//  descriptor here models a provider retaining its own accepted operation state.
[[nodiscard]] inline std::uint32_t receive_commands(host::CServiceRouter& router, host::SClientRecord& provider,
    services::SDispatchedCommand* const commands, const std::uint32_t capacity) noexcept
{
    router.progress();
    std::uint32_t count = 0u;
    threading::CErasedPodMsg message;
    while ((count < capacity) && provider.endpoint->host_to_worker_msgs.read(message))
    {
        services::ServiceCommand notice;
        const bool decoded = message.copy_payload_to(notice) && notice.descriptor && notice.operation;
        MV_ASSERT(decoded);
        if (!decoded) break;
        commands[count++] = *notice.descriptor;
    }
    return count;
}

}   //  namespace tests
