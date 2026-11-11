
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   process_priority.cpp
//  Author: Ritchie Brannan
//  Date:   9 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Current-process priority hint implementation.

#include "platform/system/process_priority.hpp"
#include "platform/platform_defines.hpp"
#include "debug/macros.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

namespace platform::system
{

//==============================================================================
//  Process priority
//==============================================================================

bool set_current_process_priority(const EProcessPriority priority) noexcept
{
#if MV_PLATFORM_WINDOWS

    DWORD native_priority = NORMAL_PRIORITY_CLASS;

    switch (priority)
    {
        case EProcessPriority::Normal:
        {
            native_priority = NORMAL_PRIORITY_CLASS;
            break;
        }
        case EProcessPriority::AboveNormal:
        {
            native_priority = ABOVE_NORMAL_PRIORITY_CLASS;
            break;
        }
        case EProcessPriority::High:
        {
            native_priority = HIGH_PRIORITY_CLASS;
            break;
        }
        default:
        {
            return false;
        }
    }

    const BOOL result = ::SetPriorityClass(::GetCurrentProcess(), native_priority);
    return (result != FALSE);

#else

    (void)priority;
    return false;

#endif
}

}   //  namespace platform::system
