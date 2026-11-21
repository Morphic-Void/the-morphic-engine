
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    client_record.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Exact endpoint identity, publication accounting and independent retirement.

#pragma once

#include "host/threading/CThreadPackage.hpp"
#include "framework/threading/CEndpointContext.hpp"

namespace host
{

enum class EClientPhase : std::uint8_t { starting, ready, stopping, stopped, retired };
enum class EDirectConnectionState : std::uint8_t { unregistered, available, closing };
enum class EReplyPublication : std::uint8_t { queued, cancelled, busy };

[[nodiscard]] std::uint32_t client_reply_count(const threading::CErasedPodMsg& message) noexcept;

struct SClientRecord
{
    SClientRecord(const module_ids::id_type module, const std::uint64_t token,
        threading::CThreadPackage* const package, const modules::SModuleInstance instance) noexcept;
    [[nodiscard]] bool initialise() noexcept;
    [[nodiscard]] module_ids::id_type module_id() const noexcept { return module; }
    void request_exit() noexcept;
    [[nodiscard]] threading::CBatchClient* batch_client() const noexcept { return endpoint->batch_client; }
    [[nodiscard]] threading::EThreadRunState query_state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] bool delivery_protected() const noexcept { return invocation_active; }
    [[nodiscard]] bool reserve_replies(const std::uint32_t count) noexcept;
    [[nodiscard]] bool replies_idle() const noexcept { return (reserved_replies == 0u) && (deferred_records == 0u); }
    void cancel_reservation(const std::uint32_t count) noexcept;
    [[nodiscard]] bool post(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool retry_reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] EReplyPublication publish_reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool read(threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool capture(threading::CErasedOwnerMsg& message) noexcept;
    void accounted_pod() noexcept;
    void accounted_owner() noexcept;
    [[nodiscard]] bool execution_quiescent() const noexcept;
    [[nodiscard]] bool install_host_execution(const modules::SHostRestrictionReader reader) noexcept;
    [[nodiscard]] bool signal_restriction(const std::uint32_t revision) noexcept;
    [[nodiscard]] bool retire_host_execution(const bool deactivated) noexcept;
    void flush() noexcept;

    const modules::SModuleInstance instance;
    const services::SClientToken identity;
    const module_ids::id_type module;
    threading::CThreadPackage* thread{ nullptr };
    threading::CEndpointResources pump_resources;
    threading::CEndpointResources* endpoint;
    EClientPhase phase{ EClientPhase::starting };
    bool stop_requested{ false };
    EDirectConnectionState direct_connections{ EDirectConnectionState::unregistered };
    bool incoming_drained{ false }, terminal_before_drain{ false };
    std::atomic<std::uint32_t>& notifications_pending;
    std::atomic<std::uint32_t> host_stop_requested{ 0u };
    platform::threading::SWakeEndpoint owner_wake;
    modules::SHostExecutionContext host_execution;
    threading::CNotification restriction_notification;
    std::uint32_t restriction_revision{ 0u }, deferred_records{ 0u }, reserved_replies{ 0u };
    std::uint64_t last_schema_request{ 0u }, last_independent_owner{ 0u };
    bool subscription_live{ false }, invocation_active{ false };
    bool provider_closed{ false }, admission_notice_pending{ false }, seal_pending{ false }, seal_acknowledged{ false };
    std::int32_t admission_slot{ -1 };
    threading::SealEndpoint seal_boundary;
    std::uint64_t consumed_pods{ 0u }, consumed_owners{ 0u }, accounted_pods{ 0u }, accounted_owners{ 0u };
};

}   //  namespace host
