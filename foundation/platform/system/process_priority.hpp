
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   process_priority.hpp
//  Author: Ritchie Brannan
//  Date:   9 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Current-process priority hint wrapper.
//
//  The priority values are process-wide scheduling intent levels, not
//  portable numeric scheduler priorities. The wrapper applies a meaningful
//  native process-priority signal where one is available.
//
//  This wrapper is intentionally separate from thread priority. Raising
//  process priority affects all threads in the process and should be used
//  more cautiously than per-thread priority.
//
//  Unsupported or rejected mappings return false.

#pragma once

#ifndef PROCESS_PRIORITY_HPP_INCLUDED
#define PROCESS_PRIORITY_HPP_INCLUDED

#include <cstdint>      //  std::uint8_t

namespace platform::system
{

//==============================================================================
//  Process priority
//==============================================================================

enum class EProcessPriority : std::uint8_t { Normal = 0u, AboveNormal = 1u, High = 2u };

//  Applies a best-effort priority hint to the current process.
//  Returns true if a native process-priority signal was applied.
//  Returns false if the mapping is unsupported or the native call fails.
bool set_current_process_priority(const EProcessPriority priority) noexcept;

}   //  namespace platform::system

#endif  //  #ifndef PROCESS_PRIORITY_HPP_INCLUDED
