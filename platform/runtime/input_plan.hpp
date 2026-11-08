
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_plan.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Immutable, resolved mapper instructions owned by an installed configuration.

#pragma once

#include "runtime/input_storage.hpp"

namespace platform::runtime
{

//  These values follow configuration schema v1. Action/profile encodings differ
//  from the public input record kinds and connection kinds.
enum class EInputAction : std::uint32_t { digital = 0u, pulse = 1u, scalar = 2u, vector = 3u, delta = 4u };
enum class EInputProfileKind : std::uint32_t { desktop = 0u, controller = 1u };
enum class EInputSource : std::uint32_t { key = 0u, button = 1u, position = 2u, wheel = 3u, pad_button = 4u, stick = 5u, trigger = 6u };
enum class EInputActivation : std::uint32_t { regular = 0u, start = 1u, release = 2u, direct = 3u, threshold = 4u, chord = 5u };
enum class EInputDirection : std::uint32_t { whole = 0u, positive_x = 1u, negative_x = 2u, positive_y = 3u, negative_y = 4u };

namespace input_control
{
inline constexpr std::uint32_t k_first_key = 1u, k_last_key = 110u;
inline constexpr std::uint32_t k_last_letter = 26u, k_last_digit = 36u, k_first_named_key = 37u;
inline constexpr std::uint32_t k_backspace = 39u, k_home = 70u, k_up = 78u;
inline constexpr std::uint32_t k_enter = 37u, k_print_screen = 66u, k_pause = 68u, k_keypad_enter = 84u;
inline constexpr std::uint32_t k_left_ctrl = 97u, k_left_shift = 98u, k_left_alt = 99u, k_left_super = 100u;
inline constexpr std::uint32_t k_right_ctrl = 101u, k_right_shift = 102u, k_right_alt = 103u, k_right_super = 104u;
inline constexpr std::uint32_t k_either_shift = 106u, k_either_ctrl = 107u, k_either_alt = 108u, k_either_super = 109u, k_alt_graph = 110u;
inline constexpr std::uint32_t k_first_mouse_button = 128u, k_last_mouse_button = 132u;
inline constexpr std::uint32_t k_mouse_position = 133u, k_mouse_wheel = 134u;
inline constexpr std::uint32_t k_first_pad_button = 256u, k_last_pad_button = 269u;
inline constexpr std::uint32_t k_left_stick = 270u, k_right_stick = 271u;
inline constexpr std::uint32_t k_left_trigger = 272u, k_right_trigger = 273u;
inline constexpr std::uint32_t k_storage_count = k_right_trigger + 1u;
inline constexpr std::uint32_t k_defined_count = 1u + k_last_key +
    (k_mouse_wheel - k_first_mouse_button + 1u) + (k_right_trigger - k_first_pad_button + 1u);

[[nodiscard]] inline bool is_key(const std::uint32_t code) noexcept
{ return (code >= k_first_key) && (code <= k_last_key); }
[[nodiscard]] inline bool is_mouse_button(const std::uint32_t code) noexcept
{ return (code >= k_first_mouse_button) && (code <= k_last_mouse_button); }
[[nodiscard]] inline bool is_pad_button(const std::uint32_t code) noexcept
{ return (code >= k_first_pad_button) && (code <= k_last_pad_button); }
[[nodiscard]] inline bool is_desktop(const std::uint32_t code) noexcept
{ return is_key(code) || ((code >= k_first_mouse_button) && (code <= k_mouse_wheel)); }
[[nodiscard]] inline bool is_controller(const std::uint32_t code) noexcept
{ return (code >= k_first_pad_button) && (code <= k_right_trigger); }
[[nodiscard]] inline bool is_digital(const std::uint32_t code) noexcept
{ return is_key(code) || is_mouse_button(code) || is_pad_button(code); }
[[nodiscard]] inline bool permits_editing_repeat(const std::uint32_t code) noexcept
{ return (code == k_backspace) || ((code >= k_home) && (code <= k_up)); }
}   // namespace input_control

struct SInputParameters
{
    float dead_zone{}, neutral_enter{}, neutral_leave{}, press{ 0.55f }, release{ 0.45f }, scale{ 1.0f };
};
static_assert(sizeof(SInputParameters) == (6u * sizeof(float)));

struct SInputBinding
{
    std::uint32_t id{}, context{}, action{}, control{};
    std::uint32_t modifier_begin{}, modifier_end{};
    EInputAction action_kind{};
    EInputDirection direction{};
    EInputSource source{};
    EInputActivation activation{};
    SInputParameters parameters;
    bool allow_text{}, repeat{};
};

struct SInputPlan
{
    TConfigurationPages<SInputBinding> bindings;
    TConfigurationPages<std::uint32_t> modifiers;
};

}   // namespace platform::runtime
