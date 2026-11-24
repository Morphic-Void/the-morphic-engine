
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    execution_timing.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Candidate operation anchors and independently bounded timing unavailability.

#pragma once

#include "framework/module/host_eligible_budget.hpp"

namespace executive
{

inline constexpr std::uint64_t k_operation_stall_ns = UINT64_C(30000000000);
inline constexpr std::uint64_t k_timing_retry_ns = UINT64_C(1000000);

template<typename TBudget = modules::CHostEligibleBudget>
class TExecutionBudget
{
public:
    explicit TExecutionBudget(const platform::threading::SAwakeClock clock = {}) noexcept;
    [[nodiscard]] modules::EBudgetStatus start(const modules::SHostRestrictionReader reader) noexcept;
    [[nodiscard]] modules::EBudgetStatus progress_start() noexcept;
    [[nodiscard]] modules::SEligibleBudgetObservation observe() noexcept;
    [[nodiscard]] bool publishable() const noexcept;
    [[nodiscard]] bool starting() const noexcept;
    [[nodiscard]] modules::SEligibleBudgetAnchor anchor() const noexcept;
    void cancel_start() noexcept;

private:
    TBudget m_live, m_candidate;
    modules::SHostRestrictionReader m_reader;
    bool m_started{ false }, m_starting{ false };
};

class CTimingAvailability
{
public:
    enum class EEvent { none, warning, recovery, failure };
    struct SObservation
    {
        EEvent event{ EEvent::none };
        std::uint64_t duration_ns{ 0u }, retries{ 0u };
    };
    explicit CTimingAvailability(const platform::threading::SAwakeClock clock = {}) noexcept;

    [[nodiscard]] SObservation observe(const modules::EBudgetStatus status) noexcept;
    [[nodiscard]] bool retrying() const noexcept;

private:
    const platform::threading::SAwakeClock m_clock;
    std::uint64_t m_begin{ 0u }, m_last{ 0u }, m_last_warning{ 0u }, m_retries{ 0u };
    bool m_active{ false }, m_warned{ false }, m_warning_emitted{ false };
};

template<typename TBudget>
inline TExecutionBudget<TBudget>::TExecutionBudget(const platform::threading::SAwakeClock clock) noexcept : m_live{ clock }, m_candidate{ clock } {}

template<typename TBudget>
inline modules::EBudgetStatus TExecutionBudget<TBudget>::start(const modules::SHostRestrictionReader reader) noexcept
{
    //  A completed phase may supersede a pending start (for example,
    //  connection retirement). Only the candidate is replaced in that case.
    m_reader = reader;
    m_starting = true;
    //  start() may discard an anchor before discovering an incoherent
    //  snapshot. Only the candidate is exposed to that operation.
    m_candidate = m_live;
    return progress_start();
}

template<typename TBudget>
inline modules::EBudgetStatus TExecutionBudget<TBudget>::progress_start() noexcept
{
    if (!m_starting)
    {
        return m_started ? modules::EBudgetStatus::running : modules::EBudgetStatus::failed;
    }
    const modules::EBudgetStatus status = m_candidate.start(m_reader, k_operation_stall_ns);
    if ((status == modules::EBudgetStatus::running) || (status == modules::EBudgetStatus::restricted))
    {
        m_live = m_candidate;
        m_started = true;
        m_starting = false;
    }
    return status;
}

template<typename TBudget>
inline modules::SEligibleBudgetObservation TExecutionBudget<TBudget>::observe() noexcept
{
    if (m_starting)
    {
        const modules::EBudgetStatus status = progress_start();
        if ((status != modules::EBudgetStatus::running) && (status != modules::EBudgetStatus::restricted))
        {
            return { status, 0u, {} };
        }
    }
    return m_live.observe();
}

template<typename TBudget>
inline bool TExecutionBudget<TBudget>::publishable() const noexcept { return m_started && !m_starting; }

template<typename TBudget>
inline bool TExecutionBudget<TBudget>::starting() const noexcept { return m_starting; }

template<typename TBudget>
inline modules::SEligibleBudgetAnchor TExecutionBudget<TBudget>::anchor() const noexcept { return m_live.anchor(); }

template<typename TBudget>
inline void TExecutionBudget<TBudget>::cancel_start() noexcept { m_starting = false; }

inline CTimingAvailability::CTimingAvailability(const platform::threading::SAwakeClock clock) noexcept : m_clock{ clock } {}

inline CTimingAvailability::SObservation CTimingAvailability::observe(const modules::EBudgetStatus status) noexcept
{
    if (status == modules::EBudgetStatus::failed)
    {
        return { EEvent::failure, 0u, m_retries };
    }
    const bool retry = status == modules::EBudgetStatus::retry;
    if (!retry && !m_active) return {};
    std::uint64_t now = 0u;
    if ((m_clock.read(now).code != platform::threading::EWaitError::none) || (m_active && (now < m_last)))
    {
        return { EEvent::failure, 0u, m_retries };
    }
    m_last = now;
    if (!retry)
    {
        const SObservation recovered{ m_warned ? EEvent::recovery : EEvent::none, now - m_begin, m_retries };
        m_active = m_warned = false;
        return recovered;
    }
    if (!m_active)
    {
        m_active = true;
        m_begin = now;
        m_retries = 0u;
    }
    if (m_retries != UINT64_MAX) ++m_retries;
    const std::uint64_t elapsed = now - m_begin;
    if (elapsed >= k_operation_stall_ns)
    {
        return { EEvent::failure, elapsed, m_retries };
    }
    if ((elapsed >= UINT64_C(1000000000)) &&
        (!m_warning_emitted || ((now >= m_last_warning) && ((now - m_last_warning) >= UINT64_C(5000000000)))))
    {
        m_warned = m_warning_emitted = true;
        m_last_warning = now;
        return { EEvent::warning, elapsed, m_retries };
    }
    return { EEvent::none, elapsed, m_retries };
}

inline bool CTimingAvailability::retrying() const noexcept { return m_active; }

}   //  namespace executive
