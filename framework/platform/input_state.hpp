
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
    initial = 1u, context = 2u, overflow = 4u, text_target = 8u, suppressed = 16u,
    capture_lost = 32u, native_history = 64u, assignment = 128u, disconnected = 256u, source_clock = 512u
};
enum class ECaptureReason : std::uint32_t { none, released, lost, invalidated, closing };
enum class EControllerState : std::uint32_t { unassigned, assigned, disconnected, unavailable, invalid_sample };

struct SInputSnapshot
{
    std::uint32_t revision{ 0u };
    EPhase phase{ EPhase::reserved };
    std::uint32_t pending{ 0u }, context{ 0u }, text_enabled{ 0u };
    std::uint32_t action_reasons{ 0u }, text_reasons{ 0u }, failure{ 0u };
    std::uint64_t action_generation{ 0u }, text_generation{ 0u }, transition{ 0u };
    std::uint64_t committed{ 0u }, action_loss{ 0u }, text_loss{ 0u }, text_target{ 0u };
    std::uint32_t ingress_pending{};
    ECaptureReason capture_reason{ ECaptureReason::none };
    std::uint64_t capture_grant{}, capture_revision{}, capture_revocations{}, ingress_loss{};
    std::uint64_t device{}, assignment_revision{}, clock_domain{}, clock_epoch{}, receipt_awake_ns{};
    EControllerState controller_state{ EControllerState::unassigned };
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
    std::atomic<std::uint32_t> m_words[42]{};
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
    m_words[21].store(snapshot.ingress_pending);
    m_words[22].store(static_cast<std::uint32_t>(snapshot.capture_reason));
    store64(23u, snapshot.capture_grant);
    store64(25u, snapshot.capture_revision);
    store64(27u, snapshot.capture_revocations);
    store64(29u, snapshot.ingress_loss);
    store64(31u, snapshot.device);
    store64(33u, snapshot.assignment_revision);
    store64(35u, snapshot.clock_domain);
    store64(37u, snapshot.clock_epoch);
    store64(39u, snapshot.receipt_awake_ns);
    m_words[41].store(static_cast<std::uint32_t>(snapshot.controller_state));
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
        candidate.ingress_pending = m_words[21].load();
        candidate.capture_reason = static_cast<ECaptureReason>(m_words[22].load());
        candidate.capture_grant = load64(23u);
        candidate.capture_revision = load64(25u);
        candidate.capture_revocations = load64(27u);
        candidate.ingress_loss = load64(29u);
        candidate.device = load64(31u);
        candidate.assignment_revision = load64(33u);
        candidate.clock_domain = load64(35u);
        candidate.clock_epoch = load64(37u);
        candidate.receipt_awake_ns = load64(39u);
        candidate.controller_state = static_cast<EControllerState>(m_words[41].load());
        if (revision != m_revision.load()) continue;
        snapshot = candidate;
        return candidate.failure ? EStateRead::failed : EStateRead::success;
    }
    return EStateRead::retry;
}

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

}   //  namespace platform::input
