
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    window_unsupported.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Nov 26
//
//  Supplies the portable adapter on systems without a native window backend,
//  reporting unsupported creation without allocating native resources.

#include "platform/platform_defines.hpp"
#if !MV_PLATFORM_WINDOWS
#include "native/window.hpp"

namespace platform::native
{

static services::EServiceStatus create(SWindow&) noexcept { return services::EServiceStatus::unsupported; }
static bool dispatch(SWindow&, const std::uint64_t) noexcept { return true; }
static bool destroy(SWindow& window) noexcept { return !window.live(); }
const SWindowAdapter& window_adapter() noexcept
{
    static const SWindowAdapter adapter{ &create, &dispatch, &destroy };
    return adapter;
}

}   //  namespace platform::native
#endif
