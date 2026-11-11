
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    window.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Defines Platform's window service commands, request payloads and immutable
//  borrowed descriptors, including native handles and access to published state.

#pragma once

#include "window_state.hpp"
#include "framework/services/service_notifications.hpp"

namespace platform::windows
{

inline constexpr services::SServiceContract k_window_contract{ 1u, 1u };
enum class EWindowCommand : std::uint16_t
{
    create_window = 16u, acquire_window = 17u, request_close = 19u,
    answer_close = 20u, window_ready = 22u
};
struct SWindowToken { std::uint64_t value{ 0u }; };
struct SWindowRequest { SWindowToken window; };
struct SAnswerClose
{
    SWindowToken window;
    std::uint64_t close_request{ 0u };
    std::uint32_t accept{ 0u }, reserved{ 0u };
};
struct SWindowReady
{
    SWindowToken window;
    services::SServiceBorrow borrow;
    services::SNotificationRegistration registration;
};
enum class ENativeWindow : std::uint32_t { windows = 1u };
struct SWindowViewV1
{
    [[nodiscard]] bool is_valid() const noexcept;
    std::uint32_t size{ sizeof(SWindowViewV1) }, version{ 1u };
    ENativeWindow kind{ ENativeWindow::windows };
    std::uint32_t controller{ 0u };
    modules::SModuleInstance provider;
    SWindowToken window;
    std::uintptr_t native_window{ 0u }, native_instance{ 0u };
    const CWindowPublication* state{ nullptr };
};

inline bool SWindowViewV1::is_valid() const noexcept
{
    return (size == sizeof(SWindowViewV1)) && (version == 1u) && (kind == ENativeWindow::windows) &&
        (controller <= 1u) && provider.value && window.value && native_window && native_instance && state;
}

static_assert((sizeof(SAnswerClose) == 24u) && (sizeof(SWindowReady) == 24u));
static_assert(std::is_trivially_copyable_v<SWindowViewV1>);

}   //  namespace platform::windows
