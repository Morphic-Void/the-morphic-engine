
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_connection.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Platform-owned input storage and single-producer boundary protocol.

#pragma once

#include "framework/platform/input.hpp"
#include "module/host_eligible_budget.hpp"
#include "runtime/input_mapper.hpp"

namespace platform::runtime
{

struct SInputInstallation
{
    input::SInstallationToken token;
    input::EConnection kind{ input::EConnection::desktop };
    std::uint32_t profile{ 0u };
    TPodVector<std::uint32_t> contexts, subjects;
    const SInputPlan* plan{ nullptr };
    std::uint32_t binding_begin{}, binding_count{};
    [[nodiscard]] bool contains(const std::uint32_t context) const noexcept;
};

enum class EInputPost { published, suppressed, overflow, invalid, closed, failed };

class CInputConnection final
{
public:
    explicit CInputConnection(const platform::threading::SAwakeClock clock) noexcept : budget{ clock } {}
    [[nodiscard]] bool initialise(const input::SInputEndpointV4& identity,
        const SInputInstallation& installation, const std::uint32_t context, const std::uint64_t transition) noexcept;
    [[nodiscard]] bool start(const ::threading::SNotificationEndpoint endpoint) noexcept;
    [[nodiscard]] services::EServiceStatus acknowledge(const input::SBoundaryReady& ready) noexcept;
    [[nodiscard]] services::EServiceStatus select_context(const std::uint32_t context, const std::uint64_t transition) noexcept;
    [[nodiscard]] services::EServiceStatus text_target(const input::STextTarget& target) noexcept;
    void progress(const std::uint64_t time_ns) noexcept;
    void close(const std::uint32_t failure = 0u) noexcept;
    void ingress(const std::uint32_t mask) noexcept;
    void history_loss() noexcept;
    void capture(const std::uint64_t grant, const input::ECaptureReason reason) noexcept;
    void capture_lost() noexcept;
    [[nodiscard]] bool assign(const std::uint64_t device, const std::uint64_t domain, const std::uint64_t epoch,
        const input::EControllerState state, const input::EReason reason) noexcept;
    void received(const std::uint64_t awake) noexcept { m_state.receipt_awake_ns = awake; (void)publish(); }
    [[nodiscard]] const input::SInputSnapshot& snapshot() const noexcept { return m_state; }
    [[nodiscard]] CInputMapper& mapper() noexcept { return m_mapper; }
    [[nodiscard]] const CInputMapper& mapper() const noexcept { return m_mapper; }

    input::SInputEndpointV4 view;
    input::SInstallationToken installation;
    services::SDispatchedCommand acquisition;
    services::SServiceBorrow borrow;
    services::SNotificationRegistration registration;
    ::threading::SNotificationEndpoint notification;
    modules::CHostEligibleBudget budget;
    services::EServiceQuiescence quiescence{ services::EServiceQuiescence::none };
    std::int32_t completion{ -1 };
    std::uint64_t closing_awake{ 0u };
    bool ready{ false }, retirement_started{ false }, diagnosed{ false }, clock_failed{ false }, closing_started{ false };
    std::uint64_t source_generation{};
    bool source_baseline_pending{}, source_baseline_inflight{};

private:
    friend class CInputMapper;
    friend class CDesktopInput;
    friend struct SPlatformInputTestAccess;
    friend struct SInputAcceptanceAccess;
    [[nodiscard]] EInputPost post_action(const input::SActionInputV1 record) noexcept;
    [[nodiscard]] EInputPost post_text(input::STextInputV1 record) noexcept;
    [[nodiscard]] static bool valid_text(const input::STextInputV1& record) noexcept;
    [[nodiscard]] bool publish() noexcept;
    [[nodiscard]] bool boundary(const std::uint32_t mask, const input::EReason reason) noexcept;
    [[nodiscard]] EInputPost lose(const std::uint32_t stream, const bool overflow) noexcept;
    [[nodiscard]] bool next_sequence(std::uint64_t& sequence) noexcept;
    [[nodiscard]] EInputPost post_mapped(input::SActionInputV1 record, const std::uint64_t generation) noexcept;
    CInputMapper m_mapper{ *this };
    input::SInputSnapshot m_state;
    input::CInputPublication m_publication;
    ::threading::transports::TRing<input::SActionInputV1> m_actions;
    ::threading::transports::TRing<input::STextInputV1> m_text;
    ::threading::transports::TRingConsumerEndpoint<input::SActionInputV1> m_action_reader{ m_actions };
    ::threading::transports::TRingConsumerEndpoint<input::STextInputV1> m_text_reader{ m_text };
    TPodVector<std::uint32_t> m_subjects;
    TPodVector<std::uint32_t> m_contexts;
    std::uint32_t m_markers{ 0u };
    bool m_context_pending{ true }, m_target_pending{ false };
};

}   //  namespace platform::runtime
