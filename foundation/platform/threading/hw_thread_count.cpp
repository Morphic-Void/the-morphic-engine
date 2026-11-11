
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   hw_thread_count.cpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Logical execution-context count query implementation.

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t

#include "platform/threading/hw_thread_count.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

#if MV_PLATFORM_MAC_OS
#include <sys/sysctl.h>
#endif

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
#include <sched.h>
#include <unistd.h>
#endif

namespace platform::threading
{

std::uint32_t query_hardware_thread_count() noexcept
{
    std::uint32_t count = 0u;

#if MV_PLATFORM_WINDOWS

    DWORD dw_count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

    if (dw_count == 0u)
    {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        dw_count = info.dwNumberOfProcessors;
    }

    if (dw_count != 0u)
    {
        count = static_cast<std::uint32_t>(dw_count);
    }

#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

    cpu_set_t set;
    CPU_ZERO(&set);

    if (sched_getaffinity(0, sizeof(set), &set) == 0)
    {
        std::uint32_t affinity_count = 0u;

        for (int i = 0; i < CPU_SETSIZE; ++i)
        {
            if (CPU_ISSET(i, &set))
            {
                ++affinity_count;
            }
        }

        if (affinity_count != 0u)
        {
            count = affinity_count;
        }
    }

    if (count == 0u)
    {
        const long online_count = sysconf(_SC_NPROCESSORS_ONLN);

        if (online_count > 0)
        {
            count = static_cast<std::uint32_t>(online_count);
        }
    }

#elif MV_PLATFORM_MAC_OS

    static const char* const k_query_names[3] =
    {
        "hw.activecpu",
        "hw.logicalcpu",
        "hw.ncpu"
    };

    for (int i = 0; i < 3; ++i)
    {
        using sysctl_type = std::uint32_t;
        constexpr std::size_t sysctl_size = sizeof(sysctl_type);
        sysctl_type sysctl_value = 0u;
        std::size_t size = sysctl_size;
        if ((sysctlbyname(k_query_names[i], &sysctl_value, &size, nullptr, 0) == 0) &&
            (size == sysctl_size) && (sysctl_value != 0u))
        {
            count = sysctl_value;
            break;
        }
    }

#else

#error "platform::threading::query_hardware_thread_count() is not implemented for this platform."

#endif

    return (count != 0u) ? count : 1u;
}

}   //  namespace platform::threading
