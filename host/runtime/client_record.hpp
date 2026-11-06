
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    client_record.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Host-owned exact-instance identity and bounded reply storage, independent of threads.

#pragma once

#include <atomic>
#include "services/service_protocol.hpp"
#include "module/module_identity.hpp"
#include "threading/CThreadPackage.hpp"

namespace host
{

class CHostMaintenance;

enum class EClientExecution : std::uint8_t { passive = 0u, threaded, pumped };
enum class EClientPhase : std::uint8_t { starting = 0u, ready, stopping, stopped, retired };
enum class EDirectConnectionState : std::uint8_t { unregistered = 0u, available, closing };
enum class EReplyPublication : std::uint8_t { queued = 0u, cancelled, busy };

struct SClientRecord
{
    static constexpr std::uint32_t k_reply_capacity = 512u;
    static constexpr std::uint32_t k_control_capacity = 16u;

    SClientRecord(const module_ids::id_type module, const std::uint64_t token,
        threading::CThreadPackage* const package = nullptr) noexcept;

    //  Identity and execution state.
    [[nodiscard]] module_ids::id_type module_id() const noexcept;
    void request_exit() noexcept;
    [[nodiscard]] threading::CBatchClient* batch_client() const noexcept;
    [[nodiscard]] threading::EThreadRunState query_state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] bool delivery_protected() const noexcept { return invocation_offered || (prefix_failed && !terminal()); }

    //  Reply admission and retirement.
    [[nodiscard]] bool reserve_replies(const std::uint32_t count) noexcept;
    [[nodiscard]] bool reserve_control_replies(const std::uint32_t count) noexcept;
    [[nodiscard]] bool replies_idle() const noexcept;
    void cancel_reservation(const std::uint32_t count) noexcept;
    void cancel_control_reservation(const std::uint32_t count) noexcept;

    //  Control storage and its credits are independent of ordinary replies.
    [[nodiscard]] bool post(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool post_reply(const threading::CErasedPodMsg& message) noexcept;

    //  A terminal recipient cancels delivery without redirecting to a successor.
    [[nodiscard]] bool reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool reply_control(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool retry_reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] EReplyPublication publish_reply(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool execution_quiescent() const noexcept;
    [[nodiscard]] bool install_host_execution(const modules::SHostRestrictionReader reader) noexcept;
    [[nodiscard]] bool signal_restriction(const std::uint32_t revision) noexcept;
    [[nodiscard]] bool retire_host_execution(const bool deactivated) noexcept;

    //  Inbox delivery and consumed prefixes.
    void flush() noexcept;
    [[nodiscard]] std::uint32_t copy_messages(threading::CErasedPodMsg* const output, const std::uint32_t capacity, std::uint32_t& controls) const noexcept;
    void consume_messages(const std::uint32_t count, const std::uint32_t offered_controls) noexcept;
    void consume(const std::uint32_t count) noexcept;

    const modules::SModuleInstance instance;
    const services::SClientToken identity;
    const module_ids::id_type module;
    threading::CThreadPackage* thread{ nullptr };
    EClientExecution execution{ EClientExecution::passive };
    EClientPhase phase{ EClientPhase::starting };
    bool stop_requested{ false };
    EDirectConnectionState direct_connections{ EDirectConnectionState::unregistered };
    bool incoming_drained{ false };
    bool terminal_before_drain{ false };
    std::atomic<std::uint32_t> notifications_pending{ 0u };
    std::atomic<std::uint32_t> host_stop_requested{ 0u };
    platform::threading::SWakeEndpoint owner_wake;
    modules::SHostExecutionContext host_execution;
    threading::CNotification restriction_notification;
    std::uint32_t restriction_revision{ 0u };
    std::uint32_t deferred_records{ 0u };
    std::uint32_t staged_replies{ 0u }, staged_controls{ 0u };
    CHostMaintenance* delivery_owner{ nullptr };
    bool subscription_live{ false };
    bool invocation_offered{ false };
    bool prefix_failed{ false };
    std::uint32_t reserved_replies{ 0u };
    std::uint32_t message_count{ 0u };
    std::uint32_t reserved_control_replies{ 0u };
    std::uint32_t control_count{ 0u };
    threading::CErasedPodMsg messages[k_reply_capacity];
    threading::CErasedPodMsg control_messages[k_control_capacity];
};

}   //  namespace host
