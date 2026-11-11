
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_eligible_budget.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26

#include "framework/module/host_eligible_budget.hpp"
#include <limits>

namespace modules
{

EBudgetStatus CHostEligibleBudget::sample(std::uint64_t& eligible, std::uint64_t& generation, std::uint64_t& awake, platform::threading::WaitError& error) const noexcept
{
    using platform::threading::EWaitError;
    for (std::uint32_t attempt = 0u; attempt < 4u; ++attempt)
    {
        SHostRestrictionSnapshot before, after;
        const auto first = m_reader.read(before);
        if (first == ERestrictionRead::failed)
        {
            error = before.error.code == EWaitError::none ? platform::threading::WaitError{ EWaitError::invalid_argument, 0u } : before.error;
            return EBudgetStatus::failed;
        }
        if (first == ERestrictionRead::retry) continue;
        error = m_clock.read(awake);
        if (error.code != EWaitError::none) return EBudgetStatus::failed;
        if ((m_reader.read(after) != ERestrictionRead::success) ||
            (before.revision != after.revision) || (before.generation != after.generation)) continue;
        if ((awake < before.completed_ns) || (before.depth &&
            ((awake < before.active_start_ns) || (before.active_start_ns < before.completed_ns))))
        {
            error = { EWaitError::overflow, 0u };
            return EBudgetStatus::failed;
        }
        //  Subtract in this order to avoid overflowing completed + active duration.
        eligible = (before.depth ? before.active_start_ns : awake) - before.completed_ns;
        generation = before.generation;
        return before.depth ? EBudgetStatus::restricted : EBudgetStatus::running;
    }
    return EBudgetStatus::retry;
}

EBudgetStatus CHostEligibleBudget::start(const SHostRestrictionReader reader, const std::uint64_t duration_ns) noexcept
{
    using platform::threading::EWaitError;
    m_status = EBudgetStatus::failed;
    m_error = { EWaitError::invalid_argument, 0u };
    m_reader = reader;
    if (duration_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        m_error.code = EWaitError::overflow;
        return m_status;
    }
    const auto status = sample(m_start, m_generation, m_last_awake, m_error);
    if ((status == EBudgetStatus::failed) || (status == EBudgetStatus::retry)) return status;
    m_last = m_start;
    m_duration = duration_ns;
    m_error = {};
    m_status = duration_ns ? status : EBudgetStatus::expired;
    return m_status;
}

SEligibleBudgetObservation CHostEligibleBudget::observe() noexcept
{
    if ((m_status == EBudgetStatus::failed) || (m_status == EBudgetStatus::expired)) return { m_status, 0u, m_error };
    std::uint64_t eligible = 0u, generation = 0u, awake = 0u;
    platform::threading::WaitError error;
    const auto status = sample(eligible, generation, awake, error);
    if (status == EBudgetStatus::retry) return { status, 0u, {} };
    if ((status == EBudgetStatus::failed) || (generation != m_generation) || (eligible < m_last) || (awake < m_last_awake))
    {
        m_status = EBudgetStatus::failed;
        m_error = error.code == platform::threading::EWaitError::none ?
            platform::threading::WaitError{ platform::threading::EWaitError::invalid_argument, 0u } : error;
        return { m_status, 0u, m_error };
    }
    m_last = eligible;
    m_last_awake = awake;
    const auto elapsed = eligible - m_start;
    m_status = elapsed >= m_duration ? EBudgetStatus::expired : status;
    return { m_status, elapsed >= m_duration ? 0u : m_duration - elapsed, {} };
}

}   //  namespace modules
