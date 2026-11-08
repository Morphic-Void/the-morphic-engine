
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    desktop.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Private native desktop state retained with its owning window.

#pragma once

#include "runtime/desktop_input.hpp"

namespace platform::native
{
struct SDesktop
{
    static constexpr std::uint32_t k_key_slots = runtime::input_control::k_last_key + 1u;
    static constexpr std::uint32_t k_button_slots = runtime::input_control::k_last_mouse_button - runtime::input_control::k_first_mouse_button + 1u;
    bool keys[k_key_slots]{}, known[k_key_slots]{}, buttons[k_button_slots]{}, buttons_known[k_button_slots]{};
    float x{}, y{};
    bool position_known{}, translation{}, system_translation{}, edit_pending{}, ctrl_pending{};
    bool altgr{}, genuine_ctrl{}, dead{}, discard_composition{}, turn_char{}, turn_dead{};
    bool ctrl_release_pending{}, suppress_ctrl_release{};
    bool ctrl_was_down{}, ctrl_was_known{};
    std::uint32_t ctrl_time{}, ctrl_release_time{};
    std::uint64_t epoch{}, translation_epoch{};
    std::uint64_t text_epoch{}, translation_text_epoch{};
    runtime::SDesktopTransaction edit, ctrl;
};
}   // namespace platform::native
