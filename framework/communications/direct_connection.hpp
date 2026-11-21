
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_connection.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Host-mediated endpoint connections, authenticated sources and access admission.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/module/module_identity.hpp"
#include "framework/services/service_protocol.hpp"
#include "system/system_type_registration.hpp"
#include "threading/CNotification.hpp"
#include "framework/threading/messages/CErasedMessageTransports.hpp"
#include "threading/transports/CBulkBufferTransport.hpp"

namespace connections
{

struct SToken { std::uint64_t value{ 0u }; };
enum class EStatus : std::uint32_t
{
    success, invalid_request, unavailable, unsupported, version_mismatch, rejected,
    busy, already_connected, closing, cancelled, timed_out, allocation_failed,
    transport_failed, notification_failed, configuration_mismatch
};

struct SContract
{
    module_ids::id_type first{}, second{};
    std::uint32_t version{ 0u };
};

[[nodiscard]] inline SContract contract(const module_ids::id_type a,
    const module_ids::id_type b, const std::uint32_t version) noexcept
{
    return (a.raw_value() < b.raw_value()) ? SContract{ a, b, version } : SContract{ b, a, version };
}

//  Zero capacity and no producer select the existing bidirectional POD transport.
//  Bulk capacity is the usable byte count of each of three 128-byte-aligned slots.
struct SBulkConfiguration
{
    std::uint64_t capacity{ 0u };
    module_ids::id_type producer{};

    [[nodiscard]] bool matches(const SBulkConfiguration& other) const noexcept
    {
        return (capacity == other.capacity) && (producer == other.producer);
    }
    [[nodiscard]] bool is_bulk() const noexcept { return capacity != 0u; }
};

//  Bulk retains its designated service and role contract. POD has no role policy.
struct SBulkPolicy
{
    SContract identity;
    SBulkConfiguration bulk;
};

//  Closing and operation acquisition modify the same word. The guard remains
//  held through signalling, and zero users never substitutes for quiescence.
class CAdmission
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

class COperation
{
public:
    explicit COperation(CAdmission* const admission) noexcept
        : m_admission{ (admission && admission->acquire()) ? admission : nullptr } {}
    ~COperation() noexcept { if (m_admission) m_admission->release(); }
    COperation(const COperation&) = delete;
    COperation& operator=(const COperation&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return m_admission != nullptr; }

private:
    CAdmission* m_admission;
};

struct SSource
{
    enum class EKind : std::uint8_t { host, direct };
    EKind kind{ EKind::host };
    system_ids::id_type address{ system_ids::host };
    services::SClientToken endpoint;
    SToken connection;
    [[nodiscard]] bool is_host() const noexcept { return kind == EKind::host; }
};

struct SReceived
{
    threading::CErasedPodMsg message;
    SSource source;
};

//  Host retains this descriptor, both transports and both notification targets
//  until the two participants have discharged their endpoint access obligations.
struct SEndpointDescriptor
{
    SToken token;
    services::SClientToken local, peer;
    system_ids::id_type local_address{}, peer_address{};
    std::uint32_t size{ sizeof(SEndpointDescriptor) }, version{ 1u };
    CAdmission* admission{ nullptr };
    threading::transports::CErasedPodMsgTransport* outgoing{ nullptr };
    threading::transports::CErasedPodMsgTransport* incoming{ nullptr };
    threading::SNotificationReader notification;
    threading::SNotificationEndpoint peer_notification;
    SBulkConfiguration bulk;
    threading::transports::CBulkBufferProducerEndpoint* bulk_producer{ nullptr };
    threading::transports::CBulkBufferConsumerEndpoint* bulk_consumer{ nullptr };
};

//  All these messages use the ordinary Host POD pipes. Request correlation uses
//  their carrier's async_slot and the authenticated originating client instance.
struct Register { std::uint32_t version{ 1u }; };
struct Registered { EStatus status{ EStatus::invalid_request }; };
struct Request
{
    system_ids::id_type first{}, second{};
    module_ids::id_type bulk_peer{};
    std::uint32_t version{ 1u };
    SBulkConfiguration bulk;
};
struct Offer
{
    SToken token;
    services::SClientToken peer;
    system_ids::id_type peer_address{};
    std::uint32_t version{ 0u };
    SBulkConfiguration bulk;
};
struct Decision { SToken token; EStatus status{ EStatus::rejected }; };
struct Install { SToken token; const SEndpointDescriptor* descriptor{ nullptr }; };
struct Ready { SToken token; };
struct Activate { SToken token; };
struct Close { SToken token; EStatus reason{ EStatus::cancelled }; };
struct Closing { SToken token; EStatus reason{ EStatus::cancelled }; };
struct Quiescent { SToken token; };
struct Retired { SToken token; };
struct Result
{
    SToken token;
    services::SClientToken first, second;
    EStatus status{ EStatus::invalid_request };
};

enum class EPost : std::uint8_t
{
    queued, full, no_pipe, invalid_message, transport_failed, queued_wake_failed
};
enum class ERead : std::uint8_t { record, empty, inactive, invalid_message, record_wake_failed };
enum class EBulkPublish : std::uint8_t { published, inactive, published_wake_failed };

static_assert(sizeof(Request) <= 48u);
static_assert(sizeof(Offer) <= 48u);
static_assert(sizeof(Result) <= 48u);

[[nodiscard]] inline bool was_queued(const EPost result) noexcept
{
    return (result == EPost::queued) || (result == EPost::queued_wake_failed);
}
[[nodiscard]] inline bool was_read(const ERead result) noexcept
{
    return (result == ERead::record) || (result == ERead::record_wake_failed);
}

}   //  namespace connections

MV_REGISTER_SYSTEM_TYPE(connections::Register, system_type_ids::direct_register);
MV_REGISTER_SYSTEM_TYPE(connections::Registered, system_type_ids::direct_registered);
MV_REGISTER_SYSTEM_TYPE(connections::Request, system_type_ids::direct_request);
MV_REGISTER_SYSTEM_TYPE(connections::Offer, system_type_ids::direct_offer);
MV_REGISTER_SYSTEM_TYPE(connections::Decision, system_type_ids::direct_decision);
MV_REGISTER_SYSTEM_TYPE(connections::Install, system_type_ids::direct_install);
MV_REGISTER_SYSTEM_TYPE(connections::Ready, system_type_ids::direct_ready);
MV_REGISTER_SYSTEM_TYPE(connections::Activate, system_type_ids::direct_activate);
MV_REGISTER_SYSTEM_TYPE(connections::Close, system_type_ids::direct_close);
MV_REGISTER_SYSTEM_TYPE(connections::Closing, system_type_ids::direct_closing);
MV_REGISTER_SYSTEM_TYPE(connections::Quiescent, system_type_ids::direct_quiescent);
MV_REGISTER_SYSTEM_TYPE(connections::Retired, system_type_ids::direct_retired);
MV_REGISTER_SYSTEM_TYPE(connections::Result, system_type_ids::direct_result);

namespace connections
{
[[nodiscard]] inline bool zero_reply_control(const threading::CErasedPodMsg& message) noexcept
{
    return message.is_payload_a<Decision>() ||
        message.is_payload_a<Ready>() || message.is_payload_a<Close>() || message.is_payload_a<Quiescent>();
}
}   //  namespace connections
