
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   hw_thread_count.hpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Logical execution-context count query.
//
//  Returns a non-zero count hint suitable for startup sizing and coarse
//  thread-system policy decisions.
//
//  The result is not a physical core count, topology description, scheduling
//  guarantee, or dynamic topology monitor. Platforms may reflect process
//  availability where that is cheap and mechanical.
//
//  Failure or unavailable platform support returns 1.

#pragma once

#ifndef HW_THREAD_COUNT_HPP_INCLUDED
#define HW_THREAD_COUNT_HPP_INCLUDED

#include <cstdint>  //  std::uint32_t

namespace platform::threading
{

std::uint32_t query_hardware_thread_count() noexcept;

}   //  namespace platform::threading

#endif  //  #ifndef HW_THREAD_COUNT_HPP_INCLUDED
