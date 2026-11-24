
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    output_retry_turn.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Finite per-recipient output retries followed by an ingress admission opportunity.

#pragma once

#include <cstdint>

namespace host
{

enum class EOutputProducer : std::uint32_t { service, borrow, asset, schema, process, report, module, filesystem, bundle, count };

class COutputRetryTurn
{
public:
    static constexpr std::uint32_t k_allowance = 8u;
    static constexpr std::uint32_t k_producers = static_cast<std::uint32_t>(EOutputProducer::count);
    [[nodiscard]] static constexpr std::uint32_t mask(const EOutputProducer producer) noexcept
    {
        return (producer < EOutputProducer::count) ? (1u << static_cast<std::uint32_t>(producer)) : 0u;
    }

    //  Ready means an existing operation has an output now, independently of
    //  pipe capacity. New operations never extend the current turn's allowance.
    //  The caller must not attempt publication until physical capacity exists.
    [[nodiscard]] EOutputProducer next(const std::uint32_t ready) noexcept
    {
        if (m_admission_due) return EOutputProducer::count;
        if (!(ready & ((1u << k_producers) - 1u)))
        {
            if (m_remaining) finish();
            return EOutputProducer::count;
        }
        if (!m_remaining) m_remaining = k_allowance;
        for (std::uint32_t offset = 0u; offset < k_producers; ++offset)
        {
            std::uint32_t index = m_cursor + offset;
            if (index >= k_producers) index -= k_producers;
            const EOutputProducer producer = static_cast<EOutputProducer>(index);
            if (ready & mask(producer)) return producer;
        }
        return EOutputProducer::count;
    }

    //  Called after one bounded producer attempt. Each producer separately
    //  advances its operation cursor, including when that operation became idle.
    //  Budget/capacity interruptions do not call this and do not restart a turn.
    void attempted(const EOutputProducer producer) noexcept
    {
        if (!m_remaining || m_admission_due || (producer >= EOutputProducer::count))
        {
            return;
        }
        m_cursor = static_cast<std::uint32_t>(producer) + 1u;
        if (m_cursor == k_producers) m_cursor = 0u;
        if (!--m_remaining) finish();
    }
    [[nodiscard]] bool admission_allowed() const noexcept { return m_remaining == 0u; }
    [[nodiscard]] bool admission_due() const noexcept { return m_admission_due; }
    [[nodiscard]] std::uint32_t remaining() const noexcept { return m_remaining; }

    //  Host calls this only after visiting this recipient's retained ingress
    //  heads and allowing an admission when their storage/state permits it.
    //  There need not be an input: it is an opportunity, not an indefinite wait.
    void admitted_or_examined() noexcept { m_admission_due = false; }

private:
    void finish() noexcept { m_remaining = 0u; m_admission_due = true; }
    std::uint32_t m_cursor{ 0u }, m_remaining{ 0u };
    bool m_admission_due{ false };
};

}   //  namespace host
