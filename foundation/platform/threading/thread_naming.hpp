
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   thread_naming.hpp
//  Author: Ritchie Brannan
//  Date:   10 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//  - No allocation.
//
//  Best-effort current-thread diagnostic naming.
//
//  The thread name is debug/profiler metadata only. It is not a thread
//  identity, is not required, and may be ignored by the platform.
//
//  Thread names are limited to 15 printable ASCII characters plus a
//  null terminator. Longer and non-ASCII names are rejected. The limit
//  is chosen to match the common Linux/Android diagnostic thread-name
//  limit and to keep behaviour consistent across platforms.
//
//  Null and empty names are rejected.

#pragma once

#ifndef THREAD_NAMING_HPP_INCLUDED
#define THREAD_NAMING_HPP_INCLUDED

namespace platform::threading
{

bool set_current_thread_name(const char* const name) noexcept;

}   //  namespace platform::threading

#endif // THREAD_NAMING_HPP_INCLUDED
