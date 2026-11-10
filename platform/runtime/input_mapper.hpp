
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_mapper.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Portable observation ownership and resumable, allocation-free action mapping.

#pragma once

#include "runtime/input_plan.hpp"
#include "framework/platform/input.hpp"
#include "runtime/input_trace.hpp"

namespace platform::runtime
{

class CInputConnection;
struct SInputInstallation;

inline constexpr std::uint32_t k_input_controls = input_control::k_storage_count;
enum class EObservationKind { transition, snapshot, text };
enum class EInputAdmission { accepted, busy, invalid, stale, baseline_required, closed };

struct SInputControl
{
    std::uint32_t control{};
    float x{}, y{};
    bool repeat{};
};

//  Sorted, unique controls. A snapshot replaces knowledge of the complete device;
//  absent controls become unknown. Virtual modifier controls are adapter supplied.
//  Digital controls use x=0/1; sticks/triggers are normalized; pointer and wheel
//  values retain logical coordinates and fractional steps respectively.
struct SInputObservation
{
#if defined(MV_INPUT_ACCEPTANCE)
    acceptance::SObservationTrace trace;
#endif
    std::uint64_t device{}, ordinal{}, time_ns{};
    EObservationKind kind{ EObservationKind::transition };
    bool baseline{}, suppressed{}, source_time_valid{ true };
    std::uint32_t count{};
    SInputControl controls[k_input_controls]{};
    input::STextInputV1 text;
    std::uint32_t raw_count{};
    input::SActionInputV1 raw[2]{};
};

struct SMapperBudget
{
    std::uint32_t work{ 2048u }, outputs{ 64u };
};

class CInputMapper final
{
public:
    explicit CInputMapper(CInputConnection& connection) noexcept : m_connection{ connection } {}
    void initialise(const SInputInstallation& installation) noexcept;
    [[nodiscard]] bool prepared() const noexcept { return m_states.size() == m_count; }
    [[nodiscard]] bool prepare_step() noexcept;
    [[nodiscard]] std::uint32_t prepare_cost() const noexcept { return (m_states.size() & k_input_page_mask) ? 1u : k_input_page_entries; }
    [[nodiscard]] bool clear_step() noexcept { return m_states.clear_page(); }
    [[nodiscard]] EInputAdmission submit(const SInputObservation& observation) noexcept;
    //  One elementary continuation step. False means blocked or out of credits.
    [[nodiscard]] bool advance(SMapperBudget& budget) noexcept;
    [[nodiscard]] bool pending() const noexcept { return m_stage != EStage::idle; }
    void invalidate(const bool preserve_text = false) noexcept;
    void text_changed() noexcept;
    void eligible(const bool value) noexcept;
    void device(const std::uint64_t instance) noexcept;
    void reset_source(const std::uint64_t instance) noexcept;
    void discontinuity() noexcept;
    void require_baseline(const bool preserve_text = false) noexcept { invalidate(preserve_text); m_need_baseline = true; }

private:
    friend struct SInputAcceptanceAccess;
    enum class EStage { idle, copy, apply, raw, binding, modifiers, evaluate, output, finish };
    struct SPhysical
    {
        float x{}, y{};
        bool valid{}, changed{}, repeat{};
    };
    struct SBindingState
    {
        std::uint64_t generation{};
        bool armed{}, neutral{}, held{}, release{};
        float x{}, y{}, published_x{}, published_y{};
        bool published_held{};
    };
    [[nodiscard]] const SInputBinding& binding(const std::uint32_t index) const noexcept;
    [[nodiscard]] bool allowed() const noexcept;
    void evaluate() noexcept;
    [[nodiscard]] bool output(SMapperBudget& budget) noexcept;
    void begin_evaluation() noexcept;

    CInputConnection& m_connection;
    const SInputPlan* m_plan{ nullptr };
    TConfigurationPages<SBindingState> m_states;
    std::uint32_t m_begin{}, m_count{}, m_cursor{}, m_binding{}, m_group{}, m_modifier{};
    SPhysical m_before[k_input_controls]{}, m_physical[k_input_controls]{};
    SInputObservation m_observation;
    EStage m_stage{ EStage::idle };
    std::uint64_t m_device{}, m_ordinal{}, m_time{}, m_generation{}, m_text_generation{};
    float m_x{}, m_y{};
    bool m_held{}, m_cancelled{}, m_modifiers{}, m_previous_modifiers{}, m_pulse{};
    bool m_modifier_valid{};
    bool m_eligible{ true }, m_need_baseline{ true }, m_admitted{}, m_selective{}, m_group_started{};
    bool m_disconnected{}, m_text_admitted{};
};

}   // namespace platform::runtime
