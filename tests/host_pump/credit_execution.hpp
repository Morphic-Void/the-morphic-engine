
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    credit_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Serviced fixture custody through 600 retained disposal/release completions.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/assets/asset_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "platform/threading/wait_deadline.hpp"

namespace pump_tests
{

class CCreditExecution
{
public:
    explicit CCreditExecution(const modules::CCommunication& communication) noexcept : m_communication{ communication } {}
    void start() noexcept { m_active = true; }
    void stop() noexcept { m_stopping = true; }
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] bool ready_to_stop() const noexcept { return m_phase == EPhase::settle; }
    [[nodiscard]] unsigned pending_results() const noexcept { return (m_count - m_disposed) + (m_acquired - m_released); }
    [[nodiscard]] bool quiescent() const noexcept
    {
        return !m_active || (!m_unsafe && (m_phase == EPhase::settle) && (m_released == m_acquired) && (m_disposed == m_count));
    }
    [[nodiscard]] bool owns(const std::int32_t slot) const noexcept
    {
        return m_active && ((slot == 210) || (slot == 211) || (slot == 214) ||
            ((slot >= 1000) && (slot < 1600)) || ((slot >= 2000) && (slot < 2600)));
    }
    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        if (m_unsafe) return;
        const std::int32_t slot = message.query_async_slot();
        if ((slot == 210) && (m_phase == EPhase::transfer) && m_sent)
        {
            AssetResult result;
            if (!message.copy_payload_to(result) ||
                ((result.status != EAssetStatus::success) && result.asset.is_valid()))
            {
                unsafe();
                return;
            }
            m_sent = m_timed = false;
            if (result.status != EAssetStatus::success)
            {
                m_failed = true;
                m_phase = EPhase::dispose;
                return;
            }
            if (!result.asset.is_valid() || (result.kind != EAssetKind::raw) || (m_count == k_count))
            {
                unsafe();
                return;
            }
            m_items[m_count++].asset = result.asset;
            m_phase = EPhase::acquire;
            return;
        }
        if ((slot == 211) && (m_phase == EPhase::acquire) && m_sent)
        {
            AssetAcquireResult result;
            if (!message.copy_payload_to(result) ||
                ((result.status != EAssetStatus::success) && result.acquisition))
            {
                unsafe();
                return;
            }
            m_sent = m_timed = false;
            if (result.status != EAssetStatus::success)
            {
                m_failed = true;
                m_phase = EPhase::dispose; // Original asset still requires disposal.
                return;
            }
            if (!result.acquisition || !m_count)
            {
                unsafe();
                return;
            }
            m_items[m_count - 1u].acquisition = result.acquisition;
            ++m_acquired;
            m_phase = (m_count == k_count) ? EPhase::dispose : EPhase::transfer;
            return;
        }
        if ((slot == 214) && (m_phase == EPhase::marker) && m_sent)
        {
            AssetAcquireResult result;
            if (!message.copy_payload_to(result) || (result.status != EAssetStatus::invalid_asset) || result.acquisition)
            {
                unsafe();
                return;
            }
            m_sent = m_timed = false;
            m_phase = EPhase::release;
            return;
        }
        if ((slot >= 1000) && (slot < (1000 + static_cast<std::int32_t>(m_count))))
        {
            SItem& item = m_items[slot - 1000];
            AssetDisposeResult result;
            if (!item.dispose_sent || item.disposed || !message.copy_payload_to(result) ||
                (result.status != EAssetStatus::success) || (result.asset != item.asset))
            {
                unsafe();
                return;
            }
            item.disposed = true;
            ++m_disposed;
            return;
        }
        if ((slot >= 2000) && (slot < (2000 + static_cast<std::int32_t>(m_count))))
        {
            SItem& item = m_items[slot - 2000];
            AssetReleaseResult result;
            if (!item.release_sent || item.released || !message.copy_payload_to(result) ||
                (result.status != EAssetStatus::success) || (result.acquisition != item.acquisition))
            {
                unsafe();
                return;
            }
            item.released = true;
            ++m_released;
            return;
        }
        unsafe();
    }
    void advance(bool& runnable, std::uint64_t& next_wake) noexcept
    {
        if (!m_active || m_unsafe || quiescent())
        {
            return;
        }
        if ((m_stopping || m_failed) && !m_sent &&
            ((m_phase == EPhase::transfer) || (m_phase == EPhase::acquire)))
        {
            m_owner = {};
            m_phase = EPhase::dispose;
            m_timed = false;
        }
        if (!m_timed)
        {
            if (m_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none)
            {
                unsafe();
                return;
            }
            m_timed = true;
        }
        std::uint64_t remaining = 0u;
        if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            unsafe();
            return;
        }
        if (remaining < next_wake) next_wake = remaining;
        for (unsigned turn = 0u; turn < 4u; ++turn)
        {
            if (m_sent || (m_phase == EPhase::settle))
            {
                return;
            }
            threading::CErasedPodMsg pod;
            switch (m_phase)
            {
                case EPhase::transfer:
                {
                    if (!m_owner.has_owner())
                    {
                        m_owner = threading::make_owner_message<RawAssetTransfer>(210);
                        RawAssetTransfer* const transfer = m_owner.owner().payload<RawAssetTransfer>();
                        if (!transfer || !transfer->storage.value.resize(32u, 32u))
                        {
                            m_owner = {};
                            m_failed = true;
                            m_phase = EPhase::dispose;
                            continue;
                        }
                    }
                    break;
                }
                case EPhase::acquire:
                {
                    pod = threading::make_pod_message(211, AssetAcquireRequest{ m_items[m_count - 1u].asset });
                    break;
                }
                case EPhase::dispose:
                {
                    if (m_dispose_posted == m_count)
                    {
                        m_phase = (m_stopping || m_failed) ? EPhase::release : EPhase::marker;
                        continue;
                    }
                    pod = threading::make_pod_message((1000 + static_cast<std::int32_t>(m_dispose_posted)),
                        AssetDisposeRequest{ m_items[m_dispose_posted].asset });
                    break;
                }
                case EPhase::marker:
                {
                    // Same POD ingress lane: this result proves that all 600
                    // disposals were admitted before any acquisition release.
                    pod = threading::make_pod_message(214, AssetAcquireRequest{});
                    break;
                }
                case EPhase::release:
                {
                    if (m_release_posted == m_count)
                    {
                        m_phase = EPhase::settle;
                        return;
                    }
                    if (!m_items[m_release_posted].acquisition)
                    {
                        ++m_release_posted;
                        continue;
                    }
                    pod = threading::make_pod_message((2000 + static_cast<std::int32_t>(m_release_posted)),
                        AssetReleaseRequest{ m_items[m_release_posted].acquisition });
                    break;
                }
                case EPhase::settle:
                {
                    return;
                }
            }
            const modules::ESubmission result = (m_phase == EPhase::transfer) ?
                m_communication.submit({}, std::move(m_owner)) : m_communication.submit({}, pod);
            if (result == modules::ESubmission::busy) return;
            if (!modules::accepted(result))
            {
                // No new transport custody. Existing cleanup obligations still
                // prevent destruction if their publication can no longer occur.
                m_owner = {};
                unsafe();
                return;
            }
            if (m_phase == EPhase::dispose) m_items[m_dispose_posted++].dispose_sent = true;
            else if (m_phase == EPhase::release) m_items[m_release_posted++].release_sent = true;
            else m_sent = true;
        }
        runnable = !m_sent && (m_phase != EPhase::settle);
    }

private:
    static constexpr unsigned k_count = 600u;
    enum class EPhase : unsigned { transfer, acquire, dispose, marker, release, settle };
    struct SItem
    {
        CAssetId asset;
        CAssetAcquisitionId acquisition;
        bool dispose_sent{}, release_sent{}, disposed{}, released{};
    };
    const modules::CCommunication& m_communication;
    SItem m_items[k_count];
    threading::CErasedOwnerMsg m_owner;
    EPhase m_phase{ EPhase::transfer };
    unsigned m_count{}, m_acquired{}, m_dispose_posted{}, m_release_posted{}, m_disposed{}, m_released{};
    bool m_active{}, m_stopping{}, m_sent{}, m_failed{}, m_unsafe{}, m_timed{};
    platform::threading::CWaitDeadline m_deadline;
    void unsafe() noexcept { m_failed = m_unsafe = true; }
};

}   // namespace pump_tests
