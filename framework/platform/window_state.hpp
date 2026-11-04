
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    window_state.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Defines coherent window geometry and close-state snapshots, with atomic Host
//  publication and bounded reads that require no imported Platform function.

#pragma once

#include <atomic>
#include <cstdint>

namespace platform::windows
{

enum class EWindowPhase : std::uint32_t { open, close_pending, closing };
enum class EWindowRead : std::uint32_t { success, retry, failed };
struct SWindowSnapshot
{
    std::uint32_t revision{ 0u };
    std::uint32_t logical_width{ 0u }, logical_height{ 0u };
    std::uint32_t pixel_width{ 0u }, pixel_height{ 0u };
    std::uint32_t dpi{ 96u }, minimised{ 0u };
    EWindowPhase phase{ EWindowPhase::open };
    std::uint64_t close_request{ 0u };
    std::uint32_t failure{ 0u };
};

class CWindowPublication
{
public:
    [[nodiscard]] bool publish(const SWindowSnapshot& snapshot) noexcept;
    [[nodiscard]] EWindowRead read(SWindowSnapshot& snapshot) const noexcept;

private:
    friend struct SWindowTestAccess;
    std::atomic<std::uint32_t> m_revision{ 0u };
    std::atomic<std::uint32_t> m_words[10]{};
};

inline bool CWindowPublication::publish(const SWindowSnapshot& snapshot) noexcept
{
    const auto previous = m_revision.load();
    if (previous == 0xfffffffeu) return false;
    const auto next = previous + 2u;
    const bool terminal = next == 0xfffffffeu;
    m_revision.store(next - 1u);
    m_words[0].store(snapshot.logical_width);
    m_words[1].store(snapshot.logical_height);
    m_words[2].store(snapshot.pixel_width);
    m_words[3].store(snapshot.pixel_height);
    m_words[4].store(snapshot.dpi);
    m_words[5].store(snapshot.minimised);
    m_words[6].store(static_cast<std::uint32_t>(terminal ? EWindowPhase::closing : snapshot.phase));
    m_words[7].store(static_cast<std::uint32_t>(snapshot.close_request));
    m_words[8].store(static_cast<std::uint32_t>(snapshot.close_request >> 32u));
    m_words[9].store(terminal ? UINT32_MAX : snapshot.failure);
    m_revision.store(next);
    return !terminal;
}

inline EWindowRead CWindowPublication::read(SWindowSnapshot& snapshot) const noexcept
{
    for (std::uint32_t attempt = 0u; attempt < 4u; ++attempt)
    {
        const auto revision = m_revision.load();
        if (!revision || (revision & 1u)) continue;
        SWindowSnapshot candidate;
        candidate.revision = revision;
        candidate.logical_width = m_words[0].load();
        candidate.logical_height = m_words[1].load();
        candidate.pixel_width = m_words[2].load();
        candidate.pixel_height = m_words[3].load();
        candidate.dpi = m_words[4].load();
        candidate.minimised = m_words[5].load();
        candidate.phase = static_cast<EWindowPhase>(m_words[6].load());
        candidate.close_request = m_words[7].load();
        candidate.close_request |= static_cast<std::uint64_t>(m_words[8].load()) << 32u;
        candidate.failure = m_words[9].load();
        if (revision != m_revision.load()) continue;
        snapshot = candidate;
        return candidate.failure ? EWindowRead::failed : EWindowRead::success;
    }
    return EWindowRead::retry;
}

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

}   //  namespace platform::windows
