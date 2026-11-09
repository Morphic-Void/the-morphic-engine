
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    window_windows.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Implements Windows window creation, message dispatch, geometry/DPI updates
//  and destruction. Modal callbacks publish state and perform bounded Host maintenance.

#include "platform/platform_defines.hpp"
#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#include "runtime/platform_runtime.hpp"
#include "native/windows/input_windows.hpp"

namespace platform::native
{

static constexpr wchar_t k_class_name[]{ L"MorphicPlatformWindow" };
static constexpr UINT_PTR k_timer = 1u;

struct SCallbackGuard
{
    explicit SCallbackGuard(runtime::CPlatformRuntime& runtime) noexcept : runtime{ runtime } { runtime.enter_callback(); }
    ~SCallbackGuard() noexcept { runtime.leave_callback(); }
    runtime::CPlatformRuntime& runtime;
};

static void cancel(SWindow& window) noexcept
{
    if (!window.handle || window.cancelling || window.destroying) return;
    window.cancelling = true;
    const auto handle = reinterpret_cast<HWND>(window.handle);
    (void)SendMessageW(handle, WM_CANCELMODE, 0u, 0);
    (void)EndMenu();
    window.cancelling = false;
}

static void extent(SWindow& window, const HWND handle, const bool minimised) noexcept
{
    RECT rect{};
    const auto dpi = GetDpiForWindow(handle);
    if (!GetClientRect(handle, &rect) || !dpi)
    {
        window.error = GetLastError();
        window.runtime->native_failure(window.error);
        return;
    }
    window.runtime->native_extent(static_cast<std::uint32_t>(rect.right - rect.left),
        static_cast<std::uint32_t>(rect.bottom - rect.top), dpi, minimised);
}

static LRESULT CALLBACK window_proc(const HWND handle, const UINT message, const WPARAM wparam, const LPARAM lparam) noexcept
{
    auto* window = reinterpret_cast<SWindow*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        window = static_cast<SWindow*>(create->lpCreateParams);
        if (!window || !window->runtime) return FALSE;
        window->handle = reinterpret_cast<std::uintptr_t>(handle);
        window->native_events = true;
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window)) && (GetLastError() != ERROR_SUCCESS))
        {
            window->error = GetLastError();
            window->handle = 0u;
            return FALSE;
        }
    }
    if (!window) return DefWindowProcW(handle, message, wparam, lparam);
    SCallbackGuard guard{ *window->runtime };
    auto& runtime = *window->runtime;
    const bool modal_boundary = (message == WM_ENTERSIZEMOVE) || (message == WM_EXITSIZEMOVE) ||
        (message == WM_ENTERMENULOOP) || (message == WM_EXITMENULOOP);
    if (!modal_boundary && !window->cancelling && !window->destroying && runtime.maintain_native()) cancel(*window);
    if (desktop_message(*window, message, wparam, lparam))
    {
        return (((message == WM_UNICHAR) && (wparam == UNICODE_NOCHAR)) ||
            (message == WM_XBUTTONDOWN) || (message == WM_XBUTTONUP)) ? TRUE : 0;
    }
    switch (message)
    {
    case WM_CLOSE:
        runtime.native_close();
        return 0;
    case WM_ENTERSIZEMOVE:
        runtime.enter_modal(false);
        if (runtime.maintain_native()) cancel(*window);
        return 0;
    case WM_EXITSIZEMOVE:
        runtime.leave_modal(false);
        (void)runtime.maintain_native();
        extent(*window, handle, IsIconic(handle) != FALSE);
        return 0;
    case WM_ENTERMENULOOP:
        runtime.enter_modal(true);
        if (runtime.maintain_native()) cancel(*window);
        return 0;
    case WM_EXITMENULOOP:
        runtime.leave_modal(true);
        (void)runtime.maintain_native();
        return 0;
    case WM_SYSCOMMAND:
        if (runtime.cancelling()) return 0;
        break;
    case WM_TIMER:
        if (wparam == k_timer)
        {
            if (runtime.maintain_native()) cancel(*window);
            return 0;
        }
        break;
    case WM_SIZE:
        extent(*window, handle, wparam == SIZE_MINIMIZED);
        return 0;
    case WM_DPICHANGED:
    {
        const auto& rect = *reinterpret_cast<const RECT*>(lparam);
        if (!SetWindowPos(handle, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOACTIVATE | SWP_NOZORDER)) runtime.native_failure(GetLastError());
        extent(*window, handle, IsIconic(handle) != FALSE);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        (void)BeginPaint(handle, &paint);
        (void)EndPaint(handle, &paint);
        return 0;
    }
    case WM_NCDESTROY:
    {
        const auto result = DefWindowProcW(handle, message, wparam, lparam);
        (void)SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        window->handle = 0u;
        if (window->established && !window->destroying)
        {
            window->error = ERROR_INVALID_WINDOW_HANDLE;
            runtime.native_failure(window->error);
        }
        window->established = false;
        return result;
    }
    default: break;
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}

