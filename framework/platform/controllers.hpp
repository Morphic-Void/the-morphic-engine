
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    controllers.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    09 Nov 26
//
//  Portable controller catalogue queries and exclusive device assignment.

#pragma once

#include "input.hpp"

namespace platform::input
{

inline constexpr std::uint32_t k_controller_capacity = 4u, k_device_capacity = 16u;
inline constexpr std::uint64_t k_default_device = 0u, k_unassigned_device = UINT64_MAX;
inline constexpr std::uint32_t k_device_eligible = 1u, k_device_claimed = 2u, k_device_end = 4u;
enum class EControllerBackend : std::uint32_t { initialising, ready, unavailable, failed, closing };

struct SCatalogueSnapshot
{
    std::uint64_t revision{};
    EControllerBackend backend{ EControllerBackend::initialising };
    std::uint32_t count{}, overflow{}, error{};
};

class CControllerPublication final
{
public:
    [[nodiscard]] bool publish(const SCatalogueSnapshot& value) noexcept
    {
        const std::uint32_t previous = m_sequence.load();
        if (previous == 0xfffffffeu) return false;
        const bool terminal = previous == 0xfffffffcu;
        m_sequence.store(previous + 1u);
        m_words[0].store(static_cast<std::uint32_t>(value.revision));
        m_words[1].store(static_cast<std::uint32_t>(value.revision >> 32u));
        m_words[2].store(static_cast<std::uint32_t>(terminal ? EControllerBackend::failed : value.backend));
        m_words[3].store(value.count);
        m_words[4].store(value.overflow);
        m_words[5].store(terminal ? UINT32_MAX : value.error);
        m_sequence.store(previous + 2u);
        return !terminal;
    }
    [[nodiscard]] EStateRead read(SCatalogueSnapshot& value) const noexcept
    {
        for (std::uint32_t attempt = 0u; attempt < 4u; ++attempt)
        {
            const std::uint32_t before = m_sequence.load();
            if (!before || (before & 1u))
            {
                continue;
            }
            SCatalogueSnapshot candidate;
            candidate.revision = m_words[0].load() | (static_cast<std::uint64_t>(m_words[1].load()) << 32u);
            candidate.backend = static_cast<EControllerBackend>(m_words[2].load());
            candidate.count = m_words[3].load();
            candidate.overflow = m_words[4].load();
            candidate.error = m_words[5].load();
            if (before != m_sequence.load()) continue;
            value = candidate;
            return EStateRead::success;
        }
        return EStateRead::retry;
    }
private:
    std::atomic<std::uint32_t> m_sequence{};
    std::atomic<std::uint32_t> m_words[6]{};
};

struct SControllerCatalogueV1
{
    std::uint32_t size{ sizeof(SControllerCatalogueV1) }, version{ 1u };
    modules::SModuleInstance provider, consumer;
    const CControllerPublication* state{};
    std::uint32_t pipe_capacity{ k_controller_capacity }, device_capacity{ k_device_capacity };
};
struct SCatalogueReady
{
    services::SServiceBorrow borrow;
    services::SNotificationRegistration registration;
    services::SServiceRequestId acquisition;
};
struct SEnumerateDevice
{
    services::SServiceBorrow borrow;
    std::uint64_t revision{};
    std::uint32_t cursor{}, reserved{};
};
struct SDeviceResult
{
    std::uint64_t revision{}, device{};
    std::uint32_t next_cursor{}, flags{};
};
struct SDeviceMetadata
{
    services::SServiceBorrow borrow;
    std::uint64_t device{};
    std::uint32_t page{}, reserved{};
};
struct SDeviceMetadataResult
{
    std::uint64_t device{};
    std::uint16_t vendor{}, product{};
    std::uint32_t page{};
    char name[8]{};
};
//  The expected action generation covers both context and assignment changes.
struct SAssignDevice
{
    SConnectionToken connection;
    std::uint64_t device{}, action_generation{};
};

static_assert((sizeof(SCatalogueReady) == 24u) && (sizeof(SEnumerateDevice) == 24u) &&
    (sizeof(SDeviceResult) == 24u) && (sizeof(SAssignDevice) == 24u) && (sizeof(SDeviceMetadataResult) == 24u));

}   //  namespace platform::input
