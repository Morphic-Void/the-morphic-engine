
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_restrictions.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26

#include "runtime/host_restrictions.hpp"
#include <limits>

namespace host
{

static std::atomic<std::uint32_t> s_generation{ 1u };

CHostRestrictions::CHostRestrictions(const platform::threading::SAwakeClock clock) noexcept : m_clock{ clock }
{
    auto generation = s_generation.load();
    while (generation && !s_generation.compare_exchange_weak(generation, generation == UINT32_MAX ? 0u : generation + 1u)) {}
    m_state.generation = generation;
    if (!generation) fail({ platform::threading::EWaitError::overflow, 0u });
    else (void)publish();
}

void CHostRestrictions::fail(const platform::threading::WaitError error) noexcept
{
    if (failed()) return;
    m_state.error = error.code == platform::threading::EWaitError::none ?
        platform::threading::WaitError{ platform::threading::EWaitError::invalid_argument, 0u } : error;
    m_state.sampling = 0u;
    m_publication.fail(m_state, m_state.error);
}

bool CHostRestrictions::publish() noexcept
{
    if (failed()) return false;
    if (m_publication.publish(m_state)) return true;
    fail({ platform::threading::EWaitError::overflow, 0u });
    return false;
}

bool CHostRestrictions::sample_boundary(std::uint64_t& value) noexcept
{
    if (failed()) return false;
    if (!m_publication.can_publish(2u))
    {
        fail({ platform::threading::EWaitError::overflow, 0u });
        return false;
    }
    m_state.sampling = 1u;
    if (!publish()) return false;
    const auto error = m_clock.read(value);
    if ((error.code != platform::threading::EWaitError::none) || (value < m_last_clock))
    {
        fail(error.code == platform::threading::EWaitError::none ?
            platform::threading::WaitError{ platform::threading::EWaitError::invalid_argument, 0u } : error);
        return false;
    }
    m_last_clock = value;
    m_state.sampling = 0u;
    return true;
}

bool CHostRestrictions::begin_invocation(const modules::SModuleInstance instance, const bool allow_restriction) noexcept
{
    if (m_invocation || !instance.value || !m_next_invocation) return false;
    m_invocation = m_next_invocation;
    m_next_invocation = m_next_invocation == UINT64_MAX ? 0u : m_next_invocation + 1u;
    m_invoking = instance;
    m_allow_restriction = allow_restriction;
    return true;
}

modules::SRestrictionEnterResult CHostRestrictions::enter(const modules::SRestrictionReason reason) noexcept
{
    using modules::ERestrictionStatus;
    if (!m_invocation || !m_allow_restriction || (reason.value >= 32u)) return {};
    if (failed()) return { ERestrictionStatus::failed, {} };
    if (!m_next_scope || (m_state.depth == 64u)) return { ERestrictionStatus::busy, {} };
    SScope* vacant = nullptr;
    for (auto& scope : m_scopes) if (!scope.token) { vacant = &scope; break; }
    if (!vacant) return { ERestrictionStatus::busy, {} };
    if (!m_state.depth)
    {
        std::uint64_t awake = 0u;
        if (!sample_boundary(awake)) return { ERestrictionStatus::failed, {} };
        m_state.active_start_ns = awake;
        m_state.authority = m_invoking;
        m_state.invocation = m_invocation;
    }
    else if (!m_publication.can_publish())
    {
        fail({ platform::threading::EWaitError::overflow, 0u });
        return { ERestrictionStatus::failed, {} };
    }
    const auto token = m_next_scope;
    m_next_scope = token == UINT64_MAX ? 0u : token + 1u;
    *vacant = { token, reason.value };
    ++m_state.depth;
    ++m_state.counts[reason.value];
    m_state.reasons |= 1u << reason.value;
    if (!publish()) return { ERestrictionStatus::failed, {} };
    return { ERestrictionStatus::success, { token } };
}

modules::ERestrictionStatus CHostRestrictions::leave(const modules::SRestrictionScope token) noexcept
{
    using modules::ERestrictionStatus;
    if (!m_invocation || !token.value) return ERestrictionStatus::invalid_scope;
    SScope* found = nullptr;
    for (auto& scope : m_scopes) if (scope.token == token.value) { found = &scope; break; }
    if (!found) return ERestrictionStatus::invalid_scope;
    if ((m_state.depth == 1u) && !failed())
    {
        std::uint64_t awake = 0u;
        if (sample_boundary(awake))
        {
            const auto duration = awake - m_state.active_start_ns;
            if (duration > UINT64_MAX - m_state.completed_ns) fail({ platform::threading::EWaitError::overflow, 0u });
            else m_state.completed_ns += duration;
        }
    }
    if (--m_state.counts[found->reason] == 0u) m_state.reasons &= ~(1u << found->reason);
    *found = {};
    if (--m_state.depth == 0u)
    {
        m_state.active_start_ns = 0u;
        m_state.authority = {};
        m_state.invocation = 0u;
    }
    return publish() ? ERestrictionStatus::success : ERestrictionStatus::failed;
}

bool CHostRestrictions::end_invocation() noexcept
{
    const bool balanced = m_state.depth == 0u;
    for (auto& scope : m_scopes) if (scope.token) (void)leave({ scope.token });
    m_invocation = 0u;
    m_invoking = {};
    m_allow_restriction = false;
    return balanced && !failed();
}

}   //  namespace host
