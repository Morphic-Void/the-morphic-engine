
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    desktop_input.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Retains native transactions, bounds decoding/repeat work and gates cutovers.

#include <cstring>
#include <algorithm>
#include "runtime/desktop_input.hpp"
#include "external/SuiteUTF/include/utf_toolkit.h"
#include "debug/macros.hpp"

namespace platform::runtime
{

void CDesktopInput::attach(CInputConnection& connection, const std::uint64_t device) noexcept
{
    detach();
    m_connection = &connection;
    m_device = device;
    m_action_generation = connection.snapshot().action_generation;
    m_text_generation = connection.snapshot().text_generation;
    cutover(input::k_desktop);
}

void CDesktopInput::detach() noexcept
{
    m_connection = nullptr;
    m_head = m_count = m_repeat = m_cutover = 0u;
    m_ordinal = m_time = 0u;
    m_high = 0u;
    m_high_repeats = 0u;
    m_quiet = m_baseline_pending = m_baseline_inflight = false;
    m_losing = false;
}

void CDesktopInput::cutover(const std::uint32_t mask) noexcept
{
    if (!m_connection) return;
    if (m_epoch == UINT64_MAX)
    {
        m_connection->close(UINT32_MAX);
        return;
    }
    ++m_epoch;
    m_cutover |= mask;
    m_quiet = false;
    if (mask & input::k_text)
    {
        if (m_text_epoch == UINT64_MAX)
        {
            m_connection->close(UINT32_MAX);
            return;
        }
        ++m_text_epoch;
        m_high = 0u;
    }
    if (m_cutover & input::k_actions)
    {
        if (m_cutover & input::k_text) m_head = m_count = m_repeat = 0u;
        m_baseline_pending = m_baseline_inflight = false;
        m_connection->mapper().require_baseline(!(m_cutover & input::k_text));
    }
    m_action_generation = m_connection->snapshot().action_generation;
    m_text_generation = m_connection->snapshot().text_generation;
    m_connection->ingress(m_cutover);
}

void CDesktopInput::lost() noexcept
{
    if (!m_connection) return;
    m_losing = true;
    m_connection->history_loss();
    if (!(m_cutover & input::k_actions) || m_baseline_pending || m_baseline_inflight || m_count)
    {
        cutover(input::k_desktop);
    }
}

bool CDesktopInput::observe(const SDesktopTransaction& transaction) noexcept
{
    synchronise();
    if (!m_connection || (m_connection->snapshot().phase == input::EPhase::closing))
    {
        return false;
    }
    m_quiet = false;
    if ((transaction.count > k_desktop_changes) || (transaction.raw_count > 2u) || !transaction.repeats ||
        (transaction.text && (transaction.count || transaction.raw_count)) ||
        (!transaction.text && !transaction.count))
    {
        lost();
        return false;
    }
    if ((!transaction.text && (m_cutover & input::k_actions) && !m_baseline_pending && !m_baseline_inflight) ||
        (transaction.text && (m_cutover & input::k_text)))
    {
        if (m_losing) m_connection->history_loss();
        return true; // Deliberately suppressed; native state still tracks input.
    }
    if (m_count == k_desktop_capacity)
    {
        lost();
        return false;
    }
    //  No external/native calls occur while appending to this Host-owned queue.
    SEntry& entry = m_entries[(m_head + m_count) & k_desktop_mask];
    entry.transaction = transaction;
    entry.action_generation = m_connection->snapshot().action_generation;
    entry.text_generation = m_connection->snapshot().text_generation;
    entry.text_epoch = m_text_epoch;
    const input::SInputSnapshot& state = m_connection->snapshot();
    const std::uint32_t stream = transaction.text ? input::k_text : input::k_actions;
    entry.suppressed = ((m_cutover | state.pending) & stream) || (state.phase != input::EPhase::active) ||
        (transaction.text && !state.text_enabled);
    ++m_count;
    return true;
}

void CDesktopInput::quiet(const SInputObservation& baseline, const std::uint64_t epoch) noexcept
{
    synchronise();
    if (!m_connection || (epoch != m_epoch))
    {
        return;
    }
    m_quiet = true;
    if ((m_cutover & input::k_actions) && !m_baseline_pending && !m_baseline_inflight)
    {
        m_baseline = baseline;
        m_baseline.device = m_device;
        m_baseline.kind = EObservationKind::snapshot;
        m_baseline.baseline = true;
        m_baseline.raw_count = 0u;
        m_baseline_pending = true;
    }
}

bool CDesktopInput::runnable() const noexcept
{
    return m_connection && (m_connection->snapshot().phase != input::EPhase::closing) &&
        (m_count || m_baseline_pending || m_baseline_inflight || (m_cutover && m_quiet) ||
            (m_action_generation != m_connection->snapshot().action_generation) ||
            (m_text_generation != m_connection->snapshot().text_generation));
}

void CDesktopInput::malformed_text(const std::uint64_t time_ns) noexcept
{
    ++m_malformed;
    if (!m_last_diagnostic || ((time_ns >= m_last_diagnostic) && ((time_ns - m_last_diagnostic) >= UINT64_C(1000000000))))
    {
        MV_LOG(warning, "Platform window {}: discarded malformed native text ({} occurrences)", m_device, m_malformed);
        m_last_diagnostic = time_ns ? time_ns : 1u;
    }
}

bool CDesktopInput::advance(SMapperBudget& budget) noexcept
{
    if (!m_connection || !budget.work || (m_connection->snapshot().phase == input::EPhase::closing))
    {
        return false;
    }
    synchronise();
    const input::SInputSnapshot& state = m_connection->snapshot();
    CInputMapper& mapper = m_connection->mapper();
    if (!mapper.prepared() || mapper.pending())
    {
        return false;
    }
    if (m_baseline_inflight)
    {
        --budget.work;
        m_baseline_inflight = false;
        m_losing = false;
        m_cutover = 0u;
        m_connection->ingress(0u);
        return true;
    }
    if (m_baseline_pending)
    {
        --budget.work;
        if (m_ordinal == UINT64_MAX)
        {
            m_connection->close(UINT32_MAX);
            return true;
        }
        m_baseline.ordinal = ++m_ordinal;
        m_baseline.time_ns = std::max(m_time, m_baseline.time_ns);
        m_time = m_baseline.time_ns;
        if (mapper.submit(m_baseline) != EInputAdmission::accepted)
        {
            m_connection->close(UINT32_MAX);
            return true;
        }
        m_baseline_pending = false;
        m_baseline_inflight = true;
        return true;
    }
    if (!m_count)
    {
        if (m_cutover && m_quiet)
        {
            --budget.work;
            m_cutover = 0u;
            m_connection->ingress(0u);
            mapper.text_changed();
            return true;
        }
        return false;
    }
    --budget.work;
    SEntry& entry = m_entries[m_head];
    const SDesktopTransaction& transaction = entry.transaction;
    SInputObservation observation;
    observation.device = m_device;
#if defined(MV_INPUT_ACCEPTANCE)
    observation.trace = transaction.trace;
#endif
    observation.time_ns = std::max(m_time, transaction.time_ns);
    observation.suppressed = entry.suppressed;
    bool submit = entry.action_generation == state.action_generation;
    if (transaction.text)
    {
        submit = (entry.text_generation == state.text_generation) && (entry.text_epoch == m_text_epoch) &&
            !(m_cutover & input::k_text) && !entry.suppressed;
        if (entry.suppressed) m_high = 0u;
        observation.kind = EObservationKind::text;
        std::uint32_t scalar = transaction.code_unit;
        if (submit && !m_repeat)
        {
            m_scalar_valid = false;
            if ((transaction.utf32 && (scalar >= 0xd800u) && (scalar <= 0xdfffu)) ||
                (!transaction.utf32 && (scalar > 0xffffu)))
            {
                malformed_text(observation.time_ns);
                m_high = 0u;
                submit = false;
            }
            else if ((scalar >= 0xd800u) && (scalar <= 0xdbffu))
            {
                if (m_high) malformed_text(observation.time_ns);
                m_high = static_cast<std::uint16_t>(scalar);
                m_high_repeats = transaction.repeats;
                submit = false;
            }
            else if ((scalar >= 0xdc00u) && (scalar <= 0xdfffu))
            {
                if (m_high && (m_high_repeats == transaction.repeats))
                {
                    scalar = 0x10000u + ((static_cast<std::uint32_t>(m_high) - 0xd800u) << 10u) + (scalar - 0xdc00u);
                }
                else { malformed_text(observation.time_ns); submit = false; }
                m_high = 0u;
            }
            else if (m_high)
            {
                malformed_text(observation.time_ns);
                m_high = 0u;
            }
            if (scalar > 0x10ffffu) { malformed_text(observation.time_ns); submit = false; }
            //  Editing/control characters are interpreted by the key path.
            if ((scalar < 0x20u) || (scalar == 0x7fu)) submit = false;
            m_scalar = scalar;
            m_scalar_valid = submit;
        }
        submit = submit && m_scalar_valid;
        if (submit)
        {
            unicode::utf::utf_text encoded{ sizeof(observation.text.payload), 0u, observation.text.payload };
            std::uint32_t bytes = 0u;
            (void)unicode::utf::toolkit::encodeUTF8(encoded, m_scalar, bytes);
            observation.text.byte_count = static_cast<std::uint16_t>(bytes);
        }
        else m_repeat = transaction.repeats - 1u;
    }
    else
    {
        observation.count = transaction.count;
        observation.raw_count = m_repeat ? 0u : transaction.raw_count;
        for (std::uint32_t index = 0u; index < transaction.count; ++index)
        {
            observation.controls[index] = transaction.controls[index];
            if (m_repeat) observation.controls[index].repeat = true;
        }
        for (std::uint32_t index = 0u; index < observation.raw_count; ++index) observation.raw[index] = transaction.raw[index];
    }
    if (submit)
    {
        if (m_ordinal == UINT64_MAX)
        {
            m_connection->close(UINT32_MAX);
            return true;
        }
        observation.ordinal = ++m_ordinal;
        m_time = observation.time_ns;
        const EInputAdmission result = mapper.submit(observation);
        if (result == EInputAdmission::baseline_required)
        {
            cutover(input::k_desktop);
            return true;
        }
        if (result != EInputAdmission::accepted)
        {
            m_connection->close(UINT32_MAX);
            return true;
        }
    }
    if (++m_repeat == transaction.repeats)
    {
        m_repeat = 0u;
        m_head = (m_head + 1u) & k_desktop_mask;
        --m_count;
    }
    return true;
}

void CDesktopInput::synchronise() noexcept
{
    if (!m_connection || (m_connection->snapshot().phase == input::EPhase::closing))
    {
        return;
    }
    const input::SInputSnapshot& state = m_connection->snapshot();
    if (m_action_generation != state.action_generation)
    {
        cutover((m_text_generation != state.text_generation) ? input::k_desktop : input::k_actions);
    }
    else if (m_text_generation != state.text_generation) cutover(input::k_text);
}

}   // namespace platform::runtime
