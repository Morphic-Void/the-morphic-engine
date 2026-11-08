
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_windows.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Windows desktop message translation, acquisition and quiet-point sampling.

#pragma once

#include "platform/platform_defines.hpp"
#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#include "native/window.hpp"

namespace platform::native
{
[[nodiscard]] bool desktop_next(SWindow& window, MSG& message) noexcept;
void desktop_translate(SWindow& window, const MSG& message) noexcept;
[[nodiscard]] bool desktop_message(SWindow& window, const UINT message, const WPARAM wparam, const LPARAM lparam) noexcept;
void desktop_baseline(SWindow& window) noexcept;
[[nodiscard]] bool desktop_capture(SWindow& window, const bool acquire) noexcept;
}   // namespace platform::native
#endif