static services::EServiceStatus create(SWindow& window) noexcept
{
    window.desktop = {};
    if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return services::EServiceStatus::unsupported;
    if (window.runtime->maintain_native()) return services::EServiceStatus::closing;
    HMODULE instance = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&window_proc), &instance))
    {
        window.error = GetLastError();
        return services::EServiceStatus::native_failure;
    }
    window.instance = reinterpret_cast<std::uintptr_t>(instance);
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.hInstance = instance;
    description.lpfnWndProc = &window_proc;
    description.lpszClassName = k_class_name;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    window.class_atom = RegisterClassExW(&description);
    if (!window.class_atom) { window.error = GetLastError(); return services::EServiceStatus::native_failure; }
    const DWORD style = WS_OVERLAPPEDWINDOW;
    const auto handle = CreateWindowExW(0u, k_class_name, L"Morphic Engine", style,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, &window);
    if (!handle) { window.error = GetLastError(); return services::EServiceStatus::native_failure; }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    RECT bounds{};
    const auto dpi = GetDpiForWindow(handle);
    RECT decoration{};
    if (!dpi || !GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor) ||
        !GetWindowRect(handle, &bounds) || !AdjustWindowRectExForDpi(&decoration, style, FALSE, 0u, dpi))
    {
        window.error = GetLastError();
        return services::EServiceStatus::native_failure;
    }
    const auto frame_width = decoration.right - decoration.left, frame_height = decoration.bottom - decoration.top;
    const auto available_width = (monitor.rcWork.right - monitor.rcWork.left) - frame_width;
    const auto available_height = (monitor.rcWork.bottom - monitor.rcWork.top) - frame_height;
    if ((available_width <= 0) || (available_height <= 0)) return services::EServiceStatus::native_failure;
    auto width = MulDiv(1280, static_cast<int>(dpi), 96), height = MulDiv(720, static_cast<int>(dpi), 96);
    if (width > available_width) { width = available_width; height = static_cast<int>((static_cast<std::int64_t>(width) * 720) / 1280); }
    if (height > available_height) { height = available_height; width = static_cast<int>((static_cast<std::int64_t>(height) * 1280) / 720); }
    if ((width <= 0) || (height <= 0)) return services::EServiceStatus::native_failure;
    const auto outer_width = width + frame_width, outer_height = height + frame_height;
    const auto left = ((bounds.left + outer_width) > monitor.rcWork.right) ? (monitor.rcWork.right - outer_width) : bounds.left;
    const auto top = ((bounds.top + outer_height) > monitor.rcWork.bottom) ? (monitor.rcWork.bottom - outer_height) : bounds.top;
    if (!SetWindowPos(handle, nullptr, (left < monitor.rcWork.left) ? monitor.rcWork.left : left,
        (top < monitor.rcWork.top) ? monitor.rcWork.top : top, outer_width, outer_height, SWP_NOACTIVATE | SWP_NOZORDER))
    {
        window.error = GetLastError();
        return services::EServiceStatus::native_failure;
    }
    if (!SetTimer(handle, k_timer, 10u, nullptr)) { window.error = GetLastError(); return services::EServiceStatus::native_failure; }
    window.timer = true;
    if (window.runtime->maintain_native()) return services::EServiceStatus::closing;
    (void)ShowWindow(handle, SW_SHOWNORMAL);
    extent(window, handle, false);
    window.established = true;
    window.runtime->input_focus(GetFocus() == handle);
    return services::EServiceStatus::success;
}

static bool dispatch(SWindow& window, const std::uint64_t budget_ns) noexcept
{
    window.work_pending = true;
    std::uint64_t start = 0u;
    if (threading::query_awake_nanoseconds(start).code != threading::EWaitError::none) return false;
    for (std::uint32_t count = 0u; count < 128u; ++count)
    {
        if (window.runtime->maintain_native()) cancel(window);
        MSG message{};
        //  This exact Platform instance owns the Host thread's native queue.
        //  Retrieval can dispatch sent messages, so the caller's native scope
        //  covers both retrieval and explicit dispatch, including helper windows.
        if (!desktop_next(window, message))
        {
            window.work_pending = false;
            //  Synchronise before deciding whether to sample. A later reentrant
            //  cutover invalidates this epoch; an empty quiet report cannot
            //  become a new action baseline. Ordinary quiet points retain the
            //  state established by native transitions, including Ctrl evidence.
            const std::uint64_t epoch = window.runtime->desktop_epoch();
            if (window.runtime->desktop_needs_baseline()) desktop_baseline(window);
            else window.runtime->desktop_quiet({}, epoch);
            break;
        }
        if (message.message == WM_QUIT)
        {
            window.runtime->native_stop();
        }
        //  Unrecognised thread-only messages have no registered recipient.
        //  Consume them; never repeatedly wake on an undispatchable record.
        else if (message.hwnd)
        {
            desktop_translate(window, message);
            (void)DispatchMessageW(&message);
        }
        std::uint64_t now = 0u;
        if ((threading::query_awake_nanoseconds(now).code != threading::EWaitError::none) || (now < start)) return false;
        if ((now - start) >= budget_ns) break;
    }
    return true;
}

static bool destroy(SWindow& window) noexcept
{
    if (window.handle)
    {
        const auto handle = reinterpret_cast<HWND>(window.handle);
        if (window.timer) { (void)KillTimer(handle, k_timer); window.timer = false; }
        window.destroying = true;
        const bool destroyed = DestroyWindow(handle) != FALSE;
        window.destroying = false;
        if (!destroyed) { window.error = GetLastError(); return false; }
    }
    if (window.class_atom)
    {
        if (!UnregisterClassW(k_class_name, reinterpret_cast<HINSTANCE>(window.instance)))
        { window.error = GetLastError(); return false; }
        window.class_atom = 0u;
    }
    window.instance = 0u;
    return true;
}

const SWindowAdapter& window_adapter() noexcept
{
    static const SWindowAdapter adapter{ &create, &dispatch, &destroy, &desktop_capture };
    return adapter;
}

}   //  namespace platform::native
#endif
