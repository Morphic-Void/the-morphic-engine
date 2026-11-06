
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

namespace platform::runtime
{

struct SInputInstallation
{
    input::SInstallationToken token;
    input::EConnection kind{ input::EConnection::desktop };
    std::uint32_t profile{ 0u };
    TPodVector<std::uint32_t> contexts, subjects;
    [[nodiscard]] bool contains(const std::uint32_t context) const noexcept;
};

enum class EInputPost { published, suppressed, overflow, invalid, closed, failed };

class CInputConnection final
{
public:
    explicit CInputConnection(const platform::threading::SAwakeClock clock) noexcept : budget{ clock } {}
    [[nodiscard]] bool initialise(const input::SInputEndpointV1& identity,
        const SInputInstallation& installation, const std::uint32_t context, const std::uint64_t transition) noexcept;
    [[nodiscard]] bool start(const ::threading::SNotificationEndpoint endpoint) noexcept;
    [[nodiscard]] services::EServiceStatus acknowledge(const input::SBoundaryReady& ready) noexcept;
    [[nodiscard]] services::EServiceStatus select_context(const std::uint32_t context, const std::uint64_t transition) noexcept;
    [[nodiscard]] services::EServiceStatus text_target(const input::STextTarget& target) noexcept;
    void progress(const std::uint64_t time_ns) noexcept;
    void close(const std::uint32_t failure = 0u) noexcept;
    //  Native mapping must keep physical neutrality current even during barriers.
    void observe_subject(const std::uint32_t subject, const bool neutral) noexcept;
    void observe_neutral(const std::uint32_t subject) noexcept { observe_subject(subject, true); }
    [[nodiscard]] EInputPost post_action(input::SActionInputV1 record) noexcept;
    [[nodiscard]] EInputPost post_text(input::STextInputV1 record) noexcept;
    [[nodiscard]] const input::SInputSnapshot& snapshot() const noexcept { return m_state; }

    input::SInputEndpointV1 view;
    services::SDispatchedCommand acquisition;
    services::SServiceBorrow borrow;
    services::SNotificationRegistration registration;
    ::threading::SNotificationEndpoint notification;
    modules::CHostEligibleBudget budget;
    services::EServiceQuiescence quiescence{ services::EServiceQuiescence::none };
    std::int32_t completion{ -1 };
    std::uint64_t closing_awake{ 0u };
    bool ready{ false }, retirement_started{ false }, diagnosed{ false }, clock_failed{ false }, closing_started{ false };

private:
    friend struct SPlatformInputTestAccess;
    struct SNeutralGate { std::uint32_t subject{ 0u }; std::uint64_t generation{ 0u }; bool neutral{ false }; };
    [[nodiscard]] bool publish() noexcept;
    [[nodiscard]] bool boundary(const std::uint32_t mask, const input::EReason reason) noexcept;
    [[nodiscard]] EInputPost lose(const std::uint32_t stream, const bool overflow) noexcept;
    [[nodiscard]] bool next_sequence(std::uint64_t& sequence) noexcept;
    input::SInputSnapshot m_state;
    input::CInputPublication m_publication;
    ::threading::transports::TRing<input::SActionInputV1> m_actions;
    ::threading::transports::TRing<input::STextInputV1> m_text;
    ::threading::transports::TRingConsumerEndpoint<input::SActionInputV1> m_action_reader{ m_actions };
    ::threading::transports::TRingConsumerEndpoint<input::STextInputV1> m_text_reader{ m_text };
    TPodVector<SNeutralGate> m_gates;
    TPodVector<std::uint32_t> m_contexts;
    std::uint32_t m_markers{ 0u };
    bool m_context_pending{ true }, m_target_pending{ false };
};

}   //  namespace platform::runtime
