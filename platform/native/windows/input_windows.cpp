
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_windows.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Ordered native keyboard/text turns and logical-coordinate mouse acquisition.

#include "native/windows/input_windows.hpp"
#if MV_PLATFORM_WINDOWS
#include <algorithm>
#include <cstring>
#include <limits>
#include "runtime/platform_runtime.hpp"

namespace platform::native
{
using namespace runtime::input_control;
using runtime::SDesktopTransaction;
using runtime::SInputControl;

//  Set-1 scan codes indexed by the portable control catalogue. E0 is retained
//  in the high byte. This table describes physical positions, never characters.
static constexpr std::uint32_t k_scans[]{
    0u,
    0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
    0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c, // A-Z
    0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b, // 1-0
    0x1c,0x01,0x0e,0x0f,0x39, // Enter, Escape, Backspace, Tab, Space
    0x0c,0x0d,0x1a,0x1b,0x2b,0x27,0x28,0x29,0x33,0x34,0x35,0x3a,
    0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x57,0x58, // F1-F12
    0xe037,0x46,0xe145,0xe052,0xe047,0xe049,0xe053,0xe04f,0xe051,0xe04d,0xe04b,0xe050,0xe048,
    0x45,0xe035,0x37,0x4a,0x4e,0xe01c, // keypad operators
    0x4f,0x50,0x51,0x4b,0x4c,0x4d,0x47,0x48,0x49,0x52,0x53,0x56,
    0x1d,0x2a,0x38,0xe05b,0xe01d,0x36,0xe038,0xe05c,0xe05d
};
static_assert((sizeof(k_scans) / sizeof(k_scans[0])) == k_either_shift);
static constexpr std::uint32_t k_modifiers[]{ k_left_shift, k_right_shift, k_left_ctrl, k_right_ctrl,
    k_left_alt, k_right_alt, k_left_super, k_right_super, k_alt_graph };
static constexpr int k_mouse_keys[]{ VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };

static void synchronise(SWindow& window) noexcept
{
    SDesktop& desktop = window.desktop;
    const std::uint64_t epoch = window.runtime->desktop_epoch();
    if (desktop.epoch != epoch)
    {
        desktop.epoch = epoch;
        desktop.edit_pending = desktop.ctrl_pending = false;
        desktop.ctrl_release_pending = desktop.suppress_ctrl_release = false;
    }
    const std::uint64_t text_epoch = window.runtime->desktop_text_epoch();
    if (desktop.text_epoch != text_epoch)
    {
        desktop.text_epoch = text_epoch;
        desktop.discard_composition = desktop.discard_composition || desktop.dead;
    }
}

static input::SDesktopState state(const SDesktop& desktop) noexcept
{
    input::SDesktopState result;
    for (std::uint32_t index = 0u; index < 9u; ++index)
    {
        if (desktop.known[k_modifiers[index]]) result.modifiers_known |= static_cast<std::uint16_t>(1u << index);
        if (desktop.keys[k_modifiers[index]]) result.modifiers |= static_cast<std::uint16_t>(1u << index);
    }
    for (std::uint32_t index = 0u; index < 5u; ++index)
    {
        if (desktop.buttons_known[index]) result.buttons_known |= static_cast<std::uint8_t>(1u << index);
        if (desktop.buttons[index]) result.buttons |= static_cast<std::uint8_t>(1u << index);
    }
    return result;
}

static void change(SDesktop& desktop, SDesktopTransaction& transaction, const std::uint32_t control,
    const bool down, const bool repeat = false) noexcept
{
    desktop.keys[control] = down;
    desktop.known[control] = true;
    transaction.controls[transaction.count++] = { control, down ? 1.0f : 0.0f, 0.0f, repeat };
}

static void virtual_modifiers(SDesktop& desktop) noexcept
{
    const std::uint32_t pairs[][3]{ { k_either_shift,k_left_shift,k_right_shift }, { k_either_ctrl,k_left_ctrl,k_right_ctrl },
        { k_either_alt,k_left_alt,k_right_alt }, { k_either_super,k_left_super,k_right_super } };
    for (const auto& pair : pairs)
    {
        desktop.keys[pair[0]] = desktop.keys[pair[1]] || desktop.keys[pair[2]];
        desktop.known[pair[0]] = desktop.known[pair[1]] && desktop.known[pair[2]];
    }
    desktop.keys[k_alt_graph] = desktop.altgr;
    desktop.known[k_alt_graph] = desktop.known[k_right_alt];
}

static void emit(SWindow& window, SDesktopTransaction& transaction) noexcept
{
    std::sort(transaction.controls, (transaction.controls + transaction.count),
        [](const SInputControl& left, const SInputControl& right) { return left.control < right.control; });
    window.runtime->desktop_observation(transaction);
}

static void flush_ctrl(SWindow& window) noexcept
{
    if (!window.desktop.ctrl_pending) return;
    window.desktop.ctrl_pending = false;
    window.desktop.genuine_ctrl = true;
    emit(window, window.desktop.ctrl);
}

static std::uint32_t control_code(const WPARAM key, const LPARAM flags) noexcept
{
    if (key == VK_PAUSE) return k_pause;
    if (key == VK_SNAPSHOT) return k_print_screen;
    const std::uint32_t scan = ((static_cast<std::uint32_t>(flags) >> 16u) & 0xffu) |
        ((flags & (1u << 24u)) ? 0xe000u : 0u);
    for (std::uint32_t code = k_first_key; code < k_either_shift; ++code)
    {
        if (k_scans[code] == scan) return code;
    }
    return 0u;
}

static void key(SWindow& window, const UINT message, const WPARAM wparam, const LPARAM lparam) noexcept
{
    SDesktop& desktop = window.desktop;
    const std::uint32_t code = control_code(wparam, lparam);
    if (!code) return;
    const bool down = (message == WM_KEYDOWN) || (message == WM_SYSKEYDOWN);
    const bool repeat = down && ((lparam & (1u << 30u)) != 0u);
    const bool ctrl_was_down = desktop.keys[k_left_ctrl], ctrl_was_known = desktop.known[k_left_ctrl];
    const input::SDesktopState previous = state(desktop);
    SDesktopTransaction transaction;
    transaction.repeats = down ? std::max(1u, (static_cast<std::uint32_t>(lparam) & 0xffffu)) : 1u;
    //  Paired AltGr bookkeeping is normalized only here, never in MSG passed
    //  to native translation. A previously established physical Ctrl survives.
    if ((code == k_left_ctrl) && !down && desktop.suppress_ctrl_release)
    {
        desktop.suppress_ctrl_release = false;
        return;
    }
    if ((code == k_left_ctrl) && !down) desktop.genuine_ctrl = false;
    change(desktop, transaction, code, down, repeat);
    if ((code == k_right_alt) && !down)
    {
        desktop.ctrl_release_pending = desktop.altgr;
        desktop.ctrl_release_time = static_cast<std::uint32_t>(GetMessageTime());
        desktop.altgr = false;
    }
    virtual_modifiers(desktop);
    if ((code >= k_left_ctrl) && (code <= k_right_super))
    {
        const std::uint32_t virtuals[]{ k_either_shift, k_either_ctrl, k_either_alt, k_either_super, k_alt_graph };
        for (const std::uint32_t value : virtuals)
        {
            const float held = desktop.keys[value] ? 1.0f : 0.0f;
            transaction.controls[transaction.count++] = { value,
                desktop.known[value] ? held : std::numeric_limits<float>::quiet_NaN(), 0.0f, false };
        }
        if ((code == k_right_alt) && desktop.altgr)
        {
            const float held = desktop.keys[k_left_ctrl] ? 1.0f : 0.0f;
            transaction.controls[transaction.count++] = { k_left_ctrl,
                desktop.known[k_left_ctrl] ? held : std::numeric_limits<float>::quiet_NaN(), 0.0f, false };
        }
    }
    const input::SDesktopState current = state(desktop);
    if ((current.modifiers != previous.modifiers) || (current.modifiers_known != previous.modifiers_known))
    {
        input::SActionInputV1& raw = transaction.raw[transaction.raw_count++];
        raw.kind = input::EActionKind::modifiers;
        const input::SModifierInput payload{ current };
        std::memcpy(raw.payload, &payload, sizeof(payload));
    }
    if ((code == k_left_ctrl) && down && !repeat)
    {
        desktop.ctrl = transaction;
        desktop.ctrl_was_down = ctrl_was_down;
        desktop.ctrl_was_known = ctrl_was_known;
        desktop.ctrl_time = static_cast<std::uint32_t>(GetMessageTime());
        desktop.ctrl_pending = true;
    }
    else if (down && desktop.translation && ((code == k_backspace) || (code == k_enter)))
    {
        desktop.edit = transaction;
        desktop.edit_pending = true;
    }
    else emit(window, transaction);
}

bool desktop_next(SWindow& window, MSG& message) noexcept
{
    synchronise(window);
    SDesktop& desktop = window.desktop;
    if (desktop.translation)
    {
        //  Earlier translation turns are complete. Resolve this physical editing
        //  key once before its character drain; control characters cannot create
        //  another edit or impersonate a physical Backspace (e.g. Ctrl+H).
        if (desktop.edit_pending)
        {
            desktop.edit_pending = false;
            if (desktop.translation_epoch == desktop.epoch) emit(window, desktop.edit);
        }
        const UINT first = desktop.system_translation ? WM_SYSCHAR : WM_CHAR;
        const UINT last = desktop.system_translation ? WM_SYSDEADCHAR : WM_DEADCHAR;
        if (PeekMessageW(&message, reinterpret_cast<HWND>(window.handle), first, last, PM_REMOVE))
        {
            return true;
        }
        synchronise(window);
        desktop.edit_pending = false;
        desktop.translation = false;
        if (desktop.turn_char && !desktop.turn_dead)
        {
            desktop.dead = desktop.discard_composition = false;
        }
    }
    if (!PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE))
    {
        synchronise(window);
        flush_ctrl(window);
        return false;
    }
    synchronise(window);
    desktop.suppress_ctrl_release = desktop.ctrl_release_pending &&
        (message.hwnd == reinterpret_cast<HWND>(window.handle)) &&
        ((message.message == WM_SYSKEYUP) || (message.message == WM_KEYUP)) &&
        (control_code(message.wParam, message.lParam) == k_left_ctrl) &&
        (message.time == desktop.ctrl_release_time);
    desktop.ctrl_release_pending = false;
    if (desktop.ctrl_pending)
    {
        const bool paired = (message.hwnd == reinterpret_cast<HWND>(window.handle)) &&
            ((message.message == WM_SYSKEYDOWN) || (message.message == WM_KEYDOWN)) &&
            (control_code(message.wParam, message.lParam) == k_right_alt) &&
            (message.time == desktop.ctrl_time);
        if (paired)
        {
            desktop.ctrl_pending = false;
            desktop.keys[k_left_ctrl] = desktop.ctrl_was_down;
            desktop.known[k_left_ctrl] = desktop.ctrl_was_known;
            desktop.altgr = true;
            virtual_modifiers(desktop);
        }
        else flush_ctrl(window);
    }
    return true;
}

