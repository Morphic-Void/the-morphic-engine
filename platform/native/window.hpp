
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    window.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Defines native window storage and the private create/dispatch/destroy adapter
//  shared by the Platform runtime, native backends and deterministic tests.

#pragma once

#include "services/service_protocol.hpp"
#include "native/desktop.hpp"

namespace platform::runtime { class CPlatformRuntime; }

namespace platform::native
{

//  The adapter owns these native references. The runtime retains this record
//  through every native call, including partial creation and destruction failure.
struct SWindow
{
    runtime::CPlatformRuntime* runtime{ nullptr };
    std::uintptr_t handle{ 0u }, instance{ 0u };
    std::uint32_t class_atom{ 0u }, error{ 0u };
    bool timer{ false }, cancelling{ false }, destroying{ false }, established{ false };
    bool native_events{ false }, work_pending{ false };
    SDesktop desktop;
    [[nodiscard]] bool live() const noexcept { return handle || class_atom; }
};

struct SWindowAdapter
{
    services::EServiceStatus(*create)(SWindow& window) noexcept{ nullptr };
    bool(*dispatch)(SWindow& window, const std::uint64_t budget_ns) noexcept{ nullptr };
    bool(*destroy)(SWindow& window) noexcept{ nullptr };
    //  Optional desktop backend. A false result never authorises capture.
    bool(*capture)(SWindow& window, const bool acquire) noexcept{ nullptr };
};

[[nodiscard]] const SWindowAdapter& window_adapter() noexcept;

}   //  namespace platform::native
