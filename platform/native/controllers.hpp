
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    controllers.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    09 Nov 26
//
//  Private, injectable controller acquisition interface; no vendor types escape.

#pragma once

#include "native/controller_types.hpp"
#include "containers/TInstance.hpp"
#include "platform/threading/awake_clock.hpp"
#if MV_PLATFORM_WINDOWS
#include "native/windows/gameinput_state.hpp"
#endif

namespace platform::runtime { class CPlatformRuntime; }

namespace platform::native
{

struct SControllers
{
    runtime::CPlatformRuntime* runtime{};
    platform::threading::SAwakeClock clock;
    void* state{}; //  Non-owning adapter context; synthetic adapters may borrow it.
#if MV_PLATFORM_WINDOWS
    TInstance<SGameInput> gameinput;
#endif
    std::uint32_t error{};
    bool work_pending{};
};
struct SControllerAdapter
{
    bool (*initialise)(SControllers&) noexcept{};
    void (*dispatch)(SControllers&, const std::uint64_t) noexcept{};
    EControllerRead (*read)(SControllers&, const std::uint64_t, const bool, SControllerReading&) noexcept{};
    bool (*destroy)(SControllers&) noexcept{};
};

[[nodiscard]] const SControllerAdapter& controller_adapter() noexcept;

}   //  namespace platform::native
