
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   processor_relax.cpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Processor-local spin-wait hint implementation.

#include "platform/threading/processor_relax.hpp"
#include "platform/platform_defines.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

namespace platform::threading
{

void processor_relax() noexcept
{
#if MV_PLATFORM_WINDOWS

    YieldProcessor();

#elif defined(__i386__) || defined(__x86_64__)

#if defined(__clang__) || defined(__GNUC__)
    __asm__ __volatile__("pause");
#endif

#elif defined(__aarch64__) || defined(__arm__)

#if defined(__clang__) || defined(__GNUC__)
    __asm__ __volatile__("yield");
#endif

#endif

    //  No-op fallback.
}

}   //  namespace platform::threading
