
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_restriction.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26

#include "framework/module/host_restriction.hpp"

namespace modules
{

static void store_wide(std::atomic<std::uint32_t>* const words, std::uint32_t& index, const std::uint64_t value) noexcept
{
    words[index++].store(static_cast<std::uint32_t>(value));
    words[index++].store(static_cast<std::uint32_t>(value >> 32u));
}

static std::uint64_t read_wide(const std::atomic<std::uint32_t>* const words, std::uint32_t& index) noexcept
{
    const std::uint64_t low = words[index++].load();
    return low | (static_cast<std::uint64_t>(words[index++].load()) << 32u);
}

bool CHostRestrictionPublication::can_publish(const std::uint32_t count) const noexcept
{
    const auto revision = m_revision.load();
    return ((revision & 1u) == 0u) && (revision < k_terminal_revision) &&
        (count <= (k_terminal_revision - 2u - revision) / 2u);
}

void CHostRestrictionPublication::store(const SHostRestrictionSnapshot& snapshot, const std::uint32_t revision) noexcept
{
    m_revision.store(revision - 1u);
    std::uint32_t index = 0u;
    store_wide(m_words, index, snapshot.generation);
    store_wide(m_words, index, snapshot.authority.value);
    store_wide(m_words, index, snapshot.invocation);
    store_wide(m_words, index, snapshot.completed_ns);
    store_wide(m_words, index, snapshot.active_start_ns);
    m_words[index++].store(snapshot.depth);
    m_words[index++].store(snapshot.reasons);
    for (const auto count : snapshot.counts) m_words[index++].store(count);
    m_words[index++].store(snapshot.sampling);
    m_words[index++].store(static_cast<std::uint32_t>(snapshot.error.code));
    m_words[index].store(snapshot.error.native_error);
    m_revision.store(revision);
}

bool CHostRestrictionPublication::publish(const SHostRestrictionSnapshot& snapshot) noexcept
{
    if (!can_publish()) return false;
    store(snapshot, m_revision.load() + 2u);
    return true;
}

void CHostRestrictionPublication::fail(SHostRestrictionSnapshot snapshot, const platform::threading::WaitError error) noexcept
{
    if (m_revision.load() == k_terminal_revision) return;
    snapshot.error = error;
    snapshot.sampling = 0u;
    store(snapshot, k_terminal_revision);
}

ERestrictionRead SHostRestrictionReader::read(SHostRestrictionSnapshot& snapshot) const noexcept
{
    if (!publication) return ERestrictionRead::failed;
    for (std::uint32_t attempt = 0u; attempt < 4u; ++attempt)
    {
        const auto revision = publication->m_revision.load();
        if ((revision & 1u) != 0u) continue;
        SHostRestrictionSnapshot candidate;
        std::uint32_t index = 0u;
        const auto* words = publication->m_words;
        candidate.revision = revision;
        candidate.generation = read_wide(words, index);
        candidate.authority.value = read_wide(words, index);
        candidate.invocation = read_wide(words, index);
        candidate.completed_ns = read_wide(words, index);
        candidate.active_start_ns = read_wide(words, index);
        candidate.depth = words[index++].load();
        candidate.reasons = words[index++].load();
        for (auto& count : candidate.counts) count = words[index++].load();
        candidate.sampling = words[index++].load();
        candidate.error.code = static_cast<platform::threading::EWaitError>(words[index++].load());
        candidate.error.native_error = words[index].load();
        if (revision != publication->m_revision.load()) continue;
        snapshot = candidate;
        if ((candidate.generation == 0u) || (candidate.error.code != platform::threading::EWaitError::none))
            return ERestrictionRead::failed;
        return candidate.sampling ? ERestrictionRead::retry : ERestrictionRead::success;
    }
    return ERestrictionRead::retry;
}

}   //  namespace modules
