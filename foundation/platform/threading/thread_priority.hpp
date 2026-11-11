
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   thread_priority.hpp
//  Author: Ritchie Brannan
//  Date:   9 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Current-thread priority hint wrapper.
//
//  The priority values are engine intent levels, not portable numeric
//  scheduler priorities. The wrapper applies a meaningful native priority
//  or QoS signal where one is available.
//
//  Unsupported or rejected mappings return false.

#pragma once

#ifndef THREAD_PRIORITY_HPP_INCLUDED
#define THREAD_PRIORITY_HPP_INCLUDED

#include <cstdint>      //  std::uint8_t

namespace platform::threading
{

//==============================================================================
//  Thread priority
//==============================================================================

enum class EThreadPriority : std::uint8_t { Background = 0u, Low = 1u, Normal = 2u, High = 3u, Realtime = 4u };

//  Applies a best-effort priority hint to the calling native thread.
//  Returns true if a native priority / QoS signal was applied.
//  Returns false if the mapping is unsupported or the native call fails.
bool set_current_thread_priority(const EThreadPriority priority) noexcept;

}   //  namespace platform::threading

#endif  //  #ifndef THREAD_PRIORITY_HPP_INCLUDED
