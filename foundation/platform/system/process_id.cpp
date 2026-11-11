
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    process_id.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Native process identifier query implementation.

#include <cstdint>      //  std::uint64_t

#include "platform/system/process_id.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#else
#include <unistd.h>
#endif

namespace platform::system
{

CPlatformProcessId query_current_process_id() noexcept
{
#if MV_PLATFORM_WINDOWS

    return CPlatformProcessId(static_cast<std::uint64_t>(GetCurrentProcessId()));

#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID || MV_PLATFORM_MAC_OS

    const auto id = ::getpid();
    return (id > 0) ? CPlatformProcessId(static_cast<std::uint64_t>(id)) : CPlatformProcessId();

#else

    return CPlatformProcessId();

#error "platform::system::query_current_process_id() is not implemented for this platform."

#endif
}

}   //  namespace platform::system