void desktop_translate(SWindow& window, const MSG& message) noexcept
{
    const bool own = message.hwnd == reinterpret_cast<HWND>(window.handle);
    const bool key_message = (message.message == WM_KEYDOWN) || (message.message == WM_KEYUP) ||
        (message.message == WM_SYSKEYDOWN) || (message.message == WM_SYSKEYUP);
    if (own && key_message)
    {
        synchronise(window);
        SDesktop& desktop = window.desktop;
        desktop.translation = true;
        desktop.system_translation = (message.message == WM_SYSKEYDOWN) || (message.message == WM_SYSKEYUP);
        desktop.translation_epoch = desktop.epoch;
        desktop.translation_text_epoch = desktop.text_epoch;
        desktop.turn_char = desktop.turn_dead = false;
    }
    (void)TranslateMessage(&message);
}

bool desktop_message(SWindow& window, const UINT message, const WPARAM wparam, const LPARAM lparam) noexcept
{
    synchronise(window);
    SDesktop& desktop = window.desktop;
    switch (message)
    {
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        {
            if (message == WM_KILLFOCUS) desktop.genuine_ctrl = false;
            window.runtime->input_focus(message == WM_SETFOCUS);
            return false;
        }
        case WM_CAPTURECHANGED:
        {
            window.runtime->desktop_capture_lost();
            return false;
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        {
            key(window, message, wparam, lparam);
            return (message == WM_KEYDOWN) || (message == WM_KEYUP);
        }
        case WM_DEADCHAR:
        case WM_SYSDEADCHAR:
        {
            desktop.dead = desktop.turn_dead = true;
            if ((desktop.translation && (desktop.translation_text_epoch != desktop.text_epoch)) ||
                window.runtime->desktop_text_pending()) desktop.discard_composition = true;
            return (message == WM_DEADCHAR) || desktop.altgr;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_UNICHAR:
        {
            if ((message == WM_UNICHAR) && (wparam == UNICODE_NOCHAR))
            {
                return true;
            }
            const bool translated = message != WM_UNICHAR;
            if (translated) desktop.turn_char = true;
            const bool current = !translated || !desktop.translation || (desktop.translation_text_epoch == desktop.text_epoch);
            if (current && (!translated || !desktop.discard_composition) && ((message != WM_SYSCHAR) || desktop.altgr))
            {
                SDesktopTransaction transaction;
                transaction.text = true;
                transaction.utf32 = !translated;
                transaction.code_unit = static_cast<std::uint32_t>(wparam);
                transaction.repeats = std::max(1u, (static_cast<std::uint32_t>(lparam) & 0xffffu));
                window.runtime->desktop_observation(transaction);
            }
            return (message != WM_SYSCHAR) || desktop.altgr;
        }
        default:
        {
            break;
        }
    }
    if ((message < WM_MOUSEFIRST) || (message > WM_MOUSELAST))
    {
        return false;
    }
    const HWND handle = reinterpret_cast<HWND>(window.handle);
    POINT position{ static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam)) };
    const bool wheel = (message == WM_MOUSEWHEEL) || (message == WM_MOUSEHWHEEL);
    if (wheel && !ScreenToClient(handle, &position))
    {
        return false;
    }
    const UINT dpi = GetDpiForWindow(handle);
    if (!dpi) return false;
    desktop.x = static_cast<float>(position.x) * (96.0f / static_cast<float>(dpi));
    desktop.y = static_cast<float>(position.y) * (96.0f / static_cast<float>(dpi));
    desktop.position_known = true;
    std::uint32_t button = UINT32_MAX;
    bool down = false;
    switch (message)
    {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        {
            button = 0u;
            down = message == WM_LBUTTONDOWN;
            break;
        }
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        {
            button = 1u;
            down = message == WM_RBUTTONDOWN;
            break;
        }
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        {
            button = 2u;
            down = message == WM_MBUTTONDOWN;
            break;
        }
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        {
            button = (HIWORD(wparam) == XBUTTON1) ? 3u : 4u;
            down = message == WM_XBUTTONDOWN;
            break;
        }
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        {
            break;
        }
        default:
        {
            return false;
        }
    }
    SDesktopTransaction transaction;
    if (button != UINT32_MAX)
    {
        desktop.buttons[button] = down;
        desktop.buttons_known[button] = true;
        transaction.controls[transaction.count++] = { k_first_mouse_button + button, down ? 1.0f : 0.0f, 0.0f, false };
    }
    transaction.controls[transaction.count++] = { k_mouse_position, desktop.x, desktop.y, false };
    input::SActionInputV1& raw = transaction.raw[transaction.raw_count++];
    if (wheel)
    {
        const float delta = static_cast<float>(static_cast<short>(HIWORD(wparam))) / static_cast<float>(WHEEL_DELTA);
        const float horizontal = (message == WM_MOUSEHWHEEL) ? delta : 0.0f;
        const float vertical = (message == WM_MOUSEWHEEL) ? delta : 0.0f;
        transaction.controls[transaction.count++] = { k_mouse_wheel, horizontal, vertical, false };
        raw.kind = input::EActionKind::wheel;
        raw.subject = k_mouse_wheel;
        const input::SWheelInput payload{ desktop.x, desktop.y, horizontal, vertical, state(desktop) };
        std::memcpy(raw.payload, &payload, sizeof(payload));
    }
    else
    {
        raw.kind = (button == UINT32_MAX) ? input::EActionKind::pointer : input::EActionKind::button;
        raw.subject = (button == UINT32_MAX) ? k_mouse_position : (k_first_mouse_button + button);
        const input::SPointerInput payload{ desktop.x, desktop.y, state(desktop) };
        std::memcpy(raw.payload, &payload, sizeof(payload));
    }
    emit(window, transaction);
    return true;
}

