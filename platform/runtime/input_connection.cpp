
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_connection.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Publishes immutable input records and durable, acknowledged recovery barriers.

#include <cstring>
#include "runtime/input_connection.hpp"

namespace platform::runtime
{

using namespace input;
using services::EServiceStatus;

bool SInputInstallation::contains(const std::uint32_t context) const noexcept
{
    for (std::size_t index = 0u; index < contexts.size(); ++index)
    {
        if (contexts[index] == context) return true;
    }
    return false;
}

bool CInputConnection::initialise(const SInputEndpointV2& identity,
    const SInputInstallation& installation, const std::uint32_t context, const std::uint64_t transition) noexcept
{
    if (!installation.token.value || !installation.profile || !transition || !installation.contains(context) ||
        (installation.kind != identity.kind) || !identity.capacity_wake.is_valid() ||
        !m_actions.initialise(k_action_capacity) ||
        ((identity.kind == EConnection::desktop) && !m_text.initialise(k_text_capacity)) ||
        !m_subjects.resize(installation.subjects.size()) || !m_contexts.resize(installation.contexts.size()))
    {
        return false;
    }
    for (std::size_t index = 0u; index < m_subjects.size(); ++index)
    {
        m_subjects[index] = installation.subjects[index];
    }
    for (std::size_t index = 0u; index < m_contexts.size(); ++index)
    {
        m_contexts[index] = installation.contexts[index];
    }
    view = identity;
    this->installation = installation.token;
    view.action_capacity = k_action_capacity;
    view.text_capacity = (view.kind == EConnection::desktop) ? k_text_capacity : 0u;
    view.actions = &m_action_reader;
    view.text = (view.kind == EConnection::desktop) ? &m_text_reader : nullptr;
    view.state = &m_publication;
    m_state.context = context;
    m_state.transition = transition;
    m_mapper.initialise(installation);
    return view.is_valid() && boundary((view.text ? k_desktop : k_actions), EReason::initial);
}

bool CInputConnection::publish() noexcept
{
    const bool published = m_publication.publish(m_state);
    if (!published)
    {
        m_state.phase = EPhase::closing;
        m_state.failure = UINT32_MAX;
    }
    if (notification.is_valid() && !notification.signal())
    {
        m_state.phase = EPhase::closing;
        m_state.failure = static_cast<std::uint32_t>(EServiceStatus::delivery_failed);
        (void)m_publication.publish(m_state);
        return false;
    }
    return published;
}

bool CInputConnection::start(const ::threading::SNotificationEndpoint endpoint) noexcept
{
    if (ready || !m_mapper.prepared() || !endpoint.is_valid() || (m_state.phase == EPhase::closing))
    {
        return false;
    }
    notification = endpoint;
    ready = true;
    return publish();
}

bool CInputConnection::boundary(const std::uint32_t mask, const EReason reason) noexcept
{
    if (m_state.phase == EPhase::closing) return false;
    if (((mask & k_actions) && (m_state.action_generation == UINT64_MAX)) ||
        ((mask & k_text) && (m_state.text_generation == UINT64_MAX)))
    {
        close(UINT32_MAX);
        return false;
    }
    if (mask & k_actions)
    {
        m_mapper.invalidate();
        ++m_state.action_generation;
        m_state.action_reasons = static_cast<std::uint32_t>(reason);
    }
    if (mask & k_text)
    {
        ++m_state.text_generation;
        m_state.text_reasons = static_cast<std::uint32_t>(reason);
    }
    m_state.pending |= mask;
    m_markers &= ~mask;
    return publish();
}

bool CInputConnection::next_sequence(std::uint64_t& sequence) noexcept
{
    if (m_state.committed == UINT64_MAX)
    {
        close(UINT32_MAX);
        return false;
    }
    sequence = m_state.committed + 1u;
    return true;
}

void CInputConnection::progress(const std::uint64_t time_ns) noexcept
{
    if (!ready || (m_state.phase == EPhase::closing))
    {
        return;
    }
    if ((m_state.pending & k_actions) && !(m_markers & k_actions) && m_actions.writable_count())
    {
        SActionInputV1 record;
        if (!next_sequence(record.sequence)) return;
        record.time_ns = time_ns;
        record.kind = EActionKind::boundary;
        const SActionBoundary marker{ m_state.action_generation, m_state.transition, m_state.context, m_state.action_reasons };
        std::memcpy(record.payload, &marker, sizeof(marker));
        if (!m_actions.post(record))
        {
            close(UINT32_MAX);
            return;
        }
        m_markers |= k_actions;
        m_state.committed = record.sequence;
        if (!publish()) return;
    }
    if ((m_state.pending & k_text) && !(m_markers & k_text) && m_text.writable_count())
    {
        STextInputV1 record;
        if (!next_sequence(record.sequence)) return;
        record.time_ns = time_ns;
        record.kind = ETextKind::boundary;
        const STextBoundary marker{ m_state.text_generation, m_state.transition };
        std::memcpy(record.payload, &marker, sizeof(marker));
        if (!m_text.post(record))
        {
            close(UINT32_MAX);
            return;
        }
        m_markers |= k_text;
        m_state.committed = record.sequence;
        (void)publish();
    }
}

EServiceStatus CInputConnection::acknowledge(const SBoundaryReady& ready) noexcept
{
    if (m_state.phase == EPhase::closing)
    {
        return EServiceStatus::closing;
    }
    if ((ready.connection.value != view.connection.value) ||
        (ready.action_generation != m_state.action_generation) || (ready.text_generation != m_state.text_generation))
    {
        return EServiceStatus::stale;
    }
    if (!this->ready || ((m_markers & m_state.pending) != m_state.pending))
    {
        return EServiceStatus::busy;
    }
    if (!m_state.pending) return EServiceStatus::success;
    m_context_pending = m_target_pending = false;
    m_state.pending = 0u;
    m_state.phase = EPhase::active;
    return publish() ? EServiceStatus::success : EServiceStatus::delivery_failed;
}

EServiceStatus CInputConnection::select_context(const std::uint32_t context, const std::uint64_t transition) noexcept
{
    if (m_state.phase == EPhase::closing)
    {
        return EServiceStatus::closing;
    }
    if (m_context_pending || m_target_pending)
    {
        return EServiceStatus::busy;
    }
    bool found = false;
    for (std::size_t index = 0u; index < m_contexts.size(); ++index)
    {
        found = found || (m_contexts[index] == context);
    }
    if (!found || !transition || (transition <= m_state.transition))
    {
        return EServiceStatus::invalid_request;
    }
    m_context_pending = true;
    m_state.transition = transition;
    m_state.context = context;
    m_state.text_enabled = 0u;
    m_state.text_target = 0u;
    return boundary((view.text ? k_desktop : k_actions), EReason::context) ?
        EServiceStatus::success : EServiceStatus::delivery_failed;
}

EServiceStatus CInputConnection::text_target(const STextTarget& target) noexcept
{
    if (m_state.phase == EPhase::closing)
    {
        return EServiceStatus::closing;
    }
    if (!view.text) return EServiceStatus::unsupported;
    if (target.transition != m_state.transition)
    {
        return EServiceStatus::stale;
    }
    if (m_context_pending || m_target_pending || m_mapper.pending())
    {
        return EServiceStatus::busy;
    }
    m_state.text_target = target.target;
    m_target_pending = true;
    m_state.text_enabled = target.target ? 1u : 0u;
    m_mapper.text_changed();
    return boundary(k_text, EReason::text_target) ? EServiceStatus::success : EServiceStatus::delivery_failed;
}

void CInputConnection::close(const std::uint32_t failure) noexcept
{
    if (m_state.phase == EPhase::closing) return;
    m_mapper.invalidate();
    m_state.phase = EPhase::closing;
    m_state.pending = 0u;
    m_state.text_enabled = 0u;
    m_state.failure = failure;
    (void)publish();
}

EInputPost CInputConnection::lose(const std::uint32_t stream, const bool overflow) noexcept
{
    std::uint64_t& loss = (stream == k_actions) ? m_state.action_loss : m_state.text_loss;
    if (loss == UINT64_MAX)
    {
        close(UINT32_MAX);
        return EInputPost::failed;
    }
    ++loss;
    const bool success = overflow ? boundary(stream, EReason::overflow) : publish();
    return success ? (overflow ? EInputPost::overflow : EInputPost::suppressed) : EInputPost::failed;
}

EInputPost CInputConnection::post_action(const SActionInputV1 record) noexcept
{
    if (m_state.phase == EPhase::closing)
    {
        return EInputPost::closed;
    }
    if ((record.kind >= EActionKind::boundary) || record.sequence || record.flags)
    {
        return EInputPost::invalid;
    }
    bool found = false;
    for (std::size_t index = 0u; index < m_subjects.size(); ++index)
    {
        if (m_subjects[index] == record.subject)
        {
            found = true;
            break;
        }
    }
    if (!found) return EInputPost::invalid;
    if ((m_state.phase != EPhase::active) || (m_state.pending & k_actions))
    {
        return lose(k_actions, false);
    }
    return post_mapped(record, m_state.action_generation);
}

EInputPost CInputConnection::post_mapped(SActionInputV1 record, const std::uint64_t generation) noexcept
{
    //  Only the resolved mapper (or the validated low-level transport path above)
    //  can publish here. Physical eligibility is per binding, never per subject.
    if (m_state.phase == EPhase::closing)
    {
        return EInputPost::closed;
    }
    if ((generation != m_state.action_generation) || (m_state.phase != EPhase::active) ||
        (m_state.pending & k_actions))
    {
        return EInputPost::suppressed;
    }
    if (!m_actions.writable_count())
    {
        return lose(k_actions, true);
    }
    if (!next_sequence(record.sequence))
    {
        return EInputPost::failed;
    }
    if (!m_actions.post(record))
    {
        close(UINT32_MAX);
        return EInputPost::failed;
    }
    m_state.committed = record.sequence;
    return publish() ? EInputPost::published : EInputPost::failed;
}

bool CInputConnection::valid_text(const STextInputV1& record) noexcept
{
    if ((record.kind != ETextKind::scalar) || record.sequence || record.reserved ||
        !record.byte_count || (record.byte_count > 4u))
    {
        return false;
    }
    const unsigned char first = record.payload[0];
    std::uint32_t value = first;
    const std::uint32_t count = (first < 0x80u) ? 1u :
        (((first >= 0xc2u) && (first <= 0xdfu)) ? 2u :
        (((first >= 0xe0u) && (first <= 0xefu)) ? 3u :
        (((first >= 0xf0u) && (first <= 0xf4u)) ? 4u : 0u)));
    if (count != record.byte_count) return false;
    if (count > 1u) value &= (1u << (7u - count)) - 1u;
    for (std::uint32_t index = 1u; index < count; ++index)
    {
        if ((record.payload[index] & 0xc0u) != 0x80u)
        {
            return false;
        }
        value = (value << 6u) | (record.payload[index] & 0x3fu);
    }
    for (std::uint32_t index = count; index < sizeof(record.payload); ++index)
    {
        if (record.payload[index]) return false;
    }
    return (value <= 0x10ffffu) && !((value >= 0xd800u) && (value <= 0xdfffu)) &&
        ((count < 2u) || (value >= 0x80u)) && ((count < 3u) || (value >= 0x800u)) &&
        ((count < 4u) || (value >= 0x10000u));
}

EInputPost CInputConnection::post_text(STextInputV1 record) noexcept
{
    if (m_state.phase == EPhase::closing)
    {
        return EInputPost::closed;
    }
    if (!view.text || !valid_text(record))
    {
        return EInputPost::invalid;
    }
    if ((m_state.phase != EPhase::active) || (m_state.pending & k_text) || !m_state.text_enabled)
    {
        return lose(k_text, false);
    }
    if (!m_text.writable_count()) return lose(k_text, true);
    if (!next_sequence(record.sequence))
    {
        return EInputPost::failed;
    }
    if (!m_text.post(record))
    {
        close(UINT32_MAX);
        return EInputPost::failed;
    }
    m_state.committed = record.sequence;
    return publish() ? EInputPost::published : EInputPost::failed;
}

}   //  namespace platform::runtime
