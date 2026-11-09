
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    controllers_unsupported.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    09 Nov 26
//
//  Explicit unavailable backend for targets without native controller acquisition.

#include "native/controllers.hpp"

#if !MV_PLATFORM_WINDOWS
namespace platform::native
{
static bool initialise(SControllers&) noexcept { return false; }
static void dispatch(SControllers&, const std::uint64_t) noexcept {}
static EControllerRead read(SControllers&, const std::uint64_t, const bool, SControllerReading&) noexcept
{
    return EControllerRead::empty;
}
static bool destroy(SControllers&) noexcept { return true; }
const SControllerAdapter& controller_adapter() noexcept
{
    static const SControllerAdapter adapter{ &initialise, &dispatch, &read, &destroy };
    return adapter;
}
}   //  namespace platform::native
#endif
