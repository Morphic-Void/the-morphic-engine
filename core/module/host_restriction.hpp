
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_restriction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Host-owned availability state. Every shared word is atomic, including on Win32.

#pragma once

#include <atomic>
#include "module/module_identity.hpp"
#include "platform/threading/awake_clock.hpp"

namespace modules
{

struct SRestrictionReason { std::uint32_t value{ 0u }; };
struct SRestrictionScope { std::uint64_t value{ 0u }; };
enum class ERestrictionStatus : std::uint32_t { success, invalid_context, invalid_scope, busy, closing, failed };
struct SRestrictionEnterResult
{
    ERestrictionStatus status{ ERestrictionStatus::invalid_context };
    SRestrictionScope scope;
};

struct SHostRestrictionSnapshot
{
    std::uint32_t revision{ 0u };
    std::uint64_t generation{ 0u };
    SModuleInstance authority;
    std::uint64_t invocation{ 0u };
    std::uint64_t completed_ns{ 0u };
    std::uint64_t active_start_ns{ 0u };
    std::uint32_t depth{ 0u };
    std::uint32_t reasons{ 0u };
    std::uint32_t counts[32]{};
    std::uint32_t sampling{ 0u };
    platform::threading::WaitError error;
};

enum class ERestrictionRead : std::uint32_t { success, retry, failed };
class CHostRestrictionPublication;

struct SHostRestrictionReader
{
    const CHostRestrictionPublication* publication{ nullptr };
    [[nodiscard]] bool is_valid() const noexcept { return publication != nullptr; }
    [[nodiscard]] ERestrictionRead read(SHostRestrictionSnapshot& snapshot) const noexcept;
};

class CHostRestrictionPublication
{
public:
    CHostRestrictionPublication() noexcept = default;
    CHostRestrictionPublication(const CHostRestrictionPublication&) = delete;
    CHostRestrictionPublication& operator=(const CHostRestrictionPublication&) = delete;
    [[nodiscard]] SHostRestrictionReader reader() const noexcept { return { this }; }
    [[nodiscard]] bool can_publish(const std::uint32_t count = 1u) const noexcept;
    [[nodiscard]] bool publish(const SHostRestrictionSnapshot& snapshot) noexcept;
    void fail(SHostRestrictionSnapshot snapshot, const platform::threading::WaitError error) noexcept;
    [[nodiscard]] std::uint32_t revision() const noexcept { return m_revision.load(); }

private:
    friend struct SHostRestrictionReader;
    friend struct SHostRestrictionTestAccess;
    void store(const SHostRestrictionSnapshot& snapshot, const std::uint32_t revision) noexcept;
    static constexpr std::uint32_t k_terminal_revision = 0xfffffffeu;
    std::atomic<std::uint32_t> m_revision{ 0u };
    std::atomic<std::uint32_t> m_words[47]{};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

}   //  namespace modules
