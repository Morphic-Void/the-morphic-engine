
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CNotification.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Coalesced non-owning notification handles. Quiesce all users before destruction.

#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>
#include "threading/CWaitPredicate.hpp"

namespace threading
{

class CNotification;

struct SNotificationEndpoint
{
    [[nodiscard]] bool is_valid() const noexcept;
    //  Reports predicate-control failure; the underlying native wake has no status.
    [[nodiscard]] bool signal() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

private:
    friend class CNotification;
    CNotification* m_state{ nullptr };
};

struct SNotificationReader
{
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool pending() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    void clear_pending() const noexcept;

private:
    friend class CNotification;
    CNotification* m_state{ nullptr };
};

class CNotification
{
public:
    CNotification() noexcept = default;
    CNotification(const CNotification&) = delete;
    CNotification& operator=(const CNotification&) = delete;
    CNotification(CNotification&&) = delete;
    CNotification& operator=(CNotification&&) = delete;

    //  Initialise once, before exposing handles. Targets outlive every user.
    [[nodiscard]] bool initialise(CWaitPredicate& predicate) noexcept;
    [[nodiscard]] bool initialise(std::atomic<std::uint32_t>& work_pending) noexcept;
    [[nodiscard]] SNotificationEndpoint endpoint() noexcept;
    [[nodiscard]] SNotificationReader reader() noexcept;

private:
    friend struct SNotificationEndpoint;
    friend struct SNotificationReader;
    [[nodiscard]] bool signal() noexcept;
    static constexpr std::uint32_t k_pending = 1u, k_failed = 2u;
    std::atomic<std::uint32_t> m_state{ 0u };
    CWaitPredicate* m_predicate{ nullptr };
    std::atomic<std::uint32_t>* m_work_pending{ nullptr };
};

inline bool CNotification::initialise(CWaitPredicate& predicate) noexcept
{
    if (m_predicate || m_work_pending || !predicate.has_control()) return false;
    m_predicate = &predicate;
    return true;
}

inline bool CNotification::initialise(std::atomic<std::uint32_t>& work_pending) noexcept
{
    if (m_predicate || m_work_pending) return false;
    m_work_pending = &work_pending;
    return true;
}

inline SNotificationEndpoint CNotification::endpoint() noexcept
{
    SNotificationEndpoint result;
    if (m_predicate || m_work_pending) result.m_state = this;
    return result;
}

inline SNotificationReader CNotification::reader() noexcept
{
    SNotificationReader result;
    if (m_predicate || m_work_pending) result.m_state = this;
    return result;
}

inline bool CNotification::signal() noexcept
{
    //  Even coalesced publications participate in the consumer's clear/recheck.
    const auto previous = m_state.fetch_or(k_pending, std::memory_order_acq_rel);
    if (previous & k_failed) return false;
    if (previous & k_pending) return true;
    if (m_work_pending)
    {
        m_work_pending->store(1u, std::memory_order_release);
        return true;
    }
    if (m_predicate && m_predicate->poke_epoch_and_wake_one()) return true;
    m_state.fetch_or(k_failed, std::memory_order_release);
    return false;
}

inline bool SNotificationEndpoint::is_valid() const noexcept { return m_state != nullptr; }
inline bool SNotificationEndpoint::signal() const noexcept { return m_state && m_state->signal(); }
inline bool SNotificationEndpoint::failed() const noexcept
{
    return !m_state || (m_state->m_state.load(std::memory_order_acquire) & CNotification::k_failed);
}
inline bool SNotificationReader::is_valid() const noexcept { return m_state != nullptr; }
inline bool SNotificationReader::pending() const noexcept
{
    return m_state && (m_state->m_state.load(std::memory_order_acquire) & CNotification::k_pending);
}
inline bool SNotificationReader::failed() const noexcept
{
    return !m_state || (m_state->m_state.load(std::memory_order_acquire) & CNotification::k_failed);
}
inline void SNotificationReader::clear_pending() const noexcept
{
    if (m_state) m_state->m_state.fetch_and(~CNotification::k_pending, std::memory_order_acq_rel);
}

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::is_standard_layout_v<SNotificationEndpoint> && std::is_trivially_copyable_v<SNotificationEndpoint>);
static_assert(std::is_standard_layout_v<SNotificationReader> && std::is_trivially_copyable_v<SNotificationReader>);

}   //  namespace threading
