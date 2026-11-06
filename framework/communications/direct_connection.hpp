
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    direct_connection.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    05 Nov 26
//
//  Host-mediated lifetime controls for direct service-thread POD connections.

#pragma once

#include "module/module_identity.hpp"
#include "system/system_type_registration.hpp"
#include "threading/CNotification.hpp"
#include "threading/messages/CErasedMessageTransports.hpp"

namespace connections
{

struct SToken { std::uint64_t value{ 0u }; };
enum class EStatus : std::uint32_t
{
    success, invalid_request, unavailable, unsupported, version_mismatch, rejected,
    busy, already_connected, closing, cancelled, timed_out, allocation_failed,
    transport_failed, notification_failed
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

struct SMessageRule
{
    system_type_id type;
    module_ids::id_type source;
};

//  Immutable policy belongs to the participating module, never a Host callback.
struct SContractPolicy
{
    SContract identity;
    const SMessageRule* messages{ nullptr };
    std::uint32_t count{ 0u };

    [[nodiscard]] bool admits(const threading::CErasedPodMsg& message,
        const module_ids::id_type source) const noexcept
    {
        system_type_id type;
        if (!message.query_message_type_id().try_system_type_id(type))
        {
            return false;
        }
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            if ((messages[index].type == type) && (messages[index].source == source))
            {
                return true;
            }
        }
        return false;
    }
};

//  Host retains this descriptor, both transports and both notification targets
//  until the two participants have discharged their endpoint access obligations.
struct SEndpointDescriptor
{
    SToken token;
    modules::SModuleInstance local, peer;
    module_ids::id_type local_role{}, peer_role{};
    std::uint32_t version{ 0u };
    threading::transports::CErasedPodMsgTransport* outgoing{ nullptr };
    threading::transports::CErasedPodMsgTransport* incoming{ nullptr };
    threading::SNotificationReader notification;
    threading::SNotificationEndpoint peer_notification;
};

//  All these messages use the ordinary Host POD pipes. Request correlation uses
//  their carrier's async_slot and the authenticated originating client instance.
struct Register { std::uint32_t version{ 1u }; };
struct Request { module_ids::id_type peer{}; std::uint32_t version{ 0u }; };
struct Offer
{
    SToken token;
    modules::SModuleInstance peer;
    module_ids::id_type peer_role{};
    std::uint32_t version{ 0u };
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
    modules::SModuleInstance peer;
    module_ids::id_type peer_role{};
    EStatus status{ EStatus::invalid_request };
};

enum class EPost : std::uint8_t
{
    queued, full, inactive, invalid_message, transport_failed, queued_wake_failed
};
enum class ERead : std::uint8_t { record, empty, inactive, invalid_message, record_wake_failed };

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
    return message.is_payload_a<Register>() || message.is_payload_a<Decision>() ||
        message.is_payload_a<Ready>() || message.is_payload_a<Close>() || message.is_payload_a<Quiescent>();
}
}   //  namespace connections
