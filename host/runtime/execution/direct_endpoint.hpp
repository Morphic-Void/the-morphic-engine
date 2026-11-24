
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_endpoint.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Host-private direct descriptors, closed admission and in-flight access proof.

#pragma once

#include "host/runtime/execution/module_pipes.hpp"
#include "framework/communications/connection_types.hpp"
#include "foundation/threading/CNotification.hpp"
#include "foundation/threading/transports/CBulkBufferTransport.hpp"

namespace host
{

class CDirectAdmission
{
public:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    void activate() noexcept { m_state.store(0u, std::memory_order_release); }
    void close() noexcept { m_state.fetch_or(k_closed, std::memory_order_acq_rel); }
    [[nodiscard]] bool available() const noexcept
    {
        return (m_state.load(std::memory_order_acquire) & k_closed) == 0u;
    }
    [[nodiscard]] bool drained() const noexcept
    {
        return m_state.load(std::memory_order_acquire) == k_closed;
    }
    [[nodiscard]] bool acquire() noexcept
    {
        std::uint32_t state = m_state.load(std::memory_order_acquire);
        while ((state & k_closed) == 0u)
        {
            if (state == (k_closed - 1u)) return false;
            if (m_state.compare_exchange_weak(state, (state + 1u), std::memory_order_acq_rel))
            {
                return true;
            }
        }
        return false;
    }
    void release() noexcept { m_state.fetch_sub(1u, std::memory_order_release); }

private:
    static constexpr std::uint32_t k_closed = UINT32_C(1) << 31u;
    std::atomic<std::uint32_t> m_state{ k_closed };
};

class CDirectOperation
{
public:
    explicit CDirectOperation(CDirectAdmission* const admission) noexcept
        : m_admission{ (admission && admission->acquire()) ? admission : nullptr } {}
    ~CDirectOperation() noexcept { if (m_admission) m_admission->release(); }
    CDirectOperation(const CDirectOperation&) = delete;
    CDirectOperation& operator=(const CDirectOperation&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return m_admission != nullptr; }

private:
    CDirectAdmission* const m_admission;
};

struct SDirectEndpointDescriptor
{
    connections::SToken token;
    services::SClientToken local, peer;
    system_ids::id_type local_address{}, peer_address{};
    std::uint32_t size{ sizeof(SDirectEndpointDescriptor) }, version{ 1u };
    CDirectAdmission* admission{ nullptr };
    CModulePodPipe* outgoing{ nullptr };
    CModulePodPipe* incoming{ nullptr };
    threading::SNotificationReader notification;
    threading::SNotificationEndpoint peer_notification;
    connections::SBulkConfiguration bulk;
    threading::transports::CBulkBufferProducerEndpoint* bulk_producer{ nullptr };
    threading::transports::CBulkBufferConsumerEndpoint* bulk_consumer{ nullptr };
};

}   //  namespace host
