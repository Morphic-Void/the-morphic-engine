
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_reader.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Bounded, non-owning input merge reader. Generic borrow and latch ownership
//  remain with the caller, which ends reader access before reporting quiescence.

#pragma once

#include <cstring>
#include "input.hpp"

namespace platform::input
{

enum class EInputRead { record, boundary, cancelled, empty, retry, progress, closing, failed };
enum class EInputValidation { current, stale, retry };
struct SInputEvent
{
    SConnectionToken connection;
    std::uint32_t stream{ 0u };
    std::uint64_t generation{ 0u };
    SActionInputV1 action;
    STextInputV1 text;
};

class CInputReader final
{
public:
    CInputReader() noexcept = default;
    CInputReader(const CInputReader&) = delete;
    CInputReader& operator=(const CInputReader&) = delete;
    [[nodiscard]] bool install(const SInputEndpointV1& view, const modules::SModuleInstance consumer) noexcept;
    [[nodiscard]] EInputRead next(SInputEvent& event, const std::uint32_t budget = 64u) noexcept;
    [[nodiscard]] EInputValidation validate(const SInputEvent& event) noexcept;
    //  Cancel logical state before submitting a context/target change. A false
    //  result requires a coherent retry before submission; application stays off.
    [[nodiscard]] bool cancel_application(const std::uint32_t streams) noexcept;
    //  Explicitly confirm the intended accepted transition after its boundary.
    //  Unrelated overflow acknowledgements never resume locally cancelled state.
    [[nodiscard]] bool resume_application(const SBoundaryReady& ready, const std::uint32_t streams) noexcept;
    [[nodiscard]] bool boundary_ready(SBoundaryReady& ready) noexcept;
    //  Call only after the control message has been accepted for submission.
    [[nodiscard]] bool submitted(const SBoundaryReady& ready) noexcept;
    //  Busy permits resubmission; stale refreshes the superseding barrier.
    void retry_boundary(const SBoundaryReady& ready) noexcept;
    [[nodiscard]] std::uint32_t take_cancellation() noexcept;
    [[nodiscard]] const SInputSnapshot& snapshot() const noexcept { return m_state; }
    //  Terminal for this helper. A new acquisition requires a new helper.
    void close() noexcept;

private:
    friend struct SInputReaderTestAccess;
    [[nodiscard]] EStateRead refresh() noexcept;
    [[nodiscard]] bool allowed(const std::uint32_t stream) const noexcept;
    const SInputEndpointV1* m_view{ nullptr };
    SInputSnapshot m_state;
    SActionInputV1 m_action;
    STextInputV1 m_text;
    std::uint64_t m_action_seen{ 0u }, m_text_seen{ 0u };
    std::uint64_t m_action_ack{ 0u }, m_text_ack{ 0u }, m_last_sequence{ 0u };
    std::uint64_t m_cancel_action{ 0u }, m_cancel_text{ 0u };
    std::uint32_t m_cancellation{ 0u };
    std::uint32_t m_local_cancel{ 0u };
    bool m_has_action{ false }, m_has_text{ false }, m_closed{ false }, m_failed{ false };
};

inline bool CInputReader::install(const SInputEndpointV1& view, const modules::SModuleInstance consumer) noexcept
{
    if (m_view || m_closed || !view.is_valid() || (view.consumer.value != consumer.value))
    {
        return false;
    }
    m_view = &view;
    return true;
}

inline void CInputReader::close() noexcept
{
    m_closed = true;
    m_view = nullptr;
    m_has_action = m_has_text = false;
    m_cancellation |= k_desktop;
}

inline EStateRead CInputReader::refresh() noexcept
{
    SInputSnapshot state;
    const EStateRead result = m_view->state->read(state);
    if (result == EStateRead::retry) return result;
    if (state.action_generation != m_state.action_generation)
    {
        m_action_seen = m_action_ack = 0u;
        m_cancellation |= k_actions;
    }
    if (state.text_generation != m_state.text_generation)
    {
        m_text_seen = m_text_ack = 0u;
        m_cancellation |= k_text;
    }
    if (state.phase == EPhase::closing)
    {
        m_cancellation |= m_view->text ? k_desktop : k_actions;
    }
    m_state = state;
    //  Never clear copied heads here: one can be the only current marker.
    return result;
}

inline std::uint32_t CInputReader::take_cancellation() noexcept
{
    const std::uint32_t cancellation = m_cancellation;
    m_cancellation = 0u;
    return cancellation;
}

inline bool CInputReader::allowed(const std::uint32_t stream) const noexcept
{
    return (m_state.phase == EPhase::active) && !(m_state.pending & stream) &&
        !(m_cancellation & stream) && !(m_local_cancel & stream) &&
        ((stream == k_actions) ? ((m_action_seen == m_state.action_generation) &&
            (m_action_ack == m_state.action_generation)) :
            (m_state.text_enabled && (m_text_seen == m_state.text_generation) && (m_text_ack == m_state.text_generation)));
}

inline EInputValidation CInputReader::validate(const SInputEvent& event) noexcept
{
    if (!m_view || m_closed || m_failed || (event.connection.value != m_view->connection.value) ||
        ((event.stream != k_actions) && (event.stream != k_text)))
    {
        return EInputValidation::stale;
    }
    const EStateRead result = refresh();
    if (result == EStateRead::retry)
    {
        return EInputValidation::retry;
    }
    const std::uint64_t generation = (event.stream == k_actions) ? m_state.action_generation : m_state.text_generation;
    return ((result == EStateRead::success) && allowed(event.stream) && (event.generation == generation)) ?
        EInputValidation::current : EInputValidation::stale;
}

inline bool CInputReader::cancel_application(const std::uint32_t streams) noexcept
{
    if (!m_view || m_closed || m_failed || !streams || (streams & ~(m_view->text ? k_desktop : k_actions)))
    {
        return false;
    }
    m_local_cancel |= streams;
    m_cancellation |= streams;
    if (streams & k_actions) m_cancel_action = UINT64_MAX;
    if (streams & k_text) m_cancel_text = UINT64_MAX;
    if (refresh() != EStateRead::success) return false;
    if (streams & k_actions) m_cancel_action = m_state.action_generation;
    if (streams & k_text) m_cancel_text = m_state.text_generation;
    return m_state.phase != EPhase::closing;
}

inline bool CInputReader::boundary_ready(SBoundaryReady& ready) noexcept
{
    if (!m_view || m_closed || m_failed || (refresh() != EStateRead::success) ||
        (m_state.phase == EPhase::closing) || !m_state.pending ||
        ((m_state.pending & k_actions) && (m_action_seen != m_state.action_generation)) ||
        ((m_state.pending & k_text) && (m_text_seen != m_state.text_generation)))
    {
        return false;
    }
    if ((m_action_ack == m_state.action_generation) && (m_text_ack == m_state.text_generation))
    {
        return false;
    }
    ready = { m_view->connection, m_state.action_generation, m_state.text_generation };
    return true;
}

inline bool CInputReader::resume_application(const SBoundaryReady& ready, const std::uint32_t streams) noexcept
{
    if (!m_view || m_closed || m_failed || !streams || (streams & ~m_local_cancel) ||
        (refresh() != EStateRead::success) || (m_state.phase != EPhase::active) || (m_state.pending & streams) ||
        (ready.connection.value != m_view->connection.value) ||
        (ready.action_generation != m_state.action_generation) || (ready.text_generation != m_state.text_generation) ||
        ((streams & k_actions) && ((ready.action_generation <= m_cancel_action) ||
            (m_action_seen != ready.action_generation) || (m_action_ack != ready.action_generation))) ||
        ((streams & k_text) && ((ready.text_generation <= m_cancel_text) ||
            (m_text_seen != ready.text_generation) || (m_text_ack != ready.text_generation))))
    {
        return false;
    }
    m_local_cancel &= ~streams;
    return true;
}

inline bool CInputReader::submitted(const SBoundaryReady& ready) noexcept
{
    if (!m_view || m_closed || m_failed || (ready.connection.value != m_view->connection.value) ||
        (ready.action_generation != m_state.action_generation) || (ready.text_generation != m_state.text_generation) ||
        ((m_state.pending & k_actions) && (m_action_seen != ready.action_generation)) ||
        ((m_state.pending & k_text) && (m_text_seen != ready.text_generation)))
    {
        return false;
    }
    m_action_ack = ready.action_generation;
    m_text_ack = ready.text_generation;
    return true;
}

inline void CInputReader::retry_boundary(const SBoundaryReady& ready) noexcept
{
    if (m_view && !m_closed && (ready.connection.value == m_view->connection.value) &&
        (ready.action_generation == m_action_ack) && (ready.text_generation == m_text_ack))
    {
        if (m_state.pending & k_actions) m_action_ack = 0u;
        if (m_state.pending & k_text) m_text_ack = 0u;
    }
}

inline EInputRead CInputReader::next(SInputEvent& event, const std::uint32_t budget) noexcept
{
    event = {};
    if (m_failed) return EInputRead::failed;
    if (!m_view || m_closed)
    {
        return EInputRead::closing;
    }
    const std::uint32_t limit = (budget < 64u) ? budget : 64u;
    for (std::uint32_t attempt = 0u; attempt < limit; ++attempt)
    {
        EStateRead result = refresh();
        if (result == EStateRead::retry)
        {
            return EInputRead::retry;
        }
        if (result == EStateRead::failed)
        {
            return EInputRead::failed;
        }
        if (m_state.phase == EPhase::closing)
        {
            return EInputRead::closing;
        }
        if (m_cancellation) return EInputRead::cancelled;
        const std::uint64_t cutoff = m_state.committed;
        bool freed = false;
        //  Both missing heads must be refreshed AFTER this cutoff was sampled.
        if (!m_has_action)
        {
            m_has_action = m_view->actions->read(m_action);
            freed = m_has_action;
        }
        if (!m_has_text && m_view->text)
        {
            m_has_text = m_view->text->read(m_text);
            freed = freed || m_has_text;
        }
        if (freed && !m_view->capacity_wake.signal())
        {
            m_failed = true;
            return EInputRead::failed;
        }
        result = refresh();
        if (result == EStateRead::retry)
        {
            return EInputRead::retry;
        }
        if (result == EStateRead::failed)
        {
            return EInputRead::failed;
        }
        if (m_state.phase == EPhase::closing)
        {
            return EInputRead::closing;
        }
        if (m_cancellation) return EInputRead::cancelled;
        //  The validation read can increase committed, but must not extend cutoff.
        const bool action = m_has_action && (m_action.sequence <= cutoff);
        const bool text = m_has_text && (m_text.sequence <= cutoff);
        if (!action && !text)
        {
            return EInputRead::empty;
        }
        const bool choose_action = action && (!text || (m_action.sequence < m_text.sequence));
        const std::uint64_t sequence = choose_action ? m_action.sequence : m_text.sequence;
        if (!sequence || (sequence <= m_last_sequence))
        {
            m_failed = true;
            return EInputRead::failed;
        }
        m_last_sequence = sequence;
        if (choose_action)
        {
            m_has_action = false;
            if (m_action.kind == EActionKind::boundary)
            {
                SActionBoundary marker;
                std::memcpy(&marker, m_action.payload, sizeof(marker));
                if ((marker.generation == m_state.action_generation) && (marker.transition == m_state.transition))
                {
                    m_action_seen = marker.generation;
                    return EInputRead::boundary;
                }
            }
            else if (allowed(k_actions))
            {
                event = { m_view->connection, k_actions, m_state.action_generation, m_action, {} };
                return EInputRead::record;
            }
        }
        else
        {
            m_has_text = false;
            if (m_text.kind == ETextKind::boundary)
            {
                STextBoundary marker;
                std::memcpy(&marker, m_text.payload, sizeof(marker));
                if ((marker.generation == m_state.text_generation) && (marker.transition == m_state.transition))
                {
                    m_text_seen = marker.generation;
                    return EInputRead::boundary;
                }
            }
            else if (allowed(k_text))
            {
                event = { m_view->connection, k_text, m_state.text_generation, {}, m_text };
                return EInputRead::record;
            }
        }
    }
    return EInputRead::progress;
}

}   //  namespace platform::input