void desktop_baseline(SWindow& window) noexcept
{
    synchronise(window);
    if (window.desktop.translation || window.desktop.ctrl_pending)
    {
        return;
    }
    const std::uint64_t epoch = window.runtime->desktop_epoch();
    BYTE keyboard[256]{};
    if (!GetKeyboardState(keyboard))
    {
        window.runtime->native_failure(GetLastError());
        return;
    }
    SDesktop& desktop = window.desktop;
    POINT position{};
    const HWND handle = reinterpret_cast<HWND>(window.handle);
    const UINT dpi = GetDpiForWindow(handle);
    desktop.position_known = dpi && GetCursorPos(&position) && ScreenToClient(handle, &position);
    if (desktop.position_known)
    {
        desktop.x = static_cast<float>(position.x) * (96.0f / static_cast<float>(dpi));
        desktop.y = static_cast<float>(position.y) * (96.0f / static_cast<float>(dpi));
    }
    const HKL layout = GetKeyboardLayout(0u);
    UINT native_keys[k_either_shift]{};
    for (std::uint32_t code = k_first_key; code < k_either_shift; ++code)
    {
        const UINT key = (code == k_pause) ? VK_PAUSE : ((code == k_print_screen) ? VK_SNAPSHOT :
            MapVirtualKeyExW(k_scans[code], MAPVK_VSC_TO_VK_EX, layout));
        desktop.known[code] = key && (key < 256u);
        desktop.keys[code] = desktop.known[code] && ((keyboard[key] & 0x80u) != 0u);
        native_keys[code] = key;
    }
    //  A virtual key may describe multiple physical positions (e.g. Enter).
    //  An up sample establishes neutrality; a down sample cannot identify which.
    for (std::uint32_t code = k_first_key; code < k_either_shift; ++code)
    {
        if (!desktop.keys[code]) continue;
        for (std::uint32_t other = k_first_key; other < k_either_shift; ++other)
        {
            if ((other != code) && (native_keys[other] == native_keys[code])) desktop.known[code] = false;
        }
    }
    if (!desktop.keys[k_right_alt]) desktop.altgr = false;
    if (desktop.keys[k_right_alt] && desktop.keys[k_left_ctrl] && !desktop.genuine_ctrl)
    {
        desktop.altgr = true;
        desktop.known[k_left_ctrl] = false;
    }
    if (desktop.altgr && !desktop.genuine_ctrl) desktop.keys[k_left_ctrl] = false;
    virtual_modifiers(desktop);
    runtime::SInputObservation baseline;
    for (std::uint32_t code = k_first_key; code <= k_last_key; ++code)
    {
        if (desktop.known[code]) baseline.controls[baseline.count++] = { code, desktop.keys[code] ? 1.0f : 0.0f, 0.0f, false };
    }
    for (std::uint32_t index = 0u; index < 5u; ++index)
    {
        desktop.buttons_known[index] = true;
        desktop.buttons[index] = (keyboard[k_mouse_keys[index]] & 0x80u) != 0u;
        baseline.controls[baseline.count++] = { k_first_mouse_button + index, desktop.buttons[index] ? 1.0f : 0.0f, 0.0f, false };
    }
    if (desktop.position_known) baseline.controls[baseline.count++] = { k_mouse_position, desktop.x, desktop.y, false };
    baseline.controls[baseline.count++] = { k_mouse_wheel, 0.0f, 0.0f, false };
    window.runtime->desktop_quiet(baseline, epoch);
}

bool desktop_capture(SWindow& window, const bool acquire) noexcept
{
    const HWND handle = reinterpret_cast<HWND>(window.handle);
    if (acquire)
    {
        (void)SetCapture(handle);
        return GetCapture() == handle;
    }
    if (GetCapture() != handle) return true;
    if (!ReleaseCapture())
    {
        window.error = GetLastError();
        return false;
    }
    return GetCapture() != handle;
}

}   // namespace platform::native
#endif
