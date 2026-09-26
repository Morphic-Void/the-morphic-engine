
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   thread_priority.cpp
//  Author: Ritchie Brannan
//  Date:   9 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Current-thread priority hint implementation.

#include "platform/threading/thread_priority.hpp"
#include "platform/platform_defines.hpp"
#include "debug/macros.hpp"

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#endif

#if MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID
#include <pthread.h>
#include <sched.h>
#endif

#if MV_PLATFORM_MAC_OS
#include <pthread.h>
#endif

namespace platform::threading
{

//==============================================================================
//  Thread priority
//==============================================================================

bool set_current_thread_priority(const EThreadPriority priority) noexcept
{
#if MV_PLATFORM_WINDOWS

    int native_priority = THREAD_PRIORITY_NORMAL;

    switch (priority)
    {
        case EThreadPriority::Background:
        {
            native_priority = THREAD_PRIORITY_LOWEST;
            break;
        }
        case EThreadPriority::Low:
        {
            native_priority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        }
        case EThreadPriority::Normal:
        {
            native_priority = THREAD_PRIORITY_NORMAL;
            break;
        }
        case EThreadPriority::High:
        {
            native_priority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        }
        case EThreadPriority::Realtime:
        {
            native_priority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
        }
        default:
        {
            return false;
        }
    }

    const BOOL result = ::SetThreadPriority(::GetCurrentThread(), native_priority);
    return (result != FALSE);

#elif MV_PLATFORM_LINUX || MV_PLATFORM_ANDROID

    if (priority == EThreadPriority::Normal)
    {
        sched_param param;
        param.sched_priority = 0;

        const int result = ::pthread_setschedparam(
            ::pthread_self(), SCHED_OTHER, &param);

        return (result == 0);
    }

    if (priority == EThreadPriority::Realtime)
    {
        const int min_priority = ::sched_get_priority_min(SCHED_FIFO);

        if (min_priority < 0)
        {
            return false;
        }

        sched_param param;
        param.sched_priority = min_priority;

        const int result = ::pthread_setschedparam(
            ::pthread_self(), SCHED_FIFO, &param);

        return (result == 0);
    }

    (void)priority;
    return false;

#elif MV_PLATFORM_MAC_OS

    qos_class_t native_qos = QOS_CLASS_DEFAULT;

    switch (priority)
    {
        case EThreadPriority::Background:
        {
            native_qos = QOS_CLASS_BACKGROUND;
            break;
        }
        case EThreadPriority::Low:
        {
            native_qos = QOS_CLASS_UTILITY;
            break;
        }
        case EThreadPriority::Normal:
        {
            native_qos = QOS_CLASS_DEFAULT;
            break;
        }
        case EThreadPriority::High:
        {
            native_qos = QOS_CLASS_USER_INITIATED;
            break;
        }
        case EThreadPriority::Realtime:
        {
            native_qos = QOS_CLASS_USER_INTERACTIVE;
            break;
        }
        default:
        {
            return false;
        }
    }

    const int result = ::pthread_set_qos_class_self_np(native_qos, 0);
    return (result == 0);

#else

    (void)priority;
    return false;

#endif
}

}   //  namespace platform::threading
