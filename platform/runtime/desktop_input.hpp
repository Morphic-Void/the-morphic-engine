
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    desktop_input.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Bounded, Host-owned desktop ingestion and native cutover bookkeeping.

#pragma once

#include "runtime/input_connection.hpp"

namespace platform::runtime
{

inline constexpr std::uint32_t k_desktop_capacity = 1u << 8u;
inline constexpr std::uint32_t k_desktop_mask = k_desktop_capacity - 1u;
inline constexpr std::uint32_t k_desktop_changes = 8u;

struct SDesktopTransaction
{
#if defined(MV_INPUT_ACCEPTANCE)
    acceptance::SObservationTrace trace;
#endif
    std::uint64_t time_ns{};
    std::uint32_t count{}, raw_count{}, repeats{ 1u };
    SInputControl controls[k_desktop_changes]{};
    input::SActionInputV1 raw[2]{};
    //  Retain a UTF-16 code unit or explicitly tagged UTF-32 scalar. Decoding
    //  and repeat expansion run in the pump.
    std::uint32_t code_unit{};
    bool text{}, utf32{};
};

class CDesktopInput final
{
public:
    void attach(CInputConnection& connection, const std::uint64_t device) noexcept;
    void detach() noexcept;
    void cutover(const std::uint32_t mask) noexcept;
    void lost() noexcept;
    void synchronise() noexcept;
    [[nodiscard]] bool observe(const SDesktopTransaction& transaction) noexcept;
    //  Only the native adapter can certify a stable native-queue quiet point.
    void quiet(const SInputObservation& baseline, const std::uint64_t epoch) noexcept;
    [[nodiscard]] bool advance(SMapperBudget& budget) noexcept;
    [[nodiscard]] bool attached() const noexcept { return m_connection != nullptr; }
    [[nodiscard]] bool runnable() const noexcept;
    [[nodiscard]] std::uint64_t epoch() const noexcept { return m_epoch; }
    [[nodiscard]] std::uint64_t text_epoch() const noexcept { return m_text_epoch; }
    [[nodiscard]] bool text_pending() const noexcept { return (m_cutover & input::k_text) != 0u; }
    [[nodiscard]] bool needs_baseline() const noexcept
    {
        return m_connection && (m_cutover & input::k_actions) && !m_baseline_pending && !m_baseline_inflight;
    }
    [[nodiscard]] std::uint64_t malformed() const noexcept { return m_malformed; }

private:
    friend struct SInputAcceptanceAccess;
    struct SEntry
    {
        SDesktopTransaction transaction;
        std::uint64_t action_generation{}, text_generation{}, text_epoch{};
        bool suppressed{};
    };
    void malformed_text(const std::uint64_t time_ns) noexcept;
    CInputConnection* m_connection{ nullptr };
    SEntry m_entries[k_desktop_capacity]{};
    SInputObservation m_baseline;
    std::uint32_t m_head{}, m_count{}, m_repeat{}, m_cutover{};
    std::uint64_t m_device{}, m_ordinal{}, m_epoch{}, m_time{}, m_action_generation{}, m_text_generation{};
    std::uint64_t m_text_epoch{};
    std::uint64_t m_malformed{}, m_last_diagnostic{};
    std::uint16_t m_high{};
    std::uint32_t m_high_repeats{}, m_scalar{};
    bool m_scalar_valid{};
    bool m_quiet{}, m_baseline_pending{}, m_baseline_inflight{}, m_losing{};
};

}   // namespace platform::runtime
