
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    value_work_budget.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Explicit operation-owned allowance for recursive value-writer visits.

#pragma once

#include <cstdint>

namespace schema
{

//  Pass the same object to every participating writer. References are borrowed
//  for synchronous use; the mutable allowance must not be shared concurrently.
class CValueWorkBudget
{
public:
    explicit CValueWorkBudget(const std::uint64_t limit) noexcept : m_remaining{ limit } {}
    CValueWorkBudget(const CValueWorkBudget&) = delete;
    CValueWorkBudget& operator=(const CValueWorkBudget&) = delete;

    [[nodiscard]] static CValueWorkBudget unbounded() noexcept { return CValueWorkBudget{ SUnbounded{} }; }
    [[nodiscard]] bool exhausted() const noexcept { return !m_unbounded && !m_remaining; }
    [[nodiscard]] bool rejected() const noexcept { return m_rejected; }
    [[nodiscard]] bool try_consume(const std::uint64_t credits = 1u) noexcept
    {
        if (m_unbounded) return true;
        if (credits > m_remaining)
        {
            m_remaining = 0u;
            m_rejected = true;
            return false;
        }
        m_remaining -= credits;
        return true;
    }

private:
    struct SUnbounded {};
    explicit CValueWorkBudget(const SUnbounded) noexcept : m_unbounded{ true } {}
    std::uint64_t m_remaining{};
    const bool m_unbounded{ false };
    bool m_rejected{};
};

}   // namespace schema
