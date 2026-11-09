
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_mapper.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Evaluates coherent observations with binding-local rearming and bounded work.

#include <algorithm>
#include <cmath>
#include <cstring>
#include "runtime/input_connection.hpp"

namespace platform::runtime
{

void CInputMapper::initialise(const SInputInstallation& installation) noexcept
{
    m_plan = installation.plan;
    m_begin = installation.binding_begin;
    m_count = installation.binding_count;
}

bool CInputMapper::prepare_step() noexcept
{
    return prepared() || m_states.push(SBindingState{});
}

const SInputBinding& CInputMapper::binding(const std::uint32_t index) const noexcept
{
    return m_plan->bindings.at(m_begin + index);
}

bool CInputMapper::allowed() const noexcept
{
    const input::SInputSnapshot& state = m_connection.snapshot();
    return m_eligible && (state.phase == input::EPhase::active) &&
        !((state.pending | state.ingress_pending) & input::k_actions);
}

EInputAdmission CInputMapper::submit(const SInputObservation& observation) noexcept
{
    if (m_connection.snapshot().phase == input::EPhase::closing)
    {
        return EInputAdmission::closed;
    }
    if (pending() || !prepared())
    {
        return EInputAdmission::busy;
    }
    if (!observation.device || !observation.ordinal || (observation.count > k_input_controls) ||
        ((observation.kind != EObservationKind::snapshot) && (observation.kind != EObservationKind::transition) &&
            (observation.kind != EObservationKind::text)) ||
        (observation.baseline && (observation.kind != EObservationKind::snapshot)) ||
        ((observation.kind == EObservationKind::transition) && !observation.count) ||
        (observation.raw_count > 2u) || (observation.raw_count && !m_connection.view.text) ||
        ((observation.kind == EObservationKind::text) &&
            (observation.count || observation.raw_count || !m_connection.view.text || !m_connection.valid_text(observation.text))))
    {
        return EInputAdmission::invalid;
    }
    if (m_disconnected || (m_device && (observation.device != m_device)) || (observation.ordinal <= m_ordinal) ||
        (observation.source_time_valid && (observation.time_ns < m_time)))
    {
        return EInputAdmission::stale;
    }
    if (m_need_baseline && !observation.baseline && (observation.kind != EObservationKind::text))
    {
        return EInputAdmission::baseline_required;
    }
    if (observation.baseline && !m_need_baseline)
    {
        return EInputAdmission::invalid;
    }
    std::uint32_t previous = 0u;
    for (std::uint32_t index = 0u; index < observation.count; ++index)
    {
        const SInputControl& control = observation.controls[index];
        const std::uint32_t code = control.control;
        const bool desktop = input_control::is_desktop(code);
        const bool controller = input_control::is_controller(code);
        if ((code <= previous) || (!desktop && !controller) ||
            (controller != (m_connection.view.kind == input::EConnection::controller)))
        {
            return EInputAdmission::invalid;
        }
        previous = code;
    }
    for (std::uint32_t index = 0u; index < observation.raw_count; ++index)
    {
        const input::SActionInputV1& record = observation.raw[index];
        if ((record.kind < input::EActionKind::pointer) || (record.kind > input::EActionKind::modifiers) ||
            record.sequence || record.flags)
        {
            return EInputAdmission::invalid;
        }
    }
    m_observation = observation;
    m_device = observation.device;
    m_ordinal = observation.ordinal;
    if (observation.source_time_valid) m_time = observation.time_ns;
    m_generation = m_connection.snapshot().action_generation;
    m_text_generation = m_connection.snapshot().text_generation;
    m_text_admitted = m_eligible && (m_connection.snapshot().phase == input::EPhase::active) &&
        m_connection.snapshot().text_enabled && !observation.suppressed &&
        !((m_connection.snapshot().pending | m_connection.snapshot().ingress_pending) & input::k_text);
    m_admitted = allowed() && !observation.suppressed;
    m_selective = false;
    m_cursor = 0u;
    m_stage = (observation.kind == EObservationKind::text) ? EStage::finish : EStage::copy;
    return EInputAdmission::accepted;
}

void CInputMapper::invalidate(const bool preserve_text) noexcept
{
    if (preserve_text && (m_stage == EStage::finish) && (m_observation.kind == EObservationKind::text))
    {
        return;
    }
    //  An interrupted apply has no coherent physical result to retain.
    if ((m_stage == EStage::apply) || (m_stage == EStage::copy)) m_need_baseline = true;
    m_stage = EStage::idle;
    m_group_started = false;
}

void CInputMapper::discontinuity() noexcept
{
    m_need_baseline = true;
    (void)m_connection.boundary((m_connection.view.text ? input::k_desktop : input::k_actions), input::EReason::suppressed);
}

void CInputMapper::device(const std::uint64_t instance) noexcept
{
    if ((instance == m_device) && (instance || m_disconnected))
    {
        return;
    }
    m_device = instance;
    m_disconnected = !instance;
    m_ordinal = 0u;
    discontinuity();
}

void CInputMapper::reset_source(const std::uint64_t instance) noexcept
{
    invalidate();
    m_device = instance;
    m_disconnected = !instance;
    m_ordinal = m_time = 0u;
    m_need_baseline = true;
}

void CInputMapper::eligible(const bool value) noexcept
{
    if (m_eligible == value) return;
    m_eligible = value;
    if (!value)
    {
        (void)m_connection.boundary((m_connection.view.text ? input::k_desktop : input::k_actions), input::EReason::suppressed);
    }
}

void CInputMapper::begin_evaluation() noexcept
{
    m_binding = 0u;
    m_group_started = false;
    m_stage = EStage::binding;
}

void CInputMapper::text_changed() noexcept
{
    m_selective = true;
    m_generation = m_connection.snapshot().action_generation;
    m_admitted = allowed();
    //  This pass has no physical transitions or relative movement.
    m_observation.kind = EObservationKind::snapshot;
    m_observation.baseline = false;
    m_observation.time_ns = m_time;
    begin_evaluation();
}

static float projection(const float x, const float y, const EInputDirection direction) noexcept
{
    switch (direction)
    {
        case EInputDirection::positive_x: return x;
        case EInputDirection::negative_x: return -x;
        case EInputDirection::positive_y: return y;
        case EInputDirection::negative_y: return -y;
        default: return x;
    }
}

void CInputMapper::evaluate() noexcept
{
    const SInputBinding& instruction = binding(m_binding);
    SBindingState& state = m_states.at(m_binding);
    const SPhysical& before = m_before[instruction.control];
    const SPhysical& after = m_physical[instruction.control];
    const SInputParameters& parameters = instruction.parameters;
    const bool analog = (instruction.source == EInputSource::stick) || (instruction.source == EInputSource::trigger);
    const bool position = instruction.source == EInputSource::position;
    const bool wheel = instruction.source == EInputSource::wheel;
    const bool keyboard = instruction.source == EInputSource::key;
    float x = after.x, y = after.y;
    const float magnitude = (instruction.source == EInputSource::stick) ?
        static_cast<float>(std::hypot(static_cast<double>(x), static_cast<double>(y))) : std::abs(x);
    if (state.generation != m_generation)
    {
        state = {};
        state.generation = m_generation;
        const double prior = (instruction.source == EInputSource::stick) ?
            std::hypot(static_cast<double>(before.x), static_cast<double>(before.y)) : std::abs(before.x);
        state.armed = before.valid && ((position || wheel) || (prior <= parameters.neutral_enter));
    }
    state.neutral = after.valid && ((position || wheel) || (analog ?
        (state.neutral ? (magnitude < parameters.neutral_leave) : (magnitude <= parameters.neutral_enter)) : (x == 0.0f)));
    if (instruction.source == EInputSource::stick)
    {
        const double length = std::hypot(static_cast<double>(x), static_cast<double>(y));
        const double factor = (length > parameters.dead_zone) ?
            ((std::min(length, 1.0) - parameters.dead_zone) / ((1.0 - parameters.dead_zone) * length)) : 0.0;
        x = static_cast<float>(x * factor);
        y = static_cast<float>(y * factor);
    }
    else if (instruction.source == EInputSource::trigger)
    {
        x = std::max(0.0f, ((std::clamp(x, 0.0f, 1.0f) - parameters.dead_zone) / (1.0f - parameters.dead_zone)));
        y = 0.0f;
    }
    if (instruction.direction != EInputDirection::whole)
    {
        x = projection(x, y, instruction.direction);
        y = 0.0f;
    }
    const float threshold = std::max(0.0f, x);
    x *= parameters.scale;
    y *= parameters.scale;
    const bool finite = std::isfinite(x) && std::isfinite(y);
    const bool enabled = !keyboard || instruction.allow_text ||
        (!m_connection.snapshot().text_enabled && !(m_connection.snapshot().ingress_pending & input::k_text));
    const bool cancel = !after.valid || !m_modifier_valid || !finite || !m_admitted || !allowed() || !enabled || m_observation.baseline;
    m_pulse = false;
    if (cancel)
    {
        m_cancelled = m_cancelled || state.held || (state.x != 0.0f) || (state.y != 0.0f);
        state.held = state.release = false;
        state.x = state.y = 0.0f;
        state.armed = state.neutral && finite;
    }
    else if (!m_selective)
    {
        if (state.neutral) state.armed = true;
        const bool down = after.x != 0.0f;
        const bool pressed = before.valid && (before.x == 0.0f) && down;
        const bool released = before.valid && (before.x != 0.0f) && !down;
        const bool qualifies = state.armed && pressed && m_modifiers;
        switch (instruction.activation)
        {
            case EInputActivation::regular:
            case EInputActivation::chord:
            {
                state.held = (state.held && down && m_modifiers) || qualifies;
                break;
            }
            case EInputActivation::start:
            {
                m_pulse = qualifies || (instruction.repeat && after.repeat && state.held && down && m_modifiers);
                state.held = (state.held && down && m_modifiers) || qualifies;
                break;
            }
            case EInputActivation::release:
            {
                m_pulse = released && state.release && m_previous_modifiers;
                state.release = (state.release && down && m_modifiers) || qualifies;
                break;
            }
            case EInputActivation::threshold:
            {
                state.held = state.armed && (state.held ? (threshold > parameters.release) : (threshold >= parameters.press));
                break;
            }
            case EInputActivation::direct:
            {
                const bool use = state.armed && (!wheel || after.changed);
                state.x = use ? x : 0.0f;
                state.y = use ? y : 0.0f;
                break;
            }
        }
    }
    m_held = m_held || state.held;
    if (!wheel || !m_selective)
    {
        m_x += state.x;
        m_y += state.y;
    }
    ++m_binding;
    const bool group_end = (m_binding == m_count) || (binding(m_binding).context != instruction.context) ||
        (binding(m_binding).action != instruction.action);
    if (m_pulse || group_end) m_stage = EStage::output;
    else m_stage = EStage::binding;
}

bool CInputMapper::output(SMapperBudget& budget) noexcept
{
    const SInputBinding& instruction = binding(m_group);
    const bool group_end = (m_binding == m_count) || (binding(m_binding).context != instruction.context) ||
        (binding(m_binding).action != instruction.action);
    SBindingState& state = m_states.at(m_group);
    input::SActionInputV1 record;
    record.subject = instruction.action;
    record.time_ns = m_observation.time_ns;
    if (!m_observation.source_time_valid) record.flags |= input::k_no_source_time;
    bool emit = false;
    switch (instruction.action_kind)
    {
        case EInputAction::digital:
        {
            record.kind = input::EActionKind::digital;
            const input::SDigitalAction payload{ m_held ? 1u : 0u };
            std::memcpy(record.payload, &payload, sizeof(payload));
            emit = group_end && (state.published_held != m_held);
            break;
        }
        case EInputAction::pulse:
        {
            record.kind = input::EActionKind::pulse;
            const input::SPulseAction payload{ binding(m_binding - 1u).id };
            std::memcpy(record.payload, &payload, sizeof(payload));
            emit = m_pulse;
            break;
        }
        case EInputAction::scalar:
        case EInputAction::vector:
        case EInputAction::delta:
        {
            record.kind = (instruction.action_kind == EInputAction::scalar) ? input::EActionKind::scalar :
                ((instruction.action_kind == EInputAction::vector) ? input::EActionKind::vector : input::EActionKind::delta);
            if (!std::isfinite(m_x) || !std::isfinite(m_y))
            {
                m_x = m_y = 0.0f;
                m_cancelled = true;
            }
            const input::SVectorAction payload{ m_x, m_y };
            std::memcpy(record.payload, &payload, sizeof(payload));
            emit = (instruction.action_kind == EInputAction::delta) ? ((m_x != 0.0f) || (m_y != 0.0f) || m_cancelled) :
                ((state.published_x != m_x) || (state.published_y != m_y));
            break;
        }
    }
    if (m_cancelled && !m_held && (m_x == 0.0f) && (m_y == 0.0f) && (instruction.action_kind != EInputAction::pulse))
    {
        record.flags |= input::k_action_cancelled;
    }
    if (emit && m_admitted && allowed() && !m_observation.baseline)
    {
        if (!budget.outputs) return false;
        --budget.outputs;
        if (m_connection.post_mapped(record, m_generation) != EInputPost::published)
        {
            //  A published prefix is never replayed after overflow/cancellation.
            invalidate();
            return true;
        }
    }
    if (group_end)
    {
        state.published_held = m_held;
        state.published_x = m_x;
        state.published_y = m_y;
        m_group_started = false;
    }
    m_pulse = false;
    m_stage = EStage::binding;
    return true;
}

bool CInputMapper::advance(SMapperBudget& budget) noexcept
{
    if (!budget.work || !pending())
    {
        return false;
    }
    if ((m_connection.snapshot().phase == input::EPhase::closing) ||
        ((m_generation != m_connection.snapshot().action_generation) && (m_observation.kind != EObservationKind::text)))
    {
        invalidate();
        return false;
    }
    if (((m_stage == EStage::output) || (m_stage == EStage::raw)) && !budget.outputs)
    {
        return false;
    }
    --budget.work;
    switch (m_stage)
    {
        case EStage::copy:
        {
            m_before[m_cursor] = m_physical[m_cursor];
            m_physical[m_cursor].changed = m_physical[m_cursor].repeat = false;
            if (m_observation.kind == EObservationKind::snapshot) m_physical[m_cursor].valid = false;
            if (++m_cursor == k_input_controls)
            {
                m_cursor = 0u;
                m_stage = EStage::apply;
            }
            break;
        }
        case EStage::apply:
        {
            if (m_cursor == m_observation.count)
            {
                m_need_baseline = false;
                m_cursor = 0u;
                m_stage = EStage::raw;
                break;
            }
            const SInputControl& value = m_observation.controls[m_cursor++];
            const bool digital = input_control::is_digital(value.control);
            const bool valid = std::isfinite(value.x) && std::isfinite(value.y) &&
                (!digital || (((value.x == 0.0f) || (value.x == 1.0f)) && (value.y == 0.0f)));
            m_physical[value.control] = { value.x, value.y, valid, true, value.repeat };
            break;
        }
        case EStage::raw:
        {
            if ((m_cursor == m_observation.raw_count) || !m_admitted || !allowed() || m_observation.baseline)
            {
                begin_evaluation();
                break;
            }
            --budget.outputs;
            input::SActionInputV1 record = m_observation.raw[m_cursor++];
            record.time_ns = m_observation.time_ns;
            if (m_connection.post_mapped(record, m_generation) != EInputPost::published) invalidate();
            break;
        }
        case EStage::binding:
        {
            if (m_binding == m_count)
            {
                m_stage = EStage::finish;
                break;
            }
            const SInputBinding& instruction = binding(m_binding);
            if (instruction.context != m_connection.snapshot().context)
            {
                ++m_binding;
                break;
            }
            if (!m_group_started)
            {
                m_group = m_binding;
                m_x = m_y = 0.0f;
                m_held = m_cancelled = false;
                m_group_started = true;
            }
            m_modifier = instruction.modifier_begin;
            m_modifiers = m_previous_modifiers = true;
            m_modifier_valid = true;
            m_stage = EStage::modifiers;
            break;
        }
        case EStage::modifiers:
        {
            if (m_modifier == binding(m_binding).modifier_end)
            {
                m_stage = EStage::evaluate;
                break;
            }
            const std::uint32_t code = m_plan->modifiers.at(m_modifier++);
            m_modifier_valid = m_modifier_valid && m_physical[code].valid;
            m_modifiers = m_modifiers && m_physical[code].valid && (m_physical[code].x != 0.0f);
            m_previous_modifiers = m_previous_modifiers && m_before[code].valid && (m_before[code].x != 0.0f);
            break;
        }
        case EStage::evaluate:
        {
            evaluate();
            break;
        }
        case EStage::output:
        {
            return output(budget);
        }
        case EStage::finish:
        {
            if (m_observation.kind == EObservationKind::text)
            {
                if (!budget.outputs) return false;
                --budget.outputs;
                if (m_eligible && m_text_admitted && (m_text_generation == m_connection.snapshot().text_generation))
                {
                    input::STextInputV1 record = m_observation.text;
                    record.time_ns = m_observation.time_ns;
                    (void)m_connection.post_text(record);
                }
            }
            m_stage = EStage::idle;
            break;
        }
        case EStage::idle:
        {
            return false;
        }
    }
    return true;
}

}   // namespace platform::runtime
