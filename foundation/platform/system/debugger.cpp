
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   debugger.cpp
//  Primary implementation: OpenAI tools
//  Reviewed and accepted by: Ritchie Brannan
//  Date:   31 Jul 26
//
//  Current-process debugger interaction and fail-fast termination.

#include <cstdlib>      //  std::abort
#include <cstdio>       //  std::fputs, std::fflush, stderr

#include "platform/platform_defines.hpp"
#include "platform/system/debugger.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#if defined(_MSC_VER)
#include <intrin.h>     //  __debugbreak
#endif
#endif

namespace platform::system
{

bool debugger_attached() noexcept
{
#if MV_PLATFORM_WINDOWS

    return ::IsDebuggerPresent() != FALSE;

#else

    return false;

#endif
}

void write_debugger_output(const char* const text) noexcept
{
    if (text == nullptr)
    {
        return;
    }

#if MV_PLATFORM_WINDOWS

    ::OutputDebugStringA(text);

#else

    (void)std::fputs(text, stderr);
    (void)std::fflush(stderr);

#endif
}

void break_into_debugger() noexcept
{
#if defined(_MSC_VER)

    __debugbreak();

#elif defined(__clang__) || defined(__GNUC__)

    __builtin_trap();

#else

    std::abort();

#endif
}

[[noreturn]] void fail_fast() noexcept
{
#if MV_PLATFORM_WINDOWS

    ::RaiseFailFastException(nullptr, nullptr, 0u);
    (void)::TerminateProcess(::GetCurrentProcess(), 0xc0000409u);

#endif

    std::abort();
}

}   //  namespace platform::system

