
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
#include "host/runtime/execution/module_preamble.hpp"
#include "host/runtime/execution/retained_replies.hpp"
#include "host/runtime/execution/output_retry_turn.hpp"

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
    [[nodiscard]] bool initialise_execution(modules::CBoundModule& binding, const modules::SExecutionV1& execution,
        void* const creation_context = nullptr, const std::uint32_t ordinary_capacity = k_module_pod_capacity,
        const std::uint32_t fallback_limit = k_module_fifo_limit) noexcept;
    [[nodiscard]] module_ids::id_type module_id() const noexcept { return module; }
    void request_exit() noexcept;
    [[nodiscard]] threading::CBatchClient* batch_client() const noexcept { return endpoint->batch_client; }
    [[nodiscard]] threading::EThreadRunState query_state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] bool delivery_protected() const noexcept { return invocation_active; }
    [[nodiscard]] bool reserve_replies(const std::uint32_t count) noexcept;
    [[nodiscard]] bool replies_idle() const noexcept
    {
        return (reserved_replies == 0u) && (!pending_replies || pending_replies->idle());
    }
    void cancel_reservation(const std::uint32_t count) noexcept;
    [[nodiscard]] bool post(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool retry_reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] EReplyPublication publish_reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool output_ready() const noexcept;
    void flush_replies() noexcept;
    void retain_control(const SLifecycleMessage& message) noexcept;
    void flush_controls() noexcept;
    [[nodiscard]] bool read(threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool capture(threading::CErasedOwnerMsg& message) noexcept;
    [[nodiscard]] bool capture_pod_head() noexcept;
    [[nodiscard]] bool capture_owner_head() noexcept;
    void accounted_pod() noexcept;
    void accounted_owner() noexcept;
    [[nodiscard]] bool execution_quiescent() const noexcept;
    //  Whole-process retention only. Never authorises endpoint retirement,
    //  acquisition reclamation, notification unpinning or DLL unload.
    [[nodiscard]] bool process_retention_eligible() const noexcept;
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
    TInstance<CModuleChannel> channel;
    TInstance<CRetainedReplies> pending_replies;
    TInstance<CModulePreamble> preamble;
    COutputRetryTurn output_turn;
    CPressureDiagnostic pressure, pod_starvation, owner_starvation;
    std::int32_t output_cursors[COutputRetryTurn::k_producers];
    threading::CErasedPodMsg pod_head;
    threading::CErasedOwnerMsg owner_head;
    bool pod_head_live{ false }, owner_head_live{ false };
    bool pod_admission_failed{ false }, owner_admission_failed{ false }, admission_served{ false };
    //  One terminal outcome per accepted facade request, plus participation.
    //  These are independent transitions, not another ordinary message queue.
    struct SControlOutcome
    {
        SLifecycleMessage message;
        bool pending{ false };
    };
    SControlOutcome control_outcomes[65];
    bool close_frontier_pending{ false }, admission_frontier_fixed{ false };
    SPublicationFrontier close_frontier;
    EClientPhase phase{ EClientPhase::starting };
    bool stop_requested{ false };
    EDirectConnectionState direct_connections{ EDirectConnectionState::unregistered };
    bool incoming_drained{ false }, terminal_before_drain{ false };
    std::atomic<std::uint32_t>& notifications_pending;
    std::atomic<std::uint32_t> host_stop_requested{ 0u };
    platform::threading::SWakeEndpoint owner_wake;
    modules::SHostExecutionContext host_execution;
    threading::CNotification restriction_notification;
    std::uint32_t restriction_revision{ 0u }, reserved_replies{ 0u };
    std::uint64_t last_schema_request{ 0u }, last_independent_pod{ 0u }, last_independent_owner{ 0u };
    bool subscription_live{ false }, invocation_active{ false };
    bool destroy_refusal_unwound{ false };
    bool provider_closed{ false }, admission_notice_pending{ false }, seal_pending{ false }, seal_acknowledged{ false };
    SPublicationFrontier seal_boundary;
    std::uint64_t consumed_pods{ 0u }, consumed_owners{ 0u }, accounted_pods{ 0u }, accounted_owners{ 0u };
};

}   //  namespace host
