
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    pressure_diagnostic.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Bounded diagnostic episodes; thresholds never change admission or failure.

#pragma once

#include <cstdint>
#include <cinttypes>
#include "debug/macros.hpp"

namespace host
{

class CPressureDiagnostic
{
public:
    static constexpr std::uint64_t k_threshold_ns = 1000000000u;
    static constexpr std::uint64_t k_repeat_ns = 5000000000u;
    enum class EReport { none, warning, recovery };
    struct SReport
    {
        EReport kind{ EReport::none };
        std::uint64_t duration_ns{ 0u }, observations{ 0u }, progress{ 0u };
    };

    [[nodiscard]] SReport observe(const std::uint64_t now, const bool waiting,
        const std::uint64_t progress, const bool reset_on_progress = false) noexcept
    {
        SReport result;
        if (m_active && (!waiting || (reset_on_progress && (progress != m_progress)) || (now < m_start)))
        {
            if (m_warned)
            {
                result = { EReport::recovery, ((now >= m_start) ? (now - m_start) : 0u),
                    m_observations, ((progress >= m_progress) ? (progress - m_progress) : 0u) };
            }
            m_active = m_warned = false;
        }
        if (!waiting) return result;
        if (!m_active)
        {
            m_active = true;
            m_start = now;
            m_progress = progress;
            m_observations = 0u;
            m_due = add(now, k_threshold_ns);
            if (m_due < m_next_warning) m_due = m_next_warning;
        }
        if (m_observations != UINT64_MAX) ++m_observations;
        if ((result.kind == EReport::none) && (now >= m_due))
        {
            m_warned = true;
            m_due = m_next_warning = add(now, k_repeat_ns);
            result = { EReport::warning, now - m_start, m_observations,
                ((progress >= m_progress) ? (progress - m_progress) : 0u) };
        }
        return result;
    }

    void limit_wait(const std::uint64_t now, std::uint64_t& remaining) const noexcept
    {
        if (!m_active) return;
        const std::uint64_t delay = (now < m_due) ? (m_due - now) : 0u;
        if (delay < remaining) remaining = delay;
    }

    static void log(const SReport report, const char* const kind, const std::uint64_t address,
        const std::uint64_t endpoint, const std::uint64_t source, const std::uint32_t occupied,
        const std::uint32_t capacity, const std::uint32_t retained) noexcept
    {
        if (report.kind == EReport::warning)
        {
            MV_LOG_RICH(warning, "Host %s: address %" PRIu64 ", endpoint %" PRIu64 ", source %" PRIu64
                ", queued %" PRIu32 "/%" PRIu32 ", retained %" PRIu32 ", duration %" PRIu64
                " ns, observations %" PRIu64 ", progress %" PRIu64,
                kind, address, endpoint, source, occupied, capacity, retained, report.duration_ns, report.observations, report.progress);
        }
        else if (report.kind == EReport::recovery)
        {
            MV_LOG_RICH(info, "Host %s recovered: address %" PRIu64 ", endpoint %" PRIu64 ", source %" PRIu64
                ", duration %" PRIu64 " ns, observations %" PRIu64 ", progress %" PRIu64,
                kind, address, endpoint, source, report.duration_ns, report.observations, report.progress);
        }
    }

private:
    [[nodiscard]] static std::uint64_t add(const std::uint64_t value, const std::uint64_t increment) noexcept
    {
        return (value > (UINT64_MAX - increment)) ? UINT64_MAX : (value + increment);
    }
    std::uint64_t m_start{ 0u }, m_due{ 0u }, m_next_warning{ 0u }, m_observations{ 0u }, m_progress{ 0u };
    bool m_active{ false }, m_warned{ false };
};

}   //  namespace host
