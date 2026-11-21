
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CEndpointContext.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Common nonblocking submission, consumption and closure for pumps and threads.

#pragma once

#include "framework/threading/CEndpointResources.hpp"
#include "framework/threading/messages/message_helpers.hpp"

namespace threading
{

enum class EEndpointPost : std::uint32_t { queued, busy, closed, invalid, transport_failed };
struct SEndpointPost
{
    EEndpointPost status{ EEndpointPost::invalid };
    [[nodiscard]] operator bool() const noexcept { return status == EEndpointPost::queued; }
};

//  Host also validates the named operation/resource for every settlement.
[[nodiscard]] bool endpoint_settlement(const CErasedPodMsg& message) noexcept;
[[nodiscard]] bool endpoint_settlement(const CErasedOwnerMsg& message) noexcept;

class CEndpointContext
{
public:
    explicit CEndpointContext(CEndpointResources& resources) noexcept : m_endpoint{ resources } {}
    [[nodiscard]] module_ids::id_type module_id() const noexcept { return m_endpoint.worker_module_id; }
    [[nodiscard]] thread_ids::id_type thread_id() const noexcept { return m_endpoint.thread_id; }
    [[nodiscard]] modules::SModuleInstance instance() const noexcept { return m_endpoint.instance; }
    [[nodiscard]] services::SClientToken identity() const noexcept { return m_endpoint.identity; }
    [[nodiscard]] CBatchClient* batch_client() noexcept { return m_endpoint.batch_client; }
    [[nodiscard]] const modules::SHostExecutionContext* host_execution() const noexcept { return m_endpoint.host_execution; }
    [[nodiscard]] bool exit_requested() const noexcept { return m_endpoint.control_state.exit_requested(); }
    [[nodiscard]] bool module_stopping() const noexcept
    {
        return m_endpoint.module_state && (m_endpoint.module_state->stopping.load(std::memory_order_acquire) != 0u);
    }
    void request_exit() noexcept { m_endpoint.request_exit(); }
    void request_module_stop() noexcept
    {
        if (m_endpoint.module_state) m_endpoint.module_state->stopping.store(1u, std::memory_order_release);
        (void)m_endpoint.owner_wake.signal();
    }
    void fail(const EEndpointFailure reason) noexcept { m_endpoint.fail(reason); }
    [[nodiscard]] bool read(CErasedPodMsg& message) noexcept
    {
        if (!m_endpoint.host_to_worker_msgs.read(message))
        {
            return false;
        }
        if (message.is_payload_a<EndpointSealed>()) m_endpoint.sealed = true;
        (void)m_endpoint.owner_wake.signal();
        return true;
    }
    [[nodiscard]] SEndpointPost post(const CErasedPodMsg& message) noexcept;
    [[nodiscard]] SEndpointPost post(CErasedOwnerMsg&& message) noexcept;
    [[nodiscard]] SEndpointPost seal() noexcept;
    [[nodiscard]] bool sealed() const noexcept { return m_endpoint.sealed; }
    [[nodiscard]] bool sealing() const noexcept { return m_endpoint.sealing; }
    [[nodiscard]] bool pod_submission_poisoned() const noexcept { return m_endpoint.worker_to_host_msgs.posting_poisoned(); }
    [[nodiscard]] bool has_data() noexcept { return m_endpoint.host_to_worker_msgs.refresh_readable_count() != 0u; }

private:
    [[nodiscard]] SEndpointPost publish(const CErasedPodMsg& message) noexcept;
    CEndpointResources& m_endpoint;
};

}   //  namespace threading
