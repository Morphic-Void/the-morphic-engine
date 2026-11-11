
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   native_thread_id.cpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Native thread identifier query implementation.

#include <cstdint>      //  std::uint64_t

#include "platform/threading/native_thread_id.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if MV_PLATFORM_MAC_OS
#include <pthread.h>
#endif

namespace platform::threading
{

CPlatformThreadId query_current_thread_id() noexcept
{
#if MV_PLATFORM_WINDOWS

    return CPlatformThreadId(static_cast<std::uint64_t>(GetCurrentThreadId()));

#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

    const long id = ::syscall(SYS_gettid);

    return (id > 0)
        ? CPlatformThreadId(static_cast<std::uint64_t>(id))
        : CPlatformThreadId();

#elif MV_PLATFORM_MAC_OS

    std::uint64_t id = 0u;
    const int result = pthread_threadid_np(nullptr, &id);

    return ((result == 0) && (id != 0u))
        ? CPlatformThreadId(id)
        : CPlatformThreadId();

#else

    return CPlatformThreadId();

#error "platform::threading::query_current_thread_id() is not implemented for this platform."

#endif
}

}   //  namespace platform::threading
