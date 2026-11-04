
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_restrictions.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Single Host writer and bounded scope ledger. No callback or allocation in publication.

#pragma once

#include "module/host_restriction.hpp"

namespace host
{

class CHostRestrictions
{
public:
    explicit CHostRestrictions(const platform::threading::SAwakeClock clock = {}) noexcept;
    [[nodiscard]] modules::SHostRestrictionReader reader() const noexcept { return m_publication.reader(); }
    [[nodiscard]] std::uint32_t revision() const noexcept { return m_publication.revision(); }
    [[nodiscard]] bool begin_invocation(const modules::SModuleInstance instance, const bool allow_restriction) noexcept;
    [[nodiscard]] bool end_invocation() noexcept;
    [[nodiscard]] modules::SRestrictionEnterResult enter(const modules::SRestrictionReason reason) noexcept;
    [[nodiscard]] modules::ERestrictionStatus leave(const modules::SRestrictionScope scope) noexcept;
    [[nodiscard]] bool active() const noexcept { return m_state.depth != 0u; }
    [[nodiscard]] bool failed() const noexcept { return m_state.error.code != platform::threading::EWaitError::none; }
    [[nodiscard]] platform::threading::WaitError now(std::uint64_t& value) const noexcept { return m_clock.read(value); }
    void fail(const platform::threading::WaitError error) noexcept;

private:
    friend struct SHostRestrictionTestAccess;
    [[nodiscard]] bool sample_boundary(std::uint64_t& value) noexcept;
    [[nodiscard]] bool publish() noexcept;
    struct SScope { std::uint64_t token{ 0u }; std::uint32_t reason{ 0u }; };
    platform::threading::SAwakeClock m_clock;
    modules::CHostRestrictionPublication m_publication;
    modules::SHostRestrictionSnapshot m_state;
    SScope m_scopes[64];
    std::uint64_t m_next_scope{ 1u }, m_next_invocation{ 1u }, m_last_clock{ 0u };
    modules::SModuleInstance m_invoking;
    std::uint64_t m_invocation{ 0u };
    bool m_allow_restriction{ false };
};

}   //  namespace host
