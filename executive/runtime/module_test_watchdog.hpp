
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_test_watchdog.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  One Host-eligible budget per generic module test; checks never renew it.

#pragma once

#include "module/host_eligible_budget.hpp"

namespace executive
{

//  The budget type is a private test seam. Production requires the exact
//  client's Host restriction view; there is no ordinary-clock fallback.
template<typename TBudget = modules::CHostEligibleBudget>
class TModuleTestWatchdog
{
public:
    enum class EState { pending, expired, retry, failed };

    [[nodiscard]] modules::EBudgetStatus start(const modules::SHostRestrictionReader reader) noexcept
    {
        return m_budget.start(reader, 30000000000u);
    }

    [[nodiscard]] EState check(modules::SEligibleBudgetObservation* const result = nullptr) noexcept
    {
        const auto observation = m_budget.observe();
        if (result) *result = observation;
        if (observation.status == modules::EBudgetStatus::failed) return EState::failed;
        if (observation.status == modules::EBudgetStatus::expired) return EState::expired;
        if (observation.status == modules::EBudgetStatus::retry) return EState::retry;
        return EState::pending;
    }

private:
    TBudget m_budget;
};

}   //  namespace executive
