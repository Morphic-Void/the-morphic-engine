
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    pressure_selection.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Bounded pressure slices with persistent service order for eligible inputs.

#pragma once

#include <cstdint>

namespace host
{

struct SPressureLane
{
    std::uint32_t occupied{ 0u }, capacity{ 0u };
    std::uint32_t order{ 0u }; //  Dense persistent waiting order; zero means ineligible.
    std::uint32_t preference{ 0u }; //  Direct before Host only when pressures tie.
    bool runnable{ false }, local{ false };
    std::uint64_t dispatched{ 0u };
};

struct SPressureSelection
{
    std::uint32_t lane{ UINT32_MAX }, allowance{ 0u };
    bool fairness{ false };
};

class CPressureSelection
{
public:
    static constexpr std::uint32_t k_slice_messages = 8u;
    static constexpr std::uint32_t k_pressure_slices = 8u;

    //  Fixed array extent for this selector's lifetime. Endpoint population may
    //  change inside it. Withdraw a slot before clearing/reusing its lane state;
    //  never compact the array or replace a ranked lane with another identity.
    CPressureSelection(SPressureLane* const lanes, const std::uint32_t count) noexcept
        : m_lanes{ lanes }, m_count{ count } {}
    CPressureSelection(const CPressureSelection&) = delete;
    CPressureSelection& operator=(const CPressureSelection&) = delete;

    void withdraw(const std::uint32_t index) noexcept
    {
        const std::uint32_t removed = m_lanes[index].order;
        m_lanes[index].order = 0u;
        m_lanes[index].runnable = false;
        if (!removed) return;
        for (std::uint32_t other = 0u; other < m_count; ++other)
        {
            if (m_lanes[other].order > removed) --m_lanes[other].order;
        }
        --m_waiting;
    }

    //  Lane slots remain stable. Eligibility includes a runnable captured head,
    //  regardless of ring occupancy; a blocked head sets runnable to false.
    [[nodiscard]] SPressureSelection select() noexcept
    {
        for (std::uint32_t index = 0u; index < m_count; ++index)
        {
            SPressureLane& lane = m_lanes[index];
            if (!lane.runnable && lane.order)
            {
                withdraw(index);
            }
        }
        for (std::uint32_t index = 0u; index < m_count; ++index)
        {
            if (m_lanes[index].runnable && !m_lanes[index].order)
            {
                m_lanes[index].order = ++m_waiting;
            }
        }

        const bool fairness = m_slices >= k_pressure_slices;
        std::uint32_t selected = UINT32_MAX;
        for (std::uint32_t offset = 0u; offset < m_count; ++offset)
        {
            const std::uint32_t index = ((m_cursor + offset) < m_count) ? (m_cursor + offset) : (m_cursor + offset - m_count);
            const SPressureLane& candidate = m_lanes[index];
            if (!candidate.runnable) continue;
            if (selected == UINT32_MAX)
            {
                selected = index;
                continue;
            }
            const SPressureLane& current = m_lanes[selected];
            if (fairness)
            {
                if (candidate.order < current.order) selected = index;
                continue;
            }
            if (candidate.local != current.local)
            {
                if (!candidate.local) selected = index;
                continue;
            }
            const std::uint64_t left = static_cast<std::uint64_t>(candidate.occupied) * current.capacity;
            const std::uint64_t right = static_cast<std::uint64_t>(current.occupied) * candidate.capacity;
            if ((left > right) || ((left == right) && (candidate.preference < current.preference)))
            {
                selected = index;
            }
        }
        return { selected, ((selected == UINT32_MAX) ? 0u : (fairness ? 1u : k_slice_messages)), fairness };
    }

    //  Call after each successful dispatch, including a self message.
    void served(const std::uint32_t index) noexcept
    {
        ++m_lanes[index].dispatched;
        const std::uint32_t previous = m_lanes[index].order;
        for (std::uint32_t other = 0u; other < m_count; ++other)
        {
            if (m_lanes[other].order > previous) --m_lanes[other].order;
        }
        m_lanes[index].order = m_waiting;
        m_cursor = ((index + 1u) == m_count) ? 0u : (index + 1u);
    }

    //  A nonempty slice shortened by a pump budget still counts. Never reset
    //  this state at a turn boundary, a capacity wake, or a new input arrival.
    void finish(const SPressureSelection selection, const bool dispatched) noexcept
    {
        if (!dispatched) return;
        m_slices = selection.fairness ? 0u : (m_slices + 1u);
    }

private:
    SPressureLane* const m_lanes;
    const std::uint32_t m_count;
    std::uint32_t m_waiting{ 0u }, m_cursor{ 0u }, m_slices{ 0u };
};

}   //  namespace host
