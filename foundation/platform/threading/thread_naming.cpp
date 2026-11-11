
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   thread_naming.cpp
//  Author: Ritchie Brannan
//  Date:   10 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.

#include "platform/threading/thread_naming.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
#include <sys/prctl.h>
#endif

#if MV_PLATFORM_MAC_OS
#include <pthread.h>
#endif

#include <cstddef>  //  std::size_t

namespace platform::threading
{

namespace internal
{

constexpr std::size_t k_thread_name_capacity = 16u;

bool copy_printable_ascii_thread_name(const char* const src, char(&dst)[k_thread_name_capacity]) noexcept
{
    for (std::size_t index = 0u; index < k_thread_name_capacity; ++index)
    {
        dst[index] = '\0';
    }

    if ((src == nullptr) || (src[0] == '\0'))
    {
        return false;
    }

    for (std::size_t index = 0u; src[index] != '\0'; ++index)
    {
        if (index >= (k_thread_name_capacity - 1u))
        {
            return false;
        }

        const char c = src[index];
        if ((c < 0x20) || (c > 0x7e))
        {
            return false;
        }

        dst[index] = c;
    }

    return true;
}

#if MV_PLATFORM_WINDOWS

void widen_ascii_thread_name(const char(&src)[k_thread_name_capacity], wchar_t(&dst)[k_thread_name_capacity]) noexcept
{
    for (std::size_t index = 0u; index < k_thread_name_capacity; ++index)
    {
        const unsigned char c = static_cast<unsigned char>(src[index]);
        dst[index] = static_cast<wchar_t>(c);
    }
}

#endif

}   //  namespace internal

bool set_current_thread_name(const char* const name) noexcept
{
    bool success = false;

    char local_name[internal::k_thread_name_capacity];
    if (internal::copy_printable_ascii_thread_name(name, local_name))
    {
#if MV_PLATFORM_WINDOWS

        wchar_t wide_name[internal::k_thread_name_capacity];
        internal::widen_ascii_thread_name(local_name, wide_name);

        const HRESULT result = ::SetThreadDescription(::GetCurrentThread(), wide_name);
        success = SUCCEEDED(result);

#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

        success = ::prctl(PR_SET_NAME, local_name, 0, 0, 0) == 0;

#elif MV_PLATFORM_MAC_OS

        success = ::pthread_setname_np(local_name) == 0;

#endif
    }

    return success;
}

}   //  namespace platform::threading
