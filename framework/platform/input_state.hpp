
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_state.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Coherent input authority, generation, ordering and loss state outside queues.

#pragma once

#include <atomic>
#include <cstdint>

namespace platform::input
{

inline constexpr std::uint32_t k_actions = 1u, k_text = 2u, k_desktop = 3u;
enum class EPhase : std::uint32_t { reserved, active, closing };
enum class EStateRead : std::uint32_t { success, retry, failed };
enum class EReason : std::uint32_t
{
    initial = 1u, context = 2u, overflow = 4u, text_target = 8u, suppressed = 16u
};

struct SInputSnapshot
{
    std::uint32_t revision{ 0u };
    EPhase phase{ EPhase::reserved };
    std::uint32_t pending{ 0u }, context{ 0u }, text_enabled{ 0u };
    std::uint32_t action_reasons{ 0u }, text_reasons{ 0u }, failure{ 0u };
    std::uint64_t action_generation{ 0u }, text_generation{ 0u }, transition{ 0u };
    std::uint64_t committed{ 0u }, action_loss{ 0u }, text_loss{ 0u }, text_target{ 0u };
};

class CInputPublication final
{
public:
    [[nodiscard]] bool publish(const SInputSnapshot& snapshot) noexcept;
    [[nodiscard]] EStateRead read(SInputSnapshot& snapshot) const noexcept;

private:
    friend struct SInputPublicationTestAccess;
    void store64(const std::uint32_t index, const std::uint64_t value) noexcept
    {
        m_words[index].store(static_cast<std::uint32_t>(value));
        m_words[index + 1u].store(static_cast<std::uint32_t>(value >> 32u));
    }
    [[nodiscard]] std::uint64_t load64(const std::uint32_t index) const noexcept
    {
        return m_words[index].load() | (static_cast<std::uint64_t>(m_words[index + 1u].load()) << 32u);
    }
    std::atomic<std::uint32_t> m_revision{ 0u };
    std::atomic<std::uint32_t> m_words[21]{};
};

inline bool CInputPublication::publish(const SInputSnapshot& snapshot) noexcept
{
    const std::uint32_t previous = m_revision.load();
    if (previous == 0xfffffffeu) return false;
    const std::uint32_t next = previous + 2u;
    const bool terminal = next == 0xfffffffeu;
    m_revision.store(next - 1u);
    m_words[0].store(static_cast<std::uint32_t>(terminal ? EPhase::closing : snapshot.phase));
    m_words[1].store(snapshot.pending);
    m_words[2].store(snapshot.context);
    m_words[3].store(snapshot.text_enabled);
    m_words[4].store(snapshot.action_reasons);
    m_words[5].store(snapshot.text_reasons);
    m_words[6].store(terminal ? UINT32_MAX : snapshot.failure);
    store64(7u, snapshot.action_generation);
    store64(9u, snapshot.text_generation);
    store64(11u, snapshot.transition);
    store64(13u, snapshot.committed);
    store64(15u, snapshot.action_loss);
    store64(17u, snapshot.text_loss);
    store64(19u, snapshot.text_target);
    m_revision.store(next);
    return !terminal;
}

inline EStateRead CInputPublication::read(SInputSnapshot& snapshot) const noexcept
{
    for (std::uint32_t attempt = 0u; attempt < 4u; ++attempt)
    {
        const std::uint32_t revision = m_revision.load();
        if (!revision || (revision & 1u))
        {
            continue;
        }
        SInputSnapshot candidate;
        candidate.revision = revision;
        candidate.phase = static_cast<EPhase>(m_words[0].load());
        candidate.pending = m_words[1].load();
        candidate.context = m_words[2].load();
        candidate.text_enabled = m_words[3].load();
        candidate.action_reasons = m_words[4].load();
        candidate.text_reasons = m_words[5].load();
        candidate.failure = m_words[6].load();
        candidate.action_generation = load64(7u);
        candidate.text_generation = load64(9u);
        candidate.transition = load64(11u);
        candidate.committed = load64(13u);
        candidate.action_loss = load64(15u);
        candidate.text_loss = load64(17u);
        candidate.text_target = load64(19u);
        if (revision != m_revision.load()) continue;
        snapshot = candidate;
        return candidate.failure ? EStateRead::failed : EStateRead::success;
    }
    return EStateRead::retry;
}

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

}   //  namespace platform::input
