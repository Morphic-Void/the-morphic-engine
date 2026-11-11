
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_eligible_budget.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  One elapsed-time budget excluding the union of Host restrictions.

#pragma once

#include "framework/module/host_restriction.hpp"

namespace modules
{

enum class EBudgetStatus : std::uint32_t { running, restricted, expired, retry, failed };
struct SEligibleBudgetObservation
{
    EBudgetStatus status{ EBudgetStatus::failed };
    std::uint64_t remaining_ns{ 0u };
    platform::threading::WaitError error{ platform::threading::EWaitError::invalid_argument, 0u };
};

class CHostEligibleBudget
{
public:
    explicit CHostEligibleBudget(const platform::threading::SAwakeClock clock = {}) noexcept : m_clock{ clock } {}
    [[nodiscard]] EBudgetStatus start(const SHostRestrictionReader reader, const std::uint64_t duration_ns) noexcept;
    [[nodiscard]] SEligibleBudgetObservation observe() noexcept;

private:
    [[nodiscard]] EBudgetStatus sample(std::uint64_t& eligible, std::uint64_t& generation, std::uint64_t& awake,
        platform::threading::WaitError& error) const noexcept;
    platform::threading::SAwakeClock m_clock;
    SHostRestrictionReader m_reader;
    std::uint64_t m_generation{ 0u }, m_start{ 0u }, m_last{ 0u }, m_duration{ 0u };
    std::uint64_t m_last_awake{ 0u };
    EBudgetStatus m_status{ EBudgetStatus::failed };
    platform::threading::WaitError m_error{ platform::threading::EWaitError::invalid_argument, 0u };
};

}   //  namespace modules
