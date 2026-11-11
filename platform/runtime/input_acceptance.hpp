
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_acceptance.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Nov 26
//
//  Private compile-time acceptance hooks; ordinary Platform builds expose none.

#pragma once

#if defined(MV_INPUT_ACCEPTANCE)
#include "framework/module/host_pump.hpp"
#include "native/window.hpp"
#include "native/controllers.hpp"

namespace platform::runtime
{
class CPlatformRuntime;
class CInputConnection;
struct SDesktopTransaction;
}
namespace platform::acceptance
{
inline constexpr services::SServiceContract k_contract{ 0x7ffeu, 1u };
[[nodiscard]] bool initialise(const modules::SHostPumpContext& context) noexcept;
void destroy() noexcept;
[[nodiscard]] const native::SWindowAdapter& window_adapter() noexcept;
[[nodiscard]] const native::SControllerAdapter& controller_adapter() noexcept;
[[nodiscard]] bool command(runtime::CPlatformRuntime& runtime, const services::SDispatchedCommand& command) noexcept;
void message(const ::threading::CErasedPodMsg& message) noexcept;
void progress(runtime::CPlatformRuntime& runtime, const bool stopping) noexcept;
[[nodiscard]] bool retired() noexcept;
void pump_begin(runtime::CPlatformRuntime& runtime) noexcept;
void pump_end() noexcept;
void desktop_ingress(runtime::SDesktopTransaction& transaction) noexcept;
void controller_ingress(native::SControllerReading& reading) noexcept;
void publication(const runtime::CInputConnection& connection, const std::uint32_t stream,
    const std::uint64_t generation, const std::uint64_t sequence, const bool boundary) noexcept;
}
#endif
